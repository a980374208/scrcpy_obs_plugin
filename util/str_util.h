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
	dstr_from_mbs(state, device_info.device.state.c_str());
	dstr_from_mbs(model, device_info.device.model.c_str());
	dstr_from_mbs(best_name, device_info.best_name.c_str());
	dstr_from_mbs(physical_size, device_info.physical_size.c_str());

	dstr_copy_dstr(device_id, serial);
	dstr_cat(device_id, ":");
	dstr_cat_dstr(device_id, state);
	dstr_cat(device_id, ":");
	dstr_cat_dstr(device_id, model);
	dstr_cat(device_id, ":");
	dstr_cat_dstr(device_id, best_name);
	dstr_cat(device_id, ":");
	dstr_cat_dstr(device_id, physical_size);
	dstr_catf(device_id, ":%d", device_info.max_fps);
	dstr_catf(device_id, ":%s", device_info.has_external ? "true" : "false");

	obs_property_list_add_string(device_list, device_info.best_name.c_str(), device_id);
	return true;
}

static sc_adb_device_info DecodeDevice(const std::string &value)
{
	std::vector<std::string> f;
	std::stringstream ss(value);
	std::string token;
	while (std::getline(ss, token, ':'))
		f.push_back(std::move(token));

	if (f.size() < 7)
		throw std::runtime_error("Invalid device info");

	int max_fps = 0;
	try {
		max_fps = std::stoi(f[5]);
	} catch (...) {
		throw std::runtime_error("Invalid max_fps in device info");
	}

	bool has_external = (f[6] == "true" || f[6] == "1");

	return sc_adb_device_info{{f[0], f[1], f[2], false}, // device: serial/state/model
				  std::move(f[3]),           // best_name
				  std::move(f[4]),           // physical_size
				  max_fps,
				  has_external};
}

#endif // SCRCPY_STR_UTIL_H
