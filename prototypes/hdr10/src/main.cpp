// HDR10 proof of concept: GL renders -> WGL_NV_DX_interop2 shared texture -> D3D11 encode -> DXGI flip swapchain
// (R10G10B10A2 + G2084/P2020), with SDR fallbacks. Single render thread owns GL and D3D (as D2GL's render thread would).
#include "config.h"
#include "d3d_present.h"
#include "font.h"
#include "gl_loader.h"
#include "interop.h"
#include "log.h"
#include "scene.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

const char* dxgi_cs_name(DXGI_COLOR_SPACE_TYPE c);

enum class Path { NONE, INTEROP_D3D11, WGL_SDR };

struct App {
	Config cfg;
	HINSTANCE inst = nullptr;
	HWND hwnd = nullptr, hidden = nullptr;
	HDC hidden_dc = nullptr, main_dc = nullptr;
	HGLRC glrc = nullptr;
	int pixel_format = 0;
	PIXELFORMATDESCRIPTOR pfd = {};
	Scene scene;
	Presenter pres;
	Interop iop;
	Path path = Path::NONE;
	bool window_had_dxgi = false;
	char interop_status[160] = "?";
	char fallback[160] = "none";
	char gl_renderer[128] = "?", gl_vendor[64] = "?", gl_version[64] = "?";
	char luid_status[96] = "?";
	LUID gl_luid = {};
	bool have_luid = false;
	// WGL fallback target
	GLuint sdr_fbo = 0, sdr_tex = 0;
	int w = 0, h = 0;
	bool resize_pending = false, display_changed = false, reinit_requested = false, quit = false;
	bool hdr_toggle_pending = false;
	RECT windowed_rect = {};
	DWORD t0 = 0;
	std::string last_panel;
};
static App g;

// ------------------------------------------------------------------ window
static LRESULT CALLBACK wndproc(HWND hw, UINT msg, WPARAM wp, LPARAM lp);

