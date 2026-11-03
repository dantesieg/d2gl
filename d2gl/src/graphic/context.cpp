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
#include "modules/adaptive_sharpen.h"
#include "modules/portal_shimmer.h"
#include "option/menu.h"
#include "upscaler.h"
#include "win32.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_impl_win32.h>

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

	SetPixelFormat(App.hdc, ChoosePixelFormat(App.hdc, &pfd), &pfd);

	HGLRC context = wglCreateContext(App.hdc);
	wglMakeCurrent(App.hdc, context);

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

		if (m_context = wglCreateContextAttribsARB(App.hdc, 0, attribs)) {
			App.gl_ver = version;
			break;
		}
	}

	if (!m_context) {
		MessageBoxA(App.hwnd, "Requires OpenGL 3.3 or newer!", "Unsupported OpenGL version!", MB_OK | MB_ICONERROR);
		error_log("Requires OpenGL 3.3 or newer! exiting.");
		exit(1);
	}

	wglMakeCurrent(App.hdc, m_context);
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
	m_vlight_supported = App.gl_caps.independent_blending && max_texture_unit > TEXTURE_SLOT_VLIGHT;

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
		for (auto& blend : g_blend_types)
			game_pipeline_ci.attachment_blends.push_back({ blend.second.second, BlendType::SAlpha_OneMinusSAlpha, BlendType::SAlpha_OneMinusSAlpha, BlendType::One_One, blend.second.first == 0 ? BlendType::One_Zero : BlendType::One_One });
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
		m_prefx_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));

		PipelineCreateInfo light_pipeline_ci = { "light" };
		light_pipeline_ci.shader = g_shader_light;
		light_pipeline_ci.bindings = {
			{ BindingType::Texture, "u_Texture", TEXTURE_SLOT_PREFX, &m_prefx_texture },
			{ BindingType::FBTexture, "u_LightTexture1", TEXTURE_SLOT_LIGHT1, &m_light_framebuffer },
			{ BindingType::Texture, "u_LightTexture2", TEXTURE_SLOT_LIGHT2, &m_light_texture },
		};
		if (m_vlight_supported)
			light_pipeline_ci.bindings.push_back({ BindingType::FBTexture, "u_VLightTexture", TEXTURE_SLOT_VLIGHT, &m_game_framebuffer, 4 });
		m_light_pipeline = Context::createPipeline(light_pipeline_ci);
		m_light_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));

		light_pipeline_ci.name = "lightfx";
		light_pipeline_ci.shader = g_shader_lightfx;
		m_lightfx_pipeline = Context::createPipeline(light_pipeline_ci);
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

		PipelineCreateInfo worldfx_pipeline_ci = { "worldfx" };
		worldfx_pipeline_ci.shader = g_shader_worldfx;
		worldfx_pipeline_ci.bindings = {
			{ BindingType::Texture, "u_Texture", TEXTURE_SLOT_PREFX, &m_prefx_texture },
			{ BindingType::FBTexture, "u_MaskTexture", TEXTURE_SLOT_MASK, &m_game_framebuffer, 2 },
		};
		m_worldfx_pipeline = Context::createPipeline(worldfx_pipeline_ci);
		m_worldfx_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));

		PipelineCreateInfo shimmer_pipeline_ci = { "shimmer" };
		shimmer_pipeline_ci.shader = g_shader_shimmer;
		shimmer_pipeline_ci.bindings = { { BindingType::Texture, "u_Texture", TEXTURE_SLOT_PREFX, &m_prefx_texture } };
		m_shimmer_pipeline = Context::createPipeline(shimmer_pipeline_ci);
		m_shimmer_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));

		PipelineCreateInfo uilayer_pipeline_ci = { "uilayer" };
		uilayer_pipeline_ci.shader = g_shader_uilayer;
		uilayer_pipeline_ci.bindings = {
			{ BindingType::FBTexture, "u_FrameTexture", TEXTURE_SLOT_GAME, &m_game_framebuffer, 0 },
			{ BindingType::FBTexture, "u_WorldTexture", TEXTURE_SLOT_WORLD, &m_world_framebuffer, 0 },
		};
		m_uilayer_pipeline = Context::createPipeline(uilayer_pipeline_ci);
		m_uilayer_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));
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
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Context::renderThread, reinterpret_cast<void*>(this), 0, NULL);
}

