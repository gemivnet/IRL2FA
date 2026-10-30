/* The IRL2FA bootloader: checks the application at every reset, finishes
 * an interrupted install, receives signed updates over IR, and turns on
 * readout protection once, on a healthy cell. The decisions are in
 * core/fwboot.c (host-tested); this is the board around them. See
 * docs/firmware-updates.md.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include <string.h>

#include "board.h"
#include "boot_pubkey.h"
#include "flash_layout.h"
#include "fwboot.h"
#include "fwlink.h"
#include "hal.h"

/* The release public key (firmware/keys/irl2fa-release.pub, or the one a
 * fork builds with: IRL2FA_BOOT_PUBKEY). All zero: a build without a key,
 * which accepts no update. The Renode tests overwrite it in emulated flash
 * with the test key, so they run this binary otherwise unchanged. */
__attribute__((used)) const uint8_t irl2fa_boot_pubkey[32] = IRL2FA_BOOT_PUBKEY;

/* Readout protection is set only above this (RM0503 3.4.2: an option-byte
 * write cut by a power loss can lock the part for good; a CR2450 under the
 * programming load must not brown out). Without ST's VREFINT calibration
 * (ES0602 2.1.2) the reading may be 1.7 % high: ask for that much more. */
#define RDP_MIN_MV 2900u
#define RDP_MIN_MV_UNCAL 2960u
/* Below this an install is not started (it would likely be cut). */
#define INSTALL_MIN_MV 2300u
#define RX_IDLE_MS 180000u   /* receiver gives up after 3 minutes of silence */
#define DONE_LINGER_MS 6000u /* keep answering the sender after the result */

#ifndef IRL2FA_DEBUG_HOOK
#define IRL2FA_DEBUG_HOOK 1
#endif

#if IRL2FA_DEBUG_HOOK
/* Read by the Renode tests at FL_BOOT_DEBUG_ADDR (kept while the
 * application runs); nothing secret. */
struct irl2fa_boot_debug {
    uint32_t magic; /* "BDBG" */
    uint32_t phase; /* 1 checking, 2 receiving, 3 installing, 4 starting the app, 5 locking */
    uint32_t app_err, install_err;
    uint32_t rx_state, rx_next;
    uint32_t rdp; /* 0 not considered, 1 locked now, 2 cell too low, 3 already, 4 unsigned app */
    uint32_t battery_mv;
};
#define BOOT_DEBUG ((volatile struct irl2fa_boot_debug *)FL_BOOT_DEBUG_ADDR)
_Static_assert(sizeof(struct irl2fa_boot_debug) == 32, "flash_layout.h FL_BOOT_DEBUG_ADDR: 32 bytes");
#define DBG(field, v) (BOOT_DEBUG->field = (uint32_t)(v))
#else
#define DBG(field, v) ((void)0)
#endif

/* The bootloader only ever writes the slots and its journal: never itself,
 * never the record store. */
static bool writable(uint32_t off)
{
    return off >= FL_APP_OFF && off < FL_STORE_OFF;
}

static bool fl_read(void *ctx, uint32_t addr, void *buf, size_t len)
{
    (void)ctx;
    return board_flash_read(addr, buf, len);
}

static bool fl_erase(void *ctx, uint32_t addr)
{
    (void)ctx;
    return writable(addr) && board_flash_erase(addr);
}

static bool fl_program(void *ctx, uint32_t addr, const void *buf, size_t len)
{
    (void)ctx;
    return writable(addr) && writable(addr + len - 1u) && board_flash_program(addr, buf, len);
}

static const fw_flash_t FLASH_OPS = {fl_read, fl_erase, fl_program, NULL, FL_PAGE};
static const fw_layout_t LAYOUT = {FL_APP_OFF, FL_APP_SIZE, FL_STAGING_OFF, FL_STAGING_SIZE,
                                   FL_LOG_OFF};
static fw_work_t work;
static fwboot_t fb;
static fwrx_t rx;

static bool has_key(void)
{
    uint8_t any = 0;
    for (int i = 0; i < 32; i++) any |= irl2fa_boot_pubkey[i];
    return any != 0;
}

static bool rdp_level0(void)
{
    return (FLASH->OPTR & FLASH_OPTR_RDP) == 0xAAu;
}

/* RM0503 3.4.2: unlock the flash and the option bytes, set RDP to level 1
 * (any value but 0xAA and 0xCC), start, wait, then reload the option bytes,
 * which resets the chip. */
static void lock_chip(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123u;
        FLASH->KEYR = 0xCDEF89ABu;
    }
    if (FLASH->CR & FLASH_CR_OPTLOCK) {
        FLASH->OPTKEYR = 0x08192A3Bu;
        FLASH->OPTKEYR = 0x4C5D6E7Fu;
    }
    while (FLASH->SR & (FLASH_SR_BSY1 | FLASH_SR_CFGBSY)) {
    }
    FLASH->OPTR = (FLASH->OPTR & ~FLASH_OPTR_RDP) | 0xBBu;
    FLASH->CR |= FLASH_CR_OPTSTRT;
    while (FLASH->SR & (FLASH_SR_BSY1 | FLASH_SR_CFGBSY)) {
    }
    FLASH->CR |= FLASH_CR_OBL_LAUNCH; /* resets */
    for (;;) NVIC_SystemReset();
}

/* At a power-on (a cell put in), with a signed application, on a healthy
 * cell: once, and never at the factory's programming resets. */
