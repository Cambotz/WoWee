#include "cli_multi_arg_required.hpp"

namespace wowee {
namespace editor {
namespace cli {

const MultiArgFlag kMultiArgRequired[] = {
    {"--adt", 3, "--adt requires <map> <x> <y>"},
    {"--diff-zone", 2, "--diff-zone requires <zoneA> <zoneB>"},
    {"--diff-glb", 2, "--diff-glb requires <a.glb> <b.glb>"},
    {"--diff-wom", 2, "--diff-wom requires <a-base> <b-base>"},
    {"--diff-wob", 2, "--diff-wob requires <a-base> <b-base>"},
    {"--diff-whm", 2, "--diff-whm requires <a-base> <b-base>"},
    {"--diff-woc", 2, "--diff-woc requires <a.woc> <b.woc>"},
    {"--diff-jsondbc", 2, "--diff-jsondbc requires <a.json> <b.json>"},
    {"--diff-extract", 2, "--diff-extract requires <dirA> <dirB>"},
    {"--diff-checksum", 2, "--diff-checksum requires <a.sha256> <b.sha256>"},
    {"--diff-wcp", 2, "--diff-wcp requires two paths"},
    {"--add-creature", 5,
        "--add-creature requires <zoneDir> <name> <x> <y> <z>"},
    {"--add-object", 6,
        "--add-object requires <zoneDir> <m2|wmo> <gamePath> <x> <y> <z>"},
    {"--add-quest", 2, "--add-quest requires <zoneDir> <title>"},
    {"--add-quest-objective", 4,
        "--add-quest-objective requires <zoneDir> <questIdx> <type> <targetName>"},
    {"--remove-quest-objective", 3,
        "--remove-quest-objective requires <zoneDir> <questIdx> <objIdx>"},
    {"--clone-quest", 2, "--clone-quest requires <zoneDir> <questIdx>"},
    {"--clone-creature", 2, "--clone-creature requires <zoneDir> <idx>"},
    {"--clone-object", 2, "--clone-object requires <zoneDir> <idx>"},
    {"--add-quest-reward-item", 3,
        "--add-quest-reward-item requires <zoneDir> <questIdx> <itemPath>"},
    {"--set-quest-reward", 2,
        "--set-quest-reward requires <zoneDir> <questIdx> [--xp N] [--gold N] [--silver N] [--copper N]"},
    {"--add-tile", 3, "--add-tile requires <zoneDir> <tx> <ty>"},
    {"--remove-tile", 3, "--remove-tile requires <zoneDir> <tx> <ty>"},
    {"--copy-zone", 2, "--copy-zone requires <srcDir> <newName>"},
    {"--rename-zone", 2, "--rename-zone requires <srcDir> <newName>"},
    {"--remove-creature", 2, "--remove-creature requires <zoneDir> <index>"},
    {"--remove-object", 2, "--remove-object requires <zoneDir> <index>"},
    {"--remove-quest", 2, "--remove-quest requires <zoneDir> <index>"},
};

const std::size_t kMultiArgRequiredSize =
    sizeof(kMultiArgRequired) / sizeof(kMultiArgRequired[0]);

} // namespace cli
} // namespace editor
} // namespace wowee
