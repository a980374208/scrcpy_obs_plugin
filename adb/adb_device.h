#pragma once
#include <string>
#include <vector>

struct sc_adb_device {
	std::string serial;
	std::string state;
	std::string model;
	bool selected;

	// 默认构造
	sc_adb_device() : serial(), state(), model(), selected(false) {}

	// 完整构造
	sc_adb_device(std::string s, std::string st, std::string m, bool sel = false)
		: serial(std::move(s)),
		  state(std::move(st)),
		  model(std::move(m)),
		  selected(sel)
	{
	}

	// 模板完美转发
	template<typename S, typename St, typename M, typename Sel = bool>
	sc_adb_device(S &&s, St &&st, M &&m, Sel sel = false)
		: serial(std::forward<S>(s)),
		  state(std::forward<St>(st)),
		  model(std::forward<M>(m)),
		  selected(sel)
	{
	}
};

struct sc_adb_device_info {
	sc_adb_device device;
	std::string physical_size;
	std::string best_name;
	int max_fps;
	bool has_external;

	// 默认构造
	sc_adb_device_info() : device(), physical_size(), best_name(), max_fps(0), has_external(false) {}

	// 完整构造
	sc_adb_device_info(sc_adb_device dev, std::string best, std::string phy, int fps, bool ext)
		: device(std::move(dev)),
		  best_name(std::move(best)),
		  physical_size(std::move(phy)),
		  max_fps(fps),
		  has_external(ext)
	{
	}

	// 模板完美转发
	template<typename D, typename B, typename P, typename F, typename H>
	sc_adb_device_info(D &&dev, B &&best, P &&phy, F fps, H ext)
		: device(std::forward<D>(dev)),
		  best_name(std::forward<B>(best)),
		  physical_size(std::forward<P>(phy)),
		  max_fps(fps),
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
typedef std::vector<sc_adb_device_info> sc_vec_adb_device_infos;
