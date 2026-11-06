// Colour-math unit tests. Pure C++17, no dependencies. Exit code = number of failures.
#include <cstdio>
#include "color_math.h"

using namespace hdr;
static int g_fail = 0, g_pass = 0;

static void check(bool ok, const char* what, double got, double want, double tol) {
	if (ok) ++g_pass; else ++g_fail;
	std::printf("[%s] %-48s got=%.6f want=%.6f tol=%.1e\n", ok ? "PASS" : "FAIL", what, got, want, tol);
}
static void near(const char* what, double got, double want, double tol) {
	check(std::fabs(got - want) <= tol, what, got, want, tol);
}

int main() {
	// PQ reference values (SMPTE ST 2084; BT.2100 tables): 100 nits -> 0.5081, 1000 -> 0.7518, 10000 -> 1.0
	near("PQ(0 nits)", pq_encode(0.0f), 0.0, 1e-6);
	near("PQ(0.1 nits)", pq_encode(0.1f), 0.0623, 5e-4);
	near("PQ(100 nits)", pq_encode(100.0f), 0.5081, 1e-4);
	near("PQ(203 nits)", pq_encode(203.0f), 0.5806, 1e-4);
	near("PQ(1000 nits)", pq_encode(1000.0f), 0.7518, 1e-4);
	near("PQ(4000 nits)", pq_encode(4000.0f), 0.9026, 1e-4);
	near("PQ(10000 nits)", pq_encode(10000.0f), 1.0, 1e-6);
	near("PQ 10-bit code @100 nits", std::round(pq_encode(100.0f) * 1023.0f), 520.0, 0.0);
	// round trips
	double worst = 0;
	for (float n = 0.01f; n <= 10000.0f; n *= 1.07f) worst = std::max(worst, (double)std::fabs(pq_decode(pq_encode(n)) - n) / n);
	near("PQ decode(encode(x)) worst rel err", worst, 0.0, 2e-3);
	worst = 0;
	for (int i = 0; i <= 1000; ++i) { float v = (float)i / 1000.0f; worst = std::max(worst, (double)std::fabs(srgb_oetf(srgb_eotf(v)) - v)); }
	near("sRGB oetf(eotf(v)) worst abs err", worst, 0.0, 1e-5);
	near("sRGB eotf(0.5)", srgb_eotf(0.5f), 0.214041, 1e-5);
	near("sRGB eotf(0.04045) (linear segment)", srgb_eotf(0.04045f), 0.0031308, 1e-6);
	near("sRGB eotf continuity at knee", srgb_eotf(0.040451f) - srgb_eotf(0.040449f), 0.0, 1e-6);
	near("sRGB eotf(1.0)", srgb_eotf(1.0f), 1.0, 1e-6);
	near("gamma2.2 eotf(0.5)", gamma22_eotf(0.5f), 0.217638, 1e-5);
	check(srgb_eotf(0.02f) != gamma22_eotf(0.02f), "sRGB piecewise != gamma2.2 near black", srgb_eotf(0.02f), gamma22_eotf(0.02f), 0);
	// matrix: each row sums to 1 so D65 white (1,1,1) maps to (1,1,1)
	for (int r = 0; r < 3; ++r) near("M709->2020 row sum", M709_2020[r][0] + M709_2020[r][1] + M709_2020[r][2], 1.0, 1e-6);
	float3 w = rec709_to_2020({1, 1, 1});
	near("709 white -> 2020 luminance", luma2020(w), 1.0, 1e-6);
	float3 red = rec709_to_2020({1, 0, 0});
	near("709 red luminance preserved (Y709=0.2126)", luma2020(red), 0.2126, 2e-3);

	Params p; // defaults: sdr_white 203, peak 1000, BT.2390, expansion off
	// neutral preservation through the full pipeline
	double maxdev = 0;
	for (int i = 0; i <= 255; ++i) {
		float v = (float)i / 255.0f;
		float3 o = encode_pixel({v, v, v}, 1.0f, p);
		maxdev = std::max({maxdev, (double)std::fabs(o.r - o.g), (double)std::fabs(o.g - o.b)});
	}
	near("neutral greys stay neutral (PQ dev <0.02 code)", maxdev, 0.0, 2e-5);
	p.highlight_strength = 1.0f;
	maxdev = 0;
	for (int i = 0; i <= 255; ++i) {
		float v = (float)i / 255.0f * 1.5f;
		float3 o = encode_pixel({v, v, v}, 1.0f, p);
		maxdev = std::max({maxdev, (double)std::fabs(o.r - o.g), (double)std::fabs(o.g - o.b)});
	}
	near("neutral preserved with expansion+EETF", maxdev, 0.0, 2e-5);
	p = Params{};
	// SDR white -> paper white exactly (peak 1000 > 203, EETF knee above 203 nits)
	float3 wo = encode_pixel({1, 1, 1}, 1.0f, p);
	near("SDR white -> PQ(203 nits)", pq_decode(wo.g), 203.0, 0.5);
	p.sdr_white_nits = 80.0f;
	near("SDR white -> PQ(80 nits) @sdr_white=80", pq_decode(encode_pixel({1, 1, 1}, 1, p).g), 80.0, 0.2);
	p = Params{};
	near("mid grey 0.5 -> 0.214*203 nits", pq_decode(encode_pixel({.5f, .5f, .5f}, 1, p).g), 0.214041 * 203.0, 0.1);
	// expansion off => values below knee identical with strength 0 vs 1
	Params q; q.highlight_strength = 1.0f;
	near("expansion: below knee untouched", encode_pixel({.5f, .5f, .5f}, 1, q).g, encode_pixel({.5f, .5f, .5f}, 1, p).g, 1e-7);
	check(encode_pixel({1, 1, 1}, 1, q).g > encode_pixel({1, 1, 1}, 1, p).g, "expansion: white boosted when on", encode_pixel({1, 1, 1}, 1, q).g, encode_pixel({1, 1, 1}, 1, p).g, 0);
	// EETF
	near("EETF(src peak) -> dst peak", bt2390_eetf_nits(10000, 10000, 1000), 1000.0, 1.0);
	near("EETF identity below knee (100 nits)", bt2390_eetf_nits(100, 10000, 1000), 100.0, 1e-2);
	near("EETF identity when dst>=src", bt2390_eetf_nits(5000, 1000, 4000), 5000.0, 1e-3);
	worst = 0; float prev = -1; bool mono = true;
	for (float n = 0; n <= 10000; n += 10) { float e = bt2390_eetf_nits(n, 10000, 600); mono &= e >= prev - 1e-3f; prev = e; worst = std::max(worst, (double)e); }
	check(mono, "EETF monotonic 0..10000 -> 600", prev, 600, 0);
	near("EETF max output <= dst peak", worst, 600.0, 1.0);
	p.tonemap = Tonemap::CLIP;
	near("CLIP: 5x SDR white clipped to peak", pq_decode(encode_pixel({srgb_oetf(5.0f * 1000 / 203), 1, 1}, 1, p).r), 1000.0, 1.0);
	p = Params{};
	// premultiplied alpha: un-premultiply before EOTF
	p.alpha = Alpha::PREMULTIPLIED;
	float3 pm = encode_pixel({0.25f, 0.25f, 0.25f}, 0.5f, p);
	Params s; float3 st = encode_pixel({0.5f, 0.5f, 0.5f}, 1.0f, s);
	near("premultiplied (0.25,a=.5) == straight 0.5", pm.g, st.g, 1e-6);
	near("premultiplied a=0 -> black", encode_pixel({0, 0, 0}, 0, p).g, 0.0, 1e-6);
	// no saturation boost: chromaticity ratio of a 709 colour preserved by expansion
	q = Params{}; q.highlight_strength = 1.0f; q.tonemap = Tonemap::CLIP; q.peak_nits = 10000;
	float3 c0 = expand_highlights({0.9f, 0.6f, 0.3f}, q);
	near("expansion keeps R:G ratio (no sat boost)", c0.r / c0.g, 1.5, 1e-5);
	std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
	return g_fail;
}
