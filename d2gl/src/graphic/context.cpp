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
#include "context.h"
#include "d2/common.h"
#include "helpers.h"
#include "modules/hd_cursor.h"
#include "modules/hd_text.h"
#include "modules/mini_map.h"
#include "modules/motion_prediction.h"
#include "option/menu.h"
#include "perf/perf_log.h"
#include "upscaler.h"
#include "win32.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_impl_win32.h>
#include <dwmapi.h>

namespace d2gl {

Context::Context()
{
	PIXELFORMATDESCRIPTOR pfd;
	memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER | PFD_SUPPORT_OPENGL | PFD_GENERIC_ACCELERATED;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 8;
	pfd.iLayerType = PFD_MAIN_PLANE;

	m_gl_hdc = App.hdc;
	if (App.hdr.mode) {
		// HDR: keep App.hwnd free of a GL pixel format so DXGI/DXVK can own it with a flip swapchain.
		WNDCLASSA wc = {};
		wc.style = CS_OWNDC;
		wc.lpfnWndProc = DefWindowProcA;
		wc.hInstance = GetModuleHandleA(NULL);
		wc.lpszClassName = "d2gl_hdr_gl";
		RegisterClassA(&wc);
		m_hdr_hidden = CreateWindowExA(0, "d2gl_hdr_gl", "d2gl_hdr_gl", WS_POPUP, 0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);
		if (m_hdr_hidden) {
			m_gl_hdc = GetDC(m_hdr_hidden);
			trace_log("HDR: GL context on a hidden window");
		} else {
			error_log("HDR: hidden GL window failed (%lu), HDR disabled", GetLastError());
			App.hdr.mode = false;
			App.hdr.float_mode = false;
		}
	}

	m_pixel_format = ChoosePixelFormat(m_gl_hdc, &pfd);
	m_pfd = pfd;
	SetPixelFormat(m_gl_hdc, m_pixel_format, &pfd);

	HGLRC context = wglCreateContext(m_gl_hdc);
	wglMakeCurrent(m_gl_hdc, context);

	if (glewInit() != GLEW_OK) {
		MessageBoxA(NULL, "OpenGL loader failed!", "OpenGL failed!", MB_OK);
		exit(1);
	}

	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(context);

	std::vector<glm::vec<2, uint8_t>> versions = { { 4, 6 }, { 4, 5 }, { 4, 4 }, { 4, 3 }, { 4, 2 }, { 4, 1 }, { 4, 0 }, { 3, 3 } };
	for (auto& version : versions) {
		if (App.gl_ver.x < version.x)
			continue;
		if (App.gl_ver.y < version.y)
			continue;

		int attribs[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, version.x,
			WGL_CONTEXT_MINOR_VERSION_ARB, version.y,
			WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
			WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0
		};
		if (App.debug)
			attribs[5] |= WGL_CONTEXT_DEBUG_BIT_ARB;

		if (m_context = wglCreateContextAttribsARB(m_gl_hdc, 0, attribs)) {
			App.gl_ver = version;
			break;
		}
	}

	if (!m_context) {
		MessageBoxA(App.hwnd, "Requires OpenGL 3.3 or newer!", "Unsupported OpenGL version!", MB_OK | MB_ICONERROR);
		error_log("Requires OpenGL 3.3 or newer! exiting.");
		exit(1);
	}

	wglMakeCurrent(m_gl_hdc, m_context);
	glewInit();

	GLint major_version, minor_version;
	glGetIntegerv(GL_MAJOR_VERSION, &major_version);
	glGetIntegerv(GL_MINOR_VERSION, &minor_version);

	char version_str[50] = { 0 };
	sprintf_s(version_str, "%d.%d", major_version, minor_version);
	trace_log("OpenGL: %s (%s | %s)", version_str, glGetString(GL_RENDERER), glGetString(GL_VENDOR));
	trace_log("OpenGL: Shading Language: %s", version_str, glGetString(GL_SHADING_LANGUAGE_VERSION));
	App.gl_ver_str = version_str;

	GLint max_texture_unit;
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_texture_unit);
	trace_log("OpenGL: GL_MAX_TEXTURE_IMAGE_UNITS = %d", max_texture_unit);

	if ((App.debug || App.log) && glewIsSupported("GL_KHR_debug")) {
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(Context::debugMessageCallback, nullptr);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
		trace_log("OpenGL: GL_KHR_debug enabled!");
	}

	if (App.use_compute_shader && (glewIsSupported("GL_VERSION_4_3") || glewIsSupported("GL_ARB_compute_shader"))) {
		App.gl_caps.compute_shader = true;
		trace_log("OpenGL: Compute shader available.");
	}

