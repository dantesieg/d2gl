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

#pragma once

#include "graphic/context.h"
#include "types.h"

// #define _DEBUG 1
// #define _HDTEXT 1

namespace d2gl {

// clang-format off
struct D2GLApp {
#ifdef _DEBUG
	const bool debug = true;
#else
	const bool debug = false;
#endif
	bool log = false;
	bool video_test = false;
	bool ready = false;
	bool direct = false;

	std::string menu_title = "D2GL";
	std::string version_str = "1.3.3";
	std::string ini_file = "d2gl.ini";
	std::string mpq_file = "d2gl.mpq";
	std::string log_file = "d2gl.log";

	Api api = Api::Glide;
	std::unique_ptr<Context> context;
	std::string gl_ver_str = "";
	bool vsync = true;
	uint32_t frame_latency = 1;

	// R0 instrumentation / R2 sync options: read from the ini once at startup, immutable afterwards.
	struct {
		bool log = false;               // perf_log: write d2gl_perf.csv + d2gl_perf_summary.jsonl
		uint32_t sample_every = 1;      // perf_sample_every: keep 1 row per N frames
		uint32_t summary_every = 600;   // perf_summary_every: frames per JSON summary line
		uint32_t gpu_sync = 0;          // gpu_sync: 0 legacy (wait this frame's fence), 1 deferred (UNMEASURED)
		bool early_release = false;     // early_slot_release: R1, free the slot before the GPU fence wait
		bool hdr_single_fence = false;  // hdr_single_fence: P3, shared-texture HDR: present()'s fence replaces the render-thread fence
		bool hdr_waitable = false;      // hdr_latency_waitable: P3, DXGI frame-latency waitable swapchain, max latency 1
		bool pace_to_refresh = false;   // pace_to_refresh: P3, snap the fps limiter period to the display refresh when within 3%
	} perf;
	bool d2fps_mod = false;

	GLCaps gl_caps;
	glm::vec<2, uint8_t> gl_ver = { 4, 6 };
	bool use_compute_shader = false;

	HMODULE hmodule = 0;
	WNDPROC wndproc = 0;
	HWND hwnd = 0;
	HDC hdc = 0;
	std::string dlls_early = "";
	std::string dlls_late = "";

	struct Window {
		DWORD style = 0;
		glm::ivec2 position = { 0, 0 };
		glm::uvec2 size = { 800, 600 };
		glm::uvec2 size_save = { 800, 600 };
		bool fullscreen = false;
		bool borderless = false;
		bool auto_minimize = false;
		bool centered = true;
		bool dark_mode = true;
		bool resized = false;
	} window;

	struct {
		glm::ivec2 offset = { 0, 0 };
		glm::uvec2 size = { 800, 600 };
		glm::vec2 scale = { 1.0f, 1.0f };
		glm::bvec2 stretched = { false, false };
	} viewport;

	struct {
		uint32_t bpp = 8;
		glm::uvec2 size = { 640, 480 };
		glm::uvec2 custom_size = { 0, 0 };
		GameScreen screen = GameScreen::Movie;
		DrawStage draw_stage = DrawStage::World;
		glm::uvec2 tex_size = { 1024, 512 };
		glm::vec2 tex_scale = { 1.0f, 1.0f };
	} game;

	struct {
		bool locked = false;   // the game should own the cursor
		bool clipped = false;  // our ClipCursor rect is actually installed
		glm::vec2 scale = { 1.0f, 1.0f };
		glm::vec2 unscale = { 1.0f, 1.0f };
		bool unlock = false;
	} cursor;

	Select<glm::uvec2> resolutions = {};
	glm::ivec4 desktop_resolution = { 0, 0, 0, 0 };

	struct ForegroundFPS {
		bool active = false;
		Range<int> range = { 60, 25, 240 };
		bool pacing = false;
	} foreground_fps;

	struct BackgroundFPS {
		bool active = true;
		Range<int> range = { 25, 25, 60 };
	} background_fps;

	struct {
		Select<std::string> presets = {};
		std::string preset = "bilinear.slangp";
		int selected = 0;
		bool world_only = false;
	} shader;

	Select<int> lut = {};

	struct {
		bool active = false;
		Select<int> presets = { 1, { { "Low", 0 }, { "Medium", 1 }, { "High", 2 } } };
	} fxaa;

	struct {
		bool active = false;
		Range<float> strength = { 1.16f, 0.1f, 3.0f };
		Range<float> clamp = { 0.15f, 0.0f, 1.0f };
		Range<float> radius = { 0.4f, 0.0f, 6.0f };
	} sharpen;

	struct {
		bool active = false;
		Range<float> exposure = { 1.2f, 1.0f, 2.0f };
		Range<float> gamma = { 1.0f, 0.5f, 1.2f };
	} bloom;

	struct {
		bool active = false;
		Range<float> intensity = { 0.1f, 0.0f, 0.3f };
		bool normals = false;
		Range<float> normals_strength = { 1.0f, 0.1f, 3.0f };
		Select<int> source = { 1, { { "Screen-Space", 0 }, { "Game Vertex Lighting", 1 } } };
		bool debug = false;
		bool depth_haze = false;
		Range<float> depth_haze_strength = { 0.5f, 0.0f, 1.0f };
		Range<float> spell_glow = { 0.5f, 0.0f, 1.5f };
	} dynamic_light;

