#include "process_intr.h"
#include "util/sc_intr.h"
#include <algorithm>

static DWORD deadline_timeout_ms(sc_tick deadline, DWORD maximum)
{
	if (!deadline) {
		return maximum;
	}

	sc_tick remaining = deadline - sc_tick_now();
	if (remaining <= 0) {
		return 0;
	}

	sc_tick milliseconds = (remaining + SC_TICK_FROM_MS(1) - 1) / SC_TICK_FROM_MS(1);
	return static_cast<DWORD>(std::min<sc_tick>(milliseconds, maximum));
}

ssize_t sc_pipe_read_all_intr(sc_intr &intr, sc_pid pid, sc_pipe pipe, char *data, size_t len)
{
	ssize_t count = -1;
	sc_process_intr_result result = sc_pipe_read_all_intr_until(intr, pid, pipe, data, len, 0, count);
	return result == SC_PROCESS_INTR_SUCCESS || count >= 0 ? count : -1;
}

sc_process_intr_result sc_pipe_read_all_intr_until(sc_intr &intr, sc_pid pid, sc_pipe pipe, char *data,
						    size_t len, sc_tick deadline, ssize_t &count)
{
	count = -1;
	if (!intr.set_process(pid)) {
		return SC_PROCESS_INTR_CANCELLED;
	}

	size_t copied = 0;
	bool process_exited = false;
	sc_process_intr_result result = SC_PROCESS_INTR_ERROR;
	while (len > 0) {
		if (intr.is_interrupted()) {
			if (copied) count = static_cast<ssize_t>(copied);
			result = SC_PROCESS_INTR_CANCELLED;
			break;
		}

		if (deadline && sc_tick_now() >= deadline) {
			if (copied) count = static_cast<ssize_t>(copied);
			intr.intr_interrupt();
			result = SC_PROCESS_INTR_TIMED_OUT;
			break;
		}

		DWORD available = 0;
		if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
			DWORD error = GetLastError();
			if (error == ERROR_BROKEN_PIPE) {
				count = static_cast<ssize_t>(copied);
				result = SC_PROCESS_INTR_SUCCESS;
			} else if (copied) {
				count = static_cast<ssize_t>(copied);
			}
			break;
		}

		if (available) {
			size_t amount = available < len ? available : len;
			ssize_t r = sc_pipe_read(pipe, data, amount);
			if (r <= 0) {
				if (r == 0 || copied) {
					count = static_cast<ssize_t>(copied);
					result = SC_PROCESS_INTR_SUCCESS;
				}
				break;
			}
			data += r;
			len -= static_cast<size_t>(r);
			copied += static_cast<size_t>(r);
			continue;
		}

		if (process_exited) {
			count = static_cast<ssize_t>(copied);
			result = SC_PROCESS_INTR_SUCCESS;
			break;
		}

		DWORD wait = WaitForSingleObject(pid, deadline_timeout_ms(deadline, 25));
		if (wait == WAIT_OBJECT_0) {
			// Peek once more to drain bytes flushed immediately before exit.
			process_exited = true;
		} else if (wait != WAIT_TIMEOUT) {
			if (copied) count = static_cast<ssize_t>(copied);
			break;
		}
	}
	if (!len) {
		count = static_cast<ssize_t>(copied);
		result = SC_PROCESS_INTR_SUCCESS;
	}

	intr.set_process(SC_PROCESS_NONE);
	return result;
}

sc_process_intr_result sc_process_wait_intr_until(sc_intr &intr, sc_pid pid, sc_tick deadline,
						   sc_exit_code &exit_code)
{
	exit_code = SC_EXIT_CODE_NONE;
	if (!intr.set_process(pid)) {
		return SC_PROCESS_INTR_CANCELLED;
	}

	DWORD wait = WaitForSingleObject(pid, deadline ? deadline_timeout_ms(deadline, INFINITE) : INFINITE);
	sc_process_intr_result result;
	if (wait == WAIT_OBJECT_0) {
		DWORD code;
		if (GetExitCodeProcess(pid, &code)) {
			exit_code = static_cast<sc_exit_code>(code);
			result = intr.is_interrupted() ? SC_PROCESS_INTR_CANCELLED : SC_PROCESS_INTR_SUCCESS;
		} else {
			result = SC_PROCESS_INTR_ERROR;
		}
	} else if (wait == WAIT_TIMEOUT) {
		intr.intr_interrupt();
		result = SC_PROCESS_INTR_TIMED_OUT;
	} else {
		result = SC_PROCESS_INTR_ERROR;
	}

	intr.set_process(SC_PROCESS_NONE);
	return result;
}
