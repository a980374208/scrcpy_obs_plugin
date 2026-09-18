#pragma once
#include "server/server.hpp"
#include "controller.h"
#include "codec/demuxer.h"

// Called by the session owner, with start/update/destroy serialized by the caller.
// No wrapper may be freed while any worker, sink callback or sender can use it.
inline void sc_stop_capture(sc_server &server, sc_controller &controller,
                            sc_demuxer &video, sc_demuxer &audio)
{
    sc_controller_stop(&controller);
    server.server_stop(); // Cancels startup and interrupts every published stream.
    sc_controller_join(&controller); // Receiver plus synchronous send barrier.
    video.join();
    audio.join(); // Also required when audio is disabled and waiting for its header.
    server.close_sockets();
}
