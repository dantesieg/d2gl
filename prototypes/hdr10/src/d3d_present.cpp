#include "d3d_present.h"
#include "hdr_shader.h"
#include "log.h"
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>

template <class T> static void safe_release(T*& p) { if (p) { p->Release(); p = nullptr; } }

static void narrow(const wchar_t* w, char* out, size_t n) {
	WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, nullptr, nullptr);
}

static const char* cs_name(DXGI_COLOR_SPACE_TYPE c) {
	switch (c) {
	case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709: return "RGB_FULL_G22_NONE_P709 (sRGB)";
	case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709: return "RGB_FULL_G10_NONE_P709 (scRGB)";
	case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020: return "RGB_FULL_G2084_NONE_P2020 (HDR10)";
	default: return "other";
	}
}
const char* dxgi_cs_name(DXGI_COLOR_SPACE_TYPE c) { return cs_name(c); }

// ---------------------------------------------------------------- shaders (runtime D3DCompile)
static pD3DCompile load_d3dcompile() {
	static pD3DCompile fn = nullptr;
	static bool tried = false;
	if (!tried) {
		tried = true;
		HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
		if (m) fn = (pD3DCompile)(void*)GetProcAddress(m, "D3DCompile");
		logf("d3d: d3dcompiler_47.dll %s, D3DCompile %s", m ? "loaded" : "NOT FOUND", fn ? "found" : "missing");
	}
	return fn;
}

static ID3DBlob* compile(const char* entry, const char* target) {
	pD3DCompile fn = load_d3dcompile();
	if (!fn) return nullptr;
	ID3DBlob *code = nullptr, *err = nullptr;
	HRESULT hr = fn(kHdrHlsl, sizeof(kHdrHlsl) - 1, "hdr_shader.hlsl", nullptr, nullptr, entry, target,
	                D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &code, &err);
	log_hr(hr, entry, __FILE__, __LINE__);
	if (err) { logf("d3dcompile %s: %.*s", entry, (int)err->GetBufferSize(), (const char*)err->GetBufferPointer()); err->Release(); }
	if (FAILED(hr)) { safe_release(code); return nullptr; }
	return code;
}

bool Presenter::compile_shaders_selftest() {
	ID3DBlob* v = compile("vs_main", "vs_4_0");
	ID3DBlob* p = compile("ps_main", "ps_4_0");
	bool ok = v && p;
	logf("selftest: HLSL vs_4_0=%s (%u bytes) ps_4_0=%s (%u bytes)", v ? "OK" : "FAIL", v ? (unsigned)v->GetBufferSize() : 0,
	     p ? "OK" : "FAIL", p ? (unsigned)p->GetBufferSize() : 0);
	safe_release(v);
	safe_release(p);
	return ok;
}

struct CB {
	float sdr_white, peak, src_peak, strength;
	float knee; int transfer, tonemap, alpha_mode;
	int out_mode; float split_x; float pad[2];
};
static_assert(sizeof(CB) == 48, "cbuffer layout");

bool Presenter::create_shaders() {
	ID3DBlob* v = compile("vs_main", "vs_4_0");
	ID3DBlob* p = compile("ps_main", "ps_4_0");
	bool ok = v && p;
	if (ok) ok = SUCCEEDED(HR(dev->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr, &vs)));
	if (ok) ok = SUCCEEDED(HR(dev->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &ps)));
	safe_release(v);
	safe_release(p);
	if (!ok) return false;
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = sizeof(CB);
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(HR(dev->CreateBuffer(&bd, nullptr, &cb)))) return false;
	D3D11_SAMPLER_DESC sd = {};
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // 1:1 texel mapping; no filtering of PQ-unencoded input
	sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.MaxLOD = D3D11_FLOAT32_MAX;
	return SUCCEEDED(HR(dev->CreateSamplerState(&sd, &smp)));
}

