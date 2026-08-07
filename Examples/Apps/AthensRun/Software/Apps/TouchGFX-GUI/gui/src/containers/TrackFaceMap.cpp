#include <gui/containers/TrackFaceMap.hpp>

#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

#include <cstdio>

TrackFaceMap::TrackFaceMap()
{
    setPosition(0, 0, 240, 240);

    mMap.setPosition(0, 0, 240, 240);
    add(mMap);

    // Bottom-centre one-liner, inset from the round bezel chord (a
    // top-anchored label lost its left edge on the device in the spike).
    mStatus.setPosition(40, 204, 160, 24);
    mStatus.setTypedText(touchgfx::TypedText(T_TMP_REGULAR_18_L));
    mStatus.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    mStatusBuf[0] = 0;
    mStatus.setWildcard(mStatusBuf);
    add(mStatus);
}

void TrackFaceMap::setSources(const SDK::RawTiles::Container* container,
                              AthensRun::TileCache* cache,
                              const AthensRun::TraceBuffer* trace)
{
    mMap.setSources(container, cache, trace);
}

void TrackFaceMap::update(int64_t centerX16, int64_t centerY16, uint8_t zoom,
                          bool fix, const char* packError, bool offCoverage)
{
    mMap.setViewport(centerX16, centerY16, zoom, fix);

    char text[kStatusBufSize];
    if (packError != nullptr) {
        // The activity must never depend on the map: say why it's blank
        // and keep recording.
        std::snprintf(text, sizeof(text), "no map: %.20s", packError);
    } else if (!fix) {
        std::snprintf(text, sizeof(text), "acquiring GPS  z%u", zoom);
    } else if (offCoverage) {
        std::snprintf(text, sizeof(text), "off map  z%u", zoom);
    } else {
        std::snprintf(text, sizeof(text), "z%u", zoom);
    }
    touchgfx::Unicode::strncpy(mStatusBuf, text, kStatusBufSize - 1);
    mStatusBuf[kStatusBufSize - 1] = 0;
    mStatus.setWildcard(mStatusBuf);
    mStatus.invalidate();
}
