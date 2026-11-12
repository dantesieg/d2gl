/*
	[Graphic] hires_textures: AI-upscaled replacements for glide textures (tools/upscale in median-2026).
	d2gl_hires.pak next to the game, memory-mapped read-only:
	  header  { u32 magic 'D2HR', u32 version 2, u32 count, u32 scale }
	  entries { u32 hash, u16 tw, u16 th, u16 fw, u16 fh, u16 flags, u16 pad, u64 offset, u32 csize }[count],
	          sorted by hash, tw, th; flags bit 0 = frame at the top of the download (else bottom)
	  data    zlib(scale*fw x scale*fh 8-bit palette indices); several entries may share one blob
	hash = helpers::hash (MurmurHash3 x86_32, seed 0) of the tw*th glide download (frame at x 0).
*/
#pragma once

#include <cstdint>
#include <cstring>
#include <array>
#include <map>
#include <vector>
#include <windows.h>

extern "C" int stbi_zlib_decode_buffer(char* obuffer, int olen, char const* ibuffer, int ilen);

namespace d2gl::hires {

#pragma pack(push, 1)
struct PakHeader {
	uint32_t magic, version, count, scale;
};
struct PakEntry {
	uint32_t hash;
	uint16_t tw, th, fw, fh, flags, pad;
	uint64_t offset;
	uint32_t csize;
};
#pragma pack(pop)

struct Pack {
	const uint8_t* base = nullptr; // mapped header + index only (32-bit process: blobs are read on demand)
	HANDLE file = INVALID_HANDLE_VALUE;
	const PakEntry* entries = nullptr;
	uint32_t count = 0, scale = 0;
	uint64_t size = 0;
};

inline Pack g_pack;

// remapped sprites (unit palettes, item tints): the game bakes the remap into the download, so the upload hook
// keys the un-remapped frame and the remap is applied to the replacement's indices
struct Pending {
	bool active = false;
	uint32_t hash = 0, tw = 0, th = 0;
	uint8_t lut[256] = {};
};
inline Pending g_pending;
inline std::map<uint32_t, std::array<uint8_t, 256>> g_luts; // download address -> remap of its replacement

inline bool load(const char* path)
{
	if (g_pack.base)
		return true;
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER sz = {};
	GetFileSizeEx(f, &sz);
	PakHeader hd = {};
	DWORD got = 0;
	if (!ReadFile(f, &hd, sizeof(hd), &got, nullptr) || got != sizeof(hd) || hd.magic != 0x52483244 || hd.version != 2 || (hd.scale != 2 && hd.scale != 4)) {
		CloseHandle(f);
		return false;
	}
	const uint64_t need = sizeof(PakHeader) + (uint64_t)hd.count * sizeof(PakEntry);
	HANDLE m = need <= (uint64_t)sz.QuadPart ? CreateFileMappingA(f, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
	const uint8_t* p = m ? (const uint8_t*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, (SIZE_T)need) : nullptr;
	if (m)
		CloseHandle(m);
	if (!p) {
		CloseHandle(f);
		return false;
	}
	g_pack = { p, f, (const PakEntry*)(p + sizeof(PakHeader)), hd.count, hd.scale, (uint64_t)sz.QuadPart };
	return true;
}

inline const PakEntry* find(uint32_t hash, uint32_t tw, uint32_t th)
{
	if (!g_pack.count)
		return nullptr;
	uint32_t lo = 0, hi = g_pack.count;
	while (lo < hi) {
		const uint32_t mid = (lo + hi) / 2;
		if (g_pack.entries[mid].hash < hash)
			lo = mid + 1;
		else
			hi = mid;
	}
	for (uint32_t i = lo; i < g_pack.count && g_pack.entries[i].hash == hash; i++) {
		const PakEntry& e = g_pack.entries[i];
		if (e.tw == tw && e.th == th && e.fw <= tw && e.fh <= th && e.offset + e.csize <= g_pack.size)
			return &e;
	}
	return nullptr;
}

// expands an entry into dst (scale*tw x scale*th, zero-filled around the frame); false on a corrupt blob
inline bool expand(const PakEntry* e, uint8_t* dst)
{
	const uint32_t s = g_pack.scale, w = e->fw * s, h = e->fh * s, dw = e->tw * s, dh = e->th * s;
	static thread_local std::vector<uint8_t> tmp, comp;
	tmp.resize((size_t)w * h);
	comp.resize(e->csize);
	OVERLAPPED ov = {};
	ov.Offset = (DWORD)(e->offset & 0xFFFFFFFF), ov.OffsetHigh = (DWORD)(e->offset >> 32);
	DWORD got = 0;
	if (!ReadFile(g_pack.file, comp.data(), e->csize, &got, &ov) || got != e->csize)
		return false;
	if (stbi_zlib_decode_buffer((char*)tmp.data(), (int)tmp.size(), (const char*)comp.data(), (int)e->csize) != (int)tmp.size())
		return false;
	memset(dst, 0, (size_t)dw * dh);
	const uint32_t y0 = (e->flags & 1) ? 0 : dh - h;
	for (uint32_t y = 0; y < h; y++)
		memcpy(dst + (size_t)(y0 + y) * dw, tmp.data() + (size_t)y * w, w);
	return true;
}

}
