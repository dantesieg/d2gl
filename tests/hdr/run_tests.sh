#!/bin/sh
# D2GL HDR tests: colour math natively + i686 under Wine (d2m-wine); HLSL vs CPU under Wine/wined3d (d2m-wine-x,
# built from prototypes/hdr10/docker/Dockerfile.wine-x on feature/hdr10-prototype).
set -e
cd "$(dirname "$0")"
OUT=${OUT:-$(mktemp -d)}
W="-Wall -Wextra -Wshadow -Wconversion -Werror"
clang++ -std=c++17 $W test_hdr_color.cpp -o "$OUT/test_hdr_color"
"$OUT/test_hdr_color" | tail -1
i686-w64-mingw32-g++ -std=c++17 -O2 $W -static -s test_hdr_color.cpp -o "$OUT/test_hdr_color.exe"
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static -s test_hdr_gpu.cpp -o "$OUT/test_hdr_gpu.exe" -ld3d11
docker run --rm --platform linux/amd64 -v "$OUT":/t d2m-wine sh -c 'wine /t/test_hdr_color.exe 2>/dev/null | tail -1'
docker run --rm --platform linux/amd64 -v "$OUT":/t d2m-wine-x sh -c 'xvfb-run -a wine /t/test_hdr_gpu.exe 2>/dev/null'
