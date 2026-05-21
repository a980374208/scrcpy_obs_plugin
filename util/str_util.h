#ifndef SCRCPY_STR_UTIL_H
#define SCRCPY_STR_UTIL_H

#include <stddef.h>
#include <vector>
#include <string>
#include <optional>
#include <obs.h>
#include "util/dstr.hpp"
#include "adb/adb_device.h"
#include <sstream>

// 安全复制字符串
char *str_util_strdup(const char *str);

// 连接两个字符串
char *str_util_strcat(const char *str1, const char *str2);

// 格式化字符串（跨平台兼容）
int str_util_snprintf(char *str, size_t size, const char *format, ...);

// 去除字符串前后空格
char *str_util_trim(char *str);

// UTF-8 转宽字符
wchar_t *sc_str_to_wchars(const char *utf8);

// 拼接字符串
std::string sc_str_join(const std::vector<std::string> &tokens, char sep);

// 给字符串加引号
std::string sc_str_quote(const std::string &src);

std::optional<long> sc_str_parse_integer(std::string_view s);


static inline bool ConvertRes(int &cx, int &cy, const char *res)
{
	return sscanf(res, "%dx%d", &cx, &cy) == 2;
}

static inline bool ResolutionValid(const std::string &res, int &cx, int &cy)
{
	if (!res.size())
		return false;

	return ConvertRes(cx, cy, res.c_str());
}

static bool AddDevice(obs_property_t *device_list, const sc_adb_device_info &device_info)
{
	DStr serial, state, model, physical_size, best_name, device_id;

	dstr_from_mbs(serial, device_info.device.serial.c_str());
	dstr_from_mbs(best_name, device_info.best_name.c_str());


	obs_property_list_add_string(device_list, best_name, serial);
	return true;
}

inline enum device_connect_state get_device_state_from_string(const std::string &state_str)
{
	if (state_str == "offline") {
		return DEVICE_STATE_OFFLINE;
	} else if (state_str == "bootloader") {
		return DEVICE_STATE_BOOTLOADER;
	} else if (state_str == "device") {
		return DEVICE_STATE_DEVICE;
	} else if (state_str == "recovery") {
		return DEVICE_STATE_RECOVERY;
	} else if (state_str == "unauthorized") {
		return DEVICE_STATE_UNAUTHORIZE;
	} else if (state_str == "sideload") {
		return DEVICE_STATE_SIDELOAD;
	} else {
		return DEVICE_STATE_UNKNOWN;
	}
}

#endif // SCRCPY_STR_UTIL_H
