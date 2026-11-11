/*
	[Graphic] hires_textures: AI-upscaled replacements for glide textures (tools/upscale in median-2026).
	d2gl_hires.pak next to the dll, memory-mapped read-only:
	  header  { u32 magic 'D2HR', u32 version 1, u32 count, u32 scale }
	  entries { u32 hash, u16 w, u16 h, u32 offset, u32 bytes }[count], sorted by hash, then w, h
	  data    scale*w x scale*h 8-bit palette indices per entry (same layout as the glide download)
	hash = helpers::hash (MurmurHash3 x86_32, seed 0) of the w*h glide download.
*/
#pragma once

#include <cstdint>
#include <windows.h>

namespace d2gl::hires {

#pragma pack(push, 1)
struct PakHeader {
	uint32_t magic, version, count, scale;
};
struct PakEntry {
	uint32_t hash;
	uint16_t w, h;
	uint32_t offset, bytes;
};
#pragma pack(pop)

struct Pack {
	const uint8_t* base = nullptr;
	const PakEntry* entries = nullptr;
	uint32_t count = 0, scale = 0;
	size_t size = 0;
};

inline Pack g_pack;

inline bool load(const char* path)
{
	if (g_pack.base)
		return true;
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER sz = {};
	GetFileSizeEx(f, &sz);
	HANDLE m = sz.QuadPart > (LONGLONG)sizeof(PakHeader) ? CreateFileMappingA(f, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
	CloseHandle(f);
	if (!m)
		return false;
	const uint8_t* p = (const uint8_t*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
	CloseHandle(m);
	if (!p)
		return false;
	const PakHeader* h = (const PakHeader*)p;
	const size_t need = sizeof(PakHeader) + (size_t)h->count * sizeof(PakEntry);
	if (h->magic != 0x52483244 || h->version != 1 || (h->scale != 2 && h->scale != 4) || need > (size_t)sz.QuadPart) {
		UnmapViewOfFile(p);
		return false;
	}
	g_pack = { p, (const PakEntry*)(p + sizeof(PakHeader)), h->count, h->scale, (size_t)sz.QuadPart };
	return true;
}

// replacement pixels for a w x h download with this hash, or nullptr
inline const uint8_t* find(uint32_t hash, uint32_t w, uint32_t h)
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
		if (e.w == w && e.h == h && (size_t)e.offset + e.bytes <= g_pack.size && e.bytes == (size_t)w * h * g_pack.scale * g_pack.scale)
			return g_pack.base + e.offset;
	}
	return nullptr;
}

}
