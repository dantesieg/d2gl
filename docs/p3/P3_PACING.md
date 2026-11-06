# P3: frame pacing and latency (HDR path)

All flags in `[Other]`, read once at startup, default **off**. Nothing measured yet.

| ini key | effect |
|---|---|
| `hdr_single_fence` | Shared-texture HDR transport only, `gpu_sync=0`: the render thread no longer waits on its own GL fence (`renderThread`); it only flushes. `Presenter::present()` already waits on a fence that covers the frame + menu before D3D11 samples, and before the next frame's uploads, so buffer protection is unchanged. Interop / CPU-copy paths keep the old fence. |
| `hdr_latency_waitable` | Swapchain created with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`, `SetMaximumFrameLatency(1)` (`Presenter::createSwapchain`). The render thread waits on the object (100 ms bound) before taking the next game frame; `ResizeBuffers` repeats the flag. Falls back to the plain swapchain if creation with the flag fails. DXVK implements the waitable object. |
| `pace_to_refresh` | Vsync off + limiter pacing only: if the fps limit is within 3% of the refresh rate (DwmGetCompositionTimingInfo, else EnumDisplaySettings), pace at the exact refresh period. With vsync on the limiter is off (unchanged). |

Expected (not measured) perf_log effect, Deck HDR vsync 90 Hz:
- `hdr_single_fence`: `fence_ms` ~0; `swap_ms` absorbs the remaining GPU wait; one wait/flush round trip fewer, net small gain.
- `hdr_latency_waitable`: `swap_ms` drops (Present no longer blocks on the queue); the vsync wait moves into `render_wait_ms`; `interval_ms` stays 11.1; input-to-photon latency up to one frame lower; `game_wait` may rise slightly (render thread takes frames later).
- `pace_to_refresh`: steadier `interval_ms` with vsync off at a ~refresh fps limit.

Unchanged: HDR colour pipeline, shaders, metadata, shared-texture double buffer, CPU-copy fallback, simulation timing. No interpolation/smoothing added. `motion_prediction` (D2DX-style, `[Feature]`) stays default **false**, untouched.
