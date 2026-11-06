// Native tests for the portable R0/R2 components (d2gl/src/graphic/perf/*.h).
// macOS:  clang++ -std=c++20 -O1 -g -fsanitize=thread test_perf.cpp -o test_tsan && ./test_tsan
// Negative control (must report a data race): add -DTSAN_NEGATIVE_CONTROL
// Win32:  i686-w64-mingw32-g++ -std=c++20 -O2 -static test_perf.cpp -o test.exe && wine test.exe

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <semaphore>
#include <thread>
#include <vector>

#include "../../d2gl/src/graphic/perf/frame_settings.h"
#include "../../d2gl/src/graphic/perf/frame_stats.h"
#include "../../d2gl/src/graphic/perf/perf_sample.h"
#include "../../d2gl/src/graphic/perf/spsc_ring.h"

using namespace d2gl::perf;

static int g_fail = 0;
#define CHECK(c)                                                       \
	do {                                                               \
		if (!(c)) {                                                    \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
			g_fail++;                                                  \
		}                                                              \
	} while (0)

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

static void testStats()
{
	CHECK(computeFrameStats({}).count == 0);

	std::vector<double> v;
	for (int i = 1; i <= 100; i++)
		v.push_back((double)i);
	FrameStats s = computeFrameStats(v);
	CHECK(s.count == 100);
	CHECK(near(s.mean, 50.5));
	CHECK(near(s.median, 50.5));
	CHECK(near(s.p95, 95.0));
	CHECK(near(s.p99, 99.0));
	CHECK(near(s.max, 100.0));
	CHECK(near(s.variance, 833.25));
	CHECK(s.stalls == 0); // > 101 needed
	CHECK(near(s.fps, 100.0 * 1000.0 / 5050.0));

	FrameStats t = computeFrameStats({ 16.0, 16.0, 16.0, 50.0, 16.0 });
	CHECK(near(t.median, 16.0));
	CHECK(t.stalls == 1);
	CHECK(near(t.max, 50.0));
	CHECK(near(computeFrameStats({ 7.0 }).p99, 7.0));
	std::printf("stats: ok\n");
}

static void testFormat()
{
	PerfSample s;
	s.game.frame = 42;
	s.slot = 1;
	s.game.inflight = 2;
	s.t_ms = 1000.5;
	s.game.frame_ms = 16.6667;
	s.gpu_frame = 40;
	s.gpu_ms = 3.25;
	char buf[512];
	const int n = formatCsv(buf, sizeof(buf), s);
	CHECK(n > 0 && n < (int)sizeof(buf));
	int commas = 0;
	for (const char* p = buf; *p; p++)
		commas += *p == ',';
	int header_commas = 0;
	for (const char* p = csvHeader(); *p; p++)
		header_commas += *p == ',';
	CHECK(commas == header_commas && commas == 16);
	CHECK(std::strncmp(buf, "42,1,2,1000.500,16.6667,", 24) == 0);
	CHECK(std::strstr(buf, ",3.2500,40\n") != nullptr);

	const std::string j = formatSummaryJson(1, 600, 0, computeFrameStats({ 10, 20 }), computeFrameStats({}), computeFrameStats({ 1 }));
	CHECK(j.find("\"first_frame\":1,\"last_frame\":600,\"dropped_samples\":0,") == 1);
	CHECK(j.find("\"game_frame_ms\":{\"count\":2,\"fps\":66.67,\"mean\":15.0000,\"median\":15.0000") != std::string::npos);
	CHECK(j.back() == '\n');
	std::printf("format: ok\n%s%s%s", csvHeader(), buf, j.c_str());
}

// Producer/consumer through the SPSC ring, 1e6 items, checking order and payload integrity.
static void testRing()
{
	static SpscRing<PerfSample, 1024> ring;
	const uint32_t total = 1000000;
	std::atomic<uint64_t> drops{ 0 };
	std::thread prod([&] {
		for (uint32_t i = 0; i < total; i++) {
			PerfSample s;
			s.game.frame = i;
			s.t_ms = (double)i * 2.0;
			s.gpu_frame = (int64_t)i * 3;
			while (!ring.tryPush(s)) {
				drops.fetch_add(1, std::memory_order_relaxed); // full: retry (logger would drop)
				std::this_thread::yield();
			}
		}
	});
	uint32_t expect = 0;
	bool order_ok = true;
	while (expect < total) {
		PerfSample s;
		if (!ring.tryPop(s)) {
			std::this_thread::yield();
			continue;
		}
		if (s.game.frame != expect || s.t_ms != (double)expect * 2.0 || s.gpu_frame != (int64_t)expect * 3)
			order_ok = false;
		expect++;
	}
	prod.join();
	PerfSample tmp;
	CHECK(order_ok);
	CHECK(!ring.tryPop(tmp));
	std::printf("ring: ok (%u items, %llu full-ring retries)\n", total, (unsigned long long)drops.load());
}

// Mirrors Context::presentFrame / renderThread: per-slot snapshot written before the "cpu"
// semaphore release, read after the acquire; slot reuse gated by the "gpu" semaphore.
static void testSnapshotHandoff()
{
	constexpr uint32_t latency = 2, slots = latency + 1, frames = 1000000;
	struct Slot {
		FrameSettings st;
		uint32_t frame = 0;
	};
	static Slot slot[slots];
	std::vector<std::binary_semaphore*> cpu, gpu;
	for (uint32_t i = 0; i < slots; i++) {
		cpu.push_back(new std::binary_semaphore(0));
		gpu.push_back(new std::binary_semaphore(1));
	}
	std::atomic<bool> bad{ false };

	std::thread render([&] {
		uint32_t idx = 0;
		for (uint32_t f = 0; f < frames; f++) {
			cpu[idx]->acquire();
			const FrameSettings& st = slot[idx].st; // immutable while we hold the slot
			const int32_t k = (int32_t)slot[idx].frame;
			if (!st.valid || st.vp_w != k || st.vp_h != k + 1 || st.window_w != k * 2 || st.limiter.generation != (uint32_t)k ||
				st.light_intensity != (float)(k & 1023))
				bad = true;
			gpu[idx]->release();
			idx = (idx + 1) % slots;
		}
	});

	uint32_t idx = 0;
	gpu[idx]->acquire();
	for (uint32_t f = 0; f < frames; f++) {
		FrameSettings& st = slot[idx].st; // presentFrame: captureSettings()
		st.vp_w = (int32_t)f;
		st.vp_h = (int32_t)f + 1;
		st.window_w = (int32_t)f * 2;
		st.limiter.generation = f;
		st.light_intensity = (float)(f & 1023);
		st.valid = true;
		slot[idx].frame = f;
		cpu[idx]->release();
#ifdef TSAN_NEGATIVE_CONTROL
		st.vp_w = (int32_t)f; // deliberately writes after the release: TSan must report a race
#endif
		idx = (idx + 1) % slots;
		gpu[idx]->acquire();
	}
	render.join();
	CHECK(!bad);
	for (uint32_t i = 0; i < slots; i++) {
		delete cpu[i];
		delete gpu[i];
	}
	std::printf("snapshot handoff: ok (%u frames, latency %u)\n", frames, latency);
}

int main()
{
	testStats();
	testFormat();
	testRing();
	testSnapshotHandoff();
	std::printf(g_fail ? "FAILED: %d\n" : "ALL PASSED\n", g_fail);
	return g_fail ? 1 : 0;
}
