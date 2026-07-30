/**
 ******************************************************************************
 * @file    ActivityWriter.hpp
 * @brief   Shared FIT-activity recording plumbing.
 *
 * Every activity app (Workout, Running, Cycling, Treadmill, Hiking, ...) opens
 * a .fit file the same way, writes the same file_id/developer_data_id/event
 * boilerplate, arms the same crash-recovery marker, and records the same
 * "time + heart rate (+ battery)" base fields on every record/lap/session
 * message before layering its own sport-specific fields (GPS, cadence,
 * calories, structured workout steps, ...) on top. That base was previously
 * copy-pasted into each app's own ActivityWriter; this class is the shared
 * core, composed (not subclassed) by each app's own thin activity-recording
 * wrapper.
 *
 * This class is deliberately FIT-specific -- local message type allocation,
 * developer-field registration and the crash-recovery path (which finalizes a
 * torn file via SDK::Fit::FitWriter::recover()) are all FIT-protocol
 * concepts, not generic ones, so there is no pretense of format-agnosticism
 * here. The one piece of this job that ISN'T about FIT bytes -- where the
 * file lives and how often to flush it -- is composed in from
 * SDK::Activity::RecordingFile, which has no notion of FIT at all.
 *
 * Composition, not inheritance: this class owns the FIT message plumbing and
 * exposes fit() so the app can define and write its OWN additional local FIT
 * messages/fields on the same underlying encoder. There is no virtual
 * dispatch and no required base class for apps to derive from.
 *
 * Local message types 0-4 are reserved by this class (file_id,
 * developer_data_id, field_description, event, activity); allocateLocalType()
 * hands out 5-15 to the app for its own record/lap/session definitions (FIT
 * local types are a 4-bit field, 16 total, shared with whatever this class
 * reserves).
 ******************************************************************************
 */

#ifndef __SDK_FIT_ACTIVITY_WRITER_HPP
#define __SDK_FIT_ACTIVITY_WRITER_HPP

#include "SDK/Activity/RecordingFile.hpp"
#include "SDK/JSON/JsonStreamWriter.hpp"
#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Fit/FitProfile.hpp"
#include "SDK/Fit/FitWriter.hpp"
#include "SDK/Fit/RecordingMarker.hpp"

#include <cstdint>
#include <ctime>
#include <string>

namespace SDK::Fit {

class ActivityWriter {
public:
    struct AppInfo {
        std::time_t timestamp  = 0;  // UTC
        uint32_t    appVersion = 0;  // Application version 4 bytes LE [patch, minor, major, 0]
        std::string devID;           // Developer ID (max len 16)
        std::string appID;           // Application ID (max len 16)
    };

    /// Sentinel returned by allocateLocalType() once all 16 FIT local message
    /// types are exhausted.
    static constexpr uint8_t kNoLocalType = 0xFF;

    ActivityWriter(const SDK::Kernel& kernel, const char* pathToDir);

    /// Create "<pathToDir>/<YYYYMM>/activity_<timestamp>.fit", write the FIT
    /// header, file_id, developer_data_id and the event message definition.
    /// The caller must then register any developer fields it needs
    /// (registerDevField), define its own record/lap/session/... local
    /// messages (fit(), allocateLocalType()), and finally call
    /// beginRecording() once everything is defined.
    bool start(const AppInfo& info);

    /// Register a developer field (label/units that survive any FIT profile
    /// version) and return its developer field number, for use in a
    /// defineMessage() devFields list.
    uint8_t registerDevField(const char* name, const char* units, SDK::Fit::BaseType baseType);

    /// Hand out the next unused local message type (5-15) for the app's own
    /// definitions. Returns kNoLocalType once exhausted.
    uint8_t allocateLocalType();

    /// The shared encoder, for defining/writing the app's own local messages.
    SDK::Fit::FitWriter& fit() { return *mFit; }

    /// Write the start event, flush, and arm the crash-recovery marker. Call
    /// once, after all local messages are defined.
    bool beginRecording(std::time_t timestamp);

    void pause(std::time_t timestamp);   // writes a Stop event
    void resume(std::time_t timestamp);  // writes a Start event

    /// Call right after writing each record (Data::write()) to run the
    /// periodic durability flush + crash-recovery marker advance.
    void noteRecordWritten(std::time_t recordTimestamp);

    /// Call right after writing each lap (Data::write()). Flushes, advances
    /// the marker, and increments the lap count (see lapCount()).
    void noteLapWritten(std::time_t lapTimestamp);

