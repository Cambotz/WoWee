#include <catch_amalgamated.hpp>

#include "rendering/m2_model_classifier.hpp"

#include <string>

using wowee::rendering::classifyM2Model;

namespace {

// Bounds and counts stand in for a mid-size doodad. None of the cases below
// turn on geometry - they are all decided by the name - but the classifier
// needs something plausible to reason about.
wowee::rendering::M2ClassificationResult classify(const std::string& name,
                                                  float horiz = 3.0f,
                                                  float vert = 3.0f) {
    return classifyM2Model(name,
                           glm::vec3(-horiz * 0.5f, -horiz * 0.5f, 0.0f),
                           glm::vec3(horiz * 0.5f, horiz * 0.5f, vert),
                           500, 0);
}

} // namespace

// Foliage tokens are substring-matched because model names concatenate words
// with no separator, so a short token can land inside an unrelated word. Every
// name here is a real asset that swayed in the wind because of it.
TEST_CASE("rigid props whose names contain a foliage token do not sway",
          "[m2][classifier][foliage]") {
    SECTION("zone name inside the filename") {
        // "thorn" inside Stranglethorn - the Stranglethorn Vale troll ruins.
        for (const char* n : {"STRANGLETHORNRUINS01", "StranglethornRuins14",
                              "stranglethornruins21", "StranglethornRuins_Pylon",
                              "StranglethornCliffRock02"}) {
            INFO(n);
            CHECK_FALSE(classify(n).isFoliageLike);
        }
    }

    SECTION("token inside a longer structural word") {
        // "corn" in Corner, "hops" in ShopSign, "crop" in Outcrop,
        // "herb" in Herbalism, "tree" in StreetSign, "melon"/"vine" likewise.
        CHECK_FALSE(classify("Azjol_Wall_Corner").isFoliageLike);
        CHECK_FALSE(classify("AquaductStone_Corner1").isFoliageLike);
        CHECK_FALSE(classify("ZulDrak_Ruin_CornerTall01").isFoliageLike);
        CHECK_FALSE(classify("WineShopSign01").isFoliageLike);
        CHECK_FALSE(classify("HumanMagicShopSign").isFoliageLike);
        CHECK_FALSE(classify("AeriePeaksRockOutcrop01").isFoliageLike);
        CHECK_FALSE(classify("BE_Signs_Herbalism").isFoliageLike);
        CHECK_FALSE(classify("Dwarfsign_Herbalist").isFoliageLike);
        CHECK_FALSE(classify("GnomeStreetSign01").isFoliageLike);
        CHECK_FALSE(classify("UldamanStreetSign").isFoliageLike);
        CHECK_FALSE(classify("DivineShield_Low_Base").isFoliageLike);
    }

    SECTION("furniture named for the part of a tree it resembles") {
        // A four-poster's canopy is a canopy, and "canopy" is a foliage token
        // because a tree's is. InnBedCanopy is the one model in that family
        // that is not a tree, and its posts and frame bent in the wind.
        //
        // Ranking cannot catch this one. The structure mechanism needs the
        // solid word to come later in the name - "street" needs "lamp" after
        // it - and here "bed" comes first, so it has to win outright.
        CHECK_FALSE(classify("InnBedCanopy").isFoliageLike);
        CHECK_FALSE(classify("InnBedCanopy").shadowWindFoliage);
        // Its own animation was being turned off as well, which is what
        // disableAnimation does to anything read as foliage.
        CHECK_FALSE(classify("InnBedCanopy").disableAnimation);
        // Substring matching carries the wardrobes along, which is furniture
        // too: warDRoBEDwarven.
        CHECK_FALSE(classify("WardrobeDwarvenOrnate01").isFoliageLike);
    }

    SECTION("and the tree canopies it shares the token with still are") {
        // The other side of the same rule: everything else named "canopy" is a
        // tree, and the whole point of the token is that those sway.
        for (const char* n : {"ElwynnTreeCanopy01", "DuskwoodTreeCanopy02",
                              "RedridgeTreeCanopy04", "SholazarHugeCanopy_Tree01",
                              "ThornCanopy_01", "SwampSorrowCanopyTree03"}) {
            INFO(n);
            CHECK(classify(n).isFoliageLike);
            CHECK(classify(n).shadowWindFoliage);
        }
    }

    SECTION("full paths are matched on the basename") {
        CHECK_FALSE(classify("WORLD\\AZEROTH\\STRANGLETHORN\\PASSIVEDOODADS"
                             "\\RUINS\\STRANGLETHORNRUINS03.M2").isFoliageLike);
    }
}

