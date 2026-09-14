#pragma once

/// Choosing a folder or a file, through whatever this platform offers.
///
/// macOS and Windows both have an operating-system call for this - NSOpenPanel
/// and IFileDialog - and those are what a person expects to see: their own
/// sidebar, their own recent places, their own keyboard shortcuts. Neither is an
/// external program being run; they are the system's own, linked against.
///
/// Everywhere else there is no such call. The desktop portal is a D-Bus service
/// and the usual answer is to shell out to zenity or kdialog, which is a tool
/// that may not be installed and a control that silently does nothing when it
/// is not. So there is a browser here instead, drawn in the window, which works
/// the same on every platform and needs nothing installed. It is also what
/// answers if the system call fails.

#include <string>
#include <vector>

namespace wowee::assets {

/// What is being chosen.
enum class PickWhat {
    Folder,
    File,
};

/// Whether the system has a chooser of its own to show.
bool haveNativePicker();

/// Put the system's chooser on screen and wait for it. False if the person
/// cancelled, or if there is no such chooser here.
///
/// `startAt` may be empty, or may not exist, in which case it is ignored.
/// `extension` narrows a file pick to one suffix, ".zip" and the like; it is
/// ignored when choosing a folder.
bool pickNative(PickWhat what, const std::string& title, const std::string& startAt,
                const std::string& extension, std::string* chosen);

/// A browser drawn in the window, for platforms with nothing to call.
///
/// Kept open across frames by the caller: `open()` starts it, `draw()` runs one
/// frame of it and answers true on the frame a choice is made.
class Browser {
public:
    void open(PickWhat what, std::string title, const std::string& startAt,
              std::string extension);

    [[nodiscard]] bool isOpen() const { return open_; }

    /// One frame. True once, on the frame the choice is made.
    bool draw(std::string* chosen);

private:
    void listHere();

    struct Entry {
        std::string name;
        bool directory = false;
    };

    bool open_ = false;
    PickWhat what_ = PickWhat::Folder;
    std::string title_;
    std::string extension_;
    std::string here_;
    std::vector<Entry> entries_;
    int selected_ = -1;
    std::string error_;
};

/// One chooser, however this platform provides it: the system's own where there
/// is one, the browser above where there is not. `draw` must be called every
/// frame; it answers true on the frame a choice is made.
class Picker {
public:
    /// Ask for something. Returns straight away having already answered when
    /// the system chooser was used, since that one blocks.
    bool ask(PickWhat what, const std::string& title, const std::string& startAt,
             const std::string& extension, std::string* chosen);

    bool draw(std::string* chosen) { return browser_.draw(chosen); }

private:
    Browser browser_;
};

}  // namespace wowee::assets
