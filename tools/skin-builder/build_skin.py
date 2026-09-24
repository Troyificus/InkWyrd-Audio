"""Build a sprite skin for Inkwyrd Audio.

Draws every sprite the app asks for (the names come from
src/app/gui/SkinSpriteNames.h, so this can't drift from the C++), packs
them into one sheet, and writes a complete skin folder:

    <out>/skin.json
    <out>/sprites.png
    <out>/logo.png

The art is original pixel art drawn by code - bevels, glyphs and LEDs in
the idiom of late-90s media players, not copied from any of them. It is
a STARTING POINT meant to be opened in Aseprite/LibreSprite and redrawn
by hand; the packer keeps the layout stable so edits survive a rebuild
only if made to the sheet, so treat the generated sheet as the thing you
edit once you start drawing.

    python build_skin.py --out "%APPDATA%/Inkwyrd Audio/skins/Pixel Phosphor"
    python build_skin.py --out out/Phosphor --textures textures/   # AI texture pass
    python build_skin.py --check                                  # names vs. code

--textures takes the folder comfy_textures.py writes. Those images are
pixelised and forced onto the skin's palette before use, and only ever
fill large areas (window body, display, panels) - bevels, corners and
glyphs are always drawn here, because diffusion output can't be kept
pixel-aligned across a button's states.
"""

import argparse
import json
import random
import re
import shutil
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter

REPO = Path(__file__).resolve().parents[2]
NAMES_HEADER = REPO / "src" / "app" / "gui" / "SkinSpriteNames.h"

# ---------------------------------------------------------------------------
# Palette. Every pixel on the sheet is one of these, which is what keeps a
# pixel-art skin looking like one piece rather than a collage.

BLACK = (4, 8, 6)
D0 = (10, 17, 13)      # deepest metal / shadow
D1 = (19, 30, 24)      # body
D2 = (32, 47, 39)      # raised face
D3 = (50, 70, 59)      # light face
D4 = (78, 104, 89)     # highlight
D5 = (120, 150, 132)   # bright highlight
G0 = (10, 38, 22)      # unlit green
G1 = (24, 86, 50)
G2 = (44, 156, 90)
G3 = (79, 224, 138)    # the accent
G4 = (178, 255, 206)   # glow
LCD0 = (4, 13, 8)
LCD1 = (7, 22, 13)
AMBER = (224, 178, 79)
RED = (224, 106, 90)

TEXT = (156, 232, 184)
TEXTDIM = (96, 156, 120)

