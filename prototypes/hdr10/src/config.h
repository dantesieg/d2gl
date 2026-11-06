#pragma once
#include "color_math.h"

struct Config {
	hdr::Params color;
	bool hdr_enabled = true;    // user request; actual output depends on detection
	bool split = true;          // left half: SDR-referenced (input clipped to 1.0), right: full pipeline
	bool fullscreen = false;    // borderless
	bool force_no_interop = false;
	bool cpu_copy = false;      // opt-in: if interop unavailable, use a LABELLED CPU readback copy into D3D11 (not zero-copy)
	int width = 1280, height = 720;
	int frames = 0;             // >0: exit after N frames (automation)
	int cycles = 0;             // >0: repeated init/shutdown stress, then exit
	bool resize_test = false;    // automation: resize + borderless toggle mid-run
	bool simulate_loss = false;  // automation: run the device-loss recovery path (teardown+reinit) mid-run
	bool screenshot = false;     // automation: write the GL-rendered SDR signal of the last frame to shot.ppm
	bool shader_selftest = false;
	bool encode_selftest = false; // GPU HLSL encode vs CPU reference, exit // compile HLSL + GLSL, report, exit
};

// hdr10.ini next to the exe, then command line (--key=value) overrides.
void config_load(Config& c, int argc, char** argv);
void config_log(const Config& c);
const char* tonemap_name(hdr::Tonemap t);