static void set_fullscreen(bool on) {
	g.cfg.fullscreen = on;
	if (on) {
		GetWindowRect(g.hwnd, &g.windowed_rect);
		MONITORINFO mi = {};
		mi.cbSize = sizeof(mi);
		GetMonitorInfoA(MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTONEAREST), &mi);
		SetWindowLongA(g.hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
		SetWindowPos(g.hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
		             mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
	} else {
		SetWindowLongA(g.hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
		RECT& r = g.windowed_rect;
		SetWindowPos(g.hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER);
	}
	logf("window: borderless fullscreen %s", on ? "ON" : "OFF");
}

static bool create_main_window() {
	RECT r = {0, 0, g.cfg.width, g.cfg.height};
	AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
	g.hwnd = CreateWindowExA(0, "hdr10_main", "D2GL HDR10 prototype", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
	                         CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, g.inst, nullptr);
	if (!g.hwnd) { logf("window: CreateWindowEx FAILED (%lu)", (unsigned long)GetLastError()); return false; }
	g.main_dc = GetDC(g.hwnd);
	GetWindowRect(g.hwnd, &g.windowed_rect);
	RECT cr;
	GetClientRect(g.hwnd, &cr);
	g.w = cr.right > 0 ? cr.right : g.cfg.width;
	g.h = cr.bottom > 0 ? cr.bottom : g.cfg.height;
	g.window_had_dxgi = false;
	if (g.cfg.fullscreen) set_fullscreen(true);
	return true;
}

static void destroy_main_window() {
	if (g.main_dc) ReleaseDC(g.hwnd, g.main_dc);
	if (g.hwnd) DestroyWindow(g.hwnd);
	g.main_dc = nullptr;
	g.hwnd = nullptr;
}

// ------------------------------------------------------------------ GL context (on a hidden window so the visible
// window stays free of a GL pixel format and can host a DXGI flip-model swapchain)
static bool init_gl() {
	g.hidden = CreateWindowExA(0, "hdr10_main", "gl", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, g.inst, nullptr);
	if (!g.hidden) { logf("gl: hidden window FAILED"); return false; }
	g.hidden_dc = GetDC(g.hidden);
	PIXELFORMATDESCRIPTOR& pfd = g.pfd;
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cAlphaBits = 8;
	g.pixel_format = ChoosePixelFormat(g.hidden_dc, &pfd);
	if (!g.pixel_format || !SetPixelFormat(g.hidden_dc, g.pixel_format, &pfd)) { logf("gl: pixel format FAILED (%lu)", (unsigned long)GetLastError()); return false; }
	HGLRC legacy = wglCreateContext(g.hidden_dc);
	if (!legacy || !wglMakeCurrent(g.hidden_dc, legacy)) { logf("gl: legacy context FAILED (%lu)", (unsigned long)GetLastError()); return false; }
	gl_load_wgl_ext(g.hidden_dc);
	if (g_wgl.CreateContextAttribsARB) {
		const int vers[][2] = {{4, 6}, {4, 5}, {4, 3}, {4, 0}, {3, 3}};
		for (auto& v : vers) {
			int attrs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, v[0], WGL_CONTEXT_MINOR_VERSION_ARB, v[1],
			               WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
			g.glrc = g_wgl.CreateContextAttribsARB(g.hidden_dc, nullptr, attrs);
			if (g.glrc) { logf("gl: core %d.%d context created", v[0], v[1]); break; }
		}
	}
	wglMakeCurrent(nullptr, nullptr);
	wglDeleteContext(legacy);
	if (!g.glrc) { logf("gl: no core 3.3+ context available"); return false; }
	wglMakeCurrent(g.hidden_dc, g.glrc);
	gl_load_wgl_ext(g.hidden_dc); // re-query under the real context
	std::snprintf(g.gl_vendor, sizeof(g.gl_vendor), "%s", (const char*)glGetString(GL_VENDOR));
	std::snprintf(g.gl_renderer, sizeof(g.gl_renderer), "%s", (const char*)glGetString(GL_RENDERER));
	std::snprintf(g.gl_version, sizeof(g.gl_version), "%s", (const char*)glGetString(GL_VERSION));
	logf("gl: vendor='%s' renderer='%s' version='%s'", g.gl_vendor, g.gl_renderer, g.gl_version);
	if (!gl_load_core()) return false;
	// Adapter LUID (GL_EXT_memory_object + GL_EXT_memory_object_win32 / semaphore_win32 define GL_DEVICE_LUID_EXT)
	bool mo = gl_has_extension("GL_EXT_memory_object");
	bool mow = gl_has_extension("GL_EXT_memory_object_win32") || gl_has_extension("GL_EXT_semaphore_win32");
	g_wgl.GetUnsignedBytevEXT = (PFNGLGETUNSIGNEDBYTEVEXTPROC)(void*)wglGetProcAddress("glGetUnsignedBytevEXT");
	if (mo && mow && g_wgl.GetUnsignedBytevEXT) {
		GLubyte luid[GL_LUID_SIZE_EXT] = {};
		while (glGetError() != GL_NO_ERROR) {}
		g_wgl.GetUnsignedBytevEXT(GL_DEVICE_LUID_EXT, luid);
		GLint mask = 0;
		glGetIntegerv(GL_DEVICE_NODE_MASK_EXT, &mask);
		if (glGetError() == GL_NO_ERROR) {
			std::memcpy(&g.gl_luid, luid, sizeof(LUID));
			g.have_luid = true;
			std::snprintf(g.luid_status, sizeof(g.luid_status), "GL LUID %08lX:%08lX node 0x%X", (unsigned long)g.gl_luid.HighPart,
			              (unsigned long)g.gl_luid.LowPart, mask);
		} else std::snprintf(g.luid_status, sizeof(g.luid_status), "GL_DEVICE_LUID_EXT query error");
	} else std::snprintf(g.luid_status, sizeof(g.luid_status), "no GL_EXT_memory_object_win32 -> vendor heuristic");
	logf("gl: adapter id: %s (EXT_memory_object=%d win32=%d)", g.luid_status, mo, mow);
	return g.scene.init();
}

static void shutdown_gl() {
	if (g.glrc) {
		wglMakeCurrent(g.hidden_dc, g.glrc);
		g.scene.shutdown();
		wglMakeCurrent(nullptr, nullptr);
		wglDeleteContext(g.glrc);
	}
	if (g.hidden_dc) ReleaseDC(g.hidden, g.hidden_dc);
	if (g.hidden) DestroyWindow(g.hidden);
	g.glrc = nullptr;
	g.hidden_dc = nullptr;
	g.hidden = nullptr;
}

// ------------------------------------------------------------------ presentation paths
static void release_sdr_target() {
	if (g.sdr_fbo) p_glDeleteFramebuffers(1, &g.sdr_fbo);
	if (g.sdr_tex) glDeleteTextures(1, &g.sdr_tex);
	g.sdr_fbo = g.sdr_tex = 0;
}

static bool make_sdr_target() {
	release_sdr_target();
	glGenTextures(1, &g.sdr_tex);
	glBindTexture(GL_TEXTURE_2D, g.sdr_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, g.w, g.h, 0, GL_RGBA, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	p_glGenFramebuffers(1, &g.sdr_fbo);
	p_glBindFramebuffer(GL_FRAMEBUFFER, g.sdr_fbo);
	p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g.sdr_tex, 0);
	GLenum st = p_glCheckFramebufferStatus(GL_FRAMEBUFFER);
	p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return st == GL_FRAMEBUFFER_COMPLETE;
}

static void shutdown_present() {
	if (g.path == Path::INTEROP_D3D11) {
		wglMakeCurrent(g.hidden_dc, g.glrc);
		g.iop.close();       // unregister + close GL side first, while the D3D device is alive
		g.pres.destroy();
		logf("present: interop/D3D11 path shut down");
	} else if (g.path == Path::WGL_SDR) {
		release_sdr_target();
		wglMakeCurrent(g.hidden_dc, g.glrc);
		logf("present: WGL SDR path shut down");
	}
	g.path = Path::NONE;
}

static bool start_wgl_sdr(const char* reason) {
	std::snprintf(g.fallback, sizeof(g.fallback), "SDR WGL SwapBuffers: %s", reason);
	logf("present: FALLBACK -> %s (HDR refused; no CPU copy path used)", g.fallback);
	if (g.window_had_dxgi) { // a window that hosted a flip-model swapchain is not reused for GL presentation
		destroy_main_window();
		if (!create_main_window()) return false;
	}
	PIXELFORMATDESCRIPTOR cur = {};
	if (!DescribePixelFormat(g.main_dc, GetPixelFormat(g.main_dc), sizeof(cur), &cur) || !GetPixelFormat(g.main_dc))
		if (!SetPixelFormat(g.main_dc, g.pixel_format, &g.pfd)) { logf("wgl: SetPixelFormat(main) FAILED (%lu)", (unsigned long)GetLastError()); return false; }
	if (!wglMakeCurrent(g.main_dc, g.glrc)) { logf("wgl: wglMakeCurrent(main) FAILED (%lu)", (unsigned long)GetLastError()); return false; }
	if (g_wgl.SwapIntervalEXT) g_wgl.SwapIntervalEXT(1);
	if (!make_sdr_target()) { logf("wgl: RGBA16F FBO incomplete"); return false; }
	g.path = Path::WGL_SDR;
	return true;
}

static bool start_interop() {
	char why[128];
	if (g.cfg.force_no_interop) { std::snprintf(g.interop_status, sizeof(g.interop_status), "disabled (--no_interop)"); return false; }
	if (!Interop::available(why, sizeof(why))) { std::snprintf(g.interop_status, sizeof(g.interop_status), "UNAVAILABLE: %s", why); return false; }
	wglMakeCurrent(g.hidden_dc, g.glrc);
	auto fail = [](const char* what) {
		std::snprintf(g.interop_status, sizeof(g.interop_status), "FAILED at %s", what);
		logf("interop: %s", g.interop_status);
		g.iop.close();
		g.pres.destroy();
		return false;
	};
	if (!g.pres.create_device(g.have_luid ? &g.gl_luid : nullptr, g.gl_vendor)) return fail("D3D11 device");
	if (!g.iop.open(g.pres.dev)) return fail("wglDXOpenDeviceNV");
	if (!g.pres.create_swapchain(g.hwnd, g.w, g.h, g.cfg.hdr_enabled)) return fail("swapchain");
	g.window_had_dxgi = true;
	if (!g.pres.create_shared_texture(g.w, g.h)) return fail("shared texture");
	if (!g.iop.register_texture(g.pres.shared_tex)) return fail("wglDXRegisterObjectNV");
	std::snprintf(g.interop_status, sizeof(g.interop_status), "WGL_NV_DX_interop2 active (zero-copy shared texture)");
	g.path = Path::INTEROP_D3D11;
	return true;
}

static bool start_present() {
	g.fallback[0] = 0;
	if (start_interop()) { std::snprintf(g.fallback, sizeof(g.fallback), "none"); return true; }
	return start_wgl_sdr(g.interop_status);
}

static bool handle_resize() {
	RECT cr;
	GetClientRect(g.hwnd, &cr);
	if (cr.right <= 0 || cr.bottom <= 0) return true; // minimised: keep old size
	if (cr.right == g.w && cr.bottom == g.h) return true;
	g.w = cr.right;
	g.h = cr.bottom;
	logf("resize: %dx%d", g.w, g.h);
	if (g.path == Path::WGL_SDR) return make_sdr_target();
	wglMakeCurrent(g.hidden_dc, g.glrc);
	g.iop.unregister_texture();
	g.pres.release_shared_texture();
	if (!g.pres.resize(g.w, g.h)) return false;
	if (!g.pres.create_shared_texture(g.w, g.h)) return false;
	return g.iop.register_texture(g.pres.shared_tex);
}

// ------------------------------------------------------------------ diagnostics panel
static void update_panel() {
	char l[24][128];
	int n = 0;
	const DisplayInfo& d = g.pres.display;
	bool dx = g.path == Path::INTEROP_D3D11;
	std::snprintf(l[n++], 128, "D2GL HDR10 PROTOTYPE");
	std::snprintf(l[n++], 128, "GL: %.60s", g.gl_renderer);
	std::snprintf(l[n++], 128, "D3D ADAPTER: %.56s", dx ? g.pres.adapter_name : "(not created)");
	std::snprintf(l[n++], 128, "ADAPTER MATCH: %.60s", g.luid_status);
	std::snprintf(l[n++], 128, "INTEROP: %.70s", g.interop_status);
	std::snprintf(l[n++], 128, "PATH: %s", dx ? "GL > NV_DX_INTEROP2 > D3D11 > DXGI FLIP" : "GL > WGL SWAPBUFFERS (SDR)");
	std::snprintf(l[n++], 128, "FALLBACK: %.70s", g.fallback);
	std::snprintf(l[n++], 128, "SWAPCHAIN: %s", !dx ? "WGL DEFAULT FB (RGBA8)" : g.pres.format == DXGI_FORMAT_R10G10B10A2_UNORM ? "R10G10B10A2_UNORM FLIP" : "R8G8B8A8_UNORM FLIP");
	std::snprintf(l[n++], 128, "COLOR SPACE: %s", dx ? dxgi_cs_name(g.pres.color_space) : "SRGB (IMPLICIT)");
	std::snprintf(l[n++], 128, "DISPLAY: %s  OUTPUT6=%s", d.device, d.found ? "YES" : "NO");
	std::snprintf(l[n++], 128, "OS HDR ENABLED: %s", !dx ? "UNKNOWN (NO DXGI)" : d.os_hdr_enabled ? "YES" : "NO");
	std::snprintf(l[n++], 128, "HDR-CAPABLE HW: %s", !dx ? "UNKNOWN" : d.hw_hdr_capable < 0 ? "UNKNOWN" : d.hw_hdr_capable ? "YES" : "NO");
	std::snprintf(l[n++], 128, "DISPLAY LUM: MIN %.3f MAX %.0f FULLFRAME %.0f NITS", d.min_lum, d.max_lum, d.max_full_frame_lum);
	std::snprintf(l[n++], 128, "PRESENTING HDR10: %s %.50s", dx && g.pres.hdr_presenting ? "YES" : "NO",
	              dx ? (g.pres.hdr_presenting ? (g.pres.hdr_metadata_ok ? "(METADATA SET)" : "(NO METADATA)") : g.pres.hdr_refusal) : "");
	const hdr::Params& p = g.cfg.color;
	std::snprintf(l[n++], 128, "HDR REQUEST: %s   SPLIT: %s", g.cfg.hdr_enabled ? "ON" : "OFF", g.cfg.split ? "ON (L=SDR REF)" : "OFF");
	std::snprintf(l[n++], 128, "SDR WHITE %.0f  PEAK %.0f  SRC PEAK %.0f NITS", p.sdr_white_nits, p.peak_nits, p.src_peak_nits);
	std::snprintf(l[n++], 128, "HIGHLIGHT %.2f KNEE %.2f  TONEMAP %s", p.highlight_strength, p.highlight_knee, tonemap_name(p.tonemap));
	std::snprintf(l[n++], 128, "EOTF %s  ALPHA %s", p.transfer == hdr::Transfer::GAMMA22 ? "GAMMA 2.2" : "SRGB PIECEWISE",
	              p.alpha == hdr::Alpha::PREMULTIPLIED ? "PREMULTIPLIED" : "STRAIGHT");
	std::snprintf(l[n++], 128, "KEYS H HDR UP/DN WHITE PGUP/DN PEAK E HIGHL T TMAP");
	std::snprintf(l[n++], 128, "     G EOTF A ALPHA S SPLIT F11 FULL R REINIT ESC");
	std::string key;
	for (int i = 0; i < n; ++i) key += l[i];
	if (key == g.last_panel) return;
	g.last_panel = key;
	const int pw = 400, ph = n * (FONT_H + 1) + 8;
	std::vector<uint32_t> px((size_t)pw * ph, 0xC8000000u); // ABGR in memory = RGBA bytes: black, alpha 200
	for (int i = 0; i < n; ++i) font_draw(px.data(), pw, ph, 4, 4 + i * (FONT_H + 1), l[i], i == 0 ? 0xFF40D0FFu : 0xFFFFFFFFu, 1);
	g.scene.set_panel(px.data(), pw, ph);
	logf("panel: %s | %s | %s | %s", l[5], l[7], l[10], l[13]);
}

// ------------------------------------------------------------------ frame
static bool render_frame() {
	float t = (float)(GetTickCount() - g.t0) / 1000.0f;
	const hdr::Params& p = g.cfg.color;
	float hmax = p.peak_nits / p.sdr_white_nits * 1.25f;
	if (hmax < 2.0f) hmax = 2.0f;
	update_panel();
	if (g.path == Path::INTEROP_D3D11) {
		if (!g.iop.lock()) return false;
		p_glBindFramebuffer(GL_FRAMEBUFFER, g.iop.fbo);
		g.scene.render(g.w, g.h, t, hmax, true);
		p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
		if (!g.iop.unlock()) return false;
		float split = g.cfg.split ? (float)g.w * 0.5f : -1.0f;
		return g.pres.draw_and_present(p, split, true);
	}
	p_glBindFramebuffer(GL_FRAMEBUFFER, g.sdr_fbo);
	g.scene.render(g.w, g.h, t, hmax, false);
	g.scene.blit_sdr(g.sdr_tex, g.w, g.h);
	if (!SwapBuffers(g.main_dc)) { logf("wgl: SwapBuffers FAILED (%lu)", (unsigned long)GetLastError()); return false; }
	GLenum e = glGetError();
	if (e != GL_NO_ERROR) logf("gl: error 0x%X", e);
	return true;
}

static LRESULT CALLBACK wndproc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
	if (hw != g.hwnd) return DefWindowProcA(hw, msg, wp, lp);
	hdr::Params& p = g.cfg.color;
	switch (msg) {
	case WM_CLOSE: g.quit = true; return 0;
	case WM_SIZE: g.resize_pending = true; return 0;
	case WM_DISPLAYCHANGE: logf("event: WM_DISPLAYCHANGE"); g.display_changed = true; return 0;
	case WM_MOVE: g.display_changed = true; return 0; // may have moved to another monitor; detect is cheap-ish, debounced in loop
	case WM_ACTIVATEAPP: logf("event: WM_ACTIVATEAPP %d", (int)wp); return 0;
	case WM_SYSKEYDOWN:
		if (wp == VK_RETURN) { set_fullscreen(!g.cfg.fullscreen); return 0; } // Alt+Enter -> borderless toggle
		break;
	case WM_KEYDOWN:
		switch (wp) {
		case VK_ESCAPE: g.quit = true; break;
		case 'H': g.cfg.hdr_enabled = !g.cfg.hdr_enabled; g.hdr_toggle_pending = true; break;
		case VK_UP: p.sdr_white_nits = p.sdr_white_nits + 10 > 1000 ? 1000 : p.sdr_white_nits + 10; break;
		case VK_DOWN: p.sdr_white_nits = p.sdr_white_nits - 10 < 40 ? 40 : p.sdr_white_nits - 10; break;
		case VK_PRIOR: p.peak_nits = p.peak_nits + 100 > 10000 ? 10000 : p.peak_nits + 100; break;
		case VK_NEXT: p.peak_nits = p.peak_nits - 100 < 100 ? 100 : p.peak_nits - 100; break;
		case 'E': p.highlight_strength = p.highlight_strength >= 1.0f ? 0.0f : p.highlight_strength + 0.25f; break;
		case 'T': p.tonemap = p.tonemap == hdr::Tonemap::CLIP ? hdr::Tonemap::BT2390 : hdr::Tonemap::CLIP; break;
		case 'G': p.transfer = p.transfer == hdr::Transfer::GAMMA22 ? hdr::Transfer::SRGB_PIECEWISE : hdr::Transfer::GAMMA22; break;
		case 'A': p.alpha = p.alpha == hdr::Alpha::STRAIGHT ? hdr::Alpha::PREMULTIPLIED : hdr::Alpha::STRAIGHT; break;
		case 'S': g.cfg.split = !g.cfg.split; break;
		case VK_F11: set_fullscreen(!g.cfg.fullscreen); break;
		case 'R': g.reinit_requested = true; break;
		}
		if (wp != VK_ESCAPE) config_log(g.cfg);
		return 0;
	}
	return DefWindowProcA(hw, msg, wp, lp);
}

static void pump() {
	MSG m;
	while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
		if (m.message == WM_QUIT) g.quit = true;
		TranslateMessage(&m);
		DispatchMessageA(&m);
	}
}

