#!/bin/zsh
#
# Verification harness for the logger redesign. Self-contained: needs only
# clang++ with C++17 support -- no TouchGFX/SDL2 tree, no network.
#
#   zsh proto-tests/run.sh
#
# Each check corresponds to a property the previous design could not hold
# simultaneously. If one fails, the design claim behind it is wrong.
#
setopt shwordsplit
set -u

P=${0:A:h}                 # this script's directory
W=${P:h}                   # repository root
B=$P/build
mkdir -p $B

CXX="clang++ -std=c++17 -Wall -Wextra -Wformat -Wformat-extra-args"
INC="-I$W/Libs/Header -I$P/stub"
LOGGER=$W/Libs/Source/UnaLogger/Logger.cpp

rc=0
fail() { print "  FAIL: $1"; rc=1 }

print "===== 1. compile at LOG_LEVEL=4, LOG_LEVEL=0, and via a transitive include ====="
$CXX -g $INC -c $P/tu_level4.cpp -o $B/tu4.o               || fail "tu_level4"
$CXX -g $INC -DLOG_LEVEL=0 -c $P/tu_level0.cpp -o $B/tu0.o || fail "tu_level0"
$CXX -g $INC -c $P/tu_transitive.cpp -o $B/trans.o         || fail "tu_transitive"
$CXX -g $INC -c $P/main.cpp -o $B/main.o                   || fail "main"
$CXX -g $INC -c $LOGGER -o $B/logger.o                     || fail "Logger.cpp"
[[ $rc -eq 0 ]] && print "  ok"

print "\n===== 2. the logger's own implementation builds at -DLOG_LEVEL=0 ====="
# Previously a hard error: the #else branch #define'd Logger_message as a macro,
# which then clobbered Logger.cpp's own definition of that same function.
if $CXX -fsyntax-only $INC -DLOG_LEVEL=0 $LOGGER; then
    print "  ok"
else
    fail "Logger.cpp does not build at LOG_LEVEL=0"
fi

print "\n===== 3. zero code emitted for a filtered-out call ====="
for opt in -O0 -Os; do
    $CXX $opt $INC -DLOG_LEVEL=0 -c $P/tu_level0.cpp -o $B/z0.o
    n=$(nm -u $B/z0.o | grep -c "SDK3Log7message") || n=0
    $CXX $opt $INC -c $P/tu_level4.cpp -o $B/z4.o
    m=$(nm -u $B/z4.o | grep -c "SDK3Log7message") || m=0
    print "  $opt: refs to SDK::Log::message -- LOG_LEVEL=0 -> $n (want 0), LOG_LEVEL=4 -> $m (want >0)"
    [[ $n -eq 0 ]] || fail "code emitted at $opt with logging disabled"
    [[ $m -gt 0 ]] || fail "no call emitted at $opt with logging enabled"
done

print "\n===== 4. format strings are checked EVEN WHEN the call is compiled out ====="
# A property `#define LOG(...) do {} while (0)` cannot have: it discarded the
# arguments unparsed, so a format bug in a disabled call was invisible.
cat > $B/badfmt.cpp <<'EOF'
#include "SDK/UnaLogger/Logger.h"
#include <cstddef>
void f(size_t n, const char* s)
{
    LOG_ERROR("size %d\n", n);      // %d against size_t
    LOG_INFO("two %s %s\n", s);     // missing argument
    LOG_DEBUG("none %s\n", 42);     // %s against int
}
EOF
for lvl in 4 0; do
    c=$($CXX -fsyntax-only $INC -DLOG_LEVEL=$lvl $B/badfmt.cpp 2>&1 | grep -c "warning:.*format") || c=0
    print "  LOG_LEVEL=$lvl: $c format diagnostics (want 3)"
    [[ $c -ge 3 ]] || fail "format checking lost at LOG_LEVEL=$lvl"
done

print "\n===== 5. behaviour is identical at both link orders ====="
clang++ -std=c++17 -g $B/tu0.o $B/tu4.o $B/trans.o $B/main.o $B/logger.o -o $B/proto_A || fail "link A"
clang++ -std=c++17 -g $B/tu4.o $B/trans.o $B/main.o $B/tu0.o $B/logger.o -o $B/proto_B || fail "link B"
for v in A B; do
    print "  --- link order $v ---"
    $B/proto_$v 2>&1 | grep -E "ok |FAIL|ALL PASS|FAILURES" | sed 's/^/  /' || true
    $B/proto_$v > /dev/null 2>&1 || fail "behavioural tests failed in order $v"