// The fix ranks matches by where they end, so a structural word only wins when
// it comes last. These names carry one too, and are still plants.
TEST_CASE("plants keep swaying when a structural word comes first",
          "[m2][classifier][foliage]") {
    SECTION("place name first, plant last") {
        for (const char* n : {"DustwallowTree04", "DustwallowBush01",
                              "DustwallowShrub03", "StoneTree06",
                              "BurntStoneTree07", "DeadwindPassRockTree02",
                              "AO_BridgeTree01"}) {
            INFO(n);
            CHECK(classify(n, 8.0f, 9.0f).isFoliageLike);
        }
    }

    SECTION("ordinary foliage is unaffected") {
        for (const char* n : {"StranglethornFern01", "StranglethornPlant02",
                              "ElwynnMelon01", "G_Watermelon",
                              "NorthshireBush01", "DesolaceCactus02"}) {
            INFO(n);
            CHECK(classify(n).isFoliageLike);
        }
    }
}

// isFoliageLike also drives collision and animation, so a false positive did
// more than add wind: it made ruins walk-through and froze their animation.
TEST_CASE("a ruin misread as foliage would also lose its collision",
          "[m2][classifier][foliage]") {
    const auto ruin = classify("StranglethornRuins07", 9.0f, 7.0f);
    CHECK_FALSE(ruin.isFoliageLike);
    CHECK_FALSE(ruin.collisionNoBlock);
    CHECK_FALSE(ruin.disableAnimation);

    const auto tree = classify("DustwallowTree04", 9.0f, 7.0f);
    CHECK(tree.isFoliageLike);
    CHECK(tree.disableAnimation);
}

// isForge forces the batch additive, so a false positive renders a solid model
// as glowing translucent VFX - the same failure the Steam Tank had.
TEST_CASE("only an actual forge is treated as forge fire", "[m2][classifier][forge]") {
    SECTION("the city of Ironforge is not a forge") {
        for (const char* n : {"IronforgeBench_Average01", "IronforgeStatue_01",
                              "IronforgeCliff01", "IronforgeElevator",
                              "IronforgeHangingLantern01", "IronforgeBanner01",
                              "IronforgeSignpost", "ironforgepiston"}) {
            INFO(n);
            CHECK_FALSE(classify(n).isForge);
        }
    }

    SECTION("nor is a part of one, or a panel that controls one") {
        CHECK_FALSE(classify("Dalaran_ForgeArms").isForge);
        CHECK_FALSE(classify("Dalaran_ForgeSmelter").isForge);
        CHECK_FALSE(classify("BU_CrystalForgeController").isForge);
        CHECK_FALSE(classify("UL_Forge_Iron_Press").isForge);
    }

    SECTION("real forges still are, with or without a numeric suffix") {
        for (const char* n : {"DR_Forge_01", "OM_Forge_01", "BU_Forge_01",
                              "ID_Forge", "TS_Forge_01", "BE_Forge01",
                              "Dalaran_Forge", "SC_RuneForge_02",
                              "ET_CrystalForge", "BlacksmithForge",
                              "DarkIronForge", "Wolvar_Forge",
                              "WORLD\\GENERIC\\HUMAN\\FORGE\\ID_FORGE.M2"}) {
            INFO(n);
            CHECK(classify(n).isForge);
        }
    }

    SECTION("forge lava stays excluded as before") {
        CHECK_FALSE(classify("UL_ForgeLava").isForge);
    }
}

TEST_CASE("Blizzard's own misspellings are foliage too", "[m2][classifier]") {
    // WETLANDSSHURB09.M2 is a real path - "shurb", not "shrub" - and it is the
    // only spelling those models have. Reported as grass with cobwebs on it
    // that the player could not walk through.
    //
    // The correct spelling stays covered beside it, because adding the typo is
    // the kind of change that invites someone to "fix" the list later.
    for (const char* path : {
             "WORLD\\KHAZMODAN\\WETLANDS\\PASSIVEDOODADS\\BUSHES\\WETLANDSSHURB09.M2",
             "WORLD\\AZEROTH\\ELWYNN\\PASSIVEDOODADS\\BUSHES\\ELWYNNSHRUB01.M2"}) {
        const auto cls = classify(path);
        INFO(path);
        CHECK(cls.collisionNoBlock);
    }
}

