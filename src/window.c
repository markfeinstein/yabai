extern struct window_manager g_window_manager;
extern int g_layer_normal_window_level;
extern int g_layer_below_window_level;
extern int g_layer_above_window_level;
extern int g_connection;

#ifdef TESTS
static AXError (*window_observe_add_notification)(AXObserverRef, AXUIElementRef, CFStringRef, void *) = AXObserverAddNotification;
static CFArrayRef (*window_ax_snapshot_create_attributes)(CFAllocatorRef, const void **, CFIndex,
                                                          const CFArrayCallBacks *) = CFArrayCreate;
static AXError (*window_ax_snapshot_copy_multiple)(AXUIElementRef, CFArrayRef,
                                                    AXCopyMultipleAttributeOptions,
                                                    CFArrayRef *) = AXUIElementCopyMultipleAttributeValues;
static AXError (*window_ax_snapshot_copy_single)(AXUIElementRef, CFStringRef,
                                                  CFTypeRef *) = AXUIElementCopyAttributeValue;
static uint32_t (*window_create_parent_id)(uint32_t) = window_parent;
static bool (*window_create_shadow)(uint32_t) = window_shadow;
static bool (*window_create_sticky)(uint32_t) = window_is_sticky;
static uint64_t (*window_create_space_id)(uint32_t) = window_space;
static bool (*window_create_space_is_fullscreen)(uint64_t) = space_is_fullscreen;
static bool (*window_create_can_move)(struct window *) = window_ax_can_move;
static bool (*window_create_can_resize)(struct window *) = window_ax_can_resize;
static CGError (*window_test_is_ordered_in)(int, uint32_t, uint8_t *) = SLSWindowIsOrderedIn;
static CFArrayRef (*window_test_description_from_array)(CFArrayRef) = CGWindowListCreateDescriptionFromArray;
static uint64_t (*window_test_serialization_space)(uint32_t) = window_space;
static bool (*window_test_serialization_is_sticky)(uint32_t) = window_is_sticky;
static uint32_t (*window_test_space_display_id)(uint64_t) = space_display_id;
static int (*window_test_display_id_arrangement)(uint32_t) = display_manager_display_id_arrangement;
#else
#define window_observe_add_notification AXObserverAddNotification
#define window_ax_snapshot_create_attributes CFArrayCreate
#define window_ax_snapshot_copy_multiple AXUIElementCopyMultipleAttributeValues
#define window_ax_snapshot_copy_single AXUIElementCopyAttributeValue
#define window_create_parent_id window_parent
#define window_create_shadow window_shadow
#define window_create_sticky window_is_sticky
#define window_create_space_id window_space
#define window_create_space_is_fullscreen space_is_fullscreen
#define window_create_can_move window_ax_can_move
#define window_create_can_resize window_ax_can_resize
#define window_test_is_ordered_in SLSWindowIsOrderedIn
#define window_test_description_from_array CGWindowListCreateDescriptionFromArray
#define window_test_serialization_space window_space
#define window_test_serialization_is_sticky window_is_sticky
#define window_test_space_display_id space_display_id
#define window_test_display_id_arrangement display_manager_display_id_arrangement
#endif

bool window_observe(struct window *window, bool *retry)
{
    bool retryable_failure = false;
    bool permanent_failure = false;
    if (retry) *retry = false;
    for (int i = 0; i < array_count(ax_window_notification); ++i) {
        AXError result = window_observe_add_notification(window->application->observer_ref, window->ref, ax_window_notification[i], window);
        if (result == kAXErrorSuccess || result == kAXErrorNotificationAlreadyRegistered) {
            window->notification |= 1 << i;
        } else {
            if (result == kAXErrorCannotComplete) {
                retryable_failure = true;
            } else {
                permanent_failure = true;
            }
            debug("%s: %s failed with error %s\n", __FUNCTION__, ax_window_notification_str[i], ax_error_str[-result]);
        }
    }

    if (retry) *retry = retryable_failure && !permanent_failure;
    return (window->notification & AX_WINDOW_ALL) == AX_WINDOW_ALL;
}

void window_unobserve(struct window *window)
{
    AXObserverRef observer_ref = window->application ? window->application->observer_ref : NULL;

    for (int i = 0; i < array_count(ax_window_notification); ++i) {
        if (!(window->notification & (1 << i))) continue;

        if (observer_ref) {
            AXObserverRemoveNotification(observer_ref, window->ref, ax_window_notification[i]);
        }

        window->notification &= ~(1 << i);
    }
}

CFStringRef window_display_uuid(uint32_t wid)
{
    CFStringRef uuid = SLSCopyManagedDisplayForWindow(g_connection, wid);
    if (!uuid) {
        CGRect frame = CGRectZero;
        if (SLSGetWindowBounds(g_connection, wid, &frame) != kCGErrorSuccess) return NULL;
        uuid = SLSCopyBestManagedDisplayForRect(g_connection, frame);
    }
    return uuid;
}

uint32_t window_display_id(uint32_t wid)
{
    CFStringRef uuid_string = window_display_uuid(wid);
    if (!uuid_string) return 0;

    CFUUIDRef uuid = CFUUIDCreateFromString(NULL, uuid_string);
    if (!uuid) {
        CFRelease(uuid_string);
        return 0;
    }

    int id = CGDisplayGetDisplayIDFromUUID(uuid);

    CFRelease(uuid);
    CFRelease(uuid_string);

    return id;
}

static uint64_t window_display_space(uint32_t wid)
{
    CFStringRef uuid = window_display_uuid(wid);
    if (!uuid) return 0;

    uint64_t sid = SLSManagedDisplayGetCurrentSpace(g_connection, uuid);
    CFRelease(uuid);

    return sid;
}

#ifdef TESTS
static uint64_t (*window_test_space_for_id)(uint32_t wid);
#endif

uint64_t window_space(uint32_t wid)
{
#ifdef TESTS
    if (window_test_space_for_id) return window_test_space_for_id(wid);
#endif
    uint64_t sid = 0;

    CFArrayRef window_list_ref = cfarray_of_cfnumbers(&wid, sizeof(uint32_t), 1, kCFNumberSInt32Type);
    if (!window_list_ref) return window_display_space(wid);

    CFArrayRef space_list_ref = SLSCopySpacesForWindows(g_connection, 0x7, window_list_ref);
    if (!space_list_ref) goto err;

    int count = CFArrayGetCount(space_list_ref);
    if (!count) goto free;

    CFNumberRef id_ref = CFArrayGetValueAtIndex(space_list_ref, 0);
    CFNumberGetValue(id_ref, CFNumberGetType(id_ref), &sid);

free:
    CFRelease(space_list_ref);
err:
    CFRelease(window_list_ref);

    return sid ? sid : window_display_space(wid);
}

