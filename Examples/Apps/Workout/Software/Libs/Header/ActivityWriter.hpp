/**
 ******************************************************************************
 * @file    ActivityWriter.hpp
 * @brief   Serializes Workout activity data to a FIT file.
 *
 * Composes the shared SDK::Fit::ActivityWriter for the file lifecycle, crash
 * recovery and the base time/heart-rate/battery fields every activity app
 * writes, and layers Workout's own extra fields (active + resting/metabolic
 * calories) on top, using the same underlying FIT encoder.
 ******************************************************************************
 */

#ifndef ACTIVITY_WRITER_HPP
#define ACTIVITY_WRITER_HPP

#include <cstdint>
#include <ctime>
#include <string>

#include "SDK/Fit/ActivityWriter.hpp"
#include "SDK/Kernel/Kernel.hpp"

/**
 * @class ActivityWriter
 * @brief Serializes Workout activity data to a FIT file.
 */
class ActivityWriter {
public:
    struct AppInfo {
        std::time_t timestamp  = 0;  // UTC
        uint32_t    appVersion = 0;  // Application version 4 bytes LE [patch, minor, major, 0]
        std::string devID;           // Developer ID (max len 16)
        std::string appID;           // Application ID (max len 16)
    };

    struct RecordData {
        enum class Field : uint8_t {
            HEART_RATE = 1u << 0,
            BATTERY    = 1u << 1,
        };

        void set(Field f)                 { mFlags |= mask(f); }
        void clear(Field f)               { mFlags &= static_cast<uint8_t>(~mask(f)); }
        void set(Field f, bool state)     { state ? set(f) : clear(f); }
        bool has(Field f) const           { return (mFlags & mask(f)) != 0; }
        void clearAll()                   { mFlags = 0; }

        std::time_t timestamp      = 0;   // UTC
        float       heartRate      = 0.0f; // bpm (arbitrated)
        uint8_t     hrSource       = 0;   // HeartRateEx::Source (0=none,1=optical,2=external)
        uint8_t     hrOpticalBpm   = 0;   // raw wrist-optical (PPG) bpm (0 = none)
        uint8_t     hrExternalBpm  = 0;   // raw external strap bpm (0 = none)
        uint8_t     batteryLevel   = 0;   // %
        uint16_t    batteryVoltage = 0;   // mV

    private:
        static constexpr uint8_t mask(Field f)
        {
            return static_cast<uint8_t>(f);
        }

        uint8_t mFlags = 0;
    };

    struct LapData {
        std::time_t timestamp        = 0;     // UTC
        std::time_t timeStart        = 0;     // UTC
        std::time_t duration         = 0;     // seconds
        std::time_t elapsed          = 0;     // seconds
        float       hrAvg            = 0.0f;  // bpm
        float       hrMax            = 0.0f;  // bpm
        float       calories         = 0.0f;  // kcal -- total active calories
        float       restingCalories  = 0.0f;  // kcal -- BMR over the lap (MET 1.0)
    };

    struct TrackData {
        std::time_t timestamp          = 0;    // UTC
        std::time_t timeStart          = 0;    // UTC
        std::time_t duration           = 0;    // seconds
        std::time_t elapsed            = 0;    // seconds
        float       hrAvg              = 0.0f; // bpm
        float       hrMax              = 0.0f; // bpm
        float       calories           = 0.0f; // kcal -- total active calories
        float       metabolicCalories  = 0.0f; // kcal -- BMR over the session (MET 1.0)
    };

    ActivityWriter(const SDK::Kernel& kernel, const char* pathToDir);

    void start(const AppInfo& info);
    void pause(std::time_t timestamp);
    void resume(std::time_t timestamp);
    void addRecord(const RecordData& record);
    void addLap(const LapData& lap);
    /// Finalize the current activity. The return value is the FIT-durability
    /// contract: true iff the FIT stream + its finish()/flush/close succeeded, so
    /// the .fit is safely on disk (the kernel auto-registers it on close, and
    /// recoverInterrupted() re-registers after a crash). The auxiliary .json
    /// summary is best-effort — a summary-only failure is logged but does NOT
    /// flip the result, so it can never suppress the activity's registration.
    bool stop(const TrackData& track);
    void discard();

    /// Finalize an activity that a previous boot left unfinished (power loss /
    /// crash mid-recording). Must run before any new activity is started.
    bool recoverInterrupted();

private:
    SDK::Fit::ActivityWriter mCore;

    // Local message types for Workout's own record/lap/session definitions
    // (base fields + calories), allocated from mCore at start().
    uint8_t mRecordLocal     = 0;
    uint8_t mRecordBattLocal = 0;
    uint8_t mLapLocal        = 0;
    uint8_t mSessionLocal    = 0;

    // Developer field numbers, registered with mCore at start().
    uint8_t mDfBattLevel     = 0;
    uint8_t mDfBattVoltage   = 0;
    uint8_t mDfLapRestingCal = 0;  // resting (BMR) calories over a lap, kcal
    uint8_t mDfHrSource      = 0;
    uint8_t mDfHrOptical     = 0;
    uint8_t mDfHrExternal    = 0;
};

#endif // ACTIVITY_WRITER_HPP