// The Rut'theran Village portal to Darnassus is TeleportTree.m2 - an archway
// you walk through, whose name ends in "tree". The file ships no collision
// geometry at all (nBoundingTriangles = 0), so the real client lets you walk
// straight in; our trunk-cylinder rule instead planted a solid block dead
// centre in the arch and the portal became unreachable without /unstuck.
//
// The bounds here are the model's real ones (~45 wide, ~45 tall), which is what
// put it over the trunk rule's horiz > 6 && vert > 4 threshold in the first
// place.
TEST_CASE("a teleport arch is a doorway, not a tree", "[m2][classifier][collision]") {
    const auto portal = classify("TeleportTree", 45.0f, 45.0f);
    CHECK_FALSE(portal.collisionTreeTrunk);
    CHECK(portal.collisionNoBlock);

    SECTION("the full path form resolves the same way") {
        const auto viaPath = classify("WORLD\\GENERIC\\NIGHTELF\\PASSIVE DOODADS"
                                      "\\TELEPORTTREE\\TELEPORTTREE.M2", 45.0f, 45.0f);
        CHECK_FALSE(viaPath.collisionTreeTrunk);
        CHECK(viaPath.collisionNoBlock);
    }

    SECTION("the rest of the family is walk-through too") {
        for (const char* n : {"BG_Teleporter_Alliance_01", "UL_TeleportationPad",
                              "SC_TeleportPad2", "BE_Teleporter_01",
                              "G_GoblinTeleporter"}) {
            INFO(n);
            CHECK(classify(n, 8.0f, 8.0f).collisionNoBlock);
        }
    }

    SECTION("an ordinary tree of the same size keeps its trunk") {
        const auto tree = classify("KalidarTree07", 45.0f, 45.0f);
        CHECK(tree.collisionTreeTrunk);
        CHECK_FALSE(tree.collisionNoBlock);
    }
}

// A conifer is tall and narrow, and the trunk rule used to demand six yards of
// spread as well as four of height. Every pine failed the width half, fell
// through to softTree, and had its collision turned off outright - a full-sized
// forest that could be walked through.
TEST_CASE("a tall tree has a trunk however narrow it is", "[m2][classifier][collision]") {
    SECTION("a pine twenty yards tall and four across is solid") {
        const auto pine = classify("TirisfalPineTree02", 4.0f, 20.0f);
        CHECK(pine.collisionTreeTrunk);
        CHECK_FALSE(pine.collisionNoBlock);
    }

    SECTION("a standing trunk on its own is solid too") {
        const auto trunk = classify("AshenvaleTreeTrunk01", 3.0f, 12.0f);
        CHECK(trunk.collisionTreeTrunk);
        CHECK_FALSE(trunk.collisionNoBlock);
    }

    SECTION("a stump and a fallen log keep their exemption") {
        CHECK_FALSE(classify("ElwynnTreeStump01", 3.0f, 12.0f).collisionTreeTrunk);
        CHECK_FALSE(classify("ElwynnTreeLog01", 3.0f, 12.0f).collisionTreeTrunk);
    }

    SECTION("a sapling short enough to push past stays walkable") {
        const auto sapling = classify("ElwynnTree01", 2.0f, 3.0f);
        CHECK_FALSE(sapling.collisionTreeTrunk);
        CHECK(sapling.collisionNoBlock);
    }
}

// The second invisible wall at that same portal. AuraPurple.m2 is the glow
// inside the arch; its VFX identity is only in the DIRECTORY, and tokens are
// matched on the basename, so "particleemitter" in kEffectTokens never fired.
// Its real unscaled bounds (1.96 x 1.96 x 3.08) land inside genericSolid's
// window, so it became a solid prop - and the doodad is placed at scale 10.69,
// making a ~21 x 21 x 33 unit block of solid nothing over the portal.
TEST_CASE("models under PARTICLEEMITTERS are VFX, not props",
          "[m2][classifier][collision]") {
    const auto bareName = classify("AuraPurple", 1.96f, 3.08f);
    CHECK(bareName.collisionSmallSolidProp);  // the name alone cannot tell

    const auto aura = classify("WORLD\\GENERIC\\PASSIVEDOODADS\\PARTICLEEMITTERS"
                               "\\AURAPURPLE.M2", 1.96f, 3.08f);
    CHECK(aura.isSpellEffect);
    CHECK(aura.collisionNoBlock);

    SECTION("the whole directory, whichever separator the path uses") {
        for (const char* p : {
                 "WORLD\\GENERIC\\PASSIVEDOODADS\\PARTICLEEMITTERS\\AURABLUETALL.M2",
                 "WORLD\\GENERIC\\PASSIVEDOODADS\\PARTICLEEMITTERS\\ASHENVALEWISPS.M2",
                 "world/generic/passivedoodads/particleemitters/auragreen.m2"}) {
            INFO(p);
            const auto cls = classify(p, 1.96f, 3.08f);
            CHECK(cls.isSpellEffect);
            CHECK(cls.collisionNoBlock);
        }
    }

    SECTION("a real prop of the same size elsewhere still blocks") {
        const auto crate = classify("WORLD\\GENERIC\\HUMAN\\PASSIVE DOODADS"
                                    "\\CRATES\\CRATE01.M2", 1.96f, 3.08f);
        CHECK_FALSE(crate.isSpellEffect);
        CHECK_FALSE(crate.collisionNoBlock);
    }
}

