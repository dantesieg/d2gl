#pragma once

#include "d2/structs.h"

namespace d2gl::d2 {

// Ref-counted drawUnit/drawMissile detours that keep d2::currently_drawing_unit
// valid while a unit is drawn. Shared by motion prediction and loot sparkle.
void toggleUnitDrawHooks(bool active);

}

namespace d2gl::modules {

// Short additive star when a valuable item drops (glide only). Items are seen in
// the world draw hook; a sparkle starts only for items first seen while dropping.
class LootSparkle {
	struct Sparkle {
		float x, y;
		int64_t start_ms;
		uint32_t last_frame;
		uint8_t kind;
	};

	std::unordered_map<d2::UnitAny*, Sparkle> m_sparkles;
	std::unordered_map<d2::UnitAny*, uint32_t> m_seen; // unit -> last frame seen (no sparkle)
	bool m_hooked = false;

	LootSparkle() = default;

public:
	static LootSparkle& Instance()
	{
		static LootSparkle instance;
		return instance;
	}

	void toggle(bool active);
	void record(d2::CellContext* cell, int x, int y);
	void draw();
};

}
