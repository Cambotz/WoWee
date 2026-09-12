#!/usr/bin/env python3
"""Bring skyboxes out of a CASC installation and into MD20 this client reads.

    tools/casc_skybox_import.py <legion-install> <output-dir> \
        --catalogue=m2names.txt [--name=SUBSTR]

The catalogue is the model list a sweep of the install produces: one line of
"fileDataId<TAB>version<TAB>name" per M2. CASC has no filenames, but an M2
carries its own, so that sweep is how a skybox is found at all.

A skybox is a dome with a few painted layers on it, and the ones this client
ships have not changed since Wrath - Cataclysm's copies are the same models
with the same textures, byte for byte but for the version field. What a later
client has that this one does not is *different* skies: Argus under a fel sun,
Suramar at night, the Broken Shore under a storm.

Those are M2 like any other, and for a skybox the format barely moved. Legion
wraps the model in an `MD21` chunk whose offsets are relative to the chunk, so
lifting the chunk out gives a standalone MD20; the header's arrays sit where
they sat in 264, field for field; and unlike most Legion models a skybox still
names its textures inline rather than by FileDataID. So the conversion is to
unwrap it, stamp the version this client expects, and fetch what it references.

Particle and ribbon emitters are the exception - those structs did change - so
a model carrying them is reported and skipped rather than written out broken.
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


def convert(storage, file_id, name, out_dir, fetched):
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
        # These two structs grew after Wrath. A dome that emits nothing is the
        # common case; one that does needs more than a version stamp.
        return "has emitters"

    patched = bytearray(body)
    struct.pack_into("<I", patched, 4, WOTLK_VERSION)
    write(out_dir, "environments\\stars\\%s.m2" % name, bytes(patched))

    # Skins moved out of the M2 into files of their own, named by the SFID
    # chunk. The loader still wants them beside the model as <name>0N.skin.
    if "SFID" in chunks:
        at, size = chunks["SFID"]
        for lod in range(size // 4):
            skin_id = struct.unpack_from("<I", blob, at + lod * 4)[0]
            skin = storage.read_fileid(skin_id)
            if skin:
                write(out_dir, "environments\\stars\\%s%02d.skin" % (name, lod), skin)

    missing = 0
    for tex in texture_paths(body):
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
    for flag in flags:
        if flag.startswith("--name="):
            want = flag.split("=", 1)[1].lower()
        elif flag.startswith("--catalogue="):
            catalogue = flag.split("=", 1)[1]

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
        if not any(token in low for token in ("skybox", "sky0", "_sky", "skydome")):
            continue
        if want and want not in low:
            continue
        done[name] = convert(storage, int(file_id), name, out_dir, fetched)
        print("  %-44s %s" % (name, done[name]), flush=True)

    ok = sum(1 for v in done.values() if v.startswith("ok"))
    print("%d skyboxes, %d written" % (len(done), ok))
    for name, status in sorted(done.items()):
        if not status.startswith("ok"):
            print("  %-40s %s" % (name, status))


if __name__ == "__main__":
    main()
