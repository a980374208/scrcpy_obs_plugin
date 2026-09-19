#include "capture_session.h"

#include "codec/packet_sink.h"
#include "util/sc_log.h"

#include <algorithm>
#include <chrono>

class sc_capture_session_registry {
public:
    ~sc_capture_session_registry() { shutdown(); }

    bool adopt(const std::shared_ptr<sc_capture_session> &session)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_ || sessions_.size() >= 16)
            return false;
        try {
            if (!manager_.joinable())
                manager_ = std::thread(&sc_capture_session_registry::run, this);
            sessions_.push_back(session);
        } catch (const std::system_error &) {
            return false;
        } catch (const std::bad_alloc &) {
            return false;
        }
        cv_.notify_all();
        return true;
    }

    void notify() { cv_.notify_all(); }

    void shutdown()
    {
        std::vector<std::shared_ptr<sc_capture_session>> sessions;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!accepting_ && !manager_.joinable())
                return;
            accepting_ = false;
            sessions = sessions_;
        }
        for (const auto &session : sessions)
            session->request_retire(false);
        cv_.notify_all();
        if (manager_.joinable())
            manager_.join();
    }

private:
    void run()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            cv_.wait(lock, [&] {
                return (!accepting_ && sessions_.empty()) ||
                       std::any_of(sessions_.begin(), sessions_.end(),
                           [](const auto &session) { return session->retired(); });
            });
            std::vector<std::shared_ptr<sc_capture_session>> completed;
            auto it = sessions_.begin();
            while (it != sessions_.end()) {
                if ((*it)->retired()) {
                    completed.push_back(*it);
                    it = sessions_.erase(it);
                } else {
                    ++it;
                }
            }
            bool finished = !accepting_ && sessions_.empty();
            lock.unlock();
            for (const auto &session : completed)
                session->join_reaper();
            lock.lock();
            if (finished)
                break;
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::shared_ptr<sc_capture_session>> sessions_;
    std::thread manager_;
    bool accepting_ = true;
};

namespace {
sc_capture_session_registry &capture_registry()
{
    static sc_capture_session_registry registry;
    return registry;
}

bool await_connection(ServerConnectSignal &signal)
{
    auto future = signal.promise.get_future();
    return future.wait_for(std::chrono::seconds(30)) == std::future_status::ready && future.get();
}
} // namespace

sc_session_output::sc_session_output(obs_source_t *source)
{
    if (source)
        weak_source_ = obs_source_get_weak_source(source);
}

sc_session_output::~sc_session_output()
{
    if (weak_source_)
        obs_weak_source_release(weak_source_);
}

void sc_session_output::close() { accepting_.store(false, std::memory_order_release); }

void sc_session_output::output_video(const obs_source_frame &frame)
{
    width_.store(frame.width, std::memory_order_release);
    height_.store(frame.height, std::memory_order_release);
    if (!accepting_.load(std::memory_order_acquire) || !weak_source_)
        return;
    obs_source_t *source = obs_weak_source_get_source(weak_source_);
    if (!source)
        return;
    if (accepting_.load(std::memory_order_acquire))
        obs_source_output_video(source, &frame);
    obs_source_release(source);
}

void sc_session_output::output_audio(const obs_source_audio &audio)
{
    if (!accepting_.load(std::memory_order_acquire) || !weak_source_)
        return;
    obs_source_t *source = obs_weak_source_get_source(weak_source_);
    if (!source)
        return;
    if (accepting_.load(std::memory_order_acquire))
        obs_source_output_audio(source, &audio);
    obs_source_release(source);
}

uint32_t sc_session_output::width() const { return width_.load(std::memory_order_acquire); }
uint32_t sc_session_output::height() const { return height_.load(std::memory_order_acquire); }

sc_capture_session::sc_capture_session(obs_source_t *source,
                                       const sc_server_params &params,
                                       uint64_t generation)
    : generation_(generation), params_(params),
      output_(std::make_shared<sc_session_output>(source))
{
    lifecycle_generation_ = lifecycle_.begin_session();
}

