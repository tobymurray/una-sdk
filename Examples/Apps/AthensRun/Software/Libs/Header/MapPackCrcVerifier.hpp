#ifndef MAP_PACK_CRC_VERIFIER_HPP
#define MAP_PACK_CRC_VERIFIER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/Kernel.hpp"

#include "MapPackVerifyLog.hpp"

/**
 * @brief Background, resumable CRC-32/ISO-HDLC verifier for the AthensRun
 *        map pack, owned exclusively by Service.
 *
 * Deliberately independent of SDK::RawTiles::Container: rawtiles spec §10's
 * CRC scope is "every byte from offset 0 up to (but not including) the
 * CRC's own 4 bytes" -- format-agnostic, no header/index parsing needed.
 * Container.cpp is vendored, GUI-private, and lives under TOUCHGFX_SOURCES
 * (see AthensRun-CMake/CMakeLists.txt), not compiled into the Service
 * binary at all -- duplicating the ~15-line table-driven CRC here is a much
 * smaller, more contained change than restructuring CMake source sets to
 * share it. The duplicated table must be kept in sync with Container.cpp's
 * copy if either is ever edited; both are checkable against the spec's
 * pinned test vector (0xCBF43926 for ASCII "123456789", rawtiles spec §10).
 *
 * Usage: call start() once (e.g. near Service::run() startup), then call
 * step() once per Service::run() loop iteration -- every iteration, not
 * just the 500 ms idle-timeout branch, since GPS/sensor traffic during an
 * active run can keep mKernel.comm.getMessage() returning true well inside
 * its timeout and would otherwise starve a background pass that only
 * progressed when idle. step() is a no-op once done().
 */
class MapPackCrcVerifier {
public:
    /// Bytes read per step() call; also the hard cap even if a caller asks
    /// for more via step(maxBytes). NOT validated on hardware yet -- this
    /// mirrors Container.cpp's post-mitigation kChunkSize (4096) as a
    /// starting point; needs real on-device IPC/read-latency measurement
    /// (see Debug/mappack_verify.log) to finalize.
    static constexpr size_t kDefaultChunkBytes = 4096;

    enum class Status {
        Idle,        ///< Not started, or no pack file found -- nothing to do.
        InProgress,  ///< Scanning; call step() again.
        Verified,    ///< CRC matched (this pass, or an already-trusted
                     ///< marker found at start()); marker is up to date.
        Mismatched,  ///< Finished: CRC did not match. Bad marker written.
        IoError,     ///< Could not open/read the pack or marker.
    };

    explicit MapPackCrcVerifier(const SDK::Kernel& kernel);

    /// Resolves the pack via AthensRun::kMapPackCandidatePaths, and:
    ///   - Idle if none of the candidates exist.
    ///   - Verified immediately, with no I/O beyond one marker read, if a
    ///     Good marker already matches the resolved file's (size, declared
    ///     CRC) -- this is what makes a reboot with an unchanged pack
    ///     cheap, not just repeated launches within one boot.
    ///   - InProgress otherwise (scan starts from byte 0).
    /// No-op (returns current status unchanged) if already InProgress.
    Status start();

    /// Advances the scan by up to min(maxBytes, kDefaultChunkBytes) bytes.
    /// No-op if not InProgress. On finishing a full pass: compares against
    /// the footer CRC, writes a Good marker on match or a Bad marker (with
    /// the mismatching declared CRC, for diagnostics) on mismatch.
    Status step(size_t maxBytes = kDefaultChunkBytes);

    Status status() const { return mStatus; }
    bool   done() const    { return mStatus != Status::InProgress; }

private:
    const SDK::Kernel&                      mKernel;
    MapPackVerifyLog                        mLog;
    std::unique_ptr<SDK::Interface::IFile>  mFile;
    const char*                             mResolvedPath = nullptr;
    uint64_t                                mFileSize    = 0;
    uint64_t                                mCrcStart    = 0;
    uint64_t                                mBytesDone   = 0;
    uint32_t                                mCrc         = 0xFFFFFFFFu;
    uint32_t                                mDeclaredCrc = 0; // read once in start(), compared in finish()
    Status                                  mStatus      = Status::Idle;

    // Throttling state for step()'s progress logging (see MapPackVerifyLog's
    // doc comment: logging every ~4KB step would flood the log and add
    // per-line file I/O overhead to the very thing being timed).
    uint64_t mLastLoggedBytes = 0;
    uint32_t mLastLoggedAtMs  = 0;
    uint32_t mStartedAtMs     = 0;

    // Seeks to mCrcStart and reads the trailing 4-byte declared CRC into
    // out. Does not restore file position -- callers that need the cursor
    // back at 0 (start(), before scanning) must seek() again afterward.
    bool readFooterCrc(uint32_t& out);
    void finish(bool matched, uint32_t declaredCrc);
};

#endif // MAP_PACK_CRC_VERIFIER_HPP
