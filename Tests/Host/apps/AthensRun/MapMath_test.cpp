/**
 * @file    MapMath_test.cpp
 * @brief   Host tests for AthensRun's WebMercator world-pixel math.
 *
 * Reference values are computed independently from the standard slippy-map
 * formulas (x = (lon+180)/360 * 2^z, y = (1 - asinh(tan lat)/pi)/2 * 2^z,
 * scaled by 256 px/tile) — the same formulas the rawtiles spec § 4.9 uses
 * for canonical bbox derivation, so the pack's tiles and this math agree
 * on where the world is.
 */
#include <gui/map/MapMath.hpp>

#include <gtest/gtest.h>

using namespace AthensRun::MapMath;

namespace
{

// Athens, Ontario village center (Nominatim, OSM relation 7434060).
constexpr int32_t kAthensLat = 44625942;
constexpr int32_t kAthensLon = -75952256;

TEST(MapMath, OriginAndAntiOriginAtZ0)
{
    // (0, 0) is the exact center of the single z0 tile.
    EXPECT_EQ(lonToWorldX(0, 0), 128);
    EXPECT_EQ(latToWorldY(0, 0), 128);
    // Lon -180 is world x = 0; +180 is the far edge.
    EXPECT_EQ(lonToWorldX(-180000000, 0), 0);
    EXPECT_EQ(lonToWorldX(180000000, 0), 256);
}

TEST(MapMath, PoleClampKeepsYInRange)
{
    for (uint8_t z : { uint8_t { 0 }, uint8_t { 12 }, uint8_t { 16 } }) {
        EXPECT_EQ(latToWorldY(90000000, z), 0) << "z" << int(z);
        EXPECT_EQ(latToWorldY(-90000000, z), worldSizePx(z) - 1) << "z" << int(z);
        EXPECT_EQ(latToWorldY(MAX_LAT_UDEG, z), 0) << "z" << int(z);
    }
}

TEST(MapMath, AthensLandsInThePacksTilesAtEveryPackZoom)
{
    // Expected tile coords for Athens center, computed independently
    // (python: x=(lon+180)/360*2^z, y=(1-asinh(tan lat)/pi)/2*2^z):
    //   z:    12      13      14      15      16
    //   x:  1183    2367    4735    9470   18941
    //   y:  1479    2958    5917   11835   23670
    const struct { uint8_t z; uint32_t x, y; } expected[] = {
        { 12, 1183, 1479 }, { 13, 2367, 2958 }, { 14, 4735, 5917 },
        { 15, 9470, 11835 }, { 16, 18941, 23670 },
    };
    for (const auto& e : expected) {
        const WorldPx p = toWorldPx(kAthensLat, kAthensLon, e.z);
        EXPECT_EQ(tileCoord(p.x), e.x) << "z" << int(e.z);
        EXPECT_EQ(tileCoord(p.y), e.y) << "z" << int(e.z);
    }
}

TEST(MapMath, SubTileOffsetsStayInTileRange)
{
    const WorldPx p = toWorldPx(kAthensLat, kAthensLon, 16);
    EXPECT_GE(subTile(p.x), 0);
    EXPECT_LT(subTile(p.x), TILE_DIM);
    EXPECT_GE(subTile(p.y), 0);
    EXPECT_LT(subTile(p.y), TILE_DIM);
}

TEST(MapMath, TileBoundaryIsExact)
{
    // Lon exactly at a z16 tile edge: tile 18944 starts at
    // lon = 18944 / 2^16 * 360 - 180 = -75.9375 exactly (representable in
    // integer microdegrees: -75937500).
    const int64_t x = lonToWorldX(-75937500, 16);
    EXPECT_EQ(x, int64_t { 18944 } * TILE_DIM);
    EXPECT_EQ(tileCoord(x), 18944u);
    EXPECT_EQ(subTile(x), 0);
    // Conversion rounds to the nearest world pixel (robust against the
    // 1e-6 factor's binary inexactness at exact boundaries), so a
    // position under half a pixel west still rounds into this tile; a
    // full ~2 px west (12 microdeg at z16) lands in the previous one.
    EXPECT_EQ(tileCoord(lonToWorldX(-75937512, 16)), 18943u);
}

TEST(MapMath, DoublePrecisionResolvesSubPixelAtZ16)
{
    // Two fixes ~3 m apart must land on distinct world pixels at z16,
    // where one pixel is ~1.7 m of ground; a float pipeline (~0.36 px
    // resolution, ~60 m of mantissa granularity at world scale) risks
    // collapsing near-adjacent fixes.
    const int64_t a = lonToWorldX(kAthensLon, 16);
    const int64_t b = lonToWorldX(kAthensLon + 40, 16); // +40 udeg ≈ 3.2 m
    EXPECT_NE(a, b);
}

TEST(MapMath, RescaleMatchesDirectConversionWithinOnePixel)
{
    // Storing the trace at z16 and shifting down must agree with direct
    // conversion at the target zoom to within 1 px (truncation).
    for (uint8_t z = 12; z <= 16; ++z) {
        const WorldPx direct = toWorldPx(kAthensLat, kAthensLon, z);
        const WorldPx at16   = toWorldPx(kAthensLat, kAthensLon, 16);
        EXPECT_LE(std::abs(rescale(at16.x, 16, z) - direct.x), 1) << "z" << int(z);
        EXPECT_LE(std::abs(rescale(at16.y, 16, z) - direct.y), 1) << "z" << int(z);
    }
}

TEST(MapMath, PackBboxCornersMapInsidePackTileRange)
{
    // The Athens pack's header bbox (README in the pack bundle):
    // lon [-76.015, -75.889], lat [44.590, 44.662]. Its z16 tiles span
    // x 18929..18952, y 23661..23680 (independent computation). Every
    // corner must land in that range.
    const int32_t lons[] = { -76015000, -75889000 };
    const int32_t lats[] = { 44590000, 44662000 };
    for (int32_t lon : lons) {
        for (int32_t lat : lats) {
            const WorldPx p = toWorldPx(lat, lon, 16);
            EXPECT_GE(tileCoord(p.x), 18929u);
            EXPECT_LE(tileCoord(p.x), 18952u);
            EXPECT_GE(tileCoord(p.y), 23661u);
            EXPECT_LE(tileCoord(p.y), 23680u);
        }
    }
}

} // namespace