std::shared_ptr<sc_capture_session> sc_capture_session::create(
    obs_source_t *source, const sc_server_params &params, uint64_t generation)
{
    auto session = std::shared_ptr<sc_capture_session>(
        new sc_capture_session(source, params, generation));
    if (!session->start_reaper())
        return nullptr;
    if (!capture_registry().adopt(session)) {
        session->request_retire(true);
        session->join_reaper();
        return nullptr;
    }
    return session;
}

sc_capture_session::~sc_capture_session()
{
    request_retire(false);
    join_reaper();
}

bool sc_capture_session::start_reaper()
{
    try {
        reaper_thread_ = std::thread(&sc_capture_session::run_reaper, this);
        return true;
    } catch (const std::system_error &) {
        retired_.store(true, std::memory_order_release);
        return false;
    }
}

bool sc_capture_session::start()
{
    {
        std::lock_guard<std::mutex> lock(retirement_mutex_);
        if (retire_requested_)
            return false;
        startup_in_progress_ = true;
    }
    struct StartupGuard {
        sc_capture_session *session;
        ~StartupGuard() { session->finish_startup(); }
    } guard{this};

    static const sc_server_callbacks server_callbacks{
        &sc_capture_session::on_server_connection_failed,
        &sc_capture_session::on_server_connected,
        &sc_capture_session::on_server_disconnected,
    };
    if (!server_.server_init(&server_callbacks, this)) {
        request_retire(true);
        return false;
    }
    server_.update_params(&params_);
    if (!server_.server_start()) {
        request_retire(true);
        return false;
    }
    if (!await_connection(server_.m_connect_signal) || cancellation_requested() || faulted()) {
        request_retire(true);
        return false;
    }

    auto video_callbacks = std::make_shared<sc_demuxer_callbacks>();
    video_callbacks->on_ended = &sc_capture_session::on_video_ended;
    video_demuxer_.init("video", server_.m_video_socket, video_callbacks, this);
    AVCodecID video_codec = params_.video_codec == SC_CODEC_H265 ? AV_CODEC_ID_HEVC
                            : params_.video_codec == SC_CODEC_AV1 ? AV_CODEC_ID_AV1
                                                                  : AV_CODEC_ID_H264;
    video_demuxer_.packet_source.clear_sinks();
    video_demuxer_.packet_source.add_sink(
        std::make_shared<sc_receive_packet_sink>(output_, video_codec));
    if (!video_demuxer_.start()) {
        request_retire(true);
        return false;
    }
    video_started_.store(true, std::memory_order_release);

    if (server_.m_audio_socket != SC_SOCKET_NONE) {
        auto audio_callbacks = std::make_shared<sc_demuxer_callbacks>();
        audio_callbacks->on_ended = &sc_capture_session::on_audio_ended;
        audio_demuxer_.init("audio", server_.m_audio_socket, audio_callbacks, this);
        AVCodecID audio_codec = params_.audio_codec == SC_CODEC_AAC ? AV_CODEC_ID_AAC
                              : params_.audio_codec == SC_CODEC_FLAC ? AV_CODEC_ID_FLAC
                              : params_.audio_codec == SC_CODEC_RAW ? AV_CODEC_ID_PCM_S16LE
                                                                    : AV_CODEC_ID_OPUS;
        audio_demuxer_.packet_source.clear_sinks();
        audio_demuxer_.packet_source.add_sink(
            std::make_shared<sc_receive_packet_sink>(output_, audio_codec));
        if (audio_demuxer_.start())
            audio_started_.store(true, std::memory_order_release);
        else
            audio_ended_.store(true, std::memory_order_release);
    } else {
        audio_ended_.store(true, std::memory_order_release);
    }

    if (params_.control) {
        static const sc_controller_callbacks controller_callbacks{
            &sc_capture_session::on_controller_ended,
            &sc_capture_session::on_device_info,
            &sc_capture_session::on_error_message,
        };
        if (!sc_controller_init(&controller_, server_.m_control_socket,
                                &controller_callbacks, this)) {
            request_retire(true);
            return false;
        }
        controller_initialized_.store(true, std::memory_order_release);
        sc_controller_configure(&controller_, nullptr, nullptr);
        if (!sc_controller_start(&controller_)) {
            request_retire(true);
            return false;
        }
        controller_started_.store(true, std::memory_order_release);
    }

    if (cancellation_requested() || !lifecycle_.mark_running(lifecycle_generation_)) {
        request_retire(true);
        return false;
    }
    return true;
}

