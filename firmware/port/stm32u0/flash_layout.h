/* The STM32U083KC's flash (256 KB, 128 pages of 2 KB) and the RAM mailbox
 * between the bootloader and the application. Offsets from FLASH_BASE.
 * flash_layout.ld says the same for the linker, tools/sign_image.py for the
 * image tools; tools/check_hw_constants.py checks all three agree.
 *
 *   0x00000  bootloader          26 KB  (pages   0-12)   boot/, written over SWD only
 *   0x06800  application slot    96 KB  (pages  13-60)   header + padding to 0x100, then the binary
 *   0x1E800  staging slot        50 KB  (pages  61-85)   an update image as received (LZMA)
 *   0x2B000  bootloader journal   4 KB  (pages  86-87)   core/fwlog.c
 *   0x2C000  record store        80 KB  (pages  88-127)  core/store.c: never touched by updates
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_FLASH_LAYOUT_H
#define IRL2FA_FLASH_LAYOUT_H

#define FL_FLASH_SIZE 0x40000u
#define FL_PAGE 2048u
#define FL_BOOT_OFF 0x00000u
#define FL_BOOT_SIZE 0x06800u
#define FL_APP_OFF 0x06800u
#define FL_APP_SIZE 0x18000u
#define FL_STAGING_OFF 0x1E800u
#define FL_STAGING_SIZE 0x0C800u
#define FL_LOG_OFF 0x2B000u
#define FL_LOG_SIZE 0x01000u
#define FL_STORE_OFF 0x2C000u
#define FL_STORE_SIZE 0x14000u

/* What the images say they are built for, and what this bootloader is
 * (core/fwimage.h min_boot). */
#define FL_HW_ID 0x00830001u /* STM32U083KC on the IRL2FA board */
#define FL_BOOT_VERSION 1u

/* The last 16 bytes of SRAM, outside both programs' .data/.bss/stack, so
 * they survive a system reset (not a power-on: SRAM is then random, which
 * the check word catches). */
#define FL_MAILBOX_ADDR 0x20009FF0u
/* The 32 bytes below: the bootloader's debug block (boot/boot_main.c), where
 * the application leaves it alone so the tests can read it afterwards. */
#define FL_BOOT_DEBUG_ADDR 0x20009FD0u
/* The 64 bytes below that: the application's debug block (main.c). */
#define FL_APP_DEBUG_ADDR 0x20009F90u

#if !defined(__ASSEMBLER__) && !defined(FL_LINKER)
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint32_t cmd;
    uint32_t arg;
    uint32_t check; /* ~(magic ^ cmd ^ arg) */
} boot_mailbox_t;

#define BOOT_MAILBOX_MAGIC 0x49324D42u /* "BM2I" */
enum {
    BOOT_CMD_UPDATE = 1, /* application -> bootloader: run the IR update receiver */
    BOOT_CMD_RESULT = 2, /* bootloader -> application: arg = fw_err_t | version << 8 */
    BOOT_CMD_LOCK = 3,   /* application -> bootloader: its owner woke it; set RDP level 1 */
};

#define BOOT_MAILBOX ((volatile boot_mailbox_t *)FL_MAILBOX_ADDR)

static inline void boot_mailbox_put(uint32_t cmd, uint32_t arg)
{
    BOOT_MAILBOX->magic = BOOT_MAILBOX_MAGIC;
    BOOT_MAILBOX->cmd = cmd;
    BOOT_MAILBOX->arg = arg;
    BOOT_MAILBOX->check = ~(BOOT_MAILBOX_MAGIC ^ cmd ^ arg);
}

/* Takes the message (once): its command, 0 if none. */
static inline uint32_t boot_mailbox_take(uint32_t *arg)
{
    volatile boot_mailbox_t *m = BOOT_MAILBOX;
    uint32_t cmd = m->cmd, a = m->arg;
    bool ok = m->magic == BOOT_MAILBOX_MAGIC && m->check == ~(BOOT_MAILBOX_MAGIC ^ cmd ^ a);
    m->magic = 0;
    m->check = 0;
    *arg = a;
    return ok ? cmd : 0;
}
#endif

#endif
