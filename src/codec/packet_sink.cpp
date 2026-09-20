#include "packet_sink.h"
#include "avsync_trace.h"
#include "../capture_session.h"
#include <cmath>
#include <iostream>
#include <cstring>
#include <utility>
#include <media-io/video-io.h>
#include <media-io/audio-io.h>
#include <util/threading.h>
#include <util/platform.h>
#include "sc_log.h"

static enum audio_format convert_ffmpeg_sample_format_to_obs(enum AVSampleFormat fmt)
{
	switch (fmt) {
	case AV_SAMPLE_FMT_U8:
		return AUDIO_FORMAT_U8BIT;
	case AV_SAMPLE_FMT_S16:
		return AUDIO_FORMAT_16BIT;
	case AV_SAMPLE_FMT_S32:
		return AUDIO_FORMAT_32BIT;
	case AV_SAMPLE_FMT_FLT:
		return AUDIO_FORMAT_FLOAT;
	case AV_SAMPLE_FMT_U8P:
		return AUDIO_FORMAT_U8BIT_PLANAR;
	case AV_SAMPLE_FMT_S16P:
		return AUDIO_FORMAT_16BIT_PLANAR;
	case AV_SAMPLE_FMT_S32P:
		return AUDIO_FORMAT_32BIT_PLANAR;
	case AV_SAMPLE_FMT_FLTP:
		return AUDIO_FORMAT_FLOAT_PLANAR;
	default:
		return AUDIO_FORMAT_UNKNOWN;
	}
}

static enum speaker_layout convert_ffmpeg_channels_to_obs(int channels)
{
	switch (channels) {
	case 1:
		return SPEAKERS_MONO;
	case 2:
		return SPEAKERS_STEREO;
	case 3:
		return SPEAKERS_2POINT1;
	case 4:
		return SPEAKERS_4POINT0;
	case 5:
		return SPEAKERS_4POINT1;
	case 6:
		return SPEAKERS_5POINT1;
	case 8:
		return SPEAKERS_7POINT1;
	default:
		return SPEAKERS_STEREO;
	}
}


// Initialize static ops structure
const struct sc_packet_sink_ops sc_receive_packet_sink::s_ops = {&sc_receive_packet_sink::receive_init,
							      &sc_receive_packet_sink::receive_end,
							      &sc_receive_packet_sink::receive_push,
							      &sc_receive_packet_sink::receive_disable};

sc_receive_packet_sink::sc_receive_packet_sink(std::shared_ptr<sc_session_output> output,
					       AVCodecID codec_id,
					       std::shared_ptr<sc_session_timing> timing,
					       std::shared_ptr<sc_avsync_trace> trace):
	  m_output(std::move(output)),
	  m_codec_id(codec_id),
	  m_codec_ctx(nullptr),
	  m_frame(nullptr),
	  m_is_video(false),
	  m_packet_count(0),
	  m_timing(std::move(timing)),
	  m_trace(std::move(trace))
{
	// Set the ops pointer to our static ops structure
	this->ops = &s_ops;
	m_frame = av_frame_alloc();
	if (!m_frame) {
		std::cerr << "Failed to allocate AVFrame" << std::endl;
	}
}