#ifdef TESTS
static CFArrayRef (*window_test_serialization_copy_spaces)(int, int, CFArrayRef) = SLSCopySpacesForWindows;
static uint64_t (*window_test_serialization_display_space)(uint32_t) = window_display_space;
#else
#define window_test_serialization_copy_spaces SLSCopySpacesForWindows
#define window_test_serialization_display_space window_display_space
#endif

static void window_serialization_membership(uint32_t wid, uint64_t *sid, bool *is_sticky)
{
    *sid = 0;
    *is_sticky = false;

    CFArrayRef window_list_ref = cfarray_of_cfnumbers(&wid, sizeof(uint32_t), 1, kCFNumberSInt32Type);
    if (!window_list_ref) goto out;

    CFArrayRef space_list_ref = window_test_serialization_copy_spaces(g_connection, 0x7, window_list_ref);
    if (!space_list_ref) goto out;

    CFIndex count = CFArrayGetCount(space_list_ref);
    *is_sticky = count > 1;
    if (count > 0) {
        CFTypeRef id_ref = CFArrayGetValueAtIndex(space_list_ref, 0);
        if (cf_is_type(id_ref, CFNumberGetTypeID())) {
            (void) CFNumberGetValue((CFNumberRef) id_ref, CFNumberGetType((CFNumberRef) id_ref), sid);
        }
    }
    CFRelease(space_list_ref);

out:
    if (window_list_ref) CFRelease(window_list_ref);
    if (!*sid) *sid = window_test_serialization_display_space(wid);
}

#ifdef TESTS
static void (*window_test_serialization_membership)(uint32_t, uint64_t *, bool *) = window_serialization_membership;
#else
#define window_test_serialization_membership window_serialization_membership
#endif

uint64_t *window_space_list_with_status(uint32_t wid, int *count, bool *success)
{
    if (count) *count = 0;
    if (success) *success = false;
    if (!count) return NULL;

    uint64_t *space_list = NULL;
    CFArrayRef window_list_ref = cfarray_of_cfnumbers(&wid, sizeof(uint32_t), 1, kCFNumberSInt32Type);
    if (!window_list_ref) return NULL;

    CFArrayRef space_list_ref = SLSCopySpacesForWindows(g_connection, 0x7, window_list_ref);
    if (!cf_is_type(space_list_ref, CFArrayGetTypeID())) {
        if (space_list_ref) CFRelease(space_list_ref);
        goto err;
    }

    if (!cfindex_to_int(CFArrayGetCount(space_list_ref), count)) goto out;
    if (!*count) {
        if (success) *success = true;
        goto out;
    }

    space_list = ts_alloc_list_checked(uint64_t, *count);
    if (!space_list) {
        *count = 0;
        goto out;
    }

    for (int i = 0; i < *count; ++i) {
        CFNumberRef id_ref = CFArrayGetValueAtIndex(space_list_ref, i);
        if (!cf_is_type(id_ref, CFNumberGetTypeID()) ||
            !CFNumberGetValue(id_ref, kCFNumberSInt64Type, space_list + i)) {
            space_list = NULL;
            *count = 0;
            goto out;
        }
    }
    if (success) *success = true;

out:
    CFRelease(space_list_ref);
err:
    CFRelease(window_list_ref);
    return space_list;
}

uint64_t *window_space_list(uint32_t wid, int *count)
{
    return window_space_list_with_status(wid, count, NULL);
}

static inline const char *window_layer(int level)
{
    if (level == g_layer_below_window_level)  return layer_str[LAYER_BELOW];
    if (level == g_layer_normal_window_level) return layer_str[LAYER_NORMAL];
    if (level == g_layer_above_window_level)  return layer_str[LAYER_ABOVE];
    return "unknown";
}

static uint64_t window_serialization_flags(uint64_t flags)
{
    return flags ? flags : ~WINDOW_PROPERTY_DIAGNOSTIC_FLAGS;
}

static int window_serialization_space_index(const struct window_serialization_context *context,
                                            uint64_t sid)
{
    if (context && context->has_managed_display_spaces_snapshot) {
        return managed_space_index(context->managed_display_spaces, sid);
    }
    return space_manager_mission_control_index(sid);
}

static int window_serialization_display_index(const struct window_serialization_context *context,
                                              uint64_t sid)
{
    if (context && context->has_managed_display_spaces_snapshot &&
        context->has_ordered_display_uuids_snapshot &&
        cf_is_type(context->managed_display_spaces, CFArrayGetTypeID()) &&
        cf_is_type(context->ordered_display_uuids, CFArrayGetTypeID())) {
        CFStringRef uuid = managed_space_display_identifier_for_space(context->managed_display_spaces, sid);
        int arrangement = display_manager_display_uuid_arrangement(context->ordered_display_uuids, uuid);
        if (arrangement) return arrangement;
    }
    return window_test_display_id_arrangement(window_test_space_display_id(sid));
}

