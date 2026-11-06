#include "scene.h"
#include "log.h"

static const char* kVS = R"(#version 330 core
out vec2 v_uv;
void main() {
	vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
	v_uv = p;
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

// uv.y = 0 at TOP of the image in all layout code below.
static const char* kSceneFS = R"(#version 330 core
in vec2 v_uv;
out vec4 o;
uniform vec2 u_res;
uniform float u_time;
uniform float u_hmax;   // highlight ramp top, linear relative to SDR white
uniform int u_flip;

float oetf(float l) { // extended sRGB encode (power segment continues above 1)
	float a = abs(l);
	float v = a <= 0.0031308 ? a * 12.92 : 1.055 * pow(a, 1.0 / 2.4) - 0.055;
	return sign(l) * v;
}
vec3 oetf3(vec3 l) { return vec3(oetf(l.r), oetf(l.g), oetf(l.b)); }
float eotf(float v) { return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }

const vec3 kChecker[24] = vec3[24](
	vec3(115,82,68), vec3(194,150,130), vec3(98,122,157), vec3(87,108,67), vec3(133,128,177), vec3(103,189,170),
	vec3(214,126,44), vec3(80,91,166), vec3(193,90,99), vec3(94,60,108), vec3(157,188,64), vec3(224,163,46),
	vec3(56,61,150), vec3(70,148,73), vec3(175,54,60), vec3(231,199,31), vec3(187,86,149), vec3(8,133,161),
	vec3(243,243,242), vec3(200,200,200), vec3(160,160,160), vec3(122,122,121), vec3(85,85,85), vec3(52,52,52));

void main() {
	vec2 uv = v_uv;
	if (u_flip == 0) uv.y = 1.0 - uv.y;  // GL default: bottom-up; make uv.y top-down
	vec2 px = uv * u_res;
	vec3 c = vec3(0.18);                 // mid-grey surround (SDR signal)
	// Row A: 24 SDR reference patches (ColorChecker sRGB approximations), 12x2
	if (uv.y < 0.20) {
		float col = floor(uv.x * 12.0), row = floor(uv.y / 0.10);
		vec2 f = fract(vec2(uv.x * 12.0, uv.y / 0.10));
		int idx = int(row) * 12 + int(col);
		c = (f.x > 0.06 && f.x < 0.94 && f.y > 0.08 && f.y < 0.92) ? kChecker[idx] / 255.0 : vec3(0.0);
	}
	// Row B: grayscale smooth gradient (top half) and 32 steps (bottom half), 0..1 SDR signal
	else if (uv.y < 0.30) {
		float g = uv.y < 0.25 ? uv.x : floor(uv.x * 32.0) / 31.0;
		c = vec3(g);
	}
	// Row C: near-black: 0..0.10 signal smooth + 16 steps (codes 0..15 of 8-bit)
	else if (uv.y < 0.38) {
		float g = uv.y < 0.34 ? uv.x * 0.10 : floor(uv.x * 16.0) / 255.0;
		c = vec3(g);
	}
	// Row D: highlight ramps above 1.0 (needs RGBA16F): linear 0..u_hmax x SDR white; W, R, G, B
	else if (uv.y < 0.54) {
		int band = int((uv.y - 0.38) / 0.04);
		float l = uv.x * u_hmax;
		vec3 tint = band == 0 ? vec3(1) : band == 1 ? vec3(1,0,0) : band == 2 ? vec3(0,1,0) : vec3(0,0,1);
		// tick marks every 1.0 (SDR white multiples)
		float tick = fract(l) < 2.0 * u_hmax / u_res.x ? 0.0 : 1.0; // 2 px tick
		c = oetf3(tint * l) * tick;
	}
	// Row E: emissive + analytic bloom on dark background
	else {
		vec3 lin = vec3(0.005);
		float s = u_res.y * 0.05;
		for (int i = 0; i < 6; ++i) {
			float e = exp2(float(i));                                   // 1,2,4,8,16,32 x SDR white
			vec2 ctr = vec2((0.40 + 0.10 * float(i)) * u_res.x, (0.70 + 0.05 * sin(u_time + float(i))) * u_res.y);
			float d = length(px - ctr);
			vec3 tint = i % 3 == 0 ? vec3(1.0, 0.55, 0.2) : i % 3 == 1 ? vec3(0.3, 0.6, 1.0) : vec3(1.0);
			lin += tint * e * (d < s * 0.35 ? 1.0 : 0.0);                       // emissive core
			lin += tint * e * 0.08 * exp(-(d * d) / (2.0 * s * s));             // bloom halo
		}
		// SDR-reference diffuse white card (signal 1.0) for comparison
		if (px.x > u_res.x * 0.38 && px.x < u_res.x * 0.95 && uv.y > 0.90 && uv.y < 0.96) lin = vec3(1.0);
		c = oetf3(lin);
	}
	o = vec4(c, 1.0);
})";

