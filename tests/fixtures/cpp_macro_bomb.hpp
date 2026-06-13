#pragma once

#define DUDU_MACRO_BOMB_VALUE 11
#define DUDU_MACRO_BOMB_FLAG 1
#define DUDU_MACRO_BOMB_SCALE(value) ((value) * 2)
#define DUDU_MACRO_BOMB_MIX(a, b, c) (((a) + (b)) * (c))
#define DUDU_MACRO_BOMB_MAX(a, b) ((a) > (b) ? (a) : (b))
#define DUDU_MACRO_BOMB_FIRST(first, ...) (first)
#define DUDU_MACRO_BOMB_SUM2(first, second, ...) ((first) + (second))
#define DUDU_MACRO_BOMB_ASSIGN(dst, value) \
    do {                                   \
        (dst) = (value);                   \
    } while (0)
#define DUDU_MACRO_BOMB_INC(value) (++(value))
#define DUDU_MACRO_BOMB_STRINGIZE(value) #value
#define DUDU_MACRO_BOMB_CAT(left, right) left##right

#define dudu_macro_bomb_lower(value) ((value) + 1)

static inline int dudu_macro_bomb_helper(int value) {
    return value;
}
