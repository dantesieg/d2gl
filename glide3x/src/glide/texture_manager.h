/*
	D2GL: Diablo 2 LoD Glide/DDraw to OpenGL Wrapper.
	Copyright (C) 2023  Bayaraa

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "graphic/texture.h"

namespace d2gl {

struct SubTextureInfo {
	uint8_t shift;
	uint16_t tex_num;
	glm::vec<2, uint16_t> offset;
};

struct TextureCache {
	uint32_t last_used_frame = 0;
	std::unordered_map<uint32_t, uint16_t> items;
};

struct TextureManagerData {
	uint16_t tex_count = 0;
	std::vector<SubTextureInfo> sub_texure_info;
	std::unordered_map<uint16_t, bool> available;
	std::unordered_map<uint32_t, TextureCache> cache;
	// [Graphic] glide_global_cache: slots keyed by content (hash, width, height) instead of by glide address, so the same
	// sprite downloaded again at another address reuses its slot; least recently used slots are evicted in batches
	struct GlobalItem {
		uint16_t id;
		uint32_t last_used_frame;
	};
	std::unordered_map<uint32_t, GlobalItem> global;
};

typedef std::vector<std::pair<uint16_t, uint16_t>> SubTextureCounts;

// one entry per glide download address (one hash lookup per draw instead of several)
struct GlideSlot {
	uint32_t hash = 0;
	const void* hires = nullptr; // [Graphic] hires_textures: hires::PakEntry of the download at this address
	const void* retry = nullptr; // match not uploaded for lack of room: entry, retried the next frame
	uint32_t retry_frame = 0;
	uint16_t w = 0, h = 0; // size of the last download (glide capture snapshot)
};

struct GlideTexture {
	uint8_t* memory = nullptr;
	std::unordered_map<uint32_t, GlideSlot> slots;
};

extern GlideTexture g_glide_texture;

class TextureManager {
	std::map<uint16_t, TextureManagerData> m_data;
	SubTextureCounts m_size_counts;

	uint32_t upload(uint32_t address, uint16_t width, uint16_t height, uint32_t frame_count, const SubTextureInfo* info, uint32_t hash, GlideSlot* hi);
	static void evictGlobal(TextureManagerData& data, uint32_t frame_count);

public:
	TextureManager(const SubTextureCounts& size_counts);
	~TextureManager() = default;

	inline size_t getUsage(uint16_t size) { return m_data[size].tex_count - m_data[size].available.size(); }

	const SubTextureInfo* getSubTextureInfo(uint32_t address, uint16_t size, uint16_t width, uint16_t height, uint32_t frame_count, const GlideSlot** slot_out = nullptr);
	void clearCache();
};

}