static const char* kPanelFS = R"(#version 330 core
in vec2 v_uv;
out vec4 o;
uniform sampler2D u_tex;
uniform vec4 u_rect; // x, y, w, h in pixels from top-left
uniform vec2 u_res;
uniform int u_flip;
void main() {
	vec2 frag = gl_FragCoord.xy;
	if (u_flip == 0) frag.y = u_res.y - frag.y;
	vec2 t = (frag - u_rect.xy) / u_rect.zw;
	if (any(lessThan(t, vec2(0))) || any(greaterThan(t, vec2(1)))) discard;
	o = texture(u_tex, t); // straight alpha, gamma-space blend like D2GL
})";

static const char* kBlitFS = R"(#version 330 core
in vec2 v_uv;
out vec4 o;
uniform sampler2D u_src;
void main() { o = vec4(clamp(texture(u_src, v_uv).rgb, 0.0, 1.0), 1.0); })";

static GLuint compile(GLenum type, const char* src, const char* name) {
	GLuint s = p_glCreateShader(type);
	p_glShaderSource(s, 1, &src, nullptr);
	p_glCompileShader(s);
	GLint ok = 0;
	p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char buf[2048] = {};
		p_glGetShaderInfoLog(s, sizeof(buf) - 1, nullptr, buf);
		logf("glsl: %s compile FAILED: %s", name, buf);
		p_glDeleteShader(s);
		return 0;
	}
	return s;
}

bool gl_compile_program(const char* vs, const char* fs, GLuint& out, const char* name) {
	GLuint v = compile(GL_VERTEX_SHADER, vs, name), f = compile(GL_FRAGMENT_SHADER, fs, name);
	if (!v || !f) return false;
	out = p_glCreateProgram();
	p_glAttachShader(out, v);
	p_glAttachShader(out, f);
	p_glLinkProgram(out);
	p_glDeleteShader(v);
	p_glDeleteShader(f);
	GLint ok = 0;
	p_glGetProgramiv(out, GL_LINK_STATUS, &ok);
	if (!ok) {
		char buf[2048] = {};
		p_glGetProgramInfoLog(out, sizeof(buf) - 1, nullptr, buf);
		logf("glsl: %s link FAILED: %s", name, buf);
		return false;
	}
	logf("glsl: %s OK", name);
	return true;
}

bool Scene::init() {
	if (!gl_compile_program(kVS, kSceneFS, prog, "scene")) return false;
	if (!gl_compile_program(kVS, kPanelFS, panel_prog, "panel")) return false;
	if (!gl_compile_program(kVS, kBlitFS, blit_prog, "sdr_blit")) return false;
	u_res = p_glGetUniformLocation(prog, "u_res");
	u_time = p_glGetUniformLocation(prog, "u_time");
	u_hmax = p_glGetUniformLocation(prog, "u_hmax");
	u_flip = p_glGetUniformLocation(prog, "u_flip");
	p_rect = p_glGetUniformLocation(panel_prog, "u_rect");
	p_tex = p_glGetUniformLocation(panel_prog, "u_tex");
	p_res = p_glGetUniformLocation(panel_prog, "u_res");
	p_flip = p_glGetUniformLocation(panel_prog, "u_flip");
	b_src = p_glGetUniformLocation(blit_prog, "u_src");
	p_glGenVertexArrays(1, &vao);
	glGenTextures(1, &panel_tex);
	glBindTexture(GL_TEXTURE_2D, panel_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return glGetError() == GL_NO_ERROR;
}

void Scene::shutdown() {
	if (prog) p_glDeleteProgram(prog);
	if (panel_prog) p_glDeleteProgram(panel_prog);
	if (blit_prog) p_glDeleteProgram(blit_prog);
	if (vao) p_glDeleteVertexArrays(1, &vao);
	if (panel_tex) glDeleteTextures(1, &panel_tex);
	*this = Scene{};
}

void Scene::set_panel(const uint32_t* rgba, int w, int h) {
	glBindTexture(GL_TEXTURE_2D, panel_tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	panel_w = w;
	panel_h = h;
}

void Scene::render(int w, int h, float time, float hmax, bool flip_y) {
	glViewport(0, 0, w, h);
	glDisable(GL_BLEND);
	p_glBindVertexArray(vao);
	p_glUseProgram(prog);
	p_glUniform2f(u_res, (float)w, (float)h);
	p_glUniform1f(u_time, time);
	p_glUniform1f(u_hmax, hmax);
	p_glUniform1i(u_flip, flip_y ? 1 : 0);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	if (panel_w > 0) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		p_glUseProgram(panel_prog);
		p_glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, panel_tex);
		p_glUniform1i(p_tex, 0);
		p_glUniform2f(p_res, (float)w, (float)h);
		p_glUniform1i(p_flip, flip_y ? 1 : 0);
		// panel at bottom-left, 12 px margin; 2x on large targets
		float sc = w >= 1800 ? 2.0f : 1.0f;
		float pw = (float)panel_w * sc, ph = (float)panel_h * sc;
		p_glUniform4f(p_rect, 12.0f, (float)h - ph - 12.0f, pw, ph);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glDisable(GL_BLEND);
	}
}

void Scene::blit_sdr(GLuint src_tex, int w, int h) {
	p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, w, h);
	glDisable(GL_BLEND);
	p_glBindVertexArray(vao);
	p_glUseProgram(blit_prog);
	p_glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, src_tex);
	p_glUniform1i(b_src, 0);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}
