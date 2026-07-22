#include "addons/addon_manager.hpp"
#include "core/logger.hpp"
#include "core/config_paths.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace wowee::addons {

AddonManager::AddonManager() = default;
AddonManager::~AddonManager() { shutdown(); }

bool AddonManager::initialize(game::GameHandler* gameHandler, const LuaServices& services) {
    gameHandler_ = gameHandler;
    luaServices_ = services;
    if (!luaEngine_.initialize()) return false;
    luaEngine_.setGameHandler(gameHandler);
    luaEngine_.setLuaServices(luaServices_);
    return true;
}

void AddonManager::scanAddons(const std::string& addonsPath) {
    addonsPath_ = addonsPath;
    addons_.clear();

    std::error_code ec;
    if (!fs::is_directory(addonsPath, ec)) {
        LOG_INFO("AddonManager: no AddOns directory at ", addonsPath);
        return;
    }

    std::vector<fs::path> dirs;
    for (const auto& entry : fs::directory_iterator(addonsPath, ec)) {
        if (entry.is_directory()) dirs.push_back(entry.path());
    }
    // Sort alphabetically for deterministic load order
    std::sort(dirs.begin(), dirs.end());

    for (const auto& dir : dirs) {
        std::string dirName = dir.filename().string();
        std::string tocPath = (dir / (dirName + ".toc")).string();
        auto toc = parseTocFile(tocPath);
        if (!toc) continue;

        if (toc->isLoadOnDemand()) {
            LOG_DEBUG("AddonManager: skipping LoadOnDemand addon: ", dirName);
            continue;
        }

        LOG_INFO("AddonManager: registered addon '", toc->getTitle(),
                 "' (", toc->files.size(), " files)");
        addons_.push_back(std::move(*toc));
    }

    LOG_INFO("AddonManager: scanned ", addons_.size(), " addons");
    // Load persisted enable/disable choices now that we know which addons exist.
    loadEnabledState();
}

void AddonManager::loadAllAddons() {
    // Only hand the Lua VM the addons that are actually enabled, so disabled ones
    // don't appear via GetNumAddOns/IsAddOnLoaded either.
    std::vector<TocFile> enabled;
    enabled.reserve(addons_.size());
    for (const auto& addon : addons_) {
        if (isAddonEnabled(addon.addonName)) enabled.push_back(addon);
    }
    luaEngine_.setAddonList(enabled);
    int loaded = 0, failed = 0, skipped = 0;
    for (const auto& addon : addons_) {
        if (!isAddonEnabled(addon.addonName)) {
            LOG_INFO("AddonManager: skipping disabled addon: ", addon.addonName);
            skipped++;
            continue;
        }
        if (loadAddon(addon)) loaded++;
        else failed++;
    }
    addonsLoaded_ = true;
    LOG_INFO("AddonManager: loaded ", loaded, " addons",
             (failed > 0 ? (", " + std::to_string(failed) + " failed") : ""),
             (skipped > 0 ? (", " + std::to_string(skipped) + " disabled") : ""));
}

// ---- Per-addon enable/disable (persisted) ----------------------------------

bool AddonManager::isAddonEnabled(const std::string& addonName) const {
    auto it = addonEnabled_.find(addonName);
    return (it == addonEnabled_.end()) ? true : it->second;  // default: enabled
}

void AddonManager::setAddonEnabled(const std::string& addonName, bool enabled) {
    addonEnabled_[addonName] = enabled;
    saveEnabledState();
}

std::string AddonManager::enabledStatePath() {
    return core::getConfigRoot() + "/addons.cfg";
}

void AddonManager::loadEnabledState() {
    std::ifstream in(enabledStatePath());
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (!name.empty()) addonEnabled_[name] = (val == "1");
    }
}

void AddonManager::saveEnabledState() const {
    std::ofstream out(enabledStatePath(), std::ios::trunc);
    if (!out) {
        LOG_WARNING("AddonManager: could not write ", enabledStatePath());
        return;
    }
    // Persist an explicit line only for addons we actually know about, so stale
    // entries for removed addons don't accumulate.
    for (const auto& addon : addons_) {
        out << addon.addonName << "=" << (isAddonEnabled(addon.addonName) ? "1" : "0") << "\n";
    }
}

