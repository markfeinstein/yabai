#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define TABLE_HASH_FUNC(name) unsigned long name(void *key)
typedef TABLE_HASH_FUNC(table_hash_func);

#define TABLE_COMPARE_FUNC(name) int name(void *key_a, void *key_b)
typedef TABLE_COMPARE_FUNC(table_compare_func);

struct bucket
{
    void *key;
    void *value;
    struct bucket *next;
};
struct table
{
    int count;
    int capacity;
    float max_load;
    table_hash_func *hash;
    table_compare_func *cmp;
    struct bucket **buckets;
};

void table_init(struct table *table, int capacity, table_hash_func hash, table_compare_func cmp);
bool table_try_init(struct table *table, int capacity, table_hash_func hash, table_compare_func cmp);
void table_free(struct table *table);

#define table_add(table, key, value) _table_add(table, key, sizeof(*key), value)
#define table_try_add(table, key, value) _table_try_add(table, key, sizeof(*key), value)
void _table_add(struct table *table, void *key, int key_size, void *value);
bool _table_try_add(struct table *table, void *key, int key_size, void *value);
bool table_replace(struct table *table, void *key, void *value);
#ifdef TESTS
void table_test_fail_allocation_after(int successful_allocations);
#endif
void table_remove(struct table *table, void *key);
void *table_find(struct table *table, void *key);

#define table_for(it, table, code) \
    for (int i = 0; i < table.capacity; ++i) { \
        for (struct bucket *bucket = table.buckets[i]; bucket; bucket = bucket->next) { \
            if (!bucket->value) continue; \
            it = bucket->value; \
            code; \
        } \
    }

#endif

#ifdef HASHTABLE_IMPLEMENTATION
#ifdef TESTS
static int table_test_allocations_until_failure = -1;

void table_test_fail_allocation_after(int successful_allocations)
{
    table_test_allocations_until_failure = successful_allocations;
}

static bool table_test_allocation_allowed(void)
{
    if (table_test_allocations_until_failure < 0) return true;
    if (table_test_allocations_until_failure == 0) return false;
    --table_test_allocations_until_failure;
    return true;
}

#define table_malloc(size) (table_test_allocation_allowed() ? malloc(size) : NULL)
#define table_calloc(count, size) (table_test_allocation_allowed() ? calloc(count, size) : NULL)
#else
#define table_malloc(size) malloc(size)
#define table_calloc(count, size) calloc(count, size)
#endif

bool table_try_init(struct table *table, int capacity, table_hash_func hash, table_compare_func cmp)
{
    if (!table || capacity <= 0 || !hash || !cmp) return false;
    struct bucket **buckets = table_calloc((size_t) capacity, sizeof(struct bucket *));
    if (!buckets) return false;
    table->count = 0;
    table->capacity = capacity;
    table->max_load = 0.75f;
    table->hash = hash;
    table->cmp = cmp;
    table->buckets = buckets;
    return true;
}

void table_init(struct table *table, int capacity, table_hash_func hash, table_compare_func cmp)
{
    if (!table_try_init(table, capacity, hash, cmp)) abort();
}

void table_free(struct table *table)
{
    if (!table) return;
    if (!table->buckets || table->capacity <= 0) {
        table->count = 0;
        table->capacity = 0;
        table->buckets = NULL;
        return;
    }
    for (int i = 0; i < table->capacity; ++i) {
        struct bucket *next, *bucket = table->buckets[i];
        while (bucket) {
            next = bucket->next;
            free(bucket->key);
            free(bucket);
            bucket = next;
        }
    }

    free(table->buckets);
    table->buckets = NULL;
    table->count = 0;
    table->capacity = 0;
}

static struct bucket **
table_get_bucket(struct table *table, void *key)
{
    struct bucket **bucket = table->buckets + (table->hash(key) % table->capacity);
    while (*bucket) {
        if (table->cmp((*bucket)->key, key)) {
            break;
        }
        bucket = &(*bucket)->next;
    }
    return bucket;
}

static bool table_rehash(struct table *table)
{
    if (table->capacity > INT_MAX / 2) return false;
    int new_capacity = 2 * table->capacity;
    struct bucket **new_buckets = table_calloc((size_t) new_capacity, sizeof(struct bucket *));
    if (!new_buckets) return false;

    for (int i = 0; i < table->capacity; ++i) {
        struct bucket *bucket = table->buckets[i];
        while (bucket) {
            struct bucket *next = bucket->next;
            unsigned long index = table->hash(bucket->key) % new_capacity;
            bucket->next = new_buckets[index];
            new_buckets[index] = bucket;
            bucket = next;
        }
    }
    free(table->buckets);
    table->buckets = new_buckets;
    table->capacity = new_capacity;
    return true;
}

bool _table_try_add(struct table *table, void *key, int key_size, void *value)
{
    if (!table || !table->buckets || table->capacity <= 0 || !table->hash || !table->cmp ||
        !key || key_size <= 0) return false;
    struct bucket **bucket = table_get_bucket(table, key);
    if (*bucket) {
        if (!(*bucket)->value) (*bucket)->value = value;
        return true;
    }

    struct bucket *new_bucket = table_malloc(sizeof(*new_bucket));
    void *new_key = table_malloc((size_t) key_size);
    if (!new_bucket || !new_key) {
        free(new_key);
        free(new_bucket);
        return false;
    }
    memcpy(new_key, key, (size_t) key_size);
    new_bucket->key = new_key;
    new_bucket->value = value;
    new_bucket->next = NULL;
    *bucket = new_bucket;
    ++table->count;

    float load = (1.0f * table->count) / table->capacity;
    if (load > table->max_load && !table_rehash(table)) {
        table_remove(table, key);
        return false;
    }
    return true;
}

void _table_add(struct table *table, void *key, int key_size, void *value)
{
    (void) _table_try_add(table, key, key_size, value);
}

bool table_replace(struct table *table, void *key, void *value)
{
    if (!table || !table->buckets || table->capacity <= 0 || !key) return false;
    struct bucket *bucket = *table_get_bucket(table, key);
    if (!bucket) return false;
    bucket->value = value;
    return true;
}

void table_remove(struct table *table, void *key)
{
    if (!table || !table->buckets || table->capacity <= 0 || !table->hash || !table->cmp || !key) return;
    struct bucket *next, **bucket = table_get_bucket(table, key);
    if (*bucket) {
        free((*bucket)->key);
        next = (*bucket)->next;
        free(*bucket);
        *bucket = next;
        --table->count;
    }
}

void *table_find(struct table *table, void *key)
{
    if (!table || !table->buckets || table->capacity <= 0 || !table->hash || !table->cmp || !key) return NULL;
    struct bucket *bucket = *table_get_bucket(table, key);
    return bucket ? bucket->value : NULL;
}

#undef table_malloc
#undef table_calloc
#endif
