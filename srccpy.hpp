#pragma once
#include "stdint.h"
#include "server/server.hpp"
#include "codec/demuxer.h"
#include <memory>
#include <obs-module.h>
#include "util/options.h"
#include <adb/adb_device.h>
#include "control_msg.h"
#include "controller.h"
#include <mutex>

enum scrcpy_exit_code {
	// Normal program termination
	SCRCPY_EXIT_SUCCESS = 0,

	// No connection could be established
	SCRCPY_EXIT_FAILURE,

	// Device was disconnected while running
	SCRCPY_EXIT_DISCONNECTED,
};

class scrcpy {
public:
	scrcpy(obs_data_t *, obs_source_t *source_);
	scrcpy(const scrcpy &) = delete;
	~scrcpy();
	int srccpy_init();

	void update(obs_data_t *settings);

	void get_device_infos(sc_vec_adb_device_infos &device_infos,std::string &serial);

	void update_device_infos(sc_vec_adb_device_infos &device_infos);

	sc_vec_adb_device_infos get_device_infos();

	static void sc_server_on_connection_failed(sc_server &server, void *userdata);

	static void sc_server_on_connected(sc_server &server, void *userdata);

	static void sc_server_on_disconnected(sc_server &server, void *userdata);

	static void sc_controller_on_ended(struct sc_controller *controller, bool error, void *userdata);

	// 控制接口声明
	bool send_control_msg(const sc_control_msg &msg);
	void send_mouse_click(const obs_mouse_event *event, int32_t type, bool mouse_up, uint8_t click_count);
	void send_mouse_move(const obs_mouse_event *event, bool mouse_leave);
	void send_mouse_wheel(const obs_mouse_event *event, int x_delta, int y_delta);
	void send_key_click(const obs_key_event *event, bool key_up);

private:
	uint32_t generate_scid();

	bool video_demuxer_started = false;
	bool audio_demuxer_started = false;
	bool server_started = false;
	obs_source_t *source;

public:
	sc_server server;
	sc_demuxer video_demuxer;
	sc_demuxer audio_demuxer;
	struct sc_server_params params;
	uint32_t width = 0;
	uint32_t height = 0;
	sc_vec_adb_device_infos device_infos;
	struct sc_controller controller;
	bool controller_initialized = false;
	bool controller_started = false;
};
void register_srccpy();
