#!/usr/bin/env python3
"""Build an asset pack out of a later client's art.

    tools/asset_pack_from_client.py --from <extraction> --against <expansion data>
                                    --include <path fragment> --name <pack name>

Cataclysm and Mists re-authored models the classic zones still use, at the same
paths: Stranglethorn's canopy is 156 vertices of crossed planes in 3.3.5 and 897
vertices of leaf cards in 4.3.4, over a 512-square leaf atlas rather than a
256-square painted blob. A player who owns one of those clients can extract it
(`asset_extract --expansion cata --include world/azeroth/stranglethorn`) and
have this build a pack from it.

The output is an ordinary pack folder - `pack.json` beside a `Data/` tree of
game-relative paths - which is what tools/asset_pipeline_gui.py installs,
orders and merges into the override directory. Nothing here writes into a game
install; the pack is a folder, and activating it is a separate, reversible act.

WHAT IT TAKES, AND WHAT IT LEAVES

A model is only worth taking if everything it draws comes with it, so each one
is walked for its dependencies - every .skin beside it, every texture its
header names, and for a .wmo its group files and their MOTX list. A model whose
texture is missing from the source is dropped rather than shipped, because a
model with no base texture renders white rather than failing.

Character models are refused outright, whatever the include says. The HD player
models carry a different geoset set and a differently composited body texture,
and this client has no compositor for them - tools/asset_pack_curate.py has the
measurements. That is also the answer to "can we take Legion's faces": beyond
being CASC and chunked MD21, which nothing here reads, they are the one class
of model a swap cannot work for.

Every file is classified as it is taken:

  replace   the target expansion has this path, and the pack supersedes it
  add       the target has never had this path, and the pack introduces it

Both work through the override directory. Both are listed with their hashes in
pack.json, so what a pack did is a question with an answer.
"""

import argparse
import hashlib
import json
import shutil
import struct
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import m2_textures
from asset_files import sha256_of, wmo_texture_names

REPO_ROOT = Path(__file__).resolve().parent.parent

# Paths a pack must not carry, whatever was asked for. The first is measured in
# asset_pack_curate.py; the rest are the client's own furniture rather than art.
REFUSED_PREFIXES = ("character/", "dbfilesclient/", "interface/")


def norm(rel: str) -> str:
    """A path in the shape the asset manifest keys it: lowercase, backslashes."""
    return str(rel).replace("/", "\\").lower()


def slashed(rel: str) -> str:
    return str(rel).replace("\\", "/").lower()


def dependencies(model: Path, source_root: Path):
    """(files the model needs, texture paths it names) - relative, slashed.

    Skins and group files travel with the model; textures are returned
    separately because whether they are missing decides if the model ships.
    """
    rel = slashed(model.relative_to(source_root))
    files = [rel]
    textures = []

    if model.suffix.lower() == ".m2":
        for skin in sorted(model.parent.glob(model.stem + "*.skin")):
            if not skin.name.startswith("._"):
                files.append(slashed(skin.relative_to(source_root)))
        for anim in sorted(model.parent.glob(model.stem + "*.anim")):
            if not anim.name.startswith("._"):
                files.append(slashed(anim.relative_to(source_root)))
        for entry in m2_textures.texture_entries(model.read_bytes()):
            if entry["type"] == 0 and entry["filename"]:
                textures.append(slashed(entry["filename"]))
    else:  # .wmo root, with its numbered group files
        stem = model.stem
        for group in sorted(model.parent.glob(stem + "_[0-9][0-9][0-9].wmo")):
            if not group.name.startswith("._"):
                files.append(slashed(group.relative_to(source_root)))
        textures = [slashed(name) for name in wmo_texture_names(model)]

    return files, textures


def load_target_paths(target: Path):
    """Every path the target expansion already has, manifest-keyed."""
    manifest = target / "manifest.json"
    if not manifest.is_file():
        sys.exit(f"No manifest.json under {target} - point --against at an extracted expansion")
    entries = json.loads(manifest.read_text()).get("entries", {})
    return set(entries)


def source_build(source_root: Path) -> dict:
    """Whatever the source extraction can say about itself."""
    info = {}
    for name in ("expansion.json", "manifest.json"):
        path = source_root / name
        if path.is_file():
            try:
                blob = json.loads(path.read_text())
            except (json.JSONDecodeError, OSError):
                continue
            if name == "expansion.json":
                info["expansion"] = blob.get("id")
                info["build"] = blob.get("build")
            else:
                info["source_file_count"] = blob.get("fileCount")
    info.setdefault("expansion", source_root.name)
    return info


