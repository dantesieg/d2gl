#pragma once

// Portable, header-only immutable per-frame settings snapshot (R2, see docs/r0r2/FRAME_SNAPSHOT_DESIGN.md).
//
// Producer: game (main/window) thread, Context::presentFrame(), written into the slot's
// CommandBuffer *before* ReleaseSemaphore(m_semaphore_cpu[slot]).
// Consumer: render thread, after WaitForSingleObject(m_semaphore_cpu[slot]) returns.
// ReleaseSemaphore / WaitForSingleObject are full memory barriers on Windows, so every write the
// producer made before the release is visible to the consumer after the wait (release/acquire).
// The producer does not touch the slot again until the render thread releases
// m_semaphore_gpu[slot], which orders the consumer's reads before the producer's next writes.
//
// Plain values only: no pointers, no references into App or into engine (Game.exe) memory.

#include <cstdint>

namespace d2gl::perf {

struct LimiterSettings {
	uint32_t generation = 0; // bumped by every setFpsLimit(); render thread resets its pacing state on change
	bool active = false;
	bool pacing = false;
	float frame_len_ms = 0.0f;
	uint64_t frame_len_ns = 0; // 100 ns units (waitable timer)
	int64_t pace_period = 0;   // QPC counts per frame
};

struct FrameSettings {
	bool valid = false;

	// viewport (App.viewport / App.window, written by the game thread in win32::windowResize)
	int32_t window_w = 0, window_h = 0;
	int32_t vp_w = 0, vp_h = 0, vp_x = 0, vp_y = 0; // already resolved for App.viewport.stretched

	// post effects
	bool sharpen = false;
	float sharpen_strength = 0.0f, sharpen_clamp = 0.0f, sharpen_radius = 0.0f;
	bool fxaa = false;
	int32_t fxaa_preset = 0;
	bool bloom = false;
	float bloom_exposure = 0.0f, bloom_gamma = 0.0f;
	int32_t lut = 0;
	bool tonemap = false;
	float tonemap_exposure = 0.0f;
	bool world_only = false;

	// dynamic light
	bool light = false;
	bool light_debug = false;
	bool light_vsource = false; // App.dynamic_light.source.selected == 1
	float light_intensity = 0.0f;
	float light_haze = 0.0f;    // 0 when depth_haze is off
	bool light_normals = false;
	float light_normals_strength = 0.0f;
	float spell_glow = 0.0f;

	// game state read from engine memory on the game thread (value copy, not a pointer)
	int32_t level_no = -1; // *d2::level_no while in game, else -1

	bool soft_shadows = false;
	bool gi = false; // App.radiance_gi.active, in game only
	float gi_strength = 0.0f;
	float shadow_softness = 1.0f;

	LimiterSettings limiter;
};

}
