#pragma once

#include "util/options.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

enum class sc_session_state : uint8_t {
	idle,
	starting,
	running,
	faulted,
	stopping,
	failed,
};

enum sc_session_fault : uint32_t {
	SC_SESSION_FAULT_NONE = 0,
	SC_SESSION_FAULT_SERVER = 1U << 0,
	SC_SESSION_FAULT_VIDEO = 1U << 1,
	SC_SESSION_FAULT_CONTROLLER = 1U << 2,
};

class sc_session_lifecycle {
public:
	uint64_t begin_session();
	bool mark_running(uint64_t generation);
	bool report_fault(uint64_t generation, uint32_t fault);
	void begin_stop();
	void finish_idle();
	void finish_failed();

	uint64_t generation() const;
	sc_session_state state() const;
	uint32_t faults() const;
	bool healthy() const;

private:
	std::atomic<uint64_t> generation_{0};
	std::atomic<sc_session_state> state_{sc_session_state::idle};
	std::atomic<uint32_t> faults_{SC_SESSION_FAULT_NONE};
};

struct sc_capture_config {
	std::string serial;
	bool tcpip = false;
	std::string tcpip_dst;
	sc_video_source video_source = SC_VIDEO_SOURCE_DISPLAY;
	uint32_t display_id = 0;
	std::string camera_id;
	std::string resolution;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t max_size = 0;
	uint32_t requested_fps = 0;
	bool audio = false;
	sc_audio_source audio_source = SC_AUDIO_SOURCE_OUTPUT;

	bool has_target() const;
};

enum class sc_update_action : uint8_t {
	no_op,
	stop_to_idle,
	start,
	restart,
	dynamic_batch,
};

struct sc_update_plan {
	sc_update_action action = sc_update_action::no_op;
	bool connection_changed = false;
	bool video_changed = false;
	bool audio_changed = false;
};

sc_update_plan sc_make_update_plan(const sc_capture_config &desired,
				   const std::optional<sc_capture_config> &submitted,
				   bool has_session, bool session_healthy,
				   bool required_consumers_ready,
				   bool audio_receiver_ended);
