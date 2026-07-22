#include "npc_presets.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/asset_manifest.hpp"
#include "core/logger.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace wowee {
namespace editor {

const char* NpcPresets::getCategoryName(CreatureCategory cat) {
    static const char* names[] = {
        "Critters", "Beasts", "Humanoids", "Undead", "Demons",
        "Elementals", "Dragonkin", "Giants", "Mechanical", "Mounts", "Bosses", "Other"
    };
    return names[static_cast<int>(cat)];
}

std::string NpcPresets::prettifyName(const std::string& dirName) const {
    std::string result;
    for (size_t i = 0; i < dirName.size(); i++) {
        char c = dirName[i];
        if (i == 0) {
            result += static_cast<char>(std::toupper(c));
        } else if (std::isupper(c) && i > 0 && std::islower(dirName[i-1])) {
            result += ' ';
            result += c;
        } else {
            result += c;
        }
    }
    return result;
}

CreatureCategory NpcPresets::classifyCreature(const std::string& name) const {
    // Critters
    static const char* critters[] = {
        "rabbit", "rat", "chicken", "frog", "snake", "squirrel", "deer", "sheep",
        "cow", "pig", "parrot", "seagull", "beetle", "cockroach", "crab", "prairie",
        "butterfly", "firefly", "maggot", "toad", "mouse", "hare", "penguin",
        "babycrocodile", "babyelekk", "bearcub", "cat", "smallfish",
        "kitten", "skunk", "ladybug", "gazelle", "gilamonster"
    };
    // Beasts
    static const char* beasts[] = {
        "bear", "boar", "wolf", "lion", "tiger", "raptor", "gorilla", "hyena",
        "scorpid", "spider", "bat", "vulture", "crocolisk", "tallstrider",
        "kodo", "elekk", "warp", "ravager", "serpent", "devilsaur", "crochet",
        "plainstrider", "stag", "moose", "worg", "rhino", "mammoth", "jormungar",
        "shoveltusk", "basilisk", "carrionbird", "condor", "hippogryph",
        "windserpent", "thunderlizard", "turtle", "silithid", "wasp", "moth",
        "nether", "cat", "arcticcondor"
    };
    // Humanoids
    static const char* humanoids[] = {
        "human", "orc", "dwarf", "nightelf", "undead", "tauren", "gnome", "troll",
        "bloodelf", "draenei", "goblin", "ogre", "murloc", "naga", "satyr",
        "centaur", "furbolg", "gnoll", "kobold", "trogg", "harpy", "pirate",
        "bandit", "vrykul", "tuskarr", "wolvar", "arakkoa", "ethereal",
        "broken", "fleshgiant", "kvaldir", "pygmy", "taunka"
    };
    // Undead
    static const char* undead[] = {
        "skeleton", "zombie", "ghoul", "ghost", "banshee", "lich", "wraith",
        "abomination", "geist", "shade", "spectre", "boneguard", "bonespider",
        "bonegolem", "crypt", "necro", "plague", "scourge", "val"
    };
    // Demons
    static const char* demons[] = {
        "demon", "felguard", "imp", "infernal", "doomguard", "succubus",
        "voidwalker", "felhound", "eredar", "pitlord", "dreadlord",
        "abyssal", "felboar", "darkhound", "terrorfiend"
    };
    // Elementals
    static const char* elementals[] = {
        "elemental", "fire", "water", "air", "earth", "arcane", "storm",
        "lava", "bog", "ooze", "slime", "revenant", "totem"
    };
    // Dragonkin
    static const char* dragonkin[] = {
        "dragon", "drake", "whelp", "wyrm", "dragonspawn", "drakonid",
        "nether", "proto", "celestialdragon"
    };
    // Giants
    static const char* giants[] = {
        "giant", "ettin", "gronn", "colossus", "titan", "mountain", "sea"
    };
    // Mechanical
    static const char* mechanical[] = {
        "mechanical", "robot", "golem", "harvest", "shredder", "gyro",
        "bomber", "tank", "turret", "cannon", "siege"
    };
    // Mounts
    static const char* mounts[] = {
        "mount", "horse", "hawkstrider", "raptor", "mechanostrider",
        "nightsaber", "ram", "kodo", "skeletal", "broom", "carpet",
        "gryphon", "wyvern", "hippogryph", "netherdrake", "protodrake"
    };
    // Boss
    static const char* bosses[] = {
        "arthas", "illidan", "kelthuzad", "ragnaros", "onyxia", "nefarian",
        "alexstrasza", "malygos", "sartharion", "yoggsaron", "lichking",
        "brutallus", "bloodqueen", "anubarak"
    };

    auto matches = [&](const char* list[], size_t count) {
        for (size_t i = 0; i < count; i++) {
            if (name.find(list[i]) != std::string::npos) return true;
        }
        return false;
    };

    if (matches(critters, sizeof(critters)/sizeof(critters[0]))) return CreatureCategory::Critter;
    if (matches(mounts, sizeof(mounts)/sizeof(mounts[0]))) return CreatureCategory::Mount;
    if (matches(bosses, sizeof(bosses)/sizeof(bosses[0]))) return CreatureCategory::Boss;
    if (matches(undead, sizeof(undead)/sizeof(undead[0]))) return CreatureCategory::Undead;
    if (matches(demons, sizeof(demons)/sizeof(demons[0]))) return CreatureCategory::Demon;
    if (matches(dragonkin, sizeof(dragonkin)/sizeof(dragonkin[0]))) return CreatureCategory::Dragonkin;
    if (matches(elementals, sizeof(elementals)/sizeof(elementals[0]))) return CreatureCategory::Elemental;
    if (matches(giants, sizeof(giants)/sizeof(giants[0]))) return CreatureCategory::Giant;
    if (matches(mechanical, sizeof(mechanical)/sizeof(mechanical[0]))) return CreatureCategory::Mechanical;
    if (matches(humanoids, sizeof(humanoids)/sizeof(humanoids[0]))) return CreatureCategory::Humanoid;
    if (matches(beasts, sizeof(beasts)/sizeof(beasts[0]))) return CreatureCategory::Beast;

    return CreatureCategory::Other;
}

uint32_t NpcPresets::estimateLevel(const std::string& /*dirName*/) const {
    return 10;
}

uint32_t NpcPresets::estimateHealth(uint32_t level) const {
    return 50 + level * 80;
}

void NpcPresets::initialize(pipeline::AssetManager* am) {
    if (initialized_ || !am) return;
    initialized_ = true;

    byCategory_.resize(static_cast<size_t>(CreatureCategory::COUNT));

    const auto& entries = am->getManifest().getEntries();
    std::set<std::string> seen;

    for (const auto& [path, entry] : entries) {
        if (!path.starts_with("creature\\")) continue;
        if (!path.ends_with(".m2")) continue;

        // Extract directory name (creature type)
        auto firstSlash = path.find('\\');
        auto secondSlash = path.find('\\', firstSlash + 1);
        if (secondSlash == std::string::npos) continue;

        std::string dirName = path.substr(firstSlash + 1, secondSlash - firstSlash - 1);
        if (seen.count(dirName)) continue;

        // Skip known effect/particle models that cause vertex explosions
        static const char* skipModels[] = {
            "alliancebomb", "alliancebrasscannon", "blackhole", "bodyofkathune",
            "band", "broom", "broommount", "carpet", "celestialhorse",
            "bloodqueen", "arthaslichking", "arthasundead"
        };
        bool skip = false;
        for (const char* s : skipModels) {
            if (dirName == s) { skip = true; break; }
        }
        if (skip) continue;

        seen.insert(dirName);

        // Get the actual M2 file path
        std::string modelFile = path;

        NpcPreset preset;
        preset.name = prettifyName(dirName);
        preset.modelPath = modelFile;
        preset.category = classifyCreature(dirName);
        preset.defaultLevel = estimateLevel(dirName);
        preset.defaultHealth = estimateHealth(preset.defaultLevel);
        preset.defaultHostile = (preset.category != CreatureCategory::Critter &&
                                  preset.category != CreatureCategory::Mount);

        presets_.push_back(preset);
        byCategory_[static_cast<size_t>(preset.category)].push_back(preset);
    }

    // Sort each category alphabetically
    for (auto& cat : byCategory_) {
        std::sort(cat.begin(), cat.end(),
                  [](const NpcPreset& a, const NpcPreset& b) { return a.name < b.name; });
    }
    std::sort(presets_.begin(), presets_.end(),
              [](const NpcPreset& a, const NpcPreset& b) { return a.name < b.name; });

    LOG_INFO("NPC presets: ", presets_.size(), " creatures in ", seen.size(), " types");
}

const std::vector<NpcPreset>& NpcPresets::getByCategory(CreatureCategory cat) const {
    return byCategory_[static_cast<size_t>(cat)];
}

} // namespace editor
} // namespace wowee
