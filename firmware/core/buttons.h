/* Joystick press filter shared by the board HALs (port/stm32u0 and the
 * virtual board in vhal/): turns contact levels plus latched press edges
 * into the "held buttons" bitmask hal_buttons() reports to the core.
 *
 * The core sees a key when a bit appears in that mask. A press is reported
 * at once, even if it was released before anyone looked (the edge is
 * latched); a release only after the contact has read open for
 * BTN_DEBOUNCE_MS, which also swallows bounce.
 *
 * The case this exists for: the core blocks in hal_display_show() for a
 * whole refresh (0.42 s partial). A person who presses, releases and presses
 * again inside that time leaves the bit set the whole while, so a plain
 * level filter shows one long press and the second one is lost. Here a
 * press edge arriving at least BTN_DEBOUNCE_MS after the button was last
 * seen down, while it is still reported held, is reported as a release now
 * and as a new press on the next call (and *again tells the caller to make
 * that call soon). Bounce, edges within BTN_DEBOUNCE_MS of the last time the
 * button was seen down, is still one press.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_BUTTONS_H
#define IRL2FA_BUTTONS_H

#include <stdbool.h>
#include <stdint.h>

#define BTN_COUNT 5
#define BTN_DEBOUNCE_MS 25u

typedef struct {
    uint8_t reported;              /* what the core was last told */
    uint8_t pending;               /* new presses held back one call */
    uint32_t last_seen[BTN_COUNT]; /* when each button was last seen down */
} btn_filter_t;

/* raw: contacts closed now (bit i = button 1 << i). edges: press edges
 * latched since the last call; edge_ms[i]: time of the latest edge of
 * button i. Returns the mask to report. *again is set when a press is
 * being held back and the caller should call again without waiting. */
uint8_t btn_filter(btn_filter_t *f, uint8_t raw, uint8_t edges, const uint32_t edge_ms[BTN_COUNT],
                   uint32_t now, bool *again);

#endif
