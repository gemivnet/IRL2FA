/* Algorithms from Howard Hinnant, "chrono-Compatible Low-Level Date
 * Algorithms" (public domain).
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include "timeutil.h"

static int64_t floordiv(int64_t a, int64_t b)
{
    return (a >= 0) ? a / b : -((-a + b - 1) / b);
}

void time_to_civil(int64_t t, civil_t *c)
{
    int64_t days = floordiv(t, 86400);
    int64_t secs = t - days * 86400;
    c->hour = (int)(secs / 3600);
    c->min = (int)(secs / 60 % 60);
    c->sec = (int)(secs % 60);

    int64_t z = days + 719468;
    int64_t era = floordiv(z, 146097);
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;
    c->year = (int)(y + (m <= 2));
    c->month = (int)m;
    c->day = (int)d;
}

int64_t time_from_civil(const civil_t *c)
{
    int64_t y = c->year - (c->month <= 2);
    int64_t era = floordiv(y, 400);
    int64_t yoe = y - era * 400;
    int64_t mp = c->month > 2 ? c->month - 3 : c->month + 9;
    int64_t doy = (153 * mp + 2) / 5 + c->day - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + doe - 719468;
    return days * 86400 + c->hour * 3600 + c->min * 60 + c->sec;
}

int time_days_in_month(int year, int month)
{
    static const uint8_t DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return DAYS[(month - 1) % 12];
}
