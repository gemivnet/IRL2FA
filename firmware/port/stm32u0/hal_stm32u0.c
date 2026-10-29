/* core/hal.h on the STM32U083KC, plus the board support declared in board.h.
 *
 * References: RM0503 (STM32U0 reference manual), DS14548 (STM32U073/U083
 * datasheet). Pin assignments come only from the generated pins.h.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include <string.h>

#include "board.h"
#include "buttons.h"
#include "epd_ssd1680.h"
#include "hal.h"
#include "rv3032.h"
#include "stm32u0xx_ll_adc.h" /* VREFINT_CAL_ADDR, VREFINT_CAL_VREF */

uint32_t SystemCoreClock = SYSCLK_HZ; /* declared by system_stm32u0xx.h */

volatile uint32_t board_wake_flags;
volatile uint32_t board_flash_errors;
volatile uint32_t board_i2c_errors;
volatile uint32_t board_epd_timeouts;

static volatile uint32_t ms_ticks;

/* Polls `*reg & mask` until it equals `want`, for at most `ms` milliseconds
 * of SysTick time (or a fixed spin count when SysTick is not running). */
static bool wait_bits(volatile uint32_t *reg, uint32_t mask, uint32_t want, uint32_t ms)
{
    uint32_t start = ms_ticks;
    uint32_t spins = 0;
    while ((*reg & mask) != want) {
        if ((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) != 0) {
            if (ms_ticks - start > ms) return false;
        } else if (++spins > ms * 4000u) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* clocks and time                                                    */

void SysTick_Handler(void)
{
    ms_ticks++;
}

uint32_t board_millis(void)
{
    return ms_ticks;
}

uint32_t hal_millis(void)
{
    return ms_ticks;
}

/* SYSCLK = HSI16. Range 1 (reset default) runs 16 MHz with zero flash wait
 * states (RM0503 flash latency table, as encoded in ST's LL_SetFlashLatency).
 * STOPWUCK makes Stop 2 wake up on HSI16 too, so the clock tree is the same
 * after a wake-up; this is still called after Stop 2 to be sure. */
void board_clock_init(void)
{
    RCC->APBENR1 |= RCC_APBENR1_PWREN;
    (void)RCC->APBENR1;
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | 0u;
    RCC->CR |= RCC_CR_HSION;
    wait_bits(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY, 5);
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE)) |
                RCC_CFGR_SW_0 | RCC_CFGR_STOPWUCK;
    wait_bits(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_0, 5);
    RCC->CR &= ~RCC_CR_MSION; /* MSI (reset clock) no longer needed */
    SystemCoreClock = SYSCLK_HZ;
}

