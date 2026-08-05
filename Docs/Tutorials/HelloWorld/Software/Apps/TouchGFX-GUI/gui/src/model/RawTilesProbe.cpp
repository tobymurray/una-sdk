#include <gui/model/RawTilesProbe.hpp>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Interfaces/ISystem.hpp"

#define LOG_MODULE_PRX      "RawTilesProbe"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "SDK/UnaLogger/Logger.h"

#include <cstring>

namespace {

// One tile of ABGR2222 pixels. Static so the cost shows up in the GUI ELF's
// .bss and the linker tells us immediately if the budget doesn't hold.
uint8_t sTileBuf[RawTilesProbe::kTileBytes];

const char* const kCandidates[RawTilesProbe::kCandidates] = {
    "2:/maps/stanley.rawtiles",             // USB-visible volume (apps + media)
    "1:/maps/stanley.rawtiles",             // eMMC user data — where packs should live
    "0:/maps/stanley.rawtiles",             // internal NOR
    "2:/Apps/HelloWorld/stanley.rawtiles",  // next to the .uapp the user just copied
};

uint16_t rdU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rdU32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// CRC-32/ISO-HDLC, bitwise (no table): speed is irrelevant next to the I/O
// being measured, and 1 KiB of table in a probe would be noise.
uint32_t crc32(const uint8_t* data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

} // namespace

const char* RawTilesProbe::candidatePath(int i)
{
    return (i >= 0 && i < kCandidates) ? ::kCandidates[i] : "";
}

const uint8_t* RawTilesProbe::tileData()
{
    return sTileBuf;
}

void RawTilesProbe::run()
{
    Result r;
    r.ran  = true;
    r.runs = mResult.runs + 1;

    SDK::Interface::IFileSystem& fs  = mKernel.fs;
    SDK::Interface::ISystem&     sys = mKernel.sys;

    uint32_t t0 = sys.getTimeMs();
    for (int i = 0; i < kCandidates; ++i) {
        r.existHit[i] = fs.exist(candidatePath(i));
        LOG_INFO("exist %s -> %d\n", candidatePath(i), r.existHit[i] ? 1 : 0);
        if (r.existHit[i] && r.pathIdx < 0) {
            r.pathIdx = static_cast<int8_t>(i);
        }
    }
    r.existScanMs = sys.getTimeMs() - t0;
    if (r.pathIdx < 0) {
        LOG_ERROR("no pack found on any candidate path (scan %ums)\n", r.existScanMs);
        r.failStage = "exist";
        mResult = r;
        return;
    }
    const char* path = candidatePath(r.pathIdx);

    t0 = sys.getTimeMs();
    auto file = fs.file(path);
    if (!file || !file->open()) {
        LOG_ERROR("open %s failed\n", path);
        r.failStage = "open";
        mResult = r;
        return;
    }
    r.openMs   = sys.getTimeMs() - t0;
    r.opened   = true;
    r.fileSize = static_cast<uint32_t>(file->size());
    LOG_INFO("open %s ok, %u bytes, %ums\n", path, r.fileSize, r.openMs);

    // Header: 292 bytes, enough for magic, enums, zoom range and the
    // per-zoom directory whose offsets are absolute byte offsets into the
    // tile index (verified against stanley.rawtiles on the host).
    t0 = sys.getTimeMs();
    uint8_t hdr[292];
    size_t  br = 0;
    if (!file->read(reinterpret_cast<char*>(hdr), sizeof(hdr), br) || br != sizeof(hdr)) {
        LOG_ERROR("header read failed (br=%u)\n", static_cast<unsigned>(br));
        r.failStage = "header";
        file->close();
        mResult = r;
        return;
    }
    r.headerMs = sys.getTimeMs() - t0;

    const bool magicOk = std::memcmp(hdr, "RAWT", 4) == 0;
    const bool shapeOk = hdr[4] == 1                       // wire major
                      && hdr[56] == 1                      // pixel_format ABGR2222
                      && rdU16(hdr + 60) == kTileDim
                      && rdU32(hdr + 92) == 292;           // index_offset
    r.zoomMin   = hdr[62];
    r.zoomMax   = hdr[63];
    r.tileCount = rdU32(hdr + 88);
    r.headerOk  = magicOk && shapeOk && r.zoomMax < 24 && r.tileCount > 0;
    if (!r.headerOk) {
        LOG_ERROR("header rejected (magic %d shape %d z %u..%u n %u)\n",
                  magicOk, shapeOk, r.zoomMin, r.zoomMax, r.tileCount);
        r.failStage = "header";
        file->close();
        mResult = r;
        return;
    }
    LOG_INFO("header ok: z%u..%u, %u tiles, %ums\n",
             r.zoomMin, r.zoomMax, r.tileCount, r.headerMs);

    const uint32_t dirOff = rdU32(hdr + 96 + r.zoomMax * 8);
    const uint32_t dirCnt = rdU32(hdr + 100 + r.zoomMax * 8);
    uint8_t entry[20];
    if (dirCnt == 0 || !file->seek(dirOff)
        || !file->read(reinterpret_cast<char*>(entry), sizeof(entry), br) || br != sizeof(entry)) {
        LOG_ERROR("index entry read failed (dir %u+%u)\n", dirOff, dirCnt);
        r.failStage = "index";
        file->close();
        mResult = r;
        return;
    }
    r.tileZ      = entry[0];
    r.tileX      = rdU32(entry + 4);
    r.tileY      = rdU32(entry + 8);
    r.tileOffset = rdU32(entry + 12);
    const uint32_t tileLen = rdU32(entry + 16);
    r.entryOk = r.tileZ == r.zoomMax && entry[1] == 0 && tileLen == kTileBytes
             && r.tileOffset <= r.fileSize - tileLen;
    if (!r.entryOk) {
        LOG_ERROR("entry rejected (z%u comp%u len%u off%u)\n",
                  r.tileZ, entry[1], tileLen, r.tileOffset);
        r.failStage = "index";
        file->close();
        mResult = r;
        return;
    }
    LOG_INFO("first z%u tile: x%u y%u at %u\n", r.tileZ, r.tileX, r.tileY, r.tileOffset);

    // THE measurement: one 64 KiB tile, seek + single read.
    t0 = sys.getTimeMs();
    if (!file->seek(r.tileOffset)
        || !file->read(reinterpret_cast<char*>(sTileBuf), kTileBytes, br) || br != kTileBytes) {
        LOG_ERROR("tile read failed (br=%u)\n", static_cast<unsigned>(br));
        r.failStage = "tile";
        file->close();
        mResult = r;
        return;
    }
    r.tileReadMs = sys.getTimeMs() - t0;
    r.tileOk     = true;

    r.tileCrc = crc32(sTileBuf, kTileBytes);

    t0 = sys.getTimeMs();
    if (file->seek(r.tileOffset)
        && file->read(reinterpret_cast<char*>(sTileBuf), kTileBytes, br) && br == kTileBytes) {
        r.rereadMs = sys.getTimeMs() - t0;
    }
    file->close();

    LOG_INFO("tile 64K: cold %ums, warm %ums, crc 0x%08X (%s)\n",
             r.tileReadMs, r.rereadMs, static_cast<unsigned>(r.tileCrc),
             r.tileCrc == kStanleyZmaxTileCrc ? "stanley OK" : "not stanley-z14-first");

    mResult = r;
}
