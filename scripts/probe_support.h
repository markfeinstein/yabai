#ifndef YABAI_PROBE_SUPPORT_H
#define YABAI_PROBE_SUPPORT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static inline uint64_t probe_clock_ns(clockid_t clock_id)
{
    struct timespec value;
    if (clock_gettime(clock_id, &value) != 0) return 0;
    return (uint64_t) value.tv_sec * UINT64_C(1000000000) + (uint64_t) value.tv_nsec;
}

static inline bool probe_safe_token(const char *value)
{
    if (!value || !value[0]) return false;
    for (const unsigned char *cursor = (const unsigned char *) value; *cursor; ++cursor) {
        if (!((*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= '0' && *cursor <= '9') ||
              *cursor == '-' || *cursor == '_' || *cursor == '.')) return false;
    }
    return true;
}

static inline bool probe_json_string(FILE *stream, const char *value)
{
    if (!stream || !value || fputc('"', stream) == EOF) return false;
    for (const unsigned char *cursor = (const unsigned char *) value; *cursor; ++cursor) {
        switch (*cursor) {
        case '"': if (fputs("\\\"", stream) == EOF) return false; break;
        case '\\': if (fputs("\\\\", stream) == EOF) return false; break;
        case '\b': if (fputs("\\b", stream) == EOF) return false; break;
        case '\f': if (fputs("\\f", stream) == EOF) return false; break;
        case '\n': if (fputs("\\n", stream) == EOF) return false; break;
        case '\r': if (fputs("\\r", stream) == EOF) return false; break;
        case '\t': if (fputs("\\t", stream) == EOF) return false; break;
        default:
            if (*cursor < 0x20) {
                if (fprintf(stream, "\\u%04x", *cursor) < 0) return false;
            } else if (fputc(*cursor, stream) == EOF) return false;
        }
    }
    return fputc('"', stream) != EOF;
}

#ifdef __OBJC__
#import <Foundation/Foundation.h>

static inline void probe_spin_ms(unsigned milliseconds)
{
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:(double) milliseconds / 1000.0];
    while ([deadline timeIntervalSinceNow] > 0) {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
}
#endif

#endif
