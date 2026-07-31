// Default-level TU. Also carries the object-graph shape from every simulator
// main(): a comm-like object declared FIRST (destroyed LAST) that logs in its
// destructor, and a kernel-like object declared SECOND. Under the old design
// this aborted with "pure virtual method called".
#define LOG_MODULE_PRX   "tu4"
#include "SDK/UnaLogger/Logger.h"

#include <cstdio>

int tu4_moduleCeiling() { return LOG_MODULE_LEVEL; }

void tu4_logAllLevels()
{
    LOG_ERROR("error %d\n", 1);
    LOG_WARNING("warning %d\n", 2);
    LOG_INFO("info %d\n", 3);
    LOG_DEBUG("debug %d\n", 4);
    const unsigned char data[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33,
                                   0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB,
                                   0xCC };
    LOG_DEBUG_DUMP(data, static_cast<int>(sizeof(data)));
}

// Teardown shape: this destructor logs, and it runs after the kernel-like object
// that used to own the sink is gone.
struct CommLike {
    ~CommLike()
    {
        LOG_WARNING("Clear Service Message 0x%08X\n", 0xDEADBEEFu);
        LOG_INFO("Queues cleared\n");
    }
};

void tu4_teardownShape()
{
    CommLike comm;   // destroyed last, logs on the way out
}
