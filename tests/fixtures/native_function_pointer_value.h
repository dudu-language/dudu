#pragma once

typedef int (*dudu_transform_fn)(int value);

static inline int dudu_increment(int value) {
    return value + 1;
}

static dudu_transform_fn dudu_transform = dudu_increment;
