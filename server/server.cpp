#include "server.hpp"
#include "adb/adb.h"
#include <cassert>
#include <memory>
#include "adb/adb_device.h"
#include "util/sc_file.h"
#include <sstream>
#include <sys/types.h>
#include "util/sc_log.h"
#include "sc_server_cmd_builder.hpp"
//#include <QMessageBox>

#define SC_SERVER_FILENAME "scrcpy-server"

#define SC_SERVER_PATH_DEFAULT "/server/" SC_SERVER_FILENAME
#define SC_DEVICE_SERVER_PATH "/data/local/tmp/scrcpy-server"

#define SC_ADB_PORT_DEFAULT 5555
#define SC_SOCKET_NAME_PREFIX "scrcpy_"

#define SCRCPY_VERSION "3.3.4"

static void sc_server_on_terminated(void *userdata)
{
	sc_server *server = (sc_server *)userdata;

	// If the server process dies before connecting to the server socket,
	// then the client will be stuck forever on accept(). To avoid the problem,
	// wake up the accept() call (or any other) when the server dies, like on
	// stop() (it is safe to call interrupt() twice).
	server->m_intr.intr_interrupt();

	server->m_cbs->on_disconnected(*server, server->m_cbs_userdata);

	//LOGD("Server terminated");
}

static bool connect_and_read_byte(sc_intr &intr, sc_socket socket, uint32_t tunnel_host, uint16_t tunnel_port)
{
	bool ok = intr.net_connect_intr(socket, tunnel_host, tunnel_port);
	if (!ok) {
		return false;
	}

	char byte;
	// the connection may succeed even if the server behind the "adb tunnel"
	// is not listening, so read one byte to detect a working connection
	if (intr.net_recv_intr(socket, &byte, 1) != 1) {
		// the server is not listening yet behind the adb tunnel
		return false;
	}

	return true;
}

static bool device_read_info(sc_intr &intr, sc_socket device_socket, struct sc_server_info &info)
{
	uint8_t buf[SC_DEVICE_NAME_FIELD_LENGTH];
	ssize_t r = intr.net_recv_intr(device_socket, buf, sizeof(buf));
	if (r < SC_DEVICE_NAME_FIELD_LENGTH) {
		//LOGE("Could not retrieve device information");
		return false;
	}
	// in case the client sends garbage
	buf[SC_DEVICE_NAME_FIELD_LENGTH - 1] = '\0';
	memcpy(info.device_name, (char *)buf, sizeof(info.device_name));

	return true;
}

sc_server::sc_server(sc_server &&other) noexcept {}

sc_server &sc_server::operator=(sc_server &&other) noexcept
{
	if (this != &other) {
		// TODO: 实现移动赋值运算符
	}
	return *this;
}

sc_server::sc_server()
	: m_serial(""),
	  m_device_socket_name(""),
	  m_stopped(false),
	  m_video_socket(SC_SOCKET_NONE),
	  m_audio_socket(SC_SOCKET_NONE),
	  m_control_socket(SC_SOCKET_NONE)
{
}

sc_server::~sc_server()
{
	
}


bool sc_server::server_init(const sc_server_callbacks *cbs, void *cbs_userdata)
{

	bool ok = sc_adb_init();
	if (!ok) {
		return false;
	}

	sc_cond_init(this->m_cond_stopped);

	assert(cbs);
	assert(cbs->on_connection_failed);
	assert(cbs->on_connected);
	assert(cbs->on_disconnected);

	m_cbs = cbs;
	m_cbs_userdata = cbs_userdata;
	// 1. 启动 adb server
	if (!sc_adb_start_server(this->m_intr, 0)) {
		this->sc_server_kill_adb_if_requested();
		this->m_cbs->on_connection_failed(*this, this->m_cbs_userdata);
		return false;
	}

	// 2. 选择设备
	sc_adb_device_selector selector{};
	if (const char *env_serial = getenv("ANDROID_SERIAL")) {
		selector.type = SC_ADB_DEVICE_SELECT_SERIAL;
		selector.serial = env_serial;
	} else {
		selector.type = SC_ADB_DEVICE_SELECT_ALL;
	}
	return true;
}

