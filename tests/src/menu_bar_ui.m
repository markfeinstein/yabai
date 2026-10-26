#define YB_MENU_BAR_TEST 1
#include "../../src/menu_bar.m"

#include <stdio.h>
#include <stdlib.h>

struct menu_bar_ui_action_log
{
    size_t count;
    enum menu_bar_action_type type[16];
    uint64_t generation[16];
    uint64_t space_id[16];
    bool is_tracking[16];
    uint32_t focused_display_id[16];
};

static void
menu_bar_ui_record_action(const struct menu_bar_action *action, void *context)
{
    struct menu_bar_ui_action_log *log = context;
    if (!log || !action || log->count >= 16) return;

    size_t index = log->count++;
    log->type[index] = action->type;
    log->generation[index] = action->generation;
    log->space_id[index] = action->type == MENU_BAR_ACTION_FOCUS_SPACE
                         ? action->data.focus_space.space_id
                         : 0;
    log->is_tracking[index] = action->type == MENU_BAR_ACTION_MENU_TRACKING_CHANGED
                            ? action->data.menu_tracking.is_tracking
                            : false;
    log->focused_display_id[index] = action->type == MENU_BAR_ACTION_MENU_TRACKING_CHANGED
                                   ? action->data.menu_tracking.focused_display_id
                                   : 0;
}

static struct menu_bar_callbacks
menu_bar_ui_callbacks(struct menu_bar_ui_action_log *log)
{
    struct menu_bar_callbacks callbacks = {0};
    callbacks.action = menu_bar_ui_record_action;
    callbacks.context = log;
    return callbacks;
}

static struct menu_bar_snapshot *
menu_bar_ui_snapshot(uint64_t generation, enum menu_bar_enabled enabled)
{
    struct menu_bar_settings settings = {0};
    settings.enabled = enabled;
    settings.style = MENU_BAR_STYLE_COMPACT;
    settings.space_filter = MENU_BAR_SPACE_FILTER_OCCUPIED;

    struct menu_bar_snapshot *snapshot = menu_bar_model_snapshot_create(generation,
                                                                        settings,
                                                                        NULL,
                                                                        0,
                                                                        false,
                                                                        0,
                                                                        NULL,
                                                                        0,
                                                                        NULL);
    if (!snapshot) {
        fprintf(stderr, "failed to allocate test snapshot generation %llu\n", (unsigned long long) generation);
        abort();
    }
    return snapshot;
}

static struct menu_bar_snapshot *
menu_bar_ui_detailed_snapshot(uint64_t generation)
{
    struct menu_bar_app_summary_input apps[] = {
        { "Editor", 2 }
    };
    struct menu_bar_space_input spaces[] = {
        { 42, 1, "work", false, true, MENU_BAR_SPACE_MEMBERSHIP_OCCUPIED, apps, 1 },
        { 84, 2, NULL, false, false, MENU_BAR_SPACE_MEMBERSHIP_UNKNOWN, NULL, 0 }
    };
    struct menu_bar_display_input displays[] = {
        { 1, "display-1", 0, "Main", NULL, spaces, 2 }
    };
    struct menu_bar_settings settings = {
        .enabled = MENU_BAR_ENABLED,
        .style = MENU_BAR_STYLE_EXPANDED,
        .space_filter = MENU_BAR_SPACE_FILTER_OCCUPIED
    };

    struct menu_bar_snapshot *snapshot = menu_bar_model_snapshot_create(generation,
                                                                        settings,
                                                                        displays,
                                                                        1,
                                                                        true,
                                                                        1,
                                                                        "display-1",
                                                                        0,
                                                                        NULL);
    if (!snapshot) {
        fprintf(stderr, "failed to allocate detailed test snapshot generation %llu\n", (unsigned long long) generation);
        abort();
    }
    return snapshot;
}

static struct menu_bar_snapshot *
menu_bar_ui_default_layout_snapshot(uint64_t generation)
{
    struct menu_bar_app_summary_input editor[] = {
        { "Editor", 2 }
    };
    struct menu_bar_app_summary_input terminal[] = {
        { "Terminal", 1 }
    };
    struct menu_bar_space_input first_spaces[] = {
        { 10, 1, "empty", false, false, MENU_BAR_SPACE_MEMBERSHIP_EMPTY, NULL, 0 },
        { 11, 2, "visible", false, true, MENU_BAR_SPACE_MEMBERSHIP_EMPTY, NULL, 0 },
        { 12, 3, "work", false, false, MENU_BAR_SPACE_MEMBERSHIP_OCCUPIED, editor, 1 }
    };
    struct menu_bar_space_input second_spaces[] = {
        { 20, 4, "chat", false, false, MENU_BAR_SPACE_MEMBERSHIP_OCCUPIED, terminal, 1 },
        { 21, 5, "focus", false, true, MENU_BAR_SPACE_MEMBERSHIP_EMPTY, NULL, 0 },
        { 22, 6, "empty", false, false, MENU_BAR_SPACE_MEMBERSHIP_EMPTY, NULL, 0 }
    };
    struct menu_bar_display_input displays[] = {
        { 1, "display-1", 0, "Left", NULL, first_spaces, 3 },
        { 2, "display-2", 1, "Right", NULL, second_spaces, 3 }
    };
    struct menu_bar_settings settings = {
        .enabled = MENU_BAR_ENABLED,
        .style = MENU_BAR_STYLE_EXPANDED,
        .space_filter = MENU_BAR_SPACE_FILTER_OCCUPIED
    };

    struct menu_bar_snapshot *snapshot = menu_bar_model_snapshot_create(generation, settings, displays, 2,
                                                                        true, 2, "display-2", 0, NULL);
    if (!snapshot) abort();
    return snapshot;
}

static struct menu_bar_snapshot *
menu_bar_ui_overflow_snapshot(uint64_t generation, size_t active_index)
{
    struct menu_bar_space_input spaces[40] = {0};
    for (size_t i = 0; i < 40; ++i) {
        spaces[i].space_id = i + 1;
        spaces[i].mission_control_index = (uint32_t) i + 101;
        spaces[i].membership = MENU_BAR_SPACE_MEMBERSHIP_OCCUPIED;
        spaces[i].is_visible = i == active_index;
    }
    struct menu_bar_display_input displays[] = {
        { 1, "display-1", 0, NULL, NULL, spaces, 40 }
    };
    struct menu_bar_settings settings = {
        .enabled = MENU_BAR_ENABLED,
        .style = MENU_BAR_STYLE_EXPANDED,
        .space_filter = MENU_BAR_SPACE_FILTER_OCCUPIED
    };
    struct menu_bar_snapshot *snapshot = menu_bar_model_snapshot_create(generation, settings, displays, 1,
                                                                        true, 1, "display-1", 0, NULL);
    if (!snapshot) abort();
    return snapshot;
}

#define TEST_CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #expr); \
        return false; \
    } \
} while (0)

@interface YBMenuBarSynchronousCloseMenu : NSMenu
@end

@implementation YBMenuBarSynchronousCloseMenu
- (void)cancelTracking
{
    id delegate = [self delegate];
    [super cancelTracking];
    if ([delegate respondsToSelector:@selector(menuDidClose:)]) {
        [delegate menuDidClose:self];
    }
}
@end

struct menu_bar_ui_bitmap_metrics
{
    NSUInteger painted_pixel_count;
    NSUInteger interior_sample_count;
    NSUInteger interior_painted_pixel_count;
    NSUInteger expected_text_painted_pixel_count;
    NSUInteger unexpected_interior_painted_pixel_count;
    NSUInteger inner_perimeter_painted_pixel_count;
    NSUInteger outline_painted_pixel_count;
    CGFloat alpha_sum;
    CGFloat interior_alpha_sum;
    CGFloat interior_max_alpha;
    CGFloat outline_alpha_sum;
    CGFloat outline_max_alpha;
    CGFloat maximum_channel_spread;
    NSUInteger colorized_pixel_count;
    NSUInteger interior_colorized_pixel_count;
    NSInteger interior_min_x;
    NSInteger interior_max_x;
    NSInteger interior_min_y;
    NSInteger interior_max_y;
};

