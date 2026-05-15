#pragma once
#include "sc_intr.h"
#include "sc_process.h"
ssize_t sc_pipe_read_all_intr(sc_intr &intr, sc_pid pid, sc_pipe pipe, char *data, size_t len);