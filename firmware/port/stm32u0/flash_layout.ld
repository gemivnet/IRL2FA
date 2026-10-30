/* The flash map, for both linker scripts (flash_layout.h says the same for
 * C; tools/check_hw_constants.py checks they agree).
 * SPDX-License-Identifier: AGPL-3.0-or-later */
__flash_base = 0x08000000;
__boot_start = 0x08000000;
__boot_size = 26K;
__app_slot_start = 0x08006800;
__app_slot_size = 96K;
__app_offset = 0x100;          /* image header + padding (core/fwimage.h FW_APP_OFFSET) */
__staging_start = 0x0801E800;
__staging_size = 50K;
__log_start = 0x0802B000;
__log_size = 4K;
__store_start = 0x0802C000;
__store_size = 80K;
__mailbox = 0x20009FF0;        /* 16 bytes at the top of SRAM, kept by both programs */
__app_debug = 0x20009F90;      /* 64 bytes below that: the application's debug block (Renode tests) */
__boot_debug = 0x20009FD0;     /* 32 bytes below it: the bootloader's debug block (Renode tests) */
__ram_start = 0x20000000;
__ram_size = 40K;

ASSERT(__boot_start + __boot_size == __app_slot_start, "layout: boot | app")
ASSERT(__app_slot_start + __app_slot_size == __staging_start, "layout: app | staging")
ASSERT(__staging_start + __staging_size == __log_start, "layout: staging | journal")
ASSERT(__log_start + __log_size == __store_start, "layout: journal | store")
ASSERT(__store_start + __store_size == __flash_base + 256K, "layout: store ends the flash")
ASSERT(__mailbox + 16 == __ram_start + __ram_size, "layout: mailbox at the top of SRAM")
