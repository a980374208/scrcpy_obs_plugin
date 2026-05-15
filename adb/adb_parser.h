#pragma once
#include <string_view>
#include <vector>
#include <cassert>
#include "adb_device.h"
#include <string>

bool sc_adb_parse_devices(std::string_view input, std::vector<sc_adb_device> &out);

bool sc_adb_parse_device(std::string_view line, sc_adb_device &device);
