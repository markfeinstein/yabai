#ifndef DECLARATIVE_REPORTER_H
#define DECLARATIVE_REPORTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

struct declarative_error;

#define DECLARATIVE_REPORT_PRIMARY_MAX 2048
#define DECLARATIVE_REPORT_LOG_MAX     1024

void declarative_report_error(FILE *primary, const struct declarative_error *error);
void declarative_report_error_client(FILE *primary, const struct declarative_error *error);
void declarative_report_runtime(FILE *primary, const char *stage, const char *public_path,
                                const char *json_path, size_t line, size_t column,
                                const char *message);
void declarative_report_runtime_client(FILE *primary, const char *stage, const char *public_path,
                                       const char *json_path, size_t line, size_t column,
                                       const char *message);
void declarative_report_stale_rule(void);

#ifdef TESTS
struct declarative_reporter_test_sinks
{
    bool (*notification)(const char *identifier, const char *title, const char *subtitle,
                         const char *payload, void *context);
    bool (*unified_log)(const char *format, const char *payload, void *context);
    void *context;
};
void declarative_reporter_test_set_sinks(const struct declarative_reporter_test_sinks *sinks);
void declarative_reporter_test_reset(void);
size_t declarative_reporter_test_render(char *output, size_t capacity, size_t limit,
                                        const char *stage, const char *public_path,
                                        const char *json_path, size_t line, size_t column,
                                        const char *message, bool newline);
#endif

#endif
