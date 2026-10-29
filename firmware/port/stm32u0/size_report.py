#!/usr/bin/env python3
"""Flash/RAM usage of irl2fa.elf against the STM32U083KC budget.
SPDX-License-Identifier: AGPL-3.0-or-later"""
import subprocess
import sys

FLASH_BUDGET = 176 * 1024  # 256 KB minus the 80 KB record store
RAM_BUDGET = 40 * 1024


def main(elf):
    out = subprocess.run(["arm-none-eabi-size", "-A", "-d", elf], check=True,
                         capture_output=True, text=True).stdout
    sec = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1].isdigit():
            sec[parts[0]] = int(parts[1])
    flash = sum(sec.get(s, 0) for s in (".isr_vector", ".text", ".ARM.exidx", ".init_array", ".data"))
    static_ram = sec.get(".data", 0) + sec.get(".bss", 0)
    stack = RAM_BUDGET - static_ram
    print(f"flash: {flash:7d} B / {FLASH_BUDGET} B ({100 * flash / FLASH_BUDGET:5.1f} %)  "
          f"[text {sec.get('.text', 0)}, data {sec.get('.data', 0)}]")
    print(f"ram:   {static_ram:7d} B / {RAM_BUDGET} B ({100 * static_ram / RAM_BUDGET:5.1f} %)  "
          f"[data {sec.get('.data', 0)}, bss {sec.get('.bss', 0)}], {stack} B left for the stack")
    syms = subprocess.run(["arm-none-eabi-nm", "-S", "--size-sort", "-t", "d", elf], check=True,
                          capture_output=True, text=True).stdout.splitlines()
    big = [s.split() for s in syms if len(s.split()) == 4 and s.split()[2] in "bBdD"][-6:]
    print("largest RAM objects: " + ", ".join(f"{n} {int(sz)}" for _, sz, _, n in reversed(big)))
    return 0 if flash <= FLASH_BUDGET and static_ram <= RAM_BUDGET - 4096 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
