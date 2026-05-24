#include "srccpy.hpp"
#include "util/rand.h"
#include "server/server.hpp"
#include "util/options.h"
#include "util/sc_log.h"
#include "codec/demuxer.h"
#include "codec/packet_sink.h"
#include <thread>
#include <obs-source.h>
#include "util/str_util.h"
#include <util/process_intr.h>
#include <adb/adb.h>
#include <nlohmann/json.hpp>
#include <obs-frontend-api.h>
#include <qmessagebox.h>
#include <QMetaObject>

#define INTERACTION_WARN_TITLE          obs_module_text("InteractionWarn")
#define INTERACTION_ERROR_TEXT          obs_module_text("InteractionWarnText")
//Inertaction failed,Make sure you have enabled USB debugging (Security Settings) and then rebooted your device.

static bool await_for_signal(ServerConnectSignal &signal)
{
	std::future<bool> future = signal.promise.get_future();

	bool ok = future.get(); // 阻塞等待

	return ok;
}

static void sc_video_demuxer_on_ended(sc_demuxer *demuxer, enum sc_demuxer_status status, void *userdata)
{
	(void)demuxer;
	(void)userdata;

	// The device may not decide to disable the video
	assert(status != SC_DEMUXER_STATUS_DISABLED);

	if (status == SC_DEMUXER_STATUS_EOS) {
		// sc_push_event(SC_EVENT_DEVICE_DISCONNECTED);
	} else {
		// sc_push_event(SC_EVENT_DEMUXER_ERROR);
	}
}

static void sc_audio_demuxer_on_ended(sc_demuxer *demuxer, enum sc_demuxer_status status, void *userdata)
{
	(void)demuxer;

	const struct scrcpy_options *options = (const struct scrcpy_options *)userdata;

	// Contrary to the video demuxer, keep mirroring if only the audio fails
	// (unless --require-audio is set).
	if (status == SC_DEMUXER_STATUS_EOS) {
		// sc_push_event(SC_EVENT_DEVICE_DISCONNECTED);
	} else if (status == SC_DEMUXER_STATUS_ERROR ||
		   (status == SC_DEMUXER_STATUS_DISABLED && options->require_audio)) {
		// sc_push_event(SC_EVENT_DEMUXER_ERROR);
	}
}

static const char *get_connect_state_error_message(device_connect_state state)
{
	switch (state) {
	case DEVICE_STATE_OFFLINE:
		return obs_module_text("DeviceState.Offline");
	case DEVICE_STATE_BOOTLOADER:
		return obs_module_text("DeviceState.Bootloader");
	case DEVICE_STATE_RECOVERY:
		return obs_module_text("DeviceState.Recovery");
	case DEVICE_STATE_UNAUTHORIZE:
		return obs_module_text("DeviceState.Unauthorized");
	case DEVICE_STATE_SIDELOAD:
		return obs_module_text("DeviceState.Sideload");
	case DEVICE_STATE_DEVICE:
		return obs_module_text("DeviceState.Connected");
	case DEVICE_STATE_UNKNOWN:
	default: {
		std::stringstream ss;
		ss << "DeviceState.Unknown" << " (" << state << ")";
		return obs_module_text(ss.str().c_str());
	}
	}
}

scrcpy::scrcpy(obs_data_t *set, obs_source_t *source_) : source(source_) {
	srccpy_init(set);
}

scrcpy::~scrcpy()
{
	if (controller_started) {
		sc_controller_stop(&this->controller);
		sc_controller_join(&this->controller);
	}
	if (controller_initialized) {
		sc_controller_destroy(&this->controller);
	}
	if (server_started) {
		this->server.server_stop();
	}
	if (video_demuxer_started) {
		this->video_demuxer.join();
	}
	if (audio_demuxer_started) {
		this->audio_demuxer.join();
	}
}