// ---------------------------------------------------------------- device / adapter selection
bool Presenter::create_device(const LUID* gl_luid, const char* gl_vendor) {
	if (FAILED(HR(CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)&factory)))) return false;
	UINT want_vendor = 0;
	if (gl_vendor) {
		if (std::strstr(gl_vendor, "NVIDIA")) want_vendor = 0x10DE;
		else if (std::strstr(gl_vendor, "AMD") || std::strstr(gl_vendor, "ATI")) want_vendor = 0x1002;
		else if (std::strstr(gl_vendor, "Intel")) want_vendor = 0x8086;
	}
	IDXGIAdapter1* pick = nullptr;
	const char* how = "default adapter (no LUID, no vendor match)";
	for (UINT i = 0;; ++i) {
		IDXGIAdapter1* a = nullptr;
		if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
		DXGI_ADAPTER_DESC1 d;
		a->GetDesc1(&d);
		char name[128];
		narrow(d.Description, name, sizeof(name));
		logf("dxgi: adapter %u '%s' vendor=0x%04X LUID=%08lX:%08lX flags=0x%X", i, name, d.VendorId,
		     (unsigned long)d.AdapterLuid.HighPart, (unsigned long)d.AdapterLuid.LowPart, d.Flags);
		bool sw = d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE;
		if (gl_luid && d.AdapterLuid.LowPart == gl_luid->LowPart && d.AdapterLuid.HighPart == gl_luid->HighPart) {
			safe_release(pick);
			pick = a;
			how = "GL_DEVICE_LUID_EXT match";
			break;
		}
		if (!gl_luid && !pick && !sw && want_vendor && d.VendorId == want_vendor) {
			pick = a;
			how = "heuristic: GL_VENDOR -> PCI vendor id, first match";
			continue;
		}
		a->Release();
	}
	if (gl_luid && std::strcmp(how, "GL_DEVICE_LUID_EXT match") != 0) logf("dxgi: WARNING GL LUID matched no adapter");
	if (!pick && FAILED(HR(factory->EnumAdapters1(0, &pick)))) return false;
	adapter = pick;
	DXGI_ADAPTER_DESC1 d;
	adapter->GetDesc1(&d);
	narrow(d.Description, adapter_name, sizeof(adapter_name));
	logf("dxgi: render adapter '%s' selected by %s", adapter_name, how);

	const D3D_FEATURE_LEVEL fls[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
	D3D_FEATURE_LEVEL got = {};
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	HRESULT hr = HR(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, fls, 4, D3D11_SDK_VERSION, &dev, &got, &ctx));
	if (hr == E_INVALIDARG) // 11_1 unknown to an 11.0 runtime
		hr = HR(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, fls + 1, 3, D3D11_SDK_VERSION, &dev, &got, &ctx));
	if (FAILED(hr)) return false;
	logf("d3d11: device created, feature level 0x%X", (unsigned)got);
	device_lost = false;
	return create_shaders();
}

// ---------------------------------------------------------------- display detection
static int query_advanced_color(const char* gdi_device, bool* enabled) {
	UINT32 np = 0, nm = 0;
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS || !np) return -1;
	DISPLAYCONFIG_PATH_INFO* paths = new DISPLAYCONFIG_PATH_INFO[np];
	DISPLAYCONFIG_MODE_INFO* modes = new DISPLAYCONFIG_MODE_INFO[nm ? nm : 1];
	int res = -1;
	LONG qr = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths, &nm, modes, nullptr);
	if (qr != ERROR_SUCCESS) logf("displayconfig: QueryDisplayConfig failed %ld", qr);
	for (UINT32 i = 0; qr == ERROR_SUCCESS && i < np; ++i) {
		DISPLAYCONFIG_SOURCE_DEVICE_NAME src = {};
		src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
		src.header.size = sizeof(src);
		src.header.adapterId = paths[i].sourceInfo.adapterId;
		src.header.id = paths[i].sourceInfo.id;
		if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS) continue;
		char name[64];
		narrow(src.viewGdiDeviceName, name, sizeof(name));
		if (std::strcmp(name, gdi_device)) continue;
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO ac = {};
		ac.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
		ac.header.size = sizeof(ac);
		ac.header.adapterId = paths[i].targetInfo.adapterId;
		ac.header.id = paths[i].targetInfo.id;
		LONG r = DisplayConfigGetDeviceInfo(&ac.header);
		if (r != ERROR_SUCCESS) { logf("displayconfig: GET_ADVANCED_COLOR_INFO failed %ld", r); break; }
		*enabled = ac.advancedColorEnabled;
		res = ac.advancedColorSupported ? 1 : 0;
		logf("displayconfig: %s advancedColorSupported=%u advancedColorEnabled=%u wideColorEnforced=%u bpc=%u",
		     name, (unsigned)ac.advancedColorSupported, (unsigned)ac.advancedColorEnabled,
		     (unsigned)ac.wideColorEnforced, (unsigned)ac.bitsPerColorChannel);
		break;
	}
	delete[] paths;
	delete[] modes;
	return res;
}

