# R2 Thread-safety audit (D2GL, base a486557)

All paths are under `d2gl/src/`. Line numbers drift; function names are authoritative.

## 1. Thread model

| Thread | Created by | Runs | GL? |
|---|---|---|---|
| **Main / game thread** | Game.exe | D2 game loop, Glide/DDraw entry points (`glide3x/`, `ddraw/`), `d2/funcs.cpp` hooks, `Context::beginFrame`, all `push*`/`flushVertices`, `onStageChange`, `presentFrame`, `Menu::check`, `win32::WndProc` (the window was created on this thread, so its messages are dispatched here), `win32::windowResize/setWindowRect`, `Context::setFpsLimit` | No (after the ctor) |
| **Render thread** | `Context::Context` → `CreateThread(Context::renderThread)` | `renderThread` loop, `onResize`, `onShaderChange`, `Upscaler::*`, `updateAmbient`, `takeScreenShot`, `toggleVsync`, `Menu::draw` (ImGui, incl. all menu writes to `App.*`), `MiniMap::resize`, `HDText::setMVP` | **Only GL caller** after the ctor |
| **Perf writer** (new, only if `perf_log=1`) | `perf::PerfLogger` ctor, on the render thread | pops `SpscRing`, writes CSV/JSONL, `THREAD_PRIORITY_LOWEST` | No |

Assumption (not verifiable here): D2 1.14d pumps window messages on the same thread that drives
rendering. All "game thread" statements below rely on it.

## 2. Context ownership
* `Context::Context` (game thread): creates a dummy context for `glewInit`, then the core context
  (`wglCreateContextAttribsARB`), makes it current, creates every GL object, calls
  `onResize`/`toggleVsync`, then `wglMakeCurrent(NULL,NULL)` and starts the render thread.
  `CreateThread` orders all ctor writes before the thread start.
* `renderThread`: `wglMakeCurrent(App.hdc, m_context)` once, releases it at exit.
* `Context::~Context` (game thread): stops the loop, now **waits for the render thread handle**
  (bounded 2 s), then makes the context current to delete objects.

## 3. Submission and buffering
* Slots: `m_command_buffer[MAX_FRAME_LATENCY]` + `m_vertices.data[slot]`,
  `m_vertices_mod.data[slot]`, `CommandBuffer::m_tex_buffer` (12 MiB per slot). Active slots:
  `App.frame_latency + 1` (ini 1-5, read once by `loadIni`, never written afterwards).
* `m_semaphore_cpu[i]` (init 0, max 1): "slot i recorded". `m_semaphore_gpu[i]` (init 1, max 1):
  "slot i retired, may be re-recorded".
* Game: `presentFrame` → capture snapshot → `ReleaseSemaphore(cpu[i])` → `i = (i+1) % (L+1)` →
  `WaitForSingleObject(gpu[i])` → `m_command_buffer[i].reset()`.
* Render: `WaitForSingleObject(cpu[i])` → upload (`glBufferSubData` VBO, PBO → textures) → execute
  commands → module pass → fence wait → `ReleaseSemaphore(gpu[i])` → `Menu::draw` → `SwapBuffers`
  → limiter → `i = (i+1) % (L+1)`.
* Win32 `ReleaseSemaphore`/`WaitForSingleObject` are full memory barriers, giving release/acquire
  ordering for all slot data in both directions.

## 4. GPU resources (single instance, shared by all slots)
`m_vertex_buffer` (VBO), `m_index_buffer`, `m_vertex_array`, `m_pixel_buffer` (PBO, 12 MiB),
all textures/FBOs/pipelines/UBOs. Created in the ctor or in `onResize`/`onShaderChange` on the
render thread; destroyed in `~Context` or on re-creation in `onResize`. No per-slot GPU buffers:
GPU-side reuse safety relies on the per-frame fence (legacy) or on implicit driver sync.

## 5. Shared-state table

Legend: G = game thread, R = render thread. "Fixed" = changed in this branch.

