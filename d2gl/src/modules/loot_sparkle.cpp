#include "pch.h"
#include "loot_sparkle.h"
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

static bool isDropping(d2::UnitAny* unit)
{
	__try {
		return isVerNot(V_109d) && unit->v110.dwMode == 5;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static int64_t nowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// [Feature] loot_sparkle_duration (default 1.1 s = the original 1100 ms)
static int64_t sparkleMs() { return (int64_t)std::llround(glm::clamp(App.loot_sparkle.duration.value, 0.5f, 10.0f) * 1000.0); }

uint8_t LootSparkle::itemGlowLevel(d2::UnitAny* unit)
{
	const int kind = classifyItem(unit);
	return kind == 2 ? 1 : kind == 3 ? 2 : kind == 4 ? 3 : 0;
}

void LootSparkle::toggle(bool active)
{
	active = active && ISGLIDE3X();
	if (active == m_hooked)
		return;
	d2::toggleUnitDrawHooks(active);
	m_hooked = active;
	m_sparkles.clear();
	m_seen.clear();
	m_minions.clear();
	m_pet_cache.clear();
}

// 1.14d client pet list: Game.exe 0x479150 fastcall(unit id) -> owner unit id or -1
// (party bar 0x452C83 uses it; same lookup as MedianRevived's totem list).
static bool isMyPet(d2::UnitAny* unit)
{
	__try {
		if (unit->dwType != d2::UnitType::Monster)
			return false;
		d2::UnitAny* me = d2::getPlayerUnit();
		if (!me)
			return false;
		auto owner = reinterpret_cast<DWORD(__fastcall*)(DWORD)>(0x479150);
		return owner(unit->v110.dwUnitId) == me->v110.dwUnitId;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void LootSparkle::recordMinion(d2::UnitAny* unit, d2::CellContext* cell, int x, int y, uint32_t frame)
{
	if (frame - m_pet_cache_frame > 60) {
		m_pet_cache.clear();
		m_pet_cache_frame = frame;
	}
	auto pc = m_pet_cache.find(unit);
	bool pet = pc != m_pet_cache.end() ? pc->second : (m_pet_cache[unit] = isMyPet(unit));
	if (!pet)
		return;
	float cx = (float)x;
	if (cell && !spriteCenter(cell, x, &cx))
		cx = (float)x;
	auto& m = m_minions[unit];
	if (m.last_frame != frame) // first cell of this unit this frame: its centre
		m = { cx, (float)y, frame };
}

void LootSparkle::drawMinions(uint32_t frame)
{
	std::vector<glm::vec2> pos;
	std::vector<uint32_t> col;
	const float rx = 20.0f, ry = 9.0f, w = 3.0f;
	const uint32_t inner = 0xFF1E5A1E, outer = 0xFF000000; // additive: dim green fading out
	for (auto it = m_minions.begin(); it != m_minions.end();) {
		if (frame - it->second.last_frame > 2) {
			it = m_minions.erase(it);
			continue;
		}
		const glm::vec2 c = { it->second.x, it->second.y - 2.0f };
		const int N = 24;
		for (int i = 0; i < N; i++) {
			const float a0 = glm::two_pi<float>() * i / N, a1 = glm::two_pi<float>() * (i + 1) / N;
			const glm::vec2 d0 = { cosf(a0), sinf(a0) }, d1 = { cosf(a1), sinf(a1) };
			auto P = [&](glm::vec2 d, float r) { return c + glm::vec2(d.x * (rx + r), d.y * (ry + r * 0.45f)); };
			pos.insert(pos.end(), { P(d0, -w), P(d1, -w), P(d1, 0.0f), P(d0, 0.0f) });
			col.insert(col.end(), { outer, outer, inner, inner });
			pos.insert(pos.end(), { P(d0, 0.0f), P(d1, 0.0f), P(d1, w), P(d0, w) });
			col.insert(col.end(), { inner, inner, outer, outer });
		}
		++it;
	}
	if (!pos.empty())
		App.context->pushAdditiveQuads(pos.data(), col.data(), (uint32_t)(pos.size() / 4));
}

void LootSparkle::record(d2::CellContext* cell, int x, int y)
{
	if (!m_hooked || App.game.screen != GameScreen::InGame || App.game.draw_stage != DrawStage::World)
		return;

	d2::UnitAny* unit = d2::currently_drawing_unit;
	if (!unit)
		return;

	const uint32_t frame = App.context->getFrameCount();
	if (App.minion_ring && isVer(V_114d))
		recordMinion(unit, isVerMin(V_113c) ? cell : nullptr, x, y, frame);
	if (!App.loot_sparkle.active)
		return;
	auto it = m_sparkles.find(unit);
	if (it != m_sparkles.end()) {
		it->second.x = (float)x;
		it->second.y = (float)y;
		it->second.last_frame = frame;
		return;
	}
	auto seen = m_seen.find(unit);
	if (seen != m_seen.end()) {
		seen->second = frame;
		return;
	}

	const int kind = classifyItem(unit);
	if (kind < 0)
		return;

	const bool dropping = isDropping(unit);

	// min_quality combo: 0 magic, 1 rare/rune, 2 set, 3 unique
	static const int rank[5] = { 0, 1, 1, 2, 3 };
	if (!dropping || rank[kind] < App.loot_sparkle.min_quality.selected) {
		if (m_seen.size() < 2048)
			m_seen[unit] = frame;
		return;
	}

	float cx = (float)x;
	if (cell && isVerMin(V_113c) && !spriteCenter(cell, x, &cx))
		cx = (float)x;
	m_sparkles[unit] = { cx, (float)y, nowMs(), frame, (uint8_t)kind };
}

void LootSparkle::draw()
{
	if (!m_hooked || App.game.screen != GameScreen::InGame)
		return;

	const uint32_t frame = App.context->getFrameCount();
	if (App.minion_ring)
		drawMinions(frame);
	else
		m_minions.clear();
	if (!App.loot_sparkle.active)
		return;
	for (auto it = m_seen.begin(); it != m_seen.end();)
		it = frame - it->second > 50 ? m_seen.erase(it) : std::next(it);

	if (m_sparkles.empty())
		return;

	static const glm::vec3 colors[5] = {
		{ 0.45f, 0.55f, 1.00f }, // magic
		{ 1.00f, 0.95f, 0.45f }, // rare
		{ 1.00f, 0.65f, 0.25f }, // rune
		{ 0.35f, 1.00f, 0.40f }, // set
		{ 1.00f, 0.82f, 0.45f }, // unique
	};
	// [Feature] loot_sparkle_colors: saturated class colours, max channel 1 like the default set
	static const glm::vec3 class_colors[5] = {
		{ 0.30f, 0.45f, 1.00f }, // magic: blue
		{ 1.00f, 1.00f, 0.25f }, // rare: yellow
		{ 1.00f, 0.50f, 0.10f }, // rune: orange
		{ 0.20f, 1.00f, 0.25f }, // set: green
		{ 1.00f, 0.72f, 0.15f }, // unique: gold
	};
	const glm::vec3* palette = App.loot_sparkle.colors ? class_colors : colors;
	auto pack = [](glm::vec3 c, float a) -> uint32_t {
		c = glm::clamp(c * a, 0.0f, 1.0f);
		return 0xFF000000 | ((uint32_t)(c.r * 255.0f) << 16) | ((uint32_t)(c.g * 255.0f) << 8) | (uint32_t)(c.b * 255.0f);
	};

	std::vector<glm::vec2> pos;
	std::vector<uint32_t> col;
	const int64_t now = nowMs();
	const int64_t SPARKLE_MS = sparkleMs();

	// One ray = triangle (as a degenerate quad): bright at the centre, black at the tip.
	auto ray = [&](glm::vec2 c, glm::vec2 dir, float len, float w, uint32_t bright) {
		const glm::vec2 n = { -dir.y, dir.x };
		const glm::vec2 tip = c + dir * len;
		pos.insert(pos.end(), { tip, c + n * w, c - n * w, tip });
		col.insert(col.end(), { 0xFF000000, bright, bright, 0xFF000000 });
	};

	for (auto it = m_sparkles.begin(); it != m_sparkles.end();) {
		const int64_t age = now - it->second.start_ms;
		if (age >= SPARKLE_MS || frame - it->second.last_frame > 2) {
			if (m_seen.size() < 2048)
				m_seen[it->first] = frame;
			it = m_sparkles.erase(it);
			continue;
		}
		const float t = (float)age / (float)SPARKLE_MS;
		const float a = sinf(glm::pi<float>() * glm::min(t * 1.6f, 1.0f)) * (1.0f - t * 0.4f);
		const float size = (it->second.kind >= 3 ? 26.0f : 20.0f) * (0.6f + 0.4f * sinf(glm::pi<float>() * t));
		const float rot = t * 1.2f;
		const glm::vec2 c = { it->second.x, it->second.y - 12.0f };
		const uint32_t bright = pack(palette[it->second.kind], a);
		const uint32_t dim = pack(palette[it->second.kind], a * 0.6f);
		for (int i = 0; i < 4; i++) {
			const float ang = rot + i * glm::half_pi<float>();
			ray(c, { cosf(ang), sinf(ang) }, size, 1.6f, bright);
			const float d = ang + glm::quarter_pi<float>();
			ray(c, { cosf(d), sinf(d) }, size * 0.45f, 1.2f, dim);
		}
		++it;
	}

	if (!pos.empty())
		App.context->pushAdditiveQuads(pos.data(), col.data(), (uint32_t)(pos.size() / 4));
}

}
