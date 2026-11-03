#pragma once

#include "d2/structs.h"

namespace d2gl {
extern const char* g_shader_shimmer;
}

namespace d2gl::modules {

// Refraction ripple and soft glow around town portals, red portals and active waypoints.
// Sources are collected on the game thread while the world is drawn (object units identified
// by objects.txt class id), committed per command-buffer slot at the UI stage, and consumed
// by a world-only pass in PreFx on the render thread.
class PortalShimmer {
public:
	static constexpr uint32_t MAX_SOURCES = 8;
	static constexpr uint32_t MAX_SLOTS = 8;

	struct Sources {
		glm::vec4 data[MAX_SOURCES]; // xy: game px (y down), z: radius px, w: type (0 town portal, 1 red portal, 2 waypoint)
		uint32_t count = 0;
	};

private:
	Sources m_pending;
	d2::UnitAny* m_units[MAX_SOURCES] = {};
	uint32_t m_frame = 0;
	Sources m_slots[MAX_SLOTS];

	PortalShimmer() = default;

public:
	static PortalShimmer& Instance()
	{
		static PortalShimmer instance;
		return instance;
	}

	// game thread, from the drawImage hook (pos already motion-adjusted)
	void drawImage(int x, int y);
	// game thread, at DrawStage::UI before PreFx is pushed
	void commit(uint32_t slot);
	// render thread
	inline const Sources& get(uint32_t slot) { return m_slots[slot % MAX_SLOTS]; }
};

}
