#include "str_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <charconv>
#ifdef _WIN32
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#endif

// 安全复制字符串
char *str_util_strdup(const char *str)
{
	if (str == NULL) {
		return NULL;
	}

	// 使用 __try/__except 捕获内存访问异常（Windows）
	size_t len = 0;
#ifdef _WIN32
	__try {
		len = strlen(str) + 1;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		fprintf(stderr, "Error: Cannot access string at %p\n", (void *)str);
		return NULL;
	}
#else
	len = strlen(str) + 1;
#endif

	if (len == 1) // 只有 '\0' 的情况
	{
		fprintf(stderr, "Warning: Empty string detected\n");
	}

	char *dup = (char *)malloc(len);
	if (dup == NULL) {
		fprintf(stderr, "Error: Memory allocation failed in str_util_strdup\n");
		return NULL;
	}

	memcpy(dup, str, len);
	return dup;
}

// 连接两个字符串
char *str_util_strcat(const char *str1, const char *str2)
{
	if (str1 == NULL && str2 == NULL) {
		return NULL;
	}
	if (str1 == NULL) {
		return str_util_strdup(str2);
	}
	if (str2 == NULL) {
		return str_util_strdup(str1);
	}

	size_t len1 = strlen(str1);
	size_t len2 = strlen(str2);
	char *result = (char *)malloc(len1 + len2 + 1);
	if (result != NULL) {
		memcpy(result, str1, len1);
		memcpy(result + len1, str2, len2 + 1);
	}
	return result;
}

// 格式化字符串（跨平台兼容）
int str_util_snprintf(char *str, size_t size, const char *format, ...)
{
	if (str == NULL || format == NULL) {
		return -1;
	}

	va_list args;
	va_start(args, format);
#ifdef _WIN32
	int ret = _vsnprintf_s(str, size, _TRUNCATE, format, args);
#else
	int ret = vsnprintf(str, size, format, args);
#endif
	va_end(args);
	return ret;
}

// 去除字符串前后空格
char *str_util_trim(char *str)
{
	if (str == NULL) {
		return NULL;
	}

	// 去除前导空格
	while (*str && isspace((unsigned char)*str)) {
		str++;
	}

	if (*str == '\0') {
		return str;
	}

	// 去除尾部空格
	char *end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end)) {
		end--;
	}
	*(end + 1) = '\0';

	return str;
}

wchar_t *sc_str_to_wchars(const char *utf8)
{
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
	if (!len) {
		return NULL;
	}

	wchar_t *wide = (wchar_t *)malloc(len * sizeof(wchar_t));
	if (!wide) {
		// LOG_OOM();
		return NULL;
	}

	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, len);
	return wide;
}

std::string sc_str_join(const std::vector<std::string> &tokens, char sep)
{
	if (tokens.empty())
		return "";

	size_t total_len = 0;
	for (const auto &t : tokens) {
		total_len += t.size();
	}
	total_len += (tokens.size() - 1);

	std::string result;
	result.reserve(total_len);

	for (size_t i = 0; i < tokens.size(); ++i) {
		if (i > 0) {
			result += sep;
		}
		result += tokens[i];
	}

	return result;
}

std::string sc_str_quote(const std::string &src)
{
	return "\"" + src + "\"";
}

std::optional<long> sc_str_parse_integer(std::string_view s)
{
	if (s.empty()) {
		return std::nullopt;
	}

	long value = 0;
	auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value, 0);

	if (ec != std::errc() || ptr != s.data() + s.size()) {
		return std::nullopt;
	}

	return value;
}
