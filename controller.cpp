#include "controller.h"
#include "util/net.h"
#include <string.h>

extern "C" bool
sc_controller_init(struct sc_controller *controller, sc_socket control_socket,
                   const struct sc_controller_callbacks *cbs,
                   void *cbs_userdata) {
    controller->control_socket = control_socket;
    controller->stopped = false;
    controller->cbs = cbs;
    controller->cbs_userdata = cbs_userdata;
    return true;
}

extern "C" void
sc_controller_configure(struct sc_controller *controller,
                        void *acksync,
                        void *uhid_devices) {
    (void)controller;
    (void)acksync;
    (void)uhid_devices;
}

extern "C" void
sc_controller_destroy(struct sc_controller *controller) {
    sc_controller_stop(controller);
}

extern "C" bool
sc_controller_start(struct sc_controller *controller) {
    (void)controller;
    return true;
}

extern "C" void
sc_controller_stop(struct sc_controller *controller) {
    controller->stopped = true;
}

extern "C" void
sc_controller_join(struct sc_controller *controller) {
    (void)controller;
}

extern "C" bool
sc_controller_push_msg(struct sc_controller *controller,
                       const struct sc_control_msg *msg) {
    if (controller->stopped || controller->control_socket == SC_SOCKET_NONE) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(controller->mutex);
    
    static uint8_t serialized_msg[SC_CONTROL_MSG_MAX_SIZE];
    size_t length = sc_control_msg_serialize(msg, serialized_msg);
    if (!length) {
        return false;
    }

    ssize_t w = net_send_all(controller->control_socket, serialized_msg, length);
    return (size_t)w == length;
}
