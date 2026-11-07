#ifndef DECLARATIVE_RELOAD_SUMMARY_H
#define DECLARATIVE_RELOAD_SUMMARY_H

#include <stddef.h>

#define DECLARATIVE_RELOAD_SUMMARY_MAX_BYTES 256u

struct declarative_config_model;

struct declarative_reload_summary
{
    char text[DECLARATIVE_RELOAD_SUMMARY_MAX_BYTES];
    size_t change_count;
};

void declarative_reload_summary_build(struct declarative_reload_summary *summary,
                                      const struct declarative_config_model *previous,
                                      const struct declarative_config_model *current);

#endif
