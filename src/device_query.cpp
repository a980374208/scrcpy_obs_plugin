#include "device_query.h"

#include "adb/adb.h"
#include "server/server.hpp"
#include "util/process_intr.h"
#include "util/sc_file.h"
#include "util/str_util.h"
#include <nlohmann/json.hpp>
#include <vector>

static sc_device_query_status map_process_failure(sc_process_intr_result result,
							   sc_device_query_status fallback)
{
	switch (result) {
	case SC_PROCESS_INTR_CANCELLED:
		return sc_device_query_status::cancelled;
	case SC_PROCESS_INTR_TIMED_OUT:
		return sc_device_query_status::timed_out;
	case SC_PROCESS_INTR_SUCCESS:
		return sc_device_query_status::success;
	case SC_PROCESS_INTR_ERROR:
	default:
		return fallback;
	}
}

static bool parse_device_info(const std::string &serial, const sc_adb_device &adb_device,
				      std::string_view json, sc_adb_device_info &info)
{
	try {
		auto root = nlohmann::json::parse(json);
		if (!root.is_object()) return false;
		info = {};
		info.device = adb_device;
		info.device.serial = serial;
		info.device.selected = false;

		if (root.contains("state") && root["state"].is_string()) {
			info.device.state = get_device_state_from_string(root["state"].get<std::string>());
		}
		if (root.contains("device") && root["device"].is_string()) {
			info.device.model = root["device"].get<std::string>();
		}
		info.best_name = info.device.model;

		if (root.contains("display") && root["display"].is_array()) {
			for (const auto &display : root["display"]) {
				sc_adb_display parsed{};
				if (display.contains("id")) {
					if (display["id"].is_number_unsigned()) {
						parsed.id = display["id"].get<uint32_t>();
					} else if (display["id"].is_number_integer()) {
						int value = display["id"].get<int>();
						if (value < 0) return false;
						parsed.id = static_cast<uint32_t>(value);
					} else if (display["id"].is_string()) {
						parsed.id = static_cast<uint32_t>(std::stoul(display["id"].get<std::string>()));
					}
				}
				if (display.contains("size") && display["size"].is_string()) {
					parsed.physical_size = display["size"].get<std::string>();
				}
				if (display.contains("fps")) {
					if (display["fps"].is_number_unsigned()) {
						parsed.fps = display["fps"].get<uint32_t>();
					} else if (display["fps"].is_number_integer()) {
						int value = display["fps"].get<int>();
						if (value < 0) return false;
						parsed.fps = static_cast<uint32_t>(value);
					} else if (display["fps"].is_string()) {
						parsed.fps = static_cast<uint32_t>(std::stoul(display["fps"].get<std::string>()));
					}
				}
				info.displays.emplace(parsed.id, std::move(parsed));
			}
		}

		if (root.contains("camera") && root["camera"].is_array()) {
			for (const auto &camera : root["camera"]) {
				sc_adb_camera parsed{};
				if (camera.contains("id")) {
					if (camera["id"].is_number_integer()) {
						parsed.id = std::to_string(camera["id"].get<int>());
					} else if (camera["id"].is_string()) {
						parsed.id = camera["id"].get<std::string>();
					}
				}
				if (parsed.id.empty()) return false;

				parsed.facing = SC_CAMERA_FACING_ANY;
				if (camera.contains("facing") && camera["facing"].is_string()) {
					std::string facing = camera["facing"].get<std::string>();
					if (facing == "front") parsed.facing = SC_CAMERA_FACING_FRONT;
					else if (facing == "back") parsed.facing = SC_CAMERA_FACING_BACK;
					else if (facing == "external") {
						parsed.facing = SC_CAMERA_FACING_EXTERNAL;
						info.has_external = true;
					}
				}
				if (camera.contains("size") && camera["size"].is_array()) {
					for (const auto &size : camera["size"]) {
						if (size.is_string()) parsed.suport_sizes.push_back(size.get<std::string>());
					}
				}
				if (camera.contains("fps") && camera["fps"].is_array()) {
					for (const auto &fps : camera["fps"]) {
						if (fps.is_number_integer()) parsed.suport_fps.push_back(fps.get<int16_t>());
					}
				}
				info.cameras.emplace(parsed.id, std::move(parsed));
			}
		}
		return true;
	} catch (const std::exception &) {
		return false;
	}
}

