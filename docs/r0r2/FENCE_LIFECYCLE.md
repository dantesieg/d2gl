# Fence lifecycle and per-slot fence design (A7)

## Legacy (`gpu_sync=0`, default, unchanged code)
Per frame in `renderThread`, after the module pass:
```
GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
glFlush();
glClientWaitSync(sync, 0, GL_TIMEOUT_IGNORED);   // blocks the render thread until the GPU is idle
glDeleteSync(sync);
ReleaseSemaphore(m_semaphore_gpu[slot]);         // slot may be re-recorded
Menu::draw(); SwapBuffers(); limiter
```
Lifetime: created and deleted in the same iteration; no fence outlives a frame. The GPU therefore
never has more than the current frame plus the ImGui/present work in flight; CPU/GPU overlap
exists only across `SwapBuffers` and the limiter. `frame_latency` buffers the *game thread*
against the render thread, not the GPU.

What the wait protects:
* CPU-side slot memory (`m_vertices.data[slot]`, `m_tex_buffer`): **not** needed - GL copies
  client memory before `glBufferSubData` returns.
* `m_vertex_buffer` / `m_pixel_buffer` (one VBO, one PBO for all slots) and the FBO/texture chain:
  the next frame's `glBufferSubData` / PBO upload overwrites a buffer the GPU may still read. With
  the legacy wait the GPU is idle, so no implicit driver sync occurs.
* It also throttles: the render thread cannot run more than one frame ahead of the GPU.

## Experimental (`gpu_sync=1`, ini only, UNMEASURED)
```
GLsync sync = glFenceSync(...); glFlush();
if (pending) { glClientWaitSync(pending, 0, GL_TIMEOUT_IGNORED); glDeleteSync(pending); }
pending = sync;                                  // waited one frame later
```
* The render thread waits for frame N-1 instead of N, so the GPU can execute frame N while the
  render thread records N+1.
* `pending` is a render-thread local, waited and deleted at loop exit before `wglMakeCurrent(NULL)`.
* **PBO / VBO conflict**: frame N+1's `glBufferSubData(GL_PIXEL_UNPACK_BUFFER, …)` and
  `glBufferSubData(GL_ARRAY_BUFFER, …)` now target buffers frame N may still be reading. GL
  semantics keep this correct (the driver stalls or renames), but it may simply move the wait into
  the driver, or cost a copy of up to 12 MiB. Whether it helps or hurts is unknown.
* Possible +1 frame of latency on the GPU side.
* `gpu_sync` is read once (`Context::Context` → `m_gpu_sync`); there is no menu entry.

## Proper per-slot design (not implemented)
1. `m_fence[MAX_FRAME_LATENCY]`; after the slot's work, `m_fence[slot] = glFenceSync(...)`.
2. Per-slot GPU buffers: VBO and PBO ring of `frame_latency + 1` regions (or one persistent-mapped
   buffer with `GL_MAP_PERSISTENT_BIT`/`GL_MAP_COHERENT_BIT` on GL 4.4, `glBufferData` orphaning on
   3.3), so frame N+1 never writes memory frame N reads.
3. Before reusing slot `s`: `glClientWaitSync(m_fence[s], GL_SYNC_FLUSH_COMMANDS_BIT, timeout)`
   with a finite timeout and `GL_TIMEOUT_EXPIRED` logging, then `glDeleteSync`.
4. Release `m_semaphore_gpu[s]` right after the uploads (CPU memory is free then), not after the fence.
5. Delete all fences on shutdown and whenever `onResize` recreates targets.
Do not enable any of this without the BENCHMARK_PROTOCOL.md A/B data (fence_ms, gpu_ms,
interval_ms p99 and input-latency feel at frame_latency 1 and 2, vsync on/off).
