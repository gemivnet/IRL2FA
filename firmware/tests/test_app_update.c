/* The application's firmware update screens: Settings -> Firmware update,
 * the PIN gate in front of the receiver, sending this device's image to
 * another device (here the core receiver on a virtual flash, as the other
 * device's bootloader runs it), and the bootloader's report after a restart.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include "../core/app.h"
#include "../core/fwboot.h"
#include "../core/fwlink.h"
#include "../core/hal.h"
#include "../core/store.h"
#include "../vhal/vhal.h"
#include "data/fw_fixtures.h"
#include "test.h"
#include "test_fw.h"
#include "vflash.h"

static void tap(uint8_t b)
{
    vhal_set_buttons(b);
    app_poll();
    vhal_advance(30);
    vhal_set_buttons(0);
    app_poll();
    vhal_advance(30);
}

static void taps(uint8_t b, int n)
{
    for (int i = 0; i < n; i++) tap(b);
}

static bool on(const char *screen)
{
    return strcmp(app_screen_name(), screen) == 0;
}

static void boot(uint64_t seed)
{
    vhal_reset(seed);
    vhal_set_unix(1800000000);
    app_init();
}

static void enter_pin(const uint8_t *syms, int n)
{
    static const uint8_t B[5] = {0, BTN_UP, BTN_RIGHT, BTN_DOWN, BTN_LEFT};
    for (int i = 0; i < n; i++) tap(B[syms[i]]);
    tap(BTN_CENTER);
}

/* Home -> Settings -> Firmware update (row 5 without a PIN, 6 with). */
static void open_update_menu(void)
{
    taps(BTN_DOWN, 4);
    tap(BTN_CENTER);
    taps(BTN_DOWN, store_settings()->pin_set ? 6 : 5);
    tap(BTN_CENTER);
}

TEST(test_app_update_receive)
{
    vhal_set_update_image(NULL, 0);
    int entered = vhal_update_entered();
    boot(30);
    open_update_menu();
    CHECK(on("menu"));
    tap(BTN_CENTER); /* Receive an update */
    CHECK_EQ_INT(vhal_update_entered(), entered + 1);
    CHECK(on("message")); /* no bootloader on the virtual board */
    tap(BTN_CENTER);
    CHECK(on("menu"));
    tap(BTN_DOWN); /* only one row: wraps to itself */
    tap(BTN_LEFT); /* back to Settings */
    tap(BTN_LEFT); /* and home */
    CHECK(on("menu"));

    /* With a PIN: asked again; a wrong one locks the device. */
    static const uint8_t PIN[] = {1, 2, 3, 4};
    static const uint8_t BAD[] = {3, 3, 3, 3};
    CHECK_EQ_INT(store_set_pin(PIN, 4), STORE_OK);
    open_update_menu();
    tap(BTN_CENTER);
    CHECK(on("pin"));
    tap(BTN_LEFT); /* empty: cancel, still unlocked */
    CHECK(on("menu"));
    CHECK(store_unlocked());
    tap(BTN_CENTER);
    enter_pin(BAD, 4);
    CHECK(on("pin"));
    CHECK(!store_unlocked());
    CHECK_EQ_INT(vhal_update_entered(), entered + 1);
    enter_pin(PIN, 4);
    CHECK(store_unlocked());
    open_update_menu();
    tap(BTN_CENTER);
    enter_pin(PIN, 4);
    CHECK_EQ_INT(vhal_update_entered(), entered + 2);
}

/* In simple mode the update is only reached the way every other setting
 * is: a 10 s centre hold and the family PIN, then Settings. */
TEST(test_app_update_simple_mode)
{
    static const uint8_t FAMILY[] = {1, 1, 2, 2, 3, 3};
    vhal_set_update_image(NULL, 0);
    int entered = vhal_update_entered();
    boot(34);
    CHECK_EQ_INT(store_set_simple(FAMILY, 6), STORE_OK);
    app_wake();
    app_poll();
    CHECK(on("simple"));
    /* Every short press stays in simple mode. */
    static const uint8_t KEYS[] = {BTN_DOWN, BTN_CENTER, BTN_RIGHT, BTN_UP, BTN_LEFT};
    for (int i = 0; i < 40; i++) {
        tap(KEYS[i % 5]);
        CHECK(on("simple"));
    }
    CHECK_EQ_INT(vhal_update_entered(), entered);
    /* The hold and the family PIN: the normal menus, with the update
     * where it always is. */
    vhal_set_buttons(BTN_CENTER);
    for (int t = 0; t < 10200; t += 50) {
        app_poll();
        vhal_advance(50);
    }
    vhal_set_buttons(0);
    app_poll();
    CHECK(on("pin"));
    enter_pin(FAMILY, 6);
    CHECK(on("menu"));
    open_update_menu();
    tap(BTN_CENTER);
    CHECK_EQ_INT(vhal_update_entered(), entered + 1);
}

static vflash_t PEER;
static fw_work_t WORK;

