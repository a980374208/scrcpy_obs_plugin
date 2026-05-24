#pragma once
#include <string>
#include <filesystem>

bool sc_file_is_regular(const std::string &path);

std::filesystem::path get_executable_dir();

std::filesystem::path get_app_relative_path(const std::string &relative);
