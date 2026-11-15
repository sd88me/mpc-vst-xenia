#!/usr/bin/env python3
"""Xenia's knobs and data dial, from Vavra's art (tools/vavra_art.py in mpc-vst-vavra): the tapered cone with a flat smoky top and a value
fill ring instead of a pointer. Xenia leaves out Vavra's clear side windows and the glass glint (plain shape + level fill). Colours are
module globals set per skin by make_tui.py (FILL, SMOKE, TRACK, TRACK_HI, SHADOW)."""
import math
from PIL import Image, ImageDraw, ImageFilter

FILL = (226, 52, 44)          # Classic: the XT's red; Shadow: teal
FILL_RED = (255, 150, 60)
SMOKE = (84, 86, 92)          # the XT's grey caps
TRACK = (150, 80, 0)
TRACK_HI = (255, 200, 120)
SHADOW = (90, 40, 0)


# ---- knobs -------------------------------------------------------------------------------------------------------------------
LIGHT = (-0.45, -0.62, 0.64)          # from the upper left (screen y down), towards the viewer


def _norm(v):
    l = math.sqrt(sum(x * x for x in v))
    return tuple(x / l for x in v)


def _knob_layers(size, red):
    """The microQ's knob seen from above, shaded per pixel at 4x: a tapered cone in dark smoky plastic, and a big flat top: a black rim
    around flat smoky plastic (red on the cutoff). The light does not turn with the knob, so these layers serve every frame; the five
    subtle side windows, which do turn, are drawn per frame between them."""
    S = 4
    n = size * S
    R = n / 2.0 - 10 * S                   # room around the knob for the value ring
    cx = cy = n / 2.0 - 1 * S
    L = _norm(LIGHT)
    H = _norm((L[0], L[1], L[2] + 1.0))
    cone = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    cap = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    pc, pt = cone.load(), cap.load()
    RC = 0.74                              # the top's radius (of R)
    RIM = 0.08                             # its black rim
    smoke = (150, 44, 36) if red else SMOKE              # smoky plastic, the yellow panel faintly through it (red on the cutoff)
    for y in range(n):
        for x in range(n):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            r = math.hypot(dx, dy) / R
            if r > 1.0:
                continue
            ux, uy = (dx / (r * R), dy / (r * R)) if r > 0 else (0.0, 0.0)
            if r >= RC - 0.01:             # the cone: dark smoky plastic, sloping out and down
                k = (r - RC) / (1 - RC)
                nn = _norm((ux * 0.62, uy * 0.62, 0.78 - 0.45 * k * k))
                dif = max(0.0, sum(a * b for a, b in zip(nn, L)))
                spc = max(0.0, sum(a * b for a, b in zip(nn, H))) ** 18
                g = (24 + 50 * dif + 60 * spc) * (1 - 0.5 * max(0.0, (r - 0.96) / 0.04))
                pc[x, y] = (int(g * 1.03), int(g), int(g * 0.92), 255)
            if r < RC:
                if r > RC - RIM:           # the black rim of the top
                    t = (r - (RC - RIM)) / RIM
                    nn = _norm((ux * (t - 0.35) * 1.5, uy * (t - 0.35) * 1.5, 1.0))
                    spc = max(0.0, sum(a * b for a, b in zip(nn, H))) ** 30
                    g = 8 + 26 * max(0.0, sum(a * b for a, b in zip(nn, L))) + 190 * spc
                    pt[x, y] = (int(g), int(g), int(g + 2), 255)
                else:                       # the flat smoky-plastic top: see-through dark, the shaft faintly under it
                    q = r / (RC - RIM)
                    lin = 1.08 - 0.16 * ((dx + dy) / (2 * R * RC) + 0.5)        # an even fall-off across the flat face
                    depth = 0.80 if r < 0.28 else 0.80 + 0.20 * min(1.0, (r - 0.28) / 0.08)   # the shaft, a little darker
                    col = [c * lin * depth for c in smoke]
                    if q > 0.93:
                        col = [c * 0.65 for c in col]                                  # a fine bevel at its edge
                    pt[x, y] = tuple(int(min(255, max(0, c))) for c in col) + (255,)
    shadow = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).ellipse((cx - R + 3 * S, cy - R + 5 * S, cx + R + 4 * S, cy + R + 7 * S), fill=SHADOW + (130,))
    shadow = shadow.filter(ImageFilter.GaussianBlur(3 * S))
    return shadow, cone, cap, (cx, cy, R, RC)