# The colour schemes the pixel skins come in - one per flat example skin, so
# every flat look has a pixel twin in the same colours. The names above are
# the ones every drawing function reads; apply_theme() swaps them.
#
#   D0..D5  the "metal": deepest shadow up to brightest highlight
#   G0..G4  the accent ramp: unlit, through the accent itself, to glow
#   AMBER   the warning colour (a muted mic lights up in it)
#
# Phosphor is the original Pixel Phosphor, value for value - regenerating
# it must not change its art.
THEMES = {
    "phosphor": dict(
        name="Pixel Phosphor",
        BLACK=(4, 8, 6), D0=(10, 17, 13), D1=(19, 30, 24), D2=(32, 47, 39), D3=(50, 70, 59),
        D4=(78, 104, 89), D5=(120, 150, 132),
        G0=(10, 38, 22), G1=(24, 86, 50), G2=(44, 156, 90), G3=(79, 224, 138), G4=(178, 255, 206),
        LCD0=(4, 13, 8), LCD1=(7, 22, 13), AMBER=(224, 178, 79), RED=(224, 106, 90),
        TEXT=(156, 232, 184), TEXTDIM=(96, 156, 120),
        MUTE={(10, 38, 22): (46, 34, 10), (24, 86, 50): (96, 72, 24),
              (44, 156, 90): (170, 128, 46), (79, 224, 138): (224, 178, 79)}),
    # From the flat Amber skin: warm browns, an orange accent, and - as in
    # that skin - a BLUE warning colour, since amber can't warn on amber.
    "amber": dict(
        name="Pixel Amber",
        BLACK=(8, 5, 2), D0=(18, 13, 5), D1=(27, 18, 6), D2=(36, 25, 8), D3=(62, 44, 18),
        D4=(92, 69, 32), D5=(140, 108, 58),
        G0=(44, 30, 8), G1=(138, 95, 30), G2=(200, 140, 50), G3=(255, 179, 64), G4=(255, 225, 160),
        LCD0=(11, 8, 3), LCD1=(22, 15, 5), AMBER=(127, 182, 255), RED=(255, 107, 90),
        TEXT=(239, 201, 138), TEXTDIM=(168, 133, 74)),
    "midnight": dict(
        name="Pixel Midnight",
        BLACK=(3, 4, 9), D0=(8, 12, 24), D1=(13, 20, 36), D2=(19, 29, 51), D3=(34, 50, 82),
        D4=(58, 80, 120), D5=(100, 125, 170),
        G0=(14, 26, 48), G1=(44, 79, 128), G2=(70, 130, 210), G3=(90, 156, 255), G4=(190, 215, 255),
        LCD0=(4, 6, 14), LCD1=(8, 12, 26), AMBER=(224, 178, 79), RED=(224, 106, 90),
        TEXT=(184, 205, 240), TEXTDIM=(113, 137, 176)),
    # Maximum contrast, like its flat twin: black body, white bevels, a
    # yellow accent. The bevels are the brightest of any theme on purpose.
    "contrast": dict(
        name="Pixel High Contrast",
        BLACK=(0, 0, 0), D0=(10, 10, 10), D1=(18, 18, 18), D2=(34, 34, 34), D3=(96, 96, 96),
        D4=(200, 200, 200), D5=(255, 255, 255),
        G0=(40, 36, 0), G1=(107, 97, 0), G2=(200, 180, 0), G3=(255, 230, 0), G4=(255, 250, 180),
        LCD0=(0, 0, 0), LCD1=(16, 16, 16), AMBER=(255, 165, 0), RED=(255, 64, 64),
        TEXT=(255, 255, 255), TEXTDIM=(204, 204, 204)),
}

# Each shipped skin's version. Bump one when its art or skin.json changes,
# so the app replaces users' untouched copies of the old one (and never an
# edited copy - see ExampleSkins.cpp). Pixel Phosphor was 1 in beta.31.
VERSIONS = {"phosphor": 2, "amber": 1, "midnight": 1, "contrast": 1}


def apply_theme(key):
    theme = THEMES[key]
    for name, value in theme.items():
        if name not in ("name", "MUTE"):
            globals()[name] = value
    # The muted-mic sprites recolour the accent ramp into the warning colour.
    # Phosphor keeps the exact shades it shipped with in beta.31.
    w = theme["AMBER"]
    shade = lambda f: tuple(int(c * f) for c in w)
    globals()["AMBER_SHADES"] = theme.get("MUTE") or {
        theme["G0"]: shade(0.2), theme["G1"]: shade(0.43), theme["G2"]: shade(0.76), theme["G3"]: w}


def hexa(rgb):
    return "#%02x%02x%02x" % rgb


# ---------------------------------------------------------------------------
# Reading the names the app uses.

def read_names():
    # Comments stripped first: they quote example names ("@on", ...) that
    # aren't entries.
    text = re.sub(r"//[^\n]*", "", NAMES_HEADER.read_text(encoding="utf-8"))

    def array(name):
        m = re.search(name + r"\[\]\s*=\s*\{(.*?)\};", text, re.S)
        if not m:
            sys.exit(f"Couldn't find {name} in {NAMES_HEADER}")
        return re.findall(r'"([^"]+)"', m.group(1))

    return array("kBaseNames"), array("kComponentIds")


SUFFIXES = ("", "@over", "@down", "@on", "@onover", "@ondown", "@disabled", "@focus", "@inactive")


