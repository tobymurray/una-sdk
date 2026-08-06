/**
 * @file    RawTilesContainer_test.cpp
 * @brief   Unit tests for SDK::RawTiles::Container: the two I/O backends
 *          behind the same validation path, truncation / short-read handling
 *          on the file backend, RGB565 (§ 9.2), and RLE's fail-closed
 *          behaviour at read time (structurally valid, decode unsupported).
 *
 * Packs here are hand-built in code (single-tile Quadtree, WebMercator,
 * zoom 0, tile (0,0,0)) so every test byte is reviewable. The spec repo
 * (github.com/tobymurray/rawtiles) also ships a 96-fixture conformance
 * corpus; this reader is verified against it out of tree, and a
 * corpus-driven suite can be vendored in as a follow-up.
 * buildMinimalPack() computes its own CRC independently of
 * Container's (a from-scratch loop, not a call into the code under test) so
 * this suite doesn't validate the reader's CRC against itself.
 */
#include "SDK/RawTiles/Container.hpp"
#include "support/FakeFileSystem.hpp"

#include <gtest/gtest.h>

#include <cassert>
#include <cstring>
#include <memory>
#include <vector>

using SDK::RawTiles::Compression;
using SDK::RawTiles::Container;
using SDK::RawTiles::OpenResult;
using SDK::RawTiles::PixelFormat;
using SDK::RawTiles::ReadResult;

namespace
{

void putU8(std::vector<uint8_t> &v, uint8_t x) { v.push_back(x); }
void putU16(std::vector<uint8_t> &v, uint16_t x) { putU8(v, x & 0xFF); putU8(v, (x >> 8) & 0xFF); }
void putU32(std::vector<uint8_t> &v, uint32_t x)
{
    putU8(v, x & 0xFF); putU8(v, (x >> 8) & 0xFF);
    putU8(v, (x >> 16) & 0xFF); putU8(v, (x >> 24) & 0xFF);
}
void putU64(std::vector<uint8_t> &v, uint64_t x)
{
    putU32(v, static_cast<uint32_t>(x));
    putU32(v, static_cast<uint32_t>(x >> 32));
}
void putI32(std::vector<uint8_t> &v, int32_t x) { putU32(v, static_cast<uint32_t>(x)); }

uint32_t align4(uint32_t n) { return (n + 3u) & ~3u; }

/// Independent CRC-32/ISO-HDLC (not the reader's), so this test doesn't
/// validate Container's CRC implementation against itself.
uint32_t crc32(const std::vector<uint8_t> &data)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t b : data) {
        crc ^= b;
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

/// A single-tile Quadtree/WebMercator/XYZ pack at (z=0, x=0, y=0), tile_dim
/// 2px, no extension sections. Correct in every field except pixel_format /
/// compression / tileBytes, which the caller chooses.
std::vector<uint8_t> buildMinimalPack(PixelFormat pixelFormat, Compression compression,
                                      const std::vector<uint8_t> &tileBytes)
{
    std::vector<uint8_t> h;
    h.reserve(292);
    h.push_back('R'); h.push_back('A'); h.push_back('W'); h.push_back('T');
    putU8(h, 1); putU8(h, 0);              // format_version_major/minor
    putU16(h, 0);                          // reserved_v1_0
    for (int i = 0; i < 16; ++i) putU8(h, 0xAB);  // pack_uuid (non-zero)
    for (int i = 0; i < 16; ++i) putU8(h, 0);      // supersedes_uuid
    for (int i = 0; i < 16; ++i) putU8(h, 0);      // parent_uuid
    putU8(h, static_cast<uint8_t>(pixelFormat));
    putU8(h, 1);                           // projection = WebMercator
    putU8(h, 1);                           // addressing = Quadtree
    putU8(h, 1);                           // axis = XYZ
    putU16(h, 2);                          // tile_dim_px
    putU8(h, 0); putU8(h, 0);              // zoom_min, zoom_max
    putI32(h, 0); putI32(h, 0); putI32(h, 0); putI32(h, 0); // bbox
    putU64(h, 0);                          // build_timestamp
    putU32(h, 1);                          // tile_count
    putU32(h, 292);                        // index_offset

    const uint32_t tileBlobStart     = align4(292 + 20 * 1); // = 312
    const uint32_t paddedTileLen     = align4(static_cast<uint32_t>(tileBytes.size()));
    const uint32_t extensionsOffset  = tileBlobStart + paddedTileLen;

    // zoom_offsets[24]: only zoom 0 has a tile, at index_offset (292).
    putU32(h, 292); putU32(h, 1);           // zoom_offsets[0] = { offset, count }
    for (int z = 1; z < 24; ++z) { putU32(h, 0); putU32(h, 0); }
    putU32(h, extensionsOffset);            // extensions_offset

    assert(h.size() == 292);

    std::vector<uint8_t> entry;
    putU8(entry, 0);                                        // z
    putU8(entry, static_cast<uint8_t>(compression));         // compression
    putU8(entry, 0); putU8(entry, 0);                        // flags, reserved
    putU32(entry, 0); putU32(entry, 0);                      // x, y
    putU32(entry, tileBlobStart);                            // offset
    putU32(entry, static_cast<uint32_t>(tileBytes.size()));  // length

    std::vector<uint8_t> pack = h;
    pack.insert(pack.end(), entry.begin(), entry.end());
    pack.insert(pack.end(), tileBytes.begin(), tileBytes.end());
    pack.resize(pack.size() + (paddedTileLen - tileBytes.size()), 0); // tile padding
    // No extension sections: extensions_offset already equals file end - 4.
    const uint32_t crc = crc32(pack);
    putU32(pack, crc);
    return pack;
}

/// Minimal IFile double that serves bytes from an in-memory buffer but caps
/// how much of it a read() will actually return, regardless of what's asked
/// for -- simulating a device read that comes back short (`br < btr`) while
/// still reporting success, exactly as IFile::read()'s contract permits for
/// a read that runs past EOF. FakeFileSystem can't express this: its
/// FakeFile snapshots the whole backing string into `mData` at open() time,
/// so reseeding the filesystem after open doesn't affect an already-open
/// handle.
class TruncatingFile : public SDK::Interface::IFile {
public:
    TruncatingFile(std::vector<uint8_t> data, std::shared_ptr<size_t> readableLimit)
        : mData(std::move(data)), mReadableLimit(std::move(readableLimit))
    {
    }

    void setPath(const char *) override { }
    const char *getPath() const override { return "truncating"; }
    bool exist() const override { return true; }
    bool rename(const char *) override { return false; }
    bool remove() override { return false; }

    size_t size() const override { return mData.size(); } // declared size is the *original* size
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
        const size_t limit = *mReadableLimit;
        const size_t avail = mPos < limit ? limit - mPos : 0;
        br = btr < avail ? btr : avail;
        if (br > 0) {
            std::memcpy(buff, mData.data() + mPos, br);
        }
        mPos += br;
        return true; // a short read past the readable limit is still success, per IFile's contract
    }

private:
    std::vector<uint8_t>   mData;
    std::shared_ptr<size_t> mReadableLimit;
    size_t                  mPos = 0;
};

