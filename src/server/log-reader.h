#pragma once
#include "util/sc_process.h"
#include "util/sc_log.h"
#include <algorithm>

// Single pipe reader. Cancellation wakes idle polling; join precedes handle close.
class server_log_reader_raii {
public:
	server_log_reader_raii(sc_pipe pout) : pout_(pout), thread_started_(false) {
		if (pout != SC_PROCESS_NONE) {
			thread_started_ = sc_thread_create(thread_, run_server_log, "scrcpy-log", this);
			if (!thread_started_) {
				error("Failed to create server log reader thread");
			}
		}
	}

	~server_log_reader_raii() {
		{
			std::lock_guard<std::mutex> lock(mutex_);
			stopped_ = true;
		}
		wake_.notify_one();
		if (thread_started_) {
			sc_thread_join(thread_, NULL);
		}
		if (pout_ != SC_PROCESS_NONE) sc_pipe_close(pout_);
	}

private:
	static int run_server_log(void *data) {
		auto *self = static_cast<server_log_reader_raii *>(data);
		sc_pipe pout = self->pout_;
		char buf[1024];
		std::string line_buffer;

		while (true) {
			{
				std::unique_lock<std::mutex> lock(self->mutex_);
				if (self->stopped_) break;
			}
			DWORD available = 0;
			if (!PeekNamedPipe(pout, nullptr, 0, nullptr, &available, nullptr)) break;
			if (!available) {
				std::unique_lock<std::mutex> lock(self->mutex_);
				self->wake_.wait_for(lock, std::chrono::milliseconds(25), [self] { return self->stopped_; });
				continue;
			}
			ssize_t r = sc_pipe_read(pout, buf, std::min<size_t>(available, sizeof(buf) - 1));
			if (r <= 0) {
				break;
			}
			buf[r] = '\0';
			line_buffer += buf;
			if (line_buffer.size() > 64 * 1024) line_buffer.erase(0, line_buffer.size() - 64 * 1024);

			size_t pos;
			while ((pos = line_buffer.find('\n')) != std::string::npos) {
				std::string line = line_buffer.substr(0, pos);
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				scrcpy_log(LOG_INFO, "[server] %s", line.c_str());
				line_buffer.erase(0, pos + 1);
			}
		}

		if (!line_buffer.empty()) {
			scrcpy_log(LOG_INFO, "[server] %s", line_buffer.c_str());
		}

		return 0;
	}

	std::mutex mutex_;
	std::condition_variable wake_;
	bool stopped_ = false;
	sc_pipe pout_{SC_PROCESS_NONE};
	sc_thread thread_{};
	bool thread_started_{false};
};

