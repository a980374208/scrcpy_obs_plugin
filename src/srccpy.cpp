#include "srccpy.hpp"
#include "capture_session.h"
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
#include <charconv>
#include <limits>

#define INTERACTION_WARN_TITLE          obs_module_text("InteractionWarn")
#define INTERACTION_ERROR_TEXT          obs_module_text("InteractionWarnText")
//Inertaction failed,Make sure you have enabled USB debugging (Security Settings) and then rebooted your device.

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

scrcpy::scrcpy(obs_data_t *set, obs_source_t *source_)
	: source(source_)
{
	source_visible.store(source && obs_source_showing(source), std::memory_order_release);
	srccpy_init(set);
}

scrcpy::~scrcpy()
{
	capture_owner.destroy();
}

void scrcpy::stop_session(bool failed)
{
	capture_owner.retire_active(failed);
	usb_debug_enable.store(false, std::memory_order_release);
	submitted_config.reset();
}

int scrcpy::srccpy_init(obs_data_t *set)
{
	uint32_t scid = generate_scid();

	params.scid = scid;
	params.req_serial = "";
	params.log_level = SC_LOG_LEVEL_DEBUG;
	params.video_codec = SC_CODEC_H264;
	params.audio_codec = SC_CODEC_OPUS;
	params.video_source = SC_VIDEO_SOURCE_DISPLAY;
	params.audio_source = SC_AUDIO_SOURCE_AUTO;
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
	params.select_tcpip = false;
	params.cleanup = true;
	params.power_on = true;
	params.kill_adb_on_close = false;
	params.camera_high_speed = false;
	params.vd_destroy_content = true;
	params.vd_system_decorations = true;
	params.list = 0;

	update(set);
	return 0;
}

void scrcpy::update(obs_data_t *settings)
{
	consume_session_events(false);

	sc_capture_config desired;
	std::string parse_error;
	if (!parse_capture_config(settings, desired, parse_error)) {
		error("Invalid srccpy source settings: %s", parse_error.c_str());
		return;
	}
	user_audio_enabled.store(desired.audio, std::memory_order_release);
	user_audio_source.store(static_cast<uint8_t>(desired.audio_source),
				std::memory_order_release);

	if (capture_owner.retiring()) {
		apply_capture_config(desired);
		if (desired.has_target())
			pending_config = desired;
		else
			pending_config.reset();
		return;
	}

	auto session = capture_owner.active();
	bool has_session = static_cast<bool>(session);
	bool session_healthy = session && session->healthy();
	bool required_ready = session && session->required_consumers_ready();
	sc_update_plan plan = sc_make_update_plan(
		desired, submitted_config, has_session, session_healthy, required_ready,
		session && session->audio_receiver_ended());

	if (plan.action == sc_update_action::no_op) {
		if (!desired.has_target()) {
			apply_capture_config(desired);
			pending_config.reset();
		}
		return;
	}

	if (plan.action == sc_update_action::stop_to_idle) {
		apply_capture_config(desired);
		pending_config.reset();
		stop_session();
		scrcpy_log(LOG_INFO, "No capture device selected; waiting for source settings");
		return;
	}

	if (plan.action == sc_update_action::dynamic_batch) {
		if (execute_dynamic_update(desired, plan) && session && session->healthy()) {
			apply_capture_config(desired);
			submitted_config = desired;
			return;
		}
		error("Dynamic capture update failed; retiring the current session");
		pending_config.reset();
		stop_session(true);
		if (source)
			obs_source_output_video(source, nullptr);
		return;
	}

	if (has_session) {
		pending_config = desired;
		stop_session();
		return;
	}
	start_session(desired);
}

void scrcpy::video_tick()
{
	consume_session_events(true);
}

static bool parse_u32(const std::string &text, uint32_t &value)
{
	if (text.empty())
		return false;
	uint32_t parsed = 0;
	auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
	if (result.ec != std::errc() || result.ptr != text.data() + text.size())
		return false;
	value = parsed;
	return true;
}

static bool parse_resolution(const std::string &text, uint32_t &width, uint32_t &height)
{
	if (text.empty()) {
		width = height = 0;
		return true;
	}
	size_t separator = text.find('x');
	if (separator == std::string::npos || text.find('x', separator + 1) != std::string::npos)
		return false;
	std::string width_text = text.substr(0, separator);
	std::string height_text = text.substr(separator + 1);
	return parse_u32(width_text, width) && parse_u32(height_text, height) && width > 0 &&
	       height > 0;
}

