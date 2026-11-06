# R1 synchronization

## Pipeline (unchanged)
Game thread records slot `s` -> `ReleaseSemaphore(cpu[s])` -> waits `gpu[s+1]`.
Render thread waits `cpu[s]` -> uploads (`glBufferSubData` VBO/PBO) + draws -> fence -> release `gpu[s]`
-> Menu + present (HDR: `hdr::Presenter::present`, else `SwapBuffers`) -> limiter.
Bounded queue: `frame_latency + 1` slots, binary semaphores, ownership = whoever holds the slot's token.

## early_slot_release=true
- Release of `gpu[s]` moves before the fence. Safe because after the last draw the render thread reads
  nothing from the slot: `glBufferSubData` copies client memory (`m_vertices`, `m_tex_buffer`) before
  returning, and `FrameSettings`/`GameTiming` were already copied to locals (R2).
- The fence itself is kept: it still keeps the render thread from re-uploading into the single shared
  VBO/PBO while the GPU reads them, and still throttles the render thread to one frame ahead of the GPU.
- HDR ordering unchanged: fence -> `present()` (which does its own GL fence + D3D11 present).
- Resize: `onResize` runs on the render thread at the start of a slot, after the previous fence; unchanged.
- Shutdown: loop exit still releases all `gpu[]` semaphores; an extra release of a full semaphore is a no-op.
- Latency: the game thread may start the next frame ~one fence earlier; queue depth is still bounded by
  `frame_latency`, so no unbounded latency accumulation.

## Not done (rationale)
- Lock-free queue: semaphores showed `render_wait` 0.0 ms on fast frames; no contention to remove.
- Per-slot fences / VBO-PBO rings (docs/r0r2/FENCE_LIFECYCLE.md): bigger change, no data that the
  fence (3.8 ms) is GPU-bound rather than present/vsync-bound; left for after A/B data.
- Swap/present (5.8 ms) is vsync/DXGI-flip blocking inside the HDR presenter; changing it needs HDR internals.
