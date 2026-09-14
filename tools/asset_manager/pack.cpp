#include "pack.hpp"

#include <zlib.h>

#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>

namespace wowee::assets {
namespace {

namespace fs = std::filesystem;

void put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(uint8_t(value & 0xFF));
    out.push_back(uint8_t(value >> 8));
}

void put32(std::vector<uint8_t>& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t((value >> (i * 8)) & 0xFF));
}

/// Raw deflate, which is what a zip entry holds - no zlib header or trailer,
/// hence the negative window size.
bool deflateBytes(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    z_stream stream{};
    if (deflateInit2(&stream, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return false;
    }
    stream.next_in = const_cast<Bytef*>(in.data());
    stream.avail_in = static_cast<uInt>(in.size());

    std::vector<uint8_t> buffer(64 * 1024);
    int status = Z_OK;
    do {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = deflate(&stream, Z_FINISH);
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
            deflateEnd(&stream);
            return false;
        }
        out.insert(out.end(), buffer.data(), buffer.data() + (buffer.size() - stream.avail_out));
    } while (status != Z_STREAM_END);
    deflateEnd(&stream);
    return true;
}

struct Entry {
    std::string name;
    uint32_t crc = 0;
    uint32_t compressed = 0;
    uint32_t raw = 0;
    uint32_t offset = 0;
};

void writeLocalHeader(std::ofstream& out, const Entry& entry) {
    std::vector<uint8_t> header;
    put32(header, 0x04034B50);
    put16(header, 20);              // version needed
    put16(header, 0);               // flags
    put16(header, 8);               // deflate
    put16(header, 0);               // time
    put16(header, 0x21);            // date: an arbitrary valid one
    put32(header, entry.crc);
    put32(header, entry.compressed);
    put32(header, entry.raw);
    put16(header, uint16_t(entry.name.size()));
    put16(header, 0);
    out.write(reinterpret_cast<const char*>(header.data()),
              static_cast<std::streamsize>(header.size()));
    out.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
}

}  // namespace

PackResult writePack(const std::string& sourceDir, const std::string& destZip,
                     const std::string& name,
                     const std::function<void(std::size_t, std::size_t)>& progress,
                     const std::atomic<bool>& cancel) {
    PackResult result;
    std::error_code ec;
    if (!fs::is_directory(sourceDir, ec)) {
        result.error = "there is nothing at " + sourceDir;
        return result;
    }

    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(sourceDir, ec), end; it != end && !ec; it.increment(ec)) {
        if (it->is_regular_file(ec)) files.push_back(it->path());
    }

    std::ofstream out(destZip, std::ios::binary);
    if (!out) {
        result.error = "could not write " + destZip;
        return result;
    }

    std::vector<Entry> entries;
    const auto addEntry = [&](const std::string& entryName, const std::vector<uint8_t>& bytes) {
        Entry entry;
        entry.name = entryName;
        entry.raw = static_cast<uint32_t>(bytes.size());
        entry.crc = static_cast<uint32_t>(
            crc32(0, bytes.data(), static_cast<uInt>(bytes.size())));
        entry.offset = static_cast<uint32_t>(out.tellp());

        std::vector<uint8_t> squeezed;
        if (!deflateBytes(bytes, squeezed)) return false;
        entry.compressed = static_cast<uint32_t>(squeezed.size());

        writeLocalHeader(out, entry);
        out.write(reinterpret_cast<const char*>(squeezed.data()),
                  static_cast<std::streamsize>(squeezed.size()));
        entries.push_back(entry);
        result.rawBytes += bytes.size();
        return true;
    };

    // A manifest first, so a reader knows what it has before unpacking any of it.
    {
        std::string manifest = "{\n  \"pack_format\": 1,\n  \"name\": \"" + name +
                               "\",\n  \"file_count\": " + std::to_string(files.size()) +
                               "\n}\n";
        addEntry("pack.json", std::vector<uint8_t>(manifest.begin(), manifest.end()));
    }

    for (std::size_t i = 0; i < files.size(); ++i) {
        if (cancel.load()) {
            result.error = "stopped";
            return result;
        }
        std::ifstream in(files[i], std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>()};
        std::string entryName = "Data/" + fs::relative(files[i], sourceDir, ec).generic_string();
        if (!addEntry(entryName, bytes)) {
            result.error = "could not compress " + entryName;
            return result;
        }
        if (progress && (i % 200 == 0 || i + 1 == files.size())) progress(i + 1, files.size());
    }

    const auto directoryAt = static_cast<uint32_t>(out.tellp());
    for (const Entry& entry : entries) {
        std::vector<uint8_t> record;
        put32(record, 0x02014B50);
        put16(record, 20);          // version made by
        put16(record, 20);          // version needed
        put16(record, 0);
        put16(record, 8);
        put16(record, 0);
        put16(record, 0x21);
        put32(record, entry.crc);
        put32(record, entry.compressed);
        put32(record, entry.raw);
        put16(record, uint16_t(entry.name.size()));
        put16(record, 0); put16(record, 0); put16(record, 0); put16(record, 0);
        put32(record, 0);
        put32(record, entry.offset);
        out.write(reinterpret_cast<const char*>(record.data()),
                  static_cast<std::streamsize>(record.size()));
        out.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
    }
    const auto directorySize = static_cast<uint32_t>(out.tellp()) - directoryAt;

    std::vector<uint8_t> end;
    put32(end, 0x06054B50);
    put16(end, 0); put16(end, 0);
    put16(end, uint16_t(entries.size()));
    put16(end, uint16_t(entries.size()));
    put32(end, directorySize);
    put32(end, directoryAt);
    put16(end, 0);
    out.write(reinterpret_cast<const char*>(end.data()),
              static_cast<std::streamsize>(end.size()));
    out.close();

    result.files = entries.size();
    result.packedBytes = static_cast<std::size_t>(fs::file_size(destZip, ec));
    result.ok = true;
    return result;
}



