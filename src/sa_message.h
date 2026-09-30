#ifndef SA_MESSAGE_H
#define SA_MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "osax/common.h"

struct sa_payload_builder {
    uint8_t *bytes;
    size_t capacity;
    size_t length;
};

static inline bool sa_payload_builder_init(struct sa_payload_builder *builder, void *bytes, size_t capacity, uint8_t op)
{
    if (!builder || !bytes || capacity < sizeof(int16_t) + sizeof(op)) return false;
    builder->bytes = bytes;
    builder->capacity = capacity;
    builder->length = sizeof(int16_t);
    builder->bytes[builder->length++] = op;
    return true;
}

static inline bool sa_payload_builder_append(struct sa_payload_builder *builder, const void *value, size_t size)
{
    if (!builder || !value || builder->length > builder->capacity ||
        size > builder->capacity - builder->length) return false;
    memcpy(builder->bytes + builder->length, value, size);
    builder->length += size;
    return true;
}

static inline bool sa_payload_builder_append_count(struct sa_payload_builder *builder, int count)
{
    return count >= 0 && sa_payload_builder_append(builder, &count, sizeof(count));
}

static inline bool sa_payload_builder_append_count_max(struct sa_payload_builder *builder, int count, int maximum)
{
    return count <= maximum && sa_payload_builder_append_count(builder, count);
}

static inline bool sa_payload_builder_append_swap_record(struct sa_payload_builder *builder, bool skip,
                                                         uint32_t wid, uint32_t proxy_wid)
{
    uint32_t zero = 0;
    if (skip) return sa_payload_builder_append(builder, &zero, sizeof(zero));
    return sa_payload_builder_append(builder, &wid, sizeof(wid)) &&
           sa_payload_builder_append(builder, &proxy_wid, sizeof(proxy_wid));
}

static inline bool sa_payload_builder_finish(struct sa_payload_builder *builder, size_t *length)
{
    if (!builder || builder->length < sizeof(int16_t) + 1 ||
        builder->length > builder->capacity || builder->length - sizeof(int16_t) > INT16_MAX) return false;

    int16_t payload_length = (int16_t) (builder->length - sizeof(int16_t));
    memcpy(builder->bytes, &payload_length, sizeof(payload_length));
    if (length) *length = builder->length;
    return true;
}

#endif