int scrcpy::srccpy_init(obs_data_t *set)
{
	sc_cond_init(device_info_cond);
	uint32_t scid = generate_scid();

	enum scrcpy_exit_code ret = SCRCPY_EXIT_FAILURE;

	static const struct sc_server_callbacks cbs = {&scrcpy::sc_server_on_connection_failed,
						       &scrcpy::sc_server_on_connected,
						       &scrcpy::sc_server_on_disconnected};
	params.scid = scid;
	params.req_serial = "";
	params.log_level = SC_LOG_LEVEL_DEBUG;
	params.video_codec = SC_CODEC_H264;
	params.audio_codec = SC_CODEC_OPUS;
	params.video_source = SC_VIDEO_SOURCE_DISPLAY;
	params.audio_source = SC_AUDIO_SOURCE_MIC;
	params.camera_facing = SC_CAMERA_FACING_FRONT;
	params.crop = "";
	params.video_codec_options = "";
	params.audio_codec_options = "";
	params.video_encoder = "";
	params.audio_encoder = "";
	params.camera_id = "";
	params.camera_size = "";
	params.camera_ar = "";
	params.camera_fps = 0;
	params.port_range.first = 27183;
	params.port_range.last = 27199;
	params.tunnel_host = 0;
	params.tunnel_port = 0;
	params.max_size = 0;
	params.video_bit_rate = 0;
	params.audio_bit_rate = 0;
	params.max_fps = "";
	params.angle = "";
	params.screen_off_timeout = -1;
	params.capture_orientation = SC_ORIENTATION_0;
	params.capture_orientation_lock = SC_ORIENTATION_UNLOCKED;
	params.control = true;
	params.display_id = 0;
	params.new_display = "";
	params.display_ime_policy = SC_DISPLAY_IME_POLICY_UNDEFINED;
	params.video = true;
	params.audio = false;
	params.audio_dup = false;
	params.show_touches = false;
	params.stay_awake = false;
	params.force_adb_forward = false;
	params.power_off_on_close = false;
	params.clipboard_autosync = true;
	params.downsize_on_error = true;
	params.tcpip = false;
	params.tcpip_dst = "";
	params.select_usb = false;
	params.cleanup = true;
	params.power_on = true;
	params.kill_adb_on_close = false;
	params.camera_high_speed = false;
	params.vd_destroy_content = true;
	params.list = 0;

	if (!server.server_init(&cbs, this)) {
		return SCRCPY_EXIT_FAILURE;
	}
	
	update(set);
	return 0;
}

