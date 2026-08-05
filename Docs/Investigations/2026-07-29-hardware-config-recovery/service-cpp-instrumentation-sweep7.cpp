// #include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp"  // Commented out: HR parser include
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "SDK/Messages/SensorLayerMessages.hpp"

#include "Service.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "SDK/UnaLogger/Logger.h"

Service::Service(SDK::Kernel& kernel)
    : mKernel(SDK::KernelProviderService::GetInstance().getKernel())
    , mGUIStarted(false)
{}

// --- hardware-config register sweep (temporary) ---
// Every line is BOTH logged (in case DEBUG TX works) and appended to a buffer
// that is written to a file on the watch storage (read back over USB) so we do
// not depend on the flaky debug UART.
static char     peekBuf[24576];
static unsigned peekLen = 0;

static void peekEmit(const char* line)
{
    LOG_INFO("%s", line);
    unsigned l = (unsigned)strlen(line);
    if (peekLen + l < sizeof(peekBuf)) { memcpy(peekBuf + peekLen, line, l); peekLen += l; }
}

static void peekLine(const char* fmt, ...)
{
    char line[640]; // must fit the widest formatted line (I2CSCAN can print up to 112 addrs)
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    peekEmit(line);
}

static void peekRange(const char* nm, uint32_t base, unsigned nwords)
{
    const volatile uint32_t* p = reinterpret_cast<const volatile uint32_t*>(static_cast<uintptr_t>(base));
    for (unsigned i = 0; i < nwords; i += 4) {
        char line[96];
        uint32_t a = base + i * 4u;
        snprintf(line, sizeof(line), "SWP %-7s %08lX: %08lX %08lX %08lX %08lX\n",
                 nm, (unsigned long)a,
                 (unsigned long)p[i + 0], (unsigned long)p[i + 1],
                 (unsigned long)p[i + 2], (unsigned long)p[i + 3]);
        peekEmit(line);
    }
}

static inline uint32_t peekRd32(uint32_t addr)
{
    return *reinterpret_cast<const volatile uint32_t*>(static_cast<uintptr_t>(addr));
}
static inline void peekWr32(uint32_t addr, uint32_t v)
{
    *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(addr)) = v;
}

// --- CPU privilege / MPU / SAU: the decisive "is there isolation" test ---
static void peekPrivilege()
{
    uint32_t control, primask, ipsr;
    __asm volatile ("MRS %0, CONTROL" : "=r" (control));
    __asm volatile ("MRS %0, PRIMASK" : "=r" (primask));
    __asm volatile ("MRS %0, IPSR"    : "=r" (ipsr));
    peekLine("SWP CPU CONTROL=%08lX(nPRIV=%u SPSEL=%u FPCA=%u) PRIMASK=%08lX IPSR=%08lX\n",
             (unsigned long)control, (unsigned)(control & 1), (unsigned)((control >> 1) & 1),
             (unsigned)((control >> 3) & 1), (unsigned long)primask, (unsigned long)ipsr);

    uint32_t mpuType = peekRd32(0xE000ED90);
    uint32_t mpuCtrl = peekRd32(0xE000ED94);
    peekLine("SWP MPU MPU_TYPE=%08lX(DREGION=%u) MPU_CTRL=%08lX(ENABLE=%u PRIVDEFENA=%u)\n",
             (unsigned long)mpuType, (unsigned)((mpuType >> 8) & 0xFF),
             (unsigned long)mpuCtrl, (unsigned)(mpuCtrl & 1), (unsigned)((mpuCtrl >> 2) & 1));
    // Walk up to 8 MPU regions regardless of ENABLE - RNR/RBAR/RLAR are readable either way.
    for (unsigned r = 0; r < 8; r++) {
        peekWr32(0xE000ED98, r); // MPU_RNR
        uint32_t rbar = peekRd32(0xE000ED9C);
        uint32_t rlar = peekRd32(0xE000EDA0);
        peekLine("SWP MPU region%u RBAR=%08lX RLAR=%08lX\n", r, (unsigned long)rbar, (unsigned long)rlar);
    }

    uint32_t sauCtrl = peekRd32(0xE000EDD0);
    uint32_t sauType = peekRd32(0xE000EDD4);
    peekLine("SWP SAU SAU_CTRL=%08lX(ENABLE=%u ALLNS=%u) SAU_TYPE=%08lX(SREGION=%u)\n",
             (unsigned long)sauCtrl, (unsigned)(sauCtrl & 1), (unsigned)((sauCtrl >> 1) & 1),
             (unsigned long)sauType, (unsigned)(sauType & 0xFF));
}

