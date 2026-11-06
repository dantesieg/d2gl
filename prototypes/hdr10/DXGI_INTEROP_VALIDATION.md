# DXGI and interop validation procedures (real hardware)

Nothing in this file has been run yet. The build host had no Windows machine, no GPU and no HDR display.
What actually ran is recorded in `HDR_TEST_RESULTS.md`. Every run writes `hdr10_log.txt` next to the
working directory. Each procedure below states its expected log lines. Record results in
`HDR_TEST_RESULTS.md`, together with the GPU, driver version, Windows build and display model.

## Tools
- **Windows Settings → System → Display → HDR**: "Use HDR" toggle, plus *SDR content brightness* (the
  OS paper white).
- **Windows HDR Calibration app**: tells you what the display really clips at, which you then use as
  `peak_nits`.
- **DXGI/Display state**: `dxdiag` (Display tab, "HDR: Supported/Enabled"), and the prototype's panel.
- **PIX on Windows** (GPU capture) or **RenderDoc** (D3D11 capture; RenderDoc also hooks GL but cannot see
  the interop handoff as one frame): capture a frame, then inspect the swapchain back buffer
  (R10G10B10A2_UNORM). Pick pixels and read the raw 10-bit codes.
- **Colorimeter** (i1Display Pro/Calibrite, with HCFR or DisplayCAL in HDR mode) for luminance. Without
  a colorimeter, **no luminance-accuracy claim may be made**.
- NVIDIA: `nvidia-smi` and the driver version. AMD: Adrenalin version. Intel: the Arc/IGCL driver version.

## Expected PQ codes (10-bit, full range, defaults sdr_white=203, peak=1000, BT.2390)
| patch | input signal | nits | PQ | 10-bit |
|---|---|---|---|---|
| black | 0 | 0 | 0 | 0 |
| SDR white (paper white) | 1.0 | 203 | 0.5806 | 594 |
| mid grey | 0.5 | 43.45 | 0.4589 | ~469 |
| SDR white @ sdr_white=100 | 1.0 | 100 | 0.5081 | 520 |
| 10x highlight ramp end, clip | – | 1000 | 0.7518 | 769 |
Rule: the left half (SDR reference) must never exceed the paper-white code. When
`highlight=0`, the right half must equal the left half for every input ≤ 1.0. The test-scene rows that
satisfy this are the ColorChecker, grey and near-black rows.

## P1 HDR display, Windows HDR ON
1. Turn HDR on and set SDR content brightness to any value. Run `hdr10_proto.exe`.
2. Log: `OS HDR enabled=1`, `HDR-capable=yes`,
   `CheckColorSpaceSupport(G2084_P2020) flags=0x1 (PRESENT=1)` (or 0x3),
   `swapchain format=R10G10B10A2_UNORM colorspace=RGB_FULL_G2084_NONE_P2020 (HDR10) HDR10 presenting=1 metadata=1`,
   and `render adapter ... selected by GL_DEVICE_LUID_EXT match` on NVIDIA/AMD drivers that expose it.
3. Panel: `PRESENTING HDR10: YES (METADATA SET)`. `INTEROP: WGL_NV_DX_interop2 active`.
4. PIX/RenderDoc: back-buffer codes match the table above within ±1.
5. Visually: the left and right halves are identical except in the highlight-ramp and emissive rows. The
   ramp continues past white on the right only. Neutral greys show no tint.
6. Colorimeter: measure the SDR white card. It must read sdr_white ±(display tolerance). Measure the
   highlight ramp at 1000 nits.
7. Press Up/Down. The white changes in steps of 10 nits and the log shows the config.
8. Press H. The log shows a switch to `R8G8B8A8_UNORM ... G22_NONE_P709 ... refusal: HDR disabled by user`,
   and the image should look like an SDR app under Windows HDR (DWM maps it to the OS SDR brightness).
   Press H again to return to HDR10.

