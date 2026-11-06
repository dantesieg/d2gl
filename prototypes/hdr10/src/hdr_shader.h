// HLSL for the final SDR->HDR10 encode. Transcribed from color_math.h::encode_pixel (keep in sync).
// Compiled at RUNTIME with D3DCompile from d3dcompiler_47.dll (loaded via LoadLibrary), vs_4_0/ps_4_0.
// Written with macros, if-blocks and no ternaries/user functions so that it also compiles with Wine 8's
// vkd3d-shader HLSL compiler (used for the automated compile test); semantics are identical.
#pragma once

static const char kHdrHlsl[] = R"HLSL(
cbuffer P : register(b0) {
	float sdr_white; float peak; float src_peak; float strength;
	float knee; int transfer; int tonemap; int alpha_mode;
	int out_mode; float split_x; float2 pad;
};
Texture2D<float4> src : register(t0);
SamplerState smp : register(s0);

struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VO vs_main(uint id : SV_VertexID) {
	VO o;
	float2 p = float2((float)((id << 1) & 2), (float)(id & 2));
	o.uv = p;                                  // texture row 0 = top
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

float4 ps_main(VO i) : SV_Target {
	float4 s = src.Sample(smp, i.uv);
	float3 rgb = s.rgb;
	if (alpha_mode == 1) {                                          // un-premultiply BEFORE transfer functions
		if (s.a <= 0) return float4(0, 0, 0, 1);                    // PQ(0) == 0 and SDR black == 0
		rgb = rgb / s.a;
	}
	if (out_mode == 0) return float4(saturate(rgb), 1);             // SDR swapchain: signal unchanged
	float hs = strength;
	if (split_x >= 0) {
		if (abs(i.pos.x - split_x) < 1.0) { float dv = PQ_ENC(sdr_white * 0.5); return float4(dv, dv, dv, 1); }
		if (i.pos.x < split_x) { rgb = saturate(rgb); hs = 0; }     // left half: SDR reference
	}
	// EOTF (exact piecewise sRGB, or pure gamma 2.2), sign-preserving
	float3 a = abs(rgb);
	float3 lin = (float3)0;
	if (transfer == 1) {
		lin = pow(a, 2.2);
	} else {
		float3 lo = a / 12.92;
		float3 hi = pow((a + 0.055) / 1.055, 2.4);
		lin = lerp(hi, lo, (float3)(a <= 0.04045));
	}
	lin = lin * lerp((float3)(-1.0), (float3)(1.0), (float3)(rgb >= 0));
	// optional highlight expansion above the knee (common RGB gain: no hue/saturation change)
	if (hs > 0) {
		float y = dot(lin, float3(0.2126, 0.7152, 0.0722));
		if (y > knee) {
			float t = saturate((y - knee) / (1.0 - knee));
			lin = lin * (1.0 + hs * (max(peak / sdr_white, 1.0) - 1.0) * t * t);
		}
	}
	float3 n709 = lin * sdr_white;
	float3 n = float3(dot(float3(0.627403896, 0.329283039, 0.043313065), n709),
	                  dot(float3(0.069097289, 0.919540395, 0.011362316), n709),
	                  dot(float3(0.016391439, 0.088013307, 0.895595253), n709));
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
