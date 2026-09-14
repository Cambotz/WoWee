#include "casc.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace wowee::assets {
namespace {

namespace fs = std::filesystem;


uint32_t readBE32(const uint8_t* at) {
    return (uint32_t(at[0]) << 24) | (uint32_t(at[1]) << 16) |
           (uint32_t(at[2]) << 8) | uint32_t(at[3]);
}

uint16_t readBE16(const uint8_t* at) {
    return static_cast<uint16_t>((uint32_t(at[0]) << 8) | uint32_t(at[1]));
}

uint32_t readLE32(const uint8_t* at) {
    return uint32_t(at[0]) | (uint32_t(at[1]) << 8) |
           (uint32_t(at[2]) << 16) | (uint32_t(at[3]) << 24);
}

uint64_t readLE64(const uint8_t* at) {
    uint64_t out = 0;
    for (int i = 7; i >= 0; --i) out = (out << 8) | at[i];
    return out;
}

std::vector<uint8_t> slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool fromHex(const std::string& text, ContentKey& out) {
    if (text.size() < out.size() * 2) return false;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = nibble(text[i * 2]);
        const int lo = nibble(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

uint32_t rot(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

/// zlib inflate over one chunk.
bool inflateBlock(const uint8_t* data, std::size_t size, std::vector<uint8_t>& out) {
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) return false;
    stream.next_in = const_cast<Bytef*>(data);
    stream.avail_in = static_cast<uInt>(size);

    std::vector<uint8_t> buffer(64 * 1024);
    int status = Z_OK;
    do {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
            inflateEnd(&stream);
            return false;
        }
        out.insert(out.end(), buffer.data(), buffer.data() + (buffer.size() - stream.avail_out));
        if (status == Z_BUF_ERROR && stream.avail_in == 0) break;
    } while (status != Z_STREAM_END);
    inflateEnd(&stream);
    return true;
}

bool decodeChunk(const uint8_t* chunk, std::size_t size, std::vector<uint8_t>& out,
                 std::string* error);

}  // namespace

/// Bob Jenkins' hashlittle2, which is how CASC names a file. The path is
/// uppercased and its separators turned to backslashes first, because that is
/// the form the hash was taken over.
uint64_t jenkins96(const std::string& path);

/// Decode a BLTE container. `limit` stops once that many bytes are out, which
/// turns identifying a file - a few hundred bytes of it - from a multi-megabyte
/// decompression into a small one. Declared here because a BLTE chunk can hold
/// another one, so the decoder calls itself.
std::vector<uint8_t> blteDecode(const uint8_t* data, std::size_t size,
                                std::size_t limit, std::string* error);

uint64_t jenkins96(const std::string& path) {
    std::string key;
    key.reserve(path.size());
    for (char c : path) {
        if (c == '/') c = '\\';
        key.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    const auto* bytes = reinterpret_cast<const uint8_t*>(key.data());
    const std::size_t length = key.size();

    uint32_t a = 0xDEADBEEFu + static_cast<uint32_t>(length);
    uint32_t b = a;
    uint32_t c = a;

    std::size_t pos = 0;
    while (length - pos > 12) {
        a += readLE32(bytes + pos);
        b += readLE32(bytes + pos + 4);
        c += readLE32(bytes + pos + 8);
        a -= c; a ^= rot(c, 4);  c += b;
        b -= a; b ^= rot(a, 6);  a += c;
        c -= b; c ^= rot(b, 8);  b += a;
        a -= c; a ^= rot(c, 16); c += b;
        b -= a; b ^= rot(a, 19); a += c;
        c -= b; c ^= rot(b, 4);  b += a;
        pos += 12;
    }

    uint8_t tail[12] = {0};
    std::memcpy(tail, bytes + pos, length - pos);
    a += readLE32(tail);
    b += readLE32(tail + 4);
    c += readLE32(tail + 8);

    if (length - pos != 0) {
        c ^= b; c -= rot(b, 14);
        a ^= c; a -= rot(c, 11);
        b ^= a; b -= rot(a, 25);
        c ^= b; c -= rot(b, 16);
        a ^= c; a -= rot(c, 4);
        b ^= a; b -= rot(a, 14);
        c ^= b; c -= rot(b, 24);
    }

    // c in the high word: hashlittle2 hands back (pc, pb) and the root stores
    // them the other way round from the order the name suggests.
    return (static_cast<uint64_t>(c) << 32) | b;
}

namespace {

bool decodeChunk(const uint8_t* chunk, std::size_t size, std::vector<uint8_t>& out,
                 std::string* error) {
    if (size == 0) return true;
    switch (chunk[0]) {
        case 'N':
            out.insert(out.end(), chunk + 1, chunk + size);
            return true;
        case 'Z':
            if (!inflateBlock(chunk + 1, size - 1, out)) {
                if (error) *error = "a compressed chunk would not inflate";
                return false;
            }
            return true;
        case 'F': {
            std::vector<uint8_t> nested = blteDecode(chunk + 1, size - 1, 0, error);
            out.insert(out.end(), nested.begin(), nested.end());
            return !nested.empty();
        }
        case 'E':
            if (error) *error = "encrypted chunk - this file needs a key we do not have";
            return false;
        default:
            if (error) *error = "unknown BLTE chunk mode";
            return false;
    }
}

}  // namespace

std::vector<uint8_t> blteDecode(const uint8_t* data, std::size_t size,
                                std::size_t limit, std::string* error) {
    std::vector<uint8_t> out;
    if (size < 8 || std::memcmp(data, "BLTE", 4) != 0) {
        if (error) *error = "not a BLTE container";
        return out;
    }
    const uint32_t headerSize = readBE32(data + 4);
    if (headerSize == 0) {
        decodeChunk(data + 8, size - 8, out, error);
        return out;
    }
    if (size < 12) return out;

    // A 24-bit count: one zero byte then three of the field.
    const uint32_t count = (uint32_t(data[9]) << 16) | (uint32_t(data[10]) << 8) | data[11];
    std::vector<uint32_t> compressed;
    compressed.reserve(count);
    std::size_t pos = 12;
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 24 > size) break;
        compressed.push_back(readBE32(data + pos));
        pos += 24;   // two sizes plus a 16-byte checksum
    }