def _flutes(n, cx, cy, R, RC, rot):
    """the five clear pill windows of the sides, turned by `rot` (radians): even-width capsules from just outside the top to near the
    foot, the yellow panel faintly through them, a dark edge, a glint along the side that faces the light"""
    S = 4
    lay = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    la = math.atan2(LIGHT[1], LIGHT[0])
    r0, r1, w = RC + 0.05, 0.93, 0.13
    for i in range(5):
        phi = rot + i * 2 * math.pi / 5
        face = 0.5 + 0.5 * math.cos(phi - la)                  # 1 = facing the light
        ux, uy, vx, vy = math.cos(phi), math.sin(phi), -math.sin(phi), math.cos(phi)

        def capsule(a0, a1, hw, off, col):
            pts = []
            for k in range(24):                                 # the outer round end, then the inner one
                t = math.pi / 2 - k / 23.0 * math.pi
                pts.append((a1 + hw * math.cos(t), hw * math.sin(t) + off))
            for k in range(24):
                t = -math.pi / 2 - k / 23.0 * math.pi
                pts.append((a0 + hw * math.cos(t), hw * math.sin(t) + off))
            d.polygon([(cx + (ux * p + vx * q) * R, cy + (uy * p + vy * q) * R) for p, q in pts], fill=col)
        g = 0.55 + 0.45 * face                                   # very subtle from above: a slightly lighter, warmer window
        capsule(r0, r1, w, 0.0, (int(FLUTE[0] * g), int(FLUTE[1] * g), int(FLUTE[2] * g), 120))
        side = -1 if math.sin(phi - la) > 0 else 1
        capsule(r0 + 0.02, r1 - 0.02, 0.02, side * 0.07, (int(140 + 60 * face), int(136 + 60 * face), int(110 + 40 * face), int(40 + 50 * face)))   # glint
    return lay.filter(ImageFilter.GaussianBlur(0.6 * S))


