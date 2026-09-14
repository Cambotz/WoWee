/// The parts of choosing that are the same everywhere: the in-window browser,
/// and deciding which chooser to use.

#include "folder_picker.hpp"

#include <algorithm>
#include <filesystem>
#include <cstdlib>

#include "imgui.h"

namespace wowee::assets {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

/// Somewhere that exists, to start at when what was asked for does not.
std::string somewhereReal(const std::string& wanted) {
    std::error_code ec;
    if (!wanted.empty() && fs::is_directory(wanted, ec)) return wanted;
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') return home;
    if (const char* profile = std::getenv("USERPROFILE"); profile != nullptr && *profile != '\0') {
        return profile;
    }
    return fs::current_path(ec).string();
}

}  // namespace

void Browser::open(PickWhat what, std::string title, const std::string& startAt,
                   std::string extension) {
    what_ = what;
    title_ = std::move(title);
    extension_ = lower(std::move(extension));
    here_ = somewhereReal(startAt);
    selected_ = -1;
    open_ = true;
    listHere();
}

void Browser::listHere() {
    entries_.clear();
    error_.clear();
    selected_ = -1;

    std::error_code ec;
    for (fs::directory_iterator it(here_, ec), end; it != end && !ec; it.increment(ec)) {
        const bool directory = it->is_directory(ec);
        std::string name = it->path().filename().string();
        // Dot files are almost never what is being looked for here and there
        // are a great many of them in a home directory.
        if (!name.empty() && name[0] == '.') continue;
        if (!directory) {
            if (what_ == PickWhat::Folder) continue;
            if (!extension_.empty() && lower(it->path().extension().string()) != extension_) {
                continue;
            }
        }
        entries_.push_back({std::move(name), directory});
    }
    if (ec) error_ = "Cannot read this folder.";

    // Folders first, then names, so the shape of the place is readable.
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        if (a.directory != b.directory) return a.directory;
        return lower(a.name) < lower(b.name);
    });
}

bool Browser::draw(std::string* chosen) {
    if (!open_) return false;

    bool decided = false;
    ImGui::OpenPopup(title_.c_str());

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.8f, viewport->WorkSize.y * 0.7f),
                             ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(title_.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted(here_.c_str());

        const fs::path up = fs::path(here_).parent_path();
        ImGui::BeginDisabled(up.empty() || up == fs::path(here_));
        if (ImGui::Button("Up")) {
            here_ = up.string();
            listHere();
        }
        ImGui::EndDisabled();

        if (!error_.empty()) {
            ImGui::SameLine();
            ImGui::TextUnformatted(error_.c_str());
        }

        const float buttons = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        if (ImGui::BeginChild("entries", ImVec2(0.0f, -buttons), ImGuiChildFlags_Borders)) {
            for (int i = 0; i < int(entries_.size()); ++i) {
                const Entry& entry = entries_[i];
                const std::string label = entry.directory ? entry.name + "/" : entry.name;
                if (ImGui::Selectable(label.c_str(), selected_ == i,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    selected_ = i;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (entry.directory) {
                            here_ = (fs::path(here_) / entry.name).string();
                            listHere();
                            break;
                        }
                        if (chosen != nullptr) {
                            *chosen = (fs::path(here_) / entry.name).string();
                        }
                        decided = true;
                    }
                }
            }
        }
        ImGui::EndChild();

        // A folder is chosen by being in it, which is how every system chooser
        // does it too: there is no selecting the folder you are looking at from
        // a list that does not contain it.
        const bool canChoose =
            what_ == PickWhat::Folder ||
            (selected_ >= 0 && selected_ < int(entries_.size()) && !entries_[selected_].directory);

        ImGui::BeginDisabled(!canChoose);
        const char* verb = what_ == PickWhat::Folder ? "Use this folder" : "Open";
        if (ImGui::Button(verb) && canChoose) {
            if (chosen != nullptr) {
                *chosen = what_ == PickWhat::Folder
                              ? here_
                              : (fs::path(here_) / entries_[selected_].name).string();
            }
            decided = true;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            open_ = false;
            ImGui::CloseCurrentPopup();
        }

        if (decided) {
            open_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return decided;
}

bool Picker::ask(PickWhat what, const std::string& title, const std::string& startAt,
                 const std::string& extension, std::string* chosen) {
    if (haveNativePicker() && pickNative(what, title, startAt, extension, chosen)) {
        return true;
    }
    // Either there is no system chooser here, or it was cancelled. Cancelling is
    // not a reason to open a second one, so the browser is only reached where
    // there was nothing to cancel.
    if (!haveNativePicker()) {
        browser_.open(what, title, startAt, extension);
    }
    return false;
}

}  // namespace wowee::assets
