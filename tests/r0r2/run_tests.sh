#!/bin/sh
# Builds and runs the portable R0/R2 tests: TSan natively, then mingw-w64 i686 under Wine (docker image d2m-wine).
set -e
cd "$(dirname "$0")"
OUT=${OUT:-$(mktemp -d)}
clang++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=thread test_perf.cpp -o "$OUT/test_tsan"
TSAN_OPTIONS=halt_on_error=1 "$OUT/test_tsan"
i686-w64-mingw32-g++ -std=c++20 -O2 -Wall -Wextra -static test_perf.cpp -o "$OUT/test.exe"
docker run --rm --platform linux/amd64 -v "$OUT":/t d2m-wine wine /t/test.exe
