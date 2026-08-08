#ifndef MAP_PACK_PATHS_HPP
#define MAP_PACK_PATHS_HPP

namespace AthensRun {

/// Sandbox-relative candidate paths for the live map pack, checked by
/// Model::ensureMapPack() (GUI). Points at the shared MapManager app's
/// directory (../SharedData/maps/) rather than a private copy under this
/// app's own folder: MapManager (github.com/tobymurray/watch-apps,
/// feat/mapmanager) owns fetching/verifying packs there in the background
/// from boot, and any app -- not just this one -- reads from the same
/// already-verified location instead of running its own copy of this
/// verification pipeline. AthensRun's own MapPackCrcVerifier has been
/// retired accordingly (see Service.hpp/.cpp) -- Model.cpp's structural-open
/// + marker-poll logic needed no change at all to make this switch, since it
/// was already written against a path constant, not an assumption about who
/// writes the marker.
inline constexpr const char* kMapPackCandidatePaths[] = {
    "../SharedData/maps/athens.rawtiles",
};

/// Suffix appended to whichever candidate resolved, to name the trust
/// marker that certifies it (e.g. "maps/athens.rawtiles.trust").
inline constexpr const char* kMapPackTrustSuffix = ".trust";

} // namespace AthensRun

#endif // MAP_PACK_PATHS_HPP
