#ifndef WORKSPACE_H
#define WORKSPACE_H

#define SUPPORTED_MACOS_VERSION_LIST          \
    SUPPORT_MACOS_VERSION(golden_gate, 27)    \
    SUPPORT_MACOS_VERSION(tahoe,       26)

#define WORKSPACE_MACOS_LATEST_KNOWN_MAJOR 27

// Future releases remain distinct from the latest named release. Compatibility
// helpers below opt them into established newer-system behavior deliberately.
static long _workspace_macos_major_version;

#define SUPPORT_MACOS_VERSION(name, major_version) \
static inline bool workspace_is_macos_##name(void) \
{ \
    return _workspace_macos_major_version == major_version; \
}
    SUPPORTED_MACOS_VERSION_LIST
#undef SUPPORT_MACOS_VERSION

static inline long workspace_macos_major_version(void)
{
    return _workspace_macos_major_version;
}

static inline bool workspace_is_macos_future(void)
{
    return _workspace_macos_major_version > WORKSPACE_MACOS_LATEST_KNOWN_MAJOR;
}

static inline void workspace_set_macos_version(long running_major_version)
{
    _workspace_macos_major_version = running_major_version;
}

@interface workspace_context : NSObject {
}
- (id)init;
@end

struct process;
#ifdef __OBJC__
NSRunningApplication *workspace_application_create_running_ns_application(struct process *process) NS_RETURNS_RETAINED;
#else
void *workspace_application_create_running_ns_application(struct process *process);
#endif
void workspace_application_destroy_running_ns_application(void *context, struct process *process);
void workspace_application_unobserve(void *context, struct process *process);
bool workspace_application_is_observable(struct process *process);
bool workspace_application_is_finished_launching(struct process *process);
void workspace_application_observe_finished_launching(void *context, struct process *process);
void workspace_application_observe_activation_policy(void *context, struct process *process);
int workspace_display_notch_height(uint32_t did);
CGRect workspace_display_visible_frame_from_bounds(CGRect display_bounds, CGRect screen_frame, CGRect visible_frame);
CGRect workspace_display_visible_frame(uint32_t did, CGRect display_bounds);
pid_t workspace_get_dock_pid(void);
bool workspace_event_handler_begin(void **context);

#endif
