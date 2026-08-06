/**
 * @file    RawTilesConformance_test.cpp
 * @brief   Runs SDK::RawTiles::Container against the vendored rawtiles v0.6
 *          conformance corpus (spec § 14): 17 golden fixtures (with their
 *          § 14.5 per-tile hash tables) and 79 negative fixtures.
 *
 * Corpus vendored from github.com/tobymurray/rawtiles @ 38d4d26 (spec v0.6)
 * under Tests/Host/rawtiles/corpus/ — see corpus/README.md for the refresh
 * procedure. Vendored rather than fetched in CI: a network fetch of test
 * fixtures is a flake source, and the corpus is 115 small files (~628 KiB).
 */
#include "SDK/RawTiles/Container.hpp"
#include "Sha256.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using SDK::RawTiles::Container;
using SDK::RawTiles::OpenResult;
using SDK::RawTiles::ReadResult;

namespace
{

namespace fs = std::filesystem;

fs::path corpusDir()
{
    return fs::path(RAWTILES_CORPUS_DIR);
}

std::vector<uint8_t> readFile(const fs::path &path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::vector<fs::path> listRawtiles(const fs::path &dir)
{
    std::vector<fs::path> out;
    for (auto &entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".rawtiles") {
            out.push_back(entry.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

struct HashEntry {
    uint8_t     z;
    uint32_t    x;
    uint32_t    y;
    std::string hashHex;
};

std::vector<HashEntry> parseHashes(const fs::path &path)
{
    std::vector<HashEntry> out;
    std::ifstream           f(path);
    std::string             line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        unsigned z, x, y;
        char     hex[80] = { };
        if (std::sscanf(line.c_str(), "%u %u %u %79s", &z, &x, &y, hex) != 4) {
            continue;
        }
        out.push_back(HashEntry { static_cast<uint8_t>(z), x, y, hex });
    }
    return out;
}

/// gtest parameterized-test names must be alnum/underscore only; fixture
/// stems use dashes (e.g. "golden-ancillary-tag").
std::string sanitizeTestName(const std::string &stem)
{
    std::string out = stem;
    for (char &c : out) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = '_';
        }
    }
    return out;
}

/// § 14.6: the one fixture whose rejection is conditional on whether the
/// reader reads tile-blob alignment padding. This Container never does
/// (row-streaming access pattern; see the class doc comment), so accepting
/// it is spec-conformant, not a bug.
bool isAccessPatternConditional(const std::string &stem)
{
    return stem == "neg-33-padding-nonzero";
}

} // namespace

class RawTilesGolden : public ::testing::TestWithParam<fs::path> { };

TEST_P(RawTilesGolden, OpensAndTileHashesMatch)
{
    const fs::path packPath = GetParam();
    const auto     bytes    = readFile(packPath);
    ASSERT_FALSE(bytes.empty()) << "fixture missing or unreadable: " << packPath;

    Container  c;
    OpenResult r = c.openFromMemory(bytes.data(), bytes.size());
    ASSERT_EQ(r, OpenResult::Ok)
        << packPath.filename() << " -- " << Container::describeResult(r);

    fs::path hashPath = packPath;
    hashPath.replace_extension(".hashes");
    ASSERT_TRUE(fs::exists(hashPath)) << "missing sibling .hashes for " << packPath;

    const auto entries = parseHashes(hashPath);
    std::vector<uint8_t> tileBuf(c.decodedTileSize());
    for (auto &e : entries) {
        auto info = c.findTile(e.z, e.x, e.y);
        ASSERT_TRUE(info.valid()) << packPath.filename() << " missing tile ("
                                  << (int) e.z << "," << e.x << "," << e.y << ")";
        ReadResult rr = c.readTile(info, tileBuf.data(), tileBuf.size());
        ASSERT_EQ(rr, ReadResult::Ok) << packPath.filename() << " readTile("
                                      << (int) e.z << "," << e.x << "," << e.y
                                      << "): " << Container::describeResult(rr);
        const std::string got = RawTilesTest::sha256Hex(tileBuf.data(), tileBuf.size());
        EXPECT_EQ(got, e.hashHex) << packPath.filename() << " tile ("
                                  << (int) e.z << "," << e.x << "," << e.y << ")";
    }
    // A pack with tiles but a silently-empty/unparsed .hashes file would
    // otherwise pass vacuously; golden-empty-quadtree (§ 8.6, tile_count=0)
    // is legitimately entry-less and exempt from this check.
    if (c.header().tileCount > 0) {
        EXPECT_GT(entries.size(), 0u) << packPath.filename() << ": empty .hashes file — test proves nothing";
    }
}

INSTANTIATE_TEST_SUITE_P(
    Corpus, RawTilesGolden, ::testing::ValuesIn(listRawtiles(corpusDir() / "golden")),
    [](const ::testing::TestParamInfo<fs::path> &info) {
        return sanitizeTestName(info.param.stem().string());
    });

class RawTilesNegative : public ::testing::TestWithParam<fs::path> { };

TEST_P(RawTilesNegative, RejectedOrAccessPatternExempt)
{
    const fs::path packPath = GetParam();
    const auto     bytes    = readFile(packPath);
    ASSERT_FALSE(bytes.empty()) << "fixture missing or unreadable: " << packPath;

    Container  c;
    OpenResult r = c.openFromMemory(bytes.data(), bytes.size());

    if (isAccessPatternConditional(packPath.stem().string())) {
        EXPECT_EQ(r, OpenResult::Ok)
            << packPath.filename() << " is access-pattern-conditional (spec § 11.2/§ 14.6) "
            << "-- expected Ok since this reader never reads tile-blob padding";
    } else {
        EXPECT_NE(r, OpenResult::Ok)
            << packPath.filename() << " was wrongly accepted";
    }
}

INSTANTIATE_TEST_SUITE_P(
    Corpus, RawTilesNegative, ::testing::ValuesIn(listRawtiles(corpusDir() / "negative")),
    [](const ::testing::TestParamInfo<fs::path> &info) {
        return sanitizeTestName(info.param.stem().string());
    });

// Sanity check on the corpus itself: catches a silently-empty vendored
// corpus directory (e.g. a bad checkout) turning every parameterized test
// above into a vacuous pass.
TEST(RawTilesCorpus, HasExpectedFixtureCounts)
{
    EXPECT_EQ(listRawtiles(corpusDir() / "golden").size(), 17u);
    EXPECT_EQ(listRawtiles(corpusDir() / "negative").size(), 79u);
}