/// @c file() always hands back a fresh TruncatingFile sharing the same
/// mutable readable-limit cell, so shrinking the limit after Container has
/// already open()'d (and cached its own IFile handle) still takes effect --
/// modelling a device file that shrinks out from under an already-open
/// reader, distinct from a file that was already short at open time.
class TruncatingFileSystem : public SDK::Interface::IFileSystem {
public:
    TruncatingFileSystem(std::vector<uint8_t> data, size_t readableLimit)
        : mData(std::move(data)), mReadableLimit(std::make_shared<size_t>(readableLimit))
    {
    }

    void setReadableLimit(size_t n) { *mReadableLimit = n; }

    bool mkdir(const char *) override { return false; }
    std::unique_ptr<SDK::Interface::IFile> file(const char *) override
    {
        return std::make_unique<TruncatingFile>(mData, mReadableLimit);
    }
    std::unique_ptr<SDK::Interface::IDirectory> dir(const char *) override { return nullptr; }
    bool exist(const char *) const override { return true; }
    bool remove(const char *) override { return false; }
    bool rename(const char *, const char *) override { return false; }
    bool copy(const char *, const char *) override { return false; }
    bool objectInfo(const char *, ObjectInfo &) const override { return false; }

private:
    std::vector<uint8_t>   mData;
    std::shared_ptr<size_t> mReadableLimit;
};

} // namespace