def knob_strip(size=76, frames=101, red=False, bipolar=False, fill="ring", pointer=False):
    """The microQ knob as a film strip (-135 .. +135 degrees over the frames): cone, turning clear windows, clear top. The real knob has
    no pointer, so the value is a fill: fill="ring" an arc in a recessed track round the knob, fill="top" a lit wedge under the clear top.
    bipolar: the fill grows from the top centre either way (a -64 .. +63 value). pointer: the editor's white bar on the top as well."""
    S = 4
    shadow, cone, cap, (cx, cy, R, RC) = _knob_layers(size, red)
    n = size * S
    out = Image.new("RGBA", (size, size * frames), (0, 0, 0, 0))
    col = FILL_RED if red else FILL
    # a flat glass reflection: a straight diagonal band of light across the clear top, clipped to it
    gr = R * (RC - 0.08)
    band = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    bd = ImageDraw.Draw(band)
    for off, alpha in ((-0.38, 34), (-0.12, 16)):
        o = off * gr
        w = 0.16 * gr if alpha > 30 else 0.07 * gr
        bd.line((cx - gr * 1.2 + o, cy + gr * 1.2 + o, cx + gr * 1.2 + o, cy - gr * 1.2 + o), fill=(255, 255, 255, alpha), width=int(w))
    band = band.filter(ImageFilter.GaussianBlur(1.5 * S))
    mask = Image.new("L", (n, n), 0)
    ImageDraw.Draw(mask).ellipse((cx - gr, cy - gr, cx + gr, cy + gr), fill=255)
    glint = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    glint.paste(band, (0, 0), Image.composite(band.getchannel("A"), Image.new("L", (n, n), 0), mask))
    base = shadow.copy()
    rr = R + 6 * S                                                 # the ring's radius, its track
    if fill == "ring":
        dd = ImageDraw.Draw(base)
        box = (cx - rr, cy - rr, cx + rr, cy + rr)
        dd.arc(box, 135, 405, fill=TRACK + (150,), width=int(4 * S))
        dd.arc((box[0] + S, box[1] + S, box[2] + S, box[3] + S), 135, 405, fill=TRACK_HI + (90,), width=int(1 * S))
    base.alpha_composite(cone)
    for f in range(frames):
        t = f / (frames - 1)
        a0, a1 = (270.0, 135 + 270 * t) if bipolar else (135.0, 135 + 270 * t)
        lo, hi = min(a0, a1), max(a0, a1)
        im = base.copy()
        if fill == "ring" and hi - lo > 0.5:
            ImageDraw.Draw(im).arc((cx - rr, cy - rr, cx + rr, cy + rr), lo, hi, fill=col + (255,), width=int(4 * S))
            im.alpha_composite(cap)
        if pointer:
            pa = math.radians(-135 + 270 * t - 90)
            pd = ImageDraw.Draw(im)
            x0, y0 = cx + math.cos(pa) * R * 0.02, cy + math.sin(pa) * R * 0.02
            x1, y1 = cx + math.cos(pa) * R * (RC + 0.04), cy + math.sin(pa) * R * (RC + 0.04)
            pd.line((x0 + 1.2 * S, y0 + 1.8 * S, x1 + 1.2 * S, y1 + 1.8 * S), fill=(0, 0, 0, 100), width=int(3.6 * S))
            pd.line((x0, y0, x1, y1), fill=(250, 250, 248, 255), width=int(3.0 * S))
            pd.ellipse((x1 - 1.5 * S, y1 - 1.5 * S, x1 + 1.5 * S, y1 + 1.5 * S), fill=(250, 250, 248, 255))
        if fill == "top" and hi - lo > 0.5:
            wedge = Image.new("RGBA", (n, n), (0, 0, 0, 0))
            wr = R * (RC - 0.09)
            ImageDraw.Draw(wedge).pieslice((cx - wr, cy - wr, cx + wr, cy + wr), lo, hi, fill=col + (150,))
            ImageDraw.Draw(wedge).ellipse((cx - R * 0.3, cy - R * 0.3, cx + R * 0.3, cy + R * 0.3), fill=(0, 0, 0, 0))
            im.alpha_composite(wedge.filter(ImageFilter.GaussianBlur(1.2 * S)))
        out.paste(im.resize((size, size), Image.LANCZOS), (0, f * size))
    return out


