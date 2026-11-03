#include "pch.h"
#include "portal_shimmer.h"
#include "d2/common.h"
#include "d2/funcs.h"
#include "helpers.h"

namespace d2gl {
const char* g_shader_shimmer = {
#include "graphic/shaders/shimmer.glsl.h"
};
}

namespace d2gl::modules {

// objects.txt (1.10+) class ids
// 59 TownPortal (blue), 60 PermanentTownPortal (red portals: Tristram, Cow, Nihlathak, Uber...)
// waypoints: 119 act1 town/wild, 145 act1 cellar/catacombs, 156 act2 town, 157 act1 wild, 237 act3 town,
// 238 act4 town, 288 act2 sewer/arcane, 323 act3 jungle, 324 act3 mephisto, 398 act4 hell, 402 act2 tomb/palace,
// 429 act5 town, 494 act5 baal, 496 act5 wild, 511 act5 ice caves, 539 act5 temple
static int classify(DWORD class_id)
{
	switch (class_id) {
		case 59: return 0;
		case 60: return 1;
		case 119: case 145: case 156: case 157: case 237: case 238: case 288: case 323:
		case 324: case 398: case 402: case 429: case 494: case 496: case 511: case 539:
			return 2;
	}
	return -1;
}

// SEH-guarded unit read; no C++ objects with destructors in here.
static int readSource(d2::UnitAny* unit)
{
	__try {
		if (unit->dwType != d2::UnitType::Object)
			return -1;
		const int type = classify(unit->v110.dwClassId);
		// inactive waypoints sit in mode 0 (neutral); activated ones animate in a non-zero mode
		if (type == 2 && unit->v110.dwMode == 0)
			return -1;
		return type;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

void PortalShimmer::drawImage(int x, int y)
{
	if (!App.portal_shimmer || !ISGLIDE3X() || !(isVerMin(V_110)))
		return;
	if (App.game.screen != GameScreen::InGame || App.game.draw_stage != DrawStage::World)
		return;

	d2::UnitAny* unit = d2::currently_drawing_unit;
	if (!unit)
		return;

	const uint32_t frame = App.context->getFrameCount();
	if (m_frame != frame) {
		m_frame = frame;
		m_pending.count = 0;
	}

	for (uint32_t i = 0; i < m_pending.count; i++) {
		if (m_units[i] == unit)
			return; // first layer of a unit only
	}
	if (m_pending.count >= MAX_SOURCES)
		return;

	const int type = readSource(unit);
	if (type < 0)
		return;

	// draw position is the unit's floor anchor; portals stand upright, waypoints lie on the floor
	const float cy = type == 2 ? (float)y - 8.0f : (float)y - 52.0f;
	const float radius = type == 2 ? 62.0f : 56.0f;
	m_units[m_pending.count] = unit;
	m_pending.data[m_pending.count++] = { (float)x, cy, radius, (float)type };
}

void PortalShimmer::commit(uint32_t slot)
{
	auto& out = m_slots[slot % MAX_SLOTS];
	out.count = 0;
	if (App.portal_shimmer && m_frame == App.context->getFrameCount()) {
		out = m_pending;
	}
	m_pending.count = 0;
}

}