	if (glewIsSupported("GL_VERSION_4_0")) {
		App.gl_caps.independent_blending = true;
		trace_log("OpenGL: Independent blending available.");
	}
	m_shadow_supported = App.gl_caps.independent_blending && max_texture_unit > TEXTURE_SLOT_SHADOW2;
	// Vertex-light capture: 5th game attachment at TEXTURE_SLOT_VLIGHT (implies m_shadow_supported).
	m_vlight_supported = App.gl_caps.independent_blending && max_texture_unit > TEXTURE_SLOT_AMBIENT;
	// HDR emissive highlights: 6th game attachment at TEXTURE_SLOT_EMIT (after the vlight attachment).
	{
		GLint max_draw_buffers = 0;
		glGetIntegerv(GL_MAX_DRAW_BUFFERS, &max_draw_buffers);
		m_emit_supported = m_vlight_supported && max_texture_unit > TEXTURE_SLOT_EMIT && max_draw_buffers >= 6;
	}
	m_gi_supported = max_texture_unit > TEXTURE_SLOT_GI;
	m_expo_supported = max_texture_unit > TEXTURE_SLOT_EXPO;
	m_hdrbloom_supported = max_texture_unit > TEXTURE_SLOT_HDRBLOOM;

	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);

	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);

	uint32_t offset = 0;
	uint32_t* indices = new uint32_t[MAX_INDICES];
	for (size_t i = 0; i < MAX_INDICES; i += 6) {
		indices[i + 0] = offset + 0;
		indices[i + 1] = offset + 1;
		indices[i + 2] = offset + 2;
		indices[i + 3] = offset + 2;
		indices[i + 4] = offset + 3;
		indices[i + 5] = offset + 0;
		offset += 4;
	}

	glGenVertexArrays(1, &m_vertex_array);
	glBindVertexArray(m_vertex_array);

	glGenBuffers(1, &m_index_buffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_index_buffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices[0]) * MAX_INDICES, indices, GL_STATIC_DRAW);
	delete[] indices;

	glGenBuffers(1, &m_vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(m_vertices.data[0]), NULL, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glGenBuffers(1, &m_pixel_buffer);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pixel_buffer);
	glBufferData(GL_PIXEL_UNPACK_BUFFER, PIXEL_BUFFER_SIZE, NULL, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

	imguiInit();

	PipelineCreateInfo movie_pipeline_ci = { "movie" };
	movie_pipeline_ci.shader = g_shader_movie;
	movie_pipeline_ci.bindings = { { BindingType::Texture, "u_Texture", TEXTURE_SLOT_DEFAULT, &m_game_texture } };
	m_movie_pipeline = Context::createPipeline(movie_pipeline_ci);

	UniformBufferCreateInfo postfx_ubo_ci;
	postfx_ubo_ci.variables = { { "sharpen", sizeof(glm::vec4) }, { "rel_size", sizeof(glm::vec2) } };
	m_postfx_ubo = Context::createUniformBuffer(postfx_ubo_ci);

	m_sharpen_data = { App.sharpen.strength.value, App.sharpen.clamp.value, App.sharpen.radius.value };
	m_postfx_ubo->updateDataVec4f("sharpen", glm::vec4(m_sharpen_data, 1.0f));

	PipelineCreateInfo postfx_pipeline_ci = { "postfx" };
	postfx_pipeline_ci.shader = g_shader_postfx;
	postfx_pipeline_ci.bindings = {
		{ BindingType::UniformBuffer, "ubo_Metrics", m_postfx_ubo->getBinding() },
		{ BindingType::FBTexture, "u_Texture0", TEXTURE_SLOT_POSTFX1, &m_postfx_framebuffer },
		{ BindingType::Texture, "u_Texture1", TEXTURE_SLOT_POSTFX2, &m_postfx_texture },
	};
	m_postfx_pipeline = Context::createPipeline(postfx_pipeline_ci);
	if (App.hdr.float_mode)
		m_postfx_pipeline->setUniform1f("u_Float", 1.0f);
	m_postfx_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));

	if (App.gl_caps.compute_shader) {
		PipelineCreateInfo fxaa_pipeline_ci = { "compute fxaa" };
		fxaa_pipeline_ci.shader = g_shader_postfx;
		fxaa_pipeline_ci.version = { 4, 3 };
		fxaa_pipeline_ci.bindings = {
			{ BindingType::Texture, "u_InTexture", TEXTURE_SLOT_POSTFX2, &m_postfx_texture },
			{ BindingType::Image, "u_OutTexture", IMAGE_UNIT_FXAA },
		};
		fxaa_pipeline_ci.compute = true;
		m_fxaa_compute_pipeline = Context::createPipeline(fxaa_pipeline_ci);
	}

	PipelineCreateInfo mod_pipeline_ci = { "module" };
	mod_pipeline_ci.shader = g_shader_mod;
	mod_pipeline_ci.attachment_blends = { { BlendType::SAlpha_OneMinusSAlpha } };
	mod_pipeline_ci.bindings = {
		{ BindingType::Texture, "u_CursorTexture", TEXTURE_SLOT_CURSOR },
		{ BindingType::Texture, "u_FontTexture", TEXTURE_SLOT_FONTS },
	};
	if (ISGLIDE3X()) {
		mod_pipeline_ci.bindings.push_back({ BindingType::FBTexture, "u_MapTexture", TEXTURE_SLOT_MAP, &m_game_framebuffer, 1 });
		mod_pipeline_ci.bindings.push_back({ BindingType::FBTexture, "u_MaskTexture", TEXTURE_SLOT_MASK, &m_game_framebuffer, 2 });
	}
	m_mod_pipeline = Context::createPipeline(mod_pipeline_ci);
	m_mod_pipeline->setUniform1i("u_IsGlide", ISGLIDE3X());

	if (ISGLIDE3X()) {
		TextureCreateInfo glide_texture_ci;
		glide_texture_ci.size = { 512, 512 };
		glide_texture_ci.layer_count = 512;
		glide_texture_ci.format = { GL_R8, GL_RED };
		m_glide_texture = std::make_unique<Texture>(glide_texture_ci);

		TextureCreateInfo movie_texture_ci;
		movie_texture_ci.size = { 640, 480 };
		movie_texture_ci.filter = { GL_LINEAR, GL_LINEAR };
		movie_texture_ci.format = { GL_RGBA8, GL_BGRA };
		m_game_texture = Context::createTexture(movie_texture_ci);

		UniformBufferCreateInfo game_ubo_ci;
		game_ubo_ci.variables = { { "palette", 256 * sizeof(glm::vec4) }, { "gamma", 256 * sizeof(glm::vec4) } };
		m_game_color_ubo = Context::createUniformBuffer(game_ubo_ci);

		PipelineCreateInfo game_pipeline_ci = { "glide" };
		game_pipeline_ci.version = { 3, 3 };
		game_pipeline_ci.shader = g_shader_glide;
		game_pipeline_ci.bindings = {
			{ BindingType::UniformBuffer, "ubo_Colors", m_game_color_ubo->getBinding() },
			{ BindingType::Texture, "u_Texture", TEXTURE_SLOT_DEFAULT, &m_glide_texture },
		};
		game_pipeline_ci.attachment_blends.clear();
		for (auto& blend : g_blend_types) {
			game_pipeline_ci.attachment_blends.push_back({ blend.second.second, BlendType::SAlpha_OneMinusSAlpha, BlendType::SAlpha_OneMinusSAlpha, BlendType::One_One, blend.second.first == 0 ? BlendType::One_Zero : BlendType::One_One });
			// emission: opaque replaces (occludes), alpha attenuates by coverage, additive/multiply add (multiply adds 0)
			if (m_emit_supported)
				game_pipeline_ci.attachment_blends.back().push_back(blend.second.first == 0 ? BlendType::One_Zero : (blend.second.first == 3 ? BlendType::SAlpha_OneMinusSAlpha :
					// float pipeline: the attachment is the additive share of the colour, so multiply must multiply it too
					(blend.second.first == 1 && App.hdr.float_mode ? BlendType::Zero_SColor : BlendType::One_One)));
		}
		m_game_pipeline = Context::createPipeline(game_pipeline_ci);

		TextureCreateInfo lut_texture_ci;
		lut_texture_ci.size = { 1024, 32 };
		lut_texture_ci.layer_count = 14;
		lut_texture_ci.slot = TEXTURE_SLOT_LUT;
		m_lut_texture = Context::createTexture(lut_texture_ci);

		auto image_data = helpers::loadImage("assets\\textures\\lut.png", false);
		m_lut_texture->fillImage(image_data, 1, 14);
		helpers::clearImage(image_data);

		if (App.gl_caps.compute_shader) {
			PipelineCreateInfo blur_pipeline_ci = { "compute blur" };
			blur_pipeline_ci.shader = g_shader_prefx;
			blur_pipeline_ci.version = { 4, 3 };
			blur_pipeline_ci.bindings = {
				{ BindingType::Texture, "u_InTexture", TEXTURE_SLOT_BLOOM2, &m_bloom_texture },
				{ BindingType::Image, "u_OutTexture", IMAGE_UNIT_BLUR },
			};
			blur_pipeline_ci.compute = true;
			m_blur_compute_pipeline = Context::createPipeline(blur_pipeline_ci);
		}

		UniformBufferCreateInfo bloom_ubo_ci;
		bloom_ubo_ci.variables = { { "bloom", sizeof(glm::vec2) }, { "rel_size", sizeof(glm::vec2) } };
		m_bloom_ubo = Context::createUniformBuffer(bloom_ubo_ci);

		m_bloom_data = { App.bloom.exposure.value, App.bloom.gamma.value };
		m_bloom_ubo->updateDataVec2f("bloom", m_bloom_data);

		PipelineCreateInfo prefx_pipeline_ci = { "prefx" };
		prefx_pipeline_ci.shader = g_shader_prefx;
		prefx_pipeline_ci.bindings = {
			{ BindingType::UniformBuffer, "ubo_Metrics", m_bloom_ubo->getBinding() },
			{ BindingType::Texture, "u_Texture", TEXTURE_SLOT_PREFX, &m_prefx_texture },
			{ BindingType::FBTexture, "u_BloomTexture1", TEXTURE_SLOT_BLOOM1, &m_bloom_framebuffer },
			{ BindingType::Texture, "u_BloomTexture2", TEXTURE_SLOT_BLOOM2, &m_bloom_texture },
			{ BindingType::Texture, "u_LUTTexture", m_lut_texture->getSlot(), &m_lut_texture },
		};
		m_prefx_pipeline = Context::createPipeline(prefx_pipeline_ci);
		if (App.hdr.float_mode)
			m_prefx_pipeline->setUniform1f("u_Float", 1.0f);
		m_prefx_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));

		PipelineCreateInfo light_pipeline_ci = { "light" };
		light_pipeline_ci.shader = g_shader_light;
		light_pipeline_ci.bindings = {
			{ BindingType::Texture, "u_Texture", TEXTURE_SLOT_PREFX, &m_prefx_texture },
			{ BindingType::FBTexture, "u_LightTexture1", TEXTURE_SLOT_LIGHT1, &m_light_framebuffer },
			{ BindingType::Texture, "u_LightTexture2", TEXTURE_SLOT_LIGHT2, &m_light_texture },
		};
		if (m_vlight_supported) {
			light_pipeline_ci.bindings.push_back({ BindingType::FBTexture, "u_VLightTexture", TEXTURE_SLOT_VLIGHT, &m_game_framebuffer, 4 });
			light_pipeline_ci.bindings.push_back({ BindingType::FBTexture, "u_AmbientTexture", TEXTURE_SLOT_AMBIENT, &m_ambient_framebuffer });
		}
		m_light_pipeline = Context::createPipeline(light_pipeline_ci);
		if (App.hdr.float_mode)
			m_light_pipeline->setUniform1f("u_Float", 1.0f);
		m_light_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));

		if (m_vlight_supported) {
			PipelineCreateInfo ambient_pipeline_ci = light_pipeline_ci;
			ambient_pipeline_ci.name = "ambient";
			ambient_pipeline_ci.attachment_blends = { { BlendType::NoBlend }, { BlendType::SAlpha_OneMinusSAlpha } };
			m_ambient_pipeline = Context::createPipeline(ambient_pipeline_ci);
			m_ambient_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));

			FrameBufferCreateInfo ambient_frambuffer_ci;
			ambient_frambuffer_ci.size = { 1, 1 };
			ambient_frambuffer_ci.attachments = { { TEXTURE_SLOT_AMBIENT, {}, { GL_NEAREST, GL_NEAREST } } };
			m_ambient_framebuffer = Context::createFrameBuffer(ambient_frambuffer_ci);
		}

		light_pipeline_ci.name = "lightfx";
		light_pipeline_ci.shader = g_shader_lightfx;
		m_lightfx_pipeline = Context::createPipeline(light_pipeline_ci);
		if (App.hdr.float_mode)
			m_lightfx_pipeline->setUniform1f("u_Float", 1.0f);
		m_lightfx_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));

		PipelineCreateInfo atmos_pipeline_ci = { "atmos" };
		atmos_pipeline_ci.shader = g_shader_atmos;
		atmos_pipeline_ci.bindings = {
			{ BindingType::Texture, "u_Texture", TEXTURE_SLOT_PREFX, &m_prefx_texture },
		};
		m_atmos_pipeline = Context::createPipeline(atmos_pipeline_ci);
		m_atmos_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));
		PipelineCreateInfo shadow_pipeline_ci = { "shadow" };
		shadow_pipeline_ci.shader = g_shader_shadow;
		shadow_pipeline_ci.attachment_blends = { { BlendType::NoBlend }, { BlendType::Zero_SColor } };
		shadow_pipeline_ci.bindings = {
			{ BindingType::FBTexture, "u_ShadowMask", TEXTURE_SLOT_SHADOW, &m_game_framebuffer, 3 },
			{ BindingType::FBTexture, "u_ShadowTexture1", TEXTURE_SLOT_SHADOW1, &m_shadow_framebuffer },
			{ BindingType::Texture, "u_ShadowTexture2", TEXTURE_SLOT_SHADOW2, &m_shadow_texture },
		};
		m_shadow_pipeline = Context::createPipeline(shadow_pipeline_ci);
		m_shadow_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));

		PipelineCreateInfo uilayer_pipeline_ci = { "uilayer" };
		uilayer_pipeline_ci.shader = g_shader_uilayer;
		uilayer_pipeline_ci.bindings = {
			{ BindingType::FBTexture, "u_FrameTexture", TEXTURE_SLOT_GAME, &m_game_framebuffer, 0 },
			{ BindingType::FBTexture, "u_WorldTexture", TEXTURE_SLOT_WORLD, &m_world_framebuffer, 0 },
		};
		m_uilayer_pipeline = Context::createPipeline(uilayer_pipeline_ci);
		m_uilayer_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));

		if (m_gi_supported) {
			PipelineCreateInfo gi_pipeline_ci = { "gi" };
			gi_pipeline_ci.shader = g_shader_gi;
			gi_pipeline_ci.bindings = {
				{ BindingType::Texture, "u_Texture", TEXTURE_SLOT_PREFX, &m_prefx_texture },
				{ BindingType::FBTexture, "u_ShadowTexture", TEXTURE_SLOT_SHADOW, &m_game_framebuffer, 3 },
				{ BindingType::FBTexture, "u_Scene", TEXTURE_SLOT_GI_SCENE, &m_gi_scene_fb },
				{ BindingType::FBTexture, "u_GI", TEXTURE_SLOT_GI, &m_gi_fb },
			};
			m_gi_pipeline = Context::createPipeline(gi_pipeline_ci);
			m_gi_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));
			m_gi_pipeline->setUniform1f("u_Interval", 1.0f);
		}

		if (m_emit_supported) {
			PipelineCreateInfo emit_pipeline_ci = { "emit" };
			emit_pipeline_ci.shader = g_shader_emit;
			emit_pipeline_ci.bindings = {
				{ BindingType::FBTexture, "u_EmitTexture", TEXTURE_SLOT_EMIT, &m_game_framebuffer, 5 },
				{ BindingType::Texture, "u_Texture", TEXTURE_SLOT_PREFX, &m_prefx_texture },
			};
			emit_pipeline_ci.attachment_blends = { { BlendType::NoBlend }, { BlendType::One_One } }; // 1: HDR bloom up/composite
			m_emit_pipeline = Context::createPipeline(emit_pipeline_ci);
			m_emit_pipeline->setUniform1i("u_Bloom", TEXTURE_SLOT_HDRBLOOM);
			m_emit_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));
		}
	} else {
		UniformBufferCreateInfo ubo_ci;
		ubo_ci.variables = { { "palette", 256 * sizeof(glm::vec4) } };
		m_game_color_ubo = Context::createUniformBuffer(ubo_ci);

		PipelineCreateInfo game_pipeline_ci = { "ddraw" };
		game_pipeline_ci.shader = g_shader_ddraw;
		game_pipeline_ci.bindings = {
			{ BindingType::UniformBuffer, "ubo_Colors", m_game_color_ubo->getBinding() },
			{ BindingType::Texture, "u_Texture", TEXTURE_SLOT_DEFAULT, &m_game_texture },
		};
		m_game_pipeline = Context::createPipeline(game_pipeline_ci);
		m_game_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, 1.0f, -1.0f));
	}

	onResize(App.window.size, App.game.size);

	LARGE_INTEGER qpf;
	QueryPerformanceFrequency(&qpf);
	m_frame.frequency = double(qpf.QuadPart) / 1000.0;
	m_frame.frame_times.assign(MAX_FRAMETIME_SAMPLE_COUNT, m_frame.frame_time);

	m_limiter.timer = CreateWaitableTimer(NULL, TRUE, NULL);
	m_perf = App.perf.log;
	m_gpu_sync = App.perf.gpu_sync;
	setFpsLimit(!App.vsync && App.foreground_fps.active, App.foreground_fps.range.value);

	m_vertices_mod.count = 0;
	m_vertices_mod.ptr = m_vertices_mod.data[m_frame_index].data();

	m_frame.vertex_count = 0;
	m_frame.drawcall_count = 0;

	for (uint32_t i = 0; i < MAX_FRAME_LATENCY; i++) {
		m_semaphore_cpu[i] = CreateSemaphore(NULL, 0, 1, NULL);
		m_semaphore_gpu[i] = CreateSemaphore(NULL, 0, 1, NULL);
		ReleaseSemaphore(m_semaphore_gpu[i], 1, NULL);
	}

	modules::HDText::Instance();
	modules::HDCursor::Instance();

	wglMakeCurrent(NULL, NULL);
	m_render_thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Context::renderThread, reinterpret_cast<void*>(this), 0, NULL);
}

Context::~Context()
{
	m_rendering = false;
	for (uint32_t i = 0; i < MAX_FRAME_LATENCY; i++)
		ReleaseSemaphore(m_semaphore_cpu[i], 1, NULL);

	// Do not make the GL context current here while the render thread may still own it.
	// Bounded: at process exit the thread is usually already gone (handle signalled).
	if (m_render_thread) {
		WaitForSingleObject(m_render_thread, 2000);
		CloseHandle(m_render_thread);
		m_render_thread = nullptr;
	}

	for (uint32_t i = 0; i < MAX_FRAME_LATENCY; i++)
		WaitForSingleObject(m_semaphore_gpu[i], INFINITE);

	wglMakeCurrent(m_gl_hdc, m_context);
	imguiDestroy();

	glDeleteBuffers(1, &m_pixel_buffer);
	glDeleteBuffers(1, &m_vertex_buffer);
	glDeleteBuffers(1, &m_index_buffer);
	glDeleteVertexArrays(1, &m_vertex_array);

	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(m_context);
}

// P3 pace_to_refresh: exact refresh rate. DwmGetCompositionTimingInfo gives the compositor's rational
// rate (loaded dynamically, may be missing under Wine/gamescope); EnumDisplaySettings gives whole Hz.
static double displayRefreshHz(HWND hwnd)
{
	if (HMODULE dwm = LoadLibraryA("dwmapi.dll")) {
		typedef HRESULT(WINAPI * Fn)(HWND, DWM_TIMING_INFO*);
		double hz = 0.0;
		if (auto fn = (Fn)GetProcAddress(dwm, "DwmGetCompositionTimingInfo")) {
			DWM_TIMING_INFO t = {};
			t.cbSize = sizeof(t);
			if (SUCCEEDED(fn(NULL, &t)) && t.rateRefresh.uiDenominator && t.rateRefresh.uiNumerator)
				hz = (double)t.rateRefresh.uiNumerator / t.rateRefresh.uiDenominator;
		}
		FreeLibrary(dwm);
		if (hz >= 23.0 && hz <= 500.0)
			return hz;
	}
	MONITORINFOEXA mi = {};
	mi.cbSize = sizeof(mi);
	DEVMODEA dm = {};
	dm.dmSize = sizeof(dm);
	if (GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi) && EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
		return (double)dm.dmDisplayFrequency;
	return 0.0;
}

