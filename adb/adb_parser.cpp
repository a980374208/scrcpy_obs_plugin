#include "adb_parser.h"

bool sc_adb_parse_devices(std::string_view input, std::vector<sc_adb_device> &out)
{
	constexpr std::string_view HEADER = "List of devices attached";

	bool header_found = false;

	while (!input.empty()) {
		// 取一行
		auto pos = input.find('\n');
		std::string_view line = (pos == std::string_view::npos) ? input : input.substr(0, pos);

		// 移动到下一行
		input.remove_prefix(pos == std::string_view::npos ? input.size() : pos + 1);

		// 去掉尾部 '\r' 和空格
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
			line.remove_suffix(1);
		}

		if (!header_found) {
			if (line == HEADER) {
				header_found = true;
			}
			continue; // header之前忽略
		}

		// 忽略空行
		if (line.empty()) {
			continue;
		}

		sc_adb_device device{};
		
		if (!sc_adb_parse_device(line, device)) {
			continue;
		}
		
		try {
			out.push_back(std::move(device));
		} catch (const std::bad_alloc &) {
			// LOG_OOM();
			continue;
		}
	}

	assert(header_found || out.empty());
	return header_found;
}

bool sc_adb_parse_device(std::string_view line, sc_adb_device &device)
{
	// 1. 忽略 adb daemon 垃圾行
	if (line.empty() || line.front() == '*') {
		return false;
	}

	// C++17 兼容的 starts_with 替代
	constexpr char adb_server_prefix[] = "adb server";
	if (line.size() >= sizeof(adb_server_prefix) - 1 &&
	    line.compare(0, sizeof(adb_server_prefix) - 1, adb_server_prefix) == 0) {
		return false;
	}

	// ---------- parse serial ----------
	auto serial_end = line.find_first_of(" \t");
	if (serial_end == std::string_view::npos || serial_end == 0) {
		return false; // empty serial or serial alone
	}

	std::string_view serial = line.substr(0, serial_end);
	line.remove_prefix(serial_end + 1);
	line.remove_prefix(line.find_first_not_of(" \t"));

	if (line.empty()) {
		return false;
	}

	// ---------- parse state ----------
	auto state_end = line.find(' ');
	std::string_view state = (state_end == std::string_view::npos) ? line : line.substr(0, state_end);

	if (state.empty()) {
		return false;
	}

	std::string_view rest = (state_end == std::string_view::npos) ? std::string_view{} : line.substr(state_end + 1);

	// ---------- parse properties ----------
	std::string_view model;

	while (!rest.empty()) {
		auto token_end = rest.find(' ');
		std::string_view token = (token_end == std::string_view::npos) ? rest : rest.substr(0, token_end);

		// C++17 兼容 starts_with 替代
		constexpr char model_prefix[] = "model:";
		if (token.size() >= sizeof(model_prefix) - 1 &&
		    token.compare(0, sizeof(model_prefix) - 1, model_prefix) == 0) {
			model = token.substr(sizeof(model_prefix) - 1);
			break;
		}

		if (token_end == std::string_view::npos) {
			break;
		}
		rest.remove_prefix(token_end + 1);
	}

	// ---------- fill device ----------
	try {
		device.serial = std::string(serial);
		device.state = std::string(state);
		device.model = model.empty() ? std::string{} : std::string(model);
		device.selected = false;
	} catch (const std::bad_alloc &) {
		//LOG_OOM();
		return false;
	}

	return true;
}

bool sc_adb_parse_device_2(std::string_view line, sc_adb_device &device)
{
	// 1. 首先执行基础解析 (Serial, State, Model)
	// 如果基础解析失败，直接返回 false
	if (!sc_adb_parse_device(line, device)) {
		return false;
	}

	// 2. 提取增强字段
	// 我们需要重新获取 rest 部分，或者从原始 line 中查找
	// 为了简单和健壮，直接在原始 line 中查找特定关键字

	// 辅助 Lambda：在整行中查找 key 并提取值
	auto extract_from_line = [&](std::string_view key, bool bracketed = false) -> std::string {
		auto pos = line.find(key);
		if (pos == std::string_view::npos)
			return "";

		std::string_view val_part = line.substr(pos + key.size());

		// 跳过冒号和空格
		size_t offset = 0;
		if (!val_part.empty() && val_part[0] == ':')
			offset++;
		while (offset < val_part.size() && (val_part[offset] == ' ' || val_part[offset] == '\t')) {
			offset++;
		}
		if (offset >= val_part.size())
			return "";

		val_part.remove_prefix(offset);

		if (bracketed) {
			if (val_part.empty() || val_part[0] != '[')
				return "";
			val_part.remove_prefix(1);
			auto end_bracket = val_part.find(']');
			if (end_bracket == std::string_view::npos)
				return "";
			return std::string(val_part.substr(0, end_bracket));
		} else {
			auto end_pos = val_part.find_first_of(" \t");
			if (end_pos == std::string_view::npos) {
				return std::string(val_part);
			}
			return std::string(val_part.substr(0, end_pos));
		}
	};

	try {
		// 提取 better_name:[...]
		device.best_name = extract_from_line("better_name:", true);

		// 提取 Physical size: ...
		device.physical_size = extract_from_line("Physical size:", false);
	} catch (const std::bad_alloc &) {
		return false;
	}

	return true;
}