static struct menu_bar_ui_bitmap_metrics
menu_bar_ui_render_screen(YBMenuBarScreenCell *cell,
                          NSAppearanceName appearance_name,
                          bool increased_contrast)
{
    struct menu_bar_ui_bitmap_metrics metrics = {0};
    NSInteger width = (NSInteger) NSWidth([cell bounds]);
    NSInteger height = (NSInteger) NSHeight([cell bounds]);
    metrics.interior_min_x = width;
    metrics.interior_min_y = height;
    metrics.interior_max_x = -1;
    metrics.interior_max_y = -1;

    NSRect screen = yb_screen_rect([cell bounds], cell->yb_index);
    NSRect interior = NSInsetRect(screen, YB_MENU_BAR_SCREEN_RADIUS, YB_MENU_BAR_SCREEN_RADIUS);
    NSRect inner_perimeter_edge = NSInsetRect(interior, 1.0, 1.0);
    NSString *index = yb_screen_index_text(cell->yb_index);
    NSRect expected_text = NSZeroRect;
    if (index) {
        NSSize text_size = [index sizeWithAttributes:@{ NSFontAttributeName: yb_screen_index_font() }];
        NSPoint text_origin = NSMakePoint(NSMidX(screen) - text_size.width * 0.5,
                                          NSMidY(screen) - text_size.height * 0.5 + 0.25);
        expected_text = NSInsetRect((NSRect) { text_origin, text_size }, -1.0, -1.0);
    }
    NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                      pixelsWide:width
                                                                      pixelsHigh:height
                                                                   bitsPerSample:8
                                                                 samplesPerPixel:4
                                                                        hasAlpha:YES
                                                                        isPlanar:NO
                                                                  colorSpaceName:NSCalibratedRGBColorSpace
                                                                     bitmapFormat:0
                                                                      bytesPerRow:0
                                                                     bitsPerPixel:0];
    NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
    NSAppearance *appearance = [NSAppearance appearanceNamed:appearance_name];
    [cell setAppearance:appearance];
    g_yb_menu_bar_contrast_override = increased_contrast ? 1 : 0;

    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:context];
    [[NSColor clearColor] setFill];
    NSRectFill([cell bounds]);
    [cell drawRect:[cell bounds]];
    [NSGraphicsContext restoreGraphicsState];

    for (NSInteger y = 0; y < height; ++y) {
        for (NSInteger x = 0; x < width; ++x) {
            NSColor *pixel = [[bitmap colorAtX:x y:y] colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
            CGFloat red = 0.0;
            CGFloat green = 0.0;
            CGFloat blue = 0.0;
            CGFloat alpha = 0.0;
            [pixel getRed:&red green:&green blue:&blue alpha:&alpha];
            NSPoint sample = NSMakePoint(x + 0.5, y + 0.5);
            CGFloat channel_spread = MAX(red, MAX(green, blue)) - MIN(red, MIN(green, blue));
            if (alpha >= 0.05) {
                ++metrics.painted_pixel_count;
                metrics.alpha_sum += alpha;
                metrics.maximum_channel_spread = MAX(metrics.maximum_channel_spread, channel_spread);
                if (channel_spread >= 0.08) ++metrics.colorized_pixel_count;
                if (!NSPointInRect(sample, interior)) {
                    ++metrics.outline_painted_pixel_count;
                    metrics.outline_alpha_sum += alpha;
                    metrics.outline_max_alpha = MAX(metrics.outline_max_alpha, alpha);
                }
            }

            if (NSPointInRect(sample, interior)) {
                ++metrics.interior_sample_count;
                if (alpha >= 0.05) {
                    bool in_expected_text = index && NSPointInRect(sample, expected_text);
                    ++metrics.interior_painted_pixel_count;
                    metrics.interior_alpha_sum += alpha;
                    metrics.interior_max_alpha = MAX(metrics.interior_max_alpha, alpha);
                    if (channel_spread >= 0.08) ++metrics.interior_colorized_pixel_count;
                    metrics.interior_min_x = MIN(metrics.interior_min_x, x);
                    metrics.interior_max_x = MAX(metrics.interior_max_x, x);
                    metrics.interior_min_y = MIN(metrics.interior_min_y, y);
                    metrics.interior_max_y = MAX(metrics.interior_max_y, y);
                    if (in_expected_text) {
                        ++metrics.expected_text_painted_pixel_count;
                    } else {
                        ++metrics.unexpected_interior_painted_pixel_count;
                        if (!NSPointInRect(sample, inner_perimeter_edge)) {
                            ++metrics.inner_perimeter_painted_pixel_count;
                        }
                    }
                }
            }
        }
    }

    g_yb_menu_bar_contrast_override = -1;
    [bitmap release];
    return metrics;
}

struct menu_bar_ui_strip_decoration_metrics
{
    CGFloat first_group_alpha;
    CGFloat second_group_alpha;
    CGFloat separator_alpha;
};

static CGFloat
menu_bar_ui_pixel_alpha(NSBitmapImageRep *bitmap, NSPoint point)
{
    NSColor *pixel = [[bitmap colorAtX:(NSInteger) floor(point.x)
                                     y:(NSInteger) floor(point.y)] colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    return [pixel alphaComponent];
}

static struct menu_bar_ui_strip_decoration_metrics
menu_bar_ui_render_strip_decorations(YBMenuBarStripView *strip,
                                     NSPoint first_group,
                                     NSPoint second_group,
                                     NSPoint separator)
{
    NSInteger width = (NSInteger) NSWidth([strip bounds]);
    NSInteger height = (NSInteger) NSHeight([strip bounds]);
    NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                      pixelsWide:width
                                                                      pixelsHigh:height
                                                                   bitsPerSample:8
                                                                 samplesPerPixel:4
                                                                        hasAlpha:YES
                                                                        isPlanar:NO
                                                                  colorSpaceName:NSCalibratedRGBColorSpace
                                                                     bitmapFormat:0
                                                                      bytesPerRow:0
                                                                     bitsPerPixel:0];
    NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
    [strip setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameAqua]];
    g_yb_menu_bar_contrast_override = 0;

    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:context];
    [[NSColor clearColor] setFill];
    NSRectFill([strip bounds]);
    [strip drawRect:[strip bounds]];
    [NSGraphicsContext restoreGraphicsState];

    struct menu_bar_ui_strip_decoration_metrics metrics = {
        .first_group_alpha = menu_bar_ui_pixel_alpha(bitmap, first_group),
        .second_group_alpha = menu_bar_ui_pixel_alpha(bitmap, second_group),
        .separator_alpha = menu_bar_ui_pixel_alpha(bitmap, separator)
    };
    g_yb_menu_bar_contrast_override = -1;
    [bitmap release];
    return metrics;
}

static size_t
menu_bar_ui_screen_selection(NSView *strip,
                             uint32_t *indexes,
                             size_t capacity,
                             size_t *omitted_before_out,
                             size_t *omitted_after_out)
{
    size_t count = 0;
    size_t omitted_before = 0;
    size_t omitted_after = 0;
    for (NSView *view in [strip subviews]) {
        if ([view isKindOfClass:[YBMenuBarScreenCell class]]) {
            if (count < capacity) indexes[count] = ((YBMenuBarScreenCell *) view)->yb_index;
            ++count;
        } else if ([view isKindOfClass:[YBMenuBarButton class]]) {
            NSString *title = [(YBMenuBarButton *) view title];
            if ([title hasPrefix:@"←+"]) {
                omitted_before = (size_t) [[title substringFromIndex:2] integerValue];
            } else if ([title hasPrefix:@"+"] && [title hasSuffix:@"→"] && [title length] > 2) {
                omitted_after = (size_t) [[title substringWithRange:NSMakeRange(1, [title length] - 2)] integerValue];
            }
        }
    }
    if (omitted_before_out) *omitted_before_out = omitted_before;
    if (omitted_after_out) *omitted_after_out = omitted_after;
    return count;
}

static bool
test_menu_tracking_captures_source_snapshot_focus(void)
{
    struct menu_bar_ui_action_log log = {0};
    struct menu_bar_callbacks callbacks = menu_bar_ui_callbacks(&log);
    YBMenuBarController *controller = [[YBMenuBarController alloc] initWithCallbacks:&callbacks];
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"test"];

    [controller testSetSnapshot:menu_bar_ui_detailed_snapshot(9)];
    [controller menuWillOpen:menu];
    TEST_CHECK(log.count == 1);
    TEST_CHECK(log.is_tracking[0] == true);
    TEST_CHECK(log.generation[0] == 9);
    TEST_CHECK(log.focused_display_id[0] == 1);
    [controller menuDidClose:menu];

    [controller testSetSnapshot:menu_bar_ui_snapshot(10, MENU_BAR_ENABLED)];
    [controller menuWillOpen:menu];
    TEST_CHECK(log.count == 3);
    TEST_CHECK(log.is_tracking[2] == true);
    TEST_CHECK(log.generation[2] == 10);
    TEST_CHECK(log.focused_display_id[2] == 0);
    [controller menuDidClose:menu];

    [menu release];
    [controller release];
    return true;
}

