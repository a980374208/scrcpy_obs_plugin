#include "sc_process.h"
#include <assert.h>
#include "str_util.h"
#include "sc_thread.h"
#include "sc_log.h"

#define CMD_MAX_LEN 8192

static int run_observer(void *data)
{
	struct sc_process_observer *observer = (struct sc_process_observer *)data;
	sc_exit_code exit_code = sc_process_wait(observer->pid, false); 
	if (exit_code != 0) {
		scrcpy_log(LOG_WARNING, "Process exited with value %" SC_PRIexitcode, exit_code);
	} else {
		scrcpy_log(LOG_INFO, "Process exited with value 0");
	}
	{
		std::lock_guard<sc_mutex> lock(observer->mutex);
		observer->terminated = true;
		sc_cond_signal(observer->cond_terminated);
	}

	if (observer->listener) {
		observer->listener->on_terminated();
	}

	return 0;
}

// RAII wrapper for Windows HANDLE resources
class HandleGuard {
	HANDLE handle_;
	bool should_close_;

public:
	explicit HandleGuard(HANDLE h = NULL) : handle_(h), should_close_(true) {}
	~HandleGuard() { release(); }

	void release() {
		if (should_close_ && handle_ && handle_ != INVALID_HANDLE_VALUE) {
			CloseHandle(handle_);
		}
		handle_ = NULL;
		should_close_ = false;
	}

	void detach() { should_close_ = false; }
	HANDLE get() const { return handle_; }
	HANDLE* ptr() { return &handle_; }
	void reset(HANDLE h) {
		release();
		handle_ = h;
		should_close_ = true;
	}

	operator HANDLE() const { return handle_; }
	operator bool() const { return handle_ != NULL; }
};

// RAII wrapper for attribute list
class AttributeListGuard {
	LPPROC_THREAD_ATTRIBUTE_LIST list_;

public:
	AttributeListGuard() : list_(NULL) {}
	~AttributeListGuard() {
		if (list_) {
			DeleteProcThreadAttributeList(list_);
			free(list_);
		}
	}

	bool allocate(SIZE_T size) {
		list_ = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(size);
		return list_ != NULL;
	}

	LPPROC_THREAD_ATTRIBUTE_LIST get() const { return list_; }
	LPPROC_THREAD_ATTRIBUTE_LIST* ptr() { return &list_; }
};