// --- write/readback sanity test on our own static SRAM (safe: our own data segment) ---
static volatile uint32_t peekScratch[4];
static void peekWriteTest()
{
    const uint32_t pat[4] = {0xA5A5A5A5u, 0x5A5A5A5Au, 0xDEADBEEFu, 0x00000000u};
    for (unsigned i = 0; i < 4; i++) peekScratch[i] = pat[i];
    bool ok = true;
    for (unsigned i = 0; i < 4; i++) if (peekScratch[i] != pat[i]) ok = false;
    peekLine("SWP WRTEST addr=%08lX result=%s\n", (unsigned long)(uintptr_t)peekScratch, ok ? "PASS" : "FAIL");
}

// --- I2C bus scan: 0x08-0x77, zero-length write probe (NBYTES=0, AUTOEND=1), log ACKs only ---
static void peekI2cScan(const char* nm, uint32_t base)
{
    const uint32_t CR2 = base + 0x04, ISR = base + 0x18, ICR = base + 0x1C;
    unsigned acks = 0;
    char ackList[512]; unsigned ackLen = 0;
    // ICR write-1-to-clear bits: NACKF=4, STOPF=5, BERR=8, ARLO=9, OVR=10.
    const uint32_t ICR_CLEAR_ALL = (1u << 4) | (1u << 5) | (1u << 8) | (1u << 9) | (1u << 10);
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        for (volatile int spin = 0; spin < 50000 && (peekRd32(ISR) & (1u << 15)); spin++) {} // wait !BUSY
        // Clear any leftover W1C flags from the previous address's transaction (incl. a STOPF
        // that hardware may set *after* NACKF on the autoend stop, which would otherwise be
        // mistaken for this address's own ACK - this was sweep #3's false-positive bug).
        peekWr32(ICR, ICR_CLEAR_ALL);
        for (volatile int spin = 0; spin < 2000 && (peekRd32(ISR) & ((1u << 4) | (1u << 5))); spin++) {
            peekWr32(ICR, ICR_CLEAR_ALL);
        }
        peekWr32(CR2, (static_cast<uint32_t>(addr) << 1) | (1u << 13) /*START*/ | (1u << 25) /*AUTOEND*/);
        bool acked = false;
        bool nacked = false;
        for (volatile int spin = 0; spin < 20000 && !acked && !nacked; spin++) {
            uint32_t isr = peekRd32(ISR);
            if (isr & (1u << 4)) { nacked = true; }                                          // NACKF
            if (isr & (1u << 5)) { acked = true; }                                            // STOPF (ack'd)
        }
        // Wait for hardware's autoend STOP to actually land before clearing/moving on, so the
        // next address's pre-clear above isn't racing this transaction's own tail end.
        for (volatile int spin = 0; spin < 20000 && !(peekRd32(ISR) & (1u << 5)); spin++) {}
        peekWr32(ICR, ICR_CLEAR_ALL);
        if (acked) {
            acks++;
            if (ackLen + 6 < sizeof(ackList)) {
                int n = snprintf(ackList + ackLen, sizeof(ackList) - ackLen, "%02X ", addr);
                if (n > 0) ackLen += (unsigned)n;
            }
        }
    }
    ackList[ackLen] = '\0';
    peekLine("SWP I2CSCAN %-7s base=%08lX acks=%u/%u [ %s]\n", nm, (unsigned long)base, acks, 112u, ackList);
}

