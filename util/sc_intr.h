#pragma once
#include <atomic>
#include <stdbool.h>
#include "sc_process.h"
#include "sc_thread.h"
#include "net.h"

class sc_intr {
public:
	sc_mutex mutex;

	sc_socket socket;
	sc_pid process;

	// Written protected by the mutex to avoid race conditions against
	// sc_intr_set_socket() and sc_intr_set_process(), but can be read
	// (atomically) without mutex
	std::atomic_bool interrupted;
	sc_intr();

public:
	bool set_process(sc_pid pid);

	bool set_socket(sc_socket socket);

	void intr_interrupt();

	bool is_interrupted();

	bool net_listen_intr(sc_socket server_socket, uint32_t addr, uint16_t port, int backlog);

	bool net_connect_intr(sc_socket socket, uint32_t addr, uint16_t port);

	sc_socket net_accept_intr(sc_socket server_socket);

	ssize_t net_recv_intr(sc_socket socket, void *buf, size_t len);
};
