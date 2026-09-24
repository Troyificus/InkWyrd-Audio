# skin-builder

Generates a complete sprite skin for Inkwyrd Audio - `skin.json`,
`sprites.png`, `logo.png` - as a starting point to redraw by hand. The
format itself is documented in the main README under
*Pictures for buttons, sliders and frames*.

Needs Python 3 and Pillow (`pip install pillow`).

```
python build_skin.py --out "%APPDATA%\Inkwyrd Audio\skins\My Skin" --name "My Skin"
python build_skin.py --check
```

`--check` compares every `setComponentID("...")` in `src/app/gui` with
`src/app/gui/SkinSpriteNames.h`. The builder reads its sprite names from
that same header, so a control added there shows up in the next
generated sheet with no change here.

The four pixel skins the app ships - **Pixel Phosphor**, **Pixel Amber**,
**Pixel Midnight** and **Pixel High Contrast** - are this script's output,
one per colour scheme in `THEMES` (each one matching a flat example skin's
colours). Regenerate them all after changing the art here:

```
python build_skin.py --all --out ../../src/app/skins
python build_skin.py --theme amber --out "some/folder"   # just one
```

**Bump the skin's number in `VERSIONS`** whenever its art or skin.json
changes, or users who already have it won't get the new one - the app
only replaces a user's untouched copy with a HIGHER version (see
`src/app/gui/ExampleSkins.h`). Phosphor's colours are its original values
exactly, so regenerating it changes nothing unless the art itself does.

Every folder under `src/app/skins` is packed into the app at build time
(CMake zips them at configure time), so a new skin folder is shipped just
by adding it.

## Textures from ComfyUI (optional)

```
python comfy_textures.py --out textures/
python build_skin.py --out out/Phosphor --textures textures/
```

`comfy_textures.py` asks a local ComfyUI (Flux dev by default) for the
two large, forgiving surfaces - the window body and the display's
backdrop. `build_skin.py` then crops to the centre, flattens the
lighting, box-filters down to pixel size, makes the tile seamless, and
posterises by brightness onto a short ramp of the skin's own colours.

Everything with a state - buttons, thumbs, glyphs, corners - is always
drawn by `build_skin.py`, never generated: a pressed button has to line
up with its unpressed self pixel for pixel, which a diffusion model
can't promise.

**Licence.** Check the terms of the exact model and any LoRA before a
generated texture ships in a distributed build. The embedded example
skin deliberately uses no generated textures.

What was learned getting textures that tile:

- Ask for a **material swatch filling the frame edge to edge**, with
  flat light. "A texture of X" makes Flux photograph an object - a lit
  panel with a hotspot, a whole monitor with its bezel.
- Even then it lights the result, so the lighting-flatten step matters
  more than the prompt.
- Snapping to the nearest palette colour after flattening collapses
  subtle grain onto one colour. Ranking by brightness and giving each
  step of the ramp a fixed share keeps the grain visible.
