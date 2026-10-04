# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2025-2026 Jan Green Larsen
"""FEAT-439: signér en firmware.bin til OTA.

  python scripts/sign_firmware.py <firmware.bin> [<ud.bin>] [--key certs/ota_signing.key]
  python scripts/sign_firmware.py --verify <firmware_signed.bin>

Format: firmwaren uændret + 72 bytes trailer = "HVPLCSG1" (8) + r (32) + s (32),
hvor (r, s) er en ECDSA P-256-signatur over SHA-256 af firmwaren (uden trailer).
PLC'en skriver ikke traileren til flash og verificerer mod include/ota_pubkey.h.
"""
import os
import sys

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature, encode_dss_signature

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAGIC = b'HVPLCSG1'
TRAILER = 72


def sign(data, key_path):
    with open(key_path, 'rb') as f:
        priv = serialization.load_pem_private_key(f.read(), None)
    r, s = decode_dss_signature(priv.sign(data, ec.ECDSA(hashes.SHA256())))
    return data + MAGIC + r.to_bytes(32, 'big') + s.to_bytes(32, 'big')


def pubkey_from_header():
    import re
    txt = open(os.path.join(ROOT, 'include', 'ota_pubkey.h'), encoding='utf-8').read()
    raw = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', txt.split('{', 1)[1]))
    return ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), raw)


def verify(blob):
    if len(blob) <= TRAILER or blob[-TRAILER:-64] != MAGIC:
        return False
    img, r, s = blob[:-TRAILER], blob[-64:-32], blob[-32:]
    try:
        pubkey_from_header().verify(encode_dss_signature(int.from_bytes(r, 'big'), int.from_bytes(s, 'big')),
                                    img, ec.ECDSA(hashes.SHA256()))
        return True
    except InvalidSignature:
        return False


def main():
    args = [a for a in sys.argv[1:]]
    if args and args[0] == '--verify':
        ok = verify(open(args[1], 'rb').read())
        print('Signatur OK' if ok else 'Signatur UGYLDIG eller mangler')
        sys.exit(0 if ok else 1)
    key = os.path.join(ROOT, 'certs', 'ota_signing.key')
    if '--key' in args:
        i = args.index('--key'); key = args[i + 1]; del args[i:i + 2]
    src = args[0]
    dst = args[1] if len(args) > 1 else src.replace('.bin', '_signed.bin')
    data = open(src, 'rb').read()
    if data[-TRAILER:-64] == MAGIC:
        sys.exit('%s er allerede signeret' % src)
    out = sign(data, key)
    open(dst, 'wb').write(out)
    assert verify(out), 'egen verifikation fejlede — passer include/ota_pubkey.h til noeglen?'
    print('Signeret:', dst, '(%d bytes)' % len(out))


if __name__ == '__main__':
    main()