void Context::renderThread(void* context)
{
	Context* ctx = reinterpret_cast<Context*>(context);
	wglMakeCurrent(ctx->m_gl_hdc, ctx->m_context);
	if (App.hdr.mode) {
		ctx->m_hdr = std::make_unique<hdr::Presenter>();
		if (!ctx->m_hdr->init(App.hwnd, (int)App.window.size.x, (int)App.window.size.y))
			ctx->hdrFallbackToWgl("presenter init failed");
	}
	uint32_t frame_index = 0;
	uint32_t limiter_generation = 0;
	GLsync pending_sync = nullptr; // gpu_sync = 1 only
	const bool early_release = App.perf.early_release; // fixed for the thread's lifetime
	const bool hdr_single_fence = App.perf.hdr_single_fence;
	const double refresh_hz = App.perf.pace_to_refresh ? displayRefreshHz(App.hwnd) : 0.0;
	if (App.perf.pace_to_refresh)
		trace_log("pace_to_refresh: display refresh %.3f Hz", refresh_hz);

	// R0: everything below is inert unless perf_log = 1 (one predictable branch per point).
	const bool perf_on = ctx->m_perf;
	std::unique_ptr<perf::PerfLogger> perf_logger;
	perf::GpuTimer gpu_timer;
	const HANDLE self = GetCurrentThread();
	double perf_cpu = -1.0, perf_gpu_ms = -1.0;
	int64_t perf_gpu_frame = -1, perf_prev_swap = 0;
	if (perf_on) {
		perf_logger = std::make_unique<perf::PerfLogger>("d2gl_perf.csv", "d2gl_perf_summary.jsonl", App.perf.summary_every);
		gpu_timer.init();
		perf_cpu = perf::threadCpuMs(self);
	}

	glBindBuffer(GL_ARRAY_BUFFER, ctx->m_vertex_buffer);
	Vertex::enableAttribArray();

	while (ctx->m_rendering.load(std::memory_order_acquire)) {
		const int64_t t_wait = perf_on ? ctx->qpcNow() : 0;
		// P3 hdr_latency_waitable: wait for DXGI before taking a frame, so the frame rendered is the
		// newest one and Present() does not block on the queue. Counted in render_wait_ms.
		if (ctx->m_hdr)
			ctx->m_hdr->waitForFrame();
		WaitForSingleObject(ctx->m_semaphore_cpu[frame_index], INFINITE);
		// Acquire: WaitForSingleObject is a full barrier, everything the game thread wrote into this
		// slot (commands, vertices, m_settings, m_game_timing) before ReleaseSemaphore is visible now.
		const int64_t t_start = perf_on ? ctx->qpcNow() : 0;
		const auto cmd = &ctx->m_command_buffer[frame_index];
		if (!cmd->m_settings.valid) // only a shutdown wake-up of a never-presented slot
			ctx->captureSettings(cmd->m_settings);
		// Copies, not references: the limiter and perf code below run after ReleaseSemaphore(gpu),
		// when the game thread may already be re-recording this slot.
		const perf::FrameSettings st = cmd->m_settings;
		const perf::GameTiming game_timing = cmd->m_game_timing;
		if (perf_on)
			gpu_timer.begin(game_timing.frame);

		if (cmd->m_resized)
			ctx->onResize(cmd->m_window_size, cmd->m_game_size, cmd->m_game_tex_bpp);
		else if (ctx->m_game_framebuffer && (ctx->emitWanted() != ctx->m_emit_attached || (ctx->m_emit_attached && ctx->emitGradedWanted() != ctx->m_emit_graded)))
			ctx->onResize(cmd->m_window_size, cmd->m_game_size, ctx->m_color_bpp); // [HDR] emissive toggled: rebuild the game framebuffer

		if (ctx->m_hdr && !ctx->m_hdr->beginFrame(st.window_w, st.window_h))
			ctx->hdrFallbackToWgl("frame setup failed");

		if (ctx->m_current_shader != App.shader.selected)
			ctx->onShaderChange();

		if (cmd->m_vertex_count)
			glBufferSubData(GL_ARRAY_BUFFER, 0, cmd->m_vertex_count * sizeof(Vertex), ctx->m_vertices.data[frame_index].data());

		if (cmd->m_tex_update_queue.count) {
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, ctx->m_pixel_buffer);
			glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, cmd->m_tex_update_queue.data_offset, cmd->m_tex_buffer);
			for (uint32_t i = 0; i < cmd->m_tex_update_queue.count; i++) {
				const auto data = &cmd->m_tex_update_queue.tex_data[i];
				ctx->m_glide_texture->fill((uint8_t*)data->offset, data->tex_size.x, data->tex_size.y, data->tex_offset.x, data->tex_offset.y, data->tex_num);
			}
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		}

		if (cmd->m_tex_update.bit && ctx->m_game_texture) {
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, ctx->m_pixel_buffer);
			glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, cmd->m_tex_update.size.x * cmd->m_tex_update.size.y * cmd->m_tex_update.bit, cmd->m_tex_buffer);
			ctx->m_game_texture->fill(0, cmd->m_tex_update.size.x, cmd->m_tex_update.size.y);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		}

		Vertex::bindingDescription();
		const glm::ivec2 vp_size = { st.vp_w, st.vp_h };
		const glm::ivec2 vp_offset = { st.vp_x, st.vp_y };

		for (uint32_t i = 0; i < cmd->m_count; i++) {
			const auto command = &cmd->m_commands[i];

			switch (command->type) {
				case CommandType::UBOUpdate: {
					const auto data = &cmd->m_ubo_update_queue.data[command->index];
					ctx->m_game_color_ubo->updateData(data->type == UBOType::Gamma ? "gamma" : "palette", data->value);
				} break;
				case CommandType::SetBlendState:
					// Uniform first: Pipeline::bind() inside setUniform resets the blend state to 0.
					ctx->m_game_pipeline->setUniform1i("u_BlendIndex", command->index);
					ctx->m_game_pipeline->setUniform1f("u_SpellGlow", st.spell_glow);
					if (App.hdr.float_mode)
						ctx->m_game_pipeline->setUniform1f("u_AddScale", ctx->m_add_scale);
					if (App.hdr.float_mode)
						ctx->m_game_pipeline->setUniform1f("u_EmitRGB", ctx->m_emit_attached ? 1.0f : 0.0f);
					ctx->bindPipeline(ctx->m_game_pipeline, command->index);
					break;
				case CommandType::UIStage:
					// Float pipeline: UI draws (panels, text, minimap) must stay <= 1.0 (paper white).
					ctx->m_add_scale = 1.0f;
					ctx->m_game_pipeline->setUniform1f("u_AddScale", 1.0f);
					ctx->bindPipeline(ctx->m_game_pipeline, command->index);
					break;
				case CommandType::DrawIndexed:
					if (command->draw.count > 0)
						glDrawElementsBaseVertex(GL_TRIANGLES, command->draw.count, GL_UNSIGNED_INT, 0, command->draw.start);
					break;
				case CommandType::PreFx:
					if (ctx->vlightActive(st))
						ctx->updateAmbient(cmd->m_game_size, st.level_no);
					if (st.light_debug && ctx->vlightActive(st)) {
						// Debug: raw game light buffer in place of the world (ambient swatch top-left).
						FrameBuffer::setDrawBuffers(1);
						ctx->bindPipeline(ctx->m_light_pipeline);
						ctx->drawQuad(4);
					} else if (st.light && ctx->m_light_tex_size.x > 0 && ctx->m_light_tex_size.y > 0) {
						const int vsource = ctx->vlightActive(st) ? 1 : 0;
						FrameBuffer::setDrawBuffers(1);
						ctx->m_prefx_texture->fillFromBuffer(ctx->m_game_framebuffer);
						ctx->bindPipeline(ctx->m_light_pipeline);
						ctx->m_light_pipeline->setUniformVec2f("u_RelSize", { 1.0f / ctx->m_light_tex_size.x, 1.0f / ctx->m_light_tex_size.y });
						ctx->m_light_pipeline->setUniform1f("u_Intensity", st.light_intensity);
						ctx->m_light_pipeline->setUniform1i("u_Source", vsource);
						ctx->m_light_pipeline->setUniform1f("u_Haze", st.light_haze);
						ctx->bindFrameBuffer(ctx->m_light_framebuffer, false);
						ctx->setViewport(ctx->m_light_tex_size);
						ctx->drawQuad(0);
						for (int i = 0; i < 2; i++) {
							ctx->m_light_texture->fillFromBuffer(ctx->m_light_framebuffer);
							ctx->drawQuad(1);
							ctx->m_light_texture->fillFromBuffer(ctx->m_light_framebuffer);
							ctx->drawQuad(2);
						}
						const bool lightfx = st.light_normals;
						if (lightfx) {
							// lightfx.glsl: blurred light -> LIGHT2, shafts -> LIGHT1, then combine.
							ctx->m_light_texture->fillFromBuffer(ctx->m_light_framebuffer);
							ctx->bindPipeline(ctx->m_lightfx_pipeline);
							ctx->m_lightfx_pipeline->setUniformVec2f("u_RelSize", { 1.0f / ctx->m_light_tex_size.x, 1.0f / ctx->m_light_tex_size.y });
							ctx->m_lightfx_pipeline->setUniform1f("u_Intensity", st.light_intensity);
							ctx->m_lightfx_pipeline->setUniform1i("u_Source", vsource);
							ctx->m_lightfx_pipeline->setUniform1f("u_Haze", st.light_haze);
							ctx->m_lightfx_pipeline->setUniform1f("u_Normals", (st.light_normals ? st.light_normals_strength : 0.0f));
							ctx->m_lightfx_pipeline->setUniform1f("u_Shafts", 0.0f);
						}
						ctx->bindFrameBuffer(ctx->m_game_framebuffer, false);
						ctx->setViewport(cmd->m_game_size);
						ctx->bindPipeline(lightfx ? ctx->m_lightfx_pipeline : ctx->m_light_pipeline);
						FrameBuffer::setDrawBuffers(1);
						ctx->drawQuad(lightfx ? 1 : 3);
					}
					if (st.gi && ctx->m_gi_pipeline)
						ctx->renderGI(st, cmd->m_game_size);
					if (st.bloom || st.lut) {
					ctx->m_prefx_texture->fillFromBuffer(ctx->m_game_framebuffer);
					ctx->bindPipeline(ctx->m_prefx_pipeline);

					if (st.bloom) {
						ctx->bindFrameBuffer(ctx->m_bloom_framebuffer, false);
						ctx->setViewport(ctx->m_bloom_tex_size);
						ctx->drawQuad();

						if (App.gl_caps.compute_shader) {
							ctx->m_bloom_texture->fillFromBuffer(ctx->m_bloom_framebuffer);
							ctx->m_blur_compute_pipeline->dispatchCompute(0, ctx->m_bloom_work_size, GL_PIXEL_BUFFER_BARRIER_BIT);
							ctx->m_bloom_texture->fillFromBuffer(ctx->m_bloom_framebuffer);
							ctx->m_blur_compute_pipeline->dispatchCompute(1, ctx->m_bloom_work_size, GL_PIXEL_BUFFER_BARRIER_BIT);
							ctx->m_bloom_texture->fillFromBuffer(ctx->m_bloom_framebuffer);
							ctx->m_blur_compute_pipeline->dispatchCompute(0, ctx->m_bloom_work_size, GL_PIXEL_BUFFER_BARRIER_BIT);
							ctx->m_bloom_texture->fillFromBuffer(ctx->m_bloom_framebuffer);
							ctx->m_blur_compute_pipeline->dispatchCompute(1, ctx->m_bloom_work_size, GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
						} else {
							ctx->m_bloom_texture->fillFromBuffer(ctx->m_bloom_framebuffer);
							ctx->drawQuad(1);
							ctx->m_bloom_texture->fillFromBuffer(ctx->m_bloom_framebuffer);
							ctx->drawQuad(2);
							ctx->m_bloom_texture->fillFromBuffer(ctx->m_bloom_framebuffer);
							ctx->drawQuad(1);
							ctx->m_bloom_texture->fillFromBuffer(ctx->m_bloom_framebuffer);
							ctx->drawQuad(2);
						}

						ctx->bindFrameBuffer(ctx->m_game_framebuffer, false);
						ctx->setViewport(cmd->m_game_size);
						ctx->bindPipeline(ctx->m_prefx_pipeline);
						FrameBuffer::setDrawBuffers(1);
					}
					ctx->drawQuad(3 + st.bloom, 0, st.lut);
					}

					if (st.tonemap) {
						FrameBuffer::setDrawBuffers(1);
						ctx->bindPipeline(ctx->m_atmos_pipeline);
						ctx->m_atmos_pipeline->setUniformVec2f("u_RelSize", { 1.0f / cmd->m_game_size.x, 1.0f / cmd->m_game_size.y });
						ctx->m_prefx_texture->fillFromBuffer(ctx->m_game_framebuffer);
						ctx->m_atmos_pipeline->setUniform1f("u_Exposure", st.tonemap_exposure);
						ctx->drawQuad(1);
					}

					if (App.hdr.float_mode && ctx->m_emit_attached && ctx->m_emit_pipeline && (App.hdr.diffuse_shoulder || App.hdr.auto_exposure))
						ctx->hdrSceneFx(st, cmd->m_game_size);
					if (App.hdr.float_mode && ctx->m_emit_attached && ctx->m_emit_pipeline && App.hdr.hdr_bloom && ctx->m_hdrbloom_supported)
						ctx->hdrBloom(cmd->m_game_size);

					ctx->bindPipeline(ctx->m_game_pipeline, command->index);
					FrameBuffer::setDrawBuffers(ctx->gameDrawBufferCount(st));
					break;
				case CommandType::SoftShadow:
					if (ctx->m_shadow_tex_size.x > 0 && ctx->m_shadow_tex_size.y > 0) {
						ctx->bindPipeline(ctx->m_shadow_pipeline);
						ctx->m_shadow_pipeline->setUniformVec2f("u_RelSize", { 1.0f / ctx->m_shadow_tex_size.x, 1.0f / ctx->m_shadow_tex_size.y });
						ctx->m_shadow_pipeline->setUniform1f("u_Softness", st.shadow_softness);
						ctx->bindFrameBuffer(ctx->m_shadow_framebuffer, false);
						ctx->setViewport(ctx->m_shadow_tex_size);
						ctx->drawQuad(0);
						for (int i = 0; i < 2; i++) {
							ctx->m_shadow_texture->fillFromBuffer(ctx->m_shadow_framebuffer);
							ctx->drawQuad(1);
							ctx->m_shadow_texture->fillFromBuffer(ctx->m_shadow_framebuffer);
							ctx->drawQuad(2);
						}
						ctx->bindFrameBuffer(ctx->m_game_framebuffer, false);
						ctx->setViewport(cmd->m_game_size);
						FrameBuffer::setDrawBuffers(1);
						ctx->bindPipeline(ctx->m_shadow_pipeline, 1);
						ctx->drawQuad(3);
						ctx->bindPipeline(ctx->m_game_pipeline, command->index);
						FrameBuffer::setDrawBuffers(ctx->gameDrawBufferCount(st));
					}
					break;
				case CommandType::WorldSnapshot:
					ctx->m_world_framebuffer->getTexture()->fillFromBuffer(ctx->m_game_framebuffer);
					ctx->bindFrameBuffer(ctx->m_game_framebuffer, false);
					FrameBuffer::setDrawBuffers(ctx->gameDrawBufferCount(st));
					ctx->m_world_ready = true;
					break;
				case CommandType::Begin:
					ctx->m_world_ready = false;
					ctx->m_add_scale = cmd->m_screen == GameScreen::InGame ? st.add_scale : 1.0f;
					if (cmd->m_screen == GameScreen::Movie) {
						ctx->bindDefaultFrameBuffer();
						ctx->setViewport({ st.window_w, st.window_h });
					} else {
						if (ISGLIDE3X()) {
							// Set draw buffers first so the clear also covers the light attachment.
							ctx->bindFrameBuffer(ctx->m_game_framebuffer, false);
							FrameBuffer::setDrawBuffers(ctx->gameDrawBufferCount(st));
						}
						ctx->bindFrameBuffer(ctx->m_game_framebuffer, ISGLIDE3X());
						ctx->setViewport(cmd->m_game_size);
					}
					break;
				case CommandType::Submit:
					if (cmd->m_screen == GameScreen::Movie) {
						ctx->bindPipeline(ctx->m_movie_pipeline);
						ctx->drawQuad();
					} else {
						if (st.sharpen) {
														const auto sharpen_data = glm::vec3(st.sharpen_strength, st.sharpen_clamp, st.sharpen_radius);
							if (ctx->m_sharpen_data != sharpen_data) {
								ctx->m_postfx_ubo->updateDataVec4f("sharpen", glm::vec4(sharpen_data, 1.0f));
								ctx->m_sharpen_data = sharpen_data;
							}
						}

						if (ISGLIDE3X()) {
							if (st.bloom) {
								const auto bloom_data = glm::vec2(st.bloom_exposure, st.bloom_gamma);
								if (ctx->m_bloom_data != bloom_data) {
									ctx->m_bloom_ubo->updateDataVec2f("bloom", bloom_data);
									ctx->m_bloom_data = bloom_data;
								}
							}
						} else {
							ctx->bindPipeline(ctx->m_game_pipeline);
							ctx->drawQuad();
						}

						// world-only shader: the preset runs on the snapshot taken before the UI was drawn,
						// and the UI is laid on top crisply by the uilayer pass below.
						const bool ui_layer = ISGLIDE3X() && st.world_only && ctx->m_world_ready;
						const auto& upscale_in = ui_layer ? ctx->m_world_framebuffer : ctx->m_game_framebuffer;
						if (st.sharpen || st.fxaa)
							Upscaler::Instance().process(upscale_in, vp_size, vp_offset, ctx->m_postfx_framebuffer);
						else
							Upscaler::Instance().process(upscale_in, vp_size, vp_offset);

						if (st.sharpen) {
							if (st.fxaa)
								ctx->m_postfx_texture->fillFromBuffer(ctx->m_postfx_framebuffer);
							else {
								ctx->bindDefaultFrameBuffer();
								ctx->setViewport(vp_size, vp_offset);
							}
							ctx->bindPipeline(ctx->m_postfx_pipeline);
							ctx->drawQuad(st.fxaa);
						}

						if (st.fxaa) {
							if (App.gl_caps.compute_shader) {
								ctx->m_postfx_texture->fillFromBuffer(ctx->m_postfx_framebuffer);
								ctx->m_fxaa_compute_pipeline->dispatchCompute(st.fxaa_preset, ctx->m_fxaa_work_size, GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
							}
							ctx->bindDefaultFrameBuffer();
							ctx->setViewport(vp_size, vp_offset);
							ctx->bindPipeline(ctx->m_postfx_pipeline);
							ctx->drawQuad(2 + App.gl_caps.compute_shader, st.fxaa_preset);
						}

						// [HDR] emissive: default-FB alpha = 1 - emission (1 = no boost), so the UI, HD text and
						// menus drawn after this blend alpha back toward 1 and are never boosted.
						if (ISGLIDE3X() && ctx->m_emit_attached && ctx->m_emit_pipeline && !App.hdr.float_mode) {
							FrameBuffer::unBind();
							ctx->setViewport(vp_size, vp_offset);
							ctx->m_emit_pipeline->setUniform1f("u_Gain", App.hdr.emissive_gain.value);
							ctx->m_emit_pipeline->setUniform1i("u_Graded", ctx->m_emit_graded ? 1 : 0); // 0: output as before
							ctx->bindPipeline(ctx->m_emit_pipeline);
							glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
							ctx->drawQuad();
							glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
						}

						if (ui_layer) {
							FrameBuffer::unBind(); // not bindDefaultFrameBuffer(): that clears the shaded world
							ctx->setViewport(vp_size, vp_offset);
							ctx->bindPipeline(ctx->m_uilayer_pipeline);
							ctx->drawQuad();
						}
					}
					break;
				case CommandType::TakeScreenShot:
					ctx->takeScreenShot();
					break;
			}
		}

		if (cmd->m_vertex_mod_count) {
			glBufferSubData(GL_ARRAY_BUFFER, 0, cmd->m_vertex_mod_count * sizeof(VertexMod), ctx->m_vertices_mod.data[frame_index].data());

			ctx->bindPipeline(ctx->m_mod_pipeline);
			ctx->m_mod_pipeline->setUniform1i("u_OccluderCount", cmd->m_occluders.count);
			if (cmd->m_occluders.count) {
				ctx->m_mod_pipeline->setUniformVec4fArray("u_Occluders", cmd->m_occluders.rects, cmd->m_occluders.count);
				ctx->m_mod_pipeline->setUniform1fArray("u_OccluderAtten", cmd->m_occluders.attenuations, cmd->m_occluders.count);
			}
			if (cmd->m_hd_text_mask.active) {
				ctx->m_mod_pipeline->setUniformVec4f("u_TextMask", cmd->m_hd_text_mask.metrics);
				ctx->m_mod_pipeline->setUniform1i("u_IsMasking", cmd->m_hd_text_mask.masking);
				cmd->m_hd_text_mask.active = false;
			}

			VertexMod::bindingDescription();
			glDrawElements(GL_TRIANGLES, cmd->m_vertex_mod_count / 4 * 6, GL_UNSIGNED_INT, 0);
		}

		int64_t t_fence = 0, t_fenced = 0;
		if (perf_on) {
			gpu_timer.end();
			t_fence = ctx->qpcNow();
		}

		// R1 (early_slot_release=true): every read of this slot (commands, m_vertices, m_tex_buffer)
		// is done; glBufferSubData copied client memory before returning. Hand the slot back now so
		// the game thread records the next frame while we wait on the GPU. GPU-side buffers are still
		// protected by the fence below, which runs before the next frame's uploads.
		if (early_release)
			ReleaseSemaphore(ctx->m_semaphore_gpu[frame_index], 1, NULL);

		if (hdr_single_fence && ctx->m_gpu_sync == 0 && ctx->m_hdr && ctx->m_hdr->fencesGlInPresent()) {
			// P3 hdr_single_fence: Presenter::present() fences all GL work (this frame + menu) before
			// D3D11 samples it, and before this thread returns to upload the next frame, so the
			// slot's GPU buffers stay protected. Just flush here so the GPU starts now.
			glFlush();
		} else if (ctx->m_gpu_sync == 0) {
			// legacy: block until this frame's GPU work completes
			GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
			glFlush();
			glClientWaitSync(sync, 0, GL_TIMEOUT_IGNORED);
			glDeleteSync(sync);
		} else {
			// gpu_sync = 1 (experimental, UNMEASURED): block on the previous frame's fence only,
			// letting the GPU overlap one frame. See docs/r0r2/FENCE_LIFECYCLE.md.
			GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
			glFlush();
			if (pending_sync) {
				glClientWaitSync(pending_sync, 0, GL_TIMEOUT_IGNORED);
				glDeleteSync(pending_sync);
			}
			pending_sync = sync;
		}
		if (perf_on)
			t_fenced = ctx->qpcNow();

		if (!early_release)
			ReleaseSemaphore(ctx->m_semaphore_gpu[frame_index], 1, NULL);
		if (ctx->m_hdr) {
			FrameBuffer::unBind(); // the menu goes into the HDR target like everything else
			option::Menu::instance().draw();
			ctx->m_hdr->setEmissive(ctx->m_emit_attached && !App.hdr.float_mode, ctx->m_emit_graded);
			ctx->m_hdr->setCinematic(cmd->m_screen != GameScreen::InGame);
			if (!ctx->m_hdr->present(App.vsync))
				ctx->hdrFallbackToWgl("present failed");
		} else {
			option::Menu::instance().draw();
			SwapBuffers(ctx->m_gl_hdc);
		}
		const int64_t t_swapped = perf_on ? ctx->qpcNow() : 0;

		if (st.limiter.generation != limiter_generation) {
			// setFpsLimit() ran on the game thread: restart pacing here, on the thread that owns it.
			limiter_generation = st.limiter.generation;
			ctx->m_limiter.pace_next = 0;
			ctx->resetFileTime();
		}

		if (st.limiter.active && st.limiter.pacing) {
			// Frame pacing: sleep to ~1.5 ms before the deadline, then spin on QPC. A frame
			// that is already more than one period late resyncs instead of bursting.
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			auto& lim = ctx->m_limiter;
			int64_t pace_period = st.limiter.pace_period;
			if (refresh_hz > 0.0) {
				// P3 pace_to_refresh: a limit of e.g. 90 against a 90.03 Hz panel beats once every
				// few seconds (one repeated or dropped frame); pace at the panel's period instead.
				const double target_hz = ctx->m_frame.frequency * 1000.0 / (double)pace_period;
				if (fabs(target_hz - refresh_hz) <= refresh_hz * 0.03)
					pace_period = (int64_t)(ctx->m_frame.frequency * 1000.0 / refresh_hz);
			}
			if (lim.pace_next == 0 || now.QuadPart - lim.pace_next > pace_period)
				lim.pace_next = now.QuadPart + pace_period;
			else {
				const double counts_per_ms = ctx->m_frame.frequency;
				for (;;) {
					QueryPerformanceCounter(&now);
					const double left_ms = (double)(lim.pace_next - now.QuadPart) / counts_per_ms;
					if (left_ms <= 0.0)
						break;
					if (left_ms > 2.0)
						Sleep((DWORD)(left_ms - 1.5));
					else
						YieldProcessor();
				}
				lim.pace_next += pace_period;
			}
		} else if (st.limiter.active) {
			WaitForSingleObject(ctx->m_limiter.timer, (DWORD)st.limiter.frame_len_ms + 1);
			ctx->m_limiter.due_time.QuadPart += st.limiter.frame_len_ns;
			SetWaitableTimer(ctx->m_limiter.timer, &ctx->m_limiter.due_time, 0, NULL, NULL, FALSE);
		}

		if (perf_on) {
			const int64_t t_end = ctx->qpcNow();
			ctx->m_inflight.fetch_sub(1, std::memory_order_relaxed);
			const double cpu = perf::threadCpuMs(self);
			double gpu_ms;
			int64_t gpu_frame;
			if (gpu_timer.poll(gpu_ms, gpu_frame)) { // non-blocking; result of an earlier frame
				perf_gpu_ms = gpu_ms;
				perf_gpu_frame = gpu_frame;
			}
			if (game_timing.frame % App.perf.sample_every == 0) {
				perf::PerfSample s;
				s.game = game_timing;
				s.slot = frame_index;
				s.t_ms = ctx->qpcMs(t_end);
				s.render_wait_ms = ctx->qpcMs(t_start - t_wait);
				s.render_busy_ms = ctx->qpcMs(t_fence - t_start);
				s.fence_ms = ctx->qpcMs(t_fenced - t_fence);
				s.swap_ms = ctx->qpcMs(t_swapped - t_fenced);
				s.limiter_ms = ctx->qpcMs(t_end - t_swapped);
				s.interval_ms = perf_prev_swap ? ctx->qpcMs(t_swapped - perf_prev_swap) : -1.0;
				s.render_cpu_ms = (perf_cpu >= 0.0 && cpu >= 0.0) ? cpu - perf_cpu : -1.0;
				s.gpu_ms = perf_gpu_ms;
				s.gpu_frame = perf_gpu_frame;
				perf_gpu_ms = -1.0; // report each GPU result once
				perf_gpu_frame = -1;
				perf_logger->push(s);
			}
			perf_prev_swap = t_swapped;
			perf_cpu = cpu;
		}

		frame_index = (frame_index + 1) % (App.frame_latency + 1);
	}

	if (pending_sync) {
		glClientWaitSync(pending_sync, 0, GL_TIMEOUT_IGNORED);
		glDeleteSync(pending_sync);
	}
	if (perf_on) {
		gpu_timer.destroy();
		perf_logger.reset(); // joins the writer, flushes the files
	}
	if (ctx->m_hdr) {
		ctx->m_hdr->shutdown();
		ctx->m_hdr.reset();
	}

	wglMakeCurrent(NULL, NULL);
	// Release every slot (the old loop released only 0 and 1, which could leave ~Context waiting
	// forever on a slot >= 2 with frame_latency >= 2). Releasing a full semaphore is a no-op.
	for (uint32_t i = 0; i < MAX_FRAME_LATENCY; i++)
		ReleaseSemaphore(ctx->m_semaphore_gpu[i], 1, NULL);
}