// --- I2C register read: write reg-pointer (no stop), repeated-start, read N bytes, autoend.
// This is the real device-identification method - an ACK during a scan only proves *something*
// answered, not what it is. NACK on the write phase (device absent, or NACK on repeated-start
// read) is a clean, expected outcome and is not logged as an error. N is 1 or 2: several chips
// we misread as 1-byte turned out to have 16-bit registers (MAX17262), so this version reads the
// raw bytes as-is and lets the caller decide how to interpret them rather than assuming an
// endianness - vendors differ (some are MSB-first, some LSB-first on the wire).
static bool peekI2cReadRegN(uint32_t base, uint8_t addr7, uint8_t reg, uint8_t* outBuf, uint8_t n)
{
    const uint32_t CR2 = base + 0x04, ISR = base + 0x18, ICR = base + 0x1C;
    const uint32_t TXDR = base + 0x28, RXDR = base + 0x24;
    const uint32_t ICR_CLEAR_ALL = (1u << 4) | (1u << 5) | (1u << 8) | (1u << 9) | (1u << 10);

    for (volatile int spin = 0; spin < 50000 && (peekRd32(ISR) & (1u << 15)); spin++) {} // wait !BUSY
    peekWr32(ICR, ICR_CLEAR_ALL);

    // Phase 1: write the register pointer byte. NBYTES=1, no AUTOEND (hold the bus for a
    // repeated start rather than issuing a STOP between the write and the read).
    peekWr32(CR2, (static_cast<uint32_t>(addr7) << 1) | (1u << 16) /*NBYTES=1*/ | (1u << 13) /*START*/);
    bool txis = false, nacked = false;
    for (volatile int spin = 0; spin < 20000 && !txis && !nacked; spin++) {
        uint32_t isr = peekRd32(ISR);
        if (isr & (1u << 4)) nacked = true; // NACKF - device absent or rejected addr phase
        if (isr & (1u << 1)) txis = true;   // TXIS - ready for the register-pointer byte
    }
    if (nacked || !txis) {
        peekWr32(CR2, peekRd32(CR2) | (1u << 14)); // STOP - abort cleanly
        for (volatile int spin = 0; spin < 20000 && !(peekRd32(ISR) & (1u << 5)); spin++) {}
        peekWr32(ICR, ICR_CLEAR_ALL);
        return false;
    }
    peekWr32(TXDR, reg);
    bool tc = false;
    for (volatile int spin = 0; spin < 20000 && !tc; spin++) {
        if (peekRd32(ISR) & (1u << 6)) tc = true; // TC - NBYTES reached, no AUTOEND: bus held
    }
    if (!tc) {
        peekWr32(CR2, peekRd32(CR2) | (1u << 14));
        for (volatile int spin = 0; spin < 20000 && !(peekRd32(ISR) & (1u << 5)); spin++) {}
        peekWr32(ICR, ICR_CLEAR_ALL);
        return false;
    }

    // Phase 2: repeated start, read N bytes, autoend (hardware issues STOP after the last byte).
    peekWr32(CR2, (static_cast<uint32_t>(addr7) << 1) | (static_cast<uint32_t>(n) << 16) | (1u << 10) /*RD_WRN*/
                      | (1u << 13) /*START*/ | (1u << 25) /*AUTOEND*/);
    uint8_t got = 0;
    nacked = false;
    while (got < n && !nacked) {
        bool rxne = false;
        for (volatile int spin = 0; spin < 20000 && !rxne && !nacked; spin++) {
            uint32_t isr = peekRd32(ISR);
            if (isr & (1u << 4)) nacked = true;
            if (isr & (1u << 2)) rxne = true; // RXNE - byte ready in RXDR
        }
        if (rxne) outBuf[got++] = static_cast<uint8_t>(peekRd32(RXDR));
        else break; // timed out waiting for this byte
    }
    for (volatile int spin = 0; spin < 20000 && !(peekRd32(ISR) & (1u << 5)); spin++) {} // wait STOPF
    peekWr32(ICR, ICR_CLEAR_ALL);
    return got == n;
}

