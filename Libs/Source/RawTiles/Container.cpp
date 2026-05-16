/**
 ******************************************************************************
 * @file    Container.cpp
 * @date    15-05-2026
 * @author  Toby Murray <toby.murray@brilliantmade.com>
 * @brief   Implementation of SDK::RawTiles::Container.
 *
 * Spec references in comments use the section numbers from rawtiles v0.2
 * (wire format v1.0).
 ******************************************************************************
 */

#include "SDK/RawTiles/Container.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace SDK
{
namespace RawTiles
{
namespace
{

constexpr std::size_t kHeaderSize        = 292;
constexpr std::size_t kFooterSize        = 4;
constexpr std::size_t kMinFileSize       = kHeaderSize + kFooterSize; // § 11 #1
constexpr std::size_t kIndexEntrySize    = 20;                        // § 5.1
constexpr std::size_t kZoomDirCount      = 24;                        // § 4.12
constexpr uint64_t    kMaxFileSize       = 0xFFFFFFFFu;               // § 11 #30

inline uint32_t align4(uint32_t n)
{
    return (n + 3u) & ~uint32_t { 3u };
}

inline uint16_t readU16LE(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t readU32LE(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t readI32LE(const uint8_t *p)
{
    return static_cast<int32_t>(readU32LE(p));
}

inline uint64_t readU64LE(const uint8_t *p)
{
    uint64_t lo = readU32LE(p);
    uint64_t hi = readU32LE(p + 4);
    return lo | (hi << 32);
}

bool uuidIsZero(const uint8_t *u)
{
    for (int i = 0; i < 16; ++i) {
        if (u[i] != 0) {
            return false;
        }
    }
    return true;
}

/// CRC-32/ISO-HDLC (the PNG/zlib variant; spec § 10).
/// Reflected polynomial 0xEDB88320, initial = 0xFFFFFFFF, final XOR = 0xFFFFFFFF.
uint32_t crc32IsoHdlc(const uint8_t *data, std::size_t length)
{
    static uint32_t table[256];
    static bool     tableReady = false;
    if (!tableReady) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        tableReady = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

/// True if the (projection, addressing) byte pair is one of the two legal v1
/// combinations per § 8.6.
bool isLegalEnumPair(Projection proj, Addressing addr)
{
    return (proj == Projection::WebMercator && addr == Addressing::Quadtree)
        || (proj == Projection::LocalLinear && addr == Addressing::SingleImage);
}

} // namespace

OpenResult Container::openFromFile(const char *path)
{
    close();

    std::FILE *f = std::fopen(path, "rb");
    if (!f) {
        return OpenResult::FileNotFound;
    }

    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return OpenResult::IoError;
    }
    long sz = std::ftell(f);
    if (sz < 0) {
        std::fclose(f);
        return OpenResult::IoError;
    }
    if (static_cast<uint64_t>(sz) > kMaxFileSize) {
        std::fclose(f);
        return OpenResult::FileTooLarge;
    }
    if (std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return OpenResult::IoError;
    }

    mBytes.resize(static_cast<std::size_t>(sz));
    std::size_t got = std::fread(mBytes.data(), 1, mBytes.size(), f);
    std::fclose(f);
    if (got != mBytes.size()) {
        close();
        return OpenResult::IoError;
    }

    OpenResult r = parseAndValidate();
    if (r != OpenResult::Ok) {
        close();
    }
    return r;
}

OpenResult Container::openFromMemory(const uint8_t *data, std::size_t size)
{
    close();
    if (static_cast<uint64_t>(size) > kMaxFileSize) {
        return OpenResult::FileTooLarge;
    }
    mBytes.assign(data, data + size);
    OpenResult r = parseAndValidate();
    if (r != OpenResult::Ok) {
        close();
    }
    return r;
}

void Container::close()
{
    mBytes.clear();
    mBytes.shrink_to_fit();
    mHeader = Header { };
}

OpenResult Container::parseAndValidate()
{
    const std::size_t size = mBytes.size();
    const uint8_t    *base = mBytes.data();

    // § 11 #1: minimum size.
    if (size < kMinFileSize) {
        return OpenResult::FileTooShort;
    }

    // § 11 #2: magic.
    if (!(base[0] == 'R' && base[1] == 'A' && base[2] == 'W' && base[3] == 'T')) {
        return OpenResult::BadMagic;
    }

    // § 11 #3 / #4: version.
    mHeader.formatMajor = base[4];
    mHeader.formatMinor = base[5];
    if (mHeader.formatMajor != 1) {
        return OpenResult::BadVersion;
    }
    // Bytes 6-7 are reserved_v1_0; spec § 4 says readers MUST accept any value.

    // § 11 #5: pack_uuid != 0.
    std::memcpy(mHeader.packUuid, base + 8, 16);
    if (uuidIsZero(mHeader.packUuid)) {
        return OpenResult::BadUuid;
    }
    std::memcpy(mHeader.supersedesUuid, base + 24, 16);
    // § 11 #6: parent_uuid (bytes 40..55) MUST be all-zero.
    for (int i = 40; i < 56; ++i) {
        if (base[i] != 0) {
            return OpenResult::BadUuid;
        }
    }

    // § 11 #7: enum bytes.
    const uint8_t pixByte  = base[56];
    const uint8_t projByte = base[57];
    const uint8_t addrByte = base[58];
    const uint8_t axisByte = base[59];
    if (pixByte != 1) {
        return OpenResult::BadEnum;
    }
    if (projByte != 1 && projByte != 3) {
        return OpenResult::BadEnum;
    }
    if (addrByte != 1 && addrByte != 2) {
        return OpenResult::BadEnum;
    }
    if (axisByte != 1 && axisByte != 2) {
        return OpenResult::BadEnum;
    }
    mHeader.pixelFormat = static_cast<PixelFormat>(pixByte);
    mHeader.projection  = static_cast<Projection>(projByte);
    mHeader.addressing  = static_cast<Addressing>(addrByte);
    mHeader.axis        = static_cast<Axis>(axisByte);

    // § 11 #8: legal projection × addressing pair.
    if (!isLegalEnumPair(mHeader.projection, mHeader.addressing)) {
        return OpenResult::BadEnumPair;
    }

    // § 11 #9: tile_dim_px > 0.
    mHeader.tileDimPx = readU16LE(base + 60);
    if (mHeader.tileDimPx == 0) {
        return OpenResult::BadDimensions;
    }

    // § 11 #10: zoom range.
    mHeader.zoomMin = base[62];
    mHeader.zoomMax = base[63];
    if (mHeader.zoomMax >= kZoomDirCount || mHeader.zoomMin > mHeader.zoomMax) {
        return OpenResult::BadZoomRange;
    }

    // § 11 #11: bbox ranges.
    mHeader.bboxMinLonUDeg = readI32LE(base + 64);
    mHeader.bboxMinLatUDeg = readI32LE(base + 68);
    mHeader.bboxMaxLonUDeg = readI32LE(base + 72);
    mHeader.bboxMaxLatUDeg = readI32LE(base + 76);
    if (mHeader.bboxMinLonUDeg < -180000000 || mHeader.bboxMinLonUDeg > 180000000) {
        return OpenResult::BadBbox;
    }
    if (mHeader.bboxMaxLonUDeg < -180000000 || mHeader.bboxMaxLonUDeg > 180000000) {
        return OpenResult::BadBbox;
    }
    if (mHeader.bboxMinLatUDeg < -90000000 || mHeader.bboxMinLatUDeg > 90000000) {
        return OpenResult::BadBbox;
    }
    if (mHeader.bboxMaxLatUDeg < -90000000 || mHeader.bboxMaxLatUDeg > 90000000) {
        return OpenResult::BadBbox;
    }
    if (mHeader.bboxMinLonUDeg > mHeader.bboxMaxLonUDeg
            || mHeader.bboxMinLatUDeg > mHeader.bboxMaxLatUDeg) {
        return OpenResult::BadBbox;
    }

    mHeader.buildTimestamp = readU64LE(base + 80);
    mHeader.tileCount      = readU32LE(base + 88);
    mHeader.indexOffset    = readU32LE(base + 92);

    // § 11 #25: v1 fixes index_offset at 292.
    if (mHeader.indexOffset != kHeaderSize) {
        return OpenResult::BadIndexOffset;
    }

    // Allocation-ordering check from § 11 prologue: validate the tile index
    // fits in the file BEFORE trusting tile_count. Use u32-safe form.
    if (size < kMinFileSize) {
        return OpenResult::FileTooShort;
    }
    const uint64_t indexCapacity = (size - kMinFileSize) / kIndexEntrySize;
    if (mHeader.tileCount > indexCapacity) {
        return OpenResult::BadIndexBounds;
    }

    // zoom_offsets[24]: 192 bytes starting at offset 96.
    for (std::size_t z = 0; z < kZoomDirCount; ++z) {
        const uint8_t *e = base + 96 + (z * 8);
        mHeader.zoomOffsets[z].offset = readU32LE(e);
        mHeader.zoomOffsets[z].count  = readU32LE(e + 4);
    }
    mHeader.extensionsOffset = readU32LE(base + 288);

    // tile_blob_start = align4(index_offset + 20 * tile_count). With
    // index_offset = 292 (4-aligned) and 20-byte entries, this is already
    // 4-aligned for any tile_count.
    const uint64_t indexBytes64    = static_cast<uint64_t>(mHeader.tileCount) * kIndexEntrySize;
    const uint64_t tileBlobStart64 = static_cast<uint64_t>(mHeader.indexOffset) + indexBytes64;
    if (tileBlobStart64 > size) {
        return OpenResult::BadIndexBounds;
    }
    const uint32_t tileBlobStart = static_cast<uint32_t>(tileBlobStart64);

    // § 11 #18 prerequisite: extensions_offset alignment and ordering.
    if ((mHeader.extensionsOffset & 3u) != 0) {
        return OpenResult::BadExtensionsOffset;
    }
    if (mHeader.extensionsOffset > size - kFooterSize) {
        return OpenResult::BadExtensionsOffset;
    }
    if (mHeader.extensionsOffset < tileBlobStart) {
        return OpenResult::BadExtensionsOffset;
    }

    // Walk the tile index. § 11 #12, #13, #14, #15, #16, #31.
    // Track zoom-directory consistency while walking. § 11 #17.
    uint32_t walkedPerZoom[kZoomDirCount]   = { };
    uint32_t firstOffsetPerZoom[kZoomDirCount] = { };
    bool     zoomSeen[kZoomDirCount]        = { };

    uint64_t expectedTileOffset = tileBlobStart;
    uint8_t  prevZ = 0;
    uint32_t prevX = 0;
    uint32_t prevY = 0;
    bool     havePrev = false;

    for (uint32_t i = 0; i < mHeader.tileCount; ++i) {
        const uint8_t *e = base + mHeader.indexOffset + (i * kIndexEntrySize);
        const uint8_t  z = e[0];
        const uint8_t  comp = e[1];
        const uint8_t  flags = e[2];
        const uint8_t  reserved = e[3];
        const uint32_t x = readU32LE(e + 4);
        const uint32_t y = readU32LE(e + 8);
        const uint32_t offset = readU32LE(e + 12);
        const uint32_t length = readU32LE(e + 16);

        // § 11 #7: compression byte (v1 allows only 0).
        if (comp != static_cast<uint8_t>(Compression::None)) {
            return OpenResult::BadEnum;
        }
        // § 11 #12: flags/reserved.
        if (flags != 0 || reserved != 0) {
            return OpenResult::BadTileEntry;
        }
        // § 11 #15: z within declared range.
        if (z < mHeader.zoomMin || z > mHeader.zoomMax) {
            return OpenResult::BadTileZoom;
        }
        if (z >= kZoomDirCount) {
            return OpenResult::BadTileZoom;
        }
        // § 11 #31: (x, y) bounded by 2^z for Quadtree.
        if (mHeader.addressing == Addressing::Quadtree) {
            const uint64_t maxCoord = (z < 32) ? (uint64_t { 1 } << z) : 0;
            if (x >= maxCoord || y >= maxCoord) {
                return OpenResult::BadTileEntry;
            }
        }
        // § 11 #13: ascending (z, x, y).
        if (havePrev) {
            if (z < prevZ) {
                return OpenResult::BadTileOrder;
            }
            if (z == prevZ) {
                if (x < prevX || (x == prevX && y <= prevY)) {
                    return OpenResult::BadTileOrder;
                }
            }
        }
        // § 11 #14: offset alignment + bounds + length bound.
        if ((offset & 3u) != 0) {
            return OpenResult::BadTileEntry;
        }
        if (offset < tileBlobStart) {
            return OpenResult::BadTileEntry;
        }
        if (offset >= mHeader.extensionsOffset) {
            return OpenResult::BadTileEntry;
        }
        // u64-safe form, equivalent to (c)+(d) in spec.
        if (static_cast<uint64_t>(offset) + length > mHeader.extensionsOffset) {
            return OpenResult::BadTileEntry;
        }
        // § 11 #32: offset must equal the tight tile-blob layout.
        if (offset != expectedTileOffset) {
            return OpenResult::BadTileEntry;
        }
        // § 11 #16: v1 (ABGR2222, None) demands length == tile_dim_px^2.
        const uint64_t expectedLen = static_cast<uint64_t>(mHeader.tileDimPx)
                                   * static_cast<uint64_t>(mHeader.tileDimPx);
        if (length != expectedLen) {
            return OpenResult::BadTileEntry;
        }

        // Track per-zoom first offset / counts for § 11 #17.
        if (!zoomSeen[z]) {
            zoomSeen[z]            = true;
            firstOffsetPerZoom[z]  = static_cast<uint32_t>(
                    mHeader.indexOffset + (i * kIndexEntrySize));
        }
        ++walkedPerZoom[z];

        expectedTileOffset += align4(length);

        prevZ = z;
        prevX = x;
        prevY = y;
        havePrev = true;
    }

    // § 11 #18: extensions_offset = tile_blob_start + Σ padded_length(i).
    if (expectedTileOffset != mHeader.extensionsOffset) {
        return OpenResult::BadExtensionsOffset;
    }

    // § 11 #17: zoom_offsets[z] consistent with the walked index.
    for (std::size_t z = 0; z < kZoomDirCount; ++z) {
        const uint32_t walked = walkedPerZoom[z];
        if (mHeader.zoomOffsets[z].count != walked) {
            return OpenResult::BadZoomDirectory;
        }
        if (walked == 0) {
            if (mHeader.zoomOffsets[z].offset != 0) {
                return OpenResult::BadZoomDirectory;
            }
        } else {
            if (mHeader.zoomOffsets[z].offset != firstOffsetPerZoom[z]) {
                return OpenResult::BadZoomDirectory;
            }
        }
    }

    // § 11 #23: SingleImage structural rules.
    if (mHeader.addressing == Addressing::SingleImage) {
        if (mHeader.tileCount != 1) {
            return OpenResult::BadSingleImage;
        }
        if (mHeader.zoomMin != 0 || mHeader.zoomMax != 0) {
            return OpenResult::BadSingleImage;
        }
        if (mHeader.axis != Axis::XYZ) {
            return OpenResult::BadSingleImage;
        }
        const uint8_t *e = base + mHeader.indexOffset;
        if (e[0] != 0 || readU32LE(e + 4) != 0 || readU32LE(e + 8) != 0) {
            return OpenResult::BadSingleImage;
        }
        if (mHeader.zoomOffsets[0].count != 1
                || mHeader.zoomOffsets[0].offset != mHeader.indexOffset) {
            return OpenResult::BadSingleImage;
        }
        for (std::size_t z = 1; z < kZoomDirCount; ++z) {
            if (mHeader.zoomOffsets[z].offset != 0 || mHeader.zoomOffsets[z].count != 0) {
                return OpenResult::BadSingleImage;
            }
        }
    }

    // § 11 #19: walk extension sections (framing-level only). Reject unknown
    // upper-case tags (§ 11 #20). Defer payload-level checks (#22, #34-#38).
    uint32_t pos = mHeader.extensionsOffset;
    const uint32_t crcStart = static_cast<uint32_t>(size - kFooterSize);
    while (pos < crcStart) {
        if (crcStart - pos < 8) {
            return OpenResult::BadExtensionFraming;
        }
        const uint8_t *tag = base + pos;
        // § 11 #27 / #28: tag byte 1 in [A-Z, a-z], bytes 2-4 printable ASCII.
        const uint8_t  b0 = tag[0];
        const bool upper = (b0 >= 'A' && b0 <= 'Z');
        const bool lower = (b0 >= 'a' && b0 <= 'z');
        if (!upper && !lower) {
            return OpenResult::BadExtensionTag;
        }
        for (int j = 1; j < 4; ++j) {
            if (tag[j] < 0x20 || tag[j] > 0x7E) {
                return OpenResult::BadExtensionTag;
            }
        }
        const uint32_t length = readU32LE(tag + 4);
        // § 11 #19: overflow-safe upper bound.
        if (length > (crcStart - pos - 8)) {
            return OpenResult::BadExtensionFraming;
        }
        const uint32_t paddedLen = align4(length);
        if (paddedLen > (crcStart - pos - 8)) {
            return OpenResult::BadExtensionFraming;
        }
        // Padding bytes after payload MUST be 0x00.
        for (uint32_t p = length; p < paddedLen; ++p) {
            if (tag[8 + p] != 0) {
                return OpenResult::BadExtensionFraming;
            }
        }
        // § 11 #20: reject unknown upper-case tags. v1 reserves NAME, SRCD,
        // ATTR, AFFN. We accept these framing-only; deeper payload validation
        // is a TODO.
        if (upper) {
            const bool known =
                   (tag[0] == 'N' && tag[1] == 'A' && tag[2] == 'M' && tag[3] == 'E')
                || (tag[0] == 'S' && tag[1] == 'R' && tag[2] == 'C' && tag[3] == 'D')
                || (tag[0] == 'A' && tag[1] == 'T' && tag[2] == 'T' && tag[3] == 'R')
                || (tag[0] == 'A' && tag[1] == 'F' && tag[2] == 'F' && tag[3] == 'N');
            if (!known) {
                return OpenResult::BadExtensionTag;
            }
        }
        pos += 8 + paddedLen;
    }
    // Last section's padded end MUST equal file_size − 4.
    if (pos != crcStart) {
        return OpenResult::BadExtensionFraming;
    }

    // § 11 #24: CRC-32 footer.
    OpenResult crcRes = verifyCrc();
    if (crcRes != OpenResult::Ok) {
        return crcRes;
    }

    return OpenResult::Ok;
}

OpenResult Container::verifyCrc() const
{
    const std::size_t size      = mBytes.size();
    const uint8_t    *base      = mBytes.data();
    const uint32_t    storedCrc = readU32LE(base + size - kFooterSize);
    const uint32_t    actualCrc = crc32IsoHdlc(base, size - kFooterSize);
    return (storedCrc == actualCrc) ? OpenResult::Ok : OpenResult::CrcMismatch;
}

TileRef Container::getTile(uint8_t z, uint32_t x, uint32_t y) const
{
    if (!isOpen()) {
        return TileRef { nullptr, 0 };
    }
    // § 5.3 step 1: callers passing z ≥ 24 MUST NOT index past the directory.
    if (z >= kZoomDirCount) {
        return TileRef { nullptr, 0 };
    }
    const auto &dir = mHeader.zoomOffsets[z];
    if (dir.count == 0) {
        return TileRef { nullptr, 0 };
    }

    // Binary search the (x, y)-ordered slice of the index for this zoom.
    const uint8_t *base   = mBytes.data();
    const uint8_t *first  = base + dir.offset;

    uint32_t lo = 0;
    uint32_t hi = dir.count;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        const uint8_t *e   = first + (mid * kIndexEntrySize);
        const uint32_t mx  = readU32LE(e + 4);
        const uint32_t my  = readU32LE(e + 8);
        if (mx < x || (mx == x && my < y)) {
            lo = mid + 1;
        } else if (mx > x || (mx == x && my > y)) {
            hi = mid;
        } else {
            const uint32_t off = readU32LE(e + 12);
            const uint32_t len = readU32LE(e + 16);
            return TileRef { base + off, len };
        }
    }
    return TileRef { nullptr, 0 };
}

TileEntry Container::getTileByIndex(uint32_t i) const
{
    TileEntry out { 0, 0, 0, TileRef { nullptr, 0 } };
    if (!isOpen() || i >= mHeader.tileCount) {
        return out;
    }
    const uint8_t *e = mBytes.data() + mHeader.indexOffset + (i * kIndexEntrySize);
    out.z         = e[0];
    out.x         = readU32LE(e + 4);
    out.y         = readU32LE(e + 8);
    const uint32_t off = readU32LE(e + 12);
    const uint32_t len = readU32LE(e + 16);
    out.tile.data   = mBytes.data() + off;
    out.tile.length = len;
    return out;
}

uint32_t Container::tileCountAtZoom(uint8_t z) const
{
    if (!isOpen() || z >= kZoomDirCount) {
        return 0;
    }
    return mHeader.zoomOffsets[z].count;
}

const char* Container::describeResult(OpenResult r)
{
    switch (r) {
        case OpenResult::Ok:                   return "ok";
        case OpenResult::FileNotFound:         return "file not found";
        case OpenResult::FileTooShort:         return "file shorter than 296 bytes (§ 11 #1)";
        case OpenResult::FileTooLarge:         return "file > 2^32-1 bytes (§ 11 #30)";
        case OpenResult::BadMagic:             return "magic ≠ 'RAWT' (§ 11 #2)";
        case OpenResult::BadVersion:           return "format_version_major ≠ 1 (§ 11 #3)";
        case OpenResult::BadUuid:              return "pack_uuid == 0 or parent_uuid ≠ 0 (§ 11 #5/#6)";
        case OpenResult::BadEnum:              return "unknown enum byte (§ 11 #7)";
        case OpenResult::BadEnumPair:          return "illegal projection × addressing pair (§ 11 #8)";
        case OpenResult::BadDimensions:        return "tile_dim_px == 0 (§ 11 #9)";
        case OpenResult::BadZoomRange:         return "zoom_max ≥ 24 or zoom_min > zoom_max (§ 11 #10)";
        case OpenResult::BadBbox:              return "bbox out of range or inverted (§ 11 #11)";
        case OpenResult::BadIndexOffset:       return "index_offset ≠ 292 (§ 11 #25)";
        case OpenResult::BadIndexBounds:       return "tile_count exceeds file bounds";
        case OpenResult::BadTileEntry:         return "tile-index entry violates § 11 #12/#14/#16/#31/#32";
        case OpenResult::BadTileOrder:         return "tile index not strictly ascending (§ 11 #13)";
        case OpenResult::BadTileZoom:          return "tile z outside [zoom_min, zoom_max] (§ 11 #15)";
        case OpenResult::BadZoomDirectory:     return "zoom_offsets[z] inconsistent with index (§ 11 #17)";
        case OpenResult::BadExtensionsOffset:  return "extensions_offset misaligned or wrong (§ 11 #18)";
        case OpenResult::BadExtensionFraming:  return "extension-section framing violates § 7.1 (§ 11 #19)";
        case OpenResult::BadExtensionTag:      return "unknown upper-case tag or invalid tag bytes (§ 11 #20/#27/#28)";
        case OpenResult::BadSingleImage:       return "SingleImage structural rules violated (§ 11 #23)";
        case OpenResult::CrcMismatch:          return "CRC-32 footer mismatch (§ 11 #24)";
        case OpenResult::IoError:              return "I/O error";
    }
    return "unknown";
}

} // namespace RawTiles
} // namespace SDK
