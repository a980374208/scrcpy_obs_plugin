#pragma once
#include <string>
#include <vector>

struct sc_adb_device {
	std::string serial;
	std::string state;
	std::string model;
	bool selected;
};

struct sc_adb_device_info {
	struct sc_adb_device device;
	std::string physical_size;
	std::string best_name;
	template<typename D, typename B, typename P>
	sc_adb_device_info(D &&device_, B &&best_name_, P &&physical_size_)
		: device(std::forward<D>(device_)),
		  best_name(std::forward<B>(best_name_)),
		  physical_size(std::forward<P>(physical_size_))
	{
	}
};

enum sc_adb_device_type {
	SC_ADB_DEVICE_TYPE_USB,
	SC_ADB_DEVICE_TYPE_TCPIP,
	SC_ADB_DEVICE_TYPE_EMULATOR,
};

typedef std::vector<sc_adb_device> sc_vec_adb_devices;
typedef std::vector<sc_adb_device_info> sc_vec_adb_device_infos;
