/// The Windows folder chooser: the shell's own dialog, through COM.

#include "folder_picker.hpp"

#include <windows.h>
#include <shlobj.h>

#include <string>

namespace wowee::assets {
namespace {

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int need = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (need <= 0) return {};
    std::wstring out(static_cast<std::size_t>(need - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), need);
    return out;
}

std::string narrow(const wchar_t* wide) {
    if (wide == nullptr) return {};
    const int need = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(static_cast<std::size_t>(need - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), need, nullptr, nullptr);
    return out;
}

}  // namespace

bool haveNativePicker() { return true; }

bool pickNative(PickWhat what, const std::string& title, const std::string& startAt,
                const std::string& extension, std::string* chosen) {
    // Apartment-threaded, and tolerant of already having been initialised: SDL
    // brings COM up for its own reasons and returning here would be refusing to
    // open a dialog because something else already did.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool weInitialised = SUCCEEDED(init);

    bool picked = false;
    IFileDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
        if (what == PickWhat::Folder) options |= FOS_PICKFOLDERS;
        dialog->SetOptions(options);
        const std::wstring caption = widen(title);
        if (!caption.empty()) dialog->SetTitle(caption.c_str());

        std::wstring pattern;
        COMDLG_FILTERSPEC filter{};
        if (what == PickWhat::File && !extension.empty()) {
            pattern = L"*" + widen(extension);
            filter.pszName = L"Asset packs";
            filter.pszSpec = pattern.c_str();
            dialog->SetFileTypes(1, &filter);
        }

        if (!startAt.empty()) {
            IShellItem* start = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(widen(startAt).c_str(), nullptr,
                                                      IID_PPV_ARGS(&start)))) {
                dialog->SetFolder(start);
                start->Release();
            }
        }

        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    if (chosen != nullptr) *chosen = narrow(path);
                    picked = true;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    if (weInitialised) CoUninitialize();
    return picked;
}

}  // namespace wowee::assets
