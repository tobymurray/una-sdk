/**
 ******************************************************************************
 * @file    ActivityWriter_test.cpp
 * @brief   Tests for the GpsLab ActivityWriter (Running + GNSS quality series) on the native encoder.
 ******************************************************************************
 */

#include "ActivityWriter.hpp"
#include "KernelTestDoubles.hpp"
#include "SDK/Fit/FitProfile.hpp"
#include "fit/FitReader.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <limits>
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

/// Reinterpret a 4-byte FIT Float32 field value.
float asF32(const testfit::FitReader::FieldVal& v)
{
    if (v.raw.size() != sizeof(float)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    float f = 0.0f;
    std::memcpy(&f, v.raw.data(), sizeof(f));
    return f;
}

/// True when the field carries the canonical Float32 invalid sentinel
/// (all bits set) rather than a real measurement.
bool isF32Invalid(const testfit::FitReader::FieldVal& v)
{
    return v.raw.size() == sizeof(float)
        && v.raw[0] == 0xFF && v.raw[1] == 0xFF
        && v.raw[2] == 0xFF && v.raw[3] == 0xFF;
}

namespace devfield {
constexpr uint8_t kGpsPrecision  = 7;
constexpr uint8_t kGpsState      = 8;
constexpr uint8_t kGpsAltitude   = 9;
constexpr uint8_t kGpsSpeed      = 10;
constexpr uint8_t kGpsFixAge     = 11;
constexpr uint8_t kGnssTtff      = 12;
constexpr uint8_t kGnssPwrOffset = 13;
constexpr uint8_t kHrTrust       = 14;
}  // namespace devfield

namespace recfield {
constexpr uint8_t kDistance    = 5;
constexpr uint8_t kGpsAccuracy = 31;
}  // namespace recfield

using GpsState = ActivityWriter::RecordData::GpsState;

}  // namespace

