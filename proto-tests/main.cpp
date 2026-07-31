// Behavioural driver for the prototype. Exercised at both link orders.
#define LOG_MODULE_PRX   "driver"
#include "SDK/UnaLogger/Logger.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>

void tu4_logAllLevels();
void tu4_teardownShape();
void tu0_logAllLevels();
void tu0_install(SDK::Interface::ILogger& sink);
void trans_logAllLevels();
int  tu4_moduleCeiling();
int  tu0_moduleCeiling();
int  trans_moduleCeiling();

namespace {

int gFailures = 0;

void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "  ok  " : "  FAIL", what);
    if (!ok) {
        ++gFailures;
    }
}

// Counting sink with process lifetime, as installSink() requires.
class CountingSink final : public SDK::Interface::ILogger {
public:
    std::atomic<int> count{0};
    char             last[512]{};

    void printf(const char*, ...) override {}
    void vprintf(const char*, va_list) override {}
    void mvprintf(const char* level, const char* module, const char* func, int line,
                  const char* fmt, va_list args) override
    {
        count.fetch_add(1);
        char body[256];
        vsnprintf(body, sizeof(body), fmt, args);
        snprintf(last, sizeof(last), "[%s/%s/%s:%d] %s",
                 level ? level : "-", module ? module : "-", func ? func : "-", line, body);
        ::printf("        sink: %s", last);
    }
};

CountingSink& sink()
{
    static CountingSink s;   // process lifetime
    return s;
}

} // namespace

int main()
{
    printf("== ceilings seen by each TU ==\n");
    check(tu4_moduleCeiling() == LOG_LEVEL_DEBUG,   "level-4 TU ceiling is DEBUG");
    check(tu0_moduleCeiling() == LOG_LEVEL_DEBUG,   "level-0 TU module ceiling is DEBUG (build ceiling is separate)");
    check(trans_moduleCeiling() == LOG_LEVEL_WARNING, "transitive-include TU ceiling is WARNING (F4)");

    printf("\n== before any sink is installed, messages are counted as dropped ==\n");
    const uint32_t droppedBefore = SDK::Log::droppedCount();
    LOG_ERROR("no sink yet\n");
    check(SDK::Log::droppedCount() == droppedBefore + 1, "drop counter incremented, no crash");

    printf("\n== install and log every level ==\n");
    tu0_install(sink());   // installed FROM the LOG_LEVEL=0 TU
    check(SDK::Log::runtimeLevel() == SDK::Log::kCompileLevel, "runtime level defaults to compile level");

    sink().count = 0;
    tu4_logAllLevels();
    // 4 messages + 2 hexdump rows (17 bytes -> 2 rows)
    check(sink().count == 6, "level-4 TU emitted 4 messages + 2 dump rows");

    printf("\n== the LOG_LEVEL=0 TU emits nothing at run time ==\n");
    sink().count = 0;
    tu0_logAllLevels();
    check(sink().count == 0, "no-log TU emitted nothing");

    printf("\n== per-TU ceiling honoured despite transitive include (F4) ==\n");
    sink().count = 0;
    trans_logAllLevels();
    check(sink().count == 2, "only ERROR and WARNING emitted");

    printf("\n== runtime level control ==\n");
    SDK::Log::setRuntimeLevel(SDK::Log::Level::Error);
    sink().count = 0;
    tu4_logAllLevels();
    check(sink().count == 1, "runtime ceiling ERROR suppressed all but one");
    SDK::Log::setRuntimeLevel(SDK::Log::Level::Debug);

    printf("\n== module prefix comes from the TU, not the header ==\n");
    sink().count = 0;
    LOG_INFO("prefix check\n");
    check(strstr(sink().last, "driver") != nullptr, "prefix is \"driver\"");

    printf("\n== teardown shape: destructor logs after the old sink owner would be gone ==\n");
    sink().count = 0;
    tu4_teardownShape();
    check(sink().count == 2, "teardown diagnostics still delivered, no abort");

    printf("\n== concurrent install while a thread logs (no race, no tear) ==\n");
    std::atomic<bool> stop{false};
    std::thread t([&] {
        while (!stop.load()) {
            LOG_DEBUG("background\n");
        }
    });
    for (int i = 0; i < 200; ++i) {
        SDK::Log::installSink(sink());
    }
    stop = true;
    t.join();
    check(true, "survived concurrent installSink + logging");

    printf("\n== hexdump bounds ==\n");
    unsigned char big[64];
    for (size_t i = 0; i < sizeof(big); ++i) {
        big[i] = static_cast<unsigned char>(i);
    }
    sink().count = 0;
    LOG_DEBUG_DUMP(big, static_cast<int>(sizeof(big)));
    check(sink().count == 4, "64 bytes -> 4 rows of 16");

    printf("\n%s (%d failures)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures);
    return gFailures == 0 ? 0 : 1;
}
