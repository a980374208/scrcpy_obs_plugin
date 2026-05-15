#ifndef SCRCPY_STR_UTIL_H
#define SCRCPY_STR_UTIL_H

#include <stddef.h>
#include <vector>
#include <string>
#include <optional>

// 安全复制字符串
char *str_util_strdup(const char *str);

// 连接两个字符串
char *str_util_strcat(const char *str1, const char *str2);

// 格式化字符串（跨平台兼容）
int str_util_snprintf(char *str, size_t size, const char *format, ...);

// 去除字符串前后空格
char *str_util_trim(char *str);

// UTF-8 转宽字符
wchar_t *sc_str_to_wchars(const char *utf8);

// 拼接字符串
std::string sc_str_join(const std::vector<std::string> &tokens, char sep);

// 给字符串加引号
std::string sc_str_quote(const std::string &src);

std::optional<long> sc_str_parse_integer(std::string_view s);

#endif // SCRCPY_STR_UTIL_H