namespace {

uint16_t get16(const uint8_t* at) { return uint16_t(at[0] | (at[1] << 8)); }
uint32_t get32(const uint8_t* at) {
    return uint32_t(at[0]) | (uint32_t(at[1]) << 8) | (uint32_t(at[2]) << 16) |
           (uint32_t(at[3]) << 24);
}

bool inflateBytes(const uint8_t* in, std::size_t inSize, std::size_t rawSize,
                  std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(rawSize);
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return false;
    stream.next_in = const_cast<Bytef*>(in);
    stream.avail_in = static_cast<uInt>(inSize);

    std::vector<uint8_t> buffer(64 * 1024);
    int status = Z_OK;
    do {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) {
            inflateEnd(&stream);
            return false;
        }
        out.insert(out.end(), buffer.data(), buffer.data() + (buffer.size() - stream.avail_out));
    } while (status != Z_STREAM_END);
    inflateEnd(&stream);
    return true;
}

/// One entry, as the central directory describes it.
struct Listed {
    std::string name;
    uint16_t method = 0;
    uint32_t compressed = 0;
    uint32_t raw = 0;
    uint32_t headerAt = 0;
};

/// Every entry in the archive, read from the directory at the end of it.
bool listEntries(std::ifstream& in, std::vector<Listed>& out, std::string* error) {
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 22) {
        if (error) *error = "Not a zip file.";
        return false;
    }

    // The end record is last, but a zip comment can follow it, so the last 64K
    // is searched backwards for its signature.
    const std::streamoff window = std::min<std::streamoff>(size, 66000);
    std::vector<uint8_t> tail(static_cast<std::size_t>(window));
    in.seekg(size - window);
    in.read(reinterpret_cast<char*>(tail.data()), window);

    std::streamoff endAt = -1;
    for (std::streamoff i = window - 22; i >= 0; --i) {
        if (get32(tail.data() + i) == 0x06054B50) { endAt = i; break; }
    }
    if (endAt < 0) {
        if (error) *error = "Not a zip file, or damaged.";
        return false;
    }

    const uint16_t count = get16(tail.data() + endAt + 10);
    const uint32_t directoryAt = get32(tail.data() + endAt + 16);
    const uint32_t directorySize = get32(tail.data() + endAt + 12);
    if (directoryAt + directorySize > uint64_t(size)) {
        if (error) *error = "The zip's directory points outside the file.";
        return false;
    }

    std::vector<uint8_t> directory(directorySize);
    in.seekg(directoryAt);
    in.read(reinterpret_cast<char*>(directory.data()), directorySize);

    std::size_t at = 0;
    for (uint16_t i = 0; i < count && at + 46 <= directory.size(); ++i) {
        if (get32(directory.data() + at) != 0x02014B50) break;
        Listed entry;
        entry.method = get16(directory.data() + at + 10);
        entry.compressed = get32(directory.data() + at + 20);
        entry.raw = get32(directory.data() + at + 24);
        const uint16_t nameLen = get16(directory.data() + at + 28);
        const uint16_t extraLen = get16(directory.data() + at + 30);
        const uint16_t commentLen = get16(directory.data() + at + 32);
        entry.headerAt = get32(directory.data() + at + 42);
        if (at + 46 + nameLen > directory.size()) break;
        entry.name.assign(reinterpret_cast<const char*>(directory.data() + at + 46), nameLen);
        at += 46 + nameLen + extraLen + commentLen;
        out.push_back(std::move(entry));
    }
    return true;
}

