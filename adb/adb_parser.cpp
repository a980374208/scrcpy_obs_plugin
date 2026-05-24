#include "adb_parser.h"
#include <util/str_util.h>

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
		device.state = get_device_state_from_string(std::string(state));
		device.model = model.empty() ? std::string{} : std::string(model);
		device.selected = false;
	} catch (const std::bad_alloc &) {
		//LOG_OOM();
		return false;
	}

	return true;
}

std::string sc_adb_parse_device_ip(std::string_view output)
{
	std::string_view input = output;
	while (!input.empty()) {
		auto pos = input.find('\n');
		std::string_view line = (pos == std::string_view::npos) ? input : input.substr(0, pos);
		input.remove_prefix(pos == std::string_view::npos ? input.size() : pos + 1);

		// Trim trailing \r or spaces
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
			line.remove_suffix(1);
		}
		if (line.empty()) {
			continue;
		}

		// Split line by whitespace
		std::vector<std::string> tokens;
		std::string current;
		for (char c : line) {
			if (c == ' ' || c == '\t') {
				if (!current.empty()) {
					tokens.push_back(current);
					current.clear();
				}
			} else {
				current.push_back(c);
			}
		}
		if (!current.empty()) {
			tokens.push_back(current);
		}

		// Look for: dev <wlan...> ... src <ip>
		size_t dev_idx = std::string::npos;
		size_t src_idx = std::string::npos;
		for (size_t i = 0; i < tokens.size(); ++i) {
			if (tokens[i] == "dev") {
				dev_idx = i;
			} else if (tokens[i] == "src") {
				src_idx = i;
			}
		}

		if (dev_idx != std::string::npos && src_idx != std::string::npos &&
			dev_idx + 1 < tokens.size() && src_idx + 1 < tokens.size()) {
			std::string dev_name = tokens[dev_idx + 1];
			// Only consider interfaces starting with "wlan"
			if (dev_name.rfind("wlan", 0) == 0) {
				return tokens[src_idx + 1];
			}
		}
	}
	return {};
}

