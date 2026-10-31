# Firmware (sniffer/)

ESP32-S2 (Wemos/LOLIN S2 mini) with a CC1101 module, Arduino core for ESP32 3.3.x. The folder is called `sniffer` for
historical reasons: it began as a radio sniffer.

## Hardware

| Part | Pin of the S2 mini |
| --- | --- |
| CC1101 SCK / MISO / MOSI / CS | 7 / 9 / 11 / 12 |
| CC1101 GDO0 / GDO2 | 5 / 3 |
| CC1101 VCC / GND | 3V3 / GND |
| DHT22 data (optional room sensor) | 4 |
| DHT22 power (a GPIO, so a hung sensor can be power cycled) | 6 |
| BOOT button / LED | 0 / 15 (on the board) |

The CC1101 module has 8 pins (1.27 mm pitch): 1 GND, 2 VCC (3.3 V), 3 GDO0, 4 CSN, 5 SCK, 6 MOSI, 7 MISO, 8 GDO2. The sketch has
its own minimal CC1101 driver (RadioLib failed to detect this chip on this board). The CC1101 is a 3.3 V part: never 5 V.

## Build and flash

Without installing anything but Docker: `docker run --rm -v "$PWD":/work piotrtomczyk/hinasto-builder build [PL EN ...]` (see `tools/builder`;
build the image yourself with `docker build -f tools/builder/Dockerfile -t hinasto-builder .`). The rest of this section describes the manual way.

Arduino IDE: board *LOLIN S2 Mini*, *USB CDC On Boot: Enabled*, partition scheme default (two OTA slots of 1.25 MB). Pick the
language of the build with a compiler flag: `-DHN_LANG_PL`, `_EN`, `_DE`, `_CS` or `_SK` (without a flag the English build is
made). With the Arduino CLI:

```
arduino-cli compile --fqbn esp32:esp32:lolin_s2_mini:CDCOnBoot=default \
  --build-property "compiler.cpp.extra_flags=-DHN_LANG_EN" --build-path build-en sniffer
```

`tools\publish-firmware.ps1 -NoPublish` builds every language (in a temporary copy of the sketch with a generated
`lang_select.h`, so the Arduino core is compiled only once) into `firmware/hinasto-fw<version>-<LANG>.bin`; with `-Server`
it uploads them to the panel. **Raise `FW_VERSION` in `sniffer/control.h` before every release**: controllers only update to
a higher number.

First flash over USB (the board is in the ROM bootloader when the serial command `boot` is sent, or when BOOT is held while
resetting). After that the panel updates controllers by itself (button *Firmware* in the panel), each one to the build of its
own language. There is no automatic rollback: a build that boots but cannot reach the server needs a USB flash, so keep the
previous `.bin`.

Flash use is about 92 % of an OTA slot (95 % with `-DHN_DEV_OTA`); the built-in CA bundle for HTTPS is about 75 kB of that. DRAM is tight: the log ring is
28 lines of 80 characters, the TLS stack (mbedtls) is redirected to the 2 MB PSRAM of the S2 mini.

## Languages

All texts that a user can see (portal, log lines, command results, smart mode reasons) are macros in `lang_<code>.h`, chosen by
`lang.h` at compile time: one build per language, no runtime cost. The panel shows the log lines as they come, so a controller
speaks the language of its firmware. Rules for a translation: same macros in the same order, same `printf` formats (a macro missing in a language breaks that language's build; the formats
are not checked by the compiler, so compare them by hand), no `'` or `"` in the `P_*` texts (they are
pasted into the HTML and JavaScript of the portal). Each image carries two markers the server reads: `HINASTO-FW:<n>` and
`HINASTO-LANG:<code>`; the controller also reports `"lang"` in every sync, and the server offers it only builds of that language.
Identifiers that travel in JSON (`on`, `off`, `level`, smart phases such as `czeka`) are protocol, not language, and stay as they are.

## Setup portal

