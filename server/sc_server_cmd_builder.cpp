#include "sc_server_cmd_builder.hpp"
#include "server.hpp"

static const char *sc_server_get_codec_name(enum sc_codec codec)
{
	switch (codec) {
	case SC_CODEC_H264:
		return "h264";
	case SC_CODEC_H265:
		return "h265";
	case SC_CODEC_AV1:
		return "av1";
	case SC_CODEC_OPUS:
		return "opus";
	case SC_CODEC_AAC:
		return "aac";
	case SC_CODEC_FLAC:
		return "flac";
	case SC_CODEC_RAW:
		return "raw";
	default:
		assert(!"unexpected codec");
		return NULL;
	}
}

static const char *sc_server_get_camera_facing_name(enum sc_camera_facing camera_facing)
{
	switch (camera_facing) {
	case SC_CAMERA_FACING_FRONT:
		return "front";
	case SC_CAMERA_FACING_BACK:
		return "back";
	case SC_CAMERA_FACING_EXTERNAL:
		return "external";
	default:
		assert(!"unexpected camera facing");
		return NULL;
	}
}

static const char *sc_server_get_audio_source_name(enum sc_audio_source audio_source)
{
	switch (audio_source) {
	case SC_AUDIO_SOURCE_OUTPUT:
		return "output";
	case SC_AUDIO_SOURCE_MIC:
		return "mic";
	case SC_AUDIO_SOURCE_PLAYBACK:
		return "playback";
	case SC_AUDIO_SOURCE_MIC_UNPROCESSED:
		return "mic-unprocessed";
	case SC_AUDIO_SOURCE_MIC_CAMCORDER:
		return "mic-camcorder";
	case SC_AUDIO_SOURCE_MIC_VOICE_RECOGNITION:
		return "mic-voice-recognition";
	case SC_AUDIO_SOURCE_MIC_VOICE_COMMUNICATION:
		return "mic-voice-communication";
	case SC_AUDIO_SOURCE_VOICE_CALL:
		return "voice-call";
	case SC_AUDIO_SOURCE_VOICE_CALL_UPLINK:
		return "voice-call-uplink";
	case SC_AUDIO_SOURCE_VOICE_CALL_DOWNLINK:
		return "voice-call-downlink";
	case SC_AUDIO_SOURCE_VOICE_PERFORMANCE:
		return "voice-performance";
	default:
		assert(!"unexpected audio source");
		return NULL;
	}
}

static const char *sc_server_get_display_ime_policy_name(enum sc_display_ime_policy policy)
{
	switch (policy) {
	case SC_DISPLAY_IME_POLICY_LOCAL:
		return "local";
	case SC_DISPLAY_IME_POLICY_FALLBACK:
		return "fallback";
	case SC_DISPLAY_IME_POLICY_HIDE:
		return "hide";
	default:
		assert(!"unexpected display IME policy");
		return NULL;
	}
}

ScServerCmdBuilder::ScServerCmdBuilder(std::vector<std::string> &out) : cmd(out) {}

void ScServerCmdBuilder::add_bool(std::string_view key, bool value, bool default_value)
{
	if (value != default_value) {
		add(key, value ? "true" : "false");
	}
}

bool ScServerCmdBuilder::add_validated(std::string_view key, const char *value)
{
	if (!validate_string(value)) {
		//LOGE("Invalid value for %.*s", int(key.size()), key.data());
		return false;
	}
	add(key, value);
	return true;
}

void ScServerCmdBuilder::add_hex(std::string_view key, uint32_t value)
{
	std::ostringstream ss;
	ss << key << std::hex << value << std::dec;
	cmd.emplace_back(ss.str());
}