// "street" contains "tree", which this list already knew about for StreetSign
// and not for StreetLamp - so Stormwind's ironwork lamps swayed in the wind.
TEST_CASE("a street lamp is ironwork, not a sapling", "[m2][classifier]") {
    // The real asset names, not invented ones.
    for (const char* n : {"StormwindStreetlamp01", "StormwindCanalLamp01",
                          "IronforgeHangingLantern01",
                          "WORLD\\AZEROTH\\ELWYNN\\PASSIVEDOODADS\\LAMPS\\STORMWINDSTREETLAMP01.M2"}) {
        INFO(n);
        CHECK_FALSE(classify(n, 1.2f, 5.0f).isFoliageLike);
        CHECK_FALSE(classify(n, 1.2f, 5.0f).shadowWindFoliage);
    }
    SECTION("and the sign it sits next to is still not a tree") {
        CHECK_FALSE(classify("GnomeStreetSign01").isFoliageLike);
    }
}

// Cloth hung from its top edge, which sways from the bar down rather than from
// the ground up. The names are the real assets: Stormwind's gate and interior
// banners are WMO doodads under PASSIVE DOODADS\BANNERS, and Karazhan's are
// tapestries.
TEST_CASE("hanging cloth is picked out by name", "[m2][classifier][cloth]") {
    SECTION("banners, tapestries and flags") {
        for (const char* n : {
                 "WORLD\\GENERIC\\HUMAN\\PASSIVE DOODADS\\BANNERS\\STORMWINDLIONBANNER.m2",
                 "world\\generic\\human\\passive doodads\\banners\\stormwindgriffonbanner01.m2",
                 "world\\azeroth\\karazahn\\passivedoodads\\tapestries\\karazantapestry01.m2",
                 "world\\azeroth\\elwynn\\passivedoodads\\battlegladebanner1\\battlegladebanner1.m2",
                 "AllianceFlag01", "HordePennant02"}) {
            INFO(n);
            CHECK(classify(n).isHangingCloth);
        }
    }

    SECTION("what is not cloth") {
        // A flagstone is a floor, and swaying a floor is worse than a still
        // banner. Foliage keeps its own sway rather than taking this one.
        for (const char* n : {"Flagstone01", "ElwynnFlagstoneFloor",
                              "StormwindBrazier01", "ElwynnTree01"}) {
            INFO(n);
            CHECK_FALSE(classify(n).isHangingCloth);
        }
    }
}

// A stump is a tree by name and timber by nature.
//
// shadowWindFoliage is what bends a model in the wind, and it was set for
// anything foliage-like - which a name carrying "tree" always is. So the cut
// stumps and fallen logs scattered through Elwynn and Duskwood swayed like
// saplings, trunk and all, with no canopy to justify it.
//
// The classifier already separates the wooden parts of a tree from the leafy
// ones: hardTreePart is trunk, stump and log, and it is what the collision
// rules use to decide that these are things you step over rather than walk
// around. The same set is what must not bend.
TEST_CASE("the wooden parts of a tree do not sway", "[m2][classifier][foliage]") {
    SECTION("stumps, logs and bare trunks") {
        for (const char* n : {"ElwynnTreeStump01", "elwynntreestump02",
                              "DuskwoodTreeStump01", "TreeStumpSmall",
                              "ElwynnLog01", "FallenTreeLog02",
                              "ElwynnTreeTrunk01", "TeldrassilTreeTrunk03",
                              "WORLD\\AZEROTH\\ELWYNN\\PASSIVEDOODADS\\TREES\\ELWYNNTREESTUMP01.M2"}) {
            INFO(n);
            CHECK_FALSE(classify(n, 2.0f, 1.5f).shadowWindFoliage);
        }
    }

    SECTION("but a whole tree still does") {
        for (const char* n : {"ElwynnTree01", "DuskwoodTree03", "TeldrassilTree02"}) {
            INFO(n);
            CHECK(classify(n, 8.0f, 14.0f).shadowWindFoliage);
        }
    }

    SECTION("and so does the undergrowth around them") {
        for (const char* n : {"ElwynnBush01", "ElwynnFern02"}) {
            INFO(n);
            CHECK(classify(n, 1.5f, 1.2f).shadowWindFoliage);
        }
    }
}

