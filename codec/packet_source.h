#pragma once
#include "packet_sink.h"
#include <mutex>
#include <vector>
#include <future>

extern "C" {
#include <libavcodec/avcodec.h>
}

#define SC_PACKET_SOURCE_MAX_SINKS 2

struct asio_packet_buffer {
	std::shared_ptr<std::vector<uint8_t>> storage;
};

static void asio_buffer_free(void *opaque, uint8_t *data)
{
	delete static_cast<asio_packet_buffer *>(opaque);
}

class sc_packet_source {
public:
	sc_packet_source();
	~sc_packet_source();

	void add_sink(std::shared_ptr<sc_packet_sink> sink);
	bool open_sinks(AVCodecContext *ctx);
	void close_sinks();
	bool push_packet(const AVPacket *packet);
	void disable_sinks();

	void close_firsts_sinks(unsigned count);

private:
	std::shared_ptr<sc_packet_sink> sinks[SC_PACKET_SOURCE_MAX_SINKS];
	unsigned sink_count;
};