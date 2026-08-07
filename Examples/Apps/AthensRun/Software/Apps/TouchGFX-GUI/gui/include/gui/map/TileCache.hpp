/**
 ******************************************************************************
 * @file    TileCache.hpp
 * @brief   Fixed-slot LRU cache of decoded map tiles for the AthensRun map.
 *
 * SLOTS x 64 KiB of static storage (.bss, so the linker enforces the GUI
 * RAM budget at build time -- and it did: 4 slots overflowed the device
 * RAM region by 160 KB on top of the Running GUI's existing statics, so
 * exactly one slot fits). With one slot a full-viewport redraw re-reads
 * up to the whole 2x2 mosaic at ~6-9 ms per tile (measured): ~30 ms
 * worst case at the 1 Hz fix-driven cadence, acceptable for the PoC and
 * the first datapoint for the cache-sweep experiment (E1) -- more slots
 * need RAM reclaimed from the inherited Running GUI, not this constant.
 *
 * Absent tiles (outside pack coverage) are resolved by findTile() alone
 * (index lookup, no I/O) and never occupy a slot.
 ******************************************************************************
 */

#ifndef ATHENSRUN_TILECACHE_HPP
#define ATHENSRUN_TILECACHE_HPP

#include "SDK/RawTiles/Container.hpp"

#include <cstdint>

namespace AthensRun
{

class TileCache
{
public:
    static constexpr uint32_t SLOTS      = 1;
    static constexpr uint32_t TILE_BYTES = 256 * 256; // ABGR2222, 1 B/px

    /// Returns the decoded tile pixels for (z, x, y), reading through the
    /// cache, or nullptr when the tile is absent from the pack or the
    /// read fails. `container` must be open.
    const uint8_t* get(const SDK::RawTiles::Container& container,
                       uint8_t z, uint32_t x, uint32_t y)
    {
        ++mClock;
        for (Slot& s : mSlots) {
            if (s.used && s.z == z && s.x == x && s.y == y) {
                s.lastUse = mClock;
                return s.pixels;
            }
        }
        const SDK::RawTiles::TileInfo info = container.findTile(z, x, y);
        if (!info.valid()) {
            return nullptr;
        }
        Slot& victim = lru();
        if (container.readTile(info, victim.pixels, TILE_BYTES)
            != SDK::RawTiles::ReadResult::Ok) {
            victim.used = false; // slot content is now garbage
            return nullptr;
        }
        victim.used    = true;
        victim.z       = z;
        victim.x       = x;
        victim.y       = y;
        victim.lastUse = mClock;
        return victim.pixels;
    }

    void clear()
    {
        for (Slot& s : mSlots) {
            s.used = false;
        }
    }

private:
    struct Slot {
        uint8_t  pixels[TILE_BYTES];
        uint32_t x       = 0;
        uint32_t y       = 0;
        uint32_t lastUse = 0;
        uint8_t  z       = 0;
        bool     used    = false;
    };

    Slot& lru()
    {
        Slot* best = &mSlots[0];
        for (Slot& s : mSlots) {
            if (!s.used) {
                return s;
            }
            if (s.lastUse < best->lastUse) {
                best = &s;
            }
        }
        return *best;
    }

    Slot     mSlots[SLOTS];
    uint32_t mClock = 0;
};

} // namespace AthensRun

#endif // ATHENSRUN_TILECACHE_HPP