void Presenter::detect_display() {
	display = DisplayInfo{};
	if (!factory) return;
	if (!factory->IsCurrent()) { // adapters/outputs changed (hotplug, HDR toggle): need a fresh factory
		logf("dxgi: factory not current -> recreating");
		safe_release(factory);
		if (FAILED(HR(CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)&factory)))) return;
	}
	HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
	display.monitor = mon;
	// The window's output may belong to a different adapter than the render adapter (hybrid GPUs).
	for (UINT ai = 0; !display.found; ++ai) {
		IDXGIAdapter1* a = nullptr;
		if (factory->EnumAdapters1(ai, &a) == DXGI_ERROR_NOT_FOUND) break;
		for (UINT oi = 0;; ++oi) {
			IDXGIOutput* o = nullptr;
			if (a->EnumOutputs(oi, &o) == DXGI_ERROR_NOT_FOUND) break;
			DXGI_OUTPUT_DESC od;
			o->GetDesc(&od);
			if (od.Monitor == mon) {
				IDXGIOutput6* o6 = nullptr;
				if (SUCCEEDED(HR(o->QueryInterface(__uuidof(IDXGIOutput6), (void**)&o6)))) {
					DXGI_OUTPUT_DESC1 d1;
					if (SUCCEEDED(HR(o6->GetDesc1(&d1)))) {
						display.found = true;
						narrow(d1.DeviceName, display.device, sizeof(display.device));
						display.color_space = d1.ColorSpace;
						display.min_lum = d1.MinLuminance;
						display.max_lum = d1.MaxLuminance;
						display.max_full_frame_lum = d1.MaxFullFrameLuminance;
						display.bits_per_color = d1.BitsPerColor;
						display.os_hdr_enabled = d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
						DXGI_ADAPTER_DESC1 ad;
						a->GetDesc1(&ad);
						narrow(ad.Description, display.adapter, sizeof(display.adapter));
					}
					o6->Release();
				}
			}
			o->Release();
			if (display.found) break;
		}
		a->Release();
	}
	if (!display.found) {
		logf("dxgi: no IDXGIOutput6 for the window's monitor (output enumeration unsupported or headless)");
		MONITORINFOEXA mi = {};
		mi.cbSize = sizeof(mi);
		if (GetMonitorInfoA(mon, &mi)) std::snprintf(display.device, sizeof(display.device), "%s", mi.szDevice);
	}
	bool ac_enabled = false;
	display.hw_hdr_capable = query_advanced_color(display.device, &ac_enabled);
	if (display.hw_hdr_capable >= 0 && ac_enabled != display.os_hdr_enabled)
		logf("display: NOTE DisplayConfig advancedColorEnabled=%d differs from Desc1 colour space (ACM/WCG case?)", ac_enabled);
	logf("display: %s on '%s' found=%d colorspace=%s bpc=%u lum min=%.4f max=%.1f maxFF=%.1f | OS HDR enabled=%d | HDR-capable=%s",
	     display.device, display.adapter, display.found, cs_name(display.color_space), display.bits_per_color,
	     display.min_lum, display.max_lum, display.max_full_frame_lum, display.os_hdr_enabled,
	     display.hw_hdr_capable < 0 ? "unknown" : display.hw_hdr_capable ? "yes" : "no");
}

// ---------------------------------------------------------------- swapchain
bool Presenter::create_rtv() {
	ID3D11Texture2D* bb = nullptr;
	if (FAILED(HR(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)))) return false;
	HRESULT hr = HR(dev->CreateRenderTargetView(bb, nullptr, &rtv));
	bb->Release();
	return SUCCEEDED(hr);
}

