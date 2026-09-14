/// WoWee Asset Manager - a native window for building this client's assets.
///
///     wowee_assets [game folder] [a later client]
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
#include <cstdlib>
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

// The implementation is already compiled into open_format_emitter.cpp.
#include "stb_image_write.h"

#include "core/local_time.hpp"
#include "ui/imgui_theme.hpp"

#include "casc.hpp"
#include "pack.hpp"
#include "install_scan.hpp"
#include "job.hpp"
#include "profiles.hpp"

namespace {

namespace fs = std::filesystem;
using namespace wowee::assets;

/// ImGui's built-in face is drawn at thirteen pixels. Everything here was laid
/// out against that, so it is the height the atlas is scaled from rather than a
/// size worth changing.
constexpr float kBaseFontSize = 13.0f;

/// FRIZQT draws smaller than ImGui's built-in face at the same nominal height,
/// so asking for thirteen of it puts noticeably smaller text into controls
/// sized for thirteen. This is the height that matches.
constexpr float kFrizqtRatio = 1.25f;

/// The log pane, once a build is running.
constexpr float kLogHeight = 200.0f;

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
    /// Mutable so a const read of the note can still take the lock: the note is
    /// written from the packing thread and read while laying the window out.
    mutable std::mutex packMutex;
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
    // Labels above rather than beside, because the widths here are set by a
    // typeface that is not the one the layout was measured against: a label
    // reserved room beside its field is a label clipped by the window edge in
    // whichever of the two is wider.
    ImGui::TextUnformatted("Game folder");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##gameDir", app.gameDir, sizeof(app.gameDir))) rescan(app);
    if (!app.gameScan.note.empty()) {
        const bool good = haveGame(app);
        ImGui::PushStyleColor(ImGuiCol_Text, good ? ImVec4(0.35f, 0.78f, 0.45f, 1.0f)
                                                  : ImVec4(0.85f, 0.65f, 0.30f, 1.0f));
        wrapped(app.gameScan.note.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("A later client (optional)");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##secondDir", app.secondDir, sizeof(app.secondDir))) {
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

    // Without a game folder nothing here can be built, and every profile says
    // so for the same reason. Said once it is an instruction; said eight times
    // it is the loudest thing on the page and buries what the profiles differ
    // by, which is the choice actually being made.
    const bool anyGame = haveGame(app);
    if (!anyGame) {
        dimmed("Point at your game folder above, and these become available.");
    }

    for (const Profile& profile : profiles()) {
        std::string why;
        const bool ok = profileAvailable(profile, anyGame, haveBorrow(app),
                                         haveLater(app), &why);
        ImGui::BeginDisabled(!ok);
        if (ImGui::RadioButton(profile.name.c_str(), app.selected == profile.id)) {
            app.selected = profile.id;
        }
        ImGui::EndDisabled();

        ImGui::Indent();
        const bool sayWhy = !ok && anyGame;
        dimmed(sayWhy ? (profile.summary + "   (" + why + ")").c_str()
                      : profile.summary.c_str());
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

/// How much of the window the actions need reserved at the bottom.
///
/// The button row always, and the progress bar and log once there is something
/// to watch - which is the moment the log matters most and the questions above
/// matter least.
float actionsHeight(const App& app) {
    const ImGuiStyle& style = ImGui::GetStyle();
    float height = 32.0f + style.ItemSpacing.y * 2.0f + style.FramePadding.y * 2.0f;
    {
        std::lock_guard<std::mutex> lock(app.packMutex);
        if (!app.packNote.empty()) height += ImGui::GetTextLineHeightWithSpacing();
    }
    if (app.started) {
        height += ImGui::GetFrameHeightWithSpacing();        // the progress bar
        height += ImGui::GetTextLineHeightWithSpacing();     // what it is doing
        height += kLogHeight + style.ItemSpacing.y * 2.0f;
    }
    return height;
}

void drawRun(App& app) {
    const Profile* profile = profileById(app.selected);
    if (profile == nullptr) return;

    std::string why;
    const bool ok = profileAvailable(*profile, haveGame(app), haveBorrow(app),
                                     haveLater(app), &why);

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
        if (ImGui::BeginChild("log", ImVec2(0, kLogHeight), ImGuiChildFlags_Borders)) {
            for (const std::string& line : app.job.log()) {
                ImGui::TextUnformatted(line.c_str());
            }
            if (app.job.running()) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
}

/// How many frames to let settle before the picture is taken. ImGui sizes a
/// good deal of its layout from what it measured last frame, so the first one
/// is not what the window looks like.
constexpr int kShotFrame = 3;

/// The renderer's own pixels, as a PNG.
void writeScreenshot(SDL_Renderer* renderer, const char* path) {
    int width = 0;
    int height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) return;
    std::vector<uint8_t> pixels(std::size_t(width) * std::size_t(height) * 4);
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ABGR8888,
                             pixels.data(), width * 4) != 0) {
        std::fprintf(stderr, "could not read the window back: %s\n", SDL_GetError());
        return;
    }
    if (stbi_write_png(path, width, height, 4, pixels.data(), width * 4) == 0) {
        std::fprintf(stderr, "could not write %s\n", path);
        return;
    }
    std::printf("wrote %s (%dx%d)\n", path, width, height);
}

/// The game's own typeface, if any extraction on this machine has produced it.
///
/// A window that installs the game's assets should look like the game, and the
/// face is most of that. It is looked for where the client itself reads from
/// and beside whatever this window is writing to - which covers the common case
/// of running this a second time, to add something to a tree already built.
/// Before any extraction there is nothing to find, and ImGui's built-in face is
/// what the layout was measured against anyway.
fs::path gameFont(const std::string& outputDir, const char* name) {
    std::vector<fs::path> roots;
    if (!outputDir.empty()) roots.emplace_back(outputDir);
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        roots.emplace_back(fs::path(home) / "Library/Application Support/Wowee/Data");
    }

    std::error_code ec;
    for (const fs::path& root : roots) {
        // Directly under the root for a single-expansion tree, and one level
        // into expansions/ for the layout the profiles write.
        std::vector<fs::path> here{root};
        const fs::path expansions = root / "expansions";
        for (fs::directory_iterator it(expansions, ec), end; it != end && !ec; it.increment(ec)) {
            here.push_back(it->path());
        }
        for (const fs::path& at : here) {
            for (const char* dir : {"fonts", "misc/fonts"}) {
                const fs::path file = at / dir / name;
                if (fs::is_regular_file(file, ec)) return file;
            }
        }
    }
    return {};
}

/// How many pixels the renderer puts down for each point the window is measured
/// in. One on an ordinary display, two on a Retina one.
float displayScale(SDL_Window* window, SDL_Renderer* renderer) {
    int windowWidth = 0;
    int pixelWidth = 0;
    SDL_GetWindowSize(window, &windowWidth, nullptr);
    if (SDL_GetRendererOutputSize(renderer, &pixelWidth, nullptr) != 0) return 1.0f;
    if (windowWidth <= 0 || pixelWidth <= 0) return 1.0f;
    return std::max(1.0f, float(pixelWidth) / float(windowWidth));
}

}  // namespace

