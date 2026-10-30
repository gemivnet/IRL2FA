/* The application's side of firmware updates (core/hal.h): its own version,
 * the way into the bootloader's receiver, its own update image for sending
 * to another device, and the bootloader's report.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include <string.h>

#include "board.h"
#include "flash_layout.h"
#include "fwimage.h"
#include "hal.h"

static fw_header_t running;
static bool running_ok;

static const fw_header_t *own_header(void)
{
    if (!running_ok) {
        uint8_t raw[FW_HDR_SIZE];
        running_ok = board_flash_read(FL_APP_OFF, raw, sizeof(raw)) &&
                     fw_header_parse(raw, &running) == FW_OK;
    }
    return running_ok ? &running : NULL;
}

uint32_t hal_fw_version(void)
{
    const fw_header_t *h = own_header();
    return h ? h->version : 0;
}

void hal_update_enter(void)
{
    boot_mailbox_put(BOOT_CMD_UPDATE, 0);
    NVIC_SystemReset();
}

/* The staging slot holds the image we run when its header is the one in
 * our own slot (the bootloader copies it verbatim): then its payload is
 * what another device needs. The receiver checks the signature anyway. */
bool hal_update_image(uint8_t hdr[128])
{
    uint8_t mine[FW_HDR_SIZE];
    fw_header_t h;
    if (!board_flash_read(FL_STAGING_OFF, hdr, FW_HDR_SIZE) ||
        !board_flash_read(FL_APP_OFF, mine, sizeof(mine)) || memcmp(hdr, mine, FW_HDR_SIZE) != 0 ||
        fw_header_parse(hdr, &h) != FW_OK || (h.flags & FW_FLAG_UNSIGNED) ||
        h.payload_size > FL_STAGING_SIZE - FW_HDR_SIZE)
        return false;
    return true;
}

bool hal_update_read(uint32_t off, uint8_t *buf, size_t len)
{
    if (off > FL_STAGING_SIZE - FW_HDR_SIZE || len > FL_STAGING_SIZE - FW_HDR_SIZE - off) return false;
    return board_flash_read(FL_STAGING_OFF + FW_HDR_SIZE + off, buf, len);
}

/* Readout protection is the bootloader's to set (docs/firmware-updates.md):
 * the first time the owner wakes the device with the joystick after a start,
 * if the chip is unprotected and runs a signed image, ask for it. A
 * programming fixture only powers the board and never gets here. The
 * bootloader declines on a weak cell; then the next start asks again. */
void board_request_lock(void)
{
    static bool asked;
    if (asked) return;
    asked = true;
    const fw_header_t *h = own_header();
    if ((FLASH->OPTR & FLASH_OPTR_RDP) != 0xAAu || !h || (h->flags & FW_FLAG_UNSIGNED)) return;
    boot_mailbox_put(BOOT_CMD_LOCK, 0);
    NVIC_SystemReset();
}

int hal_update_result(uint32_t *version)
{
    static bool taken;
    static int result = -1;
    static uint32_t ver;
    if (!taken) {
        taken = true;
        uint32_t arg;
        if (boot_mailbox_take(&arg) == BOOT_CMD_RESULT) {
            result = (int)(arg & 0xFFu);
            ver = arg >> 8;
        }
    }
    *version = ver;
    int r = result;
    result = -1;
    return r;
}