void sc_capture_session::finish_startup()
{
    {
        std::lock_guard<std::mutex> lock(retirement_mutex_);
        startup_in_progress_ = false;
    }
    retirement_cv_.notify_all();
}

void sc_capture_session::request_retire(bool failed)
{
    output_->close();
    sc_controller_request_stop(&controller_);
    {
        std::lock_guard<std::mutex> lock(retirement_mutex_);
        if (failed)
            retire_failed_.store(true, std::memory_order_release);
        if (!retire_requested_) {
            retire_requested_ = true;
            lifecycle_.begin_stop();
        }
    }
    retirement_cv_.notify_all();
    message_cv_.notify_all();
}

void sc_capture_session::run_reaper()
{
    {
        std::unique_lock<std::mutex> lock(retirement_mutex_);
        retirement_cv_.wait(lock, [&] { return retire_requested_ && !startup_in_progress_; });
    }
    sc_controller_stop(&controller_);
    server_.server_stop();
    sc_controller_join(&controller_);
    video_demuxer_.join();
    audio_demuxer_.join();
    server_.close_sockets();
    video_started_.store(false, std::memory_order_release);
    audio_started_.store(false, std::memory_order_release);
    controller_started_.store(false, std::memory_order_release);
    controller_initialized_.store(false, std::memory_order_release);
    if (retire_failed_.load(std::memory_order_acquire))
        lifecycle_.finish_failed();
    else
        lifecycle_.finish_idle();
    retired_.store(true, std::memory_order_release);
    retirement_cv_.notify_all();
    capture_registry().notify();
}

void sc_capture_session::join_reaper()
{
    if (reaper_thread_.joinable() && reaper_thread_.get_id() != std::this_thread::get_id())
        reaper_thread_.join();
}

bool sc_capture_session::cancellation_requested() const
{
    std::lock_guard<std::mutex> lock(retirement_mutex_);
    return retire_requested_;
}

bool sc_capture_session::retired() const { return retired_.load(std::memory_order_acquire); }
bool sc_capture_session::wait_retired_for(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(retirement_mutex_);
    return retirement_cv_.wait_for(lock, timeout, [&] { return retired(); });
}
bool sc_capture_session::retired_failed() const
{
    return retire_failed_.load(std::memory_order_acquire);
}
bool sc_capture_session::healthy() const { return lifecycle_.healthy(); }
bool sc_capture_session::faulted() const { return lifecycle_.state() == sc_session_state::faulted; }
uint32_t sc_capture_session::faults() const { return lifecycle_.faults(); }
bool sc_capture_session::required_consumers_ready() const
{
    return video_started_.load(std::memory_order_acquire) &&
           (!params_.control || controller_started_.load(std::memory_order_acquire));
}
bool sc_capture_session::audio_receiver_ended() const
{
    return audio_ended_.load(std::memory_order_acquire);
}
uint64_t sc_capture_session::generation() const { return generation_; }
uint32_t sc_capture_session::width() const { return output_->width(); }
uint32_t sc_capture_session::height() const { return output_->height(); }

bool sc_capture_session::send_control_msg(const sc_control_msg &msg)
{
    return healthy() && sc_controller_push_msg(&controller_, &msg);
}

bool sc_capture_session::request_device_info(std::string &json)
{
    {
        std::lock_guard<std::mutex> lock(message_mutex_);
        device_info_message_.reset();
    }
    sc_control_msg msg{};
    msg.type = SC_CONTROL_MSG_TYPE_GET_DEVICE_INFO;
    if (!send_control_msg(msg))
        return false;
    std::unique_lock<std::mutex> lock(message_mutex_);
    if (!message_cv_.wait_for(lock, std::chrono::seconds(1), [&] {
            return device_info_message_.has_value() || cancellation_requested();
        }) || !device_info_message_)
        return false;
    json = std::move(*device_info_message_);
    device_info_message_.reset();
    return true;
}

