#!/usr/bin/env python3
"""Xenia's native MPC skin: skins/TUI.json + skins/Q-Links.json + the PNGs they use (docs/SDK-PORTING.md, after Welle's tools/make_tui.py).

The pages are the 1.x pages: the static art of tools/make_skin.py (vst/images/page_*.png: the Xenia skin's amber chassis, wordmark, blue
italic headings, captions) with native widgets at the positions of vst/layout.conf, drawn in the same look (the xtDefault knob film strip,
olive LCD fields, blue keys, dark toggles). BANKS is drawn here. Popups become drawn pick-lists on ONE ui_open parameter (index 253; value
k + 1 = the k-th popup of params.lock, the same order the front end uses). Q-Links: blocks of four, one outlined block per printed row of
controls, at most four blocks per sub-tab (more blocks -> sub-tabs with the same art).
    python3 tools/make_tui.py              # -> skins/   (build-sdk.sh copies it to Plugin Skins/)
    python3 tools/make_tui.py --preview    # also docs/skin-preview/<tab>.png, composed offline (Titillium at the MPC's text scale)"""
import hashlib, json, os, re, shutil, sys
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.environ.get("XENIA_SKIN_OUT", os.path.join(ROOT, "skins"))
PREVIEW = os.path.join(ROOT, "docs", "skin-preview")
VST_IMG = os.path.join(ROOT, "vst", "images")
LIB = os.path.join(ROOT, "vst", "fonts")
TIT = os.path.join(ROOT, "tools", "fonts", "TitilliumWeb-SemiBold.ttf")
TIT_B = os.path.join(ROOT, "tools", "fonts", "TitilliumWeb-Bold.ttf")
LOCK = [l.strip() for l in open(os.path.join(ROOT, "params.lock")) if l.strip()]
PARAMS = {p["key"]: p for p in json.load(open(os.path.join(ROOT, "vst", "params.json")))["params"]}
POPUPS = [k[:-len("__open")] for k in LOCK if k.endswith("__open")]
UI_INDEX = LOCK.index(POPUPS[0] + "__open")          # 253: ui_open sits in the first popup slot
UI_STEPS = len(POPUPS) + 1                           # 0 closed, k + 1 = pick-list k
TUI_PX = 0.66                                        # a Label draws its text at about 0.66 x its height (Crate Digger, on the device)
GESTURE = "Instant"


def PI(key):
    return UI_INDEX if key == "ui_open" else LOCK.index(key)


def P(key):
    return "Parameter %d" % PI(key)


# ---- colours: the 1.x skin's (vst/layout.conf theme_*, tools/make_skin.py) ------------------------------------------------
INK = (38, 22, 6)
BLUE = (22, 118, 178)
BLUE_HI = (59, 155, 216)
LCD = (182, 206, 72)          # every LCD-style field in the classic yellow-green backlit LCD, dark characters
LCD_CELL = (172, 196, 64)
LCD_EDGE = (92, 108, 36)
LCD_INK = (22, 26, 8)
BEZEL = (184, 40, 29)
BEZEL_DK = (122, 24, 16)
DARK = (31, 36, 20)
WHITE = (255, 255, 255)


# ---- a JSON writer that keeps key order and allows the overlay widgets' duplicate "version" key ------------------------------
class Obj:
    def __init__(self, *pairs):
        self.pairs = list(pairs)


def dump(v, ind=0):
    sp = "  " * ind
    pairs = v.pairs if hasattr(v, "pairs") else list(v.items()) if isinstance(v, dict) else None
    if pairs is not None:
        if not pairs:
            return "{}"
        return "{\n%s\n%s}" % (",\n".join("%s  %s: %s" % (sp, dump(k), dump(x, ind + 1)) for k, x in pairs), sp)
    if isinstance(v, list):
        return "[]" if not v else "[\n%s\n%s]" % (",\n".join(sp + "  " + dump(x, ind + 1) for x in v), sp)
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(v) if isinstance(v, float) else str(v)
    return '"%s"' % str(v).replace("\\", "\\\\").replace('"', '\\"')


# ---- TUI building blocks --------------------------------------------------------------------------------------------------
def bounds(b, focus="No", ind=None):
    d = {"version": 2 if ind else 1, "acceptsHWFocus": focus, "showWhenDataModelInvalid": "Show", "whenVisible": "Always",
         "boundsType": "Absolute", "bounds": b}
    if ind:
        d["additionalInvalidatingHandles"] = ind
    return d


def entry(name, typ, data, b, remap=None, focus="No", ind=None, visible=None):
    e = {"version": 2, "componentData": {"version": 1, "name": name, "type": typ, "data": data},
         "handle remapping": {"version": 1, "map": [{"key": "Data", "value": remap}] if remap else []},
         "bounds": bounds(b, focus, ind)}
    if visible:
        e["bounds"]["whenVisible"] = visible
    return e


def bg(colour="0", image=""):
    return {"version": 1, "focussed": {"version": 1, "colour": colour, "image": image}, "unfocussed": {"version": 1, "colour": colour, "image": image}}


def component(children, actions=None, ignore=False, background=None):
    return {"version": 2, "actions": actions or [], "backgroundData": background or bg(), "ignoreMousePresses": ignore,
            "disableCoarseDataWheel": False, "componentsData": children}


