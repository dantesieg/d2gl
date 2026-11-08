#include "pch.h"
#include "hdr_presenter.h"
#include "hdr_shader.h"
#include "graphic/frame_buffer.h"
#include "d2/common.h"

#include <d3dcompiler.h>

namespace d2gl::hdr {

std::atomic<bool> g_display_changed = false;

template <class T>
static void safeRelease(T*& p)
{
	if (p) {
		p->Release();
		p = nullptr;
	}
}

static HRESULT logHr(HRESULT hr, const char* expr, int line)
{
	if (FAILED(hr)) {
		error_log("HDR: %s failed hr=0x%08X (line %d)", expr, (unsigned)hr, line);
	} else {
		trace_log("HDR: %s ok", expr);
	}
	return hr;
}
#define HR(x) logHr((x), #x, __LINE__)

static void narrow(const wchar_t* w, char* out, int n) { WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, nullptr, nullptr); }

void Presenter::preInit()
{
	// DXVK reads DXVK_HDR through the Win32 environment when its DXGI factory is created, i.e. long after
	// this point; d3d11/dxgi are only loaded on demand by Presenter::init. A user-set value wins.
	char cur[8] = { 0 };
	if (GetEnvironmentVariableA("DXVK_HDR", cur, sizeof(cur)) == 0) {
		SetEnvironmentVariableA("DXVK_HDR", "1");
		trace_log("HDR: DXVK_HDR=1 set in-process");
	} else {
		trace_log("HDR: DXVK_HDR already set (%s)", cur);
	}
}

Presenter::~Presenter() { shutdown(); }

const char* Presenter::transportName() const
{
	switch (m_transport) {
		case Transport::Interop: return "ZERO-COPY (WGL_NV_DX_interop2)";
		case Transport::SharedTex: return "ZERO-COPY (shared D3D11 texture, GL_EXT_memory_object_win32)";
		case Transport::CpuCopy: return "CPU READBACK COPY (PBO ring)";
		default: return "none";
	}
}

Params Presenter::currentParams()
{
	Params p;
	p.sdr_white_nits = (App.hdr.sdr_white_system && App.hdr.sdr_white_system_nits > 0.0f) ? App.hdr.sdr_white_system_nits : App.hdr.sdr_white.value;
	float peak = App.hdr.peak.value;
	if (App.hdr.peak_auto && m_display.max_lum > 0.0f)
		peak = std::clamp(m_display.max_lum, App.hdr.peak.min, App.hdr.peak.max);
	m_effective_peak = peak;
	p.peak_nits = peak;
	p.brightness_stops = App.hdr.brightness.value;
	p.black_level = App.hdr.black_level.value;
	p.contrast = App.hdr.contrast.value;
	p.saturation = App.hdr.saturation.value;
	p.highlight_strength = App.hdr.highlight.value;
	p.highlight_knee = App.hdr.knee.value;
	p.tonemap = (Tonemap)App.hdr.tonemap.selected;
	p.dice_shoulder = App.hdr.dice_shoulder.value;
	p.transfer = (Transfer)App.hdr.eotf.selected;
	return p;
}

