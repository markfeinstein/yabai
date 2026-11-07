#ifndef SBUFFER_H
#define SBUFFER_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef BUFFER_REALLOC
#define BUFFER_REALLOC(pointer, size) realloc((pointer), (size))
#endif

struct buf_hdr
{
    int len;
    int cap;
    char buf[0];
};

#define buf__hdr(b) ((struct buf_hdr *)((char *)(b) - offsetof(struct buf_hdr, buf)))
#define buf_len(b) ((b) ? buf__hdr(b)->len : 0)
#define buf_cap(b) ((b) ? buf__hdr(b)->cap : 0)
#define buf_last(b) ((b)[buf_len(b)-1])
#define buf_del(b, x) ((b) ? (b)[x] = (b)[buf_len(b)-1], buf__hdr(b)->len-- : 0)
#define buf_free(b) ((b) ? free(buf__hdr(b)) : 0)

static bool sbuffer_growth_size(int current_capacity, int requested_length, size_t element_size,
                                int *new_capacity, size_t *new_size, size_t header_size)
{
    if (current_capacity < 0 || requested_length <= 0 || !element_size ||
        !new_capacity || !new_size) return false;

    size_t doubled = current_capacity > (INT_MAX - 1) / 2
                   ? (size_t) INT_MAX
                   : 1u + 2u * (size_t) current_capacity;
    size_t capacity = doubled > (size_t) requested_length ? doubled : (size_t) requested_length;
    if (capacity > INT_MAX || capacity > (SIZE_MAX - header_size) / element_size) return false;

    *new_capacity = (int) capacity;
    *new_size = header_size + capacity * element_size;
    return true;
}

static void *buf__grow_f(const void *buf, int new_len, size_t elem_size)
{
    int new_cap;
    size_t new_size;
    if (!sbuffer_growth_size(buf_cap(buf), new_len, elem_size, &new_cap, &new_size,
                             offsetof(struct buf_hdr, buf))) return NULL;

    struct buf_hdr *new_hdr = BUFFER_REALLOC(buf ? buf__hdr(buf) : NULL, new_size);
    if (!new_hdr) return NULL;
    new_hdr->cap = new_cap;
    if (!buf) new_hdr->len = 0;
    return new_hdr->buf;
}

static bool buf__try_fit_f(void *buffer_address, int additional, size_t elem_size)
{
    if (!buffer_address || additional < 0) return false;
    void *buf;
    memcpy(&buf, buffer_address, sizeof(buf));
    int length = buf ? buf__hdr(buf)->len : 0;
    int capacity = buf ? buf__hdr(buf)->cap : 0;
    if (length < 0 || capacity < 0 || length > capacity || length > INT_MAX - additional) return false;
    int required = length + additional;
    if (required <= capacity) return true;

    void *grown = buf__grow_f(buf, required, elem_size);
    if (!grown) return false;
    memcpy(buffer_address, &grown, sizeof(grown));
    return true;
}

#define buf_try_fit(b, n) buf__try_fit_f(&(b), (n), sizeof(*(b)))
#define buf_try_push(b, x) \
    (buf_try_fit((b), 1) && (b) ? ((b)[buf_len(b)] = (x), buf__hdr(b)->len++, true) : false)
#define buf_push(b, x) ((void) buf_try_push((b), (x)))

struct ts_buf_hdr
{
    int len;
    int cap;
    char buf[0];
};

#define ts_buf__hdr(b) ((struct ts_buf_hdr *)((char *)(b) - offsetof(struct ts_buf_hdr, buf)))
#define ts_buf_len(b) ((b) ? ts_buf__hdr(b)->len : 0)
#define ts_buf_cap(b) ((b) ? ts_buf__hdr(b)->cap : 0)
#define ts_buf_last(b) ((b)[ts_buf_len(b)-1])
#define ts_buf_del(b, x) ((b) ? (b)[x] = (b)[ts_buf_len(b)-1], ts_buf__hdr(b)->len-- : 0)

static void *ts_buf__grow_f(const void *buf, int new_len, size_t elem_size)
{
    int new_cap;
    size_t new_size;
    if (!sbuffer_growth_size(ts_buf_cap(buf), new_len, elem_size, &new_cap, &new_size,
                             offsetof(struct ts_buf_hdr, buf))) return NULL;

    struct ts_buf_hdr *new_hdr;
    if (buf) {
        new_hdr = ts_buf__hdr(buf);
        size_t old_size = offsetof(struct ts_buf_hdr, buf) +
                          (size_t) new_hdr->cap * elem_size;
        if (!ts_expand_checked(new_hdr, old_size, new_size - old_size)) return NULL;
    } else {
        new_hdr = ts_alloc_aligned_checked(8, new_size);
        if (!new_hdr) return NULL;
        new_hdr->len = 0;
    }

    new_hdr->cap = new_cap;
    return new_hdr->buf;
}

static bool ts_buf__try_fit_f(void *buffer_address, int additional, size_t elem_size)
{
    if (!buffer_address || additional < 0) return false;
    void *buf;
    memcpy(&buf, buffer_address, sizeof(buf));
    int length = buf ? ts_buf__hdr(buf)->len : 0;
    int capacity = buf ? ts_buf__hdr(buf)->cap : 0;
    if (length < 0 || capacity < 0 || length > capacity || length > INT_MAX - additional) return false;
    int required = length + additional;
    if (required <= capacity) return true;

    void *grown = ts_buf__grow_f(buf, required, elem_size);
    if (!grown) return false;
    memcpy(buffer_address, &grown, sizeof(grown));
    return true;
}

static void ts_buf__fatal_growth(void)
{
    fprintf(stderr, "fatal error: temporary stretch buffer growth failed\n");
    exit(EXIT_FAILURE);
}

#define ts_buf_try_fit(b, n) ts_buf__try_fit_f(&(b), (n), sizeof(*(b)))
#define ts_buf_try_push(b, x) \
    (ts_buf_try_fit((b), 1) && (b) ? ((b)[ts_buf_len(b)] = (x), ts_buf__hdr(b)->len++, true) : false)
#define ts_buf_push(b, x) \
    (ts_buf_try_push((b), (x)) ? true : (ts_buf__fatal_growth(), false))

#endif