TEST(GpsLabActivityWriter, ProducesValidFitFile)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "D:");

    ActivityWriter::AppInfo info;
    info.timestamp  = 1782475200;  // 2026-06-26 12:00 UTC
    info.appVersion = 0x00010203;
    info.devID      = "UNA";
    info.appID      = "running";
    w.start(info);

    // Plain record (HR only).
    {
        ActivityWriter::RecordData r;
        r.timestamp = info.timestamp;
        r.set(ActivityWriter::RecordData::Field::HEART_RATE);
        r.heartRate = 120;
        r.set(ActivityWriter::RecordData::Field::CADENCE);
        r.cadenceSpm = 170;
        w.addRecord(r);
    }
    // GPS record.
    {
        ActivityWriter::RecordData r;
        r.timestamp = info.timestamp + 1;
        r.set(ActivityWriter::RecordData::Field::COORDS);
        r.latitude = 51.5074f; r.longitude = -0.1278f;
        r.set(ActivityWriter::RecordData::Field::SPEED);  r.speed = 3.0f;
        r.set(ActivityWriter::RecordData::Field::ALTITUDE); r.altitude = 35.0f;
        r.set(ActivityWriter::RecordData::Field::HEART_RATE); r.heartRate = 130;
        r.hrSource = 1; r.hrOpticalBpm = 130; r.hrTrust = 2.5f;
        w.addRecord(r);
    }
    // GPS + battery record. HEART_RATE is deliberately left unset (as the app
    // does when its hrTrustLevel gate rejects a reading) with a trust value
    // that would have failed that gate -- hr_trust must still be written, so
    // a rejected reading's trust is visible for threshold analysis.
    {
        ActivityWriter::RecordData r;
        r.timestamp = info.timestamp + 2;
        r.set(ActivityWriter::RecordData::Field::COORDS);
        r.latitude = 51.5075f; r.longitude = -0.1278f;
        r.set(ActivityWriter::RecordData::Field::BATTERY);
        r.batteryLevel = 90; r.batteryVoltage = 4100;
        r.heartRate = 140;  // arbitrated bpm the gate rejected; HEART_RATE left unset
        r.hrTrust = 0.4f;   // below the app's accepted [1,3] band
        w.addRecord(r);
    }

    ActivityWriter::LapData lap;
    lap.timestamp = info.timestamp + 2; lap.timeStart = info.timestamp;
    lap.duration = 3; lap.elapsed = 3; lap.distance = 9.0f;
    lap.speedAvg = 3.0f; lap.hrAvg = 130;
    w.addLap(lap);

    ActivityWriter::TrackData track;
    track.timestamp = info.timestamp + 2; track.timeStart = info.timestamp;
    track.duration = 3; track.elapsed = 3; track.distance = 9.0f;
    track.speedAvg = 3.0f; track.hrAvg = 130; track.hrMax = 140;
    w.stop(track);

    const std::vector<uint8_t> bytes = findFitFile(fx.fileSystem);
    ASSERT_FALSE(bytes.empty()) << "a .fit file was produced";

    testfit::FitReader r(bytes);
    EXPECT_TRUE(r.ok()) << "records parse cleanly";
    EXPECT_TRUE(r.crcValid()) << "file CRC verifies";

    const auto fileIds = r.withGlobal(fit::mesgNum(fit::MesgNum::FileId));
    ASSERT_EQ(fileIds.size(), 1u);
    EXPECT_EQ(fileIds[0]->fields.at(1).u(), 351u);  // manufacturer = Una
    EXPECT_EQ(fileIds[0]->fields.at(2).u(), 1u);    // product = UnaWatch
    EXPECT_EQ(                                       // product_name (null-terminated string)
        std::string(reinterpret_cast<const char*>(fileIds[0]->fields.at(8).raw.data())),
        "UNA Watch");
    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::Event)).size(), 1u);  // start only
    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::Record)).size(), 3u);
    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::Lap)).size(), 1u);
    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::Session)).size(), 1u);
    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::Activity)).size(), 1u);
    // 13 developer field descriptions: 6 HR/battery (incl. hr_trust), 5
    // per-record GNSS, 2 session-level GNSS acquisition timings.
    EXPECT_EQ(r.withGlobal(fit::mesgNum(fit::MesgNum::FieldDescription)).size(), 13u);

    // Records carry the hr_source/optical/external developer fields (4/5/6).
    const auto recs = r.withGlobal(fit::mesgNum(fit::MesgNum::Record));
    ASSERT_EQ(recs.size(), 3u);
    EXPECT_EQ(recs[0]->fields.at(3).u(), 120u);              // heart_rate
    EXPECT_EQ(recs[0]->devFields.count(4), 1u);              // hr_source dev field
    // hr_trust is a plain HR developer field (not gated by Field::HEART_RATE),
    // so it round-trips on the accepted-HR record just like hr_source does.
    EXPECT_FLOAT_EQ(asF32(recs[1]->devFields.at(devfield::kHrTrust)), 2.5f);
    // GPS+battery record: the app's hasHeartRate gate rejected this reading
    // (native heart_rate reads invalid), but hr_trust is still written --
    // that's the whole point, seeing the trust value a rejected reading had.
    EXPECT_EQ(recs[2]->fields.at(3).u(), fit::baseTypeInvalid(fit::BaseType::UInt8));
    EXPECT_FLOAT_EQ(asF32(recs[2]->devFields.at(devfield::kHrTrust)), 0.4f);
    // GPS+battery record carries battery developer fields 2 and 3.
    EXPECT_EQ(recs[2]->devFields.count(2), 1u);              // batteryLevel
    EXPECT_EQ(recs[2]->devFields.at(2).u(), 90u);
    EXPECT_EQ(recs[2]->devFields.at(3).u(), 4100u);          // batteryVoltage

    // session sport = running.
    const auto* ses = r.withGlobal(fit::mesgNum(fit::MesgNum::Session)).front();
    EXPECT_EQ(ses->fields.at(5).u(), static_cast<uint64_t>(fit::Sport::Running));
}

