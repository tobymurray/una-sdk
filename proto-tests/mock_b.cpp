// Same header, compiled at LOG_LEVEL=0. See mock_a.cpp.
#include "SDK/Simulator/Kernel/Mock/Logger.hpp"
#include "SDK/UnaLogger/Logger.h"

SDK::Interface::ILogger* mockFromB() { return &SDK::Simulator::Mock::logger(); }

void installFromB() { Logger_init(SDK::Simulator::Mock::logger()); }
