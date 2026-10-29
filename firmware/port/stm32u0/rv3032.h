/* Micro Crystal RV-3032-C7 real-time clock on I2C1.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_RV3032_H
#define IRL2FA_RV3032_H

#include <stdbool.h>
#include <stdint.h>

#define RV3032_ADDR 0x51

/* Checks the power-on / voltage-low flags, disables CLKOUT (persistently,
 * in the configuration EEPROM, once) and turns every interrupt off. */
void rv3032_init(void);

/* UTC seconds since the Unix epoch, or 0 while the time is not valid (never
 * set since the RTC lost power, or unreadable). */
int64_t rv3032_get(void);
void rv3032_set(int64_t unix_time);

/* Arms the periodic countdown timer to pull INT low after `seconds`
 * (rounded up to a minute beyond 4095 s). Returns false if it could not. */
bool rv3032_wake_in(uint32_t seconds);
void rv3032_wake_cancel(void);

#endif
