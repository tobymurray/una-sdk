#include "SDK/RawTiles/Container.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <openssl/sha.h>
#include <string>
#include <vector>

using namespace SDK::RawTiles;

static std::vector<uint8_t> readFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static std::string sha256hex(const uint8_t *data, size_t len)
{
    unsigned char out[32];
    SHA256(data, len, out);
    static const char hex[] = "0123456789abcdef";
    std::string s(64, '0');
    for (int i = 0; i < 32; ++i) {
        s[i * 2]     = hex[(out[i] >> 4) & 0xF];
        s[i * 2 + 1] = hex[out[i] & 0xF];
    }
    return s;
}

static std::vector<std::string> listRawtiles(const std::string &dir)
{
    std::vector<std::string> out;
    DIR *d = opendir(dir.c_str());
    if (!d) return out;
    struct dirent *e;
    while ((e = readdir(d))) {
        std::string name = e->d_name;
        if (name.size() > 9 && name.substr(name.size() - 9) == ".rawtiles") {
            out.push_back(name);
        }
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <corpus dir>\n", argv[0]); return 2; }
    std::string corpus = argv[1];

    int passed = 0, failed = 0, hashPassed = 0, hashFailed = 0;

    printf("=== golden ===\n");
    for (auto &fname : listRawtiles(corpus + "/golden")) {
        std::string path = corpus + "/golden/" + fname;
        auto bytes = readFile(path);
        Container c;
        OpenResult r = c.openFromMemory(bytes.data(), bytes.size());
        if (r != OpenResult::Ok) {
            printf("  [FAIL] %s -- open: %s\n", fname.c_str(), Container::describeResult(r));
            failed++;
            continue;
        }
        std::string hashPath = path.substr(0, path.size() - 9) + ".hashes";
        std::ifstream hf(hashPath);
        if (!hf) { printf("  [SKIP] %s (no .hashes)\n", fname.c_str()); continue; }
        std::string line;
        int perPass = 0, perFail = 0;
        std::string firstFail;
        while (std::getline(hf, line)) {
            if (line.empty() || line[0] == '#') continue;
            unsigned z, x, y; char hex[80];
            if (sscanf(line.c_str(), "%u %u %u %79s", &z, &x, &y, hex) != 4) continue;
            auto info = c.findTile((uint8_t)z, x, y);
            std::vector<uint8_t> buf(c.decodedTileSize());
            ReadResult rr = info.valid() ? c.readTile(info, buf.data(), buf.size()) : ReadResult::NotFound;
            if (rr != ReadResult::Ok) {
                perFail++;
                if (firstFail.empty()) firstFail = "getTile(" + std::to_string(z) + "," + std::to_string(x) + "," + std::to_string(y) + "): " + Container::describeResult(rr);
                continue;
            }
            std::string got = sha256hex(buf.data(), buf.size());
            if (got != hex) {
                perFail++;
                if (firstFail.empty()) firstFail = "(" + std::to_string(z) + "," + std::to_string(x) + "," + std::to_string(y) + ") hash mismatch got=" + got + " want=" + hex;
                continue;
            }
            perPass++;
        }
        hashPassed += perPass; hashFailed += perFail;
        if (perFail == 0) { printf("  [PASS] %s -- %d tile hashes match\n", fname.c_str(), perPass); passed++; }
        else { printf("  [FAIL] %s -- %d/%d match; %s\n", fname.c_str(), perPass, perPass+perFail, firstFail.c_str()); failed++; }
    }

    printf("\n=== negative ===\n");
    for (auto &fname : listRawtiles(corpus + "/negative")) {
        std::string path = corpus + "/negative/" + fname;
        auto bytes = readFile(path);
        Container c;
        OpenResult r = c.openFromMemory(bytes.data(), bytes.size());
        if (r == OpenResult::Ok) {
            printf("  [FAIL] %s -- WRONGLY ACCEPTED\n", fname.c_str());
            failed++;
        } else {
            printf("  [PASS] %s -- rejected: %s\n", fname.c_str(), Container::describeResult(r));
            passed++;
        }
    }

    printf("\n=== summary ===\nfixtures: passed %d failed %d\ntile hashes: matched %d mismatched %d\n",
           passed, failed, hashPassed, hashFailed);
    return failed == 0 ? 0 : 1;
}