void scrcpy::update(obs_data_t *settings)
{
	bool updated = false;
	if (settings) {

		std::string select_device = obs_data_get_string(settings, "device_list");
		std::string select_res = obs_data_get_string(settings, "choose_res");
		sc_video_source choose_src = static_cast<enum sc_video_source>(obs_data_get_int(settings, "choose_src"));
		std::string choose_capture = obs_data_get_string(settings, "choose_capture");
		int max_fps = (int)obs_data_get_int(settings, "choose_fps");
		if (select_device.empty()) {
			return;
		}
		if (device_infos.find(select_device) != device_infos.end()) {
			auto state = device_infos[select_device].device.state;
			if (state != DEVICE_STATE_DEVICE) {
				QWidget *parent_widget = static_cast<QWidget *>(obs_frontend_get_main_window());
				if (parent_widget) {
					const char *warn_text = get_connect_state_error_message(state);
					QString title = QString::fromUtf8(WARN_TITLE);
					QString text = QString::fromUtf8(warn_text);
					QMetaObject::invokeMethod(parent_widget, [parent_widget, title, text]() {
						QMessageBox::warning(parent_widget, title, text);
					}, Qt::QueuedConnection);
				}
				
			}
				
		}
		bool serial_changed = (params.req_serial != select_device);
		bool codec_res_fps_changed = (params.max_fps != std::to_string(max_fps));

		int cx = 0, cy = 0;
		ResolutionValid(select_res, cx, cy);

		if (choose_src == SC_VIDEO_SOURCE_DISPLAY) {
			if (params.max_size != cx) {
				codec_res_fps_changed = true;
			}
		} else {
			if (params.camera_size != select_res) {
				codec_res_fps_changed = true;
			}
		}

		bool src_or_id_changed = (params.video_source != choose_src);
		if (choose_src == SC_VIDEO_SOURCE_DISPLAY) {
			int id = choose_capture.empty() ? 0 : std::stoi(choose_capture);
			if (id != params.display_id) {
				src_or_id_changed = true;
			}
		} else {
			if (params.camera_id != choose_capture) {
				src_or_id_changed = true;
			}
		}

		bool resolution_changed = false;
		if (choose_src == SC_VIDEO_SOURCE_DISPLAY) {
			resolution_changed = (params.max_size != cx);
		} else {
			resolution_changed = (params.camera_size != select_res);
		}
		bool fps_changed = (params.max_fps != std::to_string(max_fps));

		bool config_changed = src_or_id_changed || resolution_changed || fps_changed;

		if (!serial_changed && config_changed &&
		    server_started && controller_initialized && controller_started) {
			
			scrcpy_log(LOG_INFO, "Dynamically switching video source to %s (%s)",
			     (choose_src == SC_VIDEO_SOURCE_DISPLAY) ? "display" : "camera",
			     choose_capture.c_str());

			sc_control_msg msg;
			memset(&msg, 0, sizeof(msg));
			msg.type = SC_CONTROL_MSG_TYPE_SWITCH_VIDEO_SOURCE;
			msg.switch_video_source.source = (choose_src == SC_VIDEO_SOURCE_DISPLAY) ? 0 : 1;
			if (choose_src == SC_VIDEO_SOURCE_DISPLAY) {
				msg.switch_video_source.display_id = choose_capture.empty() ? 0 : std::stoi(choose_capture);
				int size = cx > cy ? cx : cy;
				msg.switch_video_source.max_size = size;
				msg.switch_video_source.max_fps = (float)max_fps;
			} else {
				msg.switch_video_source.camera_id = _strdup(choose_capture.c_str());
				msg.switch_video_source.camera_width = cx;
				msg.switch_video_source.camera_height = cy;
				msg.switch_video_source.camera_fps = max_fps;
			}

			send_control_msg(msg);
			sc_control_msg_destroy(&msg);

			params.video_source = choose_src;
			if (choose_src == SC_VIDEO_SOURCE_DISPLAY) {
				params.display_id = choose_capture.empty() ? 0 : std::stoi(choose_capture);
				params.max_size = cx > cy ? cx : cy;
			} else {
				params.camera_id = choose_capture;
				params.camera_size = select_res;
			}
			params.max_fps = std::to_string(max_fps);

			server.update_params(&params);
			return;
		}

		if (serial_changed) {
			params.req_serial = select_device;
			updated = true;
		}
		if (params.video_source != choose_src) {
			params.video_source = choose_src;
			updated = true;
		}
		if (choose_src == SC_VIDEO_SOURCE_DISPLAY) {
			int id = choose_capture.empty() ? 0 : std::stoi(choose_capture);
			if (id != params.display_id) {
				params.display_id = id;
				updated = true;
			}
			int size = cx > cy ? cx : cy;
			if (params.max_size != size) {
				params.max_size = size;
				updated = true;
			}
		} else {
			if (params.camera_id != choose_capture) {
				params.camera_id = choose_capture;
				updated = true;
			}
			if (params.camera_size != select_res) {
				params.camera_size = select_res;
				updated = true;
			}
		}
		if (params.max_fps != std::to_string(max_fps)) {
			params.max_fps = std::to_string(max_fps);
			updated = true;
		}

		
	}
	if (!updated && server_started) {
		return;
	}

	params.scid = generate_scid();
	server.update_params(&params);

	if (controller_started) {
		this->usb_debug_enable = false;
		sc_controller_stop(&this->controller);
		sc_controller_join(&this->controller);
		controller_started = false;
	}
	if (controller_initialized) {
		sc_controller_destroy(&this->controller);
		controller_initialized = false;
	}

	if (video_demuxer_started) {
		this->video_demuxer.join();
		video_demuxer_started = false;
	}
	if (audio_demuxer_started) {
		this->audio_demuxer.join();
		audio_demuxer_started = false;
	}
	if (this->server.m_video_socket != SC_SOCKET_NONE) {
		net_close(this->server.m_video_socket);
		this->server.m_video_socket = SC_SOCKET_NONE;
	}
	if (this->server.m_audio_socket != SC_SOCKET_NONE) {
		net_close(this->server.m_audio_socket);
		this->server.m_audio_socket = SC_SOCKET_NONE;
	}
	if (this->server.m_control_socket != SC_SOCKET_NONE) {
		net_close(this->server.m_control_socket);
		this->server.m_control_socket = SC_SOCKET_NONE;
	}
	if (server_started) {
		this->server.server_stop();
		server_started = false;
	}

	server_started = server.server_start();
	bool connected = await_for_signal(server.m_connect_signal);
	if (!connected) {
		error("Server connection failed");
		return;
	}

	if (video_demuxer_started) {
		this->video_demuxer.join();
		video_demuxer_started = false;
	}

	std::shared_ptr<sc_demuxer_callbacks> video_demuxer_cbs = std::make_shared<sc_demuxer_callbacks>();
	video_demuxer_cbs->on_ended = sc_video_demuxer_on_ended;
	this->video_demuxer.init("video", this->server.m_video_socket, video_demuxer_cbs, NULL);

	AVCodecID codec_id = AV_CODEC_ID_H264;
	if (params.video_codec == SC_CODEC_H265) {
		codec_id = AV_CODEC_ID_HEVC;
	} else if (params.video_codec == SC_CODEC_AV1) {
		codec_id = AV_CODEC_ID_AV1;
	}

	this->video_demuxer.packet_source.clear_sinks();
	auto video_sink = std::make_shared<sc_receive_packet_sink>(this, this->source, codec_id);
	this->video_demuxer.packet_source.add_sink(video_sink);

	if (!this->video_demuxer.start()) {
		error("Failed to start video demuxer");
	} else {
		video_demuxer_started = true;
	}

	if (params.control) {
		static const struct sc_controller_callbacks controller_cbs = {
			&scrcpy::sc_controller_on_ended,
			&scrcpy::sc_controller_on_device_info,
			&scrcpy::sc_controller_on_error_message,
		};
		if (!sc_controller_init(&this->controller, this->server.m_control_socket, &controller_cbs, this)) {
			error("Failed to initialize controller");
			return;
		}
		controller_initialized = true;

		sc_controller_configure(&this->controller, NULL, NULL);

		if (!sc_controller_start(&this->controller)) {
			error("Failed to start controller");
			return;
		}
		controller_started = true;
		this->usb_debug_enable = true;
	}
}

