#include "avsync_trace.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <system_error>

std::shared_ptr<sc_avsync_trace> sc_avsync_trace::create_from_environment(uint64_t generation)
{
	const char *directory = std::getenv("SRCCPY_AVSYNC_TRACE_DIR");
	if (!directory || !*directory)
		return nullptr;

	std::error_code error;
	const std::filesystem::path root(directory);
	std::filesystem::create_directories(root, error);
	if (error)
		return nullptr;

	static std::atomic<uint64_t> sequence{0};
	const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	const std::string prefix = "session-" + std::to_string(generation) + "-" +
		std::to_string(now) + "-" + std::to_string(sequence.fetch_add(1)) + "-";
	std::ofstream packet_file(root / (prefix + "packet-receive.csv"),
				  std::ios::out | std::ios::trunc);
	std::ofstream submit_file(root / (prefix + "decoded-submit.csv"),
				  std::ios::out | std::ios::trunc);
	if (!packet_file || !submit_file)
		return nullptr;

	packet_file << "stream,device_pts_us,received_host_ns,packet_bytes,key_frame,config\n";
	submit_file << "stream,device_pts_us,submit_host_ns,obs_timestamp_ns,sample_count,sample_rate,signal_peak,signal_rms,center_luma\n";
	return std::shared_ptr<sc_avsync_trace>(
		new sc_avsync_trace(std::move(packet_file), std::move(submit_file)));
}

sc_avsync_trace::sc_avsync_trace(std::ofstream packet_file,
				 std::ofstream submit_file)
	: packet_file_(std::move(packet_file)), submit_file_(std::move(submit_file))
{
	submit_file_ << std::setprecision(9);
}

void sc_avsync_trace::packet_received(const char *stream, int64_t device_pts_us,
				      uint64_t host_ns, size_t packet_bytes,
				      bool key_frame, bool config)
{
	std::lock_guard<std::mutex> lock(mutex_);
	packet_file_ << stream << ',' << device_pts_us << ',' << host_ns << ','
		     << packet_bytes << ',' << (key_frame ? 1 : 0) << ','
		     << (config ? 1 : 0) << '\n';
}

void sc_avsync_trace::decoded_submit(const char *stream, int64_t device_pts_us,
				     uint64_t host_ns, uint64_t obs_timestamp_ns,
				     uint32_t sample_count, uint32_t sample_rate,
				     double signal_peak, double signal_rms,
				     int center_luma)
{
	std::lock_guard<std::mutex> lock(mutex_);
	submit_file_ << stream << ',' << device_pts_us << ',' << host_ns << ','
		     << obs_timestamp_ns << ',' << sample_count << ',' << sample_rate
		     << ',' << signal_peak << ',' << signal_rms << ',' << center_luma
		     << '\n';
}