// ------------------------------------------------------------------------------------------ D3D11
bool Presenter::createD3D()
{
	if (!m_d3d11)
		m_d3d11 = LoadLibraryA("d3d11.dll");
	if (!m_dxgi)
		m_dxgi = LoadLibraryA("dxgi.dll");
	if (!m_d3d11 || !m_dxgi) {
		error_log("HDR: d3d11.dll/dxgi.dll not available");
		return false;
	}
	auto create_factory = (HRESULT(WINAPI*)(REFIID, void**))(void*)GetProcAddress(m_dxgi, "CreateDXGIFactory1");
	auto create_device = (PFN_D3D11_CREATE_DEVICE)(void*)GetProcAddress(m_d3d11, "D3D11CreateDevice");
	if (!create_factory || !create_device)
		return false;
	if (FAILED(HR(create_factory(__uuidof(IDXGIFactory2), (void**)&m_factory))))
		return false;

	// Adapter: GL_DEVICE_LUID_EXT when exposed, else the first hardware adapter whose vendor matches GL_VENDOR,
	// else adapter 0. Under Proton there is one adapter.
	LUID luid = {};
	bool have_luid = false;
	if (glewIsSupported("GL_EXT_memory_object_win32") || glewIsSupported("GL_EXT_semaphore_win32")) {
		GLubyte bytes[GL_LUID_SIZE_EXT] = {};
		while (glGetError() != GL_NO_ERROR) {}
		glGetUnsignedBytevEXT(GL_DEVICE_LUID_EXT, bytes);
		if (glGetError() == GL_NO_ERROR) {
			memcpy(&luid, bytes, sizeof(luid));
			have_luid = true;
		}
	}
	const char* vendor = (const char*)glGetString(GL_VENDOR);
	UINT want_vendor = 0;
	if (vendor && strstr(vendor, "NVIDIA")) want_vendor = 0x10DE;
	else if (vendor && (strstr(vendor, "AMD") || strstr(vendor, "ATI"))) want_vendor = 0x1002;
	else if (vendor && strstr(vendor, "Intel")) want_vendor = 0x8086;
	const char* how = "adapter 0";
	for (UINT i = 0;; i++) {
		IDXGIAdapter1* a = nullptr;
		if (m_factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND)
			break;
		DXGI_ADAPTER_DESC1 d;
		a->GetDesc1(&d);
		if (have_luid && d.AdapterLuid.LowPart == luid.LowPart && d.AdapterLuid.HighPart == luid.HighPart) {
			safeRelease(m_adapter);
			m_adapter = a;
			how = "GL LUID";
			break;
		}
		if (!have_luid && !m_adapter && want_vendor && d.VendorId == want_vendor && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
			m_adapter = a;
			how = "GL_VENDOR heuristic";
			continue;
		}
		a->Release();
	}
	if (!m_adapter && FAILED(HR(m_factory->EnumAdapters1(0, &m_adapter))))
		return false;
	DXGI_ADAPTER_DESC1 ad;
	m_adapter->GetDesc1(&ad);
	narrow(ad.Description, m_adapter_name, sizeof(m_adapter_name));
	trace_log("HDR: D3D11 adapter '%s' (%s)", m_adapter_name, how);

	const D3D_FEATURE_LEVEL fls[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
	if (FAILED(HR(create_device(m_adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, fls, 3, D3D11_SDK_VERSION, &m_dev, nullptr, &m_ctx))))
		return false;

	HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
	pD3DCompile compile = dc ? (pD3DCompile)(void*)GetProcAddress(dc, "D3DCompile") : nullptr;
	if (!compile) {
		error_log("HDR: d3dcompiler_47.dll / D3DCompile not available");
		return false;
	}
	ID3DBlob *vsb = nullptr, *psb = nullptr, *err = nullptr;
	HRESULT hr = HR(compile(kHdrHlsl, sizeof(kHdrHlsl) - 1, "d2gl_hdr", nullptr, nullptr, "vs_main", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &err));
	if (err) {
		error_log("HDR: vs: %.*s", (int)err->GetBufferSize(), (const char*)err->GetBufferPointer());
		safeRelease(err);
	}
	if (SUCCEEDED(hr))
		hr = HR(compile(kHdrHlsl, sizeof(kHdrHlsl) - 1, "d2gl_hdr", nullptr, nullptr, "ps_main", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &err));
	if (err) {
		error_log("HDR: ps: %.*s", (int)err->GetBufferSize(), (const char*)err->GetBufferPointer());
		safeRelease(err);
	}
	bool ok = SUCCEEDED(hr) && vsb && psb;
	if (ok)
		ok = SUCCEEDED(HR(m_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &m_vs)));
	if (ok)
		ok = SUCCEEDED(HR(m_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &m_ps)));
	safeRelease(vsb);
	safeRelease(psb);
	if (!ok)
		return false;

	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = sizeof(ShaderConstants);
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(HR(m_dev->CreateBuffer(&bd, nullptr, &m_cb))))
		return false;
	D3D11_SAMPLER_DESC sd = {};
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.MaxLOD = D3D11_FLOAT32_MAX;
	return SUCCEEDED(HR(m_dev->CreateSamplerState(&sd, &m_smp)));
}

void Presenter::destroyD3D()
{
	if (m_ctx) {
		m_ctx->ClearState();
		m_ctx->Flush();
	}
	safeRelease(m_rtv);
	if (m_latency_wait)
		CloseHandle(m_latency_wait);
	m_latency_wait = nullptr;
	m_sc_flags = 0;
	safeRelease(m_sc4);
	safeRelease(m_sc3);
	safeRelease(m_sc);
	safeRelease(m_smp);
	safeRelease(m_cb);
	safeRelease(m_ps);
	safeRelease(m_vs);
	safeRelease(m_ctx);
	safeRelease(m_dev);
	safeRelease(m_adapter);
	safeRelease(m_factory);
	m_format = DXGI_FORMAT_UNKNOWN;
	m_hdr_presenting = false;
	m_metadata_set = false;
}

// ------------------------------------------------------------------------------------------ display
static int queryAdvancedColor(const char* gdi_name, bool* enabled)
{
	UINT32 np = 0, nm = 0;
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS || !np)
		return -1;
	std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm ? nm : 1);
	if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS)
		return -1;
	for (UINT32 i = 0; i < np; i++) {
		DISPLAYCONFIG_SOURCE_DEVICE_NAME src = {};
		src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
		src.header.size = sizeof(src);
		src.header.adapterId = paths[i].sourceInfo.adapterId;
		src.header.id = paths[i].sourceInfo.id;
		if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS)
			continue;
		char name[64];
		narrow(src.viewGdiDeviceName, name, sizeof(name));
		if (strcmp(name, gdi_name))
			continue;
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO ac = {};
		ac.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
		ac.header.size = sizeof(ac);
		ac.header.adapterId = paths[i].targetInfo.adapterId;
		ac.header.id = paths[i].targetInfo.id;
		if (DisplayConfigGetDeviceInfo(&ac.header) != ERROR_SUCCESS)
			return -1;
		*enabled = ac.advancedColorEnabled;
		return ac.advancedColorSupported ? 1 : 0;
	}
	return -1;
}

