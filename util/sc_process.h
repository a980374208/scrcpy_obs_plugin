#pragma once
#include <vector>
#include <string>
#include <functional>
#include <mutex>
#include "sc_thread.h"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#define SC_PRIexitcode "lu"
#define SC_PROCESS_NONE NULL
#define SC_EXIT_CODE_NONE ULONG_MAX // max value as unsigned long
typedef HANDLE sc_pid;
typedef DWORD sc_exit_code;
typedef HANDLE sc_pipe;
typedef HANDLE sc_pid;
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h>
#define SC_PRIexitcode "d"
#define SC_PROCESS_NONE -1
#define SC_EXIT_CODE_NONE -1
typedef pid_t sc_pid;
typedef int sc_exit_code;
typedef int sc_pipe;
#endif

struct sc_process_listener {
	std::function<void()> on_terminated;
};

/**
 * Tool to observe process termination
 *
 * To keep things simple and multiplatform, it runs a separate thread to wait
 * for process termination (without closing the process to avoid race
 * conditions).
 *
 * It allows a caller to block until the process is terminated (with a
 * timeout), and to be notified asynchronously from the observer thread.
 *
 * The process is not owned by the observer (the observer will never close it).
 */
struct sc_process_observer {
	sc_pid pid;

	sc_mutex mutex;
	sc_cond cond_terminated;
	bool terminated;

	sc_thread thread;
	const struct sc_process_listener *listener;
	void *listener_userdata;
};

enum sc_process_result {
	SC_PROCESS_SUCCESS,
	SC_PROCESS_ERROR_GENERIC,
	SC_PROCESS_ERROR_MISSING_BINARY,
};

#define SC_PROCESS_NO_STDOUT (1 << 0)
#define SC_PROCESS_NO_STDERR (1 << 1)

enum sc_process_result sc_process_execute_p(const std::vector<std::string> commands, HANDLE *handle, unsigned flags,
					    HANDLE *pin, HANDLE *pout, HANDLE *perr);

static bool build_cmd(std::string &cmd, size_t len, const std::vector<std::string> &commands);

ssize_t sc_pipe_read(HANDLE pipe, char *data, size_t len);

ssize_t sc_pipe_read_all(sc_pipe pipe, char *data, size_t len);

bool sc_process_terminate(HANDLE handle);

void sc_pipe_close(HANDLE pipe);

void sc_process_close(HANDLE handle);

sc_exit_code sc_process_wait(HANDLE handle, bool close);

bool sc_process_observer_init(sc_process_observer &observer, sc_pid pid, const struct sc_process_listener &listener,
			      void *listener_userdata);

void sc_process_observer_join(sc_process_observer &observer);

void sc_process_observer_destroy(sc_process_observer &observer);

bool sc_process_observer_timedwait(sc_process_observer &observer, sc_tick deadline);

class process_observer_raii {
public:
	process_observer_raii(sc_pid pid, const sc_process_listener &listener, void *userdata)
	{
		if (!sc_process_observer_init(observer_, pid, listener, userdata)) {
			throw std::runtime_error("observer init failed");
		}
		pid_ = pid;
		valid_ = true;
	}

	process_observer_raii(const process_observer_raii &) = delete;
	process_observer_raii &operator=(const process_observer_raii &) = delete;

	~process_observer_raii()
	{
		if (!valid_)
			return;
		{
			std::lock_guard<sc_mutex> lock(observer_.mutex);
			observer_.listener = nullptr;
		}
		sc_process_terminate(pid_);
		sc_process_observer_join(observer_);
		sc_process_observer_destroy(observer_);
	}

	bool timedwait(sc_tick deadline) { return sc_process_observer_timedwait(observer_, deadline); }

private:
	sc_process_observer observer_{};
	sc_pid pid_{SC_PROCESS_NONE};
	bool valid_{false};
};