void scrcpy::get_device_infos(sc_vec_adb_device_infos &device_infos, const std::string &serial)
{
	this->server.m_serial = serial;
	this->server.push_server(server.m_intr, serial);
	params.list = SC_OPTION_LIST_DEVICE_INFOS;
	sc_pipe pout = 0;
	sc_pid pid = server.execute_server(params,&pout);
	if (pid == SC_PROCESS_NONE) {
		return;
	}
	if (pid == SC_PROCESS_NONE) {
		// LOGE("Could not execute adb devices -l");
		return ;
	}
	params.list = 0;
	std::vector<char> buf;
	size_t r;
	buf.resize(BUFSIZE);
	r = sc_pipe_read_all_intr(server.m_intr, pid, pout, buf.data(), BUFSIZE - 1);
	sc_pipe_close(pout);
	bool ok = process_check_success_intr(server.m_intr, pid, "adb -s SC_OPTION_LIST_DEVICE_INFOS", 0);

	if (ok) {
		std::string json_str(buf.data(), r);
		try {
			auto j = nlohmann::json::parse(json_str);
			sc_adb_device_info info{};

			if (j.contains("serial") && j["serial"].is_string()) {
				info.device.serial = j["serial"].get<std::string>();
			}
			if (j.contains("state") && j["state"].is_string()) {
				info.device.state = get_device_state_from_string(j["state"].get<std::string>());
			}
			if (j.contains("device") && j["device"].is_string()) {
				info.device.model = j["device"].get<std::string>();
			}
			info.device.selected = false;

			info.best_name = info.device.model;

			if (j.contains("display") && j["display"].is_array()) {
				for (const auto &disp : j["display"]) {
					sc_adb_display d;
					if (disp.contains("id")) {
						if (disp["id"].is_number()) {
							d.id =disp["id"].get<int>();
						} else if (disp["id"].is_string()) {
							d.id = std::stoi(disp["id"].get<std::string>());
						}
					}
					if (disp.contains("size") && disp["size"].is_string()) {
						d.physical_size = disp["size"].get<std::string>();
					}
					if (disp.contains("fps")) {
						if (disp["fps"].is_number()) {
							d.fps = disp["fps"].get<int>();
						} else if (disp["fps"].is_string()) {
							d.fps = std::stoi(disp["fps"].get<std::string>());
						}
					}
					info.displays.emplace(d.id,std::move(d));
				}
			}


			info.has_external = false;
			if (j.contains("camera") && j["camera"].is_array()) {
				for (const auto &cam : j["camera"]) {
					sc_adb_camera c;
					if (cam.contains("id")) {
						if (cam["id"].is_number()) {
							c.id = std::to_string(cam["id"].get<int>());
						} else if (cam["id"].is_string()) {
							c.id = cam["id"].get<std::string>();
						}
					}
					if (cam.contains("facing") && cam["facing"].is_string()) {
						std::string facing = cam["facing"].get<std::string>();
						if (facing == "front") {
							c.facing = SC_CAMERA_FACING_FRONT;
						} else if (facing == "back") {
							c.facing = SC_CAMERA_FACING_BACK;
						} else if (facing == "external") {
							c.facing = SC_CAMERA_FACING_EXTERNAL;
							info.has_external = true;
						} else {
							c.facing = SC_CAMERA_FACING_ANY;
						}
					} else {
						c.facing = SC_CAMERA_FACING_ANY;
					}

					if (cam.contains("size") && cam["size"].is_array()) {
						for (const auto &sz : cam["size"]) {
							if (sz.is_string()) {
								c.suport_sizes.push_back(sz.get<std::string>());
							}
						}
					}

					if (cam.contains("fps") && cam["fps"].is_array()) {
						for (const auto &f : cam["fps"]) {
							if (f.is_number()) {
								c.suport_fps.push_back(f.get<int16_t>());
							}
						}
					}
					info.cameras.emplace(c.id,std::move(c));
				}
			}

			device_infos.emplace(info.device.serial,std::move(info));
		} catch (const std::exception &e) {
			scrcpy_log(LOG_WARNING, "Failed to parse device info JSON: %s", e.what());
		}
	}


}

void scrcpy::update_device_infos(sc_vec_adb_device_infos &device_infos)
{
	std::lock_guard<sc_mutex> lock(device_info_mutex);
	this->device_infos = std::move(device_infos);
}

void scrcpy::on_interaction_focus(bool focus)
{
	if (focus && !this->usb_debug_enable) {
		QWidget *parent_widget = static_cast<QWidget *>(obs_frontend_get_main_window());
		if (!parent_widget)
			return;

		QWidget *interact_window = nullptr;
		QList<QWidget *> all_widgets = parent_widget->findChildren<QWidget *>();
		for (QWidget *w : all_widgets) {
			if (strcmp(w->metaObject()->className(), "OBSBasicInteraction") == 0) {
				interact_window = w;
				break;
			}
		}

		if (interact_window) {
			if (last_interaction_window == interact_window) {
				return;
			}
			last_interaction_window = interact_window;
		}

		QString title = QString::fromUtf8(INTERACTION_WARN_TITLE);
		QString text = QString::fromUtf8(INTERACTION_ERROR_TEXT);
		QMetaObject::invokeMethod(parent_widget, [parent_widget, title, text]() {
			QMessageBox::warning(parent_widget, title, text);
		}, Qt::QueuedConnection);
	}
}