void window_nonax_serialize_with_context(FILE *rsp, uint32_t wid, uint64_t flags,
                                         const struct window_serialization_context *context)
{
    TIME_FUNCTION;

    flags = window_serialization_flags(flags);

    int connection = 0;
    pid_t pid = 0;
    uint64_t sid = 0;
    int level = 0;
    int sub_level = 0;
    uint32_t parent_wid = 0;
    uint64_t tags = 0;
    bool parent_known = false;
    bool tags_known = false;
    bool is_sticky = false;

    if ((flags & WINDOW_PROPERTY_PID) ||
        (flags & WINDOW_PROPERTY_APP)) {
        if (SLSGetWindowOwner(g_connection, wid, &connection) != kCGErrorSuccess ||
            SLSConnectionGetPID(connection, &pid) != kCGErrorSuccess) {
            connection = 0;
            pid = 0;
        }
    }

    bool needs_sid = (flags & WINDOW_PROPERTY_DISPLAY) ||
                     (flags & WINDOW_PROPERTY_SPACE) ||
                     (flags & WINDOW_PROPERTY_IS_FULLSCREEN);
    bool needs_sticky = flags & WINDOW_PROPERTY_IS_STICKY;
    if (needs_sid && needs_sticky) {
        window_test_serialization_membership(wid, &sid, &is_sticky);
    } else {
        if (needs_sid) sid = window_test_serialization_space(wid);
        if (needs_sticky) is_sticky = window_test_serialization_is_sticky(wid);
    }

    if ((flags & WINDOW_PROPERTY_LEVEL) ||
        (flags & WINDOW_PROPERTY_LAYER)) {
        level = window_level(wid);
    }

    if ((flags & WINDOW_PROPERTY_SUB_LEVEL) ||
        (flags & WINDOW_PROPERTY_SUB_LAYER)) {
        sub_level = window_sub_level(wid);
    }

    if ((flags & WINDOW_PROPERTY_ROOT_WINDOW) ||
        (flags & WINDOW_PROPERTY_PARENT)) {
        parent_known = window_parent_with_status(wid, &parent_wid);
    }

    if (flags & WINDOW_PROPERTY_TAGS) tags_known = window_tags_with_status(wid, &tags);

    bool did_output = false;
    fprintf(rsp, "{\n");

    if (flags & WINDOW_PROPERTY_ID) {
        fprintf(rsp, "\t\"id\":%d", wid);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_PID) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"pid\":%d", pid);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_APP) {
        if (did_output) fprintf(rsp, ",\n");

        static char process_name[PROC_PIDPATHINFO_MAXSIZE];
        process_name[0] = '\0';
        if (pid > 0) proc_name(pid, process_name, sizeof(process_name));

        char *app = process_name;
        char *escaped_app = ts_string_escape(app);

        fprintf(rsp, "\t\"app\":\"%s\"", escaped_app ? escaped_app : app);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_TITLE) {
        if (did_output) fprintf(rsp, ",\n");

        char *title = window_property_title_ts(wid);
        char *escaped_title = ts_string_escape(title);

        fprintf(rsp, "\t\"title\":\"%s\"", escaped_title ? escaped_title : title);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SCRATCHPAD) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"scratchpad\":\"%s\"", "");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_FRAME) {
        if (did_output) fprintf(rsp, ",\n");

        CGRect frame = CGRectZero;
        (void) SLSGetWindowBounds(g_connection, wid, &frame);

        fprintf(rsp, "\t\"frame\":{\n\t\t\"x\":%.4f,\n\t\t\"y\":%.4f,\n\t\t\"w\":%.4f,\n\t\t\"h\":%.4f\n\t}", frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_ROLE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"role\":\"%s\"", "");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SUBROLE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"subrole\":\"%s\"", "");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_ROOT_WINDOW) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"root-window\":%s", json_bool(parent_wid == 0));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_PARENT) {
        if (did_output) fprintf(rsp, ",\n");

        if (parent_known) fprintf(rsp, "\t\"parent\":%u", parent_wid);
        else fprintf(rsp, "\t\"parent\":null");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_DISPLAY) {
        if (did_output) fprintf(rsp, ",\n");

        int display = window_serialization_display_index(context, sid);
        fprintf(rsp, "\t\"display\":%d", display);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SPACE) {
        if (did_output) fprintf(rsp, ",\n");

        int space = window_serialization_space_index(context, sid);
        fprintf(rsp, "\t\"space\":%d", space);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_LEVEL) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"level\":%d", level);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SUB_LEVEL) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"sub-level\":%d", sub_level);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_LAYER) {
        if (did_output) fprintf(rsp, ",\n");

        const char *layer = window_layer(level);
        fprintf(rsp, "\t\"layer\":\"%s\"", layer);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SUB_LAYER) {
        if (did_output) fprintf(rsp, ",\n");

        const char *sub_layer = window_layer(sub_level);
        fprintf(rsp, "\t\"sub-layer\":\"%s\"", sub_layer);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_TAGS) {
        if (did_output) fprintf(rsp, ",\n");

        if (tags_known) fprintf(rsp, "\t\"tags\":\"0x%016llx\"", (unsigned long long) tags);
        else fprintf(rsp, "\t\"tags\":null");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_OPACITY) {
        if (did_output) fprintf(rsp, ",\n");

        float opacity = window_opacity(wid);
        fprintf(rsp, "\t\"opacity\":%.4f", opacity);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SPLIT_TYPE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"split-type\":\"%s\"", window_node_split_str[0]);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SPLIT_CHILD) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"split-child\":\"%s\"", window_node_child_str[CHILD_NONE]);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_STACK_INDEX) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"stack-index\":%d", 0);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_CAN_MOVE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"can-move\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_CAN_RESIZE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"can-resize\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_FOCUS) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-focus\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_SHADOW) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-shadow\":%s", json_bool(window_shadow(wid)));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_PARENT_ZOOM) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-parent-zoom\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_FULLSCREEN_ZOOM) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-fullscreen-zoom\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_AX_REFERENCE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-ax-reference\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_FULLSCREEN) {
        if (did_output) fprintf(rsp, ",\n");

        bool is_fullscreen = space_is_fullscreen(sid);
        fprintf(rsp, "\t\"is-native-fullscreen\":%s", json_bool(is_fullscreen));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_ORDERED_IN) {
        if (did_output) fprintf(rsp, ",\n");

        bool known = false;
        bool ordered_in = window_is_ordered_in(wid, &known);
        fprintf(rsp, "\t\"is-ordered-in\":%s", known ? json_bool(ordered_in) : "null");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_ON_SCREEN) {
        if (did_output) fprintf(rsp, ",\n");

        bool known = false;
        bool on_screen = window_is_on_screen(wid, &known);
        fprintf(rsp, "\t\"is-on-screen\":%s", known ? json_bool(on_screen) : "null");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_VISIBLE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-visible\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_MINIMIZED) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-minimized\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_HIDDEN) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-hidden\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_FLOATING) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-floating\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_STICKY) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-sticky\":%s", json_bool(is_sticky));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_GRABBED) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-grabbed\":%s", json_bool(false));
    }

    fprintf(rsp, "\n}");
}

void window_nonax_serialize(FILE *rsp, uint32_t wid, uint64_t flags)
{
    window_nonax_serialize_with_context(rsp, wid, flags, NULL);
}

static bool window_serialize_is_hidden(struct window *window)
{
    return window->application ? window->application->is_hidden : false;
}

