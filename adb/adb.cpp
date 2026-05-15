#include "adb.h"
#include "env.h"
#include <vector>
#include "util/process_intr.h"
#include "assert.h"
#include "adb_parser.h"
#include "str_util.h"
#include <sstream>

#define SC_ADB_COMMAND(...) { sc_adb_get_executable(), __VA_ARGS__ }

    const char *DEVICE_INFO = "shell sh -c \""
        "printf 'VENDOR=%s\\n' \\\"$(getprop ro.product.vendor.marketname)\\\"; "
        "printf 'ODM=%s\\n' \\\"$(getprop ro.product.odm.marketname)\\\"; "
        "wm size | head -n 1"
        "\"";

static std::string adb_executable = "";
bool sc_adb_init()
{

	adb_executable = sc_get_env("ADB");
	if (!adb_executable.empty()) {
		//LOGD("Using adb: %s", adb_executable.c_str());
		return true;
	}

#if !defined(PORTABLE) || defined(_WIN32)
	adb_executable = "adb";
#else
	// For portable builds, use the absolute path to the adb executable
	// in the same directory as scrcpy (except on Windows, where "adb"
	// is sufficient)
	adb_executable = sc_file_get_local_path("adb");
	if (!adb_executable) {
		// Error already logged
		return false;
	}

	LOGD("Using adb (portable): %s", adb_executable);
#endif
	return true;
}

std::string sc_adb_get_executable(void)
{
	return adb_executable;
}

sc_pid sc_adb_execute(const std::vector<std::string> &commands, unsigned flags)
{
	return sc_adb_execute_p(commands, flags, NULL);
}

sc_pid sc_adb_execute_p(const std::vector<std::string> &commands, unsigned flags, sc_pipe *pout)
{
	unsigned process_flags = 0;
	if (flags & SC_ADB_NO_STDOUT) {
		process_flags |= SC_PROCESS_NO_STDOUT;
	}
	if (flags & SC_ADB_NO_STDERR) {
		process_flags |= SC_PROCESS_NO_STDERR;
	}

	sc_pid pid;
	enum sc_process_result r = sc_process_execute_p(commands, &pid, process_flags, NULL, pout, NULL);
	if (r != SC_PROCESS_SUCCESS) {
		// If the execution itself failed (not the command exit code), log the
		// error in all cases
		//show_adb_err_msg(r, argv);
		pid = SC_PROCESS_NONE;
	}

	return pid;
}

bool sc_adb_start_server(sc_intr &intr, unsigned flags)
{
	std::vector<std::string> commands = SC_ADB_COMMAND("start-server");
	sc_pid pid = sc_adb_execute(commands, flags);

	return process_check_success_intr(intr, pid, "adb start-server", flags);
}

bool sc_adb_list_devices(sc_intr &intr, unsigned flags, sc_vec_adb_devices &out_vec, bool extern_str)
{
	std::vector<std::string> commands = SC_ADB_COMMAND("devices", "-l");
	

#define BUFSIZE 65536
	std::vector<char> buf(BUFSIZE);
	size_t r; 
	auto get_device_info = [&]( std::vector<char> &buf, unsigned flags,
				   size_t& r,const char *shell) {
		buf.clear();
		buf.resize(BUFSIZE);
		std::vector<std::string> commands = SC_ADB_COMMAND(shell);
		sc_pipe pout;
		sc_pid pid = sc_adb_execute_p(commands, flags, &pout);
		if (pid == SC_PROCESS_NONE) {
			// LOGE("Could not execute adb devices -l");
			return false;
		}
		r = sc_pipe_read_all_intr(intr, pid, pout, buf.data(), BUFSIZE - 1);
		sc_pipe_close(pout);
		bool ok = process_check_success_intr(intr, pid, "adb -s get_device_info", flags);

		if (!ok) {
			return false;
		}

		if (r == -1 || r >= BUFSIZE - 1) {
			// LOGE(" too large");
			return false;
		}
		return true;
	};
	get_device_info(buf, flags, r, "devices -l");
	bool ok = sc_adb_parse_devices(std::string_view(buf.data(), r), out_vec);

	
	std::string tmp = {DEVICE_INFO};
	if (extern_str) {
		for (auto &device : out_vec) {
			std::string shell = " -s " + device.serial + " " + tmp;
			get_device_info(buf, flags, r, DEVICE_INFO);
			std::string out(buf.data(), r);

			// 解析输出
			std::string vendor;
			std::string odm;
			std::string wm;
			std::istringstream iss(out);
			std::string line;

			auto trim = [](std::string &s) {
				while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
					s.pop_back();
				while (!s.empty() && s.front() == ' ')
					s.erase(s.begin());
			};

			while (std::getline(iss, line)) {
				trim(line);

				if (line.rfind("VENDOR=", 0) == 0) {
					vendor = line.substr(7);
					trim(vendor);
				} else if (line.rfind("ODM=", 0) == 0) {
					odm = line.substr(4);
					trim(odm);
				} else if (wm.empty()) {
					if (line.rfind("Physical size:", 0) == 0 ||
					    line.rfind("Override size:", 0) == 0) {

						auto pos = line.find(':');
						if (pos != std::string::npos) {
							wm = line.substr(pos + 1);
							trim(wm);
						}
					}
				}
			}
			if (odm.length() > 0) {
				device.best_name = odm;
			} else if (vendor.length() > 0) {
				device.best_name = vendor;
			} else {
				device.best_name = device.model;
			}
			device.physical_size = wm; 
		}
		return ok;
	}
	return ok;
}