def is_known_name(name, bases, ids):
    base, at, suffix = name.partition("@")
    if at and ("@" + suffix) not in SUFFIXES:
        return False
    if base in bases:
        return True
    return any(base == prefix + "." + i for prefix in list(bases) + ["icon"] for i in ids)


def check_component_ids():
    """Every setComponentID("...") in the GUI must be a listed id, or a
    skin could never give that control its own sprite."""
    _, ids = read_names()
    used = set()
    for cpp in (REPO / "src" / "app" / "gui").glob("*.cpp"):
        used.update(re.findall(r'setComponentID\(\s*"([^"]+)"', cpp.read_text(encoding="utf-8")))
    # PlayerComponent flips Play's id with a ternary.
    for cpp in (REPO / "src" / "app" / "gui").glob("*.cpp"):
        for a, b in re.findall(r'setComponentID\([^;]*\?\s*"([^"]+)"\s*:\s*"([^"]+)"', cpp.read_text(encoding="utf-8")):
            used.update((a, b))

    title_glyphs = {"close", "minimise", "maximise"}  # set in code, not via setComponentID
    missing = sorted(used - set(ids))
    unused = sorted(set(ids) - used - title_glyphs)

    ok = True
    if missing:
        ok = False
        print("setComponentID() used but not in SkinSpriteNames.h:", ", ".join(missing))
    if unused:
        print("listed in SkinSpriteNames.h but never set (fine if deliberate):", ", ".join(unused))
    if ok:
        print(f"OK - {len(used)} component ids in use, all listed.")
    return ok


# ---------------------------------------------------------------------------
# Drawing helpers. Sprites are tiny, so everything is per-pixel.

