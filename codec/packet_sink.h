#pragma once
extern "C" {
#include <libavcodec/avcodec.h>
}
#include <string>
#include <fstream>
#include <cstdint>

#include <obs.h>

struct sc_packet_sink_ops;
struct sc_packet_sink {
	const struct sc_packet_sink_ops *ops;
};

struct sc_packet_sink_ops {
	/* The codec context is valid until the sink is closed */
	bool (*open)(std::shared_ptr<sc_packet_sink>, AVCodecContext *ctx);
	void (*close)(std::shared_ptr<sc_packet_sink>);
	bool (*push)(std::shared_ptr<sc_packet_sink>, const AVPacket *packet);

	/*/
     * Called when the input stream has been disabled at runtime.
     *
     * If it is called, then open(), close() and push() will never be called.
     *
     * It is useful to notify the recorder that the requested audio stream has
     * finally been disabled because the device could not capture it.
     */
	void (*disable)(std::shared_ptr<sc_packet_sink>);
};

class scrcpy;

class sc_receive_packet_sink : public sc_packet_sink {
public:
	sc_receive_packet_sink(scrcpy *sc, obs_source_t *source, AVCodecID codec_id);
	~sc_receive_packet_sink();

private:
	static bool receive_init(std::shared_ptr<sc_packet_sink>, AVCodecContext *ctx);
	static void receive_end(std::shared_ptr<sc_packet_sink>);
	static bool receive_push(std::shared_ptr<sc_packet_sink>, const AVPacket *packet);
	static void receive_disable(std::shared_ptr<sc_packet_sink>);

	scrcpy *m_scrcpy;
	obs_source_t *m_source;
	AVCodecID m_codec_id;
	AVCodecContext *m_codec_ctx;
	AVFrame *m_frame;
	bool m_is_video;
	uint32_t m_packet_count;

	static const struct sc_packet_sink_ops s_ops;
};