void Presenter::detectDisplay()
{
	m_display = DisplayState{};
	if (!m_factory)
		return;
	if (!m_factory->IsCurrent()) {
		auto create_factory = (HRESULT(WINAPI*)(REFIID, void**))(void*)GetProcAddress(m_dxgi, "CreateDXGIFactory1");
		IDXGIFactory2* f = nullptr;
		if (create_factory && SUCCEEDED(HR(create_factory(__uuidof(IDXGIFactory2), (void**)&f)))) {
			safeRelease(m_factory);
			m_factory = f;
		}
	}
	HMONITOR mon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
	char gdi[64] = "?";
	for (UINT ai = 0; !m_display.found; ai++) {
		IDXGIAdapter1* a = nullptr;
		if (m_factory->EnumAdapters1(ai, &a) == DXGI_ERROR_NOT_FOUND)
			break;
		for (UINT oi = 0; !m_display.found; oi++) {
			IDXGIOutput* o = nullptr;
			if (a->EnumOutputs(oi, &o) == DXGI_ERROR_NOT_FOUND)
				break;
			DXGI_OUTPUT_DESC od;
			IDXGIOutput6* o6 = nullptr;
			if (SUCCEEDED(o->GetDesc(&od)) && od.Monitor == mon && SUCCEEDED(o->QueryInterface(__uuidof(IDXGIOutput6), (void**)&o6))) {
				DXGI_OUTPUT_DESC1 d1;
				if (SUCCEEDED(HR(o6->GetDesc1(&d1)))) {
					m_display.found = true;
					m_display.color_space = d1.ColorSpace;
					m_display.os_hdr = d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
					m_display.min_lum = d1.MinLuminance;
					m_display.max_lum = d1.MaxLuminance;
					m_display.max_ff_lum = d1.MaxFullFrameLuminance;
					m_display.bpc = d1.BitsPerColor;
					narrow(d1.DeviceName, gdi, sizeof(gdi));
				}
				o6->Release();
			}
			o->Release();
		}
		a->Release();
	}
	bool ac_enabled = false;
	m_display.hw_capable = queryAdvancedColor(gdi, &ac_enabled);
	trace_log("HDR: display %s found=%d os_hdr=%d hw_capable=%d bpc=%u lum %.3f/%.0f/%.0f", gdi, m_display.found, m_display.os_hdr,
		m_display.hw_capable, m_display.bpc, m_display.min_lum, m_display.max_lum, m_display.max_ff_lum);
}

// ------------------------------------------------------------------------------------------ swapchain
bool Presenter::createRtv()
{
	ID3D11Texture2D* bb = nullptr;
	if (FAILED(HR(m_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))))
		return false;
	HRESULT hr = HR(m_dev->CreateRenderTargetView(bb, nullptr, &m_rtv));
	bb->Release();
	return SUCCEEDED(hr);
}

bool Presenter::createSwapchain()
{
	detectDisplay();
	DXGI_SWAP_CHAIN_DESC1 d = {};
	d.Width = (UINT)m_w;
	d.Height = (UINT)m_h;
	d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	d.SampleDesc.Count = 1;
	d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	d.BufferCount = 2;
	d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	d.Flags = App.perf.hdr_waitable ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0;
	HRESULT hr = HR(m_factory->CreateSwapChainForHwnd(m_dev, m_hwnd, &d, nullptr, nullptr, &m_sc));
	if (FAILED(hr) && d.Flags) {
		d.Flags = 0; // waitable object unsupported: plain swapchain, as before
		hr = HR(m_factory->CreateSwapChainForHwnd(m_dev, m_hwnd, &d, nullptr, nullptr, &m_sc));
	}
	if (hr == DXGI_ERROR_INVALID_CALL) {
		d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		hr = HR(m_factory->CreateSwapChainForHwnd(m_dev, m_hwnd, &d, nullptr, nullptr, &m_sc));
	}
	if (FAILED(hr))
		return false;
	m_sc_flags = d.Flags;
	if (m_sc_flags) {
		IDXGISwapChain2* sc2 = nullptr;
		if (SUCCEEDED(m_sc->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&sc2))) {
			HR(sc2->SetMaximumFrameLatency(1));
			m_latency_wait = sc2->GetFrameLatencyWaitableObject();
			sc2->Release();
		}
		trace_log("HDR: frame-latency waitable swapchain, max latency 1, handle %s", m_latency_wait ? "ok" : "MISSING");
	}
	m_format = d.Format;
	HR(m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES));
	if (FAILED(m_sc->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&m_sc3)))
		m_sc3 = nullptr;
	if (FAILED(m_sc->QueryInterface(__uuidof(IDXGISwapChain4), (void**)&m_sc4)))
		m_sc4 = nullptr;
	return applyFormat(App.hdr.enabled);
}