static bool
test_teardown_balances_tracking_without_cancel_close(void)
{
    struct menu_bar_ui_action_log log = {0};
    struct menu_bar_callbacks callbacks = menu_bar_ui_callbacks(&log);
    YBMenuBarController *controller = [[YBMenuBarController alloc] initWithCallbacks:&callbacks];
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"test"];

    [controller testSetSnapshot:menu_bar_ui_snapshot(10, MENU_BAR_ENABLED)];
    [controller menuWillOpen:menu];
    [controller clearStatusItem];

    TEST_CHECK(log.count == 2);
    TEST_CHECK(log.type[0] == MENU_BAR_ACTION_MENU_TRACKING_CHANGED);
    TEST_CHECK(log.is_tracking[0] == true);
    TEST_CHECK(log.generation[0] == 10);
    TEST_CHECK(log.type[1] == MENU_BAR_ACTION_MENU_TRACKING_CHANGED);
    TEST_CHECK(log.is_tracking[1] == false);
    TEST_CHECK(log.generation[1] == 10);
    TEST_CHECK(![controller testIsTrackingMenu]);
    TEST_CHECK([controller testTrackingGeneration] == 0);
    TEST_CHECK(![controller testHasSnapshot]);
    TEST_CHECK(![controller testHasDeferredSnapshot]);
    TEST_CHECK(![controller testHasStatusItem]);

    [menu release];
    [controller release];
    return true;
}

static bool
test_synchronous_and_late_close_are_suppressed_during_teardown(void)
{
    struct menu_bar_ui_action_log log = {0};
    struct menu_bar_callbacks callbacks = menu_bar_ui_callbacks(&log);
    YBMenuBarController *controller = [[YBMenuBarController alloc] initWithCallbacks:&callbacks];
    YBMenuBarSynchronousCloseMenu *menu = [[YBMenuBarSynchronousCloseMenu alloc] initWithTitle:@"test"];

    [controller testSetSnapshot:menu_bar_ui_snapshot(20, MENU_BAR_ENABLED)];
    [controller testSetMenu:menu];
    [controller menuWillOpen:menu];
    [controller receiveSnapshot:menu_bar_ui_snapshot(21, MENU_BAR_ENABLED) clear:false];
    TEST_CHECK([controller testHasDeferredSnapshot]);

    [controller clearStatusItem];
    [controller menuDidClose:menu];

    TEST_CHECK(log.count == 2);
    TEST_CHECK(log.is_tracking[0] == true);
    TEST_CHECK(log.generation[0] == 20);
    TEST_CHECK(log.is_tracking[1] == false);
    TEST_CHECK(log.generation[1] == 20);
    TEST_CHECK(![controller testIsTrackingMenu]);
    TEST_CHECK(![controller testHasSnapshot]);
    TEST_CHECK(![controller testHasDeferredSnapshot]);
    TEST_CHECK(![controller testHasStatusItem]);

    [menu release];
    [controller release];
    return true;
}

static bool
test_deferred_snapshot_is_disposed_when_teardown_has_no_menu(void)
{
    struct menu_bar_ui_action_log log = {0};
    struct menu_bar_callbacks callbacks = menu_bar_ui_callbacks(&log);
    YBMenuBarController *controller = [[YBMenuBarController alloc] initWithCallbacks:&callbacks];
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"test"];

    [controller testSetSnapshot:menu_bar_ui_snapshot(30, MENU_BAR_ENABLED)];
    [controller menuWillOpen:menu];
    [controller receiveSnapshot:menu_bar_ui_snapshot(31, MENU_BAR_ENABLED) clear:false];
    TEST_CHECK([controller testHasDeferredSnapshot]);

    [controller clearStatusItem];

    TEST_CHECK(log.count == 2);
    TEST_CHECK(log.is_tracking[0] == true);
    TEST_CHECK(log.generation[0] == 30);
    TEST_CHECK(log.is_tracking[1] == false);
    TEST_CHECK(log.generation[1] == 30);
    TEST_CHECK(![controller testIsTrackingMenu]);
    TEST_CHECK(![controller testHasSnapshot]);
    TEST_CHECK(![controller testHasDeferredSnapshot]);
    TEST_CHECK(![controller testHasStatusItem]);

    [menu release];
    [controller release];
    return true;
}

static bool
test_lifecycle_reinit_ignores_stale_delivery(void)
{
    struct menu_bar_ui_action_log log = {0};
    struct menu_bar_callbacks callbacks = menu_bar_ui_callbacks(&log);

    TEST_CHECK(menu_bar_init(&callbacks));
    uint64_t stale_epoch = g_menu_bar_lifecycle_epoch;
    menu_bar_publish_snapshot(menu_bar_ui_snapshot(40, MENU_BAR_DISABLED));
    TEST_CHECK(g_menu_bar_pending_has_value);
    menu_bar_shutdown();
    TEST_CHECK(!g_menu_bar_initialized);
    TEST_CHECK(!g_menu_bar_pending_has_value);

    TEST_CHECK(menu_bar_init(&callbacks));
    YBMenuBarController *controller = g_menu_bar_controller;
    TEST_CHECK(controller != nil);
    [controller retain];

    menu_bar_delivery_main((void *)(uintptr_t) stale_epoch);
    TEST_CHECK(g_menu_bar_initialized);
    TEST_CHECK(g_menu_bar_controller == controller);
    TEST_CHECK(![controller testHasStatusItem]);

    uint64_t current_epoch = g_menu_bar_lifecycle_epoch;
    menu_bar_publish_snapshot(menu_bar_ui_snapshot(41, MENU_BAR_DISABLED));
    menu_bar_delivery_main((void *)(uintptr_t) current_epoch);
    TEST_CHECK(g_menu_bar_initialized);
    TEST_CHECK(g_menu_bar_controller == controller);
    TEST_CHECK(!g_menu_bar_pending_has_value);
    TEST_CHECK(![controller testHasStatusItem]);
    TEST_CHECK(log.count == 0);

    [controller release];
    menu_bar_shutdown();
    return true;
}

static bool
test_space_rows_emit_focus_actions_and_settings_remain_interactive(void)
{
    struct menu_bar_ui_action_log log = {0};
    struct menu_bar_callbacks callbacks = menu_bar_ui_callbacks(&log);
    YBMenuBarController *controller = [[YBMenuBarController alloc] initWithCallbacks:&callbacks];

    [controller testSetSnapshot:menu_bar_ui_detailed_snapshot(60)];
    [controller testRebuildMenu];
    NSMenu *menu = [controller testMenu];

    TEST_CHECK(menu != nil);
    TEST_CHECK([menu numberOfItems] == 8);

    NSMenuItem *title = [menu itemAtIndex:0];
    TEST_CHECK([[title title] isEqualToString:@"yabai Spaces"]);
    TEST_CHECK([title action] == nil);
    TEST_CHECK([[title accessibilityRole] isEqualToString:NSAccessibilityStaticTextRole]);
    if (@available(macOS 14.0, *)) TEST_CHECK([title isSectionHeader]);

    NSMenuItem *display_header = [menu itemAtIndex:2];
    TEST_CHECK([[display_header title] containsString:@"Display 1"]);
    TEST_CHECK([[display_header title] containsString:@"Focused"]);
    TEST_CHECK([display_header action] == nil);
    TEST_CHECK([display_header submenu] == nil);
    TEST_CHECK([[display_header accessibilityRole] isEqualToString:NSAccessibilityStaticTextRole]);
    if (@available(macOS 14.0, *)) TEST_CHECK([display_header isSectionHeader]);

    NSMenuItem *visible_space = [menu itemAtIndex:3];
    NSMenuItem *unknown_space = [menu itemAtIndex:4];
    TEST_CHECK([visible_space isEnabled]);
    TEST_CHECK([visible_space action] == @selector(performMenuAction:));
    TEST_CHECK([[visible_space representedObject] isKindOfClass:[YBMenuBarActionTarget class]]);
    TEST_CHECK(((YBMenuBarActionTarget *) [visible_space representedObject])->yb_space_id == 42);
    TEST_CHECK([visible_space state] == NSControlStateValueOn);
    TEST_CHECK([visible_space indentationLevel] == 1);
    TEST_CHECK([unknown_space action] == @selector(performMenuAction:));
    TEST_CHECK(((YBMenuBarActionTarget *) [unknown_space representedObject])->yb_space_id == 84);
    TEST_CHECK([unknown_space state] == NSControlStateValueOff);
    TEST_CHECK([[unknown_space toolTip] containsString:@"Choose to focus this space"]);
    [controller performMenuAction:unknown_space];

    NSMenuItem *filter = [menu itemAtIndex:6];
    TEST_CHECK([filter action] == @selector(performMenuAction:));
    TEST_CHECK([[filter representedObject] isKindOfClass:[YBMenuBarActionTarget class]]);
    [controller performMenuAction:filter];

    NSMenu *style_menu = [[menu itemAtIndex:7] submenu];
    TEST_CHECK(style_menu != nil);
    TEST_CHECK([style_menu numberOfItems] == 3);
    NSMenuItem *automatic = [style_menu itemAtIndex:0];
    NSMenuItem *compact = [style_menu itemAtIndex:1];
    NSMenuItem *expanded = [style_menu itemAtIndex:2];
    TEST_CHECK([[automatic title] isEqualToString:@"Automatic"]);
    TEST_CHECK([automatic action] == @selector(performMenuAction:));
    TEST_CHECK([automatic state] == NSControlStateValueOff);
    TEST_CHECK([compact state] == NSControlStateValueOff);
    TEST_CHECK([expanded state] == NSControlStateValueOn);
    [controller performMenuAction:automatic];

    TEST_CHECK(log.count == 3);
    TEST_CHECK(log.type[0] == MENU_BAR_ACTION_FOCUS_SPACE);
    TEST_CHECK(log.generation[0] == 60);
    TEST_CHECK(log.space_id[0] == 84);
    TEST_CHECK(log.type[1] == MENU_BAR_ACTION_SET_SPACE_FILTER);
    TEST_CHECK(log.generation[1] == 60);
    TEST_CHECK(log.type[2] == MENU_BAR_ACTION_SET_STYLE);
    TEST_CHECK(log.generation[2] == 60);
    for (size_t i = 0; i < log.count; ++i) {
        TEST_CHECK(log.type[i] == MENU_BAR_ACTION_FOCUS_SPACE ||
                   log.type[i] == MENU_BAR_ACTION_SET_STYLE ||
                   log.type[i] == MENU_BAR_ACTION_SET_SPACE_FILTER ||
                   log.type[i] == MENU_BAR_ACTION_MENU_TRACKING_CHANGED);
    }
    TEST_CHECK(![controller testHasStatusItem]);

    [controller release];
    return true;
}