## P2 HDR display, Windows HDR OFF
Log: `OS HDR enabled=0 | HDR-capable=yes`, `refusal: OS HDR off / output not HDR10`, format R8G8B8A8,
SDR output. Toggle Windows HDR **while the app runs**. Expect `event: WM_DISPLAYCHANGE` and/or
`dxgi: factory not current -> recreating`, then the format switches to R10G10B10A2 within about 0.5 s. If
nothing happens, that is a bug: record it, because some drivers send no WM_DISPLAYCHANGE for an HDR
toggle. A fallback poll on `IDXGIFactory::IsCurrent` is the fix in that case.

## P3 SDR-only display
Log: `HDR-capable=no`, `OS HDR enabled=0`, SDR swapchain. The panel shows all three statuses as NO. The
image must match the `--no_interop` WGL run pixel for pixel (compare screenshots).

## P4 No interop
- Force it with `--no_interop`. Log: `present: FALLBACK -> SDR WGL SwapBuffers: disabled (--no_interop) (HDR refused; no CPU copy path used)`.
- Real cases: Intel drivers without `WGL_NV_DX_interop2`, remote desktop, and the Microsoft Basic Render
  Driver. Expect `UNAVAILABLE: WGL_NV_DX_interop2 not advertised`.
- `--no_interop --cpu_copy` must show `CPU READBACK COPY (NOT ZERO-COPY)` and may present HDR10. Measure
  the frame time and compare it with interop.

## P5 Driver matrix
Run P1 on NVIDIA (≥ 470 recommended), AMD (Adrenalin ≥ 22.x; interop2 is supported but historically
fragile), Intel Xe/Arc, and hybrid laptops in both iGPU-display/dGPU-render configurations. On hybrids,
check `display: ... on '<adapter>'` (output adapter) against `render adapter`, and check whether DWM's
cross-adapter copy keeps 10-bit output.

## P6 Resize
Drag the window edges continuously for 10 s, then maximise and restore. Expect repeated
`resize: WxH` lines, no `[HR-FAIL]`, no interop errors, and the image stays 1:1 sharp. Minimise and
restore. A 0x0 client area must be skipped (no resize to 0).

## P7 Alt-Tab and fullscreen
F11 and Alt+Enter: `window: borderless fullscreen ON`. Then Alt-Tab away and back 20 times. Expect
`WM_ACTIVATEAPP` lines and no recreation. HDR stays on. Check with PresentMon that independent flip is
used ("Hardware: Independent Flip"/"Composed: Flip").

## P8 Display reconnect and monitor move
Unplug and replug the HDR display (or power-cycle it) while the app runs. Move the window between an HDR
and an SDR monitor. Expect a factory recreate and the format following the monitor's state. The swapchain
survives (no device loss). On hybrid systems a move across adapters can cause a DXGI device removal;
then expect P9's log.

## P9 Device loss
- Real: driver update or restart (`Win+Ctrl+Shift+B` resets the graphics stack), or
  `dxcap -forcetdr` in an admin prompt.
- Expect `Present ... DXGI_ERROR_DEVICE_REMOVED`, `GetDeviceRemovedReason`,
  `reinit: device lost / interop lost`, and a full init log again. After 3 consecutive failures:
  `fatal: repeated failure, giving up on interop` and the SDR WGL path. Note that a TDR usually also loses
  the GL context. The prototype does not recreate the GL context. **Gap**: D2GL integration needs a GL
  context-loss policy as well.
- Automated approximation: `--simulate_loss` runs the same teardown and re-init code without a real
  removal.

## P10 Repeated init/shutdown
`--cycles=50`. Expect 50 × `cycle n: 3 frames rendered`, then `exit rc=0`. Check that the
Task Manager handle and GPU memory counts are flat. Repeat with `--cpu_copy` and `--no_interop`.

## P11 Shader and encode validation on the real driver
`--shader_selftest` (D3DCompile with the real d3dcompiler_47) and `--encode_selftest` (GPU HLSL vs the
CPU reference, 8 parameter sets × 64 pixels, tolerance ±1 10-bit code). Both must exit 0.