void window_serialize_with_context(FILE *rsp, struct window *window, uint64_t flags,
                                   const struct window_serialization_context *context)
{
    TIME_FUNCTION;

    flags = window_serialization_flags(flags);

    uint64_t sid;
    int level;
    int sub_level;
    uint32_t parent_wid;
    uint64_t tags;
    bool parent_known;
    bool tags_known;
    struct view *view;
    struct window_node *node;
    bool is_minimized;
    bool is_sticky;

    bool needs_sid = (flags & WINDOW_PROPERTY_DISPLAY) ||
                     (flags & WINDOW_PROPERTY_SPACE) ||
                     (flags & WINDOW_PROPERTY_IS_VISIBLE);
    bool needs_sticky = (flags & WINDOW_PROPERTY_IS_VISIBLE) ||
                        (flags & WINDOW_PROPERTY_IS_STICKY);
    bool sticky_cached = window_check_flag(window, WINDOW_STICKY);
    if (needs_sid && needs_sticky && !sticky_cached) {
        window_test_serialization_membership(window->id, &sid, &is_sticky);
    } else {
        if (needs_sid) sid = window_test_serialization_space(window->id);
        if (needs_sticky) {
            is_sticky = sticky_cached || window_test_serialization_is_sticky(window->id);
        }
    }

    if ((flags & WINDOW_PROPERTY_LEVEL) ||
        (flags & WINDOW_PROPERTY_LAYER)) {
        level = window_level(window->id);
    }

    if ((flags & WINDOW_PROPERTY_SUB_LEVEL) ||
        (flags & WINDOW_PROPERTY_SUB_LAYER)) {
        sub_level = window_sub_level(window->id);
    }

    if (flags & WINDOW_PROPERTY_PARENT) parent_known = window_parent_with_status(window->id, &parent_wid);
    if (flags & WINDOW_PROPERTY_TAGS) tags_known = window_tags_with_status(window->id, &tags);

    if ((flags & WINDOW_PROPERTY_SPLIT_TYPE) ||
        (flags & WINDOW_PROPERTY_SPLIT_CHILD) ||
        (flags & WINDOW_PROPERTY_STACK_INDEX) ||
        (flags & WINDOW_PROPERTY_HAS_PARENT_ZOOM) ||
        (flags & WINDOW_PROPERTY_HAS_FULLSCREEN_ZOOM)) {
        view = window_manager_find_managed_window(&g_window_manager, window);
        node = view ? view_find_window_node(view, window->id) : NULL;
    }

    if ((flags & WINDOW_PROPERTY_IS_VISIBLE) ||
        (flags & WINDOW_PROPERTY_IS_MINIMIZED)) {
        is_minimized = window_check_flag(window, WINDOW_MINIMIZE);
    }

    bool did_output = false;
    fprintf(rsp, "{\n");

    if (flags & WINDOW_PROPERTY_ID) {
        fprintf(rsp, "\t\"id\":%d", window->id);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_PID) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"pid\":%d", window->application ? window->application->pid : 0);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_APP) {
        if (did_output) fprintf(rsp, ",\n");

        char *app = window->application ? window->application->name : "<unknown>";
        char *escaped_app = ts_string_escape(app);

        fprintf(rsp, "\t\"app\":\"%s\"", escaped_app ? escaped_app : (window->application ? app : "<unknown>"));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_TITLE) {
        if (did_output) fprintf(rsp, ",\n");

        char *title = window_title_ts(window);
        char *escaped_title = ts_string_escape(title);

        fprintf(rsp, "\t\"title\":\"%s\"", escaped_title ? escaped_title : title);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SCRATCHPAD) {
        if (did_output) fprintf(rsp, ",\n");

        char *scratchpad = window->scratchpad;
        char *escaped_scratchpad = scratchpad ? ts_string_escape(scratchpad) : NULL;
        fprintf(rsp, "\t\"scratchpad\":\"%s\"", escaped_scratchpad ? escaped_scratchpad : scratchpad ? scratchpad : "");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_FRAME) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"frame\":{\n\t\t\"x\":%.4f,\n\t\t\"y\":%.4f,\n\t\t\"w\":%.4f,\n\t\t\"h\":%.4f\n\t}", window->frame.origin.x, window->frame.origin.y, window->frame.size.width, window->frame.size.height);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_ROLE) {
        if (did_output) fprintf(rsp, ",\n");

        char *role = window_role_ts(window);
        char *escaped_role = role ? ts_string_escape(role) : NULL;
        fprintf(rsp, "\t\"role\":\"%s\"", escaped_role ? escaped_role : role ? role : "");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SUBROLE) {
        if (did_output) fprintf(rsp, ",\n");

        char *subrole = window_subrole_ts(window);
        char *escaped_subrole = subrole ? ts_string_escape(subrole) : NULL;
        fprintf(rsp, "\t\"subrole\":\"%s\"", escaped_subrole ? escaped_subrole : subrole ? subrole : "");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_ROOT_WINDOW) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"root-window\":%s", json_bool(window->is_root));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_PARENT) {
        if (did_output) fprintf(rsp, ",\n");

        if (parent_known) fprintf(rsp, "\t\"parent\":%u", parent_wid);
        else fprintf(rsp, "\t\"parent\":null");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_DISPLAY) {
        if (did_output) fprintf(rsp, ",\n");

        int display = window_serialization_display_index(context, sid);
        fprintf(rsp, "\t\"display\":%d", display);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SPACE) {
        if (did_output) fprintf(rsp, ",\n");

        int space = window_serialization_space_index(context, sid);
        fprintf(rsp, "\t\"space\":%d", space);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_LEVEL) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"level\":%d", level);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SUB_LEVEL) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"sub-level\":%d", sub_level);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_LAYER) {
        if (did_output) fprintf(rsp, ",\n");

        const char *layer = window_layer(level);
        fprintf(rsp, "\t\"layer\":\"%s\"", layer);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SUB_LAYER) {
        if (did_output) fprintf(rsp, ",\n");

        const char *sub_layer = window_layer(sub_level);
        fprintf(rsp, "\t\"sub-layer\":\"%s\"", sub_layer);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_TAGS) {
        if (did_output) fprintf(rsp, ",\n");

        if (tags_known) fprintf(rsp, "\t\"tags\":\"0x%016llx\"", (unsigned long long) tags);
        else fprintf(rsp, "\t\"tags\":null");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_OPACITY) {
        if (did_output) fprintf(rsp, ",\n");

        float opacity = window_opacity(window->id);
        fprintf(rsp, "\t\"opacity\":%.4f", opacity);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SPLIT_TYPE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"split-type\":\"%s\"", window_node_split_str[node && node->parent ? node->parent->split : 0]);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SPLIT_CHILD) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"split-child\":\"%s\"", window_node_child_str[node ? window_node_is_left_child(node) ? CHILD_FIRST : CHILD_SECOND : CHILD_NONE]);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_STACK_INDEX) {
        if (did_output) fprintf(rsp, ",\n");

        int stack_index = node && node->window_count > 1 ? window_node_index_of_window(node, window->id)+1 : 0;
        fprintf(rsp, "\t\"stack-index\":%d", stack_index);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_CAN_MOVE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"can-move\":%s", json_bool(window_can_move(window)));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_CAN_RESIZE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"can-resize\":%s", json_bool(window_can_resize(window)));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_FOCUS) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-focus\":%s", json_bool(window->id == g_window_manager.focused_window_id));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_SHADOW) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-shadow\":%s", json_bool(window_shadow(window->id)));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_PARENT_ZOOM) {
        if (did_output) fprintf(rsp, ",\n");

        bool zoom_parent = node && node->zoom && node->zoom == node->parent;
        fprintf(rsp, "\t\"has-parent-zoom\":%s", json_bool(zoom_parent));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_FULLSCREEN_ZOOM) {
        if (did_output) fprintf(rsp, ",\n");

        bool zoom_fullscreen = node && ((node->zoom && node->zoom == view->root) ||
                               (node == view->root && view_check_flag(view, VIEW_SINGLE_WINDOW_ZOOM)));
        fprintf(rsp, "\t\"has-fullscreen-zoom\":%s", json_bool(zoom_fullscreen));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_AX_REFERENCE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-ax-reference\":%s", json_bool(true));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_FULLSCREEN) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-native-fullscreen\":%s", json_bool(window_check_flag(window, WINDOW_FULLSCREEN)));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_ORDERED_IN) {
        if (did_output) fprintf(rsp, ",\n");

        bool known = false;
        bool ordered_in = window_is_ordered_in(window->id, &known);
        fprintf(rsp, "\t\"is-ordered-in\":%s", known ? json_bool(ordered_in) : "null");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_ON_SCREEN) {
        if (did_output) fprintf(rsp, ",\n");

        bool known = false;
        bool on_screen = window_is_on_screen(window->id, &known);
        fprintf(rsp, "\t\"is-on-screen\":%s", known ? json_bool(on_screen) : "null");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_VISIBLE) {
        if (did_output) fprintf(rsp, ",\n");

        bool known = false;
        bool ordered_in = window_is_ordered_in(window->id, &known);
        bool visible = known && ordered_in && !is_minimized && !window_serialize_is_hidden(window) && (is_sticky || space_is_visible(sid));
        fprintf(rsp, "\t\"is-visible\":%s", json_bool(visible));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_MINIMIZED) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-minimized\":%s", json_bool(is_minimized));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_HIDDEN) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-hidden\":%s", json_bool(window_serialize_is_hidden(window)));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_FLOATING) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-floating\":%s", json_bool(window_check_flag(window, WINDOW_FLOAT)));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_STICKY) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-sticky\":%s", json_bool(is_sticky));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_GRABBED) {
        if (did_output) fprintf(rsp, ",\n");

        bool grabbed = window == g_mouse_state.window;
        fprintf(rsp, "\t\"is-grabbed\":%s", json_bool(grabbed));
    }

    fprintf(rsp, "\n}");
}