// Render thread. HDR presentation failed beyond recovery: drop the DXGI path and present with WGL on the
// game window, i.e. the hdr=0 path. SetPixelFormat on a window that hosted a flip swapchain is a last resort.
void Context::hdrFallbackToWgl(const char* why)
{
	error_log("HDR: falling back to SDR WGL presentation (%s)", why);
	if (m_hdr) {
		m_hdr->shutdown();
		m_hdr.reset();
	}
	FrameBuffer::setDefault(0);
	if (!GetPixelFormat(App.hdc) && !SetPixelFormat(App.hdc, m_pixel_format, &m_pfd))
		error_log("HDR: SetPixelFormat on the game window failed (%lu)", GetLastError());
	if (wglMakeCurrent(App.hdc, m_context))
		m_gl_hdc = App.hdc;
	else
		error_log("HDR: wglMakeCurrent on the game window failed (%lu)", GetLastError());
	wglSwapIntervalEXT(App.vsync);
	App.hdr.mode = false;
	App.hdr.float_mode = false; // existing float buffers stay; the WGL framebuffer clips them to SDR
}

void Context::onResize(glm::uvec2 w_size, glm::uvec2 g_size, uint32_t bpp)
{
	static glm::uvec2 game_size = { 0, 0 };
	static uint32_t color_bpp = 8;
	bool game_resized = game_size != g_size || color_bpp != bpp;
	game_size = g_size;
	color_bpp = bpp;
	m_color_bpp = bpp;
	const bool emit_want = emitWanted();
	if (emit_want != m_emit_attached || (emit_want && emitGradedWanted() != m_emit_graded))
		game_resized = true;

	static glm::uvec2 window_size = { 0, 0 };
	bool window_resized = window_size != w_size;
	window_size = w_size;

	if (game_resized) {
		glm::mat4 mvp = glm::ortho(0.0f, (float)game_size.x, (float)game_size.y, 0.0f);
		modules::HDText::Instance().setMVP(mvp);
		m_mod_pipeline->setUniformMat4f("u_MVP", mvp);

		FrameBufferCreateInfo frambuffer_ci;
		frambuffer_ci.size = game_size;
		if (ISGLIDE3X())
			frambuffer_ci.attachments = {
				{ TEXTURE_SLOT_GAME, { 0.0f, 0.0f, 0.0f, 1.0f }, { GL_LINEAR, GL_LINEAR }, sceneFormat() },
				{ TEXTURE_SLOT_MAP, { 0.0f, 0.0f, 0.0f, 0.0f } },
				{ TEXTURE_SLOT_MASK, { 0.0f, 0.0f, 0.0f, 0.0f }, { GL_LINEAR, GL_LINEAR }, { GL_R8, GL_RED } },
				{ TEXTURE_SLOT_SHADOW, { 0.0f, 0.0f, 0.0f, 0.0f }, { GL_LINEAR, GL_LINEAR } },
			};
		else
			frambuffer_ci.attachments = { { TEXTURE_SLOT_GAME } };
		if (ISGLIDE3X() && m_vlight_supported) {
			// Game vertex lighting (GlideVertex pargb) of world draws; alpha = coverage.
			frambuffer_ci.attachments.push_back({ TEXTURE_SLOT_VLIGHT, { 0.0f, 0.0f, 0.0f, 0.0f }, { GL_LINEAR, GL_LINEAR } });
			if (emit_want) // r = emission of additive world sprites (HDR emissive highlights)
				frambuffer_ci.attachments.push_back({ TEXTURE_SLOT_EMIT, { 0.0f, 0.0f, 0.0f, 0.0f }, { GL_LINEAR, GL_LINEAR },
					App.hdr.float_mode ? std::pair<GLint, GLenum>{ GL_RGBA16F, GL_RGBA } : (emitGradedWanted() ? std::pair<GLint, GLenum>{ GL_R16F, GL_RED } : std::pair<GLint, GLenum>{ GL_R8, GL_RED }) }); // float: rgb additive share; graded: unclamped sum
		}
		m_emit_attached = ISGLIDE3X() && m_vlight_supported && emit_want;
		m_emit_graded = m_emit_attached && emitGradedWanted();
		m_game_framebuffer = Context::createFrameBuffer(frambuffer_ci);

		if (ISGLIDE3X()) {
			m_game_pipeline->setUniformMat4f("u_MVP", mvp);

			m_bloom_ubo->updateDataVec2f("rel_size", { 4.0f / game_size.x, 4.0f / game_size.y });
			m_bloom_tex_size = { game_size.x / 4, game_size.y / 4 };
			m_bloom_work_size = { ceil((float)m_bloom_tex_size.x / 16), ceil((float)m_bloom_tex_size.y / 16) };

			FrameBufferCreateInfo bloom_frambuffer_ci;
			bloom_frambuffer_ci.size = m_bloom_tex_size;
			bloom_frambuffer_ci.attachments = { { TEXTURE_SLOT_BLOOM1, {}, { GL_LINEAR, GL_LINEAR }, sceneFormat() } };
			m_bloom_framebuffer = Context::createFrameBuffer(bloom_frambuffer_ci);
			if (App.gl_caps.compute_shader)
				m_bloom_framebuffer->getTexture()->bindImage(IMAGE_UNIT_BLUR);

			TextureCreateInfo bloom_texture_ci;
			bloom_texture_ci.size = m_bloom_tex_size;
			bloom_texture_ci.slot = TEXTURE_SLOT_BLOOM2;
			bloom_texture_ci.filter = { GL_LINEAR, GL_LINEAR };
			bloom_texture_ci.format = sceneFormat();
			m_bloom_texture = Context::createTexture(bloom_texture_ci);

			m_light_tex_size = { game_size.x / 8, game_size.y / 8 };
			FrameBufferCreateInfo light_frambuffer_ci;
			light_frambuffer_ci.size = m_light_tex_size;
			light_frambuffer_ci.attachments = { { TEXTURE_SLOT_LIGHT1, {}, { GL_LINEAR, GL_LINEAR } } };
			m_light_framebuffer = Context::createFrameBuffer(light_frambuffer_ci);

			TextureCreateInfo light_texture_ci;
			light_texture_ci.size = m_light_tex_size;
			light_texture_ci.slot = TEXTURE_SLOT_LIGHT2;
			light_texture_ci.filter = { GL_LINEAR, GL_LINEAR };
			m_light_texture = Context::createTexture(light_texture_ci);

			m_shadow_tex_size = { game_size.x / 2, game_size.y / 2 };
			FrameBufferCreateInfo shadow_frambuffer_ci;
			shadow_frambuffer_ci.size = m_shadow_tex_size;
			shadow_frambuffer_ci.attachments = { { TEXTURE_SLOT_SHADOW1, {}, { GL_LINEAR, GL_LINEAR } } };
			m_shadow_framebuffer = Context::createFrameBuffer(shadow_frambuffer_ci);

			TextureCreateInfo shadow_texture_ci;
			shadow_texture_ci.size = m_shadow_tex_size;
			shadow_texture_ci.slot = TEXTURE_SLOT_SHADOW2;
			shadow_texture_ci.filter = { GL_LINEAR, GL_LINEAR };
			m_shadow_texture = Context::createTexture(shadow_texture_ci);

			TextureCreateInfo prefx_texture_ci;
			prefx_texture_ci.size = game_size;
			prefx_texture_ci.slot = TEXTURE_SLOT_PREFX;
			prefx_texture_ci.format = sceneFormat();
			m_prefx_texture = Context::createTexture(prefx_texture_ci);

			FrameBufferCreateInfo world_frambuffer_ci;
			world_frambuffer_ci.size = game_size;
			world_frambuffer_ci.attachments = { { TEXTURE_SLOT_WORLD, { 0.0f, 0.0f, 0.0f, 1.0f }, { GL_LINEAR, GL_LINEAR }, sceneFormat() } };
			m_world_framebuffer = Context::createFrameBuffer(world_frambuffer_ci);
		} else {
			TextureCreateInfo texture_ci;
			texture_ci.size = game_size;
			if (color_bpp == 8)
				texture_ci.format = { GL_R8, GL_RED };
			else
				texture_ci.format = { GL_RGBA8, GL_BGRA };
			texture_ci.filter = { GL_LINEAR, GL_LINEAR };
			m_game_texture = Context::createTexture(texture_ci);
		}
	}

	if (window_resized) {
		glm::vec2 scale = { (float)window_size.x / 640, (float)window_size.y / 360 };
		float offset_x = ((scale.x > scale.y) ? scale.x / scale.y : 1.0f) * 1.00f;
		float offset_y = ((scale.x < scale.y) ? scale.y / scale.x : 1.0f) * 0.75f;
		m_movie_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f * offset_x, 1.0f * offset_x, 1.0f * offset_y, -1.0f * offset_y));
	}

	if (game_resized || window_resized) {
		FrameBufferCreateInfo frambuffer_ci;
		frambuffer_ci.size = App.viewport.size;
		frambuffer_ci.attachments = { { TEXTURE_SLOT_POSTFX1, {}, { GL_LINEAR, GL_LINEAR }, ISGLIDE3X() ? sceneFormat() : std::pair<GLint, GLenum>{ GL_RGBA8, GL_RGBA } } };
		m_postfx_framebuffer = Context::createFrameBuffer(frambuffer_ci);
		if (App.gl_caps.compute_shader)
			m_postfx_framebuffer->getTexture()->bindImage(IMAGE_UNIT_FXAA);

		m_fxaa_work_size = { ceil((float)App.viewport.size.x / 16), ceil((float)App.viewport.size.y / 16) };

		TextureCreateInfo texture_ci;
		texture_ci.size = App.viewport.size;
		texture_ci.slot = TEXTURE_SLOT_POSTFX2;
		texture_ci.filter = { GL_LINEAR, GL_LINEAR };
		if (ISGLIDE3X())
			texture_ci.format = sceneFormat();
		m_postfx_texture = Context::createTexture(texture_ci);

		onShaderChange();
	}

	m_postfx_ubo->updateDataVec2f("rel_size", { 1.0f / App.viewport.size.x, 1.0f / App.viewport.size.y });
	m_mod_pipeline->setUniformVec2f("u_Scale", App.viewport.scale);
	if (m_uilayer_pipeline)
		m_uilayer_pipeline->setUniformVec2f("u_Scale", App.viewport.scale);

	const glm::ivec2 vp_size = { App.viewport.stretched.x ? App.window.size.x : App.viewport.size.x, App.viewport.stretched.y ? App.window.size.y : App.viewport.size.y };
	const glm::ivec2 vp_offset = { App.viewport.stretched.x ? 0 : App.viewport.offset.x, App.viewport.stretched.y ? 0 : App.viewport.offset.y };
	m_mod_pipeline->setUniformVec4f("u_Viewport", { (float)vp_offset.x, (float)vp_offset.y, (float)vp_size.x, (float)vp_size.y });

	modules::MiniMap::Instance().resize();
	toggleVsync();
}

