/// The two kinds of installation a model can be brought out of.

#include "model_import.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>

#include <StormLib.h>

#include "../asset_extract/extractor.hpp"
#include "casc.hpp"

namespace wowee::assets {
namespace {

class CascModelSource final : public ModelSource {
public:
    explicit CascModelSource(CascStorage& storage) : storage_(storage) {}

    std::vector<uint8_t> read(const std::string& path, std::size_t limit) override {
        return storage_.readPath(path, limit);
    }
    std::vector<uint8_t> readId(uint32_t fileId) override {
        return storage_.readFileId(fileId);
    }

private:
    CascStorage& storage_;
};

class MpqModelSource final : public ModelSource {
public:
    ~MpqModelSource() override {
        for (HANDLE handle : handles_) SFileCloseArchive(handle);
    }

    bool open(const std::string& mpqDir, const std::string& expansion, std::string* error) {
        const std::vector<std::string> chain =
            tools::Extractor::archiveChain(mpqDir, expansion, "");
        for (const std::string& path : chain) {
            HANDLE handle = nullptr;
            if (SFileOpenArchive(path.c_str(), 0, 0, &handle)) handles_.push_back(handle);
        }
        if (handles_.empty()) {
            if (error != nullptr) {
                *error = chain.empty() ? "no MPQ archives found in " + mpqDir
                                       : "none of the MPQ archives in " + mpqDir + " would open";
            }
            return false;
        }
        return true;
    }

    std::vector<uint8_t> read(const std::string& path, std::size_t limit) override {
        // MPQ names use backslashes, and StormLib is not thread-safe even
        // across separate handles, so every read is serialised.
        std::string name = path;
        std::replace(name.begin(), name.end(), '/', '\\');
        std::lock_guard<std::mutex> lock(mutex_);

        // Later archives in the chain are patches, so the last one that has the
        // file is the one that wins.
        for (auto it = handles_.rbegin(); it != handles_.rend(); ++it) {
            HANDLE file = nullptr;
            if (!SFileOpenFileEx(*it, name.c_str(), 0, &file)) continue;
            const DWORD size = SFileGetFileSize(file, nullptr);
            if (size == 0 || size == SFILE_INVALID_SIZE) {
                SFileCloseFile(file);
                continue;
            }
            const DWORD want = limit > 0 ? std::min<DWORD>(size, DWORD(limit)) : size;
            std::vector<uint8_t> data(want);
            DWORD got = 0;
            const bool ok = SFileReadFile(file, data.data(), want, &got, nullptr) || got == want;
            SFileCloseFile(file);
            if (!ok || got == 0) continue;
            data.resize(got);
            return data;
        }
        return {};
    }

private:
    std::vector<HANDLE> handles_;
    std::mutex mutex_;
};

}  // namespace

std::unique_ptr<ModelSource> cascSource(CascStorage& storage) {
    return std::make_unique<CascModelSource>(storage);
}

std::unique_ptr<ModelSource> mpqSource(const std::string& mpqDir,
                                       const std::string& expansion,
                                       std::string* error) {
    auto source = std::make_unique<MpqModelSource>();
    if (!source->open(mpqDir, expansion, error)) return nullptr;
    return source;
}

}  // namespace wowee::assets
