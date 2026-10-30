/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "fwboot.h"

#include <string.h>

static void load(fwboot_t *b)
{
    fwlog_load(&b->log, b->fl, b->lay);
    b->pol.floor = b->log.floor;
}

/* The staging image is verified; copy it and check the result. */
static fw_err_t copy_and_check(fwboot_t *b, const fw_header_t *staged)
{
    fw_err_t e = fw_install(b->work, b->fl, b->lay);
    if (e != FW_OK) return e;
    fw_header_t h;
    fw_policy_t pol = b->pol;
    pol.allow_unsigned = false;
    e = fw_verify_app(b->work, b->fl, b->lay, &pol, &h);
    if (e == FW_OK && h.version != staged->version) e = FW_E_FLASH;
    if (e != FW_OK) return e;
    if (!fwlog_append(&b->log, b->fl, b->lay, FWLOG_DONE, staged->version)) return FW_E_FLASH;
    b->pol.floor = b->log.floor;
    return FW_OK;
}

fw_err_t fwboot_install_staged(fwboot_t *b, fw_header_t *out)
{
    load(b);
    fw_err_t e = fw_verify_staging(b->work, b->fl, b->lay, &b->pol, out);
    if (e != FW_OK) return e;
    if (!fwlog_append(&b->log, b->fl, b->lay, FWLOG_INSTALL, out->version)) return FW_E_FLASH;
    return copy_and_check(b, out);
}

fwboot_action_t fwboot_reset(fwboot_t *b, bool update_requested, fwboot_report_t *rep)
{
    memset(rep, 0, sizeof(*rep));
    rep->install = FW_OK;
    load(b);
    fw_header_t staged;

    if (b->log.pending) {
        rep->installed = true;
        fw_err_t e = fw_verify_staging(b->work, b->fl, b->lay, &b->pol, &staged);
        if (e == FW_OK && staged.version != b->log.pending_version) e = FW_E_FORMAT;
        if (e == FW_OK) {
            e = copy_and_check(b, &staged);
        } else {
            /* Nothing in staging matches what was being installed (cannot
             * happen short of a damaged slot): stop trying. */
            fwlog_append(&b->log, b->fl, b->lay, FWLOG_CANCEL, b->log.pending_version);
        }
        rep->install = e;
    }

    rep->app = fw_verify_app(b->work, b->fl, b->lay, &b->pol, &rep->app_hdr);
    if (rep->app == FW_OK) {
        /* A signed application installed over SWD (the factory image) was
         * never journalled: nothing older than it may come in either. */
        if (!(rep->app_hdr.flags & FW_FLAG_UNSIGNED) && rep->app_hdr.version > b->log.floor &&
            fwlog_append(&b->log, b->fl, b->lay, FWLOG_FLOOR, rep->app_hdr.version))
            b->pol.floor = b->log.floor;
        return update_requested ? FWBOOT_WAIT_UPDATE : FWBOOT_RUN_APP;
    }

    /* No bootable application: is there a good image in staging? */
    if (!rep->installed) {
        rep->installed = true;
        rep->install = fwboot_install_staged(b, &staged);
        if (rep->install == FW_OK) {
            rep->app = fw_verify_app(b->work, b->fl, b->lay, &b->pol, &rep->app_hdr);
            if (rep->app == FW_OK) return FWBOOT_RUN_APP;
        }
    }
    return FWBOOT_WAIT_UPDATE;
}
