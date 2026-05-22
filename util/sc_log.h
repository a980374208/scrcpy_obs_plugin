#pragma once

#include <obs.h>

#define scrcpy_log(level, format, ...) \
	blog(level, "[scrcpy_source:] " format, ##__VA_ARGS__)

#define error(format, ...) \
	blog(LOG_ERROR, "[scrcpy_source:] " format, ##__VA_ARGS__)

#ifdef _WIN32
// Log system error (typically returned by GetLastError() or similar)
bool sc_log_windows_error(const char *prefix, int error);
#endif

