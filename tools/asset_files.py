#!/usr/bin/env python3
"""File facts the asset tools need more than one of.

    tools/asset_files.py

Two of them so far, both shared by the upscale pass and the pack builder: what
a file hashes to, and which textures a WMO names. Beside m2_textures.py, which
answers the same question for a model - a WMO keeps its texture list in one
MOTX chunk of null-terminated strings rather than in an array of header
entries, which is the only reason the two are separate functions.
"""

import hashlib
from pathlib import Path

# Textures a model lays over itself rather than draws itself with. Missing one
# costs an effect, not the surface. "glow" earns its place the hard way: an HD
# character model carries deathKnightEyeGlow.blp in slot 0 - the first slot is
# the body's on most models and an eye effect on these - so treating slot 0 as
# decisive without this disabled both gnome female models and left a display row
# pointing at nothing.
REFLECTION_WORDS = ("reflect", "envmap", "fresnel", "glass", "caustic",
                    "spec", "smooth", "orbreflect", "glow", "eyeglow")


def is_reflection(name: str) -> bool:
    """Whether this texture name is an overlay rather than the surface."""
    base = name.lower().replace("/", "\\").rsplit("\\", 1)[-1]
    return any(word in base for word in REFLECTION_WORDS)


def sha256_of(path) -> str:
    """The file's digest, read in chunks: some of these are megabytes."""
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def wmo_texture_names(path) -> list:
    """Texture names out of a WMO root's MOTX chunk.

    Group files (foo_000.wmo) carry no MOTX; only the root is worth reading.
    A file that does not parse returns nothing rather than raising - these are
    shipped files read by tools, and one bad archive member should not stop a
    sweep of ten thousand.
    """
    names = []
    try:
        data = Path(path).read_bytes()
    except OSError:
        return names
    off = 0
    while off + 8 <= len(data):
        magic = data[off:off + 4][::-1]
        size = int.from_bytes(data[off + 4:off + 8], "little")
        body = off + 8
        if body + size > len(data):
            break
        if magic == b"MOTX":
            names = [raw.decode("ascii", errors="replace")
                     for raw in data[body:body + size].split(b"\x00") if raw]
            break
        off = body + size
    return names
