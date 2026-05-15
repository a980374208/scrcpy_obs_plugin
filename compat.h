#ifndef SCRCPY_COMPAT_H
#define SCRCPY_COMPAT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
// 防止 windows.h 包含 winsock.h，确保只使用 winsock2.h
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define CLOSE_SOCKET closesocket
#define STRCASECMP _stricmp
#define STRNCASECMP _strnicmp
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define CLOSE_SOCKET close
#define STRCASECMP strcasecmp
#define STRNCASECMP strncasecmp
#endif

// 跨平台兼容的宏定义
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

// 数组大小宏
#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

#endif // SCRCPY_COMPAT_H