    std::size_t at = headerSize;
    for (uint32_t chunkSize : compressed) {
        if (at + chunkSize > size) break;
        if (!decodeChunk(data + at, chunkSize, out, error)) break;
        at += chunkSize;
        if (limit != 0 && out.size() >= limit) break;
    }
    return out;
}

bool CascStorage::readIndexFile(const std::string& path) {
    const std::vector<uint8_t> blob = slurp(path);
    if (blob.size() < 8) return false;

    const uint32_t headerSize = readLE32(blob.data());
    if (8 + headerSize > blob.size()) return false;
    const uint8_t* header = blob.data() + 8;

    // uint16 version, then six single bytes. Read as one field short, every
    // key comes out thirty bytes of nonsense.
    const uint8_t spanSizeBytes = header[4];
    const uint8_t spanOffsBytes = header[5];
    const uint8_t keyBytes = header[6];
    const uint8_t segmentBits = header[7];
    if (keyBytes == 0 || keyBytes > 16) return false;

    std::size_t pos = 8 + headerSize;
    pos = (pos + 0x0F) & ~static_cast<std::size_t>(0x0F);   // entries are 16-byte aligned
    if (pos + 8 > blob.size()) return false;
    const uint32_t entriesSize = readLE32(blob.data() + pos);
    pos += 8;

    const std::size_t entrySize = std::size_t(keyBytes) + spanOffsBytes + spanSizeBytes;
    if (entrySize == 0) return false;
    const std::size_t entries = entriesSize / entrySize;

    for (std::size_t i = 0; i < entries; ++i) {
        if (pos + entrySize > blob.size()) break;
        IndexKey key{};
        bool anySet = false;
        for (std::size_t k = 0; k < key.size() && k < keyBytes; ++k) {
            key[k] = blob[pos + k];
            anySet = anySet || key[k] != 0;
        }
        uint64_t rawOffset = 0;
        for (uint8_t k = 0; k < spanOffsBytes; ++k) {       // big-endian
            rawOffset = (rawOffset << 8) | blob[pos + keyBytes + k];
        }
        uint32_t size = 0;
        for (int k = spanSizeBytes - 1; k >= 0; --k) {      // little-endian
            size = (size << 8) | blob[pos + keyBytes + spanOffsBytes + k];
        }
        pos += entrySize;
        if (!anySet) continue;

        IndexEntry entry;
        entry.archive = static_cast<uint32_t>(rawOffset >> segmentBits);
        entry.offset = rawOffset & ((uint64_t(1) << segmentBits) - 1);
        entry.size = size;
        index_[key] = entry;     // later generations win
    }
    return true;
}