bool Presenter::applyFormat(bool want_hdr)
{
	m_want_hdr = want_hdr;
	m_hdr_presenting = false;
	m_metadata_set = false;
	m_refusal[0] = 0;
	bool try_hdr = want_hdr;
	if (!want_hdr)
		strcpy_s(m_refusal, "HDR off in settings");
	else if (!m_display.os_hdr) {
		try_hdr = false;
		strcpy_s(m_refusal, "display/OS not in HDR mode");
	} else if (!m_sc3) {
		try_hdr = false;
		strcpy_s(m_refusal, "no IDXGISwapChain3");
	}

	const DXGI_FORMAT fmt = try_hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
	if (fmt != m_format) {
		safeRelease(m_rtv);
		m_ctx->OMSetRenderTargets(0, nullptr, nullptr);
		m_ctx->Flush();
		if (FAILED(HR(m_sc->ResizeBuffers(0, (UINT)m_w, (UINT)m_h, fmt, m_sc_flags))))
			return false;
		m_format = fmt;
	}
	if (try_hdr) {
		UINT support = 0;
		HRESULT hr = HR(m_sc3->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &support));
		trace_log("HDR: CheckColorSpaceSupport(G2084) = 0x%X", support);
		if (SUCCEEDED(hr) && (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) && SUCCEEDED(HR(m_sc3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020))))
			m_hdr_presenting = true;
		else {
			strcpy_s(m_refusal, "swapchain rejects G2084 (KDE desktop / no HDR WSI?)");
			safeRelease(m_rtv);
			if (FAILED(HR(m_sc->ResizeBuffers(0, (UINT)m_w, (UINT)m_h, DXGI_FORMAT_R8G8B8A8_UNORM, m_sc_flags))))
				return false;
			m_format = DXGI_FORMAT_R8G8B8A8_UNORM;
		}
	}
	if (!m_hdr_presenting && m_sc3)
		HR(m_sc3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709));
	trace_log("HDR: swapchain %s, presenting HDR10 = %d %s", m_format == DXGI_FORMAT_R10G10B10A2_UNORM ? "R10G10B10A2" : "R8G8B8A8", m_hdr_presenting, m_refusal);
	return m_rtv || createRtv();
}

// ------------------------------------------------------------------------------------------ GL target
bool Presenter::createTarget()
{
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = (UINT)m_w;
	td.Height = (UINT)m_h;
	td.MipLevels = 1;
	td.ArraySize = 1;
	// the final composite is the SDR signal: RGBA8, or RGBA16F (values above 1.0) with [HDR] float_pipeline
	m_float = App.hdr.float_mode;
	td.Format = m_float ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1;

	glGenTextures(1, &m_tex);
	glGenFramebuffers(1, &m_fbo);

	if (App.hdr.shared_texture && !m_interop_available) {
		if (createShared())
			return true;
		if (m_float) {
			error_log("HDR: float (R16G16B16A16_FLOAT) shared texture failed, retrying RGBA8 (float pipeline output clipped to 1.0)");
			m_float = false;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			if (createShared())
				return true;
		}
	}

	if (m_interop_available) {
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		if (SUCCEEDED(HR(m_dev->CreateTexture2D(&td, nullptr, &m_in_tex)))) {
			if (!m_interop_dev)
				m_interop_dev = wglDXOpenDeviceNV(m_dev);
			if (m_interop_dev)
				m_interop_obj = wglDXRegisterObjectNV(m_interop_dev, m_in_tex, m_tex, GL_TEXTURE_2D, WGL_ACCESS_WRITE_DISCARD_NV);
			if (m_interop_obj && wglDXLockObjectsNV(m_interop_dev, 1, &m_interop_obj)) {
				glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
				GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
				wglDXUnlockObjectsNV(m_interop_dev, 1, &m_interop_obj);
				if (st == GL_FRAMEBUFFER_COMPLETE && SUCCEEDED(HR(m_dev->CreateShaderResourceView(m_in_tex, nullptr, &m_in_srv)))) {
					m_transport = Transport::Interop;
					return true;
				}
			}
			error_log("HDR: interop registration failed (%lu), using the CPU copy path", GetLastError());
			if (m_interop_obj)
				wglDXUnregisterObjectNV(m_interop_dev, m_interop_obj);
			m_interop_obj = nullptr;
			m_interop_available = false;
			safeRelease(m_in_tex);
			glDeleteTextures(1, &m_tex);
			glGenTextures(1, &m_tex);
		}
	}

	// CPU copy path: plain GL texture + PBO ring, dynamic D3D11 upload texture.
	glBindTexture(GL_TEXTURE_2D, m_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, m_float ? GL_RGBA16F : GL_RGBA8, m_w, m_h, 0, GL_RGBA, m_float ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		error_log("HDR: target FBO incomplete");
		return false;
	}
	glGenBuffers(RING, m_pbo);
	for (int i = 0; i < RING; i++) {
		glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[i]);
		glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)m_w * m_h * targetBpp(), nullptr, GL_STREAM_READ);
	}
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	m_ring_head = m_ring_count = 0;

	td.Usage = D3D11_USAGE_DYNAMIC;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(HR(m_dev->CreateTexture2D(&td, nullptr, &m_in_tex))) || FAILED(HR(m_dev->CreateShaderResourceView(m_in_tex, nullptr, &m_in_srv))))
		return false;
	m_transport = Transport::CpuCopy;
	trace_log("HDR: CPU copy path, %s", m_float ? "RGBA16F" : "RGBA8");
	return true;
}

