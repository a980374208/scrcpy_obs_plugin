#include "srccpy.hpp"
#include "util/rand.h"
#include "server/server.hpp"
#include "util/options.h"
#include "codec/demuxer.h"
#include "codec/packet_sink.h"
#include <thread>
#include <obs-source.h>
#include "util/str_util.h"

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

scrcpy::scrcpy(obs_data_t *, obs_source_t *source_) : source(source_) {
	srccpy_init();
}

scrcpy::~scrcpy()
{
	if (video_demuxer_started) {
		this->video_demuxer.join();
	}
	if (audio_demuxer_started) {
		this->audio_demuxer.join();
	}
}

int scrcpy::srccpy_init()
{
	uint32_t scid = generate_scid();

	enum scrcpy_exit_code ret = SCRCPY_EXIT_FAILURE;

	static const struct sc_server_callbacks cbs = {&scrcpy::sc_server_on_connection_failed,
						       &scrcpy::sc_server_on_connected,
						       &scrcpy::sc_server_on_disconnected};
	params.scid = scid;
	params.req_serial = nullptr;
	params.log_level = SC_LOG_LEVEL_DEBUG;
	params.video_codec = SC_CODEC_H264;
	params.audio_codec = SC_CODEC_OPUS;
	params.video_source = SC_VIDEO_SOURCE_DISPLAY;
	params.audio_source = SC_AUDIO_SOURCE_MIC;
	params.camera_facing = SC_CAMERA_FACING_FRONT;
	params.crop = nullptr;
	params.video_codec_options = nullptr;
	params.audio_codec_options = nullptr;
	params.video_encoder = nullptr;
	params.audio_encoder = nullptr;
	params.camera_id = nullptr;
	params.camera_size = nullptr;
	params.camera_ar = nullptr;
	params.camera_fps = 0;
	params.port_range.first = 27183;
	params.port_range.last = 27199;
	params.tunnel_host = 0;
	params.tunnel_port = 0;
	params.max_size = 0;
	params.video_bit_rate = 0;
	params.audio_bit_rate = 0;
	params.max_fps = nullptr;
	params.angle = nullptr;
	params.screen_off_timeout = -1;
	params.capture_orientation = SC_ORIENTATION_0;
	params.capture_orientation_lock = SC_ORIENTATION_UNLOCKED;
	params.control = false;
	params.display_id = 0;
	params.new_display = nullptr;
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
	params.tcpip_dst = nullptr;
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

	return 0;
}

void scrcpy::update(obs_data_t *settings)
{
	if (settings) {
		std::string select_device,select_res;
		int choose_src, choose_camera, max_fps;
		select_device = obs_data_get_string(settings, "device_list");
		select_res = obs_data_get_string(settings, "choose_res");
		choose_src = (int)obs_data_get_int(settings, "choose_src");
		choose_camera = (int)obs_data_get_int(settings, "choose_camera");
		max_fps = (int)obs_data_get_int(settings, "max_fps");
		auto device = DecodeDevice(select_device);

		params.req_serial = device.device.serial.c_str();
		params.camera_facing = static_cast<enum sc_camera_facing>(choose_camera);
		params.video_source = static_cast<enum sc_video_source>(choose_src);
		static std::string fps_str;
		fps_str = std::to_string(max_fps);
		params.max_fps = fps_str.c_str();
		params.camera_size = select_res.c_str();
		int cx = 0, cy = 0;
		ResolutionValid(select_res, cx, cy);
		params.max_size = cx;
		
		server.update_params(&params);
	}
	server.server_start();
	bool connected = await_for_signal(server.m_connect_signal);
	if (!connected) {
		blog(LOG_ERROR, "Server connection failed");
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

	auto video_sink = std::make_shared<sc_receive_packet_sink>(this, this->source, codec_id);
	this->video_demuxer.packet_source.add_sink(video_sink);

	if (!this->video_demuxer.start()) {
		blog(LOG_ERROR, "Failed to start video demuxer");
	} else {
		video_demuxer_started = true;
	}
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