namespace {
void audio_levels(const AVFrame *frame, double &peak, double &rms)
{
	peak = 0.0;
	rms = 0.0;
	const int channels = frame->ch_layout.nb_channels;
	if (!frame->extended_data || channels <= 0 || frame->nb_samples <= 0)
		return;

	const auto format = static_cast<AVSampleFormat>(frame->format);
	const bool planar = av_sample_fmt_is_planar(format) != 0;
	const AVSampleFormat packed = planar ? av_get_packed_sample_fmt(format) : format;
	const int planes = planar ? channels : 1;
	const int values_per_plane = frame->nb_samples * (planar ? 1 : channels);
	double squares = 0.0;
	uint64_t count = 0;
	for (int plane = 0; plane < planes; ++plane) {
		const uint8_t *data = frame->extended_data[plane];
		if (!data)
			continue;
		for (int i = 0; i < values_per_plane; ++i) {
			double value = 0.0;
			switch (packed) {
			case AV_SAMPLE_FMT_U8:
				value = (double(data[i]) - 128.0) / 128.0;
				break;
			case AV_SAMPLE_FMT_S16:
				value = double(reinterpret_cast<const int16_t *>(data)[i]) / 32768.0;
				break;
			case AV_SAMPLE_FMT_S32:
				value = double(reinterpret_cast<const int32_t *>(data)[i]) / 2147483648.0;
				break;
			case AV_SAMPLE_FMT_FLT:
				value = reinterpret_cast<const float *>(data)[i];
				break;
			case AV_SAMPLE_FMT_DBL:
				value = reinterpret_cast<const double *>(data)[i];
				break;
			default:
				return;
			}
			const double magnitude = std::abs(value);
			peak = (std::max)(peak, magnitude);
			squares += value * value;
			++count;
		}
	}
	if (count)
		rms = std::sqrt(squares / double(count));
}
} // namespace

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
	if (file_sink->m_is_video)
		file_sink->m_timing->reset_video_stream();
	else
		file_sink->m_timing->reset_audio_stream();

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

	if (!file_sink->m_codec_ctx || !file_sink->m_frame) {
		return true; // Ignore if not fully initialized
	}

	if (packet->pts == AV_NOPTS_VALUE) {
		return true; // Ignore config packets
	}

	int ret = avcodec_send_packet(file_sink->m_codec_ctx, packet);
	if (ret < 0) {
		char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
		av_strerror(ret, err_buf, sizeof(err_buf));
		scrcpy_log(LOG_ERROR, "Failed to send packet to decoder: %s", err_buf);
		return false;
	}

	while (avcodec_receive_frame(file_sink->m_codec_ctx, file_sink->m_frame) == 0) {
		if (file_sink->m_is_video) {
			struct obs_source_frame obs_frame = {0};

			for (size_t i = 0; i < MAX_AV_PLANES; i++) {
				obs_frame.data[i] = file_sink->m_frame->data[i];
				obs_frame.linesize[i] = file_sink->m_frame->linesize[i];
			}

			obs_frame.width = file_sink->m_frame->width;
			obs_frame.height = file_sink->m_frame->height;

			const auto video_timestamp = file_sink->m_timing->video_to_obs(
				file_sink->m_frame->pts, os_gettime_ns());
			obs_frame.timestamp = video_timestamp.timestamp_ns;

			enum video_format format = VIDEO_FORMAT_NONE;
			switch (file_sink->m_frame->format) {
			case AV_PIX_FMT_YUV420P:
			case AV_PIX_FMT_YUVJ420P:
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

				if (file_sink->m_trace) {
					int center_luma = -1;
					if (file_sink->m_frame->data[0] && file_sink->m_frame->width > 0 &&
					    file_sink->m_frame->height > 0) {
						const int y = file_sink->m_frame->height / 2;
						const int x = file_sink->m_frame->width / 2;
						center_luma = file_sink->m_frame->data[0][
							y * file_sink->m_frame->linesize[0] + x];
					}
					file_sink->m_trace->decoded_submit(
						"video", file_sink->m_frame->pts, os_gettime_ns(),
						obs_frame.timestamp, 1, 0, 0.0, 0.0, center_luma);
				}
				if (video_timestamp.reset_obs_timeline)
					file_sink->m_output->reset_video_timing();
				file_sink->m_output->output_video(obs_frame);
			}
		} else {
			struct obs_source_audio obs_audio = {0};
			for (size_t i = 0; i < MAX_AV_PLANES; i++) {
				obs_audio.data[i] = file_sink->m_frame->data[i];
			}
			obs_audio.frames = file_sink->m_frame->nb_samples;
			obs_audio.format = convert_ffmpeg_sample_format_to_obs((enum AVSampleFormat)file_sink->m_frame->format);
			obs_audio.samples_per_sec = file_sink->m_frame->sample_rate;
			const uint64_t audio_arrival_ns = os_gettime_ns();
			obs_audio.timestamp = file_sink->m_timing->audio_to_obs(
				file_sink->m_frame->pts, audio_arrival_ns,
				file_sink->m_frame->nb_samples,
				file_sink->m_frame->sample_rate);

			int channels = file_sink->m_frame->ch_layout.nb_channels;

			obs_audio.speakers = convert_ffmpeg_channels_to_obs(channels);

			if (obs_audio.format != AUDIO_FORMAT_UNKNOWN) {
				if (file_sink->m_trace) {
					double peak = 0.0;
					double rms = 0.0;
					audio_levels(file_sink->m_frame, peak, rms);
					file_sink->m_trace->decoded_submit(
						"audio", file_sink->m_frame->pts, os_gettime_ns(),
						obs_audio.timestamp, obs_audio.frames,
						obs_audio.samples_per_sec, peak, rms, -1);
				}
				file_sink->m_output->output_audio(obs_audio);
			}
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
