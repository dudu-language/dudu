#pragma once

typedef struct dudu_pointer_info {
    int value;
} dudu_pointer_info;

static inline dudu_pointer_info* dudu_pointer_info_get(void) {
    static dudu_pointer_info info = {42};
    return &info;
}
