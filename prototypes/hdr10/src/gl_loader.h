// Tiny hand-written GL/WGL loader: only the entry points this prototype uses.
#pragma once
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>

#define HDR_GL_FUNCS(X) \
	X(PFNGLCREATESHADERPROC, glCreateShader) X(PFNGLSHADERSOURCEPROC, glShaderSource) \
	X(PFNGLCOMPILESHADERPROC, glCompileShader) X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
	X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
	X(PFNGLATTACHSHADERPROC, glAttachShader) X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
	X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
	X(PFNGLUSEPROGRAMPROC, glUseProgram) X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
	X(PFNGLUNIFORM1FPROC, glUniform1f) X(PFNGLUNIFORM2FPROC, glUniform2f) X(PFNGLUNIFORM1IPROC, glUniform1i) X(PFNGLUNIFORM4FPROC, glUniform4f) \
	X(PFNGLDELETESHADERPROC, glDeleteShader) X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
	X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
	X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) \
	X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
	X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
	X(PFNGLACTIVETEXTUREPROC, glActiveTexture) X(PFNGLGETSTRINGIPROC, glGetStringi)

#define HDR_DECL(T, n) extern T p_##n;
HDR_GL_FUNCS(HDR_DECL)
#undef HDR_DECL

struct WglExt {
	PFNWGLGETEXTENSIONSSTRINGARBPROC GetExtensionsStringARB = nullptr;
	PFNWGLCREATECONTEXTATTRIBSARBPROC CreateContextAttribsARB = nullptr;
	PFNWGLSWAPINTERVALEXTPROC SwapIntervalEXT = nullptr;
	// WGL_NV_DX_interop / interop2
	PFNWGLDXOPENDEVICENVPROC DXOpenDeviceNV = nullptr;
	PFNWGLDXCLOSEDEVICENVPROC DXCloseDeviceNV = nullptr;
	PFNWGLDXREGISTEROBJECTNVPROC DXRegisterObjectNV = nullptr;
	PFNWGLDXUNREGISTEROBJECTNVPROC DXUnregisterObjectNV = nullptr;
	PFNWGLDXLOCKOBJECTSNVPROC DXLockObjectsNV = nullptr;
	PFNWGLDXUNLOCKOBJECTSNVPROC DXUnlockObjectsNV = nullptr;
	bool has_interop = false, has_interop2 = false;
	// GL_EXT_memory_object(_win32) LUID query
	PFNGLGETUNSIGNEDBYTEVEXTPROC GetUnsignedBytevEXT = nullptr;
	bool has_memory_object = false;
	char wgl_extensions[8192] = {};
};
extern WglExt g_wgl;

bool gl_load_core();                     // after a core context is current
void gl_load_wgl_ext(HDC dc);            // after any context is current
bool gl_has_extension(const char* name); // GL_NUM_EXTENSIONS / glGetStringi
bool wgl_has_extension(const char* name);
