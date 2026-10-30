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
#include "../tests/data/fw_fixtures.h"

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
    step(BTN_LEFT);
    /* Firmware update: the menu, and sending this device's image. */
    vhal_set_update_image(FIX_SAMPLE_IMAGE, sizeof(FIX_SAMPLE_IMAGE));
    steps(BTN_DOWN, 5);
    step(BTN_CENTER);
    dump("update_menu");
    step(BTN_DOWN);
    step(BTN_CENTER);
    dump("update_send");
    step(BTN_LEFT);
    vhal_set_update_image(NULL, 0);
    /* About (the debug port's state) and Lock debug port. */
    vhal_set_debug_lock(0, -1);
    step(BTN_LEFT);
    steps(BTN_DOWN, 3);
    step(BTN_CENTER);
    dump("about");
    step(BTN_CENTER);
    steps(BTN_DOWN, 6);
    step(BTN_CENTER);
    dump("lock");
    step(BTN_LEFT);
    vhal_set_debug_lock(-1, -1);
    /* Sleep screen */
    for (int i = 0; i < 1000; i++) {
        if (app_poll() == APP_SLEEP) break;
        vhal_advance(100);
    }
    dump("sleep");

    /* Simple mode: the intro from Settings, on a device with no contacts. */
    vhal_reset(43);
    vhal_set_unix(1800000007);
    store_mount();
    app_init();
    steps(BTN_DOWN, 4);
    step(BTN_CENTER); /* Settings */
    steps(BTN_DOWN, 4);
    step(BTN_CENTER); /* Simple mode */
    dump("simple_intro");
    /* Turned on (as the PIN screens would) with nobody paired yet. */
    static const uint8_t FAMILY[] = {1, 1, 2, 2, 3, 3};
    store_set_simple(FAMILY, sizeof(FAMILY));
    app_wake();
    app_poll();
    dump("simple_empty");

    /* Grandma's device: three people, simple mode on, low battery later. */
    vhal_reset(44);
    vhal_set_unix(1800000002);
    store_mount();
    const char *family[] = {"Susan", "Grandpa Joe", "Dr. Patel"};
    for (int i = 0; i < 3; i++) {
        memset(&c, 0, sizeof(c));
        strcpy(c.name, family[i]);
        for (int k = 0; k < 32; k++) c.key[k] = (uint8_t)(k * 3 + 11 * i);
        c.is_low = (uint8_t)(i & 1);
        store_write_contact(-1, &c);
    }
    store_set_simple(FAMILY, sizeof(FAMILY));
    app_init();
    app_poll();
    dump("simple");
    step(BTN_DOWN);
    vhal_set_battery_mv(2450);
    step(BTN_DOWN);
    dump("simple_lowbat");
    vhal_set_battery_mv(3000);
    step(BTN_UP);
    /* Hold the centre: after 3 s it says to keep holding, at 10 s the
     * family PIN. */
    vhal_set_buttons(BTN_CENTER);
    for (int i = 0; i < 40; i++) {
        app_poll();
        vhal_advance(100);
    }
    dump("simple_hold");
    for (int i = 0; i < 70; i++) {
        app_poll();
        vhal_advance(100);
    }
    vhal_set_buttons(0);
    app_poll();
    dump("family_pin");
    return 0;
}
