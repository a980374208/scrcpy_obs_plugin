#include "session_update.h"

uint64_t sc_session_lifecycle::begin_session()
{
	uint64_t generation = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
	faults_.store(SC_SESSION_FAULT_NONE, std::memory_order_release);
	state_.store(sc_session_state::starting, std::memory_order_release);
	return generation;
}

bool sc_session_lifecycle::mark_running(uint64_t generation)
{
	if (generation_.load(std::memory_order_acquire) != generation)
		return false;

	sc_session_state expected = sc_session_state::starting;
	return state_.compare_exchange_strong(expected, sc_session_state::running,
					      std::memory_order_acq_rel);
}

bool sc_session_lifecycle::report_fault(uint64_t generation, uint32_t fault)
{
	if (generation_.load(std::memory_order_acquire) != generation)
		return false;

	sc_session_state state = state_.load(std::memory_order_acquire);
	while (state == sc_session_state::starting || state == sc_session_state::running) {
		faults_.fetch_or(fault, std::memory_order_acq_rel);
		if (state_.compare_exchange_weak(state, sc_session_state::faulted,
						 std::memory_order_acq_rel))
			return true;
	}

	if (state == sc_session_state::faulted) {
		faults_.fetch_or(fault, std::memory_order_acq_rel);
		return true;
	}

	return false;
}

void sc_session_lifecycle::begin_stop()
{
	state_.store(sc_session_state::stopping, std::memory_order_release);
}

void sc_session_lifecycle::finish_idle()
{
	faults_.store(SC_SESSION_FAULT_NONE, std::memory_order_release);
	state_.store(sc_session_state::idle, std::memory_order_release);
}

void sc_session_lifecycle::finish_failed()
{
	state_.store(sc_session_state::failed, std::memory_order_release);
}

uint64_t sc_session_lifecycle::generation() const
{
	return generation_.load(std::memory_order_acquire);
}

sc_session_state sc_session_lifecycle::state() const
{
	return state_.load(std::memory_order_acquire);
}

uint32_t sc_session_lifecycle::faults() const
{
	return faults_.load(std::memory_order_acquire);
}

bool sc_session_lifecycle::healthy() const
{
	return state() == sc_session_state::running;
}

bool sc_capture_config::has_target() const
{
	return tcpip ? !tcpip_dst.empty() : !serial.empty();
}

static bool sc_same_connection(const sc_capture_config &a, const sc_capture_config &b)
{
	if (a.tcpip != b.tcpip)
		return false;
	return a.tcpip ? a.tcpip_dst == b.tcpip_dst : a.serial == b.serial;
}

static bool sc_same_video(const sc_capture_config &a, const sc_capture_config &b)
{
	if (a.video_source != b.video_source || a.requested_fps != b.requested_fps)
		return false;
	if (a.video_source == SC_VIDEO_SOURCE_DISPLAY)
		return a.display_id == b.display_id && a.max_size == b.max_size;
	return a.camera_id == b.camera_id && a.width == b.width && a.height == b.height;
}

sc_update_plan sc_make_update_plan(const sc_capture_config &desired,
				   const std::optional<sc_capture_config> &submitted,
				   bool has_session, bool session_healthy,
				   bool required_consumers_ready,
				   bool audio_receiver_ended)
{
	sc_update_plan plan;
	if (!desired.has_target()) {
		plan.action = has_session ? sc_update_action::stop_to_idle : sc_update_action::no_op;
		return plan;
	}

	if (!submitted || !has_session) {
		plan.action = sc_update_action::start;
		return plan;
	}

	plan.connection_changed = !sc_same_connection(desired, *submitted);
	plan.video_changed = !sc_same_video(desired, *submitted);
	plan.audio_changed = desired.audio != submitted->audio ||
			     desired.audio_source != submitted->audio_source;

	if (!session_healthy || !required_consumers_ready || plan.connection_changed ||
	    (desired.audio && audio_receiver_ended)) {
		plan.action = sc_update_action::restart;
	} else if (plan.video_changed || plan.audio_changed) {
		plan.action = sc_update_action::dynamic_batch;
	}

	return plan;
}
