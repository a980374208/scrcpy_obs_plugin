#pragma once
#include <functional>
extern "C" {
#include <libavcodec/avcodec.h>
}
#include "net.h"
#include "codec/packet_source.h"

#define SCRCPY_LAVU_HAS_CHLAYOUT //!!!!!!!!!!!!!!@jbb

enum class sc_codec_id : uint32_t {
	DISABLED = 0,
	CONFIG_ERROR = 1,
	H264 = 0x68323634,
	H265 = 0x68323635,
	AV1 = 0x00617631,
	AAC = 0x00616163,
	OPUS = 0x6f707573,
	FLAC = 0x666c6163,
	RAW = 0x00726177
};

enum sc_demuxer_status {
	SC_DEMUXER_STATUS_EOS,
	SC_DEMUXER_STATUS_DISABLED,
	SC_DEMUXER_STATUS_ERROR,
};

class sc_demuxer;
struct sc_demuxer_callbacks {
	void (*on_ended)(sc_demuxer *demuxer, enum sc_demuxer_status, void *userdata);
};

class sc_demuxer {
public:
	sc_demuxer();
	~sc_demuxer();

	void init(const char *name, sc_socket socket, std::shared_ptr<sc_demuxer_callbacks> callbacks,
		  void *cbs_userdata);

	bool start();

	void join();
	// Enable saving packets to file
	// bool enable_file_saving(const std::string &filename, bool save_with_header = true);

public:
	const char *m_name;
	sc_socket socket;
	sc_thread thread;
	std::shared_ptr<sc_demuxer_callbacks> cbs;
	sc_packet_source packet_source; // packet source trait
	void *cbs_userdata;
};