static bool peekI2cReadReg(uint32_t base, uint8_t addr7, uint8_t reg, uint8_t& outVal)
{
    return peekI2cReadRegN(base, addr7, reg, &outVal, 1);
}

struct WhoAmICandidate { const char* name; uint8_t addr; uint8_t reg; uint8_t nbytes; const char* note; };
static const WhoAmICandidate kWhoAmICandidates[] = {
    {"LSM6DSO-family",  0x6A, 0x0F, 1, "IMU, expect ~0x6C"},
    {"LSM6DSO-family",  0x6B, 0x0F, 1, "IMU alt addr, expect ~0x6C"},
    {"BMI270",          0x68, 0x00, 1, "IMU CHIP_ID, expect 0x24"},
    {"BMI270-alt",      0x69, 0x00, 1, "IMU CHIP_ID alt addr, expect 0x24"},
    {"ICM-42xxx",       0x68, 0x75, 1, "IMU WHO_AM_I, same addr as BMI270 but different reg"},
    {"BMP3xx",          0x76, 0x00, 1, "Baro CHIP_ID, expect 0x50/0x60"},
    {"BMP3xx-alt",      0x77, 0x00, 1, "Baro CHIP_ID alt addr"},
    {"LPS22-family",    0x5C, 0x0F, 1, "Baro WHO_AM_I, expect 0xB1/0xB3/0xB4"},
    {"LPS22-family-alt",0x5D, 0x0F, 1, "Baro WHO_AM_I alt addr"},
    {"MAX17048/55",     0x36, 0x08, 1, "Fuel gauge VERSION reg (raw, not a fixed constant)"},
    {"DRV2605(L)",      0x5A, 0x00, 1, "Haptic STATUS reg, top 3 bits = DEVICE_ID"},
    {"DRV2605(L)-MODE", 0x5A, 0x01, 1, "MODE reg, expect ~0x40 default - cross-check vs STATUS hit"},
    {"MAX1704x-VERLSB", 0x36, 0x09, 1, "VERSION reg LSB (0x08 was MSB) - cross-check vs sweep #4"},
    {"MAX1704x-SOC",    0x36, 0x04, 1, "SOC reg MSB = battery %, sanity-checkable against watch UI"},
    {"unk-0x40-r00",    0x40, 0x00, 1, "unidentified (I2C2 ACK); INA219/226 current sensor default addr"},
    {"unk-0x40-r0F",    0x40, 0x0F, 1, "unidentified, generic WHO_AM_I-style offset"},
    {"unk-0x61-r00",    0x61, 0x00, 1, "unidentified (I2C1 ACK)"},
    {"unk-0x61-r0F",    0x61, 0x0F, 1, "unidentified (I2C1 ACK), generic WHO_AM_I-style offset"},
    {"unk-0x50-r00",    0x50, 0x00, 1, "ACKed on all 3 buses - compare value across buses"},
    {"unk-0x58-r00",    0x58, 0x00, 1, "ACKed on all 3 buses - compare value across buses"},
    // --- sweep #7: confirm the flash-dump-derived magnetometer guess ---
    {"BMM350",          0x14, 0x00, 1, "Magnetometer CHIP_ID, expect 0x33 - confirms flash-dump string hit"},
    // --- sweep #7: PCA9420's default address (0x61) matches an unidentified I2C1 ACK sitting
    // unexplained since sweep #4 - the flash dump named PCA9420 as the PMIC but never gave us
    // its address, this address just happens to line up.
    {"PCA9420-r00",     0x61, 0x00, 1, "PMIC, generic reg (exact map unconfirmed) - address match only"},
    {"PCA9420-r01",     0x61, 0x01, 1, "PMIC, generic reg (exact map unconfirmed)"},
    // --- sweep #7: MAX17262 and the INA219/226 guess at 0x40 are 16-bit-register chips - redo
    // both properly instead of the misleading 1-byte reads from sweep #4/#5.
    {"MAX17262-VER16",  0x36, 0x08, 2, "VERSION as a proper 16-bit read this time"},
    {"INA226-MANUFID",  0x40, 0xFE, 2, "MANUFACTURER_ID, expect ASCII 'TI' = 0x5449 if this guess is right"},
    // --- sweep #7: does 0x50 look like real, stable memory content (EEPROM) and does 0x58 ever
    // return anything other than 0xFF on any register (real device) or not (artifact)?
    {"unk-0x50-mem00",  0x50, 0x00, 2, "N24S64B candidate - read as memory content, not a chip-ID reg"},
    {"unk-0x58-r01",    0x58, 0x01, 1, "does any register differ from the 0xFF seen at reg 0x00?"},
    {"unk-0x58-r0F",    0x58, 0x0F, 1, "does any register differ from the 0xFF seen at reg 0x00?"},
    // --- sweep #7: MS5837 and PAH8316LS never ACKed on I2C1-4 - try MS5837's fixed address
    // (no address pin, always 0x76) on the newly-added I2C5/I2C6 in case that's where it lives.
    {"MS5837-I2C56",    0x76, 0x00, 1, "Baro, fixed addr - only meaningful on I2C5/I2C6 this round"},
};

