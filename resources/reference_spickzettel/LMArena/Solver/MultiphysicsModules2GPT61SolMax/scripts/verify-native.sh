#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
binary="$(mktemp /tmp/vector-cluster-c.XXXXXX)"
report="$(mktemp /tmp/vector-verification.XXXXXX)"
trap 'rm -f "$binary" "$report"' EXIT
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic public/solver/cluster_c.cpp -o "$binary"
"$binary" --verify
"$binary" --verify --json > "$report"
cp "$report" public/verification.json
chmod 644 public/verification.json
for module in battery fwh mhd tps; do
  "$binary" --demo "$module" > /dev/null
  printf 'Native %s demo: passed\n' "$module"
done
printf 'Verification report refreshed: public/verification.json\n'