extern struct event_loop g_event_loop;

#define HOTKEY_SYNTHETIC_EVENT_TAG UINT64_C(0x79616261696b6579)

struct hotkey_action_job
{
    size_t count;
    struct hotkey_action actions[HOTKEY_MAX_ACTIONS];
};

struct hotkey_event_context
{
    CFMachPortRef event_tap;
};

struct hotkey_manager_resources
{
    CFMachPortRef event_tap;
    CFRunLoopSourceRef runloop_source;
    CFRunLoopRef runloop;
    pthread_t thread;
    bool thread_started;
    struct hotkey_event_context *context;
};

struct hotkey_thread_start
{
    struct hotkey_manager_resources *resources;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool ready;
    bool live;
};

enum hotkey_delivery
{
    HOTKEY_DELIVERY_UNTRACKED,
    HOTKEY_DELIVERY_PASS,
    HOTKEY_DELIVERY_CONSUME
};

struct hotkey_manager_state
{
    pthread_mutex_t lock;
    pthread_mutex_t lifecycle_lock;
    const struct hotkey_config_model *config;
    int active_mode;
    char front_bundle_id[DECLARATIVE_CONFIG_MAX_STRING_BYTES + 1];
    char front_name[DECLARATIVE_CONFIG_MAX_STRING_BYTES + 1];
    struct hotkey_manager_resources resources;
    uint64_t mode_generation;
    uint8_t delivery[UINT16_MAX + 1u];
    bool lifecycle_started;
};

static struct hotkey_manager_state hotkey_manager = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .lifecycle_lock = PTHREAD_MUTEX_INITIALIZER,
    .active_mode = HOTKEY_TRANSITION_DEFAULT
};

#ifdef TESTS
static bool (*hotkey_key_state)(CGKeyCode) = NULL;
#endif

static bool hotkey_modifier_key_is_down(CGKeyCode keycode)
{
#ifdef TESTS
    if (hotkey_key_state) return hotkey_key_state(keycode);
#endif
    return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, keycode);
}

static uint16_t hotkey_modifiers_from_event(CGEventFlags flags)
{
    uint16_t modifiers = 0;
    if (flags & kCGEventFlagMaskCommand) {
        modifiers |= HOTKEY_MODIFIER_COMMAND;
        if (hotkey_modifier_key_is_down(kVK_Command)) modifiers |= HOTKEY_MODIFIER_LEFT_COMMAND;
        if (hotkey_modifier_key_is_down(kVK_RightCommand)) modifiers |= HOTKEY_MODIFIER_RIGHT_COMMAND;
    }
    if (flags & kCGEventFlagMaskControl) {
        modifiers |= HOTKEY_MODIFIER_CONTROL;
        if (hotkey_modifier_key_is_down(kVK_Control)) modifiers |= HOTKEY_MODIFIER_LEFT_CONTROL;
        if (hotkey_modifier_key_is_down(kVK_RightControl)) modifiers |= HOTKEY_MODIFIER_RIGHT_CONTROL;
    }
    if (flags & kCGEventFlagMaskAlternate) {
        modifiers |= HOTKEY_MODIFIER_OPTION;
        if (hotkey_modifier_key_is_down(kVK_Option)) modifiers |= HOTKEY_MODIFIER_LEFT_OPTION;
        if (hotkey_modifier_key_is_down(kVK_RightOption)) modifiers |= HOTKEY_MODIFIER_RIGHT_OPTION;
    }
    if (flags & kCGEventFlagMaskShift) {
        modifiers |= HOTKEY_MODIFIER_SHIFT;
        if (hotkey_modifier_key_is_down(kVK_Shift)) modifiers |= HOTKEY_MODIFIER_LEFT_SHIFT;
        if (hotkey_modifier_key_is_down(kVK_RightShift)) modifiers |= HOTKEY_MODIFIER_RIGHT_SHIFT;
    }
    if (flags & kCGEventFlagMaskSecondaryFn) modifiers |= HOTKEY_MODIFIER_FN;
    return modifiers;
}

static CGEventFlags hotkey_event_flags(uint16_t modifiers)
{
    CGEventFlags flags = 0;
    if (modifiers & HOTKEY_MODIFIER_COMMAND) flags |= kCGEventFlagMaskCommand;
    if (modifiers & HOTKEY_MODIFIER_CONTROL) flags |= kCGEventFlagMaskControl;
    if (modifiers & HOTKEY_MODIFIER_OPTION) flags |= kCGEventFlagMaskAlternate;
    if (modifiers & HOTKEY_MODIFIER_SHIFT) flags |= kCGEventFlagMaskShift;
    if (modifiers & HOTKEY_MODIFIER_FN) flags |= kCGEventFlagMaskSecondaryFn;
    return flags;
}

