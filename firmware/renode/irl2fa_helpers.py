"""Robot Framework helpers for irl2fa.robot: decode Renode monitor output.
SPDX-License-Identifier: AGPL-3.0-or-later"""
import re
import struct


def _bytes(monitor_output):
    return bytes(int(h, 16) for h in re.findall(r"0x([0-9A-Fa-f]{2})\b", monitor_output))


def decode_debug_block(monitor_output):
    """Parses `sysbus ReadBytes <irl2fa_debug> 40` (struct irl2fa_debug in
    firmware/port/stm32u0/main.c) into a dict."""
    b = _bytes(monitor_output)
    if len(b) < 40:
        raise AssertionError(f"short debug block: {monitor_output!r}")
    magic, polls, sleeps, flash_errors, i2c_errors, epd_timeouts = struct.unpack("<6I", b[:24])
    return {
        "magic": magic, "polls": polls, "sleeps": sleeps, "flash_errors": flash_errors,
        "i2c_errors": i2c_errors, "epd_timeouts": epd_timeouts,
        "screen": b[24:40].split(b"\0", 1)[0].decode("ascii", "replace"),
    }


def monitor_int(monitor_output):
    """Renode prints integers as 0x...; returns the value."""
    m = re.search(r"0x[0-9A-Fa-f]+|-?\d+", monitor_output)
    if not m:
        raise AssertionError(f"no number in {monitor_output!r}")
    return int(m.group(0), 0)


def monitor_text(monitor_output):
    return monitor_output.strip()
