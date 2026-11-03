#include "pch.h"
#include "adaptive_sharpen.h"
#include "d2/common.h"
#include "d2/funcs.h"

namespace d2gl::modules {

static bool readPlayerPos(DWORD* x, DWORD* y)
{
	__try {
		d2::UnitAny* player = d2::getPlayerUnit();
		if (!player)
			return false;
		const d2::Path* path = d2::getUnitPath(player);
		if (!path)
			return false;
		*x = path->x;
		*y = path->y;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void AdaptiveSharpen::update()
{
	const int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	const float dt = m_last_time ? glm::clamp((float)(now - m_last_time) / 1000000.0f, 0.0f, 0.1f) : 0.0f;
	m_last_time = now;

	if (!App.sharpen.adaptive || App.game.screen != GameScreen::InGame) {
		m_ramp = 0.0f;
		m_factor = 1.0f;
		return;
	}

	DWORD x = 0, y = 0;
	if (readPlayerPos(&x, &y)) {
		// path coords are 16.16 subtiles; D2 moves them at 25 Hz, so hold "moving" briefly between ticks
		const int32_t dx = (int32_t)(x - m_last_x), dy = (int32_t)(y - m_last_y);
		const int32_t md = std::max(abs(dx), abs(dy));
		if (md > (1 << 12) && md < (8 << 16)) // ignore sub-pixel jitter and teleports/area changes
			m_last_move_time = now;
		m_last_x = x;
		m_last_y = y;
	}

	const bool moving = now - m_last_move_time < 120000;
	const float ramp_time = 0.25f;
	m_ramp = glm::clamp(m_ramp + (moving ? dt : -dt) / ramp_time, 0.0f, 1.0f);
	const float t = m_ramp * m_ramp * (3.0f - 2.0f * m_ramp);
	m_factor = glm::mix(1.0f, glm::clamp(App.sharpen.adaptive_min.value, 0.0f, 1.0f), t);
}

}