bool ScServerCmdBuilder::build_from_params(const sc_server_params &p, bool tunnel_forward)
{
	add_hex("scid=", p.scid);
	add("log_level=", log_level_to_server_string(p.log_level));

	add_bool("video=", p.video);
	add_bool("audio=", p.audio);
	add_bool("control=", p.control);

	if (p.video_bit_rate)
		add("video_bit_rate=", p.video_bit_rate);
	if (p.audio_bit_rate)
		add("audio_bit_rate=", p.audio_bit_rate);

	// 编解码器（非默认值才输出）
	if (p.video_codec != SC_CODEC_H264)
		add("video_codec=", sc_server_get_codec_name(p.video_codec));
	if (p.audio_codec != SC_CODEC_OPUS)
		add("audio_codec=", sc_server_get_codec_name(p.audio_codec));

	// 视频/音频源
	if (p.video_source != SC_VIDEO_SOURCE_DISPLAY)
		add("video_source=", "camera");
	// 如果音频启用，auto 源必须已解析
	if (p.audio_source != SC_AUDIO_SOURCE_OUTPUT && p.audio)
		add("audio_source=", sc_server_get_audio_source_name(p.audio_source));
	if (p.audio_dup)
		add("audio_dup=", true);

	if (p.max_fps && !add_validated("max_fps=", p.max_fps))
		return false;
	if (p.angle && !add_validated("angle=", p.angle))
		return false;

	// 屏幕方向捕获逻辑
	if (p.capture_orientation_lock != SC_ORIENTATION_UNLOCKED || p.capture_orientation != SC_ORIENTATION_0) {
		std::string orient_val;
		if (p.capture_orientation_lock == SC_ORIENTATION_LOCKED_INITIAL) {
			orient_val = "@";
		} else {
			const char *orient = sc_orientation_get_name(p.capture_orientation);
			bool locked = p.capture_orientation_lock != SC_ORIENTATION_UNLOCKED;
			if (locked)
				orient_val += "@";
			orient_val += orient;
		}
		if (!add_validated("capture_orientation=", orient_val.c_str()))
			return false;
	}

	if (p.crop && !add_validated("crop=", p.crop))
		return false;

	if (p.max_size)
		add("max_size=", p.max_size);

	if (p.display_id)
		add("display_id=", p.display_id);

	if (p.camera_id && !add_validated("camera_id=", p.camera_id))
		return false;
	if (p.camera_size && !add_validated("camera_size=", p.camera_size))
		return false;
	if (p.camera_facing != SC_CAMERA_FACING_ANY)
		add("camera_facing=", sc_server_get_camera_facing_name(p.camera_facing));
	if (p.camera_ar && !add_validated("camera_ar=", p.camera_ar))
		return false;

	if (p.camera_fps)
		add("camera_fps=", p.camera_fps);

	if (p.camera_high_speed)
		add("camera_high_speed=", "true");
	if (p.show_touches)
		add("show_touches=", "true");
	if (p.stay_awake)
		add("stay_awake=", "true");

	if (p.screen_off_timeout >= 0) {
		uint64_t ms = SC_TICK_TO_MS(p.screen_off_timeout);
		add("screen_off_timeout=", ms);
	}

	if (p.video_codec_options && !add_validated("video_codec_options=", p.video_codec_options)) {
		return false;
	}

	if (p.audio_codec_options && !add_validated("audio_codec_options=", p.audio_codec_options)) {
		return false;
	}

	if (p.video_encoder && !add_validated("video_encoder=", p.video_encoder)) {
		return false;
	}

	if (p.audio_encoder && !add_validated("audio_encoder=", p.audio_encoder)) {
		return false;
	}

	add_bool("clipboard_autosync=", p.clipboard_autosync);
	add_bool("downsize_on_error=", p.downsize_on_error);
	add_bool("cleanup=", p.cleanup);
	add_bool("power_on=", p.power_on);

	add_bool("power_off_on_close=", p.power_off_on_close);

	if (p.new_display && !add_validated("new_display=", p.new_display))
		return false;

	// 输入法策略
	if (p.display_ime_policy != SC_DISPLAY_IME_POLICY_UNDEFINED)
		add("display_ime_policy=", sc_server_get_display_ime_policy_name(p.display_ime_policy));

	// 虚拟显示相关
	add_bool("vd_destroy_content=", p.vd_destroy_content);
	add_bool("vd_system_decorations=", p.vd_system_decorations);

	if (tunnel_forward)
		add("tunnel_forward=", "true");

	if (p.list & SC_OPTION_LIST_ENCODERS)
		add("list_encoders=", "true");
	if (p.list & SC_OPTION_LIST_DISPLAYS)
		add("list_displays=", "true");
	if (p.list & SC_OPTION_LIST_CAMERAS)
		add("list_cameras=", "true");
	if (p.list & SC_OPTION_LIST_CAMERA_SIZES)
		add("list_camera_sizes=", "true");
	if (p.list & SC_OPTION_LIST_APPS)
		add("list_apps=", "true");

	return true;
}