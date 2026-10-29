/* Reset handler and vector table for the STM32U083 (Cortex-M0+, 32 IRQs).
 * Written for IRL2FA; the IRQ order follows IRQn_Type in stm32u083xx.h.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdint.h>
#include <string.h>

#include "stm32u0xx.h"

extern uint32_t __data_load, __data_start, __data_end, __bss_start, __bss_end, __stack_top;
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

int main(void);
void Reset_Handler(void);
void Default_Handler(void);

void Reset_Handler(void)
{
    /* .data from flash, .bss to zero. Not memcpy/memset: they may live in
     * code that expects an initialised C environment. */
    uint32_t *src = &__data_load;
    for (uint32_t *dst = &__data_start; dst < &__data_end;) *dst++ = *src++;
    for (uint32_t *dst = &__bss_start; dst < &__bss_end;) *dst++ = 0;
    for (void (**f)(void) = __init_array_start; f < __init_array_end; f++) (*f)();
    main();
    for (;;) NVIC_SystemReset();
}

/* Unexpected interrupts and faults reset the device: a stuck authenticator is
 * worse than one that reboots (the store is power-fail safe). */
void Default_Handler(void)
{
    NVIC_SystemReset();
}

#define WEAK_ALIAS __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK_ALIAS;
void HardFault_Handler(void) WEAK_ALIAS;
void SVC_Handler(void) WEAK_ALIAS;
void PendSV_Handler(void) WEAK_ALIAS;
void SysTick_Handler(void) WEAK_ALIAS;
void WWDG_IWDG_IRQHandler(void) WEAK_ALIAS;
void PVD_PVM_IRQHandler(void) WEAK_ALIAS;
void RTC_TAMP_IRQHandler(void) WEAK_ALIAS;
void FLASH_ECC_IRQHandler(void) WEAK_ALIAS;
void RCC_CRS_IRQHandler(void) WEAK_ALIAS;
void EXTI0_1_IRQHandler(void) WEAK_ALIAS;
void EXTI2_3_IRQHandler(void) WEAK_ALIAS;
void EXTI4_15_IRQHandler(void) WEAK_ALIAS;
void USB_DRD_FS_IRQHandler(void) WEAK_ALIAS;
void DMA1_Channel1_IRQHandler(void) WEAK_ALIAS;
void DMA1_Channel2_3_IRQHandler(void) WEAK_ALIAS;
void DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX_OVR_IRQHandler(void) WEAK_ALIAS;
void ADC_COMP1_2_IRQHandler(void) WEAK_ALIAS;
void TIM1_BRK_UP_TRG_COM_IRQHandler(void) WEAK_ALIAS;
void TIM1_CC_IRQHandler(void) WEAK_ALIAS;
void TIM2_IRQHandler(void) WEAK_ALIAS;
void TIM3_IRQHandler(void) WEAK_ALIAS;
void TIM6_DAC_LPTIM1_IRQHandler(void) WEAK_ALIAS;
void TIM7_LPTIM2_IRQHandler(void) WEAK_ALIAS;
void TIM15_LPTIM3_IRQHandler(void) WEAK_ALIAS;
void TIM16_IRQHandler(void) WEAK_ALIAS;
void TSC_IRQHandler(void) WEAK_ALIAS;
void LCD_IRQHandler(void) WEAK_ALIAS;
void I2C1_IRQHandler(void) WEAK_ALIAS;
void I2C2_3_4_IRQHandler(void) WEAK_ALIAS;
void SPI1_IRQHandler(void) WEAK_ALIAS;
void SPI2_3_IRQHandler(void) WEAK_ALIAS;
void USART1_IRQHandler(void) WEAK_ALIAS;
void USART2_LPUART2_IRQHandler(void) WEAK_ALIAS;
void USART3_LPUART1_IRQHandler(void) WEAK_ALIAS;
void USART4_LPUART3_IRQHandler(void) WEAK_ALIAS;
void RNG_CRYP_IRQHandler(void) WEAK_ALIAS;

typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used))
const vector_t vector_table[16 + 32] = {
    (vector_t)&__stack_top,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    0, 0, 0, 0, 0, 0, 0,
    SVC_Handler,
    0, 0,
    PendSV_Handler,
    SysTick_Handler,
    WWDG_IWDG_IRQHandler,                    /* 0 */
    PVD_PVM_IRQHandler,
    RTC_TAMP_IRQHandler,
    FLASH_ECC_IRQHandler,
    RCC_CRS_IRQHandler,
    EXTI0_1_IRQHandler,                      /* 5 */
    EXTI2_3_IRQHandler,
    EXTI4_15_IRQHandler,
    USB_DRD_FS_IRQHandler,
    DMA1_Channel1_IRQHandler,
    DMA1_Channel2_3_IRQHandler,              /* 10 */
    DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX_OVR_IRQHandler,
    ADC_COMP1_2_IRQHandler,
    TIM1_BRK_UP_TRG_COM_IRQHandler,
    TIM1_CC_IRQHandler,
    TIM2_IRQHandler,                         /* 15 */
    TIM3_IRQHandler,
    TIM6_DAC_LPTIM1_IRQHandler,
    TIM7_LPTIM2_IRQHandler,
    TIM15_LPTIM3_IRQHandler,
    TIM16_IRQHandler,                        /* 20 */
    TSC_IRQHandler,
    LCD_IRQHandler,
    I2C1_IRQHandler,
    I2C2_3_4_IRQHandler,
    SPI1_IRQHandler,                         /* 25 */
    SPI2_3_IRQHandler,
    USART1_IRQHandler,
    USART2_LPUART2_IRQHandler,
    USART3_LPUART1_IRQHandler,
    USART4_LPUART3_IRQHandler,               /* 30 */
    RNG_CRYP_IRQHandler,
};

/* newlib-nano's snprintf never allocates, but keep the link honest: there is
 * no heap on this device. */
void *_sbrk(int incr);
void *_sbrk(int incr)
{
    (void)incr;
    return (void *)-1;
}
