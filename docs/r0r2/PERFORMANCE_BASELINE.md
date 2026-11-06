# Performance baseline (R0)

**Baseline numbers: not yet measured.** No game run was possible while this was written (macOS
host, Steam Deck offline). Fill the table below only from real `d2gl_perf_summary.jsonl` output
collected with BENCHMARK_PROTOCOL.md.

| Scenario | frame_latency | vsync | fps | median ms | p95 | p99 | max | stalls | fence_ms median | gpu_ms median |
|---|---|---|---|---|---|---|---|---|---|---|
| (all) | | | not yet measured | | | | | | | |

## Instrumentation
Enable with `[Other] perf_log=true` in `d2gl.ini` (default `false`). Options:
`perf_sample_every` (1-1000, CSV row every N frames), `perf_summary_every` (60-100000 frames per
JSON summary). Output in the game's working directory: `d2gl_perf.csv`, `d2gl_perf_summary.jsonl`.
Schema: PERF_LOG_SCHEMA.md, perf_summary.schema.json.

| Metric | Thread | Where measured |
|---|---|---|
| `game_frame_ms` | game | QPC present-to-present (`presentFrame`, before the cpu release) |
| `game_work_ms` | game | `beginFrame` start -> `presentFrame` release (-1 if no `beginFrame` this frame) |
| `game_wait_ms` | game | time blocked on `m_semaphore_gpu` before this frame's recording started |
| `game_cpu_ms` | game | `GetThreadTimes` (kernel+user) delta per frame |
| `inflight` | game | frames submitted and not yet retired, incl. this one (atomic counter) |
| `render_wait_ms` | render | blocked on `m_semaphore_cpu` |
| `render_busy_ms` | render | acquire -> before fence (CPU submission; NOT GPU time) |
| `fence_ms` | render | `glFenceSync`..`glClientWaitSync` |
| `swap_ms` | render | `Menu::draw` + `SwapBuffers` (incl. vsync block) |
| `limiter_ms` | render | limiter sleep/spin (and the post-loop bookkeeping) |
| `interval_ms` | render | `SwapBuffers` return to return (displayed frame interval proxy) |
| `render_cpu_ms` | render | `GetThreadTimes` delta per frame |
| `gpu_ms`, `gpu_frame` | GPU | `GL_TIME_ELAPSED` ring of 8; read only when available; belongs to `gpu_frame` |

Cost when off: `m_perf`/`perf_on` are fixed at startup; every probe is one predictable branch, no
QPC, no atomics, no queries, no thread. When on: ~6 QPC + 2 `GetThreadTimes` per frame per thread,
one timer query, one 136-byte ring push; I/O on a `THREAD_PRIORITY_LOWEST` thread (flush every
100 ms). Ring 4096 entries; overflow increments `dropped_samples` instead of blocking.

Caveats: `GetThreadTimes` advances at scheduler-tick granularity (~15.6 ms by default), so
per-frame CPU values are quantised; use sums over many frames. Under Proton/Wine the clock
sources and GL timer queries go through Wine/the Linux driver - compare like with like.
