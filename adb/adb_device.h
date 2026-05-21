#pragma once
#include <string>
#include <vector>
#include <map>
#include <util/options.h>

enum device_connect_state {
	DEVICE_STATE_OFFLINE,
	DEVICE_STATE_BOOTLOADER,
	DEVICE_STATE_DEVICE,
	DEVICE_STATE_RECOVERY,
	DEVICE_STATE_UNAUTHORIZE,
	DEVICE_STATE_SIDELOAD,
	DEVICE_STATE_UNKNOWN,
};

struct sc_adb_device {
	std::string serial;
	std::string model;
	device_connect_state state;
	bool selected;

	sc_adb_device() : serial(), state(DEVICE_STATE_UNKNOWN), model(), selected(false) {}

	sc_adb_device(std::string s, device_connect_state st, std::string m, bool sel = false)
		: serial(std::move(s)),
		  state(st),
		  model(std::move(m)),
		  selected(sel)
	{
	}
};
struct sc_adb_display {
	std::uint32_t id;
	std::string physical_size;
	std::uint32_t fps;
};

struct sc_adb_camera {
	std::string id;
	sc_camera_facing facing;
	std::vector<std::string> suport_sizes;
	std::vector<int16_t> suport_fps;
};

struct sc_adb_device_info {
	sc_adb_device device;

	std::map<uint32_t, sc_adb_display> displays;
	std::map<std::string,sc_adb_camera> cameras;
	std::string best_name;
	bool has_external;

	// 默认构造
	sc_adb_device_info() : device(), best_name(), has_external(false) {}

	// 完整构造
	sc_adb_device_info(sc_adb_device dev, std::string best, bool ext)
		: device(std::move(dev)),
		  best_name(std::move(best)),
		  has_external(ext)
	{
	}

	// 模板完美转发
	template<typename D, typename B, typename P, typename F, typename H>
	sc_adb_device_info(D &&dev, B &&best, P &&phy, F fps, H ext)
		: device(std::forward<D>(dev)),
		  best_name(std::forward<B>(best)),
		  has_external(ext)
	{
	}
};
enum sc_adb_device_type {
	SC_ADB_DEVICE_TYPE_USB,
	SC_ADB_DEVICE_TYPE_TCPIP,
	SC_ADB_DEVICE_TYPE_EMULATOR,
};

typedef std::vector<sc_adb_device> sc_vec_adb_devices;
typedef std::map<std::string,sc_adb_device_info> sc_vec_adb_device_infos;
