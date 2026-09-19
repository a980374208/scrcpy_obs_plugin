#pragma once
#include "sc_intr.h"
#include "sc_process.h"
#include "tick.h"

#define BUFSIZE 65536

enum sc_process_intr_result {
	SC_PROCESS_INTR_SUCCESS,
	SC_PROCESS_INTR_CANCELLED,
	SC_PROCESS_INTR_TIMED_OUT,
	SC_PROCESS_INTR_ERROR,
};

ssize_t sc_pipe_read_all_intr(sc_intr &intr, sc_pid pid, sc_pipe pipe, char *data, size_t len);

sc_process_intr_result sc_pipe_read_all_intr_until(sc_intr &intr, sc_pid pid, sc_pipe pipe, char *data,
						    size_t len, sc_tick deadline, ssize_t &count);

sc_process_intr_result sc_process_wait_intr_until(sc_intr &intr, sc_pid pid, sc_tick deadline,
						   sc_exit_code &exit_code);