// The GNSS quality series exists so a trace can be scored after the fact:
// every record says how good the receiver thought it was, and -- crucially --
// records with no position say why they have none. All four variants of the
// record definition carry it, so a dropout is never a silent gap.
TEST(GpsLabActivityWriter, WritesGnssQualitySeries)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "D:");

    ActivityWriter::AppInfo info;
    info.timestamp = 1782475200;
    info.devID     = "UNA";
    info.appID     = "running";
    w.start(info);

    // No fix: subscribed but acquiring. Position is absent, yet the record
    // still reports the receiver's error estimate and the reason for the gap.
    {
        ActivityWriter::RecordData r;
        r.timestamp = info.timestamp;
        r.set(ActivityWriter::RecordData::Field::GPS_PRECISION);
        r.gpsPrecisionM = 12.5f;
        r.set(ActivityWriter::RecordData::Field::GPS_FIX_AGE);
        r.gpsFixAgeMs = 500;
        r.gpsState = GpsState::GPS_SUBSCRIBED | GpsState::GPS_SAMPLE_FRESH;
        w.addRecord(r);
    }
    // Full fix: every GNSS field populated.
    {
        ActivityWriter::RecordData r;
        r.timestamp = info.timestamp + 1;
        r.set(ActivityWriter::RecordData::Field::COORDS);
        r.latitude = 51.5074f; r.longitude = -0.1278f;
        r.set(ActivityWriter::RecordData::Field::GPS_PRECISION); r.gpsPrecisionM = 3.25f;
        r.set(ActivityWriter::RecordData::Field::GPS_ALTITUDE);  r.gpsAltitudeM  = 42.5f;
        r.set(ActivityWriter::RecordData::Field::GPS_SPEED);     r.gpsSpeedMs    = 3.75f;
        r.set(ActivityWriter::RecordData::Field::DISTANCE);      r.distanceM     = 123.5f;
        r.set(ActivityWriter::RecordData::Field::GPS_FIX_AGE);   r.gpsFixAgeMs   = 120;
        r.gpsState = GpsState::GPS_SUBSCRIBED | GpsState::GPS_COORDS_VALID
                   | GpsState::GPS_SPEED_VALID | GpsState::GPS_SAMPLE_FRESH;
        w.addRecord(r);
    }
    // Dead reckoning: the extrapolated speed is still recorded (it is what
    // explains distance creep while stationary), and an error estimate coarse
    // enough to saturate the native uint8 gps_accuracy field.
    {
        ActivityWriter::RecordData r;
        r.timestamp = info.timestamp + 2;
        r.set(ActivityWriter::RecordData::Field::GPS_PRECISION);
        r.gpsPrecisionM = 300.0f;
        r.set(ActivityWriter::RecordData::Field::GPS_SPEED); r.gpsSpeedMs = 1.5f;
        r.gpsState = GpsState::GPS_SUBSCRIBED | GpsState::GPS_DEAD_RECKONING
                   | GpsState::GPS_SAMPLE_FRESH;
        w.addRecord(r);
    }
    // Stale latch: the last sample is too old to trust, so position and
    // precision are withheld and SAMPLE_FRESH is clear. The age still reports.
    {
        ActivityWriter::RecordData r;
        r.timestamp = info.timestamp + 3;
        r.set(ActivityWriter::RecordData::Field::GPS_FIX_AGE);
        r.gpsFixAgeMs = 9000;
        r.gpsState = GpsState::GPS_SUBSCRIBED;
        w.addRecord(r);
    }

    ActivityWriter::TrackData track;
    track.timestamp = info.timestamp + 3; track.timeStart = info.timestamp;
    track.gnssTtffS          = 42;
    track.gnssPowerOnOffsetS = -117;
    w.stop(track);

    const std::vector<uint8_t> bytes = findFitFile(fx.fileSystem);
    ASSERT_FALSE(bytes.empty());
    testfit::FitReader r(bytes);
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.crcValid());

    const auto recs = r.withGlobal(fit::mesgNum(fit::MesgNum::Record));
    ASSERT_EQ(recs.size(), 4u);

    // --- no-fix record: the dropout is self-describing ---------------------
    EXPECT_EQ(recs[0]->fields.count(0), 0u) << "no position written without a fix";
    EXPECT_EQ(recs[0]->devFields.at(devfield::kGpsState).u(),
              static_cast<uint64_t>(GpsState::GPS_SUBSCRIBED
                                    | GpsState::GPS_SAMPLE_FRESH));
    EXPECT_FLOAT_EQ(asF32(recs[0]->devFields.at(devfield::kGpsPrecision)), 12.5f);
    EXPECT_EQ(recs[0]->fields.at(recfield::kGpsAccuracy).u(), 13u);  // 12.5 -> 13
    EXPECT_EQ(recs[0]->devFields.at(devfield::kGpsFixAge).u(), 500u);
    // Unavailable values must read as "no data", never as a plausible zero.
    EXPECT_TRUE(isF32Invalid(recs[0]->devFields.at(devfield::kGpsAltitude)));
    EXPECT_TRUE(isF32Invalid(recs[0]->devFields.at(devfield::kGpsSpeed)));
    EXPECT_EQ(recs[0]->fields.at(recfield::kDistance).u(), 0xFFFFFFFFu);

    // --- full-fix record ----------------------------------------------------
    EXPECT_EQ(recs[1]->devFields.at(devfield::kGpsState).u(),
              static_cast<uint64_t>(GpsState::GPS_SUBSCRIBED
                                    | GpsState::GPS_COORDS_VALID
                                    | GpsState::GPS_SPEED_VALID
                                    | GpsState::GPS_SAMPLE_FRESH));
    EXPECT_FLOAT_EQ(asF32(recs[1]->devFields.at(devfield::kGpsPrecision)), 3.25f);
    EXPECT_FLOAT_EQ(asF32(recs[1]->devFields.at(devfield::kGpsAltitude)), 42.5f);
    EXPECT_FLOAT_EQ(asF32(recs[1]->devFields.at(devfield::kGpsSpeed)), 3.75f);
    EXPECT_EQ(recs[1]->fields.at(recfield::kDistance).u(), 12350u);  // scale 100
    EXPECT_EQ(recs[1]->fields.at(recfield::kGpsAccuracy).u(), 3u);   // 3.25 -> 3
    EXPECT_EQ(recs[1]->devFields.at(devfield::kGpsFixAge).u(), 120u);

    // --- dead reckoning + saturation ---------------------------------------
    EXPECT_EQ(recs[2]->devFields.at(devfield::kGpsState).u(),
              static_cast<uint64_t>(GpsState::GPS_SUBSCRIBED
                                    | GpsState::GPS_DEAD_RECKONING
                                    | GpsState::GPS_SAMPLE_FRESH));
    // 254 is the last representable metre; 255 would read back as "no data".
    EXPECT_EQ(recs[2]->fields.at(recfield::kGpsAccuracy).u(), 254u);
    EXPECT_FLOAT_EQ(asF32(recs[2]->devFields.at(devfield::kGpsPrecision)), 300.0f)
        << "the developer field keeps full resolution past the uint8 ceiling";
    // The extrapolated speed survives; only gps_state says it is unobserved.
    EXPECT_FLOAT_EQ(asF32(recs[2]->devFields.at(devfield::kGpsSpeed)), 1.5f);

    // --- stale latch --------------------------------------------------------
    EXPECT_EQ(recs[3]->fields.count(0), 0u) << "a stale sample yields no position";
    EXPECT_EQ(recs[3]->devFields.at(devfield::kGpsState).u(),
              static_cast<uint64_t>(GpsState::GPS_SUBSCRIBED))
        << "SAMPLE_FRESH must be clear once the latch ages out";
    EXPECT_TRUE(isF32Invalid(recs[3]->devFields.at(devfield::kGpsPrecision)));
    EXPECT_EQ(recs[3]->fields.at(recfield::kGpsAccuracy).u(), 0xFFu);
    EXPECT_EQ(recs[3]->devFields.at(devfield::kGpsFixAge).u(), 9000u)
        << "the age is still reported, so the outage is measurable";

    // --- session-level acquisition timings ----------------------------------
    const auto* ses = r.withGlobal(fit::mesgNum(fit::MesgNum::Session)).front();
    EXPECT_EQ(static_cast<int32_t>(ses->devFields.at(devfield::kGnssTtff).u()), 42);
    EXPECT_EQ(static_cast<int32_t>(ses->devFields.at(devfield::kGnssPwrOffset).u()), -117);
}

