#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Writes the committed seed corpora in firmware/fuzz/corpus/<target>/.

Each seed is a small, readable starting point that reaches a different part
of the code (the fuzzers mutate from there). Run after changing a harness's
input format: python3 firmware/fuzz/make_seeds.py
"""
import os
import re
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
_NTFS_BAD = re.compile(r'[<>:"/\\|?*]')

# fuzz_pairing command bytes
PEER, ECHO, FLIP, TRUNC = 0xB0, 0xB1, 0xB2, 0xB3
# fuzz_app ops: press index: 0 up, 1 down, 2 left, 3 right, 4 centre
UP, DOWN, LEFT, RIGHT, CENTER = (i << 3 for i in range(5))
PEER_STEP, REBOOT, SLEEP = 5, 6 | 8, 6


def write(target, name, data):
    assert not _NTFS_BAD.search(name) and not name.endswith((".", " ")), name
    d = os.path.join(HERE, "corpus", target)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), "wb") as fh:
        fh.write(bytes(data))


def pairing():
    rnd = bytes((i * 7 + 3) & 0xFF for i in range(48))
    write("pairing", "honest-exchange", rnd + bytes([PEER] * 6))
    write("pairing", "echo-then-peer", rnd + bytes([ECHO, ECHO, PEER, ECHO, PEER, PEER]))
    write("pairing", "tampered-reveal", rnd + bytes([PEER, FLIP, 12, 0x01, PEER, FLIP, 40, 0x80]))
    write("pairing", "truncated-frames", rnd + bytes([TRUNC, 10, PEER, TRUNC, 50, PEER, PEER]))
    noise = b"\x00I\x13I2\x09\xc8I2\x01\x20" + bytes(range(32)) + b"\x12\x34I2\x02\x38"
    write("pairing", "line-noise", rnd + noise + bytes([PEER, PEER, PEER]))


def base32():
    for name, cap, text in (
        ("rfc-secret", 64, b"GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ"),
        ("lower-spaces-dashes", 64, b"gezd gnbv-gy3t qojq===="),
        ("exactly-full", 10, b"GEZDGNBVGY3TQOJQ"),
        ("too-long", 4, b"GEZDGNBVGY3TQOJQ"),
        ("invalid-char", 64, b"GEZDG1BVGY"),
        ("empty", 8, b""),
    ):
        write("base32", name, bytes([cap]) + text)


def store():
    write("store", "healthy-no-pin", [0])
    write("store", "healthy-pin-two-fails", [1])
    write("store", "interrupted-journal", [2])
    write("store", "blank", [3])
    write("store", "set-pin-after", [4])
    # flip a bit in the meta magic, in a slot's ciphertext, in the journal header
    write("store", "bitflips", [0] + [0, 0, 0, 1] + [0, 0x20, 0x10, 0x40] + [0, 0x08, 0x00, 0x80])
    # a forged journal header that names the meta page, over a blank data page
    hdr = struct.pack("<IIII", 0x4A4C5249, 0, 0, 0xFFFFFFFF)
    off = 0x800000 | 2048
    write("store", "forged-journal", [3] + list(off.to_bytes(3, "big")) + [len(hdr)] + list(hdr))
    # a PIN log full of tries
    tries = struct.pack("<Q", 0xF0F0F0F0) * 12
    off = 0x800000 | (3 * 2048)
    write("store", "pin-log-full-of-tries", [1] + list(off.to_bytes(3, "big")) + [len(tries)] + list(tries))


def app():
    clock = 1  # odd first byte: the clock is set
    nav_add = [DOWN, DOWN, DOWN, CENTER]
    label = [RIGHT, DOWN, RIGHT, CENTER]
    secret = [RIGHT] * 16 + [CENTER]
    write("app", "add-code", [clock] + nav_add + label + secret + [CENTER, LEFT, DOWN, UP, CENTER])
    write("app", "add-code-8-digits-sha512",
          [clock] + nav_add + label + secret + [RIGHT, DOWN, RIGHT, DOWN, LEFT, CENTER, 3, 200])
    pair = [DOWN, DOWN, CENTER] + [PEER_STEP] * 8 + [CENTER, RIGHT, CENTER]
    write("app", "pair-and-name", [clock] + pair + [3, 100, DOWN, CENTER, CENTER])
    write("app", "pair-reject-sas", [clock] + [DOWN, DOWN, CENTER] + [PEER_STEP] * 8 + [LEFT, LEFT])
    pin = [UP, RIGHT, DOWN, LEFT, CENTER]
    settings_pin = [DOWN] * 4 + [CENTER, DOWN, DOWN, CENTER] + pin + pin + [CENTER]
    write("app", "set-pin-reboot-unlock", [clock] + settings_pin + [REBOOT] + pin)
    write("app", "set-pin-wrong-pins", [clock] + settings_pin + [REBOOT] + [UP, UP, UP, UP, CENTER] * 4)
    write("app", "first-boot-set-time", [0, UP, RIGHT, UP, UP, RIGHT, DOWN, CENTER, CENTER])
    write("app", "time-zone-and-about", [clock] + [DOWN] * 4 + [CENTER, DOWN, CENTER, UP, UP, CENTER,
                                                            DOWN, DOWN, DOWN, CENTER, LEFT])
    write("app", "power-cut-while-saving", [clock] + nav_add + label + secret + [7, 3, CENTER, REBOOT])
    write("app", "sleep-and-wake", [clock, SLEEP, CENTER, 3 | 8, 2, CENTER])
    write("app", "ir-noise", [clock, DOWN, DOWN, CENTER, 4, 12] + list(b"I2\x01\x20" + bytes(8)) + [PEER_STEP])


if __name__ == "__main__":
    pairing()
    base32()
    store()
    app()