sc_vec_adb_device_infos scrcpy::get_device_infos() {
	std::lock_guard<sc_mutex> lock(device_info_mutex);
	return this->device_infos;
}

void scrcpy::sc_server_on_connection_failed(sc_server &server, void *userdata)
{
	server.m_connect_signal.promise.set_value(false);
}

void scrcpy::sc_server_on_connected(sc_server &server, void *userdata)
{
	server.m_connect_signal.promise.set_value(true);
}

void scrcpy::sc_server_on_disconnected(sc_server &server, void *userdata)
{
	// LOGD("Server disconnected");
}

uint32_t scrcpy::generate_scid()
{
	sc_rand rand;
	// Only use 31 bits to avoid issues with signed values on the Java-side
	return rand.sc_rand_u32() & 0x7FFFFFFF;
}

#include "util/net.h"

static enum android_keycode vk_to_android_keycode(uint32_t vk) {
    if (vk >= 'A' && vk <= 'Z') {
        return (enum android_keycode)(AKEYCODE_A + (vk - 'A'));
    }
    if (vk >= '0' && vk <= '9') {
        return (enum android_keycode)(AKEYCODE_0 + (vk - '0'));
    }
    if (vk >= 0x60 && vk <= 0x69) { // NUMPAD_0 - NUMPAD_9
        return (enum android_keycode)(AKEYCODE_NUMPAD_0 + (vk - 0x60));
    }
    switch (vk) {
        case 0x08: return AKEYCODE_DEL; // VK_BACK
        case 0x09: return AKEYCODE_TAB; // VK_TAB
        case 0x0D: return AKEYCODE_ENTER; // VK_RETURN
        case 0x12: return AKEYCODE_ALT_LEFT; // VK_MENU (Alt)
        case 0x13: return AKEYCODE_BREAK; // VK_PAUSE
        case 0x14: return AKEYCODE_CAPS_LOCK; // VK_CAPITAL
        case 0x1B: return AKEYCODE_ESCAPE; // VK_ESCAPE
        case 0x20: return AKEYCODE_SPACE; // VK_SPACE
        case 0x21: return AKEYCODE_PAGE_UP; // VK_PRIOR (Page Up)
        case 0x22: return AKEYCODE_PAGE_DOWN; // VK_NEXT (Page Down)
        case 0x23: return AKEYCODE_MOVE_END; // VK_END
        case 0x24: return AKEYCODE_MOVE_HOME; // VK_HOME
        case 0x25: return AKEYCODE_DPAD_LEFT; // VK_LEFT
        case 0x26: return AKEYCODE_DPAD_UP; // VK_UP
        case 0x27: return AKEYCODE_DPAD_RIGHT; // VK_RIGHT
        case 0x28: return AKEYCODE_DPAD_DOWN; // VK_DOWN
        case 0x2C: return AKEYCODE_SYSRQ; // VK_SNAPSHOT (Print Screen)
        case 0x2D: return AKEYCODE_INSERT; // VK_INSERT
        case 0x2E: return AKEYCODE_FORWARD_DEL; // VK_DELETE
        case 0x5B: return AKEYCODE_META_LEFT; // VK_LWIN
        case 0x5C: return AKEYCODE_META_RIGHT; // VK_RWIN
        case 0x10:
        case 0xA0: return AKEYCODE_SHIFT_LEFT; // VK_LSHIFT
        case 0xA1: return AKEYCODE_SHIFT_RIGHT; // VK_RSHIFT
        case 0x11:
        case 0xA2: return AKEYCODE_CTRL_LEFT; // VK_LCONTROL
        case 0xA3: return AKEYCODE_CTRL_RIGHT; // VK_RCONTROL
        case 0x90: return AKEYCODE_NUM_LOCK; // VK_NUMLOCK
        case 0x91: return AKEYCODE_SCROLL_LOCK; // VK_SCROLL
        case 0x70: return AKEYCODE_F1;
        case 0x71: return AKEYCODE_F2;
        case 0x72: return AKEYCODE_F3;
        case 0x73: return AKEYCODE_F4;
        case 0x74: return AKEYCODE_F5;
        case 0x75: return AKEYCODE_F6;
        case 0x76: return AKEYCODE_F7;
        case 0x77: return AKEYCODE_F8;
        case 0x78: return AKEYCODE_F9;
        case 0x79: return AKEYCODE_F10;
        case 0x7A: return AKEYCODE_F11;
        case 0x7B: return AKEYCODE_F12;
        case 0x6F: return AKEYCODE_NUMPAD_DIVIDE;
        case 0x6A: return AKEYCODE_NUMPAD_MULTIPLY;
        case 0x6D: return AKEYCODE_NUMPAD_SUBTRACT;
        case 0x6B: return AKEYCODE_NUMPAD_ADD;
        case 0x6E: return AKEYCODE_NUMPAD_DOT;
        case 0xBA: return AKEYCODE_SEMICOLON;
        case 0xBB: return AKEYCODE_EQUALS;
        case 0xBC: return AKEYCODE_COMMA;
        case 0xBD: return AKEYCODE_MINUS;
        case 0xBE: return AKEYCODE_PERIOD;
        case 0xBF: return AKEYCODE_SLASH;
        case 0xC0: return AKEYCODE_GRAVE;
        case 0xDB: return AKEYCODE_LEFT_BRACKET;
        case 0xDC: return AKEYCODE_BACKSLASH;
        case 0xDD: return AKEYCODE_RIGHT_BRACKET;
        case 0xDE: return AKEYCODE_APOSTROPHE;
        default: return AKEYCODE_UNKNOWN;
    }
}