static bool full_reinit(const char* why) {
	logf("reinit: %s", why);
	shutdown_present();
	return start_present();
}

int main(int argc, char** argv) {
	log_open("hdr10_log.txt");
	g.inst = GetModuleHandleA(nullptr);
	config_load(g.cfg, argc, argv);
	config_log(g.cfg);
	if (g.cfg.shader_selftest) {
		bool ok = Presenter::compile_shaders_selftest();
		log_close();
		return ok ? 0 : 3;
	}
	WNDCLASSA wc = {};
	wc.style = CS_OWNDC;
	wc.lpfnWndProc = wndproc;
	wc.hInstance = g.inst;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = "hdr10_main";
	if (!RegisterClassA(&wc)) { logf("window: RegisterClass FAILED"); return 2; }
	if (!create_main_window() || !init_gl()) { logf("fatal: window/GL init failed"); shutdown_gl(); log_close(); return 2; }
	g.t0 = GetTickCount();
	int rc = 0;
	int cycles = g.cfg.cycles > 0 ? g.cfg.cycles : 1;
	for (int c = 0; c < cycles && !g.quit && rc == 0; ++c) {
		if (g.cfg.cycles > 0) logf("cycle %d/%d: init", c + 1, cycles);
		if (!start_present()) { logf("fatal: no presentation path"); rc = 4; break; }
		int frames = 0, lost_retries = 0;
		DWORD last_detect = 0;
		while (!g.quit) {
			pump();
			if (g.quit) break;
			if (g.resize_pending) {
				g.resize_pending = false;
				if (!handle_resize()) full_reinit("resize failed");
			}
			if ((g.display_changed && GetTickCount() - last_detect > 500) || g.hdr_toggle_pending) {
				g.display_changed = false;
				g.hdr_toggle_pending = false;
				last_detect = GetTickCount();
				if (g.path == Path::INTEROP_D3D11) {
					// Always re-detect: the monitor may be the same but its HDR state/luminance may have changed.
					bool was_hdr = g.pres.hdr_presenting;
					if (!g.pres.reconfigure(g.cfg.hdr_enabled)) full_reinit("reconfigure failed");
					if (was_hdr != g.pres.hdr_presenting) logf("display: HDR presentation changed %d -> %d", was_hdr, g.pres.hdr_presenting);
				}
			}
			if (g.reinit_requested) { g.reinit_requested = false; full_reinit("user (R)"); }
			if (!render_frame()) {
				bool lost = g.pres.device_lost || g.iop.lost;
				if (++lost_retries > 3) { logf("fatal: repeated failure, giving up on interop"); shutdown_present(); start_wgl_sdr("repeated device loss"); lost_retries = 0; continue; }
				full_reinit(lost ? "device lost / interop lost" : "frame failure");
				continue;
			}
			lost_retries = 0;
			if (++frames == g.cfg.frames || (g.cfg.cycles > 0 && frames >= 3)) break;
			if (g.path == Path::WGL_SDR && !g_wgl.SwapIntervalEXT) Sleep(16);
		}
		logf("cycle %d: %d frames rendered, path=%s", c + 1, frames, g.path == Path::INTEROP_D3D11 ? "interop" : "wgl-sdr");
		shutdown_present();
	}
	shutdown_gl();
	destroy_main_window();
	logf("exit rc=%d", rc);
	log_close();
	return rc;
}