TEST(RawTilesContainer, MemoryAndFileBackendsAgree)
{
    std::vector<uint8_t> tile { 0x11, 0x22, 0x33, 0x44 }; // 2x2 ABGR2222
    auto pack = buildMinimalPack(PixelFormat::ABGR2222, Compression::None, tile);

    Container memC;
    ASSERT_EQ(memC.openFromMemory(pack.data(), pack.size()), OpenResult::Ok);

    SDK::Test::FakeFileSystem fs;
    fs.seedFile("p.rawtiles", std::string(reinterpret_cast<char *>(pack.data()), pack.size()));
    Container fileC;
    ASSERT_EQ(fileC.openFromFile(fs, "p.rawtiles"), OpenResult::Ok);

    auto memInfo  = memC.findTile(0, 0, 0);
    auto fileInfo = fileC.findTile(0, 0, 0);
    ASSERT_TRUE(memInfo.valid());
    ASSERT_TRUE(fileInfo.valid());

    std::vector<uint8_t> memBuf(memC.decodedTileSize()), fileBuf(fileC.decodedTileSize());
    ASSERT_EQ(memC.readTile(memInfo, memBuf.data(), memBuf.size()), ReadResult::Ok);
    ASSERT_EQ(fileC.readTile(fileInfo, fileBuf.data(), fileBuf.size()), ReadResult::Ok);
    EXPECT_EQ(memBuf, fileBuf);
    EXPECT_EQ(memBuf, tile);
}

TEST(RawTilesContainer, Rgb565DecodesAsPlainByteCopy)
{
    // compression = None makes RGB565 decode a straight byte-range copy —
    // no quantiser/decoder logic differs from ABGR2222 here (see class doc:
    // only the RLE decoder is deferred, not RGB565 itself).
    std::vector<uint8_t> tile { 0x1F, 0x00, 0xE0, 0x07, 0x00, 0xF8, 0xFF, 0xFF }; // 2x2, 2B/px
    auto pack = buildMinimalPack(PixelFormat::RGB565, Compression::None, tile);

    Container c;
    ASSERT_EQ(c.openFromMemory(pack.data(), pack.size()), OpenResult::Ok);
    EXPECT_EQ(c.decodedTileSize(), 8u); // 2*2*bytesPerPixel(RGB565=2)

    auto info = c.findTile(0, 0, 0);
    ASSERT_TRUE(info.valid());
    std::vector<uint8_t> buf(c.decodedTileSize());
    ASSERT_EQ(c.readTile(info, buf.data(), buf.size()), ReadResult::Ok);
    EXPECT_EQ(buf, tile);
}

TEST(RawTilesContainer, RleAcceptedStructurallyButUnsupportedAtRead)
{
    // RLE (compression = 1) is a legal v0.6 enum value: rejecting the whole
    // pack at open would be a false reject. This reader just can't decode it
    // yet (class doc) -- readTile() must fail closed, not misinterpret the
    // encoded bytes as raw pixels.
    std::vector<uint8_t> encoded { 0x03, 0xAB, 0xCD }; // arbitrary "encoded" bytes
    auto pack = buildMinimalPack(PixelFormat::ABGR2222, Compression::RLE, encoded);

    Container c;
    ASSERT_EQ(c.openFromMemory(pack.data(), pack.size()), OpenResult::Ok)
        << "a structurally-valid RLE entry must not be rejected outright (v0.6 legalises it)";

    auto info = c.findTile(0, 0, 0);
    ASSERT_TRUE(info.valid());
    EXPECT_EQ(info.compression, Compression::RLE);

    std::vector<uint8_t> buf(c.decodedTileSize());
    EXPECT_EQ(c.readTile(info, buf.data(), buf.size()), ReadResult::UnsupportedCompression);
    EXPECT_EQ(c.readTileRows(info, 0, 1, buf.data(), buf.size()), ReadResult::UnsupportedCompression);
}

TEST(RawTilesContainer, TruncatedFileFailsClosedNotSilently)
{
    std::vector<uint8_t> tile { 0x11, 0x22, 0x33, 0x44 };
    auto pack = buildMinimalPack(PixelFormat::ABGR2222, Compression::None, tile);

    // Truncate mid-tile-blob: header + index parse fine, but the tile bytes
    // and CRC footer are gone. Open must fail (CRC / bounds), not succeed
    // with a Container that then serves garbage.
    std::vector<uint8_t> truncated(pack.begin(), pack.begin() + 300);
    Container c;
    OpenResult r = c.openFromMemory(truncated.data(), truncated.size());
    EXPECT_NE(r, OpenResult::Ok);
}

