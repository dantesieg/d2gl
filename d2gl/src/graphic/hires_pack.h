/*
	[Graphic] hires_textures: AI-upscaled replacements for glide textures (tools/upscale in median-2026).
	d2gl_hires.pak next to the game; header, index and chunk table are memory-mapped read-only:
	  header  { u32 magic 'D2HR', u32 version 3 (LZMA) | 4 (LZ4), u32 count, u32 scale, u32 chunks }
	  entries { u32 hash, u16 tw, u16 th, u16 fw, u16 fh, u16 flags, u16 pad, u32 chunk, u32 offset }[count],
	          sorted by hash, tw, th; flags bit 0 = frame at the top of the download (else bottom),
	          bit 1 = stored XOR the nearest-neighbour upscale of the (un-remapped) download
	  chunks  { u64 offset, u32 csize, u32 usize }[chunks]
	  data    LZMA chunks (5 props bytes + raw stream) or LZ4 blocks of several frames' scale*fw x scale*fh 8-bit palette indices;
	          an entry's frame is at offset inside its decoded chunk (entries may share frames)
	hash = helpers::hash (MurmurHash3 x86_32, seed 0) of the tw*th glide download.
*/
#pragma once

#include <cstdint>
#include <cstring>
#include <array>
#include <map>
#include <vector>
#include <windows.h>

#include "lzma/LzmaDec.h"
#include "lz4/lz4.h"

