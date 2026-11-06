# R1 changelog

- New ini flag `[Other] early_slot_release` (default `false` = unchanged behavior).
  When `true`, the render thread releases `m_semaphore_gpu[slot]` right after the last draw of the
  slot, before the per-frame GPU fence wait, instead of after it.
- Files: `d2gl/src/graphic/context.cpp`, `d2gl/src/app.h`, `d2gl/src/option/ini.cpp`.
- No HDR files, shaders, simulation timing, or limiter logic changed. `gpu_sync` (R2) unchanged.
- Not runtime-validated (CI build only).