bool sc_adb_select_device(sc_intr &intr, const sc_adb_device_selector &selector, unsigned flags,
			  sc_adb_device &out_device)
{
	sc_vec_adb_devices vec;
	bool ok = sc_adb_list_devices(intr, flags, vec);
	if (!ok) {
		//LOGE("Could not list ADB devices");
		return false;
	}

	if (vec.size() == 0) {
		//LOGE("Could not find any ADB device");
		return false;
	}
	size_t sel_idx; // index of the single matching device if sel_count == 1
	size_t sel_count = sc_adb_devices_select(vec, vec.size(), selector, &sel_idx);

	if (sel_count == 0) {
		assert(selector.type != SC_ADB_DEVICE_SELECT_ALL);

		switch (selector.type) {
		case SC_ADB_DEVICE_SELECT_SERIAL:
			assert(!selector.serial.empty());
			//LOGE("Could not find ADB device %s:", selector.serial.c_str()  );
			break;
		case SC_ADB_DEVICE_SELECT_USB:
			//LOGE("Could not find any ADB device over USB:");
			break;
		case SC_ADB_DEVICE_SELECT_TCPIP:
			//LOGE("Could not find any ADB device over TCP/IP:");
			break;
		default:
			assert(!"Unexpected selector type");
			break;
		}

		//sc_adb_devices_log(SC_LOG_LEVEL_ERROR, vec.data, vec.size);
		return false;
	}
	if (sel_count > 1) {
		switch (selector.type) {
		case SC_ADB_DEVICE_SELECT_ALL:
			//LOGE("Multiple (%" SC_PRIsizet ") ADB devices:", sel_count);
			break;
		case SC_ADB_DEVICE_SELECT_SERIAL:
			assert(!selector.serial.empty());
			//LOGE("Multiple (%" SC_PRIsizet ") ADB devices with serial %s:",
			//    sel_count, selector.serial.c_str());
			break;
		case SC_ADB_DEVICE_SELECT_USB:
			//LOGE("Multiple (%" SC_PRIsizet ") ADB devices over USB:",
			//    sel_count);
			break;
		case SC_ADB_DEVICE_SELECT_TCPIP:
			//LOGE("Multiple (%" SC_PRIsizet ") ADB devices over TCP/IP:",
			//    sel_count);
			break;
		default:
			assert(!"Unexpected selector type");
			break;
		}
		//sc_adb_devices_log(SC_LOG_LEVEL_ERROR, vec.data, vec.size);
		//LOGE("Select a device via -s (--serial), -d (--select-usb) or -e "
		//    "(--select-tcpip)");
		return false;
	}

	assert(sel_count == 1); // sel_idx is valid only if sel_count == 1
	struct sc_adb_device &device = vec[sel_idx];

	ok = sc_adb_device_check_state(device, vec);
	if (!ok) {
		return false;
	}

	//LOGI("ADB device found:");
	//sc_adb_devices_log(SC_LOG_LEVEL_INFO, vec.data, vec.size);

	// Move devics into out_device (do not destroy device)
	out_device = device;
	return true;
}

