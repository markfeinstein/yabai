#include "declarative_modes.h"

static void *modes_default_calloc(size_t count, size_t size) { return calloc(count, size); }
static void modes_default_free(void *pointer) { free(pointer); }
static const struct declarative_allocator_ops modes_default_allocator = { modes_default_calloc, modes_default_free };
static const struct declarative_allocator_ops *modes_allocator = &modes_default_allocator;

#ifdef TESTS
void declarative_modes_set_allocator_ops(const struct declarative_allocator_ops *ops)
{
    modes_allocator = ops ? ops : &modes_default_allocator;
}
#endif

static void modes_error(struct declarative_modes_error *error, enum declarative_modes_error_code code, enum declarative_error_stage stage, const char *path, const char *message)
{
    if (!error) return;
    memset(error, 0, sizeof(*error));
    error->code = code;
    error->detail.stage = stage;
    snprintf(error->detail.json_path, sizeof(error->detail.json_path), "%s", path);
    snprintf(error->detail.message, sizeof(error->detail.message), "%s", message);
}

static void *composition_alloc(struct declarative_space_composition *composition, size_t size, struct declarative_modes_error *error)
{
    if (!size) size = 1;
    if (composition->allocated_bytes > DECLARATIVE_CONFIG_MAX_MODEL_BYTES || size > DECLARATIVE_CONFIG_MAX_MODEL_BYTES - composition->allocated_bytes) {
        modes_error(error, DECLARATIVE_MODES_ERROR_RESOURCE, DECLARATIVE_ERROR_ALLOCATION, "$", "composed model exceeds limit");
        return NULL;
    }
    void *result = modes_allocator->calloc_fn(1, size);
    if (!result) {
        modes_error(error, DECLARATIVE_MODES_ERROR_RESOURCE, DECLARATIVE_ERROR_ALLOCATION, "$", "allocation failed");
        return NULL;
    }
    composition->allocated_bytes += size;
    return result;
}

static char *composition_string(struct declarative_space_composition *composition, const char *source, struct declarative_modes_error *error)
{
    if (!source) return NULL;
    size_t size = strlen(source) + 1;
    char *copy = composition_alloc(composition, size, error);
    if (copy) memcpy(copy, source, size);
    return copy;
}

void declarative_modes_composition_destroy(struct declarative_space_composition *composition)
{
    if (!composition) return;
    void (*release)(void *) = composition->allocator_free ? composition->allocator_free : modes_default_free;
    if (composition->spaces) for (size_t i = 0; i < composition->space_count; ++i) {
        release(composition->spaces[i].uuid);
        for (size_t slot = 0; slot < composition->spaces[i].scaffold_count; ++slot) release(composition->spaces[i].scaffold[slot].pattern);
    }
    release(composition->spaces);
    memset(composition, 0, sizeof(*composition));
}

static bool copy_policy(struct declarative_space_composition *composition, struct declarative_space_policy *destination, const struct declarative_space_policy *source, struct declarative_modes_error *error)
{
    *destination = *source;
    destination->uuid = NULL;
    for (size_t slot = 0; slot < DECLARATIVE_CONFIG_MAX_SCAFFOLD_SLOTS; ++slot) destination->scaffold[slot].pattern = NULL;
    destination->uuid = composition_string(composition, source->uuid, error);
    if (source->uuid && !destination->uuid) return false;
    for (size_t slot = 0; slot < source->scaffold_count; ++slot) {
        destination->scaffold[slot].pattern = composition_string(composition, source->scaffold[slot].pattern, error);
        if (source->scaffold[slot].pattern && !destination->scaffold[slot].pattern) return false;
    }
    return true;
}

bool declarative_modes_compose_selection(const struct declarative_config_model *model, const char *selected_mode, bool use_file_active, struct declarative_space_composition *composition, struct declarative_modes_error *error)
{
    if (error) memset(error, 0, sizeof(*error));
    if (!composition) return false;
    memset(composition, 0, sizeof(*composition));
    if (!model) { modes_error(error, DECLARATIVE_MODES_ERROR_INVALID_MODEL, DECLARATIVE_ERROR_RUNTIME, "$", "model is required"); return false; }
    composition->allocator_free = modes_allocator->free_fn;
    const char *name = selected_mode ? selected_mode : use_file_active ? model->active_mode : NULL;
    const struct declarative_mode_model *mode = NULL;
    if (name) {
        for (size_t i = 0; i < model->mode_count; ++i) if (!strcmp(name, model->modes[i].name)) { mode = &model->modes[i]; break; }
        if (!mode) { if (selected_mode) modes_error(error, DECLARATIVE_MODES_ERROR_EXPLICIT_MODE_NOT_FOUND, DECLARATIVE_ERROR_RUNTIME, "$.selected_mode", "explicitly selected mode is not declared"); else modes_error(error, DECLARATIVE_MODES_ERROR_ACTIVE_MODE_NOT_FOUND, DECLARATIVE_ERROR_RUNTIME, "$.active_mode", "model active_mode is not declared"); return false; }
    }
    size_t mode_count = mode ? mode->space_count : 0;
    if (mode_count > DECLARATIVE_CONFIG_MAX_SPACES || model->space_count > DECLARATIVE_CONFIG_MAX_SPACES - mode_count) { modes_error(error, DECLARATIVE_MODES_ERROR_INVALID_MODEL, DECLARATIVE_ERROR_RUNTIME, "$.spaces", "total Space policy limit exceeded"); return false; }
    composition->space_count = model->space_count + mode_count;
    if (composition->space_count) {
        composition->spaces = composition_alloc(composition, sizeof(*composition->spaces) * composition->space_count, error);
        if (!composition->spaces) goto fail;
    }
    for (size_t i = 0; i < model->space_count; ++i) if (!copy_policy(composition, &composition->spaces[i], &model->spaces[i], error)) goto fail;
    for (size_t i = 0; i < mode_count; ++i) if (!copy_policy(composition, &composition->spaces[model->space_count + i], &mode->spaces[i], error)) goto fail;
    return true;
fail:
    declarative_modes_composition_destroy(composition);
    return false;
}

bool declarative_modes_compose(const struct declarative_config_model *model, const char *selected_mode,
                               struct declarative_space_composition *composition,
                               struct declarative_modes_error *error)
{
    return declarative_modes_compose_selection(model, selected_mode, true, composition, error);
}
