/**
 ******************************************************************************
 * @file    ActivityWriter.cpp
 * @brief   Shared FIT-activity recording plumbing.
 ******************************************************************************
 */

#include "SDK/Fit/ActivityWriter.hpp"

#include <cassert>
#include <cstring>

#define LOG_MODULE_PRX      "Fit::ActivityWriter"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "SDK/UnaLogger/Logger.h"

namespace SDK::Fit {

using Data = FitWriter::Data;

ActivityWriter::ActivityWriter(const SDK::Kernel& kernel, const char* pathToDir)
    : mRecording(kernel, pathToDir, "fit"), mMarker(kernel.fs, pathToDir)
{
    assert(pathToDir != nullptr);
}

bool ActivityWriter::start(const AppInfo& info)
{
    mLapCounter   = 0;
    mNextLocal    = kFirstAppLocal;
    mNextDevField = 0;

    if (!mRecording.create(info.timestamp)) {
        return false;
    }

    mFit = std::make_unique<FitWriter>(mRecording.file());
    const bool begun = mFit->begin(/*profileVersion=*/0);
    if (!begun) {
        LOG_ERROR("Failed to write FIT header\n");
        return false;
    }

    // file_id
    const uint8_t productNameLen =
        static_cast<uint8_t>(std::strlen(kProductName) + 1);
    mFit->defineMessage(L_FILE_ID, mesgNum(MesgNum::FileId),
        {field::FileId::Type, field::FileId::Manufacturer,
         field::FileId::Product, field::FileId::SerialNumber,
         field::FileId::TimeCreated,
         {field::FileId::kProductNameNum, BaseType::String, productNameLen}});
    mFit->data(L_FILE_ID)
        .u8(static_cast<uint8_t>(File::Activity))
        .u16(static_cast<uint16_t>(Manufacturer::Una))
        .u16(static_cast<uint16_t>(Product::UnaWatch))
        .u32(0)
        .u32(unixToFitTimestamp(info.timestamp))
        .str(kProductName, productNameLen)
        .write();

    // developer_data_id
    mFit->defineMessage(L_DEV_ID, mesgNum(MesgNum::DeveloperDataId),
        {field::DeveloperDataId::ApplicationId,
         field::DeveloperDataId::DeveloperDataIndex});
    {
        uint8_t appId[16] = {};
        std::strncpy(reinterpret_cast<char*>(appId), info.appID.c_str(), sizeof(appId));
        mFit->data(L_DEV_ID).bytes(appId, sizeof(appId)).u8(0).write();
    }

    // event
    mFit->defineMessage(L_EVENT, mesgNum(MesgNum::Event),
        {field::Event::Timestamp, field::Event::EventField,
         field::Event::EventType});

    // activity (fixed shape, never varies by sport)
    mFit->defineMessage(L_ACTIVITY, mesgNum(MesgNum::Activity),
        {field::Activity::Timestamp, field::Activity::TotalTimerTime,
         field::Activity::LocalTimestamp, field::Activity::NumSessions});

    return mFit->ok();
}

uint8_t ActivityWriter::registerDevField(const char* name, const char* units, BaseType baseType)
{
    const uint8_t devFieldNum = mNextDevField++;
    const uint8_t nameLen  = name ? static_cast<uint8_t>(std::strlen(name) + 1) : 1;
    const uint8_t unitsLen = units ? static_cast<uint8_t>(std::strlen(units) + 1) : 1;

    // Redefine the field_description slot to size the name/units strings exactly.
    mFit->defineMessage(L_FIELD_DESC, mesgNum(MesgNum::FieldDescription),
        {field::FieldDescription::DeveloperDataIndex,
         field::FieldDescription::FieldDefinitionNumber,
         field::FieldDescription::FitBaseTypeId,
         {field::FieldDescription::kFieldNameNum, BaseType::String, nameLen},
         {field::FieldDescription::kUnitsNum, BaseType::String, unitsLen}});
    mFit->data(L_FIELD_DESC)
        .u8(0)
        .u8(devFieldNum)
        .u8(baseTypeId(baseType))
        .str(name ? name : "", nameLen)
        .str(units ? units : "", unitsLen)
        .write();

    return devFieldNum;
}

uint8_t ActivityWriter::allocateLocalType()
{
    if (mNextLocal > 15) {
        return kNoLocalType;
    }
    return mNextLocal++;
}

void ActivityWriter::writeEvent(std::time_t t, EventType type)
{
    mFit->data(L_EVENT)
        .u32(unixToFitTimestamp(t))
        .u8(static_cast<uint8_t>(Event::Timer))
        .u8(static_cast<uint8_t>(type))
        .write();
}

bool ActivityWriter::beginRecording(std::time_t timestamp)
{
    writeEvent(timestamp, EventType::Start);

    // Header + all definitions are on disk: flush and drop the recovery marker.
    // This is a clean record boundary, so a crash after this point is
    // recoverable up to at least the header/definitions. Only write the
    // marker when the flush is durable: a failed flush leaves a near-empty/
    // broken or non-durable .fit that the recovery marker must not point
    // next boot's recover() at.
    if (mFit->ok() && mRecording.flush()) {
        mMarker.write(mRecording.file().getPath(), static_cast<uint32_t>(mRecording.file().getPosition()));
        mRecording.markFlushed(timestamp);
        return true;
    }
    return false;
}

void ActivityWriter::pause(std::time_t timestamp)
{
    if (mFit) {
        writeEvent(timestamp, EventType::Stop);
    }
}

void ActivityWriter::resume(std::time_t timestamp)
{
    if (mFit) {
        writeEvent(timestamp, EventType::Start);
    }
}

void ActivityWriter::noteRecordWritten(std::time_t recordTimestamp)
{
    // Periodic durability flush: sync to eMMC and advance the marker to this
    // record boundary so a later crash recovers a record-complete file.
    if (mRecording.dueForFlush(recordTimestamp)) {
        // Only advance the marker when the flush durably landed; otherwise
        // keep the previous good offset (never point recover() past
        // non-durable data).
        if (mRecording.flush()) {
            mMarker.update(static_cast<uint32_t>(mRecording.file().getPosition()));
            mRecording.markFlushed(recordTimestamp);
        }
    }
}

void ActivityWriter::noteLapWritten(std::time_t lapTimestamp)
{
    mLapCounter++;

    // Laps are sparse: flush and advance the marker unconditionally, but only
    // when the flush durably landed (else keep the previous good offset).
    if (mRecording.flush()) {
        mMarker.update(static_cast<uint32_t>(mRecording.file().getPosition()));
        mRecording.markFlushed(lapTimestamp);
    }
}

Data& ActivityWriter::writeBaseRecord(Data& d, std::time_t timestamp, bool hasHeartRate, uint8_t heartRate)
{
    d.u32(unixToFitTimestamp(timestamp));
    d.u8(hasHeartRate ? heartRate : static_cast<uint8_t>(baseTypeInvalid(BaseType::UInt8)));
    return d;
}

Data& ActivityWriter::writeBaseLap(Data& d, std::time_t timestamp, std::time_t timeStart,
                                    std::time_t elapsed, std::time_t duration, uint16_t messageIndex,
                                    float hrAvg, float hrMax)
{
    return d.u32(unixToFitTimestamp(timestamp))
            .u32(unixToFitTimestamp(timeStart))
            .u32(static_cast<uint32_t>(elapsed * 1000))
            .u32(static_cast<uint32_t>(duration * 1000))
            .u16(messageIndex)
            .u8(static_cast<uint8_t>(hrAvg))
            .u8(static_cast<uint8_t>(hrMax));
}

Data& ActivityWriter::writeBaseSession(Data& d, std::time_t timestamp, std::time_t timeStart,
                                        std::time_t elapsed, std::time_t duration, uint16_t messageIndex,
                                        uint16_t numLaps, Sport sport, SubSport subSport,
                                        float hrAvg, float hrMax)
{
    return d.u32(unixToFitTimestamp(timestamp))
            .u32(unixToFitTimestamp(timeStart))
            .u32(static_cast<uint32_t>(elapsed * 1000))
            .u32(static_cast<uint32_t>(duration * 1000))
            .u16(messageIndex)
            .u16(numLaps)
            .u8(static_cast<uint8_t>(sport))
            .u8(static_cast<uint8_t>(subSport))
            .u8(static_cast<uint8_t>(hrAvg))
            .u8(static_cast<uint8_t>(hrMax));
}

bool ActivityWriter::writeActivityMessage(std::time_t timestamp, std::time_t duration, uint16_t numSessions)
{
    if (!mFit) {
        return false;
    }
    return mFit->data(L_ACTIVITY)
        .u32(unixToFitTimestamp(timestamp))
        .u32(static_cast<uint32_t>(duration * 1000))
        .u32(unixToFitTimestamp(epochToLocal(timestamp)))
        .u16(numSessions)
        .write();
}

bool ActivityWriter::finalize()
{
    if (!mFit) {
        return false;
    }

    bool ok = mFit->ok();

    const bool finishOk = mFit->finish();
    if (!finishOk) {
        LOG_ERROR("Failed to finalize FIT file\n");
    }
    ok = finishOk && mFit->ok() && ok;
    mFit.reset();

    ok = mRecording.flush() && ok;
    ok = mRecording.close() && ok;

    // The .fit is durably finished on disk: drop the recovery marker so the
    // next boot does not treat this activity as interrupted.
    if (ok) {
        mMarker.remove();
    }

    return ok;
}

void ActivityWriter::discard()
{
    mFit.reset();
    mMarker.remove();
    mRecording.discard();
}

bool ActivityWriter::recoverInterrupted()
{
    // All marker I/O + FitWriter::recover() orchestration lives in the shared
    // SDK::Fit::RecordingMarker. Recovery needs no sibling .json to register a
    // recovered activity (the kernel's activity registry tracks .fit files
    // only), so this is pure wiring.
    const auto result = mMarker.recover();
    if (result.recovered) {
        LOG_INFO("Recovery: finalized interrupted activity [%s]\n", result.path.c_str());
    }
    return result.recovered;
}

std::time_t ActivityWriter::tm2epoch(const struct tm* tm)
{
    int y = tm->tm_year + 1900;
    int m = tm->tm_mon + 1;
    int d = tm->tm_mday;
    if (m <= 2) { y -= 1; m += 12; }
    int64_t  era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);
    uint32_t doy = (153 * (m - 3) + 2) / 5 + d - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + (int64_t)doe - 719468;
    int64_t secs = days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
    return (std::time_t)secs;
}

std::time_t ActivityWriter::epochToLocal(std::time_t utc)
{
    std::tm localTime{};
#if WIN32
    localtime_s(&localTime, &utc);
#else
    localtime_r(&utc, &localTime);
#endif
    return tm2epoch(&localTime);
}

uint32_t ActivityWriter::unixToFitTimestamp(std::time_t unixTimestamp)
{
    const std::time_t FIT_EPOCH_OFFSET = 631065600;
    return static_cast<uint32_t>(unixTimestamp - FIT_EPOCH_OFFSET);
}

}  // namespace SDK::Fit