// ── Fires whose names run their words together ────────────────

// "bonfire" and "campfire" are matched as plain substrings. A delimited-word
// match wants a non-letter in front of the token, and WoW's model names do not
// oblige: 19 of the 37 fires in the WotLK data failed it, the Grom'gol bonfire
// among them, and got none of the flame treatment or the ambient fire loop.
TEST_CASE("a fire whose name runs its words together is still a fire",
          "[m2][classifier][fire]") {
    SECTION("no delimiter before the token") {
        CHECK(classify("World\\Azeroth\\BurningSteppes\\PassiveDoodads\\Bonfire\\OrcBonFire.m2")
                  .isBrazierOrFire);
        CHECK(classify("orcpvpbonfirelarge.m2").isBrazierOrFire);
        CHECK(classify("blackrockorccampfire.m2").isBrazierOrFire);
        CHECK(classify("karazahnbonfire01.m2").isBrazierOrFire);
        CHECK(classify("northrendundeadcampfire.m2").isBrazierOrFire);
        CHECK(classify("orgrimmarbonfire01.m2").isBrazierOrFire);
    }

    SECTION("delimited names still match") {
        CHECK(classify("summerfest_bonfire_large01.m2").isBrazierOrFire);
        CHECK(classify("elwynncampfire.m2").isBrazierOrFire);
    }

    SECTION("firepit too, which has the same shape") {
        CHECK(classify("largefirepit01.m2").isBrazierOrFire);
        CHECK(classify("smallfirepit01.m2").isBrazierOrFire);
    }

    // A bare "fire" has to stand alone at both ends. Guarding only the front
    // let it match the head of these, so a stack of logs was a burning one -
    // with a flame's colour floor and an ambient fire loop over it.
    SECTION("words that merely begin with fire are not fires") {
        CHECK_FALSE(classify("firewoodpile03.m2").isBrazierOrFire);
        CHECK_FALSE(classify("fireflies01.m2").isBrazierOrFire);
        CHECK_FALSE(classify("gunshopfireworks01.m2").isBrazierOrFire);
        CHECK_FALSE(classify("g_firework01blue.m2").isBrazierOrFire);
        CHECK_FALSE(classify("firecrackerstring_red01.m2").isBrazierOrFire);
    }

    // And a fire named at the end of a compound still counts.
    SECTION("fire at the end of a name") {
        CHECK(classify("valgarde_fire.m2").isBrazierOrFire);
        CHECK(classify("orctablecooker01fire.m2").isBrazierOrFire);
    }

    // The unlit variant of a fire is not a burning one: it would pick up a
    // flame's colour floor and an ambient fire loop while standing cold.
    SECTION("an explicitly unlit variant is not lit") {
        CHECK_FALSE(classify("orcbonfireoff.m2").isBrazierOrFire);
        CHECK_FALSE(classify("torch_out.m2").isTorch);
        CHECK(classify("torch.m2").isTorch);
    }
}

// The ambient sound system asks the same question with its own entry point.
// It had its own copy of the test and the two had already drifted: a model
// could be drawn as flame and play no fire, or the reverse.
TEST_CASE("the fire sound and the fire rendering agree on what a fire is",
          "[m2][classifier][fire]") {
    using wowee::rendering::AmbientEmitterType;
    const auto emitter = [](const char* name) {
        return wowee::rendering::classifyAmbientEmitter(name);
    };
    for (const char* name : {"orcbonfire", "orcpvpbonfirelarge", "blackrockorccampfire",
                             "largefirepit01", "valgarde_fire"}) {
        CAPTURE(name);
        CHECK(classify(std::string(name) + ".m2").isBrazierOrFire);
        const auto type = emitter(name);
        CHECK((type == AmbientEmitterType::FireplaceSmall ||
               type == AmbientEmitterType::FireplaceLarge));
    }
    for (const char* name : {"firewoodpile03", "gunshopfireworks01", "orcbonfireoff"}) {
        CAPTURE(name);
        CHECK_FALSE(classify(std::string(name) + ".m2").isBrazierOrFire);
        CHECK(emitter(name) == AmbientEmitterType::None);
    }
}
