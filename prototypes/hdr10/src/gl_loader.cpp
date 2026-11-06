#include "gl_loader.h"
#include "log.h"
#include <cstring>

#define HDR_DEF(T, n) T p_##n = nullptr;
HDR_GL_FUNCS(HDR_DEF)
#undef HDR_DEF
WglExt g_wgl;

static PROC get_proc(const char* name) {
	PROC p = wglGetProcAddress(name);
	// wglGetProcAddress may return 1,2,3,-1 as failure codes on some drivers.
	if (p == nullptr || p == (PROC)1 || p == (PROC)2 || p == (PROC)3 || p == (PROC)-1) return nullptr;
	return p;
}

bool gl_load_core() {
	bool ok = true;
#define HDR_LOAD(T, n) p_##n = (T)get_proc(#n); if (!p_##n) { logf("gl: missing %s", #n); ok = false; }
	HDR_GL_FUNCS(HDR_LOAD)
#undef HDR_LOAD
	return ok;
}

static bool token_in(const char* list, const char* name) {
	if (!list) return false;
	size_t n = std::strlen(name);
	for (const char* p = list; (p = std::strstr(p, name)); p += n)
		if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0)) return true;
	return false;
}

bool wgl_has_extension(const char* name) { return token_in(g_wgl.wgl_extensions, name); }

bool gl_has_extension(const char* name) {
	if (!p_glGetStringi) return false;
	GLint n = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &n);
	for (GLint i = 0; i < n; ++i) {
		const char* e = (const char*)p_glGetStringi(GL_EXTENSIONS, (GLuint)i);
		if (e && !std::strcmp(e, name)) return true;
	}
	return false;
}

void gl_load_wgl_ext(HDC dc) {
	g_wgl.GetExtensionsStringARB = (PFNWGLGETEXTENSIONSSTRINGARBPROC)get_proc("wglGetExtensionsStringARB");
	const char* ext = g_wgl.GetExtensionsStringARB ? g_wgl.GetExtensionsStringARB(dc) : nullptr;
	if (!ext) {
		auto ext_fn = (PFNWGLGETEXTENSIONSSTRINGEXTPROC)get_proc("wglGetExtensionsStringEXT");
		ext = ext_fn ? ext_fn() : nullptr;
	}
	std::strncpy(g_wgl.wgl_extensions, ext ? ext : "", sizeof(g_wgl.wgl_extensions) - 1);
	logf("wgl: extension string %s (%u bytes)", ext ? "present" : "ABSENT", (unsigned)std::strlen(g_wgl.wgl_extensions));
	g_wgl.CreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)get_proc("wglCreateContextAttribsARB");
	g_wgl.SwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)get_proc("wglSwapIntervalEXT");
	g_wgl.has_interop = wgl_has_extension("WGL_NV_DX_interop");
	g_wgl.has_interop2 = wgl_has_extension("WGL_NV_DX_interop2");
	g_wgl.DXOpenDeviceNV = (PFNWGLDXOPENDEVICENVPROC)get_proc("wglDXOpenDeviceNV");
	g_wgl.DXCloseDeviceNV = (PFNWGLDXCLOSEDEVICENVPROC)get_proc("wglDXCloseDeviceNV");
	g_wgl.DXRegisterObjectNV = (PFNWGLDXREGISTEROBJECTNVPROC)get_proc("wglDXRegisterObjectNV");
	g_wgl.DXUnregisterObjectNV = (PFNWGLDXUNREGISTEROBJECTNVPROC)get_proc("wglDXUnregisterObjectNV");
	g_wgl.DXLockObjectsNV = (PFNWGLDXLOCKOBJECTSNVPROC)get_proc("wglDXLockObjectsNV");
	g_wgl.DXUnlockObjectsNV = (PFNWGLDXUNLOCKOBJECTSNVPROC)get_proc("wglDXUnlockObjectsNV");
	logf("wgl: WGL_NV_DX_interop=%d WGL_NV_DX_interop2=%d | wglDXOpenDeviceNV=%p RegisterObject=%p LockObjects=%p",
	     g_wgl.has_interop, g_wgl.has_interop2, (void*)g_wgl.DXOpenDeviceNV, (void*)g_wgl.DXRegisterObjectNV,
	     (void*)g_wgl.DXLockObjectsNV);
}
