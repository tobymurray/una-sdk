// Compiled with -DLOG_LEVEL=0. Under the old design this TU could not compile at
// all once anything called Logger_init/Logger_deinit, and the no-op macros threw
// the arguments away unparsed.
//
// Here it must: compile, emit no reference to SDK::Log::message, and still have
// its format strings checked.
#define LOG_MODULE_PRX   "tu0"
#include "SDK/UnaLogger/Logger.h"

int tu0_moduleCeiling() { return LOG_MODULE_LEVEL; }

void tu0_logAllLevels()
{
    LOG_ERROR("error %d\n", 1);
    LOG_WARNING("warning %d\n", 2);
    LOG_INFO("info %d\n", 3);
    LOG_DEBUG("debug %d\n", 4);
}

// Installation must still be callable from a no-log TU: every simulator main.cpp
// does exactly this.
void tu0_install(SDK::Interface::ILogger& sink)
{
    Logger_init(sink);
}