static bool hotkey_modifiers_match(uint16_t required, uint16_t actual)
{
    if ((required & HOTKEY_MODIFIER_PRIMARY_MASK) != (actual & HOTKEY_MODIFIER_PRIMARY_MASK)) return false;
    uint16_t sided = required & HOTKEY_MODIFIER_SIDED_MASK;
    return (actual & sided) == sided;
}

static bool hotkey_keycode_has_implicit_fn(uint16_t keycode)
{
    switch (keycode) {
    case kVK_LeftArrow:
    case kVK_RightArrow:
    case kVK_DownArrow:
    case kVK_UpArrow:
    case kVK_Home:
    case kVK_End:
    case kVK_PageUp:
    case kVK_PageDown:
    case kVK_ForwardDelete:
        return true;
    default:
        return false;
    }
}

static bool hotkey_application_matches(const struct hotkey_application_condition *condition,
                                       const char *bundle_id, const char *name)
{
    if (!condition->bundle_id && !condition->name) return true;
    if (condition->bundle_id && (!bundle_id || strcmp(condition->bundle_id, bundle_id))) return false;
    if (condition->name && (!name || strcmp(condition->name, name))) return false;
    return true;
}

// Returns 1 for a matching case, 0 when a binding owns the keystroke but no
// application case matches, and -1 when no binding owns the keystroke.
static int hotkey_match_bindings(const struct hotkey_binding *bindings, size_t binding_count,
                                 uint16_t keycode, uint16_t modifiers,
                                 const char *bundle_id, const char *name,
                                 struct hotkey_match_result *result)
{
    for (size_t i = 0; i < binding_count; ++i) {
        const struct hotkey_binding *binding = &bindings[i];
        if (binding->keystroke.keycode != keycode ||
            !hotkey_modifiers_match(binding->keystroke.modifiers, modifiers)) continue;
        for (size_t j = 0; j < binding->case_count; ++j) {
            const struct hotkey_binding_case *item = &binding->cases[j];
            if (!hotkey_application_matches(&item->application, bundle_id, name)) continue;
            result->matched = true;
            result->repeat = binding->repeat;
            result->disposition = item->disposition;
            result->actions = item->actions;
            result->action_count = item->action_count;
            result->transition_mode = item->transition_mode;
            return 1;
        }
        return 0;
    }
    return -1;
}

static bool hotkey_match(const struct hotkey_config_model *config, int active_mode,
                         uint16_t keycode, uint16_t modifiers,
                         const char *bundle_id, const char *name,
                         struct hotkey_match_result *result)
{
    *result = (struct hotkey_match_result) {
        .transition_mode = HOTKEY_TRANSITION_NONE
    };
    if (!config || !config->present || !config->enabled) return false;

    const struct hotkey_binding *bindings = config->bindings;
    size_t binding_count = config->binding_count;
    if (active_mode >= 0 && (size_t)active_mode < config->mode_count) {
        const struct hotkey_input_mode *mode = &config->modes[active_mode];
        bindings = mode->bindings;
        binding_count = mode->binding_count;
        result->capture_unbound = mode->capture_unbound;
    }

    int exact = hotkey_match_bindings(bindings, binding_count, keycode, modifiers,
                                      bundle_id, name, result);
    if (exact >= 0) return exact == 1;

    // macOS annotates arrow and navigation events with SecondaryFn (and often
    // NumericPad) even when the physical Fn key is not pressed. Prefer an
    // explicitly authored Fn binding above; when none exists, retry without
    // that system-added flag so ordinary Ctrl+Arrow bindings remain portable.
    if ((modifiers & HOTKEY_MODIFIER_FN) && hotkey_keycode_has_implicit_fn(keycode)) {
        uint16_t normalized = modifiers & ~HOTKEY_MODIFIER_FN;
        return hotkey_match_bindings(bindings, binding_count, keycode, normalized,
                                     bundle_id, name, result) == 1;
    }
    return false;
}

#ifdef TESTS
static bool hotkey_post_action_production(void *context)
{
    return event_loop_try_post(&g_event_loop, HOTKEY_ACTION, context, 0);
}

