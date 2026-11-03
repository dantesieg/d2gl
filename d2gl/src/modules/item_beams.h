#pragma once

#include "d2/structs.h"

namespace d2gl::d2 {

// Ref-counted drawUnit/drawMissile detours that keep d2::currently_drawing_unit
// valid while a unit is drawn. Shared by motion prediction and item beams.
void toggleUnitDrawHooks(bool active);

}

namespace d2gl::modules {

// Coloured light pillars over ground items, by quality (glide only).
// Items are recorded from the world draw hooks (exact sprite position) and
// drawn additively at the end of the world stage, before the UI.
class ItemBeams {
	enum Kind : uint8_t { Magic, Rare, Rune, Set, Unique };

	struct Beam {
		d2::UnitAny* unit;
		float x, y;
		uint8_t kind;
	};

	std::vector<Beam> m_beams;
	uint32_t m_frame = 0;
	bool m_hooked = false;

	ItemBeams() { m_beams.reserve(256); }

public:
	static ItemBeams& Instance()
	{
		static ItemBeams instance;
		return instance;
	}

	void toggle(bool active);
	void record(d2::CellContext* cell, int x, int y);
	void draw();
};

}