// The light attachment (3) is only written while game vertex lighting is in use.
// Game vertex lighting: the scene ambient is re-estimated every frame from a few
// screen samples, which jumps as shadows move in and out of them. Keep it in a 1x1
// target and blend each new estimate in with a ~0.8 s time constant (snap on reset).
void Context::updateAmbient(glm::ivec2 game_size, int level)
{
	static auto last = std::chrono::steady_clock::now();
	static int last_level = -1;
	const auto now = std::chrono::steady_clock::now();
	const float dt = glm::clamp(std::chrono::duration<float>(now - last).count(), 0.0f, 0.5f);
	last = now;

	if (level != last_level) {
		last_level = level;
		m_ambient_reset = true;
	}
	const float rate = m_ambient_reset ? 1.0f : 1.0f - expf(-dt / 0.8f);
	m_ambient_reset = false;

	// Uniforms first: setUniform binds the pipeline with blend index 0.
	m_ambient_pipeline->setUniform1f("u_AmbientRate", rate);
	bindFrameBuffer(m_ambient_framebuffer, false);
	setViewport({ 1, 1 });
	FrameBuffer::setDrawBuffers(1);
	bindPipeline(m_ambient_pipeline, 1);
	drawQuad(5);
	bindFrameBuffer(m_game_framebuffer, false);
	setViewport(game_size);
}