bool scrcpy::parse_capture_config(obs_data_t *settings, sc_capture_config &config,
				  std::string &error_message)
{
	if (!settings)
		return true;

	config.serial = obs_data_get_string(settings, "device_list");
	config.resolution = obs_data_get_string(settings, "choose_res");
	config.camera_id = obs_data_get_string(settings, "choose_capture");
	config.video_source = static_cast<sc_video_source>(obs_data_get_int(settings, "choose_src"));
	if (config.video_source != SC_VIDEO_SOURCE_DISPLAY &&
	    config.video_source != SC_VIDEO_SOURCE_CAMERA) {
		error_message = "unsupported video source";
		return false;
	}

	if (!parse_resolution(config.resolution, config.width, config.height)) {
		error_message = "resolution must be empty or WIDTHxHEIGHT";
		return false;
	}
	if (config.width > std::numeric_limits<uint16_t>::max() ||
	    config.height > std::numeric_limits<uint16_t>::max()) {
		error_message = "resolution is outside the supported range";
		return false;
	}
	config.max_size = config.width > config.height ? config.width : config.height;

	int64_t fps = obs_data_get_int(settings, "choose_fps");
	if (fps < 0 || fps > std::numeric_limits<uint16_t>::max()) {
		error_message = "frame rate is outside the supported range";
		return false;
	}
	config.max_fps = static_cast<uint32_t>(fps);

	if (config.video_source == SC_VIDEO_SOURCE_DISPLAY) {
		if (config.camera_id.empty()) {
			config.display_id = 0;
		} else if (!parse_u32(config.camera_id, config.display_id)) {
			error_message = "display id must be an unsigned integer";
			return false;
		}
	}

	std::string pair_info = obs_data_get_string(settings, "pair_info");
	bool wifi_pair = obs_data_get_bool(settings, "wifi_pair");
	config.tcpip = wifi_pair && !pair_info.empty();
	config.tcpip_dst = config.tcpip ? pair_info : std::string();
	config.audio = obs_data_get_bool(settings, "audio_enable");
	config.audio_source = config.video_source == SC_VIDEO_SOURCE_DISPLAY ? SC_AUDIO_SOURCE_OUTPUT
									  : SC_AUDIO_SOURCE_MIC;

	auto infos = get_device_infos();
	auto info = infos.find(config.serial);
	if (info != infos.end() && info->second.device.state != DEVICE_STATE_DEVICE) {
		QWidget *parent_widget = static_cast<QWidget *>(obs_frontend_get_main_window());
		if (parent_widget) {
			QString title = QString::fromUtf8(WARN_TITLE);
			QString text = QString::fromUtf8(get_connect_state_error_message(info->second.device.state));
			QMetaObject::invokeMethod(parent_widget, [parent_widget, title, text]() {
				QMessageBox::warning(parent_widget, title, text);
			}, Qt::QueuedConnection);
		}
	}
	return true;
}

void scrcpy::apply_capture_config(const sc_capture_config &config)
{
	params.req_serial = config.serial;
	params.tcpip = config.tcpip;
	params.tcpip_dst = config.tcpip_dst;
	params.video_source = config.video_source;
	params.audio_source = config.audio_source;
	params.display_id = config.display_id;
	params.camera_id = config.camera_id;
	params.camera_size = config.resolution;
	params.camera_fps = static_cast<uint16_t>(config.max_fps);
	params.max_size = static_cast<uint16_t>(config.max_size);
	params.max_fps = std::to_string(config.max_fps);
	params.audio = config.audio;
}

bool scrcpy::start_session(const sc_capture_config &config)
{
	pending_config.reset();
	apply_capture_config(config);
	params.scid = generate_scid();
	sc_server_params session_params = make_session_params(config);
	auto session = sc_capture_session::create(source, session_params, ++next_generation);
	if (!session) {
		error("Could not reserve an asynchronous capture retirement slot");
		return false;
	}
	if (!capture_owner.attach(session)) {
		error("A previous capture session is still retiring");
		session->request_retire(true);
		return false;
	}
	if (!session->start()) {
		error("Capture session failed during startup");
		stop_session(true);
		return false;
	}
	usb_debug_enable.store(params.control, std::memory_order_release);
	if (!synchronize_visibility(session, config.audio, config.audio_source)) {
		handle_control_send_failure("Synchronizing source visibility after capture startup");
		return false;
	}
	submitted_config = config;
	return true;
}

