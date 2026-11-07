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
#include "ini.h"
#include "helpers.h"

namespace d2gl::option {

#define boolString(a) (a ? "true" : "false")

bool getValue(LPCSTR section, LPCSTR key, LPSTR return_string, DWORD size)
{
	return GetPrivateProfileStringA(section, key, "", return_string, size, App.ini_file.c_str()) > 0;
}

bool getBool(LPCSTR section, LPCSTR key, bool def)
{
	char value[10];
	if (getValue(section, key, value, 10))
		return (std::string(value) == "true" || std::string(value) == "1");

	return def;
}

int getInt(LPCSTR section, LPCSTR key, int def, int min, int max)
{
	char value[10] = { 0 };
	if (getValue(section, key, value, 10)) {
		int ret = def;
		try {
			ret = std::atoi(value);
		} catch (const std::invalid_argument& e) {}

		if (ret < min)
			return min;
		if (ret > max)
			return max;

		return ret;
	}
	return def;
}

float getFloat(LPCSTR section, LPCSTR key, Range<float> val)
{
	char value[10] = { 0 };
	if (getValue(section, key, value, 10)) {
		float ret = val.value;
		try {
			ret = std::stof(value);
		} catch (const std::invalid_argument& e) {}

		if (ret < val.min)
			return val.min;
		if (ret > val.max)
			return val.max;

		return ret;
	}
	return val.value;
}

std::string getString(LPCSTR section, LPCSTR key, const std::string& def)
{
	char value[2048] = { 0 };
	if (getValue(section, key, value, sizeof(value))) {
		return std::string(value);
	}
	return def;
}

bool saveValue(LPCSTR section, LPCSTR key, LPCSTR value_string)
{
	return WritePrivateProfileStringA(section, key, value_string, App.ini_file.c_str()) > 0;
}

bool saveBool(LPCSTR section, LPCSTR key, bool val)
{
	char value[10] = { 0 };
	sprintf_s(value, "%s", (val ? "true" : "false"));
	return saveValue(section, key, value);
}

bool saveInt(LPCSTR section, LPCSTR key, int val)
{
	char value[10] = { 0 };
	sprintf_s(value, "%d", val);
	return saveValue(section, key, value);
}

bool saveFloat(LPCSTR section, LPCSTR key, float val)
{
	char value[10] = { 0 };
	sprintf_s(value, "%.3f", val);
	return saveValue(section, key, value);
}

bool saveString(LPCSTR section, LPCSTR key, const std::string& val)
{
	return saveValue(section, key, val.c_str());
}

void saveIni()
{
	static char buf[16384]; buf[0] = 0;   // was 3400: long load_dlls lines overflowed sprintf_s
	std::ofstream out_file;
	out_file.open(App.ini_file);

	static const char* screen_setting =
		"; ==== D2GL Configs ====\n\n"
		"; Recommended: (ctrl+O) opens in-game option menu.\n"
		"; All settings except \"Other\" section can be changed there.\n\n\n"
		"[Screen]\n\n"
		"; Game will run in fullscreen window.\n"
		"; Window size (window_width/window_height) will be ignored.\n"
		"fullscreen=%s\n\n"
		"; Borderless window (no title bar, borders or caption buttons).\n"
		"; Ignored while fullscreen is enabled.\n"
		"borderless_window=%s\n\n"
		"; Window size.\n"
		"window_width=%d\n"
		"window_height=%d\n\n"
		"; Always centered window on launch.\n"
		"; Window position (window_posx/window_posy) will be ignored.\n"
		"centered_window=%s\n\n"
		"; Window position.\n"
		"window_posx=%d\n"
		"window_posy=%d\n\n"
		"; Unlock Cursor (cursor will not locked within window).\n"
		"unlock_cursor=%s\n\n"
		"; Auto minimize when lose focus while in fullscreen.\n"
		"auto_minimize=%s\n\n"
		"; Dark style window title bar.\n"
		"dark_mode=%s\n\n"
		"; Vertical synchronization.\n"
		"; Game fps adapt screen refresh rate (might have input lag).\n"
		"vsync=%s\n\n"
		"; Max Foreground FPS (if vsync is on this will be ignored).\n"
		"; Limit maximum fps when game window is focused(active) (vsync must be disabled).\n"
		"foreground_fps=%s\n"
		"foreground_fps_value=%d\n"
		"; Frame pacing: precise frame timing for the fps limit (even frame times, resyncs when late).\n"
		"foreground_fps_pacing=%s\n\n"
		"; Max Background FPS.\n"
		"; Limit maximum fps when game window is in background(inactive).\n"
		"background_fps=%s\n"
		"background_fps_value=%d\n\n\n";

	sprintf_s(buf, screen_setting,
		boolString(App.window.fullscreen),
		boolString(App.window.borderless),
		App.window.size.x,
		App.window.size.y,
		boolString(App.window.centered),
		App.window.position.x,
		App.window.position.y,
		boolString(App.cursor.unlock),
		boolString(App.window.auto_minimize),
		boolString(App.window.dark_mode),
		boolString(App.vsync),
		boolString(App.foreground_fps.active),
		App.foreground_fps.range.value,
		boolString(App.foreground_fps.pacing),
		boolString(App.background_fps.active),
		App.background_fps.range.value);
	out_file << buf;

	static const char* graphic_setting =
		"[Graphic]\n\n"
		"; Upscale shader.\n"
		"; RetroArch's slang shader preset files (.slangp).\n"
		"shader_preset=%s\n"
		"; Apply the shader preset to the game world only; UI and text stay crisp (glide mode).\n"
		"shader_world_only=%s\n\n"
		"; Color grading (LUT) (only available in glide mode).\n"
		"; Set one of 1-%d predefined luts. 0 = game default.\n"
		"lut=%d\n\n"
		"; Luma sharpen.\n"
		"sharpen=%s\n"
		"sharpen_strength=%.3f\n"
		"sharpen_clamp=%.3f\n"
		"sharpen_radius=%.3f\n"
		"; Fast approximate anti-aliasing (preset: 0-2).\n"
		"fxaa=%s\n"
		"fxaa_preset=%d\n\n"
		"; Bloom effect.\n"
		"bloom=%s\n"
		"bloom_exposure=%.3f\n"
		"bloom_gamma=%.3f\n\n"
		"; Dynamic lighting: light sources glow and light the world around them.\n"
		"dynamic_light=%s\n"
		"dynamic_light_intensity=%.3f\n"
		"; Normal-mapped lighting: pseudo normals from the image, shaded toward lights (needs dynamic_light).\n"
		"light_normals=%s\n"
		"light_normals_strength=%.3f\n"
		"; Light source for dynamic_light: 0 = screen-space (bright pixels), 1 = game vertex lighting.\n"
		"dynamic_light_source=%d\n"
		"; Debug: show the captured game light buffer in place of the world (needs dynamic_light).\n"
		"dynamic_light_debug=%s\n"
		"; Light depth haze: dark areas fade into a cool haze (needs game vertex lighting, strength: 0.0-1.0).\n"
		"light_depth_haze=%s\n"
		"light_depth_haze_strength=%.3f\n"
		"; Spell glow: how much spells and fire light the world (game vertex lighting, 0.0-1.5).\n"
		"spell_glow=%.3f\n\n"
		"; HDR-style tonemapping (world only, exposure: 0.2-1.0).\n"
		"tonemap=%s\n"
		"tonemap_exposure=%.3f\n\n"
		"; Soft unit shadows: blurred shadows multiplied into the world (softness: 0.3-1.5).\n"
		"soft_shadows=%s\n"
		"soft_shadows_softness=%.3f\n\n"
		"; Stretch viewport to window size.\n"
		"stretched_horizontal=%s\n"
		"stretched_vertical=%s\n\n\n";

	sprintf_s(buf, graphic_setting,
		App.shader.preset.c_str(),
		boolString(App.shader.world_only),
		App.lut.items.size() - 1,
		App.lut.selected,
		boolString(App.sharpen.active),
		App.sharpen.strength.value,
		App.sharpen.clamp.value,
		App.sharpen.radius.value,
		boolString(App.fxaa.active),
		App.fxaa.presets.selected,
		boolString(App.bloom.active),
		App.bloom.exposure.value,
		App.bloom.gamma.value,
		boolString(App.dynamic_light.active),
		App.dynamic_light.intensity.value,
		boolString(App.dynamic_light.normals),
		App.dynamic_light.normals_strength.value,
		App.dynamic_light.source.selected,
		boolString(App.dynamic_light.debug),
		boolString(App.dynamic_light.depth_haze),
		App.dynamic_light.depth_haze_strength.value,
		App.dynamic_light.spell_glow.value,
		boolString(App.tonemap.active),
		App.tonemap.exposure.value,
		boolString(App.soft_shadows.active),
		App.soft_shadows.softness.value,
		boolString(App.viewport.stretched.x),
		boolString(App.viewport.stretched.y));
	out_file << buf;

	static const char* feature_setting =
		"[Feature]\n\n"
		"; HD cursor in game & menu screen.\n"
		"hd_cursor=%s\n\n"
		"; HD in game text.\n"
		"hd_text=%s\n"
		"hd_text_scale=%.3f\n"
		"; HD text hover/tooltip box opacity in percent (0-100).\n"
		"hd_text_bg_opacity=%d\n\n"
		//"; HD life & mana orbs.\n"
		//"hd_orbs=%s\n"
		//"hd_orbs_centered=%s\n\n"
		"; Always on Minimap widget (only available in glide mode).\n"
		"mini_map=%s\n"
		"mini_map_text_over=%s\n"
		"mini_map_width=%d\n"
		"mini_map_height=%d\n"
		"; Minimap opacity in percent (10-100).\n"
		"mini_map_opacity=%d\n\n"
		"; D2DX's Motion Prediction.\n"
		"motion_prediction=%s\n\n"
		"; Loot sparkle: short sparkle when a valuable item drops (glide only).\n"
		"; loot_sparkle_min_quality: 0 magic, 1 rare/rune, 2 set, 3 unique.\n"
		"loot_sparkle=%s\n"
		"loot_sparkle_min_quality=%d\n\n"
		"; Minion ring: soft green ring under your summons (glide only, 1.14d).\n"
		"minion_ring=%s\n\n"
		"; Skip the Intro videos.\n"
		"skip_intro=%s\n\n"
		"; Auto /nopickup option on launch (exclude 1.09d).\n"
		"no_pickup=%s\n\n"
		"; Show item quantity on bottom left corner of icon.\n"
		"show_item_quantity=%s\n\n"
		"; Show monster resistances on hp bar.\n"
		"show_monster_res=%s\n\n"
		"; Show FPS Counter (bottom center).\n"
		"show_fps=%s\n\n\n";

	sprintf_s(buf, feature_setting,
		boolString(App.hd_cursor),
		boolString(App.hd_text.active),
		App.hd_text.scale.value,
		App.hd_text.bg_opacity.value,
		// boolString(App.hd_orbs.active),
		// boolString(App.hd_orbs.centered),
		boolString(App.mini_map.active),
		boolString(App.mini_map.text_over),
		App.mini_map.width.value,
		App.mini_map.height.value,
		App.mini_map.opacity.value,
		boolString(App.motion_prediction),
		boolString(App.loot_sparkle.active),
		App.loot_sparkle.min_quality.selected,
		boolString(App.minion_ring),
		boolString(App.skip_intro),
		boolString(App.no_pickup),
		boolString(App.show_item_quantity),
		boolString(App.show_monster_res),
		boolString(App.show_fps));
	out_file << buf;

	static const char* hdr_setting =
		"[HDR]\n\n"
		"; HDR10 output through D3D11/DXGI (Windows HDR on, or Steam Deck OLED / gamescope Game Mode with\n"
		"; Proton; D2GL sets DXVK_HDR=1 itself). Enabling or disabling needs a game restart; when the\n"
		"; display is not in HDR mode the same path presents SDR. The SDR look is the reference: SDR white\n"
		"; maps to sdr_white nits.\n"
		"hdr=%s\n"
		"; Paper/UI white in nits (80-500).\n"
		"sdr_white=%.1f\n"
		"; Peak brightness in nits (400-2000); 0 = auto (display MaxLuminance).\n"
		"peak_nits=%.1f\n"
		"; Overall brightness in stops (-2.0 to 2.0).\n"
		"brightness=%.3f\n"
		"; Black level: + lifts, - crushes (-0.05 to 0.05).\n"
		"black_level=%.3f\n"
		"; Contrast around mid-grey (0.8-1.2).\n"
		"contrast=%.3f\n"
		"; Saturation, luminance preserving (0.5-1.5).\n"
		"saturation=%.3f\n"
		"; Highlight expansion above the knee (0 = off, up to 1) and knee (0.5-0.95, linear SDR).\n"
		"highlight=%.3f\n"
		"highlight_knee=%.3f\n"
		"; Tonemap toward peak: 0 = clip, 1 = BT.2390 roll-off.\n"
		"tonemap=%d\n"
		"; SDR decoding curve: 0 = gamma 2.2, 1 = gamma 2.4, 2 = sRGB piecewise (lifts blacks).\n"
		"eotf=%d\n\n\n";

	sprintf_s(buf, hdr_setting,
		boolString(App.hdr.enabled),
		App.hdr.sdr_white.value,
		App.hdr.peak_auto ? 0.0f : App.hdr.peak.value,
		App.hdr.brightness.value,
		App.hdr.black_level.value,
		App.hdr.contrast.value,
		App.hdr.saturation.value,
		App.hdr.highlight.value,
		App.hdr.knee.value,
		App.hdr.tonemap.selected,
		App.hdr.eotf.selected);
	out_file << buf;

	static const char* other_setting =
		"[Other]\n\n"
		"; Preferred OpenGL Version (must be 3.3 or between 4.0 to 4.6).\n"
		"gl_ver_major=%d\n"
		"gl_ver_minor=%d\n\n"
		"; Use compute shader (enabling this might be better on some gpu).\n"
		"use_compute_shader=%s\n\n"
		"; Frame Latency (how many frames cpu generate before rendering).\n"
		"; Set 1-5 (increasing this value notice less frame stutter but introduces more input lag).\n"
		"frame_latency=%d\n\n"
		"; Performance log (d2gl_perf.csv per frame + d2gl_perf_summary.jsonl). Off has near-zero cost.\n"
		"perf_log=%s\n"
		"; Keep one CSV row per N frames (1-1000).\n"
		"perf_sample_every=%d\n"
		"; Frames per JSON summary line (60-100000).\n"
		"perf_summary_every=%d\n\n"
		"; GPU sync mode: 0 = legacy (wait for the current frame's fence), 1 = deferred (wait for the previous\n"
		"; frame's fence; experimental, NOT measured, may add one frame of latency).\n"
		"gpu_sync=%d\n\n"
		"; R1: release the frame slot to the game thread before the GPU fence wait instead of after it\n"
		"; (the slot is fully copied into GL by then). Lets the game thread record the next frame while\n"
		"; the render thread waits on the GPU. Not measured yet.\n"
		"early_slot_release=%s\n\n"
		"; P3, HDR shared-texture path only: skip the render thread's GPU fence wait; the fence in the HDR\n"
		"; present (which also covers the menu) is the only wait. Not measured yet.\n"
		"hdr_single_fence=%s\n"
		"; P3, HDR path only: DXGI frame-latency waitable swapchain with max frame latency 1; the render thread\n"
		"; waits on it before taking the next frame instead of blocking inside Present. Not measured yet.\n"
		"hdr_latency_waitable=%s\n"
		"; P3, vsync off only: if the fps limit is within 3%% of the display refresh, pace at the exact refresh\n"
		"; period (DwmGetCompositionTimingInfo, else EnumDisplaySettings). Not measured yet.\n"
		"pace_to_refresh=%s\n\n"
		"; Comma-delimited DLLs to load (early: right after attached).\n"
		"load_dlls_early=%s\n\n"
		"; Comma-delimited DLLs to load (late: right after window created).\n"
		"load_dlls_late=%s\n";

	sprintf_s(buf, other_setting,
		App.gl_ver.x,
		App.gl_ver.y,
		boolString(App.use_compute_shader),
		App.frame_latency,
		boolString(App.perf.log),
		App.perf.sample_every,
		App.perf.summary_every,
		App.perf.gpu_sync,
		boolString(App.perf.early_release),
		boolString(App.perf.hdr_single_fence),
		boolString(App.perf.hdr_waitable),
		boolString(App.perf.pace_to_refresh),
		App.dlls_early.c_str(),
		App.dlls_late.c_str());
	out_file << buf;

	out_file.close();
}

void loadIni()
{
	App.ini_file = std::string(helpers::getCurrentDir() + App.ini_file);
	App.desktop_resolution = {
		GetSystemMetrics(SM_XVIRTUALSCREEN),
		GetSystemMetrics(SM_YVIRTUALSCREEN),
		GetSystemMetrics(SM_CXVIRTUALSCREEN),
		GetSystemMetrics(SM_CYVIRTUALSCREEN),
	};

	App.lut.items.push_back({ "Game Default" });
	for (int i = 1; i <= 14; i++) {
		char label[50] = { 0 };
		sprintf_s(label, 50, "Color #%d", i);
		App.lut.items.push_back({ label });
	}

	if (helpers::fileExists(App.ini_file)) {
		App.window.fullscreen = getBool("Screen", "fullscreen", App.window.fullscreen);
		App.window.borderless = getBool("Screen", "borderless_window", App.window.borderless);
		App.window.auto_minimize = getBool("Screen", "auto_minimize", App.window.auto_minimize);
		App.window.dark_mode = getBool("Screen", "dark_mode", App.window.dark_mode);
		App.vsync = getBool("Screen", "vsync", App.vsync);
		App.cursor.unlock = getBool("Screen", "unlock_cursor", App.cursor.unlock);

		App.window.size.x = getInt("Screen", "window_width", App.window.size.x, 800, App.desktop_resolution.z);
		App.window.size.y = getInt("Screen", "window_height", App.window.size.y, 600, App.desktop_resolution.w);
		App.window.size_save = App.window.size;

		App.window.centered = getBool("Screen", "centered_window", App.window.centered);
		App.window.position.x = getInt("Screen", "window_posx", App.window.position.x, App.desktop_resolution.x, App.desktop_resolution.z);
		App.window.position.y = getInt("Screen", "window_posy", App.window.position.y, App.desktop_resolution.y, App.desktop_resolution.w);

		App.foreground_fps.active = getBool("Screen", "foreground_fps", App.foreground_fps.active);
		App.foreground_fps.range.value = getInt("Screen", "foreground_fps_value", App.foreground_fps.range.value, App.foreground_fps.range.min, App.foreground_fps.range.max);
		App.foreground_fps.pacing = getBool("Screen", "foreground_fps_pacing", App.foreground_fps.pacing);
		App.background_fps.active = getBool("Screen", "background_fps", App.background_fps.active);
		App.background_fps.range.value = getInt("Screen", "background_fps_value", App.background_fps.range.value, App.background_fps.range.min, App.background_fps.range.max);

		App.shader.preset = getString("Graphic", "shader_preset", App.shader.preset);
		App.shader.world_only = getBool("Graphic", "shader_world_only", App.shader.world_only);
		App.lut.selected = getInt("Graphic", "lut", App.lut.selected, 0, App.lut.items.size() - 1);
		App.fxaa.active = getBool("Graphic", "fxaa", App.fxaa.active);
		App.fxaa.presets.selected = getInt("Graphic", "fxaa_preset", App.fxaa.presets.selected, 0, 2);

		App.sharpen.active = getBool("Graphic", "sharpen", App.sharpen.active);
		App.sharpen.strength.value = getFloat("Graphic", "sharpen_strength", App.sharpen.strength);
		App.sharpen.clamp.value = getFloat("Graphic", "sharpen_clamp", App.sharpen.clamp);
		App.sharpen.radius.value = getFloat("Graphic", "sharpen_radius", App.sharpen.radius);

		App.bloom.active = getBool("Graphic", "bloom", App.bloom.active);
		App.bloom.exposure.value = getFloat("Graphic", "bloom_exposure", App.bloom.exposure);
		App.bloom.gamma.value = getFloat("Graphic", "bloom_gamma", App.bloom.gamma);
		App.dynamic_light.active = getBool("Graphic", "dynamic_light", App.dynamic_light.active);
		App.dynamic_light.intensity.value = getFloat("Graphic", "dynamic_light_intensity", App.dynamic_light.intensity);
		App.dynamic_light.normals = getBool("Graphic", "light_normals", App.dynamic_light.normals);
		App.dynamic_light.normals_strength.value = getFloat("Graphic", "light_normals_strength", App.dynamic_light.normals_strength);
		App.dynamic_light.source.selected = getInt("Graphic", "dynamic_light_source", App.dynamic_light.source.selected, 0, 1);
		App.dynamic_light.debug = getBool("Graphic", "dynamic_light_debug", App.dynamic_light.debug);
		App.dynamic_light.depth_haze = getBool("Graphic", "light_depth_haze", App.dynamic_light.depth_haze);
		App.dynamic_light.depth_haze_strength.value = getFloat("Graphic", "light_depth_haze_strength", App.dynamic_light.depth_haze_strength);
		App.dynamic_light.spell_glow.value = getFloat("Graphic", "spell_glow", App.dynamic_light.spell_glow);
		App.tonemap.active = getBool("Graphic", "tonemap", App.tonemap.active);
		App.tonemap.exposure.value = getFloat("Graphic", "tonemap_exposure", App.tonemap.exposure);
		App.soft_shadows.active = getBool("Graphic", "soft_shadows", App.soft_shadows.active);
		App.soft_shadows.softness.value = getFloat("Graphic", "soft_shadows_softness", App.soft_shadows.softness);

		App.viewport.stretched.x = getBool("Graphic", "stretched_horizontal", App.viewport.stretched.x);
		App.viewport.stretched.y = getBool("Graphic", "stretched_vertical", App.viewport.stretched.y);

		App.hd_cursor = getBool("Feature", "hd_cursor", App.hd_cursor);
		App.hd_text.active = getBool("Feature", "hd_text", App.hd_text.active);
		App.hd_text.scale.value = ISHDTEXT() ? 1.0f : getFloat("Feature", "hd_text_scale", App.hd_text.scale);

		// App.hd_orbs.active = getBool("Feature", "hd_orbs", App.hd_orbs.active);
		// App.hd_orbs.centered = getBool("Feature", "hd_orbs_centered", App.hd_orbs.centered);

		App.mini_map.active = getBool("Feature", "mini_map", App.mini_map.active) && ISGLIDE3X();
		App.mini_map.text_over = getBool("Feature", "mini_map_text_over", App.mini_map.text_over);
		App.mini_map.width.value = getInt("Feature", "mini_map_width", App.mini_map.width.value, App.mini_map.width.min, App.mini_map.width.max);
		App.mini_map.height.value = getInt("Feature", "mini_map_height", App.mini_map.height.value, App.mini_map.height.min, App.mini_map.height.max);
		App.mini_map.opacity.value = getInt("Feature", "mini_map_opacity", App.mini_map.opacity.value, App.mini_map.opacity.min, App.mini_map.opacity.max);
		App.hd_text.bg_opacity.value = getInt("Feature", "hd_text_bg_opacity", App.hd_text.bg_opacity.value, App.hd_text.bg_opacity.min, App.hd_text.bg_opacity.max);

		App.motion_prediction = getBool("Feature", "motion_prediction", App.motion_prediction);

		App.loot_sparkle.active = getBool("Feature", "loot_sparkle", App.loot_sparkle.active);

		App.loot_sparkle.min_quality.selected = getInt("Feature", "loot_sparkle_min_quality", App.loot_sparkle.min_quality.selected, 0, 3);
		App.minion_ring = getBool("Feature", "minion_ring", App.minion_ring);
		App.skip_intro = getBool("Feature", "skip_intro", App.skip_intro);
		App.no_pickup = getBool("Feature", "no_pickup", App.no_pickup);
		App.show_item_quantity = getBool("Feature", "show_item_quantity", App.show_item_quantity);
		App.show_monster_res = getBool("Feature", "show_monster_res", App.show_monster_res);
		App.show_fps = getBool("Feature", "show_fps", App.show_fps);

		App.hdr.enabled = getBool("HDR", "hdr", App.hdr.enabled);
		App.hdr.shared_texture = getBool("HDR", "shared_texture", App.hdr.shared_texture);
		App.hdr.sdr_white.value = getFloat("HDR", "sdr_white", App.hdr.sdr_white);
		{
			char peak[32] = { 0 };
			if (getValue("HDR", "peak_nits", peak, sizeof(peak)) && atof(peak) > 0.0) {
				App.hdr.peak_auto = false;
				App.hdr.peak.value = getFloat("HDR", "peak_nits", App.hdr.peak);
			}
		}
		App.hdr.brightness.value = getFloat("HDR", "brightness", App.hdr.brightness);
		App.hdr.black_level.value = getFloat("HDR", "black_level", App.hdr.black_level);
		App.hdr.contrast.value = getFloat("HDR", "contrast", App.hdr.contrast);
		App.hdr.saturation.value = getFloat("HDR", "saturation", App.hdr.saturation);
		App.hdr.highlight.value = getFloat("HDR", "highlight", App.hdr.highlight);
		App.hdr.knee.value = getFloat("HDR", "highlight_knee", App.hdr.knee);
		App.hdr.tonemap.selected = getInt("HDR", "tonemap", App.hdr.tonemap.selected, 0, 1);
		App.hdr.eotf.selected = getInt("HDR", "eotf", App.hdr.eotf.selected, 0, 2);

		App.gl_ver.x = getInt("Other", "gl_ver_major", App.gl_ver.x, 3, 4);
		App.gl_ver.y = getInt("Other", "gl_ver_minor", App.gl_ver.y, 0, 6);
		App.gl_ver.y = App.gl_ver.x == 3 ? 3 : App.gl_ver.y;

		App.use_compute_shader = getBool("Other", "use_compute_shader", App.use_compute_shader);
		App.frame_latency = getInt("Other", "frame_latency", App.frame_latency, 1, 5);
		App.perf.log = getBool("Other", "perf_log", App.perf.log);
		App.perf.sample_every = getInt("Other", "perf_sample_every", App.perf.sample_every, 1, 1000);
		App.perf.summary_every = getInt("Other", "perf_summary_every", App.perf.summary_every, 60, 100000);
		App.perf.gpu_sync = getInt("Other", "gpu_sync", App.perf.gpu_sync, 0, 1);
		App.perf.early_release = getBool("Other", "early_slot_release", App.perf.early_release);
		App.perf.hdr_single_fence = getBool("Other", "hdr_single_fence", App.perf.hdr_single_fence);
		App.perf.hdr_waitable = getBool("Other", "hdr_latency_waitable", App.perf.hdr_waitable);
		App.perf.pace_to_refresh = getBool("Other", "pace_to_refresh", App.perf.pace_to_refresh);

		App.dlls_early = getString("Other", "load_dlls_early", App.dlls_early);
		App.dlls_late = getString("Other", "load_dlls_late", App.dlls_late);
	}

	// clang-format off
	std::vector<std::pair<uint32_t, uint32_t>> window_sizes = {
		{  800, 600 }, {  960, 720 }, { 1024, 768 }, { 1200,  900 }, { 1280,  960 }, { 1440, 1080 }, { 1600, 1200 }, { 1920, 1440 }, { 2560, 1920 }, { 2732, 2048 },
		{ 1068, 600 }, { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2048, 1152 }, { 2560, 1440 }, { 3200, 1800 }, { 3840, 2160 },
	};
	// clang-format on

	int index = 1;
	App.resolutions.items.push_back({ "Custom Size", glm::uvec2(0, 0) });
	for (size_t i = 0; i < window_sizes.size(); i++) {
		const auto& p = window_sizes[i];
		if (App.desktop_resolution.z <= (int)p.first || App.desktop_resolution.w <= (int)p.second)
			continue;

		char label[50] = { 0 };
		bool is_4x3 = p.first / 4 == p.second / 3;
		sprintf_s(label, 50, "%d x %d (%s)", p.first, p.second, (is_4x3 ? "4:3" : "16:9"));
		App.resolutions.items.push_back({ label, glm::uvec2(p.first, p.second) });

		if (App.window.size.x == p.first && App.window.size.y == p.second)
			App.resolutions.selected = index;
		index++;
	}

	saveIni();
}

}