// Zero-copy under Proton: DXVK exports NT handles (VK_KHR_external_memory_win32), Wine's opengl32 imports them
// (GL_EXT_memory_object_win32). D3D11 owns the memory; GL only writes, D3D11 only samples. Any failure -> false,
// everything released, caller falls through to the PBO ring.
bool Presenter::createShared()
{
	if (!GLEW_EXT_memory_object_win32 || !glCreateMemoryObjectsEXT || !glImportMemoryWin32HandleEXT || !glTexStorageMem2DEXT) {
		error_log("HDR: shared texture unavailable: ext=%d memobj=%d create=%p import=%p storage=%p", (int)GLEW_EXT_memory_object_win32, (int)GLEW_EXT_memory_object, (void*)glCreateMemoryObjectsEXT, (void*)glImportMemoryWin32HandleEXT, (void*)glTexStorageMem2DEXT);
		return false;
	}
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = (UINT)m_w;
	td.Height = (UINT)m_h;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = m_float ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	td.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	D3D11_QUERY_DESC qd = { D3D11_QUERY_EVENT, 0 };
	while (glGetError() != GL_NO_ERROR) {}
	bool ok = true;
	for (int i = 0; i < SHARED && ok; i++) {
		ok = false;
		IDXGIResource1* res = nullptr;
		HANDLE h = nullptr;
		if (FAILED(HR(m_dev->CreateTexture2D(&td, nullptr, &m_sh_tex[i]))) ||
			FAILED(HR(m_sh_tex[i]->QueryInterface(__uuidof(IDXGIResource1), (void**)&res))) ||
			FAILED(HR(res->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &h)))) {
			safeRelease(res);
			break;
		}
		safeRelease(res);
		// Wine may accept only some handle types: D3D11 image (spec), then opaque win32 (Vulkan-style).
		static const GLenum types[] = { GL_HANDLE_TYPE_D3D11_IMAGE_EXT, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT };
		GLenum e = GL_NO_ERROR;
		for (GLenum type : types) {
			for (int ded = 1; ded >= 0; ded--) {
				if (m_sh_mem[i])
					glDeleteMemoryObjectsEXT(1, &m_sh_mem[i]);
				m_sh_mem[i] = 0;
				while (glGetError() != GL_NO_ERROR) {}
				glCreateMemoryObjectsEXT(1, &m_sh_mem[i]);
				const GLint dedicated = ded;
				glMemoryObjectParameterivEXT(m_sh_mem[i], GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
				glImportMemoryWin32HandleEXT(m_sh_mem[i], (GLuint64)m_w * m_h * targetBpp(), type, h);
				e = glGetError();
				trace_log("HDR: import type=0x%X dedicated=%d -> 0x%X", type, ded, e);
				if (e == GL_NO_ERROR)
					break;
			}
			if (e == GL_NO_ERROR)
				break;
		}
		CloseHandle(h); // win32 handle import does not transfer ownership
		if (e != GL_NO_ERROR || !glIsMemoryObjectEXT(m_sh_mem[i])) {
			error_log("HDR: glImportMemoryWin32HandleEXT failed (0x%X)", e);
			break;
		}
		glGenTextures(1, &m_sh_gltex[i]);
		glBindTexture(GL_TEXTURE_2D, m_sh_gltex[i]);
		glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, m_float ? GL_RGBA16F : GL_RGBA8, m_w, m_h, m_sh_mem[i], 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glBindTexture(GL_TEXTURE_2D, 0);
		if ((e = glGetError()) != GL_NO_ERROR) {
			error_log("HDR: glTexStorageMem2DEXT failed (0x%X)", e);
			break;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sh_gltex[i], 0);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			error_log("HDR: shared texture FBO incomplete");
			break;
		}
		ok = SUCCEEDED(HR(m_dev->CreateShaderResourceView(m_sh_tex[i], nullptr, &m_sh_srv[i]))) && SUCCEEDED(HR(m_dev->CreateQuery(&qd, &m_sh_done[i])));
	}
	if (!ok) {
		destroyShared();
		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
		error_log("HDR: shared texture import failed, using the CPU copy path");
		return false;
	}
	m_sh_cur = 0;
	glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sh_gltex[0], 0);
	m_transport = Transport::SharedTex; trace_log("HDR: shared texture %s", m_float ? "R16G16B16A16_FLOAT" : "R8G8B8A8_UNORM");
	return true;
}

