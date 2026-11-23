CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
NVCC ?= $(shell command -v nvcc 2>/dev/null || echo /usr/local/cuda/bin/nvcc)
CUDA_ARCH ?= sm_75
NVFLAGS ?= -std=c++20 -O2 --fmad=false -arch=$(CUDA_ARCH)
EMXX ?= em++
OUT := build/native
DEPS := $(wildcard native/*.hpp)
CASES := bubble sod acoustic film

.PHONY: all verify cuda cuda-check data wasm site-assets clean

all: $(OUT)/cluster-b

$(OUT) public/data public/engine:
	mkdir -p $@

$(OUT)/cluster-b: native/main.cpp $(DEPS) | $(OUT)
	$(CXX) $(CXXFLAGS) $< -o $@

$(OUT)/cluster-b-tests: native/tests.cpp $(DEPS) | $(OUT)
	$(CXX) $(CXXFLAGS) $< -o $@

verify: $(OUT)/cluster-b-tests
	$<

$(OUT)/cluster-b-cuda: native/main.cpp native/cuda_backend.cu $(DEPS) | $(OUT)
	$(NVCC) $(NVFLAGS) -DCB_CUDA native/main.cpp native/cuda_backend.cu -o $@

$(OUT)/cluster-b-cuda-tests: native/cuda_tests.cu native/cuda_backend.cu $(DEPS) | $(OUT)
	$(NVCC) $(NVFLAGS) native/cuda_tests.cu native/cuda_backend.cu -o $@

cuda: $(OUT)/cluster-b-cuda $(OUT)/cluster-b-cuda-tests

cuda-check: $(OUT)/cluster-b-cuda-tests
	@code=0; $< || code=$$?; test $$code -eq 0 || test $$code -eq 77

$(OUT)/%.cbf: $(OUT)/cluster-b
	$< $* $@ 2

public/data/%.cbf.gz: $(OUT)/%.cbf native/pack.py | public/data
	python3 native/pack.py $< $@

public/data/bubble-preview.cbf.gz: $(OUT)/bubble.cbf native/pack.py | public/data
	python3 native/pack.py $< $@ 43

public/data/verification.json: $(OUT)/cluster-b-tests native/report.py | public/data
	python3 native/report.py $< $@

public/data/cluster-b-native.zip: Makefile README.md $(wildcard native/*) | public/data
	python3 -m zipfile -c $@ Makefile README.md native

data: $(CASES:%=public/data/%.cbf.gz) public/data/bubble-preview.cbf.gz public/data/verification.json

public/engine/cluster.js public/engine/cluster.wasm &: native/main.cpp $(DEPS) | public/engine
	$(EMXX) -std=c++20 -O3 -fexceptions $< --no-entry -sNO_EXIT_RUNTIME=1 -sMODULARIZE=1 \
		-sEXPORT_ES6=1 -sENVIRONMENT=worker -sALLOW_MEMORY_GROWTH=1 -o public/engine/cluster.js

wasm: public/engine/cluster.js public/engine/cluster.wasm

site-assets: data wasm public/data/cluster-b-native.zip

clean:
	rm -rf $(OUT)