// A precision of zero means the receiver declared nothing. It must not encode
// as 0 m -- the most confident value gps_accuracy can express -- or the
// declared-vs-actual accuracy comparison silently gains a cluster of
// impossibly good declarations at exactly the epochs with no estimate.
TEST(GpsLabActivityWriter, UndeclaredPrecisionIsInvalidNotZero)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "D:");

    ActivityWriter::AppInfo info;
    info.timestamp = 1782475200;
    info.devID = "UNA"; info.appID = "running";
    w.start(info);

    // GPS_PRECISION unset: the caller had no estimate to offer.
    {
        ActivityWriter::RecordData r;
        r.timestamp = info.timestamp;
        r.set(ActivityWriter::RecordData::Field::COORDS);
        r.latitude = 51.5074f; r.longitude = -0.1278f;
        r.gpsState = GpsState::GPS_SUBSCRIBED | GpsState::GPS_COORDS_VALID
                   | GpsState::GPS_SAMPLE_FRESH;
        w.addRecord(r);
    }

    ActivityWriter::TrackData track;
    track.timestamp = info.timestamp; track.timeStart = info.timestamp;
    w.stop(track);

    testfit::FitReader r(findFitFile(fx.fileSystem));
    ASSERT_TRUE(r.ok());
    const auto recs = r.withGlobal(fit::mesgNum(fit::MesgNum::Record));
    ASSERT_EQ(recs.size(), 1u);

    EXPECT_EQ(recs[0]->fields.at(recfield::kGpsAccuracy).u(), 0xFFu)
        << "no estimate must read as invalid, not as a perfect 0 m fix";
    EXPECT_TRUE(isF32Invalid(recs[0]->devFields.at(devfield::kGpsPrecision)));
    // The position itself is unaffected.
    EXPECT_EQ(recs[0]->fields.count(0), 1u);

    // Session timings default to the invalid sentinel when GNSS never
    // acquired, rather than to a plausible zero-second TTFF.
    const auto* ses = r.withGlobal(fit::mesgNum(fit::MesgNum::Session)).front();
    EXPECT_EQ(static_cast<int32_t>(ses->devFields.at(devfield::kGnssTtff).u()),
              ActivityWriter::TrackData::kGnssTimingInvalid);
}