static bool (*hotkey_test_post_action)(void *) = hotkey_post_action_production;

void hotkey_test_set_action_poster(bool (*poster)(void *))
{
    hotkey_test_post_action = poster ? poster : hotkey_post_action_production;
}

bool hotkey_test_match(const struct hotkey_config_model *config, int active_mode,
                       uint16_t keycode, uint16_t modifiers,
                       const char *bundle_id, const char *name,
                       struct hotkey_match_result *result)
{
    return hotkey_match(config, active_mode, keycode, modifiers, bundle_id, name, result);
}

int hotkey_test_active_mode(void)
{
    pthread_mutex_lock(&hotkey_manager.lock);
    int result = hotkey_manager.active_mode;
    pthread_mutex_unlock(&hotkey_manager.lock);
    return result;
}
#endif

bool hotkey_manager_copy_active_mode(char *buffer, size_t capacity)
{
    if (!buffer || !capacity) return false;
    pthread_mutex_lock(&hotkey_manager.lock);
    const char *name = "default";
    if (hotkey_manager.config && hotkey_manager.active_mode >= 0 &&
        (size_t)hotkey_manager.active_mode < hotkey_manager.config->mode_count) {
        name = hotkey_manager.config->modes[hotkey_manager.active_mode].name;
    }
    int length = snprintf(buffer, capacity, "%s", name ? name : "default");
    pthread_mutex_unlock(&hotkey_manager.lock);
    return length >= 0 && (size_t)length < capacity;
}

static void hotkey_manager_post_mode_changed(void)
{
    char *name = malloc(DECLARATIVE_CONFIG_MAX_KEY_BYTES + 1);
    if (!name) return;
    if (!hotkey_manager_copy_active_mode(name, DECLARATIVE_CONFIG_MAX_KEY_BYTES + 1)) {
        free(name);
        return;
    }
    (void)event_loop_try_post(&g_event_loop, HOTKEY_MODE_CHANGED, name, 0);
}

static void hotkey_manager_schedule_mode_timeout(uint64_t generation, uint32_t timeout_ms)
{
    if (!generation || !timeout_ms) return;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_ms * NSEC_PER_MSEC),
                   dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        bool changed = false;
        pthread_mutex_lock(&hotkey_manager.lock);
        if (hotkey_manager.mode_generation == generation && hotkey_manager.active_mode >= 0) {
            hotkey_manager.active_mode = HOTKEY_TRANSITION_DEFAULT;
            ++hotkey_manager.mode_generation;
            changed = true;
            debug("keyboard input mode timed out; returning to default\n");
        }
        pthread_mutex_unlock(&hotkey_manager.lock);
        if (changed) hotkey_manager_post_mode_changed();
    });
}

static void hotkey_manager_dispose_resources(struct hotkey_manager_resources resources)
{
    if (resources.runloop_source) {
        CFRunLoopSourceInvalidate(resources.runloop_source);
        CFRelease(resources.runloop_source);
    }
    if (resources.event_tap) {
        CGEventTapEnable(resources.event_tap, false);
        CFMachPortInvalidate(resources.event_tap);
        CFRelease(resources.event_tap);
    }
    free(resources.context);
}

static void hotkey_manager_stop_resources(struct hotkey_manager_resources *resources)
{
    if (resources->event_tap) CGEventTapEnable(resources->event_tap, false);
    if (resources->runloop && resources->runloop_source) {
        CFRunLoopRemoveSource(resources->runloop, resources->runloop_source, kCFRunLoopCommonModes);
        CFRunLoopStop(resources->runloop);
        CFRunLoopWakeUp(resources->runloop);
    }
    if (resources->thread_started) pthread_join(resources->thread, NULL);
    resources->runloop = NULL;
    resources->thread = (pthread_t) {0};
    resources->thread_started = false;
}

static void hotkey_manager_remove_event_tap_locked(void)
{
    struct hotkey_manager_resources resources = hotkey_manager.resources;
    hotkey_manager.resources = (struct hotkey_manager_resources) {0};
    hotkey_manager_stop_resources(&resources);
    hotkey_manager_dispose_resources(resources);

    pthread_mutex_lock(&hotkey_manager.lock);
    memset(hotkey_manager.delivery, HOTKEY_DELIVERY_UNTRACKED, sizeof(hotkey_manager.delivery));
    pthread_mutex_unlock(&hotkey_manager.lock);
}

