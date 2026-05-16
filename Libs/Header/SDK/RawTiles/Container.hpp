/**
 ******************************************************************************
 * @file    Container.hpp
 * @date    15-05-2026
 * @author  Toby Murray <toby.murray@brilliantmade.com>
 * @brief   Reader for the rawtiles v1 binary tile-pack format.
 *
 * Implements the on-disk format defined at
 *   https://github.com/tobymurray/rawtiles (spec v0.2, wire format v1.0)
 * to the extent required by a Quadtree / WebMercator / ABGR2222 reader.
 *
 * The reader enforces every structural rejection rule in spec § 11 that gates
 * safe pointer arithmetic and the § 5.3 tile lookup (#1–#19, #25, #30, #31).
 * Semantic payload checks on extension sections (AFFN coefficient finiteness,
 * BCP-47 grammar, UTF-8 validity, per-tile padding bytes) are deferred until
 * a consumer needs them — they don't affect Quadtree tile rendering.
 ******************************************************************************
 */

#ifndef __SDK_RAWTILES_CONTAINER_HPP
#define __SDK_RAWTILES_CONTAINER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace SDK
{
namespace RawTiles
{

/**
 * @brief Pixel-format enum (header byte 56). See spec § 8.1.
 */
enum class PixelFormat : uint8_t {
    ABGR2222 = 1,
};

/**
 * @brief Projection enum (header byte 57). See spec § 8.2.
 */
enum class Projection : uint8_t {
    WebMercator = 1,
    LocalLinear = 3,
};

/**
 * @brief Tile-addressing-scheme enum (header byte 58). See spec § 8.3.
 */
enum class Addressing : uint8_t {
    Quadtree    = 1,
    SingleImage = 2,
};

/**
 * @brief Tile-axis convention (header byte 59). See spec § 8.4.
 */
enum class Axis : uint8_t {
    XYZ = 1, ///< Slippy-map default; y increases southward.
    TMS = 2, ///< gdal2tiles default; y increases northward.
};

/**
 * @brief Per-tile compression enum (tile-index byte 1). See spec § 8.5.
 */
enum class Compression : uint8_t {
    None = 0,
};

/**
 * @brief Decoded header fields (host byte order). Mirrors spec § 4.
 */
struct Header {
    uint8_t     formatMajor;     ///< Wire-format major. Always 1 in v1.
    uint8_t     formatMinor;     ///< Wire-format minor. 0 in v1.0.
    uint8_t     packUuid[16];    ///< Non-zero opaque pack identifier.
    uint8_t     supersedesUuid[16];
    PixelFormat pixelFormat;
    Projection  projection;
    Addressing  addressing;
    Axis        axis;
    uint16_t    tileDimPx;       ///< Pixel side length; non-zero.
    uint8_t     zoomMin;
    uint8_t     zoomMax;
    int32_t     bboxMinLonUDeg;  ///< Microdegrees (1e-6 °).
    int32_t     bboxMinLatUDeg;
    int32_t     bboxMaxLonUDeg;
    int32_t     bboxMaxLatUDeg;
    uint64_t    buildTimestamp;  ///< Unix epoch seconds; 0 = no freshness info.
    uint32_t    tileCount;
    uint32_t    indexOffset;     ///< Always 292 in v1.
    struct {
        uint32_t offset;
        uint32_t count;
    } zoomOffsets[24];
    uint32_t    extensionsOffset;
};

/**
 * @brief A view into the pack's tile blob. Lifetime is tied to the Container.
 */
struct TileRef {
    const uint8_t *data;   ///< nullptr if the tile is absent.
    uint32_t       length; ///< 0 if the tile is absent.

    /// True if the tile was found and decoded successfully.
    bool valid() const { return data != nullptr; }
};

/**
 * @brief Result codes for Container::open*. Anything other than @c Ok means
 *        the container is not safe to query.
 */
enum class OpenResult {
    Ok = 0,
    FileNotFound,
    FileTooShort,        ///< Less than 296 bytes (§ 11 #1).
    FileTooLarge,        ///< > 2^32 − 1 bytes (§ 11 #30).
    BadMagic,            ///< First 4 bytes ≠ "RAWT" (§ 11 #2).
    BadVersion,          ///< Major ≠ 1 (§ 11 #3).
    BadUuid,             ///< pack_uuid == 0 or parent_uuid ≠ 0 (§ 11 #5, #6).
    BadEnum,             ///< Unknown pixel/projection/addressing/axis/compression (§ 11 #7).
    BadEnumPair,         ///< Illegal projection × addressing pair (§ 11 #8).
    BadDimensions,       ///< tile_dim_px == 0 (§ 11 #9).
    BadZoomRange,        ///< zoom_max ≥ 24 or zoom_min > zoom_max (§ 11 #10).
    BadBbox,             ///< Out-of-range or inverted bbox (§ 11 #11).
    BadIndexOffset,      ///< index_offset ≠ 292 (§ 11 #25).
    BadIndexBounds,      ///< tile_count too large for file size.
    BadTileEntry,        ///< Reserved/flags non-zero, bad alignment, bad bounds (§ 11 #12, #14, #16, #31).
    BadTileOrder,        ///< Entries not strictly ascending by (z, x, y) (§ 11 #13).
    BadTileZoom,         ///< Entry z < zoom_min or > zoom_max (§ 11 #15).
    BadZoomDirectory,    ///< zoom_offsets[z] inconsistent with walked index (§ 11 #17).
    BadExtensionsOffset, ///< extensions_offset misaligned or out of range (§ 11 #18).
    BadExtensionFraming, ///< Extension-section framing violates § 7.1 (§ 11 #19).
    BadExtensionTag,     ///< Unknown upper-case tag or invalid tag bytes (§ 11 #20, #27, #28).
    BadSingleImage,      ///< SingleImage rules violated (§ 11 #23).
    CrcMismatch,         ///< Footer CRC-32 doesn't match body (§ 11 #24).
    IoError,
};

/**
 * @brief Reads, validates, and serves tiles from a v1 rawtiles pack.
 *
 * Usage:
 * @code
 *   SDK::RawTiles::Container c;
 *   if (c.openFromFile("stanley.rawtiles") != OpenResult::Ok) { ... }
 *   auto tile = c.getTile(13, 1306, 2825);
 *   if (tile.valid()) {
 *       // tile.data points at tile_dim_px * tile_dim_px ABGR2222 bytes.
 *   }
 * @endcode
 *
 * Memory: the entire pack is held in RAM. At ~770 KB for typical neighbourhood
 * packs (stanley.rawtiles) this is fine for the simulator and the embedded
 * target; for larger packs a streaming / mmap variant is a future addition.
 */
class Container {
public:
    Container() = default;
    ~Container() = default;

    Container(const Container&)            = delete;
    Container& operator=(const Container&) = delete;
    Container(Container&&)                 = default;
    Container& operator=(Container&&)      = default;

    /**
     * @brief Opens a pack from a filesystem path.
     * @param path: NUL-terminated path; the entire file is read into RAM.
     * @return @c OpenResult::Ok on success; the container is closed on failure.
     */
    OpenResult openFromFile(const char *path);

    /**
     * @brief Opens a pack from an in-memory byte buffer (for tests / fixtures).
     * @param data: Pointer to pack bytes; copied into the container.
     * @param size: Length of @p data in bytes.
     */
    OpenResult openFromMemory(const uint8_t *data, std::size_t size);

    /**
     * @brief Releases the in-memory pack and resets header state.
     */
    void close();

    /**
     * @brief @c true if a valid pack is currently open.
     */
    bool isOpen() const { return !mBytes.empty(); }

    /**
     * @brief Returns the decoded header. Only valid when @c isOpen() is true.
     */
    const Header& header() const { return mHeader; }

    /**
     * @brief Looks up the tile at @p (z, x, y) per spec § 5.3.
     * @return A @c TileRef pointing into the pack's tile blob; @c valid() is
     *         @c false when the tile is absent or @p z is out of range.
     */
    TileRef getTile(uint8_t z, uint32_t x, uint32_t y) const;

    /**
     * @brief Number of tile-index entries at zoom @p z (0 if z ≥ 24).
     */
    uint32_t tileCountAtZoom(uint8_t z) const;

    /**
     * @brief Human-readable diagnostic for an @c OpenResult.
     */
    static const char* describeResult(OpenResult r);

private:
    std::vector<uint8_t> mBytes;
    Header               mHeader { };

    OpenResult parseAndValidate();
    OpenResult verifyCrc() const;
};

} // namespace RawTiles
} // namespace SDK

#endif // __SDK_RAWTILES_CONTAINER_HPP