def act(on, handler, extra=""):
    return {"version": 1, "onAction": on, "handler": handler, "handleName": "Data", "additionalData": extra}


def text_style(h, colour, just="horizontallyCentred verticallyCentred", style="SemiBold"):
    return {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": style, "height": float(h)}, "colour": colour,
            "justification": just, "case": "Original"}


def label(b, key, h=20, colour="ff26160a", just="horizontallyCentred verticallyCentred", style="SemiBold"):
    return entry("label", "Label", {"version": 1, "textStyle": text_style(h, colour, just, style), "type": "Value", "handleName": "Data"}, b, P(key))


KNOB = 64                     # the drawn knob: the xtDefault encoder at 64 px (1.x: r = 29 + its shadow)
FOCUS = {"version": 1, "backgroundColour": "00000000", "outlineColour": "ff1676b2", "backgroundInset": 4.0, "outlineThickness": 2.0}


def definitions():
    knob = component([
        entry("focus", "Focus", FOCUS, "-6 -6 %d %d" % (KNOB + 12, KNOB + 12), visible="WhenFocussed"),
        entry("knob", "Knob", {"version": 3, "knobType": "FilmStrip", "filmStrip": "knob_xt.png", "numFrames": 101, "invert": False,
                               "dragOrientation": "Vertical", "handleName": "Data"}, "0 0 %d %d" % (KNOB, KNOB)),
    ], actions=[act("Mouse Down", "Q-Link"), act("Double Click", "Show Overlay", "knob overlay"), act("Enter Pressed", "Show Overlay", "knob overlay")])
    knob_overlay = component([entry("overlay", "KnobOverlay", Obj(
        ("version", 2), ("version", 1),
        ("actions", [{"version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": ""}]),
        ("overlayType", "Knob With Header"), ("scaleType", "Linear"), ("polarityType", "Unipolar"),
        ("showMinusSign", True), ("handleName", "Data")), "0 0 1280 800", focus="Yes")], ignore=True)
    return [{"key": "xknob", "value": knob}, {"key": "knob overlay", "value": knob_overlay}]


# ---- art ---------------------------------------------------------------------------------------------------------------------
_fonts = {}


def F(size, path=None):
    path = path or os.path.join(LIB, "LiberationSans-Bold.ttf")
    if (size, path) not in _fonts:
        _fonts[(size, path)] = ImageFont.truetype(path, size)
    return _fonts[(size, path)]


def save(im, name):
    im.save(os.path.join(OUT, name))
    return name


def lcd_box(d, x0, y0, x1, y1, r=3):
    """The 1.x olive LCD field: a darker edge, the cell colour, a fine dot grid."""
    d.rounded_rectangle((x0, y0, x1, y1), r, fill=LCD_EDGE)
    d.rounded_rectangle((x0 + 2, y0 + 2, x1 - 2, y1 - 2), r, fill=LCD)
    for y in range(int(y0) + 5, int(y1) - 3, 3):
        for x in range(int(x0) + 5, int(x1) - 3, 3):
            d.point((x, y), fill=LCD_CELL)


