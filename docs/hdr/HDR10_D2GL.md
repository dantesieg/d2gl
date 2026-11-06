# HDR10 output in D2GL

Branch `feature/hdr10-d2gl`, built on `feature/r0-r2-threading` (8c7d13b). The design and colour math come
from `prototypes/hdr10` (branch `feature/hdr10-prototype`). On the Steam Deck OLED in Game Mode, that
prototype has presented HDR10 through Proton/DXVK.

## What `hdr=0` does (default)
Nothing changes. The GL context is still created on `App.hdc`, `FrameBuffer::unBind` still binds 0,
`SwapBuffers(App.hdc)` is still called, and d3d11.dll/dxgi.dll are never loaded. The only code differences
on this path are:
- the default framebuffer id lives in a variable that stays 0;
- `SwapBuffers` takes `m_gl_hdc`, which equals `App.hdc`.

## What `hdr=1` does
1. **Process start** (`app.cpp`, right after `loadIni`): `App.hdr.mode = true`, and
   `SetEnvironmentVariableA("DXVK_HDR", "1")` unless the variable is already set.
2. **`Context::Context`:** the GL context is created on a hidden 1x1 `CS_OWNDC` window, so `App.hwnd`
   never gets a GL pixel format and DXVK/DXGI can own it.
3. **Render thread start:** `hdr::Presenter::init`:
   - `LoadLibrary` d3d11/dxgi;
   - pick the adapter by GL LUID, else by `GL_VENDOR` heuristic, else adapter 0;
   - compile the HLSL with `D3DCompile` (d3dcompiler_47);
   - create a flip-discard swapchain on `App.hwnd`, with DXGI's window-change and Alt+Enter handling
     disabled (D2GL owns the window);
   - create the target.
   The target is either:
   - **ZERO-COPY:** `WGL_NV_DX_interop2`, where GL renders straight into a registered D3D11 RGBA8 texture.
   - **CPU READBACK COPY** (Proton/Deck, where interop2 does not exist): a GL RGBA8 texture and FBO, a
     3-PBO ring, and a dynamic D3D11 texture.

   The target FBO becomes the renderer's "default framebuffer" (`FrameBuffer::setDefault`). Every pass
   that used to end on FBO 0 now ends on the target, and so does the ImGui menu (`FrameBuffer::unBind()`
   runs before `Menu::draw`). Screenshots read the bound framebuffer, so they stay SDR.
4. **Each frame, `beginFrame`:**
   - resize on window-size change (unregister/release, `ResizeBuffers`, recreate);
   - check display changes (`WM_DISPLAYCHANGE` → `hdr::g_display_changed`, plus `IDXGIFactory::IsCurrent`
     every 120 frames, because gamescope may toggle HDR without the message);
   - live switch when `hdr` is toggled in the menu;
   - lock the interop object.
5. **At the old `SwapBuffers` point, `present(App.vsync)`:**
   - Copy path: `glReadPixels` into PBO[head] and fence it, then consume the readback queued one frame
     earlier. The fence is polled non-blocking first; only if it is not yet signalled is there a wait of at
     most 100 ms on that single readback. Then map the PBO into the dynamic texture.
   - Interop path: unlock.
   - Then: HDR10 metadata (when HDR and the peak changed), the encode draw, and `Present(vsync ? 1 : 0)`.
   The D2GL limiter and frame pacing still run after it, unchanged.
   - **Latency:** the copy path shows the previous frame, so **+1 frame**. The interop path adds no latency.
6. **Swapchain format:**
   - R10G10B10A2 + G2084/P2020 when `hdr` is on, the output reports the G2084 colour space, the swapchain
     is `IDXGISwapChain3`, and `CheckColorSpaceSupport` has the PRESENT flag.
   - Otherwise R8G8B8A8 + G22, presenting the SDR signal unchanged (for example KDE desktop mode, where
     the check returns 0). The menu shows why.
7. **Failures:** device removed or reset, a failed `Present`, a lock failure, or a failed resize triggers
   a full presenter re-init, at most 3 times (the counter is forgiven after 600 good frames). After that,
   `Context::hdrFallbackToWgl` runs: it shuts the presenter down, restores the default framebuffer to 0,
   calls `SetPixelFormat` + `wglMakeCurrent` on `App.hdc`, and the normal SDR `SwapBuffers` path follows.
   The GL context is never lost by this.

