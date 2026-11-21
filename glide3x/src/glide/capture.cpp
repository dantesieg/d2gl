/*
	D2GL: Diablo 2 LoD Glide/DDraw to OpenGL Wrapper.
	Copyright (C) 2023  Bayaraa

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "pch.h"
#include "capture.h"
#include "d2/common.h"
#include "graphic/hires_pack.h"
#include "helpers.h"
#include "option/ini.h"

namespace d2gl::capture {

#pragma pack(push, 1)
struct Header {
	char magic[8];        // "D2GLCAP\0"
	uint32_t version;     // VERSION
	uint32_t header_size; // sizeof(Header); the ini text follows
	uint32_t tex_mem;     // glide texture memory per TMU (bytes), 3 TMUs
	uint32_t game_w, game_h;
	uint32_t window_w, window_h;
	uint32_t screen_res;  // last GrScreenResolution_t of grSstWinOpen
	uint32_t screen;      // GameScreen
	uint32_t frames;      // frames requested
	uint32_t d2_version;  // helpers::getVersion()
	uint32_t flags;       // features on while capturing that draw outside glide (see FLAG_*)
	uint32_t ini_size;    // bytes of d2gl.ini text after the header
};
struct RecHead {
	uint16_t op;
	uint16_t nargs;     // u32 words after the head
	uint32_t blob_size; // bytes after the args
};
#pragma pack(pop)
static_assert(sizeof(Header) == 60 && sizeof(RecHead) == 8, "capture format");

enum : uint32_t {
	FLAG_HD_TEXT = 1,
	FLAG_HD_CURSOR = 2,
	FLAG_MINI_MAP = 4,
	FLAG_MOTION_PREDICTION = 8,
	FLAG_LOOT_SPARKLE = 16,
	FLAG_SILHOUETTES = 32,
	FLAG_HIRES = 64,
};

bool g_on = false;

static FILE* g_file = nullptr;
static std::vector<uint8_t> g_buf;
static std::vector<uint8_t> g_shadow[OP_COUNT];
static uint32_t g_frames_left = 0, g_poll = 0, g_screen_res = GR_RESOLUTION_640x480;
static uint32_t g_game[5] = { ~0u, ~0u, ~0u, ~0u, ~0u };
static void* g_lfb_ptr = nullptr;
static uint32_t g_lfb_mode = 0, g_lfb_origin = 0;
static uint64_t g_bytes = 0;

static void encode(std::vector<uint8_t>& out, Op op, std::initializer_list<uint32_t> args, const void* blob, uint32_t blob_size)
{
	const RecHead head = { (uint16_t)op, (uint16_t)args.size(), blob ? blob_size : 0 };
	const size_t at = out.size();
	out.resize(at + sizeof(RecHead) + args.size() * 4 + head.blob_size);
	uint8_t* p = out.data() + at;
	memcpy(p, &head, sizeof(RecHead));
	p += sizeof(RecHead);
	for (const uint32_t a : args) {
		memcpy(p, &a, 4);
		p += 4;
	}
	if (head.blob_size)
		memcpy(p, blob, head.blob_size);
}

static void flush()
{
	if (g_file && !g_buf.empty()) {
		fwrite(g_buf.data(), 1, g_buf.size(), g_file);
		g_bytes += g_buf.size();
	}
	g_buf.clear();
}

static bool isState(Op op)
{
	switch (op) {
		case OP_ALPHA_BLEND:
		case OP_ALPHA_COMBINE:
		case OP_CHROMAKEY_MODE:
		case OP_COLOR_COMBINE:
		case OP_CONSTANT_COLOR:
		case OP_GAMMA_TABLE:
		case OP_GAMMA_RGB:
		case OP_TEX_SOURCE:
		case OP_TEX_TABLE:
			return true;
	}
	return false;
}

static void gameState()
{
	const uint32_t g[5] = {
		(uint32_t)App.game.screen,
		d2::screen_shift ? *d2::screen_shift : 0,
		d2::automap_on ? *d2::automap_on : 0,
		d2::level_no ? (uint32_t)*d2::level_no : 0,
		d2::is_in_game ? *d2::is_in_game : 0,
	};
	if (memcmp(g, g_game, sizeof(g)) == 0)
		return;
	memcpy(g_game, g, sizeof(g));
	encode(g_buf, OP_GAME, { g[0], g[1], g[2], g[3], g[4] }, nullptr, 0);
}

void rec(Op op, std::initializer_list<uint32_t> args, const void* blob, uint32_t blob_size)
{
	if (op == OP_SST_WIN_OPEN)
		g_screen_res = *args.begin();
	if (isState(op)) {
		auto& s = g_shadow[op == OP_GAMMA_RGB ? OP_GAMMA_TABLE : op]; // the later of the two gamma calls wins
		s.clear();
		encode(s, op, args, blob, blob_size);
	}
	if (!g_on)
		return;
	gameState();
	encode(g_buf, op, args, blob, blob_size);
	if (g_buf.size() >= (8u << 20))
		flush();
}

void recDrawArray(FxU32 mode, FxU32 count, void** pointers)
{
	if (!g_on)
		return;
	static std::vector<GlideVertex> tmp;
	tmp.resize(count);
	for (FxU32 i = 0; i < count; i++)
		tmp[i] = *(const GlideVertex*)pointers[i];
	rec(OP_DRAW_ARRAY, { mode, count }, tmp.data(), count * (uint32_t)sizeof(GlideVertex));
}

void lfbLocked(const GrLfbInfo_t* info)
{
	g_lfb_ptr = info ? info->lfbPtr : nullptr;
	g_lfb_mode = info ? (uint32_t)info->writeMode : 0;
	g_lfb_origin = info ? (uint32_t)info->origin : 0;
}

void lfbUnlocked()
{
	if (g_on && g_lfb_ptr)
		rec(OP_LFB_FRAME, { g_lfb_mode, g_lfb_origin, 640 * 4 }, g_lfb_ptr, 640 * 480 * 4);
}

static void stageHook()
{
	if (g_on)
		rec(OP_STAGE, { (uint32_t)App.game.draw_stage });
}

static void start(uint32_t frames)
{
	char name[64] = { 0 };
	for (int i = 1; i < 1000; i++) {
		sprintf_s(name, "d2gl_capture_%03d.gcap", i);
		if (!helpers::fileExists(name))
			break;
	}
	if (fopen_s(&g_file, name, "wb") || !g_file) {
		error_log("glide_capture: cannot create %s", name);
		g_file = nullptr;
		return;
	}
	setvbuf(g_file, nullptr, _IOFBF, 1 << 20);

	std::string ini;
	{
		std::ifstream f(App.ini_file, std::ios::binary);
		if (f)
			ini.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	}

	Header h = {};
	memcpy(h.magic, "D2GLCAP", 8);
	h.version = VERSION;
	h.header_size = sizeof(Header);
	h.tex_mem = GLIDE_TEX_MEMORY;
	h.game_w = App.game.size.x, h.game_h = App.game.size.y;
	h.window_w = App.window.size.x, h.window_h = App.window.size.y;
	h.screen_res = g_screen_res;
	h.screen = (uint32_t)App.game.screen;
	h.frames = frames;
	h.d2_version = (uint32_t)helpers::getVersion();
	h.flags = (App.hd_text.active ? FLAG_HD_TEXT : 0) | (App.hd_cursor ? FLAG_HD_CURSOR : 0) | (App.mini_map.active ? FLAG_MINI_MAP : 0) |
		(App.motion_prediction ? FLAG_MOTION_PREDICTION : 0) | (App.loot_sparkle.active ? FLAG_LOOT_SPARKLE : 0) | (App.silhouettes ? FLAG_SILHOUETTES : 0) |
		(hires::g_count ? FLAG_HIRES : 0);
	h.ini_size = (uint32_t)ini.size();
	fwrite(&h, sizeof(h), 1, g_file);
	fwrite(ini.data(), 1, ini.size(), g_file);
	g_bytes = sizeof(h) + ini.size();

	// snapshot: current state, then every texture the game has downloaded, the game state, the bound texture last
	g_buf.clear();
	static const Op order[] = { OP_GAMMA_TABLE, OP_TEX_TABLE, OP_CONSTANT_COLOR, OP_COLOR_COMBINE, OP_ALPHA_COMBINE, OP_CHROMAKEY_MODE, OP_ALPHA_BLEND };
	for (const Op op : order)
		g_buf.insert(g_buf.end(), g_shadow[op].begin(), g_shadow[op].end());
	const size_t total = (size_t)GLIDE_TEX_MEMORY * GLIDE_MAX_NUM_TMU;
	uint32_t snap = 0;
	for (const auto& [addr, slot] : g_glide_texture.slots) {
		const size_t size = (size_t)slot.w * slot.h;
		if (!size || addr + size > total)
			continue;
		encode(g_buf, OP_SNAP_TEX, { addr, slot.w, slot.h, slot.hash }, g_glide_texture.memory + addr, (uint32_t)size);
		snap++;
		if (g_buf.size() >= (8u << 20))
			flush();
	}
	memset(g_game, 0xFF, sizeof(g_game));
	gameState();
	g_buf.insert(g_buf.end(), g_shadow[OP_TEX_SOURCE].begin(), g_shadow[OP_TEX_SOURCE].end());
	flush();

	g_frames_left = frames;
	g_on = true;
	g_stage_hook = stageHook;
	trace_log("glide_capture: recording %u frames to %s (%u textures in the snapshot)", frames, name, snap);
}

static void stop()
{
	encode(g_buf, OP_END, {}, nullptr, 0);
	flush();
	fclose(g_file);
	g_file = nullptr;
	g_on = false;
	g_stage_hook = nullptr;
	trace_log("glide_capture: done, %llu bytes", (unsigned long long)g_bytes);
}

void onFrame()
{
	if (g_on) {
		if (--g_frames_left == 0)
			stop();
		return;
	}
	if (++g_poll % 30)
		return;
	static const char* trigger = "d2gl_capture";
	if (GetFileAttributesA(trigger) == INVALID_FILE_ATTRIBUTES)
		return;
	int frames = 0;
	{
		std::ifstream f(trigger);
		f >> frames;
	}
	DeleteFileA(trigger);
	start((uint32_t)(frames > 0 ? glm::min(frames, 3600) : App.glide_capture_frames));
}

// ---- replay (d2gl_replay.exe loads this DLL with D2GL_REPLAY=1 and calls d2gl_replay) ----

static void pumpMessages()
{
	MSG msg;
	while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}
}

static int replay(const char* cap_path, const char* out_dir, const char* ini_path, int max_frames, int width, int height)
{
	if (!App.replay.active)
		return 10; // loaded without D2GL_REPLAY=1: the DLL attached as in the game

	std::vector<uint8_t> file;
	{
		std::ifstream f(cap_path, std::ios::binary | std::ios::ate);
		if (!f)
			return 11;
		file.resize((size_t)f.tellg());
		f.seekg(0);
		f.read((char*)file.data(), file.size());
	}
	if (file.size() < sizeof(Header))
		return 12;
	Header h;
	memcpy(&h, file.data(), sizeof(h));
	if (memcmp(h.magic, "D2GLCAP", 8) != 0 || h.version != VERSION || h.header_size < sizeof(Header) || (uint64_t)h.header_size + h.ini_size > file.size())
		return 13;

	// settings: the ini embedded in the capture (or --ini), as d2gl_replay.ini in the working directory
	App.ini_file = "d2gl_replay.ini";
	if (ini_path && *ini_path) {
		if (!CopyFileA(ini_path, App.ini_file.c_str(), FALSE))
			return 14;
	} else {
		std::ofstream f(App.ini_file, std::ios::binary | std::ios::trunc);
		f.write((const char*)file.data() + h.header_size, h.ini_size);
	}
	App.log = true;
	logInit();
	option::loadIni();

	// features that read live game memory or draw outside glide have nothing to work with here
	App.glide_capture = false;
	App.hdr.enabled = App.hdr.mode = false;
	App.vsync = false;
	App.foreground_fps.active = App.background_fps.active = false;
	App.motion_prediction = false;
	App.hd_text.active = false;
	App.hd_cursor = false;
	App.mini_map.active = false;
	App.loot_sparkle.active = false;
	App.show_fps = false;
	App.perf.log = false;
	App.dlls_early = App.dlls_late = "";
	App.cursor.unlock = true;
	App.frame_latency = 1;
	App.window.fullscreen = false;
	App.window.centered = true;
	App.window.size = { (uint32_t)(width > 0 ? width : h.window_w), (uint32_t)(height > 0 ? height : h.window_h) };
	App.glide_tex_mb = glm::clamp((int)(h.tex_mem >> 20), 16, 64);
	if ((uint32_t)App.glide_tex_mb << 20 != h.tex_mem)
		return 15;

	d2::replayStubs();
	*d2::screen_width = h.game_w;
	*d2::screen_height = h.game_h;
	*d2::is_in_game = 0; // first open as from the menu (an in-game open clears the wrapper cache)
	if (h.screen_res != GR_RESOLUTION_640x480 && h.screen_res != GR_RESOLUTION_800x600 && h.screen_res != GR_RESOLUTION_1024x768)
		App.game.custom_size = { h.game_w, h.game_h };

	WNDCLASSA wc = {};
	wc.style = CS_OWNDC;
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = "d2gl_replay";
	RegisterClassA(&wc);
	HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "D2GL replay", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, App.window.size.x, App.window.size.y, NULL, NULL, wc.hInstance, NULL);
	if (!hwnd)
		return 16;

	App.wndproc = DefWindowProcA; // Context::beginFrame would subclass the window with the game's input handling
	Wrapper::grSstWinOpen((FxU32)hwnd, (GrScreenResolution_t)h.screen_res);
	if (!App.context || !GlideWrapper)
		return 17;
	App.game.screen = (GameScreen)h.screen;
	App.replay.out_dir = out_dir;
	App.replay.shots_wanted = UINT32_MAX;
	pumpMessages();

	printf("capture: %ux%u game, %ux%u window, %u frames requested, flags 0x%X\n", h.game_w, h.game_h, App.window.size.x, App.window.size.y, h.frames, h.flags);
	if (h.flags & (FLAG_HD_TEXT | FLAG_HD_CURSOR | FLAG_MINI_MAP | FLAG_LOOT_SPARKLE | FLAG_SILHOUETTES))
		printf("note: HD text / HD cursor / minimap / loot sparkle / silhouettes were on; they draw outside glide and are missing here\n");

	const uint32_t frame_base = App.context->getFrameCount();
	const uint32_t limit = max_frames > 0 ? (uint32_t)max_frames : UINT32_MAX;
	uint32_t frames = 0;
	size_t pos = (size_t)h.header_size + h.ini_size;
	std::vector<GlideVertex> verts;
	std::vector<void*> ptrs;
	std::vector<FxU32> words;
	const size_t tex_total = (size_t)GLIDE_TEX_MEMORY * GLIDE_MAX_NUM_TMU;
	int rc = 0;

	while (frames < limit) {
		if (pos + sizeof(RecHead) > file.size()) {
			rc = 1; // truncated capture (game closed while recording): what was read is still replayed
			break;
		}
		RecHead rh;
		memcpy(&rh, file.data() + pos, sizeof(rh));
		const size_t need = sizeof(RecHead) + (size_t)rh.nargs * 4 + rh.blob_size;
		if (pos + need > file.size()) {
			rc = 1;
			break;
		}
		uint32_t a[8] = { 0 };
		memcpy(a, file.data() + pos + sizeof(RecHead), (size_t)glm::min<uint32_t>(rh.nargs, 8) * 4);
		const uint8_t* blob = file.data() + pos + sizeof(RecHead) + (size_t)rh.nargs * 4;
		const uint32_t bs = rh.blob_size;
		pos += need;
		if (rh.op == OP_END)
			break;

		switch (rh.op) {
			case OP_BUFFER_CLEAR:
				GlideWrapper->onBufferClear();
				break;
			case OP_BUFFER_SWAP:
				GlideWrapper->onBufferSwap();
				frames++;
				pumpMessages();
				break;
			case OP_DRAW_POINT:
				if (bs >= sizeof(GlideVertex)) {
					GlideVertex v;
					memcpy(&v, blob, sizeof(v));
					GlideWrapper->grDrawPoint(&v);
				}
				break;
			case OP_DRAW_LINE:
				if (bs >= 2 * sizeof(GlideVertex)) {
					GlideVertex v[2];
					memcpy(v, blob, sizeof(v));
					GlideWrapper->grDrawLine(&v[0], &v[1]);
				}
				break;
			case OP_DRAW_ARRAY:
			case OP_DRAW_ARRAY_CONTIG: {
				const uint32_t count = glm::min<uint32_t>(a[1], bs / (uint32_t)sizeof(GlideVertex));
				if (!count)
					break;
				verts.resize(count + 1);
				memcpy(verts.data(), blob, (size_t)count * sizeof(GlideVertex));
				verts[count] = verts[count - 1]; // strips read one past an odd count
				if (rh.op == OP_DRAW_ARRAY_CONTIG)
					GlideWrapper->grDrawVertexArrayContiguous(a[0], count, verts.data());
				else {
					ptrs.resize(count + 1);
					for (uint32_t i = 0; i <= count; i++)
						ptrs[i] = &verts[i];
					GlideWrapper->grDrawVertexArray(a[0], count, ptrs.data());
				}
				break;
			}
			case OP_ALPHA_BLEND:
				GlideWrapper->grAlphaBlendFunction((GrAlphaBlendFnc_t)a[1]);
				break;
			case OP_ALPHA_COMBINE:
				GlideWrapper->grAlphaCombine((GrCombineFunction_t)a[0], (GrCombineLocal_t)a[2]);
				break;
			case OP_CHROMAKEY_MODE:
				GlideWrapper->grChromakeyMode((GrChromakeyMode_t)a[0]);
				break;
			case OP_COLOR_COMBINE:
				GlideWrapper->grColorCombine((GrCombineFunction_t)a[0], (GrCombineLocal_t)a[2]);
				break;
			case OP_CONSTANT_COLOR:
				GlideWrapper->grConstantColorValue((GrColor_t)a[0]);
				break;
			case OP_GAMMA_TABLE: {
				const uint32_t n = glm::min<uint32_t>(a[0], bs / 12);
				words.assign((const uint32_t*)blob, (const uint32_t*)blob + n * 3);
				GlideWrapper->grLoadGammaTable(n, words.data(), words.data() + n, words.data() + 2 * n);
				break;
			}
			case OP_GAMMA_RGB: {
				float f[3];
				memcpy(f, a, sizeof(f));
				GlideWrapper->guGammaCorrectionRGB(f[0], f[1], f[2]);
				break;
			}
			case OP_TEX_SOURCE:
			case OP_TEX_DOWNLOAD: {
				GrTexInfo info = { (GrLOD_t)a[3], (GrLOD_t)a[4], (GrAspectRatio_t)a[5], (GrTextureFormat_t)a[6], nullptr };
				if (rh.op == OP_TEX_SOURCE) {
					GlideWrapper->grTexSource((GrChipID_t)a[0], a[1], &info);
					break;
				}
				uint32_t w, hh;
				Wrapper::getTexSize(&info, w, hh);
				if (a[0] >= GLIDE_MAX_NUM_TMU || (size_t)w * hh > bs || (size_t)GLIDE_TEX_MEMORY * a[0] + a[1] + (size_t)w * hh > tex_total)
					break;
				info.data = (void*)blob;
				GlideWrapper->grTexDownloadMipMap((GrChipID_t)a[0], a[1], &info);
				break;
			}
			case OP_TEX_TABLE:
				if (bs >= 1024) {
					words.assign((const uint32_t*)blob, (const uint32_t*)blob + 256);
					GlideWrapper->grTexDownloadTable(words.data());
				}
				break;
			case OP_LFB_FRAME: {
				GrLfbInfo_t info = { (int)sizeof(GrLfbInfo_t) };
				if (GlideWrapper->grLfbLock((GrLfbWriteMode_t)a[0], (GrOriginLocation_t)a[1], &info) && info.lfbPtr)
					memcpy(info.lfbPtr, blob, glm::min<uint32_t>(bs, 640 * 480 * 4));
				GlideWrapper->grLfbUnlock();
				frames++;
				pumpMessages();
				break;
			}
			case OP_SST_WIN_OPEN:
				Wrapper::grSstWinOpen((FxU32)hwnd, (GrScreenResolution_t)a[0]);
				break;
			case OP_GAME:
				App.game.screen = (GameScreen)a[0];
				*d2::screen_shift = a[1];
				*d2::automap_on = a[2];
				*d2::level_no = (int)a[3];
				*d2::is_in_game = a[4];
				break;
			case OP_STAGE:
				if ((DrawStage)a[0] != App.game.draw_stage) {
					App.game.draw_stage = (DrawStage)a[0];
					App.context->onStageChange();
				}
				break;
			case OP_SNAP_TEX: {
				const uint32_t addr = a[0], w = a[1], hh = a[2];
				if (!w || !hh || (size_t)w * hh > bs || addr + (size_t)w * hh > tex_total)
					break;
				memcpy(g_glide_texture.memory + addr, blob, (size_t)w * hh);
				GlideSlot& slot = g_glide_texture.slots[addr];
				slot.hash = a[3];
				slot.w = (uint16_t)w, slot.h = (uint16_t)hh;
				slot.retry = nullptr;
				slot.hires = nullptr;
				if (hires::g_count) {
					hires::g_on[0] = App.hires_ui, hires::g_on[1] = App.hires_textures, hires::g_on[2] = App.hires_items, hires::g_on[3] = App.hires_ui;
					slot.hires = hires::find(slot.hash, w, hh);
				}
				break;
			}
			default:
				break; // unknown op from a newer writer: skipped by its size
		}
	}

	// every presented frame gets a screenshot; flush the frame queue with blank frames that get none
	const uint32_t presented = App.context->getFrameCount() - frame_base;
	App.replay.shots_wanted = presented;
	for (uint32_t i = 0; i < App.frame_latency + 2; i++) {
		GlideWrapper->onBufferClear();
		GlideWrapper->onBufferSwap();
		pumpMessages();
	}
	for (int i = 0; i < 1500 && (uint32_t)App.replay.shots_done < presented; i++) {
		pumpMessages();
		Sleep(10);
	}
	printf("replayed %u frames, %ld screenshots in %s\n", frames, (long)App.replay.shots_done, out_dir);
	return (uint32_t)App.replay.shots_done < presented ? 18 : rc;
}

}

extern "C" __declspec(dllexport) int __cdecl d2gl_replay(const char* capture_path, const char* out_dir, const char* ini_path, int max_frames, int width, int height)
{
	return d2gl::capture::replay(capture_path, out_dir, ini_path, max_frames, width, height);
}