    /// Number of laps written so far; use as the next lap's message_index.
    uint16_t lapCount() const { return mLapCounter; }

    /// Write the activity message. Its shape never varies by sport, so this
    /// class writes it outright rather than exposing base fields to compose.
    bool writeActivityMessage(std::time_t timestamp, std::time_t duration, uint16_t numSessions);

    /// Finalize the FIT stream (finish + flush + close) and, iff durable,
    /// drop the recovery marker. Returns the FIT-durability contract: true
    /// iff the .fit is safely on disk.
    bool finalize();

    /// Abandon the in-progress activity: drop the FIT stream, remove the
    /// recovery marker, close and delete the .fit file.
    void discard();

    /// Finalize an activity a previous boot left unfinished (power loss/crash
    /// mid-recording). Safe (false, no side effects) when no marker is
    /// present. Must run before any new activity is started.
    bool recoverInterrupted();

    /// Base record fields every app writes: timestamp + optional heart rate.
    /// Chain the app's own extra field writes after this call, then write().
    static SDK::Fit::FitWriter::Data& writeBaseRecord(
        SDK::Fit::FitWriter::Data& d, std::time_t timestamp,
        bool hasHeartRate, uint8_t heartRate);

    /// Base lap fields every app writes.
    static SDK::Fit::FitWriter::Data& writeBaseLap(
        SDK::Fit::FitWriter::Data& d, std::time_t timestamp, std::time_t timeStart,
        std::time_t elapsed, std::time_t duration, uint16_t messageIndex,
        float hrAvg, float hrMax);

    /// Base session fields every app writes (includes sport/sub_sport, unlike
    /// the old per-app copies which hardcoded Sport::Generic).
    static SDK::Fit::FitWriter::Data& writeBaseSession(
        SDK::Fit::FitWriter::Data& d, std::time_t timestamp, std::time_t timeStart,
        std::time_t elapsed, std::time_t duration, uint16_t messageIndex,
        uint16_t numLaps, SDK::Fit::Sport sport, SDK::Fit::SubSport subSport,
        float hrAvg, float hrMax);

    /// Open the .json summary sidecar next to the just-finalized .fit, write
    /// the base keys every app writes (time_start/duration/hr_avg/
    /// activity_type), call @p extra(writer) so the caller can add its own,
    /// then close. Best-effort: a failure here must not affect the .fit's
    /// already-decided durability result.
    template <typename ExtraFn>
    bool writeSummary(std::time_t timeStart, std::time_t duration, float hrAvg,
                       const char* activityType, ExtraFn&& extra)
    {
        if (!mRecording.reopenWithExtension("json")) {
            return false;
        }
        SDK::JsonStreamWriter writer(&mRecording.file());
        writer.startMap();
        writer.add("time_start", static_cast<uint32_t>(timeStart));
        writer.add("duration", static_cast<uint32_t>(duration));
        writer.add("hr_avg", hrAvg);
        writer.add("activity_type", activityType);
        extra(writer);
        writer.endMap();
        const bool ok = mRecording.flush();
        return mRecording.close() && ok;
    }

    bool writeSummary(std::time_t timeStart, std::time_t duration, float hrAvg,
                       const char* activityType)
    {
        return writeSummary(timeStart, duration, hrAvg, activityType,
                             [](SDK::JsonStreamWriter&) {});
    }

    static uint32_t   unixToFitTimestamp(std::time_t unixTimestamp);
    static std::time_t epochToLocal(std::time_t utc);

private:
    /// Local message types this class defines directly (FIT record header,
    /// 0-15); the app's own definitions start at kFirstAppLocal.
    enum ReservedLocal : uint8_t {
        L_FILE_ID     = 0,
        L_DEV_ID      = 1,
        L_FIELD_DESC  = 2,
        L_EVENT       = 3,
        L_ACTIVITY    = 4,
        kFirstAppLocal = 5,
    };

    SDK::Activity::RecordingFile mRecording;
    SDK::Fit::RecordingMarker    mMarker;
    std::unique_ptr<SDK::Fit::FitWriter> mFit;
    uint8_t     mNextLocal    = kFirstAppLocal;
    uint8_t     mNextDevField = 0;
    uint16_t    mLapCounter   = 0;

    bool openSummaryFile();
    void writeEvent(std::time_t t, SDK::Fit::EventType type);

    static std::time_t tm2epoch(const struct tm* tm);
};

}  // namespace SDK::Fit

#endif  // __SDK_FIT_ACTIVITY_WRITER_HPP
