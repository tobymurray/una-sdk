/**
 ******************************************************************************
 * @file    ActivityWriter_test.cpp
 * @brief   Host tests for RawTilesMap's ActivityWriter (native SDK::Fit encoder).
 ******************************************************************************
 */

#include "ActivityWriter.hpp"

#include "KernelTestDoubles.hpp"
#include "fit/FitReader.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace fit = SDK::Fit;

namespace {

std::vector<uint8_t> findFitFile(const SDK::TestSupport::InMemoryFileSystem& fs)
{
    for (const auto& [path, entry] : fs.files) {
        if (entry.exists && path.size() > 4 && path.substr(path.size() - 4) == ".fit") {
            return std::vector<uint8_t>(entry.content.begin(), entry.content.end());
        }
    }
    return {};
}

}  // namespace

TEST(RawTilesMapActivityWriter, ProducesValidFitFileWithLapsAndGpsGating)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "D:");

    ActivityWriter::AppInfo info{};
    info.timestamp  = 1782475200;  // 2026-06-26 12:00 UTC
    info.appVersion = 0x00010203;
    info.devID      = "UNA";
    info.appID      = "A1B2C3D4E5F60001";
    w.start(info);

    // Record with a GPS fix: position/altitude carry real values.
    {
        ActivityWriter::RecordData rec{};
        rec.timestamp  = info.timestamp;
        rec.heartRate  = 120;
        rec.trustLevel = 80;
        rec.latitude   = 51.5074f;
        rec.longitude  = -0.1278f;
        rec.altitude   = 35.0f;
        rec.hasGps     = true;
        w.addRecord(rec);
    }
    // Record before first fix: hasGps is false. Position/altitude must encode
    // as the FIT invalid sentinel, not 0/0 (a real, misleading position off
    // the coast of west Africa) -- the bug the FitHelper version had.
    {
        ActivityWriter::RecordData rec{};
        rec.timestamp  = info.timestamp + 1;
        rec.heartRate  = 121;
        rec.trustLevel = 60;
        rec.hasGps     = false;
        w.addRecord(rec);
    }

    ActivityWriter::LapData lap1{};
    lap1.timestamp = info.timestamp + 1; lap1.timeStart = info.timestamp;
    lap1.duration = 1; lap1.elapsed = 1; lap1.hrAvg = 120; lap1.hrMax = 121;
    w.addLap(lap1);

    ActivityWriter::LapData lap2{};
    lap2.timestamp = info.timestamp + 2; lap2.timeStart = info.timestamp + 1;
    lap2.duration = 1; lap2.elapsed = 1; lap2.hrAvg = 121; lap2.hrMax = 121;
    w.addLap(lap2);

    ActivityWriter::TrackData track{};
    track.timeStart = info.timestamp; track.duration = 2; track.elapsed = 2;
    track.hrAvg = 120; track.hrMax = 121;
    w.stop(track);

    const std::vector<uint8_t> bytes = findFitFile(fx.fileSystem);
    ASSERT_FALSE(bytes.empty()) << "a .fit file was produced";

    testfit::FitReader r(bytes);
    EXPECT_TRUE(r.ok()) << "records parse cleanly";
    EXPECT_TRUE(r.crcValid()) << "file CRC verifies";

    const auto fileIds = r.withGlobal(fit::mesgNum(fit::MesgNum::FileId));
    ASSERT_EQ(fileIds.size(), 1u);
    EXPECT_EQ(fileIds[0]->fields.at(1).u(), static_cast<uint64_t>(fit::Manufacturer::Development));
    EXPECT_EQ(
        std::string(reinterpret_cast<const char*>(fileIds[0]->fields.at(8).raw.data())),
        "UNA Watch");

    const auto fieldDescs = r.withGlobal(fit::mesgNum(fit::MesgNum::FieldDescription));
    ASSERT_EQ(fieldDescs.size(), 1u);
    EXPECT_EQ(
        std::string(reinterpret_cast<const char*>(fieldDescs[0]->fields.at(3).raw.data())),
        "hr_trust_level");
    EXPECT_EQ(
        std::string(reinterpret_cast<const char*>(fieldDescs[0]->fields.at(8).raw.data())),
        "percents");

    const auto recs = r.withGlobal(fit::mesgNum(fit::MesgNum::Record));
    ASSERT_EQ(recs.size(), 2u);

    // Record 0: GPS fix present -> real position + altitude.
    EXPECT_EQ(recs[0]->fields.at(3).u(), 120u);  // heart_rate
    EXPECT_EQ(recs[0]->devFields.at(0).u(), 80u);  // hr_trust_level
    EXPECT_NE(recs[0]->fields.at(0).u(), fit::baseTypeInvalid(fit::BaseType::SInt32));  // position_lat
    EXPECT_NE(recs[0]->fields.at(78).u(), fit::baseTypeInvalid(fit::BaseType::UInt32)); // enhanced_altitude

    // Record 1: no fix -> position/altitude are the invalid sentinel, not 0/0.
    EXPECT_EQ(recs[1]->fields.at(3).u(), 121u);  // heart_rate still written
    EXPECT_EQ(recs[1]->devFields.at(0).u(), 60u);  // hr_trust_level
    EXPECT_EQ(recs[1]->fields.at(0).u(), fit::baseTypeInvalid(fit::BaseType::SInt32));  // position_lat
    EXPECT_EQ(recs[1]->fields.at(1).u(), fit::baseTypeInvalid(fit::BaseType::SInt32));  // position_long
    EXPECT_EQ(recs[1]->fields.at(78).u(), fit::baseTypeInvalid(fit::BaseType::UInt32)); // enhanced_altitude

    // Laps: message_index increments (0, 1), not hardcoded to 0 for every lap.
    const auto laps = r.withGlobal(fit::mesgNum(fit::MesgNum::Lap));
    ASSERT_EQ(laps.size(), 2u);
    EXPECT_EQ(laps[0]->fields.at(254).u(), 0u);
    EXPECT_EQ(laps[1]->fields.at(254).u(), 1u);

    // Session: num_laps reflects the real count, sport is Running.
    const auto sessions = r.withGlobal(fit::mesgNum(fit::MesgNum::Session));
    ASSERT_EQ(sessions.size(), 1u);
    EXPECT_EQ(sessions[0]->fields.at(26).u(), 2u);  // num_laps
    EXPECT_EQ(sessions[0]->fields.at(5).u(), static_cast<uint64_t>(fit::Sport::Running));
}

TEST(RawTilesMapActivityWriter, DiscardRemovesTheFile)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "D:");

    ActivityWriter::AppInfo info{};
    info.timestamp = 1782475200;
    info.devID     = "UNA";
    info.appID     = "A1B2C3D4E5F60001";
    w.start(info);
    w.discard();

    EXPECT_TRUE(findFitFile(fx.fileSystem).empty());
}
