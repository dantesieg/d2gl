# Per-frame settings snapshot (R2)

## Goal
`renderThread` used to read live `App.*` settings while the game thread (window/viewport) and the
menu (render thread) changed them. The snapshot gives each recorded frame one immutable, internally
consistent set of values that matches what the game thread recorded.

## Type
`perf::FrameSettings` in `d2gl/src/graphic/perf/frame_settings.h` (portable, header-only, values
only): resolved viewport (`vp_w/h/x/y`), window size, sharpen/fxaa/bloom/lut/tonemap/world_only,
dynamic light (incl. `light_vsource` for `gameDrawBufferCount`), soft shadows, spell glow,
`level_no` (copied from engine memory on the game thread), and `perf::LimiterSettings`
(`generation`, `active`, `pacing`, `frame_len_ms`, `frame_len_ns`, `pace_period`).

## Protocol
1. Game thread, `Context::presentFrame()`: `captureSettings(cb.m_settings)` writes the snapshot into
   the slot's `CommandBuffer`, then `ReleaseSemaphore(m_semaphore_cpu[slot])` (**release**).
2. Render thread, `renderThread`: `WaitForSingleObject(m_semaphore_cpu[slot])` (**acquire**), then
   copies `cmd->m_settings` into the local `st` (and `m_game_timing` into `game_timing`).
3. The game thread does not write the slot again until it acquires `m_semaphore_gpu[slot]`, which
   the render thread releases after its last `cmd->` access. The local copies make the limiter and
   perf code (which run after that release) independent of the slot.

Ordering: Win32 synchronisation functions (`ReleaseSemaphore`, `WaitForSingleObject`, …) act as
full memory barriers (MSDN "Synchronization and Multiprocessor Issues"), so all writes before the
release are visible after the wait. `tests/r0r2/test_perf.cpp::testSnapshotHandoff` reproduces the
protocol with `std::binary_semaphore` (release/acquire by the C++ standard) for 1e6 frames under
TSan, and a negative control (`-DTSAN_NEGATIVE_CONTROL`, a write after the release) is reported.

## Reads replaced in `renderThread`
viewport/offset, Movie viewport, `u_SpellGlow`, `dynamic_light.*` (PreFx light + lightfx),
`bloom.*`, `lut.selected`, `tonemap.*`, `soft_shadows.*`, `sharpen.*`, `fxaa.*`,
`shader.world_only`, `gameDrawBufferCount()` (now takes the snapshot), `updateAmbient` level,
limiter (`foreground_fps.pacing`, `m_limiter.active/frame_len_*/pace_period`).

Kept live (immutable after start or render-owned): `App.frame_latency`, `App.gl_caps.*`,
`App.hdc`, `App.perf.*`, `App.shader.selected` (written only on the render thread).

## Limiter handoff
`setFpsLimit` (game thread) now writes only `m_limiter_settings` and bumps `generation`. The render
thread owns `m_limiter.pace_next` and `m_limiter.due_time`; when it sees a new generation in the
snapshot it resets `pace_next = 0` and calls `resetFileTime()` - what `setFpsLimit` used to do
directly from the other thread.

## Behaviour changes
* Menu edits reach the renderer through the game thread's next `presentFrame`, i.e. up to
  `frame_latency + 1` frames later than before (before: next executed frame). Not perceptible in
  principle; **not verified in game**.
* A limiter change takes effect on the first frame recorded after it, not mid-frame.
* Shutdown: a slot woken by `~Context` that was never presented (`valid == false`) is filled from
  `App` on the render thread (same as the old behaviour).

## Not covered (remaining)
Callees that still read `App.*` on the render thread: `onResize` (`App.viewport.scale/size`),
`onShaderChange`/`Upscaler::*`, `takeScreenShot` (`App.viewport`), `Menu::draw`, `MiniMap::resize`.
The capture itself reads values that `Menu::draw` writes on the render thread (32-bit scalars), and
`App.window.size` has writers on both threads. See THREAD_SAFETY_AUDIT.md section 5.