static CGEventRef hotkey_event_callback(CGEventTapProxy proxy, CGEventType type,
                                        CGEventRef event, void *context)
{
    (void)proxy;
    struct hotkey_event_context *event_context = context;
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (event_context && event_context->event_tap) CGEventTapEnable(event_context->event_tap, true);
        return event;
    }
    if (type != kCGEventKeyDown && type != kCGEventKeyUp) return event;
    if ((uint64_t)CGEventGetIntegerValueField(event, kCGEventSourceUserData) == HOTKEY_SYNTHETIC_EVENT_TAG)
        return event;

    uint16_t keycode = (uint16_t)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
    bool key_down = type == kCGEventKeyDown;
    bool repeat = key_down && CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat) != 0;
    uint16_t modifiers = hotkey_modifiers_from_event(CGEventGetFlags(event));
    struct hotkey_match_result match;
    struct hotkey_action_job *job = NULL;
    uint64_t timeout_generation = 0;
    uint32_t timeout_ms = 0;
    bool mode_changed = false;

    pthread_mutex_lock(&hotkey_manager.lock);
    const struct hotkey_config_model *config = hotkey_manager.config;
    int active_mode = hotkey_manager.active_mode;
    bool matched = hotkey_match(config, active_mode, keycode, modifiers,
                                hotkey_manager.front_bundle_id[0] ? hotkey_manager.front_bundle_id : NULL,
                                hotkey_manager.front_name[0] ? hotkey_manager.front_name : NULL,
                                &match);
    if (matched) {
        if (key_down && !repeat && match.transition_mode != HOTKEY_TRANSITION_NONE &&
            hotkey_manager.active_mode != match.transition_mode) {
            hotkey_manager.active_mode = match.transition_mode;
            timeout_generation = ++hotkey_manager.mode_generation;
            mode_changed = true;
            if (match.transition_mode >= 0 &&
                (size_t)match.transition_mode < config->mode_count) {
                timeout_ms = config->modes[match.transition_mode].timeout_ms;
            }
        }
        if (key_down && (!repeat || match.repeat) && match.action_count) {
            job = malloc(sizeof(*job));
            if (job) {
                job->count = match.action_count;
                memcpy(job->actions, match.actions, match.action_count * sizeof(*job->actions));
            }
        }
    }
    if (key_down && !repeat && !mode_changed && config && hotkey_manager.active_mode >= 0 &&
        (matched || match.capture_unbound) &&
        (size_t)hotkey_manager.active_mode < config->mode_count &&
        config->modes[hotkey_manager.active_mode].timeout_ms) {
        timeout_generation = ++hotkey_manager.mode_generation;
        timeout_ms = config->modes[hotkey_manager.active_mode].timeout_ms;
    }

    bool pass_event = !matched ? !match.capture_unbound
                               : match.disposition == HOTKEY_DISPOSITION_PASS_THROUGH;
    enum hotkey_delivery delivery = hotkey_manager.delivery[keycode];
    if (key_down) {
        if (!repeat || delivery == HOTKEY_DELIVERY_UNTRACKED) {
            delivery = pass_event ? HOTKEY_DELIVERY_PASS : HOTKEY_DELIVERY_CONSUME;
            hotkey_manager.delivery[keycode] = delivery;
        }
        pass_event = delivery == HOTKEY_DELIVERY_PASS;
    } else if (delivery != HOTKEY_DELIVERY_UNTRACKED) {
        pass_event = delivery == HOTKEY_DELIVERY_PASS;
        hotkey_manager.delivery[keycode] = HOTKEY_DELIVERY_UNTRACKED;
    }
    pthread_mutex_unlock(&hotkey_manager.lock);
    hotkey_manager_schedule_mode_timeout(timeout_generation, timeout_ms);
    if (mode_changed) hotkey_manager_post_mode_changed();

    if (job) {
#ifdef TESTS
        (void)hotkey_test_post_action(job);
#else
        /* event_loop_try_post consumes the context on every outcome. */
        (void)event_loop_try_post(&g_event_loop, HOTKEY_ACTION, job, 0);
#endif
    }
    return pass_event ? event : NULL;
}