std::vector<std::string> sc_capture_session::take_error_messages()
{
    std::lock_guard<std::mutex> lock(message_mutex_);
    std::vector<std::string> messages(error_messages_.begin(), error_messages_.end());
    error_messages_.clear();
    return messages;
}

void sc_capture_session::publish_fault(uint32_t fault)
{
    lifecycle_.report_fault(lifecycle_generation_, fault);
}

void sc_capture_session::on_server_connection_failed(sc_server &server, void *userdata)
{
    auto *session = static_cast<sc_capture_session *>(userdata);
    server.m_connect_signal.try_complete(false);
    if (session)
        session->publish_fault(SC_SESSION_FAULT_SERVER);
}

void sc_capture_session::on_server_connected(sc_server &server, void *)
{
    server.m_connect_signal.try_complete(true);
}

void sc_capture_session::on_server_disconnected(sc_server &server, void *userdata)
{
    auto *session = static_cast<sc_capture_session *>(userdata);
    server.m_connect_signal.try_complete(false);
    if (session)
        session->publish_fault(SC_SESSION_FAULT_SERVER);
    server.request_stop();
}

void sc_capture_session::on_video_ended(sc_demuxer *, sc_demuxer_status status, void *userdata)
{
    auto *session = static_cast<sc_capture_session *>(userdata);
    if (session)
        session->publish_fault(SC_SESSION_FAULT_VIDEO);
    scrcpy_log(LOG_INFO, "Video demuxer ended (status=%d)", status);
}

void sc_capture_session::on_audio_ended(sc_demuxer *, sc_demuxer_status status, void *userdata)
{
    auto *session = static_cast<sc_capture_session *>(userdata);
    if (session)
        session->audio_ended_.store(true, std::memory_order_release);
    scrcpy_log(LOG_INFO, "Audio demuxer ended (status=%d)", status);
}

void sc_capture_session::on_controller_ended(sc_controller *, bool error, void *userdata)
{
    auto *session = static_cast<sc_capture_session *>(userdata);
    if (session)
        session->publish_fault(SC_SESSION_FAULT_CONTROLLER);
    scrcpy_log(LOG_INFO, "srccpy controller ended (error=%d)", error);
}

void sc_capture_session::on_device_info(sc_controller *, const char *json, void *userdata)
{
    auto *session = static_cast<sc_capture_session *>(userdata);
    if (!session || !json || session->cancellation_requested())
        return;
    {
        std::lock_guard<std::mutex> lock(session->message_mutex_);
        session->device_info_message_ = json;
    }
    session->message_cv_.notify_all();
}

void sc_capture_session::on_error_message(sc_controller *, const char *message, void *userdata)
{
    auto *session = static_cast<sc_capture_session *>(userdata);
    if (!session || !message || session->cancellation_requested())
        return;
    std::lock_guard<std::mutex> lock(session->message_mutex_);
    if (session->error_messages_.size() == 8)
        session->error_messages_.pop_front();
    session->error_messages_.emplace_back(message);
}

sc_capture_owner::~sc_capture_owner() { destroy(); }

bool sc_capture_owner::attach(const std::shared_ptr<sc_capture_session> &session)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ || retiring_)
        return false;
    active_ = session;
    return true;
}

std::shared_ptr<sc_capture_session> sc_capture_owner::active() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

std::shared_ptr<sc_capture_session> sc_capture_owner::retiring() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return retiring_;
}

void sc_capture_owner::retire_active(bool failed)
{
    std::shared_ptr<sc_capture_session> session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_) {
            retiring_ = std::move(active_);
            session = retiring_;
        } else {
            session = retiring_;
        }
    }
    if (session)
        session->request_retire(failed);
}

bool sc_capture_owner::collect_retired(bool &failed)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!retiring_ || !retiring_->retired())
        return false;
    failed = retiring_->retired_failed();
    retiring_.reset();
    return true;
}

void sc_capture_owner::destroy()
{
    std::shared_ptr<sc_capture_session> active;
    std::shared_ptr<sc_capture_session> retiring;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active = std::move(active_);
        retiring = std::move(retiring_);
    }
    if (active)
        active->request_retire(false);
    if (retiring)
        retiring->request_retire(false);
}

void sc_shutdown_capture_sessions() { capture_registry().shutdown(); }