// The battery-without-GPS variant (L_RECORD_B) has its own developer-field
// layout and is produced by no other test; a write/definition order mismatch
// confined to it would otherwise ship undetected.
TEST(GpsLabActivityWriter, BatteryWithoutGpsRecordVariantIsWellFormed)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "D:");

    ActivityWriter::AppInfo info;
    info.timestamp = 1782475200;
    info.devID = "UNA"; info.appID = "running";
    w.start(info);

    {
        ActivityWriter::RecordData r;
        r.timestamp = info.timestamp;
        r.set(ActivityWriter::RecordData::Field::BATTERY);
        r.batteryLevel = 77; r.batteryVoltage = 3950;
        r.set(ActivityWriter::RecordData::Field::HEART_RATE); r.heartRate = 145;
        r.set(ActivityWriter::RecordData::Field::GPS_PRECISION); r.gpsPrecisionM = 8.0f;
        r.set(ActivityWriter::RecordData::Field::GPS_FIX_AGE);   r.gpsFixAgeMs = 250;
        r.gpsState = GpsState::GPS_SUBSCRIBED | GpsState::GPS_SAMPLE_FRESH;
        w.addRecord(r);
    }

    ActivityWriter::TrackData track;
    track.timestamp = info.timestamp; track.timeStart = info.timestamp;
    w.stop(track);

    testfit::FitReader r(findFitFile(fx.fileSystem));
    ASSERT_TRUE(r.ok()) << "records parse cleanly";
    EXPECT_TRUE(r.crcValid());

    const auto recs = r.withGlobal(fit::mesgNum(fit::MesgNum::Record));
    ASSERT_EQ(recs.size(), 1u);
    EXPECT_EQ(recs[0]->fields.count(0), 0u) << "no position on this variant";
    // Battery and GNSS developer fields coexist in the right order.
    EXPECT_EQ(recs[0]->devFields.at(2).u(), 77u);
    EXPECT_EQ(recs[0]->devFields.at(3).u(), 3950u);
    EXPECT_EQ(recs[0]->fields.at(3).u(), 145u);  // heart_rate
    EXPECT_FLOAT_EQ(asF32(recs[0]->devFields.at(devfield::kGpsPrecision)), 8.0f);
    EXPECT_EQ(recs[0]->devFields.at(devfield::kGpsFixAge).u(), 250u);
    EXPECT_EQ(recs[0]->devFields.at(devfield::kGpsState).u(),
              static_cast<uint64_t>(GpsState::GPS_SUBSCRIBED
                                    | GpsState::GPS_SAMPLE_FRESH));
}