static bool hotkey_manager_create_resources(struct hotkey_manager_resources *resources)
{
    resources->context = calloc(1, sizeof(*resources->context));
    if (!resources->context) return false;

    CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp);
    resources->event_tap = CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap,
                                            kCGEventTapOptionDefault, mask,
                                            hotkey_event_callback, resources->context);
    if (!resources->event_tap) {
        hotkey_manager_dispose_resources(*resources);
        *resources = (struct hotkey_manager_resources) {0};
        return false;
    }
    resources->context->event_tap = resources->event_tap;
    if (!CGEventTapIsEnabled(resources->event_tap)) {
        hotkey_manager_dispose_resources(*resources);
        *resources = (struct hotkey_manager_resources) {0};
        return false;
    }
    CGEventTapEnable(resources->event_tap, false);

    resources->runloop_source = CFMachPortCreateRunLoopSource(NULL, resources->event_tap, 0);
    if (!resources->runloop_source) {
        hotkey_manager_dispose_resources(*resources);
        *resources = (struct hotkey_manager_resources) {0};
        return false;
    }
    return true;
}

static void hotkey_manager_signal_thread_start(struct hotkey_thread_start *start,
                                               CFRunLoopRef runloop, bool live)
{
    pthread_mutex_lock(&start->mutex);
    start->resources->runloop = runloop;
    start->live = live;
    start->ready = true;
    pthread_cond_signal(&start->cond);
    pthread_mutex_unlock(&start->mutex);
}

static void *hotkey_manager_thread_proc(void *context)
{
    pthread_setname_np("yabai-hotkeys");
    struct hotkey_thread_start *start = context;
    struct hotkey_manager_resources *resources = start->resources;
    CFRunLoopRef runloop = CFRunLoopGetCurrent();
    CFRetain(runloop);
    CFRunLoopAddSource(runloop, resources->runloop_source, kCFRunLoopCommonModes);
    bool live = CFRunLoopContainsSource(runloop, resources->runloop_source, kCFRunLoopCommonModes);
    if (!live) {
        CFRunLoopRemoveSource(runloop, resources->runloop_source, kCFRunLoopCommonModes);
        CFRelease(runloop);
        hotkey_manager_signal_thread_start(start, NULL, false);
        return NULL;
    }
    hotkey_manager_signal_thread_start(start, runloop, true);
    CFRunLoopRun();
    CFRelease(runloop);
    return NULL;
}

static bool hotkey_manager_start_resources(struct hotkey_manager_resources *resources)
{
    struct hotkey_thread_start start = {.resources = resources};
    if (pthread_mutex_init(&start.mutex, NULL) != 0) return false;
    if (pthread_cond_init(&start.cond, NULL) != 0) {
        pthread_mutex_destroy(&start.mutex);
        return false;
    }
    if (pthread_create(&resources->thread, NULL, hotkey_manager_thread_proc, &start) != 0) {
        pthread_cond_destroy(&start.cond);
        pthread_mutex_destroy(&start.mutex);
        return false;
    }
    pthread_mutex_lock(&start.mutex);
    while (!start.ready) pthread_cond_wait(&start.cond, &start.mutex);
    bool live = start.live;
    pthread_mutex_unlock(&start.mutex);
    pthread_cond_destroy(&start.cond);
    pthread_mutex_destroy(&start.mutex);
    if (!live) {
        pthread_join(resources->thread, NULL);
        resources->thread = (pthread_t) {0};
        return false;
    }
    resources->thread_started = true;
    return true;
}

static bool hotkey_manager_add_event_tap_locked(void)
{
    struct hotkey_manager_resources resources = {0};
    if (!hotkey_manager_create_resources(&resources) ||
        !hotkey_manager_start_resources(&resources)) {
        hotkey_manager_stop_resources(&resources);
        hotkey_manager_dispose_resources(resources);
        return false;
    }
    CGEventTapEnable(resources.event_tap, true);
    if (!CGEventTapIsEnabled(resources.event_tap)) {
        hotkey_manager_stop_resources(&resources);
        hotkey_manager_dispose_resources(resources);
        return false;
    }
    hotkey_manager.resources = resources;
    return true;
}

static bool hotkey_manager_reconcile_event_tap(void)
{
    bool enabled;
    bool started;
    pthread_mutex_lock(&hotkey_manager.lock);
    enabled = hotkey_manager.config && hotkey_manager.config->present && hotkey_manager.config->enabled;
    started = hotkey_manager.lifecycle_started;
    pthread_mutex_unlock(&hotkey_manager.lock);

    pthread_mutex_lock(&hotkey_manager.lifecycle_lock);
    bool result = true;
    if (!started || !enabled) {
        hotkey_manager_remove_event_tap_locked();
    } else if (!hotkey_manager.resources.event_tap) {
        result = hotkey_manager_add_event_tap_locked();
    }
    pthread_mutex_unlock(&hotkey_manager.lifecycle_lock);
    return result;
}

