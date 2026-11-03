#pragma once

#include "d2/structs.h"

namespace d2gl::modules {

// Cross-fades unit sprite layers between consecutive animation frames.
// D2 advances animation frames at 25 Hz; on a frame change the previous frame's
// cell is drawn again over the new one with alpha fading 1 -> 0 over one game tick.
class AnimSmoothing {
	struct Layer {
		d2::CellFile* file = nullptr;
		d2::GfxCell* cur = nullptr;
		DWORD cur_no = 0;
		d2::GfxCell* prev = nullptr;
		DWORD prev_no = 0;
		int64_t change_time = 0;
		uint32_t frame = 0;
	};

	std::unordered_map<uint64_t, Layer> m_layers;
	uint32_t m_prune_frame = 0;

	AnimSmoothing() = default;

public:
	static AnimSmoothing& Instance()
	{
		static AnimSmoothing instance;
		return instance;
	}

	// Call after the current cell was drawn. Draws the faded previous frame if any.
	void drawPrevious(d2::CellContext* cell, int x, int y, uint32_t gamma, int draw_mode, uint8_t* palette);
};

}