static bool
test_expanded_screen_cells_and_compact_option(void)
{
    struct menu_bar_settings defaults = {0};
    TEST_CHECK(defaults.style == MENU_BAR_STYLE_EXPANDED);
    TEST_CHECK(defaults.space_filter == MENU_BAR_SPACE_FILTER_OCCUPIED);

    struct menu_bar_ui_action_log log = {0};
    struct menu_bar_callbacks callbacks = menu_bar_ui_callbacks(&log);
    YBMenuBarController *controller = [[YBMenuBarController alloc] initWithCallbacks:&callbacks];
    [controller testSetSnapshot:menu_bar_ui_default_layout_snapshot(70)];

    NSView *expanded = [controller testBuildExpandedStrip];
    uint32_t indexes[8] = {0};
    size_t omitted_before = 0;
    size_t omitted_after = 0;
    size_t count = menu_bar_ui_screen_selection(expanded, indexes, 8, &omitted_before, &omitted_after);
    TEST_CHECK(count == 4);
    TEST_CHECK(omitted_before == 0);
    TEST_CHECK(omitted_after == 0);
    TEST_CHECK(indexes[0] == 2);
    TEST_CHECK(indexes[1] == 3);
    TEST_CHECK(indexes[2] == 4);
    TEST_CHECK(indexes[3] == 5);
    TEST_CHECK([[expanded subviews] count] == 4);
    TEST_CHECK(NSHeight([expanded frame]) == YB_MENU_BAR_STATUS_HEIGHT);

    YBMenuBarScreenCell *visible_other = [[expanded subviews] objectAtIndex:0];
    YBMenuBarScreenCell *occupied_one = [[expanded subviews] objectAtIndex:1];
    YBMenuBarScreenCell *occupied_two = [[expanded subviews] objectAtIndex:2];
    YBMenuBarScreenCell *active = [[expanded subviews] objectAtIndex:3];
    TEST_CHECK(visible_other->yb_state == YB_MENU_BAR_SCREEN_VISIBLE_OTHER);
    TEST_CHECK(occupied_one->yb_state == YB_MENU_BAR_SCREEN_OCCUPIED);
    TEST_CHECK(occupied_two->yb_state == YB_MENU_BAR_SCREEN_OCCUPIED);
    TEST_CHECK(active->yb_state == YB_MENU_BAR_SCREEN_ACTIVE);
    TEST_CHECK(visible_other->yb_display_index == 0);
    TEST_CHECK(occupied_one->yb_display_index == 0);
    TEST_CHECK(occupied_two->yb_display_index == 1);
    TEST_CHECK(active->yb_display_index == 1);
    TEST_CHECK(NSWidth([active frame]) == YB_MENU_BAR_CELL_WIDTH);
    TEST_CHECK(NSHeight([active frame]) == YB_MENU_BAR_STATUS_HEIGHT);
    TEST_CHECK(YB_MENU_BAR_STATUS_HEIGHT == 22.0);
    TEST_CHECK(YB_MENU_BAR_CELL_WIDTH == 25.0);
    TEST_CHECK(YB_MENU_BAR_SCREEN_WIDTH == 22.0);
    TEST_CHECK(YB_MENU_BAR_SCREEN_HEIGHT == 18.0);
    TEST_CHECK(YB_MENU_BAR_SCREEN_RADIUS == 3.0);
    TEST_CHECK(YB_MENU_BAR_INDEX_FONT_SIZE == 12.0);
    TEST_CHECK(YB_MENU_BAR_INDEX_PAD_X == 8.0);
    TEST_CHECK(YB_MENU_BAR_GAP == 0.0);
    TEST_CHECK(YB_MENU_BAR_DISPLAY_GAP == 8.0);
    TEST_CHECK(YB_MENU_BAR_DISPLAY_NEUTRAL_ALPHA == 0.035);
    TEST_CHECK(YB_MENU_BAR_DISPLAY_TINT_ALPHA == 0.075);
    TEST_CHECK(YB_MENU_BAR_SEPARATOR_WIDTH == 2.0);
    TEST_CHECK(YB_MENU_BAR_SEPARATOR_ALPHA == 0.85);
    TEST_CHECK(YB_MENU_BAR_SEPARATOR_CONTRAST_ALPHA == 1.0);
    TEST_CHECK(YB_MENU_BAR_SEPARATOR_CONTRAST_ALPHA > YB_MENU_BAR_SEPARATOR_ALPHA);
    TEST_CHECK([active image] == nil);
    TEST_CHECK([[active title] length] == 0);
    TEST_CHECK(occupied_one->yb_index == 3);
    TEST_CHECK([[occupied_one toolTip] containsString:@"space 3 work"]);
    TEST_CHECK([[occupied_one toolTip] containsString:@"Editor (2)"]);
    TEST_CHECK([[occupied_one accessibilityLabel] isEqualToString:[occupied_one toolTip]]);
    TEST_CHECK(NSMinX([occupied_one frame]) - NSMaxX([visible_other frame]) == YB_MENU_BAR_GAP);
    TEST_CHECK(NSMinX([occupied_two frame]) - NSMaxX([occupied_one frame]) == YB_MENU_BAR_DISPLAY_GAP);
    TEST_CHECK(NSMinX([active frame]) - NSMaxX([occupied_two frame]) == YB_MENU_BAR_GAP);

    for (NSView *view in [expanded subviews]) {
        TEST_CHECK([expanded hitTest:NSMakePoint(NSMidX([view frame]), NSMidY([view frame]))] == nil);
    }
    TEST_CHECK(NSMaxX([visible_other frame]) == NSMinX([occupied_one frame]));
    CGFloat display_gap = (NSMaxX([occupied_one frame]) + NSMinX([occupied_two frame])) * 0.5;
    struct menu_bar_ui_strip_decoration_metrics decorations = menu_bar_ui_render_strip_decorations((YBMenuBarStripView *) expanded,
                                                                                                    NSMakePoint(NSMinX([visible_other frame]), NSMidY([expanded bounds])),
                                                                                                    NSMakePoint(NSMinX([occupied_two frame]), NSMidY([expanded bounds])),
                                                                                                    NSMakePoint(display_gap, NSMidY([expanded bounds])));
    TEST_CHECK(decorations.first_group_alpha > 0.02);
    TEST_CHECK(decorations.second_group_alpha > decorations.first_group_alpha + 0.02);
    TEST_CHECK(decorations.separator_alpha > 0.80);
    TEST_CHECK(decorations.separator_alpha > decorations.second_group_alpha * 8.0);
    TEST_CHECK([expanded hitTest:NSMakePoint(display_gap, NSMidY([expanded bounds]))] == nil);
    TEST_CHECK([expanded hitTest:NSMakePoint(YB_MENU_BAR_OUTER_INSET * 0.5,
                                             NSMidY([expanded bounds]))] == nil);

    for (NSView *view in [expanded subviews]) {
        TEST_CHECK([view isKindOfClass:[YBMenuBarScreenCell class]]);
        YBMenuBarScreenCell *cell = (YBMenuBarScreenCell *) view;
        TEST_CHECK([cell action] == nil);
        TEST_CHECK([cell target] == nil);
        TEST_CHECK([[cell accessibilityRole] isEqualToString:NSAccessibilityStaticTextRole]);
    }
    TEST_CHECK(log.count == 0);

    NSView *compact = [controller testBuildCompactStrip];
    TEST_CHECK([[compact subviews] count] == 1);
    YBMenuBarScreenCell *compact_cell = [[compact subviews] objectAtIndex:0];
    TEST_CHECK([compact_cell isKindOfClass:[YBMenuBarScreenCell class]]);
    TEST_CHECK(compact_cell->yb_index == 5);
    TEST_CHECK(compact_cell->yb_state == YB_MENU_BAR_SCREEN_ACTIVE);
    TEST_CHECK(NSWidth([compact frame]) == YB_MENU_BAR_OUTER_INSET * 2.0 + YB_MENU_BAR_CELL_WIDTH);
    TEST_CHECK([compact hitTest:NSMakePoint(NSMidX([compact_cell frame]), NSMidY([compact_cell frame]))] == nil);
    TEST_CHECK([compact hitTest:NSMakePoint(YB_MENU_BAR_OUTER_INSET * 0.5,
                                            NSMidY([compact bounds]))] == nil);
    TEST_CHECK([compact_cell action] == nil);
    TEST_CHECK([[compact_cell accessibilityRole] isEqualToString:NSAccessibilityStaticTextRole]);

    struct menu_bar_snapshot *unresolved_snapshot = menu_bar_ui_snapshot(71, MENU_BAR_ENABLED);
    unresolved_snapshot->settings.style = MENU_BAR_STYLE_EXPANDED;
    [controller testSetSnapshot:unresolved_snapshot];
    NSView *unresolved = [controller testBuildExpandedStrip];
    TEST_CHECK([[unresolved subviews] count] == 1);
    YBMenuBarScreenCell *placeholder = [[unresolved subviews] objectAtIndex:0];
    TEST_CHECK(placeholder->yb_state == YB_MENU_BAR_SCREEN_PLACEHOLDER);
    TEST_CHECK([unresolved hitTest:NSMakePoint(NSMidX([placeholder frame]),
                                               NSMidY([placeholder frame]))] == nil);
    TEST_CHECK([placeholder action] == nil);
    TEST_CHECK([[placeholder accessibilityRole] isEqualToString:NSAccessibilityStaticTextRole]);
    TEST_CHECK(log.count == 0);

    [controller release];
    return true;
}

