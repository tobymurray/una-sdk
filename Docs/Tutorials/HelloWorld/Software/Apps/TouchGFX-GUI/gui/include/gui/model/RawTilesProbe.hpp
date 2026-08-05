#ifndef RAWTILESPROBE_HPP
#define RAWTILESPROBE_HPP

#include <cstdint>

namespace SDK { class Kernel; }

/**
 * Device-feasibility probe for rawtiles packs: locate a pack on the watch
 * filesystem, parse just enough header to find the first tile at zoom_max,
 * read that tile into a static buffer, and time every stage through
 * SDK::Interface::IFileSystem. The timings ARE the deliverable — this class
 * exists to answer "what does a 64 KiB tile read cost on the real watch",
 * not to be a reader.
 */
class RawTilesProbe
{
public:
    static constexpr uint16_t kTileDim    = 256;
    static constexpr uint32_t kTileBytes  = static_cast<uint32_t>(kTileDim) * kTileDim; // ABGR2222
    static constexpr int      kCandidates = 4;

    // CRC-32/ISO-HDLC of the first zoom_max tile (z14 x2587 y5604) of the
    // stanley.rawtiles sample pack, computed on the host. Display-only aid:
    // lets the on-screen readout say OK/BAD without a laptop at hand.
    static constexpr uint32_t kStanleyZmaxTileCrc = 0x26E5979Bu;

    struct Result {
        bool        ran          = false;
        int         runs         = 0;
        int8_t      pathIdx      = -1;      ///< winning candidatePath() index, -1 = none
        bool        existHit[kCandidates] = {};
        uint32_t    existScanMs  = 0;
        bool        opened       = false;
        uint32_t    openMs       = 0;
        uint32_t    fileSize     = 0;
        bool        headerOk     = false;
        uint32_t    headerMs     = 0;
        uint8_t     zoomMin      = 0;
        uint8_t     zoomMax      = 0;
        uint32_t    tileCount    = 0;
        bool        entryOk      = false;
        uint8_t     tileZ        = 0;
        uint32_t    tileX        = 0;
        uint32_t    tileY        = 0;
        uint32_t    tileOffset   = 0;
        bool        tileOk       = false;
        uint32_t    tileReadMs   = 0;       ///< first seek+read of the 64 KiB tile
        uint32_t    rereadMs     = 0;       ///< second seek+read, same open handle
        uint32_t    tileCrc      = 0;
        const char* failStage    = "";      ///< first failed stage, "" when all green
    };

    explicit RawTilesProbe(const SDK::Kernel& kernel) : mKernel(kernel) {}

    /// Runs synchronously; a slow filesystem stalls the GUI for exactly the
    /// time being measured. Repeat calls re-run everything (warm-cache data).
    void run();

    const Result&  result() const { return mResult; }

    /// Tile pixels (kTileBytes, ABGR2222). Valid when result().tileOk.
    static const uint8_t* tileData();

    static const char* candidatePath(int i);

private:
    const SDK::Kernel& mKernel;
    Result mResult;
};

#endif // RAWTILESPROBE_HPP
