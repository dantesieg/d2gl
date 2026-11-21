/*
	d2gl_replay: replays a D2GL glide capture (d2gl_capture_###.gcap) through glide3x.dll
	and writes one PNG per frame. Format and workflow: docs/capture/FORMAT.md.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
*/

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

typedef int(__cdecl* replay_t)(const char* capture_path, const char* out_dir, const char* ini_path, int max_frames, int width, int height);

static int usage()
{
	printf(
		"usage: d2gl_replay <capture.gcap> [-o out_dir] [--ini d2gl.ini] [--frames N] [--size WxH] [--dll glide3x.dll]\n"
		"  Run it from a working directory that holds data\\ (d2gl.mpq extracted: shaders, assets) and, optionally,\n"
		"  the hi-res packs (d2gl_hires*.pak). Not from the game directory: the game DLLs must not be found.\n"
		"  out_dir defaults to replay_out; frames are written as out_dir\\frame_00000.png, ...\n"
		"  --ini replaces the settings embedded in the capture; --size the window size of the capture.\n");
	return 2;
}

static const char* errorText(int rc)
{
	switch (rc) {
		case 0: return "ok";
		case 1: return "capture truncated (replayed up to the cut)";
		case 10: return "glide3x.dll did not attach in replay mode";
		case 11: return "cannot open the capture";
		case 12: return "capture too short";
		case 13: return "not a D2GL capture or unsupported version";
		case 14: return "cannot copy the --ini file";
		case 15: return "capture texture memory size not supported";
		case 16: return "cannot create the window";
		case 17: return "OpenGL context / wrapper setup failed";
		case 18: return "timed out waiting for the last screenshots";
	}
	return "unknown error";
}

static std::string fullPath(const char* path)
{
	char buf[MAX_PATH] = { 0 };
	const DWORD n = GetFullPathNameA(path, MAX_PATH, buf, nullptr);
	return n && n < MAX_PATH ? std::string(buf) : std::string(path);
}

int main(int argc, char** argv)
{
	const char* cap = nullptr;
	const char* ini = nullptr;
	const char* dll = nullptr;
	const char* out = "replay_out";
	int frames = 0, width = 0, height = 0;

	for (int i = 1; i < argc; i++) {
		const bool more = i + 1 < argc;
		if (!strcmp(argv[i], "-o") && more)
			out = argv[++i];
		else if (!strcmp(argv[i], "--ini") && more)
			ini = argv[++i];
		else if (!strcmp(argv[i], "--dll") && more)
			dll = argv[++i];
		else if (!strcmp(argv[i], "--frames") && more)
			frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--size") && more) {
			if (sscanf_s(argv[++i], "%dx%d", &width, &height) != 2)
				return usage();
		} else if (argv[i][0] == '-')
			return usage();
		else if (!cap)
			cap = argv[i];
		else
			return usage();
	}
	if (!cap)
		return usage();

	const std::string cap_path = fullPath(cap);
	const std::string out_path = fullPath(out);
	const std::string ini_path = ini ? fullPath(ini) : std::string();
	if (!CreateDirectoryA(out_path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
		printf("cannot create %s\n", out_path.c_str());
		return 3;
	}

	std::string dll_path;
	if (dll)
		dll_path = fullPath(dll);
	else {
		char exe[MAX_PATH] = { 0 };
		GetModuleFileNameA(nullptr, exe, MAX_PATH);
		dll_path = exe;
		dll_path = dll_path.substr(0, dll_path.find_last_of("\\/") + 1) + "glide3x.dll";
	}

	SetEnvironmentVariableA("D2GL_REPLAY", "1"); // read by the DLL's attach: no game hooks, no version check
	HMODULE module = LoadLibraryA(dll_path.c_str());
	if (!module) {
		printf("cannot load %s (error %lu)\n", dll_path.c_str(), GetLastError());
		return 4;
	}
	const auto replay = (replay_t)GetProcAddress(module, "d2gl_replay");
	if (!replay) {
		printf("%s has no d2gl_replay export (older D2GL?)\n", dll_path.c_str());
		return 5;
	}

	const int rc = replay(cap_path.c_str(), out_path.c_str(), ini_path.c_str(), frames, width, height);
	printf("d2gl_replay: %s (%d)\n", errorText(rc), rc);
	fflush(stdout);
	// skip DLL teardown (render thread, GL context): nothing in it is needed after the PNGs are written
	TerminateProcess(GetCurrentProcess(), (UINT)rc);
	return rc;
}
