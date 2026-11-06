// GL test scene: rendered as an SDR(-extended) gamma-encoded signal into an RGBA16F target,
// exactly like D2GL's final image would be (values > 1.0 only in the highlight/emissive demo areas).
#pragma once
#include "gl_loader.h"
#include <cstdint>

struct Scene {
	GLuint prog = 0, blit_prog = 0, panel_prog = 0, vao = 0, panel_tex = 0;
	int panel_w = 0, panel_h = 0;
	GLint u_res = -1, u_time = -1, u_hmax = -1, u_flip = -1, b_src = -1, p_rect = -1, p_tex = -1, p_res = -1, p_flip = -1;
	bool init();
	void shutdown();
	void set_panel(const uint32_t* rgba, int w, int h);
	// Draws scene + panel into the currently bound FBO. flip_y: render upside-down so that a D3D
	// consumer (row 0 = top) sees the image upright without an extra copy.
	void render(int w, int h, float time, float highlight_max_rel, bool flip_y);
	// SDR WGL fallback: draw src texture into the default framebuffer, clamped to [0,1] (pure SDR signal).
	void blit_sdr(GLuint src_tex, int w, int h);
};

bool gl_compile_program(const char* vs, const char* fs, GLuint& out, const char* name);
