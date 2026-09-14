/// WoWee Asset Manager - a native window for building this client's assets.
///
///     wowee_assets
///
/// Three questions in the order somebody actually has answers for: where is the
/// game, what do you want it to look like, and go.
///
/// This is a C++ program because the thing it replaced was not: a Tkinter window
/// driving shell scripts that drove Python scripts, each needing an interpreter
/// with the right modules built into it. On one machine `python3` had no Tkinter
/// and the window simply did not appear - a perfectly good interpreter sat one
/// directory away and nothing said so. A tool that installs the game's assets
/// should not itself need an install.
///
/// It uses SDL2 and Dear ImGui, both of which the client already carries, and
/// calls Extractor::run in this process. Nothing is shelled out to.

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include "core/local_time.hpp"

#include "casc.hpp"
#include "pack.hpp"
#include "install_scan.hpp"
#include "job.hpp"
#include "profiles.hpp"

namespace {

namespace fs = std::filesystem;
using namespace wowee::assets;

/// Everything the window remembers between frames.
struct App {
    char gameDir[1024] = {0};
    char secondDir[1024] = {0};
    char outputDir[1024] = {0};

    InstallScan gameScan;
    InstallScan secondScan;
    std::string selected = "wotlk";

    Job job;
    bool started = false;
    bool cascConfirmed = false;
    std::string cascError;

    // Keeping a copy of what was built. Offered once a build finishes rather
    // than assumed, and available at any time from the button.
    std::atomic<bool> packing{false};
    std::atomic<bool> packCancel{false};
    std::string packNote;
    std::mutex packMutex;
    bool offeredSave = false;
};

void wrapped(const char* text) {
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}

void dimmed(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.62f, 0.62f, 1.0f));
    wrapped(text);
    ImGui::PopStyleColor();
}

void rescan(App& app) {
    app.gameScan = scanInstall(app.gameDir);
    app.secondScan = scanInstall(app.secondDir);
    app.cascConfirmed = false;
    app.cascError.clear();
}

/// Actually open a CASC install, on demand.
///
/// Not while somebody is typing: reading its encoding table is around a second
/// of work and this happens on the frame a button is pressed, not on the frame
/// a character is entered.
void confirmSecond(App& app) {
    if (app.secondScan.kind != InstallKind::Casc) return;
    app.cascConfirmed = confirmCasc(app.secondScan, &app.cascError);
}

bool haveGame(const App& app)   { return app.gameScan.kind == InstallKind::Mpq; }
bool haveBorrow(const App& app) { return app.secondScan.kind == InstallKind::Mpq; }
bool haveLater(const App& app)  { return app.secondScan.kind == InstallKind::Casc; }

void drawSources(App& app) {
    ImGui::SeparatorText("1.  Where is the game?");
    ImGui::SetNextItemWidth(-140.0f);
    if (ImGui::InputText("Game folder", app.gameDir, sizeof(app.gameDir))) rescan(app);
    if (!app.gameScan.note.empty()) {
        const bool good = haveGame(app);
        ImGui::PushStyleColor(ImGuiCol_Text, good ? ImVec4(0.35f, 0.78f, 0.45f, 1.0f)
                                                  : ImVec4(0.85f, 0.65f, 0.30f, 1.0f));
        wrapped(app.gameScan.note.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-140.0f);
    if (ImGui::InputText("A later client (optional)", app.secondDir, sizeof(app.secondDir))) {
        rescan(app);
    }
    dimmed("Own a newer World of Warcraft? Point at it and this can borrow art from it.");
    if (!app.secondScan.note.empty()) dimmed(app.secondScan.note.c_str());
    if (app.secondScan.kind == InstallKind::Casc && !app.cascConfirmed) {
        if (ImGui::Button("Check it")) confirmSecond(app);
        ImGui::SameLine();
        dimmed("Reads its file table - about a second.");
    }
    if (!app.cascError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.42f, 0.38f, 1.0f));
        wrapped(app.cascError.c_str());
        ImGui::PopStyleColor();
    }
}

void drawProfiles(App& app) {
    ImGui::SeparatorText("2.  What do you want?");
    for (const Profile& profile : profiles()) {
        std::string why;
        const bool ok = profileAvailable(profile, haveGame(app), haveBorrow(app),
                                         haveLater(app), &why);
        ImGui::BeginDisabled(!ok);
        if (ImGui::RadioButton(profile.name.c_str(), app.selected == profile.id)) {
            app.selected = profile.id;
        }
        ImGui::EndDisabled();

        ImGui::Indent();
        dimmed(ok ? profile.summary.c_str() : (profile.summary + "   (" + why + ")").c_str());
        ImGui::Unindent();
    }
}

void drawPlan(App& app) {
    const Profile* profile = profileById(app.selected);
    if (profile == nullptr) return;

    ImGui::SeparatorText("3.  What will happen");
    wrapped(profile->detail.c_str());
    ImGui::Spacing();

    const std::vector<JobStage> planned =
        Job::plan(*profile, haveBorrow(app), haveLater(app));
    for (std::size_t i = 0; i < planned.size(); ++i) {
        const JobStage& stage = planned[i];
        std::string line = "  " + std::to_string(i + 1) + ". " + stage.label;
        if (stage.skipped) line += "   - skipped, " + stage.skipReason;
        if (stage.skipped) dimmed(line.c_str()); else wrapped(line.c_str());
    }
    ImGui::Spacing();
    dimmed(("About " + std::to_string(profile->minutes()) +
            " minutes. Nothing is written into your game install.").c_str());
}

