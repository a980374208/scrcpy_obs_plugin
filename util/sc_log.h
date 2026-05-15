#pragma once

#ifdef _WIN32
// Log system error (typically returned by GetLastError() or similar)
bool sc_log_windows_error(const char *prefix, int error);
#endif
