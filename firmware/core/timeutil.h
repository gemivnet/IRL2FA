/* Calendar conversion without libc (proleptic Gregorian, UTC).
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_TIMEUTIL_H
#define IRL2FA_TIMEUTIL_H

#include <stdint.h>

typedef struct {
    int year, month, day; /* month 1..12, day 1..31 */
    int hour, min, sec;
} civil_t;

void time_to_civil(int64_t t, civil_t *c);
int64_t time_from_civil(const civil_t *c);
int time_days_in_month(int year, int month);

#endif
