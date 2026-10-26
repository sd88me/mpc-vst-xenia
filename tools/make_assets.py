#!/usr/bin/env python3
"""Regenerate the images that come from the Xenia skin art (skin/xtDefault/, by theusualsuspects, from gearmulator's Xenia plugin):
  skin/parts/bg.png            the amber chassis gradient at the MPC's 1280 x 628 (cut from the empty lower part of xtDefaultBG.png)
  skin/parts/wordmark.png      the "theusualsuspects / Xenia" script logo with its own alpha (the chassis gradient taken out)
  skin/parts/sock_*.png        the black knob head, for the drawn rings under each live knob
  vst/images/knob_xenia.png    the knob filmstrip (xt_encoder_ranged.png: 90 frames of 128 px, resampled to 101 frames of 104 px)
  vst/images/tile.png          the 270x110 Instruments-browser tile
Committed outputs, so building does not need the 3400 x 2000 source."""
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
SK = os.path.join(ROOT, "skin", "xtDefault")
PARTS = os.path.join(ROOT, "skin", "parts")
IMG = os.path.join(ROOT, "vst", "images")
os.makedirs(PARTS, exist_ok=True)
os.makedirs(IMG, exist_ok=True)

bg = Image.open(os.path.join(SK, "xtDefaultBG.png")).convert("RGBA")
W, H = 1280, 628

# chassis: the part of the background under the header strip carries only the gradient and a fine grain
strip = bg.crop((60, 480, 3340, 1980)).resize((W, H), Image.LANCZOS).convert("RGB")
strip.save(os.path.join(PARTS, "bg.png"))

# wordmark: bbox in the 3400 x 2000 art, then per-row chassis estimate from the columns either side of it
x0, y0, x1, y1 = 1500, 40, 2130, 430
wm = bg.crop((x0, y0, x1, y1)).convert("RGB")
px = bg.convert("RGB").load()
out = Image.new("RGBA", wm.size)
op, wp = out.load(), wm.load()
for y in range(wm.height):
    l = px[x0 - 12, y0 + y]
    r = px[x1 + 12, y0 + y]
    for x in range(wm.width):
        t = x / (wm.width - 1)
        est = [l[c] * (1 - t) + r[c] * t for c in range(3)]
        p = wp[x, y]
        d = max(abs(p[c] - est[c]) for c in range(3))
        a = max(0, min(255, int((d - 14) * 9)))
        if a == 0:
            op[x, y] = (0, 0, 0, 0)
            continue
        al = a / 255.0
        col = tuple(max(0, min(255, int((p[c] - est[c] * (1 - al)) / al))) for c in range(3))     # un-premultiply against the chassis
        op[x, y] = col + (a,)
out.save(os.path.join(PARTS, "wordmark.png"))

# knob filmstrip
src = Image.open(os.path.join(SK, "xt_encoder_ranged.png")).convert("RGBA")
n = src.height // 128
film = Image.new("RGBA", (104, 104 * 101))
for i in range(101):
    f = i * (n - 1) // 100
    film.paste(src.crop((0, f * 128, 128, f * 128 + 128)).resize((104, 104), Image.LANCZOS), (0, i * 104))
film.save(os.path.join(IMG, "knob_xenia.png"))

# a still knob head for the drawn rings (the strip's first frame)
src.crop((0, 0, 128, 128)).save(os.path.join(PARTS, "knob_head.png"))

# browser tile
tile = Image.new("RGB", (270, 110))
tile.paste(strip.resize((270, 110), Image.LANCZOS))
l = out.crop(out.getbbox())
s = min(250 / l.width, 100 / l.height)
l = l.resize((int(l.width * s), int(l.height * s)), Image.LANCZOS)
tile.paste(l, ((270 - l.width) // 2, (110 - l.height) // 2), l)
tile.save(os.path.join(IMG, "tile.png"))
print("skin/parts/{bg,wordmark,knob_head}.png, vst/images/{knob_xenia,tile}.png")
