CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude
BUILD    := build

.PHONY: all verify clean

all: $(BUILD)/verify $(BUILD)/topopt_cli

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/verify: src/verify.cpp $(wildcard include/mdo/*.hpp) | $(BUILD)
	$(CXX) $(CXXFLAGS) src/verify.cpp -o $@

$(BUILD)/topopt_cli: src/topopt_cli.cpp $(wildcard include/mdo/*.hpp) | $(BUILD)
	$(CXX) $(CXXFLAGS) src/topopt_cli.cpp -o $@

verify: $(BUILD)/verify
	cd $(BUILD) && ./verify

clean:
	rm -rf $(BUILD)
