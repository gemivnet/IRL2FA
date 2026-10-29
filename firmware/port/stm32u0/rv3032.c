/* Micro Crystal RV-3032-C7 driver, written from the RV-3032-C7 Application
 * Manual (Rev 1.3, May 2023): register overview section 3.2, time setting
 * and reading 4.5, EEPROM handling 4.6, periodic countdown timer 4.8.
 *
 * The RV-3032 has no Unix-time counter (that is the RV-3028), so the BCD
 * clock and calendar registers are used. "Time valid" needs two things:
 * neither PORF (power-on reset) nor VLF (voltage low) is set, and our marker
 * is in the user RAM (40h). The marker is written by rv3032_set() and is
 * lost together with the RAM on any power loss, so a cell swap reliably
 * sends the person to the clock screen instead of showing wrong codes.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include "rv3032.h"

#include <string.h>

#include "board.h"
#include "timeutil.h"

#define R_HUNDREDTHS 0x00
#define R_SECONDS 0x01
#define R_TIMER0 0x0B
#define R_STATUS 0x0D
#define R_TEMP_LSB 0x0E
#define R_CTRL1 0x10
#define R_CTRL2 0x11
#define R_CTRL3 0x12
#define R_CLK_INT_MASK 0x14
#define R_EECMD 0x3F
#define R_USER_RAM 0x40
#define R_EE_PMU 0xC0
#define R_EE_OFFSET 0xC1

#define ST_TF (1u << 4)
#define ST_AF (1u << 3)
#define ST_PORF (1u << 1)
#define ST_VLF (1u << 0)
#define TL_EEF (1u << 3)
#define TL_EEBUSY (1u << 2)
#define C1_TE (1u << 3)
#define C1_EERD (1u << 2)
#define C1_TD_1HZ 0x2u
#define C1_TD_1_60HZ 0x3u
#define C2_TIE (1u << 4)
#define PMU_NCLKE (1u << 6)
#define OFF_PORIE (1u << 7)
#define OFF_VLIE (1u << 6)
#define EECMD_UPDATE 0x11

static const uint8_t MARKER[4] = {'I', 'R', 'L', 'T'};

static bool valid;
static int64_t cache_t;    /* whole seconds read from the clock... */
static uint32_t cache_sub; /* ...and how far into that second (ms) */
static uint32_t cache_ms;  /* board_millis() at that read */
static bool cache_ok;

/* The time now, from the last read and the millisecond tick since. */
static int64_t cache_now(uint32_t now)
{
    return cache_t + (int64_t)((cache_sub + (now - cache_ms)) / 1000u);
}

static bool rd(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_read(RV3032_ADDR, reg, buf, n);
}

static bool wr(uint8_t reg, const uint8_t *buf, size_t n)
{
    return i2c_write(RV3032_ADDR, reg, buf, n);
}

static bool wr1(uint8_t reg, uint8_t v)
{
    return wr(reg, &v, 1);
}

static uint8_t to_bcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static int from_bcd(uint8_t v)
{
    return (v >> 4) * 10 + (v & 0x0F);
}

static bool ee_idle(void)
{
    for (int i = 0; i < 200; i++) {
        uint8_t t;
        if (!rd(R_TEMP_LSB, &t, 1)) return false;
        if (!(t & TL_EEBUSY)) return true;
        board_delay_ms(1);
    }
    return false;
}

/* Section 4.6.10: disable the automatic refresh (EERD), edit the RAM
 * mirror, store all configuration RAM to EEPROM (EECMD 11h), re-enable the
 * refresh. Done once per device: afterwards the EEPROM already says so. */
static void ee_store_config(uint8_t reg, uint8_t value)
{
    if (!wr1(R_CTRL1, C1_EERD) || !ee_idle()) goto out;
    if (!wr1(reg, value)) goto out;
    if (!wr1(R_EECMD, EECMD_UPDATE)) goto out;
    ee_idle();
    uint8_t t;
    if (rd(R_TEMP_LSB, &t, 1) && (t & TL_EEF)) {
        board_i2c_errors++;
        wr1(R_TEMP_LSB, (uint8_t)(t & ~TL_EEF));
    }
out:
    wr1(R_CTRL1, 0);
}

void rv3032_init(void)
{
    valid = false;
    cache_ok = false;
    uint8_t st, mark[4];
    if (!rd(R_STATUS, &st, 1) || !rd(R_USER_RAM, mark, sizeof(mark))) return;
    if (st & (ST_PORF | ST_VLF)) {
        /* The clock stopped or ran on a collapsing supply: whatever is in
         * the time registers is not the time. Forget the marker. */
        static const uint8_t zero[4] = {0};
        wr(R_USER_RAM, zero, sizeof(zero));
    } else {
        valid = memcmp(mark, MARKER, sizeof(MARKER)) == 0;
    }
    /* Clear every flag (they are only set again by a new event; PORF in
     * particular must be cleared before it means anything, section 3.7). */
    wr1(R_STATUS, 0);
    /* No interrupts, no countdown, automatic EEPROM refresh on. */
    wr1(R_CTRL1, 0);
    wr1(R_CTRL2, 0);
    wr1(R_CTRL3, 0);
    wr1(R_CLK_INT_MASK, 0);

    /* CLKOUT runs at 32.768 kHz out of the box (NCLKE = 0) and costs current
     * for nothing here: turn it off, and PORIE/VLIE with it. */
    uint8_t pmu, off;
    if (rd(R_EE_PMU, &pmu, 1) && !(pmu & PMU_NCLKE)) ee_store_config(R_EE_PMU, pmu | PMU_NCLKE);
    if (rd(R_EE_OFFSET, &off, 1) && (off & (OFF_PORIE | OFF_VLIE)))
        ee_store_config(R_EE_OFFSET, off & (uint8_t)~(OFF_PORIE | OFF_VLIE));
}