// The .fit is flushed at start, then only when a record crosses a >=30 s
// boundary (and on every lap). Records at +0/+10/+20/+35/+70 s cross the
// boundary twice.
TEST(GpsLabActivityWriter, FlushCadence)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");

    ActivityWriter::AppInfo info;
    info.timestamp = 1000;  // small base keeps the flush deltas obvious
    info.appID     = "running";
    w.start(info);

    const std::string fitPath = findFitPath(fx.fileSystem);
    ASSERT_FALSE(fitPath.empty());
    const size_t afterStart = fx.fileSystem.flushCounts[fitPath];
    EXPECT_EQ(afterStart, 1u) << "start() flushes the .fit once (header + defs)";

    const std::time_t offsets[] = {0, 10, 20, 35, 70};
    for (std::time_t off : offsets) {
        ActivityWriter::RecordData rec;
        rec.timestamp = info.timestamp + off;
        rec.set(ActivityWriter::RecordData::Field::HEART_RATE);
        rec.heartRate = 120;
        w.addRecord(rec);
    }
    EXPECT_EQ(fx.fileSystem.flushCounts[fitPath] - afterStart, 2u)
        << "flush only when crossing the >=30 s boundary (at +35 and +70)";

    const size_t beforeLap = fx.fileSystem.flushCounts[fitPath];
    ActivityWriter::LapData lap;
    lap.timestamp = info.timestamp + 70;
    lap.timeStart = info.timestamp;
    lap.duration  = 70;
    lap.elapsed   = 70;
    w.addLap(lap);
    EXPECT_EQ(fx.fileSystem.flushCounts[fitPath] - beforeLap, 1u) << "each lap flushes";
}