class Canvas:
    def __init__(self, w, h, fill=None):
        self.img = Image.new("RGBA", (w, h), (0, 0, 0, 0) if fill is None else fill + (255,))
        self.w, self.h = w, h

    def px(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.img.putpixel((x, y), c + (255,))

    def rect(self, x, y, w, h, c):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.px(xx, yy, c)

    def hline(self, x, y, w, c):
        self.rect(x, y, w, 1, c)

    def vline(self, x, y, h, c):
        self.rect(x, y, 1, h, c)

    def outline(self, c, inset=0):
        x0, y0, x1, y1 = inset, inset, self.w - 1 - inset, self.h - 1 - inset
        self.hline(x0, y0, x1 - x0 + 1, c)
        self.hline(x0, y1, x1 - x0 + 1, c)
        self.vline(x0, y0, y1 - y0 + 1, c)
        self.vline(x1, y0, y1 - y0 + 1, c)

    def bevel(self, light, dark, inset=1):
        """Light top-left, dark bottom-right: raised. Swap for sunken."""
        x0, y0, x1, y1 = inset, inset, self.w - 1 - inset, self.h - 1 - inset
        self.hline(x0, y0, x1 - x0, light)
        self.vline(x0, y0, y1 - y0, light)
        self.hline(x0 + 1, y1, x1 - x0, dark)
        self.vline(x1, y0 + 1, y1 - y0, dark)

    def glyph(self, x, y, rows, c, shadow=None):
        if shadow:
            for j, row in enumerate(rows):
                for i, ch in enumerate(row):
                    if ch == "X":
                        self.px(x + i + 1, y + j + 1, shadow)
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch == "X":
                    self.px(x + i, y + j, c)


def framed(w, h, face, light, dark, outline=None):
    # outline=None, not =BLACK: a default is evaluated once, when the
    # function is defined, and would pin every theme to Phosphor's black.
    outline = BLACK if outline is None else outline
    c = Canvas(w, h, face)
    c.outline(outline)
    c.bevel(light, dark)
    return c


def speckle(c, x, y, w, h, colours, density, seed):
    """Sparse texture so a big flat area reads as metal, not a fill."""
    rnd = random.Random(seed)
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            if rnd.random() < density:
                c.px(xx, yy, rnd.choice(colours))


# ---------------------------------------------------------------------------
# Glyphs, as bitmaps. Drawn on a 1px drop shadow so they read on any face.

GLYPHS = {
    "transport.play": ["X......", "XXX....", "XXXXX..", "XXXXXXX", "XXXXX..", "XXX....", "X......"],
    "transport.pause": ["XX..XX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XX..XX"],
    "transport.stop": ["XXXXXXX"] * 7,
    "transport.skip": ["X.....XX", "XXX...XX", "XXXXX.XX", "XXXXXXXX", "XXXXX.XX", "XXX...XX", "X.....XX"],
    "transport.fadeout": ["X......", "X......", "X.X....", "X.X....", "X.X.X..", "X.X.X..", "X.X.X.X"],
    "close": ["X...X", ".X.X.", "..X..", ".X.X.", "X...X"],
    "minimise": [".....", ".....", ".....", ".....", "XXXXX"],
    "maximise": ["XXXXX", "X...X", "X...X", "X...X", "XXXXX"],
}

TICK = ["......X", ".....XX", "X...XX.", "XX.XX..", ".XXX...", "..X...."]


def led(on):
    """A round 7x7 lamp - the crossfade and loop toggles wear one."""
    c = Canvas(7, 7)
    ring = ["..XXX..", ".X...X.", "X.....X", "X.....X", "X.....X", ".X...X.", "..XXX.."]
    fill = ["......." , "..XXX..", ".XXXXX.", ".XXXXX.", ".XXXXX.", "..XXX..", "......."]
    c.glyph(0, 0, ring, BLACK)
    c.glyph(0, 0, fill, G3 if on else G0)
    if on:
        c.px(2, 2, G4)
        c.px(3, 2, G4)
        c.px(2, 3, G4)
    else:
        c.px(2, 2, G1)
    return c


# ---------------------------------------------------------------------------
# The sprites. Each returns (Canvas, slice or None, tile).

AMBER_SHADES = {}  # set by apply_theme()


def mute_button(state):
    """Mic muted lights amber, not green: it's the state someone needs to
    notice, not an option being on. Only the lit states get a per-control
    sprite, so every other state still falls back to the generic button."""
    c, slc, tile = button(state)
    for y in range(c.h):
        for x in range(c.w):
            rgb = c.img.getpixel((x, y))[:3]
            if rgb in AMBER_SHADES:
                c.px(x, y, AMBER_SHADES[rgb])
    return c, slc, tile


def button(state):
    faces = {
        "": (D2, D4, D0), "@over": (D3, D5, D0), "@down": (D1, D0, D3),
        "@on": (G0, G2, D0), "@onover": (G1, G3, D0), "@ondown": (G0, D0, G2),
        "@disabled": (D1, D2, D1),
    }
    face, light, dark = faces[state]
    c = framed(16, 12, face, light, dark)
    if state in ("@on", "@onover"):
        c.hline(2, 2, 12, G2 if state == "@on" else G3)   # the lit strip
    if state == "@ondown":
        c.hline(2, 3, 12, G1)
    return c, (4, 4, 4, 4), False


def title_button(state):
    faces = {"": (D2, D4, D0), "@over": (D3, D5, D0), "@down": (D1, D0, D3)}
    face, light, dark = faces[state]
    return framed(11, 11, face, light, dark), (3, 3, 3, 3), False


def icon(name, state):
    rows = GLYPHS[name]
    w, h = len(rows[0]) + 1, len(rows) + 1
    c = Canvas(w, h)
    colour = {"": G3, "@over": G4, "@down": G2, "@disabled": D3}[state]
    shadow = None if state == "@disabled" else BLACK
    c.glyph(0, 0, rows, colour, shadow)
    return c, None, False


def title_icon(name, state):
    rows = GLYPHS[name]
    c = Canvas(len(rows[0]), len(rows))
    c.glyph(0, 0, rows, {"": G3, "@over": G4, "@down": G2}[state])
    return c, None, False


def checkbox(state):
    c = Canvas(11, 11, LCD0)
    c.outline(BLACK)
    c.bevel(D0, D3)
    if state in ("@on", "@onover"):
        c.glyph(2, 2, TICK, G4 if state == "@onover" else G3)
    elif state == "@over":
        c.rect(3, 3, 5, 5, G0)
    return c, None, False


def slider_track():
    c = Canvas(12, 5, LCD0)
    c.outline(BLACK)
    c.hline(1, 1, 10, D0)
    c.hline(1, 3, 10, D1)
    return c, (3, 2, 3, 2), False


def slider_fill():
    c = Canvas(12, 5, G1)
    c.outline(BLACK)
    c.hline(1, 1, 10, G3)
    c.hline(1, 2, 10, G2)
    return c, (3, 2, 3, 2), False


def slider_thumb(state):
    faces = {"": (D3, D5, D0), "@over": (D4, D5, D0), "@down": (G1, G3, D0), "@disabled": (D1, D2, D0)}
    face, light, dark = faces[state]
    c = framed(7, 13, face, light, dark)
    for y in (4, 6, 8):
        c.hline(2, y, 3, dark if state != "@down" else G0)
    return c, None, False


def scroll_track():
    c = Canvas(10, 12, LCD0)
    c.outline(BLACK)
    c.bevel(D0, D2)
    return c, (3, 3, 3, 3), False


def scroll_thumb(state):
    faces = {"": (D2, D4, D0), "@over": (D3, D5, D0), "@down": (G1, G3, D0)}
    face, light, dark = faces[state]
    c = framed(10, 12, face, light, dark)
    for y in (4, 6):
        c.hline(3, y, 4, dark if state != "@down" else G0)
    return c, (3, 3, 3, 3), False


def textbox(state):
    c = Canvas(12, 12, LCD0)
    c.outline(G2 if state == "@focus" else BLACK)
    c.bevel(D0, G1 if state == "@focus" else D2)
    return c, (3, 3, 3, 3), False


def combobox():
    c = framed(12, 12, D1, D3, D0)
    return c, (3, 3, 3, 3), False


def popup():
    c = framed(12, 12, D1, D4, D0)
    return c, (3, 3, 3, 3), False


def panel(raised):
    c = framed(12, 12, D2 if raised else D1, D4 if raised else D2, D0)
    return c, (3, 3, 3, 3), False


def well():
    c = Canvas(12, 12, BLACK)
    c.outline(D0)
    c.bevel(D0, D3)
    return c, (3, 3, 3, 3), False


def display(texture=None):
    """The Player's LCD: sunken frame, scanlines tiled behind."""
    c = Canvas(16, 16, LCD0)
    c.outline(BLACK)
    c.bevel(D0, D3)
    if texture is not None:
        paste_texture(c, texture, 4, 4, 8, 8)
    else:
        for y in range(4, 12):
            c.hline(4, y, 8, LCD1 if y % 2 == 0 else LCD0)
    c.rect(2, 2, 12, 2, LCD0)   # inner lip, so the scanlines start clear of the frame
    c.rect(2, 12, 12, 2, LCD0)
    c.rect(2, 4, 2, 8, LCD0)
    c.rect(12, 4, 2, 8, LCD0)
    return c, (4, 4, 4, 4), True


def window(texture=None):
    c = Canvas(40, 40, D1)
    c.outline(BLACK)
    c.bevel(D4, D0)
    c.hline(2, 2, 36, D2)
    c.vline(2, 2, 36, D2)
    if texture is not None:
        paste_texture(c, texture, 6, 6, 28, 28)
    else:
        speckle(c, 6, 6, 28, 28, [D0, D2], 0.06, seed=7)
    return c, (6, 6, 6, 6), True


def titlebar(active):
    c = Canvas(24, 23, D1 if active else D0)
    c.outline(BLACK)
    c.bevel(D4 if active else D2, D0)
    line = G2 if active else G0
    c.hline(2, 3, 20, line)
    c.hline(2, 19, 20, line)
    # Ridged ends, like a grip - only in the corners, which never stretch,
    # so the middle stays calm behind the title text.
    for y in range(6, 17, 2):
        c.hline(2, y, 3, D2 if active else D1)
        c.hline(19, y, 3, D2 if active else D1)
    return c, (6, 6, 6, 6), True


def paste_texture(c, texture, x, y, w, h):
    tex = texture.resize((w, h), Image.NEAREST)
    for yy in range(h):
        for xx in range(w):
            r, g, b = tex.getpixel((xx, yy))[:3]
            c.px(x + xx, y + yy, (r, g, b))


# ---------------------------------------------------------------------------
# Optional AI textures: pixelise, make seamless, force onto the palette.


def load_texture(path, size, ramp, cuts):
    img = Image.open(path).convert("RGB")
    # The middle half only: generated images drift at their edges -
    # vignettes, frames, a stray light - and the centre is the part most
    # likely to be the even material that was asked for.
    side = min(img.size) // 2
    img = img.crop(((img.width - side) // 2, (img.height - side) // 2,
                    (img.width + side) // 2, (img.height + side) // 2))

    # Flatten the lighting: subtract a heavy blur and add the average back.
    # Diffusion models light their "flat" textures anyway - a hotspot, a
    # top-to-bottom fade - and any low-frequency shading like that shows
    # up as a seam every time the tile repeats. The grain survives; the
    # gradient doesn't.
    blurred = img.filter(ImageFilter.GaussianBlur(side / 6))
    mean = img.resize((1, 1), Image.BOX).getpixel((0, 0))
    flat = ImageChops.add(ImageChops.subtract(img, blurred, offset=128),
                          Image.new("RGB", img.size, mean), offset=-128)
    img = flat
    # Box-filter down: averaging first is what turns a photo into
    # readable pixel clusters instead of noise.
    small = img.resize((size, size), Image.BOX)

    # Seamless: cross-fade with a half-offset copy so the tile's edges
    # match wherever it repeats.
    shifted = Image.new("RGB", small.size)
    h = size // 2
    shifted.paste(small.crop((h, h, size, size)), (0, 0))
    shifted.paste(small.crop((0, h, h, size)), (h, 0))
    shifted.paste(small.crop((h, 0, size, h)), (0, h))
    shifted.paste(small.crop((0, 0, h, h)), (h, h))
    mask = Image.new("L", small.size)
    for yy in range(size):
        for xx in range(size):
            d = max(abs(xx - (size - 1) / 2), abs(yy - (size - 1) / 2)) / ((size - 1) / 2)
            mask.putpixel((xx, yy), int(255 * min(1.0, max(0.0, (d - 0.5) * 2))))
    small = Image.composite(shifted, small, mask)

    # Posterise by BRIGHTNESS RANK onto a dark-to-light ramp from the
    # palette, rather than snapping each pixel to its nearest colour. Once
    # the lighting is flattened the grain is subtle, and nearest-colour
    # collapsed it all onto one entry - a flat fill that had cost a GPU
    # minute. Ranking guarantees each step of the ramp gets its share
    # (`cuts` are the fractions where it steps up), so the grain always
    # shows, at a contrast the skin chose rather than the model.
    lum = [(0.299 * r + 0.587 * g + 0.114 * b, xx, yy)
           for yy in range(size) for xx in range(size)
           for r, g, b in [small.getpixel((xx, yy))]]
    lum.sort()
    out = Image.new("RGB", small.size)
    for rank, (_, xx, yy) in enumerate(lum):
        fraction = rank / len(lum)
        step = sum(1 for c in cuts if fraction >= c)
        out.putpixel((xx, yy), ramp[step])
    return out


# ---------------------------------------------------------------------------
# Logo: a pixel ink bottle, pre-scaled by 8 with nearest-neighbour so the
# app's smooth logo scaling has nothing soft to blur.

LOGO = [
    "....XXXX....",
    "....XGGX....",
    "....XXXX....",
    ".....XX.....",
    "...XXXXXX...",
    "..XDDDDDDX..",
    ".XDGGGGGGDX.",
    ".XGGHGGGGGX.",
    ".XGHHGGGGGX.",
    ".XGGGGGGGGX.",
    ".XGGGGGGGGX.",
    "..XGGGGGGX..",
    "...XXXXXX...",
]


def logo():
    colours = {"X": BLACK, "G": G2, "H": G4, "D": G1}
    c = Canvas(len(LOGO[0]), len(LOGO))
    for y, row in enumerate(LOGO):
        for x, ch in enumerate(row):
            if ch in colours:
                c.px(x, y, colours[ch])
    return c.img.resize((c.w * 8, c.h * 8), Image.NEAREST)


# ---------------------------------------------------------------------------
# Packing: simple shelves, tallest first, 1px gap so a hand edit to one
# sprite can't bleed into its neighbour.

def pack(sprites, width=160, gap=1):
    order = sorted(sprites.items(), key=lambda kv: (-kv[1][0].h, -kv[1][0].w, kv[0]))
    x = y = shelf = 0
    placed = {}
    for name, (canvas, slc, tile) in order:
        if x + canvas.w > width:
            x, y, shelf = 0, y + shelf + gap, 0
        placed[name] = (x, y)
        x += canvas.w + gap
        shelf = max(shelf, canvas.h)
    height = y + shelf
    sheet = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    items = {}
    for name, (canvas, slc, tile) in sprites.items():
        px, py = placed[name]
        sheet.paste(canvas.img, (px, py))
        item = {"rect": [px, py, canvas.w, canvas.h]}
        if slc:
            item["slice"] = list(slc)
        if tile:
            item["tile"] = True
        items[name] = item
    return sheet, items


def build(out, textures_dir, name, font_dir=None, version=1):
    bases, ids = read_names()

    textures = {}
    if textures_dir:
        # (ramp dark -> light, fractions where it steps up)
        surfaces = {
            "window": (28, [D0, D1, D2], (0.2, 0.85)),
            "display": (8, [LCD0, LCD1, G0], (0.45, 0.9)),
        }
        for key, (size, ramp, cuts) in surfaces.items():
            path = Path(textures_dir) / f"{key}.png"
            if path.exists():
                textures[key] = load_texture(path, size, ramp, cuts)
                print(f"  texture: {path.name} -> {size}x{size}, {len(ramp)}-step ramp")
            else:
                print(f"  (no {path.name} - drawing that one procedurally)")

    s = {}
    for st in ("", "@over", "@down", "@on", "@onover", "@ondown", "@disabled"):
        s["button" + st] = button(st)
    for st in ("@on", "@onover", "@ondown"):
        s["button.toggle.mute" + st] = mute_button(st)
    for st in ("", "@over", "@down"):
        s["titlebutton" + st] = title_button(st)
        for g in ("close", "minimise", "maximise"):
            s[f"icon.{g}{st}"] = title_icon(g, st)
    for g in ("transport.play", "transport.pause", "transport.stop", "transport.skip", "transport.fadeout"):
        for st in ("", "@over", "@down", "@disabled"):
            s[f"icon.{g}{st}"] = icon(g, st)
    for t in ("toggle.crossfade", "toggle.loop"):
        s[f"icon.{t}"] = (led(False), None, False)
        s[f"icon.{t}@on"] = (led(True), None, False)
    for st in ("", "@over", "@on", "@onover"):
        s["checkbox" + st] = checkbox(st)
    s["slider.track"] = slider_track()
    s["slider.fill"] = slider_fill()
    for st in ("", "@over", "@down", "@disabled"):
        s["slider.thumb" + st] = slider_thumb(st)
    s["scrollbar.track"] = scroll_track()
    for st in ("", "@over", "@down"):
        s["scrollbar.thumb" + st] = scroll_thumb(st)
    s["textbox"] = textbox("")
    s["textbox@focus"] = textbox("@focus")
    s["combobox"] = combobox()
    s["popup"] = popup()
    s["panel"] = panel(False)
    s["panel.raised"] = panel(True)
    s["well"] = well()
    s["display"] = display(textures.get("display"))
    s["window"] = window(textures.get("window"))
    s["titlebar"] = titlebar(True)
    s["titlebar@inactive"] = titlebar(False)

    # Guard against drawing a sprite the app would reject as unknown - the
    # same rule as SkinSprites::isKnownName().
    for key in s:
        if not is_known_name(key, bases, ids):
            sys.exit(f"sprite {key!r} isn't a name the app knows - check SkinSpriteNames.h")

    sheet, items = pack(s)

    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    sheet.save(out / "sprites.png")
    logo().save(out / "logo.png")

    skin = {
        "schemaVersion": 1,
        "name": name,
        "version": version,
        "logo": "logo.png",
        "colours": {
            "background": hexa(BLACK), "panelDeep": hexa(LCD0), "panel": hexa(D1),
            "panelRaised": hexa(D2), "titleBar": hexa(D1), "titleBarText": hexa(G3),
            "titleBarSubtle": hexa(G2), "text": hexa(TEXT), "textDim": hexa(TEXTDIM),
            "accent": hexa(G3), "accentSoft": hexa(G1), "outline": hexa(D3),
            "outlineFaint": hexa(D2), "warning": hexa(AMBER), "danger": hexa(RED),
        },
        "fonts": {"title": "Segoe UI Semibold", "label": "Segoe UI", "digits": "Consolas"},
        "fontFiles": [],
        "metrics": {"cornerRadius": 0, "titleBarHeight": 40},
        "sprites": {"sheet": "sprites.png", "scale": 2, "pixelArt": True, "items": items},
    }
    # The skin's own font, carried as files so it works on machines that
    # don't have it installed. Its licence travels with it (the SIL Open
    # Font License requires that when the font is redistributed).
    if font_dir:
        from PIL import ImageFont
        font_dir = Path(font_dir)
        family = None
        for ttf in sorted(font_dir.glob("*.[ot]tf")):
            shutil.copyfile(ttf, out / ttf.name)
            skin["fontFiles"].append(ttf.name)
            family = family or ImageFont.truetype(str(ttf), 12).getname()[0]
        for licence in font_dir.glob("*.txt"):
            shutil.copyfile(licence, out / f"{family or 'font'}-{licence.name}")
        if family:
            skin["fonts"] = {"title": family, "label": family, "digits": family}
            print(f"  font: {family} ({len(skin['fontFiles'])} file(s))")
    if not skin["fontFiles"]:
        del skin["fontFiles"]

    (out / "skin.json").write_text(json.dumps(skin, indent=2), encoding="utf-8")
    print(f"Wrote {len(items)} sprites ({sheet.width}x{sheet.height} sheet) to {out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", help="skin folder to write (with --all: the folder to write them INTO)")
    ap.add_argument("--theme", choices=sorted(THEMES), default="phosphor")
    ap.add_argument("--all", action="store_true", help="every theme, each in its own folder under --out")
    ap.add_argument("--name", help="skin name (default: the theme's own)")
    ap.add_argument("--textures", help="folder of textures from comfy_textures.py")
    ap.add_argument("--font-dir", default=str(Path(__file__).parent / "fonts" / "Silkscreen"),
                    help="folder of .ttf/.otf files (+ licence .txt) to bundle; default Silkscreen")
    ap.add_argument("--no-font", action="store_true", help="use installed fonts only")
    ap.add_argument("--check", action="store_true", help="check component ids against SkinSpriteNames.h")
    args = ap.parse_args()

    if args.check:
        sys.exit(0 if check_component_ids() else 1)
    if not args.out:
        ap.error("--out is required unless --check")
    font_dir = None if args.no_font or not Path(args.font_dir).is_dir() else args.font_dir
    keys = sorted(THEMES) if args.all else [args.theme]
    for key in keys:
        apply_theme(key)
        name = THEMES[key]["name"] if args.all or not args.name else args.name
        out = Path(args.out) / name if args.all else Path(args.out)
        build(out, args.textures, name, font_dir, VERSIONS[key])


if __name__ == "__main__":
    main()
