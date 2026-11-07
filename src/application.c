extern struct event_loop g_event_loop;
extern struct window_manager g_window_manager;
extern volatile uint32_t __pending_window_focus;

#define APPLICATION_RESPONSIVENESS_STATE_CAPACITY 1024

struct application_responsiveness_state
{
    ProcessSerialNumber psn;
    uint64_t generation;
    bool occupied;
    bool pending;
    bool unresponsive;
};

static struct
{
    pthread_mutex_t lock;
    struct application_responsiveness_state entries[APPLICATION_RESPONSIVENESS_STATE_CAPACITY];
    uint64_t generation;
    uint32_t pending_count;
} application_responsiveness = {
    .lock = PTHREAD_MUTEX_INITIALIZER
};

static uint64_t application_responsiveness_next_generation(void)
{
    if (!++application_responsiveness.generation) ++application_responsiveness.generation;
    return application_responsiveness.generation;
}

bool application_responsiveness_record(ProcessSerialNumber psn, bool unresponsive)
{
    pthread_mutex_lock(&application_responsiveness.lock);
    struct application_responsiveness_state *available = NULL;
    for (size_t i = 0; i < APPLICATION_RESPONSIVENESS_STATE_CAPACITY; ++i) {
        struct application_responsiveness_state *entry = &application_responsiveness.entries[i];
        if (entry->occupied && psn_equals(&entry->psn, &psn)) {
            available = entry;
            break;
        }
        if (!entry->occupied && !available) available = entry;
    }
    if (available) {
        bool already_pending = available->occupied && available->pending;
        *available = (struct application_responsiveness_state) {
            .psn = psn,
            .generation = application_responsiveness_next_generation(),
            .occupied = true,
            .pending = true,
            .unresponsive = unresponsive
        };
        if (!already_pending) {
            __atomic_add_fetch(&application_responsiveness.pending_count, 1, __ATOMIC_RELEASE);
        }
    }
    pthread_mutex_unlock(&application_responsiveness.lock);
    return available != NULL;
}

size_t application_responsiveness_drain(void)
{
    if (!__atomic_load_n(&application_responsiveness.pending_count, __ATOMIC_ACQUIRE)) return 0;
    size_t applied = 0;
    for (size_t i = 0; i < APPLICATION_RESPONSIVENESS_STATE_CAPACITY; ++i) {
        pthread_mutex_lock(&application_responsiveness.lock);
        struct application_responsiveness_state snapshot = application_responsiveness.entries[i];
        pthread_mutex_unlock(&application_responsiveness.lock);
        if (!snapshot.occupied || !snapshot.pending) continue;

        pid_t pid = process_pid_for_psn(snapshot.psn);
        struct application *application = pid > 0
                                        ? window_manager_find_application(&g_window_manager, pid)
                                        : NULL;

        pthread_mutex_lock(&application_responsiveness.lock);
        struct application_responsiveness_state *entry = &application_responsiveness.entries[i];
        if (!entry->occupied || !entry->pending || entry->generation != snapshot.generation) {
            pthread_mutex_unlock(&application_responsiveness.lock);
            continue;
        }
        if (pid <= 0) {
            *entry = (struct application_responsiveness_state) {0};
            __atomic_sub_fetch(&application_responsiveness.pending_count, 1, __ATOMIC_RELEASE);
            pthread_mutex_unlock(&application_responsiveness.lock);
            continue;
        }
        if (!application) {
            pthread_mutex_unlock(&application_responsiveness.lock);
            continue;
        }
        *entry = (struct application_responsiveness_state) {0};
        __atomic_sub_fetch(&application_responsiveness.pending_count, 1, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&application_responsiveness.lock);

        if (application_is_unresponsive(application) != snapshot.unresponsive) {
            application_set_unresponsive(application, snapshot.unresponsive);
            debug("%s: %s (%d) is %s\n", __FUNCTION__,
                  application->name ? application->name : "<unknown>", application->pid,
                  snapshot.unresponsive ? "unresponsive" : "responsive");
            if (!snapshot.unresponsive) {
                application->ax_retry = true;
                if (!window_manager_request_application_refresh(&g_window_manager, application)) {
                    warn("%s: could not queue refresh for %s (%d)\n", __FUNCTION__,
                         application->name ? application->name : "<unknown>", application->pid);
                }
            }
        }
        ++applied;
    }
    return applied;
}

