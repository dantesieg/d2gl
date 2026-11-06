# HDR compatibility matrix

Legend: **Verified** = run and observed on this host (see HDR_TEST_RESULTS.md). **Expected** = derived from
code, documentation or vendor documentation; not run. Nothing on real Windows or GPU hardware has been
verified.

| environment | interop2 | path chosen | swapchain | HDR10 | status |
|---|---|---|---|---|---|
| Wine 8.0 + Xvfb + Mesa llvmpipe (default) | not advertised | SDR WGL SwapBuffers | WGL default FB | refused (no interop) | **Verified** |
| Wine 8.0 + Xvfb, `--cpu_copy` | not advertised | CPU READBACK COPY → D3D11 (wined3d) | R8G8B8A8 FLIP_DISCARD | refused: OS HDR off / output not HDR10 (Wine reports G22_P709; also no IDXGISwapChain3) | **Verified** |
| Wine 8.0 without X (`d2m-wine`) | – | GUI cannot start; `--shader_selftest` works | – | – | **Verified** (selftest only) |
| Windows 10 1709+/11, NVIDIA, HDR on | expected yes | interop → D3D11 | R10G10B10A2 + G2084 | expected yes | Expected |
| Windows 10/11, NVIDIA, HDR off | expected yes | interop → D3D11 | R8G8B8A8 + G22 | refused (OS HDR off) | Expected |
| Windows 10/11, AMD | expected yes (driver-dependent) | interop → D3D11 | as above | expected yes | Expected; check P5 |
| Windows 10/11, Intel Xe/Arc | varies by driver | interop or WGL SDR | – | – | Expected; check P5 |
| Hybrid laptop (iGPU display, dGPU GL) | yes on the dGPU | interop; output on the other adapter | R10G10B10A2 | depends on DWM cross-adapter path | Expected; check P5/P8 |
| SDR-only display | – | interop → D3D11 | R8G8B8A8 + G22 | refused (HDR-capable=no) | Expected |
| RDP / Basic Render Driver | no | WGL SDR (GDI-generic GL 1.1 → no core context → fatal) | – | – | Expected: **the app exits rc=2 if no GL 3.3 core context exists** |
| Windows 7/8.1 | – | no IDXGIFactory2 (7) / no IDXGIOutput6 | FLIP_SEQUENTIAL fallback coded | refused | Expected |
| Proton / Steam Deck | DXVK and vkd3d: no WGL interop2 (wined3d GL) | WGL SDR | – | refused | Expected. HDR on Deck would need a Vulkan/DXVK path instead; out of scope |

## Feature support per API (requirements in the code)
| feature | minimum |
|---|---|
| WGL_NV_DX_interop2 with D3D11 | NVIDIA ≥ 2012-era drivers; AMD (supported in modern Adrenalin) |
| `IDXGIFactory2::CreateSwapChainForHwnd` and FLIP_SEQUENTIAL | Windows 8 |
| FLIP_DISCARD | Windows 10 |
| `IDXGISwapChain3::SetColorSpace1` and `CheckColorSpaceSupport` | Windows 10 |
| `IDXGISwapChain4::SetHDRMetaData` | Windows 10 1703 |
| `IDXGIOutput6::GetDesc1` | Windows 10 1703 |
| `DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO` | Windows 10 1709 (fails with 87 on Wine) |
| `GL_DEVICE_LUID_EXT` | GL_EXT_memory_object_win32 / semaphore_win32 (NVIDIA, AMD; Mesa on Windows varies) |
| d3dcompiler_47.dll | Windows 8.1+ in-box; redistributable for Windows 7 |
