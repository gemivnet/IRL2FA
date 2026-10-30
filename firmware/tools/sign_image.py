#!/usr/bin/env python3
"""Signed IRL2FA firmware images: keys, signing, checking, factory images.

    sign_image.py keygen  --out KEY [--pub PUB]        new Ed25519 key (KEY is secret, mode 600)
    sign_image.py pubkey  --key KEY                    print the public key (hex)
    sign_image.py sign    --key KEY --in irl2fa.bin --out irl2fa.irl2fa [--version 0.1.0]
    sign_image.py sign    --unsigned --in irl2fa.bin --out dev.irl2fa     development image
    sign_image.py verify  --pub PUB --in irl2fa.irl2fa [--bin irl2fa.bin]
    sign_image.py info    --in irl2fa.irl2fa
    sign_image.py factory --boot boot.bin --image irl2fa.irl2fa --out factory.bin [--hex factory.hex]

KEY is a file holding the 32-byte Ed25519 seed as 64 hex digits, or `env:NAME`
for an environment variable holding the same (the release workflow's
IRL2FA_SIGNING_KEY secret). PUB is 64 hex digits, as in
firmware/keys/irl2fa-release.pub; the bootloader build embeds it.

The image format (header, signature over SHA-512(header[0:64] || binary),
LZMA payload) is documented in docs/firmware-updates.md and implemented on
the device by firmware/core/fwimage.c. Ed25519 here is the RFC 8032 reference
algorithm in pure Python (no dependency; signing happens once per release,
so speed and timing side channels on the build machine do not matter).

SPDX-License-Identifier: AGPL-3.0-or-later
"""
from __future__ import annotations

import argparse
import hashlib
import lzma
import os
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# ---- flash layout (firmware/port/stm32u0/flash_layout.h) ----
FLASH_BASE = 0x08000000
BOOT_SIZE = 0x6800
APP_ADDR = 0x6800
APP_SIZE = 0x18000
STAGING_ADDR = 0x1E800
STAGING_SIZE = 0xC800

# ---- image format (firmware/core/fwimage.h) ----
MAGIC = 0x57463249
HDR_VERSION = 1
HDR_SIZE = 128
HDR_SIGNED = 64
APP_OFFSET = 256
FLAG_UNSIGNED = 1
COMP_STORED, COMP_LZMA, COMP_LZMA_THUMB = 0, 1, 2
HW_ID = 0x00830001          # STM32U083KC on the IRL2FA board (port/stm32u0/flash_layout.h)
MIN_BOOT = 1
LZMA_LC, LZMA_LP, LZMA_PB, LZMA_DICT_LOG2 = 0, 1, 1, 13