sc_server_params scrcpy::make_session_params(const sc_capture_config &config) const
{
	sc_server_params session_params = params;
	session_params.audio = config.audio &&
		source_visible.load(std::memory_order_acquire);
	return session_params;
}

bool scrcpy::execute_dynamic_update(const sc_capture_config &desired,
				    const sc_update_plan &plan)
{
	auto session = capture_owner.active();
	if (!session || !session->healthy())
		return false;

	if (plan.video_changed) {
		sc_control_msg message{};
		message.type = SC_CONTROL_MSG_TYPE_SWITCH_VIDEO_SOURCE;
		message.switch_video_source.source =
			desired.video_source == SC_VIDEO_SOURCE_DISPLAY ? 0 : 1;
		if (desired.video_source == SC_VIDEO_SOURCE_DISPLAY) {
			message.switch_video_source.display_id = desired.display_id;
			message.switch_video_source.max_size = desired.max_size;
			message.switch_video_source.max_fps = static_cast<float>(desired.max_fps);
		} else {
			message.switch_video_source.camera_id = _strdup(desired.camera_id.c_str());
			if (!message.switch_video_source.camera_id)
				return false;
			message.switch_video_source.camera_width = desired.width;
			message.switch_video_source.camera_height = desired.height;
			message.switch_video_source.camera_fps = desired.max_fps;
		}
		bool sent = send_control_msg(message);
		sc_control_msg_destroy(&message);
		if (!sent)
			return false;
	}

	if (plan.video_changed || plan.audio_changed) {
		bool visible = source_visible.load(std::memory_order_acquire);
		puse_stream_type stream_type = !visible && plan.video_changed
			? PAUSE_AUDIO_VIDEO
			: PAUSE_AUDIO;
		if (!send_stream_paused(session, stream_type, !visible || !desired.audio,
					static_cast<uint8_t>(desired.audio_source)))
			return false;
	}

	return session->healthy();
}

void scrcpy::consume_session_events(bool start_pending)
{
	auto active = capture_owner.active();
	if (active) {
		last_width.store(active->width(), std::memory_order_release);
		last_height.store(active->height(), std::memory_order_release);
		for (const auto &message : active->take_error_messages())
			handle_error_message(message);
		if (active->faulted()) {
			uint32_t faults = active->faults();
			scrcpy_log(LOG_WARNING, "Retiring failed capture session (generation=%llu faults=0x%x)",
				   static_cast<unsigned long long>(active->generation()), faults);
			pending_config.reset();
			stop_session(true);
			if (source)
				obs_source_output_video(source, nullptr);
		}
	}

	auto retiring = capture_owner.retiring();
	if (retiring) {
		last_width.store(retiring->width(), std::memory_order_release);
		last_height.store(retiring->height(), std::memory_order_release);
		for (const auto &message : retiring->take_error_messages())
			handle_error_message(message);
	}
	bool failed = false;
	if (!capture_owner.collect_retired(failed))
		return;

	if (source && (failed || !pending_config || !pending_config->has_target()))
		obs_source_output_video(source, nullptr);
	if (start_pending && pending_config && pending_config->has_target()) {
		auto next = *pending_config;
		pending_config.reset();
		start_session(next);
	} else if (failed) {
		scrcpy_log(LOG_WARNING, "Capture session retirement completed after failure");
	}
}

sc_device_query_result scrcpy::refresh_device_infos(sc_tick timeout)
{
	sc_device_query query(timeout);
	sc_device_query_result result = query.run();
	if (result.status == sc_device_query_status::success) {
		update_device_infos(result.device_infos);
	} else {
		scrcpy_log(LOG_WARNING, "Device query failed: %s%s%s", sc_device_query_status_name(result.status),
			   result.failed_serial.empty() ? "" : " (serial=",
			   result.failed_serial.empty() ? "" : (result.failed_serial + ")").c_str());
	}
	return result;
}

void scrcpy::update_device_infos(sc_vec_adb_device_infos device_infos)
{
	std::lock_guard<sc_mutex> lock(device_info_mutex);
	this->device_infos = std::move(device_infos);
}

