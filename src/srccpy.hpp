#pragma once
#include "stdint.h"
#include "capture_session.h"
#include <memory>
#include <obs-module.h>
#include "util/options.h"
#include <adb/adb_device.h>
#include "control_msg.h"
#include "controller.h"
#include "device_query.h"
#include "session_update.h"
#include "util/sc_thread.h"
#include <atomic>
#include <optional>
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
	PAUSE_VIDEO = 1,
	PAUSE_AUDIO = 2,
	PAUSE_AUDIO_VIDEO = 3,
	NO_PAUSE = -1
};

class scrcpy {
public:
	scrcpy(obs_data_t *set, obs_source_t *source_);
	scrcpy(const scrcpy &) = delete;
	~scrcpy();
	int srccpy_init(obs_data_t *set);

	void update(obs_data_t *settings);
	void video_tick();

	sc_device_query_result refresh_device_infos(sc_tick timeout = SC_TICK_FROM_SEC(10));

	void update_device_infos(sc_vec_adb_device_infos device_infos);

	void on_interaction_focus(bool focus);

	sc_vec_adb_device_infos get_device_infos();

	// 控制接口声明
	bool send_control_msg(const sc_control_msg &msg);
	void send_mouse_click(const obs_mouse_event *event, int32_t type, bool mouse_up, uint8_t click_count);
	void send_mouse_move(const obs_mouse_event *event, bool mouse_leave);
	void send_mouse_wheel(const obs_mouse_event *event, int x_delta, int y_delta);
	void send_key_click(const obs_key_event *event, bool key_up);
	bool set_stream_paused(puse_stream_type stream_type, bool pause, uint8_t audio_source = 0);
	uint32_t get_width() const;
	uint32_t get_height() const;

private:
	void stop_session(bool failed = false);
	void consume_session_events(bool start_pending);
	bool parse_capture_config(obs_data_t *settings, sc_capture_config &config,
				  std::string &error_message);
	void apply_capture_config(const sc_capture_config &config);
	bool start_session(const sc_capture_config &config);
	bool execute_dynamic_update(const sc_capture_config &desired,
				    const sc_update_plan &plan);
	uint32_t generate_scid();

	std::atomic<bool> usb_debug_enable{false};
	obs_source_t *source;
	QPointer<QWidget> last_interaction_window;
	sc_capture_owner capture_owner;
	std::optional<sc_capture_config> submitted_config;
	std::optional<sc_capture_config> pending_config;
	std::atomic<uint32_t> last_width{0};
	std::atomic<uint32_t> last_height{0};
	uint64_t next_generation = 0;

public:
	struct sc_server_params params{};
	sc_vec_adb_device_infos device_infos;
	puse_stream_type stream_pause_type = NO_PAUSE;
	bool request_device_info();
	obs_source_t *get_source() const { return source; }


private:
	sc_mutex device_info_mutex;
	void parse_and_update_device_info(const std::string &json_str,
					  const std::string &session_serial);
	void handle_error_message(const std::string &error_msg);
};

bool sc_persist_default_device_selection(obs_data_t *settings,
					 obs_property_t *device_property);
void register_srccpy();