void application_responsiveness_reset(void)
{
    pthread_mutex_lock(&application_responsiveness.lock);
    memset(application_responsiveness.entries, 0, sizeof(application_responsiveness.entries));
    application_responsiveness.generation = 0;
    __atomic_store_n(&application_responsiveness.pending_count, 0, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&application_responsiveness.lock);
}

static void application_post_window_focused(uint32_t window_id)
{
    if (!event_loop_pending_reserve(&__pending_window_focus)) return;
    if (!event_loop_try_post(&g_event_loop, WINDOW_FOCUSED, (void *)(intptr_t) window_id, 1)) {
        (void) event_loop_pending_consume(&__pending_window_focus);
    }
}

static bool application_notification_is_frame_recovery(CFStringRef notification)
{
    return CFEqual(notification, kAXLayoutChangedNotification) ||
           CFEqual(notification, kAXValueChangedNotification);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static OBSERVER_CALLBACK(application_notification_handler)
{
    if (CFEqual(notification, kAXCreatedNotification)) {
        event_loop_post(&g_event_loop, WINDOW_CREATED, (void *) CFRetain(element), 0);
    } else if (CFEqual(notification, kAXFocusedWindowChangedNotification)) {
        application_post_window_focused(ax_window_id(element));
    } else if (CFEqual(notification, kAXWindowMovedNotification)) {
        event_loop_post(&g_event_loop, WINDOW_MOVED, (void *)(intptr_t) ax_window_id(element), 0);
    } else if (CFEqual(notification, kAXWindowResizedNotification)) {
        event_loop_post(&g_event_loop, WINDOW_RESIZED, (void *)(intptr_t) ax_window_id(element), 0);
    } else if (CFEqual(notification, kAXTitleChangedNotification)) {
        event_loop_post(&g_event_loop, WINDOW_TITLE_CHANGED, (void *)(intptr_t) ax_window_id(element), 0);
    } else if (CFEqual(notification, kAXMenuOpenedNotification)) {
        event_loop_post(&g_event_loop, MENU_OPENED, (void *)(intptr_t)((struct application *) context)->pid, 0);
    } else if (CFEqual(notification, kAXMenuClosedNotification)) {
        event_loop_post(&g_event_loop, MENU_CLOSED, (void *)(intptr_t)((struct application *) context)->pid, 0);
    } else if (application_notification_is_frame_recovery(notification)) {
        event_loop_post_managed_window_correction_hint(observer, element);
    } else if (CFEqual(notification, kAXWindowMiniaturizedNotification)) {
        event_loop_post(&g_event_loop, WINDOW_MINIMIZED, (void *)(intptr_t) ax_window_id(element), 0);
    } else if (CFEqual(notification, kAXWindowDeminiaturizedNotification)) {
        event_loop_post(&g_event_loop, WINDOW_DEMINIMIZED, (void *)(intptr_t) ax_window_id(element), 0);
    } else if (CFEqual(notification, kAXUIElementDestroyedNotification)) {
        struct window *window = context;

        //
        // NOTE(asmvik): Flag events that are already queued, but not yet processed,
        // so that they will be ignored; the memory we allocated is still valid and will
        // be freed when this event is handled.
        //

        if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, NULL)) return;

        event_loop_post(&g_event_loop, WINDOW_DESTROYED, window, 0);
    }
}
#pragma clang diagnostic pop

static bool application_observation_should_retry(bool retryable_failure, bool permanent_failure)
{
    return retryable_failure && !permanent_failure;
}

static void application_observation_record_failure(AXError result, bool required,
                                                   bool *retryable_failure, bool *permanent_failure)
{
    if (!required) return;
    if (result == kAXErrorCannotComplete) {
        *retryable_failure = true;
    } else {
        *permanent_failure = true;
    }
}

static void application_observer_add_run_loop_source_direct(AXObserverRef observer)
{
    CFRunLoopAddSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(observer), kCFRunLoopDefaultMode);
}

