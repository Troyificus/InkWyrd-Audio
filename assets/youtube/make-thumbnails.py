"""YouTube thumbnails for Inkwyrd Audio, built from the app's own palette.

Colours are lifted straight from src/app/gui/InkwyrdTheme.h, and the kicker
uses Silkscreen, the pixel font the Pixel Phosphor skin ships with, so the
thumbnail looks like the program it is advertising.

Headline is Arial Black rather than Silkscreen: a thumbnail has to survive
being shown at 320x180 in a sidebar, and a pixel face falls apart there.
"""
import os
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H = 1280, 720
OUT = os.path.dirname(os.path.abspath(__file__))
REPO = r'G:\Inkwyrd-Audio'

BG        = (7, 12, 9)
PANEL     = (15, 28, 21)
ACCENT    = (79, 224, 138)
TEXT      = (143, 227, 173)
TITLEBAR  = (127, 207, 159)
OUTLINE   = (42, 92, 66)
WARNING   = (224, 178, 79)
WHITE     = (236, 255, 243)

WINFONT = os.path.join(os.environ['WINDIR'], 'Fonts')
BLACK_F = os.path.join(WINFONT, 'ariblk.ttf')
BOLD_F  = os.path.join(WINFONT, 'arialbd.ttf')
PIXEL_F = os.path.join(REPO, 'src', 'app', 'skins', 'Pixel Phosphor', 'Silkscreen-Bold.ttf')
if not os.path.exists(PIXEL_F):
    PIXEL_F = os.path.join(REPO, 'src', 'app', 'skins', 'Pixel Amber', 'Silkscreen-Bold.ttf')


def fit(path, text, target_w, start=200, min_size=30):
    """Largest size at which `text` fits `target_w`."""
    size = start
    while size > min_size:
        f = ImageFont.truetype(path, size)
        if f.getbbox(text)[2] - f.getbbox(text)[0] <= target_w:
            return f
        size -= 2
    return ImageFont.truetype(path, min_size)


def glow(layer, radius, times=1):
    g = layer
    for _ in range(times):
        g = g.filter(ImageFilter.GaussianBlur(radius))
    return g


def build(headline_lines, kicker, strap, filename, accent=ACCENT):
    img = Image.new('RGB', (W, H), BG)
    d = ImageDraw.Draw(img)

    # Dotted panel texture, as the app's own panels have.
    for y in range(0, H, 8):
        for x in range(0, W, 8):
            d.point((x, y), fill=(13, 24, 17))

    # Pool of light behind the logo side.
    halo = Image.new('L', (W, H), 0)
    ImageDraw.Draw(halo).ellipse([W - 620, -200, W + 180, 620], fill=120)
    halo = glow(halo, 160)
    img = Image.composite(Image.new('RGB', (W, H), (18, 54, 36)), img, halo)
    d = ImageDraw.Draw(img)

    # Logo, with its own bloom.
    logo = Image.open(os.path.join(REPO, 'assets', 'icon', 'icon-256.png')).convert('RGBA')
    size = 360
    logo = logo.resize((size, size), Image.LANCZOS)
    lx, ly = W - size - 96, (H - size) // 2 - 30

    bloom = Image.new('RGB', (W, H), BG)
    bl = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    bl.paste(logo, (lx, ly), logo)
    bloom.paste(accent, (0, 0), bl.split()[3])
    img = Image.blend(img, glow(bloom, 55), 0.55)
    d = ImageDraw.Draw(img)
    img.paste(logo, (lx, ly), logo)

    # Headline.
    left = 84
    avail = lx - left - 56
    fonts = [fit(BLACK_F, line, avail, start=190) for line in headline_lines]
    smallest = min(f.size for f in fonts)
    fonts = [ImageFont.truetype(BLACK_F, smallest) for _ in headline_lines]
    line_h = int(smallest * 1.04)

    block_h = line_h * len(headline_lines)
    kf = ImageFont.truetype(PIXEL_F, 30)
    sf = ImageFont.truetype(BOLD_F, 33)
    top = (H - block_h) // 2 - 16

    # Kicker above the headline.
    d.text((left + 4, top - 76), kicker, font=kf, fill=accent)

    for i, line in enumerate(headline_lines):
        y = top + i * line_h
        # A dark pad behind the text so it survives any background.
        shadow = Image.new('L', (W, H), 0)
        ImageDraw.Draw(shadow).text((left, y), line, font=fonts[i], fill=255)
        dark = Image.new('RGB', (W, H), (0, 0, 0))
        img = Image.composite(dark, img, glow(shadow, 22, 2))
        d = ImageDraw.Draw(img)
        d.text((left, y), line, font=fonts[i], fill=WHITE)

    # Accent rule and strapline under the headline.
    ry = top + block_h + 26
    d.rectangle([left, ry, left + 150, ry + 9], fill=accent)
    d.text((left, ry + 34), strap, font=sf, fill=TEXT)
    strap_bottom = ry + 34 + (sf.getbbox(strap)[3] - sf.getbbox(strap)[1])

    # Spectrum bars along the foot, the app's own motif.
    #
    # Two things constrain these. They must not reach the bottom-right,
    # where YouTube paints the duration chip over the top of whatever is
    # there; and they must stay below the strapline, which moves down when
    # a headline needs two tall lines.
    bar_base = H - 40
    bar_ceiling = strap_bottom + 26
    max_bar = max(24, bar_base - bar_ceiling)

    heights = [14, 30, 52, 38, 68, 44, 86, 58, 100, 72, 54, 88, 40, 64, 30, 48,
               22, 60, 36, 78, 46, 92, 56, 34, 20, 44, 28, 58, 38, 24]
    scale = min(1.0, max_bar / float(max(heights)))
    bw, gap = 26, 14
    x = left
    for i, bh in enumerate(heights):
        if x + bw > W - 300:   # clear of the logo and the duration chip
            break
        shade = accent if i % 3 else TITLEBAR
        d.rectangle([x, bar_base - int(bh * scale), x + bw, bar_base], fill=shade)
        x += bw + gap

    # Hairline frame.
    d.rectangle([0, 0, W - 1, H - 1], outline=OUTLINE)
    d.rectangle([0, H - 10, W - 1, H - 1], fill=accent)

    path = os.path.join(OUT, filename)
    img.save(path, 'PNG')
    print('%-28s %s  (%d KB)' % (filename, img.size, os.path.getsize(path) // 1024))
    return path


build(['FULL SETUP', 'IN 5 MINUTES'], 'INKWYRD AUDIO', 'Tabletop audio, straight into Discord.', 'thumb-a-setup.png')
build(['YOUR D&D AUDIO', 'IN DISCORD'], 'INKWYRD AUDIO', 'Playlists, soundboard and scenes. Free.', 'thumb-b-whatitis.png')
build(['GETTING', 'STARTED'], 'INKWYRD AUDIO', 'The free Discord audio player for tabletop games.', 'thumb-c-started.png')
