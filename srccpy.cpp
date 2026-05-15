#include "srccpy.hpp"
#include "util/rand.h"
#include "server/server.hpp"
#include "util/options.h"
#include "codec/demuxer.h"
#include "codec/packet_sink.h"
#include <thread>
#include <obs-source.h>

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

srccpy::srccpy(obs_data_t *, obs_source_t *source_) {
	srccpy_init();

}

srccpy::~srccpy()
{
	if (video_demuxer_started) {
		this->video_demuxer.join();
	}
	if (audio_demuxer_started) {
		this->audio_demuxer.join();
	}
}

int srccpy::srccpy_init()
{
	uint32_t scid = generate_scid();

	enum scrcpy_exit_code ret = SCRCPY_EXIT_FAILURE;

	static const struct sc_server_callbacks cbs = {&srccpy::sc_server_on_connection_failed,
						       &srccpy::sc_server_on_connected,
						       &srccpy::sc_server_on_disconnected};
	struct sc_server_params *pm = (struct sc_server_params *)malloc(sizeof(struct sc_server_params));
	memset(pm, 0, sizeof(pm));
	pm->scid = scid;
	pm->req_serial = nullptr;

	pm->log_level = SC_LOG_LEVEL_DEBUG;
	pm->video_codec = SC_CODEC_H264;
	pm->audio_codec = SC_CODEC_OPUS;
	pm->video_source = SC_VIDEO_SOURCE_DISPLAY;
	pm->audio_source = SC_AUDIO_SOURCE_MIC;
	pm->camera_facing = SC_CAMERA_FACING_FRONT;
	pm->crop = nullptr;
	pm->video_codec_options = nullptr;
	pm->audio_codec_options = nullptr;
	pm->video_encoder = nullptr;
	pm->audio_encoder = nullptr;
	pm->camera_id = nullptr;
	pm->camera_size = nullptr;
	pm->camera_ar = nullptr;
	pm->camera_fps = 0;
	pm->port_range.first = 27183;
	pm->port_range.last = 27199;
	pm->tunnel_host = 0;
	pm->tunnel_port = 0;
	pm->max_size = 0;
	pm->video_bit_rate = 0;
	pm->audio_bit_rate = 0;
	pm->max_fps = nullptr;
	pm->angle = nullptr;
	pm->screen_off_timeout = -1;
	pm->capture_orientation = SC_ORIENTATION_0;
	pm->capture_orientation_lock = SC_ORIENTATION_UNLOCKED;
	pm->control = false;
	pm->display_id = 0;
	pm->new_display = nullptr;
	pm->display_ime_policy = SC_DISPLAY_IME_POLICY_UNDEFINED;
	pm->video = true;
	pm->audio = true;
	pm->audio_dup = false;
	pm->show_touches = false;
	pm->stay_awake = false;
	pm->force_adb_forward = false;
	pm->power_off_on_close = false;
	pm->clipboard_autosync = true;
	pm->downsize_on_error = true;
	pm->tcpip = false;
	pm->tcpip_dst = nullptr;
	pm->select_usb = false;
	pm->cleanup = true;
	pm->power_on = true;
	pm->kill_adb_on_close = false;
	pm->camera_high_speed = false;
	pm->vd_destroy_content = true;
	pm->list = 0;

	if (!server.server_init(pm, &cbs, this)) {
		return SCRCPY_EXIT_FAILURE;
	}
	//if (!server.server_start()) {
	//	// goto end;
	//}

	//bool connected = await_for_signal(server.m_connect_signal);
	//if (!connected) {
	//	// LOGE("Server connection failed");
	//	// goto end;
	//}

	// 创建音视频流接收及解复用线程并初始化

	//if (options.video) {
	//	std::shared_ptr<sc_demuxer_callbacks> video_demuxer_cbs = std::make_shared<sc_demuxer_callbacks>();

	//	video_demuxer_cbs->on_ended = sc_video_demuxer_on_ended;
	//	this->video_demuxer.init("video", this->server.m_video_socket, video_demuxer_cbs, NULL);

	//	// 添加文件保存 sink（H.264 格式）
	//	auto file_sink = std::make_shared<sc_file_packet_sink>("video_stream", AV_CODEC_ID_H264);
	//	this->video_demuxer.packet_source.add_sink(file_sink);
	//}

	//if (options.audio) {
	//	std::shared_ptr<sc_demuxer_callbacks> audio_demuxer_cbs = std::make_shared<sc_demuxer_callbacks>();
	//	audio_demuxer_cbs->on_ended = sc_audio_demuxer_on_ended;

	//	this->audio_demuxer.init("audio", this->server.m_audio_socket, audio_demuxer_cbs, (void *)&options);

	//	// 添加文件保存 sink（Opus 格式）
	//	auto file_sink = std::make_shared<sc_file_packet_sink>("audio_stream", AV_CODEC_ID_OPUS);
	//	this->audio_demuxer.packet_source.add_sink(file_sink);
	//}

	/////**************************************解码逻辑待编写
	////**********************************************************************/

	////启动音视频流接收及解复用
	//if (options.video) {
	//	if (!this->video_demuxer.start()) {
	//		// goto end;
	//	}
	//	video_demuxer_started = true;
	//}

	//if (options.audio) {
	//	if (!this->audio_demuxer.start()) {
	//		// goto end;
	//	}
	//	audio_demuxer_started = true;
	//}

	return 0;
}

void srccpy::sc_server_on_connection_failed(sc_server &server, void *userdata)
{
	server.m_connect_signal.promise.set_value(false);
}

void srccpy::sc_server_on_connected(sc_server &server, void *userdata)
{
	server.m_connect_signal.promise.set_value(true);
}

void srccpy::sc_server_on_disconnected(sc_server &server, void *userdata)
{
	// LOGD("Server disconnected");
}

uint32_t srccpy::generate_scid()
{
	sc_rand rand;
	// Only use 31 bits to avoid issues with signed values on the Java-side
	return rand.sc_rand_u32() & 0x7FFFFFFF;
}
