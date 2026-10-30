#!/usr/bin/env python3
"""Flash/RAM usage of the application and the bootloader against the
STM32U083KC's flash map (flash_layout.h), and the update image against the
staging slot.

    size_report.py irl2fa.elf [boot.elf [irl2fa-dev.irl2fa]]
SPDX-License-Identifier: AGPL-3.0-or-later"""
import subprocess
import sys

APP_BUDGET = 96 * 1024 - 256     # application slot less the image header
BOOT_BUDGET = 26 * 1024          # bootloader region
STAGING_BUDGET = 50 * 1024       # staging slot: header + LZMA payload
RAM_BUDGET = 40 * 1024 - 112     # less the debug blocks and the bootloader mailbox


def sections(elf):
    out = subprocess.run(["arm-none-eabi-size", "-A", "-d", elf], check=True,
                         capture_output=True, text=True).stdout
    sec = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1].isdigit():
            sec[parts[0]] = int(parts[1])
    flash = sum(sec.get(s, 0) for s in (".isr_vector", ".text", ".ARM.exidx", ".init_array", ".data"))
    return sec, flash, sec.get(".data", 0) + sec.get(".bss", 0)


DOC = "docs/firmware-updates.md#the-flash-budget"
FIXED = ("The flash layout is fixed (the 80 KB record store stays): make the firmware "
         "smaller. See " + DOC + ".")


def overflow(what, have, limit):
    print(f"\nERROR: {what} is {have} bytes, {have - limit} more than its {limit}. {FIXED}\n",
          file=sys.stderr)


def main(elf, boot=None, image=None):
    sec, flash, static_ram = sections(elf)
    stack = RAM_BUDGET - static_ram
    ok = flash <= APP_BUDGET and static_ram <= RAM_BUDGET - 4096
    if flash > APP_BUDGET:
        overflow("the application", flash, APP_BUDGET)
    if static_ram > RAM_BUDGET - 4096:
        overflow("the application's static RAM", static_ram, RAM_BUDGET - 4096)
    print(f"flash: {flash:7d} B / {APP_BUDGET} B ({100 * flash / APP_BUDGET:5.1f} %)  "
          f"[text {sec.get('.text', 0)}, data {sec.get('.data', 0)}]  application slot")
    print(f"ram:   {static_ram:7d} B / {RAM_BUDGET} B ({100 * static_ram / RAM_BUDGET:5.1f} %)  "
          f"[data {sec.get('.data', 0)}, bss {sec.get('.bss', 0)}], {stack} B left for the stack")
    syms = subprocess.run(["arm-none-eabi-nm", "-S", "--size-sort", "-t", "d", elf], check=True,
                          capture_output=True, text=True).stdout.splitlines()
    big = [s.split() for s in syms if len(s.split()) == 4 and s.split()[2] in "bBdD"][-6:]
    print("largest RAM objects: " + ", ".join(f"{n} {int(sz)}" for _, sz, _, n in reversed(big)))
    if boot:
        _, bflash, bram = sections(boot)
        ok = ok and bflash <= BOOT_BUDGET and bram <= RAM_BUDGET - 3072
        if bflash > BOOT_BUDGET:
            overflow("the bootloader", bflash, BOOT_BUDGET)
        print(f"boot:  {bflash:7d} B / {BOOT_BUDGET} B ({100 * bflash / BOOT_BUDGET:5.1f} %)  "
              f"bootloader, {bram} B RAM")
    if image:
        with open(image, "rb") as f:
            n = len(f.read())
        ok = ok and n <= STAGING_BUDGET
        if n > STAGING_BUDGET:
            overflow("the compressed update image", n, STAGING_BUDGET)
        print(f"image: {n:7d} B / {STAGING_BUDGET} B ({100 * n / STAGING_BUDGET:5.1f} %)  "
              "update image (LZMA) in the staging slot")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
