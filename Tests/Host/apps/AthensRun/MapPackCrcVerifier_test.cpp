/**
 * @file    MapPackCrcVerifier_test.cpp
 * @brief   Host tests for MapPackCrcVerifier and MapPackTrustMarker (see the
 *          "Background CRC verification for the AthensRun map pack" design
 *          doc). Uses SDK::TestSupport::KernelFixture's InMemoryFileSystem --
 *          the same fixture pattern as apps/Running/SettingsSerializer_test.cpp.
 *
 * MapPackCrcVerifier is deliberately independent of SDK::RawTiles::Container
 * (spec § 10's CRC scope is format-agnostic: every byte from offset 0 up to
 * the trailing 4-byte CRC), so these fixtures are plain byte buffers with a
 * correct (or deliberately wrong) trailing CRC-32 -- not full rawtiles packs.
 */
#include <gtest/gtest.h>

#include <array>
#include <string>

#include "KernelTestDoubles.hpp"
#include "MapPackCrcVerifier.hpp"
#include "MapPackPaths.hpp"
#include "MapPackTrustMarker.hpp"

namespace {

using SDK::TestSupport::KernelFixture;

uint32_t crc32(const std::string& data)
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char b : data) {
        crc = table[(crc ^ b) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

void appendU32LE(std::string& out, uint32_t v)
{
    out.push_back(static_cast<char>(v));
    out.push_back(static_cast<char>(v >> 8));
    out.push_back(static_cast<char>(v >> 16));
    out.push_back(static_cast<char>(v >> 24));
}

const std::string kPackPath   = AthensRun::kMapPackCandidatePaths[0];
const std::string kMarkerPath = kPackPath + AthensRun::kMapPackTrustSuffix;

// A few KB of arbitrary content, correct trailing CRC-32 appended -- enough
// bytes to exercise several step() calls at a small chunk size.
std::string buildValidPack()
{
    std::string body;
    for (int i = 0; i < 4000; ++i) {
        body.push_back(static_cast<char>('A' + (i % 26)));
    }
    appendU32LE(body, crc32(body));
    return body;
}

std::string buildCorruptedPack()
{
    std::string pack = buildValidPack();
    pack[pack.size() - 1] ^= 0xFF; // flip a footer byte -> declared CRC now wrong
    return pack;
}

void runToCompletion(MapPackCrcVerifier& verifier, size_t chunkBytes = 64)
{
    ASSERT_NE(verifier.start(), MapPackCrcVerifier::Status::IoError);
    int guard = 0;
    while (!verifier.done()) {
        verifier.step(chunkBytes);
        ASSERT_LT(++guard, 100000) << "step() never completed -- possible infinite loop";
    }
}

TEST(MapPackCrcVerifier, IdleWhenNoPackExists)
{
    KernelFixture fixture;
    MapPackCrcVerifier verifier(fixture.kernel);
    EXPECT_EQ(verifier.start(), MapPackCrcVerifier::Status::Idle);
    EXPECT_TRUE(verifier.done());
}

TEST(MapPackCrcVerifier, FullCycleWritesGoodMarker)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(kPackPath, buildValidPack());

    MapPackCrcVerifier verifier(fixture.kernel);
    runToCompletion(verifier);

    EXPECT_EQ(verifier.status(), MapPackCrcVerifier::Status::Verified);

    MapPackTrustMarker marker(fixture.kernel, kMarkerPath.c_str());
    uint64_t markedSize = 0;
    uint32_t markedCrc  = 0;
    EXPECT_EQ(marker.read(markedSize, markedCrc), MapPackTrustMarker::Trust::Good);
    EXPECT_EQ(markedSize, buildValidPack().size());
}

TEST(MapPackCrcVerifier, SecondStartSkipsScanWhenMarkerAlreadyGood)
{
    KernelFixture fixture;
    const std::string pack = buildValidPack();
    fixture.fileSystem.seedFile(kPackPath, pack);

    // First pass: establish a Good marker.
    MapPackCrcVerifier first(fixture.kernel);
    runToCompletion(first);
    ASSERT_EQ(first.status(), MapPackCrcVerifier::Status::Verified);

    // Second instance, same fixture/marker, same unchanged pack: start()
    // alone should reach Verified with no step() calls at all.
    MapPackCrcVerifier second(fixture.kernel);
    EXPECT_EQ(second.start(), MapPackCrcVerifier::Status::Verified);
    EXPECT_TRUE(second.done());

    // step() on an already-done verifier is a documented no-op.
    EXPECT_EQ(second.step(), MapPackCrcVerifier::Status::Verified);
}

TEST(MapPackCrcVerifier, CorruptedPackWritesBadMarker)
{
    KernelFixture fixture;
    fixture.fileSystem.seedFile(kPackPath, buildCorruptedPack());

    MapPackCrcVerifier verifier(fixture.kernel);
    runToCompletion(verifier);

    EXPECT_EQ(verifier.status(), MapPackCrcVerifier::Status::Mismatched);

    MapPackTrustMarker marker(fixture.kernel, kMarkerPath.c_str());
    uint64_t markedSize = 0;
    uint32_t markedCrc  = 0;
    EXPECT_EQ(marker.read(markedSize, markedCrc), MapPackTrustMarker::Trust::Bad);
}

TEST(MapPackTrustMarker, RoundTripsGoodAndBad)
{
    KernelFixture fixture;
    MapPackTrustMarker marker(fixture.kernel, "test.trust");

    ASSERT_TRUE(marker.writeGood(12345, 0xDEADBEEFu));
    uint64_t size = 0;
    uint32_t crc  = 0;
    EXPECT_EQ(marker.read(size, crc), MapPackTrustMarker::Trust::Good);
    EXPECT_EQ(size, 12345u);
    EXPECT_EQ(crc, 0xDEADBEEFu);

    ASSERT_TRUE(marker.writeBad(999, 0x11223344u));
    EXPECT_EQ(marker.read(size, crc), MapPackTrustMarker::Trust::Bad);
    EXPECT_EQ(size, 999u);
    EXPECT_EQ(crc, 0x11223344u);
}

TEST(MapPackTrustMarker, ReadOnAbsentPathIsAbsent)
{
    KernelFixture fixture;
    MapPackTrustMarker marker(fixture.kernel, "nonexistent.trust");
    uint64_t size = 0;
    uint32_t crc  = 0;
    EXPECT_EQ(marker.read(size, crc), MapPackTrustMarker::Trust::Absent);
}

TEST(MapPackTrustMarker, ReadOnGarbageOrShortFileIsAbsent)
{
    KernelFixture fixture;

    fixture.fileSystem.seedFile("short.trust", "abc"); // 3 bytes, not 16
    fixture.fileSystem.seedFile("garbage.trust", std::string(16, '\x7F')); // 16 bytes, bad magic

    uint64_t size = 0;
    uint32_t crc  = 0;
    EXPECT_EQ(MapPackTrustMarker(fixture.kernel, "short.trust").read(size, crc),
              MapPackTrustMarker::Trust::Absent);
    EXPECT_EQ(MapPackTrustMarker(fixture.kernel, "garbage.trust").read(size, crc),
              MapPackTrustMarker::Trust::Absent);
}

} // namespace