static bool
test_strip_clicks_are_owned_by_native_status_button(void)
{
    struct menu_bar_ui_action_log log = {0};
    struct menu_bar_callbacks callbacks = menu_bar_ui_callbacks(&log);
    YBMenuBarController *controller = [[YBMenuBarController alloc] initWithCallbacks:&callbacks];

    [controller testSetSnapshot:menu_bar_ui_default_layout_snapshot(73)];
    [controller testRebuildMenu];
    [controller ensureStatusItem];
    [controller renderExpandedStatusItem];
    NSView *strip = [controller testStripView];
    NSStatusBarButton *button = [controller testStatusButton];
    TEST_CHECK(strip != nil);
    TEST_CHECK(button != nil);
    TEST_CHECK([strip superview] == button);
    TEST_CHECK([[strip subviews] count] == 4);

    for (NSView *view in [strip subviews]) {
        TEST_CHECK([view isKindOfClass:[YBMenuBarScreenCell class]]);
        YBMenuBarScreenCell *cell = (YBMenuBarScreenCell *) view;
        TEST_CHECK([cell action] == nil);
        TEST_CHECK([cell target] == nil);
        TEST_CHECK([strip hitTest:NSMakePoint(NSMidX([cell frame]), NSMidY([cell frame]))] == nil);
    }

    YBMenuBarScreenCell *before_gap = [[strip subviews] objectAtIndex:1];
    YBMenuBarScreenCell *after_gap = [[strip subviews] objectAtIndex:2];
    NSPoint gap = NSMakePoint((NSMaxX([before_gap frame]) + NSMinX([after_gap frame])) * 0.5,
                              NSMidY([strip bounds]));
    TEST_CHECK([strip hitTest:gap] == nil);
    TEST_CHECK(log.count == 0);

    [controller clearStatusItem];
    [controller release];
    return true;
}

static bool
test_expanded_render_uses_native_status_button_menu(void)
{
    struct menu_bar_ui_action_log log = {0};
    struct menu_bar_callbacks callbacks = menu_bar_ui_callbacks(&log);
    YBMenuBarController *controller = [[YBMenuBarController alloc] initWithCallbacks:&callbacks];

    [controller testSetSnapshot:menu_bar_ui_default_layout_snapshot(72)];
    [controller testRebuildMenu];
    [controller ensureStatusItem];
    [controller renderExpandedStatusItem];

    NSStatusBarButton *button = [controller testStatusButton];
    TEST_CHECK([controller testStatusItemCount] == 1);
    TEST_CHECK([controller testStatusButtonCount] == 4);
    TEST_CHECK([controller testStatusButtonOwnsStripView]);
    TEST_CHECK([controller testStatusItemOwnsMenu]);
    TEST_CHECK(button != nil);
    TEST_CHECK([[button toolTip] containsString:@"click to choose a Space"]);
    TEST_CHECK([[button accessibilityLabel] isEqualToString:@"yabai Spaces"]);

    [controller clearStatusItem];
    TEST_CHECK([controller testStatusItemCount] == 0);
    TEST_CHECK([controller testStatusButtonCount] == 0);
    TEST_CHECK([controller testStatusButton] == nil);
    TEST_CHECK(log.count == 0);

    [controller release];
    return true;
}

static bool
test_automatic_layout_uses_width_budget_and_accessibility_summary(void)
{
    struct menu_bar_ui_action_log log = {0};
    struct menu_bar_callbacks callbacks = menu_bar_ui_callbacks(&log);
    YBMenuBarController *controller = [[YBMenuBarController alloc] initWithCallbacks:&callbacks];

    struct menu_bar_snapshot *small = menu_bar_ui_default_layout_snapshot(74);
    small->settings.style = MENU_BAR_STYLE_AUTOMATIC;
    [controller receiveSnapshot:small clear:false];

    NSView *expanded = [controller testStripView];
    NSStatusBarButton *button = [controller testStatusButton];
    TEST_CHECK(expanded != nil);
    TEST_CHECK(NSWidth([expanded frame]) <= YB_MENU_BAR_MAX_AUTOMATIC_WIDTH);
    TEST_CHECK([controller testStatusButtonCount] == 4);
    TEST_CHECK([[button accessibilityValue] containsString:@"Display 1"]);
    TEST_CHECK([[button accessibilityValue] containsString:@"visible space 5 focus"]);
    TEST_CHECK(![[button toolTip] containsString:@"space 5"]);

    NSMenu *menu = [controller testMenu];
    TEST_CHECK([menu numberOfItems] == 12);
    NSMenuItem *first_display = [menu itemAtIndex:2];
    NSMenuItem *second_display = [menu itemAtIndex:6];
    TEST_CHECK([[first_display title] containsString:@"Display 1"]);
    TEST_CHECK([[second_display title] containsString:@"Display 2"]);
    TEST_CHECK([first_display action] == nil);
    TEST_CHECK([second_display action] == nil);
    TEST_CHECK([first_display submenu] == nil);
    TEST_CHECK([second_display submenu] == nil);
    TEST_CHECK([[[menu itemAtIndex:3] title] containsString:@"2 visible"]);
    TEST_CHECK([[[menu itemAtIndex:4] title] containsString:@"3 work"]);
    TEST_CHECK([[[menu itemAtIndex:7] title] containsString:@"4 chat"]);
    TEST_CHECK([[[menu itemAtIndex:8] title] containsString:@"5 focus"]);

    NSMenu *style_menu = [[menu itemAtIndex:[menu numberOfItems] - 1] submenu];
    TEST_CHECK(style_menu != nil);
    TEST_CHECK([[style_menu itemAtIndex:0] state] == NSControlStateValueOn);
    TEST_CHECK([[style_menu itemAtIndex:1] state] == NSControlStateValueOff);
    TEST_CHECK([[style_menu itemAtIndex:2] state] == NSControlStateValueOff);

    struct menu_bar_snapshot *large = menu_bar_ui_overflow_snapshot(75, 20);
    large->settings.style = MENU_BAR_STYLE_AUTOMATIC;
    [controller receiveSnapshot:large clear:false];

    NSView *compact = [controller testStripView];
    button = [controller testStatusButton];
    TEST_CHECK(compact != nil);
    TEST_CHECK(NSWidth([compact frame]) < YB_MENU_BAR_MAX_AUTOMATIC_WIDTH);
    TEST_CHECK([controller testStatusButtonCount] == 1);
    YBMenuBarScreenCell *cell = [[compact subviews] objectAtIndex:0];
    TEST_CHECK([cell isKindOfClass:[YBMenuBarScreenCell class]]);
    TEST_CHECK(cell->yb_index == 121);
    TEST_CHECK([[button accessibilityValue] containsString:@"visible space 121"]);
    TEST_CHECK(log.count == 0);

    [controller clearStatusItem];
    [controller release];
    return true;
}