static uint32_t obs_modifiers_to_android_metastate(uint32_t modifiers) {
    uint32_t state = 0;
    if (modifiers & INTERACT_SHIFT_KEY) {
        state |= AMETA_SHIFT_ON;
    }
    if (modifiers & INTERACT_CONTROL_KEY) {
        state |= AMETA_CTRL_ON;
    }
    if (modifiers & INTERACT_ALT_KEY) {
        state |= AMETA_ALT_ON;
    }
    if (modifiers & INTERACT_COMMAND_KEY) {
        state |= AMETA_META_ON;
    }
    if (modifiers & INTERACT_CAPS_KEY) {
        state |= AMETA_CAPS_LOCK_ON;
    }
    if (modifiers & INTERACT_NUMLOCK_KEY) {
        state |= AMETA_NUM_LOCK_ON;
    }
    return state;
}

bool scrcpy::send_control_msg(const sc_control_msg &msg)
{
	if (!controller_initialized || !controller_started) {
		return false;
	}
	return sc_controller_push_msg(&this->controller, &msg);
}

void scrcpy::send_mouse_click(const obs_mouse_event *event, int32_t type, bool mouse_up, uint8_t click_count)
{
	(void)click_count;
	sc_control_msg msg;
	memset(&msg, 0, sizeof(msg));
	msg.type = SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT;

	uint32_t action_button = 0;
	if (type == MOUSE_LEFT) {
		action_button = AMOTION_EVENT_BUTTON_PRIMARY;
	} else if (type == MOUSE_MIDDLE) {
		action_button = AMOTION_EVENT_BUTTON_TERTIARY;
	} else if (type == MOUSE_RIGHT) {
		action_button = AMOTION_EVENT_BUTTON_SECONDARY;
	}

	msg.inject_touch_event.action = mouse_up ? AMOTION_EVENT_ACTION_UP : AMOTION_EVENT_ACTION_DOWN;
	msg.inject_touch_event.pointer_id = SC_POINTER_ID_MOUSE;
	msg.inject_touch_event.position.screen_size.width = (uint16_t)(width > 0 ? width : 1080);
	msg.inject_touch_event.position.screen_size.height = (uint16_t)(height > 0 ? height : 1920);
	msg.inject_touch_event.position.point.x = event->x;
	msg.inject_touch_event.position.point.y = event->y;
	msg.inject_touch_event.pressure = mouse_up ? 0.0f : 1.0f;
	msg.inject_touch_event.action_button = (android_motionevent_buttons)action_button;

	uint32_t buttons = 0;
	if (event->modifiers & INTERACT_MOUSE_LEFT) {
		buttons |= AMOTION_EVENT_BUTTON_PRIMARY;
	}
	if (event->modifiers & INTERACT_MOUSE_MIDDLE) {
		buttons |= AMOTION_EVENT_BUTTON_TERTIARY;
	}
	if (event->modifiers & INTERACT_MOUSE_RIGHT) {
		buttons |= AMOTION_EVENT_BUTTON_SECONDARY;
	}

	if (mouse_up) {
		buttons &= ~action_button;
	} else {
		buttons |= action_button;
	}
	msg.inject_touch_event.buttons = (android_motionevent_buttons)buttons;

	send_control_msg(msg);
}