/* The other device: its bootloader's receiver on a blank flash, trusting
 * the test key. Runs until the app's sender is finished or `ms` pass. */
static void run_peer(fwrx_t *rx, fwboot_t *fb, uint32_t ms)
{
    uint8_t buf[512], f[FWL_FRAME_MAX];
    for (uint32_t t = 0; t < ms && on("update"); t += 10) {
        app_poll();
        size_t n = vhal_ir_take(buf, sizeof(buf));
        vhal_advance((uint32_t)(n * 10000u / 4800u) + 10u);
        if (n) fwrx_rx(rx, buf, n);
        if (rx->state == FWRX_COMPLETE) {
            fw_header_t h;
            fwrx_finish(rx, fwboot_install_staged(fb, &h));
            rx->reply = true;
        }
        if (rx->reply && vhal_ir_on()) {
            size_t m = fwrx_status(rx, f, sizeof(f));
            vhal_advance((uint32_t)(m * 10000u / 4800u));
            app_ir_rx(f, m);
        }
    }
}

TEST(test_app_update_send)
{
    vhal_set_update_image(FIX_SAMPLE_IMAGE, sizeof(FIX_SAMPLE_IMAGE));
    vhal_set_fw_version(FIX_SAMPLE_VERSION);
    boot(31);
    open_update_menu();
    tap(BTN_DOWN);
    tap(BTN_CENTER); /* Send it to another */
    CHECK(on("update"));
    CHECK(vhal_ir_on());

    vflash_init(&PEER);
    fw_flash_t fl = vflash_iface(&PEER);
    fwboot_t fb;
    memset(&fb, 0, sizeof(fb));
    fb.fl = &fl;
    fb.lay = &TFW_LAYOUT;
    fb.pol.hw_id = TFW_HW_ID;
    fb.pol.boot_version = 1;
    fb.pol.pubkey = FIX_TEST_PUB;
    fb.work = &WORK;
    static fwrx_t rx;
    fwrx_init(&rx, &fl, &TFW_LAYOUT, &fb.pol);
    uint32_t refreshes = vhal_refreshes(0) + vhal_refreshes(1);
    run_peer(&rx, &fb, 600000);
    CHECK(on("message"));
    CHECK(!vhal_ir_on());
    CHECK_EQ_INT(rx.state, FWRX_DONE);
    uint32_t drawn = vhal_refreshes(0) + vhal_refreshes(1) - refreshes;
    CHECK(drawn >= 5 && drawn < 20); /* progress in tenths, not every chunk */
    fwboot_report_t rep;
    CHECK_EQ_INT(fwboot_reset(&fb, false, &rep), FWBOOT_RUN_APP);
    CHECK_EQ_INT(rep.app_hdr.version, FIX_SAMPLE_VERSION);
    static uint8_t plain[FIX_SAMPLE_SIZE];
    tfw_sample_plain(plain, sizeof(plain));
    CHECK(memcmp(PEER.mem + TFW_LAYOUT.app_addr + FW_APP_OFFSET, plain, sizeof(plain)) == 0);
    tap(BTN_CENTER);
    CHECK(on("menu"));

    /* Nobody there: the sender gives up and says so. */
    tap(BTN_DOWN);
    tap(BTN_CENTER);
    CHECK(on("update"));
    for (int i = 0; i < 300 && on("update"); i++) {
        app_poll();
        uint8_t sink[512];
        vhal_ir_take(sink, sizeof(sink));
        vhal_advance(100);
    }
    CHECK(on("message"));
    CHECK(!vhal_ir_on());
    tap(BTN_CENTER);

    /* Cancelled with left. */
    tap(BTN_DOWN);
    tap(BTN_CENTER);
    CHECK(on("update"));
    tap(BTN_LEFT);
    CHECK(on("menu"));
    CHECK(!vhal_ir_on());

    /* A peer that refuses (another key): the reason is shown. */
    vflash_init(&PEER);
    static const uint8_t OTHER[32] = {1};
    fb.pol.pubkey = OTHER;
    fwrx_init(&rx, &fl, &TFW_LAYOUT, &fb.pol);
    tap(BTN_DOWN);
    tap(BTN_CENTER);
    run_peer(&rx, &fb, 600000);
    CHECK(on("message"));
    CHECK_EQ_INT(rx.state, FWRX_FAILED);
    vhal_set_update_image(NULL, 0);
}

TEST(test_app_update_result)
{
    vhal_set_update_result(FW_OK, 0x000200);
    boot(32);
    CHECK(on("message"));
    tap(BTN_CENTER);
    CHECK(on("menu"));
    vhal_set_update_result(FW_E_ROLLBACK, 0);
    boot(32);
    CHECK(on("message"));
    /* Shown once only. */
    boot(32);
    CHECK(on("menu"));
    /* Behind a PIN or the clock screen it is not shown (About tells). */
    vhal_set_update_result(FW_E_SIGNATURE, 0);
    vhal_reset(33);
    app_init();
    CHECK(on("time"));
}