def popup_box(d, cx, cy, w, h):
    lcd_box(d, cx - w // 2, cy - h // 2, cx + w // 2, cy + h // 2)
    ax = cx + w // 2 - 20
    d.polygon([(ax - 8, cy - 4), (ax + 8, cy - 4), (ax, cy + 5)], fill=LCD_INK)


def key_image(w, h, caption, lit, size=17):
    """A blue Xenia key (PANIC, INIT PATCH, +FAV ...): lit = the brighter blue."""
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 2, w - 1, h - 1), 4, fill=(10, 60, 92))
    d.rounded_rectangle((0, 0, w - 1, h - 3), 4, fill=BLUE_HI if lit else BLUE)
    d.line((4, 1, w - 5, 1), fill=(120, 190, 235) if lit else (70, 150, 205))
    d.text((w // 2, h // 2 - 1), caption, font=F(size), fill=WHITE, anchor="mm")
    return im


def toggle_image(lit):
    """The 1.x switch: a dark pill, the blue dot on the right when on, a grey one on the left when off."""
    w, h = 56, 30
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w - 1, h - 1), h // 2, fill=DARK, outline=(12, 14, 8), width=2)
    cx = w - h // 2 if lit else h // 2
    d.ellipse((cx - 10, h // 2 - 10, cx + 10, h // 2 + 10), fill=BLUE_HI if lit else (108, 100, 112))
    return im


def arrow_image(w, h, direction, lit):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx, cy, s = w // 2, h // 2, 10
    tip, base = cx + direction * s * 0.7, cx - direction * s * 0.7
    d.polygon([(tip, cy), (base, cy - s), (base, cy + s)], fill=WHITE if lit else LCD)
    return im


def list_key(w, h, caption, lit, size=15):
    """A pick-list row: an olive cell, lit = the current choice (blue, white text)."""
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if lit:
        d.rounded_rectangle((0, 0, w - 1, h - 1), 3, fill=BLUE, outline=BLUE_HI, width=2)
    else:
        d.rounded_rectangle((0, 0, w - 1, h - 1), 3, fill=LCD, outline=LCD_EDGE, width=1)
    f = F(size, TIT_B)
    while f.getlength(caption) > w - 10 and size > 10:
        size -= 1
        f = F(size, TIT_B)
    d.text((w // 2, h // 2), caption, font=f, fill=WHITE if lit else LCD_INK, anchor="mm")
    return im


def knob_strip():
    """The xtDefault encoder (vst/images/knob_xenia.png: 101 frames of 104 px) at KNOB px."""
    src = Image.open(os.path.join(VST_IMG, "knob_xenia.png")).convert("RGBA")
    n, fh = 101, src.width
    out = Image.new("RGBA", (KNOB, KNOB * n), (0, 0, 0, 0))
    for i in range(n):
        out.paste(src.crop((0, i * fh, fh, (i + 1) * fh)).resize((KNOB, KNOB), Image.LANCZOS), (0, i * KNOB))
    return save(out, "knob_xt.png")


CLEAR = "clear.png"


# ---- pages --------------------------------------------------------------------------------------------------------------------
class Page:
    def __init__(self, name, fn, sub=0):
        self.name, self.fn, self.sub = name, fn, sub
        self.kids, self.groups, self.qrows = [], [], []

    def tab(self, bgfile):
        kids = [entry("bg", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": bgfile}, "0 0 1280 628")] + self.kids
        return {"version": 1, "tabName": self.name, "fnKeyIndex": self.fn, "fnKeySubIndex": self.sub,
                "componentDefinition": component(kids, background=bg("ffe07400")), "qlinkBoundsData": self.groups}

    def qgroup(self, rect, keys):
        self.groups.append("%d %d %d %d" % (rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]))
        self.qrows.append(keys)


def button(kids, key, b, on, off, bid=1, n=1, name=None):
    kids.append(entry(name or key, "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": bid, "numButtonsInGroup": n,
                                              "handleName": "Data", "gestureBehaviour": GESTURE}, b, remap=P(key)))


def key_button(kids, key, cx, cy, caption, w=None, h=46):
    w = w or max(120, int(F(17).getlength(caption)) + 56)
    on = save(key_image(w, h, caption, True), "key_%s_%dx%d_on.png" % (key, w, h))
    off = save(key_image(w, h, caption, False), "key_%s_%dx%d_off.png" % (key, w, h))
    button(kids, key, "%d %d %d %d" % (cx - w // 2, cy - h // 2, w, h), on, off)


PICK_DEFS = {}


def pick_panel(key, opts):
    """A drawn pick-list for the choice `key`: a panel with one radio Button per option (text baked in) and a close key, shown only
    while ui_open == its popup index + 1. Shared per option list. Centred on the page; columns grow to fit the height."""
    n = POPUPS.index(key)
    hdr, gap = 46, 4
    ch = 44 if len(opts) <= 24 else 40 if len(opts) <= 40 else 34
    rows_max = (600 - hdr - 16) // (ch + gap)
    cols = 1 if len(opts) <= 8 else max(2, -(-len(opts) // rows_max))
    cols = max(cols, 3) if len(opts) > 12 else cols
    rows = -(-len(opts) // cols)
    cw = 260 if cols == 1 else min(236, (1240 - 16) // cols - gap)
    pw, ph = cols * (cw + gap) + gap + 12, hdr + rows * (ch + gap) + gap + 10
    ck = "list_%d_%s" % (len(opts), hashlib.md5("|".join(opts).encode()).hexdigest()[:8])
    if ck not in PICK_DEFS:
        im = Image.new("RGBA", (pw, ph), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle((0, 0, pw - 1, ph - 1), 8, fill=BEZEL_DK)
        d.rounded_rectangle((3, 3, pw - 4, ph - 4), 7, fill=BEZEL)
        d.text((16, hdr // 2 + 1), "SELECT", font=F(19, os.path.join(LIB, "LiberationSans-BoldItalic.ttf")), fill=WHITE, anchor="lm")
        kids = [entry("panel", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": save(im, ck + "_bg.png")}, "0 0 %d %d" % (pw, ph))]
        size = 16 if cw >= 200 else 14
        for i, o in enumerate(opts):
            r, c = i % rows, i // rows                 # down the columns
            on = save(list_key(cw, ch, o, True, size), "%s_%d_on.png" % (ck, i))
            off = save(list_key(cw, ch, o, False, size), "%s_%d_off.png" % (ck, i))
            kids.append(entry("opt %d" % i, "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": i, "numButtonsInGroup": len(opts),
                                                        "handleName": "Data", "gestureBehaviour": GESTURE},
                              "%d %d %d %d" % (8 + gap + c * (cw + gap), hdr + r * (ch + gap), cw, ch)))
        xon = save(key_image(56, 34, "X", True, 18), "x_on.png")
        xoff = save(key_image(56, 34, "X", False, 18), "x_off.png")
        kids.append(entry("close", "Button", {"version": 2, "onImage": xon, "offImage": xoff, "buttonId": 0, "numButtonsInGroup": UI_STEPS,
                                              "handleName": "Data", "gestureBehaviour": GESTURE}, "%d 6 56 34" % (pw - 66), remap=P("ui_open")))
        PICK_DEFS[ck] = (pw, ph, component(kids))
    pw, ph, _ = PICK_DEFS[ck]
    x, y = (1280 - pw) // 2, max(4, (628 - ph) // 2)
    return entry("list " + key, ck, {"version": 1, "handleName": "Data"}, "%d %d %d %d" % (x, y, pw, ph), remap=P(key),
                 ind=["IndexedEnabling/%d/%d/Parameter %d" % (n + 1, UI_STEPS, UI_INDEX)])


def parse_layout():
    tabs, order, cur = {}, [], None
    for line in open(os.path.join(ROOT, "vst", "layout.conf")):
        line = line.strip()
        m = re.match(r"\[tab (.+)\]", line)
        if m:
            cur = tabs.setdefault(m.group(1), {"items": [], "q": [], "art": None})
            order.append(m.group(1))
            continue
        if not cur or not line or line.startswith("#"):
            continue
        if line.startswith("qlinks"):
            cur["q"].append([k.strip() for k in line.split("=", 1)[1].split(",")])
            continue
        kind, rest = line.split(None, 1)
        attrs = {k: v.strip('"') for k, v in re.findall(r'(\w+)=("[^"]*"|\S+)', rest)}
        if kind == "art":
            cur["art"] = attrs["file"]
        else:
            cur["items"].append((kind, attrs))
    return tabs, order


# PLAY: status and stats move into the display, so the Engine section holds the keys: RANDOM / +FAV / -FAV, then PANIC / INIT PATCH
PLAY_MOVE = {"all_notes_off": (850, 254), "init_patch": (1030, 254)}


def layout_tab(name, fn, spec):
    """One printed page at the layout.conf positions; one Page per sub-tab (same art, other Q-Link blocks)."""
    art = panel(Image.open(os.path.join(VST_IMG, os.path.basename(spec["art"]))).convert("RGB"))
    d = ImageDraw.Draw(art)
    pos, kids, panels = {}, [], []
    for kind, a in spec["items"]:
        key = a.get("key")
        if key not in PARAMS and kind != "stepper":
            continue
        cx, cy = int(a["cx"]), int(a["cy"]) - 86
        if kind == "popup":
            w, h = int(a["w"]), max(int(a["h"]), 36)
            popup_box(d, cx, cy, w, h)
            kids.append(label("%d %d %d %d" % (cx - w // 2 + 8, cy - h // 2, w - 40, h), key, 21, "ff161a08", "left verticallyCentred"))
            kids.append(entry(key + " field", "Button", {"version": 2, "onImage": CLEAR, "offImage": CLEAR, "buttonId": POPUPS.index(key) + 1,
                                                         "numButtonsInGroup": UI_STEPS, "handleName": "Data", "gestureBehaviour": GESTURE},
                              "%d %d %d %d" % (cx - w // 2, cy - h // 2, w, h), remap=P("ui_open")))
            panels.append(pick_panel(key, PARAMS[key]["options"]))
        elif kind == "knob":
            pos[key] = (cx, cy)
            kids.append(entry(key, "xknob", {"version": 1, "handleName": "Data"}, "%d %d %d %d" % (cx - KNOB // 2, cy - KNOB // 2, KNOB, KNOB),
                              remap=P(key), focus="Yes"))
            kids.append(label("%d %d 120 26" % (cx - 60, cy + 44), key, 21))
        elif kind == "toggle":
            pos[key] = (cx, cy)
            button(kids, key, "%d %d 56 30" % (cx - 28, cy - 15), save(toggle_image(True), "tg_on.png"), save(toggle_image(False), "tg_off.png"))
        elif kind == "button":
            cx, cy = PLAY_MOVE.get(key, (cx, cy))
            key_button(kids, key, cx, cy, a["label"])
        elif name == "PLAY" and kind in ("readout", "stepper"):
            continue                  # drawn in the display below
        elif kind == "readout":
            w, h = int(a["w"]), int(a["h"])
            lcd_box(d, cx - w // 2, cy - h // 2, cx + w // 2, cy + h // 2)
            kids.append(label("%d %d %d %d" % (cx - w // 2 + 6, cy - h // 2, w - 12, h), key, 21, "ff161a08"))
        elif kind == "stepper":
            w, h = int(a["w"]), int(a["h"])
            stepper(d, kids, cx, cy, w, h, a["get"], a["prev"], a["next"])
    if name == "PLAY":
        play_display(art, d, kids)
    pages = []
    for sub, blocks in enumerate(auto_blocks(spec, pos, name)):
        p = Page(name, fn, sub)
        p.kids = list(kids) + list(panels)
        for keys, rect in blocks:
            p.qgroup(rect, keys)
        pages.append(p)
    return pages, art


def play_display(art, d, kids):
    """PLAY's header: one two-row display in the red bezel (bank | sound with step arrows; engine status | stats), and the patch keys."""
    for x0, y0, x1, y1 in ((740, 158, 840, 182), (1050, 158, 1140, 182)):   # the 1.x "Status" / "Stats" captions: fill from the left
        art.paste(art.crop((x0 - 110, y0, x1 - 110, y1)), (x0, y0))
    art.paste(art.crop((250, 116, 1280, 126)).resize((1030, 116)), (250, 0))     # clear the 1.x red bezel to plain panel
    lens(art, (262, 0, 1278, 124), (420, 30, 1150, 94))
    display(d, 420, 30, 1150, 94)
    display_row(d, kids, 420, 1150, 30, 64, [(704, "bank_name", "bank_prev", "bank_next"),
                                            (1150, "preset_name", "preset_prev", "preset_next")], 26)
    d.line((426, 65, 1144, 65), fill=(120, 140, 40), width=1)
    display_row(d, kids, 420, 1150, 66, 94, [(770, "loading_status", None, None), (1150, "engine_stats", None, None)], 18)
    key_button(kids, "random_patch", 760, 190, "RANDOM", w=150)
    key_button(kids, "fav_add", 940, 190, "+ FAV", w=150)
    key_button(kids, "fav_del", 1120, 190, "- FAV", w=150)


def stepper(d, kids, cx, cy, w, h, get, prev, nxt):
    """An LCD field with an arrow key either side (the header of PLAY and BANKS)."""
    aw = 40
    lcd_box(d, cx - w // 2 + aw, cy - h // 2, cx + w // 2 - aw, cy + h // 2)
    kids.append(label("%d %d %d %d" % (cx - w // 2 + aw + 12, cy - h // 2, w - 2 * aw - 24, h), get, 22, "ff161a08", "left verticallyCentred"))
    for k, bx, dr in ((prev, cx - w // 2, -1), (nxt, cx + w // 2 - aw, 1)):
        button(kids, k, "%d %d %d %d" % (bx, cy - h // 2, aw, h), save(arrow_image(aw, h, dr, True), "arrow_%d_on.png" % dr),
               save(arrow_image(aw, h, dr, False), "arrow_%d_off.png" % dr))


# ---- the Microwave XT panel print: red rings around the data encoder, a few thin cream sweeps -------------------------------
ARC_CENTRE = (700, 40)       # where the XT's big red encoder sits (top middle); every page shares it, so the rings line up across tabs


def panel(art):
    """Print the XT's ring pattern on a 1.x page: only on the plain orange (captions, headings and rules stay clean)."""
    w, h = art.size
    rings = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(rings)
    cx, cy = ARC_CENTRE
    r, step = 150, 46
    while r < 1500:
        d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=150 if r < 400 else 105, width=3 if r < 400 else 2)
        r += step
        step = int(step * 1.18)
    sweeps = Image.new("L", (w, h), 0)
    ds = ImageDraw.Draw(sweeps)
    for x0, x1 in ((330, 520), (610, 760), (1070, 1150)):      # the long thin lines that cut across the sections
        ds.line((x0, 0, x1, h), fill=70, width=2)
    for r in (520, 980):
        ds.arc((cx + 300 - r, cy + 900 - r, cx + 300 + r, cy + 900 + r), 200, 300, fill=60, width=2)
    rgb = art.load()
    orange = Image.new("L", (w, h), 0)
    op = orange.load()
    for y in range(h):
        for x in range(w):
            R, G, B = rgb[x, y][:3]
            if R > 215 and B < 60 and 95 < G < 175:
                op[x, y] = 255
    orange = orange.filter(ImageFilter.MinFilter(5))             # keep a little air around captions and rules
    red = Image.new("RGB", (w, h), (226, 52, 44))
    cream = Image.new("RGB", (w, h), (255, 214, 120))
    art = Image.composite(red, art, ImageChops.multiply(rings, orange))
    art = Image.composite(cream, art, ImageChops.multiply(sweeps, orange))
    return art


def lens(art, rect, window, pill=False):
    """The XT's display lens: a pale periwinkle glass (an ellipse, or a pill where the header is too low for one) with a lit top,
    and the LCD window set INTO it: a recessed rim (shadow above, light below) with a clear margin of glass all round."""
    S = 4
    x0, y0, x1, y1 = rect
    w, h = x1 - x0, y1 - y0
    im = Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    def shape(i, col):
        box = (i * S, i * S, (w - i) * S - 1, (h - i) * S - 1)
        if pill:
            d.rounded_rectangle(box, (h // 2 - i) * S, fill=col)
        else:
            d.ellipse(box, fill=col)
    shape(0, (96, 100, 176, 255))                 # a thin rim, then the flat glass: printed flush with the panel
    shape(2, (160, 166, 240, 255))
    wx0, wy0, wx1, wy1 = [(v - o) * S for v, o in zip(window, (x0, y0, x0, y0))]
    m = 7 * S
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((wx0 - 3 * S, wy0 - 3 * S, wx1 + 3 * S, wy1 + 3 * S), 3 * S, fill=(40, 42, 70, 255))  # the window frame
    im = im.resize((w, h), Image.LANCZOS)
    base = art.convert("RGBA")
    base.alpha_composite(im, (x0, y0))
    art.paste(base.convert("RGB"))


XT_LCD = (182, 206, 72)       # the display: the classic yellow-green backlit LCD, dark characters
XT_INK = (22, 26, 8)


# ---- the combined display: one olive LCD in the red bezel, fields split by printed rules, step arrows printed in ink ---------
def display(d, x0, y0, x1, y1):
    """A large backlit LCD (static art): a dark glass edge, a top inner shadow, the 1.x olive with its dot grid."""
    for y in range(y0, y1 + 1):                                                   # backlit: brighter in the middle
        t = abs((y - y0) / max(1, y1 - y0) - 0.45) * 2
        d.line((x0, y, x1, y), fill=(int(198 - 40 * t), int(222 - 40 * t), int(86 - 34 * t)))
    for x in range(x0 + 4, x1 - 3, 12):                                           # the character cells of the 2 x 40 LCD
        d.line((x, y0 + 4, x, y1 - 4), fill=(176, 200, 66))
    for i, c in enumerate(((96, 112, 30), (126, 146, 44), (156, 178, 58))):       # the glass shadow under the frame
        d.line((x0 + 1, y0 + 1 + i, x1 - 1, y0 + 1 + i), fill=c)


def hit_zone(kids, key, x, y, w, h):
    """An invisible momentary touch zone over a printed arrow (a dark flash while held)."""
    on = "hit_%dx%d.png" % (w, h)
    if not os.path.exists(os.path.join(OUT, on)):
        im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        ImageDraw.Draw(im).rounded_rectangle((2, 2, w - 3, h - 3), 4, fill=LCD_INK + (60,))
        save(im, on)
    button(kids, key, "%d %d %d %d" % (x, y, w, h), on, CLEAR, name=key + " hit")


def display_row(d, kids, x0, x1, y0, y1, fields, size, aw=40):
    """One LCD row of fields [(x_end, get, prev, next)]: a rule between fields, ink arrows at both ends of a field with prev/next."""
    xs = x0
    for i, (xe, get, prev, nxt) in enumerate(fields):
        if i:
            d.line((xs, y0 + 6, xs, y1 - 6), fill=XT_INK, width=1)
        cy = (y0 + y1) // 2
        pad = aw if prev else 0
        if prev:
            for k, ax, dr in ((prev, xs, -1), (nxt, xe - aw, 1)):
                c, s = ax + aw // 2, 9
                d.polygon([(c + dr * s * 0.7, cy), (c - dr * s * 0.7, cy - s), (c - dr * s * 0.7, cy + s)], fill=XT_INK)
                hit_zone(kids, k, ax, y0, aw, y1 - y0)
        kids.append(label("%d %d %d %d" % (xs + pad + 10, y0, xe - xs - 2 * pad - 16, y1 - y0), get, size, "ff161a08", "left verticallyCentred"))
        xs = xe


# Q-Link blocks where the 1.x sets don't make clean blocks of four (rearranged for the hardware groups; the art is unchanged)
BLOCKS = {
    "WAVE ENV": [["WaveEnvTime%d" % i for i in range(1, 5)], ["WaveEnvTime%d" % i for i in range(5, 9)],
                 ["WaveEnvLevel%d" % i for i in range(1, 5)], ["WaveEnvLevel%d" % i for i in range(5, 9)]],
    "MOD": [["Mod1Parameter", "Mod3Parameter"], ["Mod2Parameter", "Mod4Parameter"], ["ModDelayTime"]],
    # sub-tab 1 the free envelope, sub-tab 2 both LFOs (the 1.x order left LFO 2 Phase alone on a sub-tab)
    "ENV & LFO": [["FreeEnvTime1", "FreeEnvLevel1", "FreeEnvTime2", "FreeEnvLevel2"], ["FreeEnvTime3", "FreeEnvLevel3", "FreeEnvReleaseTime", "FreeEnvReleaseLevel"],
                  None, None,
                  ["Lfo1Rate", "Lfo1Delay", "Lfo1Symmetry", "Lfo1Humanize"], ["Lfo2Rate", "Lfo2Delay", "Lfo2Symmetry", "Lfo2Humanize"], ["Lfo2Phase"]],
}


def auto_blocks(spec, pos, name=None):
    """The 1.x Q-Link sets (each 4 keys a column there), as blocks of four on the page: each block's rectangle is the bounding box of its
    controls (caption above to value below). At most four blocks per sub-tab."""
    blocks = []
    lines = [b + ["-"] * (4 - len(b)) if b else None for b in BLOCKS[name]] if name in BLOCKS else spec["q"]
    for line in lines:
        if line is None:              # an empty slot: pads the sub-tab so the next block starts a new one
            blocks.append(None)
            continue
        for i in range(0, len(line), 4):
            ks = [k for k in line[i:i + 4] if k != "-" and k in pos]
            if ks:
                pts = [pos[k] for k in ks]
                rect = (min(x for x, _ in pts) - 52, min(y for _, y in pts) - 62, max(x for x, _ in pts) + 52, max(y for _, y in pts) + 74)
                blocks.append((ks, rect))
    return [[b for b in blocks[i:i + 4] if b] for i in range(0, len(blocks), 4)] or [[]]


def banks_page(fn):
    """BANKS: the bank tiles (left), the 30 sounds of the page (3 x 10, down the columns), BANK / PRESET / PAGE steppers in the red header,
    RANDOM and the favourite keys. Tile names are Value labels UNDER transparent Buttons (a label on top would swallow the tap)."""
    p = Page("BANKS", fn)
    art = panel(Image.open(os.path.join(VST_IMG, "page_BANKS.png")).convert("RGB"))
    d = ImageDraw.Draw(art)
    hf = F(22, os.path.join(LIB, "LiberationSans-BoldItalic.ttf"))

    def heading(x0, x1, y, title):
        tw = int(hf.getlength(title))
        cx = (x0 + x1) // 2
        d.line((x0, y, cx - tw // 2 - 12, y), fill=BLUE, width=2)
        d.line((cx + tw // 2 + 12, y, x1, y), fill=BLUE, width=2)
        d.text((cx, y), title, font=hf, fill=BLUE, anchor="mm")

    # header: the red bezel of PLAY's LCD: one display (bank | sound, with step arrows), RANDOM and the favourite keys
    lens(art, (2, 0, 840, 70), (58, 15, 790, 55), pill=True)
    display(d, 58, 15, 790, 55)
    display_row(d, p.kids, 58, 790, 15, 55, [(340, "bank_name", "bank_prev", "bank_next"),
                                             (790, "preset_name", "preset_prev", "preset_next")], 25)
    key_button(p.kids, "random_patch", 914, 34, "RANDOM", w=150, h=42)
    key_button(p.kids, "fav_add", 1062, 34, "+ FAV", w=124, h=42)
    key_button(p.kids, "fav_del", 1196, 34, "- FAV", w=124, h=42)
    lit = Image.new("RGBA", (300, 60), (0, 0, 0, 0))
    ImageDraw.Draw(lit).rounded_rectangle((0, 0, 299, 59), 4, outline=BLUE, width=4, fill=(22, 118, 178, 60))

    def tile(key, x, y, w, h):
        lcd_box(d, x, y, x + w - 1, y + h - 1)
        p.kids.append(label("%d %d %d %d" % (x + 10, y, w - 16, h), key, 21, "ff161a08", "left verticallyCentred"))
        on = "tile_on_%dx%d.png" % (w, h)
        if not os.path.exists(os.path.join(OUT, on)):
            save(lit.resize((w, h)), on)
        button(p.kids, key, "%d %d %d %d" % (x, y, w, h), on, CLEAR, name=key + " tile")

    # banks: ROM A, ROM B, then the user banks (xenia/banks/), ten per page
    heading(14, 300, 92, "Banks")
    for i in range(10):
        tile("bank_slot_%d" % (i + 1), 20, 112 + 45 * i, 280, 41)
    stepper(d, p.kids, 160, 584, 280, 40, "bank_page_text", "bank_page_prev", "bank_page_next")
    d.line((312, 84, 312, 616), fill=BLUE, width=2)
    # sounds of the current bank, 30 per page (down the columns), the page stepper on the heading line
    hf2 = hf
    d.line((326, 92, 470, 92), fill=BLUE, width=2)
    d.text((540, 92), "Sounds", font=hf2, fill=BLUE, anchor="mm")
    d.line((610, 92, 950, 92), fill=BLUE, width=2)
    stepper(d, p.kids, 1112, 92, 300, 38, "patch_page_text", "patch_page_prev", "patch_page_next")
    for c in range(3):
        for r in range(10):
            tile("patch_slot_%d" % (c * 10 + r + 1), 326 + c * 315, 120 + r * 49, 307, 44)
    p.qgroup((14, 8, 828, 60), ["bank_index", "preset"])
    return p, art


TAB_ORDER = ["PLAY", "BANKS", "OSC", "AMP & ARP", "FILTER", "ENV & LFO", "WAVE ENV", "MOD", "SLOTS 1-8", "SLOTS 9-16"]


def qlinks_for(pages):
    maps = []
    for p in pages:
        q = {"Q-Link %d" % n: -1 for n in range(1, 17)}
        # a sub-tab whose blocks are all columns (taller than wide) banks by columns: block g = Q-Links 13+g, 9+g, 5+g, 1+g
        shapes = [int(g.split()[3]) > int(g.split()[2]) for g in p.groups[:4]]
        column = bool(shapes) and all(shapes)
        for gi, keys in enumerate(p.qrows[:4]):
            for i, key in enumerate(keys):
                q["Q-Link %d" % ((13 + gi - 4 * i) if column else (13 - 4 * gi + i))] = PI(key)
        maps.append({"Tab": p.fn + 1, "SubTab": p.sub + 1, "Bank Direction": "Column" if column else "RowTopDown", "Q-Links": q})
    return {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
            "Screen Mode Q-Links": {"version": 4, "map": maps}, "Program Mode Q-Links": {"Q-Link %d" % n: -1 for n in range(1, 17)}}


# ---- offline preview: compose a tab the way the MPC draws it --------------------------------------------------------------
SAMPLE = {"preset_name": "001 CS-80      T&WMF", "bank_name": "BANK A", "loading_status": "Ready - ROM loaded",
          "engine_stats": "DROPOUTS 0   DSP CPU 36%", "patch_page_text": "PAGE 1 / 4", "bank_page_text": "BANKS 1 / 3",
          "bank_slot_1": "BANK A", "bank_slot_2": "BANK B", "bank_slot_3": "factory 1997", "bank_slot_4": "factory 1998",
          "bank_slot_5": "factory 2000", "bank_slot_6": "hans heerooms drumloop", "bank_slot_7": "hans heerooms no filter",
          "bank_slot_8": "paul nagle", "bank_slot_9": "the lab", "bank_slot_10": "Waldorf MWXT Factory"}


def sample_text(key):
    if key in SAMPLE:
        return SAMPLE[key]
    if key.startswith("patch_slot_"):
        return "%03d Sound name %s" % (int(key[11:]), key[11:])
    p = PARAMS.get(key, {})
    if "options" in p:
        return p["options"][p["options"].index(p["default"]) if isinstance(p["default"], str) else int(p["default"])]
    return str(p.get("default", ""))


def preview(pages, arts, defs, open_list=None):
    os.makedirs(PREVIEW, exist_ok=True)
    for p in pages:
        if p.sub:
            continue
        im = Image.open(os.path.join(OUT, arts[p.name])).convert("RGBA")
        d = ImageDraw.Draw(im)
        for e in p.kids:
            cd = e["componentData"]
            x, y, w, h = [int(float(v)) for v in e["bounds"]["bounds"].split()]
            ind = e["bounds"].get("additionalInvalidatingHandles")
            if ind and ind[0].split("/")[1] != str(open_list):
                continue
            if cd["type"] == "xknob":
                st = Image.open(os.path.join(OUT, "knob_xt.png"))
                im.alpha_composite(st.crop((0, KNOB * 60, KNOB, KNOB * 61)).convert("RGBA"), (x, y))
            elif cd["type"] == "Label":
                key = LOCK[int(e["handle remapping"]["map"][0]["value"].split()[1])]
                ts = cd["data"]["textStyle"]
                cen = "horizontallyCentred" in ts["justification"]
                col = tuple(int(ts["colour"][i:i + 2], 16) for i in (2, 4, 6))
                f, s = F(int(ts["font"]["height"] * TUI_PX * 1.25), TIT), sample_text(key)
                if f.getlength(s) > w:                     # the MPC cuts a label at its bounds with an ellipsis
                    while s and f.getlength(s + "...") > w:
                        s = s[:-1]
                    s = s.rstrip() + "..."
                d.text((x + w / 2 if cen else x + 2, y + h / 2), s, font=f, fill=col, anchor="mm" if cen else "lm")
            elif cd["type"] == "Button" and cd["data"]["offImage"] != CLEAR:
                im.alpha_composite(Image.open(os.path.join(OUT, cd["data"]["offImage"])).convert("RGBA"), (x, y))
            elif cd["type"] in defs:
                for sub in defs[cd["type"]]["componentsData"]:
                    sx, sy, sw, sh = [int(float(v)) for v in sub["bounds"]["bounds"].split()]
                    sd = sub["componentData"]
                    img = sd["data"].get("image") or sd["data"].get("offImage")
                    if img:
                        im.alpha_composite(Image.open(os.path.join(OUT, img)).convert("RGBA"), (x + sx, y + sy))
            if "Q" in sys.argv:
                pass
        for g in p.groups:   # the Q-Link outlines, as the MPC draws them with the bounds overlay on
            gx, gy, gw, gh = [int(v) for v in g.split()]
            d.rectangle((gx, gy, gx + gw, gy + gh), outline=(255, 255, 255, 160), width=2)
        name = p.name.replace(" ", "_").replace("&", "and").lower()
        im.convert("RGB").save(os.path.join(PREVIEW, "%s%s.png" % (name, "_list%d" % open_list if open_list else "")))


def main():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    save(Image.new("RGBA", (8, 8), (0, 0, 0, 0)), CLEAR)
    knob_strip()
    spec, _ = parse_layout()
    pages, arts = [], {}
    for fn, name in enumerate(TAB_ORDER):
        if name == "BANKS":
            p, art = banks_page(fn)
            ps = [p]
        else:
            ps, art = layout_tab(name, fn, spec[name])
        arts[name] = save(art, "bg_%s.png" % re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_"))
        pages += ps
    tabs = [p.tab(arts[p.name]) for p in pages]
    repl = [{"key": P(k), "value": PARAMS[k]["options"]} for k in POPUPS]
    defs = definitions() + [{"key": k, "value": v[2]} for k, v in PICK_DEFS.items()]
    tui = {"pageData": {"version": 1, "componentDefinitions": {"version": 2, "importFiles": [], "localComponentDefinitions": defs},
                        "replacementStrings": repl, "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    open(os.path.join(OUT, "TUI.json"), "w").write(dump(tui) + "\n")
    open(os.path.join(OUT, "Q-Links.json"), "w").write(dump(qlinks_for(pages)) + "\n")
    bad = [f for f in os.listdir(OUT) if re.search(r'[<>:"/\\|?*]', f)]
    assert not bad, "file names that exFAT rejects: %s" % bad
    assert "object at" not in open(os.path.join(OUT, "TUI.json")).read()
    if "--preview" in sys.argv:
        dd = {x["key"]: x["value"] for x in defs}
        preview(pages, arts, dd)
        preview([p for p in pages if p.name == "OSC"], arts, dd, open_list=POPUPS.index("Wave") + 1)
        preview([p for p in pages if p.name == "SLOTS 1-8"], arts, dd, open_list=POPUPS.index("Slot1Destination") + 1)
        preview([p for p in pages if p.name == "PLAY"], arts, dd, open_list=POPUPS.index("EffectType") + 1)
    print("wrote", OUT, "-", len(tabs), "tabs (", len(TAB_ORDER), "keys),", len(os.listdir(OUT)), "files,", len(PICK_DEFS), "pick-lists")


if __name__ == "__main__":
    main()
