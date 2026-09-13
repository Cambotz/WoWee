#!/usr/bin/env python3
"""Offline texture upscale pass over an extracted asset tree.

    tools/upscale_textures.py

Decodes BLPs with blp_convert, upscales them, and writes the result back as a
sidecar beside the source: ``foo.dds`` by default, DXT5 blocks with a mip chain
built to hold the cutout's coverage, or ``foo.png`` as RGBA8 with --format png.
AssetManager prefers a sidecar to the BLP - .dds first, then .png - so an
upscaled tree takes effect on the next launch with no repacking, and deleting
the sidecar reverts it. ``extract_assets.sh --upscale`` runs this as the last
step of an extraction.

The blocks are the default because a PNG override is uncompressed on the GPU:
four times the resolution costs sixty-four times the memory as RGBA8 and
sixteen as DXT5, and sixteen is the honest price of sixteen times the texels.

CONSISTENCY, WHICH IS THE WHOLE POINT

An upscale that reaches some of a model's textures and not the rest looks worse
than no upscale at all: a sharp canopy over a blurred trunk reads as a bug. So:

  1. Selection is by model, not by filename. The tool reads each M2's own
     texture list and takes all of them. Filename tokens choose the *models* -
     "tree", "bush", "vine" - and a trunk sheet named Bark03.blp comes along
     because the tree that uses it was chosen, which is exactly what a token
     sweep over texture names would have missed. The exception is a sheet more
     than --shared-limit models draw: one of those belongs to the whole world,
     not to this tree, and it is left as it shipped.
  2. One factor for the whole run. Every texture is multiplied by the same
     scale and capped at the same ceiling, so texel density stays in the
     proportion the artists set rather than being flattened to one size.
  3. Alpha is coverage, not colour. The fraction of the sheet above the cutout
     threshold is measured before and restored after - and again on every mip
     level the .dds carries, which is the one thing the GPU's own mip
     generation cannot do - so a canopy is neither thicker nor thinner than it
     was authored, near or far. Colour is dilated into the transparent texels
     first, which is where DXT keeps its black garbage; the upscaler would
     otherwise smear it into the leaf edges.
  4. A texture the model wraps is upscaled wrapped: the edges are padded from
     the opposite side and cropped off after, so a tiling sheet still tiles.
  5. Every sidecar records what made it - backend, model, scale, cap, cutoff,
     and the source's hash. A file whose source or parameters have moved on is
     stale, ``--verify`` names it, and a plain run refuses to quietly leave a
     tree half-processed under two sets of settings.

BACKENDS, best first (override with WOWEE_UPSCALER):

  realesrgan-ncnn-vulkan   AI upscale, on the x4plus-anime model: it is the
                           one trained on painted art rather than photographs,
                           and it holds the sheet's tone. Deterministic for a
                           given model and input, which is what lets rule 5
                           mean anything.
  pillow                   Lanczos plus a light unsharp. Always available,
                           adds no detail, but removes the bilinear smear.

Examples:
  python3 tools/upscale_textures.py --dry-run
  python3 tools/upscale_textures.py --data-dir Data --scale 4
  python3 tools/upscale_textures.py --verify
  python3 tools/upscale_textures.py --clean
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import struct
import sys
import tempfile
from io import BytesIO
from pathlib import Path

try:
    import numpy as np
    from PIL import Image, ImageFilter
except ImportError as exc:  # pragma: no cover - the message is the point
    # macOS ships a python3 with neither, and this is an optional pass: say
    # what to install rather than handing over a traceback.
    sys.exit(f"{exc.name} is required by this tool.\n"
             f"  python3 -m pip install --user pillow numpy\n"
             f"  (macOS with Homebrew python: python3 -m pip install --break-system-packages "
             f"pillow numpy, or run it from a venv)")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import m2_textures  # noqa: E402
from asset_files import sha256_of, wmo_texture_names  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
WMO_GROUP_RE = re.compile(r"_\d{3}\.wmo$")
MANIFEST_NAME = "upscale_manifest.json"
LEGACY_MANIFEST_NAME = "upscale_manifest.txt"

# Models whose path carries one of these are foliage. The tokens pick models,
# not textures - see rule 1.
FOLIAGE_TOKENS = [
    "tree", "bush", "plant", "leaf", "leaves", "shrub", "palm", "canopy",
    "branch", "foliage", "cactus", "vine", "fern", "flower", "grass",
    "mushroom", "kelp", "moss",
]
FALSE_POSITIVES = ["infernal"]  # contains a token, is not foliage

# Padding carried into the upscaler and cropped off after, so the filter has
# something to chew on at the edges instead of inventing it.
PAD_TEXELS = 8


# ---------------------------------------------------------------------------
# Tools and backends
# ---------------------------------------------------------------------------

def find_blp_convert() -> Path:
    for cand in (REPO_ROOT / "build/bin/blp_convert", REPO_ROOT / "build/blp_convert",
                 REPO_ROOT / "blp_convert"):
        if cand.is_file() and os.access(cand, os.X_OK):
            return cand
    sys.exit("blp_convert not found - build it: cmake --build build --target blp_convert")


def detect_backend():
    """(name, executable, model) of the best upscaler present.

    Nothing here is required: with no AI upscaler on the machine the pass still
    runs on Pillow, which is a resize and says so. That matters on macOS, where
    realesrgan-ncnn-vulkan has no package - it is a zip from the project's
    releases page, and WOWEE_UPSCALER points at wherever it was unzipped.
    """
    exe = os.environ.get("WOWEE_UPSCALER") or shutil.which("realesrgan-ncnn-vulkan")
    if exe:
        if not os.access(exe, os.X_OK):
            sys.exit(f"WOWEE_UPSCALER is not executable: {exe}")
        # x4plus-anime over x4plus: the photo model invents film-grain detail
        # and pulls the sheet darker - it measured 2.7% down in luma on the
        # Stranglethorn canopy, which across a zone is a change of hour, not of
        # resolution. The painted-art model holds the tone (0.4% up) and
        # resolves the edges, which is all that is wanted from hand-painted
        # source. WOWEE_UPSCALER_MODEL picks another.
        return ("realesrgan", exe,
                os.environ.get("WOWEE_UPSCALER_MODEL", "realesrgan-x4plus-anime"))
    return ("pillow", "", "lanczos+unsharp")


# ---------------------------------------------------------------------------
# Image steps
# ---------------------------------------------------------------------------

def load_rgba(path: Path) -> np.ndarray:
    with Image.open(path) as im:
        return np.asarray(im.convert("RGBA"), dtype=np.uint8).copy()


def save_rgba(arr: np.ndarray, path: Path) -> None:
    Image.fromarray(arr, "RGBA").save(path, optimize=True)


#: Alpha at or below this is the transparent part of a sheet.
CLEAR_ALPHA = 32
#: Mean luma below which the colour under the alpha is a backing, not artwork.
BACKING_LUMA = 20
#: Too few transparent texels to conclude anything from.
MIN_CLEAR_TEXELS = 64


def backing_is_painted(arr: np.ndarray) -> bool:
    """Whether real artwork lies under this sheet's transparent texels.

    The mirror of BLPImage::alphaIsSilhouette, and it has to stay one: the
    client decides whether to key a batch's alpha away by the same measure, so
    a texture the client draws opaque is exactly a texture whose hidden colour
    ends up on screen.

    A card drawn on a black backing leaves that backing under the mask and
    nothing is lost by filling it. An atlas painted edge to edge - the alpha
    left over from another layer - leaves the art, and the art is what an
    opaque batch samples.
    """
    clear = arr[..., 3] <= CLEAR_ALPHA
    if int(clear.sum()) < MIN_CLEAR_TEXELS:
        return False
    rgb = arr[..., :3][clear].astype(np.float32)
    luma = 0.299 * rgb[:, 0] + 0.587 * rgb[:, 1] + 0.114 * rgb[:, 2]
    return float(luma.mean()) >= BACKING_LUMA


def restore_hidden_colour(arr: np.ndarray, src: np.ndarray) -> np.ndarray:
    """Put the source's own colour back under the transparent texels.

    An upscaler is judged on what it can see, and it cannot see under a
    transparent texel - so whatever it puts there is unconstrained. Measured
    against a plain Lanczos of the same source, SilverPineTree01TrunkSkin's
    upscale drifts 6 units across the opaque part of the sheet and 15 under the
    alpha: two and a half times as far, in the one region nobody could have
    reviewed because nothing drew it.

    Nothing did draw it, until an opaque batch stopped keying its alpha away.
    Then it became the middle of every Silverpine trunk, and what the model had
    invented there read as hard-edged slabs of the wrong colour.

    So that region comes from the source, resampled and not imagined. The
    opaque texels keep the upscale, which is the part it was good at.
    """
    height, width = arr.shape[:2]
    mask = np.asarray(
        Image.fromarray(((src[..., 3] <= CLEAR_ALPHA).astype(np.uint8) * 255), "L")
        .resize((width, height), Image.NEAREST)) > 127
    if not mask.any():
        return arr
    ref = np.asarray(Image.fromarray(src[..., :3], "RGB").resize((width, height), Image.LANCZOS),
                     dtype=np.uint8)
    out = arr.copy()
    out[..., :3][mask] = ref[mask]
    return out


def dilate_rgb(arr: np.ndarray, passes: int = 6, thresh: int = 8) -> np.ndarray:
    """Push colour outward into the transparent texels.

    A DXT block stores one colour pair for every texel in it, so the texels
    that are fully transparent hold whatever the compressor found convenient -
    usually near black. Nothing samples them directly, but every filter that
    touches them does: bilinear at runtime, and the upscaler here. Filling them
    with the nearest real colour is the difference between a leaf edge that
    fades to leaf and one that fades to a dark rim.
    """
    rgb = arr[..., :3].astype(np.float32)
    known = arr[..., 3] > thresh
    if known.all() or not known.any():
        return arr
    for _ in range(passes):
        if known.all():
            break
        padded = np.pad(rgb, ((1, 1), (1, 1), (0, 0)), mode="edge")
        padded_known = np.pad(known.astype(np.float32), ((1, 1), (1, 1)), mode="edge")
        acc = np.zeros_like(rgb)
        weight = np.zeros(rgb.shape[:2], dtype=np.float32)
        for dy in (0, 1, 2):
            for dx in (0, 1, 2):
                if dy == 1 and dx == 1:
                    continue
                w = padded_known[dy:dy + rgb.shape[0], dx:dx + rgb.shape[1]]
                acc += padded[dy:dy + rgb.shape[0], dx:dx + rgb.shape[1], :] * w[..., None]
                weight += w
        fill = (weight > 0) & ~known
        rgb[fill] = acc[fill] / weight[fill][..., None]
        known = known | fill
    out = arr.copy()
    out[..., :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    return out


def pad_edges(arr: np.ndarray, pad: int, wrap_x: bool, wrap_y: bool) -> np.ndarray:
    """Pad by `pad` texels, wrapping on the axes the model wraps on."""
    mode_y = "wrap" if wrap_y else "edge"
    mode_x = "wrap" if wrap_x else "edge"
    out = np.pad(arr, ((pad, pad), (0, 0), (0, 0)), mode=mode_y)
    out = np.pad(out, ((0, 0), (pad, pad), (0, 0)), mode=mode_x)
    return out


def crop_padding(arr: np.ndarray, pad: int, scale: int) -> np.ndarray:
    p = pad * scale
    return arr[p:arr.shape[0] - p, p:arr.shape[1] - p, :] if p else arr


def alpha_coverage(alpha: np.ndarray, cutoff: float) -> float:
    return float((alpha >= cutoff * 255.0).mean())


def restore_coverage(arr: np.ndarray, target: float, cutoff: float) -> np.ndarray:
    """Scale alpha so the same fraction of the sheet survives the cutout.

    Any resampling of an alpha channel moves its coverage - a leaf sheet comes
    back thinner or fatter than it was painted, and a canopy with it. Bisect a
    multiplier the way mip generation ought to and rarely does.
    """
    alpha = arr[..., 3].astype(np.float32)
    if target <= 0.0 or target >= 1.0:
        return arr
    lo, hi = 0.25, 4.0
    best = 1.0
    for _ in range(16):
        mid = 0.5 * (lo + hi)
        if alpha_coverage(np.clip(alpha * mid, 0, 255), cutoff) < target:
            lo = mid
        else:
            hi = mid
        best = mid
    out = arr.copy()
    out[..., 3] = np.clip(alpha * best, 0, 255).astype(np.uint8)
    return out


def halve_straight(arr: np.ndarray) -> np.ndarray:
    """Halve an RGBA array without premultiplying the colour.

    Pillow's resize on an RGBA image weights colour by alpha, so a texel that
    is fully transparent contributes nothing and comes out black. That is the
    right thing for a texture whose transparent texels hold junk, and the wrong
    thing for one whose transparent texels were dilated on purpose. Splitting
    the channels and filtering each on its own keeps the colour that was put
    there.
    """
    height, width = arr.shape[:2]
    size = (max(1, width // 2), max(1, height // 2))
    rgb = Image.fromarray(arr[..., :3], "RGB").resize(size, Image.BOX)
    alpha = Image.fromarray(arr[..., 3], "L").resize(size, Image.BOX)
    return np.dstack([np.asarray(rgb, dtype=np.uint8),
                      np.asarray(alpha, dtype=np.uint8)])


def build_mip_chain(arr: np.ndarray, cutoff: float):
    """Every mip level down to 1x1, with the cutout's coverage held.

    A box filter on an alpha-tested sheet thins it at every level - the canopy
    that keeps its leaves up close is a skeleton four levels down, which is
    what the renderer's mip-alpha boost is papering over. Restoring coverage
    per level fixes it where the numbers are, which the GPU's own mip
    generation cannot: it has no idea the texture is a cutout.

    Colour is filtered straight rather than premultiplied, which is safe here
    and only here: the transparent texels were dilated to hold the colour of
    the leaf beside them, so what bleeds inward is the leaf.

    Straight is what halve_straight does, and it has to be done by hand.
    Resizing an RGBA image through Pillow premultiplies - colour under a
    transparent texel comes back zeroed - so the chain this used to build threw
    away everything dilate_rgb had just put there, at every level below the
    base. It cost nothing while the only textures with alpha were cutouts,
    because nothing samples a texel it has keyed away. It cost Silverpine's
    trunks their midsection once an opaque batch stopped keying: level 0 held
    the bark, and every level under it was black.
    """
    base_coverage = alpha_coverage(arr[..., 3], cutoff)
    levels = [arr]
    cur = arr
    while cur.shape[1] > 1 or cur.shape[0] > 1:
        cur = halve_straight(cur)
        if 0.0 < base_coverage < 1.0:
            cur = restore_coverage(cur, base_coverage, cutoff)
        levels.append(cur)
    return levels


def dds_dxt5(levels) -> bytes:
    """A DXT5 .dds holding the whole chain, encoded level by level by Pillow."""
    payload = []
    for level in levels:
        buf = BytesIO()
        Image.fromarray(level, "RGBA").save(buf, format="DDS", pixel_format="DXT5")
        payload.append(buf.getvalue()[128:])   # past the 128-byte legacy header

    h, w = levels[0].shape[:2]
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000  # caps|h|w|pixelformat|mips|linearsize
    caps = 0x1000 | 0x400000 | 0x8                        # texture|mipmap|complex
    header = struct.pack(
        "<4sIIIIIII44sIIIIIIIIIIIII",
        b"DDS ", 124, flags, h, w, len(payload[0]), 0, len(levels),
        b"\x00" * 44,
        32, 0x4, int.from_bytes(b"DXT5", "little"), 0, 0, 0, 0, 0,
        caps, 0, 0, 0, 0)
    assert len(header) == 128, len(header)
    return header + b"".join(payload)


def upscale_pillow(arr: np.ndarray, scale: int) -> np.ndarray:
    """Lanczos, with the sharpen on colour only.

    Alpha is not an image here, it is the cutout - an unsharp mask on it puts
    ringing either side of every leaf edge, which is a halo of holes and a
    halo of fringe. Colour gets the sharpen; alpha gets the resize and nothing
    else, and its coverage is restored afterwards.
    """
    rgb = Image.fromarray(arr[..., :3], "RGB")
    alpha = Image.fromarray(arr[..., 3], "L")
    size = (rgb.width * scale, rgb.height * scale)
    rgb = rgb.resize(size, Image.LANCZOS).filter(
        ImageFilter.UnsharpMask(radius=scale, percent=45, threshold=3))
    alpha = alpha.resize(size, Image.LANCZOS)
    out = np.dstack([np.asarray(rgb, dtype=np.uint8),
                     np.asarray(alpha, dtype=np.uint8)])
    return np.ascontiguousarray(out)


def upscale_realesrgan(exe: str, model: str, in_dir: Path, out_dir: Path, scale: int) -> None:
    """Directory mode: one model load for the whole batch.

    The models live in a `models` folder beside the binary and the default is
    to look for one relative to the working directory, so it is named here -
    otherwise the tool only works when run from wherever the zip was unpacked.
    A symlink on PATH is resolved first, so `ln -s .../realesrgan ~/bin` works.
    """
    cmd = [exe, "-i", str(in_dir), "-o", str(out_dir),
           "-s", str(scale), "-n", model, "-f", "png"]
    models = Path(exe).resolve().parent / "models"
    if models.is_dir():
        cmd += ["-m", str(models)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip() or "realesrgan failed")


def cap_dimensions(arr: np.ndarray, max_dim: int, block_aligned: bool = False) -> np.ndarray:
    """Shrink to the ceiling, on a multiple of four when blocks want one."""
    h, w = arr.shape[:2]
    if max(h, w) <= max_dim:
        return arr
    f = max_dim / float(max(h, w))
    nw, nh = max(1, int(round(w * f))), max(1, int(round(h * f)))
    if block_aligned:
        nw, nh = max(4, nw - nw % 4), max(4, nh - nh % 4)
    im = Image.fromarray(arr, "RGBA").resize((nw, nh), Image.LANCZOS)
    return np.asarray(im, dtype=np.uint8).copy()


# ---------------------------------------------------------------------------
# Selection
# ---------------------------------------------------------------------------

def expansion_roots(data_dir: Path):
    """Each extracted expansion is its own asset root; a bare tree is one."""
    roots = sorted(p for p in (data_dir / "expansions").glob("*") if p.is_dir()) \
        if (data_dir / "expansions").is_dir() else []
    return roots or [data_dir]


def index_root(root: Path):
    """Lowercased relative path → file, for resolving a model's texture names.

    The extractor writes paths lowercased with forward slashes; an M2 spells
    them however the artist typed them.
    """
    index = {}
    for path in root.rglob("*.blp"):
        index[str(path.relative_to(root)).lower().replace("\\", "/")] = path
    return index


def texture_users(root: Path):
    """(texture key → the models that use it, texture key → wrap flags).

    Every model in the tree, not only the selected ones: a texture is only safe
    to upscale when everything that draws it is being upscaled too.
    """
    users, flags = {}, {}
    for m2 in sorted(root.rglob("*.m2")):
        rel = str(m2.relative_to(root)).lower().replace("\\", "/")
        try:
            entries = m2_textures.texture_entries(m2.read_bytes())
        except OSError:
            continue
        for e in entries:
            if e["type"] != 0 or not e["filename"]:
                continue
            key = e["filename"].lower().replace("\\", "/")
            users.setdefault(key, set()).add(rel)
            # A texture two models disagree about wraps: the padding it buys is
            # a few texels that get cropped off either way.
            flags[key] = flags.get(key, 0) | e["flags"]
    for wmo in sorted(root.rglob("*.wmo")):
        rel = str(wmo.relative_to(root)).lower().replace("\\", "/")
        if WMO_GROUP_RE.search(rel):
            continue
        for name in wmo_texture_names(wmo):
            users.setdefault(name.lower().replace("\\", "/"), set()).add(rel)
    return users, flags


def model_matches(rel: str, tokens) -> bool:
    probe = rel
    for fp in FALSE_POSITIVES:
        probe = probe.replace(fp, "")
    return any(t in probe for t in tokens)


def collect(data_dir: Path, tokens, take_all: bool, texture_includes,
            shared_limit: int, limit: int):
    """(texture, wrap flags) for the chosen models, and the sheets left alone.

    A chosen model brings all of its textures, whatever they are called: that
    is the rule the whole pass exists for. The exception is a sheet the world
    shares - smoke, a reflection map, a shadow blob, anything more than
    `shared_limit` models draw - because upscaling one of those for one tree
    changes it for every creature and every weapon that also uses it. Those are
    left as they shipped and counted in the report.
    """
    selected = {}
    shared = 0
    for root in expansion_roots(data_dir):
        index = index_root(root)
        users, flags = texture_users(root)
        for key, model_set in users.items():
            if not any(rel.startswith(("world/", "environment/"))
                       and (take_all or model_matches(rel, tokens))
                       for rel in model_set):
                continue
            if len(model_set) > shared_limit:
                shared += 1
                continue
            blp = index.get(key)
            if blp is not None:
                selected[blp] = flags.get(key, 0)
        for frag in texture_includes:
            for key, blp in index.items():
                if frag in key:
                    selected.setdefault(blp, 0)
        if limit and len(selected) >= limit:
            break
    items = sorted(selected.items())
    return (items[:limit] if limit else items), shared


# ---------------------------------------------------------------------------
# Manifest
# ---------------------------------------------------------------------------

def load_manifest(data_dir: Path) -> dict:
    path = data_dir / MANIFEST_NAME
    if path.is_file():
        try:
            return json.loads(path.read_text())
        except (json.JSONDecodeError, OSError):
            pass
    return {"tool": "upscale_textures", "entries": {}}


def save_manifest(data_dir: Path, manifest: dict) -> None:
    (data_dir / MANIFEST_NAME).write_text(json.dumps(manifest, indent=1, sort_keys=True) + "\n")


def params_of(args, backend, model) -> dict:
    return {"backend": backend, "model": model, "scale": args.scale,
            "max_dim": args.max_dim, "alpha_cutoff": args.alpha_cutoff,
            "format": args.format}


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def decode_blp(blp: Path, blp_convert: Path, tmp: Path):
    """The source BLP as RGBA, or None if there isn't one to read.

    blp_convert writes its PNG beside its input, so the BLP is staged into a
    scratch directory first: decoding in place would create the very sidecar
    that overrides the source.
    """
    if not blp.is_file():
        return None
    staged = tmp / "src.blp"
    shutil.copyfile(blp, staged)
    r = subprocess.run([str(blp_convert), "--to-png", str(staged)],
                       capture_output=True, text=True)
    staged.unlink(missing_ok=True)
    png = tmp / "src.png"
    try:
        if r.returncode != 0 or not png.is_file():
            return None
        return load_rgba(png)
    finally:
        png.unlink(missing_ok=True)


def do_repair(data_dir: Path, blp_convert: Path, cutoff: float, dry_run: bool) -> int:
    """Repair existing .dds sidecars in place, without re-running an upscaler.

    Two faults, both of which only ever showed on a texture whose alpha
    something stopped keying away.

    build_mip_chain used to resize through Pillow's RGBA path, which weights
    colour by alpha - so every level below the base came back black wherever
    the texture was transparent. Silverpine's trunks were solid up close and
    holed in black at any distance, because the bark was in level 0 and nowhere
    else.

    And level 0 itself holds whatever the upscaler invented under the alpha,
    which is unconstrained: it is scored on what it can see. That read as
    hard-edged slabs of the wrong colour across the middle of every trunk. So
    where the backing is painted, it comes back from the source BLP beside the
    sidecar, resampled rather than imagined.

    No upscaler runs either way. A sheet whose backing is junk keeps its base
    byte for byte, not even requantised; one whose backing is artwork has only
    its hidden texels replaced.
    """
    repaired = restored = skipped = failed = 0
    with tempfile.TemporaryDirectory(prefix="wowee_repair_") as tmp:
        tmp = Path(tmp)
        for path in sorted(data_dir.rglob("*.dds")):
            try:
                raw = path.read_bytes()
                if raw[:4] != b"DDS " or len(raw) < 128:
                    skipped += 1
                    continue
                height, width = struct.unpack_from("<II", raw, 12)
                mip_count = struct.unpack_from("<I", raw, 28)[0]
                if raw[84:88] != b"DXT5" or mip_count < 2:
                    skipped += 1
                    continue

                base = np.asarray(Image.open(path).convert("RGBA"), dtype=np.uint8)
                if base.shape[0] != height or base.shape[1] != width:
                    skipped += 1
                    continue
                if not (0 < alpha_coverage(base[..., 3], cutoff) < 1.0):
                    # No cutout to hold and no transparent texel to lose colour
                    # under. Whatever chain it has is as good as one built here.
                    skipped += 1
                    continue

                src = decode_blp(path.with_suffix(".blp"), blp_convert, tmp)
                painted = src is not None and backing_is_painted(src)
                if painted:
                    base = restore_hidden_colour(base, src)
                    restored += 1
                    levels = build_mip_chain(base, cutoff)
                    out = dds_dxt5(levels)
                else:
                    levels = build_mip_chain(dilate_rgb(base), cutoff)
                    rebuilt = dds_dxt5(levels)
                    # Splice the original base back over the re-encoded one.
                    base_bytes = max(1, (width + 3) // 4) * max(1, (height + 3) // 4) * 16
                    out = rebuilt[:128] + raw[128:128 + base_bytes] + rebuilt[128 + base_bytes:]
                if not dry_run:
                    path.write_bytes(out)
                repaired += 1
                if repaired % 100 == 0:
                    print(f"  {repaired} repaired...", flush=True)
            except Exception as exc:                   # noqa: BLE001 - report and continue
                print(f"  {path.relative_to(data_dir)}: {exc}")
                failed += 1
    verb = "would repair" if dry_run else "repaired"
    print(f"{verb} {repaired} sidecar(s) ({restored} with artwork restored under the alpha), "
          f"skipped {skipped}, failed {failed}")
    return 1 if failed else 0


def do_clean(data_dir: Path) -> None:
    manifest = load_manifest(data_dir)
    removed = 0
    for rel in manifest.get("entries", {}):
        p = data_dir / rel
        if p.is_file():
            p.unlink()
            removed += 1
    for name in (MANIFEST_NAME, LEGACY_MANIFEST_NAME):
        legacy = data_dir / name
        if name == LEGACY_MANIFEST_NAME and legacy.is_file():
            # The list-of-paths manifest this tool used to keep.
            for line in legacy.read_text().splitlines():
                p = data_dir / line.strip()
                if p.is_file():
                    p.unlink()
                    removed += 1
        if legacy.is_file():
            legacy.unlink()
    print(f"Removed {removed} generated sidecar(s).")


def do_verify(data_dir: Path, selected, params) -> int:
    manifest = load_manifest(data_dir)
    entries = manifest.get("entries", {})
    missing, changed, stale, unprocessed = [], [], [], []

    for rel, rec in sorted(entries.items()):
        sidecar = data_dir / rel
        source = data_dir / rec["source"]
        if not sidecar.is_file():
            missing.append(rel)
        elif not source.is_file() or sha256_of(source) != rec["sha256"]:
            changed.append(rel)
        elif rec.get("params") != params:
            stale.append(rel)

    for blp, _flags in selected:
        rel_sidecar = str(blp.with_suffix("." + params["format"]).relative_to(data_dir))
        if rel_sidecar not in entries and not (data_dir / rel_sidecar).is_file():
            unprocessed.append(rel_sidecar)

    for label, group in (("sidecar missing", missing), ("source changed", changed),
                         ("made with other parameters", stale),
                         ("selected but not processed", unprocessed)):
        if group:
            print(f"{len(group)} {label}:")
            for rel in group[:20]:
                print("   ", rel)
            if len(group) > 20:
                print(f"    ... and {len(group) - 20} more")
    if not (missing or changed or stale or unprocessed):
        print(f"Consistent: {len(entries)} sidecar(s), all current with {params}")
        return 0
    print("Run without --verify to bring the tree up to date (--force to redo stale ones).")
    return 1


def process_batch(batch, blp_convert, backend, exe, model, args, data_dir, manifest):
    """Decode, upscale and write one batch of textures. Returns (done, failed)."""
    done = failed = 0
    with tempfile.TemporaryDirectory(prefix="wowee_upscale_") as tmp:
        tmp = Path(tmp)
        in_dir, out_dir = tmp / "in", tmp / "out"
        in_dir.mkdir()
        out_dir.mkdir()

        staged = {}   # staged name → (blp path, source coverage, source hash)
        for i, (blp, flags) in enumerate(batch):
            name = f"{i:04d}"
            copy = in_dir / f"{name}.blp"
            shutil.copyfile(blp, copy)
            # blp_convert writes beside its input; decoding in place would
            # create the very sidecar that overrides the source.
            r = subprocess.run([str(blp_convert), "--to-png", str(copy)],
                               capture_output=True, text=True)
            copy.unlink(missing_ok=True)
            png = in_dir / f"{name}.png"
            if r.returncode != 0 or not png.is_file():
                print(f"  DECODE FAIL {blp.relative_to(data_dir)}: {r.stderr.strip()}",
                      file=sys.stderr)
                failed += 1
                continue
            src = load_rgba(png)
            coverage = alpha_coverage(src[..., 3], args.alpha_cutoff)
            # Dilation fills the transparent texels with the colour beside
            # them, which is right when they hold the compressor's junk and
            # destructive when they hold artwork an opaque batch will sample.
            painted = backing_is_painted(src)
            prepared = pad_edges(src if painted else dilate_rgb(src), PAD_TEXELS,
                                 bool(flags & m2_textures.TEXTURE_WRAP_X),
                                 bool(flags & m2_textures.TEXTURE_WRAP_Y))
            save_rgba(prepared, png)
            staged[f"{name}.png"] = (blp, coverage, sha256_of(blp), src.shape[1], src.shape[0],
                                     painted, src)

        if not staged:
            return done, failed

        if backend == "realesrgan":
            try:
                upscale_realesrgan(exe, model, in_dir, out_dir, args.scale)
            except RuntimeError as e:
                print(f"  UPSCALE FAIL (batch of {len(staged)}): {e}", file=sys.stderr)
                return done, failed + len(staged)
        else:
            for name in staged:
                save_rgba(upscale_pillow(load_rgba(in_dir / name), args.scale), out_dir / name)

        for name, (blp, coverage, digest, src_w, src_h, painted, src) in staged.items():
            result = out_dir / name
            if not result.is_file():
                print(f"  MISSING OUTPUT {blp.relative_to(data_dir)}", file=sys.stderr)
                failed += 1
                continue
            arr = crop_padding(load_rgba(result), PAD_TEXELS, args.scale)
            if painted:
                # The sheet has artwork under its alpha and something draws it,
                # so it comes back from the source rather than from whatever
                # the backend guessed where it could not see.
                arr = restore_hidden_colour(arr, src)
            else:
                # Again, on the way out. Whatever the backend did with the
                # colour under the transparent texels - Lanczos undershoot, or
                # an AI model's guess - it did not have the leaf's colour in
                # mind, and runtime bilinear will drag it into the edge just
                # the same.
                arr = dilate_rgb(arr)
            if 0.0 < coverage < 1.0:
                arr = restore_coverage(arr, coverage, args.alpha_cutoff)
            arr = cap_dimensions(arr, args.max_dim, block_aligned=args.format == "dds")
            sidecar = blp.with_suffix("." + args.format)
            if args.format == "dds":
                levels = build_mip_chain(arr, args.alpha_cutoff)
                sidecar.write_bytes(dds_dxt5(levels))
                mips = len(levels)
            else:
                save_rgba(arr, sidecar)
                mips = 1
            # A sidecar of the other format for the same texture would win or
            # lose by extension rather than by being the newer one, so the one
            # this run did not write goes - if this tool was what put it there.
            other = blp.with_suffix(".png" if args.format == "dds" else ".dds")
            other_rel = str(other.relative_to(data_dir))
            if other_rel in manifest["entries"]:
                other.unlink(missing_ok=True)
                del manifest["entries"][other_rel]
            manifest["entries"][str(sidecar.relative_to(data_dir))] = {
                "source": str(blp.relative_to(data_dir)),
                "sha256": digest,
                "src": [src_w, src_h],
                "out": [int(arr.shape[1]), int(arr.shape[0])],
                "mips": mips,
                "params": params_of(args, backend, model),
            }
            done += 1
    return done, failed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data-dir", type=Path, default=REPO_ROOT / "build/bin/Data",
                    help="extracted Data root (default: build/bin/Data)")
    ap.add_argument("--include", action="append", default=[],
                    help="model path substring (repeatable); replaces the foliage preset")
    ap.add_argument("--texture-include", action="append", default=[],
                    help="texture path substring, for sheets no model claims (tilesets)")
    ap.add_argument("--all", action="store_true", help="every model under world/ and environment/")
    ap.add_argument("--shared-limit", type=int, default=8,
                    help="a sheet more than this many models use is left as it shipped")
    ap.add_argument("--format", choices=("dds", "png"), default="dds",
                    help="dds: DXT5 blocks with a coverage-preserving mip chain, a quarter "
                         "the video memory of png and what the client prefers (default). "
                         "png: RGBA8, for looking at the result in an image viewer")
    ap.add_argument("--scale", type=int, default=4, choices=(2, 3, 4))
    ap.add_argument("--max-dim", type=int, default=2048, help="cap output dimensions")
    ap.add_argument("--alpha-cutoff", type=float, default=0.5,
                    help="coverage threshold preserved across the upscale")
    ap.add_argument("--limit", type=int, default=0, help="process at most N textures")
    ap.add_argument("--batch-size", type=int, default=64, help="textures per upscaler invocation")
    ap.add_argument("--force", action="store_true", help="redo sidecars this tool already made")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true",
                    help="report sidecars that are missing, stale or hand-made; exit 1 if any")
    ap.add_argument("--clean", action="store_true", help="delete what this tool generated")
    ap.add_argument("--repair", action="store_true",
                    help="repair existing .dds sidecars in place without re-running an "
                         "upscaler: rebuild mip chains that lost their colour to the resize, "
                         "and put the source's own artwork back under transparent texels")
    args = ap.parse_args()

    data_dir = args.data_dir.resolve()
    if not data_dir.is_dir():
        sys.exit(f"Data dir not found: {data_dir}")

    if args.repair:
        return do_repair(data_dir, find_blp_convert(), args.alpha_cutoff, args.dry_run)

    if args.clean:
        do_clean(data_dir)
        return 0

    tokens = [t.lower() for t in (args.include or FOLIAGE_TOKENS)]
    selected, shared = collect(data_dir, tokens, args.all,
                               [t.lower() for t in args.texture_include],
                               args.shared_limit, args.limit)

    backend, exe, model = detect_backend()
    params = params_of(args, backend, model)

    if args.verify:
        return do_verify(data_dir, selected, params)

    manifest = load_manifest(data_dir)
    entries = manifest.setdefault("entries", {})
    manifest["params"] = params

    todo, other_params, handmade = [], 0, 0
    for blp, flags in selected:
        sidecar = blp.with_suffix("." + args.format)
        rel = str(sidecar.relative_to(data_dir))
        rec = entries.get(rel)
        if sidecar.is_file() and rec is None:
            handmade += 1          # someone's own override - never touched
            continue
        if rec is not None and sidecar.is_file():
            if rec.get("params") != params or rec.get("sha256") != sha256_of(blp):
                if not args.force:
                    other_params += 1
                    continue
            elif not args.force:
                continue
        todo.append((blp, flags))

    print(f"{len(selected)} texture(s) from the selected models, {len(todo)} to process"
          + (f" (models matching: {', '.join(tokens)})" if not args.all else " (all models)"))
    if shared:
        print(f"{shared} sheet(s) left as they shipped: more than {args.shared_limit} models "
              f"use each, so upscaling one would change every model that draws it.")
    if handmade:
        print(f"{handmade} hand-made sidecar(s) left alone.")
    if other_params and not args.force:
        print(f"{other_params} sidecar(s) were made with different settings or from a "
              f"different source - they would leave the tree inconsistent. --force redoes them.")
    if args.dry_run:
        for blp, flags in todo:
            print("   ", blp.relative_to(data_dir), f"(wrap flags {flags})")
        return 0
    if not todo:
        return 0

    blp_convert = find_blp_convert()
    print(f"Upscaler: {backend} {model}, scale {args.scale}x, cap {args.max_dim}px")
    if backend == "pillow":
        print("NOTE: this is a resize, not an upscale. For real detail, unzip "
              "realesrgan-ncnn-vulkan\n      (github.com/xinntao/Real-ESRGAN/releases; the "
              "macos build runs on Metal) and\n      set WOWEE_UPSCALER to it, then re-run "
              "with --force.")

    done = failed = 0
    for start in range(0, len(todo), args.batch_size):
        d, f = process_batch(todo[start:start + args.batch_size], blp_convert,
                             backend, exe, model, args, data_dir, manifest)
        done += d
        failed += f
        # Written per batch, so an interrupted run stays revertible and knows
        # exactly what it had already done.
        save_manifest(data_dir, manifest)
        print(f"  {min(start + args.batch_size, len(todo))}/{len(todo)} …", flush=True)

    print(f"Done: {done} sidecar(s) written, {failed} failed. "
          f"Restart wowee to see them; revert with --clean.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