void Presenter::destroyShared()
{
	if (m_ctx && m_sh_done[0]) // D3D11 must be done with the memory before GL drops its import
		m_ctx->Flush();
	for (int i = 0; i < SHARED; i++) {
		if (m_sh_pending[i] && m_sh_done[i]) {
			BOOL d = FALSE;
			for (int n = 0; n < 2000 && m_ctx->GetData(m_sh_done[i], &d, sizeof(d), 0) == S_FALSE; n++)
				Sleep(0);
		}
		m_sh_pending[i] = false;
		if (m_sh_gltex[i])
			glDeleteTextures(1, &m_sh_gltex[i]);
		if (m_sh_mem[i])
			glDeleteMemoryObjectsEXT(1, &m_sh_mem[i]);
		m_sh_gltex[i] = m_sh_mem[i] = 0;
		safeRelease(m_sh_srv[i]);
		safeRelease(m_sh_done[i]);
		safeRelease(m_sh_tex[i]);
	}
}

void Presenter::destroyTarget()
{
	if (m_locked && m_interop_obj)
		wglDXUnlockObjectsNV(m_interop_dev, 1, &m_interop_obj);
	m_locked = false;
	if (m_interop_obj)
		wglDXUnregisterObjectNV(m_interop_dev, m_interop_obj);
	m_interop_obj = nullptr;
	for (int i = 0; i < RING; i++) {
		if (m_fence[i])
			glDeleteSync(m_fence[i]);
		m_fence[i] = nullptr;
	}
	if (m_pbo[0])
		glDeleteBuffers(RING, m_pbo);
	memset(m_pbo, 0, sizeof(m_pbo));
	m_ring_head = m_ring_count = 0;
	destroyShared();
	FrameBuffer::setDefault(0);
	if (m_fbo)
		glDeleteFramebuffers(1, &m_fbo);
	if (m_tex)
		glDeleteTextures(1, &m_tex);
	m_fbo = m_tex = 0;
	if (m_ctx) {
		ID3D11ShaderResourceView* nul = nullptr;
		m_ctx->PSSetShaderResources(0, 1, &nul);
	}
	safeRelease(m_in_srv);
	safeRelease(m_in_tex);
	m_transport = Transport::None;
}

// ------------------------------------------------------------------------------------------ lifecycle
bool Presenter::init(HWND hwnd, int width, int height)
{
	m_hwnd = hwnd;
	m_w = std::max(width, 1);
	m_h = std::max(height, 1);
	m_interop_available = WGLEW_NV_DX_interop2 && wglDXOpenDeviceNV && wglDXRegisterObjectNV && wglDXLockObjectsNV;
	trace_log("HDR: WGL_NV_DX_interop2 %s", m_interop_available ? "available" : "NOT available (CPU copy path)");
	if (!createD3D() || !createSwapchain() || !createTarget()) {
		error_log("HDR: presenter init failed");
		shutdown();
		return false;
	}
	FrameBuffer::setDefault(m_fbo);
	trace_log("HDR: presenter ready, %dx%d, transport %s", m_w, m_h, transportName());
	return true;
}

void Presenter::shutdown()
{
	destroyTarget();
	if (m_interop_dev)
		wglDXCloseDeviceNV(m_interop_dev);
	m_interop_dev = nullptr;
	destroyD3D();
}

bool Presenter::recover(const char* why)
{
	m_lost_count++;
	error_log("HDR: recovering (%s), attempt %d", why, m_lost_count);
	if (m_lost_count > 3)
		return false;
	HWND hwnd = m_hwnd;
	shutdown();
	return init(hwnd, m_w, m_h);
}

