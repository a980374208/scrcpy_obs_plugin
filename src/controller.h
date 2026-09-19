#ifndef SCRCPY_CONTROLLER_H
#define SCRCPY_CONTROLLER_H

#include "util/net.h"
#include "control_msg.h"
#include <stdbool.h>

#ifdef __cplusplus
#include "util/sc_thread.h"
#endif

struct sc_controller;

// Android DeviceMessageWriter: 256 KiB total, minus type + length.
#define SC_DEVICE_MSG_CLIPBOARD_MAX_LENGTH ((1u << 18) - 5)
// Client defense for device/camera capability JSON and diagnostic error JSON.
// These custom Android messages have no protocol maximum; >1 MiB is rejected.
#define SC_DEVICE_MSG_JSON_MAX_LENGTH (1u << 20)

struct sc_controller_callbacks {
    // Once per receiver exit: false for clean EOF/local stop, true for a
    // truncated/invalid message, read error or allocation failure. Local stop
    // takes precedence. Session lifecycle independently handles remote EOF.
    void (*on_ended)(struct sc_controller *controller, bool error,
                     void *userdata);
    // Complete payload, including empty strings; borrowed only during callback.
    void (*on_device_info)(struct sc_controller *controller, const char *json,
                           void *userdata);
    void (*on_error_message)(struct sc_controller *controller, const char *error_msg,
                             void *userdata);
};

struct sc_controller {
    sc_socket control_socket = SC_SOCKET_NONE;
    std::atomic<bool> stopped{true};
    const struct sc_controller_callbacks *cbs;
    void *cbs_userdata;
#ifdef __cplusplus
    sc_mutex mutex;
#endif
    // Owned by this controller; mutex protects serialization through the last send.
    uint8_t serialized_msg[SC_CONTROL_MSG_MAX_SIZE];
    void *recv_thread = nullptr;
};

#ifdef __cplusplus
extern "C" {
#endif

bool
sc_controller_init(struct sc_controller *controller, sc_socket control_socket,
                   const struct sc_controller_callbacks *cbs,
                   void *cbs_userdata);

void
sc_controller_configure(struct sc_controller *controller,
                        void *acksync,
                        void *uhid_devices);

void
sc_controller_destroy(struct sc_controller *controller);

bool
sc_controller_start(struct sc_controller *controller);

void
sc_controller_request_stop(struct sc_controller *controller);

void
sc_controller_stop(struct sc_controller *controller);

void
sc_controller_join(struct sc_controller *controller);

bool
sc_controller_push_msg(struct sc_controller *controller,
                       const struct sc_control_msg *msg);

#ifdef __cplusplus
}
#endif

#endif // SCRCPY_CONTROLLER_H