## Settings ([HDR] in d2gl.ini; HDR tab in the menu, all live except `hdr` itself)
| key | default | range | notes |
|---|---|---|---|
| hdr | false | bool | restart needed to switch the HDR path on or off. While it is on, unticking it presents SDR live. |
| sdr_white | 200 | 80–500 nits | paper white. UI and world share it. A separate `ui_white` is **not** implemented: the final composite has no UI mask. |
| peak_nits | 0 (auto) | 400–2000 | auto = DXGI MaxLuminance clamped (Deck OLED reports 1015) |
| brightness | 0.0 | −2..2 stops | linear gain |
| black_level | 0.0 | −0.05..0.05 | signal domain: + lifts, − crushes. White is fixed. Monotonic. |
| contrast | 1.0 | 0.8–1.2 | luminance power around 0.18 linear, hue-preserving |
| saturation | 1.0 | 0.5–1.5 | linear Rec.709, luminance-preserving, before the 2020 matrix |
| highlight | 0.0 | 0–1 | expansion above the knee only |
| highlight_knee | 0.75 | 0.5–0.95 | |
| tonemap | 1 | 0 clip, 1 BT.2390 | |
| eotf | 0 | 0 gamma 2.2, 1 gamma 2.4, 2 sRGB piecewise | sRGB lifts near-black more than 2× vs 2.2 at 3 % signal (tested). That lift is the likely cause of the "washed out" look. |
| (menu only) calibration overlay | off | | patches: paper white (signal 1.0), peak (direct `peak` nits), and near-black at 0/1/2/3/5 % through the full pipeline |

Order in the shader: black level → EOTF → saturation → contrast → brightness → highlight expansion →
×paper white → Rec.709→2020 → tonemap → PQ. With HDR off (SDR swapchain) none of these apply.

## Steam Deck
- **In-process `DXVK_HDR=1`:** expected to work. DXVK is a PE build under Proton and reads the variable
  with `GetEnvironmentVariable` when the DXGI factory is created. D2GL sets it before d3d11/dxgi are loaded
  (they are loaded on demand at render-thread start). **This has not been verified on the Deck.** The log
  line `HDR: DXVK_HDR=1 set in-process` only proves that D2GL set it.
- **What D2GL cannot set:** Linux-side variables read by the host Vulkan loader or gamescope WSI layer
  (`ENABLE_HDR_WSI`, `ENABLE_GAMESCOPE_WSI`, `GAMESCOPE_WAYLAND_DISPLAY`). Win32 environment changes do
  not reach the Unix `environ`. Steam's Game Mode session provides the gamescope ones.
- **Fallback launch options:** `DXVK_HDR=1 ENABLE_HDR_WSI=1 %command%`.
- The Deck uses the CPU READBACK COPY path (no interop2).

## Tests
- `tests/hdr/run_tests.sh` runs:
  - `test_hdr_color`: 53 checks, native and under Wine;
  - `test_hdr_gpu`: the real HLSL on wined3d into R10G10B10A2 versus the CPU reference, 13 parameter sets
    including every new control and flip_y, at most 1 10-bit code apart.
- CI: windows-2022 msbuild, run 42260323466 on 53cb7ff: success, 38 warnings (the deck baseline), 0 errors.

## Not verified
- Anything in game: the Deck, Windows HDR, the interop path on NVIDIA/AMD, the frame time of the copy
  path, menu layout, live toggling, resize and fullscreen switches, device loss, the fallback to WGL after a
  swapchain existed, and whether in-process DXVK_HDR suffices.
- Luminance accuracy (no colorimeter).

## Known risks
- **SetPixelFormat on a window that already hosted a DXVK swapchain** (the fallback) may fail under
  Proton. The log says so, and the game would then keep presenting nothing new until restart.
- **+1 frame latency** on the copy path, plus a bounded wait of up to 100 ms if the GPU is behind.
- **ImGui input** keeps using `App.hwnd`. GL never touches that window in HDR mode, which should be fine
  but is untested.
- **The `gpu_sync` / `frame_latency` interaction** with the PBO ring is unmeasured.
- **The HLSL avoids** ternaries, helper functions, `frac` and `sign` so that Wine 8's vkd3d accepts it.
  Proton 11's vkd3d is newer.