std::string AddonManager::getSavedVariablesPath(const TocFile& addon) const {
    return addon.basePath + "/" + addon.addonName + ".lua.saved";
}

std::string AddonManager::getSavedVariablesPerCharacterPath(const TocFile& addon) const {
    if (characterName_.empty()) return "";
    return addon.basePath + "/" + addon.addonName + "." + characterName_ + ".lua.saved";
}

bool AddonManager::loadAddon(const TocFile& addon) {
    // Load SavedVariables before addon code (so globals are available at load time)
    auto savedVars = addon.getSavedVariables();
    if (!savedVars.empty()) {
        std::string svPath = getSavedVariablesPath(addon);
        luaEngine_.loadSavedVariables(svPath);
        LOG_DEBUG("AddonManager: loaded saved variables for '", addon.addonName, "'");
    }
    // Load per-character SavedVariables
    auto savedVarsPC = addon.getSavedVariablesPerCharacter();
    if (!savedVarsPC.empty()) {
        std::string svpcPath = getSavedVariablesPerCharacterPath(addon);
        if (!svpcPath.empty()) {
            luaEngine_.loadSavedVariables(svpcPath);
            LOG_DEBUG("AddonManager: loaded per-character saved variables for '", addon.addonName, "'");
        }
    }

    bool success = true;
    for (const auto& filename : addon.files) {
        std::string lower = filename;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".lua") {
            std::string fullPath = addon.basePath + "/" + filename;
            if (!luaEngine_.executeFile(fullPath)) {
                success = false;
            }
        } else if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".xml") {
            LOG_DEBUG("AddonManager: skipping XML file '", filename,
                      "' in addon '", addon.addonName, "' (XML frames not yet implemented)");
        }
    }

    // Fire ADDON_LOADED event after all addon files are executed
    // This is the standard WoW pattern for addon initialization
    if (success) {
        luaEngine_.fireEvent("ADDON_LOADED", {addon.addonName});
    }
    return success;
}

bool AddonManager::runScript(const std::string& code) {
    return luaEngine_.executeString(code);
}

void AddonManager::fireEvent(const std::string& event, const std::vector<std::string>& args) {
    luaEngine_.fireEvent(event, args);
}

void AddonManager::update(float deltaTime) {
    luaEngine_.dispatchOnUpdate(deltaTime);
}

void AddonManager::saveAllSavedVariables() {
    for (const auto& addon : addons_) {
        auto savedVars = addon.getSavedVariables();
        if (!savedVars.empty()) {
            std::string svPath = getSavedVariablesPath(addon);
            luaEngine_.saveSavedVariables(svPath, savedVars);
        }
        auto savedVarsPC = addon.getSavedVariablesPerCharacter();
        if (!savedVarsPC.empty()) {
            std::string svpcPath = getSavedVariablesPerCharacterPath(addon);
            if (!svpcPath.empty()) {
                luaEngine_.saveSavedVariables(svpcPath, savedVarsPC);
            }
        }
    }
}

bool AddonManager::reload() {
    LOG_INFO("AddonManager: reloading all addons...");
    saveAllSavedVariables();
    addons_.clear();
    luaEngine_.shutdown();

    if (!luaEngine_.initialize()) {
        LOG_ERROR("AddonManager: failed to reinitialize Lua VM during reload");
        return false;
    }
    luaEngine_.setGameHandler(gameHandler_);
    luaEngine_.setLuaServices(luaServices_);

    if (!addonsPath_.empty()) {
        scanAddons(addonsPath_);
        loadAllAddons();
    }
    LOG_INFO("AddonManager: reload complete");
    return true;
}

void AddonManager::shutdown() {
    saveAllSavedVariables();
    addons_.clear();
    luaEngine_.shutdown();
}

} // namespace wowee::addons
