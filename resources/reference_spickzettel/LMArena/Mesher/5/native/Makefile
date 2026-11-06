# Native FVM/SPH mesher core.
#
#   make            -> build/mesher  (release, OpenMP)
#   make debug      -> build/mesher-debug
#   make bench      -> build/mesher + run the internal scaling benchmark
#   make clean
#
# Only a C++20 compiler with OpenMP is required; there are no third-party deps.

CXX      ?= g++
CXXSTD   ?= -std=c++20
OPT      ?= -O3 -fno-math-errno -funroll-loops -ftree-vectorize
WARN     := -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
OMP      := -fopenmp
CXXFLAGS := $(CXXSTD) $(OPT) $(WARN) $(OMP) -pthread
LDFLAGS  := $(OMP) -pthread -lm

SRC_DIR  := src
BUILD    := build
SOURCES  := $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS  := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD)/%.o,$(SOURCES))
TARGET   := $(BUILD)/mesher

.PHONY: all clean debug bench info

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(BUILD)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "built $@ ($(shell nproc) threads available)"

$(BUILD)/%.o: $(SRC_DIR)/%.cpp $(wildcard $(SRC_DIR)/*.hpp)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

debug: CXXSTD := -std=c++20
debug: OPT    := -O0 -g -fsanitize=address,undefined
debug: LDFLAGS := -fopenmp -pthread -lm -fsanitize=address,undefined
debug: $(BUILD)/mesher-debug

$(BUILD)/mesher-debug: $(SOURCES)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXSTD) $(OPT) $(WARN) $(SOURCES) -o $@ $(LDFLAGS)

bench: all
	./$(TARGET) selftest

info:
	@echo "CXX=$(CXX)"; $(CXX) --version | head -1; echo "sources: $(SOURCES)"

clean:
	rm -rf $(BUILD)
