/**
 ******************************************************************************
 * @file    TrackFaceMap.hpp
 * @brief   The live-map face of the Track screen: basemap + trace + status.
 *
 * Hand-written Container (no generated base, no Designer edit): TrackView
 * owns it as an extra member next to the generated faces and toggles it
 * with the same setVisible() face machinery. All map data lives in the
 * Model (screens are destroyed on every transition); this face only
 * renders it and formats the one-line status.
 ******************************************************************************
 */

#ifndef ATHENSRUN_TRACKFACEMAP_HPP
#define ATHENSRUN_TRACKFACEMAP_HPP

#include <gui/map/MapTileView.hpp>

#include <touchgfx/containers/Container.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

class TrackFaceMap : public touchgfx::Container
{
public:
    TrackFaceMap();

    void setSources(const SDK::RawTiles::Container* container,
                    AthensRun::TileCache* cache,
                    const AthensRun::TraceBuffer* trace);

    /// Refresh from the model's current map state.
    /// packError is Container::describeResult(openResult) when the pack
    /// failed to open structurally (or was confirmed corrupt by the
    /// background CRC pass), nullptr otherwise. validating is true when the
    /// pack opened structurally but Service's background CRC verification
    /// hasn't confirmed it trustworthy yet -- tiles are withheld and a
    /// neutral status shown until that resolves.
    void update(int64_t centerX16, int64_t centerY16, uint8_t zoom, bool fix,
                const char* packError, bool validating, bool offCoverage);

private:
    static constexpr uint16_t kStatusBufSize = 32;

    MapTileView                       mMap;
    touchgfx::TextAreaWithOneWildcard mStatus;
    touchgfx::Unicode::UnicodeChar    mStatusBuf[kStatusBufSize];
};

#endif // ATHENSRUN_TRACKFACEMAP_HPP