void window_serialize(FILE *rsp, struct window *window, uint64_t flags)
{
    window_serialize_with_context(rsp, window, flags, NULL);
}

char *window_property_title_ts(uint32_t wid)
{
    CFTypeRef value = NULL;
    SLSCopyWindowProperty(g_connection, wid, CFSTR("kCGSWindowTitle"), &value);
    if (!value) return ts_string_copy("");

    char *result = ts_cfstring_copy(value);
    CFRelease(value);
    return result;
}

char *window_title_ts(struct window *window)
{
    return window->title ? ts_cfstring_copy(window->title) : ts_string_copy("");
}

CFStringRef window_title(struct window *window)
{
    CFTypeRef value = NULL;
    AXUIElementCopyAttributeValue(window->ref, kAXTitleAttribute, &value);
    return value;
}

static const CFStringRef window_ax_snapshot_attribute[WINDOW_AX_SNAPSHOT_SLOT_COUNT] =
{
    [WINDOW_AX_POSITION_SLOT]   = kAXPositionAttribute,
    [WINDOW_AX_SIZE_SLOT]       = kAXSizeAttribute,
    [WINDOW_AX_ROLE_SLOT]       = kAXRoleAttribute,
    [WINDOW_AX_SUBROLE_SLOT]    = kAXSubroleAttribute,
    [WINDOW_AX_TITLE_SLOT]      = kAXTitleAttribute,
    [WINDOW_AX_PARENT_SLOT]     = kAXParentAttribute,
    [WINDOW_AX_MINIMIZED_SLOT]  = kAXMinimizedAttribute,
    [WINDOW_AX_FULLSCREEN_SLOT] = kAXFullscreenAttribute
};

_Static_assert(WINDOW_AX_SNAPSHOT_SLOT_COUNT < 32, "snapshot fields must fit in the public masks");

static inline uint32_t window_ax_snapshot_field_for_slot(int slot)
{
    return 1u << slot;
}

static bool window_ax_snapshot_error_allows_fallback(AXError error)
{
    return error == kAXErrorAttributeUnsupported ||
           error == kAXErrorNoValue ||
           error == kAXErrorCannotComplete ||
           error == kAXErrorNotImplemented ||
           error == kAXErrorFailure ||
           error == kAXErrorInvalidUIElement;
}

static AXError window_ax_snapshot_store_value(struct window_ax_snapshot *snapshot, int slot, CFTypeRef value)
{
    if (!value || value == kCFNull) return kAXErrorNoValue;

    if (cf_is_type(value, AXValueGetTypeID()) &&
        AXValueGetType((AXValueRef) value) == kAXValueTypeAXError) {
        AXError error = kAXErrorFailure;
        return AXValueGetValue((AXValueRef) value, kAXValueTypeAXError, &error) ? error : kAXErrorFailure;
    }

    switch (slot) {
    case WINDOW_AX_POSITION_SLOT:
        if (!cf_is_type(value, AXValueGetTypeID()) ||
            AXValueGetType((AXValueRef) value) != kAXValueTypeCGPoint ||
            !AXValueGetValue((AXValueRef) value, kAXValueTypeCGPoint, &snapshot->position)) {
            return kAXErrorIllegalArgument;
        }
        break;
    case WINDOW_AX_SIZE_SLOT:
        if (!cf_is_type(value, AXValueGetTypeID()) ||
            AXValueGetType((AXValueRef) value) != kAXValueTypeCGSize ||
            !AXValueGetValue((AXValueRef) value, kAXValueTypeCGSize, &snapshot->size)) {
            return kAXErrorIllegalArgument;
        }
        break;
    case WINDOW_AX_ROLE_SLOT:
        if (!cf_is_type(value, CFStringGetTypeID())) return kAXErrorIllegalArgument;
        snapshot->role = (CFStringRef) CFRetain(value);
        break;
    case WINDOW_AX_SUBROLE_SLOT:
        if (!cf_is_type(value, CFStringGetTypeID())) return kAXErrorIllegalArgument;
        snapshot->subrole = (CFStringRef) CFRetain(value);
        break;
    case WINDOW_AX_TITLE_SLOT:
        if (!cf_is_type(value, CFStringGetTypeID())) return kAXErrorIllegalArgument;
        snapshot->title = (CFStringRef) CFRetain(value);
        break;
    case WINDOW_AX_PARENT_SLOT:
        if (!cf_is_type(value, AXUIElementGetTypeID())) return kAXErrorIllegalArgument;
        snapshot->parent = (AXUIElementRef) CFRetain(value);
        break;
    case WINDOW_AX_MINIMIZED_SLOT:
        if (!cf_is_type(value, CFBooleanGetTypeID())) return kAXErrorIllegalArgument;
        snapshot->minimized = CFBooleanGetValue(value);
        break;
    case WINDOW_AX_FULLSCREEN_SLOT:
        if (!cf_is_type(value, CFBooleanGetTypeID())) return kAXErrorIllegalArgument;
        snapshot->fullscreen = CFBooleanGetValue(value);
        break;
    default:
        return kAXErrorIllegalArgument;
    }

    snapshot->valid |= window_ax_snapshot_field_for_slot(slot);
    return kAXErrorSuccess;
}

