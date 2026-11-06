#pragma once

// Portable, header-only per-frame perf record and its CSV / JSON text forms (no OS headers).
// All times are milliseconds measured with QueryPerformanceCounter, except gpu_ms (GL_TIME_ELAPSED)
// and the *_cpu_ms columns (GetThreadTimes, kernel + user, 100 ns granularity but typically
// updated only at the scheduler tick). -1 means "not available for this row".

#include <cstdint>
#include <cstdio>
#include <string>

#include "frame_stats.h"

namespace d2gl::perf {

// Game-thread part, recorded by presentFrame() into the slot's CommandBuffer before the
// cpu semaphore is released, and read by the render thread after it acquires that semaphore.
struct GameTiming {
	uint32_t frame = 0;          // game-thread frame counter
	uint32_t inflight = 0;       // frames submitted but not yet retired, including this one
	double frame_ms = -1.0;      // present-to-present on the game thread
	double work_ms = -1.0;       // beginFrame() -> presentFrame() release
	double wait_ms = -1.0;       // blocked on m_semaphore_gpu before this frame's recording
	double cpu_ms = -1.0;        // game thread CPU time since the previous sampled frame
};

struct PerfSample {
	GameTiming game;
	uint32_t slot = 0;
	double t_ms = 0.0;             // render thread QPC timestamp at end of frame
	double render_wait_ms = -1.0;  // render thread blocked on m_semaphore_cpu
	double render_busy_ms = -1.0;  // acquire -> just before the fence wait (CPU submission only)
	double fence_ms = -1.0;        // glClientWaitSync
	double swap_ms = -1.0;         // Menu::draw + SwapBuffers
	double limiter_ms = -1.0;      // limiter sleep / spin
	double interval_ms = -1.0;     // render present-to-present (SwapBuffers return to return)
	double render_cpu_ms = -1.0;   // render thread CPU time since the previous sampled frame
	double gpu_ms = -1.0;          // GL_TIME_ELAPSED of the frame gpu_frame (an earlier one)
	int64_t gpu_frame = -1;        // game frame number gpu_ms belongs to
};

inline const char* csvHeader()
{
	return "frame,slot,inflight,t_ms,game_frame_ms,game_work_ms,game_wait_ms,game_cpu_ms,"
		   "render_wait_ms,render_busy_ms,fence_ms,swap_ms,limiter_ms,interval_ms,render_cpu_ms,"
		   "gpu_ms,gpu_frame\n";
}

inline int formatCsv(char* buf, size_t size, const PerfSample& s)
{
	return std::snprintf(buf, size, "%u,%u,%u,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%lld\n",
		s.game.frame, s.slot, s.game.inflight, s.t_ms, s.game.frame_ms, s.game.work_ms, s.game.wait_ms, s.game.cpu_ms,
		s.render_wait_ms, s.render_busy_ms, s.fence_ms, s.swap_ms, s.limiter_ms, s.interval_ms, s.render_cpu_ms,
		s.gpu_ms, (long long)s.gpu_frame);
}

inline void appendStatsJson(std::string& out, const char* name, const FrameStats& st)
{
	char buf[384];
	std::snprintf(buf, sizeof(buf),
		"\"%s\":{\"count\":%u,\"fps\":%.2f,\"mean\":%.4f,\"median\":%.4f,\"p95\":%.4f,\"p99\":%.4f,\"max\":%.4f,\"variance\":%.4f,\"stalls_2x_median\":%u}",
		name, st.count, st.fps, st.mean, st.median, st.p95, st.p99, st.max, st.variance, st.stalls);
	out += buf;
}

// One JSON object per line (JSON Lines).
inline std::string formatSummaryJson(uint32_t first_frame, uint32_t last_frame, uint64_t dropped,
	const FrameStats& game, const FrameStats& render, const FrameStats& gpu)
{
	char head[160];
	std::snprintf(head, sizeof(head), "{\"first_frame\":%u,\"last_frame\":%u,\"dropped_samples\":%llu,", first_frame, last_frame, (unsigned long long)dropped);
	std::string out = head;
	appendStatsJson(out, "game_frame_ms", game);
	out += ",";
	appendStatsJson(out, "render_interval_ms", render);
	out += ",";
	appendStatsJson(out, "gpu_ms", gpu);
	out += "}\n";
	return out;
}

}
