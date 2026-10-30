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

/* The bootloader's message from before this start, read once. */
static uint32_t mailbox_cmd, mailbox_arg;

static void mailbox_read(void)
{
    static bool taken;
    if (taken) return;
    taken = true;
    mailbox_cmd = boot_mailbox_take(&mailbox_arg);
}

int hal_update_result(uint32_t *version)
{
    mailbox_read();
    *version = mailbox_arg >> 8;
    if (mailbox_cmd != BOOT_CMD_RESULT) return -1;
    mailbox_cmd = 0;
    return (int)(mailbox_arg & 0xFFu);
}

/* Readout protection (docs/firmware-updates.md#locking-the-debug-port):
 * never automatic. Settings -> Lock debug port asks the bootloader, which
 * sets RDP level 1 only for a signed image on a healthy cell. */
int hal_debug_lock_state(void)
{
    return (FLASH->OPTR & FLASH_OPTR_RDP) == 0xAAu ? 0 : 1;
}

void hal_debug_lock(void)
{
    boot_mailbox_put(BOOT_CMD_LOCK, 0);
    NVIC_SystemReset();
}

int hal_debug_lock_result(void)
{
    mailbox_read();
    if (mailbox_cmd != BOOT_CMD_LOCK_RESULT) return -1;
    mailbox_cmd = 0;
    return (int)mailbox_arg;
}
