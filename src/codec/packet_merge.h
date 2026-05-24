#pragma once
#include <cstdint>
#include <cstdlib>
extern "C" {
#include <libavcodec/avcodec.h>
}

class sc_packet_merger {
public:
	sc_packet_merger();
	~sc_packet_merger();

	bool merge(AVPacket *packet);

	uint8_t *config;
	size_t config_size;
};
