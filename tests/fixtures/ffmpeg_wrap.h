#pragma once

extern "C" {
#include <libavcodec/packet.h>
}

static inline AVPacket* dudu_av_packet_alloc(void) {
    return av_packet_alloc();
}

static inline void dudu_av_packet_free(AVPacket** packet) {
    av_packet_free(packet);
}
