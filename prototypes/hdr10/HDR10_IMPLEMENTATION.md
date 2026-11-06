# HDR10 prototype: implementation

A standalone Windows x86 prototype that uses neither Diablo II nor the D2GL renderer.
Pipeline: **OpenGL renders → shared D3D11 texture (WGL_NV_DX_interop2) → D3D11 encode pass → DXGI
flip-model swapchain (R10G10B10A2_UNORM + RGB_FULL_G2084_NONE_P2020)**. There is an SDR fallback.

## Build
```
cd prototypes/hdr10
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8          # build/hdr10_proto.exe, build/test_color.exe (i686, static, stripped)
c++ -std=c++17 -Wall -Wextra -Wshadow -Wconversion -Werror -Isrc tests/test_color.cpp -o /tmp/test_color  # native macOS
./run_wine_tests.sh              # Wine runs (needs docker images d2m-wine and d2m-wine-x)
docker build --platform linux/amd64 -t d2m-wine-x -f docker/Dockerfile.wine-x docker   # once
```
The build uses `-Wall -Wextra -Wshadow -Wconversion -Werror` and finishes with 0 warnings. A copy of the exes
is in `dist/` (each well under 2 MB). There are no third-party dependencies: only the Win32, GL 1.1, WGL,
D3D11 and DXGI headers and libraries from mingw-w64, plus a hand-written loader (`gl_loader.*`) and bitmap
font (`font.*`).

**Shaders.** The HLSL is compiled **at runtime** with `D3DCompile`, loaded through
`LoadLibrary("d3dcompiler_47.dll")` (present on Windows 8.1 and later, and redistributable). It is not
precompiled, because there is no fxc/dxc on the build host. The HLSL avoids ternaries and user functions
(it uses macros and if-blocks), so Wine 8's vkd3d HLSL compiler also accepts it. That makes the compile
step automatically testable (`--shader_selftest`). The GLSL is 330 core, compiled by the GL driver.

## Source map
| file | role |
|---|---|
| `src/color_math.h` | portable CPU reference of the colour pipeline (tested) |
| `src/hdr_shader.h` | HLSL encode pass (transcribes color_math.h) |
| `src/gl_loader.*` | GL core and WGL entry points, extension-string checks, interop function pointers |
| `src/interop.*` | WGL_NV_DX_interop2 open/register/lock/unlock/close |
| `src/d3d_present.*` | adapter selection, D3D11 device, DXGI flip swapchain, output detection, HDR10 colour space and metadata, present, device-loss detection |
| `src/scene.*` | GL test scene and panel overlay, SDR blit |
| `src/font.*` | 5x7 bitmap font rasterised on the CPU into the panel texture |
| `src/config.*` | `hdr10.ini` (section `[hdr10]`, next to the exe) plus `--key=value` overrides |
| `src/main.cpp` | window, GL context, path selection, frame loop, resize/display events, hotkeys |

## Design
- **The GL context lives on a hidden window.** The visible window never gets a GL pixel format, so it can
  host a DXGI flip-model swapchain. In the WGL fallback, the context is made current on the visible window
  (same pixel format). A window that has already hosted a DXGI swapchain is destroyed and recreated before
  GL presents to it.
- **Adapter selection.** If GL exposes `GL_EXT_memory_object` together with `GL_EXT_memory_object_win32`
  (or `GL_EXT_semaphore_win32`), `glGetUnsignedBytevEXT(GL_DEVICE_LUID_EXT)` gives the LUID, and the DXGI
  adapter with the same `AdapterLuid` is used. Otherwise a **heuristic** applies: `GL_VENDOR` is mapped to a
  PCI vendor id (NVIDIA 0x10DE, AMD 0x1002, Intel 0x8086) and the first non-software DXGI adapter with that
  id is taken. If nothing matches, adapter 0 is used. The heuristic fails on systems with two GPUs from the
  same vendor. In that case `wglDXOpenDeviceNV` or `wglDXRegisterObjectNV` fails and the SDR fallback is
  taken. The display output is searched across **all** adapters, because on hybrid laptops the monitor
  can belong to a different adapter.
- **Interop availability** requires all of the following: a WGL extension string, `WGL_NV_DX_interop2` in it
  (v1 alone is D3D9-only and is rejected), and non-null `wglDXOpenDeviceNV`, `CloseDevice`,
  `RegisterObject`, `UnregisterObject`, `LockObjects` and `UnlockObjects`. The extension string is queried
  under the core context.
- **Shared resource.** An `R16G16B16A16_FLOAT` D3D11 texture (RTV|SRV), registered as `GL_TEXTURE_2D`
  with `WGL_ACCESS_WRITE_DISCARD_NV` and attached to a GL FBO. Each frame:
  `wglDXLockObjectsNV` → GL renders (upside-down, so D3D sees row 0 at the top and no copy is needed) →
  `wglDXUnlockObjectsNV` (the driver orders GL writes before subsequent D3D use) → D3D samples it
  1:1 with point sampling → `Present(1,0)`. The GL→D3D handoff is zero-copy.
- **Ownership on resize.** Unregister → release SRV/texture → `ResizeBuffers` → create texture → register.
  The FBO is attached while the object is locked.
