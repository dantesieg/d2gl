#pragma once

#include <atomic>

namespace d2gl::modules {

// Eases sharpen strength down while the player/camera moves to cut shimmer and crawl.
class AdaptiveSharpen {
	int64_t m_last_time = 0;
	int64_t m_last_move_time = 0;
	DWORD m_last_x = 0;
	DWORD m_last_y = 0;
	float m_ramp = 0.0f; // 0 still, 1 moving
	std::atomic<float> m_factor = 1.0f;

	AdaptiveSharpen() = default;

public:
	static AdaptiveSharpen& Instance()
	{
		static AdaptiveSharpen instance;
		return instance;
	}

	void update();                                     // game thread, once per frame
	inline float getFactor() { return m_factor.load(); } // render thread
};

}
