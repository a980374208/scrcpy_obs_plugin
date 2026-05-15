#include "packet_sink.h"
#include <iostream>
#include <cstring>

// Initialize static ops structure
const struct sc_packet_sink_ops sc_file_packet_sink::s_ops = {&sc_file_packet_sink::file_open,
							      &sc_file_packet_sink::file_close,
							      &sc_file_packet_sink::file_push,
							      &sc_file_packet_sink::file_disable};

sc_file_packet_sink::sc_file_packet_sink(const std::string &filename, AVCodecID codec_id)
	: m_filename(filename),
	  m_codec_id(codec_id),
	  m_codec_ctx(nullptr),
	  m_is_video(false),
	  m_packet_count(0),
	  m_aac_profile(2),       // AAC-LC
	  m_sample_rate_index(4), // 48000 Hz
	  m_channels(2)           // Stereo
{
	// Set the ops pointer to our static ops structure
	this->ops = &s_ops;
}

sc_file_packet_sink::~sc_file_packet_sink()
{
	if (m_file.is_open()) {
		m_file.close();
	}
}

bool sc_file_packet_sink::file_open(std::shared_ptr<sc_packet_sink> sink, AVCodecContext *ctx)
{
	std::shared_ptr<sc_file_packet_sink> file_sink = std::static_pointer_cast<sc_file_packet_sink>(sink);

	file_sink->m_codec_ctx = ctx;
	file_sink->m_is_video = (ctx->codec_type == AVMEDIA_TYPE_VIDEO);

	// Determine file extension based on codec
	std::string actual_filename = file_sink->m_filename;
	if (actual_filename.find('.') == std::string::npos) {
		// Add appropriate extension if not present
		switch (file_sink->m_codec_id) {
		case AV_CODEC_ID_H264:
			actual_filename += ".h264";
			break;
		case AV_CODEC_ID_HEVC:
			actual_filename += ".h265";
			break;
		case AV_CODEC_ID_AAC:
			actual_filename += ".aac";
			break;
		case AV_CODEC_ID_OPUS:
			actual_filename += ".opus";
			break;
		default:
			actual_filename += ".bin";
			break;
		}
	}

	// Open file for writing in binary mode
	file_sink->m_file.open(actual_filename, std::ios::binary);
	if (!file_sink->m_file.is_open()) {
		std::cerr << "Failed to open file for writing: " << actual_filename << std::endl;
		return false;
	}

	// Initialize AAC parameters from codec context
	if (file_sink->m_codec_id == AV_CODEC_ID_AAC) {
		// Get sample rate index
		int sample_rate = ctx->sample_rate;
		switch (sample_rate) {
		case 96000:
			file_sink->m_sample_rate_index = 0;
			break;
		case 88200:
			file_sink->m_sample_rate_index = 1;
			break;
		case 64000:
			file_sink->m_sample_rate_index = 2;
			break;
		case 48000:
			file_sink->m_sample_rate_index = 3;
			break;
		case 44100:
			file_sink->m_sample_rate_index = 4;
			break;
		case 32000:
			file_sink->m_sample_rate_index = 5;
			break;
		case 24000:
			file_sink->m_sample_rate_index = 6;
			break;
		case 22050:
			file_sink->m_sample_rate_index = 7;
			break;
		case 16000:
			file_sink->m_sample_rate_index = 8;
			break;
		case 12000:
			file_sink->m_sample_rate_index = 9;
			break;
		case 11025:
			file_sink->m_sample_rate_index = 10;
			break;
		case 8000:
			file_sink->m_sample_rate_index = 11;
			break;
		default:
			file_sink->m_sample_rate_index = 4; // Default to 48000
			break;
		}

#ifdef SCRCPY_LAVU_HAS_CHLAYOUT
		file_sink->m_channels = ctx->ch_layout.nb_channels;
#else
		// For FFmpeg 59+, ch_layout is always available
		file_sink->m_channels = ctx->ch_layout.nb_channels;
#endif

		std::cout << "AAC parameters: sample_rate=" << sample_rate << ", channels=" << file_sink->m_channels
			  << ", profile=" << file_sink->m_aac_profile << std::endl;
	}

	std::cout << "File packet sink opened: " << actual_filename
		  << " (codec: " << avcodec_get_name(file_sink->m_codec_id) << ")" << std::endl;
	return true;
}

