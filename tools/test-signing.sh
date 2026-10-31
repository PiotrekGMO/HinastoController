#!/bin/sh
# Tests the signing chain end to end: ota-sign.py makes a key and signs a fake image, the C++ verifier of the firmware
# (sniffer/sigverify.h, built with the host's mbedtls) must accept it and refuse every tampered variant.
# Needs python3 with "cryptography", g++ and libmbedtls-dev; the builder image has them (tools/builder).
# Run from the repository root:  sh tools/test-signing.sh
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

python3 "$ROOT/tools/ota-sign.py" keygen --keys "$WORK/keys" --header "$WORK/signing_key.h" >/dev/null
# a fake image: random bytes with the two markers the real builds carry
python3 - "$WORK/fake.bin" <<'PY'
import os, sys
open(sys.argv[1], "wb").write(os.urandom(150000) + b"HINASTO-FW:77\x00" + b"HINASTO-LANG:PL\x00" + os.urandom(5000))
PY
python3 "$ROOT/tools/ota-sign.py" sign "$WORK/fake.bin" --keys "$WORK/keys"
python3 "$ROOT/tools/ota-sign.py" verify "$WORK/fake.bin" --header "$WORK/signing_key.h"
g++ -std=c++17 -O1 -Wall -I"$WORK" -I"$ROOT/sniffer" "$ROOT/tools/sigverify-test.cpp" -o "$WORK/t" -lmbedcrypto
"$WORK/t" "$WORK/fake.bin" 77 PL