int64_t rv3032_get(void)
{
    if (!valid) return 0;
    uint32_t now = board_millis();
    /* The core asks many times per screen; one read per 250 ms is plenty.
     * In between, count on from the last read with the millisecond tick,
     * starting from the hundredths it gave: returning the cached second
     * as is could lag a second boundary by up to 250 ms, and the screen
     * would show the previous word or code and redraw a moment later
     * (an extra refresh the emulator caught). */
    if (cache_ok && now - cache_ms < 250u) return cache_now(now);
    uint8_t b[8];
    if (!rd(R_HUNDREDTHS, b, sizeof(b))) {
        /* Keep counting from the last good read rather than flash "--:--". */
        return cache_ok ? cache_now(now) : 0;
    }
    const uint8_t *r = b + 1; /* Seconds .. Year */
    civil_t c = {
        .sec = from_bcd(r[0] & 0x7F),
        .min = from_bcd(r[1] & 0x7F),
        .hour = from_bcd(r[2] & 0x3F),
        .day = from_bcd(r[4] & 0x3F),
        .month = from_bcd(r[5] & 0x1F),
        .year = 2000 + from_bcd(r[6]),
    };
    uint32_t hundredths = (uint32_t)from_bcd(b[0]);
    if (c.sec > 59 || c.min > 59 || c.hour > 23 || c.month < 1 || c.month > 12 || c.day < 1 ||
        c.day > time_days_in_month(c.year, c.month)) {
        valid = false;
        return 0;
    }
    cache_t = time_from_civil(&c);
    cache_sub = hundredths < 100u ? hundredths * 10u : 0u;
    cache_ms = now;
    cache_ok = true;
    return cache_t;
}

void rv3032_set(int64_t unix_time)
{
    /* The calendar covers 2000-01-01 .. 2099-12-31. */
    if (unix_time < 946684800) unix_time = 946684800;
    if (unix_time > 4102444799) unix_time = 4102444799;
    civil_t c;
    time_to_civil(unix_time, &c);
    int64_t days = unix_time / 86400;
    uint8_t r[7] = {
        to_bcd(c.sec), to_bcd(c.min), to_bcd(c.hour),
        (uint8_t)((days + 4) % 7), /* weekday, 0 = Sunday (1970-01-01 was a Thursday) */
        to_bcd(c.day), to_bcd(c.month), to_bcd(c.year - 2000),
    };
    /* One burst from Seconds to Year; writing Seconds also clears the
     * hundredths and restarts the prescaler, so the second starts now. */
    if (!wr(R_SECONDS, r, sizeof(r))) return;
    wr1(R_STATUS, 0);
    valid = wr(R_USER_RAM, MARKER, sizeof(MARKER));
    cache_t = unix_time;
    cache_sub = 0; /* the prescaler restarted */
    cache_ms = board_millis();
    cache_ok = valid;
}

bool rv3032_wake_in(uint32_t seconds)
{
    if (seconds == 0) seconds = 1;
    uint8_t td = C1_TD_1HZ;
    uint32_t n = seconds;
    if (n > 4095u) {
        td = C1_TD_1_60HZ;
        n = (seconds + 59u) / 60u;
        if (n > 4095u) n = 4095u;
    }
    /* Section 4.8.2: TE, TIE, TF to 0 in that order, TD, the value, TIE,
     * then TE from 0 to 1 starts the countdown. INT then pulses low. */
    uint8_t v[2] = {(uint8_t)n, (uint8_t)(n >> 8)};
    uint8_t st;
    bool ok = wr1(R_CTRL1, 0) && wr1(R_CTRL2, 0) && rd(R_STATUS, &st, 1) &&
              wr1(R_STATUS, (uint8_t)(st & ~ST_TF)) && wr1(R_CTRL1, td) &&
              wr(R_TIMER0, v, 2) && wr1(R_CTRL2, C2_TIE) && wr1(R_CTRL1, (uint8_t)(td | C1_TE));
    if (!ok) rv3032_wake_cancel();
    return ok;
}

void rv3032_wake_cancel(void)
{
    uint8_t st;
    wr1(R_CTRL1, 0);
    wr1(R_CTRL2, 0);
    if (rd(R_STATUS, &st, 1)) wr1(R_STATUS, (uint8_t)(st & ~(ST_TF | ST_AF)));
    cache_ok = false; /* we slept: re-read the time */
}