void hotkey_manager_install_config(const struct hotkey_config_model *config)
{
    pthread_mutex_lock(&hotkey_manager.lock);
    bool mode_changed = hotkey_manager.active_mode != HOTKEY_TRANSITION_DEFAULT;
    hotkey_manager.config = config;
    hotkey_manager.active_mode = HOTKEY_TRANSITION_DEFAULT;
    ++hotkey_manager.mode_generation;
    bool started = hotkey_manager.lifecycle_started;
    pthread_mutex_unlock(&hotkey_manager.lock);
    if (mode_changed) hotkey_manager_post_mode_changed();
    if (started && !hotkey_manager_reconcile_event_tap()) {
        warn("yabai: could not update the keyboard event tap; verify Accessibility permission and secure-input state\n");
    }
}

static void hotkey_manager_set_frontmost(const char *bundle_id, const char *name)
{
    pthread_mutex_lock(&hotkey_manager.lock);
    snprintf(hotkey_manager.front_bundle_id, sizeof(hotkey_manager.front_bundle_id), "%s", bundle_id ? bundle_id : "");
    snprintf(hotkey_manager.front_name, sizeof(hotkey_manager.front_name), "%s", name ? name : "");
    pthread_mutex_unlock(&hotkey_manager.lock);
}

void hotkey_manager_update_frontmost_application(pid_t pid, const char *name)
{
    @autoreleasepool {
        NSRunningApplication *application = pid > 0 ?
            [NSRunningApplication runningApplicationWithProcessIdentifier:pid] : nil;
        NSString *bundle = [application bundleIdentifier];
        NSString *display_name = name ? @(name) : [application localizedName];
        hotkey_manager_set_frontmost(bundle ? [bundle UTF8String] : NULL,
                                     display_name ? [display_name UTF8String] : NULL);
    }
}

bool hotkey_manager_begin(void)
{
    pthread_mutex_lock(&hotkey_manager.lock);
    hotkey_manager.lifecycle_started = true;
    pthread_mutex_unlock(&hotkey_manager.lock);
    NSRunningApplication *front = [[NSWorkspace sharedWorkspace] frontmostApplication];
    hotkey_manager_update_frontmost_application([front processIdentifier], [[front localizedName] UTF8String]);
    return hotkey_manager_reconcile_event_tap();
}

void hotkey_manager_end(void)
{
    pthread_mutex_lock(&hotkey_manager.lock);
    hotkey_manager.lifecycle_started = false;
    pthread_mutex_unlock(&hotkey_manager.lock);
    (void)hotkey_manager_reconcile_event_tap();
}

bool hotkey_post_synthetic_keystroke(struct hotkey_keystroke keystroke)
{
    CGEventRef down = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)keystroke.keycode, true);
    CGEventRef up = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)keystroke.keycode, false);
    if (!down || !up) {
        if (down) CFRelease(down);
        if (up) CFRelease(up);
        return false;
    }
    CGEventFlags flags = hotkey_event_flags(keystroke.modifiers);
    CGEventSetFlags(down, flags);
    CGEventSetFlags(up, flags);
    CGEventSetIntegerValueField(down, kCGEventSourceUserData, (int64_t)HOTKEY_SYNTHETIC_EVENT_TAG);
    CGEventSetIntegerValueField(up, kCGEventSourceUserData, (int64_t)HOTKEY_SYNTHETIC_EVENT_TAG);
    CGEventPost(kCGHIDEventTap, down);
    CGEventPost(kCGHIDEventTap, up);
    CFRelease(down);
    CFRelease(up);
    return true;
}

void hotkey_action_job_destroy(void *context)
{
    free(context);
}

static EVENT_HANDLER(HOTKEY_MODE_CHANGED)
{
    (void)param1;
    char *name = context;
    event_signal_push(SIGNAL_KEYBOARD_MODE_CHANGED, name);
    menu_bar_keyboard_mode_changed(name);
    free(name);
}

static EVENT_HANDLER(HOTKEY_ACTION)
{
    (void)param1;
    struct hotkey_action_job *job = context;
    if (!job) return;
    hotkey_action_execute_sequence(job->actions, job->count);
    hotkey_action_job_destroy(job);
}
