#!/usr/bin/env python3
"""Sends a signed firmware update to an IRL2FA over infrared.

    irl2fa_update.py --port /dev/ttyUSB0 irl2fa-v0.2.0.irl2fa

On the device: Settings -> Firmware update -> Receive an update (with the
PIN if one is set). Hold the adapter's LED and phototransistor to the
device's top edge, a few centimetres away, and run this. The device installs
the image, keeps every code and contact, and restarts into it (about three
minutes in all).

The link is the IRL2FA's own: 4800 baud 8N1, *inverted* (idle = LED off),
half duplex. A USB-UART adapter has to invert both lines in hardware (an
FT232R configured with FT_PROG's "Invert TXD" and "Invert RXD", or any
adapter through a 74LVC1G04 on each line); docs/firmware-updates.md has the
wiring of a ~$5 IR adapter, and how to use a plain USB-UART on the board's
test pads instead.

PORT is a serial device (needs pyserial), tcp://HOST:PORT (a raw socket,
e.g. an emulator's UART), or exec:COMMAND (a program speaking the link on
stdin/stdout; the tests use one).

The protocol (firmware/core/fwlink.h): OFFER with the 128-byte image header,
then DATA chunks of 128 bytes at whatever offset the receiver's STATUS asks
for. An interrupted transfer of the same image resumes where it stopped.

SPDX-License-Identifier: AGPL-3.0-or-later
"""
from __future__ import annotations

import argparse
import os
import select
import shlex
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sign_image  # noqa: E402

OFFER, DATA, STATUS = 0x21, 0x22, 0x23
CHUNK = 128
OFFER_S, RETRY_S, POLL_S = 0.6, 0.9, 2.0
SILENCE_S, INSTALL_S = 20.0, 120.0
RX_LISTEN, RX_RECEIVING, RX_COMPLETE, RX_DONE, RX_FAILED = range(5)
ERRORS = {1: "not an IRL2FA image", 2: "made for other hardware", 3: "needs a newer bootloader",
          4: "older than the firmware on the device", 5: "too big", 6: "not signed with the device's key",
          7: "flash error on the device", 8: "payload does not decompress", 9: "unsigned (development) image",
          10: "the device was built without a release key", 11: "cancelled on the device",
          12: "timed out", 13: "the device's battery is too low"}


def crc16(data: bytes) -> int:
    crc = 0xFFFF  # CRC-16/CCITT-FALSE
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def frame(ftype: int, payload: bytes) -> bytes:
    body = bytes([ftype, len(payload)]) + payload
    return b"I2" + body + crc16(body).to_bytes(2, "big")


