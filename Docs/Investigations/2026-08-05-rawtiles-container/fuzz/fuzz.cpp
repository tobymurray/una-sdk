// Adversarial mutation fuzz driver for SDK::RawTiles::Container.
//
// Loads every corpus fixture as a seed, then runs two phases per seed:
//   1. Targeted structural mutations: truncate at every byte boundary up to
//      a cap, flip every bit in the first N header/index bytes, corrupt key
//      u32 fields (index_offset, extensions_offset, tile_count, offsets).
//   2. Random mutations: N random single/multi-byte flips + random
//      truncations, RNG-seeded for reproducibility.
//
// Every accepted mutant additionally drives BOTH backends: openFromMemory
// on the mutant bytes directly, and openFromFile via a TruncatingFileSystem
// double that also mid-flight shrinks the "readable window" between
// findTile() and readTile()/readTileRows() -- the file-backend-specific
// attack surface (short reads, truncation mid-readTileRows) that the old
// whole-pack-resident reader could never hit because it had no I/O layer to
// attack.
//
// Build: g++ -std=c++17 -O1 -g -fsanitize=address,undefined
//        -fno-sanitize-recover=all -I <una-sdk>/Libs/Header fuzz.cpp
//        <una-sdk>/Libs/Source/RawTiles/Container.cpp -o fuzz
// Run:   ./fuzz <corpus-dir> [iterations]
#include "SDK/RawTiles/Container.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <vector>

using namespace SDK::RawTiles;
namespace fs = std::filesystem;

namespace
{

std::vector<uint8_t> readFile(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// --- File backend attack surface -------------------------------------------

class TruncatingFile : public SDK::Interface::IFile {
public:
    TruncatingFile(std::vector<uint8_t> data, std::shared_ptr<size_t> limit)
        : mData(std::move(data)), mLimit(std::move(limit))
    {
    }
    void setPath(const char *) override { }
    const char *getPath() const override { return "fuzz"; }
    bool exist() const override { return true; }
    bool rename(const char *) override { return false; }
    bool remove() override { return false; }
    size_t size() const override { return mData.size(); }
    bool open(bool, bool) override { return true; }
    bool isOpen() const override { return true; }
    bool close() override { return true; }
    bool write(const char *, size_t, size_t &) override { return false; }
    bool truncate(size_t) override { return false; }
    bool flush() override { return true; }
    size_t getPosition() const override { return mPos; }
    bool seek(size_t offset) override { mPos = offset; return true; }
    bool read(char *buff, size_t btr, size_t &br) override
    {
        const size_t limit = *mLimit;
        const size_t avail = mPos < limit ? limit - mPos : 0;
        br = btr < avail ? btr : avail;
        if (br > 0) std::memcpy(buff, mData.data() + mPos, br);
        mPos += br;
        return true;
    }

private:
    std::vector<uint8_t>    mData;
    std::shared_ptr<size_t> mLimit;
    size_t                  mPos = 0;
};

class TruncatingFileSystem : public SDK::Interface::IFileSystem {
public:
    TruncatingFileSystem(std::vector<uint8_t> data, size_t limit)
        : mData(std::move(data)), mLimit(std::make_shared<size_t>(limit))
    {
    }
    void setLimit(size_t n) { *mLimit = n; }
    bool mkdir(const char *) override { return false; }
    std::unique_ptr<SDK::Interface::IFile> file(const char *) override
    {
        return std::make_unique<TruncatingFile>(mData, mLimit);
    }
    std::unique_ptr<SDK::Interface::IDirectory> dir(const char *) override { return nullptr; }
    bool exist(const char *) const override { return true; }
    bool remove(const char *) override { return false; }
    bool rename(const char *, const char *) override { return false; }
    bool copy(const char *, const char *) override { return false; }
    bool objectInfo(const char *, ObjectInfo &) const override { return false; }

private:
    std::vector<uint8_t>    mData;
    std::shared_ptr<size_t> mLimit;
};

// Exercises a mutant through both backends, including the file-backend
// truncation-after-open / short-read-mid-readTileRows attack surface.
void driveMutant(const std::vector<uint8_t> &bytes, std::mt19937 &rng)
{
    {
        Container c;
        OpenResult r = c.openFromMemory(bytes.data(), bytes.size());
        if (r == OpenResult::Ok) {
            for (uint32_t i = 0; i < std::min<uint32_t>(c.header().tileCount, 8); ++i) {
                auto info = c.tileAtIndex(i);
                if (!info.valid()) continue;
                std::vector<uint8_t> buf(std::max<size_t>(c.decodedTileSize(), 1));
                c.readTile(info, buf.data(), buf.size());
                if (c.header().tileDimPx > 0) {
                    c.readTileRows(info, 0, 1, buf.data(), buf.size());
                }
            }
            // A handful of out-of-range / boundary lookups.
            c.findTile(0, 0, 0);
            c.findTile(23, 0xFFFFFFFFu, 0xFFFFFFFFu);
            c.findTile(255, 0, 0); // z >= 24: must not index past zoom_offsets
        }
    }
    {
        // File backend: fully readable at open (so structural validation
        // runs the same as memory), then shrink the readable window to a
        // random cut point before touching any found tile -- the
        // short-read / truncation-mid-readTileRows surface this reader's
        // I/O layer introduces that a whole-pack-resident reader never had.
        auto fs = std::make_shared<TruncatingFileSystem>(bytes, bytes.size());
        Container c;
        OpenResult r = c.openFromFile(*fs, "fuzz.rawtiles");
        if (r == OpenResult::Ok) {
            std::uniform_int_distribution<size_t> cutDist(0, bytes.size());
            for (uint32_t i = 0; i < std::min<uint32_t>(c.header().tileCount, 4); ++i) {
                auto info = c.tileAtIndex(i);
                if (!info.valid()) continue;
                fs->setLimit(cutDist(rng)); // may or may not still cover this tile
                std::vector<uint8_t> buf(std::max<size_t>(c.decodedTileSize(), 1));
                c.readTile(info, buf.data(), buf.size());
                if (c.header().tileDimPx > 1) {
                    c.readTileRows(info, c.header().tileDimPx / 2, 1, buf.data(), buf.size());
                }
                fs->setLimit(bytes.size()); // restore for the next entry
            }
        }
    }
}

uint32_t readU32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
void writeU32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <corpus-dir> [random-iterations]\n", argv[0]);
        return 2;
    }
    const size_t randomIterations = argc > 2 ? static_cast<size_t>(std::stoul(argv[2])) : 2000;