bool CascStorage::loadIndices(const std::string& dataDir, std::string* error) {
    std::error_code ec;
    // One .idx per bucket and several generations of each; the highest numbered
    // file for a bucket is the live one, so they are read in order and later
    // ones overwrite earlier.
    std::map<int, std::pair<uint32_t, std::string>> buckets;
    for (fs::directory_iterator it(dataDir, ec), end; it != end && !ec; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("._", 0) == 0) continue;
        if (it->path().extension() != ".idx" || name.size() < 10) continue;
        const int bucket = static_cast<int>(std::strtol(name.substr(0, 2).c_str(), nullptr, 16));
        const auto version = static_cast<uint32_t>(
            std::strtoul(name.substr(2, 8).c_str(), nullptr, 16));
        auto found = buckets.find(bucket);
        if (found == buckets.end() || version > found->second.first) {
            buckets[bucket] = {version, it->path().string()};
        }
    }
    if (buckets.empty()) {
        if (error) *error = "no index files in " + dataDir;
        return false;
    }
    for (const auto& [bucket, entry] : buckets) {
        (void)bucket;
        readIndexFile(entry.second);
    }
    return !index_.empty();
}

bool CascStorage::open(const std::string& installDir, std::string* error) {
    std::error_code ec;
    dataDir_ = (fs::path(installDir) / "data" / "data").string();
    if (!fs::is_directory(dataDir_, ec)) {
        if (error) *error = "no data/data directory - is this a Warlords or later install?";
        return false;
    }

    // The build config, found by what it contains rather than by name: a local
    // install normally names it in .build.info and that file is not always
    // there, while the config directory is only ever a handful of files.
    const fs::path configDir = fs::path(installDir) / "data" / "config";
    bool found = false;
    for (fs::recursive_directory_iterator it(configDir, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (it->path().filename().string().rfind("._", 0) == 0) continue;
        std::ifstream in(it->path());
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.find("root = ") == std::string::npos ||
            text.find("encoding = ") == std::string::npos) {
            continue;
        }
        std::size_t at = 0;
        while (at < text.size()) {
            const std::size_t eol = text.find('\n', at);
            std::string line = text.substr(at, eol == std::string::npos ? eol : eol - at);
            at = (eol == std::string::npos) ? text.size() : eol + 1;
            const std::size_t sep = line.find(" = ");
            if (line.empty() || line[0] == '#' || sep == std::string::npos) continue;
            const std::string key = line.substr(0, sep);
            std::vector<std::string> values;
            std::string rest = line.substr(sep + 3);
            std::size_t start = 0;
            while (start < rest.size()) {
                const std::size_t space = rest.find(' ', start);
                std::string piece = rest.substr(start, space == std::string::npos ? space : space - start);
                if (!piece.empty() && piece.back() == '\r') piece.pop_back();
                if (!piece.empty()) values.push_back(piece);
                if (space == std::string::npos) break;
                start = space + 1;
            }
            config_[key] = values;
        }
        found = true;
        break;
    }
    if (!found) {
        if (error) *error = "no build config under " + configDir.string();
        return false;
    }

    if (!loadIndices(dataDir_, error)) return false;

    // encoding is named by its encoding key, the second of the pair; root by
    // its content key, which has to go through encoding to be found at all.
    ContentKey encodingKey{};
    if (config_["encoding"].size() < 2 || !fromHex(config_["encoding"][1], encodingKey)) {
        if (error) *error = "the build config does not name an encoding file";
        return false;
    }
    const std::vector<uint8_t> encodingBlob = readEKey(encodingKey, 0);
    if (encodingBlob.size() < 22 || encodingBlob[0] != 'E' || encodingBlob[1] != 'N') {
        if (error) *error = "the encoding table would not read";
        return false;
    }
    {
        const uint8_t* blob = encodingBlob.data();
        const uint8_t ckeySize = blob[3];
        const uint8_t ekeySize = blob[4];
        const uint16_t pageKb = readBE16(blob + 5);
        const uint32_t pages = readBE32(blob + 9);
        const uint32_t especSize = readBE32(blob + 18);
        const std::size_t pageTable = 22 + especSize;
        const std::size_t firstPage = pageTable + std::size_t(pages) * (ckeySize + 16);
        const std::size_t pageBytes = std::size_t(pageKb) * 1024;

        for (uint32_t page = 0; page < pages; ++page) {
            std::size_t pos = firstPage + std::size_t(page) * pageBytes;
            const std::size_t end = pos + pageBytes;
            while (pos + 6 + ckeySize <= end && pos + 6 + ckeySize <= encodingBlob.size()) {
                const uint8_t keyCount = blob[pos];
                if (keyCount == 0) break;
                ContentKey ckey{};
                std::memcpy(ckey.data(), blob + pos + 6, std::min<std::size_t>(ckeySize, ckey.size()));
                const std::size_t ekeyAt = pos + 6 + ckeySize;
                if (ekeyAt + ekeySize > encodingBlob.size()) break;
                ContentKey ekey{};
                std::memcpy(ekey.data(), blob + ekeyAt, std::min<std::size_t>(ekeySize, ekey.size()));
                encoding_[ckey] = ekey;
                pos = ekeyAt + std::size_t(ekeySize) * keyCount;
            }
        }
    }

    ContentKey rootKey{};
    if (config_["root"].empty() || !fromHex(config_["root"][0], rootKey)) {
        if (error) *error = "the build config does not name a root file";
        return false;
    }
    const std::vector<uint8_t> rootBlob = readCKey(rootKey, 0);
    if (rootBlob.empty()) {
        if (error) *error = "the root table would not read";
        return false;
    }
    {
        // Blocks of records, each headed by a count and two flag words. Ids are
        // stored as deltas because they mostly run consecutively.
        std::size_t pos = 0;
        const std::size_t total = rootBlob.size();
        while (pos + 12 <= total) {
            const uint32_t count = readLE32(rootBlob.data() + pos);
            pos += 12;
            if (count == 0 || pos + std::size_t(count) * 4 > total) break;
            const std::size_t deltasAt = pos;
            pos += std::size_t(count) * 4;
            int64_t fileId = -1;
            for (uint32_t i = 0; i < count; ++i) {
                const auto delta = static_cast<int32_t>(readLE32(rootBlob.data() + deltasAt + i * 4));
                fileId += delta + 1;
                const std::size_t entry = pos + std::size_t(i) * 24;
                if (entry + 24 > total) break;
                const auto id = static_cast<uint32_t>(fileId);
                if (root_.count(id)) continue;
                RootEntry record;
                std::memcpy(record.ckey.data(), rootBlob.data() + entry, 16);
                record.nameHash = readLE64(rootBlob.data() + entry + 16);
                root_[id] = record;
            }
            pos += std::size_t(count) * 24;
        }
    }

    for (const auto& [fileId, entry] : root_) {
        if (entry.nameHash != 0) byNameHash_.emplace(entry.nameHash, fileId);
    }

    open_ = true;
    return true;
}

