#ifndef SCRCPY_COMMON_H
#define SCRCPY_COMMON_H

#include "compat.h"

// 日志级别
typedef enum {
	LOG_LEVEL_VERBOSE = 0,
	LOG_LEVEL_DEBUG = 1,
	LOG_LEVEL_INFO = 2,
	LOG_LEVEL_WARN = 3,
	LOG_LEVEL_ERROR = 4
} LogLevel;

// 日志函数
void log_message(LogLevel level, const char *tag, const char *format, ...);

#define LOGV(tag, ...) log_message(LOG_LEVEL_VERBOSE, tag, __VA_ARGS__)
#define LOGD(tag, ...) log_message(LOG_LEVEL_DEBUG, tag, __VA_ARGS__)
#define LOGI(tag, ...) log_message(LOG_LEVEL_INFO, tag, __VA_ARGS__)
#define LOGW(tag, ...) log_message(LOG_LEVEL_WARN, tag, __VA_ARGS__)
#define LOGE(tag, ...) log_message(LOG_LEVEL_ERROR, tag, __VA_ARGS__)

// 设备信息结构
typedef struct {
	char *serial;
	char *device_name;
	int width;
	int height;
} DeviceInfo;

#endif // SCRCPY_COMMON_H
