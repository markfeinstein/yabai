#ifndef DECLARATIVE_MODES_H
#define DECLARATIVE_MODES_H

#include "declarative_config.h"

enum declarative_modes_error_code {
    DECLARATIVE_MODES_ERROR_NONE,
    DECLARATIVE_MODES_ERROR_INVALID_MODEL,
    DECLARATIVE_MODES_ERROR_EXPLICIT_MODE_NOT_FOUND,
    DECLARATIVE_MODES_ERROR_ACTIVE_MODE_NOT_FOUND,
    DECLARATIVE_MODES_ERROR_ALLOCATION
};
struct declarative_modes_error { enum declarative_modes_error_code code; struct declarative_error detail; };
struct declarative_space_composition {
    struct declarative_space_policy *spaces;
    size_t space_count;
    size_t allocated_bytes;
    void (*allocator_free)(void *);
};

/* selected_mode overrides the file's active_mode when non-NULL. The returned
 * preparation-lifetime copy owns all of its storage and never aliases model.
 * Failures are typed in error->code; explicit selection lookup is a runtime
 * error and is never reported as an active_mode schema failure. */
bool declarative_modes_compose(const struct declarative_config_model *model,
                               const char *selected_mode,
                               struct declarative_space_composition *composition,
                               struct declarative_modes_error *error);
void declarative_modes_composition_destroy(struct declarative_space_composition *composition);

#ifdef TESTS
void declarative_modes_set_allocator_ops(const struct declarative_allocator_ops *ops);
#endif

#endif
