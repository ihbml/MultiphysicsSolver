CXX ?= g++
AR ?= ar
CXXFLAGS ?= -O3 -std=c++20 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude -Isrc
LDFLAGS ?=
# make OMP=1 enables parallel neighbor counting/filling and scalar transfer.
ifeq ($(OMP),1)
  CXXFLAGS += -fopenmp
  LDFLAGS += -fopenmp
endif

OBJECTS := build/mesh.o build/io.o build/sph.o build/transfer.o
.PHONY: all test test-web web bench clean example
all: build/hybridmesh build/libhybridmesh.a

build:
	mkdir -p build
build/%.o: src/%.cpp src/internal.hpp include/hybridmesh/hybridmesh.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
build/libhybridmesh.a: $(OBJECTS)
	$(AR) rcs $@ $^
build/hybridmesh: src/cli.cpp build/libhybridmesh.a include/hybridmesh/hybridmesh.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/cli.cpp build/libhybridmesh.a $(LDFLAGS) -o $@
build/tests: tests/test.cpp build/libhybridmesh.a include/hybridmesh/hybridmesh.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/test.cpp build/libhybridmesh.a $(LDFLAGS) -o $@
test: build/tests
	./build/tests
test-web: build/hybridmesh
	python3 -m unittest discover -s web -p 'test_web.py' -v
web: build/hybridmesh
	python3 web/server.py --host 0.0.0.0 --port $${PORT:-8000}
build/bench: examples/bench.cpp build/libhybridmesh.a include/hybridmesh/hybridmesh.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) examples/bench.cpp build/libhybridmesh.a $(LDFLAGS) -o $@
bench: build/bench
	./build/bench 2d
	./build/bench 3d
example: build/hybridmesh
	./build/hybridmesh --box2 24 24 1 1 --spacing 0.04 \
	  --core-box 0.28 0.28 0.72 0.72 --output examples/output --no-links
clean:
	rm -rf build examples/output