- **Device loss.** `Present` returning `DXGI_ERROR_DEVICE_REMOVED` or `_RESET` (with `GetDeviceRemovedReason`
  logged), or a failure in `wglDXLockObjectsNV`, `Unlock` or `Register`, marks the path lost. A full teardown
  (interop close first, then D3D) and re-init follows. After 3 consecutive failures the app falls back to
  WGL SDR.
- **Shutdown order:** unlock → `wglDXUnregisterObjectNV` → GL FBO/texture delete → `wglDXCloseDeviceNV` →
  D3D `ClearState`/`Flush` → release swapchain/device/factory → GL context → windows.
  `--cycles=N` exercises this N times.
- **HDR decision.** HDR10 is used only if all of these hold: `hdr=1`, the output's
  `IDXGIOutput6::GetDesc1().ColorSpace == G2084_NONE_P2020` (Windows HDR on for that monitor), the swapchain
  is `IDXGISwapChain3`, `CheckColorSpaceSupport(G2084_P2020)` has the PRESENT flag, and `SetColorSpace1`
  succeeds. Otherwise the buffers are switched to R8G8B8A8 with G22_P709 and the refusal reason is logged
  and shown. HDR10 static metadata (`SetHDRMetaData`, `DXGI_HDR_METADATA_TYPE_HDR10`) is set from the
  output's luminance and the Rec.2020 primaries.
- **Three separate statuses** (log and panel):
  1. *OS HDR enabled*: Desc1 colour space is G2084 (cross-checked against DisplayConfig
     `advancedColorEnabled`).
  2. *HDR-capable hardware*: DisplayConfig `DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO.advancedColorSupported`,
     or "unknown" if the query fails.
  3. *Presenting HDR10*: `Presenter::hdr_presenting`.
- **Display changes.** `WM_DISPLAYCHANGE` and `WM_MOVE` (debounced to 500 ms), plus the H hotkey, call
  `reconfigure()`. That recreates the DXGI factory if `IsCurrent()` is false, re-detects the output and
  re-applies the format and colour space. Pressing H therefore switches HDR10↔SDR live through
  `ResizeBuffers` with the new format.
- **Borderless fullscreen:** F11 or Alt+Enter (DXGI's own Alt+Enter is disabled through
  `MakeWindowAssociation`). The window becomes WS_POPUP covering the monitor, and the flip swapchain can
  then use independent flip. Exclusive fullscreen is deliberately not used.
- **HRESULTs.** Every D3D/DXGI setup call goes through `HR()`, which logs success and failure with the
  expression, file and line. Per-frame `Present` and `Map` log only on failure, so the log is not flooded.
- **Fallbacks:**
  - (a) Default: interop unavailable → **HDR is refused**, and output goes through SDR WGL `SwapBuffers`
    from an RGBA16F FBO. A diagnostics-only DXGI factory still reports the display state.
  - (b) Opt-in `--cpu_copy`: a **CPU READBACK COPY (NOT ZERO-COPY)**. `glReadPixels(GL_HALF_FLOAT)` →
    `UpdateSubresource` → the same D3D11/DXGI path. It is labelled as such everywhere. It exists for
    validation on systems without interop (for example Wine) and is a stall per frame.
- **Threading:** one render thread owns the GL context, the interop handles and the D3D immediate context.
  This matches D2GL's dedicated render thread. The window procedure only sets flags.

## Integration notes for D2GL (not done here; names checked against base a486557)
- `d2gl/src/graphic/context.cpp`, `Context::Context` (around line 79,
  `wglCreateContextAttribsARB(App.hdc, ...)`): when HDR is on, create the context on a hidden helper
  window's DC instead of the game window's `App.hdc`. The game window then stays free for the DXGI
  swapchain. Then create `Presenter` and `Interop` on the render thread (`Context::renderThread`, line
  409). Every GL and D3D call must stay on that thread.
- `Context::renderThread`, the present block (around line 697: fence → `option::Menu::instance().draw()`
  → `SwapBuffers(App.hdc)`): bind the interop FBO, lock it before the final composite pass that today
  targets the default framebuffer (`Context::bindDefaultFrameBuffer`, line 1030), and draw the ImGui menu
  into it as well. Then unlock and call `Presenter::draw_and_present` instead of `SwapBuffers`. The existing
  fence and `glClientWaitSync` become redundant with lock/unlock but are harmless.
- `Context::onResize` (line 742): add the resize sequence (unregister → `ResizeBuffers` → recreate →
  register).
- `d2gl/src/win32.cpp`: forward `WM_DISPLAYCHANGE` and the monitor change on move to the render thread as
  a command (like the existing resize command).
- `d2gl/src/option/ini.cpp` and `menu.cpp`: add the keys `hdr`, `sdr_white`, `peak_nits`, `highlight`,
  `tonemap` and `transfer`, and menu controls for them.
- New files: `d2gl/src/graphic/hdr/` holding `color_math.h`, `hdr_shader.h`, `interop.*` and
  `d3d_present.*` as they are here. Link `d3d11` and `dxgi`, and load `d3dcompiler_47` at runtime.
- RGBA8 gamma-space blending stays as it is. Encoding happens only after the final SDR composite, so the
  art is unchanged. If interop is missing, D2GL keeps its current `SwapBuffers` path unchanged.
