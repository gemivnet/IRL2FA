#!/usr/bin/env python3
"""Factory images for the update tests (firmware/renode/update.robot), from
the release build in firmware/build/stm32u0:

    v1.bin     application signed with the TEST key as v0.1.0, its update image staged
    v2.bin     the same binary signed as v0.2.0
    other.bin  the same binary signed as v0.3.0 with a key the tests' devices
               do not trust (its own bootloader trusts it, so it can send it)

Each holds the shipped bootloader binary with the 32 bytes of the release
public key (firmware/keys/irl2fa-release.pub) replaced by the test key's:
the tests run the release bootloader, not a test build. The TEST key is
RFC 8032's TEST 1 key, public on purpose (firmware/tests/data).

    make_test_images.py BUILD_DIR OUT_DIR
SPDX-License-Identifier: AGPL-3.0-or-later
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "firmware/tools"))
import sign_image as si  # noqa: E402

TEST_SEED = bytes.fromhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")
OTHER_SEED = bytes(range(1, 33))


def main(build, out):
    build, out = Path(build), Path(out)
    out.mkdir(parents=True, exist_ok=True)
    boot = (build / "boot.bin").read_bytes()
    app = (build / "irl2fa.bin").read_bytes()
    release = bytes.fromhex((ROOT / "firmware/keys/irl2fa-release.pub").read_text().strip())
    if boot.count(release) != 1:
        sys.exit("make_test_images: the release key is not in boot.bin exactly once "
                 "(built with another IRL2FA_BOOT_PUBKEY?)")
    for name, version, seed in (("v1", "0.1.0", TEST_SEED), ("v2", "0.2.0", TEST_SEED),
                                ("other", "0.3.0", OTHER_SEED)):
        image = si.make_image(app, si.parse_version(version), seed)
        patched = boot.replace(release, si.ed25519_public(seed))
        (out / f"{name}.bin").write_bytes(si.factory(patched, image, with_staging=True))
        (out / f"{name}.irl2fa").write_bytes(image)
    print(f"make_test_images: v1, v2, other in {out}")


if __name__ == "__main__":
    main(*sys.argv[1:])
