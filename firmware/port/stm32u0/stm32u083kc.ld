/* Linker script for the IRL2FA application on the STM32U083KC (256 KB
 * flash, 40 KB SRAM), started by the bootloader (boot/boot.ld).
 *
 * The application lives in the application slot (flash_layout.ld): the
 * slot's first 256 bytes hold the signed image header (added after the
 * link by tools/sign_image.py), the vector table follows at 0x08006900.
 * The link fails if the binary would not fit the slot. The record store,
 * the staging slot and the bootloader's journal are only reached through
 * the flash driver.
 * SPDX-License-Identifier: AGPL-3.0-or-later */

INCLUDE flash_layout.ld

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08006900, LENGTH = 96K - 0x100
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 40K - 48
}

/* Minimum stack the image must leave free (checked below). Argon2 and
 * X25519 are the deepest users, roughly 2 KB each. */
__min_stack_size = 6K;

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

ASSERT(ORIGIN(FLASH) == __app_slot_start + __app_offset, "vector table at the application slot + 0x100")
ASSERT(ORIGIN(FLASH) % 256 == 0, "VTOR needs the vector table 256-byte aligned (48 entries)")
ASSERT(__flash_image_end <= __app_slot_start + __app_slot_size, "application overlaps the staging slot")
ASSERT(__stack_top == __boot_debug, "the stack ends below the bootloader debug block and mailbox")
ASSERT(__store_start % 2048 == 0, "store must start on a flash page boundary")
ASSERT(__store_size == 40 * 2048, "store must be 40 pages (core/store.h)")
