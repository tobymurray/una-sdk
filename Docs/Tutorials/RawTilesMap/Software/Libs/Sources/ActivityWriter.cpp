/**
 ******************************************************************************
 * @file    ActivityWriter.cpp
 * @brief   Serializes GPS activity data to a FIT file (native SDK::Fit encoder).
 ******************************************************************************
 */

#include "ActivityWriter.hpp"

#include <cassert>
#include <cstring>

#include "SDK/Interfaces/IFileSystem.hpp"

#define LOG_MODULE_PRX      "ActivityWriter"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "SDK/UnaLogger/Logger.h"

namespace fit = SDK::Fit;

ActivityWriter::ActivityWriter(const SDK::Kernel& kernel, const char* pathToDir)
   : mKernel(kernel), mPath(pathToDir)
{
    assert(pathToDir != nullptr);
}

void ActivityWriter::start(const AppInfo& info)
{
    mLapCounter = 0;

    if (!createAndOpenFile(info.timestamp)) {
        return;
    }

    mFit = std::make_unique<fit::FitWriter>(*mFile);
    mFit->begin(/*profileVersion=*/0);

    // file_id
    const uint8_t productNameLen =
        static_cast<uint8_t>(std::strlen(fit::kProductName) + 1);
    mFit->defineMessage(L_FILE_ID, fit::mesgNum(fit::MesgNum::FileId),
        {fit::field::FileId::Type, fit::field::FileId::Manufacturer,
         fit::field::FileId::Product, fit::field::FileId::SerialNumber,
         fit::field::FileId::TimeCreated,
         {fit::field::FileId::kProductNameNum, fit::BaseType::String, productNameLen}});
    mFit->data(L_FILE_ID)
        .u8(static_cast<uint8_t>(fit::File::Activity))
        .u16(static_cast<uint16_t>(fit::Manufacturer::Development))
        .u16(0)
        .u32(0)
        .u32(unixToFitTimestamp(info.timestamp))
        .str(fit::kProductName, productNameLen)
        .write();

    // developer_data_id
    mFit->defineMessage(L_DEV_ID, fit::mesgNum(fit::MesgNum::DeveloperDataId),
        {fit::field::DeveloperDataId::ApplicationId,
         fit::field::DeveloperDataId::DeveloperDataIndex});
    {
        uint8_t appId[16] = {};
        std::strncpy(reinterpret_cast<char*>(appId), info.appID.c_str(), sizeof(appId));
        mFit->data(L_DEV_ID).bytes(appId, sizeof(appId)).u8(0).write();
    }

    // Developer field description (label/units survive any profile).
    writeFieldDescription(DF_HR_TRUST_LEVEL, "hr_trust_level", "percents", fit::BaseType::UInt8);

    // event
    mFit->defineMessage(L_EVENT, fit::mesgNum(fit::MesgNum::Event),
        {fit::field::Event::Timestamp, fit::field::Event::EventField,
         fit::field::Event::EventType});

    // record: timestamp, heart_rate, position + altitude, + 1 HR-trust developer field
    mFit->defineMessage(L_RECORD, fit::mesgNum(fit::MesgNum::Record),
        {fit::field::Record::Timestamp, fit::field::Record::PositionLat,
         fit::field::Record::PositionLong, fit::field::Record::HeartRate,
         fit::field::Record::EnhancedAltitude},
        {{DF_HR_TRUST_LEVEL, 1, 0}});

    // lap / session / activity
    mFit->defineMessage(L_LAP, fit::mesgNum(fit::MesgNum::Lap),
        {fit::field::Lap::Timestamp, fit::field::Lap::StartTime,
         fit::field::Lap::TotalElapsedTime, fit::field::Lap::TotalTimerTime,
         fit::field::Lap::MessageIndex, fit::field::Lap::AvgHeartRate,
         fit::field::Lap::MaxHeartRate});
    mFit->defineMessage(L_SESSION, fit::mesgNum(fit::MesgNum::Session),
        {fit::field::Session::Timestamp, fit::field::Session::StartTime,
         fit::field::Session::TotalElapsedTime, fit::field::Session::TotalTimerTime,
         fit::field::Session::MessageIndex, fit::field::Session::Sport,
         fit::field::Session::SubSport, fit::field::Session::AvgHeartRate,
         fit::field::Session::MaxHeartRate, fit::field::Session::NumLaps});
    mFit->defineMessage(L_ACTIVITY, fit::mesgNum(fit::MesgNum::Activity),
        {fit::field::Activity::Timestamp, fit::field::Activity::TotalTimerTime,
         fit::field::Activity::LocalTimestamp, fit::field::Activity::NumSessions});

    addMessageEvent(info.timestamp, fit::EventType::Start);
}

void ActivityWriter::writeFieldDescription(uint8_t devFieldNum, const char* name,
                                           const char* units, fit::BaseType baseType)
{
    const uint8_t nameLen  = name ? static_cast<uint8_t>(std::strlen(name) + 1) : 1;
    const uint8_t unitsLen = units ? static_cast<uint8_t>(std::strlen(units) + 1) : 1;

    mFit->defineMessage(L_FIELD_DESC, fit::mesgNum(fit::MesgNum::FieldDescription),
        {fit::field::FieldDescription::DeveloperDataIndex,
         fit::field::FieldDescription::FieldDefinitionNumber,
         fit::field::FieldDescription::FitBaseTypeId,
         {fit::field::FieldDescription::kFieldNameNum, fit::BaseType::String, nameLen},
         {fit::field::FieldDescription::kUnitsNum, fit::BaseType::String, unitsLen}});
    mFit->data(L_FIELD_DESC)
        .u8(0)
        .u8(devFieldNum)
        .u8(fit::baseTypeId(baseType))
        .str(name ? name : "", nameLen)
        .str(units ? units : "", unitsLen)
        .write();
}

