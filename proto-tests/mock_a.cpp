// Mock::Logger instantiated from a DEFAULT-level TU.
// Paired with mock_b.cpp (LOG_LEVEL=0). If any inline body in Mock/Logger.hpp
// still depended on LOG_LEVEL, these two would compile different definitions of
// the same inline entity and behaviour would depend on link order.
#include "SDK/Simulator/Kernel/Mock/Logger.hpp"
#include "SDK/UnaLogger/Logger.h"

SDK::Interface::ILogger* mockFromA() { return &SDK::Simulator::Mock::logger(); }

void installFromA() { Logger_init(SDK::Simulator::Mock::logger()); }
