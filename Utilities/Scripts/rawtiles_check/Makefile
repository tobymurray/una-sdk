# Host-side checker for SDK::RawTiles::Container.
#
#   make                  — build build/rawtiles_check
#   make clean            — remove build outputs
#
# Usage:
#   build/rawtiles_check <pack.rawtiles>
#       Validates the pack and prints a header dump.
#
#   build/rawtiles_check <pack.rawtiles> <pack.hashes>
#       As above, then performs the § 14.5 reader-conformance check: for each
#       line in the .hashes sidecar, calls Container::getTile(z, x, y),
#       SHA-256s the returned bytes, and compares to the recorded digest.
#
# Exits 0 on full pass, 1 on any failure.

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion

SDK_ROOT := $(abspath ../../..)
INCLUDES := -I$(SDK_ROOT)/Libs/Header

BIN_DIR  := build
BIN      := $(BIN_DIR)/rawtiles_check
SRC      := main.cpp $(SDK_ROOT)/Libs/Source/RawTiles/Container.cpp

.PHONY: all clean

all: $(BIN)

$(BIN): $(SRC) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(SRC)

$(BIN_DIR):
	mkdir -p $@

clean:
	rm -rf $(BIN_DIR)
