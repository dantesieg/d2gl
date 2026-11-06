#pragma once
#include <windows.h>
void log_open(const char* path);
void log_close();
void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// Logs every HRESULT (success too, at call sites that matter) and returns it.
HRESULT log_hr(HRESULT hr, const char* expr, const char* file, int line);
#define HR(expr) log_hr((expr), #expr, __FILE__, __LINE__)
const char* hr_name(HRESULT hr);
