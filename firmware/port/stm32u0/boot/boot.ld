/* Linker script for the IRL2FA bootloader: the first 16 KB of flash, all of
 * SRAM but the mailbox (flash_layout.ld).
 * SPDX-License-Identifier: AGPL-3.0-or-later */

INCLUDE flash_layout.ld

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 26K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 40K - 112
}

__min_stack_size = 4K;
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

ASSERT(ORIGIN(FLASH) == __boot_start && LENGTH(FLASH) == __boot_size, "bootloader region")
ASSERT(__flash_image_end <= __app_slot_start, "bootloader overlaps the application slot")
ASSERT(__stack_top == __app_debug, "the stack ends below the debug blocks and the mailbox")
