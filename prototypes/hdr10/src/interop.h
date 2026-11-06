// WGL_NV_DX_interop2: GL renders straight into a D3D11 texture (zero-copy; the GPU driver shares the allocation).
#pragma once
#include "gl_loader.h"
#include <d3d11.h>

struct Interop {
	HANDLE hdev = nullptr;   // wglDXOpenDeviceNV
	HANDLE hobj = nullptr;   // wglDXRegisterObjectNV
	GLuint tex = 0, fbo = 0;
	bool locked = false;
	bool lost = false;       // lock/register failure: treat as device loss -> full re-init

	static bool available(char* why, size_t n); // extension string + entry points present
	bool open(ID3D11Device* dev);
	bool register_texture(ID3D11Texture2D* t);
	void unregister_texture();
	bool lock();   // GL may render only between lock() and unlock()
	bool unlock(); // after unlock D3D may read the texture (driver orders GL before D3D)
	void close();
};
