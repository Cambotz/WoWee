# Upgraded assets

Borrowing art from a later World of Warcraft client you own, so the world this
client draws is the world a later one drew. Nothing here ships assets: every
file comes out of your own installation.

The worked example is Stranglethorn's canopy. The same tree, at the same path,
in two clients:

| | 3.3.5a | 4.3.4 |
|---|---|---|
| `StranglethornTree01.m2` | 156 vertices, 150 triangles | 897 vertices, 2430 triangles |
| canopy | crossed planes, one painted sheet | leaf cards, a leaf atlas |
| leaf texture | 256x256 | 512x512 |

Cataclysm re-authored the trees the classic zones still use and left them where
they were, so this is a file swap rather than a port.

## What can be borrowed, and what cannot

**Doodads, world models and creature models, from any MPQ-era client**: Classic,
TBC, WotLK, Cataclysm and Mists. Their models are `MD20`, their textures are
BLP, and `src/pipeline/m2_loader.cpp` reads both. This is the case that works.

**Nothing from Warlords onwards.** 6.x moved to CASC, which `asset_extract` does
not read - it is built on StormLib and StormLib is an MPQ library. Legion's
models are the chunked `MD21`, which nothing here parses.

**Player character models cannot be swapped, from any client.** This is worth
stating plainly because it is the most-asked-for case. The HD player models
introduced in Warlords carry a different geoset set - no 1, no 701, no 1501 -
and this client picks equipment geosets by number and composites a 512x512 body
texture from `CharSections.dbc`. Neither survives the swap; the result is a
naked player with segmented limbs. `tools/asset_pack_curate.py` has the
measurements. A character model the target has *never had* is a different
thing - the naga in a model pack are NPCs no player race wears - and is judged
on its textures like anything else.

**A conversion pack is not the same as a later client.** "WoW Legion Models for
WotLK 3.3.5a" and packs like it ship MPQ patch archives whose models have
already been converted: 548 of its 551 models are version 264, plain WotLK
MD20, and they load as they are. Point the extractor at the folder holding the
`Patch-*.MPQ` files with `--expansion wotlk` and it reads them like any other
patch chain. The CASC and MD21 limits above are about a *raw* Legion
installation, and they say nothing about what a converter has already done.

## Doing it

Three steps. The first two are once per client, the third per pack.

**1. Extract the art you want from the later client.** Point the extractor at
its `Data` directory. `--include` keeps this to seconds rather than unpacking a
whole client:

```sh
./build/bin/asset_extract \
    --mpq-dir /path/to/4.3.4/Data \
    --output /tmp/cata --expansion-subdir --skip-dbc \
    --include world/azeroth/stranglethorn
```

The expansion is detected from the archive set. 4.3.4 is `cata`; it extracts for
its art and cannot be played - see `docs/plan-cataclysm.md` for what that would
take.

**2. Build a pack from the extraction.** The pack is checked against the
expansion it is for, so it knows which of its files replace something and which
add something new:

```sh
python3 tools/asset_pack_from_client.py \
    --from /tmp/cata/expansions/cata \
    --against Data/expansions/wotlk \
    --include stranglethorn/passivedoodads/trees \
    --name cata-stranglethorn-trees
```

Each model is walked for what it draws - its skins, its animations, every
texture its header names, and for a `.wmo` its group files and their texture
list. A model is dropped only when the texture it is *drawn with* - its first slot,
and then only when that slot names its own art - is in neither the pack nor the
target. Everything else is kept: a pack of changed files is not a client, and
most of what its models draw is already in the expansion they are going over.
Checking the pack alone dropped 164 of 353 creature models whose "missing" art
was the base game's own. A reflection or a glow in that slot does not count
either, for reasons `asset_pack_curate.py` measured the hard way.

The result is a folder: `pack.json` beside a `Data/` tree. `pack.json` records
where it came from, what it was built against, and every file with its hash and
whether it replaces or adds.

**3. Install it.**

```sh
python3 tools/asset_pack_from_client.py \
    --install asset_packs/cata-stranglethorn-trees \
    --against ~/Library/Application\ Support/wowee/Data/expansions/wotlk
```

Or open `tools/asset_pipeline_gui.py`, add the folder on the Packs tab, order it
against any other packs and rebuild. Either way the files land in the
expansion's `override/` directory, which `AssetManager::resolveFile` reads
before the manifest. The extraction itself is never touched, and
`--uninstall` takes out exactly the files the pack listed.

## How it resolves, and why additions used to fail

`resolveFile` tries, in order:

1. `override/<the manifest's path for this file>` - a replacement
2. `override/<the path itself>` - an addition, which has no manifest entry
3. the expansion's own manifest
4. a loose file under the data path

Step 2 is the one that lets a pack add. Cataclysm's tree needs
`STRANGLETHORNTREE_LEAVES_SET_COLOR.BLP`, a name 3.3.5 never had, so there is no
manifest entry to hang an override on. Without step 2 the model was reachable
and its own texture was not, and it rendered white. `tests/test_override_add.cpp`
pins the order.

## What it looks like on a real pack

A model pack of 20,014 files, built against a 3.3.5a install:

| include | models | files | dropped |
|---|---|---|---|
| `creature/` | 353 | 3732 | 1 |
| `world/` | 164 | 496 | 4 |
| `character/` | 2 | 0 | 2 refused as replacements |

The four dropped world models are the real thing rather than a false positive:
`stormwindlionbanner.m2` is drawn with
`world/expansion06/doodads/stormwind/7sw_stormwind_funerarybanner.blp`, a Legion
texture the pack did not ship, and it would have rendered white.

## Two packs, one directory

Packs are laid down file by file rather than owning `override/`, so several can
live there. Where two carry the same path the later install wins, and the
installer says so. Removing one leaves the other intact.

## Handing it to something that is not WoWee

WoWee reads loose files and never opens a patch archive, so the override
directory is all it needs. `mpq_build` packs that directory into one:

```sh
./build/bin/mpq_build --input <expansion>/override --output Patch-W.MPQ
```

Names inside the archive are the paths relative to the input directory,
backslashed and lowercased, with a `(listfile)` written so the contents can be
found by something other than guesswork. MPQ v1 by default, which is what a
3.3.5a client reads.

That is for the cases a directory cannot serve: giving the same art to a client
that only reads archives, standing beside the `Patch-*.MPQ` files a server's
players already install, or keeping a whole overlay as one file with its paths
intact. `asset_extract` reads back what it writes, which is how to check one.

## What to expect

Placements are still your client's. A Cataclysm tree stands where the 3.3.5 ADT
put a tree, which for the same species at the same scale looks right, and for a
model the later client also resized may not. Zone lighting, terrain textures and
the doodads you did not swap are unchanged - swapping a zone's canopy and not its
undergrowth is visible, so take the whole of a thing rather than part of it.
