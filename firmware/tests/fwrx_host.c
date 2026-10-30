/* A device's update receiver on the host, for testing tools/irl2fa_update.py:
 * the bootloader's core (fwlink receiver, verify, install) on a virtual
 * flash, trusting the test key, speaking the IR protocol on stdin/stdout.
 *
 *     fwrx_host [FLOOR [CUT_AFTER_BYTES]]
 *
 * Exits when stdin closes: 0 if an image was installed (its version on
 * stderr), 1 otherwise. CUT_AFTER_BYTES drops the link (exit 3) once that
 * many bytes have arrived, to test a resumed transfer.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../core/fwboot.h"
#include "../core/fwlink.h"
#include "data/fw_fixtures.h"
#include "vflash.h"

static const fw_layout_t LAY = {0x6800, 0x18000, 0x1E800, 0xC800, 0x2B000};
static vflash_t F;
static fw_work_t W;

int main(int argc, char **argv)
{
    vflash_init(&F);
    fw_flash_t fl = vflash_iface(&F);
    /* Staging survives between runs (a resumed transfer): keep it in a file. */
    const char *keep = getenv("FWRX_STAGING");
    if (keep) {
        FILE *f = fopen(keep, "rb");
        if (f) {
            size_t n = fread(F.mem + LAY.staging_addr, 1, LAY.staging_size, f);
            (void)n;
            fclose(f);
        }
    }
    fwboot_t fb = {0};
    fb.fl = &fl;
    fb.lay = &LAY;
    fb.pol.hw_id = 0x00830001u;
    fb.pol.boot_version = 1;
    fb.pol.floor = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 0) : 0;
    fb.pol.pubkey = FIX_TEST_PUB;
    fb.work = &W;
    long cut = argc > 2 ? strtol(argv[2], NULL, 0) : -1;
    static fwrx_t rx;
    fwrx_init(&rx, &fl, &LAY, &fb.pol);
    uint8_t buf[512], f[FWL_FRAME_MAX];
    long got = 0;
    int rc = 1;
    for (;;) {
        ssize_t n = read(0, buf, sizeof(buf));
        if (n <= 0) break;
        got += n;
        fwrx_rx(&rx, buf, (size_t)n);
        if (cut >= 0 && got >= cut) {
            rc = 3;
            break;
        }
        if (rx.state == FWRX_COMPLETE) {
            fw_header_t h;
            fw_err_t e = fwboot_install_staged(&fb, &h);
            fwrx_finish(&rx, e);
            rx.reply = true;
            if (e == FW_OK) {
                fprintf(stderr, "fwrx_host: installed version 0x%06x\n", (unsigned)h.version);
                rc = 0;
            } else {
                fprintf(stderr, "fwrx_host: refused, error %d\n", e);
            }
        }
        if (rx.reply) {
            size_t m = fwrx_status(&rx, f, sizeof(f));
            if (write(1, f, m) != (ssize_t)m) break;
        }
    }
    if (keep) {
        FILE *o = fopen(keep, "wb");
        if (o) {
            fwrite(F.mem + LAY.staging_addr, 1, LAY.staging_size, o);
            fclose(o);
        }
    }
    return rc;
}