done

print "\n===== 6. simulator sink: one immortal instance, identical codegen ====="
$CXX -g -w $INC -c $P/mock_a.cpp -o $B/ma.o               || fail "mock_a"
$CXX -g -w $INC -DLOG_LEVEL=0 -c $P/mock_b.cpp -o $B/mb.o || fail "mock_b"
$CXX -g -w $INC -c $P/mock_main.cpp -o $B/mm.o            || fail "mock_main"
$CXX -g -w $INC -c $P/stub/os_stub.cpp -o $B/mo.o         || fail "os_stub"
# Mock/Logger.hpp defines the sink inline. If any of its bodies still depended on
# the including TU's LOG_LEVEL, these two objects would carry different
# definitions of one inline function -- an ODR violation the linker resolves
# arbitrarily, making behaviour depend on link order.
sym=$(nm $B/ma.o | grep -i render | awk '{print $3}' | head -1)
for t in ma mb; do
    objdump --disassemble-symbols=$sym $B/$t.o 2>/dev/null \
      | tail -n +3 | grep -oE '\b[a-z][a-z0-9.]+\b' > $B/$t.mn
done
if [[ -s $B/ma.mn ]] && diff -q $B/ma.mn $B/mb.mn > /dev/null; then
    print "  ok: identical instruction stream for Logger::render() at LOG_LEVEL 4 and 0"
else
    fail "codegen differs between LOG_LEVEL 4 and 0 -- ODR divergence is back"
fi
clang++ -std=c++17 -g $B/mb.o $B/ma.o $B/mm.o $B/logger.o $B/mo.o -o $B/mock_A || fail "link mock A"
clang++ -std=c++17 -g $B/ma.o $B/mb.o $B/mm.o $B/logger.o $B/mo.o -o $B/mock_B || fail "link mock B"
for v in A B; do
    $B/mock_$v 2>&1 | grep -E "ok |FAIL|ALL PASS" | sed 's/^/  /' || true
    $B/mock_$v > /dev/null 2>&1 || fail "simulator-sink tests failed in order $v"
done

print "\n===== 7. sanitizers ====="
for san in address thread; do
    $CXX -g -w -fsanitize=$san $INC -DLOG_LEVEL=0 -c $P/tu_level0.cpp -o $B/s0.o
    $CXX -g -w -fsanitize=$san $INC -c $P/tu_level4.cpp     -o $B/s4.o
    $CXX -g -w -fsanitize=$san $INC -c $P/tu_transitive.cpp -o $B/st.o
    $CXX -g -w -fsanitize=$san $INC -c $P/main.cpp          -o $B/sm.o
    $CXX -g -w -fsanitize=$san $INC -c $LOGGER              -o $B/sl.o
    clang++ -std=c++17 -g -fsanitize=$san $B/s0.o $B/s4.o $B/st.o $B/sm.o $B/sl.o -o $B/san_$san
    $B/san_$san > $B/$san.out 2>&1
    if grep -q "Sanitizer" $B/$san.out; then
        print "  $san: REPORTED"
        grep -E "WARNING|ERROR|SUMMARY" $B/$san.out | head -4 | sed 's/^/    /'
        fail "$san found a problem"
    else
        print "  $san: clean"
    fi
done

print "\n===== 8. existing sources compile unmodified, at both levels ====="
for lvl in 4 0; do
    ok=0; skip=0; broke=0; fmt=0
    for f in $(grep -rlE 'LOG_(DEBUG|INFO|WARNING|ERROR)' --include='*.cpp' $W/Libs/Source); do
        out=$($CXX -fsyntax-only -DLOG_LEVEL=$lvl $INC $f 2>&1)
        if print -r -- "$out" | grep -q "file not found"; then
            skip=$((skip+1)); continue
        fi
        ok=$((ok+1))
        print -r -- "$out" | grep -q "error:" && { broke=$((broke+1)); print "    BROKE ${f#$W/}" }
        fmt=$((fmt + $(print -r -- "$out" | grep -c "warning:.*format")))
    done
    print "  LOG_LEVEL=$lvl: compiled=$ok skipped(needs TouchGFX/SDL)=$skip broken=$broke format-bugs-found=$fmt"
    [[ $broke -eq 0 ]] || fail "existing sources broke at LOG_LEVEL=$lvl"
