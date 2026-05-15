#include "common.h"
#include <stdarg.h>
#include <time.h>

// 获取日志级别字符串
static const char *get_level_string(LogLevel level)
{
	switch (level) {
	case LOG_LEVEL_VERBOSE:
		return "VERBOSE";
	case LOG_LEVEL_DEBUG:
		return "DEBUG";
	case LOG_LEVEL_INFO:
		return "INFO";
	case LOG_LEVEL_WARN:
		return "WARN";
	case LOG_LEVEL_ERROR:
		return "ERROR";
	default:
		return "UNKNOWN";
	}
}

// 日志函数实现
void log_message(LogLevel level, const char *tag, const char *format, ...)
{
	if (tag == NULL || format == NULL) {
		return;
	}

	// 获取当前时间
	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	char time_buf[64];
	strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

	// 打印日志头
	printf("%s [%s] %s: ", time_buf, get_level_string(level), tag);

	// 打印格式化消息
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);

	// 换行
	printf("\n");

	// 错误级别刷新输出
	if (level >= LOG_LEVEL_ERROR) {
		fflush(stdout);
	}
}
