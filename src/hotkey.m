extern struct event_loop g_event_loop;

#define HOTKEY_SYNTHETIC_EVENT_TAG UINT64_C(0x79616261696b6579)

struct hotkey_action_job
{
    size_t count;
    struct hotkey_action actions[HOTKEY_MAX_ACTIONS];
};

struct hotkey_manager_state
{
    pthread_mutex_t lock;
    const struct hotkey_config_model *config;
    int active_mode;
    char front_bundle_id[DECLARATIVE_CONFIG_MAX_STRING_BYTES + 1];
    char front_name[DECLARATIVE_CONFIG_MAX_STRING_BYTES + 1];
    CFMachPortRef event_tap;
    CFRunLoopSourceRef runloop_source;
    bool lifecycle_started;
};

static struct hotkey_manager_state hotkey_manager = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .active_mode = HOTKEY_TRANSITION_DEFAULT
};

static uint8_t hotkey_modifiers_from_event(CGEventFlags flags)
{
    uint8_t modifiers = 0;
    if (flags & kCGEventFlagMaskCommand) modifiers |= HOTKEY_MODIFIER_COMMAND;
    if (flags & kCGEventFlagMaskControl) modifiers |= HOTKEY_MODIFIER_CONTROL;
    if (flags & kCGEventFlagMaskAlternate) modifiers |= HOTKEY_MODIFIER_OPTION;
    if (flags & kCGEventFlagMaskShift) modifiers |= HOTKEY_MODIFIER_SHIFT;
    return modifiers;
}

static CGEventFlags hotkey_event_flags(uint8_t modifiers)
{
    CGEventFlags flags = 0;
    if (modifiers & HOTKEY_MODIFIER_COMMAND) flags |= kCGEventFlagMaskCommand;
    if (modifiers & HOTKEY_MODIFIER_CONTROL) flags |= kCGEventFlagMaskControl;
    if (modifiers & HOTKEY_MODIFIER_OPTION) flags |= kCGEventFlagMaskAlternate;
    if (modifiers & HOTKEY_MODIFIER_SHIFT) flags |= kCGEventFlagMaskShift;
    return flags;
}

static bool hotkey_application_matches(const struct hotkey_application_condition *condition,
                                       const char *bundle_id, const char *name)
{
    if (!condition->bundle_id && !condition->name) return true;
    if (condition->bundle_id && (!bundle_id || strcmp(condition->bundle_id, bundle_id))) return false;
    if (condition->name && (!name || strcmp(condition->name, name))) return false;
    return true;
}

