#include "packet_source.h"
#include <cstring>
#include "iostream"
#include "assert.h"

sc_packet_source::sc_packet_source() : sink_count(0), sinks{} {}

sc_packet_source::~sc_packet_source()
{
	close_sinks();
}

void sc_packet_source::add_sink(std::shared_ptr<sc_packet_sink> sink)
{
	assert(this->sink_count < SC_PACKET_SOURCE_MAX_SINKS);
	assert(sink);
	assert(sink->ops);
	this->sinks[this->sink_count++] = sink;
}

bool sc_packet_source::open_sinks(AVCodecContext *ctx)
{
	for (unsigned i = 0; i < this->sink_count; ++i) {
		auto sink = this->sinks[i];
		if (!sink->ops->open(sink, ctx)) {
			close_firsts_sinks(i);
			return false;
		}
	}

	return true;
}

bool sc_packet_source::push_packet(const AVPacket *packet)
{
	assert(this->sink_count);
	for (unsigned i = 0; i < this->sink_count; ++i) {
		auto sink = this->sinks[i];
		if (!sink->ops->push(sink, packet)) {
			return false;
		}
	}

	return true;
}

void sc_packet_source::close_sinks()
{
	assert(this->sink_count);
	this->close_firsts_sinks(this->sink_count);
}

void sc_packet_source::disable_sinks()
{
	assert(this->sink_count);
	for (unsigned i = 0; i < sink_count; ++i) {
		if (sinks[i] && sinks[i]->ops && sinks[i]->ops->disable) {
			sinks[i]->ops->disable(sinks[i]);
		}
	}
}

void sc_packet_source::close_firsts_sinks(unsigned count)
{
	while (count) {
		auto sink = this->sinks[--count];
		sink->ops->close(sink);
	}
}
