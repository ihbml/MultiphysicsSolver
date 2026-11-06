CXX ?= g++
CXXFLAGS ?= -O3 -std=c++20 -Wall -Wextra -Wpedantic

.PHONY: all test clean
all: mesher
mesher: mesher.cpp
	$(CXX) $(CXXFLAGS) $< -o $@
test: mesher
	python3 tests/test_mesher.py ./mesher
clean:
	rm -f mesher
