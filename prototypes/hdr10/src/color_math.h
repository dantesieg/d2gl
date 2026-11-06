// Portable CPU reference of the HDR10 encode pipeline.
// The HLSL in hdr_shader.h is a line-by-line transcription of encode_pixel();
// tests/test_color.cpp validates this file. Keep the two in sync.
#pragma once
#include <algorithm>
#include <cmath>

namespace hdr {

struct float3 { float r, g, b; };

enum class Transfer : int { SRGB_PIECEWISE = 0, GAMMA22 = 1 };
enum class Tonemap : int { CLIP = 0, BT2390 = 1 };
enum class Alpha : int { STRAIGHT = 0, PREMULTIPLIED = 1 };

struct Params {
	float sdr_white_nits = 203.0f;   // paper white: SDR 1.0 maps here (ITU-R BT.2408 reference)
	float peak_nits = 1000.0f;       // display/target peak
	float src_peak_nits = 10000.0f;  // assumed mastering peak for the EETF
	float highlight_strength = 0.0f; // 0 = off (default): SDR art unchanged
	float highlight_knee = 0.75f;    // linear relative luminance where expansion starts
	Transfer transfer = Transfer::SRGB_PIECEWISE;
	Tonemap tonemap = Tonemap::BT2390;
	Alpha alpha = Alpha::STRAIGHT;
};

// ---- sRGB (IEC 61966-2-1), exact piecewise. Extended range: mirrored sign, >1 continues the power segment.
inline float srgb_eotf(float v) {
	float a = std::fabs(v);
	float l = (a <= 0.04045f) ? a / 12.92f : std::pow((a + 0.055f) / 1.055f, 2.4f);
	return std::copysign(l, v);
}
inline float srgb_oetf(float l) {
	float a = std::fabs(l);
	float v = (a <= 0.0031308f) ? a * 12.92f : 1.055f * std::pow(a, 1.0f / 2.4f) - 0.055f;
	return std::copysign(v, l);
}
inline float gamma22_eotf(float v) { return std::copysign(std::pow(std::fabs(v), 2.2f), v); }
inline float gamma22_oetf(float l) { return std::copysign(std::pow(std::fabs(l), 1.0f / 2.2f), l); }

// ---- SMPTE ST 2084 PQ. Input: absolute nits. Output: [0,1] code value.
constexpr float PQ_M1 = 2610.0f / 16384.0f;
constexpr float PQ_M2 = 2523.0f / 4096.0f * 128.0f;
constexpr float PQ_C1 = 3424.0f / 4096.0f;
constexpr float PQ_C2 = 2413.0f / 4096.0f * 32.0f;
constexpr float PQ_C3 = 2392.0f / 4096.0f * 32.0f;

inline float pq_encode(float nits) {
	float y = std::clamp(nits / 10000.0f, 0.0f, 1.0f);
	float ym = std::pow(y, PQ_M1);
	return std::pow((PQ_C1 + PQ_C2 * ym) / (1.0f + PQ_C3 * ym), PQ_M2);
}
inline float pq_decode(float e) {
	float ep = std::pow(std::clamp(e, 0.0f, 1.0f), 1.0f / PQ_M2);
	float y = std::pow(std::max(ep - PQ_C1, 0.0f) / (PQ_C2 - PQ_C3 * ep), 1.0f / PQ_M1);
	return y * 10000.0f;
}

// ---- Rec.709 -> Rec.2020 primaries (ITU-R BT.2087, linear light, same D65 white).
constexpr float M709_2020[3][3] = {
	{0.627403896f, 0.329283039f, 0.043313065f},
	{0.069097289f, 0.919540395f, 0.011362316f},
	{0.016391439f, 0.088013307f, 0.895595253f},
};
inline float3 rec709_to_2020(float3 c) {
	return {M709_2020[0][0] * c.r + M709_2020[0][1] * c.g + M709_2020[0][2] * c.b,
	        M709_2020[1][0] * c.r + M709_2020[1][1] * c.g + M709_2020[1][2] * c.b,
	        M709_2020[2][0] * c.r + M709_2020[2][1] * c.g + M709_2020[2][2] * c.b};
}
inline float luma709(float3 c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
inline float luma2020(float3 c) { return 0.2627f * c.r + 0.6780f * c.g + 0.0593f * c.b; }

// ---- Optional highlight expansion (linear, relative to SDR white). Identity at/below the knee and when
// strength == 0. Scales RGB by a common factor -> chromaticity and neutrality preserved (no saturation boost).
inline float3 expand_highlights(float3 c, const Params& p) {
	if (p.highlight_strength <= 0.0f) return c;
	float y = luma709(c);
	if (y <= p.highlight_knee) return c;
	float peak_rel = std::max(p.peak_nits / p.sdr_white_nits, 1.0f);
	float t = std::clamp((y - p.highlight_knee) / (1.0f - p.highlight_knee), 0.0f, 1.0f);
	float gain = 1.0f + p.highlight_strength * (peak_rel - 1.0f) * t * t;
	return {c.r * gain, c.g * gain, c.b * gain};
}

// ---- ITU-R BT.2390 EETF (black level 0), applied in PQ domain to max(R,G,B) and then as a common
// RGB scale so hue and neutrality are preserved. Input/output: absolute nits.
inline float bt2390_eetf_nits(float nits, float src_peak, float dst_peak) {
	float src_pq = pq_encode(src_peak);
	float max_lum = pq_encode(dst_peak) / src_pq;
	if (max_lum >= 1.0f) return nits;
	float e1 = pq_encode(nits) / src_pq;
	float ks = 1.5f * max_lum - 0.5f;
	float e2 = e1;
	if (e1 > ks) {
		float t = (e1 - ks) / (1.0f - ks);
		float t2 = t * t, t3 = t2 * t;
		e2 = (2 * t3 - 3 * t2 + 1) * ks + (t3 - 2 * t2 + t) * (1 - ks) + (-2 * t3 + 3 * t2) * max_lum;
	}
	return pq_decode(std::min(e2, 1.0f) * src_pq);
}
inline float3 tonemap_nits(float3 c, const Params& p) {
	if (p.tonemap == Tonemap::CLIP)
		return {std::clamp(c.r, 0.0f, p.peak_nits), std::clamp(c.g, 0.0f, p.peak_nits), std::clamp(c.b, 0.0f, p.peak_nits)};
	float m = std::max({c.r, c.g, c.b});
	if (m <= 0.0f) return {0, 0, 0};
	float s = bt2390_eetf_nits(std::min(m, p.src_peak_nits), p.src_peak_nits, p.peak_nits) / m;
	return {std::max(c.r * s, 0.0f), std::max(c.g * s, 0.0f), std::max(c.b * s, 0.0f)};
}

inline float decode_sdr(float v, Transfer t) { return t == Transfer::GAMMA22 ? gamma22_eotf(v) : srgb_eotf(v); }

// Full per-pixel HDR10 encode: SDR(-extended) gamma-encoded signal in, PQ Rec.2020 code values out.
// rgb: encoded signal (may exceed 1.0 from an RGBA16F source); a: alpha.
inline float3 encode_pixel(float3 rgb, float a, const Params& p) {
	if (p.alpha == Alpha::PREMULTIPLIED) {  // un-premultiply BEFORE any non-linear transfer
		if (a <= 0.0f) return {pq_encode(0), pq_encode(0), pq_encode(0)};
		rgb = {rgb.r / a, rgb.g / a, rgb.b / a};
	}
	float3 lin = {decode_sdr(rgb.r, p.transfer), decode_sdr(rgb.g, p.transfer), decode_sdr(rgb.b, p.transfer)};
	lin = expand_highlights(lin, p);
	float3 nits = {lin.r * p.sdr_white_nits, lin.g * p.sdr_white_nits, lin.b * p.sdr_white_nits};
	float3 n2020 = rec709_to_2020(nits);
	// Out-of-gamut (negative) components after the matrix cannot be shown in Rec.2020 container: clamp.
	n2020 = {std::max(n2020.r, 0.0f), std::max(n2020.g, 0.0f), std::max(n2020.b, 0.0f)};
	n2020 = tonemap_nits(n2020, p);
	return {pq_encode(n2020.r), pq_encode(n2020.g), pq_encode(n2020.b)};
}

} // namespace hdr
