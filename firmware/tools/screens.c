/* Drives the app on the virtual board and writes a PBM of each screen, for
 * docs/images/screens (converted to PNG by tools/screens.py).
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdio.h>
#include <string.h>

#include "../core/app.h"
#include "../core/hal.h"
#include "../core/pairing.h"
#include "../core/store.h"
#include "../vhal/vhal.h"

static const char *outdir = ".";

static void dump(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.pbm", outdir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P4\n%d %d\n", DISPLAY_W, DISPLAY_H);
    fwrite(vhal_framebuffer(), 1, DISPLAY_BYTES, f);
    fclose(f);
}

static void step(uint8_t b)
{
    vhal_set_buttons(b);
    app_poll();
    vhal_advance(30);
    vhal_set_buttons(0);
    app_poll();
    vhal_advance(30);
}

static void steps(uint8_t b, int n)
{
    while (n--) step(b);
}

int main(int argc, char **argv)
{
    if (argc > 1) outdir = argv[1];
    vhal_reset(42);
    vhal_set_unix(1800000007); /* 2027-01-15 08:00:07 UTC */
    store_mount();

    totp_rec_t t = {0};
    const char *labels[] = {"GitHub", "Google", "AWS root", "Bank", "Proton", "Fastmail", "Discord"};
    for (int i = 0; i < 7; i++) {
        memset(&t, 0, sizeof(t));
        strcpy(t.label, labels[i]);
        t.digits = 6;
        t.period = 30;
        t.secret_len = 20;
        for (int k = 0; k < 20; k++) t.secret[k] = (uint8_t)(k * 13 + i);
        store_write_totp(-1, &t);
    }
    contact_rec_t c = {0};
    const char *names[] = {"Mom", "Alex", "Sam"};
    for (int i = 0; i < 3; i++) {
        memset(&c, 0, sizeof(c));
        strcpy(c.name, names[i]);
        for (int k = 0; k < 32; k++) c.key[k] = (uint8_t)(k + 7 * i);
        c.is_low = (uint8_t)(i & 1);
        store_write_contact(-1, &c);
    }

    app_init();
    app_poll();
    dump("home");
    step(BTN_CENTER);
    dump("codes");
    step(BTN_CENTER);
    dump("code");
    step(BTN_LEFT);
    step(BTN_LEFT);
    step(BTN_DOWN);
    step(BTN_CENTER);
    dump("contacts");
    step(BTN_CENTER);
    dump("contact");
    step(BTN_LEFT);
    step(BTN_LEFT);
    steps(BTN_DOWN, 2);
    step(BTN_CENTER); /* pair */
    dump("pair");
    /* Fake a peer to reach the SAS screen. */
    pairing_t peer;
    uint8_t rnd[48];
    for (int i = 0; i < 48; i++) rnd[i] = (uint8_t)(i * 3 + 1);
    pairing_start(&peer, rnd);
    uint8_t f[PAIR_FRAME_MAX], rx[256];
    for (unsigned seq = 0; seq < 8; seq++) {
        size_t n = pairing_tx(&peer, f, sizeof(f), seq);
        app_ir_rx(f, n);
        vhal_advance(400);
        app_poll();
        size_t m = vhal_ir_take(rx, sizeof(rx));
        pairing_rx(&peer, rx, m);
    }
    dump("sas");
    step(BTN_CENTER);
    steps(BTN_DOWN, 25); /* Z? no: wheel starts at A; 'J' = 9 */
    step(BTN_LEFT);
    steps(BTN_UP, 16);   /* back to 'J' */
    step(BTN_RIGHT);
    steps(BTN_DOWN, 26); /* 'a' */
    step(BTN_RIGHT);
    steps(BTN_DOWN, 12); /* 'm' */
    dump("text");
    step(BTN_LEFT);
    step(BTN_LEFT);
    step(BTN_LEFT);
    step(BTN_CENTER); /* abandon naming: fall back to home through sleep below */

    /* Settings and time */
    vhal_reset(42);
    vhal_set_unix(1800000007);
    store_mount();
    app_init();
    steps(BTN_DOWN, 4);
    step(BTN_CENTER);
    dump("settings");
    step(BTN_CENTER);
    dump("time");
    step(BTN_LEFT);
    steps(BTN_DOWN, 2);
    step(BTN_CENTER);
    dump("pin");
    /* Sleep screen */
    for (int i = 0; i < 1000; i++) {
        if (app_poll() == APP_SLEEP) break;
        vhal_advance(100);
    }
    dump("sleep");
    return 0;
}
