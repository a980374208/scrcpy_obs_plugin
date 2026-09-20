#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

class sc_avsync_trace {
public:
	static std::shared_ptr<sc_avsync_trace> create_from_environment(uint64_t generation);

	void packet_received(const char *stream, int64_t device_pts_us,
			     uint64_t host_ns, size_t packet_bytes,
			     bool key_frame, bool config);
	void decoded_submit(const char *stream, int64_t device_pts_us,
			    uint64_t host_ns, uint64_t obs_timestamp_ns,
			    uint32_t sample_count, uint32_t sample_rate,
			    double signal_peak, double signal_rms,
			    int center_luma);

private:
	sc_avsync_trace(std::ofstream packet_file, std::ofstream submit_file);

	std::mutex mutex_;
	std::ofstream packet_file_;
	std::ofstream submit_file_;
};
