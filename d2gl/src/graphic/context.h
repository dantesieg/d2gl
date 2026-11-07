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

#include <atomic>

#include "types.h"
#include "vertex.h"

#include "command_buffer.h"
#include "frame_buffer.h"
#include "object.h"
#include "pipeline.h"
#include "texture.h"
#include "uniform_buffer.h"
#include "hdr/hdr_presenter.h"

namespace d2gl {

#define MAX_FRAME_LATENCY 6
#define MAX_INDICES 6 * 50000
#define MAX_VERTICES 4 * 50000
#define MAX_VERTICES_MOD 4 * 20000
#define PIXEL_BUFFER_SIZE 12 * 1024 * 1024
#define MAX_FRAMETIME_SAMPLE_COUNT 120

#define TEXTURE_SLOT_DEFAULT 0
#define TEXTURE_SLOT_GAME 1
#define TEXTURE_SLOT_POSTFX1 2
#define TEXTURE_SLOT_POSTFX2 3
#define TEXTURE_SLOT_PREFX 4
#define TEXTURE_SLOT_BLOOM1 5
#define TEXTURE_SLOT_BLOOM2 6
#define TEXTURE_SLOT_WORLD 7
#define TEXTURE_SLOT_LIGHT1 8
#define TEXTURE_SLOT_LIGHT2 9
#define TEXTURE_SLOT_SHADOW 10

#define TEXTURE_SLOT_LUT 11
#define TEXTURE_SLOT_CURSOR 12
#define TEXTURE_SLOT_FONTS 13
#define TEXTURE_SLOT_MASK 14
#define TEXTURE_SLOT_MAP 15
#define TEXTURE_SLOT_SHADOW1 16
#define TEXTURE_SLOT_SHADOW2 17
#define TEXTURE_SLOT_VLIGHT 18 // game framebuffer attachment 4 (needs > 18 texture units)
#define TEXTURE_SLOT_AMBIENT 19 // 1x1 time-smoothed ambient for game vertex lighting
#define TEXTURE_SLOT_GI_SCENE 21 // radiance GI: 1/4-res emitters + occluders
#define TEXTURE_SLOT_GI_C0 22    // radiance GI cascades 0..3 (22..25)
#define TEXTURE_SLOT_GI 26       // radiance GI fluence
#define TEXTURE_SLOT_EXPO 29 // float pipeline: 1x1 adapted exposure (ping-pong)
#define TEXTURE_SLOT_EMIT 20 // game framebuffer attachment 5: HDR emission (additive sprites), [HDR] emissive

#define IMAGE_UNIT_BLUR 0
#define IMAGE_UNIT_FXAA 1

#pragma warning(push)
#pragma warning(disable : 26495)
template <typename T, size_t t_size, size_t d_size>
struct Vertices {
	T* ptr = nullptr;
	uint32_t count = 0;
	uint32_t start = 0;
	std::array<T, t_size> data[d_size];
};
#pragma warning(pop)

struct VertexParams {
	uint32_t color = 0;
	uint8_t tex_shift = 0;
	glm::vec<2, uint16_t> tex_ids = { 0, 0 };
	glm::vec<2, uint16_t> offsets = { 0, 0 };
	glm::vec<4, uint8_t> flags = { 0, 0, 0, 0 };
};

struct SharpenData {
	float strength = 0.405f;
	float clamp = 0.135f;
	float radius = 1.0f;
};

struct FrameMetrics {
	double frame_time = 0.0;
	double prev_time = 0.0;
	std::deque<double> frame_times;
	double average_frame_time = 0.0;
	LARGE_INTEGER time = { 0 };
	double frequency = 0.0;

