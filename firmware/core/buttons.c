/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "buttons.h"

uint8_t btn_filter(btn_filter_t *f, uint8_t raw, uint8_t edges, const uint32_t edge_ms[BTN_COUNT],
                   uint32_t now, bool *again)
{
    uint8_t held_back = 0;
    for (unsigned i = 0; i < BTN_COUNT; i++) {
        uint8_t bit = (uint8_t)(1u << i);
        if (f->pending & bit) {
            /* Released last call; now the new press. */
            f->pending &= (uint8_t)~bit;
            f->reported |= bit;
            f->last_seen[i] = now;
            continue;
        }
        if ((edges & bit) && (f->reported & bit) &&
            edge_ms[i] - f->last_seen[i] >= BTN_DEBOUNCE_MS) {
            /* A second press the core has not seen released. */
            f->reported &= (uint8_t)~bit;
            f->pending |= bit;
            held_back |= bit;
            continue;
        }
        if ((raw | edges) & bit) {
            f->last_seen[i] = now;
            f->reported |= bit;
        } else if ((f->reported & bit) && now - f->last_seen[i] >= BTN_DEBOUNCE_MS) {
            f->reported &= (uint8_t)~bit;
        }
    }
    if (again) *again = held_back != 0;
    return f->reported;
}
