# Signed firmware updates (optional)

By default a controller installs the update its server offers: the image is authenticated by HTTPS (the certificate of the
server is checked) and its MD5 protects it in transit. That is as strong as the server and the panel password. Whoever takes
over one of them can put any code on every controller. If that matters to you (a shared host, several people with the panel
password, a server you do not fully control), turn on signing.

## What it does

- You make a key pair once. The **private key** stays on your computer; the **public key** is compiled into the firmware.
- Every image you publish is signed. A controller built with a public key installs an update only when the signature is valid,
  and it names exactly the offered version and the language of that build.
- A taken over server can no longer put code on the controllers (it can only refuse to offer an update), nor offer an old image
  as a new one, nor hand out a build of another language.

Without a public key in the build nothing changes: every image is accepted as before. Signing is a choice of whoever builds the
firmware; the ready-made images of a release are not signed with your key.

## How

```
python tools/ota-sign.py keygen          # keys/ota-private.pem and sniffer/signing_key.h
tools\publish-firmware.ps1 -NoPublish    # builds all languages and signs every image (or use the builder image)
```

`keygen` refuses to overwrite an existing key. **Back up `keys/ota-private.pem`** (an encrypted drive, a password manager): without
it you cannot sign, and controllers that only accept signed images can then be updated over USB only. Never commit it (`keys/`
and `sniffer/signing_key.h` are in `.gitignore`). Python 3 with the `cryptography` package is needed; the builder image has both.

The first release that contains the public key still reaches the controllers unsigned-checked (they cannot verify before they
know the key). From the next release on, every update is verified. Publishing works as before, the panel and the server do not
know about signatures: the signature is a 74 byte trailer behind the image, which the server stores like any other bytes and the
bootloader ignores.

## Format

`"HNSG" | version (u32 big endian) | language (2 letters) | r | s (2 x 32 bytes)`, signed with ECDSA P-256 over
`SHA-256(image || version || language)`. The controller hashes the image while it streams into flash and checks the
signature before it makes the new image the boot image. See `sniffer/sigverify.h` (the check) and `tools/ota-sign.py`
(the signer). `sh tools/test-signing.sh` tests the whole chain on a PC: the verifier of the firmware has to accept an image
signed by the script and refuse every changed image, version, language and an unsigned file.

## Limits

- Not tested on a real controller yet: the trailer behind the image is ignored by the bootloader on paper (the image
  carries its own length and checksum). Try a signed build on one controller you can reach with a USB cable before you rely on it.
- A controller only checks images, not the server's other answers (commands). The commands are bounded by the checks of the
  controller itself (ranges, the state of the heater), but a hostile server could still switch a heater on or off. HTTPS with a
  verified certificate is what protects that channel.
