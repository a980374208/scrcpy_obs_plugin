#pragma once
#include <string>
#include <vector>

struct sc_adb_device {
	std::string serial;
	std::string state;
	std::string model;
	std::string physical_size;
	std::string best_name;
	bool selected;
};

enum sc_adb_device_type {
	SC_ADB_DEVICE_TYPE_USB,
	SC_ADB_DEVICE_TYPE_TCPIP,
	SC_ADB_DEVICE_TYPE_EMULATOR,
};

typedef std::vector<sc_adb_device> sc_vec_adb_devices;