sc_process_result sc_process_execute_p(const std::vector<std::string> commands, HANDLE *handle, unsigned flags,
				       HANDLE *pin, HANDLE *pout, HANDLE *perr)
{
	bool inherit_stdout = !pout && !(flags & SC_PROCESS_NO_STDOUT);
	bool inherit_stderr = !perr && !(flags & SC_PROCESS_NO_STDERR);

	// Add 1 per non-NULL pointer
	unsigned handle_count = !!pin || !!pout || !!perr;

	enum sc_process_result ret = SC_PROCESS_ERROR_GENERIC;

	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	HANDLE stdin_read_handle;
	HANDLE stdout_write_handle;
	HANDLE stderr_write_handle;
	std::string cmd = "";
	wchar_t *wide = NULL;
	LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList = NULL;
	BOOL bInheritHandles = false;
	DWORD dwCreationFlags = 0;
	dwCreationFlags |= CREATE_NO_WINDOW;
	BOOL ok = false;
	if (pin) {
		if (!CreatePipe(&stdin_read_handle, pin, &sa, 0)) {
			perror("pipe");
			return SC_PROCESS_ERROR_GENERIC;
		}
		if (!SetHandleInformation(*pin, HANDLE_FLAG_INHERIT, 0)) {
			//LOGE("SetHandleInformation stdin failed");
			goto error_close_stdin;
		}
	}
	if (pout) {
		if (!CreatePipe(pout, &stdout_write_handle, &sa, 0)) {
			perror("pipe");
			goto error_close_stdin;
		}
		if (!SetHandleInformation(*pout, HANDLE_FLAG_INHERIT, 0)) {
			//LOGE("SetHandleInformation stdout failed");
			goto error_close_stdout;
		}
	}
	if (perr) {
		if (!CreatePipe(perr, &stderr_write_handle, &sa, 0)) {
			perror("pipe");
			goto error_close_stdout;
		}
		if (!SetHandleInformation(*perr, HANDLE_FLAG_INHERIT, 0)) {
			//LOGE("SetHandleInformation stderr failed");
			goto error_close_stderr;
		}
	}

	STARTUPINFOEXW si;
	PROCESS_INFORMATION pi;
	memset(&si, 0, sizeof(si));
	si.StartupInfo.cb = sizeof(si);
	HANDLE handles[3];

	si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	if (inherit_stdout) {
		si.StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	}
	if (inherit_stderr) {
		si.StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	}

	
	if (handle_count) {
		unsigned i = 0;
		if (pin) {
			si.StartupInfo.hStdInput = stdin_read_handle;
			handles[i++] = si.StartupInfo.hStdInput;
		}
		if (pout) {
			assert(!inherit_stdout);
			si.StartupInfo.hStdOutput = stdout_write_handle;
			handles[i++] = si.StartupInfo.hStdOutput;
		}
		if (perr) {
			assert(!inherit_stderr);
			si.StartupInfo.hStdError = stderr_write_handle;
			handles[i++] = si.StartupInfo.hStdError;
		}

		SIZE_T size;
		// Call it once to know the required buffer size
		BOOL ok = InitializeProcThreadAttributeList(NULL, 1, 0, &size) ||
			  GetLastError() == ERROR_INSUFFICIENT_BUFFER;
		if (!ok) {
			goto error_close_stderr;
		}

		lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(size);
		if (!lpAttributeList) {
			//LOG_OOM();
			goto error_close_stderr;
		}

		ok = InitializeProcThreadAttributeList(lpAttributeList, 1, 0, &size);
		if (!ok) {
			free(lpAttributeList);
			goto error_close_stderr;
		}

		ok = UpdateProcThreadAttribute(lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
					       handle_count * sizeof(HANDLE), NULL, NULL);
		if (!ok) {
			goto error_free_attribute_list;
		}

		si.lpAttributeList = lpAttributeList;
	}

	if (!build_cmd(cmd, CMD_MAX_LEN, commands)) {
		//LOG_OOM();
		goto error_free_attribute_list;
	}

	wide = sc_str_to_wchars(cmd.c_str());
	if (!wide) {
		//LOG_OOM();
		goto error_free_attribute_list;
	}

	bInheritHandles = handle_count > 0 || inherit_stdout || inherit_stderr;
	
	if (handle_count > 0) {
		dwCreationFlags |= EXTENDED_STARTUPINFO_PRESENT;
	}
	if (!inherit_stdout && !inherit_stderr) {
		// DETACHED_PROCESS to disable stdin, stdout and stderr
		dwCreationFlags |= DETACHED_PROCESS;
	}
	ok = CreateProcessW(NULL, wide, NULL, NULL, bInheritHandles, dwCreationFlags, NULL, NULL, &si.StartupInfo,
				 &pi);
	free(wide);
	if (!ok) {
		int err = GetLastError();
		//LOGE("CreateProcessW() error %d", err);
		if (err == ERROR_FILE_NOT_FOUND) {
			ret = SC_PROCESS_ERROR_MISSING_BINARY;
		}
		goto error_free_attribute_list;
	}

	if (lpAttributeList) {
		DeleteProcThreadAttributeList(lpAttributeList);
		free(lpAttributeList);
	}

	CloseHandle(pi.hThread);

	// These handles are used by the child process, close them for this process
	if (pin) {
		CloseHandle(stdin_read_handle);
	}
	if (pout) {
		CloseHandle(stdout_write_handle);
	}
	if (perr) {
		CloseHandle(stderr_write_handle);
	}

	*handle = pi.hProcess;

	return SC_PROCESS_SUCCESS;

error_free_attribute_list:
	if (lpAttributeList) {
		DeleteProcThreadAttributeList(lpAttributeList);
		free(lpAttributeList);
	}
error_close_stderr:
	if (perr) {
		CloseHandle(*perr);
		CloseHandle(stderr_write_handle);
	}
error_close_stdout:
	if (pout) {
		CloseHandle(*pout);
		CloseHandle(stdout_write_handle);
	}
error_close_stdin:
	if (pin) {
		CloseHandle(*pin);
		CloseHandle(stdin_read_handle);
	}

	return ret;
}

