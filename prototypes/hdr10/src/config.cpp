#include "config.h"
#include "log.h"
#include <cstdlib>
#include <cstring>
#include <string>

const char* tonemap_name(hdr::Tonemap t) { return t == hdr::Tonemap::CLIP ? "none/clip" : "BT.2390 EETF"; }

static void apply(Config& c, const char* k, const char* v) {
	auto f = [&] { return (float)std::atof(v); };
	auto b = [&] { return std::atoi(v) != 0 || !std::strcmp(v, "true") || !std::strcmp(v, "on"); };
	if (!std::strcmp(k, "sdr_white")) c.color.sdr_white_nits = f();
	else if (!std::strcmp(k, "peak_nits")) c.color.peak_nits = f();
	else if (!std::strcmp(k, "src_peak_nits")) c.color.src_peak_nits = f();
	else if (!std::strcmp(k, "highlight")) c.color.highlight_strength = f();
	else if (!std::strcmp(k, "knee")) c.color.highlight_knee = f();
	else if (!std::strcmp(k, "tonemap")) c.color.tonemap = (!std::strcmp(v, "clip") || !std::strcmp(v, "none") || !std::strcmp(v, "0")) ? hdr::Tonemap::CLIP : hdr::Tonemap::BT2390;
	else if (!std::strcmp(k, "transfer")) c.color.transfer = (!std::strcmp(v, "gamma22") || !std::strcmp(v, "1")) ? hdr::Transfer::GAMMA22 : hdr::Transfer::SRGB_PIECEWISE;
	else if (!std::strcmp(k, "alpha")) c.color.alpha = !std::strcmp(v, "premultiplied") ? hdr::Alpha::PREMULTIPLIED : hdr::Alpha::STRAIGHT;
	else if (!std::strcmp(k, "hdr")) c.hdr_enabled = b();
	else if (!std::strcmp(k, "split")) c.split = b();
	else if (!std::strcmp(k, "fullscreen")) c.fullscreen = b();
	else if (!std::strcmp(k, "no_interop")) c.force_no_interop = b();
	else if (!std::strcmp(k, "width")) c.width = std::atoi(v);
	else if (!std::strcmp(k, "height")) c.height = std::atoi(v);
	else if (!std::strcmp(k, "frames")) c.frames = std::atoi(v);
	else if (!std::strcmp(k, "cycles")) c.cycles = std::atoi(v);
	else if (!std::strcmp(k, "shader_selftest")) c.shader_selftest = b();
	else logf("config: unknown key '%s'", k);
}

static void clamp_cfg(Config& c) {
	auto cl = [](float& x, float lo, float hi) { x = x < lo ? lo : (x > hi ? hi : x); };
	cl(c.color.sdr_white_nits, 40, 1000);
	cl(c.color.peak_nits, 80, 10000);
	cl(c.color.src_peak_nits, 100, 10000);
	cl(c.color.highlight_strength, 0, 1);
	cl(c.color.highlight_knee, 0.1f, 0.99f);
	if (c.width < 64 || c.width > 16384) c.width = 1280;
	if (c.height < 64 || c.height > 16384) c.height = 720;
}

void config_load(Config& c, int argc, char** argv) {
	char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
	if (n && n < MAX_PATH) {
		std::string p(path);
		p = p.substr(0, p.find_last_of("\\/") + 1) + "hdr10.ini";
		static const char* keys[] = {"sdr_white", "peak_nits", "src_peak_nits", "highlight", "knee", "tonemap", "transfer",
		                             "alpha", "hdr", "split", "fullscreen", "no_interop", "width", "height"};
		for (const char* k : keys) {
			char v[64];
			if (GetPrivateProfileStringA("hdr10", k, "", v, sizeof(v), p.c_str()) > 0) apply(c, k, v);
		}
	}
	for (int i = 1; i < argc; ++i) {
		const char* a = argv[i];
		if (std::strncmp(a, "--", 2)) { logf("config: ignoring arg '%s'", a); continue; }
		std::string s(a + 2);
		size_t eq = s.find('=');
		std::string k = s.substr(0, eq), v = eq == std::string::npos ? "1" : s.substr(eq + 1);
		for (char& ch : k) if (ch == '-') ch = '_';
		apply(c, k.c_str(), v.c_str());
	}
	clamp_cfg(c);
}

void config_log(const Config& c) {
	logf("config: hdr=%d sdr_white=%.0f peak=%.0f src_peak=%.0f highlight=%.2f knee=%.2f tonemap=%s transfer=%s alpha=%s split=%d",
	     c.hdr_enabled, c.color.sdr_white_nits, c.color.peak_nits, c.color.src_peak_nits, c.color.highlight_strength,
	     c.color.highlight_knee, tonemap_name(c.color.tonemap),
	     c.color.transfer == hdr::Transfer::GAMMA22 ? "gamma2.2" : "sRGB-piecewise",
	     c.color.alpha == hdr::Alpha::PREMULTIPLIED ? "premultiplied" : "straight", c.split);
}
