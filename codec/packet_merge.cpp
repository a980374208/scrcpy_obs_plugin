#include "packet_merge.h"

sc_packet_merger::sc_packet_merger() : config(nullptr) {}

sc_packet_merger::~sc_packet_merger()
{
	if (config) {
		free(config);
	}
}

bool sc_packet_merger::merge(AVPacket *packet)
{
	bool is_config = packet->pts == AV_NOPTS_VALUE;

	if (is_config) {
		free(this->config);

		this->config = (uint8_t *)malloc(packet->size);
		if (!this->config) {
			//LOG_OOM();
			return false;
		}

		memcpy(this->config, packet->data, packet->size);
		this->config_size = packet->size;
	} else if (this->config) {
		size_t config_size = this->config_size;
		size_t media_size = packet->size;

		if (av_grow_packet(packet, static_cast<int>(config_size))) {
			//LOG_OOM();
			return false;
		}

		memmove(packet->data + config_size, packet->data, media_size);
		memcpy(packet->data, this->config, config_size);

		free(this->config);
		this->config = NULL;
		// merger->size is meaningless when merger->config is NULL
	}

	return true;
}