    std::vector<std::vector<uint8_t>> seeds;
    for (auto &sub : { "golden", "negative" }) {
        for (auto &entry : fs::directory_iterator(fs::path(argv[1]) / sub)) {
            if (entry.path().extension() == ".rawtiles") {
                seeds.push_back(readFile(entry.path()));
            }
        }
    }
    printf("loaded %zu seeds\n", seeds.size());

    std::mt19937 rng(0xC0FFEE);
    size_t targeted = 0, random_ = 0;

    for (auto &seed : seeds) {
        // --- targeted: truncate at every boundary up to 512 bytes, then every
        // 64th byte beyond that (full byte-by-byte on 787 KB stanley-scale
        // packs would dominate runtime for no extra coverage past the header
        // + index + first few extension bytes).
        for (size_t cut = 0; cut <= seed.size(); ++cut) {
            if (cut > 512 && (cut % 64) != 0 && cut != seed.size()) continue;
            driveMutant(std::vector<uint8_t>(seed.begin(), seed.begin() + cut), rng);
            ++targeted;
        }
        // --- targeted: flip every bit in the first 300 bytes (header +
        // first index entry) individually.
        for (size_t byteIdx = 0; byteIdx < std::min<size_t>(seed.size(), 300); ++byteIdx) {
            for (int bit = 0; bit < 8; ++bit) {
                auto mutant = seed;
                mutant[byteIdx] ^= static_cast<uint8_t>(1u << bit);
                driveMutant(mutant, rng);
                ++targeted;
            }
        }
        // --- targeted: corrupt key u32 fields directly with boundary values.
        static const uint32_t kNasties[] = { 0, 1, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu };
        static const size_t   kFieldOffsets[] = { 88 /*tile_count*/, 92 /*index_offset*/,
                                                   288 /*extensions_offset*/ };
        for (size_t fieldOff : kFieldOffsets) {
            if (fieldOff + 4 > seed.size()) continue;
            for (uint32_t nasty : kNasties) {
                auto mutant = seed;
                writeU32(mutant.data() + fieldOff, nasty);
                driveMutant(mutant, rng);
                ++targeted;
            }
        }
    }
    printf("targeted mutations: %zu\n", targeted);

    // --- random: pick a seed, apply 1-4 random byte flips and/or a random
    // truncation.
    std::uniform_int_distribution<size_t> seedDist(0, seeds.size() - 1);
    std::uniform_int_distribution<int>    flipCountDist(0, 4);
    std::bernoulli_distribution           truncateAlso(0.3);

    for (size_t i = 0; i < randomIterations; ++i) {
        auto mutant = seeds[seedDist(rng)];
        if (mutant.empty()) continue;
        std::uniform_int_distribution<size_t> byteDist(0, mutant.size() - 1);
        int flips = flipCountDist(rng);
        for (int f = 0; f < flips; ++f) {
            mutant[byteDist(rng)] = static_cast<uint8_t>(rng() & 0xFF);
        }
        if (truncateAlso(rng)) {
            std::uniform_int_distribution<size_t> cutDist(0, mutant.size());
            mutant.resize(cutDist(rng));
        }
        driveMutant(mutant, rng);
        ++random_;
    }
    printf("random mutations: %zu\n", random_);
    printf("TOTAL mutations driven through both backends: %zu\n", targeted + random_);
    printf("no crash / ASan / UBSan report => survived to this line cleanly\n");
    return 0;
}
