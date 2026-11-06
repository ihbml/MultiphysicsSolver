# DiFVM Mesher — build (g++ >= 12 / clang >= 15, C++20)
CXX      ?= g++
CXXFLAGS ?= -O3 -march=native -std=c++20 -fno-math-errno -funroll-loops -pthread
WARN     ?= -Wall -Wextra -Wno-unused-parameter
INC      := -Iinclude
SRC      := $(wildcard src/*.cpp)
OBJ      := $(patsubst src/%.cpp,build/%.o,$(SRC))

PREFIX   ?= /usr/local

.PHONY: all clean test bench install

all: bin/difvm_mesh bin/difvm_mesh_tests

build:
	@mkdir -p build bin

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) $(WARN) $(INC) -c $< -o $@

libdifvm_mesh.a: $(OBJ)
	ar rcs $@ $^

bin/difvm_mesh: tools/difvm_mesh.cpp libdifvm_mesh.a | build
	$(CXX) $(CXXFLAGS) $(WARN) $(INC) $< libdifvm_mesh.a -o $@ -pthread

bin/difvm_mesh_tests: tests/run_tests.cpp libdifvm_mesh.a | build
	$(CXX) $(CXXFLAGS) $(WARN) $(INC) $< libdifvm_mesh.a -o $@ -pthread

test: bin/difvm_mesh_tests
	./bin/difvm_mesh_tests

bench: bin/difvm_mesh
	./bin/difvm_mesh bench --n 96

clean:
	rm -rf build bin libdifvm_mesh.a

install: bin/difvm_mesh
	install -m 755 bin/difvm_mesh $(PREFIX)/bin/difvm_mesh