uint32_t Context::gameDrawBufferCount(const perf::FrameSettings& st) const
{
	const uint32_t count = m_game_framebuffer->getAttachmentCount();
	// 0 game, 1 map, 2 mask, 3 shadow (always drawn so it is cleared every frame), 4 vlight (only while used)
	if (count < 5)
		return count;
	if (count >= 6)
		return count; // emission at location 5 needs every attachment bound
	const bool vlight = st.light && (st.light_vsource || st.light_debug);
	return vlight ? 5 : 4;
}

bool Context::vlightActive(const perf::FrameSettings& st) const
{
	return m_game_framebuffer && m_game_framebuffer->getAttachmentCount() >= 5 && st.light && (st.light_vsource || st.light_debug);
}

void Context::renderGI(const perf::FrameSettings& st, glm::uvec2 game_size)
{
	if (m_gi_game_size != game_size || !m_gi_fb) {
		m_gi_game_size = game_size;
		// 1/4 game res, multiple of 8 so every cascade's probe grid divides evenly
		m_gi_size = { std::max((game_size.x / 4 + 7) / 8 * 8, 8u), std::max((game_size.y / 4 + 7) / 8 * 8, 8u) };
		FrameBufferCreateInfo ci;
		ci.size = m_gi_size;
		ci.attachments = { { TEXTURE_SLOT_GI_SCENE, { 0.0f, 0.0f, 0.0f, 0.0f }, { GL_LINEAR, GL_LINEAR }, { GL_RGBA16F, GL_RGBA } } };
		m_gi_scene_fb = Context::createFrameBuffer(ci);
		ci.attachments = { { TEXTURE_SLOT_GI, { 0.0f, 0.0f, 0.0f, 0.0f }, { GL_LINEAR, GL_LINEAR }, { GL_RGBA16F, GL_RGBA } } };
		m_gi_fb = Context::createFrameBuffer(ci);
		ci.size = m_gi_size * 2u;
		for (uint32_t i = 0; i < 4; i++) {
			ci.attachments = { { TEXTURE_SLOT_GI_C0 + i, { 0.0f, 0.0f, 0.0f, 1.0f }, { GL_NEAREST, GL_NEAREST }, { GL_RGBA16F, GL_RGBA } } };
			m_gi_casc_fb[i] = Context::createFrameBuffer(ci);
		}
	}

	const auto& p = m_gi_pipeline;
	const glm::ivec2 lo(m_gi_size);
	bindFrameBuffer(m_game_framebuffer, false);
	FrameBuffer::setDrawBuffers(1);
	m_prefx_texture->fillFromBuffer(m_game_framebuffer);

	// Uniforms (and manual texture binds) before bindPipeline: setUniform* binds the program.
	const bool emit = m_emit_attached && m_game_framebuffer->getAttachmentCount() > 5;
	if (emit)
		m_game_framebuffer->getTexture(5)->bind();
	p->setUniform1i("u_HasEmit", emit ? 1 : 0);
	p->setUniform1i("u_EmitTexture", emit ? TEXTURE_SLOT_EMIT : TEXTURE_SLOT_PREFX);
	p->setUniformVec2f("u_SceneSize", glm::vec2(lo));
	p->setUniform1i("u_Upper", TEXTURE_SLOT_GI_SCENE);
	p->setUniform1f("u_Strength", st.gi_strength);
	bindPipeline(p);
	m_game_framebuffer->getTexture(3)->bind();
	bindFrameBuffer(m_gi_scene_fb, false);
	setViewport(lo);
	drawQuad(0);
	m_gi_scene_fb->getTexture()->bind();

	for (int i = 3; i >= 0; i--) {
		if (i < 3)
			m_gi_casc_fb[i + 1]->getTexture()->bind();
		p->setUniform1i("u_Cascade", i);
		p->setUniform1i("u_Upper", i < 3 ? TEXTURE_SLOT_GI_C0 + i + 1 : TEXTURE_SLOT_GI_SCENE);
		bindFrameBuffer(m_gi_casc_fb[i], false);
		setViewport(lo * 2);
		drawQuad(1);
	}

	m_gi_casc_fb[0]->getTexture()->bind();
	p->setUniform1i("u_Upper", TEXTURE_SLOT_GI_C0);
	bindFrameBuffer(m_gi_fb, false);
	setViewport(lo);
	drawQuad(2);

	m_gi_fb->getTexture()->bind();
	bindFrameBuffer(m_game_framebuffer, false);
	FrameBuffer::setDrawBuffers(1);
	setViewport(game_size);
	drawQuad(3);
}

std::pair<GLint, GLenum> Context::sceneFormat()
{
	if (App.hdr.float_mode)
		return { GL_RGBA16F, GL_RGBA };
	return { GL_RGBA8, GL_RGBA };
}

// Float pipeline, end of PreFx (world only, before UI): auto exposure adapt (1x1), then one pass
// that exposes colour and soft-clips its diffuse share (colour - additive share, attachment 5).
void Context::hdrSceneFx(const perf::FrameSettings& st, glm::uvec2 game_size)
{
	const auto& p = m_emit_pipeline;
	const glm::ivec2 gs(game_size);
	bindFrameBuffer(m_game_framebuffer, false);
	FrameBuffer::setDrawBuffers(1);
	m_prefx_texture->fillFromBuffer(m_game_framebuffer);

	const bool ae = App.hdr.auto_exposure && m_expo_supported;
	if (ae) {
		bool reset = false;
		if (!m_expo_fb[0]) {
			FrameBufferCreateInfo ci;
			ci.size = { 1, 1 };
			ci.attachments = { { TEXTURE_SLOT_EXPO, { 0.0f, 0.0f, 0.0f, 1.0f }, { GL_NEAREST, GL_NEAREST }, { GL_RGBA16F, GL_RGBA } } };
			m_expo_fb[0] = Context::createFrameBuffer(ci);
			m_expo_fb[1] = Context::createFrameBuffer(ci);
			reset = true;
		}
		LARGE_INTEGER now, freq;
		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&freq);
		float dt = m_expo_qpc ? (float)((double)(now.QuadPart - m_expo_qpc) / (double)freq.QuadPart) : 0.0f;
		m_expo_qpc = now.QuadPart;
		if (dt <= 0.0f || dt > 0.25f) // first frame, or after a pause/loading: no time jump
			dt = 1.0f / 60.0f;
		if (st.level_no != m_expo_level) { // new area: adapt at once
			m_expo_level = st.level_no;
			reset = true;
		}
		const int prev = m_expo_cur, cur = m_expo_cur ^ 1;
		m_expo_fb[prev]->getTexture()->bind();
		p->setUniform1i("u_Mode", 3);
		p->setUniform1f("u_Key", 0.08f);
		p->setUniform1f("u_Strength", App.hdr.auto_exposure_strength.value);
		p->setUniformVec2f("u_ExpoRange", { App.hdr.auto_exposure_min.value, App.hdr.auto_exposure_max.value });
		p->setUniformVec2f("u_Rate", { 1.0f - std::exp(-dt / 1.5f), 1.0f - std::exp(-dt / 3.0f) }); // darken ~1.5 s, brighten ~3 s
		p->setUniform1i("u_Reset", reset ? 1 : 0);
		bindPipeline(p);
		bindFrameBuffer(m_expo_fb[cur], false);
		setViewport({ 1, 1 });
		drawQuad();
		m_expo_cur = cur;
		m_expo_fb[cur]->getTexture()->bind();
		bindFrameBuffer(m_game_framebuffer, false);
	}

	p->setUniform1i("u_Mode", 1);
	p->setUniform1i("u_ExpoOn", ae ? 1 : 0);
	p->setUniform1f("u_Knee", App.hdr.diffuse_shoulder ? App.hdr.shoulder_knee.value : 1.0e9f);
	bindPipeline(p);
	setViewport(gs);
	drawQuad();
}

// Float pipeline HDR bloom (end of PreFx, before UI): prefilter > 1.0 at half res, dual-filter
// down/up over hdr_bloom_radius RGBA16F levels, added onto the game colour.
void Context::hdrBloom(glm::uvec2 game_size)
{
	const int levels = std::clamp(App.hdr.hdr_bloom_radius.value, 2, 6);
	if (m_hdrbloom_game_size != game_size || (int)m_hdrbloom_fb.size() != levels) {
		m_hdrbloom_game_size = game_size;
		m_hdrbloom_fb.clear();
		glm::uvec2 size = game_size;
		for (int i = 0; i < levels; i++) {
			size = glm::max(size / 2u, glm::uvec2(1));
			FrameBufferCreateInfo ci;
			ci.size = size;
			ci.attachments = { { TEXTURE_SLOT_HDRBLOOM, { 0.0f, 0.0f, 0.0f, 1.0f }, { GL_LINEAR, GL_LINEAR }, { GL_RGBA16F, GL_RGBA } } };
			m_hdrbloom_fb.push_back(Context::createFrameBuffer(ci));
		}
	}
	const auto& p = m_emit_pipeline;
	const auto texel = [](const std::unique_ptr<FrameBuffer>& fb) { return glm::vec2(1.0f / fb->getWidth(), 1.0f / fb->getHeight()); };
	const auto size = [](const std::unique_ptr<FrameBuffer>& fb) { return glm::ivec2(fb->getWidth(), fb->getHeight()); };

	bindFrameBuffer(m_game_framebuffer, false);
	FrameBuffer::setDrawBuffers(1);
	m_prefx_texture->fillFromBuffer(m_game_framebuffer);

	p->setUniform1i("u_Mode", 4);
	p->setUniformVec2f("u_SrcTexel", { 1.0f / game_size.x, 1.0f / game_size.y });
	bindPipeline(p, 0);
	bindFrameBuffer(m_hdrbloom_fb[0], false);
	setViewport(size(m_hdrbloom_fb[0]));
	drawQuad();

	for (int i = 1; i < levels; i++) {
		m_hdrbloom_fb[i - 1]->getTexture()->bind();
		p->setUniform1i("u_Mode", 5);
		p->setUniformVec2f("u_SrcTexel", texel(m_hdrbloom_fb[i - 1]));
		bindPipeline(p, 0);
		bindFrameBuffer(m_hdrbloom_fb[i], false);
		setViewport(size(m_hdrbloom_fb[i]));
		drawQuad();
	}
	for (int i = levels - 2; i >= 0; i--) {
		m_hdrbloom_fb[i + 1]->getTexture()->bind();
		p->setUniform1i("u_Mode", 6);
		p->setUniformVec2f("u_SrcTexel", texel(m_hdrbloom_fb[i + 1]));
		bindPipeline(p, 1);
		bindFrameBuffer(m_hdrbloom_fb[i], false);
		setViewport(size(m_hdrbloom_fb[i]));
		drawQuad();
	}

	m_hdrbloom_fb[0]->getTexture()->bind();
	p->setUniform1i("u_Mode", 7);
	p->setUniform1f("u_Intensity", App.hdr.hdr_bloom_intensity.value);
	p->setUniform1f("u_Levels", (float)levels);
	bindPipeline(p, 1);
	bindFrameBuffer(m_game_framebuffer, false);
	setViewport(glm::ivec2(game_size));
	drawQuad();
}

