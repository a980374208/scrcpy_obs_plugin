#include "controller.h"
#include "util/net.h"
#include "util/binary.h"
#include <obs-module.h>
#include "util/sc_log.h"
#include <string.h>
#include <thread>
#include <vector>

static void run_recv_thread(struct sc_controller *controller) {
    while (!controller->stopped) {
        uint8_t type;
        ssize_t r = net_recv_all(controller->control_socket, &type, 1);
        if (r <= 0) {
            scrcpy_log(LOG_INFO, "[controller] Socket closed or error, exiting recv thread");
            break;
        }

        if (type == 3) { // TYPE_DEVICE_INFO
            uint8_t len_buf[4];
            r = net_recv_all(controller->control_socket, len_buf, 4);
            if (r <= 0) {
                scrcpy_log(LOG_WARNING, "[controller] Failed to read device info length");
                break;
            }
            uint32_t len = sc_read32be(len_buf);
            if (len > 0) {
                std::vector<char> json_buf(len + 1, 0);
                r = net_recv_all(controller->control_socket, json_buf.data(), len);
                if (r <= 0) {
                    scrcpy_log(LOG_WARNING, "[controller] Failed to read device info JSON payload");
                    break;
                }
                
                if (controller->cbs && controller->cbs->on_device_info) {
                    controller->cbs->on_device_info(controller, json_buf.data(), controller->cbs_userdata);
                }
            }
        } else if (type == 0) { // TYPE_CLIPBOARD
            uint8_t len_buf[4];
            r = net_recv_all(controller->control_socket, len_buf, 4);
            if (r <= 0) break;
            uint32_t len = sc_read32be(len_buf);
            if (len > 0) {
                std::vector<char> clipboard_buf(len, 0);
                r = net_recv_all(controller->control_socket, clipboard_buf.data(), len);
                if (r <= 0) break;
            }
        } else if (type == 4) { // TYPE_ERROR
            uint8_t len_buf[4];
            r = net_recv_all(controller->control_socket, len_buf, 4);
            if (r <= 0) {
                scrcpy_log(LOG_WARNING, "[controller] Failed to read error message length");
                break;
            }
            uint32_t len = sc_read32be(len_buf);
            if (len > 0) {
                std::vector<char> err_buf(len + 1, 0);
                r = net_recv_all(controller->control_socket, err_buf.data(), len);
                if (r <= 0) {
                    scrcpy_log(LOG_WARNING, "[controller] Failed to read error message payload");
                    break;
                }

                if (controller->cbs && controller->cbs->on_error_message) {
                    controller->cbs->on_error_message(controller, err_buf.data(), controller->cbs_userdata);
                }
            }
        } else {
            error("[controller] Unknown device message type received: %d. Closing receiver.", (int)type);
            break;
        }
    }
    
    if (controller->cbs && controller->cbs->on_ended) {
        controller->cbs->on_ended(controller, false, controller->cbs_userdata);
    }
}

extern "C" bool
sc_controller_init(struct sc_controller *controller, sc_socket control_socket,
                   const struct sc_controller_callbacks *cbs,
                   void *cbs_userdata) {
    controller->control_socket = control_socket;
    controller->stopped = false;
    controller->cbs = cbs;
    controller->cbs_userdata = cbs_userdata;
    controller->recv_thread = nullptr;
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
    sc_controller_join(controller);
}

extern "C" bool
sc_controller_start(struct sc_controller *controller) {
    if (controller->control_socket == SC_SOCKET_NONE) {
        return false;
    }
    controller->stopped = false;
    controller->recv_thread = new std::thread(run_recv_thread, controller);
    return true;
}

extern "C" void
sc_controller_stop(struct sc_controller *controller) {
    controller->stopped = true;
    if (controller->control_socket != SC_SOCKET_NONE) {
        net_interrupt(controller->control_socket);
    }
}

extern "C" void
sc_controller_join(struct sc_controller *controller) {
    if (controller->recv_thread) {
        std::thread *th = static_cast<std::thread *>(controller->recv_thread);
        if (th->joinable()) {
            th->join();
        }
        delete th;
        controller->recv_thread = nullptr;
    }
}

extern "C" bool
sc_controller_push_msg(struct sc_controller *controller,
                       const struct sc_control_msg *msg) {
    if (controller->stopped || controller->control_socket == SC_SOCKET_NONE) {
        return false;
    }
    
    std::lock_guard<sc_mutex> lock(controller->mutex);
    
    static uint8_t serialized_msg[SC_CONTROL_MSG_MAX_SIZE];
    size_t length = sc_control_msg_serialize(msg, serialized_msg);
    if (!length) {
        return false;
    }

    ssize_t w = net_send_all(controller->control_socket, serialized_msg, length);
    return (size_t)w == length;
}