static bool hotkey_match(const struct hotkey_config_model *config, int active_mode,
                         uint16_t keycode, uint8_t modifiers,
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

    for (size_t i = 0; i < binding_count; ++i) {
        const struct hotkey_binding *binding = &bindings[i];
        if (binding->keystroke.keycode != keycode || binding->keystroke.modifiers != modifiers) continue;
        for (size_t j = 0; j < binding->case_count; ++j) {
            const struct hotkey_binding_case *item = &binding->cases[j];
            if (!hotkey_application_matches(&item->application, bundle_id, name)) continue;
            result->matched = true;
            result->disposition = item->disposition;
            result->actions = item->actions;
            result->action_count = item->action_count;
            result->transition_mode = item->transition_mode;
            return true;
        }
        return false;
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
                       uint16_t keycode, uint8_t modifiers,
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

static void hotkey_manager_remove_event_tap(void)
{
    if (hotkey_manager.runloop_source) {
        CFRunLoopRemoveSource(CFRunLoopGetMain(), hotkey_manager.runloop_source, kCFRunLoopCommonModes);
        CFRunLoopSourceInvalidate(hotkey_manager.runloop_source);
        CFRelease(hotkey_manager.runloop_source);
        hotkey_manager.runloop_source = NULL;
    }
    if (hotkey_manager.event_tap) {
        CGEventTapEnable(hotkey_manager.event_tap, false);
        CFMachPortInvalidate(hotkey_manager.event_tap);
        CFRelease(hotkey_manager.event_tap);
        hotkey_manager.event_tap = NULL;
    }
}

static CGEventRef hotkey_event_callback(CGEventTapProxy proxy, CGEventType type,
                                        CGEventRef event, void *context)
{
    (void)proxy;
    (void)context;
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (hotkey_manager.event_tap) CGEventTapEnable(hotkey_manager.event_tap, true);
        return event;
    }
    if (type != kCGEventKeyDown && type != kCGEventKeyUp) return event;
    if ((uint64_t)CGEventGetIntegerValueField(event, kCGEventSourceUserData) == HOTKEY_SYNTHETIC_EVENT_TAG)
        return event;

    uint16_t keycode = (uint16_t)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
    bool repeat = type == kCGEventKeyDown &&
                  CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat) != 0;
    uint8_t modifiers = hotkey_modifiers_from_event(CGEventGetFlags(event));
    struct hotkey_match_result match;
    struct hotkey_action_job *job = NULL;
    bool binding_repeat = false;

    pthread_mutex_lock(&hotkey_manager.lock);
    const struct hotkey_config_model *config = hotkey_manager.config;
    int active_mode = hotkey_manager.active_mode;
    bool matched = hotkey_match(config, active_mode, keycode, modifiers,
                                hotkey_manager.front_bundle_id[0] ? hotkey_manager.front_bundle_id : NULL,
                                hotkey_manager.front_name[0] ? hotkey_manager.front_name : NULL,
                                &match);
    if (matched) {
        const struct hotkey_binding *bindings = config->bindings;
        size_t binding_count = config->binding_count;
        if (active_mode >= 0 && (size_t)active_mode < config->mode_count) {
            bindings = config->modes[active_mode].bindings;
            binding_count = config->modes[active_mode].binding_count;
        }
        for (size_t i = 0; i < binding_count; ++i) {
            if (bindings[i].keystroke.keycode == keycode && bindings[i].keystroke.modifiers == modifiers) {
                binding_repeat = bindings[i].repeat;
                break;
            }
        }
        bool key_down = type == kCGEventKeyDown;
        if (key_down && !repeat && match.transition_mode != HOTKEY_TRANSITION_NONE)
            hotkey_manager.active_mode = match.transition_mode;
        if (key_down && (!repeat || binding_repeat) && match.action_count) {
            job = malloc(sizeof(*job));
            if (job) {
                job->count = match.action_count;
                memcpy(job->actions, match.actions, match.action_count * sizeof(*job->actions));
            }
        }
    }
    pthread_mutex_unlock(&hotkey_manager.lock);

    if (job) {
#ifdef TESTS
        (void)hotkey_test_post_action(job);
#else
        /* event_loop_try_post consumes the context on every outcome. */
        (void)event_loop_try_post(&g_event_loop, HOTKEY_ACTION, job, 0);
#endif
    }
    if (!matched) return match.capture_unbound ? NULL : event;
    return match.disposition == HOTKEY_DISPOSITION_PASS_THROUGH ? event : NULL;
}

static bool hotkey_manager_add_event_tap(void)
{
    CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp);
    hotkey_manager.event_tap = CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap,
                                                kCGEventTapOptionDefault, mask,
                                                hotkey_event_callback, NULL);
    if (!hotkey_manager.event_tap) return false;
    hotkey_manager.runloop_source = CFMachPortCreateRunLoopSource(NULL, hotkey_manager.event_tap, 0);
    if (!hotkey_manager.runloop_source) {
        hotkey_manager_remove_event_tap();
        return false;
    }
    CFRunLoopAddSource(CFRunLoopGetMain(), hotkey_manager.runloop_source, kCFRunLoopCommonModes);
    CGEventTapEnable(hotkey_manager.event_tap, true);
    if (!CGEventTapIsEnabled(hotkey_manager.event_tap)) {
        hotkey_manager_remove_event_tap();
        return false;
    }
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
    if (!started || !enabled) {
        hotkey_manager_remove_event_tap();
        return true;
    }
    if (hotkey_manager.event_tap) return true;
    return hotkey_manager_add_event_tap();
}

void hotkey_manager_install_config(const struct hotkey_config_model *config)
{
    pthread_mutex_lock(&hotkey_manager.lock);
    hotkey_manager.config = config;
    hotkey_manager.active_mode = HOTKEY_TRANSITION_DEFAULT;
    bool started = hotkey_manager.lifecycle_started;
    pthread_mutex_unlock(&hotkey_manager.lock);
    if (started) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!hotkey_manager_reconcile_event_tap()) {
                warn("yabai: could not update the keyboard event tap; verify Accessibility permission and secure-input state\n");
            }
        });
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
    hotkey_manager_remove_event_tap();
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

static EVENT_HANDLER(HOTKEY_ACTION)
{
    (void)param1;
    struct hotkey_action_job *job = context;
    if (!job) return;
    hotkey_action_execute_sequence(job->actions, job->count);
    hotkey_action_job_destroy(job);
}
