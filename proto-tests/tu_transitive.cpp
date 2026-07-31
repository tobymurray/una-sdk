// F4 regression: a TU that pulls Logger.h in TRANSITIVELY first, then sets its
// own LOG_MODULE_LEVEL afterwards -- the shape of all 14 simulator main.cpp files
// plus AppCore.cpp and AppMessageCore.cpp.
//
// Under the old `#if` scheme the later #define was silently ignored, because
// Logger.h's include guard meant the #if had already been evaluated with the
// default. Here LOG_MODULE_LEVEL is expanded at the USE site, so it must win.

#include "SDK/UnaLogger/Logger.h"   // stands in for the transitive include

#define LOG_MODULE_PRX      "transitive"
#define LOG_MODULE_LEVEL    LOG_LEVEL_WARNING
#include "SDK/UnaLogger/Logger.h"   // no-op: guard already defined

int trans_moduleCeiling() { return LOG_MODULE_LEVEL; }

// With the ceiling at WARNING, the first two must emit and the last two must not.
void trans_logAllLevels()
{
    LOG_ERROR("transitive error\n");
    LOG_WARNING("transitive warning\n");
    LOG_INFO("transitive info -- MUST NOT APPEAR\n");
    LOG_DEBUG("transitive debug -- MUST NOT APPEAR\n");
}
