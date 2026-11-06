#pragma once

// R0 frame-time logger (Windows). Created and destroyed by the render thread, which is the
// only producer; a low-priority writer thread is the only consumer. Nothing here is touched
// when App.perf.log is false.

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "frame_stats.h"
#include "perf_sample.h"
#include "spsc_ring.h"

namespace d2gl::perf {

// Kernel + user CPU time of a thread, in ms (GetThreadTimes, 100 ns units).
inline double threadCpuMs(HANDLE thread)
{
	FILETIME c, e, k, u;
	if (!GetThreadTimes(thread, &c, &e, &k, &u))
		return -1.0;
	const uint64_t kk = ((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime;
	const uint64_t uu = ((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime;
	return (double)(kk + uu) / 10000.0;
}

class PerfLogger {
	SpscRing<PerfSample, 4096> m_ring;
	std::atomic<bool> m_stop{ false };
	std::atomic<uint64_t> m_dropped{ 0 };
	std::thread m_writer;
	uint32_t m_summary_every;

	void run(std::string csv_path, std::string json_path)
	{
		FILE* csv = nullptr;
		FILE* json = nullptr;
		if (fopen_s(&csv, csv_path.c_str(), "w") != 0)
			csv = nullptr;
		if (fopen_s(&json, json_path.c_str(), "w") != 0)
			json = nullptr;
		if (csv)
			fputs(csvHeader(), csv);

		std::vector<double> game, render, gpu;
		uint32_t first = 0, last = 0;
		char line[512];

		for (;;) {
			const bool stopping = m_stop.load(std::memory_order_acquire);
			PerfSample s;
			while (m_ring.tryPop(s)) {
				if (csv && formatCsv(line, sizeof(line), s) > 0)
					fputs(line, csv);
				if (game.empty())
					first = s.game.frame;
				last = s.game.frame;
				if (s.game.frame_ms >= 0.0)
					game.push_back(s.game.frame_ms);
				if (s.interval_ms >= 0.0)
					render.push_back(s.interval_ms);
				if (s.gpu_ms >= 0.0)
					gpu.push_back(s.gpu_ms);
				if (game.size() >= m_summary_every) {
					if (json)
						fputs(formatSummaryJson(first, last, m_dropped.load(std::memory_order_relaxed), computeFrameStats(game), computeFrameStats(render), computeFrameStats(gpu)).c_str(), json);
					game.clear();
					render.clear();
					gpu.clear();
				}
			}
			if (stopping)
				break;
			if (csv)
				fflush(csv);
			if (json)
				fflush(json);
			Sleep(100);
		}
		if (json && !game.empty())
			fputs(formatSummaryJson(first, last, m_dropped.load(std::memory_order_relaxed), computeFrameStats(game), computeFrameStats(render), computeFrameStats(gpu)).c_str(), json);
		if (csv)
			fclose(csv);
		if (json)
			fclose(json);
	}

public:
	PerfLogger(const std::string& csv_path, const std::string& json_path, uint32_t summary_every)
		: m_summary_every(summary_every ? summary_every : 600)
	{
		m_writer = std::thread(&PerfLogger::run, this, csv_path, json_path);
		SetThreadPriority(m_writer.native_handle(), THREAD_PRIORITY_LOWEST);
	}

	~PerfLogger()
	{
		m_stop.store(true, std::memory_order_release);
		if (m_writer.joinable())
			m_writer.join();
	}

	PerfLogger(const PerfLogger&) = delete;
	PerfLogger& operator=(const PerfLogger&) = delete;

	// Render thread only. Never blocks; counts a drop when the writer is behind.
	inline void push(const PerfSample& s)
	{
		if (!m_ring.tryPush(s))
			m_dropped.fetch_add(1, std::memory_order_relaxed);
	}
};

// GL_TIME_ELAPSED queries in a ring; results are read only once GL_QUERY_RESULT_AVAILABLE says so.
// Render thread only (needs the GL context current).
class GpuTimer {
	static constexpr uint32_t N = 8; // > MAX_FRAME_LATENCY: a few frames of GPU lag before a query is skipped
	GLuint m_query[N] = {};
	uint32_t m_frame[N] = {};
	uint32_t m_head = 0, m_tail = 0; // issued, retired (monotonic)
	bool m_open = false;

public:
	void init() { glGenQueries(N, m_query); }
	void destroy() { glDeleteQueries(N, m_query); }

	void begin(uint32_t frame)
	{
		m_open = m_head - m_tail < N;
		if (!m_open)
			return; // ring full: GPU is far behind, skip timing this frame
		m_frame[m_head % N] = frame;
		glBeginQuery(GL_TIME_ELAPSED, m_query[m_head % N]);
	}

	void end()
	{
		if (!m_open)
			return;
		glEndQuery(GL_TIME_ELAPSED);
		m_head++;
		m_open = false;
	}

	// Returns the oldest finished result, never waits.
	bool poll(double& ms, int64_t& frame)
	{
		if (m_tail == m_head)
			return false;
		const GLuint q = m_query[m_tail % N];
		GLint available = 0;
		glGetQueryObjectiv(q, GL_QUERY_RESULT_AVAILABLE, &available);
		if (!available)
			return false;
		GLuint64 ns = 0;
		glGetQueryObjectui64v(q, GL_QUERY_RESULT, &ns);
		ms = (double)ns / 1e6;
		frame = m_frame[m_tail % N];
		m_tail++;
		return true;
	}
};

}
