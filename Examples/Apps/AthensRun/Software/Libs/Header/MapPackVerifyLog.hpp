#ifndef MAP_PACK_VERIFY_LOG_HPP
#define MAP_PACK_VERIFY_LOG_HPP

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Kernel/Kernel.hpp"

/**
 * @brief On-device diagnostic log for the background map-pack CRC
 *        verification path, modeled on the watch-apps Squash app's
 *        ImuFileSink (device-side file sink for diagnostic data, kept in
 *        its own directory, separate from user-facing files).
 *
 * Almost every timing number in that design (chunk size, real elapsed scan
 * time, Service::run() loop cadence under sensor load, whether cross-process
 * marker-file access ever produces a torn/malformed read) is unvalidated
 * without real hardware. Seeing any of that today requires the debug-UART
 * rig -- a separate physical FTDI connection. This log instead rides out on
 * the same USB-MSC connection already used to deploy the app and the pack,
 * so one on-device test run can be fully reconstructed after the fact from
 * a single USB pull.
 *
 * Header-only for the same reason MapPackTrustMarker is: Model.cpp (GUI)
 * and MapPackCrcVerifier (Service) are separate binaries that only share
 * headers. There's no real "singleton" possible across two separate
 * processes anyway -- each side just independently opens-appends-writes-
 * closes the same path via its own mKernel.fs.
 */
class MapPackVerifyLog {
public:
    explicit MapPackVerifyLog(const SDK::Kernel& kernel) : mKernel(kernel) {}

    /// printf-style. Opens (append -- creates the dir/file on first use,
    /// never truncates), writes one line prefixed with
    /// "[<tag> <elapsedMs>ms] ", flushes, closes. Every call is a fresh,
    /// self-contained round-trip (not a held-open handle) so nothing is
    /// lost if the process dies mid-run, and it minimizes the window where
    /// Service and GUI could ever have the same file open at once.
    /// Best-effort: failures are silently swallowed (this is a diagnostic
    /// aid, not something the app's correctness can depend on).
    void logf(const char* tag, const char* fmt, ...) const
    {
        // mkdir is a no-op (and returns true) if the directory already
        // exists, so just always ensure it's there before opening -- no
        // need to special-case "first call ever" separately.
        mKernel.fs.mkdir(kDir);

        std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kPath);
        if (!file) {
            return;
        }
        // wMode=true, override=false: open for writing without truncating.
        if (!file->open(true, false)) {
            return;
        }
        // Explicit seek-to-end: this SDK's IFile doesn't document whether a
        // non-override write-mode open positions the cursor at EOF or at 0,
        // so don't rely on it -- confirmed empirically on device (see the
        // design doc's flagged open question) whether this seek is in fact
        // necessary.
        file->seek(file->size());

        char line[256];
        int len = std::snprintf(line, sizeof(line), "[%s %lums] ", tag,
                                 static_cast<unsigned long>(mKernel.sys.getTimeMs()));
        if (len > 0 && static_cast<size_t>(len) < sizeof(line)) {
            va_list args;
            va_start(args, fmt);
            std::vsnprintf(line + len, sizeof(line) - static_cast<size_t>(len), fmt, args);
            va_end(args);
        }

        size_t written = 0;
        file->write(line, std::strlen(line), written);
        file->flush();
        file->close();
    }

private:
    const SDK::Kernel& mKernel;
    static constexpr const char* kDir  = "Debug";
    static constexpr const char* kPath = "Debug/mappack_verify.log";
};

#endif // MAP_PACK_VERIFY_LOG_HPP