static void peekWhoAmI(const char* busName, uint32_t base)
{
    for (const auto& c : kWhoAmICandidates) {
        uint8_t val[2] = {0, 0};
        bool ok = peekI2cReadRegN(base, c.addr, c.reg, val, c.nbytes);
        if (ok) {
            if (c.nbytes == 1) {
                peekLine("SWP WHOAMI %-7s addr=%02X reg=%02X val=%02X cand=%s (%s)\n",
                          busName, c.addr, c.reg, val[0], c.name, c.note);
            } else {
                uint16_t be = (static_cast<uint16_t>(val[0]) << 8) | val[1];
                uint16_t le = (static_cast<uint16_t>(val[1]) << 8) | val[0];
                peekLine("SWP WHOAMI %-7s addr=%02X reg=%02X b0=%02X b1=%02X BE=%04X LE=%04X cand=%s (%s)\n",
                          busName, c.addr, c.reg, val[0], val[1], be, le, c.name, c.note);
            }
        }
    }
}

// Write the accumulated buffer to a named file on the watch (read it back over USB MSC).
static void peekWriteFileNamed(SDK::Kernel& k, const char* name)
{
    auto f = k.fs.file(name);
    if (f && f->open(true, true)) {
        size_t bw = 0;
        f->write(peekBuf, peekLen, bw);
        f->flush();
        f->close();
        LOG_INFO("PEEK: wrote %u/%u bytes to %s\n", (unsigned)bw, (unsigned)peekLen, name);
    } else {
        LOG_INFO("PEEK: file open FAILED (%s)\n", name);
    }
}

static void peekWriteFile(SDK::Kernel& k) { peekWriteFileNamed(k, "peek_sweep7.txt"); }

// --- CRC32 (standard IEEE 802.3 / zlib polynomial, reflected, init/final 0xFFFFFFFF) ---
static inline uint32_t crc32Byte(uint32_t crc, uint8_t b)
{
    crc ^= b;
    for (int i = 0; i < 8; i++) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    return crc;
}
static uint32_t crc32Buf(const uint8_t* data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) crc = crc32Byte(crc, data[i]);
    return crc ^ 0xFFFFFFFFu;
}

