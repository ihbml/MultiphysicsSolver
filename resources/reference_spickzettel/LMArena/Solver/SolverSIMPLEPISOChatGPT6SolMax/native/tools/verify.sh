#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
python3 native/tools/generate_mechanism.py native/mechanisms/chain.json native/include/generated/chain_abc.hpp
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
g++ -std=c++20 -O2 -I native/include native/tests/verify.cpp native/enzyme_bridge.cpp -o "$scratch/verify"
"$scratch/verify"
"$scratch/verify" --json > public/verification.json
g++ -std=c++20 -O2 -fopenmp -I native/include native/tests/verify.cpp native/enzyme_bridge.cpp -o "$scratch/verify_omp"
OMP_NUM_THREADS=4 "$scratch/verify_omp"