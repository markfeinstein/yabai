extern struct event_loop g_event_loop;
extern int g_connection;

static CGRect display_bounds_platform_display_bounds(uint32_t did);
static CGRect display_bounds_platform_visible_frame(uint32_t did);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static DISPLAY_EVENT_HANDLER(display_handler)
{
    if (flags & kCGDisplayAddFlag) {
        event_loop_post(&g_event_loop, DISPLAY_ADDED, (void *)(intptr_t) did, 0);
    } else if (flags & kCGDisplayRemoveFlag) {
        event_loop_post(&g_event_loop, DISPLAY_REMOVED, (void *)(intptr_t) did, 0);
    } else if (flags & kCGDisplayMovedFlag) {
        event_loop_post(&g_event_loop, DISPLAY_MOVED, (void *)(intptr_t) did, 0);
    } else if (flags & kCGDisplayDesktopShapeChangedFlag) {
        event_loop_post(&g_event_loop, DISPLAY_RESIZED, (void *)(intptr_t) did, 0);
    }
}
#pragma clang diagnostic pop

void display_serialize(FILE *rsp, uint32_t did, uint64_t flags)
{
    TIME_FUNCTION;

    if (flags == 0x0) flags |= ~flags;

    bool did_output = false;
    fprintf(rsp, "{\n");

    if (flags & DISPLAY_PROPERTY_ID) {
        fprintf(rsp, "\t\"id\":%d", did);
        did_output = true;
    }

    if (flags & DISPLAY_PROPERTY_UUID) {
        if (did_output) fprintf(rsp, ",\n");

        char *uuid = NULL;
        CFStringRef uuid_ref = display_uuid(did);
        if (uuid_ref) {
            uuid = ts_cfstring_copy(uuid_ref);
            CFRelease(uuid_ref);
        }

        fprintf(rsp, "\t\"uuid\":\"%s\"", uuid ? uuid : "<unknown>");
        did_output = true;
    }

    if (flags & DISPLAY_PROPERTY_INDEX) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"index\":%d", display_manager_display_id_arrangement(did));
        did_output = true;
    }

    if (flags & DISPLAY_PROPERTY_LABEL) {
        if (did_output) fprintf(rsp, ",\n");

        struct display_label *display_label = display_manager_get_label_for_display(&g_display_manager, did);
        char *label = display_label ? display_label->label : NULL;
        char *escaped_label = label ? ts_string_escape(label) : NULL;
        fprintf(rsp, "\t\"label\":\"%s\"", escaped_label ? escaped_label : label ? label : "");
        did_output = true;
    }

    if (flags & DISPLAY_PROPERTY_FRAME) {
        if (did_output) fprintf(rsp, ",\n");

        CGRect frame = display_bounds_platform_display_bounds(did);
        fprintf(rsp, "\t\"frame\":{\n\t\t\"x\":%.4f,\n\t\t\"y\":%.4f,\n\t\t\"w\":%.4f,\n\t\t\"h\":%.4f\n\t}", frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
        did_output = true;
    }

    if (flags & DISPLAY_PROPERTY_VISIBLE_FRAME) {
        if (did_output) fprintf(rsp, ",\n");

        CGRect frame = display_bounds_platform_visible_frame(did);
        fprintf(rsp, "\t\"visible-frame\":{\n\t\t\"x\":%.4f,\n\t\t\"y\":%.4f,\n\t\t\"w\":%.4f,\n\t\t\"h\":%.4f\n\t}", frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
        did_output = true;
    }

    if (flags & DISPLAY_PROPERTY_SPACES) {
        if (did_output) fprintf(rsp, ",\n");

        int count;
        uint64_t *space_list = display_space_list(did, &count);

        fprintf(rsp, "\t\"spaces\":[");
        if (space_list && count > 0) {
            int first_mci = space_manager_mission_control_index(space_list[0]);
            for (int i = 0; i < count; ++i) {
                if (i < count - 1) {
                    fprintf(rsp, "%d, ", first_mci + i);
                } else {
                    fprintf(rsp, "%d", first_mci + i);
                }
            }
        }
        fprintf(rsp, "]");
        did_output = true;
    }

    if (flags & DISPLAY_PROPERTY_HAS_FOCUS) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-focus\":%s", json_bool(did == g_display_manager.current_display_id));
    }

    fprintf(rsp, "\n}");
}

inline CFStringRef display_uuid(uint32_t did)
{
    CFUUIDRef uuid_ref = CGDisplayCreateUUIDFromDisplayID(did);
    if (!uuid_ref) return NULL;

    CFStringRef uuid_str = CFUUIDCreateString(NULL, uuid_ref);
    CFRelease(uuid_ref);

    return uuid_str;
}

