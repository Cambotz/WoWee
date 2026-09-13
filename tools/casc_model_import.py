#!/usr/bin/env python3
"""Bring models out of a CASC installation and into MD20 this client reads.

    tools/casc_model_import.py <casc-install> <output-dir> \
        --catalogue=m2names.txt --local=<3.3.5 install> \
        --prefix=world/azeroth/elwynn --better

The local installation is what says where a model goes and what it is being
improved on. CASC knows a FileDataID and not a path, so a pack cannot be
written without it: the name inside the model is matched against the local
tree, and the file lands where its counterpart already lives.

Given that, the selection can be made rather than curated. `--prefix` takes
everything under a path, `--better` keeps only what the later client draws
with more geometry than the local copy, and the two compose - "every model in
Elwynn worth replacing" is one command.

The catalogue is the model list a sweep of the install produces: one line of
"fileDataId<TAB>version<TAB>name" per M2, written by
`casc_extract.py --sweep`. CASC has no filenames, but an M2 carries its own,
so that sweep is what makes any of this addressable.

Most art is not re-authored between expansions - of 22033 models Cataclysm
shares with 3.3.5, 20992 have the identical vertex count - which is why
`--better` is worth having: pointed at a whole zone it takes the handful that
changed and leaves the rest, and moving a model that did not change is pure
cost.

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
    """The texture paths a model names, and how many slots of its own name none.

    A slot's type says who is meant to fill it. Type 0 is the model naming its
    own file. Types 11 to 13 are a creature's skin, which the client composes
    from CreatureDisplayInfo at runtime and which is empty here by design - a
    rock elemental's body texture is chosen by the display, not by the model.

    So an empty name means opposite things in the two cases, and the second
    number is the one that matters: a type 0 slot that names no file is a model
    that promised a texture and did not name it, which the client draws flat
    white.
    """
    count = struct.unpack_from("<I", body, TEXTURES_COUNT)[0]
    offset = struct.unpack_from("<I", body, TEXTURES_OFFSET)[0]
    out = []
    unnamed_own = 0
    for i in range(count):
        entry = offset + i * 16
        kind, _flags, length, at = struct.unpack_from("<4I", body, entry)
        name = body[at:at + length].split(b"\0")[0].decode("latin-1") if length else ""
        if name:
            out.append(name)
        elif kind == 0:
            unnamed_own += 1
    return out, unnamed_own


def write(out_dir, rel_path, blob):
    path = os.path.join(out_dir, rel_path.replace("\\", os.sep))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(blob)
    return path


def convert(storage, file_id, name, out_dir, fetched, failed, dest=None):
    """One skybox, its skins and its textures. Returns a short status.

    `fetched` carries the texture paths already written and `failed` those
    already looked for and not found. Skies share their star fields and
    galaxies heavily - galaxy_01 alone is referenced by dozens of domes - and
    each one is a full decode of a file several megabytes wide, so the answer
    is remembered either way.

    The two sets used to be one, which recorded every path attempted whether
    or not it arrived. The second model to name a texture that had already
    failed skipped the fetch, found nothing missing and was written anyway.
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

    # Every refusal comes before anything is written. A model left half-written
    # is worse than one not written at all: it is on disk, it looks complete,
    # and it renders white where the texture it wanted never arrived.
    #
    # The test used to be that every declared slot carried a name, which asks
    # the wrong thing of a creature: its skin slots are empty by design and
    # the client fills them from CreatureDisplayInfo. Read that way, no
    # creature could ever be imported - and the ones that can be were, before
    # this test existed, along with five that could not.
    #
    # What those five have in common is a slot of type 0 with no name, which
    # is a model naming its own texture by FileDataID through a TXID chunk
    # this does not read. The client has no way to fill such a slot and draws
    # white. ElementalEarth is the case: three of its seven slots, and the two
    # its additive dust shells sample, so they fell back to the creature's own
    # rock skin and drew as white sheets over it.
    named, unnamed_own = texture_paths(body)
    if unnamed_own:
        by_id = " (TXID)" if "TXID" in chunks else ""
        return "%d texture slots name no file%s" % (unnamed_own, by_id)

    # Every texture is resolved before anything at all is written.
    #
    # This ran the other way round: the model went to disk, then its skins,
    # then the textures were fetched and whatever had not arrived was reported
    # with the model already in the install. That is the half-written model the
    # refusals above exist to prevent, and it is how five creatures reached an
    # install with texture slots nothing fills - an elemental whose additive
    # dust shells fell back to sampling its own rock skin and drew as white
    # sheets, and a larva and a compy with no named texture at all.
    #
    # A texture that did not come across is named, not counted. This returned
    # "ok, 2 textures missing" and the caller printed it beside a hundred other
    # lines, so three models went into an install referring to
    # World\Expansion05 textures that were never converted - a gryphon roost, a
    # wyvern roost and a horde banner, found later by the client logging the
    # same four paths every run. A count cannot be chased; a path can.
    pending = []
    missing = []
    for tex in named:
        key = tex.lower()
        if key in fetched:
            continue
        if key in failed:
            missing.append(tex)
            continue
        data = storage.read_path(tex)
        if data:
            pending.append((tex, data))
        else:
            failed.add(key)
            missing.append(tex)
    if missing:
        for tex in missing:
            sys.stderr.write("  texture not in this install: %s  (%s)\n" % (tex, name))
        return "%d textures MISSING, not written" % len(missing)

    patched = bytearray(body)
    struct.pack_into("<I", patched, 4, WOTLK_VERSION)
    model_path = dest or ("environments\\stars\\%s.m2" % name)
    write(out_dir, model_path, bytes(patched))
    # The skin is named after the model file, not after the name inside the
    # model. The client works the skin path out from the model path, and
    # anything else walking a pack matches them by filename stem - a stem that
    # differs even in case is a model with no index data, which draws as
    # spikes. Legion's internal name is CamelCase and a 3.3.5 path is often
    # not.
    written_dir, _, written_file = model_path.replace("/", "\\").rpartition("\\")
    stem = written_file[:-3] if written_file.lower().endswith(".m2") else written_file

    # Skins moved out of the M2 into files of their own, named by the SFID
    # chunk. The loader still wants them beside the model as <name>0N.skin.
    if "SFID" in chunks:
        at, size = chunks["SFID"]
        for lod in range(size // 4):
            skin_id = struct.unpack_from("<I", blob, at + lod * 4)[0]
            skin = storage.read_fileid(skin_id)
            if skin:
                write(out_dir, "%s\\%s%02d.skin" % (written_dir, stem, lod), skin)

    for tex, data in pending:
        write(out_dir, tex, data)
        fetched.add(tex.lower())
    return "ok"


def index_local(root):
    """Every model in the local installation: name -> (vertex count, path).

    This is the baseline `--better` compares against and the placement every
    written file needs.

    `override/` counts, and counts first, because that is the order
    AssetManager::resolveFile reads in: a pack already installed there is what
    the client draws, so it is what a candidate has to beat. Measuring against
    the untouched 3.3.5 file instead says every model in an installed pack is
    an improvement worth making again - and worse, will happily replace a
    better model with a poorer one. Elwynn's lion statue is 205 vertices as
    shipped, 980 in a pack already installed, and 919 in a raw Legion install:
    taking it "because it beats 205" loses 61.

    The path recorded is always the game-relative one, never the override
    copy's, because that is where a new pack has to put its file.
    """
    out = {}
    for dirpath, _dirs, files in os.walk(root):
        rel_dir = os.path.relpath(dirpath, root)
        parts = rel_dir.lower().split(os.sep)
        overridden = parts[0] == "override"
        for name in files:
            if not name.lower().endswith(".m2") or name.startswith("._"):
                continue
            path = os.path.join(dirpath, name)
            try:
                head = open(path, "rb").read(200)
            except OSError:
                continue
            if head[:4] != b"MD20":
                continue
            version = struct.unpack_from("<I", head, 4)[0]
            # Vanilla and TBC carry a playable-animation lookup WotLK dropped,
            # so every array after it sits eight bytes later.
            shift = 0 if version >= 264 else 8
            try:
                verts = struct.unpack_from("<I", head, 60 + shift)[0]
            except struct.error:
                continue
            key = name[:-3].lower()
            rel = os.path.relpath(path, root)
            if overridden:
                rel = os.sep.join(rel.split(os.sep)[1:])
            previous = out.get(key)
            if previous is None:
                out[key] = (verts, rel, overridden)
            elif overridden and not previous[2]:
                # An override replaces the shipped file whatever its size.
                out[key] = (verts, rel, True)
            elif overridden == previous[2] and verts > previous[0]:
                out[key] = (verts, rel, overridden)
    return {k: (v[0], v[1]) for k, v in out.items()}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = dict()
    for flag in sys.argv[1:]:
        if flag.startswith("--"):
            key, _, value = flag[2:].partition("=")
            flags[key] = value
    if len(args) < 2:
        raise SystemExit(__doc__)
    install, out_dir = args[0], args[1]

    catalogue = flags.get("catalogue")
    if not catalogue or not os.path.exists(catalogue):
        raise SystemExit("pass --catalogue=<model list from casc_extract --sweep>")
    local_root = flags.get("local")
    if not local_root or not os.path.isdir(local_root):
        raise SystemExit("pass --local=<the installation being improved>")

    prefix = (flags.get("prefix") or "").lower().replace("\\", "/").strip("/")
    substring = (flags.get("name") or "").lower() or None
    ratio = float(flags.get("better") or 1.3) if "better" in flags else 0.0
    if not prefix and not substring and not ratio:
        raise SystemExit("pass at least one of --prefix, --name or --better")

    local = index_local(local_root)
    print("local models: %d" % len(local))

    storage = ce.CascStorage(install)
    print("casc: index %d, encoding %d, root %d" %
          (len(storage.index.entries), len(storage.encoding), len(storage.root)))

    done = {}
    fetched = set()
    failed = set()
    skipped_unknown = 0
    for line in open(catalogue):
        file_id, _version, name = line.rstrip("\n").split("\t")
        low = name.lower()
        if substring and substring not in low:
            continue
        here = local.get(low)
        if here is None:
            # Nothing to improve and nowhere to put it. A model the local
            # client never had is new content, not an upgrade.
            skipped_unknown += 1
            continue
        verts, rel = here
        if prefix and not rel.lower().replace(os.sep, "/").startswith(prefix):
            continue
        if ratio:
            try:
                blob = storage.read_fileid(int(file_id), limit=2048)
                body, _chunks = md21_body(blob) if blob else (None, {})
                theirs = struct.unpack_from("<I", body, 60)[0] if body else 0
            except Exception:
                continue
            if verts < 20 or theirs <= verts * ratio:
                continue
        done[name] = convert(storage, int(file_id), name, out_dir, fetched, failed,
                             rel.replace(os.sep, "\\"))
        print("  %-44s %s" % (name, done[name]), flush=True)

    ok = sum(1 for v in done.values() if v.startswith("ok"))
    print("%d selected, %d written (%d models the local client does not have)"
          % (len(done), ok, skipped_unknown))
    for name, status in sorted(done.items()):
        if not status.startswith("ok"):
            print("  %-40s %s" % (name, status))


if __name__ == "__main__":
    main()
