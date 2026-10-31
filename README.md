# Hinasto Controller

Remote control and monitoring for the cheap Chinese diesel air heaters that are sold as "Webasto 8 kW" (Vevor "SUN" and
clones with a two-way 433 MHz remote). A small ESP32 board with a CC1101 radio talks to the heater the way its own remote
does; a PHP panel in the cloud (or on your own server) shows every heater, switches it, and keeps the room at a target
temperature by itself.

- **Panel**: web page, installable as an app on a phone. Power, mode (manual H1-H6, the heater's own auto, or *smart*),
  target temperature, live state, room temperature and humidity chart with the periods in which the heater ran, an analysis
  view (run time, starts, fuel estimate, time per power level, day by day), a console with the controller's log.
- **Controller**: ESP32-S2 mini + CC1101 (+ DHT22). No web page of its own: it connects to your server over HTTPS, is paired
  by an approval in the panel, and updates its firmware from the panel.
- **Smart mode**: the controller starts the heater when the room drops below the target, picks the power level, and stops it
  when it is warm enough.
- **Languages**: Polish, English, German, Czech, Slovak. The language of the panel is chosen once in the installer; the language
  of a controller is the language of the firmware you flash (one build per language, files end in `-PL`, `-EN`, `-DE`, `-CS`,
  `-SK`).
- MIT licensed. No framework, no Composer, no cloud service of anybody else: your heaters talk to your server.

> **Safety.** This software starts and stops a fuel burning appliance without a person next to it. You use it at your own risk:
> read [docs/safety.md](docs/safety.md) first. The authors are not responsible for damage or injury.

## How it fits together

```
 heater  <--433 MHz-->  controller (ESP32-S2 + CC1101)  <--HTTPS, every 3 s-->  server (PHP + MySQL)  <--  panel in a browser
```

The controller polls the heater once in a while like the remote does, decodes the answers (state, level, set point, chamber
temperature, voltage), reports them to the server and fetches the commands you gave in the panel. Nothing is hard-coded in the
firmware: WiFi, server address and the key are entered once in a setup portal that the controller opens on its own.

## Quick start

1. **Server**: install [`server/`](server/README.md) on any PHP 8.1+ host with MySQL or MariaDB (open the site, the installer
   asks for the language, the database and an admin password). It shows an *enrollment key*.
2. **Hardware**: wire the CC1101 and optionally a DHT22 to the ESP32-S2 mini, see [docs/firmware.md](docs/firmware.md).
3. **Firmware**: the easiest way is the builder image, which holds the whole toolchain (only Docker is needed, any system):
   `docker run --rm -v "$PWD":/work piotrtomczyk/hinasto-builder build` makes `firmware/hinasto-fw<N>-PL|EN|DE|CS|SK.bin`.
   Alternatively `tools\publish-firmware.ps1 -NoPublish` (Windows, Arduino CLI) or the Arduino IDE (board *LOLIN S2 Mini*,
   USB CDC On Boot enabled, add `-DHN_LANG_EN` or another language to the compiler flags). Flash one image over USB; after that
   updates come from the panel. Optional signed updates: [docs/signing.md](docs/signing.md).4. **Pair**: hold BOOT on the controller for 3 s, join the WiFi network `Hinasto-XXXX`, open `192.168.1.111`, enter your WiFi,
   the server and the enrollment key, and let it find the remote of your heater (or type its 6-hex-digit address). Approve the
   controller in the panel.

## Repository

| Path | What |
| --- | --- |
| [`server/`](server/README.md) | PHP panel and API, installer, translations, deploy scripts |
| [`sniffer/`](docs/firmware.md) | ESP32 firmware (Arduino): radio, heater model, smart mode, cloud client, setup portal, translations |
| [`tools/`](tools) | `publish-firmware.ps1` (build per language and publish), `builder/` (Docker image with the toolchain), `ota-sign.py` (signing keys and signatures), `smart-sim.cpp` and the tests (`smart-test.cpp`, `sigverify-test.cpp`, `check-lang.py`) |
| [`docs/`](docs) | radio protocol, firmware notes, safety, raw captures |

## Heaters and compatibility

Developed against two heaters of the older "SUN" generation (**V1 protocol**: 433.892 MHz, 2.4 kBaud GFSK). The newer
generation (V2: 434 MHz, 10 kBaud, CRC) is described by the [DieselHeaterRF](https://github.com/jakkik/DieselHeaterRF) project and is
not supported by this firmware. If your heater answers to the frames in [docs/protocol.md](docs/protocol.md) it should work; a
new model is best explored with the `v1 rawpoll` console command first.

## License

[MIT](LICENSE). Protocol knowledge is partly based on the work of the
[DieselPilot](https://github.com/PPTG/DieselPilot) and [DieselHeaterRF](https://github.com/jakkik/DieselHeaterRF) projects.
