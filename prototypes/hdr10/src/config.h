#pragma once
#include "color_math.h"

struct Config {
	hdr::Params color;
	bool hdr_enabled = true;    // user request; actual output depends on detection
	bool split = true;          // left half: SDR-referenced (input clipped to 1.0), right: full pipeline
	bool fullscreen = false;    // borderless
	bool force_no_interop = false;
	int width = 1280, height = 720;
	int frames = 0;             // >0: exit after N frames (automation)
	int cycles = 0;             // >0: repeated init/shutdown stress, then exit
	bool shader_selftest = false; // compile HLSL + GLSL, report, exit
};

// hdr10.ini next to the exe, then command line (--key=value) overrides.
void config_load(Config& c, int argc, char** argv);
void config_log(const Config& c);
const char* tonemap_name(hdr::Tonemap t);