static void systick_start(void)
{
    SysTick->LOAD = SYSCLK_HZ / 1000u - 1u;
    SysTick->VAL = 0;
    NVIC_SetPriority(SysTick_IRQn, 3);
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

static void systick_stop(void)
{
    SysTick->CTRL = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
}

void board_delay_us(uint32_t us)
{
    /* ~5 cycles per iteration at 16 MHz: 3 iterations per microsecond. */
    for (volatile uint32_t i = us * 3u + 1u; i != 0; i--) {
    }
}

void board_delay_ms(uint32_t ms)
{
    uint32_t start = ms_ticks;
    while (ms_ticks - start < ms) __WFI();
}

void board_idle_until(uint32_t deadline)
{
    while ((int32_t)(deadline - ms_ticks) > 0) {
        __disable_irq();
        if (board_wake_flags & (WAKE_BUTTON | WAKE_IR)) {
            __enable_irq();
            break;
        }
        __WFI(); /* wakes on any pending interrupt, PRIMASK only delays the ISR */
        __enable_irq();
    }
    __disable_irq();
    board_wake_flags &= ~(WAKE_BUTTON | WAKE_IR);
    __enable_irq();
}

/* ------------------------------------------------------------------ */
/* GPIO                                                               */

void pin_config(GPIO_TypeDef *port, uint32_t mask, uint32_t mode, uint32_t pull, uint32_t af)
{
    for (uint32_t bit = 1; bit != 0 && bit <= mask; bit <<= 1) {
        if (!(mask & bit)) continue;
        if (mode == LL_GPIO_MODE_ALTERNATE) {
            if (bit < (1u << 8)) LL_GPIO_SetAFPin_0_7(port, bit, af);
            else LL_GPIO_SetAFPin_8_15(port, bit, af);
        }
        LL_GPIO_SetPinPull(port, bit, pull);
        LL_GPIO_SetPinSpeed(port, bit, LL_GPIO_SPEED_FREQ_LOW);
        LL_GPIO_SetPinMode(port, bit, mode);
    }
}

#define PIN_CFG(p, mode, pull, af) pin_config(PIN_PORT(p), PIN_MASK(p), (mode), (pull), (af))

static btn_filter_t btn_filter_state;
static volatile uint8_t btn_latched;
static volatile uint32_t btn_edge_ms[BTN_COUNT]; /* time of each button's latest press edge */

static void gpio_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN | RCC_IOPENR_GPIOFEN;
    (void)RCC->IOPENR;

    /* Unused and spare pins: analog, the lowest-leakage state. */
    pin_config(GPIOA, PINS_GPIOA_ANALOG, LL_GPIO_MODE_ANALOG, LL_GPIO_PULL_NO, 0);
    pin_config(GPIOB, PINS_GPIOB_ANALOG, LL_GPIO_MODE_ANALOG, LL_GPIO_PULL_NO, 0);
    pin_config(GPIOC, PINS_GPIOC_ANALOG, LL_GPIO_MODE_ANALOG, LL_GPIO_PULL_NO, 0);
#ifdef PINS_GPIOF_ANALOG
    pin_config(GPIOF, PINS_GPIOF_ANALOG, LL_GPIO_MODE_ANALOG, LL_GPIO_PULL_NO, 0);
#endif

    /* Joystick and RTC interrupt: inputs with pull-ups (pins.yaml). */
#define X(p, b) PIN_CFG(p, LL_GPIO_MODE_INPUT, LL_GPIO_PULL_UP, 0);
    JOY_FOR_EACH(X)
#undef X
    PIN_CFG(PIN_RTC_INT, LL_GPIO_MODE_INPUT, LL_GPIO_PULL_UP, 0);

    /* E-paper: load switch off first, then every panel line low so nothing
     * back-powers the unpowered panel; BUSY analog while the panel is off. */
    pin_write(PIN_PORT(PIN_EPD_PWR_N), PIN_MASK(PIN_EPD_PWR_N), true);
    PIN_CFG(PIN_EPD_PWR_N, LL_GPIO_MODE_OUTPUT, LL_GPIO_PULL_NO, 0);
    epd_lines_off();

    /* IR: LED off (TX low; the USART inverts, so idle is low too), receiver
     * unpowered, RX analog. */
    pin_write(PIN_PORT(PIN_IR_TX), PIN_MASK(PIN_IR_TX), false);
    PIN_CFG(PIN_IR_TX, LL_GPIO_MODE_OUTPUT, LL_GPIO_PULL_NO, 0);
    pin_write(PIN_PORT(PIN_IR_RX_PWR), PIN_MASK(PIN_IR_RX_PWR), false);
    PIN_CFG(PIN_IR_RX_PWR, LL_GPIO_MODE_OUTPUT, LL_GPIO_PULL_NO, 0);
    PIN_CFG(PIN_IR_RX, LL_GPIO_MODE_ANALOG, LL_GPIO_PULL_NO, 0);

    /* I2C: open drain, external 4.7k pull-ups. */
    LL_GPIO_SetPinOutputType(PIN_PORT(PIN_I2C_SCL), PIN_MASK(PIN_I2C_SCL), LL_GPIO_OUTPUT_OPENDRAIN);
    LL_GPIO_SetPinOutputType(PIN_PORT(PIN_I2C_SDA), PIN_MASK(PIN_I2C_SDA), LL_GPIO_OUTPUT_OPENDRAIN);
    PIN_CFG(PIN_I2C_SCL, LL_GPIO_MODE_ALTERNATE, LL_GPIO_PULL_NO, PIN_I2C_SCL_AF);
    PIN_CFG(PIN_I2C_SDA, LL_GPIO_MODE_ALTERNATE, LL_GPIO_PULL_NO, PIN_I2C_SDA_AF);

    /* Ports C and F hold only spare / dedicated pins: their configuration
     * is latched, so their clocks can go. */
    RCC->IOPENR &= ~(RCC_IOPENR_GPIOCEN | RCC_IOPENR_GPIOFEN);
}