#ifdef TESTS
static AXError (*application_observer_create)(pid_t, AXObserverCallback, AXObserverRef *) = AXObserverCreate;
static AXError (*application_observer_add_notification)(AXObserverRef, AXUIElementRef, CFStringRef, void *) = AXObserverAddNotification;
static AXError (*application_observer_remove_notification)(AXObserverRef, AXUIElementRef, CFStringRef) = AXObserverRemoveNotification;
static void (*application_observer_add_run_loop_source)(AXObserverRef) = application_observer_add_run_loop_source_direct;
static void (*application_observer_invalidate)(AXObserverRef);
static void (*application_observer_release)(AXObserverRef);
#else
#define application_observer_create AXObserverCreate
#define application_observer_add_notification AXObserverAddNotification
#define application_observer_remove_notification AXObserverRemoveNotification
#define application_observer_add_run_loop_source application_observer_add_run_loop_source_direct
#endif

bool application_observe_frame_recovery(struct application *application)
{
    if (!application || !application->is_observing || application_is_unresponsive(application)) return false;

    for (int i = AX_APPLICATION_LAYOUT_CHANGED_INDEX; i <= AX_APPLICATION_VALUE_CHANGED_INDEX; ++i) {
        uint16_t flag = (uint16_t) (1U << i);
        if ((application->notification | application->frame_recovery_unsupported) & flag) continue;

        AXError result = application_observer_add_notification(application->observer_ref, application->ref,
                                                               ax_application_notification[i], application);
        if (result == kAXErrorSuccess || result == kAXErrorNotificationAlreadyRegistered) {
            application->notification |= flag;
            application->frame_recovery_unobserve_retries = 0;
            application->frame_recovery_cleanup_retry_time = 0;
        } else {
            if (result != kAXErrorCannotComplete) application->frame_recovery_unsupported |= flag;
            debug("%s: error '%s' for application '%s' and notification '%s'\n",
                  __FUNCTION__, ax_error_str[-result], application->name, ax_application_notification_str[i]);
        }
    }

    uint16_t expected = AX_APPLICATION_FRAME_RECOVERY & ~application->frame_recovery_unsupported;
    return expected && (application->notification & expected) == expected;
}

bool application_unobserve_frame_recovery(struct application *application)
{
    if (!application) return true;

    for (int i = AX_APPLICATION_LAYOUT_CHANGED_INDEX; i <= AX_APPLICATION_VALUE_CHANGED_INDEX; ++i) {
        uint16_t flag = (uint16_t) (1U << i);
        if (!(application->notification & flag)) continue;

        AXError result = application_observer_remove_notification(application->observer_ref, application->ref,
                                                                  ax_application_notification[i]);
        if (result == kAXErrorSuccess || result == kAXErrorNotificationNotRegistered) {
            application->notification &= ~flag;
        } else {
            debug("%s: error '%s' for application '%s' and notification '%s'\n",
                  __FUNCTION__, ax_error_str[-result], application->name, ax_application_notification_str[i]);
        }
    }

    bool removed = (application->notification & AX_APPLICATION_FRAME_RECOVERY) == 0;
    if (removed) {
        application->frame_recovery_unobserve_retries = 0;
        application->frame_recovery_cleanup_retry_time = 0;
    }
    return removed;
}

bool application_observe(struct application *application)
{
    if (!application || application_is_unresponsive(application)) return false;
    bool retryable_failure = false;
    bool permanent_failure = false;
    application->ax_retry = false;

    AXError create_result = application_observer_create(application->pid, application_notification_handler, &application->observer_ref);
    if (create_result == kAXErrorSuccess) {
        for (int i = 0; i < array_count(ax_application_notification); ++i) {
            if (i == AX_APPLICATION_LAYOUT_CHANGED_INDEX || i == AX_APPLICATION_VALUE_CHANGED_INDEX) continue;
            AXError result = application_observer_add_notification(application->observer_ref, application->ref, ax_application_notification[i], application);
            if (result == kAXErrorSuccess || result == kAXErrorNotificationAlreadyRegistered) {
                application->notification |= 1 << i;
            } else {
                bool required = (AX_APPLICATION_ALL & (1 << i)) != 0;
                application_observation_record_failure(result, required, &retryable_failure, &permanent_failure);
                debug("%s: error '%s' for application '%s' and notification '%s'\n", __FUNCTION__, ax_error_str[-result], application->name, ax_application_notification_str[i]);
            }
        }

        application->is_observing = true;
        application_observer_add_run_loop_source(application->observer_ref);
    } else {
        application_observation_record_failure(create_result, true, &retryable_failure, &permanent_failure);
    }

    application->ax_retry = application_observation_should_retry(retryable_failure, permanent_failure);
    return (application->notification & AX_APPLICATION_ALL) == AX_APPLICATION_ALL;
}

