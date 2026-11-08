// Compiles D2GL's HLSL (hdr_shader.h) with D3DCompile, runs it on a D3D11 device into R10G10B10A2 and
// compares every pixel with hdr_color.h::encode_pixel. Windows/Wine (needs a D3D11 device: d2m-wine-x).
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "../../d2gl/src/graphic/hdr/hdr_shader.h"

using namespace d2gl::hdr;
template <class T> static void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

int main()
{
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) {
		std::printf("D3D11CreateDevice failed\n");
		return 10;
	}
	HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
	pD3DCompile compile = dc ? (pD3DCompile)(void*)GetProcAddress(dc, "D3DCompile") : nullptr;
	if (!compile) { std::printf("no D3DCompile\n"); return 11; }
	ID3DBlob *vsb = nullptr, *psb = nullptr, *err = nullptr;
	HRESULT hr = compile(kHdrHlsl, sizeof(kHdrHlsl) - 1, "d2gl_hdr", nullptr, nullptr, "vs_main", "vs_4_0", 0, 0, &vsb, &err);
	if (err) { std::printf("vs: %s\n", (const char*)err->GetBufferPointer()); rel(err); }
	if (SUCCEEDED(hr)) hr = compile(kHdrHlsl, sizeof(kHdrHlsl) - 1, "d2gl_hdr", nullptr, nullptr, "ps_main", "ps_4_0", 0, 0, &psb, &err);
	if (err) { std::printf("ps: %s\n", (const char*)err->GetBufferPointer()); rel(err); }
	if (FAILED(hr)) { std::printf("compile failed 0x%08lX\n", (unsigned long)hr); return 12; }
	std::printf("HLSL compiled: vs %u B, ps %u B\n", (unsigned)vsb->GetBufferSize(), (unsigned)psb->GetBufferSize());
	ID3D11VertexShader* vs = nullptr;
	ID3D11PixelShader* ps = nullptr;
	dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
	dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);

	const int W = 64, H = 4;
	uint8_t in[H][W][4];
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x) {
			uint8_t* c = in[y][x];
			if (x < 32) c[0] = c[1] = c[2] = (uint8_t)(x * 255 / 31);                       // grey ramp
			else if (x < 40) { c[0] = (x & 1) ? 255 : 0; c[1] = (x & 2) ? 255 : 0; c[2] = (x & 4) ? 255 : 0; }
			else if (x < 48) { c[0] = (uint8_t)(200 - (x - 40) * 10); c[1] = 120; c[2] = (uint8_t)(40 + (x - 40) * 5); }
			else c[0] = c[1] = c[2] = (uint8_t)(x - 48);                                 // codes 0..15
			c[3] = 255;
			if (y == 1) { c[0] = (uint8_t)(c[0] / 2 + 60); }                               // vary rows
			if (y == 2) { c[1] = (uint8_t)(255 - c[1]); }
		}
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1; td.SampleDesc.Count = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = {in, W * 4, 0};
	ID3D11Texture2D *tin = nullptr, *tout = nullptr, *tst = nullptr;
	ID3D11ShaderResourceView* srv = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	dev->CreateTexture2D(&td, &sd, &tin);
	dev->CreateShaderResourceView(tin, nullptr, &srv);
	td.Format = DXGI_FORMAT_R10G10B10A2_UNORM; td.BindFlags = D3D11_BIND_RENDER_TARGET;
	dev->CreateTexture2D(&td, nullptr, &tout);
	dev->CreateRenderTargetView(tout, nullptr, &rtv);
	td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	dev->CreateTexture2D(&td, nullptr, &tst);
	D3D11_BUFFER_DESC bd = {sizeof(ShaderConstants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
	ID3D11Buffer* cb = nullptr;
	dev->CreateBuffer(&bd, nullptr, &cb);
	D3D11_SAMPLER_DESC smd = {};
	smd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	smd.AddressU = smd.AddressV = smd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	ID3D11SamplerState* smp = nullptr;
	dev->CreateSamplerState(&smd, &smp);

	struct Case { const char* name; Params p; bool flip; };
	Case cases[] = {
		{"defaults (200 nits, gamma2.2, BT.2390)", Params{}, false},
		{"flip_y", Params{}, true},
		{"gamma 2.4 + clip", [] { Params p; p.transfer = Transfer::GAMMA24; p.tonemap = Tonemap::CLIP; return p; }(), false},
		{"sRGB piecewise", [] { Params p; p.transfer = Transfer::SRGB_PIECEWISE; return p; }(), false},
		{"saturation 1.4", [] { Params p; p.saturation = 1.4f; return p; }(), false},
		{"saturation 0.6", [] { Params p; p.saturation = 0.6f; return p; }(), false},
		{"black +0.04", [] { Params p; p.black_level = 0.04f; return p; }(), false},
		{"black -0.04", [] { Params p; p.black_level = -0.04f; return p; }(), false},
		{"contrast 1.15", [] { Params p; p.contrast = 1.15f; return p; }(), false},
		{"contrast 0.85", [] { Params p; p.contrast = 0.85f; return p; }(), false},
		{"brightness +1.5 stops, peak 600", [] { Params p; p.brightness_stops = 1.5f; p.peak_nits = 600; return p; }(), false},
		{"highlight 1.0 knee .6, peak 1015", [] { Params p; p.highlight_strength = 1.0f; p.highlight_knee = 0.6f; p.peak_nits = 1015; return p; }(), false},
		{"all controls", [] { Params p; p.sdr_white_nits = 300; p.peak_nits = 800; p.brightness_stops = -0.5f; p.black_level = 0.02f;
			p.contrast = 1.1f; p.saturation = 1.2f; p.highlight_strength = 0.5f; p.transfer = Transfer::GAMMA24; return p; }(), false},
	};
	int total_bad = 0;
	for (const Case& cs : cases) {
		ShaderConstants k = makeConstants(cs.p, 1, false, cs.flip, (float)W, (float)H);
		D3D11_MAPPED_SUBRESOURCE m;
		ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
		memcpy(m.pData, &k, sizeof(k));
		ctx->Unmap(cb, 0);
		D3D11_VIEWPORT vp = {0, 0, (float)W, (float)H, 0, 1};
		ctx->OMSetRenderTargets(1, &rtv, nullptr);
		ctx->RSSetViewports(1, &vp);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->VSSetShader(vs, nullptr, 0);
		ctx->PSSetShader(ps, nullptr, 0);
		ctx->PSSetConstantBuffers(0, 1, &cb);
		ctx->PSSetShaderResources(0, 1, &srv);
		ctx->PSSetSamplers(0, 1, &smp);
		ctx->Draw(3, 0);
		ctx->CopyResource(tst, tout);
		if (FAILED(ctx->Map(tst, 0, D3D11_MAP_READ, 0, &m))) { std::printf("map failed\n"); return 13; }
		int bad = 0, maxd = 0;
		for (int y = 0; y < H; ++y) {
			const uint32_t* px = (const uint32_t*)((const char*)m.pData + (size_t)y * m.RowPitch);
			const int sy = cs.flip ? H - 1 - y : y;
			for (int x = 0; x < W; ++x) {
				const uint8_t* c = in[sy][x];
				float3 ref = encode_pixel({c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f}, cs.p);
				int rr = (int)std::lround(ref.r * 1023), rg = (int)std::lround(ref.g * 1023), rb = (int)std::lround(ref.b * 1023);
				int gr = (int)(px[x] & 1023), gg = (int)((px[x] >> 10) & 1023), gb = (int)((px[x] >> 20) & 1023);
				int d = std::max({std::abs(rr - gr), std::abs(rg - gg), std::abs(rb - gb)});
				maxd = std::max(maxd, d);
				if (d > 1) {
					if (bad < 3) std::printf("  mismatch x=%d y=%d gpu=(%d,%d,%d) ref=(%d,%d,%d)\n", x, y, gr, gg, gb, rr, rg, rb);
					++bad;
				}
			}
		}
		ctx->Unmap(tst, 0);
		std::printf("[%s] %-40s max |gpu-ref| = %d 10-bit codes (%d px)\n", bad ? "FAIL" : "PASS", cs.name, maxd, W * H);
		total_bad += bad;
	}
	std::printf("\n%s: %d mismatching pixels (> 1 code)\n", total_bad ? "FAIL" : "PASS", total_bad);
	return total_bad ? 1 : 0;
}
