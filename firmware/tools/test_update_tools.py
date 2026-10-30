#!/usr/bin/env python3
"""Tests of the update tools (run by `make -C firmware test`):
sign_image.py against RFC 8032 and its own checks, and irl2fa_update.py
sending to the device's real receiver code (tests/fwrx_host.c, the
bootloader's core on a virtual flash, trusting the test key).

    python3 firmware/tools/test_update_tools.py [path/to/fwrx_host]
SPDX-License-Identifier: AGPL-3.0-or-later
"""
import io
import os
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
FW = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(FW / "tests/data"))
import irl2fa_update as up  # noqa: E402
import make_fw_fixtures as fx  # noqa: E402
import sign_image as si  # noqa: E402

HOST = os.environ.get("FWRX_HOST", str(FW / "build/host/fwrx_host"))


class SignImage(unittest.TestCase):
    def test_rfc8032(self):
        for seed, pub, msg, sig in (
            ("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
             "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
             "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"),
            ("c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
             "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
             "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a")):
            s, m = bytes.fromhex(seed), bytes.fromhex(msg)
            self.assertEqual(si.ed25519_public(s).hex(), pub)
            self.assertEqual(si.ed25519_sign(s, m).hex(), sig)
            self.assertTrue(si.ed25519_verify(bytes.fromhex(pub), m, bytes.fromhex(sig)))
            self.assertFalse(si.ed25519_verify(bytes.fromhex(pub), m + b"x", bytes.fromhex(sig)))

    def test_images(self):
        binary = fx.thumb_plain(9000)
        pub = si.ed25519_public(fx.TEST_SEED)
        for comp in (si.COMP_STORED, si.COMP_LZMA, si.COMP_LZMA_THUMB):
            img = si.make_image(binary, 0x10203, fx.TEST_SEED, comp)
            self.assertEqual(si.unpack_binary(img), binary)
            self.assertTrue(si.check_image(img, pub)[0])
            self.assertFalse(si.check_image(img, si.ed25519_public(bytes(32)))[0])
            bad = bytearray(img)
            bad[200] ^= 1
            try:
                self.assertFalse(si.check_image(bytes(bad), pub)[0])
            except Exception:  # noqa: BLE001 (a payload that no longer decompresses)
                pass
        dev = si.make_image(binary, 1, None)
        self.assertTrue(si.check_image(dev, None)[0])
        self.assertEqual(si.parse_version("v1.2.3"), 0x10203)

    def test_factory(self):
        boot = b"\x01" * 1000
        binary = fx.thumb_plain(5000)
        img = si.make_image(binary, 1, fx.TEST_SEED)
        f = si.factory(boot, img, with_staging=True)
        self.assertEqual(f[:1000], boot)
        self.assertEqual(f[si.APP_ADDR:si.APP_ADDR + 128], img[:128])
        self.assertEqual(f[si.APP_ADDR + si.APP_OFFSET:si.APP_ADDR + si.APP_OFFSET + 5000], binary)
        self.assertEqual(f[si.STAGING_ADDR:si.STAGING_ADDR + len(img)], img)
        hexed = si.to_ihex(f, si.FLASH_BASE)
        self.assertTrue(hexed.endswith(":00000001FF\n"))
        with self.assertRaises(ValueError):
            si.factory(boot, si.make_image(binary, 1, None), with_staging=True)


@unittest.skipUnless(Path(HOST).exists(), "build fwrx_host first (make -C firmware test)")
class Sender(unittest.TestCase):
    def setUp(self):
        self.image = si.make_image(fx.thumb_plain(30000), 0x20000, fx.TEST_SEED)
        up.SILENCE_S = 1.5

    def run_tool(self, args, env=None):
        out = io.StringIO()
        old = dict(os.environ)
        os.environ.update(env or {})
        try:
            link = up.open_link("exec:" + " ".join([HOST] + args), 4800)
            rc = up.send(link, self.image, out=out, airtime=False)
            link.close()
            host_rc = link.p.returncode
        finally:
            os.environ.clear()
            os.environ.update(old)
        return rc, host_rc, out.getvalue()

    def test_install(self):
        rc, host_rc, out = self.run_tool([])
        self.assertEqual((rc, host_rc), (0, 0), out)
        self.assertIn("installed", out)

    def test_rollback_refused(self):
        rc, host_rc, out = self.run_tool(["0x30000"])
        self.assertEqual(rc, 1, out)
        self.assertIn("older", out)

    def test_wrong_key_refused(self):
        self.image = si.make_image(fx.thumb_plain(3000), 0x20000, bytes(range(32)))
        rc, host_rc, out = self.run_tool([])
        self.assertEqual(rc, 1, out)
        self.assertIn("not signed", out)

    def test_resume(self):
        with tempfile.TemporaryDirectory() as d:
            env = {"FWRX_STAGING": os.path.join(d, "staging.bin")}
            rc, host_rc, out = self.run_tool(["0", "12000"], env)
            self.assertEqual((rc, host_rc), (2, 3), out)
            rc, host_rc, out = self.run_tool([], env)
            self.assertEqual((rc, host_rc), (0, 0), out)
            first = out.split("sending", 2)[1].split("%")[0]
            self.assertGreater(int(first), 10, "did not resume: " + out[:200])


if __name__ == "__main__":
    if len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
        HOST = sys.argv.pop(1)
        Sender = unittest.skipUnless(Path(HOST).exists(), "no fwrx_host")(Sender)
    unittest.main()
