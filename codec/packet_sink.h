#pragma once
extern "C" {
#include <libavcodec/avcodec.h>
}
#include <string>
#include <fstream>
#include <cstdint>

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

// File saver packet sink - saves packets in playable format
class sc_file_packet_sink : public sc_packet_sink {
public:
	sc_file_packet_sink(const std::string &filename, AVCodecID codec_id);
	~sc_file_packet_sink();

private:
	static bool file_open(std::shared_ptr<sc_packet_sink>, AVCodecContext *ctx);
	static void file_close(std::shared_ptr<sc_packet_sink>);
	static bool file_push(std::shared_ptr<sc_packet_sink>, const AVPacket *packet);
	static void file_disable(std::shared_ptr<sc_packet_sink>);

	// Helper functions for different formats
	void write_h264_h265(const AVPacket *packet);
	void write_aac(const AVPacket *packet);
	void create_adts_header(uint8_t *header, int aac_frame_length, int profile, int sample_rate_index,
				int channels);

	std::string m_filename;
	std::ofstream m_file;
	AVCodecID m_codec_id;
	AVCodecContext *m_codec_ctx;
	bool m_is_video;
	uint32_t m_packet_count;

	// AAC specific
	int m_aac_profile;
	int m_sample_rate_index;
	int m_channels;

	static const struct sc_packet_sink_ops s_ops;
};