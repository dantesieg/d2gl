// D2GL build stamp: CI (.github/workflows/build.yml) writes build_stamp_ci.h before msbuild; local builds say "unknown"
#pragma once

#if __has_include("build_stamp_ci.h")
#include "build_stamp_ci.h"
#endif
#ifndef D2GL_GIT_HASH
#define D2GL_GIT_HASH "unknown"
#endif
#ifndef D2GL_BUILD_DATE
#define D2GL_BUILD_DATE __DATE__
#endif
#define D2GL_VERSION_STRING D2GL_GIT_HASH " built " D2GL_BUILD_DATE