// --- Full flash dump: 0x08000000-0x08400000 (4 MB) in 128 KB chunks, one file per chunk,
// named by offset so sorting by filename reassembles in order. Each file is written as several
// smaller sub-writes (not a single 128 KB call) in case the fs write path has an undocumented
// size cap - if it does, this shows up as bw < requested per sub-write rather than silently
// truncating the whole chunk. A manifest (offset/size/CRC32 per chunk) is rewritten after EVERY
// chunk, not just at the end, so an interrupted run (reconnected too early) still leaves a
// readable, honest record of exactly how far it got, and the chunks that did complete are
// individually self-verifiable via their CRC32 without needing the rest of the dump.
static void peekDumpFlash(SDK::Kernel& k)
{
    const uint32_t FLASH_BASE = 0x08000000u;
    const uint32_t FLASH_SIZE = 0x00400000u; // 4 MB
    const uint32_t CHUNK      = 0x00020000u; // 128 KB
    const uint32_t SUBWRITE   = 0x00004000u; // 16 KB per fs->write() call
    const unsigned NCHUNKS    = FLASH_SIZE / CHUNK; // 32

    peekLen = 0;
    peekLine("DUMP base=%08lX size=%08lX chunk=%08lX subwrite=%08lX nchunks=%u\n",
              (unsigned long)FLASH_BASE, (unsigned long)FLASH_SIZE, (unsigned long)CHUNK,
              (unsigned long)SUBWRITE, NCHUNKS);
    peekWriteFileNamed(k, "dump_manifest.txt");

    uint32_t wholeCrc = 0xFFFFFFFFu; // running, unfinalized, chained across all chunks in order

    for (unsigned i = 0; i < NCHUNKS; i++) {
        uint32_t off = i * CHUNK;
        const uint8_t* src = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(FLASH_BASE + off));

        uint32_t chunkCrc = crc32Buf(src, CHUNK);
        for (uint32_t j = 0; j < CHUNK; j++) wholeCrc = crc32Byte(wholeCrc, src[j]);

        char fname[32];
        snprintf(fname, sizeof(fname), "dump_%06lX.bin", (unsigned long)off);
        size_t bwTotal = 0;
        bool openOk = false;
        auto f = k.fs.file(fname);
        if (f && f->open(true, true)) {
            openOk = true;
            for (uint32_t o = 0; o < CHUNK; o += SUBWRITE) {
                uint32_t n = (CHUNK - o < SUBWRITE) ? (CHUNK - o) : SUBWRITE;
                size_t bw = 0;
                f->write(reinterpret_cast<const char*>(src + o), n, bw);
                bwTotal += bw;
            }
            f->flush();
            f->close();
        }
        bool ok = openOk && (bwTotal == CHUNK);
        peekLine("DUMP chunk=%u/%u off=%08lX size=%08lX crc32=%08lX bw=%lu ok=%s\n",
                  i, NCHUNKS, (unsigned long)off, (unsigned long)CHUNK, (unsigned long)chunkCrc,
                  (unsigned long)bwTotal, ok ? "Y" : "N");
        peekWriteFileNamed(k, "dump_manifest.txt"); // update after every chunk, not just at the end
    }

    peekLine("DUMP whole_image_crc32=%08lX\n", (unsigned long)(wholeCrc ^ 0xFFFFFFFFu));

    // Spot-check bytes at fixed offsets, logged in the manifest for a direct host-side compare
    // against the reassembled .bin - this is the "verify by re-reading sample ranges" step.
    auto spot = [](uint32_t addr, unsigned n) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(addr));
        char hex[40]; unsigned hl = 0;
        for (unsigned j = 0; j < n && hl + 2 < sizeof(hex); j++) {
            int w = snprintf(hex + hl, sizeof(hex) - hl, "%02X", p[j]);
            if (w > 0) hl += (unsigned)w;
        }
        peekLine("DUMP spot addr=%08lX bytes=%s\n", (unsigned long)addr, hex);
    };
    spot(FLASH_BASE, 16);
    spot(FLASH_BASE + FLASH_SIZE / 2, 16);
    spot(FLASH_BASE + FLASH_SIZE - 16, 16);
    peekWriteFileNamed(k, "dump_manifest.txt");
}

