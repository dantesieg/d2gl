// D2GL HDR colour math tests (d2gl/src/graphic/hdr/hdr_color.h). Portable C++17. Exit code = failures.
#include <cstdio>
#include "../../d2gl/src/graphic/hdr/hdr_color.h"

using namespace d2gl::hdr;
static int g_fail = 0, g_pass = 0;

static void check(bool ok, const char* what, double got, double want, double tol)
{
	if (ok) ++g_pass; else ++g_fail;
	std::printf("[%s] %-56s got=%.6f want=%.6f tol=%.1e\n", ok ? "PASS" : "FAIL", what, got, want, tol);
}
static void near(const char* what, double got, double want, double tol) { check(std::fabs(got - want) <= tol, what, got, want, tol); }
static double neutral_dev(const Params& p, float maxv)
{
	double d = 0;
	for (int i = 0; i <= 255; ++i) {
		float v = (float)i / 255.0f * maxv;
		float3 o = encode_pixel({v, v, v}, p);
		d = std::max({d, (double)std::fabs(o.r - o.g), (double)std::fabs(o.g - o.b)});
	}
	return d;
}
static double nits_of(float v, const Params& p) { return pq_decode(encode_pixel({v, v, v}, p).g); }

int main()
{
	// PQ (ST 2084) reference values
	near("PQ(100 nits)", pq_encode(100.0f), 0.5081, 1e-4);
	near("PQ(1000 nits)", pq_encode(1000.0f), 0.7518, 1e-4);
	near("PQ(10000 nits)", pq_encode(10000.0f), 1.0, 1e-6);
	near("PQ(0)", pq_encode(0.0f), 0.0, 1e-6);
	double worst = 0;
	for (float n = 0.01f; n <= 10000.0f; n *= 1.07f) worst = std::max(worst, (double)std::fabs(pq_decode(pq_encode(n)) - n) / n);
	near("PQ round trip worst rel err", worst, 0.0, 2e-3);
	worst = 0;
	for (int i = 0; i <= 1000; ++i) { float v = (float)i / 1000.0f; worst = std::max(worst, (double)std::fabs(srgb_oetf(srgb_eotf(v)) - v)); }
	near("sRGB round trip worst abs err", worst, 0.0, 1e-5);
	for (int r = 0; r < 3; ++r) near("709->2020 row sum == 1", M709_2020[r][0] + M709_2020[r][1] + M709_2020[r][2], 1.0, 1e-6);
	near("709 white -> 2020 luminance 1", luma2020(rec709_to_2020({1, 1, 1})), 1.0, 1e-6);

	// EOTF choices: gamma 2.2 is the default; sRGB piecewise is brighter near black (the "washed out" lift)
	Params p;
	check(p.transfer == Transfer::GAMMA22, "default EOTF is gamma 2.2", (double)p.transfer, 0, 0);
	near("gamma2.2(0.5)", decode_sdr(0.5f, Transfer::GAMMA22), 0.217638, 1e-5);
	near("gamma2.4(0.5)", decode_sdr(0.5f, Transfer::GAMMA24), 0.189465, 1e-5);
	near("sRGB(0.5)", decode_sdr(0.5f, Transfer::SRGB_PIECEWISE), 0.214041, 1e-5);
	check(decode_sdr(0.03f, Transfer::SRGB_PIECEWISE) > 2.0f * decode_sdr(0.03f, Transfer::GAMMA22), "sRGB lifts near-black vs gamma 2.2 (>2x at 3%)",
		decode_sdr(0.03f, Transfer::SRGB_PIECEWISE), decode_sdr(0.03f, Transfer::GAMMA22), 0);

	// paper white / defaults
	near("SDR white -> 200 nits (default)", nits_of(1.0f, p), 200.0, 0.5);
	near("mid grey 0.5 -> 0.2176*200 nits", nits_of(0.5f, p), 0.217638 * 200.0, 0.1);
	near("black -> 0 nits", nits_of(0.0f, p), 0.0, 1e-6);

	// neutrality across every control
	near("neutral: defaults", neutral_dev(p, 1.0f), 0.0, 2e-5);
	Params q = p; q.saturation = 1.5f; near("neutral: saturation 1.5", neutral_dev(q, 1.0f), 0.0, 2e-5);
	q = p; q.saturation = 0.5f; near("neutral: saturation 0.5", neutral_dev(q, 1.0f), 0.0, 2e-5);
	q = p; q.contrast = 1.2f; near("neutral: contrast 1.2", neutral_dev(q, 1.0f), 0.0, 2e-5);
	q = p; q.black_level = 0.05f; near("neutral: black +0.05", neutral_dev(q, 1.0f), 0.0, 2e-5);
	q = p; q.black_level = -0.05f; near("neutral: black -0.05", neutral_dev(q, 1.0f), 0.0, 2e-5);
	q = p; q.brightness_stops = 1.5f; q.highlight_strength = 1.0f; near("neutral: +1.5 stops, expansion", neutral_dev(q, 1.0f), 0.0, 2e-5);

	// saturation
	float3 c = {0.6f, 0.3f, 0.1f};
	float3 s1 = saturate_709(c, 1.0f);
	near("saturation 1.0 is identity (R)", s1.r, c.r, 1e-7);
	near("saturation preserves luminance (s=1.4)", luma709(saturate_709(c, 1.4f)), luma709(c), 1e-6);
	near("saturation preserves luminance (s=0.5)", luma709(saturate_709(c, 0.5f)), luma709(c), 1e-6);
	check(saturate_709(c, 1.4f).r - saturate_709(c, 1.4f).b > c.r - c.b, "saturation 1.4 widens R-B spread", saturate_709(c, 1.4f).r - saturate_709(c, 1.4f).b, c.r - c.b, 0);

	// black level
	near("black 0 is identity", black_adjust(0.37f, 0.0f), 0.37, 1e-7);
	near("black +0.05: white fixed", black_adjust(1.0f, 0.05f), 1.0, 1e-7);
	near("black -0.05: white fixed", black_adjust(1.0f, -0.05f), 1.0, 1e-7);
	near("black +0.05 lifts 0 -> 0.05", black_adjust(0.0f, 0.05f), 0.05, 1e-7);
	near("black -0.05 crushes 0.05 -> 0", black_adjust(0.05f, -0.05f), 0.0, 1e-7);
	for (float b : {-0.05f, -0.02f, 0.0f, 0.02f, 0.05f}) {
		bool mono = true; double prev = -1;
		Params r = p; r.black_level = b;
		for (int i = 0; i <= 1000; ++i) { double n = nits_of((float)i / 1000.0f, r); mono &= n >= prev - 1e-6; prev = n; }
		check(mono, "black level: output monotonic in input", b, 0, 0);
	}
	bool mono_b = true; double prevb = -1;
	for (float b = -0.05f; b <= 0.0501f; b += 0.005f) { Params r = p; r.black_level = b; double n = nits_of(0.03f, r); mono_b &= n >= prevb - 1e-6; prevb = n; }
	check(mono_b, "black level: 3% grey monotonic in setting", prevb, 0, 0);

	// contrast
	float3 g18 = {CONTRAST_PIVOT, CONTRAST_PIVOT, CONTRAST_PIVOT};
	near("contrast keeps pivot (0.18) fixed", contrast_luma(g18, 1.2f).g, CONTRAST_PIVOT, 1e-6);
	near("contrast 1.0 identity", contrast_luma(c, 1.0f).r, c.r, 1e-7);
	check(contrast_luma({0.05f, 0.05f, 0.05f}, 1.2f).g < 0.05f && contrast_luma({0.6f, 0.6f, 0.6f}, 1.2f).g > 0.6f, "contrast 1.2 darkens shadows, brightens above pivot", 0, 0, 0);
	bool mono_c = true; double prevc = -1;
	q = p; q.contrast = 0.8f;
	for (int i = 0; i <= 1000; ++i) { double n = nits_of((float)i / 1000.0f, q); mono_c &= n >= prevc - 1e-6; prevc = n; }
	check(mono_c, "contrast 0.8 monotonic", prevc, 0, 0);
	near("contrast keeps R:G ratio", contrast_luma(c, 1.2f).r / contrast_luma(c, 1.2f).g, c.r / c.g, 1e-5);

	// brightness
	q = p; q.tonemap = Tonemap::CLIP; q.brightness_stops = 1.0f;
	near("+1 stop doubles mid grey nits", nits_of(0.5f, q), 2.0 * nits_of(0.5f, p), 0.2);
	q.brightness_stops = -1.0f;
	near("-1 stop halves white nits", nits_of(1.0f, q), 100.0, 0.3);

	// highlight expansion and EETF
	q = p; q.highlight_strength = 1.0f;
	near("expansion: below knee untouched", nits_of(0.5f, q), nits_of(0.5f, p), 1e-4);
	check(nits_of(1.0f, q) > nits_of(1.0f, p), "expansion: white boosted when on", nits_of(1.0f, q), nits_of(1.0f, p), 0);
	// BT.2446 Method A ITM: SDR white -> peak, monotonic, black stays black
	near("ITM white -> peak", bt2446a_itm_ratio(1.0f, 1000.0f, 200.0f), 5.0, 0.02);
	check(bt2446a_itm_ratio(0.18f, 1000.0f, 200.0f) * 0.18f < bt2446a_itm_ratio(0.5f, 1000.0f, 200.0f) * 0.5f, "ITM monotonic", 0, 0, 0);
	{ Params q2; q2.itm = 0.0f; near("ITM off = identity", nits_of(0.5f, q2), nits_of(0.5f, Params{}), 1e-3); }
	// gamut expansion: grey unchanged, saturated red more saturated in 2020
	{ float3 g = to2020_expanded({0.5f, 0.5f, 0.5f}, 1.0f), r0 = to2020_expanded({1, 0, 0}, 0.0f), r1 = to2020_expanded({1, 0, 0}, 1.0f);
	  near("gamut: grey kept r", g.r, 0.5, 1e-4); near("gamut: grey kept b", g.b, 0.5, 1e-4);
	  check(r1.g < r0.g && r1.r > r0.r, "gamut: red deepens", r1.g, r0.g, 0); }
	{ Params r; r.sdr_white_nits = 240; r.brightness_stops = 1; r.peak_nits = 1015; r.src_peak_nits = content_src_peak(r);
	  near("rolloff_content src peak", r.src_peak_nits, 1920.0, 0.5);
	  check(nits_of(1.0f, r) > 470.0, "rolloff_content keeps 480-nit white", nits_of(1.0f, r), 480, 0);
	  Params q3 = r; q3.src_peak_nits = 10000; check(nits_of(1.0f, q3) < nits_of(1.0f, r), "10000-nit source compresses white more", 0, 0, 0); }
	// highlight whitening: off = identity, below half peak untouched, at the peak colour goes, luminance kept
	{ float3 hc = {900, 100, 50}, lo = {400, 40, 20};
	  float3 o0 = desat_highlights(hc, 0.0f, 1000), o1 = desat_highlights(hc, 1.0f, 1000), ol = desat_highlights(lo, 1.0f, 1000);
	  near("desat off = identity", o0.g, 100.0, 1e-4);
	  near("desat below half peak untouched", ol.g, 40.0, 1e-4);
	  check(o1.g > hc.g && o1.r < hc.r, "desat near peak toward white", o1.g, hc.g, 0);
	  near("desat keeps luminance", luma2020(o1), luma2020(hc), 1e-2); }
	near("EETF(src peak) -> dst peak", bt2390_eetf_nits(10000, 10000, 1000), 1000.0, 1.0);
	near("EETF identity at paper white", bt2390_eetf_nits(200, 10000, 1000), 200.0, 0.05);
	q = p; q.peak_nits = 400; q.brightness_stops = 2.0f;
	check(nits_of(1.0f, q) <= 400.5, "BT.2390: +2 stops white stays <= 400 peak", nits_of(1.0f, q), 400, 0);
	q.tonemap = Tonemap::CLIP;
	near("clip: +2 stops white clipped to 400", nits_of(1.0f, q), 400.0, 0.5);

	std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
	return g_fail;
}