static void window_ax_snapshot_fallback_field(AXUIElementRef ref, int slot,
                                               struct window_ax_snapshot *snapshot)
{
    CFTypeRef value = NULL;
    AXError error = window_ax_snapshot_copy_single(ref, window_ax_snapshot_attribute[slot], &value);
    if (error == kAXErrorSuccess) {
        error = window_ax_snapshot_store_value(snapshot, slot, value);
    }
    snapshot->slot_error[slot] = error;
    if (value) CFRelease(value);
}

static void window_ax_snapshot_fallback_frame(AXUIElementRef ref, struct window_ax_snapshot *snapshot)
{
    const uint32_t frame_mask = WINDOW_AX_POSITION | WINDOW_AX_SIZE;
    snapshot->valid &= ~frame_mask;
    snapshot->position = CGPointZero;
    snapshot->size = CGSizeZero;

    window_ax_snapshot_fallback_field(ref, WINDOW_AX_POSITION_SLOT, snapshot);
    window_ax_snapshot_fallback_field(ref, WINDOW_AX_SIZE_SLOT, snapshot);

    if ((snapshot->valid & frame_mask) != frame_mask) {
        snapshot->valid &= ~frame_mask;
        snapshot->position = CGPointZero;
        snapshot->size = CGSizeZero;
    }
}

bool window_ax_snapshot_read(AXUIElementRef ref, uint32_t requested, struct window_ax_snapshot *snapshot)
{
    if (!snapshot) return false;
    memset(snapshot, 0, sizeof(*snapshot));

    const uint32_t known_fields = (1u << WINDOW_AX_SNAPSHOT_SLOT_COUNT) - 1u;
    if (!ref || !requested || (requested & ~known_fields)) return false;
    snapshot->requested = requested;

    const void *attributes[WINDOW_AX_SNAPSHOT_SLOT_COUNT];
    uint8_t attribute_slots[WINDOW_AX_SNAPSHOT_SLOT_COUNT];
    CFIndex attribute_count = 0;
    for (int slot = 0; slot < WINDOW_AX_SNAPSHOT_SLOT_COUNT; ++slot) {
        if (!(requested & window_ax_snapshot_field_for_slot(slot))) continue;
        attributes[attribute_count] = window_ax_snapshot_attribute[slot];
        attribute_slots[attribute_count++] = (uint8_t) slot;
        snapshot->slot_error[slot] = kAXErrorNoValue;
    }

    CFArrayRef attribute_array = window_ax_snapshot_create_attributes(NULL, attributes, attribute_count,
                                                                       &kCFTypeArrayCallBacks);
    CFArrayRef values = NULL;
    AXError batch_error = kAXErrorFailure;
    if (attribute_array) {
        batch_error = window_ax_snapshot_copy_multiple(ref, attribute_array, 0, &values);
        CFRelease(attribute_array);
    }

    if (batch_error == kAXErrorSuccess && cf_is_type(values, CFArrayGetTypeID())) {
        CFIndex value_count = CFArrayGetCount(values);
        if (value_count <= attribute_count) {
            for (CFIndex index = 0; index < value_count; ++index) {
                int slot = attribute_slots[index];
                CFTypeRef value = CFArrayGetValueAtIndex(values, index);
                snapshot->slot_error[slot] = window_ax_snapshot_store_value(snapshot, slot, value);
            }
        } else {
            for (CFIndex index = 0; index < attribute_count; ++index) {
                snapshot->slot_error[attribute_slots[index]] = kAXErrorFailure;
            }
        }
    } else {
        AXError error = batch_error == kAXErrorSuccess ? kAXErrorFailure : batch_error;
        for (CFIndex index = 0; index < attribute_count; ++index) {
            snapshot->slot_error[attribute_slots[index]] = error;
        }
    }

    if (values) CFRelease(values);

    const uint32_t frame_mask = WINDOW_AX_POSITION | WINDOW_AX_SIZE;
    bool full_frame_requested = (requested & frame_mask) == frame_mask;
    if (full_frame_requested && (snapshot->valid & frame_mask) != frame_mask) {
        window_ax_snapshot_fallback_frame(ref, snapshot);
    }

    for (int slot = 0; slot < WINDOW_AX_SNAPSHOT_SLOT_COUNT; ++slot) {
        uint32_t field = window_ax_snapshot_field_for_slot(slot);
        if (!(requested & field) || (snapshot->valid & field)) continue;
        if (full_frame_requested && (field & frame_mask)) continue;
        if (window_ax_snapshot_error_allows_fallback(snapshot->slot_error[slot])) {
            window_ax_snapshot_fallback_field(ref, slot, snapshot);
        }
    }

    return (snapshot->valid & requested) == requested;
}

void window_ax_snapshot_destroy(struct window_ax_snapshot *snapshot)
{
    if (!snapshot) return;
    if (snapshot->role) CFRelease(snapshot->role);
    if (snapshot->subrole) CFRelease(snapshot->subrole);
    if (snapshot->title) CFRelease(snapshot->title);
    if (snapshot->parent) CFRelease(snapshot->parent);
    memset(snapshot, 0, sizeof(*snapshot));
}

bool window_ax_snapshot_frame(const struct window_ax_snapshot *snapshot, CGRect *frame)
{
    const uint32_t frame_mask = WINDOW_AX_POSITION | WINDOW_AX_SIZE;
    if (!snapshot || !frame || (snapshot->valid & frame_mask) != frame_mask) return false;
    *frame = (CGRect) { snapshot->position, snapshot->size };
    return true;
}

CGPoint window_ax_origin(struct window *window)
{
    CGPoint origin = {0};
    CFTypeRef position_ref = NULL;

    AXUIElementCopyAttributeValue(window->ref, kAXPositionAttribute, &position_ref);

    if (position_ref) {
        AXValueGetValue(position_ref, kAXValueTypeCGPoint, &origin);
        CFRelease(position_ref);
    }

    return origin;
}

CGRect window_ax_frame(struct window *window)
{
    CGRect frame = {0};
    CFTypeRef position_ref = NULL;
    CFTypeRef size_ref = NULL;

    AXUIElementCopyAttributeValue(window->ref, kAXPositionAttribute, &position_ref);
    AXUIElementCopyAttributeValue(window->ref, kAXSizeAttribute, &size_ref);

    if (position_ref) {
        AXValueGetValue(position_ref, kAXValueTypeCGPoint, &frame.origin);
        CFRelease(position_ref);
    }

    if (size_ref) {
        AXValueGetValue(size_ref, kAXValueTypeCGSize, &frame.size);
        CFRelease(size_ref);
    }

    return frame;
}

