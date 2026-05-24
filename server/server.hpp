#ifndef SC_SERVER_HPP
#define SC_SERVER_HPP

#include <stdint.h>
#include <stdbool.h>
#include <memory>
#include <string>
#include "util/sc_thread.h"
#include "util/sc_intr.h"
#include "adb/adb_tunnel.h"
#include "util/tick.h"
#include "event/sc_event.h"

#define SC_DEVICE_NAME_FIELD_LENGTH 64

class sc_server;

struct sc_server_callbacks {
	/**
     * Called when the server failed to connect
     *
     * If it is called, then on_connected() and on_disconnected() will never be
     * called.
     */
	void (*on_connection_failed)(sc_server &server, void *userdata);

	/**
     * Called on server connection
     */
	void (*on_connected)(sc_server &server, void *userdata);

	/**
     * Called on server disconnection (after it has been connected)
     */
	void (*on_disconnected)(sc_server &server, void *userdata);
};

struct sc_server_info {
	char device_name[SC_DEVICE_NAME_FIELD_LENGTH];
};

struct sc_server_params {
	uint32_t scid;
	std::string req_serial;
	enum sc_log_level log_level;
	enum sc_codec video_codec;
	enum sc_codec audio_codec;
	enum sc_video_source video_source;
	enum sc_audio_source audio_source;
	enum sc_camera_facing camera_facing;
	std::string crop;
	std::string video_codec_options;
	std::string audio_codec_options;
	std::string video_encoder;
	std::string audio_encoder;
	std::string camera_id;
	std::string camera_size;
	std::string camera_ar;
	uint16_t camera_fps;
	struct sc_port_range port_range;
	uint32_t tunnel_host;
	uint16_t tunnel_port;
	uint16_t max_size;
	uint32_t video_bit_rate;
	uint32_t audio_bit_rate;
	std::string max_fps; // float to be parsed by the server
	std::string angle;   // float to be parsed by the server
	sc_tick screen_off_timeout;
	enum sc_orientation capture_orientation;
	enum sc_orientation_lock capture_orientation_lock;
	bool control;
	uint32_t display_id;
	std::string new_display;
	enum sc_display_ime_policy display_ime_policy;
	bool video;
	bool audio;
	bool audio_dup;
	bool show_touches;
	bool stay_awake;
	bool force_adb_forward;
	bool power_off_on_close;
	bool clipboard_autosync;
	bool downsize_on_error;
	bool tcpip;
	std::string tcpip_dst;
	bool select_usb;
	bool select_tcpip;
	bool cleanup;
	bool power_on;
	bool kill_adb_on_close;
	bool camera_high_speed;
	bool vd_destroy_content;
	bool vd_system_decorations;
	uint8_t list;
};

class sc_server {
public:
	sc_server(const sc_server &) = delete;
	sc_server &operator=(const sc_server &) = delete;

	sc_server(sc_server &&other) noexcept;
	sc_server &operator=(sc_server &&other) noexcept;

	explicit sc_server();

	~sc_server();

	bool server_init(const struct sc_server_callbacks *cbs,
			 void *cbs_userdata);

	void update_params(const sc_server_params *params);

	bool server_start();

	void server_stop();

	bool push_server(sc_intr &intr, const std::string &serial);

	static std::string get_server_path();

	sc_pid execute_server(const struct sc_server_params &params, sc_pipe *pout);

	bool sc_server_connect_to(struct sc_server_info &info);

	sc_socket connect_to_server(unsigned attempts, sc_tick delay, uint32_t host, uint16_t port);

	bool sc_server_sleep(sc_tick deadline);

	void sc_server_kill_adb_if_requested();

public:
	sc_intr m_intr;
	const struct sc_server_callbacks *m_cbs;
	void *m_cbs_userdata;
	sc_process_listener m_listener;
	ServerConnectSignal m_connect_signal;

	struct sc_server_params m_params;
	std::string m_serial;
	std::string m_device_socket_name;

	sc_thread m_thread;
	struct sc_server_info m_info; // initialized once connected

	sc_mutex m_mutex;
	sc_cond m_cond_stopped;
	bool m_stopped;
	sc_adb_tunnel m_tunnel;
	sc_socket m_video_socket;
	sc_socket m_audio_socket;
	sc_socket m_control_socket;
	std::vector<std::string> pushed_serials;

private:
	static int run_server(void *);

	bool sc_server_configure_tcpip_unknown_address(const std::string &serial);
	bool sc_server_configure_tcpip_known_address(const std::string &addr, bool disconnect);
	bool sc_server_connect_to_tcpip(const std::string &ip_port, bool disconnect);
	std::string sc_server_switch_to_tcpip(const std::string &serial);
	uint16_t get_adb_tcp_port(const std::string &serial);
	bool wait_tcpip_mode_enabled(const std::string &serial, uint16_t expected_port, unsigned attempts, sc_tick delay);
	static std::string append_port(const std::string &ip, uint16_t port);
};


#endif // SC_SERVER_HPP
