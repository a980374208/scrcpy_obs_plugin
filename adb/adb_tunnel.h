#pragma once
#include "net.h"
#include "util/options.h"
#include "util/sc_intr.h"
class sc_adb_tunnel {
public:
	bool m_enabled;
	bool m_forward;            // use "adb forward" instead of "adb reverse" 暂不实现forward模式
	sc_socket m_server_socket; // only used if !forward
	uint16_t m_local_port;

	sc_adb_tunnel();

	bool adb_tunnel_open(sc_intr &intr, const std::string &serial, const std::string &device_socket_name,
			     const sc_port_range &port_range, bool force_adb_forward);
	bool enable_tunnel_reverse_any_port(sc_intr &intr, const std::string &serial,
					    const std::string &device_socket_name, const sc_port_range &port_range);

	static bool enable_tunnel_forward_any_port(sc_intr &intr, const std::string &serial,
						   const std::string &device_socket_name,
						   const sc_port_range &port_range);

	static bool listen_on_port(sc_intr &intr, const sc_socket &socket, uint16_t port);

	bool sc_adb_tunnel_close(sc_intr &intr, const std::string serial, const std::string device_socket_name);
};
