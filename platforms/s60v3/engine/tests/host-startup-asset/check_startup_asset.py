#!/usr/bin/env python3
"""Host-side regression checks for the official startup image guard."""
from pathlib import Path
import hashlib
import sys
import tempfile
import unittest

ENGINE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ENGINE / 'scripts'))
from startup_asset import checked_startup, verify_embedded_startup  # noqa: E402


class StartupAssetTests(unittest.TestCase):
    def test_canonical_asset(self):
        data = checked_startup(ENGINE)
        self.assertEqual(hashlib.sha256(data).hexdigest(),
                         (ENGINE / 'assets/startup.sha256').read_text().split()[0])

    def test_reject_wrong_png_without_manifest_update(self):
        canonical = checked_startup(ENGINE)
        with tempfile.TemporaryDirectory() as temp:
            assets = Path(temp) / 'assets'
            assets.mkdir()
            (assets / 'startup.sha256').write_bytes((ENGINE / 'assets/startup.sha256').read_bytes())
            (assets / 'startup.png').write_bytes(canonical[:-1] + bytes([canonical[-1] ^ 1]))
            with self.assertRaisesRegex(RuntimeError, 'does not match'):
                checked_startup(temp)

    def test_reject_old_exe_and_accept_canonical_embed(self):
        canonical = checked_startup(ENGINE)
        with self.assertRaisesRegex(RuntimeError, 'lacks the pinned'):
            verify_embedded_startup(b'not the canonical PNG', canonical)
        self.assertEqual(verify_embedded_startup(b'header' + canonical + b'trailer', canonical),
                         hashlib.sha256(canonical).hexdigest())


if __name__ == '__main__':
    unittest.main()
