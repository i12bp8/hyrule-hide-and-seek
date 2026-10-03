#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace hs::rarc {

// Read an immutable disc archive without mounting it in JKR's global volume list. Mounting a
// second AlAnm archive allocates another file-pointer table from Dusklight's tiny System heap,
// leaving too little space to load crowded interiors such as Telma's Bar.
inline std::span<const uint8_t> resource_at(std::span<const uint8_t> bytes, uint32_t index) {
    if (bytes.size() < 0x40) return {};
    const auto word = [&](size_t offset) -> uint32_t {
        return (uint32_t{bytes[offset]} << 24) | (uint32_t{bytes[offset + 1]} << 16) |
               (uint32_t{bytes[offset + 2]} << 8) | bytes[offset + 3];
    };
    if (word(0) != 0x52415243) return {};  // RARC
    const size_t length = word(4);
    const size_t header = word(8);
    if (length < 0x40 || length > bytes.size() || header < 0x20 || header > length - 0x20) return {};
    const size_t count = word(header + 8);
    const size_t tableOffset = word(header + 12);
    const size_t dataOffset = word(12);
    const size_t dataLength = word(16);
    if (tableOffset > length - header || dataOffset > length - header) return {};
    const size_t table = header + tableOffset;
    const size_t data = header + dataOffset;
    if (count > (length - table) / 0x14 || index >= count || dataLength > length - data) return {};
    const size_t entry = table + size_t{index} * 0x14;
    const uint32_t flags = word(entry + 4) >> 24;
    if (!(flags & 1) || (flags & 2)) return {};  // files only, never directory indices
    const size_t offset = word(entry + 8);
    const size_t size = word(entry + 12);
    if (offset > dataLength || size > dataLength - offset) return {};
    return bytes.subspan(data + offset, size);
}

}  // namespace hs::rarc
