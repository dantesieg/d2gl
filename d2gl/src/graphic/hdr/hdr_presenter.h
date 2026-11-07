/*
	D2GL HDR10 presenter: final SDR composite -> D3D11 encode (hdr_shader.h) -> DXGI flip swapchain.
	R10G10B10A2_UNORM + DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 when the output is in HDR mode, otherwise
	R8G8B8A8_UNORM + G22_P709 (SDR, signal passed through unchanged).

	GL -> D3D11 transfer:
	  - ZERO-COPY: WGL_NV_DX_interop2 (GL renders straight into a registered D3D11 texture);
	  - CPU READBACK COPY: async glReadPixels into a PBO ring (3), fence-checked, consumed one frame later,
	    uploaded into a dynamic D3D11 texture. This is the Proton/DXVK (Steam Deck) path.
	Render thread only (owns the GL context, the D3D11 immediate context and the swapchain).
	d3d11.dll / dxgi.dll are loaded on demand, so hdr=0 never loads them.
*/
#pragma once

#include <atomic>
#include <d3d11.h>
#include <dxgi1_6.h>

#include "hdr_color.h"

namespace d2gl::hdr {

enum class Transport { None, Interop, SharedTex, CpuCopy };

extern std::atomic<bool> g_display_changed; // set by the window procedure (WM_DISPLAYCHANGE)

struct DisplayState {
	bool found = false;
	bool os_hdr = false;     // output colour space is G2084 (OS/compositor HDR on)
	int hw_capable = -1;     // DisplayConfig advancedColorSupported, -1 unknown
	float min_lum = 0.0f, max_lum = 0.0f, max_ff_lum = 0.0f;
	UINT bpc = 0;
	DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
};

class Presenter {
public:
	static void preInit(); // process start, hdr=1: SetEnvironmentVariableA("DXVK_HDR", "1")

	Presenter() = default;
	~Presenter();

	bool init(HWND hwnd, int width, int height); // GL context current on the render thread
	void shutdown();

	// Frame start: resize if needed, lock interop, make the HDR target the GL "default framebuffer".
	bool beginFrame(int width, int height);
	// After the final composite and the menu. false = unrecoverable, caller must fall back to WGL.
	bool present(bool vsync);
	// P3 hdr_latency_waitable: block (bounded) until DXGI can accept another frame. No-op without the flag.
	void waitForFrame();
	// P3 hdr_single_fence: present() itself waits for all GL work (shared-texture transport).
	bool fencesGlInPresent() const { return m_transport == Transport::SharedTex; }

	Transport transport() const { return m_transport; }
	const char* transportName() const;
	bool presentingHdr() const { return m_hdr_presenting; }
	const DisplayState& display() const { return m_display; }
	const char* adapterName() const { return m_adapter_name; }
	const char* refusal() const { return m_refusal; }
	float effectivePeak() const { return m_effective_peak; }
	int lostCount() const { return m_lost_count; }
	void setEmissive(bool on) { m_emissive = on; } // render thread: GL wrote 1 - emission into alpha this frame
	void setCinematic(bool on) { m_cinematic = on; } // render thread: movie / menu / loading frame ([HDR] movie_nits)

private:
	bool createD3D();
	void destroyD3D();
	bool createSwapchain();
	bool createShared();   // D3D11 NT-handle textures imported into GL (GL_EXT_memory_object_win32)
	void destroyShared();
	bool createTarget();   // GL FBO + (registered D3D texture | GL texture + PBO ring) + D3D input SRV
	void destroyTarget();
	bool applyFormat(bool want_hdr);
	bool createRtv();
	void detectDisplay();
	bool recover(const char* why);
	Params currentParams();

	HWND m_hwnd = 0;
	int m_w = 0, m_h = 0;
	HMODULE m_d3d11 = 0, m_dxgi = 0;
	IDXGIFactory2* m_factory = nullptr;
	IDXGIAdapter1* m_adapter = nullptr;
	ID3D11Device* m_dev = nullptr;
	ID3D11DeviceContext* m_ctx = nullptr;
	IDXGISwapChain1* m_sc = nullptr;
	IDXGISwapChain3* m_sc3 = nullptr;
	IDXGISwapChain4* m_sc4 = nullptr;
	UINT m_sc_flags = 0;               // creation flags; ResizeBuffers must repeat them
	HANDLE m_latency_wait = nullptr;   // frame-latency waitable object (hdr_latency_waitable)
	ID3D11RenderTargetView* m_rtv = nullptr;
	ID3D11VertexShader* m_vs = nullptr;
	ID3D11PixelShader* m_ps = nullptr;
	ID3D11Buffer* m_cb = nullptr;
	ID3D11SamplerState* m_smp = nullptr;
	ID3D11Texture2D* m_in_tex = nullptr; // interop: shared texture; copy: dynamic upload texture
	ID3D11ShaderResourceView* m_in_srv = nullptr;
	DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;

	Transport m_transport = Transport::None;
	bool m_interop_available = false;
	HANDLE m_interop_dev = nullptr, m_interop_obj = nullptr;
	bool m_locked = false;

	GLuint m_fbo = 0, m_tex = 0;
	static constexpr int SHARED = 2; // GL writes one while D3D11 samples the other
	ID3D11Texture2D* m_sh_tex[SHARED] = {};
	ID3D11ShaderResourceView* m_sh_srv[SHARED] = {};
	ID3D11Query* m_sh_done[SHARED] = {}; // D3D11 finished sampling slot i
	bool m_sh_pending[SHARED] = {};
	GLuint m_sh_mem[SHARED] = {}, m_sh_gltex[SHARED] = {};
	int m_sh_cur = 0;
	static constexpr int RING = 3;
	GLuint m_pbo[RING] = {};
	GLsync m_fence[RING] = {};
	int m_ring_head = 0;   // next slot to write
	int m_ring_count = 0;  // pending readbacks

	bool m_hdr_presenting = false;
	bool m_want_hdr = false;
	bool m_metadata_set = false;
	float m_metadata_peak = 0.0f;
	float m_effective_peak = 1000.0f;
	int m_lost_count = 0;
	bool m_emissive = false;
	bool m_cinematic = false;
	uint32_t m_frame = 0;
	char m_adapter_name[128] = "?";
	char m_refusal[128] = "";
	DisplayState m_display;
};

}
