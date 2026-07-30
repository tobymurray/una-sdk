/**
 ******************************************************************************
 * @file    ActivityWriter_test.cpp
 * @brief   Host tests for the shared SDK::Fit::ActivityWriter plumbing,
 *          exercised the way an app composes it: define local messages using
 *          the base field-list constants (+ its own extras, none needed for
 *          this "Workout-shape" test), write through the base field helpers,
 *          and drive the shared lifecycle (start/beginRecording/finalize/
 *          discard/recoverInterrupted).
 ******************************************************************************
 */

#include "SDK/Fit/ActivityWriter.hpp"
#include "KernelTestDoubles.hpp"
#include "SDK/Fit/FitProfile.hpp"
#include "fit/FitReader.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace fit = SDK::Fit;
using SDK::Fit::ActivityWriter;

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

/// Local message layout for a minimal ("Workout-shape") app: no extra fields
/// beyond the shared base -- just time + heart rate (+ optional battery).
struct TestAppLocals {
    uint8_t recordLocal;
    uint8_t recordBattLocal;
    uint8_t lapLocal;
    uint8_t sessionLocal;
    uint8_t devHrSource;
    uint8_t devBattLevel;
    uint8_t devBattVoltage;
};

TestAppLocals defineTestApp(ActivityWriter& w)
{
    TestAppLocals L{};
    L.devHrSource     = w.registerDevField("hr_source", nullptr, fit::BaseType::UInt8);
    L.devBattLevel    = w.registerDevField("batteryLevel", "%", fit::BaseType::UInt8);
    L.devBattVoltage  = w.registerDevField("battVoltage", "mV", fit::BaseType::UInt16);

    L.recordLocal = w.allocateLocalType();
    w.fit().defineMessage(L.recordLocal, fit::mesgNum(fit::MesgNum::Record),
        {fit::field::Record::Timestamp, fit::field::Record::HeartRate},
        {{L.devHrSource, 1, 0}});

    L.recordBattLocal = w.allocateLocalType();
    w.fit().defineMessage(L.recordBattLocal, fit::mesgNum(fit::MesgNum::Record),
        {fit::field::Record::Timestamp, fit::field::Record::HeartRate},
        {{L.devBattLevel, 1, 0}, {L.devBattVoltage, 2, 0}, {L.devHrSource, 1, 0}});

    L.lapLocal = w.allocateLocalType();
    w.fit().defineMessage(L.lapLocal, fit::mesgNum(fit::MesgNum::Lap),
        {fit::field::Lap::Timestamp, fit::field::Lap::StartTime,
         fit::field::Lap::TotalElapsedTime, fit::field::Lap::TotalTimerTime,
         fit::field::Lap::MessageIndex, fit::field::Lap::AvgHeartRate,
         fit::field::Lap::MaxHeartRate});

    L.sessionLocal = w.allocateLocalType();
    w.fit().defineMessage(L.sessionLocal, fit::mesgNum(fit::MesgNum::Session),
        {fit::field::Session::Timestamp, fit::field::Session::StartTime,
         fit::field::Session::TotalElapsedTime, fit::field::Session::TotalTimerTime,
         fit::field::Session::MessageIndex, fit::field::Session::NumLaps,
         fit::field::Session::Sport, fit::field::Session::SubSport,
         fit::field::Session::AvgHeartRate, fit::field::Session::MaxHeartRate});

    return L;
}

}  // namespace

