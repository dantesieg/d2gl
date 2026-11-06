# R0/R2 changelog (branch feature/r0-r2-threading, base a486557)

| File | Change |
|---|---|
| `d2gl/src/app.h` | `App.perf { log, sample_every, summary_every, gpu_sync }` |
| `d2gl/src/option/ini.cpp` | `[Other]` `perf_log`, `perf_sample_every`, `perf_summary_every`, `gpu_sync`: template specifiers and args added in the same order (`%s %d %d %d` after `frame_latency`); `loadIni` reads them with clamping |
| `d2gl/src/graphic/command_buffer.h` | `CommandBuffer::m_settings` (snapshot), `m_game_timing` |
| `d2gl/src/graphic/context.h` | `m_rendering` -> `std::atomic<bool>`; `m_render_thread`; `LimiterMetrics` reduced to render-owned state; `m_limiter_settings`; perf members; `m_inflight` atomic; `gameDrawBufferCount(const FrameSettings&)`; `updateAmbient(size, level)`; `captureSettings`, `qpcNow`, `qpcMs` |
| `d2gl/src/graphic/context.cpp` | `Context::Context`: keep the thread handle, latch `m_perf`/`m_gpu_sync`. `~Context`: wait for the render thread (2 s bound) before taking the context. `renderThread`: local snapshot copy, all live `App.*` setting reads replaced, limiter from snapshot + generation reset, perf probes, GPU timer, `gpu_sync` branch (legacy code byte-for-byte in branch 0), releases all gpu semaphores on exit. `beginFrame`/`presentFrame`: perf probes, `captureSettings` before the cpu release. `setFpsLimit`: writes only game-thread settings. New `captureSettings`. `gameDrawBufferCount`, `updateAmbient`: snapshot inputs |
| `d2gl/src/graphic/perf/spsc_ring.h` | new, portable SPSC ring |
| `d2gl/src/graphic/perf/frame_stats.h` | new, portable stats/percentiles |
| `d2gl/src/graphic/perf/perf_sample.h` | new, portable CSV/JSON row + summary format |
| `d2gl/src/graphic/perf/frame_settings.h` | new, portable snapshot types |
| `d2gl/src/graphic/perf/perf_log.h` | new, Windows: `PerfLogger` (writer thread), `GpuTimer`, `threadCpuMs` |
| `d2gl/d2gl.vcxitems(.filters)` | list the new headers |
| `tests/r0r2/test_perf.cpp`, `run_tests.sh` | portable tests (TSan + mingw/Wine) |
| `docs/r0r2/*` | this documentation |

Behaviour with default ini (`perf_log=false`, `gpu_sync=0`): same GL command stream; differences
are the snapshot timing (settings reach the renderer at record time) and the shutdown fixes.
