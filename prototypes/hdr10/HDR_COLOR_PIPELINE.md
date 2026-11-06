# HDR colour pipeline

Reference implementation: `src/color_math.h` (`hdr::encode_pixel`). The HLSL in `src/hdr_shader.h` is a
transcription of it. Unit tests: `tests/test_color.cpp`.

## Input contract
The final D2GL image: an **SDR gamma-encoded signal** (what an SDR monitor would receive), stored in an
RGBA16F texture. In SDR art, values are in [0,1]. Values above 1.0 are allowed only where something
deliberately emits more than SDR white (the prototype's highlight ramps and emissive demo). D2GL itself
renders RGBA8 today, so in a first integration every pixel is in [0,1].

## Per-pixel steps (HDR10 output)
1. **Alpha.** `alpha=straight` (default): RGB is used as is. `alpha=premultiplied`: `rgb /= a` (a = 0 gives
   black) **before** any transfer function. Un-premultiplying after the EOTF would be wrong, because
   `eotf(a*c) != a*eotf(c)`. The output is opaque (alpha 1).
2. **EOTF to linear light.** `transfer=srgb` (default): the exact IEC 61966-2-1 piecewise curve
   (linear segment below 0.04045, then ((v+0.055)/1.055)^2.4). `transfer=gamma22`: pure power 2.2, which
   matches many SDR monitors in practice and gives slightly darker near-black. Both are sign-preserving.
   Above 1.0 the power segment simply continues.
3. **Optional highlight expansion** (off by default, `highlight=0`). Y709 is the Rec.709 luminance of the
   linear value. For Y > knee (default 0.75): `gain = 1 + s*(peak/sdr_white - 1)*t^2` with
   `t = saturate((Y-knee)/(1-knee))`. The gain multiplies R, G and B equally, so chromaticity is unchanged
   (no saturation boost) and neutrals stay neutral. At or below the knee it is the identity.
4. **Paper white.** `nits = linear * sdr_white` (default 203 nits, ITU-R BT.2408 reference white).
   SDR 1.0 therefore lands exactly on the paper white.
5. **Rec.709 → Rec.2020** (BT.2087 linear matrix, same D65 white). Each row sums to 1, so neutrals map to
   neutrals. Negative results (input outside the 709 gamut, which is impossible for [0,1] input) are
   clamped to 0.
6. **Tonemap toward the target peak** (`peak_nits`, default 1000):
   - `clip` (none): per-channel clamp to [0, peak].
   - `bt2390` (default): the ITU-R BT.2390 EETF hermite knee in the PQ domain, with source peak
     `src_peak_nits` (default 10000) and black level 0. It is evaluated on max(R,G,B) and applied as a
     common RGB scale, so hue and neutrality are preserved. Below the knee start (KS = 1.5·maxLum − 0.5 in
     normalised PQ) it is the identity. With the defaults it starts at about 400 nits, so the 203-nit paper
     white is never touched.
7. **ST 2084 PQ encode** of absolute nits/10000, written to R10G10B10A2_UNORM with colour space
   `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`.

## SDR output (fallback or HDR off)
The signal is written unchanged (`saturate(rgb)`) to an R8G8B8A8_UNORM swapchain using G22_P709, or through
WGL SwapBuffers. This is bit-identical in intent to today's D2GL output: there is no linearisation round trip.

## Split comparison
With `split=1` the left half of the screen clamps the input to [0,1] and forces expansion off. That half
shows "SDR content as an HDR10 display shows it at paper white". The right half runs the full pipeline. A
mid-grey divider line marks the boundary. The split exists only in HDR10 mode.

## Parameters
| key (ini `[hdr10]` / `--key=`) | default | hotkey |
|---|---|---|
| hdr | 1 | H |
| sdr_white | 203 | Up / Down (±10) |
| peak_nits | 1000 | PgUp / PgDn (±100) |
| src_peak_nits | 10000 | – |
| highlight | 0 | E (0, .25, .5, .75, 1) |
| knee | 0.75 | – |
| tonemap | bt2390 (`clip`/`none`) | T |
| transfer | srgb (`gamma22`) | G |
| alpha | straight (`premultiplied`) | A |
| split | 1 | S |
| fullscreen | 0 | F11 / Alt+Enter |
| no_interop, cpu_copy | 0 | R = full reinit |

Peak luminance is not taken from the display automatically. DXGI's MaxLuminance is shown on the panel
and written into the HDR10 metadata. Using it as the default `peak_nits` is a later integration decision,
because EDID values are often wrong.

## Numeric references (verified by tests)
PQ(100) = 0.5081 (10-bit code 520), PQ(203) = 0.5806, PQ(1000) = 0.7518, PQ(4000) = 0.9026,
PQ(10000) = 1.0. The neutral-grey deviation through the full pipeline is < 2e-5 PQ (< 0.02 of a 10-bit
code). It comes from float rounding of the matrix row sums.