void scrcpy::send_mouse_move(const obs_mouse_event *event, bool mouse_leave)
{
	if (mouse_leave) {
		return;
	}
	sc_control_msg msg;
	memset(&msg, 0, sizeof(msg));
	msg.type = SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT;
	msg.inject_touch_event.action = AMOTION_EVENT_ACTION_HOVER_MOVE;
	msg.inject_touch_event.pointer_id = SC_POINTER_ID_MOUSE;
	msg.inject_touch_event.position.screen_size.width = (uint16_t)(width > 0 ? width : 1080);
	msg.inject_touch_event.position.screen_size.height = (uint16_t)(height > 0 ? height : 1920);
	msg.inject_touch_event.position.point.x = event->x;
	msg.inject_touch_event.position.point.y = event->y;
	msg.inject_touch_event.pressure = 0.0f;
	msg.inject_touch_event.action_button = (android_motionevent_buttons)0;

	uint32_t buttons = 0;
	if (event->modifiers & INTERACT_MOUSE_LEFT) {
		buttons |= AMOTION_EVENT_BUTTON_PRIMARY;
	}
	if (event->modifiers & INTERACT_MOUSE_MIDDLE) {
		buttons |= AMOTION_EVENT_BUTTON_TERTIARY;
	}
	if (event->modifiers & INTERACT_MOUSE_RIGHT) {
		buttons |= AMOTION_EVENT_BUTTON_SECONDARY;
	}
	msg.inject_touch_event.buttons = (android_motionevent_buttons)buttons;

	if (buttons != 0) {
		msg.inject_touch_event.action = AMOTION_EVENT_ACTION_MOVE;
		msg.inject_touch_event.pressure = 1.0f;
	}

	send_control_msg(msg);
}

void scrcpy::send_mouse_wheel(const obs_mouse_event *event, int x_delta, int y_delta)
{
	sc_control_msg msg;
	memset(&msg, 0, sizeof(msg));
	msg.type = SC_CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT;
	msg.inject_scroll_event.position.screen_size.width = (uint16_t)(width > 0 ? width : 1080);
	msg.inject_scroll_event.position.screen_size.height = (uint16_t)(height > 0 ? height : 1920);
	msg.inject_scroll_event.position.point.x = event->x;
	msg.inject_scroll_event.position.point.y = event->y;
	msg.inject_scroll_event.hscroll = (float)x_delta / 120.0f;
	msg.inject_scroll_event.vscroll = (float)y_delta / 120.0f;

	uint32_t buttons = 0;
	if (event->modifiers & INTERACT_MOUSE_LEFT) {
		buttons |= AMOTION_EVENT_BUTTON_PRIMARY;
	}
	if (event->modifiers & INTERACT_MOUSE_MIDDLE) {
		buttons |= AMOTION_EVENT_BUTTON_TERTIARY;
	}
	if (event->modifiers & INTERACT_MOUSE_RIGHT) {
		buttons |= AMOTION_EVENT_BUTTON_SECONDARY;
	}
	msg.inject_scroll_event.buttons = (android_motionevent_buttons)buttons;

	send_control_msg(msg);
}

void scrcpy::send_key_click(const obs_key_event *event, bool key_up)
{
	enum android_keycode keycode = vk_to_android_keycode(event->native_vkey);
	if (event->text && strlen(event->text) > 0 && (event->native_vkey == 0xE5 || (unsigned char)event->text[0] > 127)) {
		if (!key_up) {
			sc_control_msg text_msg;
			memset(&text_msg, 0, sizeof(text_msg));
			text_msg.type = SC_CONTROL_MSG_TYPE_INJECT_TEXT;
			text_msg.inject_text.text = _strdup(event->text);
			send_control_msg(text_msg);
			sc_control_msg_destroy(&text_msg);
		}
	} else if (keycode != AKEYCODE_UNKNOWN) {
		sc_control_msg msg;
		memset(&msg, 0, sizeof(msg));
		msg.type = SC_CONTROL_MSG_TYPE_INJECT_KEYCODE;
		msg.inject_keycode.action = key_up ? AKEY_EVENT_ACTION_UP : AKEY_EVENT_ACTION_DOWN;
		msg.inject_keycode.keycode = keycode;
		msg.inject_keycode.repeat = 0;
		msg.inject_keycode.metastate = (android_metastate)obs_modifiers_to_android_metastate(event->modifiers);
		send_control_msg(msg);
	}
}

bool scrcpy::set_stream_paused(puse_stream_type stream_type, bool pause)
{
	if (stream_pause_type == stream_type) {
		return true;
	}
	stream_pause_type = stream_type;
	sc_control_msg msg;
	memset(&msg, 0, sizeof(msg));
	msg.type = SC_CONTROL_MSG_TYPE_PAUSE_RESUME_STREAM;
	msg.pause_resume.stream_type = stream_type;
	msg.pause_resume.pause = pause;
	bool ok = send_control_msg(msg);
	sc_control_msg_destroy(&msg);
	return ok;
}

void scrcpy::sc_controller_on_ended(struct sc_controller *controller, bool error, void *userdata)
{
	(void)controller;
	(void)userdata;
	scrcpy_log(LOG_INFO, "srccpy controller ended (error=%d)", error);
}

bool scrcpy::request_device_info()
{
	device_info_mutex.lock();
	device_info_received = false;

	sc_control_msg msg;
	memset(&msg, 0, sizeof(msg));
	msg.type = SC_CONTROL_MSG_TYPE_GET_DEVICE_INFO;
	bool ok = send_control_msg(msg);
	sc_control_msg_destroy(&msg);

	if (ok) {
		sc_tick deadline = sc_tick_now() + SC_TICK_FROM_MS(1000);
		while (!device_info_received) {
			if (!sc_cond_timedwait(device_info_cond, device_info_mutex, deadline)) {
				// 超时
				break;
			}
		}
	}
	device_info_mutex.unlock();
	return ok && device_info_received;
}