static bool
test_invalid_index_placeholder_has_transparent_interior(void)
{
    YBMenuBarScreenCell *cell = [[YBMenuBarScreenCell alloc] initWithFrame:NSMakeRect(0.0, 0.0,
                                                                                     YB_MENU_BAR_CELL_WIDTH,
                                                                                     YB_MENU_BAR_STATUS_HEIGHT)];
    [cell setTitle:@""];
    [cell setImage:nil];
    [cell setBordered:NO];
    cell->yb_index = 0;
    cell->yb_state = YB_MENU_BAR_SCREEN_PLACEHOLDER;

    struct menu_bar_ui_bitmap_metrics metrics = menu_bar_ui_render_screen(cell, NSAppearanceNameAqua, false);
    TEST_CHECK(yb_screen_index_text(cell->yb_index) == nil);
    TEST_CHECK(metrics.painted_pixel_count > 0);
    TEST_CHECK(metrics.interior_sample_count > 0);
    TEST_CHECK(metrics.interior_painted_pixel_count == 0);
    TEST_CHECK(metrics.expected_text_painted_pixel_count == 0);
    TEST_CHECK(metrics.unexpected_interior_painted_pixel_count == 0);
    TEST_CHECK(metrics.inner_perimeter_painted_pixel_count == 0);

    [cell release];
    return true;
}

static bool
test_screen_number_visual_states_are_centered_monochrome_and_unfilled(void)
{
    YBMenuBarScreenCell *cell = [[YBMenuBarScreenCell alloc] initWithFrame:NSMakeRect(0.0, 0.0,
                                                                                     YB_MENU_BAR_CELL_WIDTH,
                                                                                     YB_MENU_BAR_STATUS_HEIGHT)];
    [cell setTitle:@""];
    [cell setImage:nil];
    [cell setBordered:NO];
    cell->yb_index = 8;

    TEST_CHECK(yb_screen_line_width(YB_MENU_BAR_SCREEN_ACTIVE) == 2.25);
    TEST_CHECK(yb_screen_line_width(YB_MENU_BAR_SCREEN_VISIBLE_OTHER) == 1.40625);
    TEST_CHECK(yb_screen_line_width(YB_MENU_BAR_SCREEN_OCCUPIED) == 1.125);
    TEST_CHECK(yb_screen_line_width(YB_MENU_BAR_SCREEN_VISIBLE_OTHER) /
               yb_screen_line_width(YB_MENU_BAR_SCREEN_ACTIVE) == 1.25 / 2.0);
    TEST_CHECK(yb_screen_line_width(YB_MENU_BAR_SCREEN_OCCUPIED) /
               yb_screen_line_width(YB_MENU_BAR_SCREEN_ACTIVE) == 1.0 / 2.0);
    TEST_CHECK(!yb_screen_is_dashed(YB_MENU_BAR_SCREEN_ACTIVE));
    TEST_CHECK(!yb_screen_is_dashed(YB_MENU_BAR_SCREEN_OCCUPIED));
    TEST_CHECK(yb_screen_is_dashed(YB_MENU_BAR_SCREEN_UNKNOWN));
    TEST_CHECK([yb_screen_index_text(cell->yb_index) isEqualToString:@"8"]);
    TEST_CHECK(yb_screen_width(cell->yb_index) == YB_MENU_BAR_SCREEN_WIDTH);
    TEST_CHECK([[yb_screen_index_font() fontName] length] > 0);
    TEST_CHECK([yb_screen_index_font() pointSize] == YB_MENU_BAR_INDEX_FONT_SIZE);
    TEST_CHECK([yb_screen_index_font() isEqual:[NSFont monospacedDigitSystemFontOfSize:12.0
                                                                                 weight:NSFontWeightBold]]);
    TEST_CHECK(([[NSFontManager sharedFontManager] traitsOfFont:yb_screen_index_font()] & NSBoldFontMask) != 0);

    NSRect bounds = [cell bounds];
    NSRect screen = yb_screen_rect(bounds, cell->yb_index);
    TEST_CHECK(NSEqualRects(screen, NSMakeRect(1.5, 2.0, 22.0, 18.0)));
    TEST_CHECK(NSMidX(screen) == NSMidX(bounds));
    TEST_CHECK(NSMidY(screen) == NSMidY(bounds));

    cell->yb_state = YB_MENU_BAR_SCREEN_ACTIVE;
    struct menu_bar_ui_bitmap_metrics active_aqua = menu_bar_ui_render_screen(cell, NSAppearanceNameAqua, false);
    struct menu_bar_ui_bitmap_metrics active_dark = menu_bar_ui_render_screen(cell, NSAppearanceNameDarkAqua, false);
    cell->yb_state = YB_MENU_BAR_SCREEN_VISIBLE_OTHER;
    struct menu_bar_ui_bitmap_metrics visible_aqua = menu_bar_ui_render_screen(cell, NSAppearanceNameAqua, false);
    struct menu_bar_ui_bitmap_metrics visible_dark = menu_bar_ui_render_screen(cell, NSAppearanceNameDarkAqua, false);
    cell->yb_state = YB_MENU_BAR_SCREEN_OCCUPIED;
    struct menu_bar_ui_bitmap_metrics occupied_aqua = menu_bar_ui_render_screen(cell, NSAppearanceNameAqua, false);
    struct menu_bar_ui_bitmap_metrics occupied_dark = menu_bar_ui_render_screen(cell, NSAppearanceNameDarkAqua, false);
    struct menu_bar_ui_bitmap_metrics occupied_contrast = menu_bar_ui_render_screen(cell, NSAppearanceNameAqua, true);
    struct menu_bar_ui_bitmap_metrics occupied_dark_contrast = menu_bar_ui_render_screen(cell, NSAppearanceNameDarkAqua, true);
    cell->yb_state = YB_MENU_BAR_SCREEN_UNKNOWN;
    struct menu_bar_ui_bitmap_metrics unknown_aqua = menu_bar_ui_render_screen(cell, NSAppearanceNameAqua, false);
    cell->yb_state = YB_MENU_BAR_SCREEN_PLACEHOLDER;
    struct menu_bar_ui_bitmap_metrics placeholder_aqua = menu_bar_ui_render_screen(cell, NSAppearanceNameAqua, false);

    struct menu_bar_ui_bitmap_metrics metrics[] = {
        active_aqua, active_dark, visible_aqua, visible_dark,
        occupied_aqua, occupied_dark, occupied_contrast, occupied_dark_contrast,
        unknown_aqua, placeholder_aqua
    };
    for (size_t i = 0; i < sizeof(metrics) / sizeof(metrics[0]); ++i) {
        TEST_CHECK(metrics[i].painted_pixel_count > 0);
        TEST_CHECK(metrics[i].maximum_channel_spread <= 0.03);
        TEST_CHECK(metrics[i].interior_painted_pixel_count > 0);
        TEST_CHECK(metrics[i].interior_painted_pixel_count < metrics[i].interior_sample_count);
        TEST_CHECK(metrics[i].interior_max_alpha >= 0.80);
        TEST_CHECK(metrics[i].outline_painted_pixel_count > 0);
        TEST_CHECK(metrics[i].expected_text_painted_pixel_count == metrics[i].interior_painted_pixel_count);
        TEST_CHECK(metrics[i].unexpected_interior_painted_pixel_count == 0);
        TEST_CHECK(metrics[i].inner_perimeter_painted_pixel_count == 0);
        TEST_CHECK(metrics[i].interior_min_x >= 3);
        TEST_CHECK(metrics[i].interior_max_x < YB_MENU_BAR_CELL_WIDTH - 3);
        TEST_CHECK(metrics[i].interior_min_y >= 2);
        TEST_CHECK(metrics[i].interior_max_y < YB_MENU_BAR_STATUS_HEIGHT - 2);
        CGFloat number_mid_x = (metrics[i].interior_min_x + metrics[i].interior_max_x + 1.0) * 0.5;
        CGFloat number_mid_y = (metrics[i].interior_min_y + metrics[i].interior_max_y + 1.0) * 0.5;
        TEST_CHECK(fabs(number_mid_x - YB_MENU_BAR_CELL_WIDTH * 0.5) <= 1.0);
        TEST_CHECK(fabs(number_mid_y - YB_MENU_BAR_STATUS_HEIGHT * 0.5) <= 1.5);
    }
    TEST_CHECK(active_aqua.alpha_sum > visible_aqua.alpha_sum);
    TEST_CHECK(visible_aqua.alpha_sum > occupied_aqua.alpha_sum);
    TEST_CHECK(visible_dark.alpha_sum > occupied_dark.alpha_sum);
    TEST_CHECK(fabs(active_aqua.interior_alpha_sum - visible_aqua.interior_alpha_sum) <= 0.1);
    TEST_CHECK(fabs(active_dark.interior_alpha_sum - visible_dark.interior_alpha_sum) <= 0.1);
    TEST_CHECK(fabs(visible_aqua.interior_alpha_sum - occupied_aqua.interior_alpha_sum) <= 0.1);
    TEST_CHECK(fabs(visible_dark.interior_alpha_sum - occupied_dark.interior_alpha_sum) <= 0.1);
    TEST_CHECK(fabs(occupied_contrast.interior_alpha_sum - occupied_aqua.interior_alpha_sum) <= 0.1);
    TEST_CHECK(fabs(occupied_dark_contrast.interior_alpha_sum - occupied_dark.interior_alpha_sum) <= 0.1);
    TEST_CHECK(fabs(unknown_aqua.interior_alpha_sum - occupied_aqua.interior_alpha_sum) <= 0.1);
    TEST_CHECK(fabs(placeholder_aqua.interior_alpha_sum - occupied_aqua.interior_alpha_sum) <= 0.1);
    TEST_CHECK(occupied_aqua.interior_alpha_sum / occupied_aqua.interior_painted_pixel_count >
               occupied_aqua.outline_alpha_sum / occupied_aqua.outline_painted_pixel_count);
    TEST_CHECK(occupied_aqua.interior_max_alpha > occupied_aqua.outline_max_alpha);
    TEST_CHECK(unknown_aqua.interior_alpha_sum / unknown_aqua.interior_painted_pixel_count >
               unknown_aqua.outline_alpha_sum / unknown_aqua.outline_painted_pixel_count);
    TEST_CHECK(unknown_aqua.interior_max_alpha > unknown_aqua.outline_max_alpha);
    TEST_CHECK(placeholder_aqua.interior_alpha_sum / placeholder_aqua.interior_painted_pixel_count >
               placeholder_aqua.outline_alpha_sum / placeholder_aqua.outline_painted_pixel_count);
    TEST_CHECK(placeholder_aqua.interior_max_alpha > placeholder_aqua.outline_max_alpha);

    [cell release];
    return true;
}