TEST(RawTilesContainer, FileBackendShortReadDuringReadTileFailsClosed)
{
    // A device read that returns fewer bytes than requested (IFile::read()
    // may legitimately do this for a read that runs past what's actually
    // available, per its contract) must not hand back a partially-filled
    // buffer as if it were valid.
    std::vector<uint8_t> tile { 0x11, 0x22, 0x33, 0x44 };
    auto pack = buildMinimalPack(PixelFormat::ABGR2222, Compression::None, tile);

    // Fully readable at open time (eager CRC verification scans the whole
    // file, so a file already-short-at-open is caught there, not here) --
    // then shrink the readable window afterward, simulating the file
    // shrinking out from under an already-open Container, and confirm the
    // later readTile() fails closed instead of returning a partial buffer.
    TruncatingFileSystem fs(pack, /*readableLimit=*/pack.size());
    Container             c;
    ASSERT_EQ(c.openFromFile(fs, "p.rawtiles"), OpenResult::Ok);
    auto info = c.findTile(0, 0, 0);
    ASSERT_TRUE(info.valid());

    fs.setReadableLimit(300); // shorter than the tile blob's end
    std::vector<uint8_t> buf(c.decodedTileSize(), 0xEE);
    EXPECT_EQ(c.readTile(info, buf.data(), buf.size()), ReadResult::IoError);
}

TEST(RawTilesContainer, ReadTileRowsMatchesFullTileSlice)
{
    const int dim = 2;
    std::vector<uint8_t> tile { 0xAA, 0xBB, 0xCC, 0xDD }; // 2 rows x 2 px x 1B
    auto pack = buildMinimalPack(PixelFormat::ABGR2222, Compression::None, tile);
    Container c;
    ASSERT_EQ(c.openFromMemory(pack.data(), pack.size()), OpenResult::Ok);
    auto info = c.findTile(0, 0, 0);

    std::vector<uint8_t> row1(dim);
    ASSERT_EQ(c.readTileRows(info, 1, 1, row1.data(), row1.size()), ReadResult::Ok);
    EXPECT_EQ(row1[0], 0xCC);
    EXPECT_EQ(row1[1], 0xDD);

    std::vector<uint8_t> tooFar(dim);
    EXPECT_EQ(c.readTileRows(info, 2, 1, tooFar.data(), tooFar.size()), ReadResult::RowOutOfRange);
}

TEST(RawTilesContainer, MissingFileIsFileNotFound)
{
    SDK::Test::FakeFileSystem fs;
    Container                 c;
    EXPECT_EQ(c.openFromFile(fs, "nope.rawtiles"), OpenResult::FileNotFound);
}

TEST(RawTilesContainer, CloseResetsIsOpen)
{
    std::vector<uint8_t> tile { 0x11, 0x22, 0x33, 0x44 };
    auto pack = buildMinimalPack(PixelFormat::ABGR2222, Compression::None, tile);
    Container c;
    ASSERT_EQ(c.openFromMemory(pack.data(), pack.size()), OpenResult::Ok);
    EXPECT_TRUE(c.isOpen());
    c.close();
    EXPECT_FALSE(c.isOpen());
    EXPECT_FALSE(c.findTile(0, 0, 0).valid());
}

TEST(RawTilesContainer, MoveTransfersOwnershipOfFileHandle)
{
    std::vector<uint8_t> tile { 0x11, 0x22, 0x33, 0x44 };
    auto pack = buildMinimalPack(PixelFormat::ABGR2222, Compression::None, tile);
    SDK::Test::FakeFileSystem fs;
    fs.seedFile("p.rawtiles", std::string(reinterpret_cast<char *>(pack.data()), pack.size()));

    Container a;
    ASSERT_EQ(a.openFromFile(fs, "p.rawtiles"), OpenResult::Ok);
    Container b(std::move(a));
    EXPECT_TRUE(b.isOpen());
    auto info = b.findTile(0, 0, 0);
    std::vector<uint8_t> buf(b.decodedTileSize());
    EXPECT_EQ(b.readTile(info, buf.data(), buf.size()), ReadResult::Ok);
}