void scrcpy::sc_controller_on_device_info(struct sc_controller *controller, const char *json, void *userdata)
{
	(void)controller;
	scrcpy *sc = static_cast<scrcpy *>(userdata);
	if (sc && json) {
		sc->parse_and_update_device_info(json);
	}
}

void scrcpy::parse_and_update_device_info(const std::string &json_str)
{
	try {
		auto j = nlohmann::json::parse(json_str);
		sc_adb_device_info info{};

		if (j.contains("serial") && j["serial"].is_string()) {
			info.device.serial = j["serial"].get<std::string>();
		}
		if (j.contains("state") && j["state"].is_string()) {
			info.device.state = get_device_state_from_string(j["state"].get<std::string>());
		}
		if (j.contains("device") && j["device"].is_string()) {
			info.device.model = j["device"].get<std::string>();
		}
		info.device.selected = false;

		info.best_name = info.device.model;

		if (j.contains("display") && j["display"].is_array()) {
			for (const auto &disp : j["display"]) {
				sc_adb_display d;
				if (disp.contains("id")) {
					if (disp["id"].is_number()) {
						d.id = disp["id"].get<int>();
					} else if (disp["id"].is_string()) {
						d.id = std::stoi(disp["id"].get<std::string>());
					}
				}
				if (disp.contains("size") && disp["size"].is_string()) {
					d.physical_size = disp["size"].get<std::string>();
				}
				if (disp.contains("fps")) {
					if (disp["fps"].is_number()) {
						d.fps = disp["fps"].get<int>();
					} else if (disp["fps"].is_string()) {
						d.fps = std::stoi(disp["fps"].get<std::string>());
					}
				}
				info.displays.emplace(d.id, std::move(d));
			}
		}

		info.has_external = false;
		if (j.contains("camera") && j["camera"].is_array()) {
			for (const auto &cam : j["camera"]) {
				sc_adb_camera c;
				if (cam.contains("id")) {
					if (cam["id"].is_number()) {
						c.id = std::to_string(cam["id"].get<int>());
					} else if (cam["id"].is_string()) {
						c.id = cam["id"].get<std::string>();
					}
				}
				if (cam.contains("facing") && cam["facing"].is_string()) {
					std::string facing = cam["facing"].get<std::string>();
					if (facing == "front") {
						c.facing = SC_CAMERA_FACING_FRONT;
					} else if (facing == "back") {
						c.facing = SC_CAMERA_FACING_BACK;
					} else if (facing == "external") {
						c.facing = SC_CAMERA_FACING_EXTERNAL;
						info.has_external = true;
					} else {
						c.facing = SC_CAMERA_FACING_ANY;
					}
				} else {
					c.facing = SC_CAMERA_FACING_ANY;
				}

				if (cam.contains("size") && cam["size"].is_array()) {
					for (const auto &sz : cam["size"]) {
						if (sz.is_string()) {
							c.suport_sizes.push_back(sz.get<std::string>());
						}
					}
				}

				if (cam.contains("fps") && cam["fps"].is_array()) {
					for (const auto &f : cam["fps"]) {
						if (f.is_number()) {
							c.suport_fps.push_back(f.get<int16_t>());
						}
					}
				}
				info.cameras.emplace(c.id, std::move(c));
			}
		}

		if (!info.device.serial.empty()) {
			std::lock_guard<sc_mutex> lock(device_info_mutex);
			this->device_infos[info.device.serial] = std::move(info);
			device_info_received = true;
			sc_cond_signal(device_info_cond);
			scrcpy_log(LOG_INFO, "Updated device info for serial: %s via control socket", info.device.serial.c_str());
		}
	} catch (const std::exception &e) {
		scrcpy_log(LOG_WARNING, "Failed to parse dynamically received device info JSON: %s", e.what());
	}
}

void scrcpy::sc_controller_on_error_message(struct sc_controller *controller, const char *error_msg, void *userdata)
{
	(void)controller;
	scrcpy *sc = static_cast<scrcpy *>(userdata);
	if (sc && error_msg) {
		sc->handle_error_message(error_msg);
	}
}

void scrcpy::handle_error_message(const std::string &error_msg)
{
	try {
		auto j = nlohmann::json::parse(error_msg);
		sc_error_type error_type = (sc_error_type)j.value("error_type", 0);
		std::string error_text = j.value("error_text", "");

		if (error_type == SC_ERROR_TYPE_DEVICE_DISCONNECTED) {
			if (error_text.find("USB debugging (Security Settings)") != std::string::npos) {
				this->usb_debug_enable = false;
			}
		}
		error("[scrcpy] Device control/capture error returned from server (type=%d): %s", error_type, error_text.c_str());

	} catch (const std::exception &) {
		error("[scrcpy] Device control/capture error returned from server (raw): %s", error_msg.c_str());
	}
}
