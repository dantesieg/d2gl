# Benchmark protocol (manual)

There is **no gameplay automation** in D2GL or this repo: every run below is driven by hand.
No numbers have been collected yet; do not copy any into PERFORMANCE_BASELINE.md unless they come
from a real run of this protocol.

## Setup (keep identical across runs)
1. Same machine and OS (record: device, GPU, driver/Mesa, Proton/Wine version, display refresh).
2. Same D2GL DLL build (record the commit hash), same `d2gl.ini` except the variable under test.
3. Same character and save; resolution and window mode fixed; other overlays off.
4. Before each run delete `d2gl_perf.csv` and `d2gl_perf_summary.jsonl`.
5. Each scenario: 3 runs of at least 60 s after a 10 s warm-up; discard the first summary line.
6. Note device temperature/power mode (Steam Deck: fixed TDP and GPU clock if possible).

## Matrix
| ID | perf_log | frame_latency | vsync | fps limit | Scenario |
|---|---|---|---|---|---|
| B0 | false | 1 | on | - | Idle in town (Act 1 Rogue Encampment, stand still). Measure with an external tool only (MangoHud / PresentMon), to price the instrumentation |
| B1 | true | 1 | on | - | same as B0 |
| B2 | true | 1 | off | off | idle town |
| B3 | true | 1 | off | 60 (pacing off), then pacing on | idle town |
| B4 | true | 2 | on | - | idle town |
| B5 | true | 2 | off | off | idle town |
| D1 | true | 1 and 2 | on | - | Monster density: same dense area (e.g. Cow Level or Chaos Sanctuary entrance), walk a fixed path for 60 s |
| P1 | true | 1 and 2 | on | - | Particles: repeated casting of a particle-heavy skill (e.g. Blizzard/Fist of the Heavens) at a fixed spot |
| T1 | true | 1 and 2 | on | - | Area transitions: take the same waypoint pair 10 times; mark each transition time |
| G1 | true | 1 | on/off | - | `gpu_sync=1` vs `0` on D1 and P1 (only after B-series exists) |

Instrumentation overhead = B0 (external tool) vs B1 (external tool, while perf_log runs).

## Analysis
* Per run: last N summary lines of `d2gl_perf_summary.jsonl` (fps, mean, median, p95, p99, max,
  variance, stalls). Report median of the 3 runs.
* From the CSV: medians and p99 of `fence_ms`, `render_busy_ms`, `game_wait_ms`, `gpu_ms`, max
  `inflight`, `dropped_samples`.
* Transitions: locate spikes in `interval_ms` and `render_busy_ms`/`game_work_ms` at the marked times.
* A change is a win only if p99 and stalls improve without a median regression > 2 %, across all 3 runs.