TEST(ActivityWriter, ProducesValidFitFile)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");

    ActivityWriter::AppInfo info;
    info.timestamp  = 1782475200;  // 2026-06-26 12:00 UTC
    info.appVersion = 0x00010203;
    info.devID      = "UNA";
    info.appID      = "squash";
    ASSERT_TRUE(w.start(info));

    const TestAppLocals L = defineTestApp(w);
    ASSERT_TRUE(w.beginRecording(info.timestamp));

    // Plain HR record.
    {
        auto d = w.fit().data(L.recordLocal);
        ActivityWriter::writeBaseRecord(d, info.timestamp, /*hasHr=*/true, 120).u8(1 /*hr_source*/);
        EXPECT_TRUE(d.write());
        w.noteRecordWritten(info.timestamp);
    }
    // HR + battery record.
    {
        auto d = w.fit().data(L.recordBattLocal);
        ActivityWriter::writeBaseRecord(d, info.timestamp + 1, /*hasHr=*/true, 130)
            .u8(90).u16(4100).u8(1 /*hr_source*/);
        EXPECT_TRUE(d.write());
        w.noteRecordWritten(info.timestamp + 1);
    }

    {
        auto d = w.fit().data(L.lapLocal);
        ActivityWriter::writeBaseLap(d, info.timestamp + 1, info.timestamp, /*elapsed=*/1,
                              /*duration=*/1, w.lapCount(), /*hrAvg=*/125.0f, /*hrMax=*/130.0f);
        EXPECT_TRUE(d.write());
        w.noteLapWritten(info.timestamp + 1);
    }

    {
        auto d = w.fit().data(L.sessionLocal);
        ActivityWriter::writeBaseSession(d, info.timestamp + 1, info.timestamp, /*elapsed=*/1,
                                  /*duration=*/1, /*messageIndex=*/0, w.lapCount(),
                                  fit::Sport::Cycling, fit::SubSport::Generic,
                                  /*hrAvg=*/125.0f, /*hrMax=*/130.0f);
        EXPECT_TRUE(d.write());
    }
    EXPECT_TRUE(w.writeActivityMessage(info.timestamp + 1, /*duration=*/1, /*numSessions=*/1));

    EXPECT_TRUE(w.finalize());
    EXPECT_FALSE(fx.fileSystem.exist(kMarkerPath)) << "success clears the marker";

    EXPECT_TRUE(w.writeSummary(info.timestamp, 1, 125.0f, "squash"));

    const std::vector<uint8_t> bytes = findFitFile(fx.fileSystem);
    ASSERT_FALSE(bytes.empty()) << "a .fit file was produced";

    testfit::FitReader r(bytes);
    EXPECT_TRUE(r.ok()) << "records parse cleanly";
    EXPECT_TRUE(r.crcValid()) << "file CRC verifies";

    const auto fileIds = r.withGlobal(fit::mesgNum(fit::MesgNum::FileId));
    ASSERT_EQ(fileIds.size(), 1u);
    EXPECT_EQ(fileIds[0]->fields.at(1).u(), 351u);  // manufacturer = Una

    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::Event)).size(), 1u);  // start only
    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::Record)).size(), 2u);
    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::Lap)).size(), 1u);
    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::Session)).size(), 1u);
    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::Activity)).size(), 1u);
    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::FieldDescription)).size(), 3u);

    const auto recs = r.withGlobal(fit::mesgNum(fit::MesgNum::Record));
    EXPECT_EQ(recs[0]->fields.at(3).u(), 120u);   // heart_rate
    EXPECT_EQ(recs[1]->devFields.at(L.devBattLevel).u(), 90u);
    EXPECT_EQ(recs[1]->devFields.at(L.devBattVoltage).u(), 4100u);

    const auto* ses = r.withGlobal(fit::mesgNum(fit::MesgNum::Session)).front();
    EXPECT_EQ(ses->fields.at(5).u(), static_cast<uint64_t>(fit::Sport::Cycling));
    EXPECT_EQ(ses->fields.at(6).u(), static_cast<uint64_t>(fit::SubSport::Generic));

    const std::string fitPath = findFitPath(fx.fileSystem);
    const std::string jsonPath = fitPath.substr(0, fitPath.size() - 3) + "json";
    const std::string json = fx.fileSystem.readFile(jsonPath);
    EXPECT_NE(json.find("\"activity_type\":\"squash\""), std::string::npos);
}

// The .fit is flushed once beginRecording() lands (header + defs), then only
// when a record crosses a >=30 s boundary (and on every lap) -- unchanged
// from the per-app copies this replaces.
TEST(ActivityWriter, FlushCadence)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");

    ActivityWriter::AppInfo info;
    info.timestamp = 1000;  // small base keeps the flush deltas obvious
    info.appID     = "squash";
    ASSERT_TRUE(w.start(info));
    const TestAppLocals L = defineTestApp(w);
    ASSERT_TRUE(w.beginRecording(info.timestamp));

    const std::string fitPath = findFitPath(fx.fileSystem);
    ASSERT_FALSE(fitPath.empty());
    const size_t afterStart = fx.fileSystem.flushCounts[fitPath];
    EXPECT_EQ(afterStart, 1u) << "beginRecording() flushes the .fit once (header + defs)";

    const std::time_t offsets[] = {0, 10, 20, 35, 70};
    for (std::time_t off : offsets) {
        auto d = w.fit().data(L.recordLocal);
        ActivityWriter::writeBaseRecord(d, info.timestamp + off, true, 120).u8(1);
        d.write();
        w.noteRecordWritten(info.timestamp + off);
    }
    EXPECT_EQ(fx.fileSystem.flushCounts[fitPath] - afterStart, 2u)
        << "flush only when crossing the >=30 s boundary (at +35 and +70)";

    const size_t beforeLap = fx.fileSystem.flushCounts[fitPath];
    auto d = w.fit().data(L.lapLocal);
    ActivityWriter::writeBaseLap(d, info.timestamp + 70, info.timestamp, 70, 70, w.lapCount(), 120.0f, 120.0f);
    d.write();
    w.noteLapWritten(info.timestamp + 70);
    EXPECT_EQ(fx.fileSystem.flushCounts[fitPath] - beforeLap, 1u) << "each lap flushes";
}

