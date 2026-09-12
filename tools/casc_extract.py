#!/usr/bin/env python3
"""Read a local CASC installation - Warlords and later - and pull files out of it.

    tools/casc_extract.py <install-dir> [--list] [--name SUBSTR] [--out DIR]

asset_extract handles MPQ, which covers Classic through Mists. From Warlords
on the game stores its files in CASC instead, and nothing here could read one -
so a Legion install on a shelf was 43GB nobody could open.

CASC is four lookups deep. The build config names a root file and an encoding
file by content key; encoding maps a content key to an encoding key; the .idx
buckets map an encoding key to an offset inside one of the data.### archives;
and every archive entry is BLTE, a container of independently compressed
chunks. The root maps FileDataID to content key, which is where a file's
identity comes from - CASC has no filenames in it at all.
"""

import hashlib
import os
import struct
import sys
import zlib


# ── build config ──────────────────────────────────────────────

def find_build_config(install_dir):
    """The build config, found by what it contains rather than by name.

    A local install normally names it in .build.info at the root. That file is
    not always there - it is absent from the install this was written against -
    and the config directory is only ever a handful of files, so the one that
    declares a root and an encoding is identifiable on sight.
    """
    config_dir = os.path.join(install_dir, "data", "config")
    for dirpath, _dirnames, filenames in os.walk(config_dir):
        for name in filenames:
            if name.startswith("._"):
                continue
            path = os.path.join(dirpath, name)
            try:
                with open(path, "rb") as handle:
                    head = handle.read(4096).decode("utf-8", "replace")
            except OSError:
                continue
            if "root = " in head and "encoding = " in head:
                return path, parse_config(head)
    raise SystemExit("no build config under " + config_dir)


def parse_config(text):
    out = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or " = " not in line:
            continue
        key, value = line.split(" = ", 1)
        out[key.strip()] = value.strip().split()
    return out


# ── .idx: encoding key -> where the bytes are ─────────────────