static void exti_route(unsigned line, GPIO_TypeDef *port)
{
    unsigned shift = (line & 3u) * 8u;
    EXTI->EXTICR[line >> 2] = (EXTI->EXTICR[line >> 2] & ~(0xFFu << shift)) |
                              (PIN_PORT_INDEX(port) << shift);
}

static void exti_init(void)
{
#define X(p, b) exti_route(PIN_NUM(p), PIN_PORT(p));
    JOY_FOR_EACH(X)
#undef X
    exti_route(PIN_RTC_INT_NUM, PIN_RTC_INT_PORT);
    EXTI->RTSR1 &= ~PINS_EXTI_MASK;
    EXTI->FTSR1 |= PINS_EXTI_MASK;
    EXTI->FPR1 = PINS_EXTI_MASK;
    EXTI->RPR1 = PINS_EXTI_MASK;
    EXTI->IMR1 |= PINS_EXTI_MASK;
    NVIC_SetPriority(EXTI0_1_IRQn, 2);
    NVIC_SetPriority(EXTI2_3_IRQn, 2);
    NVIC_SetPriority(EXTI4_15_IRQn, 2);
    NVIC_EnableIRQ(EXTI0_1_IRQn);
    NVIC_EnableIRQ(EXTI2_3_IRQn);
    NVIC_EnableIRQ(EXTI4_15_IRQn);
}

static void exti_irq(void)
{
    uint32_t p = EXTI->FPR1 & PINS_EXTI_MASK;
    EXTI->FPR1 = p;
    uint8_t b = 0;
#define X(pin, btn) if (p & PIN_MASK(pin)) b |= (btn);
    JOY_FOR_EACH(X)
#undef X
    if (b) {
        btn_latched |= b;
        for (unsigned i = 0; i < BTN_COUNT; i++)
            if (b & (1u << i)) btn_edge_ms[i] = ms_ticks;
        board_wake_flags |= WAKE_BUTTON;
    }
    if (p & PIN_RTC_INT_MASK) board_wake_flags |= WAKE_RTC;
}

void EXTI0_1_IRQHandler(void) { exti_irq(); }
void EXTI2_3_IRQHandler(void) { exti_irq(); }
void EXTI4_15_IRQHandler(void) { exti_irq(); }

/* Press edges are latched by EXTI, so a press is never missed; the
 * filter (core/buttons.c) also keeps a second press made during a display
 * refresh from merging with the first. */