inline uint32_t display_id(CFStringRef uuid)
{
    if (!cf_is_type(uuid, CFStringGetTypeID())) return 0;
    CFUUIDRef uuid_ref = CFUUIDCreateFromString(NULL, uuid);
    if (!uuid_ref) return 0;

    uint32_t did = CGDisplayGetDisplayIDFromUUID(uuid_ref);
    CFRelease(uuid_ref);

    return did;
}

static CGRect display_bounds_apply_constraints(CGRect frame, uint32_t did, uint32_t main_did,
                                                int external_bar_mode,
                                                int external_top_padding, int external_bottom_padding,
                                                bool menu_bar_hidden, int notch_height, CGRect menu,
                                                bool dock_hidden, bool dock_display, int dock_orientation,
                                                CGRect dock)
{
    int effective_ext_top_padding = 0;
    bool external_bar_applies = (external_bar_mode == EXTERNAL_BAR_MAIN && did == main_did) ||
                                external_bar_mode == EXTERNAL_BAR_ALL;
    if (external_bar_applies) {
        effective_ext_top_padding = external_top_padding;
        frame.origin.y += effective_ext_top_padding;
        frame.size.height -= effective_ext_top_padding;
        frame.size.height -= external_bottom_padding;
    }

    if (menu_bar_hidden) {
        if (notch_height > effective_ext_top_padding) {
            CGFloat notch_adjustment = (CGFloat) notch_height - (CGFloat) effective_ext_top_padding;
            frame.origin.y += notch_adjustment;
            frame.size.height -= notch_adjustment;
        }
    } else {
        frame.origin.y += menu.size.height;
        frame.size.height -= menu.size.height;
    }

    if (!dock_hidden && dock_display) {
        switch (dock_orientation) {
        case DOCK_ORIENTATION_LEFT: {
            frame.origin.x += dock.size.width;
            frame.size.width -= dock.size.width;
        } break;
        case DOCK_ORIENTATION_RIGHT: {
            frame.size.width -= dock.size.width;
        } break;
        case DOCK_ORIENTATION_BOTTOM: {
            frame.size.height -= dock.size.height;
        } break;
        }
    }
    return frame;
}

#ifdef TESTS
struct display_bounds_platform_ops
{
    CGRect (*display_bounds)(uint32_t did, void *context);
    CGRect (*visible_frame)(uint32_t did, CGRect display_bounds, void *context);
    uint32_t (*main_display_id)(void *context);
    bool (*menu_bar_hidden)(void *context);
    int (*notch_height)(uint32_t did, void *context);
    CGRect (*menu_bar_rect)(uint32_t did, void *context);
    bool (*dock_hidden)(void *context);
    uint32_t (*dock_display_id)(void *context);
    int (*dock_orientation)(void *context);
    CGRect (*dock_rect)(void *context);
    void *context;
};
static struct display_bounds_platform_ops display_bounds_test_ops;
#endif

static CGRect display_bounds_platform_display_bounds(uint32_t did)
{
#ifdef TESTS
    if (display_bounds_test_ops.display_bounds) return display_bounds_test_ops.display_bounds(did, display_bounds_test_ops.context);
#endif
    return CGDisplayBounds(did);
}

static CGRect display_bounds_platform_visible_frame(uint32_t did)
{
    CGRect display_bounds = display_bounds_platform_display_bounds(did);
#ifdef TESTS
    if (display_bounds_test_ops.visible_frame) return display_bounds_test_ops.visible_frame(did, display_bounds, display_bounds_test_ops.context);
#endif
    return workspace_display_visible_frame(did, display_bounds);
}

static uint32_t display_bounds_platform_main_display_id(void)
{
#ifdef TESTS
    if (display_bounds_test_ops.main_display_id) return display_bounds_test_ops.main_display_id(display_bounds_test_ops.context);
#endif
    return display_manager_main_display_id();
}

static bool display_bounds_platform_menu_bar_hidden(void)
{
#ifdef TESTS
    if (display_bounds_test_ops.menu_bar_hidden) return display_bounds_test_ops.menu_bar_hidden(display_bounds_test_ops.context);
#endif
    return display_manager_menu_bar_hidden();
}

static int display_bounds_platform_notch_height(uint32_t did)
{
#ifdef TESTS
    if (display_bounds_test_ops.notch_height) return display_bounds_test_ops.notch_height(did, display_bounds_test_ops.context);
#endif
    return workspace_display_notch_height(did);
}

static CGRect display_bounds_platform_menu_bar_rect(uint32_t did)
{
#ifdef TESTS
    if (display_bounds_test_ops.menu_bar_rect) return display_bounds_test_ops.menu_bar_rect(did, display_bounds_test_ops.context);
#endif
    return display_manager_menu_bar_rect(did);
}

