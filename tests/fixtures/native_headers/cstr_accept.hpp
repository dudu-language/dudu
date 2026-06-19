#pragma once

namespace dudu_native_cstr {
inline int accepts_cstr(const char* text) {
    return text == nullptr ? 42 : 1;
}
} // namespace dudu_native_cstr