done

print "\n===== 9. LOG_LEVEL_* stay usable in real preprocessor conditionals ====="
# SensorManager.cpp and SensorListener.cpp use `#if LOG_MODULE_LEVEL == LOG_LEVEL_DEBUG`.
cat > $B/ifcheck.cpp <<'EOF'
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"
#if LOG_MODULE_LEVEL == LOG_LEVEL_DEBUG
#error "INFO compared equal to DEBUG in the preprocessor"
#endif
#if LOG_MODULE_LEVEL != LOG_LEVEL_INFO
#error "INFO did not compare equal to INFO in the preprocessor"
#endif
int ok() { return 1; }
EOF
$CXX -fsyntax-only $INC $B/ifcheck.cpp && print "  ok" || fail "broke #if usage of LOG_LEVEL_*"

print "\n===== 10. embedded constraints: -fno-exceptions -fno-rtti -Os ====="
for lvl in 4 0; do
    $CXX -Os -fno-exceptions -fno-rtti -DLOG_LEVEL=$lvl -fsyntax-only $INC $LOGGER \
      && print "  LOG_LEVEL=$lvl ok" || fail "Logger.cpp needs exceptions/RTTI at LOG_LEVEL=$lvl"
done
cat > $B/lockfree.cpp <<'EOF'
#include <atomic>
#include <cstdint>
// No libatomic dependency, which matters under -nostdlib.
static_assert(std::atomic<void*>::is_always_lock_free,    "pointer not lock free");
static_assert(std::atomic<uint8_t>::is_always_lock_free,  "uint8_t not lock free");
static_assert(std::atomic<uint32_t>::is_always_lock_free, "uint32_t not lock free");
EOF
$CXX -fsyntax-only $B/lockfree.cpp && print "  atomics always-lock-free" || fail "atomics not lock free"

print "\n===== 11. non-GNU compiler path (the Windows simulator builds with MSVC) ====="
# MSVC does not implement __attribute__. UNA_PRINTF_FMT_OFF forces the same
# unannotated path MSVC takes, so that configuration is exercised here rather
# than discovered on a machine no one in CI has.
for lvl in 4 0; do
    $CXX -DUNA_PRINTF_FMT_OFF -DLOG_LEVEL=$lvl -fsyntax-only $INC $LOGGER \
      && print "  Logger.cpp, LOG_LEVEL=$lvl: ok" \
      || fail "Logger.cpp does not build on the non-GNU path at LOG_LEVEL=$lvl"
done
$CXX -DUNA_PRINTF_FMT_OFF -fsyntax-only $INC $P/mock_a.cpp \
  && print "  Mock/Logger.hpp + Logger.h via a simulator TU: ok" \
  || fail "simulator headers do not build on the non-GNU path"
# Every logging source, on the non-GNU path.
broke=0
for f in $(grep -rlE 'LOG_(DEBUG|INFO|WARNING|ERROR)' --include='*.cpp' $W/Libs/Source); do
    out=$($CXX -DUNA_PRINTF_FMT_OFF -fsyntax-only $INC $f 2>&1)
    print -r -- "$out" | grep -q "file not found" && continue
    print -r -- "$out" | grep -q "error:" && { broke=$((broke+1)); print "    BROKE ${f#$W/}" }
done
print "  existing logging sources on the non-GNU path: broken=$broke"
[[ $broke -eq 0 ]] || fail "existing sources break on the non-GNU path"

print "\n===== 12. no unguarded compiler-specific syntax in the public headers ====="
# The annotation must only ever appear inside UNA_PRINTF_FMT's own definition.
stray=$(grep -rn "__attribute__" $W/Libs/Header/ \
        | grep -v "define UNA_PRINTF_FMT" \
        | grep -vE "^\S+:[0-9]+: \*" \
        | wc -l | tr -d ' ')
print "  raw __attribute__ outside the macro definition: $stray (want 0)"
[[ $stray -eq 0 ]] || {
    grep -rn "__attribute__" $W/Libs/Header/ | grep -v "define UNA_PRINTF_FMT" | grep -vE "^\S+:[0-9]+: \*" | sed 's/^/    /'
    fail "compiler-specific syntax leaked into a public header"
}

print ""
if [[ $rc -eq 0 ]]; then print "===== ALL CHECKS PASSED ====="; else print "===== FAILURES ====="; fi
exit $rc
