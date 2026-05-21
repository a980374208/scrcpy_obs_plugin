#pragma once
#include "util/sc_intr.h"
#include <vector>
#include "util/sc_process.h"
#include "adb_device.h"

#define SC_ADB_NO_STDOUT (1 << 0)
#define SC_ADB_NO_STDERR (1 << 1)
#define SC_ADB_NO_LOGERR (1 << 2)

#define SC_ADB_SILENT (SC_ADB_NO_STDOUT | SC_ADB_NO_STDERR | SC_ADB_NO_LOGERR)


enum sc_adb_device_selector_type {
	SC_ADB_DEVICE_SELECT_ALL,
	SC_ADB_DEVICE_SELECT_SERIAL,
	SC_ADB_DEVICE_SELECT_USB,
	SC_ADB_DEVICE_SELECT_TCPIP,
};

struct sc_adb_device_selector {
	enum sc_adb_device_selector_type type;
	std::string serial;
};

bool sc_adb_init();
std::string sc_adb_get_executable(void);

sc_pid sc_adb_execute(const std::vector<std::string> &commands, unsigned flags);

sc_pid sc_adb_execute_p(const std::vector<std::string> &commands, unsigned flags, sc_pipe *pout);

bool sc_adb_start_server(sc_intr &intr, unsigned flags);

bool sc_adb_get_device_info(std::vector<char> &buf, sc_intr &intr, unsigned flags, size_t &r, const char *shell);

bool sc_adb_list_devices(sc_intr &intr, unsigned flags, sc_vec_adb_devices &out_vec);

//bool sc_adb_list_device_infos(sc_intr &intr, unsigned flags, sc_vec_adb_device_infos &out_vec);

bool sc_adb_select_device(class sc_intr &intr, const struct sc_adb_device_selector &selector, unsigned flags,
			  struct sc_adb_device &out_device);

bool process_check_success_intr(sc_intr &intr, sc_pid pid, const char *name, unsigned flags);

static bool process_check_success_internal(sc_pid pid, const char *name, bool close, unsigned flags);

static size_t sc_adb_devices_select(const sc_vec_adb_devices &devices, size_t len,
				    const struct sc_adb_device_selector &selector, size_t *idx_out);

static bool sc_adb_accept_device(const struct sc_adb_device &device, const struct sc_adb_device_selector &selector);

enum sc_adb_device_type sc_adb_device_get_type(const std::string &serial);

static bool sc_adb_device_check_state(struct sc_adb_device &device, sc_vec_adb_devices &devices);

bool sc_adb_push(sc_intr &intr, const std::string &serial, const std::string &local_path,
		 const std::string &remote_path, unsigned flags);

bool sc_adb_reverse(sc_intr &intr, const std::string &serial, const std::string &device_socket_name,
		    uint16_t local_port, unsigned flags);

bool sc_adb_reverse_remove(sc_intr &intr, const std::string &serial, const std::string &device_socket_name,
			   unsigned flags);

uint16_t sc_adb_get_device_sdk_version(sc_intr &intr, const std::string &serial);

bool sc_adb_forward_remove(sc_intr &intr, const std::string serial, uint16_t local_port, unsigned flags);

bool sc_adb_kill_server(sc_intr &intr, unsigned flags);
