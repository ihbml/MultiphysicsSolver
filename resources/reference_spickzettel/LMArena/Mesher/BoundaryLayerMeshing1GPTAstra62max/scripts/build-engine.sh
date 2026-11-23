#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build public/engine public/meshes
${CXX:-g++} -std=c++20 -O2 octree_hexcore.cpp -o build/hexalab
build/hexalab --self-test
build/hexalab --geometry l-channel --out public/meshes/l-channel.json
build/hexalab --geometry twin-plate --out public/meshes/twin-plate.json
PYTHONDONTWRITEBYTECODE=1 python3 tests/verify_mesh.py
PYTHONDONTWRITEBYTECODE=1 python3 tests/certify_dyadic.py --write-report
em++ -std=c++20 -O2 -fexceptions octree_hexcore.cpp --no-entry \
  -sMODULARIZE=1 -sEXPORT_NAME=HexaEngine -sENVIRONMENT=worker \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 \
  -sEXPORTED_RUNTIME_METHODS='["cwrap"]' -sFILESYSTEM=0 \
  -o public/engine/hexalab.js
zip -q -r public/engine/hexalab-source.zip octree_hexcore.cpp engine README.md scripts/build-engine.sh tests