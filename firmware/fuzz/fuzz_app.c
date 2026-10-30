/* libFuzzer harness: the whole application driven through its inputs.
 *
 * The input is a program of steps for the virtual board: joystick presses
 * and holds, time passing, raw bytes on the IR receiver, a whole exchange
 * with an honest peer device (so pairing can complete and the SAS and naming
 * screens are reached), sleep and wake, reboots, power cuts after a chosen
 * number of flash operations (followed by a reboot), and simple mode turned
 * on with the family PIN FAMILY (as the settings screens would, then a
 * reboot; the fuzzer then has to find the 10 s centre hold and that PIN to
 * get out). Every screen is
 * drawn on every poll, so the fuzzer also covers rendering of whatever the
 * store and the input produce.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../core/app.h"
#include "../core/hal.h"
#include "../core/pairing.h"
#include "../core/store.h"
#include "../vhal/vhal.h"

#define ASSERT(c) do { if (!(c)) abort(); } while (0)
#define MAX_STEPS 600

static const uint8_t FAMILY[] = {1, 1, 2, 2, 3, 3}; /* up up right right down down */
static pairing_t peer;
static unsigned peer_seq;

static uint32_t poll(void)
{
    uint32_t r = app_poll();
    ASSERT(app_screen_name() != NULL);
    if (r == APP_SLEEP) {
        ASSERT(vhal_ir_on() == 0);
        vhal_wake();
        app_wake();
    }
    return r;
}

static void press(uint8_t mask)
{
    vhal_set_buttons(mask);
    poll();
    vhal_advance(30);
    vhal_set_buttons(0);
    poll();
    vhal_advance(30);
}

/* A reset: RAM and peripherals start over, the flash and the clock keep. */
static void reboot(void)
{
    vhal_fail_after(-1);
    vhal_set_buttons(0);
    hal_ir_enable(false);
    vhal_wake();
    app_init();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) return 0;
    vhal_reset(data[0]);
    vhal_set_unix(data[0] & 1 ? 1800000000 : 0); /* even: the clock is not set yet */
    app_init();
    uint8_t r[48];
    for (int i = 0; i < 48; i++) r[i] = (uint8_t)(i * 37 + data[0]);
    pairing_start(&peer, r);
    peer_seq = 0;

    size_t i = 1;
    for (int steps = 0; i < size && steps < MAX_STEPS; steps++) {
        uint8_t op = data[i++];
        uint8_t arg = i < size ? data[i] : 0;
        switch (op & 7) {
        case 0: /* press one direction (or, rarely, a chord) */
        case 1: {
            static const uint8_t B[8] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_CENTER,
                                         BTN_CENTER, BTN_DOWN, BTN_UP | BTN_LEFT};
            press(B[(op >> 3) & 7]);
            break;
        }
        case 2: /* hold a direction for a while (auto-repeat; arg bit 6: up to 16 s) */
            vhal_set_buttons((uint8_t)(1u << ((op >> 3) % 5)));
            for (int k = 0; k < (arg & 63); k++) {
                poll();
                vhal_advance(arg & 64 ? 250 : 50);
            }
            vhal_set_buttons(0);
            poll();
            i++;
            break;
        case 3: /* time passes: 0..25 s, or up to ~3 days */
            vhal_advance((op & 8) ? (uint32_t)arg * 1000u * 1000u : (uint32_t)arg * 100u);
            poll();
            i++;
            break;
        case 4: { /* raw bytes on the IR receiver */
            size_t n = arg % 64;
            i++;
            if (n > size - i) n = size - i;
            app_ir_rx(data + i, n);
            i += n;
            poll();
            break;
        }
        case 5: { /* an honest peer next to us: one frame each way */
            uint8_t buf[256], f[PAIR_FRAME_MAX];
            size_t n = vhal_ir_take(buf, sizeof(buf));
            pairing_rx(&peer, buf, n);
            n = pairing_tx(&peer, f, sizeof(f), peer_seq++);
            if (vhal_ir_on()) app_ir_rx(f, n);
            vhal_advance(250);
            poll();
            break;
        }
        case 6: /* reboot, simple mode on, or sleep until woken */
            if (op & 8) {
                reboot();
            } else if (op & 16) {
                /* As if set in Settings, then a reset: the app is never
                 * woken while awake (that would leave a pairing's IR on). */
                if (store_unlocked()) store_set_simple(FAMILY, sizeof(FAMILY));
                reboot();
            } else {
                vhal_advance(10u * 60u * 1000u);
                poll();
            }
            break;
        case 7: /* power cut after `arg` more flash operations, then reboot later */
            vhal_fail_after(arg % 48);
            i++;
            break;
        }
        if (vhal_fail_tripped()) reboot();
    }
    vhal_fail_after(-1);
    pairing_wipe(&peer);
    return 0;
}
