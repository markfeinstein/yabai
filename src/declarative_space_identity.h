#ifndef DECLARATIVE_SPACE_IDENTITY_H
#define DECLARATIVE_SPACE_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define DECLARATIVE_SPACE_IDENTITY_STRING_CAPACITY 1025

struct declarative_space_identity
{
    bool has_uuid;
    char uuid[DECLARATIVE_SPACE_IDENTITY_STRING_CAPACITY];
    char parent_display_uuid[DECLARATIVE_SPACE_IDENTITY_STRING_CAPACITY];
    uint32_t parent_did;
    int parent_display_ordinal;
    uint64_t sid;
    int kind;
    int mission_control_ordinal;
    uint64_t fingerprint;
};

static inline uint64_t declarative_space_identity_hash_bytes(uint64_t hash, const void *bytes, size_t length)
{
    const unsigned char *cursor = bytes;
    for (size_t i = 0; i < length; ++i) {
        hash ^= cursor[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static inline uint64_t declarative_space_identity_fingerprint(const struct declarative_space_identity *identity)
{
    if (!identity) return 0;
    uint64_t hash = UINT64_C(1469598103934665603);
    unsigned char mode = identity->has_uuid ? 1 : 0;
    hash = declarative_space_identity_hash_bytes(hash, &mode, sizeof(mode));
    if (identity->has_uuid) {
        hash = declarative_space_identity_hash_bytes(hash, identity->uuid, strlen(identity->uuid) + 1);
    } else {
        hash = declarative_space_identity_hash_bytes(hash, identity->parent_display_uuid,
                                                      strlen(identity->parent_display_uuid) + 1);
        hash = declarative_space_identity_hash_bytes(hash, &identity->parent_did, sizeof(identity->parent_did));
        hash = declarative_space_identity_hash_bytes(hash, &identity->parent_display_ordinal,
                                                      sizeof(identity->parent_display_ordinal));
        hash = declarative_space_identity_hash_bytes(hash, &identity->sid, sizeof(identity->sid));
        hash = declarative_space_identity_hash_bytes(hash, &identity->kind, sizeof(identity->kind));
        hash = declarative_space_identity_hash_bytes(hash, &identity->mission_control_ordinal,
                                                      sizeof(identity->mission_control_ordinal));
    }
    return hash ? hash : 1;
}

static inline bool declarative_space_identity_valid(const struct declarative_space_identity *identity)
{
    if (!identity || !identity->fingerprint || !identity->sid || identity->kind < 0 ||
        identity->mission_control_ordinal < 0) return false;
    if (identity->has_uuid) return identity->uuid[0] != '\0' &&
        identity->fingerprint == declarative_space_identity_fingerprint(identity);
    return identity->uuid[0] == '\0' && identity->parent_display_uuid[0] != '\0' &&
        identity->parent_did != 0 && identity->parent_display_ordinal >= 0 &&
        identity->fingerprint == declarative_space_identity_fingerprint(identity);
}

static inline bool declarative_space_identity_equal(const struct declarative_space_identity *a,
                                                     const struct declarative_space_identity *b)
{
    if (!declarative_space_identity_valid(a) || !declarative_space_identity_valid(b) ||
        a->has_uuid != b->has_uuid || a->fingerprint != b->fingerprint) return false;
    if (a->has_uuid) return strcmp(a->uuid, b->uuid) == 0;
    return a->parent_did == b->parent_did &&
           a->parent_display_ordinal == b->parent_display_ordinal &&
           a->sid == b->sid && a->kind == b->kind &&
           a->mission_control_ordinal == b->mission_control_ordinal &&
           strcmp(a->parent_display_uuid, b->parent_display_uuid) == 0;
}

#endif
