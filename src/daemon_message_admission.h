#ifndef DAEMON_MESSAGE_ADMISSION_H
#define DAEMON_MESSAGE_ADMISSION_H

#include <stdbool.h>
#include <stdint.h>

#define DAEMON_MESSAGE_ADMISSION_MAX 32

struct daemon_message_admission
{
    uint32_t count;
};

static inline bool daemon_message_admission_acquire(struct daemon_message_admission *admission)
{
    uint32_t count = __atomic_load_n(&admission->count, __ATOMIC_RELAXED);
    while (count < DAEMON_MESSAGE_ADMISSION_MAX) {
        if (__atomic_compare_exchange_n(&admission->count, &count, count + 1, true,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            return true;
        }
    }
    return false;
}

static inline void daemon_message_admission_release(struct daemon_message_admission *admission)
{
    uint32_t count = __atomic_load_n(&admission->count, __ATOMIC_RELAXED);
    while (count != 0) {
        if (__atomic_compare_exchange_n(&admission->count, &count, count - 1, true,
                                        __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
            return;
        }
    }
}

static inline uint32_t daemon_message_admission_count(struct daemon_message_admission *admission)
{
    return __atomic_load_n(&admission->count, __ATOMIC_ACQUIRE);
}

extern struct daemon_message_admission g_daemon_message_admission;

#endif
