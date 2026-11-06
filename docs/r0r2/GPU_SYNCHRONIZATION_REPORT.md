# GPU synchronization report

## Primitives in use
| Primitive | Where | Purpose |
|---|---|---|
| Win32 semaphores `m_semaphore_cpu[i]` / `m_semaphore_gpu[i]` | `Context` ctor, `presentFrame`, `renderThread`, `~Context` | slot handoff game <-> render, max `frame_latency + 1` slots |
| `glFenceSync` + `glClientWaitSync(GL_TIMEOUT_IGNORED)` | `renderThread` end of frame | full CPU/GPU sync every frame (legacy) |
| `glFlush` | before the wait | makes the fence reachable |
| `glMemoryBarrier` (inside `Pipeline::dispatchCompute`) | bloom blur / FXAA compute | GPU-side ordering of compute writes |
| `SwapBuffers` + `wglSwapIntervalEXT(App.vsync)` | `renderThread`, `toggleVsync` | present, vsync blocking (driver dependent) |
| Waitable timer / Sleep + QPC spin | limiter | frame cap / pacing |
| `GL_TIME_ELAPSED` queries (new, perf only) | `perf::GpuTimer` | GPU time, read only when `GL_QUERY_RESULT_AVAILABLE` |

## Findings
1. The fence wait makes every frame fully synchronous; the GPU is idle when the render thread
   starts the next frame. Cost = GPU time of the frame minus nothing (no overlap). `perf_log`
   now reports it as `fence_ms`; it has **not been measured**.
2. Single VBO and single PBO shared by all slots: overlap would need per-slot buffers (see
   FENCE_LIFECYCLE.md). This is why `gpu_sync=1` is only an experiment.
3. `GL_TIMEOUT_IGNORED`: a GPU hang hangs the render thread and, through `m_semaphore_gpu`, the
   game thread. Unchanged (any timeout policy needs a recovery path that does not exist).
4. Shutdown: the old exit path released only `m_semaphore_gpu[0..1]`, so `~Context` could block
   forever on slot >= 2 with `frame_latency >= 2`; `~Context` also made the context current
   without waiting for the render thread. Both fixed (all slots released; thread handle waited, 2 s bound).
5. GPU timer queries bracket the frame's command execution (upload -> module pass), not
   `Menu::draw` (ImGui) or the present blit, and never block. With `gpu_sync=0` the result is
   usually available on the next frame; `gpu_frame` records which frame a value belongs to.
   CPU submit time (`render_busy_ms`) is never reported as GPU time.
6. Timer queries are skipped (not blocked on) when 8 are outstanding.
