#pragma once

#include <stdint.h>

typedef void (*dudu_audio_callback)(float* output, int32_t frames, void* user_data);

struct dudu_audio_state {
    float phase;
    float step;
    int32_t calls;
};

static inline void dudu_audio_run(dudu_audio_callback callback, void* user_data, float* output,
                                  int32_t frames) {
    callback(output, frames, user_data);
}
