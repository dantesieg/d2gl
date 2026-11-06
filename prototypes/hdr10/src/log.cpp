#include "log.h"
#include <cstdarg>
#include <cstdio>
#include <dxgi.h>

static FILE* g_log = nullptr;

void log_open(const char* path) { g_log = std::fopen(path, "w"); }
void log_close() { if (g_log) std::fclose(g_log); g_log = nullptr; }

void logf(const char* fmt, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	std::printf("%s\n", buf);
	std::fflush(stdout);
	if (g_log) { std::fprintf(g_log, "%s\n", buf); std::fflush(g_log); }
}

const char* hr_name(HRESULT hr) {
	switch (hr) {
	case S_OK: return "S_OK";
	case S_FALSE: return "S_FALSE";
	case E_FAIL: return "E_FAIL";
	case E_INVALIDARG: return "E_INVALIDARG";
	case E_NOINTERFACE: return "E_NOINTERFACE";
	case E_NOTIMPL: return "E_NOTIMPL";
	case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
	case DXGI_ERROR_DEVICE_REMOVED: return "DXGI_ERROR_DEVICE_REMOVED";
	case DXGI_ERROR_DEVICE_RESET: return "DXGI_ERROR_DEVICE_RESET";
	case DXGI_ERROR_DEVICE_HUNG: return "DXGI_ERROR_DEVICE_HUNG";
	case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
	case DXGI_ERROR_INVALID_CALL: return "DXGI_ERROR_INVALID_CALL";
	case DXGI_ERROR_NOT_FOUND: return "DXGI_ERROR_NOT_FOUND";
	case DXGI_ERROR_UNSUPPORTED: return "DXGI_ERROR_UNSUPPORTED";
	case DXGI_STATUS_OCCLUDED: return "DXGI_STATUS_OCCLUDED";
	default: return "?";
	}
}

HRESULT log_hr(HRESULT hr, const char* expr, const char* file, int line) {
	const char* f = file;
	for (const char* q = file; *q; ++q) if (*q == '/' || *q == '\\') f = q + 1;
	logf("%s hr=0x%08lX %s  <- %s (%s:%d)", FAILED(hr) ? "[HR-FAIL]" : "[hr]", (unsigned long)hr, hr_name(hr), expr, f, line);
	return hr;
}
