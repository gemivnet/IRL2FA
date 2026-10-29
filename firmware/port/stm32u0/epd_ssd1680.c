/* Driver for the Good Display GDEY0213B74 2.13" e-paper (Solomon SSD1680
 * controller), written for IRL2FA from the SSD1680 datasheet (Rev 0.14,
 * "SSD1680.pdf"): command table section 7, typical operating sequence
 * section 9, and the GDEY0213B74 datasheet's reference flow. The sequence is
 * the one GxEPD2's GxEPD2_213_B74 uses (studied, not copied).
 *
 * The panel sits behind a P-FET load switch and is powered only for the
 * duration of an update. Its RAM is therefore lost every time, and a partial
 * (DISPLAY_FAST) update needs the previous image written to the "RED" RAM
 * (0x26), which display mode 2 uses as the old image (SSD1680 Table 6-5).
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include "epd_ssd1680.h"

#include <stdbool.h>
#include <string.h>

#include "board.h"

/* Panel geometry: 122 sources = RAM X (16 bytes of 8 pixels per gate line,
 * the last 6 bits unused), 250 gates = RAM Y. With driver output control
 * 0x01 = F9 00 00 and data entry mode 0x03 below, RAM (0, 0) is the top-left
 * pixel of the panel held in portrait with its FPC at the bottom (the
 * orientation of Good Display's drawing and demo code). IRL2FA mounts it
 * with the FPC leaving the LEFT short edge seen from the front
 * (hardware/board.yaml `display.fpc_exit`) and FPC pin 1 at the top, which
 * is exactly the GDEY0213B74 mechanical drawing (datasheet p.7) as printed,
 * and that is the portrait view turned 90 degrees clockwise (the source
 * axis runs along the 122-pixel short edge):
 *
 *     landscape (lx, ly)  ->  RAM x = ly, RAM y = 249 - lx
 *
 * If a prototype shows the image upside down, build with EPD_ROTATE_180. */
#define EPD_SOURCES 122
#define EPD_GATES 250
#define EPD_RAM_STRIDE 16

#define FULL_TIMEOUT_MS 10000u /* datasheet: 3 s typ at 25 C, slower when cold */
#define FAST_TIMEOUT_MS 5000u  /* 0.42 s typ */
#define CMD_TIMEOUT_MS 200u

_Static_assert(DISPLAY_W == EPD_GATES && DISPLAY_H == EPD_SOURCES, "panel is 250 x 122");

static uint8_t shown[DISPLAY_BYTES]; /* what the panel shows now */
static bool shown_valid;

static void cs(bool high) { pin_write(PIN_PORT(PIN_EPD_CS), PIN_MASK(PIN_EPD_CS), high); }
static void dc(bool high) { pin_write(PIN_PORT(PIN_EPD_DC), PIN_MASK(PIN_EPD_DC), high); }
static void rst(bool high) { pin_write(PIN_PORT(PIN_EPD_RST), PIN_MASK(PIN_EPD_RST), high); }
static bool busy(void) { return pin_read(PIN_PORT(PIN_EPD_BUSY), PIN_MASK(PIN_EPD_BUSY)); }

static void command(uint8_t c, const uint8_t *params, size_t n)
{
    cs(false);
    dc(false);
    spi_write(&c, 1);
    if (n) {
        dc(true);
        spi_write(params, n);
    }
    cs(true);
}

#define CMD(c, ...)                                         \
    do {                                                    \
        static const uint8_t p_[] = {__VA_ARGS__};          \
        command((c), p_, sizeof(p_));                       \
    } while (0)

/* BUSY is high while the controller works (SSD1680 pin description). */
static bool wait_idle(uint32_t timeout_ms)
{
    uint32_t start = board_millis();
    while (busy()) {
        if (board_millis() - start > timeout_ms) {
            board_epd_timeouts++;
            return false;
        }
        __WFI(); /* Sleep mode; SysTick wakes us every millisecond */
    }
    return true;
}

static inline bool black(const uint8_t *fb, int lx, int ly)
{
    return (fb[ly * DISPLAY_STRIDE + (lx >> 3)] >> (7 - (lx & 7))) & 1;
}

/* One gate line of panel RAM (bit 1 = white, SSD1680 Table 6-5). */
static void ram_row(const uint8_t *fb, int y, uint8_t out[EPD_RAM_STRIDE])
{
    for (int bx = 0; bx < EPD_RAM_STRIDE; bx++) {
        uint8_t v = 0xFF;
        for (int k = 0; k < 8; k++) {
            int x = bx * 8 + k;
            if (x >= EPD_SOURCES) break;
#ifndef EPD_ROTATE_180
            int lx = EPD_GATES - 1 - y, ly = x;
#else
            int lx = y, ly = EPD_SOURCES - 1 - x;
#endif
            if (black(fb, lx, ly)) v &= (uint8_t)~(0x80u >> k);
        }
        out[bx] = v;
    }
}

