# Radio protocol of the "SUN" (V1) diesel heaters

Reverse engineered from two heaters of the older generation with a two-way 433 MHz remote, and cross-checked with the
[DieselPilot](https://github.com/PPTG/DieselPilot) project (`cc1101_init_V1`). Everything here was confirmed on real heaters
unless it says otherwise. The addresses in the examples are made up (`AABBCC`).

## Radio

- 433.892 MHz, 2.4 kBaud GFSK, sync word `09 1A`, fixed packet length 10 bytes, **no CRC**. The CC1101 appends RSSI and LQI.
- The remote polls the heater every ~0.67 s and the heater answers right after each poll. The heater answers an ESP32 that
  sends the same polls; the original remote is not needed (and not used while the controller works).
- The newer generation (V2) uses 434 MHz, 10 kBaud, CRC16: see [DieselHeaterRF](https://github.com/jakkik/DieselHeaterRF).

## Frame

```
2B <id> <addr0> <addr1> <addr2> <cmd> <sub> <d1> <d2> <noise>   (+ RSSI, LQI from the radio)
```

- `<addr0..2>` is the address of the remote (the heater is paired to it); `<id>` is a second per-remote byte (`0x15`, `0x12`
  were seen). The identifier of a heater is the address **plus** that byte: `AABBCC:15`. The last payload byte is demodulator
  noise, ignore it.
- A value is `n = d1 * 2 + (d2 >> 7)`. The low bits of `d2` flicker (`0x10`..`0x1F`), a plausibility filter that accepts
  `0x10..0x1F` and `0x00` drops garbage frames.

## Remote to heater (`cmd` has bit 7 set when the real remote sends it)

| Frame (`cmd sub d1 d2`) | Meaning |
| --- | --- |
| `A9 00 00 00` | poll (state request), also what the remote sends all the time |
| `AB A9 00 00` / `AB A9 80 00` | UP / DOWN, one frame per press |
| `AB AD 80 00` | power on (starts the heater!) |
| `AB AE 00 00` | power off (the heater cools down for about 2 min 15 s) |
| `AB AA 00 00` | long OK: toggles manual level mode and set point mode |
| `AB AA 80 00` | short OK: next info screen |

## Heater to remote

Replies are `B1 sub d1 d2` (main page), `B2 20 d1 d2` (voltage page) and, on one model, `80 3C d1 d2` (its own temperature).

- The **state** is in the top three bits of `sub` (`sub & 0xE0`): `0x00` off, `0x20` running, `0x60` cooling down,
  `0xA0` the chamber temperature page of a running heater; `0x30` was seen while starting. The low bits are flags that differ
  between models (the second model idles at `0x10` and runs at `0x2A`/`0x3C`/`0xBC`).
- Running, manual mode (`B1 20`): `n = 1..6` is the power level H1..H6.
- Running, set point mode (`B1 20`): `n = 64 + set point in °C` (`n = 80` is 16 °C).
- Chamber temperature page (`B1 A0`): `n` is degrees C. Voltage page (`B2 20`): `n` is volts.
- `80 3C d1 d2`: `n` is the heater's own temperature reading in °C (only one of the two models has this screen).

## Behaviours worth knowing

- The info screens cycle with the short OK: main, chamber, voltage, (own temperature on one model), main. The heater stays on a
  screen until the next press, so a controller that walks the screens must always find its way back to the main page; count
  the screens of a new model first (`v1 rawpoll` plus `v1 menu` in the firmware console).
- UP/DOWN walk a list whose order is not simple (from H6, UP gives the set point 20 °C; from set point 16 °C, UP gives H1).
  Poll after every step and compare; the firmware walks one step at a time and verifies each one.
- Long OK from set point mode always lands in manual level 1; from manual it lands in set point mode with the stored set point.
- The heater refuses a power on while it cools down: block it in the UI by state.
- After a long time without the remote some heaters stop answering polls while idle; a controller that polls keeps them awake in
  the tests so far, but treat "no reply" as "unknown", never as "off".
- The remote has its own thermometer: no frame carries the room temperature of the remote. The firmware uses a DHT22 for that.

## Captures

Raw captures of the button tests are in [`captures/`](captures) (addresses replaced by `AA BB CC`). They are the output of the
firmware console in V1 listening mode (`heater1`).
