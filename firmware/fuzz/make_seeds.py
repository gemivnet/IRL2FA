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
SIMPLE = 6 | 16  # simple mode on, family PIN up up right right down down
HOLD_CENTER_11S = [2 | (4 << 3), 64 | 44]  # 44 x 250 ms
FAMILY = [UP, UP, RIGHT, RIGHT, DOWN, DOWN, CENTER]


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
    write("store", "simple-mode", [16])
    write("store", "simple-mode-pin-two-fails", [17, 0, 0, 0, 0])
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
    pair_named = [clock] + pair + [3, 100]
    write("app", "simple-mode-leave-with-family-pin",
          pair_named + [SIMPLE, UP, DOWN, LEFT, RIGHT, CENTER] + HOLD_CENTER_11S + FAMILY + [DOWN, CENTER])
    write("app", "simple-mode-wrong-family-pins",
          pair_named + [SIMPLE] + HOLD_CENTER_11S + [DOWN] * 6 + [CENTER] + [DOWN] * 6 + [CENTER]
          + [DOWN] * 6 + [CENTER] + [3 | 8, 1, LEFT, SLEEP, CENTER] + HOLD_CENTER_11S + FAMILY)
    write("app", "simple-mode-empty-reboot", [clock, SIMPLE, DOWN, REBOOT, UP] + HOLD_CENTER_11S + [LEFT])
    write("app", "ir-noise", [clock, DOWN, DOWN, CENTER, 4, 12] + list(b"I2\x01\x20" + bytes(8)) + [PEER_STEP])


def crc16(data):
    crc = 0xFFFF  # CRC-16/CCITT-FALSE, as core/pairing.c
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def frame(t, payload):
    body = bytes([t, len(payload)]) + bytes(payload)
    return b"I2" + body + crc16(body).to_bytes(2, "big")


def fwupdate():
    """fuzz_fwupdate: first byte = target (0 LZMA, 1 image, 2 IR receiver,
    3 journal, 4 sender). The image is the test fixture (signed with the
    public test key), so the receiver seed runs a whole install."""
    import sys
    sys.path.insert(0, os.path.join(HERE, "..", "tests", "data"))
    import make_fw_fixtures as fx
    plain = fx.sample_plain(fx.SAMPLE_SIZE)
    image = fx.sign_image.make_image(plain, fx.SAMPLE_VERSION, fx.TEST_SEED)
    hdr, payload = image[:128], image[128:]
    write("fwupdate", "lzma-sample", bytes([0, 54, 1]) + struct.pack("<H", fx.SAMPLE_SIZE) + payload)
    write("fwupdate", "image-sample", bytes([1]) + image)
    stream = frame(0x21, hdr)
    for off in range(0, len(payload), 128):
        stream += frame(0x22, struct.pack("<I", off) + payload[off:off + 128])
    write("fwupdate", "receive-sample", bytes([2]) + stream)
    write("fwupdate", "receive-noise", bytes([2]) + frame(0x21, hdr) + b"I2\x22\x05\x00\x00\x00" +
          frame(0x22, struct.pack("<I", 128) + payload[:128]) + frame(0x23, bytes(10)))
    rec = lambda t, v: (bytes([t]) + struct.pack("<I", v) + b"\xa5" +  # noqa: E731
                        crc16(bytes([t]) + struct.pack("<I", v) + b"\xa5").to_bytes(2, "big"))
    page = rec(1, 5) + rec(2, 0x100) + rec(3, 0x200) + b"\x00" * 8 + rec(4, 0x200)
    write("fwupdate", "journal", bytes([3]) + page + b"\xff" * (2048 - len(page)) + rec(1, 4))
    status = lambda st, nxt: frame(0x23, bytes([st, 0]) + struct.pack("<II", nxt, 0))  # noqa: E731
    write("fwupdate", "sender-replies", bytes([4]) + status(1, 0) + status(1, 128) + status(1, 256) +
          status(2, len(payload)) + status(3, len(payload)))


if __name__ == "__main__":
    pairing()
    base32()
    store()
    app()
    fwupdate()
