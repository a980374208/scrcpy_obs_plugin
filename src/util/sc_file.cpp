#include "sc_file.h"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif
bool sc_file_is_regular(const std::string &path)
{
	try {
		std::filesystem::path p = path;
		std::cout << "Checking file: " << p << "\n";
		if (!std::filesystem::exists(p)) {
			std::cout << "File does not exist\n";
			return false;
		}
		return std::filesystem::is_regular_file(p);
	} catch (const std::filesystem::filesystem_error &e) {
		std::cerr << "Filesystem error: " << e.what() << "\n";
		return false;
	}
}

std::filesystem::path get_executable_dir()
{
#ifdef _WIN32
	wchar_t buffer[MAX_PATH] = {0};
	GetModuleFileNameW(nullptr, buffer, MAX_PATH);
	std::filesystem::path exe_path(buffer);
#else
	char buffer[PATH_MAX] = {0};
	ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
	if (len == -1) {
		throw std::runtime_error("Cannot determine executable path");
	}
	buffer[len] = '\0';
	std::filesystem::path exe_path(buffer);
#endif
	return exe_path.parent_path(); // 返回可执行程序目录
}

std::filesystem::path get_app_relative_path(const std::string &relative)
{
	if (relative.empty()) {
		throw std::invalid_argument("Relative path is empty");
	}
	// 去掉开头的 '/' 或 '\'，防止被视作绝对路径
	std::string clean_relative = relative;
	if (clean_relative[0] == '/' || clean_relative[0] == '\\') {
		clean_relative.erase(0, 1);
	}
	return get_executable_dir() / clean_relative;
}
