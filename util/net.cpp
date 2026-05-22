#include "net.h"
#include <assert.h>
#include "util/sc_log.h"

#ifdef _WIN32
static int ws_initialized = 0;
#endif

// 网络初始化
int net_init(void)
{
#ifdef _WIN32
	if (!ws_initialized) {
		WSADATA wsa_data;
		int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
		if (result != 0) {
			return -1;
		}
		ws_initialized = 1;
	}
#endif
	return 0;
}

// 网络清理
void net_cleanup(void)
{
#ifdef _WIN32
	if (ws_initialized) {
		WSACleanup();
		ws_initialized = 0;
	}
#endif
}

static inline bool sc_raw_socket_close(sc_raw_socket raw_sock)
{
#ifndef _WIN32
	return !close(raw_sock);
#else
	return !closesocket(raw_sock);
#endif
}

#ifndef HAVE_SOCK_CLOEXEC
// If SOCK_CLOEXEC does not exist, the flag must be set manually once the
// socket is created
static bool set_cloexec_flag(sc_raw_socket raw_sock)
{
#ifndef _WIN32
	if (fcntl(raw_sock, F_SETFD, FD_CLOEXEC) == -1) {
		perror("fcntl F_SETFD");
		return false;
	}
#else
	if (!SetHandleInformation((HANDLE)raw_sock, HANDLE_FLAG_INHERIT, 0)) {
		//LOGE("SetHandleInformation socket failed");
		return false;
	}
#endif
	return true;
}
#endif

static void net_perror(const char *s)
{
#ifdef _WIN32
	sc_log_windows_error(s, WSAGetLastError());
#else
	perror(s);
#endif
}

// 创建 TCP 服务器 socket
socket_t net_create_server_socket(int port)
{
	socket_t server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server_sock == INVALID_SOCKET) {
		return INVALID_SOCKET;
	}

	// 设置地址重用
	int opt = 1;
	setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

	// 绑定地址
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons((unsigned short)port);

	if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
		CLOSE_SOCKET(server_sock);
		return INVALID_SOCKET;
	}

	// 监听
	if (listen(server_sock, 5) == SOCKET_ERROR) {
		CLOSE_SOCKET(server_sock);
		return INVALID_SOCKET;
	}

	return server_sock;
}

// 接受客户端连接
socket_t net_accept_client(socket_t server_socket)
{
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);

	socket_t client_sock = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
	return client_sock;
}

// 发送数据
int net_send(socket_t sock, const char *data, int len)
{
	if (sock == INVALID_SOCKET || data == NULL || len <= 0) {
		return -1;
	}
	return send(sock, data, len, 0);
}

// 接收数据
int net_recv(socket_t sock, char *buffer, int len)
{
	if (sock == INVALID_SOCKET || buffer == NULL || len <= 0) {
		return -1;
	}
	return recv(sock, buffer, len, 0);
}

static inline sc_raw_socket unwrap(sc_socket socket)
{
#ifdef SC_SOCKET_CLOSE_ON_INTERRUPT
	if (socket == SC_SOCKET_NONE) {
		return SC_RAW_SOCKET_NONE;
	}

	return socket->socket;
#else
	return socket;
#endif
}

// 关闭 socket
bool net_close(sc_socket socket)
{
	sc_raw_socket raw_sock = unwrap(socket);

#ifdef SC_SOCKET_CLOSE_ON_INTERRUPT
	bool ret = true;
	if (!atomic_flag_test_and_set(&socket->closed)) {
		ret = sc_raw_socket_close(raw_sock);
	}
	free(socket);
	return ret;
#else
	return sc_raw_socket_close(raw_sock);
#endif
}

static inline sc_socket wrap(sc_raw_socket sock)
{
#ifdef SC_SOCKET_CLOSE_ON_INTERRUPT
	if (sock == SC_RAW_SOCKET_NONE) {
		return SC_SOCKET_NONE;
	}

	struct sc_socket_wrapper *socket = (struct sc_socket_wrapper *)malloc(sizeof(*socket));
	if (!socket) {
		//LOG_OOM();
		sc_raw_socket_close(sock);
		return SC_SOCKET_NONE;
	}

	socket->socket = sock;
	socket->closed.clear(std::memory_order_relaxed);

	return socket;
#else
	return sock;
#endif
}

bool net_interrupt(sc_socket socket)
{
	assert(socket != SC_SOCKET_NONE);

	sc_raw_socket raw_sock = unwrap(socket);

#ifdef SC_SOCKET_CLOSE_ON_INTERRUPT
	if (!socket->closed.test_and_set(std::memory_order_acquire)) {
		return sc_raw_socket_close(raw_sock);
	}
	return true;
#else
	return !shutdown(raw_sock, SHUT_RDWR);
#endif
}

