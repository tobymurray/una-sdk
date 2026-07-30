/**
 ******************************************************************************
 * @file    ActivityWriter_test.cpp
 * @brief   Host smoke test for the Workout ActivityWriter, now composed on
 *          top of the shared SDK::Fit::ActivityWriter. Confirms the migration
 *          preserved Workout's own extra fields (active + resting/metabolic
 *          calories) exactly as before, on top of the shared base.
 ******************************************************************************
 */

#include "ActivityWriter.hpp"
#include "KernelTestDoubles.hpp"
#include "SDK/Fit/FitProfile.hpp"
#include "fit/FitReader.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace fit = SDK::Fit;

namespace {

std::string findFitPath(const SDK::TestSupport::InMemoryFileSystem& fs)
{
    for (const auto& kv : fs.files) {
        const std::string& path = kv.first;
        if (kv.second.exists && path.size() > 4
            && path.compare(path.size() - 4, 4, ".fit") == 0) {
            return path;
        }
    }
    return {};
}

std::vector<uint8_t> findFitFile(const SDK::TestSupport::InMemoryFileSystem& fs)
{
    const std::string path = findFitPath(fs);
    if (path.empty()) {
        return {};
    }
    const std::string s = fs.readFile(path);
    return std::vector<uint8_t>(s.begin(), s.end());
}

constexpr const char* kMarkerPath = "Activity/.recording";

}  // namespace

TEST(WorkoutActivityWriter, ProducesValidFitFileWithCalories)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");

    ActivityWriter::AppInfo info;
    info.timestamp  = 1782475200;  // 2026-06-26 12:00 UTC
    info.appVersion = 0x00010203;
    info.devID      = "UNA";
    info.appID      = "workout";
    w.start(info);

    ActivityWriter::RecordData rec;
    rec.timestamp = info.timestamp;
    rec.set(ActivityWriter::RecordData::Field::HEART_RATE);
    rec.heartRate = 120;
    w.addRecord(rec);

    rec.timestamp = info.timestamp + 1;
    rec.set(ActivityWriter::RecordData::Field::BATTERY);
    rec.batteryLevel = 88; rec.batteryVoltage = 4050;
    w.addRecord(rec);

    ActivityWriter::LapData lap;
    lap.timestamp = info.timestamp + 1;
    lap.timeStart = info.timestamp;
    lap.duration = 1; lap.elapsed = 1;
    lap.hrAvg = 125; lap.hrMax = 130;
    lap.calories = 12.4f;
    lap.restingCalories = 1.1f;
    w.addLap(lap);

    ActivityWriter::TrackData track;
    track.timestamp = info.timestamp + 1;
    track.timeStart = info.timestamp;
    track.duration = 1; track.elapsed = 1;
    track.hrAvg = 125; track.hrMax = 130;
    track.calories = 12.4f;
    track.metabolicCalories = 0.8f;
    EXPECT_TRUE(w.stop(track));
    EXPECT_FALSE(fx.fileSystem.exist(kMarkerPath)) << "success clears the marker";

    const std::vector<uint8_t> bytes = findFitFile(fx.fileSystem);
    ASSERT_FALSE(bytes.empty());
    testfit::FitReader r(bytes);
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.crcValid());

    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::Record)).size(), 2u);
    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::Lap)).size(), 1u);
    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::Session)).size(), 1u);
    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::Activity)).size(), 1u);
    // 5 developer field descriptions: battLevel, battVoltage, resting_calories,
    // hr_source, hr_optical, hr_external.
    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::FieldDescription)).size(), 6u);

    const auto* lapMsg = r.withGlobal(fit::mesgNum(fit::MesgNum::Lap)).front();
    EXPECT_EQ(lapMsg->fields.at(11).u(), 12u);        // total_calories (rounded)
    ASSERT_EQ(lapMsg->devFields.size(), 1u);
    EXPECT_EQ(lapMsg->devFields.begin()->second.u(), 1u);  // resting_calories (rounded)

    const auto* ses = r.withGlobal(fit::mesgNum(fit::MesgNum::Session)).front();
    EXPECT_EQ(ses->fields.at(5).u(), static_cast<uint64_t>(fit::Sport::Generic));
    EXPECT_EQ(ses->fields.at(11).u(), 12u);   // total_calories
    EXPECT_EQ(ses->fields.at(196).u(), 1u);   // metabolic_calories (0.8 rounds to 1)

    const auto recs = r.withGlobal(fit::mesgNum(fit::MesgNum::Record));
    EXPECT_EQ(recs[1]->devFields.size(), 5u);  // battery record carries all 5 dev fields
}

TEST(WorkoutActivityWriter, RecoverInterruptedThroughApp)
{
    SDK::TestSupport::KernelFixture fx;

    {
        ActivityWriter w(fx.kernel, "Activity");
        ActivityWriter::AppInfo info;
        info.timestamp = 1782475200;
        info.appID     = "workout";
        w.start(info);

        ActivityWriter::RecordData rec;
        rec.timestamp = info.timestamp;
        rec.set(ActivityWriter::RecordData::Field::HEART_RATE);
        rec.heartRate = 120;
        w.addRecord(rec);

        ActivityWriter::LapData lap;  // laps flush -> marker covers the record
        lap.timestamp = info.timestamp;
        lap.timeStart = info.timestamp;
        w.addLap(lap);
        // Crash: no stop() -> file left unfinished, marker present.
    }

    ASSERT_TRUE(fx.fileSystem.exist(kMarkerPath));

    ActivityWriter fresh(fx.kernel, "Activity");
    EXPECT_TRUE(fresh.recoverInterrupted());
    EXPECT_FALSE(fx.fileSystem.exist(kMarkerPath));

    const std::vector<uint8_t> bytes = findFitFile(fx.fileSystem);
    ASSERT_FALSE(bytes.empty());
    testfit::FitReader r(bytes);
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.crcValid());
}
