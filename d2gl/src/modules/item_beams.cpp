#include "pch.h"
#include "item_beams.h"
#include "d2/common.h"
#include "d2/funcs.h"
#include "d2/stubs.h"
#include "helpers.h"

#include <detours/detours.h>

namespace d2gl::d2 {

static int unit_hook_refs = 0;

void toggleUnitDrawHooks(bool active)
{
	if (active) {
		if (unit_hook_refs++ > 0)
			return;
	} else {
		if (unit_hook_refs == 0 || --unit_hook_refs > 0)
			return;
	}

	PVOID unit_stub = isVerMax(V_110) || isVer(V_114d) ? (PVOID)drawUnitStubESI : (PVOID)drawUnitStubStack;
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	if (active) {
		DetourAttach(&(PVOID&)drawUnit, unit_stub);
		if (isVerNot(V_109d) && isVerNot(V_110))
			DetourAttach(&(PVOID&)drawMissile, drawMissileStub);
	} else {
		DetourDetach(&(PVOID&)drawUnit, unit_stub);
		if (isVerNot(V_109d) && isVerNot(V_110))
			DetourDetach(&(PVOID&)drawMissile, drawMissileStub);
	}
	DetourTransactionCommit();
	if (!active)
		currently_drawing_unit = nullptr;
}

}

namespace d2gl::modules {

// Item unit modes (D2MOO / d2bs): 3 = on ground, 5 = dropping.
// Rune class ids (vanilla misc.txt r01..r33): 610..642.
static int classifyItem(d2::UnitAny* unit)
{
	__try {
		if (unit->dwType != d2::UnitType::Item)
			return -1;

		if (isVer(V_109d)) {
			if (!unit->v109.pItemData)
				return -1;
		} else {
			const DWORD mode = unit->v110.dwMode;
			if ((mode != 3 && mode != 5) || !unit->v110.pItemData)
				return -1;
			const DWORD class_id = unit->v110.dwClassId;
			if (class_id >= 610 && class_id <= 642)
				return 2; // Rune
		}

		switch (d2::getItemQuality(unit)) {
			case d2::ItemQuality::Magic: return 0;
			case d2::ItemQuality::Rare:
			case d2::ItemQuality::Craft: return 1;
			case d2::ItemQuality::Set: return 3;
			case d2::ItemQuality::Unique: return 4;
			default: return -1;
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

static bool spriteCenter(d2::CellContext* cell, int x, float* cx)
{
	__try {
		const d2::GfxCell* gfx = cell->v113.pCurGfxCell;
		if (!gfx || gfx->width == 0 || gfx->width > 512)
			return false;
		*cx = (float)(x + (int)gfx->xoffs) + gfx->width * 0.5f;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void ItemBeams::toggle(bool active)
{
	active = active && ISGLIDE3X();
	if (active == m_hooked)
		return;
	d2::toggleUnitDrawHooks(active);
	m_hooked = active;
	m_beams.clear();
}

void ItemBeams::record(d2::CellContext* cell, int x, int y)
{
	if (!m_hooked || !App.item_beams.active || App.game.screen != GameScreen::InGame || App.game.draw_stage != DrawStage::World)
		return;

	d2::UnitAny* unit = d2::currently_drawing_unit;
	if (!unit)
		return;

	const uint32_t frame = App.context->getFrameCount();
	if (frame != m_frame) {
		m_frame = frame;
		m_beams.clear();
	}

	for (const auto& b : m_beams)
		if (b.unit == unit)
			return;

	if (m_beams.size() >= 256)
		return;

	const int kind = classifyItem(unit);
	if (kind < 0)
		return;

	// min_quality combo: 0 magic, 1 rare/rune, 2 set, 3 unique
	static const int rank[5] = { 0, 1, 1, 2, 3 };
	if (rank[kind] < App.item_beams.min_quality.selected)
		return;

	float cx = (float)x;
	if (cell && isVerMin(V_113c) && !spriteCenter(cell, x, &cx))
		cx = (float)x;

	m_beams.push_back({ unit, cx, (float)y, (uint8_t)kind });
}

void ItemBeams::draw()
{
	if (!m_hooked || !App.item_beams.active || App.game.screen != GameScreen::InGame)
		return;

	const uint32_t frame = App.context->getFrameCount();
	if (frame != m_frame || m_beams.empty())
		return;

	static const glm::vec3 colors[5] = {
		{ 0.35f, 0.45f, 1.00f }, // magic: blue
		{ 1.00f, 0.95f, 0.30f }, // rare: yellow
		{ 1.00f, 0.55f, 0.10f }, // rune: orange
		{ 0.20f, 1.00f, 0.25f }, // set: green
		{ 0.95f, 0.75f, 0.30f }, // unique: gold
	};

	const float t = (float)(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() % 600000) / 1000.0f;

	auto pack = [](glm::vec3 c, float a) -> uint32_t {
		c = glm::clamp(c * a, 0.0f, 1.0f);
		return 0xFF000000 | ((uint32_t)(c.r * 255.0f) << 16) | ((uint32_t)(c.g * 255.0f) << 8) | (uint32_t)(c.b * 255.0f);
	};

	std::vector<glm::vec2> pos;
	std::vector<uint32_t> col;
	pos.reserve(m_beams.size() * 16);
	col.reserve(m_beams.size() * 16);

	// Two columns per beam (halo + core), each split in left/right halves so
	// the centre is bright and the edges fade to black (additive = invisible).
	auto column = [&](float cx, float base, float half_w, float height, glm::vec3 c, float a) {
		const uint32_t bottom = pack(c, a), top = pack(c, 0.0f), edge = pack(c, 0.0f);
		const float y0 = base, y1 = base - height;
		for (int side = -1; side <= 1; side += 2) {
			const float xe = cx + side * half_w;
			pos.insert(pos.end(), { { xe, y1 }, { cx, y1 }, { cx, y0 }, { xe, y0 } });
			col.insert(col.end(), { edge, top, bottom, edge });
		}
	};

	for (const auto& b : m_beams) {
		const float phase = (float)(((uintptr_t)b.unit >> 4) & 63) * 0.1f;
		const float pulse = 0.8f + 0.2f * sinf(t * 2.4f + phase);
		const float scale = b.kind == Unique || b.kind == Set ? 1.0f : 0.8f;
		const glm::vec3 c = colors[b.kind];
		column(b.x, b.y + 4.0f, 18.0f * scale, 150.0f * scale, c, 0.35f * pulse);
		column(b.x, b.y + 2.0f, 5.0f * scale, 120.0f * scale, c, 0.75f * pulse);
	}

	App.context->pushAdditiveQuads(pos.data(), col.data(), (uint32_t)(pos.size() / 4));
}

}
