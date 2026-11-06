#pragma once

// Portable, header-only single-producer / single-consumer ring (no OS headers).
// Exactly one thread may call tryPush() and exactly one other thread may call tryPop().
// Capacity N must be a power of two; all N slots are usable (monotonically
// increasing 32-bit head/tail counters; unsigned wrap-around is harmless).

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace d2gl::perf {

template <typename T, size_t N>
class SpscRing {
	static_assert(N >= 2 && (N & (N - 1)) == 0, "SpscRing capacity must be a power of two");

	alignas(64) std::atomic<uint32_t> m_head{ 0 }; // written by producer only
	alignas(64) std::atomic<uint32_t> m_tail{ 0 }; // written by consumer only
	alignas(64) T m_data[N];

public:
	// Producer. Returns false (and drops the item) when full; never blocks.
	bool tryPush(const T& item)
	{
		const uint32_t head = m_head.load(std::memory_order_relaxed);
		const uint32_t tail = m_tail.load(std::memory_order_acquire); // see consumer's release
		if (head - tail >= N)
			return false;
		m_data[head & (N - 1)] = item;
		m_head.store(head + 1, std::memory_order_release); // publish the item
		return true;
	}

	// Consumer. Returns false when empty; never blocks.
	bool tryPop(T& out)
	{
		const uint32_t tail = m_tail.load(std::memory_order_relaxed);
		const uint32_t head = m_head.load(std::memory_order_acquire); // see producer's item write
		if (head == tail)
			return false;
		out = m_data[tail & (N - 1)];
		m_tail.store(tail + 1, std::memory_order_release); // slot may be reused by producer
		return true;
	}

	static constexpr size_t capacity() { return N; }
};

}
