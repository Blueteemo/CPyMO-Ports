"""Fail closed if the S60v3 release embeds an outdated startup image."""
import hashlib
from pathlib import Path
import re
import struct

ENGINE_ROOT = Path(__file__).resolve().parents[1]


def checked_startup(engine_root=ENGINE_ROOT):
    """Return the pinned 320x240 PNG bytes; reject stale or invalid assets."""
    assets = Path(engine_root) / 'assets'
    manifest = (assets / 'startup.sha256').read_text(encoding='ascii').strip()
    if re.fullmatch(r'[0-9a-f]{64}  startup\.png', manifest) is None:
        raise RuntimeError('Invalid assets/startup.sha256; use the documented canonical image.')
    expected = manifest.split()[0]
    data = (assets / 'startup.png').read_bytes()
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected:
        raise RuntimeError(
            f'S60v3 startup image does not match the pinned release asset: '
            f'expected {expected}, got {actual}. '
            'Restore the canonical startup.png; do not update its checksum '
            'to accept an old image.'
        )
    if (len(data) < 33 or data[:8] != b'\x89PNG\r\n\x1a\n'
            or data[12:16] != b'IHDR'
            or struct.unpack('>II', data[16:24]) != (320, 240)):
        raise RuntimeError('The canonical S60v3 startup asset must be a 320x240 PNG.')
    return data


def verify_embedded_startup(exe_bytes, canonical):
    """The startup PNG must actually be embedded in the linked ARM executable."""
    if canonical not in exe_bytes:
        raise RuntimeError('The linked EXE lacks the pinned S60v3 startup PNG; stop packaging.')
    return hashlib.sha256(canonical).hexdigest()


if __name__ == '__main__':
    print('Canonical S60v3 startup SHA-256:', hashlib.sha256(checked_startup()).hexdigest())
