CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -Iinclude -march=native
SRC      := src/main.cpp
HDRS     := $(wildcard include/mo/*.hpp)
BIN      := build/opt_cockpit

all: $(BIN)

$(BIN): $(SRC) $(HDRS)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN)

run: $(BIN)
	./$(BIN) all

clean:
	rm -rf build

.PHONY: all run clean