static bool display_bounds_platform_dock_hidden(void)
{
#ifdef TESTS
    if (display_bounds_test_ops.dock_hidden) return display_bounds_test_ops.dock_hidden(display_bounds_test_ops.context);
#endif
    return display_manager_dock_hidden();
}

static uint32_t display_bounds_platform_dock_display_id(void)
{
#ifdef TESTS
    if (display_bounds_test_ops.dock_display_id) return display_bounds_test_ops.dock_display_id(display_bounds_test_ops.context);
#endif
    return display_manager_dock_display_id();
}

static int display_bounds_platform_dock_orientation(void)
{
#ifdef TESTS
    if (display_bounds_test_ops.dock_orientation) return display_bounds_test_ops.dock_orientation(display_bounds_test_ops.context);
#endif
    return display_manager_dock_orientation();
}

static CGRect display_bounds_platform_dock_rect(void)
{
#ifdef TESTS
    if (display_bounds_test_ops.dock_rect) return display_bounds_test_ops.dock_rect(display_bounds_test_ops.context);
#endif
    return display_manager_dock_rect();
}

CGRect display_bounds_constrained_with_external_bar(uint32_t did, int mode, int top_padding, int bottom_padding)
{
    uint32_t main_did = mode == EXTERNAL_BAR_MAIN ? display_bounds_platform_main_display_id() : 0;
    bool menu_bar_hidden = display_bounds_platform_menu_bar_hidden();
    bool dock_hidden = display_bounds_platform_dock_hidden();
    bool dock_display = !dock_hidden && did == display_bounds_platform_dock_display_id();
    return display_bounds_apply_constraints(display_bounds_platform_display_bounds(did), did, main_did, mode,
                                            top_padding, bottom_padding,
                                            menu_bar_hidden,
                                            menu_bar_hidden ? display_bounds_platform_notch_height(did) : 0,
                                            menu_bar_hidden ? CGRectZero : display_bounds_platform_menu_bar_rect(did),
                                            dock_hidden, dock_display,
                                            dock_display ? display_bounds_platform_dock_orientation() : 0,
                                            dock_display ? display_bounds_platform_dock_rect() : CGRectZero);
}

CGRect display_bounds_constrained(uint32_t did, bool ignore_external_bar)
{
    return display_bounds_constrained_with_external_bar(did,
                                                        ignore_external_bar ? EXTERNAL_BAR_OFF : g_display_manager.mode,
                                                        g_display_manager.top_padding,
                                                        g_display_manager.bottom_padding);
}

CGPoint display_center(uint32_t did)
{
    CGRect bounds = CGDisplayBounds(did);
    return (CGPoint) { bounds.origin.x + bounds.size.width/2, bounds.origin.y + bounds.size.height/2 };
}

inline uint64_t display_space_id(uint32_t did)
{
    CFStringRef uuid = display_uuid(did);
    if (!uuid) return 0;

    uint64_t sid = SLSManagedDisplayGetCurrentSpace(g_connection, uuid);
    CFRelease(uuid);

    return sid;
}

int display_space_count(uint32_t did)
{
    int result = 0;
    CFStringRef uuid = display_uuid(did);
    if (!uuid) return 0;

    CFArrayRef topology = SLSCopyManagedDisplaySpaces(g_connection);
    if (topology) {
        CFArrayRef spaces = managed_space_spaces_for_display(topology, uuid);
        managed_space_count_valid_checked(spaces, &result);
        CFRelease(topology);
    }
    CFRelease(uuid);
    return result;
}

static uint64_t *display_space_list_from_spaces(CFArrayRef spaces, int *count)
{
    if (count) *count = 0;
    if (!count || !cf_is_type(spaces, CFArrayGetTypeID())) return NULL;

    int valid_count = 0;
    if (!managed_space_count_valid_checked(spaces, &valid_count)) return NULL;
    uint64_t *result = ts_alloc_list(uint64_t, valid_count);
    if (result) *count = managed_space_copy_valid(spaces, result);
    return result;
}

uint64_t *display_space_list(uint32_t did, int *count)
{
    if (count) *count = 0;
    if (!count) return NULL;

    uint64_t *result = NULL;
    CFStringRef uuid = display_uuid(did);
    if (!uuid) return NULL;

    CFArrayRef topology = SLSCopyManagedDisplaySpaces(g_connection);
    if (topology) {
        CFArrayRef spaces = managed_space_spaces_for_display(topology, uuid);
        result = display_space_list_from_spaces(spaces, count);
        CFRelease(topology);
    }
    CFRelease(uuid);
    return result;
}
