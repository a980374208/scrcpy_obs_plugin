#ifndef SCRCPY_CONTROLLER_H
#define SCRCPY_CONTROLLER_H

#include "util/net.h"
#include "control_msg.h"
#include <stdbool.h>

#ifdef __cplusplus
#include <mutex>
#endif

struct sc_controller;

struct sc_controller_callbacks {
    void (*on_ended)(struct sc_controller *controller, bool error,
                     void *userdata);
};

struct sc_controller {
    sc_socket control_socket;
    bool stopped;
    const struct sc_controller_callbacks *cbs;
    void *cbs_userdata;
#ifdef __cplusplus
    std::mutex mutex;
#endif
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