	struct {
		bool active = false;
		Select<int> min_quality = { 1, { { "Magic", 0 }, { "Rare / Rune", 1 }, { "Set", 2 }, { "Unique", 3 } } };
	} loot_sparkle;

	bool minion_ring = false;


	struct {
		bool active = false;
		Range<float> exposure = { 0.5f, 0.2f, 1.0f };
	} tonemap;

	struct {
		bool active = false;
		Range<float> softness = { 0.8f, 0.3f, 1.5f };
	} soft_shadows;

	struct {
		bool active = false;
		Range<float> strength = { 0.5f, 0.1f, 1.5f };
	} radiance_gi;

	// HDR10 output ([HDR] in the ini). `mode` is decided once at startup from `enabled` (GL context on a
	// hidden window + DXGI presenter); `enabled` is then live (HDR10 vs SDR swapchain) while mode is on.
	struct {
		bool enabled = false;
		bool mode = false;
		Range<float> sdr_white = { 200.0f, 80.0f, 500.0f };
		bool peak_auto = true; // from IDXGIOutput6::GetDesc1 MaxLuminance, clamped to peak.min/max
		Range<float> peak = { 1000.0f, 400.0f, 2000.0f };
		Range<float> brightness = { 0.0f, -2.0f, 2.0f };   // stops
		Range<float> black_level = { 0.0f, -0.20f, 0.05f }; // signal lift (+) / crush (-)
		Range<float> contrast = { 1.0f, 0.8f, 1.2f };
		Range<float> saturation = { 1.0f, 0.5f, 1.5f };
		Range<float> highlight = { 0.0f, 0.0f, 1.0f };
		Range<float> knee = { 0.75f, 0.5f, 0.95f };
		Select<int> tonemap = { 1, { { "Clip", 0 }, { "BT.2390 roll-off", 1 } } };
		Select<int> eotf = { 0, { { "Gamma 2.2", 0 }, { "Gamma 2.4", 1 }, { "sRGB piecewise", 2 } } };
		bool calibration = false;
		bool float_pipeline = false; // [HDR] float_pipeline: RGBA16F scene buffers + transfer (restart)
		bool float_mode = false;     // decided once at startup: mode && float_pipeline
		bool emissive = false;                                  // HDR highlights for additive sprites (spells, fire)
		Range<float> emissive_nits = { 600.0f, 200.0f, 1000.0f }; // brightness of full emission
		Range<float> emissive_gain = { 1.5f, 0.5f, 4.0f };       // emission = clamp(additive luma * gain)
		Range<float> movie_nits = { 0.0f, 0.0f, 500.0f }; // paper white of movies/menus/loading; 0 = sdr_white
		bool shared_texture = true; // GL_EXT_memory_object_win32 zero-copy; false forces the PBO path
	} hdr;

	struct {
		bool active = false;
		Range<float> scale = { 1.0f, 0.8f, 1.5f };
		Range<int> bg_opacity = { 80, 0, 100 }; // % for hover/tooltip boxes (was fixed 0xCC)
	} hd_text;

	bool hd_cursor = false;
	bool motion_prediction = false;
	bool skip_intro = false;
	bool no_pickup = false;
	bool show_item_quantity = false;
	bool show_monster_res = false;
	bool show_fps = false;

	struct {
		bool active = false;
		bool centered = false;
	} hd_orbs;

	struct {
		bool active = false;
		bool available = true;
		bool text_over = true;
		Range<int> width = { 200, 200, 300 };
		Range<int> height = { 140, 100, 200 };
		Range<int> opacity = { 100, 10, 100 }; // %
	} mini_map;

#ifdef _DEBUG
	int var[10] = { 0 };
#endif
#ifdef _HDTEXT
	struct {
		Select<int> fonts = {};
		Range<float> size = { 10.0f, 5.0f, 40.0f };
		Range<float> weight = { 1.0f, 0.5f, 1.5f };
		Range<float> letter_spacing = { 0.0f, -1.0f, 1.0f };
		Range<float> line_height = { 1.0f, 0.0f, 3.0f };
		Range<float> shadow_intensity = { 0.35f, 0.0f, 1.0f };
		Range<float> offset_x = { 0.0f, -10.0f, 10.0f };
		Range<float> offset_y = { 0.0f, -10.0f, 10.0f };
		Range<float> symbol_offset = { 0.0f, -5.0f, 5.0f };
		bool show_sample = false;
	} hdt;
#endif
};
// clang-format on

extern D2GLApp App;

void dllAttach(HMODULE hmodule);
void dllDetach();

constexpr inline bool ISGLIDE3X()
{
#ifdef GLIDE3X_EXPORTS
	return true;
#else
	return false;
#endif
}

constexpr inline float FLOATVAL(float glide3x, float ddraw)
{
#ifdef GLIDE3X_EXPORTS
	return glide3x;
#else
	return ddraw;
#endif
}

constexpr inline bool ISHDTEXT()
{
#ifdef _HDTEXT
	return true;
#else
	return false;
#endif
}

}