int main(int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL could not start: %s\n", SDL_GetError());
        return 1;
    }

    // As much of the page as the display will show at once, since what is not
    // shown has to be scrolled past. Clamped to the space the desktop actually
    // leaves, so a laptop does not open a window taller than its screen.
    int wide = 980;
    int high = 900;
    if (SDL_Rect usable; SDL_GetDisplayUsableBounds(0, &usable) == 0) {
        wide = std::min(wide, std::max(640, usable.w - 80));
        high = std::min(high, std::max(520, usable.h - 80));
    }

    SDL_Window* window = SDL_CreateWindow(
        "WoWee Asset Manager", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        wide, high, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
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
    wowee::ui::applyWoweeStyle(ImGui::GetStyle());

    // On a Retina display the window is 900 points wide and the thing drawn
    // into is 1800 pixels. SDL_Renderer does not bridge that on its own, so
    // without this the interface is drawn at point coordinates into a pixel
    // buffer: half size, in the top-left quarter of its own window, with the
    // rest left empty.
    //
    // The renderer is told to scale, which puts the layout back where it
    // belongs. The font atlas is then built at the pixel size it will actually
    // occupy and laid out as though it were half that, so the glyphs land one
    // texel to a pixel rather than being magnified along with everything else.
    const float scale = displayScale(window, renderer);
    ImGuiIO& io = ImGui::GetIO();

    App app;
    const std::string defaultOut = (fs::current_path() / "Data").string();
    std::snprintf(app.outputDir, sizeof(app.outputDir), "%s", defaultOut.c_str());

    // The same two folders the window takes by drag and drop, for anyone who
    // already has the paths in a terminal.
    if (argc > 1) std::snprintf(app.gameDir, sizeof(app.gameDir), "%s", argv[1]);
    if (argc > 2) std::snprintf(app.secondDir, sizeof(app.secondDir), "%s", argv[2]);

    // FRIZQT is what the game writes its interface in. A little larger than
    // ImGui's built-in face at the same nominal height, so it is asked for at a
    // size that keeps the controls around it the size they were laid out at.
    const fs::path face = gameFont(app.outputDir, "frizqt__.ttf");
    if (!face.empty()) {
        ImFontConfig config;
        config.SizePixels = kBaseFontSize * kFrizqtRatio * scale;
        io.Fonts->AddFontFromFileTTF(face.string().c_str(), config.SizePixels);
    } else if (scale > 1.0f) {
        ImFontConfig config;
        config.SizePixels = kBaseFontSize * scale;
        io.Fonts->AddFontDefault(&config);
    }
    io.FontGlobalScale = 1.0f / scale;

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    rescan(app);

    const char* shotPath = std::getenv("WOWEE_ASSETS_SCREENSHOT");
    int frames = 0;

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

        // The questions scroll; the button that answers them does not. With
        // everything in one scrolling page, adding a line anywhere above pushed
        // Build off the bottom of the window - the one control the whole
        // program exists to offer, reachable only by scrolling past the reading.
        const float actionsHigh = actionsHeight(app);
        if (ImGui::BeginChild("questions", ImVec2(0.0f, -actionsHigh))) {
            drawSources(app);
            ImGui::Spacing();
            drawProfiles(app);
            ImGui::Spacing();
            drawPlan(app);
        }
        ImGui::EndChild();

        ImGui::Separator();
        drawRun(app);

        ImGui::End();

        ImGui::Render();
        // Re-read every frame: a window dragged between a Retina display and an
        // ordinary one changes this without resizing.
        const float now = displayScale(window, renderer);
        SDL_RenderSetScale(renderer, now, now);
        SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);

        // A picture of the window, written once and then done with. What this
        // looks like cannot be checked by reading it, and a bug report about a
        // window is a screenshot; this is how one gets taken on a machine where
        // the person hitting the problem cannot easily take one.
        //
        // Read before the frame is presented, not after: presenting leaves the
        // buffer it came from undefined, and on Metal reading it back then
        // gives a blank image rather than an error.
        if (shotPath != nullptr && ++frames == kShotFrame) {
            writeScreenshot(renderer, shotPath);
            quit = true;
        }

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