void ActivityWriter::pause()
{
    if (mFit) {
        addMessageEvent(std::time(nullptr), fit::EventType::Stop);
    }
}

void ActivityWriter::resume()
{
    if (mFit) {
        addMessageEvent(std::time(nullptr), fit::EventType::Start);
    }
}

void ActivityWriter::addRecord(const RecordData& record)
{
    if (!mFit) {
        return;
    }

    fit::FitWriter::Data d = mFit->data(L_RECORD);

    d.u32(unixToFitTimestamp(record.timestamp));

    // position_lat/long (semicircles) and enhanced_altitude (5 * m + 500): the
    // invalid sentinel on a no-fix record, not 0/0 -- 0 semicircles/0 is a
    // real (if unlikely) position off the coast of west Africa, so it must
    // never stand in for "no reading" the way the old FitHelper code left it.
    if (record.hasGps) {
        d.i32(degreesToSemicircles(record.latitude))
         .i32(degreesToSemicircles(record.longitude));
    } else {
        d.invalid(fit::BaseType::SInt32);
        d.invalid(fit::BaseType::SInt32);
    }

    d.u8(record.heartRate);

    if (record.hasGps) {
        d.u32(static_cast<uint32_t>((record.altitude + 500.0f) * 5.0f));
    } else {
        d.invalid(fit::BaseType::UInt32);
    }

    // Developer field, in definition order.
    d.u8(record.trustLevel);

    d.write();
}

void ActivityWriter::addLap(const LapData& lap)
{
    if (!mFit) {
        return;
    }

    mFit->data(L_LAP)
        .u32(unixToFitTimestamp(lap.timestamp))
        .u32(unixToFitTimestamp(lap.timeStart))
        .u32(static_cast<uint32_t>(lap.elapsed * 1000))
        .u32(static_cast<uint32_t>(lap.duration * 1000))
        .u16(mLapCounter)  // message_index
        .u8(lap.hrAvg)
        .u8(lap.hrMax)
        .write();

    mLapCounter++;
}

void ActivityWriter::stop(const TrackData& track)
{
    if (!mFit) {
        return;
    }

    addMessageEvent(std::time(nullptr), fit::EventType::Stop);

    mFit->data(L_SESSION)
        .u32(unixToFitTimestamp(track.timeStart))
        .u32(unixToFitTimestamp(track.timeStart))
        .u32(static_cast<uint32_t>(track.elapsed * 1000))
        .u32(static_cast<uint32_t>(track.duration * 1000))
        .u16(0)  // message_index
        .u8(static_cast<uint8_t>(fit::Sport::Running))
        .u8(static_cast<uint8_t>(fit::SubSport::Generic))
        .u8(track.hrAvg)
        .u8(track.hrMax)
        .u16(mLapCounter)  // num_laps
        .write();

    mFit->data(L_ACTIVITY)
        .u32(unixToFitTimestamp(track.timeStart))
        .u32(static_cast<uint32_t>(track.duration * 1000))
        .u32(unixToFitTimestamp(epochToLocal(track.timeStart)))
        .u16(1)  // num_sessions
        .write();

    mFit->finish();
    mFit.reset();

    saveFile();
}

void ActivityWriter::discard()
{
    mFit.reset();
    deleteFile();
}

void ActivityWriter::addMessageEvent(std::time_t t, fit::EventType type)
{
    mFit->data(L_EVENT)
        .u32(unixToFitTimestamp(t))
        .u8(static_cast<uint8_t>(fit::Event::Timer))
        .u8(static_cast<uint8_t>(type))
        .write();
}

bool ActivityWriter::createAndOpenFile(std::time_t utc)
{
    char buff[256]{};
    std::tm localTime{};
#if WIN32
    localtime_s(&localTime, &utc);
#else
    localtime_r(&utc, &localTime);
#endif

    int len = snprintf(buff, sizeof(buff), "%s/%04u%02u/", mPath,
                       localTime.tm_year + 1900, localTime.tm_mon + 1);
    if (len <= 0 || !mKernel.fs.mkdir(buff)) {
        LOG_ERROR("Failed to create dir [%s]\n", buff);
        return false;
    }

    snprintf(&buff[len], sizeof(buff) - len,
             "activity_%04u%02u%02uT%02u%02u%02u.fit",
             localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
             localTime.tm_hour, localTime.tm_min, localTime.tm_sec);

    mFile = mKernel.fs.file(buff);
    if (!mFile || !mFile->open(true, true)) {
        LOG_ERROR("Failed to create file [%s]\n", buff);
        mFile.reset();
        return false;
    }

    LOG_INFO("Recording to [%s]\n", buff);
    return true;
}

void ActivityWriter::saveFile()
{
    if (!mFile) {
        return;
    }
    mFile->flush();
    mFile->close();
    LOG_INFO("FIT file saved.\n");
}

void ActivityWriter::deleteFile()
{
    if (!mFile) {
        return;
    }
    if (mFile->isOpen()) {
        mFile->close();
    }
    mFile->remove();
    mFile.reset();
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

int32_t ActivityWriter::degreesToSemicircles(float degrees)
{
    return static_cast<int32_t>(degrees * (2147483648.0 / 180.0));
}
