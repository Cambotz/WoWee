#pragma once

/// Reading a Warlords-or-later installation, which does not keep files in
/// archives any more.
///
/// CASC is four indirections deep and none of them is a filename. A path is
/// hashed to a 64-bit number; the root maps that to a content key, the hash of
/// what the file says; encoding maps that to an encoding key, the hash of what
/// the file looks like on disk; and an index maps that to an archive, an offset
/// and a length. What is there is a BLTE container, which is chunked and mostly
/// deflated.
///
/// So a path cannot be listed out of an install - the names simply are not in
/// there - but a path that is already known can be asked for. The texture paths
/// inside an M2 are names already known, which is what makes importing a model
/// from one of these possible at all.
///
/// This is the C++ of tools/casc_extract.py, so that the program that needs it
/// does not need a Python with the right modules to be installed first.

#include <array>
#include <fstream>
#include <memory>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace wowee::assets {

using ContentKey = std::array<uint8_t, 16>;
using IndexKey = std::array<uint8_t, 9>;   ///< an encoding key, truncated

struct KeyHash {
    template <std::size_t N>
    std::size_t operator()(const std::array<uint8_t, N>& key) const noexcept {
        // The keys are already hashes, so the first eight bytes are as good a
        // spread as anything this could do to them.
        std::size_t out = 0;
        for (std::size_t i = 0; i < 8 && i < N; ++i) {
            out = (out << 8) | key[i];
        }
        return out;
    }
};

/// Where one file's bytes are.
struct IndexEntry {
    uint32_t archive = 0;
    uint64_t offset = 0;
    uint32_t size = 0;
};

/// What the root says about one file id.
struct RootEntry {
    ContentKey ckey{};
    uint64_t nameHash = 0;
};

/// Bob Jenkins' hashlittle2, which is how CASC names a file.
///


class CascStorage {
public:
    /// Returns false and fills `error` rather than throwing: this is opened in
    /// response to somebody choosing a folder, and choosing the wrong folder is
    /// an ordinary thing to do.
    bool open(const std::string& installDir, std::string* error);

    [[nodiscard]] bool isOpen() const { return open_; }
    [[nodiscard]] std::size_t indexCount() const { return index_.size(); }
    [[nodiscard]] std::size_t encodingCount() const { return encoding_.size(); }
    [[nodiscard]] std::size_t rootCount() const { return root_.size(); }

    /// The bytes of a file, by the path it had when it had one.
    std::vector<uint8_t> readPath(const std::string& path, std::size_t limit = 0);
    std::vector<uint8_t> readFileId(uint32_t fileId, std::size_t limit = 0);

    [[nodiscard]] const std::map<uint32_t, RootEntry>& root() const { return root_; }

private:
    std::vector<uint8_t> readCKey(const ContentKey& ckey, std::size_t limit);
    std::vector<uint8_t> readEKey(const ContentKey& ekey, std::size_t limit);
    bool loadIndices(const std::string& dataDir, std::string* error);
    bool readIndexFile(const std::string& path);

    bool open_ = false;
    std::string dataDir_;
    /// One open handle per archive, kept.
    ///
    /// Opening and seeking a file per read costs more than the read: a survey
    /// of a whole installation is three quarters of a million of them, and
    /// with a handle per call it never finishes.
    std::map<uint32_t, std::shared_ptr<std::ifstream>> archives_;
    std::map<std::string, std::vector<std::string>> config_;
    std::unordered_map<IndexKey, IndexEntry, KeyHash> index_;
    std::unordered_map<ContentKey, ContentKey, KeyHash> encoding_;
    std::map<uint32_t, RootEntry> root_;
    std::unordered_map<uint64_t, uint32_t> byNameHash_;
};

}  // namespace wowee::assets
