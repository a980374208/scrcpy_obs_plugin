#pragma once

#include <string>
#include <vector>
#include <string_view>
#include <sstream>
#include <cstdint>
#include <cassert>
#include "util/options.h"

static const char *log_level_to_server_string(enum sc_log_level level)
{
	switch (level) {
	case SC_LOG_LEVEL_VERBOSE:
		return "verbose";
	case SC_LOG_LEVEL_DEBUG:
		return "debug";
	case SC_LOG_LEVEL_INFO:
		return "info";
	case SC_LOG_LEVEL_WARN:
		return "warn";
	case SC_LOG_LEVEL_ERROR:
		return "error";
	default:
		assert(!"unexpected log level");
		return NULL;
	}
}

static inline bool validate_string(const std::string &s)
{
	// The parameters values are passed as command line arguments to adb, so
	// they must either be properly escaped, or they must not contain any
	// special shell characters.
	// Since they are not properly escaped on Windows anyway (see
	// sys/win/process.c), just forbid special shell characters.
	static const char *invalid = " ;'\"*$?&`#\\|<>[]{}()!~\r\n";
	if (s.find_first_of(invalid) != std::string::npos) {
		//LOGE("Invalid server param: [%s]", s.c_str());
		return false;
	}
	return true;
}

struct sc_server_params;

class ScServerCmdBuilder {
public:
	explicit ScServerCmdBuilder(std::vector<std::string> &out);

	// 基础添加
	template<typename T> void add(std::string_view key, const T &value);

	// bool 参数（与默认值不同才输出）
	void add_bool(std::string_view key, bool value, bool default_value = true);

	// 需要校验的字符串参数
	bool add_validated(std::string_view key, const char *value);

	// hex 参数
	void add_hex(std::string_view key, uint32_t value);

	// 高层：从 params 批量构建
	bool build_from_params(const sc_server_params &params, bool tunnel_forward);

private:
	std::vector<std::string> &cmd;
};

// 模板必须放在 header
template<typename T> inline void ScServerCmdBuilder::add(std::string_view key, const T &value)
{
	std::ostringstream ss;
	ss << key << value;
	cmd.emplace_back(ss.str());
}