void sc_file_packet_sink::file_close(std::shared_ptr<sc_packet_sink> sink)
{
	std::shared_ptr<sc_file_packet_sink> file_sink = std::static_pointer_cast<sc_file_packet_sink>(sink);

	if (file_sink->m_file.is_open()) {
		file_sink->m_file.close();
		std::cout << "File packet sink closed. Total packets saved: " << file_sink->m_packet_count << std::endl;
	}

	file_sink->m_codec_ctx = nullptr;
}

bool sc_file_packet_sink::file_push(std::shared_ptr<sc_packet_sink> sink, const AVPacket *packet)
{
	std::shared_ptr<sc_file_packet_sink> file_sink = std::static_pointer_cast<sc_file_packet_sink>(sink);

	if (!file_sink->m_file.is_open()) {
		std::cerr << "File not open for writing" << std::endl;
		return false;
	}

	// Write based on codec type
	switch (file_sink->m_codec_id) {
	case AV_CODEC_ID_H264:
	case AV_CODEC_ID_HEVC:
		file_sink->write_h264_h265(packet);
		break;
	case AV_CODEC_ID_AAC:
		file_sink->write_aac(packet);
		break;
	default:
		// For other codecs, write raw data
		if (packet->size > 0 && packet->data) {
			file_sink->m_file.write(reinterpret_cast<const char *>(packet->data), packet->size);
		}
		break;
	}

	file_sink->m_packet_count++;

	// Flush every 100 packets to ensure data is written
	if (file_sink->m_packet_count % 100 == 0) {
		file_sink->m_file.flush();
	}

	return true;
}

void sc_file_packet_sink::write_h264_h265(const AVPacket *packet)
{
	// H.264/H.265 packets from scrcpy already have start codes (0x00000001)
	// Just write the raw packet data
	if (packet->size > 0 && packet->data) {
		m_file.write(reinterpret_cast<const char *>(packet->data), packet->size);
	}
}

void sc_file_packet_sink::write_aac(const AVPacket *packet)
{
	if (packet->size <= 0 || !packet->data) {
		return;
	}

	// AAC packets from scrcpy are raw AAC frames (without ADTS header)
	// We need to add ADTS header before each frame

	int frame_size = packet->size;
	int adts_header_size = 7; // ADTS header size
	int total_size = frame_size + adts_header_size;

	// Create ADTS header
	uint8_t adts_header[7];
	create_adts_header(adts_header, total_size, m_aac_profile, m_sample_rate_index, m_channels);

	// Write ADTS header
	m_file.write(reinterpret_cast<const char *>(adts_header), adts_header_size);

	// Write AAC frame data
	m_file.write(reinterpret_cast<const char *>(packet->data), frame_size);
}

void sc_file_packet_sink::create_adts_header(uint8_t *header, int aac_frame_length, int profile, int sample_rate_index,
					     int channels)
{
	// ADTS header structure (7 bytes, no CRC)
	// See ISO/IEC 13818-7 for details

	memset(header, 0, 7);

	// Syncword (12 bits): 0xFFF
	header[0] = 0xFF;
	header[1] = 0xF0;

	// ID (1 bit): 0 for MPEG-4
	// Layer (2 bits): 00
	// Protection absent (1 bit): 1 (no CRC)
	header[1] |= 0x00; // Already set above

	// Profile (2 bits): AAC profile - 1
	// Sample rate index (4 bits)
	// Private bit (1 bit): 0
	header[2] = ((profile - 1) & 0x03) << 6;
	header[2] |= (sample_rate_index & 0x0F) << 2;

	// Private bit (1 bit): 0
	// Channel configuration (3 bits)
	header[3] = (channels & 0x07) << 4;

	// Original/copy (1 bit): 0
	// Home (1 bit): 0
	// Copyrighted id bit (1 bit): 0
	// Copyrighted id start (1 bit): 0

	// Frame length (13 bits)
	header[3] |= ((aac_frame_length >> 11) & 0x03);
	header[4] = ((aac_frame_length >> 3) & 0xFF);
	header[5] = ((aac_frame_length & 0x07) << 5);

	// Buffer fullness (11 bits): 0x7FF for variable bitrate
	header[5] |= 0x1F;
	header[6] = 0xFC;

	// Number of AAC frames (RDBs) in ADTS frame minus 1 (2 bits): 00
	header[6] |= 0x00;
}

void sc_file_packet_sink::file_disable(std::shared_ptr<sc_packet_sink> sink)
{
	std::shared_ptr<sc_file_packet_sink> file_sink = std::static_pointer_cast<sc_file_packet_sink>(sink);
	std::cout << "File packet sink disabled" << std::endl;

	if (file_sink->m_file.is_open()) {
		file_sink->m_file.close();
	}
}
