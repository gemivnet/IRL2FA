/* Linker script for the STM32U083KC (256 KB flash, 40 KB SRAM).
 *
 * The top 80 KB of flash (40 x 2 KB pages, 0x0802C000-0x0803FFFF) belong to
 * the record store (core/store.c via hal_flash_*); code and constant data
 * must stay below it, so FLASH is only 176 KB long here and the link fails
 * if the image would not fit.
 * SPDX-License-Identifier: AGPL-3.0-or-later */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 176K
    STORE (r)   : ORIGIN = 0x0802C000, LENGTH = 80K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 40K
}

/* Minimum stack the image must leave free (checked below). Argon2 and
 * X25519 are the deepest users, roughly 2 KB each. */
__min_stack_size = 6K;

__store_start = ORIGIN(STORE);
__store_size = LENGTH(STORE);
__stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text .text.*)
        *(.rodata .rodata.*)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    __data_load = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM AT > FLASH

    /* Flash is programmed in 8-byte double-words. */
    __flash_image_end = LOADADDR(.data) + SIZEOF(.data);

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    .stack_check (NOLOAD) :
    {
        . = ALIGN(8);
        __heap_start = .;
        . = . + __min_stack_size;
    } > RAM

    /DISCARD/ :
    {
        *(.ARM.attributes)
    }
}

ASSERT(__flash_image_end <= ORIGIN(STORE), "firmware image overlaps the record store")
ASSERT(__store_start % 2048 == 0, "store must start on a flash page boundary")
ASSERT(__store_size == 40 * 2048, "store must be 40 pages (core/store.h)")
