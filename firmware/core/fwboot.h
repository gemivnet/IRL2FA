/* What the bootloader does at reset, and after an image has been received.
 * Portable (the host tests run it against a virtual flash cut at every
 * operation); port/stm32u0/boot supplies the flash, the IR link and the
 * jump. See docs/firmware-updates.md.
 *
 * At reset:
 *   1. an install was under way (INSTALL without DONE): check the staging
 *      image again and finish the copy (fw_install is idempotent);
 *   2. the application slot verifies: run it, unless the application asked
 *      for the update receiver;
 *   3. it does not, but the staging slot holds a valid image not older than
 *      the floor: install that (a copy cut short, or a damaged slot);
 *   4. otherwise wait for an update over IR.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_FWBOOT_H
#define IRL2FA_FWBOOT_H

#include <stdbool.h>
#include <stdint.h>

#include "fwimage.h"
#include "fwlog.h"

typedef struct {
    const fw_flash_t *fl;
    const fw_layout_t *lay;
    fw_policy_t pol;  /* pol.floor is taken from the journal */
    fw_work_t *work;
    fwlog_t log;
} fwboot_t;

typedef enum {
    FWBOOT_RUN_APP = 0,
    FWBOOT_WAIT_UPDATE = 1,
} fwboot_action_t;

typedef struct {
    fw_err_t app;         /* verification of the application slot */
    fw_header_t app_hdr;  /* valid when app == FW_OK */
    fw_err_t install;     /* FW_OK if an install ran and finished, else why not */
    bool installed;       /* an install ran at this reset */
} fwboot_report_t;

fwboot_action_t fwboot_reset(fwboot_t *b, bool update_requested, fwboot_report_t *rep);

/* After the IR receiver has filled the staging slot: verify it, journal the
 * intent, copy, verify the result, journal completion. FW_OK when the new
 * firmware is in place. On a verification failure nothing was changed. */
fw_err_t fwboot_install_staged(fwboot_t *b, fw_header_t *out);

#endif
