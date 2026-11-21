/*
	[Graphic] hires_textures: AI-upscaled replacements for glide textures (tools/upscale in median-2026).
	d2gl_hires.pak next to the game; header, index and chunk table are memory-mapped read-only:
	  header  { u32 magic 'D2HR', u32 version 3 (LZMA) | 4 (LZ4) | 5 (LZ4, full colour BC7), u32 count, u32 scale, u32 chunks }
	  entries { u32 hash, u16 tw, u16 th, u16 fw, u16 fh, u16 flags, u16 pad, u32 chunk, u32 offset }[count],
	          sorted by hash, tw, th; flags bit 0 = frame at the top of the download (else bottom),
	          bit 1 = stored XOR the nearest-neighbour upscale of the (un-remapped) download,
	          bits 3-5 (v5) = act palette the colours were made with (0 = act 1; see hires_bake_pal.h)
	  chunks  { u64 offset, u32 csize, u32 usize }[chunks]
	  data    LZMA chunks (5 props bytes + raw stream) or LZ4 blocks of several frames' scale*fw x scale*fh 8-bit palette indices
	          (v5: BC7 RGBA blocks of the entry's region of the scale x download, bc7Region: smooth alpha edges);
	          an entry's frame is at offset inside its decoded chunk (entries may share frames)
	hash = helpers::hash (MurmurHash3 x86_32, seed 0) of the tw*th glide download.
*/
#pragma once

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_set>
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

constexpr int NPACK = 4;
inline Pack g_packs[NPACK]; // 0 = interface (d2gl_hires_ui.pak, [Graphic] hires_ui), 1 = world (d2gl_hires.pak, hires_textures),
                           // 2 = items (d2gl_hires_items.pak, hires_items), 3 = extra interface sprites of our own
                           // (d2gl_hires_extra.pak: MedianRevived PanelStyle art; follows hires_ui)
