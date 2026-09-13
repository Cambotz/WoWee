#!/usr/bin/env python3
"""Colour has to survive the mip chain the upscaler builds.

    tools/upscale_mip_check.py

Pillow's resize weights colour by alpha. A texel that is fully transparent
contributes nothing to the average and comes back black, so an RGBA resize
quietly throws away whatever is painted under the transparent part of a sheet.

That is harmless while every texture carrying alpha is a cutout - nothing
samples a texel it has keyed away. It stopped being harmless when an M2 batch
marked opaque stopped keying: blend mode 0 does not read alpha at all, and
Silverpine's trunks sample an atlas panel whose alpha is left over from another
layer. Level 0 held the bark and every level under it was black, so the trunks
were solid up close and holed in black at any distance. 727 of the 823
alpha-bearing sidecars were built that way.

So the tool halves the channels separately, and this holds it there. The trap
is silent and one line wide - build_mip_chain going back to Image.resize on an
RGBA image would reintroduce it with nothing to show for it until someone
walked a mip chain by hand.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import numpy as np
    from PIL import Image
except ImportError as exc:                   # pragma: no cover - environment
    print(f"skipped: {exc}")
    sys.exit(0)

from upscale_textures import build_mip_chain, halve_straight

BARK = (107, 84, 47)          # measured under the alpha of SilverPineTree01TrunkSkin
CUTOFF = 0.5
DARK = 20                     # luma below which a backing is a backing


def luma(rgb):
    return 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]


def sheet():
    """Bright colour everywhere, transparent down the left half."""
    arr = np.zeros((64, 64, 4), np.uint8)
    arr[..., 0], arr[..., 1], arr[..., 2] = BARK
    arr[:, :32, 3] = 0
    arr[:, 32:, 3] = 255
    return arr


def colour_under_alpha(level):
    clear = level[..., 3] < 50
    if not clear.any():
        return None
    return level[..., :3][clear].mean(axis=0)


def main() -> int:
    failures = []

    # The premultiply itself, named so the reason this file exists stays
    # visible even if Pillow ever changes its mind.
    resized = np.asarray(Image.fromarray(sheet(), "RGBA").resize((32, 32), Image.BOX))
    if luma(colour_under_alpha(resized)) >= DARK:
        print("note: Pillow's RGBA resize no longer premultiplies")

    halved = halve_straight(sheet())
    under = colour_under_alpha(halved)
    if under is None or luma(under) < DARK:
        failures.append(f"halve_straight lost the colour under alpha: {under}")

    levels = build_mip_chain(sheet(), CUTOFF)
    if len(levels) < 6:
        failures.append(f"mip chain stopped at {len(levels)} levels")
    for i, level in enumerate(levels):
        under = colour_under_alpha(level)
        if under is None:
            continue
        if luma(under) < DARK:
            failures.append(
                f"mip {i} ({level.shape[1]}x{level.shape[0]}) went black under alpha: "
                f"{under.round(1)}, luma {luma(under):.1f}")

    for line in failures:
        print(f"FAIL: {line}")
    if failures:
        return 1
    print(f"ok: colour holds under alpha across {len(levels)} mip levels")
    return 0


if __name__ == "__main__":
    sys.exit(main())