bool window_ax_can_move(struct window *window)
{
    Boolean result;
    if (AXUIElementIsAttributeSettable(window->ref, kAXPositionAttribute, &result) != kAXErrorSuccess) {
        result = 0;
    }
    return result;
}

bool window_can_move(struct window *window)
{
    return window_check_flag(window, WINDOW_MOVABLE);
}

bool window_ax_can_resize(struct window *window)
{
    Boolean result;
    if (AXUIElementIsAttributeSettable(window->ref, kAXSizeAttribute, &result) != kAXErrorSuccess) {
        result = 0;
    }
    return result;
}

bool window_can_resize(struct window *window)
{
    return window_check_flag(window, WINDOW_RESIZABLE);
}

bool window_can_minimize(struct window *window)
{
    Boolean result;
    if (AXUIElementIsAttributeSettable(window->ref, kAXMinimizedAttribute, &result) != kAXErrorSuccess) {
        result = 0;
    }
    return result;
}

bool window_is_undersized(struct window *window)
{
    if (window->frame.size.width  <= 500.0f) return true;
    if (window->frame.size.height <= 500.0f) return true;
    return false;
}

bool window_is_fullscreen(struct window *window)
{
    Boolean result = 0;
    CFTypeRef value;

    if (AXUIElementCopyAttributeValue(window->ref, kAXFullscreenAttribute, &value) == kAXErrorSuccess) {
        result = CFBooleanGetValue(value);
        CFRelease(value);
    }

    return result;
}

bool window_is_sticky(uint32_t wid)
{
    bool result = false;

    CFArrayRef window_list_ref = cfarray_of_cfnumbers(&wid, sizeof(uint32_t), 1, kCFNumberSInt32Type);
    if (!window_list_ref) return false;

    CFArrayRef space_list_ref = SLSCopySpacesForWindows(g_connection, 0x7, window_list_ref);
    if (!space_list_ref) goto err;

    result = CFArrayGetCount(space_list_ref) > 1;
    CFRelease(space_list_ref);

err:
    CFRelease(window_list_ref);
    return result;
}

bool window_shadow(uint32_t wid)
{
    uint64_t tags = window_tags(wid);
    return !(tags & 0x8);
}

float window_opacity(uint32_t wid)
{
    float alpha = 0.0f;
    SLSGetWindowAlpha(g_connection, wid, &alpha);
    return alpha;
}

bool window_parent_with_status(uint32_t wid, uint32_t *parent_wid)
{
    if (parent_wid) *parent_wid = 0;
    if (!wid || !parent_wid) return false;

    bool success = false;
    CFArrayRef window_ref = cfarray_of_cfnumbers(&wid, sizeof(uint32_t), 1, kCFNumberSInt32Type);
    if (!window_ref) return false;

    CFTypeRef query = SLSWindowQueryWindows(g_connection, window_ref, 1);
    if (!query) goto out;

    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    if (!iterator) goto query_out;

    if (SLSWindowIteratorGetCount(iterator) == 1 && SLSWindowIteratorAdvance(iterator)) {
        *parent_wid = SLSWindowIteratorGetParentID(iterator);
        success = true;
    }

    CFRelease(iterator);
query_out:
    CFRelease(query);
out:
    CFRelease(window_ref);
    return success;
}

uint32_t window_parent(uint32_t wid)
{
    uint32_t parent_wid = 0;
    window_parent_with_status(wid, &parent_wid);
    return parent_wid;
}

int window_level(uint32_t wid)
{
    int level = 0;

    if (workspace_is_macos_ventura_or_newer()) {
        CFArrayRef window_ref = cfarray_of_cfnumbers(&wid, sizeof(uint32_t), 1, kCFNumberSInt32Type);
        if (!window_ref) return 0;

        CFTypeRef query = SLSWindowQueryWindows(g_connection, window_ref, 1);
        if (!query) goto err2;

        CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
        if (!iterator) goto err1;

        if (SLSWindowIteratorGetCount(iterator) == 1) {
            if (SLSWindowIteratorAdvance(iterator)) {
                level = SLSWindowIteratorGetLevel(iterator);
            }
        }

        CFRelease(iterator);
    err1:
        CFRelease(query);
    err2:
        CFRelease(window_ref);
    } else {
        SLSGetWindowLevel(g_connection, wid, &level);
    }

    return level;
}

static uint32_t window_sub_level_message_id(void)
{
    return workspace_is_macos_tahoe_or_newer() ? 0x76E3 : 0x73C3;
}

static int SLSGetWindowSubLevel__Internal(int cid, uint32_t wid)
{
    #pragma pack(push,4)
    struct {
        mach_msg_header_t header;
        NDR_record_t NDR_record;
        uint32_t window_id;
        int32_t sub_level;
        int32_t padding1;
        int32_t padding2;
    } msg = {0};
    #pragma pack(pop)

    msg.NDR_record = NDR_record;
    msg.window_id = wid;
    msg.header.msgh_bits = 0x1513;
    msg.header.msgh_remote_port = CGSGetConnectionPortById(cid);
    msg.header.msgh_local_port = mig_get_special_reply_port();
    msg.header.msgh_id = window_sub_level_message_id();
    mach_msg(&msg.header, MACH_SEND_MSG|MACH_RCV_MSG, 0x24, 0x30, msg.header.msgh_local_port, 0, 0);

    return msg.sub_level;
}

int window_sub_level(uint32_t wid)
{
    if (CGSGetConnectionPortById) {
        return SLSGetWindowSubLevel__Internal(g_connection, wid);
    } else {
        return SLSGetWindowSubLevel(g_connection, wid);
    }
}

bool window_tags_with_status(uint32_t wid, uint64_t *tags)
{
    if (tags) *tags = 0;
    if (!wid || !tags) return false;

    bool success = false;
    CFArrayRef window_ref = cfarray_of_cfnumbers(&wid, sizeof(uint32_t), 1, kCFNumberSInt32Type);
    if (!window_ref) return false;

    CFTypeRef query = SLSWindowQueryWindows(g_connection, window_ref, 1);
    if (!query) goto out;

    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    if (!iterator) goto query_out;

    if (SLSWindowIteratorGetCount(iterator) == 1 && SLSWindowIteratorAdvance(iterator)) {
        *tags = SLSWindowIteratorGetTags(iterator);
        success = true;
    }

    CFRelease(iterator);
query_out:
    CFRelease(query);
out:
    CFRelease(window_ref);
    return success;
}

uint64_t window_tags(uint32_t wid)
{
    uint64_t tags = 0;
    window_tags_with_status(wid, &tags);
    return tags;
}