namespace d2gl::hires {

#pragma pack(push, 1)
struct PakHeader {
	uint32_t magic, version, count, scale, chunks;
};
struct PakEntry {
	uint32_t hash;
	uint16_t tw, th, fw, fh, flags, pad;
	uint32_t chunk, offset;
};
struct PakChunk {
	uint64_t offset;
	uint32_t csize, usize;
};
#pragma pack(pop)

struct Pack {
	const uint8_t* base = nullptr; // mapped header + index only (32-bit process: blobs are read on demand)
	HANDLE file = INVALID_HANDLE_VALUE;
	const PakEntry* entries = nullptr;
	const PakChunk* chunks = nullptr;
	uint32_t count = 0, scale = 0, nchunks = 0, version = 0;
	uint64_t size = 0;
};

inline Pack g_packs[2]; // 0 = interface (d2gl_hires_ui.pak, [Graphic] hires_ui), 1 = world (d2gl_hires.pak, hires_textures)
inline uint32_t g_scale = 0, g_count = 0; // shared scale of the loaded packs, total entries
inline bool g_on[2] = {}; // packs enabled right now (off applies at once)

// remapped sprites (unit palettes, item tints): the game bakes the remap into the download, so the upload hook
// keys the un-remapped frame and the remap is applied to the replacement's indices
struct Pending {
	bool active = false;
	uint32_t hash = 0, tw = 0, th = 0;
	uint8_t lut[256] = {};
	const uint8_t* src = nullptr; // un-remapped download, valid until the download hook
};
inline Pending g_pending;
struct Remap {
	std::array<uint8_t, 256> lut;
	std::vector<uint8_t> src; // the un-remapped download (XOR-coded entries)
};
inline std::map<uint32_t, Remap> g_luts; // download address -> remap of its replacement
inline uint32_t g_pal[256] = {}; // current glide palette (ARGB), for remapped replacements

// remapped replacement: the upscaler picked indices by their un-remapped colours; where the remap pulls a picked index
// far from the remapped colour of the source pixel (tint ramps that are not monotonic), keep the source pixel
inline void remap(const PakEntry* e, uint8_t* dst, const uint8_t* src, const uint8_t* lut)
{
	auto dist = [](uint32_t a, uint32_t b) {
		const int dr = (int)((a >> 16) & 0xFF) - (int)((b >> 16) & 0xFF), dg = (int)((a >> 8) & 0xFF) - (int)((b >> 8) & 0xFF), db = (int)(a & 0xFF) - (int)(b & 0xFF);
		return dr * dr + dg * dg + db * db;
	};
	const uint32_t s = g_scale, dw = e->tw * s, dh = e->th * s;
	for (uint32_t y = 0; y < dh; y++) {
		const uint8_t* sr = src + (size_t)(y / s) * e->tw;
		uint8_t* d = dst + (size_t)y * dw;
		for (uint32_t x = 0; x < dw; x++) {
			uint8_t q = d[x];
			const uint8_t o = sr[x / s];
			if (!q) {
				continue;
			}
			if (o && q != o && dist(g_pal[lut[q]], g_pal[lut[o]]) > 2 * dist(g_pal[q], g_pal[o]) + 300)
				q = o;
			d[x] = lut[q];
		}
	}
}

inline bool load(int pi, const char* path)
{
	Pack& P = g_packs[pi];
	if (P.base)
		return true;
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER sz = {};
	GetFileSizeEx(f, &sz);
	PakHeader hd = {};
	DWORD got = 0;
	if (!ReadFile(f, &hd, sizeof(hd), &got, nullptr) || got != sizeof(hd) || hd.magic != 0x52483244 || (hd.version != 3 && hd.version != 4) || (hd.scale != 2 && hd.scale != 4) || (g_scale && hd.scale != g_scale)) {
		CloseHandle(f);
		return false;
	}
	const uint64_t need = sizeof(PakHeader) + (uint64_t)hd.count * sizeof(PakEntry) + (uint64_t)hd.chunks * sizeof(PakChunk);
	HANDLE m = need <= (uint64_t)sz.QuadPart ? CreateFileMappingA(f, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
	const uint8_t* p = m ? (const uint8_t*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, (SIZE_T)need) : nullptr;
	if (m)
		CloseHandle(m);
	if (!p) {
		CloseHandle(f);
		return false;
	}
	const PakEntry* ents = (const PakEntry*)(p + sizeof(PakHeader));
	P = { p, f, ents, (const PakChunk*)(ents + hd.count), hd.count, hd.scale, hd.chunks, hd.version, (uint64_t)sz.QuadPart };
	g_scale = hd.scale, g_count += hd.count;
	return true;
}

inline int packOf(const PakEntry* e)
{
	for (int i = 0; i < 2; i++)
		if (g_packs[i].count && e >= g_packs[i].entries && e < g_packs[i].entries + g_packs[i].count)
			return i;
	return -1;
}

// is the pack of this entry enabled right now
inline bool active(const void* e)
{
	const int i = e ? packOf((const PakEntry*)e) : -1;
	return i >= 0 && g_on[i];
}

inline const PakEntry* find(uint32_t hash, uint32_t tw, uint32_t th)
{
	for (int pi = 0; pi < 2; pi++) {
		const Pack& P = g_packs[pi];
		if (!P.count || !g_on[pi])
			continue;
		uint32_t lo = 0, hi = P.count;
		while (lo < hi) {
			const uint32_t mid = (lo + hi) / 2;
			if (P.entries[mid].hash < hash)
				lo = mid + 1;
			else
				hi = mid;
		}
		for (uint32_t i = lo; i < P.count && P.entries[i].hash == hash; i++) {
			const PakEntry& e = P.entries[i];
			if (e.tw == tw && e.th == th && e.fw <= tw && e.fh <= th && e.chunk < P.nchunks)
				return &e;
		}
	}
	return nullptr;
}

// decoded chunk (small LRU: frames of one sprite share a chunk); nullptr on a corrupt chunk
inline const uint8_t* chunkData(int pi, uint32_t ci)
{
	const Pack& P = g_packs[pi];
	ci |= (uint32_t)pi << 31; // slot key
	struct Slot {
		uint32_t chunk = UINT32_MAX, tick = 0;
		std::vector<uint8_t> data;
	};
	static Slot slots[64];
	static uint32_t tick = 0;
	Slot* lru = &slots[0];
	for (auto& sl : slots) {
		if (sl.chunk == ci) {
			sl.tick = ++tick;
			return sl.data.data();
		}
		if (sl.tick < lru->tick)
			lru = &sl;
	}
	const PakChunk& c = P.chunks[ci & 0x7FFFFFFF];
	if (c.csize < 1 || c.offset + c.csize > P.size)
		return nullptr;
	static std::vector<uint8_t> comp;
	comp.resize(c.csize);
	OVERLAPPED ov = {};
	ov.Offset = (DWORD)(c.offset & 0xFFFFFFFF), ov.OffsetHigh = (DWORD)(c.offset >> 32);
	DWORD got = 0;
	if (!ReadFile(P.file, comp.data(), c.csize, &got, &ov) || got != c.csize)
		return nullptr;
	lru->chunk = UINT32_MAX;
	lru->data.resize(c.usize);
	if (P.version == 4) {
		if (LZ4_decompress_safe((const char*)comp.data(), (char*)lru->data.data(), (int)c.csize, (int)c.usize) != (int)c.usize)
			return nullptr;
		lru->chunk = ci, lru->tick = ++tick;
		return lru->data.data();
	}
	if (c.csize < 5)
		return nullptr;
	static const ISzAlloc alloc = { [](ISzAllocPtr, size_t n) -> void* { return malloc(n); }, [](ISzAllocPtr, void* p) { free(p); } };
	SizeT dlen = c.usize, slen = c.csize - 5;
	ELzmaStatus st;
	if (LzmaDecode(lru->data.data(), &dlen, comp.data() + 5, &slen, comp.data(), 5, LZMA_FINISH_ANY, &st, &alloc) != SZ_OK || dlen != c.usize)
		return nullptr;
	lru->chunk = ci, lru->tick = ++tick;
	return lru->data.data();
}

// expands an entry into dst (scale*tw x scale*th, zero-filled around the frame); false on a corrupt chunk
inline bool expand(const PakEntry* e, uint8_t* dst)
{
	const int pi = packOf(e);
	if (pi < 0)
		return false;
	const uint32_t s = g_scale, w = e->fw * s, h = e->fh * s, dw = e->tw * s, dh = e->th * s;
	if ((uint64_t)e->offset + (uint64_t)w * h > g_packs[pi].chunks[e->chunk].usize)
		return false;
	const uint8_t* src = chunkData(pi, e->chunk);
	if (!src)
		return false;
	src += e->offset;
	memset(dst, 0, (size_t)dw * dh);
	const uint32_t y0 = (e->flags & 1) ? 0 : dh - h;
	for (uint32_t y = 0; y < h; y++)
		memcpy(dst + (size_t)(y0 + y) * dw, src + (size_t)y * w, w);
	return true;
}

// undoes XOR coding: dst (scale*tw x scale*th) ^= nearest-neighbour upscale of the tw x th download
inline void unxor(const PakEntry* e, uint8_t* dst, const uint8_t* src)
{
	const uint32_t s = g_scale, dw = e->tw * s, dh = e->th * s;
	for (uint32_t y = 0; y < dh; y++) {
		const uint8_t* sr = src + (size_t)(y / s) * e->tw;
		uint8_t* d = dst + (size_t)y * dw;
		for (uint32_t x = 0; x < dw; x++)
			d[x] ^= sr[x / s];
	}
}

}
