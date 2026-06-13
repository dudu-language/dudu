#pragma once

#include <pthread.h>

struct dudu_pthread_counter {
    pthread_mutex_t mutex;
    int value;
};

static inline void dudu_pthread_counter_init(struct dudu_pthread_counter* counter) {
    pthread_mutex_init(&counter->mutex, 0);
    counter->value = 0;
}

static inline void dudu_pthread_counter_destroy(struct dudu_pthread_counter* counter) {
    pthread_mutex_destroy(&counter->mutex);
}

static inline void dudu_pthread_counter_add(struct dudu_pthread_counter* counter, int value) {
    pthread_mutex_lock(&counter->mutex);
    counter->value += value;
    pthread_mutex_unlock(&counter->mutex);
}

typedef void* (*dudu_pthread_entry)(void*);

static inline int dudu_pthread_run_join(dudu_pthread_entry entry, void* user_data) {
    pthread_t thread;
    int err = pthread_create(&thread, 0, entry, user_data);
    if (err != 0) {
        return err;
    }
    return pthread_join(thread, 0);
}