bool Context::emitGradedWanted() const
{
	return App.hdr.emissive_graded && !App.hdr.float_mode;
}

bool Context::emitWanted() const
{
	// float pipeline: the attachment carries the additive share of the colour for the diffuse shoulder
	return ISGLIDE3X() && m_emit_supported && m_hdr && App.hdr.mode && (App.hdr.float_mode ? (App.hdr.diffuse_shoulder || App.hdr.auto_exposure || App.hdr.hdr_bloom) : App.hdr.emissive);
}

void Context::onShaderChange()
{
	if (m_current_shader != App.shader.selected) {
		if (!Upscaler::Instance().loadPreset())
			Upscaler::Instance().loadDefaultPreset();
	}

	Upscaler::Instance().setupPasses();
	m_current_shader = App.shader.selected;
}

void Context::onStageChange()
{
	if (App.game.screen == GameScreen::Movie)
		return;

	switch (App.game.draw_stage) {
		case DrawStage::World:
			break;
		case DrawStage::UI:
			if (App.hdr.float_mode && ISGLIDE3X()) {
				flushVertices();
				m_command_buffer[m_frame_index].pushCommand(CommandType::UIStage, m_current_blend_index);
			}
			addPanelOccluders();
			modules::HDText::drawFpsCounter();
			if (m_shadow_fx && m_shadow_seen && *d2::screen_shift != SCREENPANEL_BOTH) {
				flushVertices();
				m_command_buffer[m_frame_index].pushCommand(CommandType::SoftShadow, m_current_blend_index);
			}
			if (ISGLIDE3X() && (App.bloom.active || App.lut.selected || App.dynamic_light.active || App.tonemap.active || App.radiance_gi.active || (App.hdr.float_mode && (App.hdr.auto_exposure || App.hdr.hdr_bloom))) && *d2::screen_shift != SCREENPANEL_BOTH) {
				flushVertices();
				m_command_buffer[m_frame_index].pushCommand(CommandType::PreFx, m_current_blend_index);
			}
			if (ISGLIDE3X() && App.shader.world_only) {
				flushVertices();
				m_command_buffer[m_frame_index].pushCommand(CommandType::WorldSnapshot);
			}
			break;
		case DrawStage::Map:
			if (modules::MiniMap::Instance().isActive()) {
				flushVertices();
				m_blend_locked = true;
				m_command_buffer[m_frame_index].pushCommand(CommandType::SetBlendState, 3);
				setVertexFlagW(1 + !*d2::automap_on);
			}
			break;
		case DrawStage::HUD:
			if (modules::MiniMap::Instance().isActive()) {
				flushVertices();
				m_blend_locked = false;
				m_command_buffer[m_frame_index].pushCommand(CommandType::SetBlendState, m_current_blend_index);
				setVertexFlagW(0);

				modules::MiniMap::Instance().draw();
			}
			modules::HDText::Instance().drawEntryText();
			break;
		case DrawStage::CursorItem:
			flushVertices();
			setVertexFlagW(10);
			break;
		case DrawStage::Cursor:
#ifdef _HDTEXT
			modules::HDText::showSampleText();
#endif
			flushVertices();
			setVertexFlagW(10);
			appendDelayedObjects();
			modules::HDCursor::Instance().draw();
			break;
	}
}

void Context::setBlendState(uint32_t index)
{
	flushVertices();
	m_current_blend_index = g_blend_types.at(index).first;
	if (!m_blend_locked)
		m_command_buffer[m_frame_index].pushCommand(CommandType::SetBlendState, m_current_blend_index);
}

void Context::beginFrame()
{
	if (m_perf)
		m_perf_begin_qpc = qpcNow();

	if (!App.wndproc && App.game.screen == GameScreen::Menu)
		App.wndproc = (WNDPROC)SetWindowLongA(App.hwnd, GWL_WNDPROC, (LONG)win32::WndProc);

	m_vertices.count = m_vertices.start = 0;
	m_vertices.ptr = m_vertices.data[m_frame_index].data();

	m_vertices_mod.count = 0;
	m_vertices_mod.ptr = m_vertices_mod.data[m_frame_index].data();

	m_delay_push = false;
	m_vertices_late.count = 0;
	m_vertices_late.ptr = m_vertices_late.data[0].data();

	m_occluders.count = 0;
	memset(m_mod_coverage, 0, sizeof(m_mod_coverage));

	m_frame.vertex_count = 0;
	m_frame.drawcall_count = 0;

	App.game.draw_stage = DrawStage::World;
	m_shadow_fx = ISGLIDE3X() && m_shadow_supported && App.game.screen == GameScreen::InGame && App.soft_shadows.active;
	m_shadow_seen = false;
	m_shadow_draw = 0;
	modules::HDText::Instance().reset();
	modules::MotionPrediction::Instance().update();

	m_command_buffer[m_frame_index].pushCommand(CommandType::Begin);
}

void Context::bindDefaultFrameBuffer()
{
	FrameBuffer::unBind();
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

void Context::presentFrame()
{
	flushVertices();
	setVertexFlagW(0);
	m_command_buffer[m_frame_index].pushCommand(CommandType::Submit);

	modules::HDText::Instance().update();

	if (m_vertices_mod.count) {
		m_command_buffer[m_frame_index].m_vertex_mod_count = m_vertices_mod.count;
		m_command_buffer[m_frame_index].m_occluders = m_occluders;
		m_frame.drawcall_count++;
	}
	option::Menu::instance().check();
	win32::checkCursorLock();

	CommandBuffer& cb = m_command_buffer[m_frame_index];
	captureSettings(cb.m_settings);
	if (m_perf) {
		const int64_t now = qpcNow();
		const double cpu = perf::threadCpuMs(GetCurrentThread());
		auto& g = cb.m_game_timing;
		g.frame = m_perf_frame++;
		g.work_ms = (m_perf_begin_qpc && m_perf_begin_qpc > m_perf_present_qpc) ? qpcMs(now - m_perf_begin_qpc) : -1.0;
		g.frame_ms = m_perf_present_qpc ? qpcMs(now - m_perf_present_qpc) : -1.0;
		g.wait_ms = m_perf_wait_ms;
		g.cpu_ms = (m_perf_cpu_ms >= 0.0 && cpu >= 0.0) ? cpu - m_perf_cpu_ms : -1.0;
		g.inflight = m_inflight.fetch_add(1, std::memory_order_relaxed) + 1;
		m_perf_present_qpc = now;
		m_perf_cpu_ms = cpu;
	}

	// Release: publishes the slot (commands, vertices, snapshot) to the render thread.
	ReleaseSemaphore(m_semaphore_cpu[m_frame_index], 1, NULL);
	m_frame_index = (m_frame_index + 1) % (App.frame_latency + 1);

	if (m_perf) {
		const int64_t t = qpcNow();
		WaitForSingleObject(m_semaphore_gpu[m_frame_index], INFINITE);
		m_perf_wait_ms = qpcMs(qpcNow() - t);
	} else
		WaitForSingleObject(m_semaphore_gpu[m_frame_index], INFINITE);
	m_command_buffer[m_frame_index].reset();

	QueryPerformanceCounter(&m_frame.time);
	double cur_time = (double(m_frame.time.QuadPart) / m_frame.frequency);
	m_frame.frame_time = cur_time - m_frame.prev_time;
	m_frame.prev_time = cur_time;

	m_frame.frame_times.pop_front();
	m_frame.frame_times.push_back(m_frame.frame_time);
	std::deque<double>::iterator iter = m_frame.frame_times.begin() + (MAX_FRAMETIME_SAMPLE_COUNT - m_frame.frame_sample_count);
	m_frame.average_frame_time = std::reduce(iter, m_frame.frame_times.end()) / m_frame.frame_sample_count;
	m_frame.frame_sample_count += m_frame.frame_sample_count == MAX_FRAMETIME_SAMPLE_COUNT ? 0 : 1;
	m_frame.frame_count++;
}

void Context::setViewport(glm::ivec2 size, glm::ivec2 offset)
{
	static glm::ivec4 viewport_metrics = { 0, 0, 0, 0 };
	const auto metrics = glm::ivec4(size, offset);
	if (viewport_metrics == metrics)
		return;

	glViewport(offset.x, offset.y, size.x, size.y);
	viewport_metrics = metrics;
}

void Context::pushVertex(const GlideVertex* vertex, glm::vec2 fix, glm::ivec2 offset)
{
	if (m_vertices.count >= MAX_VERTICES - 4)
		flushVertices();

	m_vertices.ptr->position = {
		glm::detail::toFloat16(vertex->x - (float)offset.x),
		glm::detail::toFloat16(vertex->y - (float)offset.y),
	};
	m_vertices.ptr->tex_coord = {
		((float)((uint32_t)vertex->s >> m_vertex_params.tex_shift) + (float)m_vertex_params.offsets.x) / (512.0f + fix.x),
		((float)((uint32_t)vertex->t >> m_vertex_params.tex_shift) + (float)m_vertex_params.offsets.y) / (512.0f + fix.y),
	};
	m_vertices.ptr->color1 = vertex->pargb;
	m_vertices.ptr->color2 = m_vertex_params.color;
	m_vertices.ptr->tex_ids = m_vertex_params.tex_ids;
	m_vertices.ptr->flags = m_vertex_params.flags;
	if (m_shadow_fx && App.game.draw_stage == DrawStage::World && m_shadow_draw != 2) {
		// High nibble of flag x tags shadow-buffer writes (see glide.glsl).
		const uint32_t blend = m_current_blend_index;
		uint8_t kind = 0;
		if (m_shadow_draw == 1) {
			if (App.soft_shadows.active)
				kind = blend == 1 ? 1 : (blend == 3 ? 4 : 0);
		} else if (m_shadow_seen)
			kind = blend == 0 ? 2 : (blend == 3 ? 5 : 0);
		if (kind == 1 || kind == 4)
			m_shadow_seen = true;
		m_vertices.ptr->flags.x |= (uint8_t)(kind << 4);
	}

	m_vertices.ptr++;
	m_vertices.count++;
	m_frame.vertex_count++;
}

bool Context::pushFlatQuad(glm::vec2 pos, glm::vec2 size, uint32_t color)
{
	if (!ISGLIDE3X())
		return false;

	if (m_vertices.count >= MAX_VERTICES - 4)
		flushVertices();

	// Flat fills need SAlpha_OneMinusSAlpha (glide blend 5 = pipeline index 3). While the
	// blend state is locked it is already bound to 3, so no state switch is needed there.
	const bool switch_blend = !m_blend_locked && m_current_blend_index != 3;
	if (switch_blend) {
		flushVertices();
		m_command_buffer[m_frame_index].pushCommand(CommandType::SetBlendState, 3);
	}

	const glm::vec2 end = pos + size;
	const glm::vec2 corners[4] = { { pos.x, pos.y }, { end.x, pos.y }, { end.x, end.y }, { pos.x, end.y } };

	for (const auto& corner : corners) {
		m_vertices.ptr->position = { glm::detail::toFloat16(corner.x), glm::detail::toFloat16(corner.y) };
		m_vertices.ptr->tex_coord = { 0.0f, 0.0f };
		m_vertices.ptr->color1 = 0xFFFFFFFF;
		m_vertices.ptr->color2 = color;
		m_vertices.ptr->tex_ids = { 0, 0 };
		m_vertices.ptr->flags = { 0, 1, 1, 0 }; // untextured constant color, alpha from color2

		m_vertices.ptr++;
		m_vertices.count++;
		m_frame.vertex_count++;
	}

	if (switch_blend) {
		flushVertices();
		m_command_buffer[m_frame_index].pushCommand(CommandType::SetBlendState, m_current_blend_index);
	}

	return true;
}

// Untextured per-vertex coloured quads (0xAARRGGBB, rgb premultiplied), One_One blend (pipeline 2).
void Context::pushAdditiveQuads(const glm::vec2* pos, const uint32_t* colors, uint32_t quad_count)
{
	if (!ISGLIDE3X() || !pos || !colors || quad_count == 0 || m_blend_locked)
		return;

	flushVertices();
	m_command_buffer[m_frame_index].pushCommand(CommandType::SetBlendState, 2);

	for (uint32_t q = 0; q < quad_count; q++) {
		if (m_vertices.count >= MAX_VERTICES - 4)
			flushVertices();
		for (uint32_t i = q * 4; i < q * 4 + 4; i++) {
			m_vertices.ptr->position = { glm::detail::toFloat16(pos[i].x), glm::detail::toFloat16(pos[i].y) };
			m_vertices.ptr->tex_coord = { 0.0f, 0.0f };
			m_vertices.ptr->color1 = colors[i];
			m_vertices.ptr->color2 = 0xFFFFFFFF;
			m_vertices.ptr->tex_ids = { 0, 0 };
			m_vertices.ptr->flags = { 0, 2, 0, 0 }; // iterated colour, alpha 1
			m_vertices.ptr++;
			m_vertices.count++;
			m_frame.vertex_count++;
		}
	}

	flushVertices();
	m_command_buffer[m_frame_index].pushCommand(CommandType::SetBlendState, m_current_blend_index);
}

void Context::flushVertices()
{
	if (m_vertices.count == 0)
		return;

	m_command_buffer[m_frame_index].drawIndexed(m_vertices.start, m_vertices.count);

	m_vertices.start += m_vertices.count;
	m_vertices.count = 0;
	m_frame.drawcall_count++;
}

void Context::drawQuad(int8_t flag_x, int8_t flag_y, int16_t tex_id)
{
	static Vertex quad[4] = {
		{ { glm::detail::toFloat16(-1.0f), glm::detail::toFloat16(-1.0f) }, { 0.0f, 0.0f } },
		{ { glm::detail::toFloat16(+1.0f), glm::detail::toFloat16(-1.0f) }, { 1.0f, 0.0f } },
		{ { glm::detail::toFloat16(+1.0f), glm::detail::toFloat16(+1.0f) }, { 1.0f, 1.0f } },
		{ { glm::detail::toFloat16(-1.0f), glm::detail::toFloat16(+1.0f) }, { 0.0f, 1.0f } },
	};
	for (size_t i = 0; i < 4; i++) {
		quad[i].tex_ids = { tex_id, 0 };
		quad[i].flags = { flag_x, flag_y, 0, 0 };
	}

	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quad), &quad[0]);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void Context::pushObject(const std::unique_ptr<Object>& object)
{
	const auto vertices = object->getVertices();

	// Delaying a push is about draw order within the module pass, not about z-order against the
	// game pass: a delayed object still belongs under any panel the game draws after it.
	const uint16_t occluder_index = (uint16_t)m_occluders.count;
	markModCoverage(object->getPosition(), object->getSize());

	VertexMod* dst = m_delay_push ? m_vertices_late.ptr : m_vertices_mod.ptr;
	memcpy(dst, vertices, sizeof(VertexMod) * 4);
	for (uint32_t i = 0; i < 4; i++)
		dst[i].tex_ids.y = occluder_index;

	if (m_delay_push) {
		m_vertices_late.ptr += 4;
		m_vertices_late.count += 4;
	} else {
		m_vertices_mod.ptr += 4;
		m_vertices_mod.count += 4;
	}
	m_frame.vertex_count += 4;
}

