// Portable CPU reference of D2GL's final SDR -> HDR10 encode (header-only, no D2GL dependencies).
// hdr_shader.h is a transcription of encode_pixel(); tests/hdr validates both (CPU natively and under
// Wine, GPU vs CPU under Wine/wined3d). Keep them in sync.
#pragma once
#include <algorithm>
#include <cmath>

namespace d2gl::hdr {

struct float3 { float r, g, b; };

enum class Transfer : int { GAMMA22 = 0, GAMMA24 = 1, SRGB_PIECEWISE = 2 };
enum class Tonemap : int { CLIP = 0, BT2390 = 1 };

struct Params {
	float sdr_white_nits = 200.0f;   // paper white: SDR 1.0 maps here
	float peak_nits = 1000.0f;       // target peak
	float src_peak_nits = 10000.0f;  // assumed source peak for the EETF
	float brightness_stops = 0.0f;   // overall linear gain = 2^stops
	float black_level = 0.0f;        // signal-domain lift (>0) / crush (<0), white fixed
	float contrast = 1.0f;           // luminance power around mid-grey (0.18 linear)
	float saturation = 1.0f;         // linear Rec.709, luminance preserving
	float highlight_strength = 0.0f; // 0 = off
	float highlight_knee = 0.75f;
	Transfer transfer = Transfer::GAMMA22;
	Tonemap tonemap = Tonemap::BT2390;
	float tint[3] = { 1.0f, 1.0f, 1.0f }; // per-act colour grading (linear multiplier)
	float gamut_expand = 0.0f; // 0 = off; saturated colours move from Rec.709 toward P3 (neutrals unchanged)
	float itm = 0.0f;          // 0 = off; BT.2446 Method A inverse tone mapping blend (luminance only)
	float dither = 0.0f;       // shader only: noise in LSBs of the 8-bit composite before decoding
};

constexpr float CONTRAST_PIVOT = 0.18f;

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
inline float gamma_eotf(float v, float g) { return std::copysign(std::pow(std::fabs(v), g), v); }
inline float decode_sdr(float v, Transfer t) {
	switch (t) {
	case Transfer::GAMMA24: return gamma_eotf(v, 2.4f);
	case Transfer::SRGB_PIECEWISE: return srgb_eotf(v);
	default: return gamma_eotf(v, 2.2f);
	}
}

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

// Signal-domain black level: lift maps [0,1] -> [b,1]; crush maps [-b,1] -> [0,1]. Monotonic, 1 -> 1.
inline float black_adjust(float v, float b) {
	if (b >= 0.0f) return b + (1.0f - b) * v;
	return std::max((v + b) / (1.0f + b), 0.0f);
}
inline float3 saturate_709(float3 c, float s) {
	float y = luma709(c);
	return {std::max(y + s * (c.r - y), 0.0f), std::max(y + s * (c.g - y), 0.0f), std::max(y + s * (c.b - y), 0.0f)};
}
inline float3 contrast_luma(float3 c, float k) {
	float y = luma709(c);
	if (k == 1.0f || y <= 0.0f) return c;
	float s = CONTRAST_PIVOT * std::pow(y / CONTRAST_PIVOT, k) / y;
	return {c.r * s, c.g * s, c.b * s};
}
inline float3 expand_highlights(float3 c, const Params& p) {
	if (p.highlight_strength <= 0.0f) return c;
	float y = luma709(c);
	if (y <= p.highlight_knee) return c;
	float peak_rel = std::max(p.peak_nits / p.sdr_white_nits, 1.0f);
	float t = std::clamp((y - p.highlight_knee) / (1.0f - p.highlight_knee), 0.0f, 1.0f);
	float gain = 1.0f + p.highlight_strength * (peak_rel - 1.0f) * t * t;
	return {c.r * gain, c.g * gain, c.b * gain};
}
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

// BT.2446 Method A inverse tone mapping: luminance ratio HDR / SDR for a linear SDR luminance y
// (1 = paper white), output relative to paper white. SDR reference 100 nits, HDR peak = peak_nits.
inline float bt2446a_itm_ratio(float y, float peak_nits, float sdr_white_nits) {
	if (y <= 0.0f) return 1.0f;
	y = std::min(y, 1.0f);
	const float rs = 1.0f + 32.0f * std::pow(100.0f / 10000.0f, 1.0f / 2.4f);
	const float rh = 1.0f + 32.0f * std::pow(peak_nits / 10000.0f, 1.0f / 2.4f);
	const float yp = std::pow(y, 1.0f / 2.4f);
	const float yc = std::log(1.0f + (rs - 1.0f) * yp) / std::log(rs);
	float ypp;
	if (yc <= 0.7968723f) ypp = yc / 1.077f;
	else if (yc <= 0.9954521f) ypp = (2.7811f - std::sqrt(std::max(2.7811f * 2.7811f - 4.0f * 1.151f * (0.6302f + yc), 0.0f))) / (2.0f * 1.151f);
	else ypp = (yc - 0.5f) / 0.5f;
	const float yh = (std::pow(rh, ypp) - 1.0f) / (rh - 1.0f);
	const float nits = std::pow(std::max(yh, 0.0f), 2.4f) * peak_nits;
	return nits / sdr_white_nits / y;
}
constexpr float MP3_2020[3][3] = {
	{0.753833034f, 0.198597369f, 0.047569597f},
	{0.045743849f, 0.941777220f, 0.012478931f},
	{-0.001210340f, 0.017601717f, 0.983608623f},
};
// Gamut expansion: blend the 709 -> 2020 result toward "treat the colour as P3", weighted by chroma
// squared so greys and skin barely move while saturated colours deepen.
inline float3 to2020_expanded(float3 c, float strength) {
	float3 a = rec709_to_2020(c);
	if (strength <= 0.0f) return a;
	float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
	if (mx <= 0.0f) return a;
	float s = (mx - mn) / mx, t = strength * s * s;
	float3 b = {MP3_2020[0][0] * c.r + MP3_2020[0][1] * c.g + MP3_2020[0][2] * c.b,
	            MP3_2020[1][0] * c.r + MP3_2020[1][1] * c.g + MP3_2020[1][2] * c.b,
	            MP3_2020[2][0] * c.r + MP3_2020[2][1] * c.g + MP3_2020[2][2] * c.b};
	return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// SDR signal (opaque final composite) -> PQ Rec.2020 code values in [0,1].
inline float3 encode_pixel(float3 rgb, const Params& p) {
	rgb = {black_adjust(rgb.r, p.black_level), black_adjust(rgb.g, p.black_level), black_adjust(rgb.b, p.black_level)};
	float3 lin = {decode_sdr(rgb.r, p.transfer), decode_sdr(rgb.g, p.transfer), decode_sdr(rgb.b, p.transfer)};
	lin = saturate_709(lin, p.saturation);
	lin = contrast_luma(lin, p.contrast);
	if (p.itm > 0.0f) {
		float r = bt2446a_itm_ratio(luma709(lin), p.peak_nits, p.sdr_white_nits);
		r = 1.0f + (r - 1.0f) * p.itm;
		lin = {lin.r * r, lin.g * r, lin.b * r};
	}
	float gain = std::exp2(p.brightness_stops);
	lin = {lin.r * gain, lin.g * gain, lin.b * gain};
	lin = expand_highlights(lin, p);
	float3 n = to2020_expanded({lin.r * p.sdr_white_nits, lin.g * p.sdr_white_nits, lin.b * p.sdr_white_nits}, p.gamut_expand);
	n = {std::max(n.r, 0.0f), std::max(n.g, 0.0f), std::max(n.b, 0.0f)};
	n = tonemap_nits(n, p);
	return {pq_encode(n.r), pq_encode(n.g), pq_encode(n.b)};
}

} // namespace d2gl::hdr
