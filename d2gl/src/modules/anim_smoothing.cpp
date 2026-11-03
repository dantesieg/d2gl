#include "pch.h"
#include "anim_smoothing.h"
#include "d2/common.h"
#include "d2/funcs.h"
#include "helpers.h"

namespace d2gl::modules {

static int64_t nowUs()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void AnimSmoothing::drawPrevious(d2::CellContext* cell, int x, int y, uint32_t gamma, int draw_mode, uint8_t* palette)
{
	if (!App.animation_smoothing || !ISGLIDE3X() || !cell || !(isVerMin(V_113c)))
		return;
	if (App.game.screen != GameScreen::InGame || App.game.draw_stage != DrawStage::World)
		return;

	d2::UnitAny* unit = d2::currently_drawing_unit;
	if (!unit || (unit->dwType != d2::UnitType::Player && unit->dwType != d2::UnitType::Monster && unit->dwType != d2::UnitType::Missile))
		return;

	d2::CellFile* file = cell->v113.pCellFile;
	d2::GfxCell* gfx = cell->v113.pCurGfxCell;
	const DWORD cell_no = cell->v113.nCellNo;
	if (!file || !gfx)
		return;

	const uint32_t frame = App.context->getFrameCount();
	const int64_t now = nowUs();

	const uint64_t key = ((uint64_t)(d2::getUnitID(unit) | ((uint32_t)unit->dwType << 24)) << 32) | (uint32_t)(uintptr_t)file;
	Layer& l = m_layers[key];

	if (l.frame != frame) {
		if (l.file != file || l.frame + 1 != frame) {
			// new layer, or not drawn last frame: no history to blend from
			l.file = file;
			l.cur = gfx;
			l.cur_no = cell_no;
			l.prev = nullptr;
		} else if (cell_no != l.cur_no || gfx != l.cur) {
			l.prev = l.cur;
			l.prev_no = l.cur_no;
			l.cur = gfx;
			l.cur_no = cell_no;
			l.change_time = now;
		}
		l.frame = frame;
	}

	// prune stale layers once in a while
	if (frame - m_prune_frame > 250) {
		m_prune_frame = frame;
		for (auto it = m_layers.begin(); it != m_layers.end();)
			it = (frame - it->second.frame > 2) ? m_layers.erase(it) : std::next(it);
	}

	if (!l.prev || l.prev == gfx)
		return;

	// only blend a plain one-step advance (or loop wrap) within the same direction
	const bool step = l.prev_no + 1 == cell_no || (cell_no == 0 && l.prev_no > 0 && l.prev_no <= 64);
	if (!step) {
		l.prev = nullptr;
		return;
	}

	const float t = (float)(now - l.change_time) / 40000.0f;
	if (t >= 1.0f) {
		l.prev = nullptr;
		return;
	}
	const uint8_t alpha = (uint8_t)((1.0f - t) * 255.0f);
	if (alpha < 8)
		return;

	cell->v113.pCurGfxCell = l.prev;
	cell->v113.nCellNo = l.prev_no;
	App.context->setAlphaOverride(alpha);
	d2::drawImage(cell, x, y, gamma, draw_mode, palette);
	App.context->setAlphaOverride(255);
	cell->v113.pCurGfxCell = gfx;
	cell->v113.nCellNo = cell_no;
}

}
