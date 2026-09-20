#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>

struct sc_video_timestamp {
	uint64_t timestamp_ns;
	bool reset_obs_timeline;
};

// One instance per capture session. Both decoder threads use the same device
// clock mapping; per-stream state only handles media-specific continuity.
class sc_session_timing {
public:
	explicit sc_session_timing(uint32_t requested_fps = 0)
		: max_video_gap_ns_(requested_fps
			? 2500000000ULL / requested_fps
			: discontinuity_ns)
	{
	}

	void reset_video_stream()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		video_ = {};
	}

	void reset_audio_stream()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		audio_ = {};
	}

	sc_video_timestamp video_to_obs(int64_t pts_us, uint64_t arrival_ns)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const bool pts_valid = valid_pts(pts_us);
		const bool had_video = video_.valid;
		const bool forward = had_video && pts_valid && pts_us > video_.last_pts_us;
		const uint64_t pts_gap_ns = forward
			? uint64_t(pts_us - video_.last_pts_us) * 1000
			: 0;
		const uint64_t arrival_gap_ns = had_video && arrival_ns >= video_.last_arrival_ns
			? arrival_ns - video_.last_arrival_ns
			: 0;
		const bool sparse_gap = forward &&
			(pts_gap_ns > max_video_gap_ns_ || arrival_gap_ns > max_video_gap_ns_);
		const bool local_discontinuity = had_video &&
			(!pts_valid || !forward || arrival_ns < video_.last_arrival_ns);

		auto mapped = map_locked(pts_us, arrival_ns, true);
		if (mapped.epoch != video_.epoch) {
			video_.last_output_ns = 0;
			video_.headroom_ns = 0;
		}

		const bool continuous = had_video && !local_discontinuity && !sparse_gap &&
			mapped.epoch == video_.epoch;
		if (continuous) {
			video_.headroom_ns = (std::min)(
				video_.headroom_ns + uint64_t(5000000), max_video_headroom_ns);
		} else if (!had_video || local_discontinuity || mapped.reanchored) {
			video_.headroom_ns = 0;
		}

		uint64_t timestamp = add_saturated(mapped.timestamp_ns, video_.headroom_ns);
		if (!local_discontinuity && video_.last_output_ns && timestamp <= video_.last_output_ns)
			timestamp = video_.last_output_ns + 1;

		const bool reset_obs = had_video &&
			(sparse_gap || local_discontinuity || mapped.reanchored);
		video_.valid = pts_valid;
		video_.last_pts_us = pts_us;
		video_.last_arrival_ns = arrival_ns;
		video_.last_output_ns = timestamp;
		video_.epoch = mapped.epoch;
		return {timestamp, reset_obs};
	}

	uint64_t audio_to_obs(int64_t pts_us, uint64_t arrival_ns,
			      uint32_t samples, uint32_t sample_rate)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto mapped = map_locked(pts_us, arrival_ns, false);
		const uint64_t duration_ns = sample_rate
			? uint64_t(samples) * 1000000000ULL / sample_rate
			: 0;
		uint64_t timestamp = mapped.timestamp_ns;
		if (audio_.valid && mapped.epoch == audio_.epoch &&
		    arrival_ns >= audio_.last_arrival_ns &&
		    arrival_ns - audio_.last_arrival_ns <= discontinuity_ns) {
			const uint64_t expected = add_saturated(
				audio_.last_timestamp_ns, audio_.last_duration_ns);
			const uint64_t difference = distance(expected, timestamp);
			if (difference <= audio_smoothing_ns)
				timestamp = expected;
		}
		if (audio_.valid && mapped.epoch == audio_.epoch &&
		    timestamp <= audio_.last_timestamp_ns)
			timestamp = audio_.last_timestamp_ns + 1;

		audio_.valid = valid_pts(pts_us);
		audio_.last_arrival_ns = arrival_ns;
		audio_.last_timestamp_ns = timestamp;
		audio_.last_duration_ns = duration_ns;
		audio_.epoch = mapped.epoch;
		return timestamp;
	}

private:
	static constexpr uint64_t discontinuity_ns = 500000000;
	static constexpr uint64_t audio_smoothing_ns = 70000000;
	static constexpr uint64_t max_video_headroom_ns = 50000000;

	struct mapped_timestamp {
		uint64_t timestamp_ns;
		uint64_t epoch;
		bool reanchored;
	};

	struct video_state {
		bool valid = false;
		int64_t last_pts_us = 0;
		uint64_t last_arrival_ns = 0;
		uint64_t last_output_ns = 0;
		uint64_t headroom_ns = 0;
		uint64_t epoch = 0;
	};

	struct audio_state {
		bool valid = false;
		uint64_t last_arrival_ns = 0;
		uint64_t last_timestamp_ns = 0;
		uint64_t last_duration_ns = 0;
		uint64_t epoch = 0;
	};

	static bool valid_pts(int64_t pts_us)
	{
		return pts_us >= 0 &&
			pts_us <= (std::numeric_limits<int64_t>::max)() / 1000;
	}

	static uint64_t distance(uint64_t first, uint64_t second)
	{
		return first > second ? first - second : second - first;
	}

	static uint64_t add_saturated(uint64_t value, uint64_t amount)
	{
		return value > (std::numeric_limits<uint64_t>::max)() - amount
			? (std::numeric_limits<uint64_t>::max)()
			: value + amount;
	}

	void anchor_locked(int64_t pts_us, uint64_t host_ns)
	{
		anchor_pts_us_ = pts_us;
		anchor_host_ns_ = host_ns;
		anchor_valid_ = true;
		++epoch_;
	}

	uint64_t project_locked(int64_t pts_us) const
	{
		if (pts_us >= anchor_pts_us_)
			return add_saturated(anchor_host_ns_, uint64_t(pts_us - anchor_pts_us_) * 1000);
		const uint64_t backwards = uint64_t(anchor_pts_us_ - pts_us) * 1000;
		return backwards > anchor_host_ns_ ? 0 : anchor_host_ns_ - backwards;
	}

	mapped_timestamp map_locked(int64_t pts_us, uint64_t arrival_ns, bool follow_drift)
	{
		if (!valid_pts(pts_us))
			return {arrival_ns, epoch_, false};

		bool reanchored = false;
		if (!anchor_valid_) {
			anchor_locked(pts_us, arrival_ns);
			reanchored = true;
		}
		uint64_t timestamp = project_locked(pts_us);
		if (distance(timestamp, arrival_ns) > discontinuity_ns) {
			anchor_locked(pts_us, arrival_ns);
			timestamp = arrival_ns;
			reanchored = true;
		} else if (follow_drift) {
			const uint64_t magnitude = distance(timestamp, arrival_ns);
			const int64_t error = timestamp > arrival_ns
				? -int64_t(magnitude) : int64_t(magnitude);
			const int64_t correction = std::clamp(
				error / 64, int64_t(-100000), int64_t(100000));
			if (correction < 0) {
				const uint64_t amount = uint64_t(-correction);
				anchor_host_ns_ = amount > anchor_host_ns_ ? 0 : anchor_host_ns_ - amount;
			} else {
				anchor_host_ns_ = add_saturated(anchor_host_ns_, uint64_t(correction));
			}
			timestamp = project_locked(pts_us);
		}
		return {timestamp, epoch_, reanchored};
	}

	std::mutex mutex_;
	const uint64_t max_video_gap_ns_;
	bool anchor_valid_ = false;
	int64_t anchor_pts_us_ = 0;
	uint64_t anchor_host_ns_ = 0;
	uint64_t epoch_ = 0;
	video_state video_;
	audio_state audio_;
};
