#include "process_intr.h"
#include "util/sc_intr.h"

ssize_t sc_pipe_read_all_intr(sc_intr &intr, sc_pid pid, sc_pipe pipe, char *data, size_t len)
{
	if (!intr.set_process(pid)) {
		// Already interrupted
		return -1;
	}
	ssize_t ret = sc_pipe_read_all(pipe, data, len);

	intr.set_process(SC_PROCESS_NONE);

	return ret;
}
