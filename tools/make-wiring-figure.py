#!/usr/bin/env python3
"""Draws the wiring on the photos of the real boards: docs/hardware/wiring-photos.svg.

The photos live in docs/hardware/photos and are embedded in the SVG (an SVG shown inside a README may not load files). The
positions of the holes are read off the photos, the wires are routed at right angles and the order of the channels is picked
by trying all of them and keeping the one with the fewest crossings.
    python3 tools/make-wiring-figure.py
"""
import base64
import itertools
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PHOTOS = os.path.join(ROOT, "docs", "hardware", "photos")
OUT = os.path.join(ROOT, "docs", "hardware", "wiring-photos.svg")


def data_uri(name):
    mime = "image/webp" if name.endswith(".webp") else "image/png"
    with open(os.path.join(PHOTOS, name), "rb") as f:
        return f"data:{mime};base64," + base64.b64encode(f.read()).decode()


# ---- S2 mini (628 x 775 px photo of the underside), scale 0.75 ----
S2X, S2Y, S2S = 30, 80, 0.75
ROWS = {"EN": 180, 3: 232, 5: 287, 7: 340, 9: 393, 11: 447, 12: 500, "3V3": 555, "GND": 506}
OUTER, INNER, LOUTER, LINNER = 568, 513, 75, 130  # x of the hole columns on the photo


def s2(px, py):
    return (S2X + px * S2S, S2Y + py * S2S)


# ---- CC1101 (294 x 718 px photo), turned 90 degrees counter-clockwise so that its header faces the S2 mini ----
CCX, CCY, CCS = 840, 215, 0.75
CC_W = 294
HOLE = {1: (212, 42), 3: (163, 42), 5: (118, 42), 7: (72, 42), 2: (212, 90), 4: (163, 90), 6: (118, 90), 8: (72, 90)}  # pin -> (x, y) on the photo


def cc(pin):
    ox, oy = HOLE[pin]
    return (CCX + oy * CCS, CCY + (CC_W - ox) * CCS)


# ---- DHT22 (344 x 667 px photo), scale 0.5 ----
DX, DY, DS = 1090, 500, 0.5
DHT_PINS = {"GND": (133, 655), "VCC": (170, 655), "DATA": (208, 655)}


def dht(name):
    ox, oy = DHT_PINS[name]
    return (DX + ox * DS, DY + oy * DS)


COLOR = {"3V3": "#dc2626", "GND": "#111827", 3: "#7c3aed", 5: "#f97316", 7: "#2563eb", 9: "#16a34a", 11: "#0d9488", 12: "#92400e", 4: "#ca8a04", 6: "#dc2626"}

# radio wires: (S2 pin, CC1101 pin, label)
RADIO = [(3, 8, "GPIO 3 → GDO2 (8)"), (5, 3, "GPIO 5 → GDO0 (3)"), (7, 5, "GPIO 7 → SCK (5)"), (9, 7, "GPIO 9 → MISO (7)"),
         (11, 6, "GPIO 11 → MOSI (6)"), (12, 4, "GPIO 12 → CSN (4)"), ("3V3", 2, "3V3 → VCC (2)")]
CHANNELS = [690 + 18 * i for i in range(len(RADIO))]


def radio_path(src, pin, xc):
    xs, ys = s2(OUTER, ROWS[src])
    tx, ty = cc(pin)
    if pin % 2 == 0:  # right column of the header: arrive in the gap above the hole, so the hole of the left column stays free
        ay = ty - 17
        return [(xs, ys), (xc, ys), (xc, ay), (tx, ay), (tx, ty)]
    return [(xs, ys), (xc, ys), (xc, ty), (tx, ty)]


def fixed_paths():
    gnd_out = s2(LOUTER, ROWS["GND"])
    gnd_in = s2(LINNER, ROWS["GND"])
    corridor = (gnd_out[0] + gnd_in[0]) / 2
    cc1 = cc(1)
    p4 = s2(INNER, ROWS[5])
    p6 = s2(INNER, ROWS[7])
    ds = {k: dht(k) for k in DHT_PINS}
    return {
        "gnd_cc": ([gnd_out, (gnd_out[0] - 28, gnd_out[1]), (gnd_out[0] - 28, 60), (cc1[0], 60), cc1], "GND"),
        "gnd_dht": ([gnd_in, (corridor, gnd_in[1]), (corridor, 862), (ds["GND"][0], 862), ds["GND"]], "GND"),
        "p6": ([p6, (p6[0] + 21, p6[1]), (p6[0] + 21, 892), (ds["VCC"][0], 892), ds["VCC"]], 6),
        "p4": ([p4, (p4[0] + 26, p4[1]), (p4[0] + 26, 922), (ds["DATA"][0], 922), ds["DATA"]], 4),
    }


def segments(path):
    return list(zip(path[:-1], path[1:]))


def crosses(a, b):
    (ax1, ay1), (ax2, ay2) = a
    (bx1, by1), (bx2, by2) = b
    a_h, b_h = ay1 == ay2, by1 == by2
    if a_h == b_h:
        return False
    h, v = (a, b) if a_h else (b, a)
    hx1, hx2 = sorted((h[0][0], h[1][0]))
    vy1, vy2 = sorted((v[0][1], v[1][1]))
    return hx1 < v[0][0] < hx2 and vy1 < h[0][1] < vy2


