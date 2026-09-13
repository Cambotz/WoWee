#!/usr/bin/env python3
"""The asset sets this client can be given, and what each one needs.

    from asset_profiles import PROFILES, availability

A profile is a named answer to "what do you want the game to look like". It
names one base extraction and any number of enrichments on top, and every step
says which source it needs - the game you are extracting from, a later client
you also own, or nothing at all.

This exists so the question is asked once and answered in one place. The GUI
offers these, the shell script takes them by name, and neither holds its own
idea of what "wotlk with upscaled foliage" means. Adding a profile here adds it
to both.

WHAT A SOURCE IS

  mpq      a pre-Warlords installation, read with StormLib: classic through
           Mists. This is the game being extracted.
  mpq2     a SECOND pre-Warlords installation, to borrow art from. Distinct
           from mpq because the game you are extracting is not a second client:
           without this distinction "Wrath with Cataclysm trees" looks buildable
           to somebody who owns only Wrath.
  casc     Warlords onward, read with tools/casc_extract.py. Legion is the one
           measured here; it supplies models, not a whole client, because not
           one of its 245 DBC tables exists in a form this client reads and its
           terrain is post-Cataclysm geography besides.
  none     something the client can do to art it already has.

WHAT IS AND IS NOT KNOWN TO WORK

Creature, world and item models from a later client are proven: 784 of them
are in a working installation as this is written. Player character models are
not refused here, because the reasons they were refused turn out not to hold -
Legion's HumanMale carries the same geoset groups as 3.3.5's, has the 1 and 701
the refusal said it lacked, is 7% more vertices rather than three times, and
its bone indices fit inside this client's 127-index vertex format. What is
genuinely unsettled is whether its body UVs still match the skins CharSections
names, which is a question for a screenshot rather than for a file. So it is
offered, marked unproven, and it is reversible like every other pack.
"""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass(frozen=True)
class Step:
    """One thing done to the assets, and what it needs to do it."""
    kind: str            # extract | upscale | import_models
    source: str          # mpq | casc | none
    summary: str         # what the player gets
    detail: str          # why, and what it costs
    args: dict = field(default_factory=dict)


@dataclass(frozen=True)
class Profile:
    profile_id: str
    name: str
    summary: str
    detail: str
    expansion: str       # the base game this is built from
    steps: tuple[Step, ...] = ()

    @property
    def sources(self) -> set[str]:
        return {s.source for s in self.steps if s.source != "none"}

    @property
    def minutes(self) -> int:
        """Roughly how long, so a choice is not made blind."""
        total = 0
        for step in self.steps:
            total += {"extract": 8, "upscale": 25, "import_models": 6}.get(step.kind, 2)
        return total


def _extract(expansion: str) -> Step:
    return Step(
        kind="extract", source="mpq",
        summary="Extract the game you own",
        detail="Reads the MPQ archives and writes a tree of loose files under "
               "Data/expansions/%s. Nothing is written into the game install." % expansion,
        args={"expansion": expansion},
    )


_UPSCALE = Step(
    kind="upscale", source="none",
    summary="Upscale the foliage four times",
    detail="Every texture of every foliage model, upscaled into DXT5 sidecars beside "
           "the originals. Selection is by model rather than by filename, so a trunk "
           "sheet comes along with the tree that uses it. A 1024-square sheet costs "
           "1.3 MB of video memory against 5.3 MB as raw pixels. Reversible: the "
           "sidecars can be deleted and the originals are untouched.",
)

_CATA_TREES = Step(
    kind="import_models", source="mpq2",
    summary="Cataclysm's trees, over Wrath's",
    detail="Cataclysm re-authored the trees the classic zones still use and left them "
           "at the same paths. Stranglethorn's canopy is 156 vertices of crossed planes "
           "in 3.3.5 and 897 vertices of leaf cards in 4.3.4. Needs a Cataclysm "
           "installation you own.",
    args={"expansion": "cata", "include": "world"},
)

_LEGION_CREATURES = Step(
    kind="import_models", source="casc",
    summary="Legion's creatures and doodads",
    detail="Creature, world and item models from a Legion installation, converted to "
           "the format this client reads. Proven: 784 of them run in a working install. "
           "A model whose textures cannot be resolved is refused rather than written "
           "half-finished.",
    args={"prefixes": ("creature", "world", "item")},
)