std::vector<uint8_t> CascStorage::readEKey(const ContentKey& ekey, std::size_t limit) {
    IndexKey shortKey{};
    std::memcpy(shortKey.data(), ekey.data(), shortKey.size());
    auto found = index_.find(shortKey);
    if (found == index_.end()) return {};

    auto handle = archives_.find(found->second.archive);
    if (handle == archives_.end()) {
        char name[64];
        std::snprintf(name, sizeof(name), "data.%03u", found->second.archive);
        auto opened = std::make_shared<std::ifstream>(fs::path(dataDir_) / name,
                                                      std::ios::binary);
        if (!opened->is_open()) return {};
        handle = archives_.emplace(found->second.archive, std::move(opened)).first;
    }
    std::ifstream& in = *handle->second;
    in.clear();
    in.seekg(static_cast<std::streamoff>(found->second.offset));

    std::vector<uint8_t> raw(found->second.size);
    in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    // Each archive entry carries a 30-byte header of its own before the BLTE:
    // the key backwards, the size, and two fields nothing here needs.
    if (raw.size() <= 30) return {};
    std::string ignored;
    return blteDecode(raw.data() + 30, raw.size() - 30, limit, &ignored);
}

std::vector<uint8_t> CascStorage::readCKey(const ContentKey& ckey, std::size_t limit) {
    auto found = encoding_.find(ckey);
    if (found == encoding_.end()) return {};
    return readEKey(found->second, limit);
}

std::vector<uint8_t> CascStorage::readFileId(uint32_t fileId, std::size_t limit) {
    auto found = root_.find(fileId);
    if (found == root_.end()) return {};
    return readCKey(found->second.ckey, limit);
}

std::vector<uint8_t> CascStorage::readPath(const std::string& path, std::size_t limit) {
    auto found = byNameHash_.find(jenkins96(path));
    if (found == byNameHash_.end()) return {};
    return readFileId(found->second, limit);
}

}  // namespace wowee::assets
