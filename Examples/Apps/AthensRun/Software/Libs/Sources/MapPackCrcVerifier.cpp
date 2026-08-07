/**
 ******************************************************************************
 * @file    MapPackCrcVerifier.cpp
 * @brief   Background, resumable CRC-32 verifier for the AthensRun map pack.
 ******************************************************************************
 */

#include "MapPackCrcVerifier.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include "MapPackPaths.hpp"
#include "MapPackTrustMarker.hpp"

namespace {

constexpr size_t kFooterSize = 4; // rawtiles spec § 10: trailing u32 LE CRC-32.

/// CRC-32/ISO-HDLC table -- byte-for-byte the same generator as
/// Container.cpp's (SDK::RawTiles anonymous namespace). Kept as an
/// independent copy because Service does not link Container.cpp (see this
/// class's header doc comment); both are checkable against the same spec
/// test vector (0xCBF43926 for ASCII "123456789", rawtiles spec § 10).
const uint32_t* crc32Table()
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
    return table.data();
}

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length)
{
    const uint32_t* table = crc32Table();
    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

uint32_t readU32LE(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void markerPathFor(const char* resolvedPath, char* out, size_t outLen)
{
    std::snprintf(out, outLen, "%s%s", resolvedPath, AthensRun::kMapPackTrustSuffix);
}

} // namespace

MapPackCrcVerifier::MapPackCrcVerifier(const SDK::Kernel& kernel)
    : mKernel(kernel), mLog(kernel)
{
}

MapPackCrcVerifier::Status MapPackCrcVerifier::start()
{
    if (mStatus == Status::InProgress) {
        return mStatus; // no-op per doc comment
    }

    mResolvedPath = nullptr;
    for (const char* path : AthensRun::kMapPackCandidatePaths) {
        if (mKernel.fs.exist(path)) {
            mResolvedPath = path;
            break;
        }
    }
    if (mResolvedPath == nullptr) {
        mStatus = Status::Idle;
        mLog.logf("SVC", "MapPackCrcVerifier::start() no pack found among candidates\n");
        return mStatus;
    }

    mFile = mKernel.fs.file(mResolvedPath);
    if (!mFile || !mFile->open(false, false)) {
        mFile.reset();
        mStatus = Status::IoError;
        mLog.logf("SVC", "MapPackCrcVerifier::start() failed to open %s\n", mResolvedPath);
        return mStatus;
    }

    mFileSize = mFile->size();
    if (mFileSize < kFooterSize) {
        mFile->close();
        mFile.reset();
        mStatus = Status::IoError;
        mLog.logf("SVC", "MapPackCrcVerifier::start() %s too short (%llu bytes)\n",
                  mResolvedPath, static_cast<unsigned long long>(mFileSize));
        return mStatus;
    }
    mCrcStart = mFileSize - kFooterSize;

    if (!readFooterCrc(mDeclaredCrc)) {
        mFile->close();
        mFile.reset();
        mStatus = Status::IoError;
        mLog.logf("SVC", "MapPackCrcVerifier::start() failed to read footer CRC of %s\n", mResolvedPath);
        return mStatus;
    }

    char markerPath[SDK::Interface::IFileSystem::skMaxPathLen];
    markerPathFor(mResolvedPath, markerPath, sizeof(markerPath));
    MapPackTrustMarker marker(mKernel, markerPath);
    uint64_t markedSize = 0;
    uint32_t markedCrc  = 0;
    MapPackTrustMarker::Trust trust = marker.read(markedSize, markedCrc);

    if (trust == MapPackTrustMarker::Trust::Good && markedSize == mFileSize && markedCrc == mDeclaredCrc) {
        mFile->close();
        mFile.reset();
        mStatus = Status::Verified;
        mLog.logf("SVC", "MapPackCrcVerifier::start() %s already trusted via cached marker "
                         "(size=%llu crc=0x%08lX) -- skipping scan\n",
                  mResolvedPath, static_cast<unsigned long long>(mFileSize),
                  static_cast<unsigned long>(mDeclaredCrc));
        return mStatus;
    }

    // No matching Good marker: begin (or restart) the scan from byte 0.
    // Covers Absent (never checked), Bad (previously failed), and a Good
    // marker whose (size, crc) no longer match -- the pack was redeployed.
    mFile->seek(0);
    mBytesDone       = 0;
    mCrc             = 0xFFFFFFFFu;
    mLastLoggedBytes = 0;
    mStartedAtMs     = mKernel.sys.getTimeMs();
    mLastLoggedAtMs  = mStartedAtMs;
    mStatus          = Status::InProgress;

    const char* trustDesc = trust == MapPackTrustMarker::Trust::Bad ? "Bad"
                           : trust == MapPackTrustMarker::Trust::Good ? "Good-but-stale"
                                                                       : "Absent";
    mLog.logf("SVC", "MapPackCrcVerifier::start() %s size=%llu declaredCrc=0x%08lX "
                     "priorMarker=%s -> beginning scan from 0\n",
              mResolvedPath, static_cast<unsigned long long>(mFileSize),
              static_cast<unsigned long>(mDeclaredCrc), trustDesc);
    return mStatus;
}

MapPackCrcVerifier::Status MapPackCrcVerifier::step(size_t maxBytes)
{
    if (mStatus != Status::InProgress) {
        return mStatus;
    }

    const size_t take = static_cast<size_t>(
        std::min<uint64_t>(std::min<size_t>(maxBytes, kDefaultChunkBytes), mCrcStart - mBytesDone));

    uint8_t buf[kDefaultChunkBytes];
    size_t got = 0;
    if (!mFile->read(reinterpret_cast<char*>(buf), take, got) || got != take) {
        mFile->close();
        mFile.reset();
        mStatus = Status::IoError;
        mLog.logf("SVC", "MapPackCrcVerifier::step() read failed at offset %llu of %s\n",
                  static_cast<unsigned long long>(mBytesDone), mResolvedPath);
        return mStatus;
    }
    mCrc = crc32Update(mCrc, buf, take);
    mBytesDone += take;

    const uint32_t nowMs = mKernel.sys.getTimeMs();
    constexpr uint64_t kLogEveryBytes = 256 * kDefaultChunkBytes; // ~1MB at the default chunk size
    if ((mBytesDone - mLastLoggedBytes) >= kLogEveryBytes || (nowMs - mLastLoggedAtMs) >= 1000) {
        const uint32_t elapsedMs = nowMs - mStartedAtMs;
        const double throughputKBs = elapsedMs > 0
            ? (static_cast<double>(mBytesDone) / 1024.0) / (static_cast<double>(elapsedMs) / 1000.0)
            : 0.0;
        mLog.logf("SVC", "MapPackCrcVerifier::step() %llu/%llu bytes elapsed=%lums throughput=%.1fKB/s\n",
                  static_cast<unsigned long long>(mBytesDone), static_cast<unsigned long long>(mCrcStart),
                  static_cast<unsigned long>(elapsedMs), throughputKBs);
        mLastLoggedBytes = mBytesDone;
        mLastLoggedAtMs  = nowMs;
    }

    if (mBytesDone >= mCrcStart) {
        finish((mCrc ^ 0xFFFFFFFFu) == mDeclaredCrc, mDeclaredCrc);
    }
    return mStatus;
}

bool MapPackCrcVerifier::readFooterCrc(uint32_t& out)
{
    if (!mFile->seek(static_cast<size_t>(mCrcStart))) {
        return false;
    }
    uint8_t footer[kFooterSize];
    size_t got = 0;
    if (!mFile->read(reinterpret_cast<char*>(footer), kFooterSize, got) || got != kFooterSize) {
        return false;
    }
    out = readU32LE(footer);
    return true;
}

void MapPackCrcVerifier::finish(bool matched, uint32_t declaredCrc)
{
    const uint32_t computedCrc = mCrc ^ 0xFFFFFFFFu;
    const uint32_t elapsedMs   = mKernel.sys.getTimeMs() - mStartedAtMs;

    char markerPath[SDK::Interface::IFileSystem::skMaxPathLen];
    markerPathFor(mResolvedPath, markerPath, sizeof(markerPath));
    MapPackTrustMarker marker(mKernel, markerPath);

    mFile->close();
    mFile.reset();

    if (matched) {
        marker.writeGood(mFileSize, declaredCrc);
        mStatus = Status::Verified;
        mLog.logf("SVC", "MapPackCrcVerifier::step() DONE elapsed=%lums declaredCrc=0x%08lX "
                         "computedCrc=0x%08lX -> Verified, marker written\n",
                  static_cast<unsigned long>(elapsedMs), static_cast<unsigned long>(declaredCrc),
                  static_cast<unsigned long>(computedCrc));
    } else {
        marker.writeBad(mFileSize, declaredCrc);
        mStatus = Status::Mismatched;
        mLog.logf("SVC", "MapPackCrcVerifier::step() DONE elapsed=%lums declaredCrc=0x%08lX "
                         "computedCrc=0x%08lX -> Mismatched, Bad marker written\n",
                  static_cast<unsigned long>(elapsedMs), static_cast<unsigned long>(declaredCrc),
                  static_cast<unsigned long>(computedCrc));
    }
}