def install_pack(pack_dir: Path, target: Path, remove: bool) -> int:
    """Lay a pack into the target's override directory, or take it back out.

    The override directory is read before the expansion's own manifest and is
    not part of the extraction, so putting a pack there changes nothing that was
    extracted and taking it out leaves no trace. Only files this pack lists are
    touched, so two packs can live there without either of them owning the
    directory.
    """
    manifest_path = pack_dir / "pack.json"
    if not manifest_path.is_file():
        sys.exit(f"No pack.json in {pack_dir}")
    pack = json.loads(manifest_path.read_text())
    override = target / "override"
    entries = pack.get("entries", {})

    if remove:
        removed = 0
        for rel in sorted(entries):
            path = override / rel
            if path.is_file():
                path.unlink()
                removed += 1
        # Tidy the directories the pack made, deepest first, leaving any that
        # another pack still has files in.
        for path in sorted(override.rglob("*"), key=lambda p: -len(p.parts)):
            if path.is_dir() and not any(path.iterdir()):
                path.rmdir()
        print(f"Removed {removed} of {len(entries)} file(s) from {override}")
        return 0

    data_root = pack_dir / "Data"
    written = 0
    clashes = []
    for rel in sorted(entries):
        src = data_root / rel
        if not src.is_file():
            print(f"  missing from the pack: {rel}", file=sys.stderr)
            continue
        dst = override / rel
        if dst.is_file() and sha256_of(dst) != entries[rel]["sha256"]:
            clashes.append(rel)
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)
        written += 1
    print(f"Installed {written} file(s) into {override}")
    if clashes:
        print(f"{len(clashes)} file(s) were already there from another pack and have "
              f"been overwritten; the pack that wrote them first is now partly shadowed.")
    print("Restart the client to see it. Undo with --uninstall.")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--from", dest="source", type=Path, default=Path("."),
                    help="a later client's extraction, e.g. .../expansions/cata")
    ap.add_argument("--against", type=Path, required=True,
                    help="the expansion the pack is for, e.g. .../expansions/wotlk")
    ap.add_argument("--include", action="append", default=[],
                    help="path fragment to take models from (repeatable)")
    ap.add_argument("--name", help="pack name, used for the folder")
    ap.add_argument("--out", type=Path, default=REPO_ROOT / "asset_packs",
                    help="where to write the pack folder (default: ./asset_packs)")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--install", type=Path, metavar="PACK",
                    help="lay a built pack into <against>/override/ and stop")
    ap.add_argument("--uninstall", type=Path, metavar="PACK",
                    help="take a built pack back out of <against>/override/ and stop")
    args = ap.parse_args()

    if args.install or args.uninstall:
        return install_pack(args.install or args.uninstall, args.against.resolve(),
                            remove=bool(args.uninstall))
    if not args.include or not args.name:
        ap.error("--include and --name are required when building a pack")

    source = args.source.resolve()
    target = args.against.resolve()
    if not source.is_dir():
        sys.exit(f"Source extraction not found: {source}")
    known = load_target_paths(target)
    fragments = [slashed(f) for f in args.include]

    models = []
    for pattern in ("*.m2", "*.wmo"):
        for model in sorted(source.rglob(pattern)):
            if model.name.startswith("._"):
                continue
            rel = slashed(model.relative_to(source))
            if not any(f in rel for f in fragments):
                continue
            if any(rel.startswith(p) for p in REFUSED_PREFIXES):
                continue
            # A WMO group file travels with its root, never on its own.
            if model.suffix.lower() == ".wmo" and len(model.stem) > 4 \
                    and model.stem[-4] == "_" and model.stem[-3:].isdigit():
                continue
            models.append(model)

    take = {}          # relative path -> source file
    dropped = []
    for model in models:
        files, textures = dependencies(model, source)
        missing = [t for t in textures if not (source / t).is_file()]
        if missing:
            dropped.append((slashed(model.relative_to(source)), missing))
            continue
        for rel in files + textures:
            take[rel] = source / rel

    refused = [rel for rel in take if any(rel.startswith(p) for p in REFUSED_PREFIXES)]
    for rel in refused:
        del take[rel]

    replace = sorted(rel for rel in take if norm(rel) in known)
    add = sorted(rel for rel in take if norm(rel) not in known)

    print(f"{len(models)} model(s) matched, {len(take)} file(s) to pack")
    print(f"  {len(replace)} replace an existing path, {len(add)} add a new one")
    if dropped:
        print(f"  {len(dropped)} model(s) dropped - a texture they draw is not in the source:")
        for rel, missing in dropped[:5]:
            print(f"     {rel}  (missing {missing[0]})")
    if refused:
        print(f"  {len(refused)} file(s) refused as character art or client furniture")
    if args.dry_run:
        for rel in replace[:10]:
            print("   replace", rel)
        for rel in add[:10]:
            print("   add    ", rel)
        return 0
    if not take:
        print("Nothing to pack.")
        return 1

    pack_dir = args.out / args.name
    data_dir = pack_dir / "Data"
    if pack_dir.exists():
        shutil.rmtree(pack_dir)
    data_dir.mkdir(parents=True)

    entries = {}
    for rel, src in sorted(take.items()):
        dst = data_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)
        entries[rel] = {
            "kind": "replace" if norm(rel) in known else "add",
            "sha256": sha256_of(dst),
            "bytes": dst.stat().st_size,
        }

    (pack_dir / "pack.json").write_text(json.dumps({
        "name": args.name,
        "built_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "built_by": "tools/asset_pack_from_client.py",
        "source": source_build(source),
        "for_expansion": target.name,
        "include": fragments,
        "counts": {"models": len(models), "files": len(entries),
                   "replace": len(replace), "add": len(add),
                   "dropped_models": len(dropped)},
        "entries": entries,
    }, indent=1, sort_keys=True) + "\n")

    total = sum(e["bytes"] for e in entries.values())
    print(f"\nWrote {pack_dir} - {len(entries)} file(s), {total / 1048576:.1f} MB")
    print("Install it with tools/asset_pipeline_gui.py (Packs tab), or copy its Data/ "
          "tree into\n<expansion>/override/ by hand. Both are reversible; neither "
          "touches the extracted assets.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
