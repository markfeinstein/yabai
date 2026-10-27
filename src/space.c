extern int g_connection;

#ifdef TESTS
static CFArrayRef (*space_window_copy_candidates)(int, uint32_t, CFArrayRef, uint32_t, uint64_t *, uint64_t *) =
    SLSCopyWindowsWithOptionsAndTags;
static CFIndex (*space_window_candidate_count)(CFArrayRef) = CFArrayGetCount;
static struct window_server_snapshot *(*space_window_capture_snapshot)(int, CFArrayRef, int) =
    window_server_snapshot_create;
static void (*space_window_destroy_snapshot)(struct window_server_snapshot *) =
    window_server_snapshot_destroy;
#else
#define space_window_copy_candidates SLSCopyWindowsWithOptionsAndTags
#define space_window_candidate_count CFArrayGetCount
#define space_window_capture_snapshot window_server_snapshot_create
#define space_window_destroy_snapshot window_server_snapshot_destroy
#endif

static bool space_window_record_is_eligible(const struct window_server_window_record *record,
                                            struct window *window, bool include_minimized)
{
    if (window && (include_minimized || !window_check_flag(window, WINDOW_MINIMIZE))) return true;
    /* Preserve the legacy fallthrough for a tracked minimized record whose
     * WindowServer metadata still matches the root-window predicate. */
    if (record->parent_id != 0 || (record->level != 0 && record->level != 3 && record->level != 8)) {
        return false;
    }

    if (((record->attributes & 0x2) || (record->tags & 0x400000000000000)) &&
        ((record->tags & 0x1) || ((record->tags & 0x2) && (record->tags & 0x80000000)))) {
        return true;
    }

    return include_minimized &&
           (record->attributes == 0x0 || record->attributes == 0x1) &&
           ((record->tags & 0x1000000000000000) || (record->tags & 0x300000000000000)) &&
           ((record->tags & 0x1) || ((record->tags & 0x2) && (record->tags & 0x80000000)));
}

inline uint32_t space_display_id(uint64_t sid)
{
    CFStringRef uuid_string = SLSCopyManagedDisplayForSpace(g_connection, sid);
    if (!uuid_string) return 0;

    CFUUIDRef uuid = CFUUIDCreateFromString(NULL, uuid_string);
    if (!uuid) {
        CFRelease(uuid_string);
        return 0;
    }

    uint32_t id = CGDisplayGetDisplayIDFromUUID(uuid);

    CFRelease(uuid);
    CFRelease(uuid_string);

    return id;
}

uint32_t *space_window_list_for_connection_with_snapshot(uint64_t *space_list, int space_count, int cid, int *count,
                                                         bool include_minimized, bool *success,
                                                         struct window_server_snapshot **snapshot_out)
{
    *count = 0;
    if (success) *success = false;
    if (snapshot_out) *snapshot_out = NULL;
    uint32_t *window_list = NULL;
    struct window_server_snapshot *snapshot = NULL;
    uint64_t set_tags = 0;
    uint64_t clear_tags = 0;
    uint32_t options = include_minimized ? 0x7 : 0x2;

    CFArrayRef space_list_ref = cfarray_of_cfnumbers(space_list, sizeof(uint64_t), space_count, kCFNumberSInt64Type);
    if (!space_list_ref) return NULL;

    CFArrayRef window_list_ref = space_window_copy_candidates(g_connection, cid, space_list_ref,
                                                               options, &set_tags, &clear_tags);
    if (!cf_is_type(window_list_ref, CFArrayGetTypeID())) {
        if (window_list_ref) CFRelease(window_list_ref);
        goto err;
    }

    if (!cfindex_to_int(space_window_candidate_count(window_list_ref), count)) goto inventory_failed;
    if (!*count) {
        if (success) *success = true;
        goto out;
    }

    snapshot = space_window_capture_snapshot(g_connection, window_list_ref, *count);
    if (!snapshot || snapshot->count != (size_t) *count || !snapshot->records) goto inventory_failed;

    const uint64_t required_fields = WINDOW_SERVER_WINDOW_FIELD_BASE;
    int window_count = 0;
    window_list = ts_alloc_list(uint32_t, *count);
    for (size_t i = 0; i < snapshot->count; ++i) {
        const struct window_server_window_record *record = snapshot->records + i;
        if ((record->valid_fields & required_fields) != required_fields) goto inventory_failed;
        struct window *window = window_manager_find_window(&g_window_manager, record->id);
        if (space_window_record_is_eligible(record, window, include_minimized)) {
            window_list[window_count++] = record->id;
        }
    }

    window_list = ts_resize(window_list, sizeof(uint32_t) * *count, sizeof(uint32_t) * window_count);
    *count = window_count;
    if (success) *success = true;
    if (snapshot_out) {
        *snapshot_out = snapshot;
        snapshot = NULL;
    }
    goto out;

inventory_failed:
    window_list = NULL;
    *count = 0;
out:
    if (snapshot) space_window_destroy_snapshot(snapshot);
    CFRelease(window_list_ref);
err:
    CFRelease(space_list_ref);
    return window_list;
}

uint32_t *space_window_list_for_connection_with_status(uint64_t *space_list, int space_count, int cid,
                                                       int *count, bool include_minimized, bool *success)
{
    return space_window_list_for_connection_with_snapshot(space_list, space_count, cid, count,
                                                          include_minimized, success, NULL);
}

uint32_t *space_window_list_for_connection(uint64_t *space_list, int space_count, int cid, int *count, bool include_minimized)
{
    return space_window_list_for_connection_with_status(space_list, space_count, cid, count, include_minimized, NULL);
}

inline uint32_t *space_window_list(uint64_t sid, int *count, bool include_minimized)
{
    return space_window_list_for_connection(&sid, 1, 0, count, include_minimized);
}

inline  bool space_is_user(uint64_t sid)
{
    return SLSSpaceGetType(g_connection, sid) == 0;
}

inline bool space_is_fullscreen(uint64_t sid)
{
    return SLSSpaceGetType(g_connection, sid) == 4;
}

inline bool space_is_system(uint64_t sid)
{
    return SLSSpaceGetType(g_connection, sid) == 2;
}

inline bool space_is_visible(uint64_t sid)
{
    return sid == display_space_id(space_display_id(sid));
}
