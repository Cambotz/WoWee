#pragma once

#include <string>
#include <vector>
#include <array>

namespace wowee {
namespace editor {

enum class Biome {
    Grassland,
    Forest,
    Jungle,
    Desert,
    Barrens,
    Snow,
    Swamp,
    Rocky,
    Beach,
    Volcanic,
    COUNT
};

struct BiomeTextures {
    const char* name;
    const char* base;       // Primary ground texture
    const char* secondary;  // Secondary layer (dirt/path)
    const char* accent;     // Accent (rocks/roots)
    const char* detail;     // Detail overlay
};

inline const BiomeTextures& getBiomeTextures(Biome biome) {
    static const std::array<BiomeTextures, static_cast<size_t>(Biome::COUNT)> biomes = {{
        { // Grassland
            "Grassland",
            "Tileset\\Elwynn\\ElwynnGrassBase.blp",
            "Tileset\\Elwynn\\ElwynnDirtBase.blp",
            "Tileset\\Elwynn\\ElwynnCobblestoneBase.blp",
            "Tileset\\Elwynn\\ElwynnGrassHighlight.blp"
        },
        { // Forest
            "Forest",
            "Tileset\\Ashenvale\\AshenvaleGrass.blp",
            "Tileset\\Ashenvale\\AshenvaleDirt.blp",
            "Tileset\\Ashenvale\\AshenvaleRoots.blp",
            "Tileset\\Ashenvale\\AshenvaleMossBase.blp"
        },
        { // Jungle
            "Jungle",
            "Tileset\\Stranglethorn\\StranglethornGrass.blp",
            "Tileset\\Stranglethorn\\StranglethornDirt03.blp",
            "Tileset\\Stranglethorn\\StranglethornMossRoot01.blp",
            "Tileset\\Stranglethorn\\StranglethornPlants01.blp"
        },
        { // Desert
            "Desert",
            "Tileset\\Tanaris\\TanarisSandBase01.blp",
            "Tileset\\Tanaris\\TanarisCrackedGround.blp",
            "Tileset\\Tanaris\\TanarisRockBase01.blp",
            "Tileset\\Tanaris\\TanarisSandBase02.blp"
        },
        { // Barrens
            "Barrens",
            "Tileset\\Barrens\\BarrensBaseDirt.blp",
            "Tileset\\Barrens\\BarrensBaseGrassGold.blp",
            "Tileset\\Barrens\\BarrensBaseRock.blp",
            "Tileset\\Barrens\\BarrensBaseDirtLighter.blp"
        },
        { // Snow
            "Snow",
            "Tileset\\Expansion02\\Dragonblight\\DragonblightFreshSmoothSnowA.blp",
            "Tileset\\Winterspring Grove\\WinterspringDirt.blp",
            "Tileset\\Winterspring Grove\\WinterspringRock.blp",
            "Tileset\\Winterspring Grove\\WinterspringRockSnow.blp"
        },
        { // Swamp
            "Swamp",
            "Tileset\\Wetlands\\WetlandsGrassDark01.blp",
            "Tileset\\Wetlands\\WetlandsDirt01.blp",
            "Tileset\\Wetlands\\WetlandsDirtMoss01.blp",
            "Tileset\\Wetlands\\WetlandsBaseRock.blp"
        },
        { // Rocky
            "Rocky",
            "Tileset\\Barrens\\BarrensRock01.blp",
            "Tileset\\Barrens\\BarrensBaseDirt.blp",
            "Tileset\\Desolace\\DesolaceRock01.blp",
            "Tileset\\Desolace\\DesolaceDirt.blp"
        },
        { // Beach
            "Beach",
            "Tileset\\Ashenvale\\AshenvaleSand.blp",
            "Tileset\\Feralas\\FeralasSand.blp",
            "Tileset\\Ashenvale\\AshenvaleShore.blp",
            "Tileset\\Feralas\\FeralasGrass.blp"
        },
        { // Volcanic
            "Volcanic",
            "Tileset\\Desolace\\DesolaceDirt.blp",
            "Tileset\\Desolace\\DesolaceCracks.blp",
            "Tileset\\Desolace\\DesolaceRock01.blp",
            "Tileset\\Tanaris\\TanarisRockBaseBurn.blp"
        }
    }};
    return biomes[static_cast<size_t>(biome)];
}

inline const char* getBiomeName(Biome b) {
    return getBiomeTextures(b).name;
}

// Vegetation rule: which M2 models to scatter per biome
struct VegetationAsset {
    const char* path;
    float density;      // objects per 100x100 unit area
    float minScale;
    float maxScale;
    float maxSlope;     // max terrain slope (0-1, 0=flat only, 1=any)
    float minHeight;    // relative to base (-999=any)
    float maxHeight;    // relative to base (999=any)
};

struct BiomeVegetation {
    const char* name;
    std::vector<VegetationAsset> assets;
};

inline BiomeVegetation getBiomeVegetation(Biome biome) {
    switch (biome) {
        case Biome::Grassland: return {"Grassland", {
            {"World\\Doodad\\Azeroth\\Elwynn\\PineTree\\ElwynnPineTree01.m2", 3.0f, 0.8f, 1.4f, 0.6f, -999, 999},
            {"World\\Doodad\\Azeroth\\Elwynn\\ElwynnBush01.m2", 5.0f, 0.6f, 1.2f, 0.8f, -999, 999},
            {"World\\Doodad\\Azeroth\\Rock\\Rock01.m2", 1.0f, 0.5f, 1.5f, 1.0f, -999, 999},
        }};
        case Biome::Forest: return {"Forest", {
            {"World\\Doodad\\Azeroth\\Ashenvale\\AshenvaleTree01.m2", 6.0f, 0.7f, 1.5f, 0.5f, -999, 999},
            {"World\\Doodad\\Azeroth\\Ashenvale\\AshenvaleTree02.m2", 4.0f, 0.8f, 1.3f, 0.5f, -999, 999},
            {"World\\Doodad\\Azeroth\\Ashenvale\\AshenvaleFern01.m2", 8.0f, 0.4f, 0.9f, 0.7f, -999, 999},
            {"World\\Doodad\\Azeroth\\Rock\\ForestRock01.m2", 1.5f, 0.6f, 1.8f, 1.0f, -999, 999},
        }};
        case Biome::Jungle: return {"Jungle", {
            {"World\\Doodad\\Azeroth\\Stranglethorn\\StranglethornPalmTree01.m2", 5.0f, 0.8f, 1.4f, 0.5f, -999, 999},
            {"World\\Doodad\\Azeroth\\Stranglethorn\\StranglethornFern01.m2", 10.0f, 0.3f, 0.8f, 0.8f, -999, 999},
            {"World\\Doodad\\Azeroth\\Stranglethorn\\StranglethornVines01.m2", 3.0f, 0.7f, 1.2f, 0.6f, -999, 999},
        }};
        case Biome::Desert: return {"Desert", {
            {"World\\Doodad\\Azeroth\\Tanaris\\TanarisCactus01.m2", 2.0f, 0.6f, 1.3f, 0.7f, -999, 999},
            {"World\\Doodad\\Azeroth\\Rock\\DesertRock01.m2", 1.5f, 0.5f, 2.0f, 1.0f, -999, 999},
            {"World\\Doodad\\Azeroth\\Tanaris\\TanarisBones01.m2", 0.5f, 0.8f, 1.2f, 0.5f, -999, 999},
        }};
        case Biome::Barrens: return {"Barrens", {
            {"World\\Doodad\\Azeroth\\Barrens\\BarrensTree01.m2", 1.5f, 0.7f, 1.3f, 0.6f, -999, 999},
            {"World\\Doodad\\Azeroth\\Barrens\\BarrensBush01.m2", 3.0f, 0.5f, 1.0f, 0.8f, -999, 999},
            {"World\\Doodad\\Azeroth\\Rock\\BarrensRock01.m2", 1.0f, 0.6f, 1.5f, 1.0f, -999, 999},
        }};
        case Biome::Snow: return {"Snow", {
            {"World\\Doodad\\Azeroth\\Winterspring\\WinterspringPine01.m2", 4.0f, 0.8f, 1.5f, 0.5f, -999, 999},
            {"World\\Doodad\\Azeroth\\Winterspring\\WinterspringSnowDrift01.m2", 2.0f, 0.5f, 1.2f, 0.4f, -999, 999},
            {"World\\Doodad\\Azeroth\\Rock\\SnowRock01.m2", 1.0f, 0.6f, 1.8f, 1.0f, -999, 999},
        }};
        case Biome::Swamp: return {"Swamp", {
            {"World\\Doodad\\Azeroth\\Wetlands\\WetlandsTree01.m2", 4.0f, 0.7f, 1.3f, 0.5f, -999, 999},
            {"World\\Doodad\\Azeroth\\Wetlands\\WetlandsMushroom01.m2", 6.0f, 0.3f, 0.7f, 0.8f, -999, 999},
            {"World\\Doodad\\Azeroth\\Wetlands\\WetlandsLog01.m2", 1.5f, 0.8f, 1.2f, 0.4f, -999, 999},
        }};
        case Biome::Rocky: return {"Rocky", {
            {"World\\Doodad\\Azeroth\\Rock\\Rock01.m2", 3.0f, 0.5f, 2.5f, 1.0f, -999, 999},
            {"World\\Doodad\\Azeroth\\Rock\\RockPile01.m2", 2.0f, 0.6f, 1.5f, 1.0f, -999, 999},
        }};
        case Biome::Beach: return {"Beach", {
            {"World\\Doodad\\Azeroth\\Stranglethorn\\StranglethornPalmTree01.m2", 2.0f, 0.7f, 1.3f, 0.4f, -999, 999},
            {"World\\Doodad\\Azeroth\\Rock\\BeachRock01.m2", 1.5f, 0.5f, 1.5f, 0.6f, -999, 999},
        }};
        case Biome::Volcanic: return {"Volcanic", {
            {"World\\Doodad\\Azeroth\\Rock\\LavaRock01.m2", 2.5f, 0.6f, 2.0f, 1.0f, -999, 999},
            {"World\\Doodad\\Azeroth\\Burning Steppes\\BurningSteppesCharredTree01.m2", 1.0f, 0.8f, 1.2f, 0.5f, -999, 999},
        }};
        default: return {"Unknown", {}};
    }
}

} // namespace editor
} // namespace wowee