bool Presenter::beginFrame(int width, int height)
{
	if (!m_sc)
		return false;
	if (width > 0 && height > 0 && (width != m_w || height != m_h)) {
		trace_log("HDR: resize %dx%d", width, height);
		destroyTarget();
		m_w = width;
		m_h = height;
		safeRelease(m_rtv);
		m_ctx->ClearState();
		m_ctx->Flush();
		if ((FAILED(HR(m_sc->ResizeBuffers(0, (UINT)m_w, (UINT)m_h, DXGI_FORMAT_UNKNOWN, m_sc_flags))) || !createRtv() || !createTarget()) && !recover("resize"))
			return false;
	}
	if ((m_frame++ % 120) == 0 && m_factory && !m_factory->IsCurrent())
		g_display_changed = true; // compositor/HDR state may have changed without WM_DISPLAYCHANGE
	if (g_display_changed.exchange(false) || m_want_hdr != App.hdr.enabled) {
		detectDisplay();
		if (!applyFormat(App.hdr.enabled) && !recover("format change"))
			return false;
	}
	if (m_transport == Transport::Interop && !m_locked) {
		if (!wglDXLockObjectsNV(m_interop_dev, 1, &m_interop_obj)) {
			if (!recover("interop lock"))
				return false;
			if (m_transport == Transport::Interop && !wglDXLockObjectsNV(m_interop_dev, 1, &m_interop_obj))
				return false;
		}
		m_locked = m_transport == Transport::Interop;
	}
	if (m_transport == Transport::SharedTex && m_sh_pending[m_sh_cur]) {
		BOOL d = FALSE;
		HRESULT q;
		const DWORD t0 = GetTickCount();
		while ((q = m_ctx->GetData(m_sh_done[m_sh_cur], &d, sizeof(d), 0)) == S_FALSE && GetTickCount() - t0 < 100) // normally done: sampled a frame ago
			YieldProcessor();
		m_sh_pending[m_sh_cur] = false;
		if (FAILED(q) && !recover("shared texture query"))
			return false;
	}
	FrameBuffer::setDefault(m_fbo);
	return true;
}

void Presenter::waitForFrame()
{
	// Bounded: a compositor that never signals (or a lost swapchain) must not hang the render thread.
	if (m_latency_wait)
		WaitForSingleObjectEx(m_latency_wait, 100, TRUE);
}