	uint32_t vertex_count = 0;
	uint32_t drawcall_count = 0;
	uint32_t frame_count = 0;
	uint32_t frame_sample_count = 0;
};

// Render-thread-owned limiter state (the settings live in perf::LimiterSettings, see setFpsLimit).
struct LimiterMetrics {
	HANDLE timer = 0;
	LARGE_INTEGER due_time = { 0 };
	int64_t pace_next = 0;
};

struct GLCaps {
	bool compute_shader = false;
	bool independent_blending = false;
};

class Context {
	HGLRC m_context = nullptr;
	// HDR (App.hdr.mode): GL lives on a hidden window's DC so App.hwnd can host the DXGI swapchain.
	// m_gl_hdc == App.hdc otherwise (unchanged behaviour).
	HDC m_gl_hdc = nullptr;
	HWND m_hdr_hidden = nullptr;
	int m_pixel_format = 0;
	PIXELFORMATDESCRIPTOR m_pfd = {};
	std::unique_ptr<hdr::Presenter> m_hdr; // render thread only
	HANDLE m_semaphore_cpu[MAX_FRAME_LATENCY];
	HANDLE m_semaphore_gpu[MAX_FRAME_LATENCY];
	CommandBuffer m_command_buffer[MAX_FRAME_LATENCY];
	std::atomic<bool> m_rendering{ true }; // written by ~Context (game thread), read by renderThread
	HANDLE m_render_thread = nullptr;

	GLuint m_pixel_buffer;
	GLuint m_index_buffer;
	GLuint m_vertex_array;
	GLuint m_vertex_buffer;
	uint32_t m_frame_index = 0;

	bool m_delay_push = false;
	Vertices<Vertex, MAX_VERTICES, MAX_FRAME_LATENCY> m_vertices;
	Vertices<VertexMod, MAX_VERTICES_MOD, MAX_FRAME_LATENCY> m_vertices_mod;
	Vertices<VertexMod, MAX_VERTICES_MOD, 1> m_vertices_late;
	VertexParams m_vertex_params;
	Occluders m_occluders;
	uint32_t m_mod_coverage[MOD_COVERAGE_CELLS] = {};

	FrameMetrics m_frame;
	LimiterMetrics m_limiter;              // render thread: timer, due_time, pace_next
	perf::LimiterSettings m_limiter_settings; // game thread (setFpsLimit), copied into each frame snapshot

	// R0 instrumentation (game-thread state unless noted). m_perf / m_gpu_sync are fixed at startup.
	bool m_perf = false;
	uint32_t m_gpu_sync = 0;
	uint32_t m_perf_frame = 0;
	int64_t m_perf_begin_qpc = 0;
	int64_t m_perf_present_qpc = 0;
	double m_perf_wait_ms = -1.0;
	double m_perf_cpu_ms = -1.0;
	std::atomic<uint32_t> m_inflight{ 0 }; // game ++ at submit, render -- at retire (perf only)

	std::unique_ptr<Texture> m_game_texture;
	std::unique_ptr<UniformBuffer> m_game_color_ubo;
	std::unique_ptr<Pipeline> m_game_pipeline;
	std::unique_ptr<FrameBuffer> m_game_framebuffer;
	std::unique_ptr<Pipeline> m_movie_pipeline;

	glm::vec3 m_sharpen_data;
	glm::uvec2 m_fxaa_work_size = { 0, 0 };
	std::unique_ptr<UniformBuffer> m_postfx_ubo;
	std::unique_ptr<Texture> m_postfx_texture;
	std::unique_ptr<FrameBuffer> m_postfx_framebuffer;
	std::unique_ptr<Pipeline> m_postfx_pipeline;
	std::unique_ptr<Pipeline> m_fxaa_compute_pipeline;

	std::unique_ptr<Pipeline> m_mod_pipeline;
	int m_current_shader = -1;

	// Glide only
	std::unique_ptr<Texture> m_glide_texture;
	std::map<uint32_t, std::pair<uint32_t, BlendType>> m_blend_types;
	uint32_t m_current_blend_index = 0;
	bool m_blend_locked = false;

	glm::vec2 m_bloom_data;
	glm::uvec2 m_bloom_tex_size = { 0, 0 };
	glm::uvec2 m_bloom_work_size = { 0, 0 };
	std::unique_ptr<UniformBuffer> m_bloom_ubo;
	std::unique_ptr<Texture> m_bloom_texture;
	std::unique_ptr<FrameBuffer> m_bloom_framebuffer;
	std::unique_ptr<Pipeline> m_blur_compute_pipeline;

