#pragma once

#include "adb/adb_device.h"
#include "util/sc_intr.h"
#include "util/tick.h"
#include <string>
#include <unordered_set>

enum class sc_device_query_status {
	success,
	cancelled,
	timed_out,
	list_failed,
	server_missing,
	push_failed,
	probe_start_failed,
	probe_read_failed,
	probe_exit_failed,
	parse_failed,
};

struct sc_device_query_result {
	sc_device_query_status status = sc_device_query_status::list_failed;
	sc_vec_adb_device_infos device_infos;
	std::string failed_serial;
};

class sc_device_query {
public:
	explicit sc_device_query(sc_tick timeout);
	sc_device_query_result run();
	void cancel();

private:
	sc_intr intr_;
	sc_tick deadline_;
	std::unordered_set<std::string> pushed_serials_;
};

const char *sc_device_query_status_name(sc_device_query_status status);
