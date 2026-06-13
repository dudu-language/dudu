#pragma once

#define DUDU_WRAP_MAGIC 17
#define DUDU_WRAP_SCALE(value) ((value) * 3)

static inline int dudu_wrap_add_magic(int value) {
    return value + DUDU_WRAP_MAGIC;
}