bool Presenter::apply_format(bool want_hdr) {
	// Decide HDR10 vs SDR. HDR10 only if requested, the OS has HDR on for this output and the swapchain accepts G2084.
	hdr_presenting = false;
	hdr_metadata_ok = false;
	hdr_refusal[0] = 0;
	bool try_hdr = want_hdr;
	if (!want_hdr) std::snprintf(hdr_refusal, sizeof(hdr_refusal), "HDR disabled by user");
	else if (!sc3) { try_hdr = false; std::snprintf(hdr_refusal, sizeof(hdr_refusal), "no IDXGISwapChain3"); }
	else if (!display.os_hdr_enabled) { try_hdr = false; std::snprintf(hdr_refusal, sizeof(hdr_refusal), "OS HDR off / output not HDR10"); }

	DXGI_FORMAT fmt = try_hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
	if (fmt != format) {
		safe_release(rtv);
		ctx->OMSetRenderTargets(0, nullptr, nullptr);
		ctx->Flush();
		if (FAILED(HR(sc->ResizeBuffers(0, (UINT)w, (UINT)h, fmt, 0)))) return false;
		format = fmt;
	}
	if (try_hdr) {
		UINT support = 0;
		HRESULT hr = HR(sc3->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &support));
		logf("dxgi: CheckColorSpaceSupport(G2084_P2020) flags=0x%X (PRESENT=%u)", support, support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT);
		if (SUCCEEDED(hr) && (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) &&
		    SUCCEEDED(HR(sc3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)))) {
			color_space = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
			hdr_presenting = true;
		} else {
			std::snprintf(hdr_refusal, sizeof(hdr_refusal), "G2084 colour space not supported by swapchain");
			safe_release(rtv);
			if (FAILED(HR(sc->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_R8G8B8A8_UNORM, 0)))) return false;
			format = DXGI_FORMAT_R8G8B8A8_UNORM;
		}
	}
	if (!hdr_presenting) {
		color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
		if (sc3) HR(sc3->SetColorSpace1(color_space));
	}
	if (hdr_presenting && sc4) {
		// HDR10 static metadata: Rec.2020 primaries, D65, units of 0.00002; luminance from the output (fallback 1000/0.001).
		auto c = [](double v) { return (UINT16)(v / 0.00002 + 0.5); };
		DXGI_HDR_METADATA_HDR10 md = {};
		md.RedPrimary[0] = c(0.708); md.RedPrimary[1] = c(0.292);
		md.GreenPrimary[0] = c(0.170); md.GreenPrimary[1] = c(0.797);
		md.BluePrimary[0] = c(0.131); md.BluePrimary[1] = c(0.046);
		md.WhitePoint[0] = c(0.3127); md.WhitePoint[1] = c(0.3290);
		float maxl = display.max_lum > 0 ? display.max_lum : 1000.0f;
		md.MaxMasteringLuminance = (UINT)maxl;
		md.MinMasteringLuminance = (UINT)((display.min_lum > 0 ? display.min_lum : 0.001f) * 10000.0f);
		md.MaxContentLightLevel = (UINT16)maxl;
		md.MaxFrameAverageLightLevel = (UINT16)(display.max_full_frame_lum > 0 ? display.max_full_frame_lum : maxl * 0.5f);
		hdr_metadata_ok = SUCCEEDED(HR(sc4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(md), &md)));
	} else if (sc4) {
		HR(sc4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr));
	}
	logf("dxgi: swapchain format=%s colorspace=%s HDR10 presenting=%d metadata=%d%s%s",
	     format == DXGI_FORMAT_R10G10B10A2_UNORM ? "R10G10B10A2_UNORM" : "R8G8B8A8_UNORM", cs_name(color_space),
	     hdr_presenting, hdr_metadata_ok, hdr_refusal[0] ? " | refusal: " : "", hdr_refusal);
	return rtv || create_rtv();
}

