#include "packet_sink.h"
#include "../srccpy.hpp"
#include <iostream>
#include <cstring>
#include <media-io/video-io.h>
#include <util/threading.h>
#include <util/platform.h>

// Initialize static ops structure
const struct sc_packet_sink_ops sc_receive_packet_sink::s_ops = {&sc_receive_packet_sink::receive_init,
							      &sc_receive_packet_sink::receive_end,
							      &sc_receive_packet_sink::receive_push,
							      &sc_receive_packet_sink::receive_disable};

sc_receive_packet_sink::sc_receive_packet_sink(scrcpy *sc, obs_source_t *source, AVCodecID codec_id):
	  m_scrcpy(sc),
	  m_source(source),
	  m_codec_id(codec_id),
	  m_codec_ctx(nullptr),
	  m_frame(nullptr),
	  m_is_video(false),
	  m_packet_count(0)
{
	// Set the ops pointer to our static ops structure
	this->ops = &s_ops;
	m_frame = av_frame_alloc();
	if (!m_frame) {
		std::cerr << "Failed to allocate AVFrame" << std::endl;
	}
}

sc_receive_packet_sink::~sc_receive_packet_sink()
{
	if (m_frame) {
		av_frame_free(&m_frame);
	}
}

bool sc_receive_packet_sink::receive_init(std::shared_ptr<sc_packet_sink> sink, AVCodecContext *ctx)
{
	std::shared_ptr<sc_receive_packet_sink> file_sink = std::static_pointer_cast<sc_receive_packet_sink>(sink);

	file_sink->m_codec_ctx = ctx;
	file_sink->m_is_video = (ctx->codec_type == AVMEDIA_TYPE_VIDEO);

	std::cout << "Packet sink opened (codec: " << avcodec_get_name(file_sink->m_codec_id) << ")" << std::endl;
	return true;
}

void sc_receive_packet_sink::receive_end(std::shared_ptr<sc_packet_sink> sink)
{
	std::shared_ptr<sc_receive_packet_sink> file_sink = std::static_pointer_cast<sc_receive_packet_sink>(sink);
	file_sink->m_codec_ctx = nullptr;
}