sc_socket net_socket(void)
{
#ifdef HAVE_SOCK_CLOEXEC
	sc_raw_socket raw_sock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
	sc_raw_socket raw_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (raw_sock != SC_RAW_SOCKET_NONE && !set_cloexec_flag(raw_sock)) {
		sc_raw_socket_close(raw_sock);
		return SC_SOCKET_NONE;
	}
#endif

	sc_socket sock = wrap(raw_sock);
	if (sock == SC_SOCKET_NONE) {
		net_perror("socket");
	}
	return sock;
}

bool net_listen(sc_socket server_socket, uint32_t addr, uint16_t port, int backlog)
{
	sc_raw_socket raw_sock = unwrap(server_socket);

	int reuse = 1;
	if (setsockopt(raw_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse)) ==
	    -1) {
		net_perror("setsockopt(SO_REUSEADDR)");
	}

	SOCKADDR_IN sin;
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(addr); // htonl() harmless on INADDR_ANY
	sin.sin_port = htons(port);

	if (bind(raw_sock, (SOCKADDR *)&sin, sizeof(sin)) == SOCKET_ERROR) {
		net_perror("bind");
		return false;
	}

	if (listen(raw_sock, backlog) == SOCKET_ERROR) {
		net_perror("listen");
		return false;
	}

	return true;
}

bool net_connect(sc_socket socket, uint32_t addr, uint16_t port)
{
	sc_raw_socket raw_sock = unwrap(socket);

	SOCKADDR_IN sin;
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(addr);
	sin.sin_port = htons(port);

	if (connect(raw_sock, (SOCKADDR *)&sin, sizeof(sin)) == SOCKET_ERROR) {
		net_perror("connect");
		return false;
	}

	return true;
}

sc_socket net_accept(sc_socket server_socket)
{
	sc_raw_socket raw_server_socket = unwrap(server_socket);

	SOCKADDR_IN csin;
	socklen_t sinsize = sizeof(csin);

#ifdef HAVE_SOCK_CLOEXEC
	sc_raw_socket raw_sock = accept4(raw_server_socket, (SOCKADDR *)&csin, &sinsize, SOCK_CLOEXEC);
#else
	sc_raw_socket raw_sock = accept(raw_server_socket, (SOCKADDR *)&csin, &sinsize);
	if (raw_sock != SC_RAW_SOCKET_NONE && !set_cloexec_flag(raw_sock)) {
		sc_raw_socket_close(raw_sock);
		return SC_SOCKET_NONE;
	}
#endif
	if (raw_sock == SC_RAW_SOCKET_NONE) {
		int err = WSAGetLastError();
		net_perror("accept");
		return SC_SOCKET_NONE;
	}
	return wrap(raw_sock);
}

ssize_t net_recv(sc_socket socket, void *buf, size_t len)
{
	if (socket == SC_SOCKET_NONE) {
		return -1;
	}
	sc_raw_socket raw_sock = unwrap(socket);
	if (raw_sock == SC_RAW_SOCKET_NONE) {
		return -1;
	}
	return recv(raw_sock, (char *)buf, (int)len, 0);
}

ssize_t net_recv_all(sc_socket socket, void *buf, size_t len)
{
	if (socket == SC_SOCKET_NONE) {
		return -1;
	}
	sc_raw_socket raw_sock = unwrap(socket);
	if (raw_sock == SC_RAW_SOCKET_NONE) {
		return -1;
	}
	return recv(raw_sock, (char *)buf, (int)len, MSG_WAITALL);
}

bool net_set_tcp_nodelay(sc_socket socket, bool tcp_nodelay)
{
	sc_raw_socket raw_sock = unwrap(socket);

	int value = tcp_nodelay ? 1 : 0;
	int ret = setsockopt(raw_sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&value, sizeof(value));
	if (ret == -1) {
		net_perror("setsockopt(TCP_NODELAY)");
		return false;
	}

	assert(ret == 0);
	return true;
}

ssize_t net_send_all(sc_socket socket, const void *buf, size_t len)
{
	if (socket == SC_SOCKET_NONE) {
		return -1;
	}
	sc_raw_socket raw_sock = unwrap(socket);
	if (raw_sock == SC_RAW_SOCKET_NONE) {
		return -1;
	}
	size_t total = 0;
	const char *p = (const char *)buf;
	while (total < len) {
		int r = send(raw_sock, p + total, (int)(len - total), 0);
		if (r < 0) {
#ifdef _WIN32
			int err = WSAGetLastError();
			net_perror("net_send_all send");
			if (err == WSAEINTR) {
				continue;
			}
#else
			net_perror("net_send_all send");
			if (errno == EINTR) {
				continue;
			}
#endif
			return -1;
		}
		if (r == 0) {
			return total;
		}
		total += r;
	}
	return total;
}
