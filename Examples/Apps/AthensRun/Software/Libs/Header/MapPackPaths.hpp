#ifndef MAP_PACK_PATHS_HPP
#define MAP_PACK_PATHS_HPP

namespace AthensRun {

/// Sandbox-relative candidate paths for the live map pack, tried in this
/// order by both Model::ensureMapPack() (GUI) and MapPackCrcVerifier
/// (Service). Single source of truth: if these two ever disagreed, the
/// trust marker would never match and the map would show "verifying" for
/// the entire life of the app for no discoverable reason.
inline constexpr const char* kMapPackCandidatePaths[] = {
    "maps/athens.rawtiles",
    "athens.rawtiles",
};

/// Suffix appended to whichever candidate resolved, to name the trust
/// marker that certifies it (e.g. "maps/athens.rawtiles.trust").
inline constexpr const char* kMapPackTrustSuffix = ".trust";

} // namespace AthensRun

#endif // MAP_PACK_PATHS_HPP