_LEGION_CHARACTERS = Step(
    kind="import_models", source="casc",
    summary="Legion's player character models (unproven)",
    detail="The reasons these were refused do not survive measurement: Legion's "
           "HumanMale has the same geoset groups as 3.3.5's, has the 1 and 701 it was "
           "said to lack, is 7% more vertices rather than three times, and its bone "
           "indices fit this client's format. What is unsettled is whether its body "
           "UVs still match the skins CharSections names. Try it and look; it is a "
           "pack like any other and switching it off puts the old models back.",
    args={"prefixes": ("character",)},
)


PROFILES: tuple[Profile, ...] = (
    Profile(
        profile_id="wotlk",
        name="Wrath of the Lich King",
        summary="The game as it shipped in 3.3.5a.",
        detail="What this client targets. Everything else here is built on top of it.",
        expansion="wotlk",
        steps=(_extract("wotlk"),),
    ),
    Profile(
        profile_id="wotlk-upscaled",
        name="Wrath, with upscaled foliage",
        summary="3.3.5a with sharper trees, bushes and grass.",
        detail="The same game, with the foliage textures upscaled. The change is "
               "resolution only - nothing is re-authored and nothing moves.",
        expansion="wotlk",
        steps=(_extract("wotlk"), _UPSCALE),
    ),
    Profile(
        profile_id="wotlk-cata-trees",
        name="Wrath, with Cataclysm trees",
        summary="3.3.5a, with the later client's trees where it has them.",
        detail="Cataclysm's foliage is a generation better and sits at the same paths, "
               "so it drops in. Needs a Cataclysm installation as well as a Wrath one.",
        expansion="wotlk",
        steps=(_extract("wotlk"), _CATA_TREES, _UPSCALE),
    ),
    Profile(
        profile_id="wotlk-legion",
        name="Wrath, with Legion models",
        summary="3.3.5a with Legion's creatures, doodads and upscaled foliage.",
        detail="The furthest this goes while staying on ground that has been measured. "
               "Player characters are left as they shipped; add them separately if you "
               "want to try them.",
        expansion="wotlk",
        steps=(_extract("wotlk"), _LEGION_CREATURES, _UPSCALE),
    ),
    Profile(
        profile_id="wotlk-legion-characters",
        name="Wrath, with Legion models and characters",
        summary="As above, and Legion's player models too. Unproven.",
        detail="Everything in the profile above, plus the player character models. "
               "Those are the one part nobody has confirmed on screen - see the note "
               "on the step itself.",
        expansion="wotlk",
        steps=(_extract("wotlk"), _LEGION_CREATURES, _LEGION_CHARACTERS, _UPSCALE),
    ),
    Profile(
        profile_id="tbc",
        name="The Burning Crusade",
        summary="2.4.3, as it shipped.",
        detail="Extracted into its own tree, so it neither sees nor disturbs any other "
               "expansion you have extracted.",
        expansion="tbc",
        steps=(_extract("tbc"),),
    ),
    Profile(
        profile_id="classic",
        name="Classic",
        summary="1.12, as it shipped.",
        detail="Extracted into its own tree, so it neither sees nor disturbs any other "
               "expansion you have extracted.",
        expansion="classic",
        steps=(_extract("classic"),),
    ),
    Profile(
        profile_id="turtle",
        name="Turtle WoW",
        summary="The Turtle client's assets, 1.18.1.",
        detail="Turtle ships custom models and zones on a 1.12 base; this keeps them "
               "together in their own tree.",
        expansion="turtle",
        steps=(_extract("turtle"),),
    ),
)


def by_id(profile_id: str) -> Profile | None:
    for profile in PROFILES:
        if profile.profile_id == profile_id:
            return profile
    return None


def availability(have: set[str]) -> dict[str, tuple[bool, str]]:
    """For each profile: can it be built from the sources at hand, and if not why.

    `have` is the set of source kinds present - "mpq" for the game being
    extracted, "casc" for a Warlords-or-later installation. The reason is
    written to be shown to somebody who does not know what CASC is.
    """
    out: dict[str, tuple[bool, str]] = {}
    for profile in PROFILES:
        missing = profile.sources - have
        if not missing:
            out[profile.profile_id] = (True, "")
            continue
        if "mpq" in missing:
            out[profile.profile_id] = (
                False, "Point at the game you want to extract first.")
        elif "casc" in missing:
            out[profile.profile_id] = (
                False, "Needs a Legion or later installation in the second box.")
        else:
            out[profile.profile_id] = (
                False, "Needs a Cataclysm or Mists installation in the second box.")
    return out