// End-to-end wiring: a crash (no stop()) leaves the marker; a fresh writer's
// recoverInterrupted() finalizes the .fit and clears the marker.
TEST(GpsLabActivityWriter, RecoverInterruptedThroughApp)
{
    SDK::TestSupport::KernelFixture fx;

    {
        ActivityWriter w(fx.kernel, "Activity");
        ActivityWriter::AppInfo info;
        info.timestamp = 1782475200;  // 2026-06-26 12:00 UTC
        info.appID     = "running";
        w.start(info);

        ActivityWriter::RecordData rec;
        rec.timestamp = info.timestamp;
        rec.set(ActivityWriter::RecordData::Field::HEART_RATE);
        rec.heartRate = 120;
        w.addRecord(rec);
        rec.timestamp = info.timestamp + 1;
        w.addRecord(rec);

        ActivityWriter::LapData lap;  // laps flush -> marker covers all records
        lap.timestamp = info.timestamp + 1;
        lap.timeStart = info.timestamp;
        lap.duration  = 1;
        lap.elapsed   = 1;
        w.addLap(lap);
        // Crash: no stop() -> file left unfinished, marker present.
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

    // Second call: no marker left -> nothing to do.
    EXPECT_FALSE(fresh.recoverInterrupted());
}

// #40: stop() returns whether the activity was durably saved.
TEST(GpsLabActivityWriter, StopReturnsTrueOnSuccess)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");

    ActivityWriter::AppInfo info;
    info.timestamp = 1782475200;
    info.appID     = "running";
    w.start(info);

    ActivityWriter::RecordData rec;
    rec.timestamp = info.timestamp;
    rec.set(ActivityWriter::RecordData::Field::HEART_RATE);
    rec.heartRate = 120;
    w.addRecord(rec);

    ActivityWriter::TrackData track;
    track.timestamp = info.timestamp + 1;
    track.timeStart = info.timestamp;
    track.duration  = 1;
    track.elapsed   = 1;
    EXPECT_TRUE(w.stop(track));
    EXPECT_FALSE(fx.fileSystem.exist(kMarkerPath)) << "success clears the marker";
}

TEST(GpsLabActivityWriter, StopReturnsFalseOnWriteFailure)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");

    ActivityWriter::AppInfo info;
    info.timestamp = 1782475200;
    info.appID     = "running";
    w.start(info);

    ActivityWriter::RecordData rec;
    rec.timestamp = info.timestamp;
    rec.set(ActivityWriter::RecordData::Field::HEART_RATE);
    rec.heartRate = 120;
    w.addRecord(rec);

    // Fail every write from here on: the session/activity/finish writes fail.
    fx.fileSystem.failWritesAfterBytes = fx.fileSystem.bytesWritten;

    ActivityWriter::TrackData track;
    track.timestamp = info.timestamp + 1;
    track.timeStart = info.timestamp;
    track.duration  = 1;
    track.elapsed   = 1;
    EXPECT_FALSE(w.stop(track));
    EXPECT_TRUE(fx.fileSystem.exist(kMarkerPath))
        << "on failure the marker survives so the next boot recovers the .fit";
}

// Decoupling: the FIT finishes/flushes/closes durably but the auxiliary .json
// summary cannot be persisted. stop() must still report success (FIT durability
// is the save contract -- the kernel registers the .fit on close), clear the
// marker, and leave a valid CRC-good .fit. A summary-only failure must never
// suppress the activity's registration.
TEST(GpsLabActivityWriter, StopSucceedsWhenOnlySummaryFails)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");

    ActivityWriter::AppInfo info;
    info.timestamp = 1782475200;
    info.appID     = "running";
    w.start(info);

    ActivityWriter::RecordData rec;
    rec.timestamp = info.timestamp;
    rec.set(ActivityWriter::RecordData::Field::HEART_RATE);
    rec.heartRate = 120;
    w.addRecord(rec);

    // Fail ONLY the summary: the .json open() fails, which happens after the
    // .fit has been durably finished, flushed and closed. The FIT writes are
    // untouched.
    fx.fileSystem.failWriteOpenSuffix = ".json";

    ActivityWriter::TrackData track;
    track.timestamp = info.timestamp + 1;
    track.timeStart = info.timestamp;
    track.duration  = 1;
    track.elapsed   = 1;

    EXPECT_TRUE(w.stop(track))
        << "FIT durable -> stop() succeeds despite the summary failure";
    EXPECT_FALSE(fx.fileSystem.exist(kMarkerPath)) << "marker cleared: FIT durably saved";

    // No .json persisted, but the .fit is present and CRC-valid.
    const std::vector<uint8_t> fit = findFitFile(fx.fileSystem);
    ASSERT_FALSE(fit.empty()) << ".fit present on disk";
    testfit::FitReader r(fit);
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.crcValid()) << "recovered .fit CRC is valid";
}
