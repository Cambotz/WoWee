#!/usr/bin/env python3
"""The texture list an M2 carries, and where in the header to find it.

    tools/m2_textures.py

Two tools need to know which .blp files a model uses: the viewer, to draw one,
and the upscale pipeline, to treat a model's textures as a set rather than as
whatever the filenames happened to match. The header offsets are version-gated
- vanilla and TBC carry a playable-animation lookup that WotLK dropped, so
every array after it sits eight bytes later - and a table of those in two
places is a table that goes wrong in one of them.

A texture entry carries its type (0 is a filename on disk; anything else is
supplied by the client, a skin or a hair colour), its wrap flags (1 = wrap in
x, 2 = wrap in y) and, for type 0, the filename.
"""

import struct

# Header field → byte offset. Same key spelling for both, so a caller asks for
# a field and not for a version.
_WOTLK = {
    "nGlobalSeq": 20, "ofsGlobalSeq": 24,
    "nAnims": 28, "ofsAnims": 32,
    "nBones": 44, "ofsBones": 48,
    "nVerts": 60, "ofsVerts": 64,
    "nTextures": 80, "ofsTextures": 84,
    "nTextureLookup": 128, "ofsTextureLookup": 132,
    "nBoneLookup": 120, "ofsBoneLookup": 124,
}
_VANILLA = {
    "nGlobalSeq": 20, "ofsGlobalSeq": 24,
    "nAnims": 28, "ofsAnims": 32,
    "nBones": 52, "ofsBones": 56,
    "nVerts": 68, "ofsVerts": 72,
    "nTextures": 92, "ofsTextures": 96,
    "nTextureLookup": 148, "ofsTextureLookup": 152,
    "nBoneLookup": 140, "ofsBoneLookup": 144,
}

TEXTURE_WRAP_X = 1
TEXTURE_WRAP_Y = 2


def header_offset(version: int, field: str) -> int:
    """Byte offset of a header field for a model of this version."""
    return (_VANILLA if version <= 256 else _WOTLK)[field]


def m2_array(data: bytes, version: int, field: str):
    """(count, offset) of an M2Array header field. Field is spelled bare -
    "Textures", not "nTextures"."""
    n = struct.unpack_from("<I", data, header_offset(version, f"n{field}"))[0]
    o = struct.unpack_from("<I", data, header_offset(version, f"ofs{field}"))[0]
    return n, o


def texture_entries(data: bytes) -> list:
    """Every texture the model declares, in declaration order.

    Returns dicts of {type, flags, filename}; filename is "" for a texture the
    client supplies rather than one on disk. A header that does not add up
    returns what it could read rather than raising: these are shipped files
    read by tools, and one bad model should not stop a sweep of ten thousand.
    """
    if len(data) < 16 or data[:4] != b"MD20":
        return []
    version = struct.unpack_from("<I", data, 4)[0]
    try:
        n, ofs = m2_array(data, version, "Textures")
    except (struct.error, KeyError):
        return []
    if n == 0 or n > 1000 or ofs + n * 16 > len(data):
        return []

    out = []
    for i in range(n):
        base = ofs + i * 16
        tex_type, tex_flags = struct.unpack_from("<II", data, base)
        name_len, name_ofs = struct.unpack_from("<II", data, base + 8)
        filename = ""
        if tex_type == 0 and name_len > 1 and name_ofs + name_len <= len(data):
            raw = data[name_ofs:name_ofs + name_len]
            filename = raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")
        out.append({"type": tex_type, "flags": tex_flags, "filename": filename})
    return out