	std::unique_ptr<Texture> m_lut_texture;
	std::unique_ptr<Texture> m_prefx_texture;
	std::unique_ptr<Pipeline> m_prefx_pipeline;
	std::unique_ptr<Pipeline> m_uilayer_pipeline;
	std::unique_ptr<Pipeline> m_light_pipeline;
	std::unique_ptr<Pipeline> m_ambient_pipeline;
	std::unique_ptr<FrameBuffer> m_ambient_framebuffer;
	bool m_ambient_reset = true;
	void updateAmbient(glm::ivec2 game_size, int level);
	std::unique_ptr<Pipeline> m_atmos_pipeline;
	std::unique_ptr<Pipeline> m_lightfx_pipeline;
	std::unique_ptr<FrameBuffer> m_light_framebuffer;
	std::unique_ptr<Texture> m_light_texture;
	glm::uvec2 m_light_tex_size = { 0, 0 };
	uint32_t gameDrawBufferCount(const perf::FrameSettings& st) const;
	std::unique_ptr<FrameBuffer> m_world_framebuffer;
	bool m_world_ready = false;

	// Soft shadows / ambient occlusion (game framebuffer attachment 3: r = darkness, g = occluder)
	bool m_shadow_supported = false;
	bool m_vlight_supported = false;
	// HDR emissive highlights: 6th game attachment (r = additive emission), packed into default-FB alpha
	bool m_emit_supported = false;
	bool m_emit_attached = false;
	uint32_t m_color_bpp = 8;
	std::unique_ptr<Pipeline> m_emit_pipeline;
	bool emitWanted() const;
	float m_add_scale = 1.0f; // render thread: current u_AddScale (float pipeline)
	bool m_expo_supported = false;
	std::unique_ptr<FrameBuffer> m_expo_fb[2];
	int m_expo_cur = 0;
	int m_expo_level = -2;
	int64_t m_expo_qpc = 0;
	void hdrSceneFx(const perf::FrameSettings& st, glm::uvec2 game_size); // float pipeline: exposure + shoulder

	// 2D radiance-cascade GI ([Graphic] radiance_gi), PreFx stage, allocated on first use
	bool m_gi_supported = false;
	std::unique_ptr<Pipeline> m_gi_pipeline;
	std::unique_ptr<FrameBuffer> m_gi_scene_fb, m_gi_fb, m_gi_casc_fb[4];
	glm::uvec2 m_gi_size = { 0, 0 }, m_gi_game_size = { 0, 0 };
	void renderGI(const perf::FrameSettings& st, glm::uvec2 game_size);
	bool vlightActive(const perf::FrameSettings& st) const;
	bool m_shadow_fx = false;
	bool m_shadow_seen = false;
	uint8_t m_shadow_draw = 0; // 0: normal, 1: unit shadow, 2: ground/tile
	std::unique_ptr<Pipeline> m_shadow_pipeline;
	std::unique_ptr<FrameBuffer> m_shadow_framebuffer;
	std::unique_ptr<Texture> m_shadow_texture;
	glm::uvec2 m_shadow_tex_size = { 0, 0 };

public:
	Context();
	~Context();

	static void renderThread(void* context);
	static std::pair<GLint, GLenum> sceneFormat(); // RGBA16F with [HDR] float_pipeline, else RGBA8
	void hdrFallbackToWgl(const char* why); // render thread

	void onResize(glm::uvec2 w_size, glm::uvec2 g_size, uint32_t bpp = 8);
	void onShaderChange();
	void onStageChange();
	void setBlendState(uint32_t index);

	void beginFrame();
	void bindDefaultFrameBuffer();
	void presentFrame();

	inline uint32_t getFrameIndex() { return m_frame_index; }
	inline CommandBuffer* getCommandBuffer() { return &m_command_buffer[m_frame_index]; }

