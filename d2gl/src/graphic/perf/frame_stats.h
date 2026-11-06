#pragma once

// Portable, header-only frame-time statistics (no OS headers).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace d2gl::perf {

struct FrameStats {
	uint32_t count = 0;
	double fps = 0.0;      // count / sum(ms) * 1000
	double mean = 0.0;     // ms
	double median = 0.0;   // ms
	double p95 = 0.0;      // ms
	double p99 = 0.0;      // ms
	double max = 0.0;      // ms
	double variance = 0.0; // ms^2 (population)
	uint32_t stalls = 0;   // samples > 2 x median
};

// Nearest-rank percentile on an ascending-sorted array; p in [0, 100].
inline double percentileSorted(const std::vector<double>& sorted, double p)
{
	if (sorted.empty())
		return 0.0;
	if (p <= 0.0)
		return sorted.front();
	if (p >= 100.0)
		return sorted.back();
	size_t rank = (size_t)std::ceil(p / 100.0 * (double)sorted.size()); // 1-based
	if (rank < 1)
		rank = 1;
	return sorted[rank - 1];
}

inline double medianSorted(const std::vector<double>& sorted)
{
	const size_t n = sorted.size();
	if (n == 0)
		return 0.0;
	return (n & 1) ? sorted[n / 2] : 0.5 * (sorted[n / 2 - 1] + sorted[n / 2]);
}

// Takes its input by value: it is sorted in place.
inline FrameStats computeFrameStats(std::vector<double> ms)
{
	FrameStats s;
	s.count = (uint32_t)ms.size();
	if (ms.empty())
		return s;

	double sum = 0.0;
	for (double v : ms)
		sum += v;
	s.mean = sum / (double)ms.size();

	double sq = 0.0;
	for (double v : ms)
		sq += (v - s.mean) * (v - s.mean);
	s.variance = sq / (double)ms.size();

	std::sort(ms.begin(), ms.end());
	s.median = medianSorted(ms);
	s.p95 = percentileSorted(ms, 95.0);
	s.p99 = percentileSorted(ms, 99.0);
	s.max = ms.back();
	s.fps = sum > 0.0 ? (double)ms.size() * 1000.0 / sum : 0.0;
	for (double v : ms)
		s.stalls += v > 2.0 * s.median ? 1 : 0;
	return s;
}

}
