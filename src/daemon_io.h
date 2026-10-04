#ifndef DAEMON_IO_H
#define DAEMON_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "daemon_message_admission.h"
#include "daemon_response.h"

#define DAEMON_IO_READER_COUNT 4
#define DAEMON_IO_WRITER_COUNT 4
#define DAEMON_IO_QUEUE_CAPACITY DAEMON_MESSAGE_ADMISSION_MAX

#ifndef DAEMON_IO_READ_TIMEOUT_MS
#define DAEMON_IO_READ_TIMEOUT_MS SOCKET_IO_PRODUCTION_TIMEOUT_MS
#endif
#ifndef DAEMON_IO_WRITE_TIMEOUT_MS
#define DAEMON_IO_WRITE_TIMEOUT_MS DAEMON_RESPONSE_WRITE_TIMEOUT_MS
#endif
#define DAEMON_IO_SHUTDOWN_GRACE_MS 2000U

/*
 * At most 32 admitted jobs exist. Each owns at most a 1 MiB request and, only
 * after reserving a writer slot, one 8 MiB response. Queue storage is fixed.
 * Thus peer-controlled aggregate payload storage is bounded by 288 MiB, plus
 * small fixed job/queue bookkeeping and socket buffers.
 *
 * Ownership moves ACCEPT -> READER -> EVENT -> WRITER -> FINALIZED. Posting an
 * event always consumes EVENT ownership, including rejection. The sole
 * finalizer closes the descriptor before releasing admission.
 */
enum daemon_io_job_owner
{
    DAEMON_IO_OWNER_READER,
    DAEMON_IO_OWNER_EVENT,
    DAEMON_IO_OWNER_WRITER,
    DAEMON_IO_OWNER_FINALIZED
};

struct daemon_io_job
{
    volatile uint32_t finalized;
    enum daemon_io_job_owner owner;
    uint64_t event_generation;
    int fd;
    char *request;
    size_t request_length;
    struct daemon_response_buffer response;
    bool response_owned;
    bool writer_reserved;
};

bool daemon_io_begin(uint64_t event_generation);
/* Returns false without changing state when called by an event/I/O worker. */
bool daemon_io_end(void);
bool daemon_io_enqueue_fd(int fd);
void daemon_io_execute(struct daemon_io_job *job);
void daemon_io_job_destroy(struct daemon_io_job *job);

#ifdef TESTS
uint32_t daemon_io_test_reader_thread_count(void);
uint32_t daemon_io_test_writer_thread_count(void);
uint32_t daemon_io_test_active_jobs(void);
uint32_t daemon_io_test_writer_reservations(void);
#endif

#endif