static bool
test_multi_digit_number_geometry_stays_compact_and_within_bounds(void)
{
    NSFont *font = yb_screen_index_font();
    TEST_CHECK(font != nil);
    TEST_CHECK([font pointSize] == 12.0);
    CGFloat elevens_width = [yb_screen_index_text(11) sizeWithAttributes:@{ NSFontAttributeName: font }].width;
    CGFloat eights_width = [yb_screen_index_text(88) sizeWithAttributes:@{ NSFontAttributeName: font }].width;
    CGFloat hundreds_width = [yb_screen_index_text(123) sizeWithAttributes:@{ NSFontAttributeName: font }].width;
    TEST_CHECK(fabs(elevens_width - eights_width) <= 0.01);

    CGFloat one_digit_width = yb_screen_width(9);
    CGFloat two_digit_width = yb_screen_width(12);
    CGFloat three_digit_width = yb_screen_width(123);
    TEST_CHECK(one_digit_width == YB_MENU_BAR_SCREEN_WIDTH);
    TEST_CHECK(two_digit_width == MAX(YB_MENU_BAR_SCREEN_WIDTH, ceil(elevens_width + YB_MENU_BAR_INDEX_PAD_X)));
    TEST_CHECK(three_digit_width == MAX(YB_MENU_BAR_SCREEN_WIDTH, ceil(hundreds_width + YB_MENU_BAR_INDEX_PAD_X)));
    TEST_CHECK(two_digit_width >= one_digit_width);
    TEST_CHECK(three_digit_width > two_digit_width);
    TEST_CHECK(three_digit_width - hundreds_width >= YB_MENU_BAR_INDEX_PAD_X);
    TEST_CHECK(YB_MENU_BAR_SCREEN_HEIGHT >= [yb_screen_index_text(123)
                                             sizeWithAttributes:@{ NSFontAttributeName: font }].height);
    TEST_CHECK(yb_screen_cell_width(123) == YB_MENU_BAR_CELL_WIDTH +
                                                   three_digit_width - YB_MENU_BAR_SCREEN_WIDTH);

    CGFloat cell_width = yb_screen_cell_width(123);
    YBMenuBarScreenCell *cell = [[YBMenuBarScreenCell alloc] initWithFrame:NSMakeRect(0.0, 0.0,
                                                                                     cell_width,
                                                                                     YB_MENU_BAR_STATUS_HEIGHT)];
    [cell setTitle:@""];
    [cell setImage:nil];
    [cell setBordered:NO];
    cell->yb_index = 123;
    cell->yb_state = YB_MENU_BAR_SCREEN_OCCUPIED;
    struct menu_bar_ui_bitmap_metrics metrics = menu_bar_ui_render_screen(cell, NSAppearanceNameAqua, false);
    TEST_CHECK(metrics.interior_painted_pixel_count > 0);
    TEST_CHECK(metrics.interior_painted_pixel_count < metrics.interior_sample_count);
    TEST_CHECK(metrics.expected_text_painted_pixel_count == metrics.interior_painted_pixel_count);
    TEST_CHECK(metrics.unexpected_interior_painted_pixel_count == 0);
    TEST_CHECK(metrics.inner_perimeter_painted_pixel_count == 0);
    TEST_CHECK(metrics.interior_min_x >= 2);
    TEST_CHECK(metrics.interior_max_x < (NSInteger) cell_width - 2);
    TEST_CHECK(metrics.interior_min_y >= 3);
    TEST_CHECK(metrics.interior_max_y < YB_MENU_BAR_STATUS_HEIGHT - 3);
    CGFloat number_mid_x = (metrics.interior_min_x + metrics.interior_max_x + 1.0) * 0.5;
    CGFloat number_mid_y = (metrics.interior_min_y + metrics.interior_max_y + 1.0) * 0.5;
    TEST_CHECK(fabs(number_mid_x - cell_width * 0.5) <= 1.0);
    TEST_CHECK(fabs(number_mid_y - YB_MENU_BAR_STATUS_HEIGHT * 0.5) <= 1.5);
    TEST_CHECK(metrics.maximum_channel_spread <= 0.03);

    [cell release];
    return true;
}