| State | Writer(s) | Reader(s) | Before | Now |
|---|---|---|---|---|
| `Context::m_rendering` | G (`~Context`) | R (loop condition) | plain `bool`: data race, loop may never observe it | **Fixed**: `std::atomic<bool>` (acquire load) |
| Slot data (commands, vertices, tex buffer, occluders, HD text mask) | G | R | semaphore-ordered | unchanged, documented |
| `App.viewport.*`, `App.window.size` (glm pairs) | G (`win32::windowResize`), **R** (`Menu::draw` applies window options, `menu.cpp` ~268) | R (`renderThread` vp_size/offset, Movie viewport, `onResize`, `takeScreenShot`, `Menu::draw`), G | torn-pair race | **Fixed for `renderThread`**: snapshot. Remaining: R writer in `Menu::draw` vs G `windowResize` (write/write), `onResize`/`takeScreenShot`/`Menu::draw` reads |
| `App.bloom/lut/tonemap/sharpen/fxaa/dynamic_light/soft_shadows/shader.world_only` | R (`Menu::draw`), G (`loadIni` only) | R (`renderThread`), G (`onStageChange`, `beginFrame`) | R-side reads were same-thread as the writer, but could disagree with what G recorded for that frame (e.g. `PreFx` recorded under one `dynamic_light.active`, executed under another; `gameDrawBufferCount` changing mid-frame with `frame_latency>1`) | **Fixed for `renderThread`**: snapshot, so execution matches recording. Remaining: G reads (`onStageChange`, `beginFrame`, `captureSettings`) of values R writes in `Menu::draw` - formally a race on 32-bit scalars |
| `App.shader.selected` | R (`Menu::draw`, `Upscaler`) | R | same thread | unchanged (render-owned) |
| `LimiterMetrics` (`active`, `frame_len_*`, `pace_period`, `pace_next`, `due_time`) | G (`setFpsLimit` from ctor, `Menu::check`, `WndProc` WM_ACTIVATE) **and** R (`pace_next`, `due_time` every frame; `resetFileTime` via `toggleVsync`) | R | write/write race on `pace_next`/`due_time`, torn 64-bit reads possible on x86-32 | **Fixed**: settings in `m_limiter_settings` (G only) + generation, copied into the snapshot; R owns `pace_next`/`due_time` and resets them when the generation changes |
| `App.foreground_fps.pacing` | R (`Menu::draw`) | R (limiter) | | now via snapshot (`captureSettings`, G read of an R-written bool: remaining) |
| `*d2::level_no` (engine memory) | Game.exe (G) | R (`updateAmbient`) | engine memory read off-thread | **Fixed**: copied into the snapshot on G |
| `App.game.screen`, `App.game.size/bpp` | G | R via `CommandBuffer::reset/resize` copies | ordered | unchanged |
| `App.frame_latency`, `App.gl_caps.*`, `App.perf.*`, `App.hdc` | startup only | both | immutable after start | unchanged |
| `option::Menu` `m_visible`, `m_changed`, `m_closing`, `m_options` | R (`draw`), G (`toggle` via WndProc) | G (`check`, `isVisible`), R | plain bools/structs | **Remaining** |
| ImGui context / `ImGui_ImplWin32_WndProcHandler` | R (`NewFrame`/`Render`), G (WndProc input) | both | ImGui IO written from WndProc on G while R runs a frame | **Remaining** (pre-existing upstream design) |
| `Upscaler` presets/passes | R | R | | render-owned |
| `modules::*` singletons (HDText, MiniMap, HDCursor, MotionPrediction) | mostly G; `HDText::setMVP`, `MiniMap::resize` on R (`onResize`) | both | | **Remaining**: not audited per member |
| `FrameMetrics m_frame` | G (`presentFrame`, `setFpsLimit`) | G (`getFrameTime`, fps counter); R reads only `frequency` (ctor-constant) | | unchanged |
| `m_inflight` (new) | G ++ / R -- | G | | `std::atomic<uint32_t>`, perf only |
| static locals in `onResize`, `setViewport`, `updateAmbient` | R | R | | render-owned |

## 6. Pointer lifetimes
* The snapshot (`perf::FrameSettings`) holds values only: no pointers into `App`, `Context`,
  modules or Game.exe memory. It lives inside `CommandBuffer`, i.e. as long as `Context`.
* `cmd` (`CommandBuffer*`) in `renderThread`: valid for the life of `Context`; ownership of the slot
  passes G->R at `ReleaseSemaphore(cpu[i])` and R->G at `ReleaseSemaphore(gpu[i])`. Every `cmd->`
  access happens before `ReleaseSemaphore(gpu[i])`. The limiter and perf code that run after it
  use **local copies** `st` and `game_timing` taken right after the acquire (a first draft used a
  reference into the slot, which would have raced with the game thread re-recording the slot;
  present in commit 59c3db6 and fixed in the following commit).
* `CommandBuffer::m_tex_buffer`: heap, per slot, freed in `~CommandBuffer`.
* `d2::*` engine pointers: static addresses into Game.exe, read on G only (after this change, except
  at shutdown in the `captureSettings` fallback).