void Context::addOccluder(glm::vec2 pos, glm::vec2 size, uint32_t color)
{
	const uint8_t alpha = color & 0xFF;
	if (!alpha || m_occluders.count >= MAX_OCCLUDERS)
		return;

	// An occluder only ever attenuates module content pushed before it, so one covering none is
	// dead weight.
	if (!coversModContent(pos, size))
		return;

	const glm::vec2 game_size = App.game.size;
	const glm::vec2 end = pos + size;

	m_occluders.rects[m_occluders.count] = {
		2.0f * pos.x / game_size.x - 1.0f,
		1.0f - 2.0f * pos.y / game_size.y,
		2.0f * end.x / game_size.x - 1.0f,
		1.0f - 2.0f * end.y / game_size.y,
	};
	m_occluders.attenuations[m_occluders.count] = 1.0f - (float)alpha / 255.0f;
	m_occluders.count++;
}

glm::ivec4 Context::coverageCells(glm::vec2 pos, glm::vec2 size)
{
	const glm::vec2 cell = glm::vec2(App.game.size) / (float)MOD_COVERAGE_CELLS;
	const glm::vec2 end = pos + size;

	return {
		glm::clamp((int)glm::floor(pos.x / cell.x), 0, MOD_COVERAGE_CELLS - 1),
		glm::clamp((int)glm::floor(pos.y / cell.y), 0, MOD_COVERAGE_CELLS - 1),
		glm::clamp((int)glm::floor(end.x / cell.x), 0, MOD_COVERAGE_CELLS - 1),
		glm::clamp((int)glm::floor(end.y / cell.y), 0, MOD_COVERAGE_CELLS - 1),
	};
}

void Context::markModCoverage(glm::vec2 pos, glm::vec2 size)
{
	const auto cells = coverageCells(pos, size);
	const uint32_t mask = (uint32_t)(((uint64_t)1 << (cells.z - cells.x + 1)) - 1) << cells.x;

	for (int row = cells.y; row <= cells.w; row++)
		m_mod_coverage[row] |= mask;
}

bool Context::coversModContent(glm::vec2 pos, glm::vec2 size)
{
	const auto cells = coverageCells(pos, size);
	const uint32_t mask = (uint32_t)(((uint64_t)1 << (cells.z - cells.x + 1)) - 1) << cells.x;

	for (int row = cells.y; row <= cells.w; row++) {
		if (m_mod_coverage[row] & mask)
			return true;
	}

	return false;
}

void Context::addPanelOccluders()
{
	if (App.game.screen != GameScreen::InGame || *d2::screen_shift == SCREENPANEL_NONE)
		return;

	// The game's own left/right panels are cell blits that go straight into the game vertex stream
	// during this stage, so nothing registers them the way HDText::drawSolidRect registers the
	// backgrounds it intercepts. Without an occluder, module content pushed during World/Map/HUD -
	// the automap stat lines third-party overlays draw down the screen edge, among others - keeps
	// floating over a panel that natively covers it.
	const glm::vec2 size = { (float)App.game.size.x / 2.0f, (float)App.game.size.y };

	if (*d2::screen_shift & SCREENPANEL_LEFT)
		addOccluder({ 0.0f, 0.0f }, size, 0x000000FF);
	if (*d2::screen_shift & SCREENPANEL_RIGHT)
		addOccluder({ size.x, 0.0f }, size, 0x000000FF);
}

void Context::appendDelayedObjects()
{
	if (m_vertices_late.count == 0)
		return;

	memcpy(m_vertices_mod.ptr, m_vertices_late.data[0].data(), m_vertices_late.count * sizeof(VertexMod));

	m_vertices_mod.count += m_vertices_late.count;
	m_vertices_mod.ptr += m_vertices_late.count;

	m_delay_push = false;
	m_vertices_late.count = 0;
	m_vertices_late.ptr = m_vertices_late.data[0].data();
}

void Context::toggleVsync()
{
	wglSwapIntervalEXT(App.vsync);
	resetFileTime();
}

void Context::setFpsLimit(bool active, int max_fps)
{
	// Game thread only (ctor, Menu::check, WndProc). The render thread picks the new values up from
	// the next frame snapshot and resets its own pacing state (pace_next, due_time) when the
	// generation changes, so this no longer writes render-thread state.
	auto& ls = m_limiter_settings;
	ls.active = active;
	ls.frame_len_ms = 1000.0f / max_fps;
	ls.frame_len_ns = (uint64_t)(ls.frame_len_ms * 10000);
	ls.pace_period = (int64_t)(m_frame.frequency * ls.frame_len_ms);
	ls.generation++;
	m_frame.frame_sample_count = 1;
}

// Game thread, presentFrame(), before the slot is released. Also used once by the render thread for
// a slot that was never presented (shutdown wake-up), when the game thread no longer records.
void Context::captureSettings(perf::FrameSettings& st) const
{
	st.window_w = (int32_t)App.window.size.x;
	st.window_h = (int32_t)App.window.size.y;
	st.vp_w = (int32_t)(App.viewport.stretched.x ? App.window.size.x : App.viewport.size.x);
	st.vp_h = (int32_t)(App.viewport.stretched.y ? App.window.size.y : App.viewport.size.y);
	st.vp_x = App.viewport.stretched.x ? 0 : App.viewport.offset.x;
	st.vp_y = App.viewport.stretched.y ? 0 : App.viewport.offset.y;

	st.sharpen = App.sharpen.active;
	st.sharpen_strength = App.sharpen.strength.value;
	st.sharpen_clamp = App.sharpen.clamp.value;
	st.sharpen_radius = App.sharpen.radius.value;
	st.fxaa = App.fxaa.active;
	st.fxaa_preset = App.fxaa.presets.selected;
	st.bloom = App.bloom.active;
	st.bloom_exposure = App.bloom.exposure.value;
	st.bloom_gamma = App.bloom.gamma.value;
	st.lut = App.lut.selected;
	st.tonemap = App.tonemap.active && !App.hdr.float_mode; // float pipeline: one HDR tonemap in the presenter
	st.add_scale = 1.0f;
	if (App.hdr.float_mode && App.hdr.emissive)
		st.add_scale = std::pow(std::max(App.hdr.emissive_nits.value / std::max(App.hdr.sdr_white.value, 1.0f), 1.0f), 1.0f / 2.2f);
	st.tonemap_exposure = App.tonemap.exposure.value;
	st.world_only = App.shader.world_only;

	st.light = App.dynamic_light.active;
	st.light_debug = App.dynamic_light.debug;
	st.light_vsource = App.dynamic_light.source.selected == 1;
	st.light_intensity = App.dynamic_light.intensity.value;
	st.light_haze = App.dynamic_light.depth_haze ? App.dynamic_light.depth_haze_strength.value : 0.0f;
	st.light_normals = App.dynamic_light.normals;
	st.light_normals_strength = App.dynamic_light.normals_strength.value;
	st.spell_glow = App.dynamic_light.spell_glow.value;

	st.level_no = App.game.screen == GameScreen::InGame ? (int32_t)*d2::level_no : -1;

	st.soft_shadows = App.soft_shadows.active;
	st.gi = App.radiance_gi.active && App.game.screen == GameScreen::InGame;
	st.gi_strength = App.radiance_gi.strength.value;
	st.shadow_softness = App.soft_shadows.active ? App.soft_shadows.softness.value : 1.0f;

	st.limiter = m_limiter_settings;
	st.limiter.pacing = App.foreground_fps.pacing;
	st.valid = true;
}

void Context::resetFileTime()
{
	FILETIME ft = { 0 };
	GetSystemTimeAsFileTime(&ft);
	memcpy(&m_limiter.due_time, &ft, sizeof(LARGE_INTEGER));
}

void Context::takeScreenShot()
{
	uint8_t* data = new GLubyte[App.viewport.size.x * App.viewport.size.y * 3];
	memset(data, 0, App.viewport.size.x * App.viewport.size.y * 3);

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(App.viewport.offset.x, App.viewport.offset.y, App.viewport.size.x, App.viewport.size.y, GL_RGB, GL_UNSIGNED_BYTE, data);

	std::string file_name = helpers::saveScreenShot(data, App.viewport.size.x, App.viewport.size.y);
	delete[] data;
}

void Context::imguiInit()
{
	ImGui::CreateContext();
	ImGui_ImplWin32_Init(App.hwnd);
	ImGui_ImplOpenGL3_Init("#version 150");
}

void Context::imguiDestroy()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void Context::imguiStartFrame()
{
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void Context::imguiRender()
{
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void APIENTRY Context::debugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* data)
{
	const char* source_str;
	const char* severity_str;

	// clang-format off
	switch (type) {
		case GL_DEBUG_TYPE_ERROR: logTrace(C_RED, false, "\nError: "); break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: logTrace(C_YELLOW, false, "\nDeprecated behavior: "); break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: logTrace(C_YELLOW, false, "\nUndefined behavior: "); break;
		case GL_DEBUG_TYPE_PORTABILITY: logTrace(C_BLUE, false, "\nPortability: "); break;
		case GL_DEBUG_TYPE_PERFORMANCE: logTrace(C_BLUE, false, "\nPerformance: "); break;
		case GL_DEBUG_TYPE_MARKER: logTrace(C_MAGENTA, false, "\nMarker: "); break;
		case GL_DEBUG_TYPE_OTHER: logTrace(C_GRAY, false, "\nOther: "); break;
		default: logTrace(C_RED, false, "\nUnknown: ");
	}

	switch (source) {
		case GL_DEBUG_SOURCE_API: source_str = "Api"; break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM: source_str = "Window system"; break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER: source_str = "Shader compiler"; break;
		case GL_DEBUG_SOURCE_THIRD_PARTY: source_str = "Third party"; break;
		case GL_DEBUG_SOURCE_APPLICATION: source_str = "Application"; break;
		case GL_DEBUG_SOURCE_OTHER: source_str = "Other"; break;
		default: source_str = "Unknown";
	}

	switch (severity) {
		case GL_DEBUG_SEVERITY_HIGH: severity_str = "High"; break;
		case GL_DEBUG_SEVERITY_MEDIUM: severity_str = "Medium"; break;
		case GL_DEBUG_SEVERITY_LOW: severity_str = "Low"; break;
		case GL_DEBUG_SEVERITY_NOTIFICATION: severity_str = "Notification"; break;
		default: severity_str = "Unknown";
	}
	// clang-format on

	logTrace(C_WHITE, false, "[%u / %s]: ", id, severity_str);
	logTrace(C_GRAY, true, "%s", source_str);
	trace("%s", message);

	if (App.log)
		logFileWrite(0, "OpenGL: [%u / %s]: %s | %s", id, severity_str, source_str, message);
}

}