bool Presenter::present(bool vsync)
{
	if (!m_sc || !m_fbo)
		return false;

	if (m_transport == Transport::Interop) {
		m_locked = false;
		if (!wglDXUnlockObjectsNV(m_interop_dev, 1, &m_interop_obj))
			return recover("interop unlock");
	} else if (m_transport == Transport::SharedTex) {
		// GL -> D3D11 ordering: the GL frame must be complete before DXVK samples the shared memory. Two drivers
		// (radeonsi / RADV) share only the dma-buf, so a GL fence wait is the portable barrier. No pixel copy.
		GLsync s = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		GLenum r = glClientWaitSync(s, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ull);
		glDeleteSync(s);
		if (r != GL_ALREADY_SIGNALED && r != GL_CONDITION_SATISFIED)
			error_log("HDR: shared texture fence wait failed (0x%X)", r);
		safeRelease(m_in_srv);
		m_in_srv = m_sh_srv[m_sh_cur];
		m_in_srv->AddRef();
	} else {
		// Async readback: queue this frame into the ring, consume the oldest pending one (one frame later).
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[m_ring_head]);
		glPixelStorei(GL_PACK_ALIGNMENT, 4);
		glReadPixels(0, 0, m_w, m_h, GL_RGBA, m_float ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE, nullptr);
		if (m_fence[m_ring_head])
			glDeleteSync(m_fence[m_ring_head]);
		m_fence[m_ring_head] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		glFlush();
		m_ring_head = (m_ring_head + 1) % RING;
		m_ring_count = std::min(m_ring_count + 1, RING);

		// Consume: the slot written one frame ago if available, otherwise (first frame) this one.
		const int lag = m_ring_count >= 2 ? 2 : 1;
		const int slot = (m_ring_head - lag + RING) % RING;
		GLenum r = glClientWaitSync(m_fence[slot], GL_SYNC_FLUSH_COMMANDS_BIT, 0);
		if (r == GL_TIMEOUT_EXPIRED) // not ready yet: wait only for this readback, bounded
			r = glClientWaitSync(m_fence[slot], GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ull);
		if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED) {
			glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[slot]);
			const void* src = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)m_w * m_h * targetBpp(), GL_MAP_READ_BIT);
			D3D11_MAPPED_SUBRESOURCE m;
			if (src && SUCCEEDED(m_ctx->Map(m_in_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
				const size_t row = (size_t)m_w * targetBpp();
				for (int y = 0; y < m_h; y++)
					memcpy((uint8_t*)m.pData + (size_t)y * m.RowPitch, (const uint8_t*)src + (size_t)y * row, row);
				m_ctx->Unmap(m_in_tex, 0);
			}
			if (src)
				glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		} else {
			error_log("HDR: readback fence wait failed (0x%X)", r);
		}
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
	}

	// Encode + present.
	Params p = currentParams();
	if (m_cinematic) {
		// Movies, menus and loading screens: own paper white, no game brightness / highlight expansion
		// (emissive is zeroed below). Peak, tonemap, curve, contrast, saturation and black level stay.
		if (App.hdr.movie_nits.value > 0.0f)
			p.sdr_white_nits = App.hdr.movie_nits.value;
		p.brightness_stops = 0.0f;
		p.highlight_strength = 0.0f;
	}
	if (m_float)
		p.highlight_strength = 0.0f; // float pipeline: real light above 1.0 replaces highlight expansion
	if (App.hdr.act_grading && !m_cinematic && App.game.screen == GameScreen::InGame && d2::level_no) {
		// per-act look: Act 1 neutral, Act 2 warm desert, Act 3 humid green, Act 4 hellish red, Act 5 cold
		static const float tints[5][3] = { { 1.00f, 1.00f, 1.00f }, { 1.04f, 1.00f, 0.92f }, { 0.95f, 1.03f, 0.96f }, { 1.05f, 0.96f, 0.94f }, { 0.95f, 0.99f, 1.06f } };
		const int lvl = *d2::level_no;
		const int act = lvl >= 109 ? 4 : lvl >= 103 ? 3 : lvl >= 75 ? 2 : lvl >= 40 ? 1 : 0;
		p.brightness_stops += App.hdr.act_exposure[act].value;
		for (int i = 0; i < 3; i++)
			p.tint[i] = 1.0f + (tints[act][i] - 1.0f) * App.hdr.act_tint.value;
	}
	if (m_hdr_presenting && m_sc4 && (!m_metadata_set || m_metadata_peak != p.peak_nits)) {
		auto c = [](double v) { return (UINT16)(v / 0.00002 + 0.5); };
		DXGI_HDR_METADATA_HDR10 md = {};
		md.RedPrimary[0] = c(0.708), md.RedPrimary[1] = c(0.292);
		md.GreenPrimary[0] = c(0.170), md.GreenPrimary[1] = c(0.797);
		md.BluePrimary[0] = c(0.131), md.BluePrimary[1] = c(0.046);
		md.WhitePoint[0] = c(0.3127), md.WhitePoint[1] = c(0.3290);
		md.MaxMasteringLuminance = (UINT)p.peak_nits;
		md.MinMasteringLuminance = (UINT)((m_display.min_lum > 0.0f ? m_display.min_lum : 0.001f) * 10000.0f);
		md.MaxContentLightLevel = (UINT16)p.peak_nits;
		md.MaxFrameAverageLightLevel = (UINT16)(m_display.max_ff_lum > 0.0f ? std::min(m_display.max_ff_lum, p.peak_nits) : p.peak_nits * 0.5f);
		m_metadata_set = SUCCEEDED(HR(m_sc4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(md), &md)));
		m_metadata_peak = p.peak_nits;
	}
	// UI tag: the emit alpha pass caps world alpha below 1, UI drawn after it writes alpha 1
	p.ui_white_nits = (m_emissive && !m_float && !m_cinematic && App.hdr.ui_nits.value > 0.0f && App.game.screen == GameScreen::InGame) ? App.hdr.ui_nits.value : 0.0f;
	const float emit_scale = (m_emissive && App.hdr.emissive && !m_cinematic) ? std::max(App.hdr.emissive_nits.value / std::max(p.sdr_white_nits, 1.0f) - 1.0f, 0.0f) : 0.0f;
	const ShaderConstants k = makeConstants(p, m_hdr_presenting ? 1 : 0, App.hdr.calibration, true, (float)m_w, (float)m_h, emit_scale, m_emissive_graded);
	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(m_ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
		memcpy(m.pData, &k, sizeof(k));
		m_ctx->Unmap(m_cb, 0);
	}
	D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)m_w, (float)m_h, 0.0f, 1.0f };
	m_ctx->OMSetRenderTargets(1, &m_rtv, nullptr);
	m_ctx->RSSetViewports(1, &vp);
	m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_ctx->IASetInputLayout(nullptr);
	m_ctx->VSSetShader(m_vs, nullptr, 0);
	m_ctx->PSSetShader(m_ps, nullptr, 0);
	m_ctx->PSSetConstantBuffers(0, 1, &m_cb);
	m_ctx->PSSetShaderResources(0, 1, &m_in_srv);
	m_ctx->PSSetSamplers(0, 1, &m_smp);
	m_ctx->Draw(3, 0);
	ID3D11ShaderResourceView* nul = nullptr;
	m_ctx->PSSetShaderResources(0, 1, &nul); // interop texture is written by GL next frame
	if (m_transport == Transport::SharedTex) {
		m_ctx->End(m_sh_done[m_sh_cur]);
		m_sh_pending[m_sh_cur] = true;
		m_sh_cur = (m_sh_cur + 1) % SHARED;
		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sh_gltex[m_sh_cur], 0);
	}

	HRESULT hr = m_sc->Present(vsync ? 1 : 0, 0);
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
		HR(hr);
		HR(m_dev->GetDeviceRemovedReason());
		return recover("device removed");
	}
	if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) {
		HR(hr);
		return recover("present failed");
	}
	if (m_lost_count && (m_frame % 600) == 0)
		m_lost_count = 0; // a long stable run forgives earlier recoveries
	return true;
}

}
