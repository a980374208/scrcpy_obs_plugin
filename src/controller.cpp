#include "controller.h"
#include "util/net.h"
#include "util/binary.h"
#include <obs-module.h>
#include "util/sc_log.h"
#include <string.h>
#include <thread>
#include <vector>
#include <climits>
#include <new>
#include <stdexcept>

// true means a receive/protocol failure; local stop is classified by the caller.
static bool recv_messages(struct sc_controller *controller) {
    static_assert(SC_DEVICE_MSG_CLIPBOARD_MAX_LENGTH < INT_MAX);
    static_assert(SC_DEVICE_MSG_JSON_MAX_LENGTH < INT_MAX);
    static_assert(SC_DEVICE_MSG_JSON_MAX_LENGTH < SIZE_MAX);
    while (!controller->stopped) {
        uint8_t type;
        ssize_t r = net_recv_all(controller->control_socket, &type, 1);
        if (r != 1) {
            return r < 0; // EOF between messages is a clean remote end.
        }
        if (type != 0 && type != 3 && type != 4) {
            error("[controller] Unknown device message type received: %d. Closing receiver.", (int)type);
            return true;
        }

        uint8_t len_buf[4];
        if (net_recv_all(controller->control_socket, len_buf, sizeof(len_buf)) != sizeof(len_buf)) {
            scrcpy_log(LOG_WARNING, "[controller] Incomplete message length (type=%u)", (unsigned)type);
            return true;
        }
        const uint32_t len = sc_read32be(len_buf);
        const uint32_t limit = type == 0 ? SC_DEVICE_MSG_CLIPBOARD_MAX_LENGTH : SC_DEVICE_MSG_JSON_MAX_LENGTH;
        if (len > limit) {
            scrcpy_log(LOG_WARNING, "[controller] Message too large (type=%u, length=%u, limit=%u)",
                       (unsigned)type, (unsigned)len, (unsigned)limit);
            return true;
        }
        // The checked limit fits int and leaves room for the local terminator.
        // Wire length counts UTF-8 bytes; no terminator is consumed from the stream.
        std::vector<char> payload(static_cast<size_t>(len) + 1, '\0');
        if (net_recv_all(controller->control_socket, payload.data(), len) != static_cast<ssize_t>(len)) {
            scrcpy_log(LOG_WARNING, "[controller] Incomplete message payload (type=%u)", (unsigned)type);
            return true;
        }
        if (controller->stopped) return false;
        if (controller->cbs) {
            if (type == 3 && controller->cbs->on_device_info) {
                controller->cbs->on_device_info(controller, payload.data(), controller->cbs_userdata);
            } else if (type == 4 && controller->cbs->on_error_message) {
                controller->cbs->on_error_message(controller, payload.data(), controller->cbs_userdata);
            }
        }
        // Clipboard (including empty text) is consumed without a callback.
    }
    return false;
}

static void run_recv_thread(struct sc_controller *controller) {
    bool failed;
    try {
        failed = recv_messages(controller);
    } catch (const std::bad_alloc &) {
        // Includes copies made by the session callbacks while payload is borrowed.
        scrcpy_log(LOG_WARNING, "[controller] Could not allocate received message");
        failed = true;
    } catch (const std::length_error &) {
        scrcpy_log(LOG_WARNING, "[controller] Received message allocation length rejected");
        failed = true;
    }
    if (controller->cbs && controller->cbs->on_ended) {
        controller->cbs->on_ended(controller, failed && !controller->stopped, controller->cbs_userdata);
    }
}

extern "C" bool
sc_controller_init(struct sc_controller *controller, sc_socket control_socket,
                   const struct sc_controller_callbacks *cbs,
                   void *cbs_userdata) {
    std::lock_guard<sc_mutex> lock(controller->mutex);
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
    try {
        controller->recv_thread = new std::thread(run_recv_thread, controller);
    } catch (const std::system_error &) {
        controller->stopped = true;
        return false;
    }
    return true;
}

extern "C" void
sc_controller_request_stop(struct sc_controller *controller) {
    controller->stopped = true;
}

extern "C" void
sc_controller_stop(struct sc_controller *controller) {
    sc_controller_request_stop(controller);
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
    // stop() has already interrupted a blocked send. Wait for its last access.
    std::lock_guard<sc_mutex> lock(controller->mutex);
    controller->control_socket = SC_SOCKET_NONE;
}

extern "C" bool
sc_controller_push_msg(struct sc_controller *controller,
                       const struct sc_control_msg *msg) {
    if (controller->stopped.load(std::memory_order_acquire)) {
        return false;
    }
    std::lock_guard<sc_mutex> lock(controller->mutex);
    if (controller->stopped || controller->control_socket == SC_SOCKET_NONE) {
        return false;
    }
    
    size_t length = sc_control_msg_serialize(msg, controller->serialized_msg);
    if (!length) {
        return false;
    }

    ssize_t w = net_send_all(controller->control_socket, controller->serialized_msg, length);
    return (size_t)w == length;
}