void sc_server::update_params(const sc_server_params *params) {
	this->m_params = *params;

}

bool sc_server::server_start()
{
	this->m_stopped = false;
	this->m_intr.reset();
	this->m_connect_signal.promise = std::promise<bool>();

	bool ok = sc_thread_create(this->m_thread, run_server, "scrcpy-server", this);
	if (!ok) {
		//LOGE("Could not create server thread");
		return false;
	}

	return true;
}

void sc_server::server_stop()
{
	{
		std::unique_lock<sc_mutex> lock(this->m_mutex);
		this->m_stopped = true;
		sc_cond_signal(this->m_cond_stopped);
		this->m_intr.intr_interrupt();
	}
	
	sc_thread_join(this->m_thread, NULL);

	if (this->m_video_socket != SC_SOCKET_NONE) {
		net_close(this->m_video_socket);
		this->m_video_socket = SC_SOCKET_NONE;
	}
	if (this->m_audio_socket != SC_SOCKET_NONE) {
		net_close(this->m_audio_socket);
		this->m_audio_socket = SC_SOCKET_NONE;
	}
	if (this->m_control_socket != SC_SOCKET_NONE) {
		net_close(this->m_control_socket);
		this->m_control_socket = SC_SOCKET_NONE;
	}
}

bool sc_server::push_server(sc_intr &intr, const std::string &serial)
{
	if (serial.empty()) {
		return false;
	}
	if (std::find(pushed_serials.begin(), pushed_serials.end(), serial) != pushed_serials.end()) {
		return true;
	}
	std::string server_path = get_server_path();
	if (server_path.empty()) {
		return false;
	}
	if (!sc_file_is_regular(server_path)) {
		//LOGE("'%s' does not exist or is not a regular file\n", server_path);
		return false;
	}
	bool ok = sc_adb_push(intr, serial, server_path, SC_DEVICE_SERVER_PATH, 0);
	if (ok) {
		pushed_serials.emplace_back(serial);
	}
	return ok;
}

std::string sc_server::get_server_path()
{
	const char *env_path = std::getenv("SCRCPY_SERVER_PATH");

	if (env_path) {
		// if the envvar is set, use it
		//LOGD("Using SCRCPY_SERVER_PATH: %s", env_path);
		return std::string(env_path);
	}

#ifndef PORTABLE
	//LOGD("Using server: " SC_SERVER_PATH_DEFAULT);
	std::string path = SC_SERVER_PATH_DEFAULT;
	path = get_app_relative_path(path).string();
	return path;
#else
	auto local_path = sc_file_get_local_path(SC_SERVER_FILENAME);
	if (!local_path) {
		LOGE("Could not get local file path, "
		     "using " SC_SERVER_FILENAME " from current directory");
		return std::string(SC_SERVER_FILENAME);
	}
	LOGD("Using server (portable): %s", local_path);
	return std::string(local_path);
#endif
}

