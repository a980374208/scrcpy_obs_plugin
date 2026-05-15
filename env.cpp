#include "env.h"
#include <stdlib.h>
#include <string.h>
#include <locale>
#include <codecvt>

std::string sc_get_env(const std::string &varname)
{
	/* std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;

#ifdef _WIN32
	std::wstring w_varname = conv.from_bytes(varname);
    
    const wchar_t* value = _wgetenv(w_varname.c_str());
    if (!value) {
        return "";
    } 
#else
    const char* value = getenv(varname);
    if (!value) {
        return NULL;
    }

#endif
    return conv.to_bytes(value);*/
	const char *value = std::getenv(varname.c_str());
	if (!value)
		return "";
	return std::string(value);
}