Hold BOOT for 3 s while the controller runs and release: it opens the open WiFi network `Hinasto-XXXX` (192.168.1.111, captive
DNS). One page: WiFi network (scanned list), WiFi password, server (a domain or an IP, nothing else), the enrollment key from
the panel and the address of the heater's remote (or *find the remote automatically*: it listens for 25 s and takes the address
heard most often). *Save and test* joins the WiFi, gets the time (needed for HTTPS), tries HTTPS with a full certificate check,
makes one sync request and saves the settings only when that worked; then it restarts. Plain HTTP is used only when the
box *allow plain HTTP* is ticked (for a server in your own network without a valid certificate): the secret then travels in the
clear, and an update is never taken over HTTP.

Who may change a configured controller: the portal opened with the button (or from the serial console) is trusted, the person
is at the device. A portal that opened **by itself** (no configuration at boot is fine, it needs no key; but also after 3 minutes
without WiFi) is an open access point on a configured controller, so its form must carry the enrollment key stored in the
controller. If the server address changes, the controller makes a **new secret** (the old one never reaches another host) and must
be approved there like a new controller. The enrollment key is sent only until the server has approved the controller. Hold BOOT
for 10 s for a factory reset (also forgets smart mode; the controller gets a new secret, so delete its old record in the panel).

Network flashing with `espota` is off in normal builds; compile with `-DHN_DEV_OTA` for it (password: the enrollment key). The
panel update path needs verified HTTPS, and optionally a signature, see [signing.md](signing.md).
LED: 1 flash every 2 s online, 2 flashes waiting for approval, 3 no server, 4 no WiFi, 1 Hz rejected, 5 Hz portal, steady while
testing, 10 Hz radio failure.

## Serial console (115200)

`cfg show|ssid|pass|api|key|id|find|ins|save|erase`, `portal [lan]` (lan = serve the form on the home WiFi, for testing from a
PC), `wifi`, `wifitx <dBm>`, `restart`, `log`, `verbose 0|1`, `status`, `boot`; radio work: `heater1 [s]` (listen to V1 frames),
`v1 id AABBCC[:HH]`, `v1 poll`, `v1 rawpoll`, `v1 menu|mode|up|down|off|on yes|restart yes`. Commands that start the heater need
the word `yes`. Do not keep a serial monitor open while a script uses the port.

## Cloud client

`POST <server>/api/v1/sync` every 3 s with the state, the new log lines and the acknowledgements of finished commands; the
answer carries the commands and, when a newer build of the controller's language exists, an update offer (see
`server/README.md`). A command is run only when the checks pass again against the live heater state; `off` always runs.
Settings (WiFi, server, key, secret, remote ids) are in NVS namespace `hinasto`; smart mode survives a restart in `hnsmart`.

## Smart mode

`smart.h` (no hardware in it), its simulation `tools/smart-sim.cpp` and its tests `tools/smart-test.cpp`
(`g++ -std=c++17 tools/smart-test.cpp -o smart-test && ./smart-test`). Rules: the heater is off and the last three samples
(30 s apart) are 3 °C below the target: start; after ignition and 4 minutes of warm-up switch to manual and pick a first level
from the gap; then every 11-17 minutes compare the measured rate of rise with the wanted one and move one level; at the target
the level comes down; when even H1 still lifts the temperature at the target, switch off (restart at target - 3 °C).

Safety rules: any command from the panel that is accepted, or a heater found stopped by someone else, switches smart off. When
smart cannot take manual control of a heater it started, it stops the heater and then switches itself off. No temperature
reading for 10 minutes, or a reading that does not move by 0.05 °C for an hour (a dead sensor), switches the heater off and
blocks a start. A heater that is not heard for 5 minutes while the sensor is gone or the room is 1 °C above the target gets a
power off anyway. What smart believes about the heater and the number of failed starts are kept in flash: after a restart, an
update or a power failure, a heater that smart was running and that is found stopped ends smart mode instead of being started
again. A factory reset, or pairing the controller with another heater, forgets smart mode. Time is counted in seconds of a
64-bit clock, and deadlines in `control.cpp` are compared wrap-safe, so the 49.7 day wrap of `millis()` changes nothing.
Constants are in `smartcfg`. The thermal model of the simulation is a guess: watch the first runs in the panel console and tune.