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

#include "pch.h"
#include "texture_manager.h"
#include "graphic/hires_pack.h"

namespace d2gl {

GlideTexture g_glide_texture;

TextureManager::TextureManager(const SubTextureCounts& size_counts)
	: m_size_counts(size_counts)
{
	uint16_t tex_start = 0;

	for (auto& size_count : size_counts) {
		auto size = size_count.first;
		auto count = size_count.second;

		uint16_t div = 512 / size;
		uint16_t tex_count = count * div * div;

		uint8_t shift = 0;
		switch (size) {
			case 8: shift = 5; break;
			case 16: shift = 4; break;
			case 32: shift = 3; break;
			case 64: shift = 2; break;
			case 128: shift = 1; break;
		}

		m_data.insert({ size, {} });
		m_data[size].sub_texure_info.assign(tex_count + 1, { 0 });
		m_data[size].tex_count = tex_count;
		m_data[size].available.reserve(tex_count);
		m_data[size].cache.reserve(tex_count);

		for (uint16_t i = 0; i < count; i++) {
			uint16_t n = i * div * div;
			uint16_t tex_num = tex_start + i;

			for (uint16_t y = 0; y < div; y++) {
				uint16_t ny = y * div;

				for (uint16_t x = 0; x < div; x++) {
					uint16_t ix = n + ny + x + 1;

					m_data[size].sub_texure_info[ix].tex_num = tex_num;
					m_data[size].sub_texure_info[ix].offset = { x * size, y * size };
					m_data[size].sub_texure_info[ix].shift = shift;

					m_data[size].available[ix] = true;
				}
			}
		}

		tex_start += count;
	}
}

const SubTextureInfo* TextureManager::getSubTextureInfo(uint32_t address, uint16_t size, uint16_t width, uint16_t height, uint32_t frame_count, const GlideSlot** slot_out)
{
	const auto st = g_glide_texture.slots.find(address);
	if (st == g_glide_texture.slots.end())
		return nullptr;
	GlideSlot& slot = st->second;
	if (slot_out)
		*slot_out = &slot;

	auto hash = slot.hash;
	// a match that found no room in an earlier frame's hi-res arena / queue was drawn from the original: try it again now
	// (it used to stay on the original until the game downloaded the texture again, which for cached tiles is rarely)
	if (slot.retry && slot.retry_frame != frame_count) {
		slot.hires = slot.retry;
		slot.retry = nullptr;
		hires::g_retry++;
	}
	// [Graphic] hires_textures: whether the slot holds a replacement is part of the cache key. Otherwise a slot cached
	// without one (arena full, corrupt chunk) is reused after a re-download of the same data that matched, and the
	// draw samples whatever an earlier texture left in that hi-res region.
	if (slot.hires)
		hash ^= 0x5BD1E995u;
	GlideSlot* hi_slot = slot.hires ? &slot : nullptr;
	auto& data = m_data[size];

	if (App.glide_global_cache) {
		const auto key = [&](uint32_t h) { return h ^ (width * 0x9E3779B1u) ^ (height * 0x85EBCA77u); };
		const auto it = data.global.find(key(hash));
		if (it != data.global.end()) {
			it->second.last_used_frame = frame_count;
			return &data.sub_texure_info[it->second.id];
		}
		if (data.available.empty())
			evictGlobal(data, frame_count);
		if (data.available.empty())
			return nullptr;

		const auto id = data.available.begin()->first;
		const SubTextureInfo* texture_info = &data.sub_texure_info[id];
		hash = upload(address, width, height, frame_count, texture_info, hash, hi_slot);
		data.global[key(hash)] = { id, frame_count };
		data.available.erase(id);
		return texture_info;
	}

	auto& cache = data.cache.try_emplace(address, TextureCache{ frame_count }).first->second;

	if (cache.last_used_frame != frame_count) {
		for (auto it = cache.items.begin(); it != cache.items.end();) {
			if ((*it).first != hash) {
				data.available[(*it).second] = true;
				it = cache.items.erase(it);
			} else
				it++;
		}
		cache.last_used_frame = frame_count;
	}

	const auto ci = cache.items.find(hash);
	if (ci == cache.items.end()) {
		if (data.available.begin() == data.available.end())
			return nullptr;

		const auto id = data.available.begin()->first;
		const SubTextureInfo* texture_info = &data.sub_texure_info[id];
		hash = upload(address, width, height, frame_count, texture_info, hash, hi_slot);

		cache.items.insert({ hash, id });
		data.available.erase(id);

		return texture_info;
	}

	return &data.sub_texure_info[ci->second];
}

// uploads the download at address (and its hi-res replacement, if any) into the slot; returns the cache hash, which
// drops the hi-res bit when the replacement could not be uploaded
uint32_t TextureManager::upload(uint32_t address, uint16_t width, uint16_t height, uint32_t frame_count, const SubTextureInfo* texture_info, uint32_t hash, GlideSlot* hi)
{
	const auto command_buffer = App.context->getCommandBuffer();

	const auto rm = hires::g_luts.find(address); // remapped frame: its un-remapped download goes to the atlas G channel
	const bool has_orig = rm != hires::g_luts.end() && rm->second.src.size() == (size_t)width * height;
	command_buffer->textureUpdate(g_glide_texture.memory + address, has_orig ? rm->second.src.data() : g_glide_texture.memory + address, texture_info->tex_num, { width, height }, texture_info->offset);
	if (hi && hi->hires && hires::g_bc7 == 1) { // v5: BC7 blocks of the whole download, recoloured in the shader
		const auto he = (const hires::PakEntry*)hi->hires;
		const size_t bytes = hires::bc7Size(width, height);
		uint8_t* dst = command_buffer->allocHi(bytes);
		const bool ok = dst && hires::expandBc7(he, dst);
		const uint16_t pw = (uint16_t)((hires::g_scale * width + 3) & ~3u), ph = (uint16_t)((hires::g_scale * height + 3) & ~3u);
		if (ok)
			command_buffer->textureUpdateHi(dst, texture_info->tex_num, { pw, ph }, { (uint16_t)(texture_info->offset.x * hires::g_scale), (uint16_t)(texture_info->offset.y * hires::g_scale) }, (uint32_t)bytes);
		else {
			if (!dst) {
				hi->retry = hi->hires, hi->retry_frame = frame_count;
				hires::g_no_room++;
			} else
				hires::g_bad++;
			hi->hires = nullptr;
			hash ^= 0x5BD1E995u;
		}
		return hash;
	}
	if (hi && hi->hires) {
		const uint16_t s = (uint16_t)hires::g_scale;
		uint8_t* dst = command_buffer->allocHi((size_t)width * s * height * s);
		const auto he = (const hires::PakEntry*)hi->hires;
		bool ok = dst && hires::expand(he, dst);
		const auto lut = ok ? hires::g_luts.find(address) : hires::g_luts.end();
		if (ok && (he->flags & 2)) {
			const bool remapped = lut != hires::g_luts.end();
			if (remapped && lut->second.src.size() != (size_t)width * height)
				ok = false;
			else
				hires::unxor(he, dst, remapped ? lut->second.src.data() : g_glide_texture.memory + address);
		}
		if (ok && lut != hires::g_luts.end()) {
			if (lut->second.src.size() == (size_t)width * height)
				hires::remap(he, dst, lut->second.src.data(), lut->second.lut.data());
			else
				for (size_t i = 0, n = (size_t)width * s * height * s; i < n; i++)
					dst[i] = dst[i] ? lut->second.lut[dst[i]] : 0;
		}
		if (ok)
			command_buffer->textureUpdateHi(dst, texture_info->tex_num, { (uint16_t)(width * s), (uint16_t)(height * s) }, { (uint16_t)(texture_info->offset.x * s), (uint16_t)(texture_info->offset.y * s) });
		else {
			if (!dst) { // no room this frame: retried the next frame (corrupt / mismatched entries stay off)
				hi->retry = hi->hires, hi->retry_frame = frame_count;
				hires::g_no_room++;
			} else
				hires::g_bad++;
			hi->hires = nullptr;       // not uploaded: draw the original
			hash ^= 0x5BD1E995u; // cached as a plain slot
		}
	}
	return hash;
}

// frees the least recently used quarter of the slots, never one used this frame (its draw may still be queued)
void TextureManager::evictGlobal(TextureManagerData& data, uint32_t frame_count)
{
	std::vector<std::pair<uint32_t, uint32_t>> old; // last used frame, key
	old.reserve(data.global.size());
	for (const auto& [k, item] : data.global)
		if (item.last_used_frame != frame_count)
			old.push_back({ item.last_used_frame, k });
	const size_t n = std::min(old.size(), std::max<size_t>(1, data.tex_count / 4));
	std::nth_element(old.begin(), old.begin() + (n ? n - 1 : 0), old.end());
	for (size_t i = 0; i < n; i++) {
		const auto it = data.global.find(old[i].second);
		data.available[it->second.id] = true;
		data.global.erase(it);
	}
}

void TextureManager::clearCache()
{
	for (auto& size_count : m_size_counts) {
		auto size = size_count.first;
		auto& data = m_data[size];

		for (auto& cache : data.cache) {
			for (auto& item : cache.second.items) {
				data.available[item.second] = true;
			}
		}
		data.cache.clear();
		for (auto& item : data.global)
			data.available[item.second.id] = true;
		data.global.clear();
	}
}

}