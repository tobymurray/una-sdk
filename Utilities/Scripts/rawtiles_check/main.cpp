// Host-side driver for SDK::RawTiles::Container. See Makefile header for usage.
//
// This is a dev artifact, not shipped SDK code: it exists to validate the
// reader against rawtiles spec fixtures (golden, negative, and arbitrary
// slippypack output like /tmp/stanley.rawtiles) before integrating into the
// simulator.

#include "SDK/RawTiles/Container.hpp"

#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4). Public-domain reference implementation, just enough
// to verify § 14.5 per-tile hash tables. Inlined here so the dev driver has
// zero external dependencies.

struct Sha256 {
    uint32_t state[8];
    uint64_t bitLen;
    uint8_t  buf[64];
    uint32_t bufLen;

    void init() {
        state[0] = 0x6a09e667u; state[1] = 0xbb67ae85u;
        state[2] = 0x3c6ef372u; state[3] = 0xa54ff53au;
        state[4] = 0x510e527fu; state[5] = 0x9b05688cu;
        state[6] = 0x1f83d9abu; state[7] = 0x5be0cd19u;
        bitLen = 0;
        bufLen = 0;
    }

    static uint32_t rotr(uint32_t v, uint32_t n) { return (v >> n) | (v << (32 - n)); }

    void compress(const uint8_t *block) {
        static const uint32_t K[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u,
        };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(block[i*4]) << 24) | (uint32_t(block[i*4+1]) << 16)
                 | (uint32_t(block[i*4+2]) << 8) | uint32_t(block[i*4+3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = h + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    void update(const uint8_t *data, std::size_t len) {
        for (std::size_t i = 0; i < len; ++i) {
            buf[bufLen++] = data[i];
            if (bufLen == 64) {
                compress(buf);
                bitLen += 512;
                bufLen = 0;
            }
        }
    }

    std::array<uint8_t, 32> finish() {
        bitLen += uint64_t(bufLen) * 8;
        buf[bufLen++] = 0x80;
        if (bufLen > 56) {
            while (bufLen < 64) buf[bufLen++] = 0;
            compress(buf);
            bufLen = 0;
        }
        while (bufLen < 56) buf[bufLen++] = 0;
        for (int i = 7; i >= 0; --i) {
            buf[bufLen++] = uint8_t((bitLen >> (i * 8)) & 0xFF);
        }
        compress(buf);
        std::array<uint8_t, 32> out;
        for (std::size_t i = 0; i < 8; ++i) {
            out[i*4]   = uint8_t((state[i] >> 24) & 0xFF);
            out[i*4+1] = uint8_t((state[i] >> 16) & 0xFF);
            out[i*4+2] = uint8_t((state[i] >> 8) & 0xFF);
            out[i*4+3] = uint8_t(state[i] & 0xFF);
        }
        return out;
    }
};

std::string toHex(const std::array<uint8_t, 32> &bytes)
{
    static const char *digits = "0123456789abcdef";
    std::string out;
    out.resize(64);
    for (std::size_t i = 0; i < 32; ++i) {
        out[i*2]   = digits[(bytes[i] >> 4) & 0xFu];
        out[i*2+1] = digits[bytes[i] & 0xFu];
    }
    return out;
}

// ---------------------------------------------------------------------------

const char* projectionName(SDK::RawTiles::Projection p)
{
    switch (p) {
        case SDK::RawTiles::Projection::WebMercator: return "WebMercator";
        case SDK::RawTiles::Projection::LocalLinear: return "LocalLinear";
    }
    return "?";
}

const char* addressingName(SDK::RawTiles::Addressing a)
{
    switch (a) {
        case SDK::RawTiles::Addressing::Quadtree:    return "Quadtree";
        case SDK::RawTiles::Addressing::SingleImage: return "SingleImage";
    }
    return "?";
}

const char* axisName(SDK::RawTiles::Axis a)
{
    switch (a) {
        case SDK::RawTiles::Axis::XYZ: return "XYZ";
        case SDK::RawTiles::Axis::TMS: return "TMS";
    }
    return "?";
}

void printHeader(const SDK::RawTiles::Header &h)
{
    std::printf("rawtiles v%u.%u\n", h.formatMajor, h.formatMinor);
    std::printf("  pack_uuid          ");
    for (int i = 0; i < 16; ++i) {
        std::printf("%02x", h.packUuid[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9) std::printf("-");
    }
    std::printf("\n");
    std::printf("  pixel_format       ABGR2222\n");
    std::printf("  projection         %s\n", projectionName(h.projection));
    std::printf("  addressing         %s\n", addressingName(h.addressing));
    std::printf("  axis               %s\n", axisName(h.axis));
    std::printf("  tile_dim_px        %u\n", h.tileDimPx);
    std::printf("  zoom_range         [%u, %u]\n", h.zoomMin, h.zoomMax);
    std::printf("  bbox (µ°)          lon [%d, %d]  lat [%d, %d]\n",
            h.bboxMinLonUDeg, h.bboxMaxLonUDeg, h.bboxMinLatUDeg, h.bboxMaxLatUDeg);
    std::printf("  bbox (deg)         lon [%.6f, %.6f]  lat [%.6f, %.6f]\n",
            h.bboxMinLonUDeg / 1e6, h.bboxMaxLonUDeg / 1e6,
            h.bboxMinLatUDeg / 1e6, h.bboxMaxLatUDeg / 1e6);
    std::printf("  build_timestamp    %" PRIu64 "\n", h.buildTimestamp);
    std::printf("  tile_count         %u\n", h.tileCount);
    std::printf("  extensions_offset  %u\n", h.extensionsOffset);
    std::printf("  zoom_offsets:\n");
    for (int z = 0; z < 24; ++z) {
        if (h.zoomOffsets[z].count > 0) {
            std::printf("    z=%-2d  offset=%-8u  count=%u\n",
                    z, h.zoomOffsets[z].offset, h.zoomOffsets[z].count);
        }
    }
}

int runHashCheck(SDK::RawTiles::Container &c, const char *hashesPath)
{
    std::ifstream in(hashesPath);
    if (!in.is_open()) {
        std::fprintf(stderr, "cannot open hashes file: %s\n", hashesPath);
        return 1;
    }

    int total   = 0;
    int matched = 0;
    int missing = 0;
    int badHash = 0;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        unsigned z, x, y;
        std::string expectedHex;
        if (!(ss >> z >> x >> y >> expectedHex)) continue;
        ++total;

        auto tile = c.getTile(uint8_t(z), uint32_t(x), uint32_t(y));
        if (!tile.valid()) {
            std::printf("  MISS  %u %u %u  (tile not found)\n", z, x, y);
            ++missing;
            continue;
        }
        Sha256 h;
        h.init();
        h.update(tile.data, tile.length);
        std::string actualHex = toHex(h.finish());
        if (actualHex == expectedHex) {
            ++matched;
        } else {
            std::printf("  FAIL  %u %u %u\n    expected %s\n    actual   %s\n",
                    z, x, y, expectedHex.c_str(), actualHex.c_str());
            ++badHash;
        }
    }

    std::printf("§ 14.5 reader conformance: %d/%d tiles matched", matched, total);
    if (missing) std::printf(", %d missing", missing);
    if (badHash) std::printf(", %d hash mismatch", badHash);
    std::printf("\n");

    return (matched == total) ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "usage: %s <pack.rawtiles> [pack.hashes]\n", argv[0]);
        return 2;
    }

    SDK::RawTiles::Container c;
    auto r = c.openFromFile(argv[1]);
    std::printf("opening %s -> %s\n", argv[1], SDK::RawTiles::Container::describeResult(r));
    if (r != SDK::RawTiles::OpenResult::Ok) {
        return 1;
    }
    printHeader(c.header());

    if (argc == 3) {
        std::printf("\nverifying against %s\n", argv[2]);
        return runHashCheck(c, argv[2]);
    }
    return 0;
}