def dial_strip(size=150, frames=101):
    """The microQ's red data dial seen from above: a plain cylinder of see-through smoky red plastic: a flat face (the shaft and its
    collar dark under it), a thin lit bevel, the wall showing a little below the face, a flat glass glint, and a concave dot near the
    rim that turns with the value (the light does not)."""
    S = 4
    n = size * S
    R = n / 2.0 - 18 * S                   # room for the wall and the shadow inside the frame
    cx, cy = n / 2.0 - 3 * S, n / 2.0 - 4 * S
    WALL = 0.035 * R                       # how much of the wall shows below the face
    L = _norm(LIGHT)
    H = _norm((L[0], L[1], L[2] + 1.0))
    smoke = (150, 24, 22)                  # smoky red: deep, a little see-through (the face lets the panel through)
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    sh = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse((cx - R + 2 * S, cy - R + WALL + 6 * S, cx + R + 6 * S, cy + R + WALL + 9 * S), fill=SHADOW + (150,))
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(4 * S)))
    px = im.load()
    for y in range(n):
        for x in range(n):
            dx = x + 0.5 - cx
            r_face = math.hypot(dx, y + 0.5 - cy) / R
            r_foot = math.hypot(dx, y + 0.5 - cy - WALL) / R
            if r_face <= 1.0:              # the flat face
                ux, uy = dx / (r_face * R + 1e-9), (y + 0.5 - cy) / (r_face * R + 1e-9)
                lin = 1.12 - 0.30 * ((dx + (y + 0.5 - cy)) / (2 * R) + 0.5)          # an even fall-off across the face
                core = 0.55 if r_face < 0.22 else 0.55 + 0.45 * min(1.0, (r_face - 0.22) / 0.12)   # the shaft under the plastic
                ring = 0.82 if 0.40 < r_face < 0.46 else 1.0                          # the collar, faintly
                edge = 1.0 + 0.55 * max(0.0, (r_face - 0.7) / 0.3) ** 2                # lit through towards the rim
                col = [c * lin * core * ring * edge for c in smoke]
                if r_face > 0.95:          # the bevel: bright on the side facing the light, dark away from it
                    nn = _norm((ux * 0.8, uy * 0.8, 0.6))
                    dif = sum(a * b for a, b in zip(nn, L))
                    spc = max(0.0, sum(a * b for a, b in zip(nn, H))) ** 12
                    col = [c * (0.55 + 0.7 * max(0.0, dif)) + 230 * spc for c in col]
                px[x, y] = tuple(int(min(255, max(0, c))) for c in col) + (int(240 + 15 * max(0.0, (r_face - 0.9) / 0.1)),)
            elif r_foot <= 1.0 and y + 0.5 > cy:   # the wall below the face: darker, a soft highlight down its front
                ux = dx / R
                g = 0.42 + 0.30 * max(0.0, 1 - abs(ux + 0.25) * 1.6)
                px[x, y] = tuple(int(c * g) for c in smoke) + (255,)
    # the glass: a straight diagonal glint across the face, clipped to it
    band = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    bd = ImageDraw.Draw(band)
    for off, alpha, w in ((-0.42, 72, 0.20), (-0.15, 34, 0.07)):
        o = off * R
        bd.line((cx - R * 1.2 + o, cy + R * 1.2 + o, cx + R * 1.2 + o, cy - R * 1.2 + o), fill=(255, 235, 230, alpha), width=int(w * R))
    band = band.filter(ImageFilter.GaussianBlur(2 * S))
    mask = Image.new("L", (n, n), 0)
    ImageDraw.Draw(mask).ellipse((cx - R * 0.95, cy - R * 0.95, cx + R * 0.95, cy + R * 0.95), fill=255)
    glint = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    glint.paste(band, (0, 0), Image.composite(band.getchannel("A"), Image.new("L", (n, n), 0), mask))
    # the concave dot: shaded per pixel (dark on the side towards the light, lit on the far side), drawn once and pasted per frame
    dr = int(R * 0.135)
    dot = Image.new("RGBA", (2 * dr + 2, 2 * dr + 2), (0, 0, 0, 0))
    dp = dot.load()
    for yy in range(2 * dr + 2):
        for xx in range(2 * dr + 2):
            ex, ey = (xx + 0.5 - dr - 1) / dr, (yy + 0.5 - dr - 1) / dr
            q = math.hypot(ex, ey)
            if q > 1.0:
                continue
            nn = _norm((-ex * 0.9, -ey * 0.9, 1.0))                  # a dish: the normals point inwards
            dif = sum(a * b for a, b in zip(nn, L))
            a = 210 if q < 0.9 else int(210 * (1 - q) / 0.1)
            if dif < 0.62:
                dp[xx, yy] = (40, 2, 2, int(a * min(1.0, (0.62 - dif) * 2.4)))
            else:
                dp[xx, yy] = (255, 150, 130, int(a * min(1.0, (dif - 0.62) * 1.6)))
    out = Image.new("RGBA", (size, size * frames), (0, 0, 0, 0))
    for f in range(frames):
        fr = im.copy()
        a = math.radians(-135 + 270 * f / (frames - 1) - 90)
        x, y = cx + math.cos(a) * R * 0.70, cy + math.sin(a) * R * 0.70
        fr.alpha_composite(dot, (int(x - dr - 1), int(y - dr - 1)))
        fr.alpha_composite(glint)
        out.paste(fr.resize((size, size), Image.LANCZOS), (0, f * size))
    return out


