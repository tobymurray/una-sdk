/**
 ******************************************************************************
 * @file    ActivityWriter.cpp
 * @brief   Serializes Workout activity data to a FIT file.
 ******************************************************************************
 */

#include "ActivityWriter.hpp"

#include "SDK/Fit/FitProfile.hpp"

#define LOG_MODULE_PRX      "ActivityWriter"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "SDK/UnaLogger/Logger.h"

namespace fit = SDK::Fit;
using Data = fit::FitWriter::Data;

ActivityWriter::ActivityWriter(const SDK::Kernel& kernel, const char* pathToDir)
    : mCore(kernel, pathToDir)
{
}

void ActivityWriter::start(const AppInfo& info)
{
    SDK::Fit::ActivityWriter::AppInfo coreInfo;
    coreInfo.timestamp  = info.timestamp;
    coreInfo.appVersion = info.appVersion;
    coreInfo.devID      = info.devID;
    coreInfo.appID      = info.appID;
    if (!mCore.start(coreInfo)) {
        return;
    }

    // Developer field descriptions (label/units survive any profile).
    mDfBattLevel   = mCore.registerDevField("batteryLevel", "%", fit::BaseType::UInt8);
    mDfBattVoltage = mCore.registerDevField("battVoltage", "mV", fit::BaseType::UInt16);
    // Lap-level "resting calories" (BMR over the lap, kcal). Kept as a developer
    // field because resting_calories is not part of the public FIT Profile
    // (session metabolic_calories is a native field instead).
    mDfLapRestingCal = mCore.registerDevField("resting_calories", "kcal", fit::BaseType::UInt16);
    mDfHrSource      = mCore.registerDevField("hr_source", nullptr, fit::BaseType::UInt8);
    mDfHrOptical     = mCore.registerDevField("hr_optical", "bpm", fit::BaseType::UInt8);
    mDfHrExternal    = mCore.registerDevField("hr_external", "bpm", fit::BaseType::UInt8);

    // Plain record (HR only) + 3 HR developer fields.
    mRecordLocal = mCore.allocateLocalType();
    mCore.fit().defineMessage(mRecordLocal, fit::mesgNum(fit::MesgNum::Record),
        {fit::field::Record::Timestamp, fit::field::Record::HeartRate},
        {{mDfHrSource, 1, 0}, {mDfHrOptical, 1, 0}, {mDfHrExternal, 1, 0}});

    // + battery (5 developer fields).
    mRecordBattLocal = mCore.allocateLocalType();
    mCore.fit().defineMessage(mRecordBattLocal, fit::mesgNum(fit::MesgNum::Record),
        {fit::field::Record::Timestamp, fit::field::Record::HeartRate},
        {{mDfBattLevel, 1, 0}, {mDfBattVoltage, 2, 0},
         {mDfHrSource, 1, 0}, {mDfHrOptical, 1, 0}, {mDfHrExternal, 1, 0}});

    mLapLocal = mCore.allocateLocalType();
    mCore.fit().defineMessage(mLapLocal, fit::mesgNum(fit::MesgNum::Lap),
        {fit::field::Lap::Timestamp, fit::field::Lap::StartTime,
         fit::field::Lap::TotalElapsedTime, fit::field::Lap::TotalTimerTime,
         fit::field::Lap::MessageIndex, fit::field::Lap::AvgHeartRate,
         fit::field::Lap::MaxHeartRate, fit::field::Lap::TotalCalories},
        {{mDfLapRestingCal, 2, 0}});

    mSessionLocal = mCore.allocateLocalType();
    mCore.fit().defineMessage(mSessionLocal, fit::mesgNum(fit::MesgNum::Session),
        {fit::field::Session::Timestamp, fit::field::Session::StartTime,
         fit::field::Session::TotalElapsedTime, fit::field::Session::TotalTimerTime,
         fit::field::Session::MessageIndex, fit::field::Session::NumLaps,
         fit::field::Session::Sport, fit::field::Session::SubSport,
         fit::field::Session::AvgHeartRate, fit::field::Session::MaxHeartRate,
         fit::field::Session::TotalCalories, fit::field::Session::MetabolicCalories});

    mCore.beginRecording(info.timestamp);
}

void ActivityWriter::pause(std::time_t timestamp)
{
    mCore.pause(timestamp);
}

void ActivityWriter::resume(std::time_t timestamp)
{
    mCore.resume(timestamp);
}

void ActivityWriter::addRecord(const RecordData& record)
{
    const bool batt = record.has(RecordData::Field::BATTERY);
    const uint8_t local = batt ? mRecordBattLocal : mRecordLocal;

    Data d = mCore.fit().data(local);
    SDK::Fit::ActivityWriter::writeBaseRecord(d, record.timestamp,
        record.has(RecordData::Field::HEART_RATE), static_cast<uint8_t>(record.heartRate));

    // Developer fields, in definition order.
    if (batt) {
        d.u8(record.batteryLevel).u16(record.batteryVoltage);
    }
    d.u8(record.hrSource).u8(record.hrOpticalBpm).u8(record.hrExternalBpm);

    d.write();
    mCore.noteRecordWritten(record.timestamp);
}

void ActivityWriter::addLap(const LapData& lap)
{
    Data d = mCore.fit().data(mLapLocal);
    SDK::Fit::ActivityWriter::writeBaseLap(d, lap.timestamp, lap.timeStart, lap.elapsed,
        lap.duration, mCore.lapCount(), lap.hrAvg, lap.hrMax);
    d.u16(static_cast<uint16_t>(lap.calories + 0.5f))
     // Developer field: lap resting (BMR) calories, kcal.
     .u16(static_cast<uint16_t>(lap.restingCalories + 0.5f))
     .write();
    mCore.noteLapWritten(lap.timestamp);
}

bool ActivityWriter::stop(const TrackData& track)
{
    Data d = mCore.fit().data(mSessionLocal);
    SDK::Fit::ActivityWriter::writeBaseSession(d, track.timestamp, track.timeStart, track.elapsed,
        track.duration, /*messageIndex=*/0, mCore.lapCount(),
        fit::Sport::Generic, fit::SubSport::Generic, track.hrAvg, track.hrMax);
    bool ok = d.u16(static_cast<uint16_t>(track.calories + 0.5f))
               .u16(static_cast<uint16_t>(track.metabolicCalories + 0.5f))
               .write();

    ok = mCore.writeActivityMessage(track.timestamp, track.duration, /*numSessions=*/1) && ok;
    ok = mCore.finalize() && ok;

    // FIT durability IS the save-success contract: the kernel auto-registers the
    // .fit the moment its FileGuard::close() fires (and recoverInterrupted()
    // re-registers after a crash), so once `ok` is true the activity is never
    // orphaned. The .json summary is auxiliary/best-effort — recovery cannot
    // rebuild it — so a summary-only failure must NOT suppress registration.
    // Attempt it ONLY when the FIT is durable (ok): with ok == false the marker
    // is kept for next-boot recovery / the FIT is invalid, so a .json sidecar
    // would misrepresent a non-durable activity.
    if (ok && !mCore.writeSummary(track.timeStart, track.duration, track.hrAvg, "workout")) {
        LOG_ERROR("Activity summary (.json) save failed; FIT is durable and registered\n");
    }
    return ok;
}

void ActivityWriter::discard()
{
    mCore.discard();
}

bool ActivityWriter::recoverInterrupted()
{
    return mCore.recoverInterrupted();
}