static void maybe_lock(const fw_header_t *app, bool power_on)
{
    if (!rdp_level0()) {
        DBG(rdp, 3);
        return;
    }
    if (app->flags & FW_FLAG_UNSIGNED) {
        DBG(rdp, 4);
        return;
    }
    if (!power_on) return;
    bool cal;
    uint16_t a = board_battery_mv(&cal);
    board_delay_ms(20);
    uint16_t b = board_battery_mv(&cal);
    uint16_t mv = a < b ? a : b;
    DBG(battery_mv, mv);
    if (mv < (cal ? RDP_MIN_MV : RDP_MIN_MV_UNCAL)) {
        DBG(rdp, 2);
        return;
    }
    DBG(rdp, 1);
    DBG(phase, 5);
    lock_chip();
}

static void start_app(void)
{
    DBG(phase, 4);
    const uint32_t *vt = (const uint32_t *)(FLASH_BASE + FL_APP_OFF + FW_APP_OFFSET);
    hal_ir_enable(false);
    __disable_irq();
    SysTick->CTRL = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    NVIC->ICER[0] = 0xFFFFFFFFu;
    NVIC->ICPR[0] = 0xFFFFFFFFu;
    SCB->VTOR = (uint32_t)vt;
    __set_MSP(vt[0]);
    __enable_irq();
    ((void (*)(void))vt[1])();
    for (;;) NVIC_SystemReset();
}

static void report_and_restart(fw_err_t e, uint32_t version)
{
    boot_mailbox_put(BOOT_CMD_RESULT, (uint32_t)e | (version << 8));
    hal_ir_enable(false);
    NVIC_SystemReset();
}

/* The IR receiver. Leaves by a restart: into the new firmware, or back to
 * the old one on a refusal, a cancel (joystick left) or a timeout. With no
 * application to go back to it sleeps in Stop 2 instead and listens again
 * when the joystick is pressed. */
static void receive(bool have_app)
{
    DBG(phase, 2);
    fwrx_init(&rx, &FLASH_OPS, &LAYOUT, &fb.pol);
    hal_ir_enable(true);
    uint32_t last = board_millis(), done_at = 0;
    bool done = false;
    fw_err_t result = FW_OK;
    uint32_t version = 0;
    uint8_t buf[64], frame[FWL_FRAME_MAX];
    for (;;) {
        size_t n = board_ir_take(buf, sizeof(buf));
        uint32_t now = board_millis();
        if (n && fwrx_rx(&rx, buf, n)) last = now;
        if (rx.reply) hal_ir_send(frame, fwrx_status(&rx, frame, sizeof(frame)));
        DBG(rx_state, rx.state);
        DBG(rx_next, rx.next);
        if (rx.state == FWRX_COMPLETE && !done) {
            board_delay_ms(60); /* let the COMPLETE status leave the LED */
            DBG(phase, 3);
            bool cal;
            uint16_t mv = board_battery_mv(&cal);
            fw_header_t h;
            memset(&h, 0, sizeof(h));
            result = mv && mv < INSTALL_MIN_MV ? FW_E_BATTERY : fwboot_install_staged(&fb, &h);
            version = h.version;
            DBG(install_err, result);
            fwrx_finish(&rx, result);
            rx.reply = true; /* tell the sender at once */
            done = true;
            done_at = board_millis();
            DBG(phase, 2);
        } else if (rx.state == FWRX_FAILED && !done) {
            /* Refused at the offer (older, other hardware, ...): say so to
             * the sender for a while, then go back. */
            result = (fw_err_t)rx.err;
            done = true;
            done_at = now;
        }
        if (done && now - done_at > DONE_LINGER_MS) report_and_restart(result, version);
        if (!done && (hal_buttons() & BTN_LEFT) && have_app) report_and_restart(FW_E_CANCELLED, 0);
        if (!done && now - last > RX_IDLE_MS) {
            if (have_app) report_and_restart(rx.state == FWRX_FAILED ? result : FW_E_TIMEOUT, 0);
            hal_sleep(0); /* nothing to go back to: wait for the joystick */
            hal_ir_enable(true);
            last = board_millis();
        }
        board_idle_until(board_millis() + 20u);
    }
}

int main(void)
{
    uint32_t arg;
    uint32_t cmd = boot_mailbox_take(&arg);
    if (cmd == BOOT_CMD_RESULT) boot_mailbox_put(cmd, arg); /* for the application */
    bool power_on = (RCC->CSR & RCC_CSR_PWRRSTF) != 0;
    RCC->CSR |= RCC_CSR_RMVF;
#if IRL2FA_DEBUG_HOOK
    memset((void *)BOOT_DEBUG, 0, sizeof(*BOOT_DEBUG));
    BOOT_DEBUG->magic = 0x47424442u;
#endif
    DBG(phase, 1);
    board_init_boot();

    fb.fl = &FLASH_OPS;
    fb.lay = &LAYOUT;
    fb.work = &work;
    fb.pol.hw_id = FL_HW_ID;
    fb.pol.boot_version = FL_BOOT_VERSION;
    fb.pol.pubkey = has_key() ? irl2fa_boot_pubkey : NULL;
    fb.pol.allow_unsigned = rdp_level0();
    fwboot_report_t rep;
    fwboot_action_t act = fwboot_reset(&fb, cmd == BOOT_CMD_UPDATE, &rep);
    DBG(app_err, rep.app);
    DBG(install_err, rep.install);
    if (rep.installed && rep.install == FW_OK && rep.app == FW_OK)
        boot_mailbox_put(BOOT_CMD_RESULT, (uint32_t)FW_OK | (rep.app_hdr.version << 8));
    if (act == FWBOOT_RUN_APP) {
        maybe_lock(&rep.app_hdr, power_on);
        start_app();
    }
    receive(rep.app == FW_OK);
    return 0;
}
