#include <gui/containers/TrackFaceMap.hpp>

#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

#include <cstdio>

TrackFaceMap::TrackFaceMap()
{
    setPosition(0, 0, 240, 240);

    mMap.setPosition(0, 0, 240, 240);
    add(mMap);

    // Bottom-centre one-liner. Sized to the round bezel's actual chord
    // width at its lowest row (240x240 panel, r=120: a box any wider than
    // ~154px here runs past the visible glass, same bug class as the
    // top-anchored label that lost its left edge in the spike). Centred
    // text (not the "_L" left-aligned variant) keeps the glyph run
    // symmetric about the panel's centre, and word-wrap-ellipsis is a
    // hard backstop so an unexpectedly long packError string is truncated
    // instead of overflowing the box into the bezel-hidden corners.
    mStatus.setPosition(48, 188, 144, 24);
    mStatus.setTypedText(touchgfx::TypedText(T_TMP_REGULAR_18));
    mStatus.setWideTextAction(touchgfx::WIDE_TEXT_WORDWRAP_ELLIPSIS);
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
                          bool fix, const char* packError, bool validating,
                          bool offCoverage)
{
    // Don't feed the viewport to MapTileView until the pack is both
    // structurally valid AND CRC-trusted: MapTileView's own guard is just
    // isOpen() (true as soon as the structural-only open succeeds, before
    // the CRC is confirmed), so withholding this call here is what actually
    // keeps tiles from rendering during "validating".
    if (packError == nullptr && !validating) {
        mMap.setViewport(centerX16, centerY16, zoom, fix);
    }

    char text[kStatusBufSize];
    if (packError != nullptr) {
        // The activity must never depend on the map: say why it's blank
        // and keep recording. Short prefix so the actionable part (the
        // reason) survives the WIDE_TEXT_WORDWRAP_ELLIPSIS truncation
        // that guards the round bezel's narrow lower chord.
        std::snprintf(text, sizeof(text), "map: %.16s", packError);
    } else if (validating) {
        // Neutral, non-alarming: expected on first run after a fresh pack
        // deploy, resolves on its own once Service's background CRC pass
        // finishes -- not an error.
        std::snprintf(text, sizeof(text), "verifying map%s", fix ? "" : "  no gps");
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
