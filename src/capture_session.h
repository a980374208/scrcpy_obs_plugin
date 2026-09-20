#pragma once

#include "codec/demuxer.h"
#include "controller.h"
#include "server/server.hpp"
#include "session_update.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <obs.h>

class sc_session_output {
public:
    explicit sc_session_output(obs_source_t *source);
    ~sc_session_output();
    sc_session_output(const sc_session_output &) = delete;
    sc_session_output &operator=(const sc_session_output &) = delete;

    void close();
	void reset_video_timing();
    void output_video(const obs_source_frame &frame);
    void output_audio(const obs_source_audio &audio);
    uint32_t width() const;
    uint32_t height() const;

private:
    std::atomic<bool> accepting_{true};
    std::atomic<uint32_t> width_{0};
    std::atomic<uint32_t> height_{0};
    obs_weak_source_t *weak_source_ = nullptr;
};

class sc_avsync_trace;
class sc_session_timing;

class sc_capture_session : public std::enable_shared_from_this<sc_capture_session> {
public:
    static std::shared_ptr<sc_capture_session> create(obs_source_t *source,
                                                      const sc_server_params &params,
                                                      uint64_t generation);
#ifdef SC_TESTING
    static std::shared_ptr<sc_capture_session> create_control_fixture(
        sc_socket control_socket, uint64_t generation, bool ready = true);
#endif
    ~sc_capture_session();
    sc_capture_session(const sc_capture_session &) = delete;
    sc_capture_session &operator=(const sc_capture_session &) = delete;

    bool start();
    void request_retire(bool failed);
    bool retired() const;
    bool wait_retired_for(std::chrono::milliseconds timeout);
    bool retired_failed() const;
    bool healthy() const;
    bool faulted() const;
    uint32_t faults() const;
    bool required_consumers_ready() const;
    bool audio_receiver_ended() const;
    uint64_t generation() const;
    bool send_control_msg(const sc_control_msg &msg);
    bool request_device_info(std::string &json);
    std::vector<std::string> take_error_messages();
    uint32_t width() const;
    uint32_t height() const;

    // Deterministic owner/thread fixtures configure these resources directly.
    sc_server &server_for_test() { return server_; }
    sc_controller &controller_for_test() { return controller_; }
    sc_demuxer &video_for_test() { return video_demuxer_; }
    sc_demuxer &audio_for_test() { return audio_demuxer_; }
    void publish_fault(uint32_t fault);

private:
    sc_capture_session(obs_source_t *source, const sc_server_params &params,
                       uint64_t generation);
    bool start_reaper();
    void finish_startup();
    void run_reaper();
    void join_reaper();
    bool cancellation_requested() const;

    static void on_server_connection_failed(sc_server &server, void *userdata);
    static void on_server_connected(sc_server &server, void *userdata);
    static void on_server_disconnected(sc_server &server, void *userdata);
    static void on_video_ended(sc_demuxer *, sc_demuxer_status, void *userdata);
    static void on_audio_ended(sc_demuxer *, sc_demuxer_status, void *userdata);
    static void on_controller_ended(sc_controller *, bool error, void *userdata);
    static void on_device_info(sc_controller *, const char *json, void *userdata);
    static void on_error_message(sc_controller *, const char *message, void *userdata);

    friend class sc_capture_session_registry;

    const uint64_t generation_;
    sc_server_params params_;
    std::shared_ptr<sc_session_output> output_;
	std::shared_ptr<sc_session_timing> timing_;
	std::shared_ptr<sc_avsync_trace> avsync_trace_;
    sc_session_lifecycle lifecycle_;
    uint64_t lifecycle_generation_ = 0;
    sc_server server_;
    sc_demuxer video_demuxer_;
    sc_demuxer audio_demuxer_;
    sc_controller controller_;
    std::atomic<bool> video_started_{false};
    std::atomic<bool> audio_started_{false};
    std::atomic<bool> controller_initialized_{false};
    std::atomic<bool> controller_started_{false};
    std::atomic<bool> audio_ended_{false};

    mutable std::mutex message_mutex_;
    std::condition_variable message_cv_;
    std::optional<std::string> device_info_message_;
    std::deque<std::string> error_messages_;
    mutable std::mutex retirement_mutex_;
    std::condition_variable retirement_cv_;
    bool startup_in_progress_ = false;
    bool retire_requested_ = false;
    std::atomic<bool> retire_failed_{false};
    std::atomic<bool> retired_{false};
    std::thread reaper_thread_;
};

class sc_capture_owner {
public:
    ~sc_capture_owner();
    bool attach(const std::shared_ptr<sc_capture_session> &session);
    std::shared_ptr<sc_capture_session> active() const;
    std::shared_ptr<sc_capture_session> retiring() const;
    void retire_active(bool failed);
    bool collect_retired(bool &failed);
    void destroy();

private:
    mutable std::mutex mutex_;
    std::shared_ptr<sc_capture_session> active_;
    std::shared_ptr<sc_capture_session> retiring_;
};

void sc_shutdown_capture_sessions();

// Low-level synchronous cleanup remains for direct resource fixtures.
inline void sc_stop_capture(sc_server &server, sc_controller &controller,
                            sc_demuxer &video, sc_demuxer &audio)
{
    sc_controller_stop(&controller);
    server.server_stop();
    sc_controller_join(&controller);
    video.join();
    audio.join();
    server.close_sockets();
}
