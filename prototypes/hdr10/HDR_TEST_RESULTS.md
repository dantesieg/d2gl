# HDR test results (real outputs only)

Host: macOS (Apple Silicon), 2026-11-06. **No Windows machine, no GPU, no HDR display.**
- **Not verified:** HDR10 presentation, interop, luminance accuracy, and the visual result on any real
  display. Nothing here shows that HDR works.
- Raw logs: `dist/wine_logs/`. The `log_*.txt` files are the app logs of each run, and `suite.txt` holds the
  exit codes.
- The binaries in `dist/` were rebuilt after the suite with two cosmetic scene changes (tick width, panel
  scale) and re-run for the `cpu_copy` screenshot. The colour, D3D and interop code is identical to what
  the suite ran.

## 1. Build
`cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release && cmake --build build`
→ `hdr10_proto.exe` (PE32 i686, 272 KB stripped, static libstdc++) and `test_color.exe`. The build gives
0 warnings with `-Wall -Wextra -Wshadow -Wconversion -Werror`.

## 2. Colour-math unit tests
| run | result |
|---|---|
| native macOS (Apple clang 21, same warning flags) | **37 passed, 0 failed**, rc 0 |
| `test_color.exe` under Wine 8.0 (`d2m-wine`) | **37 passed, 0 failed**, rc 0 |

Covered: PQ(0/0.1/100/203/1000/4000/10000 nits) = 0 / 0.0623 / **0.5081** / 0.5806 / **0.7518** / 0.9026 / 1.0
(tolerance 1e-4); a 10-bit code of 520 at 100 nits; a PQ round trip with worst relative error ≤ 2e-3 over
0.01..10000 nits; an sRGB round trip with worst error ≤ 1e-5; sRGB piecewise knee continuity, plus
sRGB ≠ gamma 2.2 near black; each 709→2020 matrix row sums to 1, white luminance is 1, red luminance is
0.2126; neutral greys stay neutral through the full pipeline (deviation ≤ 2e-5 PQ), including with
expansion and the EETF; SDR white → 203 nits and → 80 nits; mid grey → 43.45 nits; expansion leaves values
below the knee untouched and boosts white when on; EETF(src peak) = dst peak, identity below the knee,
identity when dst ≥ src, monotonic, max ≤ peak; clip to peak; premultiplied (0.25, a = .5) equals straight
0.5; a = 0 → black; expansion keeps the R:G ratio (no saturation boost).

## 3. Shader compile and GPU-vs-CPU encode (Wine D3D11 = wined3d on Mesa llvmpipe)
- `--shader_selftest` (d2m-wine, D3DCompile from Wine's d3dcompiler_47/vkd3d-shader): vs_4_0 OK (1152 B),
  ps_4_0 OK (11608 B), rc 0. The first version of the HLSL used ternaries and helper functions, which Wine
  8's compiler rejects (`E5017 not yet implemented`). It was rewritten using macros and if-blocks.
- `--encode_selftest` (d2m-wine-x): the real HLSL rendered into an R10G10B10A2 offscreen target and was
  compared per pixel against `color_math.h`. **8 parameter sets × 64 px: all PASS, max |gpu−ref| = 1 10-bit
  code** (rounding), 0 mismatches > 1, rc 0. The sets were: default, clip, gamma 2.2, highlight 1.0,
  highlight .5 with peak 600, premultiplied, 100/400, and 300/4000 clip.
- The same test in `d2m-wine` without X gives `CreateDXGIFactory1 → DXGI_ERROR_UNSUPPORTED`, rc 5. This is
  expected, because wined3d needs a GL display.
- This validates the shader math on a software D3D11 implementation. It does **not** validate NVIDIA, AMD
  or Intel shader compilers, or the real d3dcompiler_47.

## 4. Application runs under Wine 8.0 + Xvfb 1280x800 + Mesa 22.3.6 llvmpipe (`d2m-wine-x`)
GL: `llvmpipe (LLVM 15.0.6, 256 bits)`, core 4.5 context. WGL extension string present (467 bytes).

| run | rc | observed |
|---|---|---|
| default `--frames=60` | 0 | `WGL_NV_DX_interop=0 WGL_NV_DX_interop2=0`, all wglDX* entry points NULL → `FALLBACK -> SDR WGL SwapBuffers: UNAVAILABLE: WGL_NV_DX_interop2 not advertised (HDR refused; no CPU copy path used)`, 60 frames |
| `--no_interop --frames=10` | 0 | `FALLBACK -> SDR WGL SwapBuffers: disabled (--no_interop)`, 10 frames |
| `--cpu_copy --frames=60` | 0 | D3D11 FL 11_0 on the adapter Wine fakes, "NVIDIA GeForce GTX 470" (picked as the default adapter: GL_VENDOR "Mesa/X.org" has no vendor match and there is no GL LUID because `GL_EXT_memory_object_win32` is absent); IDXGIOutput6 found and GetDesc1 OK: `RGB_FULL_G22_NONE_P709, lum 0/0/0`; DisplayConfig advanced colour info **failed 87** → HDR-capable = unknown; `QueryInterface(IDXGISwapChain3/4) → E_NOINTERFACE`; swapchain `R8G8B8A8_UNORM FLIP_DISCARD`, `refusal: OS HDR off / output not HDR10`; path labelled `CPU READBACK COPY (NOT ZERO-COPY)`; 60 frames |
| `--cpu_copy --hdr=0` | 0 | refusal `HDR disabled by user`, 30 frames |
| `--cpu_copy --resize_test` | 0 | resize to 792x573 client, borderless ON → 1280x800, OFF → 792x573, `ResizeBuffers` S_OK each time, 45 frames |
| `--resize_test` (WGL) | 0 | same resize sequence on the WGL path, 45 frames |
| `--cpu_copy --simulate_loss` | 0 | the full teardown and re-init path ran once (simulated, **not** a real device removal), 40 frames |
| `--cpu_copy --cycles=10` | 0 | 10 × init → 3 frames → shutdown |
| `--cycles=10` (WGL) | 0 | 10 × init → 3 frames → shutdown |

Across all runs the only `[HR-FAIL]` entries were the expected `IDXGISwapChain3`/`IDXGISwapChain4`
`E_NOINTERFACE` on Wine (15 of each) and the no-X `CreateDXGIFactory1` failure.

The three statuses as Wine reports them:
- OS HDR enabled: **NO** (Desc1 = G22_P709).
- HDR-capable hardware: **UNKNOWN** (the DisplayConfig query fails with 87).
- Presenting HDR10: **NO**.

Screenshot `dist/wine_cpu_copy_gl_signal.png`: the GL-rendered SDR signal (the FBO before D3D, clamped to 8
bit), with the patches, gradients, ramps, emissive and bloom areas, and the diagnostics panel all rendering
correctly. It is **not** the swapchain output and not an HDR capture.

## 5. Not run (needs Windows + GPU + HDR display; see DXGI_INTEROP_VALIDATION.md)
WGL_NV_DX_interop2 on a real driver; GL_DEVICE_LUID_EXT matching; R10G10B10A2 + G2084 presentation;
SetHDRMetaData; PQ code capture with PIX or RenderDoc; colorimeter luminance; Windows HDR on/off toggle at
runtime; SDR display; Alt-Tab and independent flip; display reconnect; real device removal (TDR); and
driver differences between NVIDIA, AMD and Intel.
