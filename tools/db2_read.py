#!/usr/bin/env python3
"""Read a WDC1 client database - the DB2 that replaced DBC in Legion.

Enough of the format to answer questions about the light tables: plain inline
records, an optional separate id list, the string table, and array fields
(which WDC1 does not count - a field's element size is given, and how many
elements it holds is the gap to the next field's offset).

Plainly bit-packed fields are handled - a table whose id column is packed into
the tail of each record is common, and LightParams is one. Pallet and
common-data storage are not; a table using those says so rather than returning
something quietly wrong.
"""

import struct


HEADER = "<IIIIIIIIIIIHHIIIIIIIII"
HEADER_KEYS = (
    "magic record_count field_count record_size string_table_size table_hash "
    "layout_hash min_id max_id locale copy_table_size flags id_index "
    "total_field_count bitpacked_offset lookup_column_count offset_map_offset "
    "id_list_size field_storage_info_size common_data_size pallet_data_size "
    "relationship_data_size").split()

FLAG_OFFSET_MAP = 0x01
FLAG_ID_LIST = 0x04


class Wdc1:
    def __init__(self, blob):
        if blob[:4] != b"WDC1":
            raise ValueError("not WDC1")
        self.blob = blob
        self.head = dict(zip(HEADER_KEYS, struct.unpack_from(HEADER, blob, 0)))
        if self.head["flags"] & FLAG_OFFSET_MAP:
            raise ValueError("offset-map records are not handled")
        if self.head["pallet_data_size"] or self.head["common_data_size"]:
            raise ValueError("pallet/common-data storage is not handled")

        count = self.head["field_count"]
        self.fields = [struct.unpack_from("<hH", blob, 84 + i * 4) for i in range(count)]
        self.record_data = 84 + count * 4
        self.string_table = self.record_data + \
            self.head["record_count"] * self.head["record_size"]
        self.ids = None
        after_strings = self.string_table + self.head["string_table_size"]
        if self.head["flags"] & FLAG_ID_LIST:
            n = self.head["id_list_size"] // 4
            self.ids = struct.unpack_from("<%dI" % n, blob, after_strings)

        # Where a field really lives, when the field structure says it has no
        # bits of its own. WDC1 puts this table last, after the records, the
        # strings, the id list and the copy table.
        info_at = (after_strings + self.head["id_list_size"]
                   + self.head["copy_table_size"])
        self.storage = []
        for i in range(self.head["field_storage_info_size"] // 24):
            offset_bits, size_bits, extra, kind = struct.unpack_from(
                "<HHII", blob, info_at + i * 24)
            self.storage.append((offset_bits, size_bits, extra, kind))

    def __len__(self):
        return self.head["record_count"]

    def field_span(self, index):
        """Element size and element count. WDC1 stores neither count directly:
        a field runs until the next field starts, or until the record ends."""
        size_bits, offset = self.fields[index]
        element = (32 - size_bits) // 8
        if index + 1 < len(self.fields):
            end = self.fields[index + 1][1]
        else:
            end = self.head["record_size"]
        return element, max(1, (end - offset) // max(element, 1))

    def bits(self, row, index):
        """A field packed into the record rather than given bytes of its own."""
        offset_bits, size_bits, _extra, _kind = self.storage[index]
        record = self.record_data + row * self.head["record_size"]
        first = record + offset_bits // 8
        chunk = int.from_bytes(
            self.blob[first:first + (size_bits + offset_bits % 8 + 7) // 8], "little")
        return (chunk >> (offset_bits % 8)) & ((1 << size_bits) - 1)

    def raw(self, row, index, element_index=0, signed=False):
        element, _count = self.field_span(index)
        if element == 0:
            return self.bits(row, index)
        at = (self.record_data + row * self.head["record_size"]
              + self.fields[index][1] + element_index * element)
        fmt = {1: "b", 2: "h", 4: "i"}[element] if signed else {1: "B", 2: "H", 4: "I"}[element]
        return struct.unpack_from("<" + fmt, self.blob, at)[0]

    def real(self, row, index, element_index=0):
        at = (self.record_data + row * self.head["record_size"]
              + self.fields[index][1] + element_index * 4)
        return struct.unpack_from("<f", self.blob, at)[0]

    def text(self, row, index):
        """A WDC1 string offset is relative to where the field itself sits."""
        at = self.record_data + row * self.head["record_size"] + self.fields[index][1]
        value = struct.unpack_from("<I", self.blob, at)[0]
        if not value:
            return ""
        start = at + value
        end = self.blob.index(b"\0", start)
        return self.blob[start:end].decode("latin-1")

    def row_id(self, row):
        if self.ids is not None:
            return self.ids[row]
        return self.raw(row, self.head["id_index"])
