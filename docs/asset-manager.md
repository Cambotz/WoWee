# The asset manager

`wowee_assets` builds the asset tree this client reads, out of a World of
Warcraft installation you own. It is a native program - SDL2 and Dear ImGui,
both of which the client already carries - and it calls the extractor in its own
process. Nothing is shelled out to, and it needs no interpreter of its own.

```
wowee_assets [game folder] [a later client] [where to put it]
```

All three are optional: the window takes the same two folders by drag and
drop, by typing, or through the system's folder chooser.

## What it asks

1. **Where is your game?** The folder, and where the assets should go. The
   archives say which game they came from, so it reads that off rather than
   asking. The destination defaults to the per-user data directory the client
   itself looks in - `~/Library/Application Support/Wowee/Data` on macOS,
   `%LOCALAPPDATA%\Wowee\Data` on Windows, `$XDG_DATA_HOME/wowee/Data` on Linux
   - so a build lands where the client will find it.

2. **Which game does your server run?** Vanilla, The Burning Crusade, Wrath,
   Cataclysm, or Turtle. Preselected to whatever the folder turned out to be.

3. **Do you want updated assets?** Optional, and each can be left off:

   | Upgrade | Needs | What it does |
   | --- | --- | --- |
   | Sharper foliage | nothing | Resamples every foliage texture into a sidecar beside the original. Reversible. |
   | Cataclysm's trees and doodads | a Cataclysm install | The re-authored foliage, where it sits at the same paths. Not offered for Cataclysm itself. |
   | Legion's creatures and doodads | a Legion install | Models converted to the format this client reads. |
   | Legion's player character models | a Legion install | Unproven on screen. |

4. **What will happen.** The steps, in the order they will run, and roughly how
   long. Models are imported before the foliage is sharpened, because the
   upscale resamples the textures of the models that are there.

## Several games in one folder

Each game is built under `expansions/<id>` in the destination, so building a
second one into the same folder adds to it rather than replacing it. The window
says which are already there, and the client offers the choice at its login
screen - an Assets row appears once more than one set is installed.

## Packs

**Save what I have as a pack** writes everything in the destination to one
`.zip`, with a `pack.json` describing it. With more than one game in there it is
named `universal`, because that is what it holds.

**Install a pack...** unpacks one back over the destination. Any entry naming a
path outside the folder it is installing into is refused: a zip entry's name is
chosen by whoever built the archive.

## Where models come from

The importer asks the other installation for each model already here, by that
model's own path. It does not sweep the installation matching on the name inside
each file - Legion's earth elemental calls itself `ElementalEarth2` and lives at
`elementalearth.m2`, so that match never lands. A model is taken only when it is
meaningfully bigger than the one already there, and it is resolved whole before
anything is written: skins, the animations an `AFID` chunk names, and every
texture it names for itself. A model whose textures cannot be resolved is
refused rather than written half-finished, because a model on disk that looks
complete and draws flat white is worse than one that is not there.

## Reporting what it looks like

`WOWEE_ASSETS_SCREENSHOT=<file> wowee_assets` writes a PNG of the window and
exits, for a bug report from a machine where taking a screenshot is the hard
part.