def count(paths):
    n = 0
    for i, p in enumerate(paths):
        for q in paths[i + 1:]:
            n += sum(crosses(a, b) for a in segments(p) for b in segments(q))
    return n


fixed = fixed_paths()
best = None
for order in itertools.permutations(CHANNELS):
    paths = [radio_path(src, pin, xc) for (src, pin, _), xc in zip(RADIO, order)] + [p for p, _ in fixed.values()]
    c = count(paths)
    if best is None or c < best[0]:
        best = (c, order)
crossings, order = best
print(f"{crossings} crossings with channels {order}")


def d(path):
    return "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y in path)


def wire(path, color, dashed=False):
    dash = ' stroke-dasharray="9 6"' if dashed else ""
    return (f'<path d="{d(path)}" fill="none" stroke="#ffffff" stroke-width="10" stroke-linejoin="round" stroke-linecap="round" opacity="0.9"/>\n'
            f'  <path d="{d(path)}" fill="none" stroke="{color}" stroke-width="5" stroke-linejoin="round" stroke-linecap="round"{dash}/>')


def dot(p, color):
    return f'<circle cx="{p[0]:.1f}" cy="{p[1]:.1f}" r="7.5" fill="#ffffff" stroke="{color}" stroke-width="3.5"/>'


def label(x, y, text, color, anchor="start"):
    return (f'<text x="{x:.1f}" y="{y:.1f}" font-size="16" font-weight="700" fill="{color}" text-anchor="{anchor}" '
            f'stroke="#ffffff" stroke-width="4" paint-order="stroke">{text}</text>')


parts = []
W, H = 1420, 960
parts.append(f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
             'font-family="system-ui, -apple-system, Segoe UI, Roboto, sans-serif">')
parts.append("<title>Wiring on the real boards: ESP32-S2 mini, CC1101 and DHT22</title>")
parts.append(f'<rect width="{W}" height="{H}" fill="#ffffff"/>')
parts.append(f'<text x="{W/2}" y="30" text-anchor="middle" font-size="22" font-weight="700" fill="#111827">Wiring on the real boards</text>')
parts.append(f'<text x="{W/2}" y="48" text-anchor="middle" font-size="13" fill="#4b5563">All logic is 3.3 V. The CC1101 must never get 5 V.</text>')

# photos
parts.append(f'<image x="{S2X}" y="{S2Y}" width="{628*S2S}" height="{775*S2S}" xlink:href="{data_uri("esp32-s2-mini.webp")}"/>')
parts.append(f'<g transform="translate({CCX} {CCY + CC_W*CCS}) rotate(-90) scale({CCS})"><image width="294" height="718" xlink:href="{data_uri("cc1101-module.webp")}"/></g>')
parts.append(f'<image x="{DX}" y="{DY}" width="{344*DS}" height="{667*DS}" xlink:href="{data_uri("dht22-module.webp")}"/>')
parts.append(f'<text x="{S2X + 628*S2S/2}" y="{S2Y + 775*S2S + 22}" text-anchor="middle" font-size="14" font-weight="700" fill="#111827">LOLIN / Wemos S2 mini (underside)</text>')
parts.append(f'<text x="{CCX + 718*CCS/2}" y="{CCY + 294*CCS + 22}" text-anchor="middle" font-size="14" font-weight="700" fill="#111827">CC1101 433 MHz module</text>')
parts.append(f'<text x="{DX + 344*DS/2}" y="{DY - 10}" text-anchor="middle" font-size="14" font-weight="700" fill="#111827">DHT22 module</text>')

# wires
for (src, pin, text), xc in zip(RADIO, order):
    color = COLOR[src]
    path = radio_path(src, pin, xc)
    parts.append(wire(path, color))
for name, (path, key) in fixed.items():
    parts.append(wire(path, COLOR[key], dashed=(name == "p6")))
# ends
for (src, pin, text), xc in zip(RADIO, order):
    color = COLOR[src]
    parts.append(dot(s2(OUTER, ROWS[src]), color))
    parts.append(dot(cc(pin), color))
    xs, ys = s2(OUTER, ROWS[src])
    parts.append(label(xs + 18, ys - 8, text, color))
for name, (path, key) in fixed.items():
    parts.append(dot(path[0], COLOR[key]))
    parts.append(dot(path[-1], COLOR[key]))
parts.append(label(110, 56, "GND → CC1101 GND (1)", COLOR["GND"]))
parts.append(label(640, 856, "GND → DHT22 GND", COLOR["GND"]))
parts.append(label(640, 886, "GPIO 6 → DHT22 VCC (it powers the sensor)", COLOR[6]))
parts.append(label(640, 916, "GPIO 4 → DHT22 DATA", COLOR[4]))
parts.append(f'<text x="30" y="{H-14}" font-size="11" fill="#6b7280">Pin numbers of the CC1101 are the numbers printed on the module (1, 2 at one end of the header, 7, 8 at the other). '
             'Check the labels on your own module and board before you power it.</text>')
parts.append("</svg>")

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(parts) + "\n")
print("written", OUT, os.path.getsize(OUT) // 1024, "KB")
