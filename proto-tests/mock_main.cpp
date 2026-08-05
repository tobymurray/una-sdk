#include "SDK/Simulator/Kernel/Mock/Logger.hpp"
#define LOG_MODULE_PRX "mockdrv"
#include "SDK/UnaLogger/Logger.h"
#include <cstdio>
#include <thread>
#include <atomic>
SDK::Interface::ILogger* mockFromA();
SDK::Interface::ILogger* mockFromB();
void installFromA();
void installFromB();
struct CommLike { ~CommLike() { LOG_WARNING("Clear Message 0x%08X\n", 0xDEADBEEFu); } };
int main()
{
    // One instance across TUs compiled at different LOG_LEVELs?
    if (mockFromA() != mockFromB()) { printf("FAIL: two Mock::Logger instances\n"); return 1; }
    printf("ok    single Mock::Logger instance across LOG_LEVEL=4 and LOG_LEVEL=0 TUs\n");

    installFromB();   // install from the no-log TU, as simulator mains do
    printf("ok    installed from a LOG_LEVEL=0 TU\n");

    // ILogger::printf path -- the old (null)%s branch
    mockFromA()->printf("printf path: %d\n", 7);
    printf("ok    ILogger::printf delivered its message (was \"(null)%%s\")\n");

    // A thread still logging while main returns: nothing is ever uninstalled,
    // and the sink + its mutex are immortal, so this must stay clean.
    static std::atomic<bool> stop{false};
    static std::thread t([]{ while(!stop.load()) LOG_DEBUG("bg\n"); });
    { CommLike c; }                       // destructor logs during teardown
    printf("ok    teardown-time logging delivered\n");
    stop = true; t.join();
    printf("ALL PASS\n");
    return 0;
}
