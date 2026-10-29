/* The IRL2FA application: UI state machine on top of the store, TOTP,
 * contact words and pairing. Non-blocking, so the same code runs on the
 * device (port/stm32u0/main.c) and in the browser simulator (sim/).
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_APP_H
#define IRL2FA_APP_H

#include <stddef.h>
#include <stdint.h>

#define IRL2FA_VERSION "0.1.0"

/* Returned by app_poll() when the app wants to sleep. */
#define APP_SLEEP 0xffffffffu

/* Call once after reset. */
void app_init(void);

/* Call repeatedly while awake. Reads the buttons, runs timers and redraws
 * the display if needed. Returns how many milliseconds may pass before the
 * next call, or APP_SLEEP: the caller then calls hal_sleep(app_wake_at())
 * and app_wake() when it returns. */
uint32_t app_poll(void);
int64_t app_wake_at(void);
void app_wake(void);

/* Bytes received by the IR transceiver. */
void app_ir_rx(const uint8_t *data, size_t len);

/* Introspection for the simulator and tests. */
const char *app_screen_name(void);

#endif
