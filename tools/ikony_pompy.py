#!/usr/bin/env python3
"""Generuje 5 animowanych ikon pompy 24x24 dla OLED 128x64 (monochromatyczne).

Wyjscie:
  docs/ikony/ikony_pompy.gif           - podglad wszystkich 5 (powiekszony, wyglad OLED)
  docs/ikony/ikona_<n>_<nazwa>.gif     - kazda ikona osobno
  firmware/sterownik_oleju/ikony_pompy.h - klatki w formacie XBM dla U8g2 drawXBMP()

Uruchom z katalogu repozytorium:  python3 tools/ikony_pompy.py
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

N = 24          # rozmiar ikony w pikselach OLED
SS = 8          # nadprobkowanie przy rysowaniu
THRESH = 110    # prog binaryzacji (0-255) po zmniejszeniu

ROOT = Path(__file__).resolve().parent.parent


def canvas():
    im = Image.new("L", (N * SS, N * SS), 0)
    return im, ImageDraw.Draw(im)


def P(x, y):
    """wspolrzedne w pikselach ikony -> plotno nadprobkowane"""
    return (x * SS, y * SS)


def binarize(im):
    small = im.resize((N, N), Image.BOX)
    return [[1 if small.getpixel((x, y)) >= THRESH else 0 for x in range(N)] for y in range(N)]


def circle(d, cx, cy, r, fill=255, width=None):
    box = [P(cx - r, cy - r), P(cx + r, cy + r)]
    if width:
        d.ellipse(box, outline=fill, width=int(width * SS))
    else:
        d.ellipse(box, fill=fill)


def line(d, pts, w, fill=255):
    d.line([P(x, y) for x, y in pts], fill=fill, width=int(w * SS), joint="curve")
    for x, y in (pts[0], pts[-1]):  # zaokraglone konce
        circle(d, x, y, w / 2, fill)


# ---------------------------------------------------------------------------
# 1. Zebatka - obracajace sie kolo zebate (pompa zebata)
# ---------------------------------------------------------------------------
def gear_poly(cx, cy, r_root, r_tip, teeth, rot):
    pts = []
    pitch = 2 * math.pi / teeth
    for k in range(teeth):
        a = k * pitch + rot
        for frac, r in ((0.0, r_root), (0.12, r_tip), (0.42, r_tip), (0.54, r_root)):
            ang = a + frac * pitch
            pts.append(P(cx + r * math.cos(ang), cy + r * math.sin(ang)))
    return pts


def icon_gear(f, frames=6):
    im, d = canvas()
    teeth = 8
    rot = (2 * math.pi / teeth) * f / frames
    d.polygon(gear_poly(12, 12, 8.3, 11.5, teeth, rot), fill=255)
    circle(d, 12, 12, 4.2, 0)
    circle(d, 12, 12, 1.6, 255)
    return im


# ---------------------------------------------------------------------------
# 2. Kropla - olej kapie z dyszy do miseczki palnika
# ---------------------------------------------------------------------------
def drop(d, cx, tip_y, r):
    """kropla: kolko + trojkat do gory"""
    cy = tip_y + 1.6 * r
    circle(d, cx, cy, r)
    d.polygon([P(cx - r * 0.92, cy - r * 0.35), P(cx + r * 0.92, cy - r * 0.35), P(cx, tip_y)], fill=255)


def icon_drop(f, frames=8):
    im, d = canvas()
    # dysza
    d.rectangle([P(7, 0), P(17, 3)], fill=255)
    d.rectangle([P(10.5, 3), P(13.5, 6)], fill=255)
    # miseczka palnika
    d.rectangle([P(3, 21), P(21, 23)], fill=255)
    d.rectangle([P(3, 18), P(5, 21)], fill=255)
    d.rectangle([P(19, 18), P(21, 21)], fill=255)
    cx = 12
    if f < 3:  # kropla rosnie na dyszy
        r = (1.2, 1.9, 2.6)[f]
        drop(d, cx, 5.5, r)
    elif f < 6:  # spada
        y = (7.5, 10.5, 13.5)[f - 3]
        drop(d, cx, y, 2.2)
    elif f == 6:  # rozprysk
        d.rectangle([P(8, 19.5), P(16, 21)], fill=255)
        circle(d, 8, 16.5, 1.1)
        circle(d, 16, 16.5, 1.1)
    else:  # fala
        d.rectangle([P(6, 20), P(18, 21)], fill=255)
    return im


# ---------------------------------------------------------------------------
# 3. Symbol pompy - wirnik obracajacy sie w obudowie z krocem tlocznym
# ---------------------------------------------------------------------------
def icon_impeller(f, frames=6):
    im, d = canvas()
    # obudowa + krociec tloczny styczny u gory po prawej
    circle(d, 12, 13, 10.2, 255, width=2.0)
    d.rectangle([P(12, 0.8), P(23.5, 2.6)], fill=255)   # gorna scianka krocca
    d.rectangle([P(18, 5.4), P(23.5, 7.2)], fill=255)   # dolna scianka krocca
    d.rectangle([P(12.5, 2.6), P(23.5, 5.3)], fill=0)   # wnetrze krocca (otwarte)
    # wirnik: 3 lopatki zakrzywione
    rot = (2 * math.pi / 3) * f / frames
    for k in range(3):
        a = rot + k * 2 * math.pi / 3
        pts = []
        for i in range(6):
            t = i / 5
            r = 2.0 + 5.6 * t
            ang = a + 0.9 * t
            pts.append((12 + r * math.cos(ang), 13 + r * math.sin(ang)))
        line(d, pts, 1.9)
    circle(d, 12, 13, 2.6)
    return im


# ---------------------------------------------------------------------------
# 4. Przeplyw - rura z przesuwajacymi sie strzalkami i kroplami na wylocie
# ---------------------------------------------------------------------------
def icon_flow(f, frames=8):
    im, d = canvas()
    # rura: scianki
    d.rectangle([P(0, 5), P(19, 6.5)], fill=255)
    d.rectangle([P(0, 15.5), P(19, 17)], fill=255)
    # kolnierz wylotu
    d.rectangle([P(19, 3.5), P(21, 18.5)], fill=255)
    # strzalki przesuwaja sie o 1 piksel na klatke (okres 8 px)
    off = f * (8 / frames)
    for base in (-8, 0, 8, 16):
        x = base + off
        if -3 < x < 17:
            line(d, [(x, 8), (x + 3, 11), (x, 14)], 1.6)
    # zasloniecie strzalek wychodzacych za kolnierz
    d.rectangle([P(18, 7), P(19, 15)], fill=0)
    # krople pod wylotem
    yd = 19 + (f % 4) * 1.2
    circle(d, 22.2, yd, 1.0)
    return im


# ---------------------------------------------------------------------------
# 5. Silnik z korba - tloczek chodzi gora/dol (silnik wycieraczki + pompa)
# ---------------------------------------------------------------------------
def icon_crank(f, frames=8):
    im, d = canvas()
    cx, cy, rc, L = 12, 17, 3.6, 9.0
    th = 2 * math.pi * f / frames
    px, py = cx + rc * math.sin(th), cy - rc * math.cos(th)  # czop korby
    piston_y = cy - (rc * math.cos(th) + math.sqrt(L * L - (rc * math.sin(th)) ** 2))
    # cylinder
    d.rectangle([P(6, 0), P(7.5, 10)], fill=255)
    d.rectangle([P(16.5, 0), P(18, 10)], fill=255)
    # tloczek
    d.rectangle([P(8.5, piston_y - 3.2), P(15.5, piston_y)], fill=255)
    # tarcza korby (obrys)
    circle(d, cx, cy, 6.3, 255, width=1.6)
    # korbowod
    line(d, [(px, py), (12, piston_y - 0.5)], 1.7)
    circle(d, px, py, 1.6)
    circle(d, cx, cy, 1.0)
    return im


ICONS = [
    ("zebatka", "Zebatka", icon_gear, 6),
    ("kropla", "Kropla", icon_drop, 8),
    ("wirnik", "Wirnik", icon_impeller, 6),
    ("przeplyw", "Przeplyw", icon_flow, 8),
    ("korba", "Korba", icon_crank, 8),
]


# ---------------------------------------------------------------------------
# podglad w stylu OLED
# ---------------------------------------------------------------------------
BG = (6, 8, 12)
OFF = (18, 24, 34)
ON = (170, 225, 255)
SCALE = 8
FRAME_MS = 150


def render_icon(bits, scale=SCALE):
    im = Image.new("RGB", (N * scale, N * scale), BG)
    d = ImageDraw.Draw(im)
    for y in range(N):
        for x in range(N):
            d.rectangle([x * scale, y * scale, x * scale + scale - 2, y * scale + scale - 2],
                        fill=ON if bits[y][x] else OFF)
    return im


def main():
    out_docs = ROOT / "docs" / "ikony"
    out_docs.mkdir(parents=True, exist_ok=True)
    all_bits = {}
    for key, _, fn, nf in ICONS:
        all_bits[key] = [binarize(fn(i, nf)) for i in range(nf)]

    # pojedyncze GIF-y
    for idx, (key, label, _, nf) in enumerate(ICONS, 1):
        frames = [render_icon(b) for b in all_bits[key]]
        frames[0].save(out_docs / f"ikona_{idx}_{key}.gif", save_all=True, append_images=frames[1:],
                       duration=FRAME_MS, loop=0, disposal=2)

    # zbiorczy podglad
    try:
        font = ImageFont.load_default(size=22)
    except TypeError:
        font = ImageFont.load_default()
    cell = N * SCALE
    pad = 28
    W = len(ICONS) * cell + (len(ICONS) + 1) * pad
    H = cell + 2 * pad + 34
    total = 24  # NWW(6, 8)
    sheet = []
    for t in range(total):
        im = Image.new("RGB", (W, H), BG)
        d = ImageDraw.Draw(im)
        for i, (key, label, _, nf) in enumerate(ICONS):
            x0 = pad + i * (cell + pad)
            im.paste(render_icon(all_bits[key][t % nf]), (x0, pad))
            text = f"{i + 1}. {label}"
            tw = d.textlength(text, font=font)
            d.text((x0 + (cell - tw) / 2, pad + cell + 8), text, fill=(200, 210, 220), font=font)
        sheet.append(im)
    sheet[0].save(out_docs / "ikony_pompy.gif", save_all=True, append_images=sheet[1:],
                  duration=FRAME_MS, loop=0, disposal=2)

    # naglowek C (XBM: bit 0 = lewy piksel, 3 bajty na wiersz)
    lines = [
        "// Wygenerowane przez tools/ikony_pompy.py - nie edytuj recznie.",
        "// Animowane ikony pompy 24x24 dla U8g2: oled.drawXBMP(x, y, 24, 24, IKONA_x[klatka]);",
        "#pragma once",
        "#include <avr/pgmspace.h>",
        "",
        "#define IKONA_W 24",
        "#define IKONA_H 24",
        "",
    ]
    for idx, (key, label, _, nf) in enumerate(ICONS, 1):
        name = f"IKONA_{key.upper()}"
        lines.append(f"// {idx}. {label}")
        lines.append(f"#define {name}_KLATKI {nf}")
        lines.append(f"const uint8_t {name}[{nf}][72] PROGMEM = {{")
        for bits in all_bits[key]:
            data = []
            for row in bits:
                for b in range(3):
                    v = 0
                    for k in range(8):
                        if row[b * 8 + k]:
                            v |= 1 << k
                    data.append(f"0x{v:02X}")
            lines.append("  {" + ",".join(data) + "},")
        lines.append("};")
        lines.append("")
    hdr = ROOT / "firmware" / "sterownik_oleju" / "ikony_pompy.h"
    hdr.write_text("\n".join(lines))

    # podglad ASCII pierwszej klatki (do kontroli w terminalu)
    for key, label, _, nf in ICONS:
        print(f"== {label} (klatka 0 z {nf})")
        for row in all_bits[key][0]:
            print("".join("#" if v else "." for v in row))


if __name__ == "__main__":
    main()
