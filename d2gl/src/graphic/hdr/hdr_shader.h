// HLSL for D2GL's final SDR -> HDR10 encode (D3D11, vs_4_0/ps_4_0). Transcribes hdr_color.h::encode_pixel.
// Compiled at runtime with D3DCompile (d3dcompiler_47.dll; under Proton that is Wine's vkd3d-shader).
// Written with macros and if-blocks, no ternaries or helper functions, so old vkd3d-shader versions accept it.
#pragma once

#include "hdr_color.h"

namespace d2gl::hdr {

static const char kHdrHlsl[] = R"HLSL(
cbuffer P : register(b0) {
	float sdr_white; float peak; float src_peak; float strength;
	float knee; int transfer; int tonemap; int out_mode;
	float gain; float black; float contrast; float saturation;
	int calib; int flip_y; float width; float height;
	float emit_scale; int graded; float gamut; float dither;
	float3 tint; float itm;
};
Texture2D<float4> src : register(t0);
SamplerState smp : register(s0);

struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VO vs_main(uint id : SV_VertexID) {
	VO o;
	float2 p = float2((float)((id << 1) & 2), (float)(id & 2));
	o.uv = p;
	o.pos = float4(p.x * 2 - 1, 1 - p.y * 2, 0, 1);
	return o;
}

#define M1 (2610.0 / 16384.0)
#define M2 (2523.0 / 4096.0 * 128.0)
#define C1 (3424.0 / 4096.0)
#define C2 (2413.0 / 4096.0 * 32.0)
#define C3 (2392.0 / 4096.0 * 32.0)
#define PQ_ENC(n) pow((C1 + C2 * pow(saturate((n) / 10000.0), M1)) / (1 + C3 * pow(saturate((n) / 10000.0), M1)), M2)
#define PQ_DEC(e) (pow(max(pow(saturate(e), 1.0 / M2) - C1, 0) / (C2 - C3 * pow(saturate(e), 1.0 / M2)), 1.0 / M1) * 10000.0)
#define LUMA(c) dot(c, float3(0.2126, 0.7152, 0.0722))

float4 ps_main(VO i) : SV_Target {
	float2 uv = i.uv;
	if (flip_y == 1) uv.y = 1.0 - uv.y;                 // GL rows are bottom-up
	float4 texel = src.Sample(smp, uv);
	float3 rgb = texel.rgb;
	// [HDR] dither: triangular noise of +-1 LSB on the 8-bit composite, so stretched dark gradients do not band
	if (dither > 0 && out_mode != 0) {
		float2 dp = i.pos.xy;
		float d1 = frac(sin(dot(dp, float2(12.9898, 78.233))) * 43758.5453);
		float d2 = frac(sin(dot(dp, float2(39.3468, 11.135))) * 24634.6345);
		rgb = rgb + (d1 + d2 - 1.0) * (dither / 255.0);
		rgb = max(rgb, 0);
	}
	float ev = 1.0 - texel.a;                           // GL wrote alpha = 1 - emission ([HDR] emissive)
	if (out_mode == 0) return float4(saturate(rgb), 1); // SDR swapchain: signal unchanged

	// Calibration overlay: dark band with paper-white (signal 1.0, full pipeline), peak (direct peak nits)
	// and five near-black steps (signal 0, 1, 2, 3, 5 % through the full pipeline).
	if (calib == 1) {
		float2 q = i.pos.xy / float2(width, height);
		if (q.y > 0.35 && q.y < 0.65) {
			rgb = float3(0, 0, 0);
			int cell = (int)(q.x * 8.0);
			float fx = q.x * 8.0 - (float)cell;
			bool inpatch = (q.y > 0.42 && q.y < 0.58) && (fx > 0.15 && fx < 0.85);
			if (inpatch && cell == 1) rgb = float3(1, 1, 1);
			if (inpatch && cell == 2) { float pv = PQ_ENC(peak); return float4(pv, pv, pv, 1); }
			if (inpatch && cell == 3) rgb = float3(0, 0, 0);
			if (inpatch && cell == 4) rgb = float3(0.01, 0.01, 0.01);
			if (inpatch && cell == 5) rgb = float3(0.02, 0.02, 0.02);
			if (inpatch && cell == 6) rgb = float3(0.03, 0.03, 0.03);
			if (inpatch && cell == 7) rgb = float3(0.05, 0.05, 0.05);
		}
	}

	// black level (signal domain, white fixed)
	if (black >= 0) rgb = black + (1.0 - black) * rgb;
	else rgb = max((rgb + black) / (1.0 + black), 0);
	// EOTF: 0 gamma 2.2, 1 gamma 2.4, 2 sRGB piecewise
	float3 a = abs(rgb);
	float3 lin = (float3)0;
	if (transfer == 2) {
		float3 lo = a / 12.92;
		float3 hi = pow((a + 0.055) / 1.055, 2.4);
		lin = lerp(hi, lo, (float3)(a <= 0.04045));
	} else {
		float g = 2.2;
		if (transfer == 1) g = 2.4;
		lin = pow(a, g);
	}
	// saturation (luminance preserving, linear Rec.709)
	float y = LUMA(lin);
	lin = max(y + saturation * (lin - y), 0);
	lin = lin * tint; // per-act colour grading
	// contrast around 0.18 linear, on luminance (hue/neutrality preserved)
	y = LUMA(lin);
	if (y > 0) lin = lin * (0.18 * pow(y / 0.18, contrast) / y);
	// [HDR] itm: BT.2446 Method A inverse tone mapping on luminance (SDR ref 100 nits -> peak), blended
	if (itm > 0) {
		float iy = min(LUMA(lin), 1.0);
		if (iy > 0) {
			float irs = 1.0 + 32.0 * pow(0.01, 1.0 / 2.4);
			float irh = 1.0 + 32.0 * pow(peak / 10000.0, 1.0 / 2.4);
			float iyc = log(1.0 + (irs - 1.0) * pow(iy, 1.0 / 2.4)) / log(irs);
			float iyp = (iyc - 0.5) / 0.5;
			if (iyc <= 0.9954521) iyp = (2.7811 - sqrt(max(2.7811 * 2.7811 - 4.0 * 1.151 * (0.6302 + iyc), 0))) / (2.0 * 1.151);
			if (iyc <= 0.7968723) iyp = iyc / 1.077;
			float iyh = (pow(irh, iyp) - 1.0) / (irh - 1.0);
			float ir = pow(max(iyh, 0), 2.4) * peak / sdr_white / iy;
			lin = lin * (1.0 + (ir - 1.0) * itm);
		}
	}
	lin = lin * gain;
	// optional highlight expansion above the knee
	if (strength > 0) {
		y = LUMA(lin);
		if (y > knee) {
			float t = saturate((y - knee) / (1.0 - knee));
			lin = lin * (1.0 + strength * (max(peak / sdr_white, 1.0) - 1.0) * t * t);
		}
	}
	// emissive highlights: additive sprites lifted toward emissive_nits; the tonemap below rolls off peaks
	// graded: alpha = 1 / (1 + e) with unbounded e (layers add up), so e = 1 / alpha - 1
	if (emit_scale > 0) {
		float ee = saturate(ev);
		if (graded == 1) ee = min(1.0 / max(texel.a, 1.0 / 255.0) - 1.0, 254.0);
		lin = lin * (1.0 + ee * emit_scale);
	}
	float3 n709 = lin * sdr_white;
	float3 n = float3(dot(float3(0.627403896, 0.329283039, 0.043313065), n709),
	                  dot(float3(0.069097289, 0.919540395, 0.011362316), n709),
	                  dot(float3(0.016391439, 0.088013307, 0.895595253), n709));
	// [HDR] gamut_expand: toward "the colour is P3", weighted by chroma^2 (greys and skin barely move)
	if (gamut > 0) {
		float gmx = max(n709.r, max(n709.g, n709.b));
		float gmn = min(n709.r, min(n709.g, n709.b));
		if (gmx > 0) {
			float gs = (gmx - gmn) / gmx;
			float3 np3 = float3(dot(float3(0.753833034, 0.198597369, 0.047569597), n709),
			                    dot(float3(0.045743849, 0.941777220, 0.012478931), n709),
			                    dot(float3(-0.001210340, 0.017601717, 0.983608623), n709));
			n = lerp(n, np3, gamut * gs * gs);
		}
	}
	n = max(n, 0);
	if (tonemap == 0) {
		n = min(n, peak);
	} else {
		float m = max(n.r, max(n.g, n.b));
		float sp = PQ_ENC(src_peak);
		float ml = PQ_ENC(peak) / sp;
		if (m > 0 && ml < 1) {
			float e1 = PQ_ENC(min(m, src_peak)) / sp;
			float ks = 1.5 * ml - 0.5;
			float e2 = e1;
			if (e1 > ks) {
				float t = (e1 - ks) / (1 - ks);
				float t2 = t * t;
				float t3 = t2 * t;
				e2 = (2 * t3 - 3 * t2 + 1) * ks + (t3 - 2 * t2 + t) * (1 - ks) + (-2 * t3 + 3 * t2) * ml;
			}
			float outn = PQ_DEC(min(e2, 1.0) * sp);
			n = max(n * (outn / m), 0);
		}
	}
	return float4(PQ_ENC(n.r), PQ_ENC(n.g), PQ_ENC(n.b), 1);
}
)HLSL";

struct ShaderConstants {
	float sdr_white, peak, src_peak, strength;
	float knee; int transfer, tonemap, out_mode;
	float gain, black, contrast, saturation;
	int calib, flip_y; float width, height;
	float emit_scale; int graded; float gamut, dither;
	float tint_r, tint_g, tint_b, itm;
};
static_assert(sizeof(ShaderConstants) == 96, "cbuffer layout");

inline ShaderConstants makeConstants(const Params& p, int out_mode, bool calib, bool flip_y, float w, float h, float emit_scale = 0.0f, bool graded = false)
{
	return { p.sdr_white_nits, p.peak_nits, p.src_peak_nits, p.highlight_strength, p.highlight_knee, (int)p.transfer,
		(int)p.tonemap, out_mode, std::exp2(p.brightness_stops), p.black_level, p.contrast, p.saturation,
		calib ? 1 : 0, flip_y ? 1 : 0, w, h, emit_scale, graded ? 1 : 0, p.gamut_expand, p.dither, p.tint[0], p.tint[1], p.tint[2], p.itm };
}

}