sc_pid sc_server::execute_server(const sc_server_params &params, sc_pipe *pout = nullptr)
{
	sc_pid pid = SC_PROCESS_NONE;

	std::string serial = this->m_serial;

	std::vector<std::string> cmd;
	cmd.reserve(128);
	cmd.emplace_back(sc_adb_get_executable());
	if (!serial.empty()) {
		cmd.emplace_back("-s");
		cmd.emplace_back(serial);
	}
	cmd.emplace_back("shell");
	cmd.emplace_back("CLASSPATH=" SC_DEVICE_SERVER_PATH);
	cmd.emplace_back("app_process");

#ifdef SERVER_DEBUGGER
	uint16_t sdk_version = sc_adb_get_device_sdk_version(&server->intr, serial);
	if (!sdk_version) {
		LOGE("Could not determine SDK version");
		return 0;
	}

#define SERVER_DEBUGGER_PORT "5005"
	const char *dbg;
	if (sdk_version < 28) {
		// Android < 9
		dbg = "-agentlib:jdwp=transport=dt_socket,suspend=y,server=y,address=" SERVER_DEBUGGER_PORT;
	} else if (sdk_version < 30) {
		// Android >= 9 && Android < 11
		dbg = "-XjdwpProvider:internal -XjdwpOptions:transport=dt_socket,"
		      "suspend=y,server=y,address=" SERVER_DEBUGGER_PORT;
	} else {
		// Android >= 11
		// Contrary to the other methods, this does not suspend on start.
		dbg = "-XjdwpProvider:adbconnection";
	}
	cmd.emplace_back(dbg);
#endif

	cmd.emplace_back("/"); // unused
	cmd.emplace_back("com.genymobile.scrcpy.Server");
	cmd.emplace_back(SCRCPY_VERSION);
	if (this->m_tunnel.m_forward) {
		cmd.emplace_back("tunnel_forward=true");
	} else {
		cmd.emplace_back("tunnel_forward=false");
	}

	ScServerCmdBuilder builder(cmd);
	if (!builder.build_from_params(params, m_tunnel.m_forward)) {
		return SC_PROCESS_NONE;
	}

#ifdef SERVER_DEBUGGER
	LOGI("Server debugger listening%s...", sdk_version < 30 ? " on port " SERVER_DEBUGGER_PORT : "");
	// For Android < 11, from the computer:
	//     - run `adb forward tcp:5005 tcp:5005`
	// For Android >= 11:
	//     - execute `adb jdwp` to get the jdwp port
	//     - run `adb forward tcp:5005 jdwp:XXXX` (replace XXXX)
	//
	// Then, from Android Studio: Run > Debug > Edit configurations...
	// On the left, click on '+', "Remote", with:
	//     Host: localhost
	//     Port: 5005
	// Then click on "Debug"
#endif
	// Inherit both stdout and stderr (all server logs are printed to stdout)
	if (pout) {
		pid = sc_adb_execute_p(cmd, 0, pout);
	} else {
		pid = sc_adb_execute(cmd, 0);
	}

	return pid;
}

