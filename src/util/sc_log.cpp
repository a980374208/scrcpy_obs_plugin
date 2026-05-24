#include "sc_log.h"
#include "assert.h"
#if _WIN32
#include <windows.h>
#endif
#include "obs.h"


#ifdef _WIN32
bool sc_log_windows_error(const char *prefix, int error)
{
	assert(prefix);

	char *message;
	DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM;
	DWORD lang_id = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
	int ret = FormatMessageA(flags, NULL, error, lang_id, (char *)&message, 0, NULL);
	if (ret <= 0) {
		return false;
	}

	// Note: message already contains a trailing '\n'
	//LOGE("%s: [%d] %s", prefix, error, message);
	LocalFree(message);
	return true;
}
#endif

