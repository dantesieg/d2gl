// D3D11 + DXGI flip-model presenter: samples the GL-rendered shared texture and encodes SDR or HDR10.
#pragma once
#include <d3d11.h>
#include <dxgi1_6.h>
#include "color_math.h"

struct DisplayInfo {
	bool found = false;                 // IDXGIOutput6 for the window's monitor located
	char device[64] = "?";
	char adapter[128] = "?";            // adapter that owns the output (may differ from render adapter)
	DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	float min_lum = 0, max_lum = 0, max_full_frame_lum = 0;
	UINT bits_per_color = 0;
	// The three separate statuses' first two (the third lives in Presenter::hdr_presenting):
	bool os_hdr_enabled = false;        // Windows "Use HDR" on for this display (Desc1.ColorSpace == G2084, or DisplayConfig advancedColorEnabled)
	int hw_hdr_capable = -1;            // DisplayConfig advancedColorSupported: 1/0, -1 = query failed/unknown
	HMONITOR monitor = nullptr;
};

struct Presenter {
	IDXGIFactory2* factory = nullptr;
	IDXGIAdapter1* adapter = nullptr;
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	IDXGISwapChain1* sc = nullptr;
	IDXGISwapChain3* sc3 = nullptr;
	IDXGISwapChain4* sc4 = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11Texture2D* shared_tex = nullptr;      // GL renders here through WGL_NV_DX_interop2
	ID3D11ShaderResourceView* srv = nullptr;
	ID3D11VertexShader* vs = nullptr;
	ID3D11PixelShader* ps = nullptr;
	ID3D11Buffer* cb = nullptr;
	ID3D11SamplerState* smp = nullptr;
	HWND hwnd = nullptr;
	int w = 0, h = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	bool hdr_presenting = false;        // status 3: swapchain is R10G10B10A2 + G2084/P2020 accepted
	bool hdr_metadata_ok = false;
	char adapter_name[128] = "?";
	char hdr_refusal[128] = "";         // why HDR10 was not used (empty if presenting HDR)
	bool device_lost = false;
	DisplayInfo display;

	bool create_device(const LUID* gl_luid, const char* gl_vendor);
	bool create_swapchain(HWND wnd, int width, int height, bool want_hdr);
	bool create_shared_texture(int width, int height);
	void release_shared_texture();
	bool resize(int width, int height);
	bool reconfigure(bool want_hdr);    // after display/HDR state change: re-detect + format/colour space
	void detect_display();
	bool draw_and_present(const hdr::Params& p, float split_x, bool interval1);
	void destroy();
	static bool compile_shaders_selftest(); // D3DCompile only, no device needed
	// Runs the real HLSL encode (HDR10 mode) on the D3D device into an offscreen R10G10B10A2 target and compares
	// every pixel with color_math.h::encode_pixel. Needs a device but no swapchain/HDR display. Returns #mismatches.
	int encode_selftest();
private:
	void encode_pass(ID3D11RenderTargetView* target, ID3D11ShaderResourceView* input, const hdr::Params& p, int out_mode,
	                 float split_x, int width, int height);
	bool create_shaders();
	bool apply_format(bool want_hdr);
	bool create_rtv();
};
