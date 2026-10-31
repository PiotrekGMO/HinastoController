#!/usr/bin/env python3
"""Signing of Hinasto firmware images (optional, see docs/signing.md). Needs Python 3 and the "cryptography" package
(it is in the builder image, see tools/builder).

  ota-sign.py keygen [--keys keys] [--header sniffer/signing_key.h]
      makes the private key (keys/ota-private.pem: keep it safe, never commit it) and the C header with the public key
      that goes into the firmware (a build that contains the header only accepts signed updates)
  ota-sign.py sign IMAGE.bin [--keys keys]
      appends the signature trailer to the image (version and language are read from the image itself)
  ota-sign.py verify IMAGE.bin [--header sniffer/signing_key.h]
      checks a signed image against the public key of the header

The trailer is "HNSG" | version (u32 big endian) | language (2 letters) | r | s (32 bytes each), signed over
SHA-256(image || version || language): see sniffer/sigverify.h.
"""
import argparse
import hashlib
import os
import re
import struct
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

TRAILER = 74


def markers(image: bytes):
    fw = re.search(rb"HINASTO-FW:(\d{1,5})\x00", image)
    lang = re.search(rb"HINASTO-LANG:([A-Z]{2})\x00", image)
    if not fw or not lang:
        sys.exit("the image has no HINASTO-FW / HINASTO-LANG markers: is this a Hinasto build?")
    return int(fw.group(1)), lang.group(1)


def digest(image: bytes, version: int, lang: bytes) -> bytes:
    h = hashlib.sha256(image)
    h.update(struct.pack(">I", version) + lang)
    return h.digest()


def keygen(args):
    os.makedirs(args.keys, exist_ok=True)
    path = os.path.join(args.keys, "ota-private.pem")
    if os.path.exists(path) and not args.force:
        sys.exit(f"{path} exists: it is your signing key, losing it means updating the controllers over USB. Use --force to replace it.")
    key = ec.generate_private_key(ec.SECP256R1())
    with open(path, "wb") as f:
        f.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    try:
        os.chmod(path, 0o600)
    except OSError:
        pass
    pub = key.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    rows = ",\n".join("    " + ", ".join(f"0x{b:02X}" for b in pub[i:i + 8]) for i in range(0, len(pub), 8))
    with open(args.header, "w") as f:
        f.write("// Public key for signed updates (made by tools/ota-sign.py keygen). A build with this file accepts only signed images.\n")
        f.write("#pragma once\n#include <stdint.h>\nstatic const uint8_t HN_PUBKEY[65] = {\n" + rows + "\n};\n")
    print(f"private key: {path} (keep a backup, never commit it)\npublic key header: {args.header}")


def sign(args):
    image = open(args.image, "rb").read()
    if image[-TRAILER:-TRAILER + 4] == b"HNSG":
        sys.exit("the image already carries a signature")
    version, lang = markers(image)
    key = serialization.load_pem_private_key(open(os.path.join(args.keys, "ota-private.pem"), "rb").read(), None)
    sig = key.sign(digest(image, version, lang), ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    r, s = utils.decode_dss_signature(sig)
    trailer = b"HNSG" + struct.pack(">I", version) + lang + r.to_bytes(32, "big") + s.to_bytes(32, "big")
    with open(args.image, "wb") as f:
        f.write(image + trailer)
    print(f"signed {args.image}: version {version}, language {lang.decode()}, +{len(trailer)} bytes")


def verify(args):
    data = open(args.image, "rb").read()
    text = open(args.header).read()
    pub = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", text))
    image, trailer = data[:-TRAILER], data[-TRAILER:]
    if trailer[:4] != b"HNSG":
        sys.exit("no signature trailer")
    version = struct.unpack(">I", trailer[4:8])[0]
    lang = trailer[8:10]
    r, s = int.from_bytes(trailer[10:42], "big"), int.from_bytes(trailer[42:], "big")
    ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), pub).verify(
        utils.encode_dss_signature(r, s), digest(image, version, lang), ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    print(f"signature ok: version {version}, language {lang.decode()}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    k = sub.add_parser("keygen")
    k.add_argument("--keys", default="keys")
    k.add_argument("--header", default="sniffer/signing_key.h")
    k.add_argument("--force", action="store_true")
    k.set_defaults(fn=keygen)
    s = sub.add_parser("sign")
    s.add_argument("image")
    s.add_argument("--keys", default="keys")
    s.set_defaults(fn=sign)
    v = sub.add_parser("verify")
    v.add_argument("image")
    v.add_argument("--header", default="sniffer/signing_key.h")
    v.set_defaults(fn=verify)
    args = p.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
