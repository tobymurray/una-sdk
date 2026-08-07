#ifndef MAP_PACK_TRUST_MARKER_HPP
#define MAP_PACK_TRUST_MARKER_HPP

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/Kernel.hpp"

/**
 * @brief Tiny fixed-layout binary marker: "this exact pack (by size +
 *        declared CRC-32) has already passed a full CRC-32 scan" -- or has
 *        been confirmed corrupt.
 *
 * 16 bytes, little-endian:
 *   [0..3]   magic   -- kMagicGood ('M','P','T','1') or kMagicBad ('M','P','T','X')
 *   [4..11]  packSize (uint64)
 *   [12..15] crc      (uint32) -- the declared footer CRC that was checked
 *
 * Header-only by necessity: both the GUI (Model.cpp, reads only) and Service
 * (MapPackCrcVerifier, writes on completion) need this, but GUI_SOURCES does
 * not compile Software/Libs/Sources sources (see AthensRun-CMake's
 * CMakeLists.txt) -- only LIBS_INCLUDE_DIRS (headers) is shared into both
 * binaries. A .cpp companion here would silently link into Service only.
 *
 * ANY failure to read a well-formed marker (absent file, short read, bad
 * magic, or a torn read from a concurrent Service write) is treated as
 * Trust::Absent by read(). There is no distinct "corrupt marker" state
 * because that fallback is always safe (never a false-trust) and always
 * self-correcting (Service's next completed pass overwrites it).
 */
class MapPackTrustMarker {
public:
    enum class Trust { Absent, Bad, Good };

    static constexpr uint32_t kMagicGood  = 0x3154504Du; // 'M','P','T','1' (file byte order)
    static constexpr uint32_t kMagicBad   = 0x5854504Du; // 'M','P','T','X' (file byte order)
    static constexpr size_t   kMarkerSize = 16;

    MapPackTrustMarker(const SDK::Kernel& kernel, const char* path)
        : mKernel(kernel), mPath(path)
    {
    }

    /// @return Trust::Good/Bad with (sizeOut, crcOut) filled iff a
    /// well-formed marker of that kind exists. Trust::Absent (outputs
    /// untouched) on anything else -- missing file, short/garbage read, or
    /// an unrecognized magic.
    Trust read(uint64_t& sizeOut, uint32_t& crcOut) const
    {
        std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(mPath);
        if (!file || !file->exist()) {
            return Trust::Absent;
        }
        if (!file->open(false, false)) {
            return Trust::Absent;
        }
        uint8_t buf[kMarkerSize];
        bool ok = false;
        if (file->size() == kMarkerSize) {
            size_t got = 0;
            ok = file->read(reinterpret_cast<char*>(buf), kMarkerSize, got) && (got == kMarkerSize);
        }
        file->close();
        if (!ok) {
            return Trust::Absent;
        }

        const uint32_t magic = readU32LE(buf + 0);
        if (magic != kMagicGood && magic != kMagicBad) {
            return Trust::Absent;
        }
        sizeOut = readU64LE(buf + 4);
        crcOut  = readU32LE(buf + 12);
        return (magic == kMagicGood) ? Trust::Good : Trust::Bad;
    }

    /// Publishes a Good marker for (packSize, crc). Overwrites any existing
    /// marker (of either kind). @return false on any I/O failure -- nothing
    /// is published in that case.
    bool writeGood(uint64_t packSize, uint32_t crc) const { return write(kMagicGood, packSize, crc); }

    /// Publishes a Bad marker recording the (packSize, crc) that failed to
    /// verify, so callers can distinguish "confirmed corrupt" from
    /// "not yet checked" instead of waiting on a check that will never pass.
    bool writeBad(uint64_t packSize, uint32_t crc) const { return write(kMagicBad, packSize, crc); }

private:
    const SDK::Kernel& mKernel;
    const char*         mPath;

    static uint32_t readU32LE(const uint8_t* p)
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
             | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    static uint64_t readU64LE(const uint8_t* p)
    {
        uint64_t lo = readU32LE(p);
        uint64_t hi = readU32LE(p + 4);
        return lo | (hi << 32);
    }

    static void writeU32LE(uint8_t* p, uint32_t v)
    {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v >> 16);
        p[3] = static_cast<uint8_t>(v >> 24);
    }

    static void writeU64LE(uint8_t* p, uint64_t v)
    {
        writeU32LE(p, static_cast<uint32_t>(v));
        writeU32LE(p + 4, static_cast<uint32_t>(v >> 32));
    }

    // Same slash-split-mkdir idiom as SettingsSerializer::save() -- create
    // the parent directory (e.g. "maps/") before creating the marker file,
    // in case the pack lives in a subdirectory the marker also needs.
    bool write(uint32_t magic, uint64_t packSize, uint32_t crc) const
    {
        const char* slash = strrchr(mPath, '/');
        if (slash) {
            char dir[SDK::Interface::IFileSystem::skMaxPathLen]{};
            snprintf(dir, sizeof(dir), "%.*s", static_cast<int>(slash - mPath), mPath);
            if (!mKernel.fs.mkdir(dir)) {
                return false;
            }
        }

        std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(mPath);
        if (!file || !file->open(true, true)) {
            return false;
        }

        uint8_t buf[kMarkerSize];
        writeU32LE(buf + 0, magic);
        writeU64LE(buf + 4, packSize);
        writeU32LE(buf + 12, crc);

        size_t written = 0;
        bool ok = file->write(reinterpret_cast<const char*>(buf), kMarkerSize, written) && (written == kMarkerSize);
        ok = file->flush() && ok;
        file->close();
        return ok;
    }
};

#endif // MAP_PACK_TRUST_MARKER_HPP