static void write_ram(uint8_t which, const uint8_t *fb)
{
    CMD(0x4E, 0x00);       /* RAM X address counter */
    CMD(0x4F, 0x00, 0x00); /* RAM Y address counter */
    cs(false);
    dc(false);
    spi_write(&which, 1);  /* 0x24 B/W RAM or 0x26 RED RAM */
    dc(true);
    uint8_t row[EPD_RAM_STRIDE];
    for (int y = 0; y < EPD_GATES; y++) {
        ram_row(fb, y, row);
        spi_write(row, sizeof(row));
    }
    cs(true);
}

void epd_lines_off(void)
{
    cs(false);
    dc(false);
    rst(false);
    pin_config(PIN_PORT(PIN_EPD_CS), PIN_MASK(PIN_EPD_CS), LL_GPIO_MODE_OUTPUT, LL_GPIO_PULL_NO, 0);
    pin_config(PIN_PORT(PIN_EPD_DC), PIN_MASK(PIN_EPD_DC), LL_GPIO_MODE_OUTPUT, LL_GPIO_PULL_NO, 0);
    pin_config(PIN_PORT(PIN_EPD_RST), PIN_MASK(PIN_EPD_RST), LL_GPIO_MODE_OUTPUT, LL_GPIO_PULL_NO, 0);
    pin_write(PIN_PORT(PIN_EPD_SCK), PIN_MASK(PIN_EPD_SCK), false);
    pin_write(PIN_PORT(PIN_EPD_SDI), PIN_MASK(PIN_EPD_SDI), false);
    pin_config(PIN_PORT(PIN_EPD_SCK), PIN_MASK(PIN_EPD_SCK), LL_GPIO_MODE_OUTPUT, LL_GPIO_PULL_NO, 0);
    pin_config(PIN_PORT(PIN_EPD_SDI), PIN_MASK(PIN_EPD_SDI), LL_GPIO_MODE_OUTPUT, LL_GPIO_PULL_NO, 0);
    pin_config(PIN_PORT(PIN_EPD_BUSY), PIN_MASK(PIN_EPD_BUSY), LL_GPIO_MODE_ANALOG, LL_GPIO_PULL_NO, 0);
}

static bool power_on(void)
{
    /* Supply VCI, wait 10 ms (SSD1680 section 9 flow), only then raise the
     * control lines: a powered-down panel must not be fed through its pins. */
    pin_write(PIN_PORT(PIN_EPD_PWR_N), PIN_MASK(PIN_EPD_PWR_N), false);
    board_delay_ms(10);
    pin_config(PIN_PORT(PIN_EPD_BUSY), PIN_MASK(PIN_EPD_BUSY), LL_GPIO_MODE_INPUT, LL_GPIO_PULL_NO, 0);
    cs(true);
    rst(true);
    spi_open();
    /* Hardware reset, then software reset 0x12 (it restores every register
     * to its POR value; BUSY is high while it runs). */
    board_delay_ms(1);
    rst(false);
    board_delay_ms(10);
    rst(true);
    board_delay_ms(10);
    if (!wait_idle(CMD_TIMEOUT_MS)) return false;
    command(0x12, NULL, 0);
    board_delay_ms(10);
    return wait_idle(CMD_TIMEOUT_MS);
}

static void power_off(void)
{
    CMD(0x10, 0x01); /* deep sleep mode 1, then cut the supply */
    spi_close();
    epd_lines_off();
    pin_write(PIN_PORT(PIN_EPD_PWR_N), PIN_MASK(PIN_EPD_PWR_N), true);
}

void epd_show(const uint8_t *fb, display_mode_t mode)
{
    bool fast = mode == DISPLAY_FAST && shown_valid;
    bool ok = power_on();
    if (ok) {
        CMD(0x01, 0xF9, 0x00, 0x00);       /* driver output: MUX 250 gates, GD=SM=TB=0 */
        CMD(0x11, 0x03);                   /* data entry: X+, Y+, X first */
        CMD(0x44, 0x00, EPD_RAM_STRIDE - 1); /* RAM X window, in bytes */
        CMD(0x45, 0x00, 0x00, (EPD_GATES - 1) & 0xFF, (EPD_GATES - 1) >> 8);
        if (fast) CMD(0x3C, 0x80);         /* border: VCOM, i.e. left alone */
        else CMD(0x3C, 0x05);              /* border: GS transition, LUT1 (white) */
        CMD(0x21, 0x00, 0x80);             /* normal RAM use; sources S8..S167 */
        CMD(0x18, 0x80);                   /* internal temperature sensor */
        write_ram(0x24, fb);               /* new image */
        write_ram(0x26, fast ? shown : fb); /* old image (mode 2) */
        /* Display update control 2: clock + analog on, load temperature and
         * LUT, display, analog + clock off. F7 = mode 1 (full, flashing),
         * FF = mode 2 (partial, the OTP's differential waveform). */
        if (fast) CMD(0x22, 0xFF);
        else CMD(0x22, 0xF7);
        command(0x20, NULL, 0);            /* master activation */
        ok = wait_idle(fast ? FAST_TIMEOUT_MS : FULL_TIMEOUT_MS);
    }
    power_off();
    if (ok) {
        memcpy(shown, fb, DISPLAY_BYTES);
        shown_valid = true;
    } else {
        shown_valid = false; /* unknown panel state: next update is a full one */
    }
}
