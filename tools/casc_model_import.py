#!/usr/bin/env python3
"""Bring models out of a CASC installation and into MD20 this client reads.

    tools/casc_model_import.py <legion-install> <output-dir> \
        --catalogue=m2names.txt --name=SUBSTR
    tools/casc_model_import.py <legion-install> <output-dir> \
        --catalogue=m2names.txt --list=wanted.txt

The list is one model per line, and where a line carries a tab the second
field is where to write it - "stranglethorntree01<TAB>world/azeroth/...". A
pack has to put a model at the path the client already looks for it at, and
CASC does not know that path: it is the local installation that knows, so the
placement comes in with the list rather than out of the archive.

The catalogue is the model list a sweep of the install produces: one line of
"fileDataId<TAB>version<TAB>name" per M2. CASC has no filenames, but an M2
carries its own, so that sweep is how a skybox is found at all.

Most art is not re-authored between expansions - of 22033 models Cataclysm
shares with 3.3.5, 20992 have the identical vertex count - but some is, and a
sweep finds it: 241 models Legion draws with more geometry than the originals
here, Teldrassil's canopy at 7x and Tirisfal's graves at 5x among them.

For a model that already existed in Wrath the format barely moved. Legion
wraps the model in an `MD21` chunk whose offsets are relative to the chunk, so
lifting the chunk out gives a standalone MD20; the header's arrays sit where
they sat in 264, field for field; and it still names its textures inline. Only
models authored after Wrath reference textures by FileDataID through a `TXID`
chunk, and those need a listfile this does not have. So the conversion is to
unwrap, stamp the version this client expects, and fetch what it references.

Two exceptions are reported and skipped rather than written out broken:
particle and ribbon emitters, whose structs did change, and the handful of
models - creatures and armour, which compose their skins - that name some
texture by id. Of those 241 improved models, 165 convert as they are.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import casc_extract as ce


WOTLK_VERSION = 264
NAME_OFFSET, NAME_COUNT = 12, 8
TEXTURES_COUNT, TEXTURES_OFFSET = 80, 84
RIBBONS_COUNT, PARTICLES_COUNT = 288, 296


def md21_body(blob):
    """The MD20 inside a Legion M2, or None if it is not chunked."""
    if blob[:4] == b"MD20":
        return blob, {}
    if blob[:4] != b"MD21":
        return None, {}
    chunks = {}
    pos = 0
    while pos + 8 <= len(blob):
        tag = blob[pos:pos + 4]
        size = struct.unpack_from("<I", blob, pos + 4)[0]
        chunks[tag.decode("latin-1")] = (pos + 8, size)
        pos += 8 + size
        if size == 0:
            break
    start, size = chunks["MD21"]
    return blob[start:start + size], chunks


def texture_paths(body):
    count = struct.unpack_from("<I", body, TEXTURES_COUNT)[0]
    offset = struct.unpack_from("<I", body, TEXTURES_OFFSET)[0]
    out = []
    for i in range(count):
        entry = offset + i * 16
        _type, _flags, length, at = struct.unpack_from("<4I", body, entry)
        if not length:
            continue
        name = body[at:at + length].split(b"\0")[0].decode("latin-1")
        if name:
            out.append(name)
    return out


def write(out_dir, rel_path, blob):
    path = os.path.join(out_dir, rel_path.replace("\\", os.sep))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(blob)
    return path


def convert(storage, file_id, name, out_dir, fetched, dest=None):
    """One skybox, its skins and its textures. Returns a short status.

    `fetched` carries the texture paths already written. Skies share their
    star fields and galaxies heavily - galaxy_01 alone is referenced by dozens
    of domes - and each one is a full decode of a file several megabytes wide.
    """
    blob = storage.read_fileid(file_id)
    if not blob:
        return "missing"
    body, chunks = md21_body(blob)
    if body is None:
        return "not an M2"

    if struct.unpack_from("<I", body, RIBBONS_COUNT)[0] or \
       struct.unpack_from("<I", body, PARTICLES_COUNT)[0]:
        # These two structs grew after Wrath. A model that emits nothing is the
        # common case; one that does needs more than a version stamp.
        return "has emitters"

    # Both refusals come before anything is written. A model left half-written
    # is worse than one not written at all: it is on disk, it looks complete,
    # and it renders white where the texture it wanted never arrived.
    named = texture_paths(body)
    declared = struct.unpack_from("<I", body, TEXTURES_COUNT)[0]
    if len(named) < declared:
        # A creature or a piece of armour, composing its skin from ids this
        # has no listfile to resolve.
        return "%d textures by id" % (declared - len(named))

    patched = bytearray(body)
    struct.pack_into("<I", patched, 4, WOTLK_VERSION)
    model_path = dest or ("environments\\stars\\%s.m2" % name)
    write(out_dir, model_path, bytes(patched))
    beside = model_path.replace("/", "\\").rsplit("\\", 1)[0]

    # Skins moved out of the M2 into files of their own, named by the SFID
    # chunk. The loader still wants them beside the model as <name>0N.skin.
    if "SFID" in chunks:
        at, size = chunks["SFID"]
        for lod in range(size // 4):
            skin_id = struct.unpack_from("<I", blob, at + lod * 4)[0]
            skin = storage.read_fileid(skin_id)
            if skin:
                write(out_dir, "%s\\%s%02d.skin" % (beside, name, lod), skin)

    missing = 0
    for tex in named:
        key = tex.lower()
        if key in fetched:
            continue
        data = storage.read_path(tex)
        fetched.add(key)
        if data:
            write(out_dir, tex, data)
        else:
            missing += 1
    return "ok" if not missing else "ok, %d textures missing" % missing


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    if len(args) < 2:
        raise SystemExit(__doc__)
    install, out_dir = args[0], args[1]
    want = None
    catalogue = None
    wanted_names = None
    for flag in flags:
        if flag.startswith("--name="):
            want = flag.split("=", 1)[1].lower()
        elif flag.startswith("--catalogue="):
            catalogue = flag.split("=", 1)[1]
        elif flag.startswith("--list="):
            wanted_names = {}
            for line in open(flag.split("=", 1)[1]):
                line = line.rstrip("\n")
                if not line.strip():
                    continue
                parts = line.split("\t")
                wanted_names[parts[0].strip().lower()] = \
                    parts[1].strip() if len(parts) > 1 else None
    if want is None and wanted_names is None:
        raise SystemExit("pass --name=<substring> or --list=<file of model names>")

    storage = ce.CascStorage(install)
    print("index %d, encoding %d, root %d" %
          (len(storage.index.entries), len(storage.encoding), len(storage.root)))

    if not catalogue or not os.path.exists(catalogue):
        raise SystemExit("pass --catalogue=<model list>; see the module docstring")

    done = {}
    fetched = set()
    for line in open(catalogue):
        file_id, _version, name = line.rstrip("\n").split("\t")
        low = name.lower()
        if wanted_names is not None and low not in wanted_names:
            continue
        if want is not None and want not in low:
            continue
        dest = wanted_names.get(low) if wanted_names else None
        done[name] = convert(storage, int(file_id), name, out_dir, fetched, dest)
        print("  %-44s %s" % (name, done[name]), flush=True)

    ok = sum(1 for v in done.values() if v.startswith("ok"))
    print("%d selected, %d written" % (len(done), ok))
    for name, status in sorted(done.items()):
        if not status.startswith("ok"):
            print("  %-40s %s" % (name, status))


if __name__ == "__main__":
    main()
