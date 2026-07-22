// ChatCommandRegistry — command registration + dispatch.
// Moved from the if/else chain in ChatPanel::sendChatMessage() (Phase 3.1).
#include "ui/chat/chat_command_registry.hpp"
#include <algorithm>

namespace wowee { namespace ui {

void ChatCommandRegistry::registerCommand(std::unique_ptr<IChatCommand> cmd) {
    IChatCommand* raw = cmd.get();
    for (const auto& alias : raw->aliases()) {
        commandMap_[alias] = raw;
    }
    commands_.push_back(std::move(cmd));
}

ChatCommandResult ChatCommandRegistry::dispatch(const std::string& cmdLower,
                                                  ChatCommandContext& ctx) {
    auto it = commandMap_.find(cmdLower);
    if (it == commandMap_.end()) {
        return {false, false};  // not handled
    }
    return it->second->execute(ctx);
}

std::vector<std::string> ChatCommandRegistry::getCompletions(const std::string& prefix) const {
    std::vector<std::string> results;
    for (const auto& [alias, cmd] : commandMap_) {
        if (alias.size() >= prefix.size() &&
            alias.compare(0, prefix.size(), prefix) == 0) {
            results.push_back(alias);
        }
    }
    std::sort(results.begin(), results.end());
    return results;
}

std::vector<std::pair<std::string, std::string>> ChatCommandRegistry::getHelpEntries() const {
    std::vector<std::pair<std::string, std::string>> entries;
    for (const auto& cmd : commands_) {
        const auto& aliases = cmd->aliases();
        std::string helpText = cmd->helpText();
        if (!aliases.empty() && !helpText.empty()) {
            entries.emplace_back("/" + aliases[0], helpText);
        }
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

bool ChatCommandRegistry::hasCommand(const std::string& alias) const {
    return commandMap_.find(alias) != commandMap_.end();
}

} // namespace ui
} // namespace wowee