static bool process_check_success_intr(sc_intr &intr, sc_pid pid, const char *name, unsigned flags)
{
	if (!intr.set_process(pid)) {
		// Already interrupted
		return false;
	}

	// Always pass close=false, interrupting would be racy otherwise
	bool ret = process_check_success_internal(pid, name, false, flags);

	intr.set_process(SC_PROCESS_NONE);
	// Close separately
	sc_process_close(pid);

	return ret;
}

static bool process_check_success_internal(sc_pid pid, const char *name, bool close, unsigned flags)
{
	bool log_errors = !(flags & SC_ADB_NO_LOGERR);

	if (pid == SC_PROCESS_NONE) {
		if (log_errors) {
			//LOGE("Could not execute \"%s\"", name);
		}
		return false;
	}
	sc_exit_code exit_code = sc_process_wait(pid, close);
	if (exit_code) {
		if (log_errors) {
			if (exit_code != SC_EXIT_CODE_NONE) {
				//LOGE("\"%s\" returned with value %" SC_PRIexitcode, name,
				//    exit_code);
			} else {
				//LOGE("\"%s\" exited unexpectedly", name);
			}
		}
		return false;
	}
	return true;
}

static size_t sc_adb_devices_select(const sc_vec_adb_devices &devices, size_t len, const sc_adb_device_selector &selector,
			     size_t *idx_out)
{
	size_t count = 0;
	for (size_t i = 0; i < len; ++i) {
		struct sc_adb_device device = devices[i];
		device.selected = sc_adb_accept_device(device, selector);
		if (device.selected) {
			if (idx_out && !count) {
				*idx_out = i;
			}
			++count;
		}
	}

	return count;
}

static bool sc_adb_accept_device(const sc_adb_device &device, const sc_adb_device_selector &selector)
{
	switch (selector.type) {
	case SC_ADB_DEVICE_SELECT_ALL:
		return true;

	case SC_ADB_DEVICE_SELECT_SERIAL: {
		assert(!selector.serial.empty());

		auto pos_device_colon = device.serial.find(':');
		auto pos_selector_colon = selector.serial.find(':');

		if (pos_device_colon != std::string::npos) {
			// Device serial contains IP:port
			if (pos_selector_colon == std::string::npos) {
				// Requested serial has no ':', match only IP part
				if (selector.serial.length() != pos_device_colon) {
					return false;
				}
				return device.serial.compare(0, pos_device_colon, selector.serial) == 0;
			}
		}
		// Direct string comparison
		return device.serial == selector.serial;
	}
	case SC_ADB_DEVICE_SELECT_USB:
		return sc_adb_device_get_type(device.serial) == SC_ADB_DEVICE_TYPE_USB;

	case SC_ADB_DEVICE_SELECT_TCPIP:
		// Both emulators and TCP/IP devices are selected via -e
		return sc_adb_device_get_type(device.serial) != SC_ADB_DEVICE_TYPE_USB;

	default:
		assert(!"Missing SC_ADB_DEVICE_SELECT_* handling");
		break;
	}

	return false;
}

sc_adb_device_type sc_adb_device_get_type(const std::string &serial)
{
	// Starts with "emulator-"
	constexpr std::string_view EMULATOR_PREFIX = "emulator-";
	if (serial.size() >= EMULATOR_PREFIX.size() && serial.substr(0, EMULATOR_PREFIX.size()) == EMULATOR_PREFIX) {
		return SC_ADB_DEVICE_TYPE_USB;
	}

	// If the serial contains a ':', then it is a TCP/IP device
	if (serial.find(':') != std::string_view::npos) {
		return SC_ADB_DEVICE_TYPE_TCPIP;
	}

	return SC_ADB_DEVICE_TYPE_USB;
}

static bool sc_adb_device_check_state(sc_adb_device &device, sc_vec_adb_devices &devices)
{
	const std::string &state = device.state;

	if (state == "device") {
		return true;
	}

	if (state == "unauthorized") {
		//LOGE("Device is unauthorized:");
		for (auto &d : devices) {
			//LOGE("  %s [%s]", d.serial.c_str(), d.state.c_str());
		}
		//LOGE("A popup should open on the device to request authorization.");
	} else {
		//LOGE("Device could not be connected (state=%s)", state.c_str());
	}

	return false;
}