inline uint32_t g_scale = 0, g_count = 0; // shared scale of the loaded packs, total entries
inline int g_bc7 = -1; // -1 = no pack yet, 0 = palette-index packs (v3/v4), 1 = full-colour BC7 packs (v5); one kind at a time
inline bool g_on[NPACK] = {}; // packs enabled right now (off applies at once)
// diagnostics for the periodic hires_textures log line (cumulative): unmatched downloads shaped like a floor tile (256x128)
// or wall block (32x32) outside the sprite upload hook, matches not uploaded for lack of room in the frame's hi-res arena or
// queue (retried next frame), matches dropped as corrupt / size mismatch, retries, chunk decodes (chunk cache misses)
inline uint32_t g_miss_tile = 0, g_no_room = 0, g_bad = 0, g_retry = 0, g_chunk_loads = 0;

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
	const uint32_t s = g_scale, sh = s == 4 ? 2 : 1, dw = e->tw * s, dh = e->th * s; // scale is 2 or 4 (load): shifts, not divides per pixel
	for (uint32_t y = 0; y < dh; y++) {
		const uint8_t* sr = src + (size_t)(y >> sh) * e->tw;
		uint8_t* d = dst + (size_t)y * dw;
		for (uint32_t x = 0; x < dw; x++) {
			uint8_t q = d[x];
			const uint8_t o = sr[x >> sh];
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
	if (!ReadFile(f, &hd, sizeof(hd), &got, nullptr) || got != sizeof(hd) || hd.magic != 0x52483244 || (hd.version < 3 || hd.version > 5) || (hd.scale != 2 && hd.scale != 4) || (g_scale && hd.scale != g_scale) ||
		(g_bc7 >= 0 && g_bc7 != (hd.version == 5 ? 1 : 0))) {
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
	g_scale = hd.scale, g_count += hd.count, g_bc7 = hd.version == 5 ? 1 : 0;
	return true;
}

inline int packOf(const PakEntry* e)
{
	for (int i = 0; i < NPACK; i++)
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
	// interface (0) and items (2) before world (1): the world pack also holds nearly every interface and item frame, so
	// searched first it would claim them and only hires_textures would switch them. Disabled packs still claim their
	// frames; active() hides them at draw time, so each pack follows its own setting.
	static constexpr int order[NPACK] = { 0, 3, 2, 1 };
	for (int pi : order) {
		const Pack& P = g_packs[pi];
		if (!P.count || (pi == 1 && !g_on[pi]))
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

// [Graphic] hires_tint: d2gl_hires_mask.idx (tools/upscale/dumpmatch.c) maps the shape of a frame's download, the
// MurmurHash3 of its binary mask (index > 0 -> 1, tw*th bytes), to the plain hash of one pack frame with that shape:
// header { u32 'D2HM', u32 1, u32 count }, records { u32 maskhash, u16 tw, u16 th, u32 hash } sorted by maskhash, tw, th.
// It finds the replacement of a colour-remapped frame the game draws outside the upload hook (remap baked in, plain
// hash unknown); the shader takes only alpha and detail from it and the colours from the download (v_TexIds.y = 9).
#pragma pack(push, 1)
struct MaskRec {
	uint32_t mh;
	uint16_t tw, th;
	uint32_t hash;
};
#pragma pack(pop)
inline const MaskRec* g_mask = nullptr;
inline uint32_t g_mask_count = 0;

inline bool loadMask(const char* path)
{
	if (g_mask)
		return true;
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER sz = {};
	GetFileSizeEx(f, &sz);
	HANDLE m = sz.QuadPart >= 12 ? CreateFileMappingA(f, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
	CloseHandle(f);
	const uint32_t* p = m ? (const uint32_t*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0) : nullptr;
	if (m)
		CloseHandle(m);
	if (!p)
		return false;
	if (p[0] != 0x4D483244 || p[1] != 1 || 12 + (uint64_t)p[2] * sizeof(MaskRec) > (uint64_t)sz.QuadPart) {
		UnmapViewOfFile(p);
		return false;
	}
	g_mask = (const MaskRec*)(p + 3), g_mask_count = p[2];
	return true;
}

// the pack entry of a frame shaped like the download (mask hash mh), or nullptr
inline const PakEntry* findMask(uint32_t mh, uint32_t tw, uint32_t th)
{
	uint32_t lo = 0, hi = g_mask_count;
	while (lo < hi) {
		const uint32_t mid = (lo + hi) / 2;
		const MaskRec& r = g_mask[mid];
		if (r.mh < mh || (r.mh == mh && (r.tw < tw || (r.tw == tw && r.th < th))))
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo < g_mask_count && g_mask[lo].mh == mh && g_mask[lo].tw == tw && g_mask[lo].th == th ? find(g_mask[lo].hash, tw, th) : nullptr;
}

// [Graphic] hires_async: chunk reads and decodes on a worker thread; a draw whose chunk is not decoded yet uses the
// original this frame and is retried the next (g_chunk_wait). Off: decoded on the game thread at first use.
inline bool g_async = false;
inline bool g_chunk_wait = false; // last withChunk() missed because the chunk is still being loaded (not corrupt)

// reads and decodes chunk key k (pack in bits 30-31) into out; false on a corrupt chunk
inline bool loadChunk(uint32_t k, std::vector<uint8_t>& out, std::vector<uint8_t>& comp)
{
	const Pack& P = g_packs[k >> 30];
	const PakChunk& c = P.chunks[k & 0x3FFFFFFF];
	if (c.csize < 1 || c.offset + c.csize > P.size)
		return false;
	comp.resize(c.csize);
	OVERLAPPED ov = {}; // positional read: safe from two threads on one synchronous handle
	ov.Offset = (DWORD)(c.offset & 0xFFFFFFFF), ov.OffsetHigh = (DWORD)(c.offset >> 32);
	DWORD got = 0;
	if (!ReadFile(P.file, comp.data(), c.csize, &got, &ov) || got != c.csize)
		return false;
	out.resize(c.usize);
	if (P.version >= 4)
		return LZ4_decompress_safe((const char*)comp.data(), (char*)out.data(), (int)c.csize, (int)c.usize) == (int)c.usize;
	if (c.csize < 5)
		return false;
	static const ISzAlloc alloc = { [](ISzAllocPtr, size_t n) -> void* { return malloc(n); }, [](ISzAllocPtr, void* p) { free(p); } };
	SizeT dlen = c.usize, slen = c.csize - 5;
	ELzmaStatus st;
	return LzmaDecode(out.data(), &dlen, comp.data() + 5, &slen, comp.data(), 5, LZMA_FINISH_ANY, &st, &alloc) == SZ_OK && dlen == c.usize;
}

// decoded chunks, LRU (frames of one sprite share a chunk). The old 64 slots (~7 MB of v5 chunks) reloaded ~160 chunks/s;
// now as many slots as fit BUDGET bytes of average chunks, 64..N (32-bit process: v4 chunks are bigger)
struct ChunkCache {
	struct Slot {
		uint32_t chunk = UINT32_MAX, tick = 0;
		std::vector<uint8_t> data;
	};
	static constexpr int N = 512, MAXQ = 32;
	static constexpr uint64_t BUDGET = 96ull << 20;
	Slot slots[N];
	int n = 0; // slots in use, set at first use from the loaded packs
	uint32_t tick = 0;
	std::mutex m; // async only: slots, queue, failed
	std::condition_variable cv;
	std::deque<uint32_t> queue;
	std::unordered_set<uint32_t> queued, failed;
	bool started = false;

	void size()
	{
		uint64_t bytes = 0, count = 0;
		for (const auto& P : g_packs)
			for (uint32_t i = 0; i < P.nchunks; i++)
				bytes += P.chunks[i].usize, count++;
		const uint64_t avg = count ? bytes / count : 1;
		n = (int)std::clamp<uint64_t>(BUDGET / (avg ? avg : 1), 64, N);
	}
	Slot* find(uint32_t k)
	{
		if (!n)
			size();
		for (int i = 0; i < n; i++)
			if (slots[i].chunk == k)
				return slots[i].tick = ++tick, &slots[i];
		return nullptr;
	}
	Slot* lru()
	{
		Slot* r = &slots[0];
		for (int i = 1; i < n; i++)
			if (slots[i].tick < r->tick)
				r = &slots[i];
		return r;
	}
};
inline ChunkCache g_cc;

inline void chunkWorker()
{
	std::vector<uint8_t> buf, comp;
	for (;;) {
		uint32_t k;
		{
			std::unique_lock lk(g_cc.m);
			g_cc.cv.wait(lk, [] { return !g_cc.queue.empty(); });
			k = g_cc.queue.front();
			g_cc.queue.pop_front();
		}
		const bool ok = loadChunk(k, buf, comp);
		std::lock_guard lk(g_cc.m);
		g_cc.queued.erase(k);
		if (!ok) {
			g_cc.failed.insert(k);
			continue;
		}
		auto* sl = g_cc.lru();
		std::swap(sl->data, buf);
		sl->chunk = k, sl->tick = ++g_cc.tick;
		g_chunk_loads++;
	}
}

// calls f(decoded chunk) and returns true; false on a corrupt chunk, or (async) while the chunk is still loading
// (g_chunk_wait set). f runs under the cache lock in async mode: keep it a short copy.
template <class F>
inline bool withChunk(int pi, uint32_t ci, F&& f)
{
	g_chunk_wait = false;
	if (ci >= g_packs[pi].nchunks)
		return false;
	const uint32_t k = ci | (uint32_t)pi << 30; // slot key (pack in bits 30-31: << 31 dropped pack 2, its chunks hit pack 0's cache)
	if (!g_async) {
		if (auto* sl = g_cc.find(k))
			return f(sl->data.data()), true;
		static std::vector<uint8_t> comp;
		auto* sl = g_cc.lru();
		sl->chunk = UINT32_MAX;
		if (!loadChunk(k, sl->data, comp))
			return false;
		sl->chunk = k, sl->tick = ++g_cc.tick;
		g_chunk_loads++;
		return f(sl->data.data()), true;
	}
	std::unique_lock lk(g_cc.m);
	if (auto* sl = g_cc.find(k))
		return f(sl->data.data()), true;
	if (g_cc.failed.count(k))
		return false;
	if (!g_cc.queued.count(k) && g_cc.queue.size() < ChunkCache::MAXQ) { // full queue: asked again on the retry
		g_cc.queued.insert(k);
		g_cc.queue.push_back(k);
		if (!g_cc.started)
			g_cc.started = true, std::thread(chunkWorker).detach();
		g_cc.cv.notify_one();
	}
	g_chunk_wait = true;
	return false;
}

// expands an entry into dst (scale*tw x scale*th, zero-filled around the frame); false on a corrupt chunk
inline bool expand(const PakEntry* e, uint8_t* dst)
{
	g_chunk_wait = false;
	const int pi = packOf(e);
	if (pi < 0)
		return false;
	const uint32_t s = g_scale, w = e->fw * s, h = e->fh * s, dw = e->tw * s, dh = e->th * s;
	if ((uint64_t)e->offset + (uint64_t)w * h > g_packs[pi].chunks[e->chunk].usize)
		return false;
	return withChunk(pi, e->chunk, [&](const uint8_t* src) {
		src += e->offset;
		memset(dst, 0, (size_t)dw * dh);
		const uint32_t y0 = (e->flags & 1) ? 0 : dh - h;
		for (uint32_t y = 0; y < h; y++)
			memcpy(dst + (size_t)(y0 + y) * dw, src + (size_t)y * w, w);
	});
}

// v5: an entry's BC7 region inside its scale x download: block columns, first block row, block rows
// (same formula as tools/upscale build_pack.py bc7_region)
struct Bc7Region { uint32_t bw, by, bh; };
inline Bc7Region bc7Region(const PakEntry* e)
{
	const uint32_t s = g_scale, y0 = (e->flags & 1) ? 0 : s * (e->th - e->fh), ys = y0 & ~3u;
	return { (s * e->fw + 3) / 4, ys / 4, (((s * e->th + 3) & ~3u) - ys) / 4 };
}
// bytes of the whole download in BC7 (scale x, padded to 4x4 blocks)
inline size_t bc7Size(uint32_t tw, uint32_t th) { return (size_t)((g_scale * tw + 3) / 4) * ((g_scale * th + 3) / 4) * 16; }

// v5: the whole download as BC7 into dst (bc7Size bytes): the entry's region, zero blocks (transparent) elsewhere
inline bool expandBc7(const PakEntry* e, uint8_t* dst)
{
	g_chunk_wait = false;
	const int pi = packOf(e);
	if (pi < 0)
		return false;
	const Bc7Region r = bc7Region(e);
	const uint32_t dbw = (g_scale * e->tw + 3) / 4, dbh = (g_scale * e->th + 3) / 4;
	if (r.bw > dbw || r.by + r.bh > dbh || (uint64_t)e->offset + (uint64_t)r.bw * r.bh * 16 > g_packs[pi].chunks[e->chunk].usize)
		return false;
	return withChunk(pi, e->chunk, [&](const uint8_t* src) {
		src += e->offset;
		memset(dst, 0, (size_t)dbw * dbh * 16);
		for (uint32_t y = 0; y < r.bh; y++)
			memcpy(dst + ((size_t)(r.by + y) * dbw) * 16, src + (size_t)y * r.bw * 16, (size_t)r.bw * 16);
	});
}

// undoes XOR coding: dst (scale*tw x scale*th) ^= nearest-neighbour upscale of the tw x th download
inline void unxor(const PakEntry* e, uint8_t* dst, const uint8_t* src)
{
	const uint32_t s = g_scale, sh = s == 4 ? 2 : 1, dw = e->tw * s, dh = e->th * s; // scale is 2 or 4 (load)
	for (uint32_t y = 0; y < dh; y++) {
		const uint8_t* sr = src + (size_t)(y >> sh) * e->tw;
		uint8_t* d = dst + (size_t)y * dw;
		for (uint32_t x = 0; x < dw; x++)
			d[x] ^= sr[x >> sh];
	}
}

}
