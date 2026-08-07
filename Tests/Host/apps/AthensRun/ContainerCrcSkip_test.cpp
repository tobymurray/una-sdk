/**
 * @file    ContainerCrcSkip_test.cpp
 * @brief   Host tests for Container's local-only skipCrcVerify / declaredCrc32
 *          surface (see the "Background CRC verification for the AthensRun
 *          map pack" design doc). This is NOT part of the vendored reader's
 *          upstream conformance evidence (feat/rawtiles-container) -- it
 *          tests only the additive Caller-asserted-trust (spec § 10) opt-in
 *          added locally on poc/athensrun.
 *
 * Builds the smallest legal rawtiles v1 pack by hand: a 292-byte header
 * (Quadtree/WebMercator, zero tiles, zero extensions) plus a 4-byte CRC-32
 * footer. With tile_count == 0, the tile-index walk and zoom_offsets checks
 * degenerate to "everything must be zero", and extensions_offset must equal
 * tile_blob_start (== index_offset == 292, spec § 11 #18).
 */
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "SDK/RawTiles/Container.hpp"

namespace {

using SDK::RawTiles::Container;
using SDK::RawTiles::OpenResult;

constexpr size_t kHeaderSize = 292;
constexpr size_t kFooterSize = 4;
constexpr size_t kPackSize   = kHeaderSize + kFooterSize;

void writeU16LE(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); }
void writeU32LE(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}
void writeI32LE(uint8_t* p, int32_t v) { writeU32LE(p, static_cast<uint32_t>(v)); }
void writeU64LE(uint8_t* p, uint64_t v)
{
    writeU32LE(p, static_cast<uint32_t>(v));
    writeU32LE(p + 4, static_cast<uint32_t>(v >> 32));
}

// CRC-32/ISO-HDLC, independent of Container.cpp's own copy -- this test
// deliberately re-derives the checksum rather than importing the
// implementation under test, and cross-checks it against the spec's own
// pinned vector below before trusting it for the fixture.
uint32_t crc32(const uint8_t* data, size_t length)
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

TEST(ContainerCrcSkip, Crc32MatchesSpecPinnedTestVector)
{
    // rawtiles spec § 10: check value for ASCII "123456789" is 0xCBF43926.
    const uint8_t input[] = "123456789";
    EXPECT_EQ(crc32(input, 9), 0xCBF43926u);
}

// Builds a minimal valid pack (see file doc comment) and returns it with a
// correct footer CRC already written.
std::vector<uint8_t> buildMinimalPack()
{
    std::vector<uint8_t> pack(kPackSize, 0);
    uint8_t* h = pack.data();

    h[0] = 'R'; h[1] = 'A'; h[2] = 'W'; h[3] = 'T'; // magic
    h[4] = 1;                                       // format_major
    h[5] = 0;                                       // format_minor
    // bytes 6-7 reserved, left 0.
    for (int i = 0; i < 16; ++i) {
        h[8 + i] = static_cast<uint8_t>(0xA0 + i);  // pack_uuid: non-zero
    }
    // supersedes_uuid (24..39) and parent_uuid (40..55) left all-zero: legal.
    h[56] = 1; // pixel_format = ABGR2222
    h[57] = 1; // projection   = WebMercator
    h[58] = 1; // addressing   = Quadtree
    h[59] = 1; // axis         = XYZ
    writeU16LE(h + 60, 64); // tile_dim_px
    h[62] = 0; // zoom_min
    h[63] = 0; // zoom_max
    // bbox: a small box around Athens, Ontario (matches MapMath_test.cpp's
    // reference village-center value) -- min <= max on both axes.
    writeI32LE(h + 64, -75960000); // bboxMinLon
    writeI32LE(h + 68,  44620000); // bboxMinLat
    writeI32LE(h + 72, -75950000); // bboxMaxLon
    writeI32LE(h + 76,  44630000); // bboxMaxLat
    writeU64LE(h + 80, 0);         // build_timestamp: 0 = no freshness info
    writeU32LE(h + 88, 0);         // tile_count = 0
    writeU32LE(h + 92, 292);       // index_offset = 292 (fixed in v1)
    // zoom_offsets[24] (96..287): all-zero, required when every zoom's
    // walked count is 0 (spec § 11 #17).
    writeU32LE(h + 288, 292);      // extensions_offset == tile_blob_start

    const uint32_t crc = crc32(h, kHeaderSize);
    writeU32LE(h + kHeaderSize, crc);
    return pack;
}

TEST(ContainerCrcSkip, EagerVerifyStillPassesOnValidPack)
{
    std::vector<uint8_t> pack = buildMinimalPack();
    Container c;
    // Default skipCrcVerify=false: unchanged eager-verify behavior.
    EXPECT_EQ(c.openFromMemory(pack.data(), pack.size()), OpenResult::Ok);
}

TEST(ContainerCrcSkip, EagerVerifyRejectsCorruptedFooter)
{
    std::vector<uint8_t> pack = buildMinimalPack();
    pack[kHeaderSize] ^= 0xFF; // flip a footer byte -> declared CRC no longer matches
    Container c;
    EXPECT_EQ(c.openFromMemory(pack.data(), pack.size()), OpenResult::CrcMismatch);
}

TEST(ContainerCrcSkip, SkipCrcVerifyOpensDespiteCorruptedFooter)
{
    std::vector<uint8_t> pack = buildMinimalPack();
    pack[kHeaderSize] ^= 0xFF; // same corruption as above
    Container c;
    // skipCrcVerify=true: structural fields are untouched by a footer-only
    // corruption, so every other § 11 rule still passes and open succeeds --
    // this is the "Caller-asserted trust" path (spec § 10), never meant to
    // be used on unverified bytes, but exercised here to prove the flag
    // actually skips only the CRC step and nothing else.
    EXPECT_EQ(c.openFromMemory(pack.data(), pack.size(), /*skipCrcVerify=*/true), OpenResult::Ok);
}

TEST(ContainerCrcSkip, SkipCrcVerifyStillRejectsStructuralCorruption)
{
    std::vector<uint8_t> pack = buildMinimalPack();
    pack[0] = 'X'; // corrupt the magic -- a structural rule, not the CRC
    Container c;
    EXPECT_EQ(c.openFromMemory(pack.data(), pack.size(), /*skipCrcVerify=*/true), OpenResult::BadMagic);
}

TEST(ContainerCrcSkip, DeclaredCrc32AndPackSizeMatchTheRealValues)
{
    std::vector<uint8_t> pack = buildMinimalPack();
    const uint32_t expectedCrc = crc32(pack.data(), kHeaderSize);

    Container c;
    ASSERT_EQ(c.openFromMemory(pack.data(), pack.size()), OpenResult::Ok);

    EXPECT_EQ(c.packSize(), pack.size());

    uint32_t declared = 0;
    ASSERT_TRUE(c.declaredCrc32(declared));
    EXPECT_EQ(declared, expectedCrc);
}

TEST(ContainerCrcSkip, DeclaredCrc32AndPackSizeAreUnavailableWhenNotOpen)
{
    Container c;
    EXPECT_EQ(c.packSize(), 0u);
    uint32_t declared = 0;
    EXPECT_FALSE(c.declaredCrc32(declared));
}

} // namespace
