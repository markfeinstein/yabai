#ifndef LEGACY_ALLOCATOR_H
#define LEGACY_ALLOCATOR_H

#include <stddef.h>

/* Allocation seam used only by legacy lifecycle and BSP constructors. */
void *legacy_allocation_calloc(size_t count, size_t size);
bool legacy_allocation_adopt(void *memory);
void legacy_allocation_unadopt(void *memory);
void legacy_allocation_free(void *memory);

#ifdef TESTS
void legacy_allocation_test_fail_at(size_t ordinal);
void legacy_allocation_test_reset(void);
size_t legacy_allocation_test_count(void);
size_t legacy_allocation_test_outstanding(void);
void legacy_allocation_test_fail_adoption_at(size_t ordinal);
#endif

#endif

#ifdef LEGACY_ALLOCATOR_IMPLEMENTATION
#include <stdint.h>
#include <stdlib.h>

#ifdef TESTS
static size_t legacy_allocation_ordinal;
static size_t legacy_allocation_failure;
static size_t legacy_allocation_outstanding;
struct legacy_allocation_record { void *memory; struct legacy_allocation_record *next; };
static struct legacy_allocation_record *legacy_allocation_records;
static size_t legacy_adoption_ordinal;
static size_t legacy_adoption_failure;

static bool legacy_allocation_register(void *memory)
{
    if (!memory) return false;
    for (struct legacy_allocation_record *record = legacy_allocation_records; record; record = record->next) {
        if (record->memory == memory) return true;
    }
    struct legacy_allocation_record *record = malloc(sizeof(*record));
    if (!record) return false;
    record->memory = memory;
    record->next = legacy_allocation_records;
    legacy_allocation_records = record;
    ++legacy_allocation_outstanding;
    return true;
}

static bool legacy_allocation_unregister(void *memory)
{
    struct legacy_allocation_record **record = &legacy_allocation_records;
    while (*record && (*record)->memory != memory) record = &(*record)->next;
    if (!*record) return false;
    struct legacy_allocation_record *removed = *record;
    *record = removed->next;
    free(removed);
    --legacy_allocation_outstanding;
    return true;
}

void legacy_allocation_test_fail_adoption_at(size_t ordinal)
{
    legacy_adoption_ordinal = 0;
    legacy_adoption_failure = ordinal;
}

void legacy_allocation_test_fail_at(size_t ordinal)
{
    legacy_allocation_ordinal = 0;
    legacy_allocation_failure = ordinal;
}

void legacy_allocation_test_reset(void)
{
    legacy_allocation_ordinal = 0;
    legacy_allocation_failure = 0;
    legacy_adoption_ordinal = 0;
    legacy_adoption_failure = 0;
}

size_t legacy_allocation_test_count(void) { return legacy_allocation_ordinal; }
size_t legacy_allocation_test_outstanding(void) { return legacy_allocation_outstanding; }
#endif

void *legacy_allocation_calloc(size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size) return NULL;
#ifdef TESTS
    ++legacy_allocation_ordinal;
    if (legacy_allocation_failure && legacy_allocation_ordinal == legacy_allocation_failure) return NULL;
#endif
    void *memory = calloc(count, size);
#ifdef TESTS
    if (memory && !legacy_allocation_register(memory)) { free(memory); return NULL; }
#endif
    return memory;
}

bool legacy_allocation_adopt(void *memory)
{
#ifdef TESTS
    ++legacy_adoption_ordinal;
    if (legacy_adoption_failure && legacy_adoption_ordinal == legacy_adoption_failure) return false;
    return legacy_allocation_register(memory);
#else
    (void) memory;
    return true;
#endif
}

void legacy_allocation_unadopt(void *memory)
{
#ifdef TESTS
    (void) legacy_allocation_unregister(memory);
#else
    (void) memory;
#endif
}

void legacy_allocation_free(void *memory)
{
    if (!memory) return;
#ifdef TESTS
    (void) legacy_allocation_unregister(memory);
#endif
    free(memory);
}
#endif
