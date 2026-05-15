#include "adb_tunnel.h"
#include "assert.h"
#include "adb/adb.h"

sc_adb_tunnel::sc_adb_tunnel() : m_enabled(false), m_forward(false), m_server_socket(SC_SOCKET_NONE), m_local_port(0) {}

bool sc_adb_tunnel::adb_tunnel_open(sc_intr &intr, const std::string &serial, const std::string &device_socket_name,
				    const sc_port_range &port_range, bool force_adb_forward)
{
	assert(!this->m_enabled);

	if (!force_adb_forward) {
		// Attempt to use "adb reverse"
		if (enable_tunnel_reverse_any_port(intr, serial, device_socket_name, port_range)) {
			return true;
		}

		// if "adb reverse" does not work (e.g. over "adb connect"), it
		// fallbacks to "adb forward", so the app socket is the client

		//LOGW("'adb reverse' failed, fallback to 'adb forward'");
	}
	return enable_tunnel_forward_any_port(intr, serial, device_socket_name, port_range);
}

bool sc_adb_tunnel::enable_tunnel_reverse_any_port(sc_intr &intr, const std::string &serial,
						   const std::string &device_socket_name,
						   const sc_port_range &port_range)
{
	uint16_t port = port_range.first;
	for (;;) {
		if (!sc_adb_reverse(intr, serial, device_socket_name, port, SC_ADB_NO_STDOUT)) {
			// the command itself failed, it will fail on any port
			return false;
		}

		// At the application level, the device part is "the server" because it
		// serves video stream and control. However, at the network level, the
		// client listens and the server connects to the client. That way, the
		// client can listen before starting the server app, so there is no
		// need to try to connect until the server socket is listening on the
		// device.
		sc_socket server_socket = net_socket();
		if (server_socket != SC_SOCKET_NONE) {
			bool ok = listen_on_port(intr, server_socket, port);
			if (ok) {
				// success
				this->m_server_socket = server_socket;
				this->m_local_port = port;
				this->m_enabled = true;
				return true;
			}

			net_close(server_socket);
		}

		if (intr.is_interrupted()) {
			// Stop immediately
			return false;
		}

		// failure, disable tunnel and try another port
		if (!sc_adb_reverse_remove(intr, serial, device_socket_name, SC_ADB_NO_STDOUT)) {
			//LOGW("Could not remove reverse tunnel on port %" PRIu16, port);
		}

		// check before incrementing to avoid overflow on port 65535
		if (port < port_range.last) {
			//LOGW("Could not listen on port %" PRIu16 ", retrying on %" PRIu16,
			//    port, (uint16_t)(port + 1));
			port++;
			continue;
		}

		if (port_range.first == port_range.last) {
			//LOGE("Could not listen on port %" PRIu16, port_range.first);
		} else {
			//LOGE("Could not listen on any port in range %" PRIu16 ":%" PRIu16,
			//    port_range.first, port_range.last);
		}
		return false;
	}
}

bool sc_adb_tunnel::enable_tunnel_forward_any_port(sc_intr &intr, const std::string &serial,
						   const std::string &device_socket_name,
						   const sc_port_range &port_range)
{
	return false;
}

bool sc_adb_tunnel::listen_on_port(sc_intr &intr, const sc_socket &socket, uint16_t port)
{
	return intr.net_listen_intr(socket, IPV4_LOCALHOST, port, 1);
}

bool sc_adb_tunnel::sc_adb_tunnel_close(sc_intr &intr, const std::string serial, const std::string device_socket_name)
{
	assert(this->m_enabled);

	bool ret;
	if (this->m_forward) {
		ret = sc_adb_forward_remove(intr, serial, this->m_local_port, SC_ADB_NO_STDOUT);
	} else {
		ret = sc_adb_reverse_remove(intr, serial, device_socket_name, SC_ADB_NO_STDOUT);

		assert(this->m_server_socket != SC_SOCKET_NONE);
		if (!net_close(this->m_server_socket)) {
			//LOGW("Could not close server socket");
		}

		// server_socket is never used anymore
	}

	// Consider tunnel disabled even if the command failed
	this->m_enabled = false;

	return ret;
}
