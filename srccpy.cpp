#include "srccpy.hpp"
#include "util/rand.h"
#include "server/server.hpp"
#include "util/options.h"
#include "codec/demuxer.h"
#include "codec/packet_sink.h"
#include <thread>
#include <obs-source.h>
#include "util/str_util.h"
#include <util/process_intr.h>
#include <adb/adb.h>
#include <nlohmann/json.hpp>

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
	if (!server.push_server(server.m_intr, server.m_serial)) {
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

		params.req_serial = strdup(select_device.c_str());
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

void scrcpy::get_device_infos(sc_vec_adb_device_infos &device_infos, std::string &serial)
{
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
			blog(LOG_WARNING, "Failed to parse device info JSON: %s", e.what());
		}
	}


}

void scrcpy::update_device_infos(sc_vec_adb_device_infos &device_infos)
{
	this->device_infos = std::move(device_infos);
}

sc_vec_adb_device_infos scrcpy::get_device_infos() {
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
