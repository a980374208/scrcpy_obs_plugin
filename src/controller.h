#ifndef SCRCPY_CONTROLLER_H
#define SCRCPY_CONTROLLER_H

#include "util/net.h"
#include "control_msg.h"
#include <stdbool.h>

#ifdef __cplusplus
#include "util/sc_thread.h"
#endif

struct sc_controller;

struct sc_controller_callbacks {
    void (*on_ended)(struct sc_controller *controller, bool error,
                     void *userdata);
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