bool build_cmd(std::string &cmd, size_t len, const std::vector<std::string> &commands)
{
	cmd = sc_str_join(commands, ' ');
	if (cmd.length() >= len) {
		//LOGE("Command too long (%" SC_PRIsizet " chars)", len - 1);
		return false;
	}
	return true;
}

ssize_t sc_pipe_read(HANDLE pipe, char *data, size_t len)
{
	DWORD r;
	if (!ReadFile(pipe, data, (DWORD)len, &r, NULL)) {
		return -1;
	}
	return r;
}

ssize_t sc_pipe_read_all(sc_pipe pipe, char *data, size_t len)
{
	size_t copied = 0;
	while (len > 0) {
		ssize_t r = sc_pipe_read(pipe, data, len);
		if (r <= 0) {
			return copied ? (ssize_t)copied : r;
		}
		len -= r;
		data += r;
		copied += r;
	}
	return copied;
}

bool sc_process_terminate(HANDLE handle)
{
	return TerminateProcess(handle, 1);
}

void sc_pipe_close(HANDLE pipe)
{
	if (!CloseHandle(pipe)) {
		//LOGW("Cannot close pipe");
	}
}

void sc_process_close(HANDLE handle)
{
	bool closed = CloseHandle(handle);
	assert(closed);
	(void)closed;
}

sc_exit_code sc_process_wait(HANDLE handle, bool close)
{
	DWORD code;
	if (WaitForSingleObject(handle, INFINITE) != WAIT_OBJECT_0 || !GetExitCodeProcess(handle, &code)) {
		// could not wait or retrieve the exit code
		code = SC_EXIT_CODE_NONE;
	}
	if (close) {
		CloseHandle(handle);
	}
	return code;
}

bool sc_process_observer_init(sc_process_observer &observer, sc_pid pid, const sc_process_listener &listener,
			      void *listener_userdata)
{
	// Either no listener, or on_terminated() is defined
	assert(listener.on_terminated);

	std::lock_guard<sc_mutex> lock(observer.mutex);

	sc_cond_init(observer.cond_terminated);
	observer.pid = pid;
	observer.listener = &listener;
	observer.listener_userdata = listener_userdata;
	observer.terminated = false;

	bool ok = sc_thread_create(observer.thread, run_observer, "scrcpy-proc", &observer);

	if (!ok) {
		return false;
	}

	return true;
}

void sc_process_observer_join(sc_process_observer &observer)
{
	sc_thread_join(observer.thread, NULL);
}

void sc_process_observer_destroy(sc_process_observer &observer)
{
	//Smart pointers manage object lifetimes automatically, eliminating the need for explicit deallocation.

	/*sc_cond_destroy(&observer->cond_terminated);
    sc_mutex_destroy(&observer->mutex);*/
}

bool sc_process_observer_timedwait(sc_process_observer &observer, sc_tick deadline)
{
	std::unique_lock<sc_mutex> lock(observer.mutex);

	while (!observer.terminated) {
		if (!sc_cond_timedwait(observer.cond_terminated, observer.mutex, deadline)) {
			return false; // timeout
		}
	}

	return true;
}