bool window_is_ordered_in(uint32_t wid, bool *known)
{
    if (known) *known = false;
    uint8_t ordered_in = 0;
    if (!wid || window_test_is_ordered_in(g_connection, wid, &ordered_in) != kCGErrorSuccess) return false;
    if (known) *known = true;
    return ordered_in != 0;
}

bool window_is_on_screen(uint32_t wid, bool *known)
{
    if (known) *known = false;
    if (!wid) return false;

    bool on_screen = false;
    CFArrayRef window_ref = cfarray_of_cfnumbers(&wid, sizeof(uint32_t), 1, kCFNumberSInt32Type);
    if (!window_ref) return false;

    CFArrayRef descriptions = window_test_description_from_array(window_ref);
    if (!cf_is_type(descriptions, CFArrayGetTypeID()) || CFArrayGetCount(descriptions) != 1) goto out;

    CFTypeRef description = CFArrayGetValueAtIndex(descriptions, 0);
    if (!cf_is_type(description, CFDictionaryGetTypeID())) goto out;

    CFTypeRef value = CFDictionaryGetValue(description, kCGWindowIsOnscreen);
    if (!cf_is_type(value, CFBooleanGetTypeID())) goto out;
    on_screen = CFBooleanGetValue(value);
    if (known) *known = true;

out:
    if (descriptions) CFRelease(descriptions);
    CFRelease(window_ref);
    return on_screen;
}

CFStringRef window_ax_role(struct window *window)
{
    const void *role = NULL;
    AXUIElementCopyAttributeValue(window->ref, kAXRoleAttribute, &role);
    return role;
}

CFStringRef window_role(struct window *window)
{
    return window->role;
}

char *window_role_ts(struct window *window)
{
    CFStringRef role = window_role(window);
    if (!role) return ts_string_copy("");

    char *result = ts_cfstring_copy(role);
    return result;
}

CFStringRef window_ax_subrole(struct window *window)
{
    const void *srole = NULL;
    AXUIElementCopyAttributeValue(window->ref, kAXSubroleAttribute, &srole);
    return srole;
}

CFStringRef window_subrole(struct window *window)
{
    return window->subrole;
}

char *window_subrole_ts(struct window *window)
{
    CFStringRef subrole = window_subrole(window);
    if (!subrole) return ts_string_copy("");

    char *result = ts_cfstring_copy(subrole);
    return result;
}

bool window_is_real(struct window *window)
{
    bool win = false;
    CFStringRef role  = NULL;
    CFStringRef srole = NULL;

    if (!(role  = window_role(window)))    goto out;
    if (!(srole = window_subrole(window))) goto out;

    win = CFEqual(role, kAXWindowRole) &&
          (CFEqual(srole, kAXStandardWindowSubrole) ||
           CFEqual(srole, kAXFloatingWindowSubrole) ||
           CFEqual(srole, kAXDialogSubrole));

out:
    return win;
}

bool window_is_standard(struct window *window)
{
    bool standard_win = false;
    CFStringRef role  = NULL;
    CFStringRef srole = NULL;

    if (!(role  = window_role(window)))    goto out;
    if (!(srole = window_subrole(window))) goto out;

    standard_win = CFEqual(role, kAXWindowRole) &&
                   CFEqual(srole, kAXStandardWindowSubrole);

out:
    return standard_win;
}

bool window_level_is_standard(struct window *window)
{
    int level = window_level(window->id);
    return level == g_layer_normal_window_level;
}

static bool window_subrole_is_unknown(CFStringRef subrole)
{
    return subrole && CFEqual(subrole, kAXUnknownSubrole);
}

bool window_is_unknown(struct window *window)
{
    CFStringRef subrole = window_ax_subrole(window);
    if (!subrole) return false;

    bool result = window_subrole_is_unknown(subrole);
    CFRelease(subrole);
    return result;
}

struct window *window_create(struct application *application, AXUIElementRef window_ref, uint32_t window_id)
{
    if (!window_ref) return NULL;
    if (!application || !window_id) {
        CFRelease(window_ref);
        return NULL;
    }
    struct window *window = legacy_allocation_calloc(1, sizeof(struct window));
    if (!window) {
        CFRelease(window_ref);
        return NULL;
    }

    window->application = application;
    window->ref = window_ref;
    window->id = window_id;
    window->id_ptr = &window->id;

    const uint32_t requested = WINDOW_AX_POSITION | WINDOW_AX_SIZE | WINDOW_AX_ROLE |
                               WINDOW_AX_SUBROLE | WINDOW_AX_TITLE | WINDOW_AX_PARENT |
                               WINDOW_AX_MINIMIZED | WINDOW_AX_FULLSCREEN;
    struct window_ax_snapshot snapshot = {0};
    (void) window_ax_snapshot_read(window->ref, requested, &snapshot);

    CGRect frame;
    if (window_ax_snapshot_frame(&snapshot, &frame)) window->frame = frame;
    window->role = snapshot.role;
    snapshot.role = NULL;
    window->subrole = snapshot.subrole;
    snapshot.subrole = NULL;
    window->title = snapshot.title;
    snapshot.title = NULL;

    uint32_t parent_id = window_create_parent_id(window->id);
    bool ax_root = (snapshot.valid & WINDOW_AX_PARENT) &&
                   (!snapshot.parent || CFEqual(snapshot.parent, window->application->ref));
    window->is_root = !parent_id || ax_root;

    if (window_create_shadow(window->id)) {
        window_set_flag(window, WINDOW_SHADOW);
    }

    if ((snapshot.valid & WINDOW_AX_MINIMIZED) && snapshot.minimized) {
        window_set_flag(window, WINDOW_MINIMIZE);
    }

    if (window_create_can_move(window)) {
        window_set_flag(window, WINDOW_MOVABLE);
    }

    if (window_create_can_resize(window)) {
        window_set_flag(window, WINDOW_RESIZABLE);
    }

    bool ax_fullscreen = (snapshot.valid & WINDOW_AX_FULLSCREEN) && snapshot.fullscreen;
    if (ax_fullscreen || window_create_space_is_fullscreen(window_create_space_id(window->id))) {
        window_set_flag(window, WINDOW_FULLSCREEN);
    }

    if (window_create_sticky(window->id)) {
        window_set_flag(window, WINDOW_STICKY);
    }

    window_ax_snapshot_destroy(&snapshot);
    return window;
}

#ifdef TESTS
static void (*window_test_ref_release)(CFTypeRef);
#endif

void window_destroy(struct window *window)
{
    window->id = 0;
    if (window->role) CFRelease(window->role);
    if (window->subrole) CFRelease(window->subrole);
    if (window->title) CFRelease(window->title);
    if (window->ref) {
#ifdef TESTS
        if (window_test_ref_release) window_test_ref_release(window->ref);
        else CFRelease(window->ref);
#else
        CFRelease(window->ref);
#endif
    }
    legacy_allocation_free(window);
}