class Parser:
    """'I' '2' type len payload crc16, as firmware/core/fwlink.c fwl_parse."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, data: bytes):
        for c in data:
            if not self.buf and c != ord("I"):
                continue
            if len(self.buf) == 1 and c != ord("2"):
                self.buf = bytearray(b"I") if c == ord("I") else bytearray()
                continue
            self.buf.append(c)
            if len(self.buf) >= 4:
                n = self.buf[3]
                if len(self.buf) == 6 + n:
                    body, crc = bytes(self.buf[2:4 + n]), int.from_bytes(self.buf[4 + n:6 + n], "big")
                    self.buf = bytearray()
                    if crc16(body) == crc:
                        yield body[0], body[2:]


# ---- transports: write(bytes), read(timeout) -> bytes ----

class SerialLink:
    def __init__(self, port, baud):
        try:
            import serial  # pyserial
        except ImportError:
            sys.exit("irl2fa_update: a serial port needs pyserial (pip install pyserial)")
        self.s = serial.Serial(port, baud, bytesize=8, parity="N", stopbits=1, timeout=0)

    def write(self, b):
        self.s.write(b)

    def read(self, timeout):
        end = time.monotonic() + timeout
        while True:
            data = self.s.read(4096)
            if data or time.monotonic() >= end:
                return data
            time.sleep(0.005)


class TcpLink:
    def __init__(self, hostport):
        host, port = hostport.rsplit(":", 1)
        self.s = socket.create_connection((host, int(port)))

    def write(self, b):
        self.s.sendall(b)

    def read(self, timeout):
        r, _, _ = select.select([self.s], [], [], timeout)
        return self.s.recv(4096) if r else b""


class ExecLink:
    def __init__(self, cmd):
        self.p = subprocess.Popen(shlex.split(cmd), stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def write(self, b):
        try:
            self.p.stdin.write(b)
            self.p.stdin.flush()
        except BrokenPipeError:
            pass  # the other end went away: like a device out of range


    def read(self, timeout):
        r, _, _ = select.select([self.p.stdout], [], [], timeout)
        return os.read(self.p.stdout.fileno(), 4096) if r else b""

    def close(self):
        try:
            self.p.stdin.close()
        except BrokenPipeError:
            pass
        self.p.wait(10)


def open_link(port, baud):
    if port.startswith("tcp://"):
        return TcpLink(port[6:])
    if port.startswith("exec:"):
        return ExecLink(port[5:])
    return SerialLink(port, baud)


# ---- the sender (firmware/core/fwlink.c fwtx_*) ----

def send(link, image: bytes, out=sys.stdout, airtime=True) -> int:
    hdr, payload = image[:128], image[128:]
    total = len(payload)
    parser = Parser()
    state, nxt = "offering", 0
    last_tx = -1e9
    last_rx = phase = time.monotonic()
    answered = False
    shown = -1

    def tx(b):
        nonlocal last_tx, answered
        link.write(b)
        last_tx, answered = time.monotonic(), False
        if airtime:
            time.sleep(len(b) * 10 / 4800)  # half duplex: let it leave the LED

    while True:
        now = time.monotonic()
        if state == "installing" and now - phase > INSTALL_S or state != "installing" and now - last_rx > SILENCE_S:
            print("\nirl2fa_update: the device went quiet (out of range, or not in update mode?)", file=out)
            return 2
        since = now - last_tx
        if state == "offering" and since >= OFFER_S:
            tx(frame(OFFER, hdr))
        elif state == "sending" and (answered or since >= RETRY_S):
            tx(frame(DATA, struct.pack("<I", nxt) + payload[nxt:nxt + CHUNK]))
        elif state == "installing" and since >= POLL_S:
            last = max(0, total - CHUNK)
            tx(frame(DATA, struct.pack("<I", last) + payload[last:last + CHUNK]))
        for ftype, pl in parser.feed(link.read(0.02)):
            if ftype != STATUS or len(pl) != 10:
                continue  # our own echo, or noise
            rx_state, err = pl[0], pl[1]
            nxt, floor = struct.unpack_from("<II", pl, 2)
            answered, last_rx = True, time.monotonic()
            if rx_state == RX_LISTEN:
                state = "offering"
            elif rx_state == RX_RECEIVING:
                state = "sending" if nxt <= total else "offering"
            elif rx_state == RX_COMPLETE:
                if state != "installing":
                    phase = time.monotonic()
                    print("\nall sent; the device is checking the signature and installing...", file=out)
                state = "installing"
            elif rx_state == RX_DONE:
                print("\ninstalled: the device restarts into the new firmware", file=out)
                return 0
            else:
                print(f"\nrefused by the device: {ERRORS.get(err, f'error {err}')}"
                      + (f" (it has v{sign_image.format_version(floor)})" if err == 4 else ""), file=out)
                return 1
        if state == "sending":
            pct = nxt * 100 // max(total, 1)
            if pct != shown:
                shown = pct
                out.write(f"\rsending {pct:3d} % ({nxt} of {total} bytes)")
                out.flush()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image", help=".irl2fa update image (release asset)")
    ap.add_argument("--port", required=True, help="serial device, tcp://HOST:PORT or exec:COMMAND")
    ap.add_argument("--baud", type=int, default=4800)
    ap.add_argument("--pub", default=str(Path(__file__).resolve().parents[1] / "keys/irl2fa-release.pub"),
                    help="public key to check the image against first (default: the release key)")
    ap.add_argument("--no-check", action="store_true", help="send even if the local check fails")
    a = ap.parse_args(argv)
    image = Path(a.image).read_bytes()
    h = sign_image.parse_header(image)
    ok, how = sign_image.check_image(image, sign_image.read_pub(a.pub))
    print(f"{a.image}: v{sign_image.format_version(h['version'])}, {len(image) - 128} bytes, "
          f"{'signature OK' if ok else 'NOT valid for ' + a.pub} ({how})")
    if not ok and not a.no_check:
        sys.exit("irl2fa_update: refusing to send an image the device would refuse (--no-check to try anyway)")
    link = open_link(a.port, a.baud)
    try:
        return send(link, image, airtime=not a.port.startswith("exec:"))
    finally:
        if isinstance(link, ExecLink):
            link.close()


if __name__ == "__main__":
    sys.exit(main())
