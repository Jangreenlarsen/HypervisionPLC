# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2025-2026 Jan Green Larsen
"""FEAT-439: opret noegleparret til signeret firmware (ECDSA P-256).

  python scripts/ota_keygen.py            # opretter certs/ota_signing.key + include/ota_pubkey.h
  python scripts/ota_keygen.py --pubkey   # genskab kun include/ota_pubkey.h fra en eksisterende noegle

Den PRIVATE noegle (certs/ota_signing.key) maa aldrig committes (dækket af
.gitignore: certs/*.key) og SKAL sikkerhedskopieres: uden den kan der kun
opdateres via USB/seriel. Den offentlige noegle (include/ota_pubkey.h)
bygges ind i firmwaren og er ikke hemmelig.
"""
import os
import sys

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEY = os.path.join(ROOT, 'certs', 'ota_signing.key')
HDR = os.path.join(ROOT, 'include', 'ota_pubkey.h')


def write_header(priv):
    pub = priv.public_key().public_bytes(serialization.Encoding.X962,
                                         serialization.PublicFormat.UncompressedPoint)
    assert len(pub) == 65 and pub[0] == 4
    rows = ',\n'.join('  ' + ', '.join('0x%02x' % b for b in pub[i:i + 13]) for i in range(0, 65, 13))
    with open(HDR, 'w', encoding='utf-8', newline='\n') as f:
        f.write('// SPDX-License-Identifier: AGPL-3.0-or-later\n')
        f.write('// Copyright (C) 2025-2026 Jan Green Larsen\n')
        f.write('// FEAT-439: offentlig noegle til verifikation af signeret firmware (ECDSA P-256,\n')
        f.write('// ukomprimeret punkt 04||X||Y). Genereret af scripts/ota_keygen.py — redigér ikke.\n')
        f.write('#pragma once\n#include <stdint.h>\n\n')
        f.write('static const uint8_t OTA_SIGNING_PUBKEY[65] = {\n%s\n};\n' % rows)
    print('Skrev', os.path.relpath(HDR, ROOT))


def main():
    if '--pubkey' in sys.argv:
        with open(KEY, 'rb') as f:
            priv = serialization.load_pem_private_key(f.read(), None)
        write_header(priv)
        return
    if os.path.exists(KEY):
        sys.exit('%s findes allerede — slet den bevidst foerst, eller brug --pubkey' % KEY)
    priv = ec.generate_private_key(ec.SECP256R1())
    os.makedirs(os.path.dirname(KEY), exist_ok=True)
    with open(KEY, 'wb') as f:
        f.write(priv.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                   serialization.NoEncryption()))
    print('Skrev', os.path.relpath(KEY, ROOT), '— SIKKERHEDSKOPIÉR DEN (ikke i git)')
    write_header(priv)


if __name__ == '__main__':
    main()