// End-to-end wiring: a crash (no finalize()) leaves the marker; a fresh
// writer's recoverInterrupted() finalizes the .fit and clears the marker.
TEST(ActivityWriter, RecoverInterruptedThroughApp)
{
    SDK::TestSupport::KernelFixture fx;

    {
        ActivityWriter w(fx.kernel, "Activity");
        ActivityWriter::AppInfo info;
        info.timestamp = 1782475200;
        info.appID     = "squash";
        ASSERT_TRUE(w.start(info));
        const TestAppLocals L = defineTestApp(w);
        ASSERT_TRUE(w.beginRecording(info.timestamp));

        auto d = w.fit().data(L.recordLocal);
        ActivityWriter::writeBaseRecord(d, info.timestamp, true, 120).u8(1);
        d.write();
        w.noteRecordWritten(info.timestamp);

        auto lapData = w.fit().data(L.lapLocal);  // laps flush -> marker covers the record
        ActivityWriter::writeBaseLap(lapData, info.timestamp, info.timestamp, 0, 0, w.lapCount(), 120.0f, 120.0f);
        lapData.write();
        w.noteLapWritten(info.timestamp);
        // Crash: no finalize() -> file left unfinished, marker present.
    }

    ASSERT_TRUE(fx.fileSystem.exist(kMarkerPath));

    ActivityWriter fresh(fx.kernel, "Activity");
    EXPECT_TRUE(fresh.recoverInterrupted());
    EXPECT_FALSE(fx.fileSystem.exist(kMarkerPath)) << "marker cleared after recovery";

    const std::vector<uint8_t> bytes = findFitFile(fx.fileSystem);
    ASSERT_FALSE(bytes.empty());
    testfit::FitReader r(bytes);
    EXPECT_TRUE(r.ok()) << "recovered .fit parses";
    EXPECT_TRUE(r.crcValid()) << "recovered .fit CRC verifies";

    EXPECT_FALSE(fresh.recoverInterrupted()) << "no marker left -> nothing to do";
}

// finalize() surfaces the FIT-durability contract: false (and the marker
// kept) when the underlying writes fail.
TEST(ActivityWriter, FinalizeReturnsFalseOnWriteFailure)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");

    ActivityWriter::AppInfo info;
    info.timestamp = 1782475200;
    info.appID     = "squash";
    ASSERT_TRUE(w.start(info));
    const TestAppLocals L = defineTestApp(w);
    ASSERT_TRUE(w.beginRecording(info.timestamp));

    auto d = w.fit().data(L.recordLocal);
    ActivityWriter::writeBaseRecord(d, info.timestamp, true, 120).u8(1);
    d.write();

    // Fail every write from here on: the activity/finish writes fail.
    fx.fileSystem.failWritesAfterBytes = fx.fileSystem.bytesWritten;

    EXPECT_FALSE(w.writeActivityMessage(info.timestamp + 1, 1, 1));
    EXPECT_FALSE(w.finalize());
    EXPECT_TRUE(fx.fileSystem.exist(kMarkerPath))
        << "on failure the marker survives so the next boot recovers the .fit";
}

TEST(ActivityWriter, DiscardRemovesFile)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");

    ActivityWriter::AppInfo info;
    info.timestamp = 1782475200;
    info.appID     = "squash";
    ASSERT_TRUE(w.start(info));
    defineTestApp(w);
    ASSERT_TRUE(w.beginRecording(info.timestamp));

    const std::string fitPath = findFitPath(fx.fileSystem);
    ASSERT_FALSE(fitPath.empty());

    w.discard();
    EXPECT_FALSE(fx.fileSystem.exist(fitPath.c_str())) << "discard() removes the .fit";
    EXPECT_FALSE(fx.fileSystem.exist(kMarkerPath)) << "discard() clears the marker";
}
