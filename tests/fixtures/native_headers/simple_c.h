#pragma once

typedef union DuduNativeEvent {
    int type;
} DuduNativeEvent;

typedef struct DuduNativeWindow DuduNativeWindow;

enum DuduNativeKind {
    dudu_native_kind_ok = 1,
};