sc_device_query::sc_device_query(sc_tick timeout)
	: deadline_(sc_tick_now() + (timeout > 0 ? timeout : 0))
{
}

void sc_device_query::cancel()
{
	intr_.intr_interrupt();
}

sc_device_query_result sc_device_query::run()
{
	sc_device_query_result result;
	if (!sc_adb_init()) {
		result.status = sc_device_query_status::list_failed;
		return result;
	}
	if (sc_tick_now() >= deadline_) {
		cancel();
		result.status = sc_device_query_status::timed_out;
		return result;
	}

	sc_vec_adb_devices devices;
	sc_process_intr_result list_result = sc_adb_list_devices_until(intr_, 0, devices, deadline_);
	if (list_result != SC_PROCESS_INTR_SUCCESS) {
		result.status = map_process_failure(list_result, sc_device_query_status::list_failed);
		return result;
	}

	if (devices.empty()) {
		result.status = sc_device_query_status::success;
		return result;
	}

	std::string server_path;
	for (const auto &device : devices) {
		result.failed_serial = device.serial;
		if (device.state != DEVICE_STATE_DEVICE) {
			sc_adb_device_info info{};
			info.device = device;
			info.best_name = device.model;
			result.device_infos.emplace(device.serial, std::move(info));
			continue;
		}

		if (server_path.empty()) {
			server_path = sc_server::get_server_path();
			if (server_path.empty() || !sc_file_is_regular(server_path)) {
				result.status = sc_device_query_status::server_missing;
				return result;
			}
		}

		if (pushed_serials_.find(device.serial) == pushed_serials_.end()) {
			sc_process_intr_result push_result = sc_adb_push_until(
				intr_, device.serial, server_path, sc_server::get_device_server_path(), 0, deadline_);
			if (push_result != SC_PROCESS_INTR_SUCCESS) {
				result.status = map_process_failure(push_result, sc_device_query_status::push_failed);
				return result;
			}
			pushed_serials_.insert(device.serial);
		}

		sc_pipe output = SC_PROCESS_NONE;
		sc_pid pid = sc_server::execute_device_info(device.serial, &output);
		if (pid == SC_PROCESS_NONE) {
			result.status = sc_device_query_status::probe_start_failed;
			return result;
		}

		std::vector<char> buffer(BUFSIZE);
		ssize_t count;
		sc_process_intr_result read_result = sc_pipe_read_all_intr_until(
			intr_, pid, output, buffer.data(), buffer.size() - 1, deadline_, count);
		sc_pipe_close(output);
		if (read_result != SC_PROCESS_INTR_SUCCESS) {
			sc_process_terminate(pid);
			sc_process_close(pid);
			result.status = map_process_failure(read_result, sc_device_query_status::probe_read_failed);
			return result;
		}
		if (count < 0 || static_cast<size_t>(count) >= buffer.size() - 1) {
			sc_process_terminate(pid);
			sc_process_close(pid);
			result.status = sc_device_query_status::probe_read_failed;
			return result;
		}

		sc_process_intr_result wait_result = process_check_success_intr_until(
			intr_, pid, "adb device info probe", 0, deadline_);
		if (wait_result != SC_PROCESS_INTR_SUCCESS) {
			result.status = map_process_failure(wait_result, sc_device_query_status::probe_exit_failed);
			return result;
		}
		sc_adb_device_info info{};
		if (!parse_device_info(device.serial, device,
				       std::string_view(buffer.data(), static_cast<size_t>(count)), info)) {
			result.status = sc_device_query_status::parse_failed;
			return result;
		}
		result.device_infos.emplace(device.serial, std::move(info));
	}

	result.failed_serial.clear();
	result.status = sc_device_query_status::success;
	return result;
}

const char *sc_device_query_status_name(sc_device_query_status status)
{
	switch (status) {
	case sc_device_query_status::success: return "success";
	case sc_device_query_status::cancelled: return "cancelled";
	case sc_device_query_status::timed_out: return "timed out";
	case sc_device_query_status::list_failed: return "device list failed";
	case sc_device_query_status::server_missing: return "server file missing";
	case sc_device_query_status::push_failed: return "server push failed";
	case sc_device_query_status::probe_start_failed: return "device probe start failed";
	case sc_device_query_status::probe_read_failed: return "device probe read failed";
	case sc_device_query_status::probe_exit_failed: return "device probe process failed";
	case sc_device_query_status::parse_failed: return "device probe response invalid";
	default: return "unknown";
	}
}