bool Presenter::create_swapchain(HWND wnd, int width, int height, bool want_hdr) {
	hwnd = wnd;
	w = width;
	h = height;
	detect_display();
	DXGI_SWAP_CHAIN_DESC1 d = {};
	d.Width = (UINT)w;
	d.Height = (UINT)h;
	d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	d.SampleDesc.Count = 1;
	d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	d.BufferCount = 2;
	d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	HRESULT hr = HR(factory->CreateSwapChainForHwnd(dev, hwnd, &d, nullptr, nullptr, &sc));
	if (hr == DXGI_ERROR_INVALID_CALL) { // pre-Win10 runtimes: FLIP_DISCARD unknown
		d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		hr = HR(factory->CreateSwapChainForHwnd(dev, hwnd, &d, nullptr, nullptr, &sc));
	}
	if (FAILED(hr)) return false;
	format = d.Format;
	HR(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER)); // we do borderless fullscreen ourselves
	if (FAILED(HR(sc->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3)))) sc3 = nullptr;
	if (FAILED(HR(sc->QueryInterface(__uuidof(IDXGISwapChain4), (void**)&sc4)))) sc4 = nullptr;
	return apply_format(want_hdr);
}

bool Presenter::reconfigure(bool want_hdr) {
	if (!sc) return false;
	detect_display();
	return apply_format(want_hdr);
}

bool Presenter::create_shared_texture(int width, int height) {
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = (UINT)width;
	td.Height = (UINT)height;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; // RGBA16F: SDR signal + >1.0 highlights
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(HR(dev->CreateTexture2D(&td, nullptr, &shared_tex)))) return false;
	return SUCCEEDED(HR(dev->CreateShaderResourceView(shared_tex, nullptr, &srv)));
}

void Presenter::release_shared_texture() {
	if (ctx) { ID3D11ShaderResourceView* nul = nullptr; ctx->PSSetShaderResources(0, 1, &nul); }
	safe_release(srv);
	safe_release(shared_tex);
}

bool Presenter::resize(int width, int height) {
	if (!sc || width <= 0 || height <= 0) return false;
	w = width;
	h = height;
	safe_release(rtv);
	ctx->OMSetRenderTargets(0, nullptr, nullptr);
	ctx->ClearState();
	ctx->Flush();
	if (FAILED(HR(sc->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0)))) return false;
	return create_rtv();
}

bool Presenter::draw_and_present(const hdr::Params& p, float split_x, bool interval1) {
	if (!sc || !rtv || !srv) return false;
	D3D11_MAPPED_SUBRESOURCE m;
	HRESULT hr = ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
	if (FAILED(hr)) { log_hr(hr, "Map(cb)", __FILE__, __LINE__); }
	else {
		CB c = {p.sdr_white_nits, p.peak_nits, p.src_peak_nits, p.highlight_strength, p.highlight_knee, (int)p.transfer,
		        (int)p.tonemap, (int)p.alpha, hdr_presenting ? 1 : 0, split_x, {0, 0}};
		std::memcpy(m.pData, &c, sizeof(c));
		ctx->Unmap(cb, 0);
	}
	D3D11_VIEWPORT vp = {0, 0, (float)w, (float)h, 0, 1};
	ctx->OMSetRenderTargets(1, &rtv, nullptr);
	ctx->RSSetViewports(1, &vp);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->IASetInputLayout(nullptr);
	ctx->VSSetShader(vs, nullptr, 0);
	ctx->PSSetShader(ps, nullptr, 0);
	ctx->PSSetConstantBuffers(0, 1, &cb);
	ctx->PSSetShaderResources(0, 1, &srv);
	ctx->PSSetSamplers(0, 1, &smp);
	ctx->Draw(3, 0);
	hr = sc->Present(interval1 ? 1 : 0, 0);
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
		log_hr(hr, "Present", __FILE__, __LINE__);
		log_hr(dev->GetDeviceRemovedReason(), "GetDeviceRemovedReason", __FILE__, __LINE__);
		device_lost = true;
		return false;
	}
	if (hr == DXGI_STATUS_OCCLUDED) { Sleep(16); return true; }
	if (FAILED(hr)) { log_hr(hr, "Present", __FILE__, __LINE__); return false; }
	return true;
}

void Presenter::destroy() {
	if (ctx) { ctx->ClearState(); ctx->Flush(); }
	release_shared_texture();
	safe_release(rtv);
	safe_release(sc4);
	safe_release(sc3);
	safe_release(sc);
	safe_release(smp);
	safe_release(cb);
	safe_release(ps);
	safe_release(vs);
	safe_release(ctx);
	safe_release(dev);
	safe_release(adapter);
	safe_release(factory);
	format = DXGI_FORMAT_UNKNOWN;
	hdr_presenting = false;
}