bool sc_adb_push(sc_intr &intr, const std::string &serial, const std::string &local_path,
		 const std::string &remote_path, unsigned flags)
{
	std::string local = local_path;
	std::string remote = remote_path;

#ifdef _WIN32
	// Windows 需要对路径加引号
	local = sc_str_quote(local.c_str());
	if (local.empty()) {
		return false;
	}

	remote = sc_str_quote(remote.c_str());
	if (remote.empty()) {
		return false;
	}
#endif

	assert(!serial.empty());

	// 构建 argv
	std::vector<std::string> argv = SC_ADB_COMMAND("-s", serial, "push", local, remote);

	// 执行 adb 命令
	sc_pid pid = sc_adb_execute(argv, flags);

	// 检查执行结果
	return process_check_success_intr(intr, pid, "adb push", flags);
}

bool sc_adb_reverse(sc_intr &intr, const std::string &serial, const std::string &device_socket_name,
		    uint16_t local_port, unsigned flags)
{
	assert(!serial.empty() && !device_socket_name.empty());

	// 构造 local 和 remote 字符串
	std::string local = "tcp:" + std::to_string(local_port);
	std::string remote = "localabstract:" + device_socket_name;

	// 构造 adb 命令
	std::vector<std::string> argv = SC_ADB_COMMAND("-s", serial, "reverse", remote.c_str(), local.c_str());

	sc_pid pid = sc_adb_execute(argv, flags);
	return process_check_success_intr(intr, pid, "adb reverse", flags);
}

bool sc_adb_reverse_remove(sc_intr &intr, const std::string &serial, const std::string &device_socket_name,
			   unsigned flags)
{
	std::string remote = std::string("localabstract:") + device_socket_name;
	; // localabstract:NAME

	assert(!serial.empty());
	std::vector<std::string> argv = SC_ADB_COMMAND("-s", serial, "reverse", "--remove", remote);

	sc_pid pid = sc_adb_execute(argv, flags);
	return process_check_success_intr(intr, pid, "adb reverse --remove", flags);
}

uint16_t sc_adb_get_device_sdk_version(sc_intr &intr, const std::string &serial);

std::string sc_adb_getprop(sc_intr &intr, const std::string &serial, const char *prop, unsigned flags)
{
	assert(!serial.empty());
	std::vector<std::string> argv = SC_ADB_COMMAND("-s", serial, "shell", "getprop", prop);

	sc_pipe pout;
	sc_pid pid = sc_adb_execute_p(argv, flags, &pout);
	if (pid == SC_PROCESS_NONE) {
		//LOGE("Could not execute \"adb getprop\"");
		return {};
	}

	std::vector<char> buf(128);
	ssize_t r = sc_pipe_read_all_intr(intr, pid, pout, buf.data(), buf.size() - 1);
	sc_pipe_close(pout);

	if (!process_check_success_intr(intr, pid, "adb getprop", flags) || r == -1) {
		return {};
	}

	assert(static_cast<size_t>(r) < buf.size());
	buf[r] = '\0';

	// 去掉尾部空白或换行
	size_t len = strcspn(buf.data(), " \r\n");
	buf[len] = '\0';

	// 直接返回 std::string
	return std::string(buf.data(), len);
}

uint16_t sc_adb_get_device_sdk_version(sc_intr &intr, const std::string &serial)
{
	std::string sdk_version = sc_adb_getprop(intr, serial, "ro.build.version.sdk", SC_ADB_SILENT);
	if (sdk_version.empty()) {
		return 0;
	}

	auto val = sc_str_parse_integer("1234");
	if (!val) {
		return 0;
	}

	long value = *val;
	return (uint16_t)value;
}

bool sc_adb_forward_remove(sc_intr &intr, const std::string serial, uint16_t local_port, unsigned flags)
{
	// tcp:PORT
	std::stringstream ss;
	ss << "tcp:" << local_port;
	std::string local = ss.str();
	assert(!serial.empty());
	const std::vector<std::string> argv = SC_ADB_COMMAND("-s", serial, "forward", "--remove", local);

	sc_pid pid = sc_adb_execute(argv, flags);

	return process_check_success_intr(intr, pid, "adb forward --remove", flags);
}

bool sc_adb_kill_server(sc_intr &intr, unsigned flags)
{
	std::vector<std::string> argv = SC_ADB_COMMAND("kill-server");

	sc_pid pid = sc_adb_execute(argv, flags);
	return process_check_success_intr(intr, pid, "adb kill-server", flags);
}