void application_unobserve(struct application *application)
{
    if (application->is_observing) {
        for (int i = 0; i < array_count(ax_application_notification); ++i) {
            if (!(application->notification & (1 << i))) continue;

#ifdef TESTS
            if (application_observer_remove_notification) {
                application_observer_remove_notification(application->observer_ref, application->ref, ax_application_notification[i]);
            } else
#endif
            AXObserverRemoveNotification(application->observer_ref, application->ref, ax_application_notification[i]);
            application->notification &= ~(1 << i);
        }

        application->is_observing = false;
#ifdef TESTS
        if (application_observer_invalidate) application_observer_invalidate(application->observer_ref);
        else CFRunLoopSourceInvalidate(AXObserverGetRunLoopSource(application->observer_ref));
        if (application_observer_release) application_observer_release(application->observer_ref);
        else CFRelease(application->observer_ref);
#else
        CFRunLoopSourceInvalidate(AXObserverGetRunLoopSource(application->observer_ref));
        CFRelease(application->observer_ref);
#endif
    }
}

uint32_t application_focused_window(struct application *application)
{
    if (application_is_unresponsive(application)) return 0;
    CFTypeRef window_ref = NULL;
    AXUIElementCopyAttributeValue(application->ref, kAXFocusedWindowAttribute, &window_ref);
    if (!window_ref) return 0;

    uint32_t window_id = ax_window_id(window_ref);
    CFRelease(window_ref);

    return window_id;
}

bool application_is_frontmost(struct application *application)
{
    ProcessSerialNumber psn = {0};
    _SLPSGetFrontProcess(&psn);
    return psn_equals(&psn, &application->psn);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
bool application_is_hidden(struct application *application)
{
    return IsProcessVisible(&application->psn) == 0;
}
#pragma clang diagnostic pop

CF_RETURNS_RETAINED CFArrayRef application_window_list(struct application *application)
{
    if (application_is_unresponsive(application)) return NULL;
    CFTypeRef window_list_ref = NULL;
    AXUIElementCopyAttributeValue(application->ref, kAXWindowsAttribute, &window_list_ref);
    return window_list_ref;
}

#ifdef TESTS
static AXUIElementRef (*application_create_ax_element)(pid_t) = AXUIElementCreateApplication;
static bool (*application_create_is_hidden)(struct application *) = application_is_hidden;
static void (*application_create_connection)(struct application *);
static void (*application_destroy_observer)(struct application *);
#define APPLICATION_CREATE_AX_ELEMENT(pid) application_create_ax_element(pid)
#else
#define APPLICATION_CREATE_AX_ELEMENT(pid) AXUIElementCreateApplication(pid)
#endif

struct application *application_create(struct process *process)
{
    if (!process || process->pid <= 0) return NULL;
    struct application *application = legacy_allocation_calloc(1, sizeof(struct application));
    if (!application) return NULL;

    application->ref = APPLICATION_CREATE_AX_ELEMENT(process->pid);
    if (!application->ref) {
        legacy_allocation_free(application);
        return NULL;
    }
    application->psn = process->psn;
    application->pid = process->pid;
    application->name = process->name;
    application->initial_window_inventory = process_initial_window_inventory_load(process);
#ifdef TESTS
    application->is_hidden = application_create_is_hidden(application);
    if (application_create_connection) application_create_connection(application);
    else SLSGetConnectionIDForPSN(g_connection, &application->psn, &application->connection);
#else
    application->is_hidden = application_is_hidden(application);
    SLSGetConnectionIDForPSN(g_connection, &application->psn, &application->connection);
#endif

    return application;
}

void application_destroy(struct application *application)
{
    if (!application) return;
#ifdef TESTS
    if (application_destroy_observer) application_destroy_observer(application);
#endif
    if (application->ref) CFRelease(application->ref);
    legacy_allocation_free(application);
}
