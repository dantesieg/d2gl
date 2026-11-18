#include "pch.h"
#include "command_buffer.h"
#include "d2/common.h"

namespace d2gl {

CommandBuffer::CommandBuffer()
{
	m_tex_buffer = new uint8_t[PIXEL_BUFFER_SIZE];
	reset();
}

CommandBuffer::~CommandBuffer()
{
	delete[] m_tex_buffer;
}

void CommandBuffer::reset()
{
	m_count = 0;
	m_command = &m_commands[0];
	m_ubo_update_queue.count = 0;
	m_tex_update_queue.count = 0;
	m_hi_tex_update_queue.count = 0;
	m_hi_used = 0;
	m_tex_update_queue.data_offset = 0;
	m_vertex_count = 0;
	m_vertex_mod_count = 0;
	m_occluders.count = 0;
	m_tex_update.bit = 0;

	m_screen = App.game.screen;
	m_window_size = App.window.size;
	m_game_size = App.game.render_size;
	m_logical_size = App.game.size;
	m_bound_blend = UINT32_MAX;
	m_resized = false;
}

inline void CommandBuffer::next()
{
	m_command++;
	m_count++;
}

// The command array is fixed (2048): past it, commands are dropped (one broken frame) instead of overwriting the
// queues behind the array. The last slots are kept for Submit / TakeScreenShot so the frame still presents.
bool CommandBuffer::full(CommandType type)
{
	const uint32_t reserve = (type == CommandType::Submit || type == CommandType::TakeScreenShot) ? 0 : 4;
	if (m_count + reserve < (uint32_t)m_commands.size())
		return false;
	static bool logged = false;
	if (!logged) {
		logged = true;
		trace_log("command buffer full (%u), commands dropped this frame", m_count);
	}
	return true;
}

void CommandBuffer::pushCommand(CommandType type, uint32_t index)
{
	if (full(type)) {
		m_bound_blend = UINT32_MAX;
		return;
	}
	// Any other command may bind another pipeline on the render thread.
	m_bound_blend = type == CommandType::SetBlendState ? index : UINT32_MAX;
	m_command->type = type;
	m_command->index = index;
	next();
}

void CommandBuffer::drawIndexed(uint32_t start, uint32_t count)
{
	m_vertex_count += count; // the vertices are written either way; later draws index past them
	if (full(CommandType::DrawIndexed))
		return;
	m_command->type = CommandType::DrawIndexed;
	m_command->draw.start = start;
	m_command->draw.count = count / 4 * 6;
	next();
}

void CommandBuffer::resize()
{
	m_resized = true;
	m_game_size = App.game.render_size;
	m_logical_size = App.game.size;
	m_game_tex_bpp = App.game.bpp;
	m_window_size = App.window.size;
}

void CommandBuffer::colorUpdate(UBOType type, const void* data)
{
	if (m_ubo_update_queue.count >= m_ubo_update_queue.data.size() || full(CommandType::UBOUpdate))
		return;
	memcpy(m_ubo_update_queue.data[m_ubo_update_queue.count].value, data, sizeof(glm::vec4) * 256);
	m_ubo_update_queue.data[m_ubo_update_queue.count].type = type;

	m_command->type = CommandType::UBOUpdate;
	m_command->index = m_ubo_update_queue.count++;
	next();
}

void CommandBuffer::textureUpdate(uint8_t* data, uint16_t tex_num, glm::vec<2, uint16_t> size, glm::vec<2, uint16_t> offset)
{
	const uint32_t data_size = size.x * size.y;
	memcpy((void*)((uint32_t)m_tex_buffer + m_tex_update_queue.data_offset), data, data_size);

	auto tex_data = &m_tex_update_queue.tex_data[m_tex_update_queue.count];
	tex_data->offset = m_tex_update_queue.data_offset;
	tex_data->tex_num = tex_num;
	tex_data->tex_size = size;
	tex_data->tex_offset = offset;

	m_tex_update_queue.data_offset += data_size;
	m_tex_update_queue.count++;
}

uint8_t* CommandBuffer::allocHi(size_t bytes)
{
	if (m_hi_arena.empty())
		m_hi_arena.resize(16u << 20);
	if (m_hi_used + bytes > m_hi_arena.size() || m_hi_tex_update_queue.count >= m_hi_tex_update_queue.tex_data.size())
		return nullptr;
	uint8_t* p = m_hi_arena.data() + m_hi_used;
	m_hi_used += (bytes + 15) & ~(size_t)15;
	return p;
}

void CommandBuffer::textureUpdateHi(const uint8_t* data, uint16_t tex_num, glm::vec<2, uint16_t> size, glm::vec<2, uint16_t> offset)
{
	if (m_hi_tex_update_queue.count >= m_hi_tex_update_queue.tex_data.size())
		return;
	m_hi_tex_update_queue.tex_data[m_hi_tex_update_queue.count++] = { data, tex_num, size, offset };
}

void CommandBuffer::gameTextureUpdate(uint8_t* data, glm::vec<2, uint16_t> size, uint32_t bit)
{
	memcpy(m_tex_buffer, data, size.x * size.y * bit);
	m_tex_update.bit = bit;
	m_tex_update.size = size;
}

void CommandBuffer::setHDTextMasking(bool masking, glm::vec4 metrics)
{
	m_hd_text_mask.active = true;
	m_hd_text_mask.masking = masking;
	m_hd_text_mask.metrics = metrics;
}

}