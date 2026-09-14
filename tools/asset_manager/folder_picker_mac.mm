/// The macOS chooser: AppKit's own panel, not a program being run.

#include "folder_picker.hpp"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <filesystem>

namespace wowee::assets {

bool haveNativePicker() { return true; }

bool pickNative(PickWhat what, const std::string& title, const std::string& startAt,
                const std::string& extension, std::string* chosen) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        const bool wantFolder = what == PickWhat::Folder;
        panel.canChooseDirectories = wantFolder ? YES : NO;
        panel.canChooseFiles = wantFolder ? NO : YES;
        panel.allowsMultipleSelection = NO;
        panel.canCreateDirectories = wantFolder ? YES : NO;
        panel.message = [NSString stringWithUTF8String:title.c_str()];
        panel.prompt = wantFolder ? @"Choose" : @"Open";

        if (!wantFolder && !extension.empty()) {
            // Without the leading dot, which is how a type is named here.
            NSString* suffix = [NSString stringWithUTF8String:extension.c_str()];
            if ([suffix hasPrefix:@"."]) suffix = [suffix substringFromIndex:1];
            if (UTType* type = [UTType typeWithFilenameExtension:suffix]; type != nil) {
                panel.allowedContentTypes = @[type];
            }
        }

        std::error_code ec;
        std::string start = startAt;
        // A path that names a file starts the panel in the folder holding it,
        // which is what somebody means by handing one over.
        if (!start.empty() && !std::filesystem::is_directory(start, ec)) {
            start = std::filesystem::path(start).parent_path().string();
        }
        if (!start.empty() && std::filesystem::is_directory(start, ec)) {
            panel.directoryURL =
                [NSURL fileURLWithPath:[NSString stringWithUTF8String:start.c_str()]
                           isDirectory:YES];
        }

        // Run from a terminal this process is not the active application, and
        // the panel opens behind whatever is. It is modal, so a panel nobody can
        // see is a window that has stopped responding.
        [NSApp activateIgnoringOtherApps:YES];

        if ([panel runModal] != NSModalResponseOK) return false;
        NSURL* url = panel.URLs.firstObject;
        if (url == nil) return false;
        if (chosen != nullptr) *chosen = url.path.UTF8String;
        return true;
    }
}

}  // namespace wowee::assets
