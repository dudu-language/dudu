#pragma once

enum DuduNativeEventType : unsigned int {
    DUDU_NATIVE_EVENT_NONE = 0,
    DUDU_NATIVE_EVENT_QUIT = 256,
};

struct DuduNativeEvent {
    unsigned int type;
};

inline DuduNativeEvent dudu_native_event(unsigned int type) {
    return DuduNativeEvent{type};
}