# ---- Ed25519 (RFC 8032 section 6 reference code) ----
_p = 2**255 - 19
_L = 2**252 + 27742317777372353535851937790883648493
_d = -121665 * pow(121666, _p - 2, _p) % _p
_SQRT_M1 = pow(2, (_p - 1) // 4, _p)


def _add(P, Q):
    A = (P[1] - P[0]) * (Q[1] - Q[0]) % _p
    B = (P[1] + P[0]) * (Q[1] + Q[0]) % _p
    C = 2 * P[3] * Q[3] * _d % _p
    D = 2 * P[2] * Q[2] % _p
    E, F, G, H = B - A, D - C, D + C, B + A
    return (E * F % _p, G * H % _p, F * G % _p, E * H % _p)


def _mul(s, P):
    Q = (0, 1, 1, 0)
    while s > 0:
        if s & 1:
            Q = _add(Q, P)
        P = _add(P, P)
        s >>= 1
    return Q


def _equal(P, Q):
    return (P[0] * Q[2] - Q[0] * P[2]) % _p == 0 and (P[1] * Q[2] - Q[1] * P[2]) % _p == 0


def _recover_x(y, sign):
    if y >= _p:
        return None
    x2 = (y * y - 1) * pow(_d * y * y + 1, _p - 2, _p)
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (_p + 3) // 8, _p)
    if (x * x - x2) % _p != 0:
        x = x * _SQRT_M1 % _p
    if (x * x - x2) % _p != 0:
        return None
    if (x & 1) != sign:
        x = _p - x
    return x


_gy = 4 * pow(5, _p - 2, _p) % _p
_gx = _recover_x(_gy, 0)
_G = (_gx, _gy, 1, _gx * _gy % _p)


def _compress(P):
    zinv = pow(P[2], _p - 2, _p)
    x, y = P[0] * zinv % _p, P[1] * zinv % _p
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def _decompress(s):
    if len(s) != 32:
        return None
    y = int.from_bytes(s, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    return None if x is None else (x, y, 1, x * y % _p)


def _h(*parts):
    return int.from_bytes(hashlib.sha512(b"".join(parts)).digest(), "little")


def _expand(seed):
    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def ed25519_public(seed: bytes) -> bytes:
    a, _ = _expand(seed)
    return _compress(_mul(a, _G))


def ed25519_sign(seed: bytes, msg: bytes) -> bytes:
    a, prefix = _expand(seed)
    A = _compress(_mul(a, _G))
    r = _h(prefix, msg) % _L
    R = _compress(_mul(r, _G))
    k = _h(R, A, msg) % _L
    return R + int.to_bytes((r + k * a) % _L, 32, "little")


def ed25519_verify(pub: bytes, msg: bytes, sig: bytes) -> bool:
    A = _decompress(pub)
    R = _decompress(sig[:32])
    if A is None or R is None or len(sig) != 64:
        return False
    s = int.from_bytes(sig[32:], "little")
    if s >= _L:
        return False
    k = _h(sig[:32], pub, msg) % _L
    return _equal(_mul(s, _G), _add(R, _mul(k, A)))


# ---- keys ----

def read_seed(spec: str) -> bytes:
    if spec.startswith("env:"):
        text = os.environ.get(spec[4:], "")
        if not text:
            sys.exit(f"sign_image: environment variable {spec[4:]} is empty")
    else:
        text = Path(spec).expanduser().read_text()
    text = text.strip()
    if not re.fullmatch(r"[0-9a-fA-F]{64}", text):
        sys.exit("sign_image: a key is 64 hex digits (the Ed25519 seed)")
    return bytes.fromhex(text)


def read_pub(spec: str) -> bytes:
    text = Path(spec).read_text().split("#")[0].strip() if not re.fullmatch(r"[0-9a-fA-F]{64}", spec) else spec
    if not re.fullmatch(r"[0-9a-fA-F]{64}", text):
        sys.exit(f"sign_image: {spec}: a public key is 64 hex digits")
    return bytes.fromhex(text)


# ---- images ----

def parse_version(text: str) -> int:
    m = re.fullmatch(r"v?(\d+)\.(\d+)\.(\d+)", text.strip())
    if not m:
        sys.exit(f"sign_image: version {text!r} is not X.Y.Z")
    major, minor, patch = (int(g) for g in m.groups())
    if minor > 255 or patch > 255 or major > 255:
        sys.exit("sign_image: version parts must be 0-255")
    return (major << 16) | (minor << 8) | patch


def app_version() -> str:
    """IRL2FA_VERSION from firmware/core/app.h: the version a build reports."""
    m = re.search(r'#define IRL2FA_VERSION "([^"]+)"', (ROOT / "firmware/core/app.h").read_text())
    return m.group(1)


def format_version(v: int) -> str:
    return f"{v >> 16}.{(v >> 8) & 0xFF}.{v & 0xFF}"


def lzma_filters(comp, props=None, dict_log2=LZMA_DICT_LOG2):
    """xz's ARM-Thumb BCJ filter (COMP_LZMA_THUMB) then raw LZMA1."""
    if props is None:
        lc, lp, pb = LZMA_LC, LZMA_LP, LZMA_PB
    else:
        lc, lp, pb = props % 9, (props // 9) % 5, props // 45
    lz = {"id": lzma.FILTER_LZMA1, "lc": lc, "lp": lp, "pb": pb, "dict_size": 1 << dict_log2}
    return ([{"id": lzma.FILTER_ARMTHUMB}] if comp == COMP_LZMA_THUMB else []) + [lz]


def compress(binary: bytes, comp=None) -> bytes:
    if comp == COMP_LZMA_THUMB:
        f = lzma_filters(comp)
        f[-1]["preset"] = 9 | lzma.PRESET_EXTREME
        return lzma.compress(binary, format=lzma.FORMAT_RAW, filters=f)
    filters = [{"id": lzma.FILTER_LZMA1, "preset": 9 | lzma.PRESET_EXTREME,
                "lc": LZMA_LC, "lp": LZMA_LP, "pb": LZMA_PB, "dict_size": 1 << LZMA_DICT_LOG2}]
    return lzma.compress(binary, format=lzma.FORMAT_RAW, filters=filters)


def header_fields(version, image_size, payload_size, comp, flags=0, hw_id=HW_ID, min_boot=MIN_BOOT):
    props = (LZMA_PB * 5 + LZMA_LP) * 9 + LZMA_LC if comp != COMP_STORED else 0
    dict_log2 = LZMA_DICT_LOG2 if comp != COMP_STORED else 0
    return struct.pack("<IHHIIIIIBBBB", MAGIC, HDR_VERSION, flags, version, hw_id, min_boot,
                       image_size, payload_size, comp, props, dict_log2, 0) + bytes(32)


def make_image(binary: bytes, version: int, seed: bytes | None, comp=COMP_LZMA_THUMB, hw_id=HW_ID,
               min_boot=MIN_BOOT) -> bytes:
    payload = binary if comp == COMP_STORED else compress(binary, comp)
    flags = 0 if seed else FLAG_UNSIGNED
    fields = header_fields(version, len(binary), len(payload), comp, flags, hw_id, min_boot)
    digest = hashlib.sha512(fields + binary).digest()
    sig = ed25519_sign(seed, digest) if seed else digest
    return fields + sig + payload


def parse_header(image: bytes) -> dict:
    if len(image) < HDR_SIZE:
        raise ValueError("shorter than a header")
    (magic, hv, flags, version, hw_id, min_boot, image_size, payload_size, comp, props, dict_log2,
     _) = struct.unpack_from("<IHHIIIIIBBBB", image)
    if magic != MAGIC or hv != HDR_VERSION:
        raise ValueError("not an IRL2FA firmware image")
    return {"flags": flags, "version": version, "hw_id": hw_id, "min_boot": min_boot,
            "image_size": image_size, "payload_size": payload_size, "comp": comp, "props": props,
            "dict_log2": dict_log2, "sig": image[HDR_SIGNED:HDR_SIZE]}


def unpack_binary(image: bytes) -> bytes:
    h = parse_header(image)
    payload = image[HDR_SIZE:HDR_SIZE + h["payload_size"]]
    if len(payload) != h["payload_size"]:
        raise ValueError("truncated payload")
    if h["comp"] == COMP_STORED:
        return payload
    filters = lzma_filters(h["comp"], h["props"], h["dict_log2"])
    dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=filters)
    return dec.decompress(payload, max_length=h["image_size"])


def check_image(image: bytes, pub: bytes | None) -> tuple[bool, str]:
    h = parse_header(image)
    binary = unpack_binary(image)
    if len(binary) != h["image_size"]:
        return False, "payload does not decompress to image_size bytes"
    digest = hashlib.sha512(image[:HDR_SIGNED] + binary).digest()
    if h["flags"] & FLAG_UNSIGNED:
        return digest == h["sig"], "unsigned development image (digest only)"
    if pub is None:
        return False, "signed image, no public key given"
    return ed25519_verify(pub, digest, h["sig"]), "Ed25519 signature"


def factory(boot: bytes, image: bytes, with_staging: bool) -> bytes:
    """Bootloader + application slot (+ the update image in staging, so the
    device can send its own firmware to another): what goes on a new chip
    over SWD."""
    if len(boot) > BOOT_SIZE:
        raise ValueError(f"bootloader is {len(boot)} bytes, its region {BOOT_SIZE}")
    h = parse_header(image)
    binary = unpack_binary(image)
    if APP_OFFSET + len(binary) > APP_SIZE:
        raise ValueError("application does not fit its slot")
    end = STAGING_ADDR + len(image) if with_staging else APP_ADDR + APP_OFFSET + len(binary)
    end = (end + 7) & ~7
    out = bytearray(b"\xff" * end)
    out[:len(boot)] = boot
    out[APP_ADDR:APP_ADDR + HDR_SIZE] = image[:HDR_SIZE]
    out[APP_ADDR + APP_OFFSET:APP_ADDR + APP_OFFSET + len(binary)] = binary
    if with_staging:
        if h["flags"] & FLAG_UNSIGNED:
            raise ValueError("an unsigned image cannot be staged (it would never install)")
        if len(image) > STAGING_SIZE:
            raise ValueError("update image does not fit the staging slot")
        out[STAGING_ADDR:STAGING_ADDR + len(image)] = image
    return bytes(out)


def to_ihex(data: bytes, base: int) -> str:
    """Intel HEX, skipping runs of erased (0xFF) 16-byte lines."""
    lines, upper = [], None
    for off in range(0, len(data), 16):
        chunk = data[off:off + 16]
        if chunk == b"\xff" * len(chunk):
            continue
        addr = base + off
        if addr >> 16 != upper:
            upper = addr >> 16
            rec = bytes([2, 0, 0, 4]) + upper.to_bytes(2, "big")
            lines.append(":" + rec.hex().upper() + f"{(-sum(rec)) & 0xFF:02X}")
        rec = bytes([len(chunk)]) + (addr & 0xFFFF).to_bytes(2, "big") + b"\x00" + chunk
        lines.append(":" + rec.hex().upper() + f"{(-sum(rec)) & 0xFF:02X}")
    lines.append(":00000001FF")
    return "\n".join(lines) + "\n"


# ---- command line ----

def cmd_keygen(a):
    out = Path(a.out).expanduser()
    if out.exists():
        sys.exit(f"sign_image: {out} exists; refusing to overwrite a key")
    out.parent.mkdir(parents=True, exist_ok=True)
    seed = os.urandom(32)
    fd = os.open(out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(seed.hex() + "\n")
    pub = ed25519_public(seed).hex()
    if a.pub:
        Path(a.pub).write_text(pub + "\n")
    print(f"secret key: {out} (mode 600; keep it off any repository)")
    print(f"public key: {pub}")


def cmd_pubkey(a):
    print(ed25519_public(read_seed(a.key)).hex())


def cmd_sign(a):
    binary = Path(a.input).read_bytes()
    version = parse_version(a.version or app_version())
    seed = None if a.unsigned else read_seed(a.key) if a.key else sys.exit("sign_image: --key or --unsigned")
    image = make_image(binary, version, seed, COMP_STORED if a.stored else COMP_LZMA_THUMB,
                       int(a.hw_id, 0), a.min_boot)
    if HDR_SIZE + parse_header(image)["payload_size"] > STAGING_SIZE:
        sys.exit(f"sign_image: ERROR: the update image is {len(image)} bytes, {len(image) - STAGING_SIZE} "
                 f"more than the {STAGING_SIZE}-byte staging slot. The flash layout is fixed: make the "
                 f"firmware smaller (docs/firmware-updates.md#the-flash-budget)")
    Path(a.out).write_bytes(image)
    kind = "unsigned development image" if a.unsigned else f"signed, key {ed25519_public(seed).hex()[:16]}..."
    print(f"{a.out}: v{format_version(version)}, {len(binary)} -> {len(image) - HDR_SIZE} bytes, {kind}")


def cmd_verify(a):
    image = Path(a.input).read_bytes()
    ok, how = check_image(image, read_pub(a.pub) if a.pub else None)
    if ok and a.bin and unpack_binary(image) != Path(a.bin).read_bytes():
        ok, how = False, "does not match " + a.bin
    print(f"{a.input}: {'OK' if ok else 'BAD'} ({how})")
    return 0 if ok else 1


def cmd_info(a):
    h = parse_header(Path(a.input).read_bytes())
    print(f"version {format_version(h['version'])}  hw 0x{h['hw_id']:08x}  min boot {h['min_boot']}  "
          f"binary {h['image_size']} B  payload {h['payload_size']} B  "
          f"{('stored', 'LZMA', 'Thumb+LZMA')[min(h['comp'], 2)]}  "
          f"{'UNSIGNED' if h['flags'] & FLAG_UNSIGNED else 'signed'}")


def cmd_factory(a):
    image = Path(a.image).read_bytes()
    staged = not (parse_header(image)["flags"] & FLAG_UNSIGNED) and not a.no_staging
    data = factory(Path(a.boot).read_bytes(), image, staged)
    Path(a.out).write_bytes(data)
    if a.hex:
        Path(a.hex).write_text(to_ihex(data, FLASH_BASE))
    print(f"{a.out}: {len(data)} bytes from 0x{FLASH_BASE:08x}" + (", update image staged" if staged else ""))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("keygen")
    p.add_argument("--out", required=True)
    p.add_argument("--pub")
    p.set_defaults(fn=cmd_keygen)
    p = sub.add_parser("pubkey")
    p.add_argument("--key", required=True)
    p.set_defaults(fn=cmd_pubkey)
    p = sub.add_parser("sign")
    p.add_argument("--key")
    p.add_argument("--unsigned", action="store_true")
    p.add_argument("--in", dest="input", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--version", help="X.Y.Z (default: IRL2FA_VERSION in firmware/core/app.h)")
    p.add_argument("--hw-id", default=hex(HW_ID))
    p.add_argument("--min-boot", type=int, default=MIN_BOOT)
    p.add_argument("--stored", action="store_true", help="no compression (tests)")
    p.set_defaults(fn=cmd_sign)
    p = sub.add_parser("verify")
    p.add_argument("--pub")
    p.add_argument("--in", dest="input", required=True)
    p.add_argument("--bin")
    p.set_defaults(fn=cmd_verify)
    p = sub.add_parser("info")
    p.add_argument("--in", dest="input", required=True)
    p.set_defaults(fn=cmd_info)
    p = sub.add_parser("factory")
    p.add_argument("--boot", required=True)
    p.add_argument("--image", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--hex")
    p.add_argument("--no-staging", action="store_true")
    p.set_defaults(fn=cmd_factory)
    a = ap.parse_args(argv)
    return a.fn(a) or 0


if __name__ == "__main__":
    sys.exit(main())
