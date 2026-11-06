# R2 test results (real outputs only)

Run on 2026-11-06 (Europe/Istanbul). Host: macOS (arm64). No GPU/game test was possible.

## 1. Portable components: `tests/r0r2/run_tests.sh`
Toolchains: Apple clang version 21.0.0 (clang-2100.4.1.1); i686-w64-mingw32-g++ (GCC) 16.2.0; Wine in docker image `d2m-wine` (linux/amd64).
Commands:
```
clang++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=thread test_perf.cpp -o test_tsan && TSAN_OPTIONS=halt_on_error=1 ./test_tsan
i686-w64-mingw32-g++ -std=c++20 -O2 -Wall -Wextra -static test_perf.cpp -o test.exe
docker run --rm --platform linux/amd64 -v <dir>:/t d2m-wine wine /t/test.exe
```
Result: exit 0, no compiler warnings, no TSan reports. Output (TSan run, then Wine run):
```
stats: ok
format: ok
ring: ok (1000000 items, 0 full-ring retries)
snapshot handoff: ok (1000000 frames, latency 2)
ALL PASSED
stats: ok
format: ok
ring: ok (1000000 items, 0 full-ring retries)
snapshot handoff: ok (1000000 frames, latency 2)
ALL PASSED
OUT=$S ./run_tests.sh  3.47s user 0.43s system 76% cpu 5.122 total
```
(The CSV header / fixture row / JSON line printed by `testFormat` are omitted here; see PERF_LOG_SCHEMA.md.)

Negative control: `clang++ ... -fsanitize=thread -DTSAN_NEGATIVE_CONTROL` (one write after the
semaphore release) -> exit 134, `WARNING: ThreadSanitizer: data race` at test_perf.cpp:170 vs :149.
This shows the handoff test can detect the bug class it guards against.

## 2. Full D2GL build (GitHub Actions, windows-2022, msbuild Release x86 v143)
| Run | Commit | Result | Warnings |
|---|---|---|---|
| 42252514565 | 59c3db6 | success | 38, same set as deck baseline run 42114775134 (C4390 menu.cpp, C4101 ini.cpp) |
| 42252732843 | ac55fec | success | (not diffed) |
| 42252916797 | 10dce0d | success | 38, identical set to baseline |
Artifact of 42252916797: `d2gl/bin/Release/ddraw.dll` (2877440 B), `glide3x.dll` (2912256 B).
The final docs-only commit's run is listed in the final report.

## 3. Not tested (no Windows GPU / game available)
* Any runtime behaviour: rendering correctness, perf_log files written in game, GPU timer
  queries on a real driver, Proton/Wine GL behaviour, overhead.
* Resolution / fullscreen / window mode changes, shader preset switching, menu edits, renderer
  reset (`onResize` recreation), focus-loss limiter switching, shutdown with frame_latency >= 2.
* `gpu_sync=1` (compiled only).
* MSVC build of `tests/r0r2` (the CI workflow builds only d2gl.sln).
