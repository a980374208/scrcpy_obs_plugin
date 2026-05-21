#ifndef SCRCPY_NET_H
#define SCRCPY_NET_H

#include "compat.h"
#include <sys/types.h>
#include <atomic>
#include "util/sc_process.h"

#include <winsock2.h>
#include <ws2tcpip.h>

// 定义 sc_raw_socket 类型
#ifdef _WIN32
typedef SOCKET sc_raw_socket;
#define SC_RAW_SOCKET_NONE INVALID_SOCKET
#else
typedef int sc_raw_socket;
#define SC_RAW_SOCKET_NONE -1
#endif

#if defined(_WIN32) || defined(__APPLE__)
// On Windows and macOS, shutdown() does not interrupt accept() or read()
// calls, so net_interrupt() must call close() instead, and net_close() must
// behave accordingly.
// This causes a small race condition (once the socket is closed, its
// handle becomes invalid and may in theory be reassigned before another
// thread calls accept() or read()), but it is deemed acceptable as a
// workaround.
#define SC_SOCKET_CLOSE_ON_INTERRUPT
#endif

#ifdef SC_SOCKET_CLOSE_ON_INTERRUPT
#ifdef _MSC_VER
// MSVC 不支持 C11 <stdatomic.h>，使用 Windows Interlocked 函数
#include <windows.h>
#define SC_SOCKET_NONE NULL
typedef struct sc_socket_wrapper {
	sc_raw_socket socket;
	std::atomic_flag closed;
} *sc_socket;
#else
// GCC/Clang 支持 C11 原子操作
#include <stdatomic.h>
#define SC_SOCKET_NONE NULL
typedef struct sc_socket_wrapper {
	sc_raw_socket socket;
	atomic_flag closed;
} *sc_socket;
#endif
#else
#define SC_SOCKET_NONE -1
typedef sc_raw_socket sc_socket;
#endif

#define IPV4_LOCALHOST 0x7F000001

// 网络初始化（Windows 需要）
int net_init(void);

// 网络清理
void net_cleanup(void);

// 创建 TCP 服务器 socket
socket_t net_create_server_socket(int port);

// 接受客户端连接
socket_t net_accept_client(socket_t server_socket);

// 发送数据
int net_send(socket_t sock, const char *data, int len);

// 接收数据
int net_recv(socket_t sock, char *buffer, int len);

// 关闭 socket
bool net_close(sc_socket sock);

bool net_interrupt(sc_socket socket);

sc_socket net_socket(void);

bool net_listen(sc_socket server_socket, uint32_t addr, uint16_t port, int backlog);

bool net_connect(sc_socket socket, uint32_t addr, uint16_t port);

sc_socket net_accept(sc_socket server_socket);

ssize_t net_recv(sc_socket socket, void *buf, size_t len);

ssize_t net_recv_all(sc_socket socket, void *buf, size_t len);

ssize_t net_send_all(sc_socket socket, const void *buf, size_t len);

bool net_set_tcp_nodelay(sc_socket socket, bool tcp_nodelay);

#endif // SCRCPY_NET_H
