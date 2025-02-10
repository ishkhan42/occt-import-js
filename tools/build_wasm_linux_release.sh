#/bin/bash
set -e

emcmake cmake -B build/wasm -DEMSCRIPTEN=1 -DCMAKE_BUILD_TYPE=Release .
emmake make -C build/wasm -j16

echo Build Succeeded.