class IndexTable:
    """Encoding key (first 9 bytes) -> (archive number, offset, size).

    One .idx per bucket, and several generations of each; the highest-numbered
    file for a bucket is the live one and the older ones are stale, so they are
    read in order and later ones win.
    """

    def __init__(self, data_dir):
        self.entries = {}
        buckets = {}
        for name in os.listdir(data_dir):
            if name.startswith("._") or not name.endswith(".idx"):
                continue
            bucket = int(name[:2], 16)
            version = int(name[2:10], 16)
            if bucket not in buckets or version > buckets[bucket][0]:
                buckets[bucket] = (version, os.path.join(data_dir, name))
        for _bucket, (_version, path) in sorted(buckets.items()):
            self._read(path)

    def _read(self, path):
        with open(path, "rb") as handle:
            blob = handle.read()
        header_size = struct.unpack_from("<I", blob, 0)[0]
        header = blob[8:8 + header_size]
        # uint16 version, then six single bytes, then a uint64 max offset.
        # Read as <HHBBBBB this comes out one field short and every key is
        # thirty bytes of nonsense.
        (_version,) = struct.unpack_from("<H", header, 0)
        (_bucket, _extra, span_size_bytes, span_offs_bytes,
         key_bytes, segment_bits) = struct.unpack_from("<BBBBBB", header, 2)

        pos = 8 + header_size
        pos = (pos + 0x0F) & ~0x0F          # entries start 16-byte aligned
        entries_size = struct.unpack_from("<I", blob, pos)[0]
        pos += 8

        entry_size = key_bytes + span_offs_bytes + span_size_bytes
        for _ in range(entries_size // entry_size):
            key = blob[pos:pos + key_bytes]
            raw_offset = int.from_bytes(
                blob[pos + key_bytes:pos + key_bytes + span_offs_bytes], "big")
            size = int.from_bytes(
                blob[pos + key_bytes + span_offs_bytes:pos + entry_size], "little")
            pos += entry_size
            if not any(key):
                continue
            archive = raw_offset >> segment_bits
            offset = raw_offset & ((1 << segment_bits) - 1)
            self.entries[key] = (archive, offset, size)

    def find(self, ekey):
        return self.entries.get(ekey[:9])


# ── BLTE ──────────────────────────────────────────────────────

def blte_decode(blob, limit=0):
    """Decode a BLTE container. limit stops once that many bytes are out.

    Identifying a file needs its first few hundred bytes and nothing else, and
    a terrain texture is several megabytes across a dozen chunks. Stopping
    early turns a survey of the whole install from hours into minutes.
    """
    if blob[:4] != b"BLTE":
        raise ValueError("not BLTE")
    header_size = struct.unpack_from(">I", blob, 4)[0]
    if header_size == 0:
        return _blte_chunk(blob[8:])
    count = struct.unpack_from(">I", b"\x00" + blob[9:12])[0]
    pos = 12
    chunks = []
    for _ in range(count):
        comp_size, _decomp_size = struct.unpack_from(">II", blob, pos)
        pos += 24                      # two sizes plus a 16-byte checksum
        chunks.append(comp_size)
    out = bytearray()
    data_pos = header_size
    for comp_size in chunks:
        out += _blte_chunk(blob[data_pos:data_pos + comp_size])
        data_pos += comp_size
        if limit and len(out) >= limit:
            break
    return bytes(out)


def _blte_chunk(chunk):
    mode = chunk[:1]
    if mode == b"N":
        return chunk[1:]
    if mode == b"Z":
        return zlib.decompress(chunk[1:])
    if mode == b"F":
        return blte_decode(chunk[1:])
    if mode == b"E":
        raise ValueError("encrypted chunk")
    raise ValueError("unknown BLTE mode %r" % mode)


# ── the store ─────────────────────────────────────────────────

def parse_encoding(blob):
    """Content key -> encoding key.

    A file is named by the hash of its contents and stored under the hash of
    its encoded form, so every lookup goes through this table. It is paged:
    a directory of first-keys, then fixed-size pages of entries, which is how
    the real client binary-searches an 80MB table without reading it.
    """
    if blob[:2] != b"EN":
        raise ValueError("not an encoding file")
    ckey_size, ekey_size = blob[3], blob[4]
    cpage_kb = struct.unpack_from(">H", blob, 5)[0]
    cpages = struct.unpack_from(">I", blob, 9)[0]
    espec_size = struct.unpack_from(">I", blob, 18)[0]

    page_table = 22 + espec_size
    pages = page_table + cpages * (ckey_size + 16)
    page_bytes = cpage_kb * 1024

    out = {}
    for page in range(cpages):
        pos = pages + page * page_bytes
        end = pos + page_bytes
        while pos + 6 + ckey_size <= end:
            key_count = blob[pos]
            if key_count == 0:
                break
            ckey = blob[pos + 6:pos + 6 + ckey_size]
            ekey_at = pos + 6 + ckey_size
            out[ckey] = blob[ekey_at:ekey_at + ekey_size]
            pos = ekey_at + ekey_size * key_count
    return out


def _rot(x, k):
    return ((x << k) | (x >> (32 - k))) & 0xFFFFFFFF


def jenkins96(text):
    """Bob Jenkins' hashlittle2, which is how CASC names a file.

    The root has no filenames in it, only a 64-bit hash of the uppercased path
    with backslashes. So a path cannot be listed out of an install, but it can
    be asked for - which is enough when the names are already known, and the
    texture paths inside an M2 are names already known.
    """
    key = text.upper().replace("/", "\\").encode("latin-1")
    length = len(key)
    a = b = c = (0xDEADBEEF + length) & 0xFFFFFFFF

    pos = 0
    while length - pos > 12:
        a = (a + int.from_bytes(key[pos:pos + 4], "little")) & 0xFFFFFFFF
        b = (b + int.from_bytes(key[pos + 4:pos + 8], "little")) & 0xFFFFFFFF
        c = (c + int.from_bytes(key[pos + 8:pos + 12], "little")) & 0xFFFFFFFF
        a = (a - c) & 0xFFFFFFFF; a ^= _rot(c, 4);  c = (c + b) & 0xFFFFFFFF
        b = (b - a) & 0xFFFFFFFF; b ^= _rot(a, 6);  a = (a + c) & 0xFFFFFFFF
        c = (c - b) & 0xFFFFFFFF; c ^= _rot(b, 8);  b = (b + a) & 0xFFFFFFFF
        a = (a - c) & 0xFFFFFFFF; a ^= _rot(c, 16); c = (c + b) & 0xFFFFFFFF
        b = (b - a) & 0xFFFFFFFF; b ^= _rot(a, 19); a = (a + c) & 0xFFFFFFFF
        c = (c - b) & 0xFFFFFFFF; c ^= _rot(b, 4);  b = (b + a) & 0xFFFFFFFF
        pos += 12

    tail = key[pos:] + b"\x00" * 12
    a = (a + int.from_bytes(tail[0:4], "little")) & 0xFFFFFFFF
    b = (b + int.from_bytes(tail[4:8], "little")) & 0xFFFFFFFF
    c = (c + int.from_bytes(tail[8:12], "little")) & 0xFFFFFFFF

    if length - pos:
        c ^= b; c = (c - _rot(b, 14)) & 0xFFFFFFFF
        a ^= c; a = (a - _rot(c, 11)) & 0xFFFFFFFF
        b ^= a; b = (b - _rot(a, 25)) & 0xFFFFFFFF
        c ^= b; c = (c - _rot(b, 16)) & 0xFFFFFFFF
        a ^= c; a = (a - _rot(c, 4))  & 0xFFFFFFFF
        b ^= a; b = (b - _rot(a, 14)) & 0xFFFFFFFF
        c ^= b; c = (c - _rot(b, 24)) & 0xFFFFFFFF

    # c in the high word. hashlittle2 hands back (pc, pb) and the root stores
    # them the other way round from the order the name suggests.
    return (c << 32) | b


def parse_root(blob):
    """FileDataID -> content key.

    CASC holds no filenames. Every file is a number, and the mapping from
    number to name lives in a community listfile that is not part of the
    install - so a name is not something this can answer. What it can answer
    is which numbers exist, and an M2 carries its own name inside it.

    Blocks of records, each block headed by a count and two flag words: which
    locales it is for, and whether the files in it are the real thing or a
    placeholder. The ids are stored as deltas because they mostly run
    consecutively.
    """
    out = {}
    pos = 0
    total = len(blob)
    while pos + 12 <= total:
        count, _content_flags, _locale_flags = struct.unpack_from("<III", blob, pos)
        pos += 12
        if count == 0 or pos + count * 4 > total:
            break
        deltas = struct.unpack_from("<%di" % count, blob, pos)
        pos += count * 4
        file_id = -1
        for i in range(count):
            file_id += deltas[i] + 1
            entry = pos + i * 24
            if entry + 24 > total:
                break
            out.setdefault(file_id, (blob[entry:entry + 16],
                                     struct.unpack_from("<Q", blob, entry + 16)[0]))
        pos += count * 24
    return out


class CascStorage:
    def __init__(self, install_dir):
        self.data_dir = os.path.join(install_dir, "data", "data")
        _path, self.config = find_build_config(install_dir)
        self.index = IndexTable(self.data_dir)
        self._archives = {}
        self._encoding = None
        self._root = None
        self._by_hash = None

    @property
    def encoding(self):
        if self._encoding is None:
            self._encoding = parse_encoding(
                self.read_ekey(bytes.fromhex(self.config["encoding"][1])))
        return self._encoding

    @property
    def root(self):
        if self._root is None:
            self._root = parse_root(
                self.read_ckey(bytes.fromhex(self.config["root"][0])))
        return self._root

    def read_fileid(self, file_id, limit=0):
        found = self.root.get(file_id)
        return self.read_ckey(found[0], limit) if found else None

    @property
    def by_name_hash(self):
        if self._by_hash is None:
            self._by_hash = {}
            for fid, (_ckey, name_hash) in self.root.items():
                if name_hash:
                    self._by_hash.setdefault(name_hash, fid)
        return self._by_hash

    def read_path(self, path, limit=0):
        """A file by its path, which CASC itself does not store - see jenkins96."""
        fid = self.by_name_hash.get(jenkins96(path))
        return self.read_fileid(fid, limit) if fid else None

    def read_ckey(self, ckey, limit=0):
        ekey = self.encoding.get(ckey)
        return self.read_ekey(ekey, limit) if ekey else None

    def read_ekey(self, ekey, limit=0):
        found = self.index.find(ekey)
        if not found:
            return None
        archive, offset, size = found
        handle = self._archives.get(archive)
        if handle is None:
            handle = open(os.path.join(self.data_dir, "data.%03d" % archive), "rb")
            self._archives[archive] = handle
        handle.seek(offset)
        raw = handle.read(size)
        # Each archive entry carries a 30-byte header of its own before the
        # BLTE: the key backwards, the size, and two unknown fields.
        return blte_decode(raw[30:], limit)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    install = sys.argv[1]
    storage = CascStorage(install)
    print("build config keys:", sorted(storage.config)[:12])
    print("root  =", storage.config["root"][0])
    print("encoding =", storage.config["encoding"])
    print("index entries:", len(storage.index.entries))


if __name__ == "__main__":
    main()
