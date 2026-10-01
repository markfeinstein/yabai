extern struct event_loop g_event_loop;
extern int g_connection;

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

        CGRect frame = CGDisplayBounds(did);
        fprintf(rsp, "\t\"frame\":{\n\t\t\"x\":%.4f,\n\t\t\"y\":%.4f,\n\t\t\"w\":%.4f,\n\t\t\"h\":%.4f\n\t}", frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
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

CGRect display_bounds_constrained(uint32_t did, bool ignore_external_bar)
{
    CGRect frame = CGDisplayBounds(did);
    int effective_ext_top_padding = 0;

    if (!ignore_external_bar) {
        if ((g_display_manager.mode == EXTERNAL_BAR_MAIN &&
             did == display_manager_main_display_id()) ||
            (g_display_manager.mode == EXTERNAL_BAR_ALL)) {
            effective_ext_top_padding = g_display_manager.top_padding;

            frame.origin.y    += effective_ext_top_padding;
            frame.size.height -= effective_ext_top_padding;
            frame.size.height -= g_display_manager.bottom_padding;
        }
    }

    if (display_manager_menu_bar_hidden()) {
        int notch_height = workspace_display_notch_height(did);
        if (notch_height > effective_ext_top_padding) {
             frame.origin.y    += (notch_height - effective_ext_top_padding);
             frame.size.height -= (notch_height - effective_ext_top_padding);
        }
    } else {
        CGRect menu = display_manager_menu_bar_rect(did);
        frame.origin.y    += menu.size.height;
        frame.size.height -= menu.size.height;
    }

    if (!display_manager_dock_hidden()) {
        if (did == display_manager_dock_display_id()) {
            CGRect dock = display_manager_dock_rect();
            switch (display_manager_dock_orientation()) {
            case DOCK_ORIENTATION_LEFT: {
                frame.origin.x   += dock.size.width;
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
    }

    return frame;
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