Context::~Context()
{
	m_rendering = false;
	for (uint32_t i = 0; i < MAX_FRAME_LATENCY; i++)
		ReleaseSemaphore(m_semaphore_cpu[i], 1, NULL);

	for (uint32_t i = 0; i < MAX_FRAME_LATENCY; i++)
		WaitForSingleObject(m_semaphore_gpu[i], INFINITE);

	wglMakeCurrent(App.hdc, m_context);
	imguiDestroy();

	glDeleteBuffers(1, &m_pixel_buffer);
	glDeleteBuffers(1, &m_vertex_buffer);
	glDeleteBuffers(1, &m_index_buffer);
	glDeleteVertexArrays(1, &m_vertex_array);

	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(m_context);
}

void Context::renderThread(void* context)
{
	Context* ctx = reinterpret_cast<Context*>(context);
	wglMakeCurrent(App.hdc, ctx->m_context);
	uint32_t frame_index = 0;

	glBindBuffer(GL_ARRAY_BUFFER, ctx->m_vertex_buffer);
	Vertex::enableAttribArray();

	while (ctx->m_rendering) {
		WaitForSingleObject(ctx->m_semaphore_cpu[frame_index], INFINITE);
		const auto cmd = &ctx->m_command_buffer[frame_index];

		if (cmd->m_resized)
			ctx->onResize(cmd->m_window_size, cmd->m_game_size, cmd->m_game_tex_bpp);

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
		const glm::ivec2 vp_size = { App.viewport.stretched.x ? App.window.size.x : App.viewport.size.x, App.viewport.stretched.y ? App.window.size.y : App.viewport.size.y };
		const glm::ivec2 vp_offset = { App.viewport.stretched.x ? 0 : App.viewport.offset.x, App.viewport.stretched.y ? 0 : App.viewport.offset.y };

		for (uint32_t i = 0; i < cmd->m_count; i++) {
			const auto command = &cmd->m_commands[i];

			switch (command->type) {
				case CommandType::UBOUpdate: {
					const auto data = &cmd->m_ubo_update_queue.data[command->index];
					ctx->m_game_color_ubo->updateData(data->type == UBOType::Gamma ? "gamma" : "palette", data->value);
				} break;
				case CommandType::SetBlendState:
					ctx->bindPipeline(ctx->m_game_pipeline, command->index);
					ctx->m_game_pipeline->setUniform1i("u_BlendIndex", command->index);
					break;
				case CommandType::DrawIndexed:
					if (command->draw.count > 0)
						glDrawElementsBaseVertex(GL_TRIANGLES, command->draw.count, GL_UNSIGNED_INT, 0, command->draw.start);
					break;
				case CommandType::PreFx:
					if (App.deband.active || App.water.active) {
						ctx->setViewport(cmd->m_game_size);
						FrameBuffer::setDrawBuffers(1);
						if (App.deband.active) {
							ctx->m_prefx_texture->fillFromBuffer(ctx->m_game_framebuffer);
							ctx->bindPipeline(ctx->m_worldfx_pipeline);
							ctx->m_worldfx_pipeline->setUniform1f("u_Strength", App.deband.strength.value);
							ctx->drawQuad(0);
						}
						if (App.water.active) {
							ctx->m_prefx_texture->fillFromBuffer(ctx->m_game_framebuffer);
							ctx->bindPipeline(ctx->m_worldfx_pipeline);
							ctx->m_worldfx_pipeline->setUniform1f("u_Strength", App.water.strength.value);
							ctx->m_worldfx_pipeline->setUniform1f("u_Time", (float)(GetTickCount() % 600000u) * 0.001f);
							ctx->drawQuad(1);
						}
					}
					if (App.dynamic_light.active && App.dynamic_light.debug && ctx->gameDrawBufferCount() == 5) {
						// Debug: raw game light buffer in place of the world (ambient swatch top-left).
						FrameBuffer::setDrawBuffers(1);
						ctx->bindPipeline(ctx->m_light_pipeline);
						ctx->drawQuad(4);
					} else if (App.dynamic_light.active && ctx->m_light_tex_size.x > 0 && ctx->m_light_tex_size.y > 0) {
						const int vsource = ctx->gameDrawBufferCount() == 5 ? 1 : 0;
						FrameBuffer::setDrawBuffers(1);
						ctx->m_prefx_texture->fillFromBuffer(ctx->m_game_framebuffer);
						ctx->bindPipeline(ctx->m_light_pipeline);
						ctx->m_light_pipeline->setUniformVec2f("u_RelSize", { 1.0f / ctx->m_light_tex_size.x, 1.0f / ctx->m_light_tex_size.y });
						ctx->m_light_pipeline->setUniform1f("u_Intensity", App.dynamic_light.intensity.value);
						ctx->m_light_pipeline->setUniform1i("u_Source", vsource);
						ctx->bindFrameBuffer(ctx->m_light_framebuffer, false);
						ctx->setViewport(ctx->m_light_tex_size);
						ctx->drawQuad(0);
						for (int i = 0; i < 2; i++) {
							ctx->m_light_texture->fillFromBuffer(ctx->m_light_framebuffer);
							ctx->drawQuad(1);
							ctx->m_light_texture->fillFromBuffer(ctx->m_light_framebuffer);
							ctx->drawQuad(2);
						}
						const bool lightfx = App.dynamic_light.normals || App.dynamic_light.shafts;
						if (lightfx) {
							// lightfx.glsl: blurred light -> LIGHT2, shafts -> LIGHT1, then combine.
							ctx->m_light_texture->fillFromBuffer(ctx->m_light_framebuffer);
							ctx->bindPipeline(ctx->m_lightfx_pipeline);
							ctx->m_lightfx_pipeline->setUniformVec2f("u_RelSize", { 1.0f / ctx->m_light_tex_size.x, 1.0f / ctx->m_light_tex_size.y });
							ctx->m_lightfx_pipeline->setUniform1f("u_Intensity", App.dynamic_light.intensity.value);
							ctx->m_lightfx_pipeline->setUniform1i("u_Source", vsource);
							ctx->m_lightfx_pipeline->setUniform1f("u_Normals", App.dynamic_light.normals ? App.dynamic_light.normals_strength.value : 0.0f);
							ctx->m_lightfx_pipeline->setUniform1f("u_Shafts", App.dynamic_light.shafts ? App.dynamic_light.shafts_strength.value : 0.0f);
							if (App.dynamic_light.shafts)
								ctx->drawQuad(0);
						}
						ctx->bindFrameBuffer(ctx->m_game_framebuffer, false);
						ctx->setViewport(cmd->m_game_size);
						ctx->bindPipeline(lightfx ? ctx->m_lightfx_pipeline : ctx->m_light_pipeline);
						FrameBuffer::setDrawBuffers(1);
						ctx->drawQuad(lightfx ? 1 : 3);
					}
					if (App.portal_shimmer) {
						const auto& shimmer = modules::PortalShimmer::Instance().get(frame_index);
						if (shimmer.count) {
							const double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
							ctx->m_prefx_texture->fillFromBuffer(ctx->m_game_framebuffer);
							ctx->bindPipeline(ctx->m_shimmer_pipeline);
							ctx->m_shimmer_pipeline->setUniformVec4fArray("u_Sources", shimmer.data, shimmer.count);
							ctx->m_shimmer_pipeline->setUniform1i("u_Count", (int)shimmer.count);
							ctx->m_shimmer_pipeline->setUniformVec2f("u_Size", glm::vec2(cmd->m_game_size));
							ctx->m_shimmer_pipeline->setUniform1f("u_Time", (float)fmod(t, 3600.0));
							ctx->bindFrameBuffer(ctx->m_game_framebuffer, false);
							ctx->setViewport(cmd->m_game_size);
							FrameBuffer::setDrawBuffers(1);
							ctx->drawQuad();
						}
					}
					if (App.bloom.active || App.lut.selected) {
					ctx->m_prefx_texture->fillFromBuffer(ctx->m_game_framebuffer);
					ctx->bindPipeline(ctx->m_prefx_pipeline);

					if (App.bloom.active) {
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
					ctx->drawQuad(3 + App.bloom.active, 0, App.lut.selected);
					}

					if (App.atmos.heat_haze || App.atmos.fog || App.tonemap.active) {
						FrameBuffer::setDrawBuffers(1);
						ctx->bindPipeline(ctx->m_atmos_pipeline);
						ctx->m_atmos_pipeline->setUniformVec2f("u_RelSize", { 1.0f / cmd->m_game_size.x, 1.0f / cmd->m_game_size.y });
						if (App.atmos.heat_haze || App.atmos.fog) {
							static const auto t0 = std::chrono::steady_clock::now();
							const float t = std::fmod(std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count(), 3600.0f);
							const int level = App.game.screen == GameScreen::InGame ? *d2::level_no : 0;
							static const glm::vec3 tints[5] = { { 0.70f, 0.74f, 0.80f }, { 0.86f, 0.76f, 0.58f }, { 0.58f, 0.72f, 0.56f }, { 0.78f, 0.34f, 0.26f }, { 0.82f, 0.88f, 0.96f } };
							const int act = level >= 109 ? 4 : level >= 103 ? 3 : level >= 75 ? 2 : level >= 40 ? 1 : 0;
							ctx->m_prefx_texture->fillFromBuffer(ctx->m_game_framebuffer);
							ctx->m_atmos_pipeline->setUniform1f("u_Time", t);
							ctx->m_atmos_pipeline->setUniform1f("u_Haze", App.atmos.heat_haze ? 1.0f : 0.0f);
							ctx->m_atmos_pipeline->setUniform1f("u_Fog", App.atmos.fog ? App.atmos.fog_density.value : 0.0f);
							ctx->m_atmos_pipeline->setUniformVec4f("u_FogTint", glm::vec4(tints[act], 1.0f));
							ctx->drawQuad(0);
						}
						if (App.tonemap.active) {
							ctx->m_prefx_texture->fillFromBuffer(ctx->m_game_framebuffer);
							ctx->m_atmos_pipeline->setUniform1f("u_Exposure", App.tonemap.exposure.value);
							ctx->drawQuad(1);
						}
					}

					ctx->bindPipeline(ctx->m_game_pipeline, command->index);
					FrameBuffer::setDrawBuffers(ctx->gameDrawBufferCount());
					break;
				case CommandType::SoftShadow:
					if (ctx->m_shadow_tex_size.x > 0 && ctx->m_shadow_tex_size.y > 0) {
						ctx->bindPipeline(ctx->m_shadow_pipeline);
						ctx->m_shadow_pipeline->setUniformVec2f("u_RelSize", { 1.0f / ctx->m_shadow_tex_size.x, 1.0f / ctx->m_shadow_tex_size.y });
						ctx->m_shadow_pipeline->setUniform1f("u_Softness", App.soft_shadows.active ? App.soft_shadows.softness.value : 1.0f);
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
						FrameBuffer::setDrawBuffers(ctx->gameDrawBufferCount());
					}
					break;
				case CommandType::WorldSnapshot:
					ctx->m_world_framebuffer->getTexture()->fillFromBuffer(ctx->m_game_framebuffer);
					ctx->bindFrameBuffer(ctx->m_game_framebuffer, false);
					FrameBuffer::setDrawBuffers(ctx->gameDrawBufferCount());
					ctx->m_world_ready = true;
					break;
				case CommandType::Begin:
					ctx->m_world_ready = false;
					if (cmd->m_screen == GameScreen::Movie) {
						ctx->bindDefaultFrameBuffer();
						ctx->setViewport(App.window.size);
					} else {
						if (ISGLIDE3X()) {
							// Set draw buffers first so the clear also covers the light attachment.
							ctx->bindFrameBuffer(ctx->m_game_framebuffer, false);
							FrameBuffer::setDrawBuffers(ctx->gameDrawBufferCount());
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
						if (App.sharpen.active) {
							const float sharpen_factor = App.sharpen.adaptive ? modules::AdaptiveSharpen::Instance().getFactor() : 1.0f;
							const auto sharpen_data = glm::vec3(App.sharpen.strength.value * sharpen_factor, App.sharpen.clamp.value, App.sharpen.radius.value);
							if (ctx->m_sharpen_data != sharpen_data) {
								ctx->m_postfx_ubo->updateDataVec4f("sharpen", glm::vec4(sharpen_data, 1.0f));
								ctx->m_sharpen_data = sharpen_data;
							}
						}

						if (ISGLIDE3X()) {
							if (App.bloom.active) {
								const auto bloom_data = glm::vec2(App.bloom.exposure.value, App.bloom.gamma.value);
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
						const bool ui_layer = ISGLIDE3X() && App.shader.world_only && ctx->m_world_ready;
						const auto& upscale_in = ui_layer ? ctx->m_world_framebuffer : ctx->m_game_framebuffer;
						if (App.sharpen.active || App.fxaa.active)
							Upscaler::Instance().process(upscale_in, vp_size, vp_offset, ctx->m_postfx_framebuffer);
						else
							Upscaler::Instance().process(upscale_in, vp_size, vp_offset);

						if (App.sharpen.active) {
							if (App.fxaa.active)
								ctx->m_postfx_texture->fillFromBuffer(ctx->m_postfx_framebuffer);
							else {
								ctx->bindDefaultFrameBuffer();
								ctx->setViewport(vp_size, vp_offset);
							}
							ctx->bindPipeline(ctx->m_postfx_pipeline);
							ctx->drawQuad(App.fxaa.active);
						}

						if (App.fxaa.active) {
							if (App.gl_caps.compute_shader) {
								ctx->m_postfx_texture->fillFromBuffer(ctx->m_postfx_framebuffer);
								ctx->m_fxaa_compute_pipeline->dispatchCompute(App.fxaa.presets.selected, ctx->m_fxaa_work_size, GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
							}
							ctx->bindDefaultFrameBuffer();
							ctx->setViewport(vp_size, vp_offset);
							ctx->bindPipeline(ctx->m_postfx_pipeline);
							ctx->drawQuad(2 + App.gl_caps.compute_shader, App.fxaa.presets.selected);
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

		GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		glFlush();
		glClientWaitSync(sync, 0, GL_TIMEOUT_IGNORED);
		glDeleteSync(sync);

		ReleaseSemaphore(ctx->m_semaphore_gpu[frame_index], 1, NULL);
		option::Menu::instance().draw();
		SwapBuffers(App.hdc);

		if (ctx->m_limiter.active) {
			WaitForSingleObject(ctx->m_limiter.timer, (DWORD)ctx->m_limiter.frame_len_ms + 1);
			ctx->m_limiter.due_time.QuadPart += ctx->m_limiter.frame_len_ns;
			SetWaitableTimer(ctx->m_limiter.timer, &ctx->m_limiter.due_time, 0, NULL, NULL, FALSE);
		}

		frame_index = (frame_index + 1) % (App.frame_latency + 1);
	}

	wglMakeCurrent(NULL, NULL);
	for (uint32_t i = 0; i < 2; i++)
		ReleaseSemaphore(ctx->m_semaphore_gpu[i], 1, NULL);
}

void Context::onResize(glm::uvec2 w_size, glm::uvec2 g_size, uint32_t bpp)
{
	static glm::uvec2 game_size = { 0, 0 };
	static uint32_t color_bpp = 8;
	bool game_resized = game_size != g_size || color_bpp != bpp;
	game_size = g_size;
	color_bpp = bpp;

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
				{ TEXTURE_SLOT_GAME, { 0.0f, 0.0f, 0.0f, 1.0f }, { GL_LINEAR, GL_LINEAR } },
				{ TEXTURE_SLOT_MAP, { 0.0f, 0.0f, 0.0f, 0.0f } },
				{ TEXTURE_SLOT_MASK, { 0.0f, 0.0f, 0.0f, 0.0f }, { GL_LINEAR, GL_LINEAR }, { GL_R8, GL_RED } },
				{ TEXTURE_SLOT_SHADOW, { 0.0f, 0.0f, 0.0f, 0.0f }, { GL_LINEAR, GL_LINEAR } },
			};
		else
			frambuffer_ci.attachments = { { TEXTURE_SLOT_GAME } };
		if (ISGLIDE3X() && m_vlight_supported) {
			// Game vertex lighting (GlideVertex pargb) of world draws; alpha = coverage.
			frambuffer_ci.attachments.push_back({ TEXTURE_SLOT_VLIGHT, { 0.0f, 0.0f, 0.0f, 0.0f }, { GL_LINEAR, GL_LINEAR } });
		}
		m_game_framebuffer = Context::createFrameBuffer(frambuffer_ci);

		if (ISGLIDE3X()) {
			m_game_pipeline->setUniformMat4f("u_MVP", mvp);

			m_bloom_ubo->updateDataVec2f("rel_size", { 4.0f / game_size.x, 4.0f / game_size.y });
			m_bloom_tex_size = { game_size.x / 4, game_size.y / 4 };
			m_bloom_work_size = { ceil((float)m_bloom_tex_size.x / 16), ceil((float)m_bloom_tex_size.y / 16) };

			FrameBufferCreateInfo bloom_frambuffer_ci;
			bloom_frambuffer_ci.size = m_bloom_tex_size;
			bloom_frambuffer_ci.attachments = { { TEXTURE_SLOT_BLOOM1, {}, { GL_LINEAR, GL_LINEAR } } };
			m_bloom_framebuffer = Context::createFrameBuffer(bloom_frambuffer_ci);
			if (App.gl_caps.compute_shader)
				m_bloom_framebuffer->getTexture()->bindImage(IMAGE_UNIT_BLUR);

			TextureCreateInfo bloom_texture_ci;
			bloom_texture_ci.size = m_bloom_tex_size;
			bloom_texture_ci.slot = TEXTURE_SLOT_BLOOM2;
			bloom_texture_ci.filter = { GL_LINEAR, GL_LINEAR };
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
			m_prefx_texture = Context::createTexture(prefx_texture_ci);

			FrameBufferCreateInfo world_frambuffer_ci;
			world_frambuffer_ci.size = game_size;
			world_frambuffer_ci.attachments = { { TEXTURE_SLOT_WORLD, { 0.0f, 0.0f, 0.0f, 1.0f }, { GL_LINEAR, GL_LINEAR } } };
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
		frambuffer_ci.attachments = { { TEXTURE_SLOT_POSTFX1, {}, { GL_LINEAR, GL_LINEAR } } };
		m_postfx_framebuffer = Context::createFrameBuffer(frambuffer_ci);
		if (App.gl_caps.compute_shader)
			m_postfx_framebuffer->getTexture()->bindImage(IMAGE_UNIT_FXAA);

		m_fxaa_work_size = { ceil((float)App.viewport.size.x / 16), ceil((float)App.viewport.size.y / 16) };

		TextureCreateInfo texture_ci;
		texture_ci.size = App.viewport.size;
		texture_ci.slot = TEXTURE_SLOT_POSTFX2;
		texture_ci.filter = { GL_LINEAR, GL_LINEAR };
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
uint32_t Context::gameDrawBufferCount() const
{
	const uint32_t count = m_game_framebuffer->getAttachmentCount();
	// 0 game, 1 map, 2 mask, 3 shadow (always drawn so it is cleared every frame), 4 vlight (only while used)
	if (count < 5)
		return count;
	const bool vlight = App.dynamic_light.active && (App.dynamic_light.source.selected == 1 || App.dynamic_light.debug);
	return vlight ? 5 : 4;
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
			addPanelOccluders();
			modules::HDText::drawFpsCounter();
			if (m_shadow_fx && m_shadow_seen && *d2::screen_shift != SCREENPANEL_BOTH) {
				flushVertices();
				m_command_buffer[m_frame_index].pushCommand(CommandType::SoftShadow, effectiveBlendIndex());
			}
			modules::PortalShimmer::Instance().commit(m_frame_index);
			if (ISGLIDE3X() && (App.bloom.active || App.lut.selected || App.dynamic_light.active || App.deband.active || App.water.active || App.atmos.heat_haze || App.atmos.fog || App.tonemap.active || App.portal_shimmer) && *d2::screen_shift != SCREENPANEL_BOTH) {
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
		m_command_buffer[m_frame_index].pushCommand(CommandType::SetBlendState, effectiveBlendIndex());
}

// Animation smoothing: draw opaque (One_Zero) quads alpha-blended at a fixed alpha.
void Context::setAlphaOverride(uint8_t alpha)
{
	if (alpha == m_alpha_override)
		return;
	const uint32_t old_blend = effectiveBlendIndex();
	flushVertices();
	m_alpha_override = alpha;
	const uint32_t new_blend = effectiveBlendIndex();
	if (!m_blend_locked && new_blend != old_blend)
		m_command_buffer[m_frame_index].pushCommand(CommandType::SetBlendState, new_blend);
}

void Context::beginFrame()
{
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
	m_shadow_fx = ISGLIDE3X() && m_shadow_supported && App.game.screen == GameScreen::InGame && (App.soft_shadows.active || App.ambient_occlusion.active);
	m_shadow_seen = false;
	m_shadow_draw = 0;
	modules::HDText::Instance().reset();
	modules::MotionPrediction::Instance().update();
	modules::AdaptiveSharpen::Instance().update();

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

	ReleaseSemaphore(m_semaphore_cpu[m_frame_index], 1, NULL);
	m_frame_index = (m_frame_index + 1) % (App.frame_latency + 1);

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
	if (m_alpha_override < 255 && m_current_blend_index == 0) {
		m_vertices.ptr->color1 = (vertex->pargb & 0x00FFFFFF) | ((uint32_t)m_alpha_override << 24);
		m_vertices.ptr->flags.z = 2;
	}
	if (m_shadow_fx && App.game.draw_stage == DrawStage::World && m_shadow_draw != 2) {
		// High nibble of flag x tags shadow-buffer writes (see glide.glsl).
		const uint32_t blend = effectiveBlendIndex();
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
	m_command_buffer[m_frame_index].pushCommand(CommandType::SetBlendState, effectiveBlendIndex());
}

void Context::pushAOEllipse(glm::vec2 center, glm::vec2 radius, float strength)
{
	if (!m_shadow_fx || App.game.draw_stage != DrawStage::World)
		return;

	if (m_vertices.count >= MAX_VERTICES - 4)
		flushVertices();

	// AO quads keep the frame untouched (alpha 0) and only add darkness to attachment 3.
	const bool switch_blend = !m_blend_locked && effectiveBlendIndex() != 3;
	if (switch_blend) {
		flushVertices();
		m_command_buffer[m_frame_index].pushCommand(CommandType::SetBlendState, 3);
	}

	const glm::vec2 corners[4] = { center - radius, { center.x + radius.x, center.y - radius.y }, center + radius, { center.x - radius.x, center.y + radius.y } };
	const glm::vec2 uvs[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
	const uint32_t color = (uint32_t)(glm::clamp(strength, 0.0f, 1.0f) * 255.0f) | 0xFFFFFF00;

	for (int i = 0; i < 4; i++) {
		m_vertices.ptr->position = { glm::detail::toFloat16(corners[i].x), glm::detail::toFloat16(corners[i].y) };
		m_vertices.ptr->tex_coord = uvs[i];
		m_vertices.ptr->color1 = 0xFFFFFFFF;
		m_vertices.ptr->color2 = color;
		m_vertices.ptr->tex_ids = { 0, 0 };
		m_vertices.ptr->flags = { (uint8_t)(3 << 4), 1, 1, 0 };

		m_vertices.ptr++;
		m_vertices.count++;
		m_frame.vertex_count++;
	}
	m_shadow_seen = true;

	if (switch_blend) {
		flushVertices();
		m_command_buffer[m_frame_index].pushCommand(CommandType::SetBlendState, effectiveBlendIndex());
	}
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
	m_limiter.active = active;
	m_limiter.frame_len_ms = 1000.0f / max_fps;
	m_limiter.frame_len_ns = (uint64_t)(m_limiter.frame_len_ms * 10000);
	m_frame.frame_sample_count = 1;

	resetFileTime();
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