uint8_t hal_buttons(void)
{
    uint8_t raw = 0;
#define X(p, b) if (!pin_read(PIN_PORT(p), PIN_MASK(p))) raw |= (b);
    JOY_FOR_EACH(X)
#undef X
    uint32_t edge_ms[BTN_COUNT];
    __disable_irq();
    uint8_t latched = btn_latched;
    btn_latched = 0;
    for (unsigned i = 0; i < BTN_COUNT; i++) edge_ms[i] = btn_edge_ms[i];
    __enable_irq();
    bool again = false;
    uint8_t r = btn_filter(&btn_filter_state, raw, latched, edge_ms, ms_ticks, &again);
    if (again) {
        /* A press is held back one call: don't let the main loop idle. */
        __disable_irq();
        board_wake_flags |= WAKE_BUTTON;
        __enable_irq();
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* I2C1                                                               */

/* 100 kHz Standard mode from a 16 MHz kernel clock (RM0503 I2C timing
 * examples: PRESC 3, SCLDEL 4, SDADEL 2, SCLH 0x0F, SCLL 0x13). */
#define I2C_TIMING_100K_16M 0x30420F13u
#define I2C_TIMEOUT_MS 10u

static void i2c_init(void)
{
    RCC->APBENR1 |= RCC_APBENR1_I2C1EN;
    (void)RCC->APBENR1;
    I2C1->CR1 = 0;
    I2C1->TIMINGR = I2C_TIMING_100K_16M;
    I2C1->CR1 = I2C_CR1_PE;
}

static bool i2c_fail(void)
{
    board_i2c_errors++;
    if (!(I2C1->ISR & I2C_ISR_STOPF)) I2C1->CR2 |= I2C_CR2_STOP;
    wait_bits(&I2C1->ISR, I2C_ISR_BUSY, 0, I2C_TIMEOUT_MS);
    I2C1->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF;
    I2C1->CR1 &= ~I2C_CR1_PE; /* software reset of the state machine */
    (void)I2C1->CR1;
    I2C1->CR1 |= I2C_CR1_PE;
    return false;
}

/* Waits for `flag` in ISR; fails early on NACK. */
static bool i2c_wait(uint32_t flag)
{
    uint32_t start = ms_ticks;
    for (;;) {
        uint32_t isr = I2C1->ISR;
        if (isr & flag) return true;
        if (isr & (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO)) return false;
        if (ms_ticks - start > I2C_TIMEOUT_MS) return false;
    }
}

static void i2c_start(uint8_t addr7, size_t n, bool read, bool autoend)
{
    I2C1->CR2 = ((uint32_t)addr7 << 1) | ((uint32_t)n << I2C_CR2_NBYTES_Pos) |
                (read ? I2C_CR2_RD_WRN : 0) | (autoend ? I2C_CR2_AUTOEND : 0) | I2C_CR2_START;
}

bool i2c_write(uint8_t addr7, uint8_t reg, const uint8_t *data, size_t len)
{
    if (len > 254) return false;
    if (!wait_bits(&I2C1->ISR, I2C_ISR_BUSY, 0, I2C_TIMEOUT_MS)) return i2c_fail();
    i2c_start(addr7, len + 1, false, true);
    for (size_t i = 0; i <= len; i++) {
        if (!i2c_wait(I2C_ISR_TXIS)) return i2c_fail();
        I2C1->TXDR = i == 0 ? reg : data[i - 1];
    }
    if (!i2c_wait(I2C_ISR_STOPF)) return i2c_fail();
    I2C1->ICR = I2C_ICR_STOPCF;
    return true;
}

bool i2c_read(uint8_t addr7, uint8_t reg, uint8_t *data, size_t len)
{
    if (len == 0 || len > 255) return false;
    if (!wait_bits(&I2C1->ISR, I2C_ISR_BUSY, 0, I2C_TIMEOUT_MS)) return i2c_fail();
    i2c_start(addr7, 1, false, false);
    if (!i2c_wait(I2C_ISR_TXIS)) return i2c_fail();
    I2C1->TXDR = reg;
    if (!i2c_wait(I2C_ISR_TC)) return i2c_fail();
    i2c_start(addr7, len, true, true); /* repeated start */
    for (size_t i = 0; i < len; i++) {
        if (!i2c_wait(I2C_ISR_RXNE)) return i2c_fail();
        data[i] = (uint8_t)I2C1->RXDR;
    }
    if (!i2c_wait(I2C_ISR_STOPF)) return i2c_fail();
    I2C1->ICR = I2C_ICR_STOPCF;
    return true;
}

/* ------------------------------------------------------------------ */
/* SPI1 (write only, 8-bit, mode 0, MSB first)                        */

void spi_open(void)
{
    RCC->APBENR2 |= RCC_APBENR2_SPI1EN;
    (void)RCC->APBENR2;
    SPI1->CR1 = 0;
    SPI1->CR2 = (7u << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH; /* 8-bit frames */
    /* fPCLK/2 = 8 MHz; the SSD1680 accepts up to 20 MHz on writes. */
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_SPE;
    PIN_CFG(PIN_EPD_SCK, LL_GPIO_MODE_ALTERNATE, LL_GPIO_PULL_NO, PIN_EPD_SCK_AF);
    PIN_CFG(PIN_EPD_SDI, LL_GPIO_MODE_ALTERNATE, LL_GPIO_PULL_NO, PIN_EPD_SDI_AF);
}

void spi_write(const uint8_t *data, size_t len)
{
    volatile uint8_t *dr = (volatile uint8_t *)&SPI1->DR;
    for (size_t i = 0; i < len; i++) {
        if (!wait_bits(&SPI1->SR, SPI_SR_TXE, SPI_SR_TXE, 5)) return;
        *dr = data[i];
        if (!wait_bits(&SPI1->SR, SPI_SR_RXNE, SPI_SR_RXNE, 5)) return;
        (void)*dr; /* keep the RX FIFO empty, no overrun */
    }
    wait_bits(&SPI1->SR, SPI_SR_BSY, 0, 5);
}

void spi_close(void)
{
    wait_bits(&SPI1->SR, SPI_SR_BSY, 0, 5);
    SPI1->CR1 = 0;
    RCC->APBENR2 &= ~RCC_APBENR2_SPI1EN;
    pin_write(PIN_PORT(PIN_EPD_SCK), PIN_MASK(PIN_EPD_SCK), false);
    pin_write(PIN_PORT(PIN_EPD_SDI), PIN_MASK(PIN_EPD_SDI), false);
    PIN_CFG(PIN_EPD_SCK, LL_GPIO_MODE_OUTPUT, LL_GPIO_PULL_NO, 0);
    PIN_CFG(PIN_EPD_SDI, LL_GPIO_MODE_OUTPUT, LL_GPIO_PULL_NO, 0);
}

/* ------------------------------------------------------------------ */
/* display, RTC                                                       */

void hal_display_show(const uint8_t *fb, display_mode_t mode)
{
    epd_show(fb, mode);
}

int64_t hal_rtc_get(void)
{
    return rv3032_get();
}

void hal_rtc_set(int64_t unix_time)
{
    rv3032_set(unix_time);
}

/* ------------------------------------------------------------------ */
/* IR: USART1, 4800 baud 8N1, TX and RX inverted (idle = LED off)     */

#define IR_BAUD 4800u
#define IR_RX_SIZE 256u /* powers of two */
#define IR_TX_SIZE 256u

static volatile uint8_t ir_rx_buf[IR_RX_SIZE];
static volatile uint32_t ir_rx_head, ir_rx_tail;
static volatile uint8_t ir_tx_buf[IR_TX_SIZE];
static volatile uint32_t ir_tx_head, ir_tx_tail;
static bool ir_on;

void hal_ir_enable(bool on)
{
    if (on == ir_on) return;
    if (on) {
        ir_rx_head = ir_rx_tail = 0;
        ir_tx_head = ir_tx_tail = 0;
        RCC->APBENR2 |= RCC_APBENR2_USART1EN;
        (void)RCC->APBENR2;
        USART1->CR1 = 0;
        USART1->CR2 = USART_CR2_TXINV | USART_CR2_RXINV; /* only while UE = 0 */
        USART1->CR3 = 0;
        USART1->BRR = (SYSCLK_HZ + IR_BAUD / 2u) / IR_BAUD;
        pin_write(PIN_PORT(PIN_IR_RX_PWR), PIN_MASK(PIN_IR_RX_PWR), true);
        PIN_CFG(PIN_IR_TX, LL_GPIO_MODE_ALTERNATE, LL_GPIO_PULL_NO, PIN_IR_TX_AF);
        PIN_CFG(PIN_IR_RX, LL_GPIO_MODE_ALTERNATE, LL_GPIO_PULL_NO, PIN_IR_RX_AF);
        USART1->ICR = 0xFFFFFFFFu;
        USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE;
        NVIC_SetPriority(USART1_IRQn, 1);
        NVIC_EnableIRQ(USART1_IRQn);
    } else {
        NVIC_DisableIRQ(USART1_IRQn);
        USART1->CR1 = 0;
        RCC->APBENR2 &= ~RCC_APBENR2_USART1EN;
        pin_write(PIN_PORT(PIN_IR_TX), PIN_MASK(PIN_IR_TX), false);
        PIN_CFG(PIN_IR_TX, LL_GPIO_MODE_OUTPUT, LL_GPIO_PULL_NO, 0);
        PIN_CFG(PIN_IR_RX, LL_GPIO_MODE_ANALOG, LL_GPIO_PULL_NO, 0);
        pin_write(PIN_PORT(PIN_IR_RX_PWR), PIN_MASK(PIN_IR_RX_PWR), false);
    }
    ir_on = on;
}

void hal_ir_send(const uint8_t *buf, size_t len)
{
    if (!ir_on) return;
    for (size_t i = 0; i < len; i++) {
        uint32_t next = (ir_tx_head + 1u) & (IR_TX_SIZE - 1u);
        if (next == ir_tx_tail) break; /* full: drop the rest, the protocol retransmits */
        ir_tx_buf[ir_tx_head] = buf[i];
        ir_tx_head = next;
    }
    USART1->CR1 |= USART_CR1_TXEIE_TXFNFIE;
}

void USART1_IRQHandler(void)
{
    uint32_t isr = USART1->ISR;
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE))
        USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    if (isr & USART_ISR_RXNE_RXFNE) {
        uint8_t c = (uint8_t)USART1->RDR;
        uint32_t next = (ir_rx_head + 1u) & (IR_RX_SIZE - 1u);
        if (next != ir_rx_tail) {
            ir_rx_buf[ir_rx_head] = c;
            ir_rx_head = next;
        }
        board_wake_flags |= WAKE_IR;
    }
    if ((USART1->CR1 & USART_CR1_TXEIE_TXFNFIE) && (isr & USART_ISR_TXE_TXFNF)) {
        if (ir_tx_tail != ir_tx_head) {
            USART1->TDR = ir_tx_buf[ir_tx_tail];
            ir_tx_tail = (ir_tx_tail + 1u) & (IR_TX_SIZE - 1u);
        } else {
            USART1->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
        }
    }
}

size_t board_ir_take(uint8_t *buf, size_t cap)
{
    size_t n = 0;
    while (n < cap && ir_rx_tail != ir_rx_head) {
        buf[n++] = ir_rx_buf[ir_rx_tail];
        ir_rx_tail = (ir_rx_tail + 1u) & (IR_RX_SIZE - 1u);
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* entropy: TRNG clocked from HSI48, both on only while in use        */

static bool rng_word(uint32_t *out)
{
    for (int tries = 0; tries < 8; tries++) {
        uint32_t start = ms_ticks;
        for (;;) {
            uint32_t sr = RNG->SR;
            if (sr & (RNG_SR_SECS | RNG_SR_SEIS | RNG_SR_CEIS)) {
                /* Seed or clock error: clear and restart the conditioning
                 * stage (RM0503 RNG error management). */
                RNG->SR = 0;
                RNG->CR |= RNG_CR_CONDRST;
                RNG->CR &= ~RNG_CR_CONDRST;
                wait_bits(&RNG->CR, RNG_CR_CONDRST, 0, 2);
                break;
            }
            if (sr & RNG_SR_DRDY) {
                uint32_t v = RNG->DR;
                if (v != 0) { /* 0 is what DR reads after a seed error */
                    *out = v;
                    return true;
                }
            }
            if (ms_ticks - start > 5) break;
        }
    }
    return false;
}

void hal_random(uint8_t *buf, size_t len)
{
    RCC->CRRCR |= RCC_CRRCR_HSI48ON;
    bool ok = wait_bits(&RCC->CRRCR, RCC_CRRCR_HSI48RDY, RCC_CRRCR_HSI48RDY, 5);
    RCC->CCIPR = (RCC->CCIPR & ~RCC_CCIPR_CLK48SEL) | RCC_CCIPR_CLK48SEL; /* HSI48 */
    RCC->AHBENR |= RCC_AHBENR_RNGEN;
    (void)RCC->AHBENR;
    /* Same bring-up as ST's HAL_RNG_Init: clock error detection on, reset
     * the conditioning stage, keep the reset (NIST) configuration. */
    RNG->CR = (RNG->CR & ~(RNG_CR_CED | RNG_CR_IE)) | RNG_CR_CONDRST;
    RNG->CR &= ~RNG_CR_CONDRST;
    ok = ok && wait_bits(&RNG->CR, RNG_CR_CONDRST, 0, 5);
    RNG->CR |= RNG_CR_RNGEN;
    size_t i = 0;
    while (ok && i < len) {
        uint32_t w;
        if (!rng_word(&w)) {
            ok = false;
            break;
        }
        for (int k = 0; k < 4 && i < len; k++, w >>= 8) buf[i++] = (uint8_t)w;
    }
    RNG->CR &= ~RNG_CR_RNGEN;
    RCC->AHBENR &= ~RCC_AHBENR_RNGEN;
    RCC->CRRCR &= ~RCC_CRRCR_HSI48ON;
    if (!ok) {
        /* Never hand out weak randomness: keys are generated from this. */
        memset(buf, 0, len);
        NVIC_SystemReset();
    }
}

/* ------------------------------------------------------------------ */
/* flash store: the 40 pages above the image (see stm32u083kc.ld)     */

extern const uint8_t __store_start[];
#define STORE_PAGES 40u
#define STORE_BYTES (STORE_PAGES * HAL_FLASH_PAGE)
#define FLASH_KEY1 0x45670123u
#define FLASH_KEY2 0xCDEF89ABu
#define FLASH_SR_ERRORS (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                         FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR)

uint32_t hal_flash_pages(void)
{
    return STORE_PAGES;
}

void hal_flash_read(uint32_t off, void *buf, size_t len)
{
    if (off > STORE_BYTES || len > STORE_BYTES - off) {
        memset(buf, 0xff, len);
        board_flash_errors++;
        return;
    }
    memcpy(buf, __store_start + off, len);
}

static bool flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
    return (FLASH->CR & FLASH_CR_LOCK) == 0;
}

static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

/* Waits out an operation and reports (then clears) any error flags. */
static bool flash_wait(void)
{
    bool ok = wait_bits(&FLASH->SR, FLASH_SR_BSY1 | FLASH_SR_CFGBSY, 0, 100);
    uint32_t sr = FLASH->SR;
    if (sr & FLASH_SR_ERRORS) ok = false;
    FLASH->SR = sr & (FLASH_SR_ERRORS | FLASH_SR_EOP); /* write 1 to clear */
    return ok;
}

static bool flash_prepare(void)
{
    if (!wait_bits(&FLASH->SR, FLASH_SR_BSY1 | FLASH_SR_CFGBSY, 0, 100)) return false;
    FLASH->SR = FLASH_SR_ERRORS | FLASH_SR_EOP; /* stale flags would block programming */
    return flash_unlock();
}

void hal_flash_erase(uint32_t page)
{
    if (page >= STORE_PAGES) {
        board_flash_errors++;
        return;
    }
    uint32_t pnb = ((uint32_t)__store_start - FLASH_BASE) / HAL_FLASH_PAGE + page;
    bool ok = flash_prepare();
    if (ok) {
        FLASH->CR = (FLASH->CR & ~(FLASH_CR_PNB | FLASH_CR_PG | FLASH_CR_MER1)) | FLASH_CR_PER |
                    (pnb << FLASH_CR_PNB_Pos);
        FLASH->CR |= FLASH_CR_STRT;
        ok = flash_wait();
        FLASH->CR &= ~(FLASH_CR_PER | FLASH_CR_PNB);
    }
    flash_lock();
    /* Verify: the store relies on erased bytes reading 0xFF. */
    const uint32_t *w = (const uint32_t *)(__store_start + page * HAL_FLASH_PAGE);
    for (uint32_t i = 0; ok && i < HAL_FLASH_PAGE / 4; i++) ok = w[i] == 0xFFFFFFFFu;
    if (!ok) board_flash_errors++;
}

void hal_flash_write(uint32_t off, const void *buf, size_t len)
{
    if ((off & 7u) || (len & 7u) || off > STORE_BYTES || len > STORE_BYTES - off) {
        board_flash_errors++;
        return;
    }
    const uint8_t *src = buf;
    bool ok = flash_prepare();
    for (size_t i = 0; ok && i < len; i += 8) {
        uint32_t w[2];
        memcpy(w, src + i, 8); /* the caller's buffer need not be aligned */
        if (w[0] == 0xFFFFFFFFu && w[1] == 0xFFFFFFFFu) continue; /* already erased */
        volatile uint32_t *dst = (volatile uint32_t *)(__store_start + off + i);
        FLASH->CR = (FLASH->CR & ~(FLASH_CR_PER | FLASH_CR_MER1)) | FLASH_CR_PG;
        dst[0] = w[0]; /* a double-word: both words, in order (RM0503) */
        dst[1] = w[1];
        ok = flash_wait();
        FLASH->CR &= ~FLASH_CR_PG;
        ok = ok && dst[0] == w[0] && dst[1] == w[1];
    }
    flash_lock();
    if (!ok) board_flash_errors++;
}

/* ------------------------------------------------------------------ */
/* battery: VDDA (= the cell) from the internal reference             */

uint16_t hal_battery_mv(void)
{
    static uint16_t cached;
    static uint32_t when;
    if (cached && ms_ticks - when < 10000u) return cached;

    RCC->APBENR2 |= RCC_APBENR2_ADCEN;
    (void)RCC->APBENR2;
    ADC1->CR = ADC_CR_ADVREGEN;
    board_delay_us(25); /* tADCVREG_STUP 20 us */
    ADC1->CFGR2 = ADC_CFGR2_CKMODE_0; /* PCLK/2, synchronous */
    ADC1->CR |= ADC_CR_ADCAL;
    bool ok = wait_bits(&ADC1->CR, ADC_CR_ADCAL, 0, 2);
    board_delay_us(2);
    ADC1_COMMON->CCR |= ADC_CCR_VREFEN;
    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    ok = ok && wait_bits(&ADC1->ISR, ADC_ISR_ADRDY, ADC_ISR_ADRDY, 2);
    ADC1->SMPR = 7u << ADC_SMPR_SMP1_Pos; /* 160.5 cycles = 20 us > ts_vrefint */
    ADC1->ISR = ADC_ISR_CCRDY;
    ADC1->CHSELR = 1u << 12;              /* VREFINT is channel 12 on the U0 */
    ok = ok && wait_bits(&ADC1->ISR, ADC_ISR_CCRDY, ADC_ISR_CCRDY, 2);
    board_delay_us(15);                   /* VREFINT start-up */
    uint32_t sum = 0;
    for (int i = 0; ok && i < 4; i++) {
        ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
        ADC1->CR |= ADC_CR_ADSTART;
        ok = wait_bits(&ADC1->ISR, ADC_ISR_EOC, ADC_ISR_EOC, 2);
        sum += ADC1->DR & 0xFFFu;
    }
    if (ADC1->CR & ADC_CR_ADEN) {
        ADC1->CR |= ADC_CR_ADDIS;
        wait_bits(&ADC1->CR, ADC_CR_ADEN, 0, 2);
    }
    ADC1_COMMON->CCR &= ~ADC_CCR_VREFEN;
    ADC1->CR = 0;
    RCC->APBENR2 &= ~RCC_APBENR2_ADCEN;

    uint32_t raw = sum / 4u;
    if (!ok || raw == 0) return cached ? cached : 3000;
    uint32_t mv = VREFINT_CAL_VREF * (uint32_t)(*VREFINT_CAL_ADDR) / raw;
    if (mv > 3600u) mv = 3600u;
    cached = (uint16_t)mv;
    when = ms_ticks;
    return cached;
}

/* ------------------------------------------------------------------ */
/* sleep: Stop 2, woken by the joystick (EXTI) or the RV-3032 INT      */

void hal_sleep(int64_t wake_at)
{
    hal_ir_enable(false);
    bool timer = false;
    if (wake_at > 0) {
        int64_t delta = wake_at - rv3032_get();
        if (delta <= 0) return;
        timer = rv3032_wake_in((uint32_t)(delta > 0x7FFFFFFF ? 0x7FFFFFFF : delta));
    }

    systick_stop(); /* SysTick does not run in Stop 2; don't let it pend */
    __disable_irq();
    /* A press between the core deciding to sleep and here must not be lost:
     * if one is latched, pending or still held, don't sleep at all. It is
     * then reported by hal_buttons() as the press that woke us. */
    uint8_t held = btn_latched;
#define X(p, b) if (!pin_read(PIN_PORT(p), PIN_MASK(p))) held |= (b);
    JOY_FOR_EACH(X)
#undef X
    if (EXTI->FPR1 & PINS_EXTI_MASK & ~PIN_RTC_INT_MASK) held |= 1;
    if (!held) {
        PWR->CR1 = (PWR->CR1 & ~(PWR_CR1_LPMS | PWR_CR1_LPR)) | PWR_CR1_LPMS_1; /* Stop 2 */
        PWR->CR3 |= PWR_CR3_ENULP; /* sample BOR/PVD in Stop 2: lower current */
        SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
        __DSB();
        __WFI(); /* PRIMASK set: wakes, then the pending EXTI ISR runs below */
        SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    }
    board_clock_init();
    systick_start();
    __enable_irq(); /* the EXTI handler latches the press here */

    if (timer) rv3032_wake_cancel();
    __disable_irq();
    board_wake_flags &= ~(WAKE_BUTTON | WAKE_RTC);
    __enable_irq();
}

/* ------------------------------------------------------------------ */
/* identity                                                           */

void hal_device_uid(uint8_t uid[12])
{
    memcpy(uid, (const void *)UID_BASE, 12);
}

/* ------------------------------------------------------------------ */

void board_init(void)
{
    board_clock_init();
    systick_start();
    gpio_init();
    exti_init();
    i2c_init();
    rv3032_init();
}