bool sc_server::sc_server_connect_to(sc_server_info &info)
{
	assert(this->m_tunnel.m_enabled);

	const char *serial = this->m_serial.c_str();
	assert(serial);

	bool video = this->m_params.video;
	bool audio = this->m_params.audio;
	bool control = this->m_params.control;

	sc_socket video_socket = SC_SOCKET_NONE;
	sc_socket audio_socket = SC_SOCKET_NONE;
	sc_socket control_socket = SC_SOCKET_NONE;

	auto cleanup_sockets = [](sc_socket v, sc_socket a, sc_socket c) {
		if (v != SC_SOCKET_NONE) net_close(v);
		if (a != SC_SOCKET_NONE) net_close(a);
		if (c != SC_SOCKET_NONE) net_close(c);
	};

	auto close_tunnel = [this, serial]() {
		if (this->m_tunnel.m_enabled) {
			this->m_tunnel.sc_adb_tunnel_close(this->m_intr, serial, this->m_device_socket_name);
		}
	};

	if (!this->m_tunnel.m_forward) {
		// Accept mode: wait for connections from device
		if (video) {
			video_socket = this->m_intr.net_accept_intr(this->m_tunnel.m_server_socket);
			if (video_socket == SC_SOCKET_NONE) {
				close_tunnel();
				return false;
			}
		}

		if (audio) {
			audio_socket = this->m_intr.net_accept_intr(this->m_tunnel.m_server_socket);
			if (audio_socket == SC_SOCKET_NONE) {
				cleanup_sockets(video_socket, SC_SOCKET_NONE, SC_SOCKET_NONE);
				close_tunnel();
				return false;
			}
		}

		if (control) {
			control_socket = this->m_intr.net_accept_intr(this->m_tunnel.m_server_socket);
			if (control_socket == SC_SOCKET_NONE) {
				cleanup_sockets(video_socket, audio_socket, SC_SOCKET_NONE);
				close_tunnel();
				return false;
			}
		}
	} else {
		// Forward mode: connect to device
		uint32_t tunnel_host = this->m_params.tunnel_host ? this->m_params.tunnel_host : IPV4_LOCALHOST;
		uint16_t tunnel_port = this->m_params.tunnel_port ? this->m_params.tunnel_port : this->m_tunnel.m_local_port;

		unsigned attempts = 100;
		sc_tick delay = SC_TICK_FROM_MS(100);
		sc_socket first_socket = this->connect_to_server(attempts, delay, tunnel_host, tunnel_port);
		if (first_socket == SC_SOCKET_NONE) {
			close_tunnel();
			return false;
		}

		if (video) {
			video_socket = first_socket;
		}

		if (audio) {
			if (video) {
				audio_socket = net_socket();
				if (audio_socket == SC_SOCKET_NONE ||
					!this->m_intr.net_connect_intr(audio_socket, tunnel_host, tunnel_port)) {
					cleanup_sockets(video_socket, audio_socket, SC_SOCKET_NONE);
					close_tunnel();
					return false;
				}
			} else {
				audio_socket = first_socket;
			}
		}

		if (control) {
			if (video || audio) {
				control_socket = net_socket();
				if (control_socket == SC_SOCKET_NONE ||
					!this->m_intr.net_connect_intr(control_socket, tunnel_host, tunnel_port)) {
					cleanup_sockets(video_socket, audio_socket, control_socket);
					close_tunnel();
					return false;
				}
			} else {
				control_socket = first_socket;
			}
		}
	}

	// Disable Nagle's algorithm for the control socket
	if (control_socket != SC_SOCKET_NONE) {
		net_set_tcp_nodelay(control_socket, true);
	}

	// Close tunnel - we don't need it anymore
	close_tunnel();

	// Determine which socket to read info from
	sc_socket first_socket = video ? video_socket : audio ? audio_socket : control_socket;

	// Read device information from the first connected socket
	if (!device_read_info(this->m_intr, first_socket, info)) {
		cleanup_sockets(video_socket, audio_socket, control_socket);
		return false;
	}

	assert(!video || video_socket != SC_SOCKET_NONE);
	assert(!audio || audio_socket != SC_SOCKET_NONE);
	assert(!control || control_socket != SC_SOCKET_NONE);

	this->m_video_socket = video_socket;
	this->m_audio_socket = audio_socket;
	this->m_control_socket = control_socket;

	return true;
}

sc_socket sc_server::connect_to_server(unsigned attempts, sc_tick delay, uint32_t host, uint16_t port)
{
	do {
		//LOGD("Remaining connection attempts: %u", attempts);
		sc_socket socket = net_socket();
		if (socket != SC_SOCKET_NONE) {
			bool ok = connect_and_read_byte(this->m_intr, socket, host, port);
			if (ok) {
				// it worked!
				return socket;
			}

			net_close(socket);
		}

		if (this->m_intr.is_interrupted()) {
			// Stop immediately
			break;
		}

		if (attempts) {
			sc_tick deadline = sc_tick_now() + delay;
			bool ok = this->sc_server_sleep(deadline);
			if (!ok) {
				//LOGI("Connection attempt stopped");
				break;
			}
		}
	} while (--attempts);
	return SC_SOCKET_NONE;
}

bool sc_server::sc_server_sleep(sc_tick deadline)
{
	std::unique_lock<sc_mutex> lock(this->m_mutex);
	bool timed_out = false;
	while (!this->m_stopped && !timed_out) {
		timed_out = !sc_cond_timedwait(this->m_cond_stopped, this->m_mutex, deadline);
	}
	bool stopped = this->m_stopped;
	// 被 stop 唤醒
	return !stopped;
}

void sc_server::sc_server_kill_adb_if_requested()
{
	if (this->m_params.kill_adb_on_close) {
		//LOGI("Killing adb server...");
		unsigned flags = SC_ADB_NO_STDOUT | SC_ADB_NO_STDERR | SC_ADB_NO_LOGERR;
		sc_adb_kill_server(this->m_intr, flags);
	}
}

