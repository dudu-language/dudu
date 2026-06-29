#pragma once

#include <stdbool.h>

typedef union DuduNativeEvent {
    int type;
} DuduNativeEvent;

typedef struct DuduNativeWindow DuduNativeWindow;
typedef void (*DuduNativeProc)(void);

enum DuduNativeKind {
    dudu_native_kind_ok = 1,
};

#define DUDU_NATIVE_MAGIC 7
#define DUDU_NATIVE_SCALE(value) ((value) * 2)
#define DUDU_NATIVE_CHECK() dudu_native_ready(nullptr)

bool dudu_native_ready(DuduNativeEvent* event);
/** Adds two native integers. */ int dudu_native_add(int a, int b);
const char* dudu_native_format(const char* text, ...);
DuduNativeProc dudu_native_proc(void);
