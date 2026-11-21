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

#include "wrapper.h"
#include <initializer_list>

// Glide command-stream capture ([Other] glide_capture) and replay (d2gl_replay.exe).
// File format: docs/capture/FORMAT.md. Op values are part of the format: append only.
namespace d2gl::capture {

enum Op : uint16_t {
	OP_END = 0,
	OP_BUFFER_CLEAR = 1,
	OP_BUFFER_SWAP = 2,
	OP_DRAW_POINT = 3,
	OP_DRAW_LINE = 4,
	OP_DRAW_ARRAY = 5,
	OP_DRAW_ARRAY_CONTIG = 6,
	OP_ALPHA_BLEND = 7,
	OP_ALPHA_COMBINE = 8,
	OP_CHROMAKEY_MODE = 9,
	OP_COLOR_COMBINE = 10,
	OP_CONSTANT_COLOR = 11,
	OP_GAMMA_TABLE = 12,
	OP_GAMMA_RGB = 13,
	OP_TEX_SOURCE = 14,
	OP_TEX_DOWNLOAD = 15,
	OP_TEX_TABLE = 16,
	OP_LFB_FRAME = 17,
	OP_SST_WIN_OPEN = 18,
	OP_GAME = 32,     // side band: game screen / panels / automap / level, emitted when they change
	OP_STAGE = 33,    // side band: D2GL draw stage change (Context::onStageChange)
	OP_SNAP_TEX = 34, // capture start: one glide texture memory slot
	OP_COUNT = 35,
};

constexpr uint32_t VERSION = 1;

extern bool g_on; // recording (between the trigger and the last frame)

// state calls are also shadowed while App.glide_capture is on, so a capture starts with the current state
void rec(Op op, std::initializer_list<uint32_t> args, const void* blob = nullptr, uint32_t blob_size = 0);
void recDrawArray(FxU32 mode, FxU32 count, void** pointers);
void lfbLocked(const GrLfbInfo_t* info);
void lfbUnlocked();
void onFrame(); // after every grBufferSwap while App.glide_capture: counts frames, polls the trigger file

inline uint32_t f2u(float f)
{
	uint32_t u;
	memcpy(&u, &f, 4);
	return u;
}

}
