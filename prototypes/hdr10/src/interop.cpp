#include "interop.h"
#include "log.h"
#include <cstdio>

bool Interop::available(char* why, size_t n) {
	if (!g_wgl.wgl_extensions[0]) { std::snprintf(why, n, "no WGL extension string"); return false; }
	if (!g_wgl.has_interop2) {
		std::snprintf(why, n, g_wgl.has_interop ? "only WGL_NV_DX_interop (v1, D3D9) - interop2 required for D3D11" : "WGL_NV_DX_interop2 not advertised");
		return false;
	}
	if (!g_wgl.DXOpenDeviceNV || !g_wgl.DXRegisterObjectNV || !g_wgl.DXLockObjectsNV || !g_wgl.DXUnlockObjectsNV ||
	    !g_wgl.DXUnregisterObjectNV || !g_wgl.DXCloseDeviceNV) {
		std::snprintf(why, n, "interop2 advertised but entry points missing");
		return false;
	}
	why[0] = 0;
	return true;
}

bool Interop::open(ID3D11Device* dev) {
	hdev = g_wgl.DXOpenDeviceNV(dev);
	logf("interop: wglDXOpenDeviceNV -> %p (GetLastError=%lu)", hdev, hdev ? 0ul : (unsigned long)GetLastError());
	if (!hdev) return false;
	glGenTextures(1, &tex);
	p_glGenFramebuffers(1, &fbo);
	lost = false;
	return true;
}

bool Interop::register_texture(ID3D11Texture2D* t) {
	hobj = g_wgl.DXRegisterObjectNV(hdev, t, tex, GL_TEXTURE_2D, WGL_ACCESS_WRITE_DISCARD_NV);
	if (!hobj) {
		logf("interop: wglDXRegisterObjectNV FAILED (GetLastError=%lu)", (unsigned long)GetLastError());
		lost = true;
		return false;
	}
	// FBO attachment must happen while the object is locked (texture storage only valid then).
	if (!lock()) return false;
	p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	GLenum st = p_glCheckFramebufferStatus(GL_FRAMEBUFFER);
	p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
	unlock();
	logf("interop: registered D3D11 texture -> GL tex %u, FBO status 0x%X", tex, st);
	if (st != GL_FRAMEBUFFER_COMPLETE) { lost = true; return false; }
	return true;
}

void Interop::unregister_texture() {
	if (locked) unlock();
	if (hobj) {
		if (!g_wgl.DXUnregisterObjectNV(hdev, hobj)) logf("interop: wglDXUnregisterObjectNV failed (%lu)", (unsigned long)GetLastError());
		hobj = nullptr;
	}
}

bool Interop::lock() {
	if (!hobj) return false;
	if (!g_wgl.DXLockObjectsNV(hdev, 1, &hobj)) {
		logf("interop: wglDXLockObjectsNV FAILED (GetLastError=0x%lX) -> treating as device loss", (unsigned long)GetLastError());
		lost = true;
		return false;
	}
	locked = true;
	return true;
}

bool Interop::unlock() {
	if (!locked) return true;
	locked = false;
	if (!g_wgl.DXUnlockObjectsNV(hdev, 1, &hobj)) {
		logf("interop: wglDXUnlockObjectsNV FAILED (GetLastError=0x%lX)", (unsigned long)GetLastError());
		lost = true;
		return false;
	}
	return true;
}

void Interop::close() {
	unregister_texture();
	if (fbo) p_glDeleteFramebuffers(1, &fbo);
	if (tex) glDeleteTextures(1, &tex);
	fbo = tex = 0;
	if (hdev) {
		if (!g_wgl.DXCloseDeviceNV(hdev)) logf("interop: wglDXCloseDeviceNV failed (%lu)", (unsigned long)GetLastError());
		hdev = nullptr;
	}
}
