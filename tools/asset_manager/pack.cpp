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

}  // namespace wowee::assets