/// The bytes of one entry, following its local header to find where they start.
bool readEntry(std::ifstream& in, const Listed& entry, std::vector<uint8_t>& out,
               std::string* error) {
    uint8_t local[30];
    in.seekg(entry.headerAt);
    in.read(reinterpret_cast<char*>(local), sizeof(local));
    if (!in || get32(local) != 0x04034B50) {
        if (error) *error = "Damaged entry: " + entry.name;
        return false;
    }
    // The local header's own name and extra lengths, not the directory's: the
    // two are allowed to differ in the extra field and often do.
    const std::streamoff dataAt =
        std::streamoff(entry.headerAt) + 30 + get16(local + 26) + get16(local + 28);

    std::vector<uint8_t> packed(entry.compressed);
    in.seekg(dataAt);
    in.read(reinterpret_cast<char*>(packed.data()), std::streamsize(packed.size()));
    if (!in) {
        if (error) *error = "Truncated: " + entry.name;
        return false;
    }

    if (entry.method == 0) {
        out = std::move(packed);
        return true;
    }
    if (entry.method != 8) {
        if (error) *error = "Unsupported compression in: " + entry.name;
        return false;
    }
    if (!inflateBytes(packed.data(), packed.size(), entry.raw, out)) {
        if (error) *error = "Could not decompress: " + entry.name;
        return false;
    }
    return true;
}

/// Where an entry should land, or empty if it should not land anywhere.
///
/// A zip entry's name is a string chosen by whoever built the archive, and
/// "../../../.ssh/authorized_keys" is a valid one. Nothing here trusts it: the
/// path is rebuilt a component at a time, and anything that is not a plain name
/// throws the entry away rather than being resolved.
fs::path safeDestination(const fs::path& destDir, const std::string& name) {
    const fs::path entry(name);

    // An absolute name is refused outright. Appending one component at a time
    // is not enough on its own: iterating "/etc/passwd" yields "/" as its first
    // component, and appending that to a relative path does not extend it, it
    // replaces it - so the walk below would have built an absolute path and
    // written exactly where the name asked. A backslashed Windows name reaches
    // here as one component on POSIX, which the drive-letter test catches.
    if (entry.is_absolute() || entry.has_root_path()) return {};
    if (name.empty() || name.front() == '/' || name.front() == '\\') return {};

    fs::path relative;
    bool first = true;
    for (const fs::path& part : entry) {
        const std::string piece = part.string();
        if (piece.empty() || piece == "." || piece == "..") return {};
        if (piece.find(':') != std::string::npos) return {};   // a drive letter
        if (piece.find('/') != std::string::npos) return {};   // a separator, on Windows
        if (piece.find('\\') != std::string::npos) return {};
        // The prefix everything was written under, dropped on the way back out.
        if (first) {
            first = false;
            if (piece == "Data") continue;
        }
        relative /= piece;
    }
    if (relative.empty() || !relative.is_relative()) return {};

    // Whatever the checks above missed, the answer still has to be under the
    // folder being installed into. Lexical, not canonical: the file is not
    // there yet, and a path that does not exist cannot be resolved.
    const fs::path full = (destDir / relative).lexically_normal();
    const fs::path base = destDir.lexically_normal();
    const auto relation = full.lexically_relative(base);
    if (relation.empty() || *relation.begin() == "..") return {};
    return full;
}

}  // namespace