void Service::run()
{
    LOG_INFO("thread started\n");

    // === HW-CONFIG SWEEP #7 ===
    // Sweep #6's flash dump named several chips via kernel driver strings but didn't tie them to
    // I2C addresses (MS5837, PAH8316LS, PCA9420) or confirm the magnetometer guess (BMM350, found
    // as a single ProdTest string, not a driver class name like the others). This round: adds
    // I2C5/I2C6 (confirmed present on this chip via RM0456, never scanned before - MS5837/PAH8316LS
    // never ACKed on I2C1-4, so this is the most likely place to find them), a BMM350 CHIP_ID probe
    // to upgrade it to double-confirmed like BMI270, a PCA9420 probe at 0x61 (its default address,
    // which happens to match an I2C1 ACK that's been unidentified since sweep #4), proper 16-bit
    // reads for MAX17262 and the INA219/226 candidate at 0x40 (both are 16-bit-register chips -
    // sweep #4/#5's 1-byte reads were very likely reading half a register, which is exactly what
    // made MAX17262 look inconsistent before its real identity was found), and a couple more
    // register offsets at 0x50/0x58 to further test the real-EEPROM-vs-artifact question. The
    // 4 MB flash dump itself is NOT re-run this round - already done and verified in sweep #6.
    peekLen = 0;
    LOG_INFO("SWP === start r7 ===\n");
    peekPrivilege();
    peekRange("DBGMCU", 0xE0044000, 4);    // IDCODE: DEV_ID[11:0] + REV_ID[31:16] for THIS unit
    peekWriteTest();
    peekWriteFile(mKernel);                // persist safe data before the RCC/I2C reads
    peekRange("RCC", 0x46020C00, 64);      // raw dump, decode later against confirmed offsets
    peekWriteFile(mKernel);
    peekI2cScan("I2C1", 0x40005400);
    peekI2cScan("I2C2", 0x40005800);
    peekI2cScan("I2C3", 0x46002800);
    peekI2cScan("I2C4", 0x40008400);
    peekI2cScan("I2C5", 0x40009800);
    peekI2cScan("I2C6", 0x40009C00);
    peekWriteFile(mKernel);
    peekWhoAmI("I2C1", 0x40005400);
    peekWhoAmI("I2C2", 0x40005800);
    peekWhoAmI("I2C3", 0x46002800);
    peekWhoAmI("I2C4", 0x40008400);
    peekWhoAmI("I2C5", 0x40009800);
    peekWhoAmI("I2C6", 0x40009C00);
    peekWriteFile(mKernel);
    LOG_INFO("SWP === done r7 ===\n");
    // === END SWEEP ===

    while (true) {
        SDK::MessageBase *msg;
        if (mKernel.comm.getMessage(msg, 1000)) {
            // Command handling
            switch (msg->getType()) {
                // Kernel messages
                case SDK::MessageType::COMMAND_APP_STOP:
                    LOG_INFO("Force exit from the application\n");
                    // We must release message because this is the last event.
                    mKernel.comm.releaseMessage(msg);
                    return;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                    LOG_INFO("GUI is now running\n");
                    onStartGUI();
                    break;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                    LOG_INFO("GUI has stopped\n");
                    onStopGUI();
                    break;
                default:
                    break;
            }

            // Release message after processing
            mKernel.comm.releaseMessage(msg);
        }
    }

    LOG_INFO("thread stopped\n");
}

void Service::onStartGUI()
{
    LOG_INFO("GUI started\n");
    mGUIStarted = true;
}

void Service::onStopGUI()
{
    LOG_INFO("GUI stopped\n");
    mGUIStarted = false;
}

uint32_t Service::ParseVersion(const char* str)
{
    if (str == nullptr) {
        return 0;
    }

    typedef union {
        struct {
            uint8_t patch;
            uint8_t minor;
            uint8_t major;
        };
        uint32_t u32;
    } FirmwareVersion_t;

    FirmwareVersion_t v{};

    int major, minor, patch;

    if (sscanf(str, "%d.%d.%d", &major, &minor, &patch) == 3) {
        v.major = static_cast<uint8_t>(major);
        v.minor = static_cast<uint8_t>(minor);
        v.patch = static_cast<uint8_t>(patch);
        return v.u32;
    }

    return 0;
}