int sc_server::run_server(void *data)
{  
	auto *server = static_cast<sc_server *>(data);
	const auto &params = server->m_params;
	
	// 2. 选择设备
	sc_adb_device_selector selector{};
	if (const char *env_serial = getenv("ANDROID_SERIAL")) {
		selector.type = SC_ADB_DEVICE_SELECT_SERIAL;
		selector.serial = env_serial;
	} else {
		selector.type = SC_ADB_DEVICE_SELECT_ALL;
	}

	// 3. 确定 serial
	if (!params.tcpip) {
		server->m_serial = params.req_serial;
	}
	const std::string serial = server->m_serial;
	if (serial.empty()) {
		server->m_cbs->on_connection_failed(*server, server->m_cbs_userdata);
		return -1;
	}

	// 4. push server
	if (!server->push_server(server->m_intr, serial)) {
		server->m_cbs->on_connection_failed(*server, server->m_cbs_userdata);
		return -1;
	}
	auto scid_hex = [](uint32_t scid) {
		std::ostringstream oss;
		oss << std::hex << scid; // 小写 hex，和官方一致
		return oss.str();
	};

	// 5. socket name
	server->m_device_socket_name = SC_SOCKET_NAME_PREFIX + scid_hex(params.scid);

	// 6. 打开 adb tunnel（RAII：失败立即关闭）
	if (!server->m_tunnel.adb_tunnel_open(server->m_intr, serial, server->m_device_socket_name, params.port_range,
					      params.force_adb_forward)) {
		server->m_cbs->on_connection_failed(*server, server->m_cbs_userdata);
		return -1;
	}

	// 7. 启动 server 进程
	sc_pid pid = server->execute_server(params);
	if (pid == SC_PROCESS_NONE) {
		server->m_tunnel.sc_adb_tunnel_close(server->m_intr, serial, server->m_device_socket_name);
		server->m_cbs->on_connection_failed(*server, server->m_cbs_userdata);
		return -1;
	}

	// 8. 注册进程终止回调
	server->m_listener.on_terminated = [server]() {
		server->m_intr.intr_interrupt();
		if (server->m_cbs) {
			server->m_cbs->on_disconnected(*server, server->m_cbs_userdata);
		}
	};

	try {
		bool connect_ok = false;
		{
			// 9. observer（RAII）
			process_observer_raii observer(pid, server->m_listener, server);

			// 10. 连接 server socket
			if (server->sc_server_connect_to(server->m_info)) {
				connect_ok = true;

				// 11. 已连接
				server->m_cbs->on_connected(*server, server->m_cbs_userdata);

				// 12. 等待 stop
				{
					std::lock_guard<sc_mutex> lock(server->m_mutex);
					while (!server->m_stopped) {
						sc_cond_wait(server->m_cond_stopped, server->m_mutex);
					}
				}

				// 13. 中断 socket
				if (server->m_video_socket != SC_SOCKET_NONE)
					net_interrupt(server->m_video_socket);
				if (server->m_audio_socket != SC_SOCKET_NONE)
					net_interrupt(server->m_audio_socket);
				if (server->m_control_socket != SC_SOCKET_NONE)
					net_interrupt(server->m_control_socket);

				// 14. 等待 server 退出
				sc_tick deadline = sc_tick_now() + SC_TICK_FROM_SEC(1);
				if (!observer.timedwait(deadline)) {
					sc_process_terminate(pid);
				}
			}
		}

		if (!connect_ok) {
			server->m_cbs->on_connection_failed(*server, server->m_cbs_userdata);
			sc_process_close(pid);
			pid = SC_PROCESS_NONE;
			return -1;
		}

		sc_process_close(pid);
		pid = SC_PROCESS_NONE;
		server->sc_server_kill_adb_if_requested();
		return 0;
	} catch (const std::exception &e) {
		e;
		sc_process_terminate(pid);
		sc_process_wait(pid, true);
		pid = SC_PROCESS_NONE;
		server->m_cbs->on_connection_failed(*server, server->m_cbs_userdata);
		return -1;
	}
}