PackInfo readPackInfo(const std::string& zipPath) {
    PackInfo info;
    std::ifstream in(zipPath, std::ios::binary);
    if (!in) {
        info.error = "Cannot open that file.";
        return info;
    }

    std::vector<Listed> entries;
    if (!listEntries(in, entries, &info.error)) return info;

    for (const Listed& entry : entries) {
        if (entry.name == "Data/pack.json" || entry.name == "pack.json") {
            std::vector<uint8_t> bytes;
            if (readEntry(in, entry, bytes, &info.error)) {
                const std::string text(bytes.begin(), bytes.end());
                // One string out of a small object this program wrote itself.
                const std::string key = "\"name\":";
                if (const std::size_t at = text.find(key); at != std::string::npos) {
                    const std::size_t open = text.find('"', at + key.size());
                    const std::size_t close = open == std::string::npos
                                                  ? std::string::npos
                                                  : text.find('"', open + 1);
                    if (close != std::string::npos) {
                        info.name = text.substr(open + 1, close - open - 1);
                    }
                }
            }
            continue;
        }
        if (!entry.name.empty() && entry.name.back() == '/') continue;
        ++info.files;
        info.rawBytes += entry.raw;
    }

    if (info.files == 0) {
        info.error = "There are no asset files in that zip.";
        return info;
    }
    info.ok = true;
    return info;
}

PackResult readPack(const std::string& zipPath, const std::string& destDir,
                    const std::function<void(std::size_t, std::size_t)>& progress,
                    const std::atomic<bool>& cancel) {
    PackResult result;
    std::ifstream in(zipPath, std::ios::binary);
    if (!in) {
        result.error = "Cannot open that file.";
        return result;
    }

    std::vector<Listed> entries;
    if (!listEntries(in, entries, &result.error)) return result;

    std::error_code ec;
    fs::create_directories(destDir, ec);

    std::size_t done = 0;
    for (const Listed& entry : entries) {
        if (cancel.load()) {
            result.error = "Stopped.";
            return result;
        }
        ++done;
        if (progress && done % 64 == 0) progress(done, entries.size());

        if (!entry.name.empty() && entry.name.back() == '/') continue;
        if (entry.name == "Data/pack.json" || entry.name == "pack.json") continue;

        const fs::path at = safeDestination(destDir, entry.name);
        if (at.empty()) {
            result.error = "That pack contains a path outside the folder it "
                           "would be installed into, and was not trusted: " + entry.name;
            return result;
        }

        std::vector<uint8_t> bytes;
        if (!readEntry(in, entry, bytes, &result.error)) return result;

        fs::create_directories(at.parent_path(), ec);
        std::ofstream out(at, std::ios::binary | std::ios::trunc);
        if (!out) {
            result.error = "Cannot write " + at.string();
            return result;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            result.error = "Ran out of room writing " + at.string();
            return result;
        }
        ++result.files;
        result.rawBytes += bytes.size();
        result.packedBytes += entry.compressed;
    }

    if (progress) progress(entries.size(), entries.size());
    result.ok = result.files > 0;
    if (!result.ok) result.error = "There were no asset files in that zip.";
    return result;
}

}  // namespace wowee::assets
