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
#include "util/sc_thread.h"
#include <QPointer>


#define WARN_TITLE		  obs_module_text("Warn")

class QWidget;
enum scrcpy_exit_code {
	// Normal program termination
	SCRCPY_EXIT_SUCCESS = 0,

	// No connection could be established
	SCRCPY_EXIT_FAILURE,

	// Device was disconnected while running
	SCRCPY_EXIT_DISCONNECTED,
};

enum puse_stream_type {
	PAUSE_AUDIO = 0,
	PAUSE_VIDEO,
	PAUSE_AUDIO_VIDEO,
	NO_PAUSE = -1
};

class scrcpy {
public:
	scrcpy(obs_data_t *set, obs_source_t *source_);
	scrcpy(const scrcpy &) = delete;
	~scrcpy();
	int srccpy_init(obs_data_t *set);

	void update(obs_data_t *settings);

	void get_device_infos(sc_vec_adb_device_infos &device_infos, const std::string &serial);

	void update_device_infos(sc_vec_adb_device_infos &device_infos);

	void on_interaction_focus(bool focus);

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
	bool set_stream_paused(puse_stream_type stream_type, bool pause);

private:
	uint32_t generate_scid();

	bool video_demuxer_started = false;
	bool audio_demuxer_started = false;
	bool server_started = false;
	bool usb_debug_enable = false;
	obs_source_t *source;
	QPointer<QWidget> last_interaction_window;

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
	puse_stream_type stream_pause_type = NO_PAUSE;
	bool request_device_info();

private:
	sc_mutex device_info_mutex;
	sc_cond device_info_cond;
	bool device_info_received = false;
	void parse_and_update_device_info(const std::string &json_str);
	static void sc_controller_on_device_info(struct sc_controller *controller, const char *json, void *userdata);
	static void sc_controller_on_error_message(struct sc_controller *controller, const char *error_msg, void *userdata);
	void handle_error_message(const std::string &error_msg);
};
void register_srccpy();