	void setViewport(glm::ivec2 size, glm::ivec2 offset = { 0, 0 });
	inline void bindFrameBuffer(const std::unique_ptr<FrameBuffer>& framebuffer, bool clear = true) { framebuffer->bind(clear); }
	inline void bindPipeline(const std::unique_ptr<Pipeline>& pipeline, uint32_t index = 0) { pipeline->bind(index); }

	void pushVertex(const GlideVertex* vertex, glm::vec2 fix = { 0.0f, 0.0f }, glm::ivec2 offset = { 0, 0 });
	bool pushFlatQuad(glm::vec2 pos, glm::vec2 size, uint32_t color);
	void flushVertices();
	void drawQuad(int8_t flag_x = 0, int8_t flag_y = 0, int16_t tex_id = 0);

	inline void toggleDelayPush(bool delay) { m_delay_push = delay; }
	void pushObject(const std::unique_ptr<Object>& object);
	void appendDelayedObjects();
	void addOccluder(glm::vec2 pos, glm::vec2 size, uint32_t color);
	void addPanelOccluders();

	inline void setVertexColor(uint32_t color) { m_vertex_params.color = color; }
	inline void setVertexTexShift(uint8_t shift) { m_vertex_params.tex_shift = shift; }
	inline void setVertexTexNum(glm::vec<2, uint16_t> tex_ids) { m_vertex_params.tex_ids = tex_ids; }
	inline void setVertexOffset(glm::vec<2, uint16_t> offsets) { m_vertex_params.offsets = offsets; }
	inline void setVertexFlagX(uint8_t flag) { m_vertex_params.flags.x = flag; }
	inline void setVertexFlagY(uint8_t flag) { m_vertex_params.flags.y = flag; }
	inline void setVertexFlagZ(uint8_t flag) { m_vertex_params.flags.z = flag; }
	inline void setVertexFlagW(uint8_t flag) { m_vertex_params.flags.w = flag; }
	void pushAdditiveQuads(const glm::vec2* pos, const uint32_t* colors, uint32_t quad_count);
	inline void setShadowDraw(uint8_t mode) { m_shadow_draw = mode; }
	inline bool isShadowFxActive() { return m_shadow_fx; }

	inline const double getFrameTime() { return m_frame.frame_time; }
	inline const double getAvgFrameTime() { return m_frame.average_frame_time; }
	inline const uint32_t getFrameCount() { return m_frame.frame_count; }
	inline const uint32_t getVertexCount() { return m_frame.vertex_count; }
	inline const uint32_t getDrawCallCount() { return m_frame.drawcall_count; }

	void toggleVsync();
	void setFpsLimit(bool active, int max_fps);
	void takeScreenShot();

	void imguiStartFrame();
	void imguiRender();

private:
	void resetFileTime();
	void captureSettings(perf::FrameSettings& st) const;
	inline int64_t qpcNow() const
	{
		LARGE_INTEGER t;
		QueryPerformanceCounter(&t);
		return t.QuadPart;
	}
	inline double qpcMs(int64_t counts) const { return (double)counts / m_frame.frequency; }

	glm::ivec4 coverageCells(glm::vec2 pos, glm::vec2 size);
	void markModCoverage(glm::vec2 pos, glm::vec2 size);
	bool coversModContent(glm::vec2 pos, glm::vec2 size);

	void imguiInit();
	void imguiDestroy();

public:
	inline hdr::Presenter* hdrPresenter() { return m_hdr.get(); } // render thread (menu draws there)
	static void APIENTRY debugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* data);

	inline static std::unique_ptr<Context> createContext() { return std::make_unique<Context>(); }
	inline static std::unique_ptr<Texture> createTexture(const TextureCreateInfo& info) { return std::make_unique<Texture>(info); }
	inline static std::unique_ptr<Pipeline> createPipeline(const PipelineCreateInfo& info) { return std::make_unique<Pipeline>(info); }
	inline static std::unique_ptr<FrameBuffer> createFrameBuffer(const FrameBufferCreateInfo& info) { return std::make_unique<FrameBuffer>(info); }
	inline static std::unique_ptr<UniformBuffer> createUniformBuffer(const UniformBufferCreateInfo& info) { return std::make_unique<UniformBuffer>(info); }
};

}