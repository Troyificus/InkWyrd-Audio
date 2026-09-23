"""Generate surface textures for a sprite skin with a local ComfyUI.

    python comfy_textures.py --out textures/            # all of them
    python comfy_textures.py --out textures/ --only window --seed 7

Talks to ComfyUI's HTTP API (default http://127.0.0.1:8188) and writes one
PNG per surface. build_skin.py --textures <folder> then pixelises them,
makes them tile, and forces them onto the skin's palette - so what comes
out of the model is raw material, never used as-is.

Only LARGE, FORGIVING areas are generated: the window body and the
display's backdrop. Bevels, corners, glyphs and anything with a state are
drawn by build_skin.py, because a diffusion model can't produce the
pixel-exact, pixel-aligned variants a button's normal/hover/pressed
states need.

LICENCE: check the terms of the exact model (and any LoRA) you generate
with before shipping the result in a distributed build. The skin the app
ships with is built WITHOUT this step for exactly that reason.

The prompts avoid naming any real product, so the model has no reason to
reach for anyone's logo or trade dress.
"""

import argparse
import json
import random
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

SURFACES = {
    # Phrased as a MATERIAL SWATCH filling the frame edge to edge, with
    # flat light: asked for "a texture of X", Flux tends to photograph an
    # object - a lit panel with a hotspot, a whole CRT with its bezel -
    # which can't tile. (Both happened on the first run.)
    "window": (
        "material swatch, dark gunmetal sheet metal surface filling the entire frame edge to edge, "
        "fine uniform brushed grain, a few tiny scratches, faint dark green tint, "
        "perfectly flat even diffuse lighting, no highlights, no hotspot, no vignette, "
        "orthographic top-down, seamless pattern, no objects, no text, no logo"
    ),
    "display": (
        "material swatch, extreme macro of a dark green monochrome LCD pixel grid filling the entire "
        "frame edge to edge, uniform fine horizontal scanlines, very dark green-black, faint even glow, "
        "perfectly flat even lighting, no bezel, no screen edges, no vignette, no text, no characters, "
        "seamless pattern"
    ),
}

NEGATIVE_HINT = ""  # Flux dev ignores negative prompts at cfg 1; kept for other models.


def workflow(prompt, seed, width, height, steps, unet, weight_dtype):
    """API-format graph: Flux dev, fp8 weights, T5 + CLIP-L, guidance 3.5."""
    return {
        "1": {"class_type": "UNETLoader",
              "inputs": {"unet_name": unet, "weight_dtype": weight_dtype}},
        "2": {"class_type": "DualCLIPLoader",
              "inputs": {"clip_name1": "t5xxl_fp8_e4m3fn.safetensors",
                         "clip_name2": "clip_l.safetensors", "type": "flux"}},
        "3": {"class_type": "VAELoader", "inputs": {"vae_name": "ae.safetensors"}},
        "4": {"class_type": "CLIPTextEncode", "inputs": {"text": prompt, "clip": ["2", 0]}},
        "5": {"class_type": "FluxGuidance", "inputs": {"guidance": 3.5, "conditioning": ["4", 0]}},
        "6": {"class_type": "CLIPTextEncode", "inputs": {"text": NEGATIVE_HINT, "clip": ["2", 0]}},
        "7": {"class_type": "EmptySD3LatentImage",
              "inputs": {"width": width, "height": height, "batch_size": 1}},
        "8": {"class_type": "KSampler",
              "inputs": {"model": ["1", 0], "positive": ["5", 0], "negative": ["6", 0],
                         "latent_image": ["7", 0], "seed": seed, "steps": steps, "cfg": 1.0,
                         "sampler_name": "euler", "scheduler": "simple", "denoise": 1.0}},
        "9": {"class_type": "VAEDecode", "inputs": {"samples": ["8", 0], "vae": ["3", 0]}},
        "10": {"class_type": "SaveImage",
               "inputs": {"images": ["9", 0], "filename_prefix": "inkwyrd_skin"}},
    }


def post(server, path, payload):
    req = urllib.request.Request(server + path, data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.loads(r.read())


def get(server, path):
    with urllib.request.urlopen(server + path, timeout=30) as r:
        return r.read()


def generate(server, graph, timeout_s):
    prompt_id = post(server, "/prompt", {"prompt": graph, "client_id": "inkwyrd-skin-builder"})["prompt_id"]
    started = time.time()
    while time.time() - started < timeout_s:
        history = json.loads(get(server, "/history/" + prompt_id))
        entry = history.get(prompt_id)
        if entry:
            status = entry.get("status", {})
            if status.get("status_str") == "error":
                sys.exit("ComfyUI reported an error: " + json.dumps(status.get("messages", []))[:2000])
            for node in entry.get("outputs", {}).values():
                for image in node.get("images", []):
                    query = urllib.parse.urlencode({"filename": image["filename"],
                                                    "subfolder": image.get("subfolder", ""),
                                                    "type": image.get("type", "output")})
                    return get(server, "/view?" + query)
        time.sleep(2)
    sys.exit(f"Timed out after {timeout_s}s waiting for ComfyUI")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--server", default="http://127.0.0.1:8188")
    ap.add_argument("--only", choices=sorted(SURFACES))
    ap.add_argument("--seed", type=int)
    ap.add_argument("--steps", type=int, default=24)
    ap.add_argument("--size", type=int, default=1024)
    ap.add_argument("--unet", default="flux1-dev.safetensors")
    ap.add_argument("--weight-dtype", default="fp8_e4m3fn")
    ap.add_argument("--timeout", type=int, default=900)
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    names = [args.only] if args.only else sorted(SURFACES)
    manifest = {}

    for name in names:
        seed = args.seed if args.seed is not None else random.randrange(2 ** 31)
        print(f"{name}: seed {seed} ...", flush=True)
        t0 = time.time()
        png = generate(args.server,
                       workflow(SURFACES[name], seed, args.size, args.size, args.steps,
                                args.unet, args.weight_dtype),
                       args.timeout)
        (out / f"{name}.png").write_bytes(png)
        manifest[name] = {"seed": seed, "prompt": SURFACES[name], "model": args.unet,
                          "steps": args.steps, "size": args.size}
        print(f"  wrote {out / (name + '.png')} in {time.time() - t0:.0f}s", flush=True)

    # So a texture someone likes can be regenerated or credited later.
    (out / "textures.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
