#pragma once
#include "stdint.h"
#include "server/server.hpp"
#include "codec/demuxer.h"
#include <memory>
#include <obs-module.h>
#include "util/options.h"


enum scrcpy_exit_code {
	// Normal program termination
	SCRCPY_EXIT_SUCCESS = 0,

	// No connection could be established
	SCRCPY_EXIT_FAILURE,

	// Device was disconnected while running
	SCRCPY_EXIT_DISCONNECTED,
};

class srccpy {
public:
	srccpy(obs_data_t *, obs_source_t *source_);
	srccpy(const srccpy &) = delete;
	~srccpy();
	int srccpy_init();

	static void sc_server_on_connection_failed(sc_server &server, void *userdata);

	static void sc_server_on_connected(sc_server &server, void *userdata);

	static void sc_server_on_disconnected(sc_server &server, void *userdata);

private:
	uint32_t generate_scid();

	bool video_demuxer_started = false;
	bool audio_demuxer_started = false;
	obs_source_t *source;


public:
	sc_server server;
	sc_demuxer video_demuxer;
	sc_demuxer audio_demuxer;
};
void register_srccpy();
