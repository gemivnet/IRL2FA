/* IRL2FA on the STM32U083: board bring-up and the main loop around the
 * portable core (core/app.h).
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include <string.h>

#include "app.h"
#include "board.h"
#include "hal.h"

#ifndef IRL2FA_DEBUG_HOOK
#define IRL2FA_DEBUG_HOOK 1
#endif

#if IRL2FA_DEBUG_HOOK
/* Debug channel for the emulator tests and for a debugger (no semihosting,
 * nothing leaves the chip): a RAM block a test reads by symbol name. It
 * holds only non-secret state. Disable with -DIRL2FA_DEBUG_HOOK=0. */
struct irl2fa_debug {
    uint32_t magic;        /* "IDBG" once main() has started */
    uint32_t polls;        /* app_poll() calls */
    uint32_t sleeps;       /* Stop 2 entries */
    uint32_t flash_errors; /* failed erase / program / verify */
    uint32_t i2c_errors;
    uint32_t epd_timeouts;
    char screen[16];       /* app_screen_name() after the last poll */
};
__attribute__((used)) volatile struct irl2fa_debug irl2fa_debug;

static void debug_update(void)
{
    const char *s = app_screen_name();
    size_t n = strlen(s);
    if (n > sizeof(irl2fa_debug.screen) - 1) n = sizeof(irl2fa_debug.screen) - 1;
    for (size_t i = 0; i < sizeof(irl2fa_debug.screen); i++)
        irl2fa_debug.screen[i] = i < n ? s[i] : '\0';
    irl2fa_debug.polls++;
    irl2fa_debug.flash_errors = board_flash_errors;
    irl2fa_debug.i2c_errors = board_i2c_errors;
    irl2fa_debug.epd_timeouts = board_epd_timeouts;
}
#define DEBUG_INIT() (irl2fa_debug.magic = 0x47424449u)
#define DEBUG_UPDATE() debug_update()
#define DEBUG_SLEEP() (irl2fa_debug.sleeps++)
#else
#define DEBUG_INIT() ((void)0)
#define DEBUG_UPDATE() ((void)0)
#define DEBUG_SLEEP() ((void)0)
#endif

int main(void)
{
    board_init();
    DEBUG_INIT();
    app_init();
    uint8_t rx[64];
    for (;;) {
        size_t n;
        while ((n = board_ir_take(rx, sizeof(rx))) != 0) app_ir_rx(rx, n);
        uint32_t wait = app_poll();
        DEBUG_UPDATE();
        if (wait == APP_SLEEP) {
            DEBUG_SLEEP();
            hal_sleep(app_wake_at());
            app_wake();
            continue;
        }
        board_idle_until(board_millis() + wait);
    }
}
