/**
 ******************************************************************************
 * @file    RecordingFile.hpp
 * @brief   Format-agnostic durable-file lifecycle for periodic recordings.
 *
 * Every periodic on-watch recording (FIT activities today, potentially other
 * payloads later) needs the same three things, none of which have anything to
 * do with the bytes being written: a dated file inside a per-month directory,
 * a policy for how often to sync to storage while recording (don't flush on
 * every single write), and a way to point the same handle at a sibling path
 * (e.g. a summary file next to the main recording). This class owns exactly
 * that and nothing about the payload format -- it has no notion of FIT, CSV,
 * or any other encoding. A format-specific writer composes one of these for
 * "where do I write, and when do I flush" and layers its own encoding and
 * crash-recovery semantics on top (see SDK::Fit::ActivityWriter).
 ******************************************************************************
 */

#ifndef __SDK_ACTIVITY_RECORDING_FILE_HPP
#define __SDK_ACTIVITY_RECORDING_FILE_HPP

#include "SDK/Kernel/Kernel.hpp"

#include <ctime>
#include <memory>

namespace SDK::Activity {

class RecordingFile {
public:
    /// @param kernel        Provides the filesystem.
    /// @param pathToDir     Base activity directory (e.g. "Activity").
    /// @param extension     File extension WITHOUT the leading dot (e.g. "fit").
    /// @param flushIntervalSec  Minimum time between flushes that dueForFlush()
    ///                          reports as due; callers may still flush more
    ///                          often on their own (e.g. on sparse events).
    RecordingFile(const SDK::Kernel& kernel, const char* pathToDir,
                  const char* extension, std::time_t flushIntervalSec = 30);

    /// Create "<pathToDir>/<YYYYMM>/activity_<utc>.<extension>" and open it
    /// for writing. Resets the flush-cadence clock to @p utc.
    bool create(std::time_t utc);

    SDK::Interface::IFile& file() { return *mFile; }
    bool isOpen() const { return mFile != nullptr; }

    /// True once at least flushIntervalSec has elapsed since create() or the
    /// last markFlushed(). Purely a timing decision -- callers still own
    /// actually calling file().flush() and only markFlushed() on success, so
    /// a failed flush never advances the cadence past non-durable data.
    bool dueForFlush(std::time_t now) const;
    void markFlushed(std::time_t now) { mLastFlushUtc = now; }

    /// Re-point the same file handle at a sibling path with a different
    /// extension (e.g. swap "fit" for "json" for a summary sidecar) and open
    /// it fresh. Resets the handle on failure.
    bool reopenWithExtension(const char* newExtension);

    bool flush() { return mFile && mFile->flush(); }
    bool close() { return mFile && mFile->close(); }

    /// Close (if open) and delete the file.
    void discard();

private:
    const SDK::Kernel& mKernel;
    const char*        mPath;
    const char*        mExtension;
    std::time_t        mFlushIntervalSec;
    std::time_t        mLastFlushUtc = 0;

    std::unique_ptr<SDK::Interface::IFile> mFile;
};

}  // namespace SDK::Activity

#endif  // __SDK_ACTIVITY_RECORDING_FILE_HPP
