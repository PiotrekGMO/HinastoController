# Hinasto builder image

A Docker image with the whole firmware toolchain (Arduino CLI, the ESP32 core pinned to the version the firmware is built
with, Python with `cryptography` for signing, and a C++ compiler for the host tests). With it nothing but Docker is needed to
build the firmware in every language, on Windows, Linux or macOS.

```
# in the project folder (the one with sniffer/ and tools/)
docker run --rm -v "$PWD":/work piotrtomczyk/hinasto-builder build          # PL EN DE CS SK  ->  firmware/
docker run --rm -v "$PWD":/work piotrtomczyk/hinasto-builder build PL EN    # only these
docker run --rm -v "$PWD":/work piotrtomczyk/hinasto-builder keygen         # keys/ota-private.pem + sniffer/signing_key.h
docker run --rm -v "$PWD":/work piotrtomczyk/hinasto-builder sign firmware/hinasto-fw24-PL.bin
docker run --rm -v "$PWD":/work piotrtomczyk/hinasto-builder test           # smart mode and signing tests
```

PowerShell: use `${PWD}` instead of `$PWD`. On Linux add `-u "$(id -u):$(id -g)"` so that the files it writes belong to you.
The first build of a language takes a few minutes, the core is already compiled in the image.

Signing is optional (see [docs/signing.md](../../docs/signing.md)): when `sniffer/signing_key.h` exists, `build` signs every
image with `keys/ota-private.pem`. The private key never enters the image, it lives in the folder you mount.

## Build the image yourself

```
docker build -f tools/builder/Dockerfile -t hinasto-builder .
```

It downloads the ESP32 core (a few GB, only the ESP32-S2 parts are kept) and compiles the sketch once to warm the cache.
`.github/workflows/builder-image.yml` builds it for amd64 and arm64 and pushes it to Docker Hub when a tag `builder-v<N>` is
pushed (repository secrets `DOCKERHUB_USERNAME` and `DOCKERHUB_TOKEN`).