bool sc_receive_packet_sink::receive_push(std::shared_ptr<sc_packet_sink> sink, const AVPacket *packet)
{
	std::shared_ptr<sc_receive_packet_sink> file_sink = std::static_pointer_cast<sc_receive_packet_sink>(sink);

	if (!file_sink->m_is_video || !file_sink->m_codec_ctx || !file_sink->m_frame) {
		return true; // Ignore if not video, or not fully initialized
	}

	int ret = avcodec_send_packet(file_sink->m_codec_ctx, packet);
	if (ret < 0) {
		std::cerr << "avcodec_send_packet failed: " << ret << std::endl;
		return false;
	}

	while (avcodec_receive_frame(file_sink->m_codec_ctx, file_sink->m_frame) == 0) {
		if (file_sink->m_scrcpy) {
			if (file_sink->m_scrcpy->width == 0 || file_sink->m_scrcpy->height == 0) {
				file_sink->m_scrcpy->width = file_sink->m_frame->width;
				file_sink->m_scrcpy->height = file_sink->m_frame->height;
			}
		}

		struct obs_source_frame obs_frame = {0};

		for (size_t i = 0; i < MAX_AV_PLANES; i++) {
			obs_frame.data[i] = file_sink->m_frame->data[i];
			obs_frame.linesize[i] = file_sink->m_frame->linesize[i];
		}

		obs_frame.width = file_sink->m_frame->width;
		obs_frame.height = file_sink->m_frame->height;

		obs_frame.timestamp = os_gettime_ns();
		/*if (file_sink->m_frame->pts != AV_NOPTS_VALUE) {
			obs_frame.timestamp = file_sink->m_frame->pts * 1000;
		} else {
			obs_frame.timestamp = os_gettime_ns();
		}*/

		enum video_format format = VIDEO_FORMAT_NONE;
		switch (file_sink->m_frame->format) {
		case AV_PIX_FMT_YUV420P:
			format = VIDEO_FORMAT_I420;
			break;
		case AV_PIX_FMT_NV12:
			format = VIDEO_FORMAT_NV12;
			break;
		case AV_PIX_FMT_YUYV422:
			format = VIDEO_FORMAT_YUY2;
			break;
		case AV_PIX_FMT_UYVY422:
			format = VIDEO_FORMAT_UYVY;
			break;
		case AV_PIX_FMT_YUV444P:
			format = VIDEO_FORMAT_I444;
			break;
		case AV_PIX_FMT_RGBA:
			format = VIDEO_FORMAT_RGBA;
			break;
		case AV_PIX_FMT_BGRA:
			format = VIDEO_FORMAT_BGRA;
			break;
		case AV_PIX_FMT_BGR0:
			format = VIDEO_FORMAT_BGRX;
			break;
		default:
			format = VIDEO_FORMAT_NONE;
			break;
		}
		obs_frame.format = format;

		if (format != VIDEO_FORMAT_NONE) {
			enum video_colorspace colorspace = VIDEO_CS_DEFAULT;
			switch (file_sink->m_frame->colorspace) {
			case AVCOL_SPC_BT709:
				colorspace = (file_sink->m_frame->color_trc == AVCOL_TRC_IEC61966_2_1) ? VIDEO_CS_SRGB : VIDEO_CS_709;
				break;
			case AVCOL_SPC_FCC:
			case AVCOL_SPC_BT470BG:
			case AVCOL_SPC_SMPTE170M:
			case AVCOL_SPC_SMPTE240M:
				colorspace = VIDEO_CS_601;
				break;
			case AVCOL_SPC_BT2020_NCL:
				colorspace = (file_sink->m_frame->color_trc == AVCOL_TRC_ARIB_STD_B67) ? VIDEO_CS_2100_HLG : VIDEO_CS_2100_PQ;
				break;
			default:
				colorspace = (file_sink->m_frame->color_primaries == AVCOL_PRI_BT2020)
								 ? ((file_sink->m_frame->color_trc == AVCOL_TRC_ARIB_STD_B67) ? VIDEO_CS_2100_HLG : VIDEO_CS_2100_PQ)
								 : VIDEO_CS_DEFAULT;
				break;
			}

			enum video_range_type range = (file_sink->m_frame->color_range == AVCOL_RANGE_JPEG) ? VIDEO_RANGE_FULL : VIDEO_RANGE_DEFAULT;
			obs_frame.full_range = (range == VIDEO_RANGE_FULL);

			video_format_get_parameters_for_format(
				colorspace,
				range,
				format,
				obs_frame.color_matrix,
				obs_frame.color_range_min,
				obs_frame.color_range_max
			);

			switch (file_sink->m_frame->color_trc) {
			case AVCOL_TRC_BT709:
			case AVCOL_TRC_GAMMA22:
			case AVCOL_TRC_GAMMA28:
			case AVCOL_TRC_SMPTE170M:
			case AVCOL_TRC_SMPTE240M:
			case AVCOL_TRC_IEC61966_2_1:
				obs_frame.trc = VIDEO_TRC_SRGB;
				break;
			case AVCOL_TRC_SMPTE2084:
				obs_frame.trc = VIDEO_TRC_PQ;
				break;
			case AVCOL_TRC_ARIB_STD_B67:
				obs_frame.trc = VIDEO_TRC_HLG;
				break;
			default:
				obs_frame.trc = VIDEO_TRC_DEFAULT;
				break;
			}

			obs_frame.flip = (file_sink->m_frame->linesize[0] < 0);
			obs_frame.flags = 0;

			obs_source_output_video(file_sink->m_source, &obs_frame);
		}

		av_frame_unref(file_sink->m_frame);
	}

	return true;
}

void sc_receive_packet_sink::receive_disable(std::shared_ptr<sc_packet_sink> sink)
{
	(void)sink;
	std::cout << "Packet sink disabled" << std::endl;
}