static bool
test_overflow_keeps_active_and_nearest_source_neighbors(void)
{
    const size_t total = 40;
    const size_t active = 20;
    struct menu_bar_ui_action_log log = {0};
    struct menu_bar_callbacks callbacks = menu_bar_ui_callbacks(&log);
    YBMenuBarController *controller = [[YBMenuBarController alloc] initWithCallbacks:&callbacks];
    struct menu_bar_snapshot *snapshot = menu_bar_ui_overflow_snapshot(80, active);
    const struct menu_bar_display *display = &snapshot->displays[0];
    struct yb_screen_entry entries[40] = {0};
    for (size_t i = 0; i < total; ++i) {
        entries[i] = (struct yb_screen_entry) { display, &display->spaces[i], 0 };
    }
    struct yb_screen_selection expected = yb_select_screen_interval(entries, total, active);
    TEST_CHECK(YB_MENU_BAR_MAX_EXPANDED_WIDTH == 520.0);
    TEST_CHECK(YB_MENU_BAR_MAX_AUTOMATIC_WIDTH == 260.0);
    TEST_CHECK(expected.start > 0);
    TEST_CHECK(expected.start + expected.count < total);
    TEST_CHECK(expected.count > 1 && expected.count < total);
    [controller testSetSnapshot:snapshot];
    [controller testRebuildMenu];
    [controller ensureStatusItem];
    [controller renderExpandedStatusItem];

    NSView *expanded = [controller testStripView];
    uint32_t indexes[40] = {0};
    size_t omitted_before = 0;
    size_t omitted_after = 0;
    size_t count = menu_bar_ui_screen_selection(expanded, indexes, 40, &omitted_before, &omitted_after);
    TEST_CHECK(count == expected.count);
    TEST_CHECK(indexes[0] == expected.start + 101);
    TEST_CHECK(indexes[count - 1] == expected.start + expected.count + 100);
    TEST_CHECK(omitted_before == expected.start);
    TEST_CHECK(omitted_after == total - expected.start - expected.count);
    TEST_CHECK(omitted_before + omitted_after == total - count);
    TEST_CHECK(NSWidth([expanded frame]) == yb_screen_interval_width(entries, total,
                                                                     expected.start, expected.count));
    TEST_CHECK(NSWidth([expanded frame]) <= YB_MENU_BAR_MAX_EXPANDED_WIDTH);
    TEST_CHECK(yb_screen_interval_fits(entries, total, expected.start, expected.count));
    if (expected.start > 0) {
        TEST_CHECK(!yb_screen_interval_fits(entries, total, expected.start - 1, expected.count + 1));
    }
    if (expected.start + expected.count < total) {
        TEST_CHECK(!yb_screen_interval_fits(entries, total, expected.start, expected.count + 1));
    }

    NSArray *subviews = [expanded subviews];
    TEST_CHECK([subviews count] == count + 2);
    YBMenuBarButton *leading = [subviews firstObject];
    YBMenuBarButton *trailing = [subviews lastObject];
    TEST_CHECK([leading isKindOfClass:[YBMenuBarButton class]]);
    TEST_CHECK([trailing isKindOfClass:[YBMenuBarButton class]]);
    TEST_CHECK([[leading title] isEqualToString:yb_overflow_title(omitted_before, true)]);
    TEST_CHECK([[trailing title] isEqualToString:yb_overflow_title(omitted_after, false)]);
    TEST_CHECK([[leading accessibilityLabel] containsString:@"before"]);
    TEST_CHECK([[trailing accessibilityLabel] containsString:@"after"]);
    TEST_CHECK([leading action] == nil && [trailing action] == nil);
    TEST_CHECK([leading target] == nil && [trailing target] == nil);
    TEST_CHECK([[leading accessibilityRole] isEqualToString:NSAccessibilityStaticTextRole]);
    TEST_CHECK([[trailing accessibilityRole] isEqualToString:NSAccessibilityStaticTextRole]);
    TEST_CHECK(NSMinX([leading frame]) == YB_MENU_BAR_OUTER_INSET);
    TEST_CHECK(NSWidth([leading frame]) == yb_overflow_width(omitted_before, true));

    CGFloat expected_x = NSMaxX([leading frame]) + YB_MENU_BAR_GAP;
    for (NSUInteger i = 1; i + 1 < [subviews count]; ++i) {
        YBMenuBarScreenCell *cell = [subviews objectAtIndex:i];
        TEST_CHECK([cell isKindOfClass:[YBMenuBarScreenCell class]]);
        TEST_CHECK(NSMinX([cell frame]) == expected_x);
        TEST_CHECK(NSWidth([cell frame]) == yb_screen_cell_width(cell->yb_index));
        TEST_CHECK(NSHeight([cell frame]) == YB_MENU_BAR_STATUS_HEIGHT);
        expected_x = NSMaxX([cell frame]) + YB_MENU_BAR_GAP;
    }
    TEST_CHECK(NSMinX([trailing frame]) == expected_x);
    TEST_CHECK(NSWidth([trailing frame]) == yb_overflow_width(omitted_after, false));
    TEST_CHECK(NSMaxX([trailing frame]) + YB_MENU_BAR_OUTER_INSET == NSWidth([expanded frame]));
    TEST_CHECK([expanded hitTest:NSMakePoint(NSMidX([leading frame]), NSMidY([leading frame]))] == nil);
    TEST_CHECK([expanded hitTest:NSMakePoint(NSMidX([trailing frame]), NSMidY([trailing frame]))] == nil);

    bool found_active = false;
    for (size_t i = 0; i < count; ++i) {
        if (indexes[i] == 121) found_active = true;
        if (i) TEST_CHECK(indexes[i] == indexes[i - 1] + 1);
    }
    TEST_CHECK(found_active);
    TEST_CHECK(indexes[0] < 121);
    TEST_CHECK(indexes[count - 1] > 121);

    [controller release];
    return true;
}

static bool
test_generation_ordering_without_status_item_delivery(void)
{
    struct menu_bar_ui_action_log log = {0};
    struct menu_bar_callbacks callbacks = menu_bar_ui_callbacks(&log);

    TEST_CHECK(menu_bar_init(&callbacks));
    YBMenuBarController *controller = g_menu_bar_controller;
    TEST_CHECK(controller != nil);

    menu_bar_publish_snapshot(menu_bar_ui_snapshot(50, MENU_BAR_DISABLED));
    TEST_CHECK(g_menu_bar_pending_has_value);
    TEST_CHECK(g_menu_bar_pending_snapshot != NULL);
    TEST_CHECK(g_menu_bar_pending_snapshot->generation == 50);
    TEST_CHECK(g_menu_bar_latest_generation == 50);

    menu_bar_publish_snapshot(menu_bar_ui_snapshot(49, MENU_BAR_DISABLED));
    TEST_CHECK(g_menu_bar_pending_has_value);
    TEST_CHECK(g_menu_bar_pending_snapshot != NULL);
    TEST_CHECK(g_menu_bar_pending_snapshot->generation == 50);
    TEST_CHECK(g_menu_bar_latest_generation == 50);

    menu_bar_publish_snapshot(menu_bar_ui_snapshot(51, MENU_BAR_ENABLED));
    TEST_CHECK(g_menu_bar_pending_has_value);
    TEST_CHECK(g_menu_bar_pending_snapshot != NULL);
    TEST_CHECK(g_menu_bar_pending_snapshot->generation == 51);
    TEST_CHECK(g_menu_bar_pending_snapshot->settings.enabled == MENU_BAR_ENABLED);
    TEST_CHECK(g_menu_bar_latest_generation == 51);
    TEST_CHECK(![controller testHasStatusItem]);

    menu_bar_publish_snapshot(menu_bar_ui_snapshot(50, MENU_BAR_DISABLED));
    TEST_CHECK(g_menu_bar_pending_has_value);
    TEST_CHECK(g_menu_bar_pending_snapshot != NULL);
    TEST_CHECK(g_menu_bar_pending_snapshot->generation == 51);
    TEST_CHECK(g_menu_bar_pending_snapshot->settings.enabled == MENU_BAR_ENABLED);
    TEST_CHECK(g_menu_bar_latest_generation == 51);
    TEST_CHECK(log.count == 0);

    menu_bar_shutdown();
    return true;
}

struct menu_bar_ui_test_case
{
    const char *name;
    bool (*run)(void);
};

int main(void)
{
    @autoreleasepool {
        struct menu_bar_ui_test_case tests[] = {
            { "menu_tracking_captures_source_snapshot_focus", test_menu_tracking_captures_source_snapshot_focus },
            { "teardown_balances_tracking_without_cancel_close", test_teardown_balances_tracking_without_cancel_close },
            { "synchronous_and_late_close_are_suppressed_during_teardown", test_synchronous_and_late_close_are_suppressed_during_teardown },
            { "deferred_snapshot_is_disposed_when_teardown_has_no_menu", test_deferred_snapshot_is_disposed_when_teardown_has_no_menu },
            { "lifecycle_reinit_ignores_stale_delivery", test_lifecycle_reinit_ignores_stale_delivery },
            { "space_rows_emit_focus_actions_and_settings_remain_interactive", test_space_rows_emit_focus_actions_and_settings_remain_interactive },
            { "expanded_screen_cells_and_compact_option", test_expanded_screen_cells_and_compact_option },
            { "strip_clicks_are_owned_by_native_status_button", test_strip_clicks_are_owned_by_native_status_button },
            { "expanded_render_uses_native_status_button_menu", test_expanded_render_uses_native_status_button_menu },
            { "automatic_layout_uses_width_budget_and_accessibility_summary", test_automatic_layout_uses_width_budget_and_accessibility_summary },
            { "invalid_index_placeholder_has_transparent_interior", test_invalid_index_placeholder_has_transparent_interior },
            { "screen_number_visual_states_are_centered_monochrome_and_unfilled", test_screen_number_visual_states_are_centered_monochrome_and_unfilled },
            { "multi_digit_number_geometry_stays_compact_and_within_bounds", test_multi_digit_number_geometry_stays_compact_and_within_bounds },
            { "overflow_keeps_active_and_nearest_source_neighbors", test_overflow_keeps_active_and_nearest_source_neighbors },
            { "generation_ordering_without_status_item_delivery", test_generation_ordering_without_status_item_delivery },
        };

        size_t passed = 0;
        size_t total = sizeof(tests) / sizeof(tests[0]);
        for (size_t i = 0; i < total; ++i) {
            bool ok = tests[i].run();
            printf("%s %s\n", ok ? "PASS" : "FAIL", tests[i].name);
            if (ok) ++passed;
        }

        printf("%zu/%zu menu bar UI tests passed\n", passed, total);
        return passed == total ? EXIT_SUCCESS : EXIT_FAILURE;
    }
}