void scrcpy::on_interaction_focus(bool focus)
{
	if (focus && !this->usb_debug_enable.load(std::memory_order_acquire)) {
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
	auto session = capture_owner.active();
	return session && session->send_control_msg(msg);
}

#ifdef SC_TESTING
bool scrcpy::attach_session_fixture(
	const std::shared_ptr<sc_capture_session> &session,
	const sc_capture_config &config, bool synchronize)
{
	apply_capture_config(config);
	user_audio_enabled.store(config.audio, std::memory_order_release);
	user_audio_source.store(static_cast<uint8_t>(config.audio_source),
				std::memory_order_release);
	submitted_config = config;
	if (!capture_owner.attach(session))
		return false;
	return !synchronize || synchronize_visibility(session, config.audio,
						       config.audio_source);
}

bool scrcpy::session_audio_at_start_fixture(const sc_capture_config &config) const
{
	return make_session_params(config).audio;
}
#endif

uint32_t scrcpy::get_width() const
{
	auto session = capture_owner.active();
	return session ? session->width() : last_width.load(std::memory_order_acquire);
}

uint32_t scrcpy::get_height() const
{
	auto session = capture_owner.active();
	return session ? session->height() : last_height.load(std::memory_order_acquire);
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
	uint32_t width = get_width();
	uint32_t height = get_height();
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
	uint32_t width = get_width();
	uint32_t height = get_height();
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
	uint32_t width = get_width();
	uint32_t height = get_height();
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

bool scrcpy::set_stream_paused(puse_stream_type stream_type, bool pause, uint8_t audio_source)
{
	return send_stream_paused(capture_owner.active(), stream_type, pause, audio_source);
}

bool scrcpy::send_stream_paused(const std::shared_ptr<sc_capture_session> &session,
				puse_stream_type stream_type, bool pause,
				uint8_t audio_source)
{
	if (!session || !session->healthy())
		return false;
	sc_control_msg msg;
	memset(&msg, 0, sizeof(msg));
	msg.type = SC_CONTROL_MSG_TYPE_PAUSE_RESUME_STREAM;
	msg.pause_resume.stream_type = stream_type;
	msg.pause_resume.pause = pause;
	msg.pause_resume.audio_source = audio_source;
	bool ok = session->send_control_msg(msg);
	sc_control_msg_destroy(&msg);
	return ok;
}

bool scrcpy::synchronize_visibility(
	const std::shared_ptr<sc_capture_session> &session, bool audio_enabled,
	sc_audio_source audio_source)
{
	bool visible = source_visible.load(std::memory_order_acquire);
	puse_stream_type stream_type = visible && !audio_enabled
		? PAUSE_VIDEO
		: PAUSE_AUDIO_VIDEO;
	return send_stream_paused(session, stream_type, !visible,
				  static_cast<uint8_t>(audio_source));
}

void scrcpy::handle_control_send_failure(const char *operation)
{
	error("%s failed; retiring the current session", operation);
	pending_config.reset();
	stop_session(true);
	if (source)
		obs_source_output_video(source, nullptr);
}

void scrcpy::set_source_visible(bool visible)
{
	source_visible.store(visible, std::memory_order_release);
	auto session = capture_owner.active();
	if (!session || !session->healthy())
		return;
	bool audio_enabled = user_audio_enabled.load(std::memory_order_acquire);
	auto audio_source = static_cast<sc_audio_source>(
		user_audio_source.load(std::memory_order_acquire));
	if (!synchronize_visibility(session, audio_enabled, audio_source))
		handle_control_send_failure(visible ? "Showing source" : "Hiding source");
}

bool scrcpy::request_device_info()
{
	auto session = capture_owner.active();
	if (!session)
		return false;
	std::string json;
	if (!session->request_device_info(json))
		return false;
	parse_and_update_device_info(json, params.req_serial);
	return true;
}

void scrcpy::parse_and_update_device_info(const std::string &json_str,
					  const std::string &session_serial)
{
	try {
		auto j = nlohmann::json::parse(json_str);
		sc_adb_device_info info{};

		if (j.contains("serial") && j["serial"].is_string()) {
			if (session_serial.empty()) {
				info.device.serial = j["serial"].get<std::string>();
			} else {
				info.device.serial = session_serial;
			}
			
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
			scrcpy_log(LOG_INFO, "Updated device info for serial: %s via control socket", info.device.serial.c_str());
		}
	} catch (const std::exception &e) {
		scrcpy_log(LOG_WARNING, "Failed to parse dynamically received device info JSON: %s", e.what());
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
				this->usb_debug_enable.store(false, std::memory_order_release);
			}
		}
		error("[scrcpy] Device control/capture error returned from server (type=%d): %s", error_type, error_text.c_str());

	} catch (const std::exception &) {
		error("[scrcpy] Device control/capture error returned from server (raw): %s", error_msg.c_str());
	}
}