/// Keep a copy of what was built, as one file somebody else could install.
void startPack(App& app, const std::string& profileId) {
    std::string out = app.outputDir;
    if (out.empty()) out = (fs::current_path() / "Data").string();

    const std::tm local = wowee::core::localTime(std::time(nullptr));
    char stamp[16];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d", &local);
    const std::string dest =
        (fs::path(out).parent_path() / ("wowee-" + profileId + "-" + stamp + ".zip")).string();

    app.packing.store(true);
    app.packCancel.store(false);
    {
        std::lock_guard<std::mutex> lock(app.packMutex);
        app.packNote = "Packing...";
    }
    std::thread([&app, out, dest, profileId]() {
        PackResult result = writePack(
            out, dest, profileId,
            [&app](std::size_t done, std::size_t total) {
                std::lock_guard<std::mutex> lock(app.packMutex);
                app.packNote = "Packing " + std::to_string(done) + " of " +
                               std::to_string(total) + " files...";
            },
            app.packCancel);
        std::lock_guard<std::mutex> lock(app.packMutex);
        if (!result.ok) {
            app.packNote = "Could not save: " + result.error;
        } else {
            const double raw = double(result.rawBytes) / (1024.0 * 1024.0 * 1024.0);
            const double packed = double(result.packedBytes) / (1024.0 * 1024.0 * 1024.0);
            char line[512];
            std::snprintf(line, sizeof(line),
                          "Saved %zu files - %.1f GB of assets in a %.1f GB file. %s",
                          result.files, raw, packed, dest.c_str());
            app.packNote = line;
        }
        app.packing.store(false);
    }).detach();
}

void drawRun(App& app) {
    const Profile* profile = profileById(app.selected);
    if (profile == nullptr) return;

    std::string why;
    const bool ok = profileAvailable(*profile, haveGame(app), haveBorrow(app),
                                     haveLater(app), &why);

    ImGui::Separator();
    ImGui::BeginDisabled(!ok || app.job.running());
    if (ImGui::Button("Build my assets", ImVec2(180, 32))) {
        std::string out = app.outputDir;
        if (out.empty()) out = (fs::current_path() / "Data").string();
        app.job.start(*profile, app.gameDir, app.secondDir, out,
                      haveBorrow(app), haveLater(app));
        app.started = true;
    }
    ImGui::EndDisabled();

    if (app.job.running()) {
        ImGui::SameLine();
        if (ImGui::Button("Stop", ImVec2(90, 32))) app.job.cancel();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(app.job.running() || app.packing.load());
    if (ImGui::Button("Save what I have as a pack", ImVec2(240, 32))) {
        startPack(app, profile->id);
    }
    ImGui::EndDisabled();
    {
        std::lock_guard<std::mutex> lock(app.packMutex);
        if (!app.packNote.empty()) dimmed(app.packNote.c_str());
    }

    if (app.started) {
        ImGui::Spacing();
        ImGui::ProgressBar(app.job.progress(), ImVec2(-1, 0));
        const std::string label = app.job.currentLabel();
        if (!label.empty()) wrapped(label.c_str());
        else if (app.job.finished()) {
            wrapped(app.job.succeeded() ? "Done - your assets are ready."
                                        : "Finished, but some steps did not complete.");
        }

        ImGui::Spacing();
        if (ImGui::BeginChild("log", ImVec2(0, 200), ImGuiChildFlags_Borders)) {
            for (const std::string& line : app.job.log()) {
                ImGui::TextUnformatted(line.c_str());
            }
            if (app.job.running()) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
}

}  // namespace

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL could not start: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "WoWee Asset Manager", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        900, 760, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr) {
        std::fprintf(stderr, "Could not open a window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr) {
        // Software is slower and perfectly adequate for a form with a log in it.
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (renderer == nullptr) {
        std::fprintf(stderr, "Could not draw: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    App app;
    const std::string defaultOut = (fs::current_path() / "Data").string();
    std::snprintf(app.outputDir, sizeof(app.outputDir), "%s", defaultOut.c_str());
    rescan(app);

    bool quit = false;
    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) quit = true;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window)) {
                quit = true;
            }
            if (event.type == SDL_DROPFILE && event.drop.file != nullptr) {
                std::snprintf(app.gameDir, sizeof(app.gameDir), "%s", event.drop.file);
                SDL_free(event.drop.file);
                rescan(app);
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("WoWee Asset Manager", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

        wrapped("Point this at a World of Warcraft folder you own, choose how you want "
                "the game to look, and press Build. Nothing is written into the game "
                "install - the assets are copied out into a tree of their own.");
        ImGui::Spacing();

        drawSources(app);
        ImGui::Spacing();
        drawProfiles(app);
        ImGui::Spacing();
        drawPlan(app);
        ImGui::Spacing();
        drawRun(app);

        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    app.job.cancel();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
