/* Board support shared by the STM32U0 port's files (not part of hal.h).
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_BOARD_H
#define IRL2FA_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pins.h"
#include "stm32u0xx.h"
#include "stm32u0xx_ll_gpio.h"

#define SYSCLK_HZ 16000000u /* HSI16, no PLL */

/* ---- clocks, time ---- */
void board_init(void);
void board_clock_init(void);
uint32_t board_millis(void);
/* Waits in Sleep mode (WFI, SysTick wakes every ms). */
void board_delay_ms(uint32_t ms);
void board_delay_us(uint32_t us); /* busy wait, short delays only */
/* Waits in Sleep mode until `deadline` (board_millis() clock), a button
 * press or received IR data, whichever comes first. */
void board_idle_until(uint32_t deadline);

/* ---- GPIO ---- */
#define PIN_PORT_INDEX(port) (((uint32_t)(port) - IOPORT_BASE) / 0x400u)

/* PIN_MASK(JOY_UP) etc.: expand the argument first, then paste, so the
 * aliases from pins.h (JOY_UP -> PIN_JOY_A) work. */
#define PIN_CAT2_(a, b) a##b
#define PIN_CAT_(a, b) PIN_CAT2_(a, b)
#define PIN_PORT(p) PIN_CAT_(p, _PORT)
#define PIN_NUM(p) PIN_CAT_(p, _NUM)
#define PIN_MASK(p) PIN_CAT_(p, _MASK)

static inline void pin_write(GPIO_TypeDef *port, uint32_t mask, bool high)
{
    if (high) port->BSRR = mask;
    else port->BRR = mask;
}

static inline bool pin_read(const GPIO_TypeDef *port, uint32_t mask)
{
    return (port->IDR & mask) != 0;
}

/* mode: LL_GPIO_MODE_*, pull: LL_GPIO_PULL_*, af only used in AF mode. */
void pin_config(GPIO_TypeDef *port, uint32_t mask, uint32_t mode, uint32_t pull, uint32_t af);

/* ---- I2C1 (RV-3032) ---- */
bool i2c_write(uint8_t addr7, uint8_t reg, const uint8_t *data, size_t len);
bool i2c_read(uint8_t addr7, uint8_t reg, uint8_t *data, size_t len);

/* ---- SPI1 (e-paper), write only ---- */
void spi_open(void);  /* pins to AF, peripheral on */
void spi_close(void); /* peripheral off, SCK/MOSI driven low */
void spi_write(const uint8_t *data, size_t len);

/* ---- wake sources ---- */
#define WAKE_BUTTON (1u << 0)
#define WAKE_RTC (1u << 1)
#define WAKE_IR (1u << 2)
extern volatile uint32_t board_wake_flags;

/* ---- counters for the debug hook ---- */
extern volatile uint32_t board_flash_errors;
extern volatile uint32_t board_i2c_errors;
extern volatile uint32_t board_epd_timeouts;

/* ---- IR receive buffer, drained by main() into app_ir_rx() ---- */
size_t board_ir_take(uint8_t *buf, size_t cap);

/* ---- flash, anywhere (offsets from FLASH_BASE, flash_layout.h) ----
 * Reads survive torn (ECC-bad) double-words: zeros and false. */
bool board_flash_read(uint32_t off, void *buf, size_t len);
bool board_flash_erase(uint32_t off);   /* the page holding off */
bool board_flash_program(uint32_t off, const void *buf, size_t len);

/* ---- the bootloader's subset of board_init(): clocks, SysTick, pins,
 * joystick interrupts (no RTC, no panel) ---- */
void board_init_boot(void);
/* VDDA in mV, and whether it was measured against ST's factory VREFINT
 * calibration (false: the ES0602 2.1.2 fallback, a typical value). */
uint16_t board_battery_mv(bool *calibrated);

/* fw_app.c: at the first joystick wake, ask the bootloader to turn on
 * readout protection (restarts the device if it does). */
void board_request_lock(void);

#endif
