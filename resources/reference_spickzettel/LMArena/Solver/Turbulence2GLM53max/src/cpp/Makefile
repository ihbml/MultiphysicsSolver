# =============================================================================
#  CLUSTER A - multiphysics suite build
#  CPU  : g++ 12.2, C++20, -O2 -Wall -Wextra  (verified)
#  GPU  : nvcc, C++20, sm_80                   (kernel bodies are additionally
#         regression-tested on a CPU node with -DCLA_HOST_MIRROR)
# =============================================================================
CXX      ?= g++
NVCC     ?= nvcc
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -I.
NVCCFLAGS?= -std=c++20 -O3 -arch=sm_80 -I.

.PHONY: all verify cuda host-mirror clean

all: verify

verify: cluster_a_verify.cpp
	$(CXX) $(CXXFLAGS) -o cla_verify cluster_a_verify.cpp
	./cla_verify > cluster_a_results.json
	@echo "== $(shell grep -c '"pass": true' cluster_a_results.json) checks passed =="

cuda: cluster_a_cuda.cu
	$(NVCC) $(NVCCFLAGS) -o cla_cuda cluster_a_cuda.cu

# CPU mirror of the device code - used by CI when no GPU is present
host-mirror: cluster_a_cuda.cu
	$(CXX) $(CXXFLAGS) -DCLA_HOST_MIRROR -x c++ -o cla_cuda_host cluster_a_cuda.cu

clean:
	rm -f cla_verify cla_cuda cla_cuda_host cluster_a_results.json
