CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pedantic
BIN := build/mdo_engine

all: $(BIN)

$(BIN): src/main.cpp include/mdo_core.hpp include/mdo_optim.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/main.cpp -o $@

verify: $(BIN)
	./$(BIN) verify nx=9 ny=8 samples=2500

test: $(BIN)
	./$(BIN) verify nx=8 ny=7 samples=1500
	./$(BIN) revolve steps=64 slots=5 trace=0
	./$(BIN) shape generations=20 psoIter=15
	./$(BIN) optimize nx=10 ny=8 iters=24

clean:
	rm -rf build
.PHONY: all verify test clean
