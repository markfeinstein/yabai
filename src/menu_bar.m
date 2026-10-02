#include "menu_bar.h"

#import <AppKit/AppKit.h>
#include <dispatch/dispatch.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define YB_MENU_BAR_MAX_EXPANDED_WIDTH 520.0
#define YB_MENU_BAR_STATUS_HEIGHT       22.0
#define YB_MENU_BAR_BUTTON_PAD_X         7.0
#define YB_MENU_BAR_GAP                  0.0
#define YB_MENU_BAR_DISPLAY_GAP          4.0
#define YB_MENU_BAR_OUTER_INSET          2.0
#define YB_MENU_BAR_CELL_WIDTH          25.0
#define YB_MENU_BAR_SCREEN_WIDTH        22.0
#define YB_MENU_BAR_SCREEN_HEIGHT       18.0
#define YB_MENU_BAR_SCREEN_RADIUS        3.0
#define YB_MENU_BAR_INDEX_FONT_SIZE     12.0
#define YB_MENU_BAR_INDEX_PAD_X          8.0

static int g_yb_menu_bar_contrast_override = -1;

enum yb_menu_bar_screen_state
{
    YB_MENU_BAR_SCREEN_ACTIVE = 0,
    YB_MENU_BAR_SCREEN_VISIBLE_OTHER,
    YB_MENU_BAR_SCREEN_OCCUPIED,
    YB_MENU_BAR_SCREEN_UNKNOWN,
    YB_MENU_BAR_SCREEN_EMPTY,
    YB_MENU_BAR_SCREEN_PLACEHOLDER
};

enum yb_menu_bar_control_kind
{
    YB_MENU_BAR_CONTROL_OPEN_MENU = 0,
    YB_MENU_BAR_CONTROL_SET_STYLE,
    YB_MENU_BAR_CONTROL_SET_FILTER
};

@class YBMenuBarController;

@interface YBMenuBarButton : NSButton {
@public
    enum yb_menu_bar_control_kind yb_kind;
    uint64_t yb_generation;
    enum menu_bar_style yb_style;
    enum menu_bar_space_filter yb_filter;
}
@end

@implementation YBMenuBarButton
@end

@interface YBMenuBarScreenCell : YBMenuBarButton {
@public
    enum yb_menu_bar_screen_state yb_state;
    uint32_t yb_index;
}
@end

@interface YBMenuBarActionTarget : NSObject {
@public
    enum yb_menu_bar_control_kind yb_kind;
    uint64_t yb_generation;
    enum menu_bar_style yb_style;
    enum menu_bar_space_filter yb_filter;
}
@end

@implementation YBMenuBarActionTarget
@end

@interface YBMenuBarStripView : NSView
@end

@interface YBMenuBarController : NSObject <NSMenuDelegate> {
    struct menu_bar_callbacks _callbacks;
    NSStatusItem *_status_item;
    NSMenu *_menu;
    NSView *_strip_view;
    struct menu_bar_snapshot *_snapshot;
    struct menu_bar_snapshot *_deferred_snapshot;
    bool _is_tracking_menu;
    bool _is_tearing_down_status_item;
    uint64_t _tracking_generation;
}
- (id)initWithCallbacks:(const struct menu_bar_callbacks *)callbacks;
- (void)shutdown;
- (void)receiveSnapshot:(struct menu_bar_snapshot *)snapshot clear:(bool)clear;
- (void)performMenuAction:(id)sender;
- (void)performStripAction:(id)sender;
- (void)openMenu:(id)sender;
#if defined(YB_MENU_BAR_TEST)
- (void)testSetSnapshot:(struct menu_bar_snapshot *)snapshot;
- (void)testSetMenu:(NSMenu *)menu;
- (void)testRebuildMenu;
- (NSMenu *)testMenu;
- (bool)testHasSnapshot;
- (bool)testHasDeferredSnapshot;
- (bool)testHasStatusItem;
- (NSUInteger)testStatusItemCount;
- (NSUInteger)testStatusButtonCount;
- (bool)testStatusItemOwnsStripView;
- (bool)testIsTrackingMenu;
- (uint64_t)testTrackingGeneration;
- (NSView *)testBuildCompactStrip;
- (NSView *)testBuildExpandedStrip;
#endif
@end

@implementation YBMenuBarStripView
- (NSView *)hitTest:(NSPoint)point
{
    NSView *target = [super hitTest:point];
    return target == self ? nil : target;
}
@end

static bool
yb_increased_contrast(void)
{
    if (g_yb_menu_bar_contrast_override >= 0) return g_yb_menu_bar_contrast_override != 0;
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldIncreaseContrast];
}

static CGFloat
yb_screen_line_width(enum yb_menu_bar_screen_state state)
{
    switch (state) {
    case YB_MENU_BAR_SCREEN_ACTIVE:        return 2.25;
    case YB_MENU_BAR_SCREEN_VISIBLE_OTHER: return 1.40625;
    case YB_MENU_BAR_SCREEN_OCCUPIED:
    case YB_MENU_BAR_SCREEN_UNKNOWN:
    case YB_MENU_BAR_SCREEN_EMPTY:
    case YB_MENU_BAR_SCREEN_PLACEHOLDER:   return 1.125;
    }
    return 1.125;
}

static CGFloat
yb_screen_alpha(enum yb_menu_bar_screen_state state)
{
    switch (state) {
    case YB_MENU_BAR_SCREEN_ACTIVE:
    case YB_MENU_BAR_SCREEN_VISIBLE_OTHER:
        return 1.0;
    case YB_MENU_BAR_SCREEN_OCCUPIED:
        return yb_increased_contrast() ? 0.78 : 0.55;
    case YB_MENU_BAR_SCREEN_UNKNOWN:
    case YB_MENU_BAR_SCREEN_EMPTY:
    case YB_MENU_BAR_SCREEN_PLACEHOLDER:
        return yb_increased_contrast() ? 0.68 : 0.40;
    }
    return 1.0;
}

static bool
yb_screen_is_dashed(enum yb_menu_bar_screen_state state)
{
    return state == YB_MENU_BAR_SCREEN_UNKNOWN || state == YB_MENU_BAR_SCREEN_PLACEHOLDER;
}

static NSColor *
yb_screen_pressed_color(void)
{
    return [NSColor systemOrangeColor];
}

static CGFloat
yb_screen_pressed_fill_alpha(void)
{
    return yb_increased_contrast() ? 0.34 : 0.22;
}

static NSFont *
yb_screen_index_font(void)
{
    return [NSFont monospacedDigitSystemFontOfSize:YB_MENU_BAR_INDEX_FONT_SIZE weight:NSFontWeightBold];
}

static NSString *
yb_screen_index_text(uint32_t index)
{
    return index ? [NSString stringWithFormat:@"%u", index] : nil;
}

static CGFloat
yb_screen_width(uint32_t index)
{
    NSString *text = yb_screen_index_text(index);
    if (!text) return YB_MENU_BAR_SCREEN_WIDTH;

    CGFloat required = ceil([text sizeWithAttributes:@{ NSFontAttributeName: yb_screen_index_font() }].width +
                            YB_MENU_BAR_INDEX_PAD_X);
    return MAX(YB_MENU_BAR_SCREEN_WIDTH, required);
}

static CGFloat
yb_screen_cell_width(uint32_t index)
{
    return YB_MENU_BAR_CELL_WIDTH + yb_screen_width(index) - YB_MENU_BAR_SCREEN_WIDTH;
}

static NSRect
yb_screen_rect(NSRect bounds, uint32_t index)
{
    CGFloat width = yb_screen_width(index);
    return NSMakeRect(NSMidX(bounds) - width * 0.5,
                      NSMidY(bounds) - YB_MENU_BAR_SCREEN_HEIGHT * 0.5,
                      width,
                      YB_MENU_BAR_SCREEN_HEIGHT);
}

static enum yb_menu_bar_screen_state
yb_screen_state(const struct menu_bar_display *display, const struct menu_bar_space *space)
{
    if (!display || !space) return YB_MENU_BAR_SCREEN_PLACEHOLDER;
    if (space->is_visible) return display->has_focus ? YB_MENU_BAR_SCREEN_ACTIVE : YB_MENU_BAR_SCREEN_VISIBLE_OTHER;
    if (space->membership == MENU_BAR_SPACE_MEMBERSHIP_UNKNOWN) return YB_MENU_BAR_SCREEN_UNKNOWN;
    if (space->membership == MENU_BAR_SPACE_MEMBERSHIP_OCCUPIED) return YB_MENU_BAR_SCREEN_OCCUPIED;
    return YB_MENU_BAR_SCREEN_EMPTY;
}

@implementation YBMenuBarScreenCell

- (BOOL)isOpaque
{
    return NO;
}

- (void)viewDidChangeEffectiveAppearance
{
    [super viewDidChangeEffectiveAppearance];
    [self setNeedsDisplay:YES];
}

- (void)highlight:(BOOL)flag
{
    [super highlight:flag];
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirty_rect
{
    (void) dirty_rect;
    NSAppearance *appearance = [self effectiveAppearance];
    void (^draw_screen)(void) = ^{
        NSRect screen = yb_screen_rect([self bounds], yb_index);
        NSBezierPath *outline = [NSBezierPath bezierPathWithRoundedRect:screen
                                                                xRadius:YB_MENU_BAR_SCREEN_RADIUS
                                                                yRadius:YB_MENU_BAR_SCREEN_RADIUS];
        [outline setLineWidth:yb_screen_line_width(yb_state)];
        if (yb_screen_is_dashed(yb_state)) {
            CGFloat pattern[] = { 2.0, 1.5 };
            [outline setLineDash:pattern count:2 phase:0.0];
        }

        bool pressed = [self isHighlighted];
        NSColor *pressed_ink = yb_screen_pressed_color();
        if (pressed) {
            [[pressed_ink colorWithAlphaComponent:yb_screen_pressed_fill_alpha()] setFill];
            [outline fill];
        }

        NSColor *outline_ink = pressed
                             ? pressed_ink
                             : [[NSColor labelColor] colorWithAlphaComponent:yb_screen_alpha(yb_state)];
        [outline_ink setStroke];
        [outline stroke];

        NSString *index = yb_screen_index_text(yb_index);
        if (index) {
            NSDictionary *attributes = @{
                NSFontAttributeName: yb_screen_index_font(),
                NSForegroundColorAttributeName: pressed ? pressed_ink : [NSColor labelColor]
            };
            NSSize text_size = [index sizeWithAttributes:attributes];
            NSPoint text_origin = NSMakePoint(NSMidX(screen) - text_size.width * 0.5,
                                              NSMidY(screen) - text_size.height * 0.5 + 0.25);
            [index drawAtPoint:text_origin withAttributes:attributes];
        }
    };

    if (appearance) {
        [appearance performAsCurrentDrawingAppearance:draw_screen];
    } else {
        draw_screen();
    }
}

@end

static pthread_mutex_t g_menu_bar_lock = PTHREAD_MUTEX_INITIALIZER;
static YBMenuBarController *g_menu_bar_controller;
static bool g_menu_bar_initialized;
static uint64_t g_menu_bar_lifecycle_epoch;
static uint64_t g_menu_bar_latest_generation;
static struct menu_bar_snapshot *g_menu_bar_pending_snapshot;
static bool g_menu_bar_pending_has_value;
static bool g_menu_bar_pending_clear;
static bool g_menu_bar_delivery_scheduled;
static uint64_t g_menu_bar_delivery_epoch;

static NSString *
yb_nsstring(const char *text)
{
    if (!text || !text[0]) return nil;

    NSString *result = [NSString stringWithUTF8String:text];
    if (!result) result = [NSString stringWithCString:text encoding:NSISOLatin1StringEncoding];
    return result;
}

static bool
yb_space_matches_filter(const struct menu_bar_space *space, enum menu_bar_space_filter filter)
{
    if (!space) return false;
    if (filter == MENU_BAR_SPACE_FILTER_ALL) return true;
    if (space->is_visible) return true;
    return space->membership == MENU_BAR_SPACE_MEMBERSHIP_OCCUPIED ||
           space->membership == MENU_BAR_SPACE_MEMBERSHIP_UNKNOWN;
}

static uint32_t
yb_display_number(const struct menu_bar_display *display, size_t fallback_index)
{
    if (!display) return (uint32_t) fallback_index + 1;
    return display->arrangement_index + 1;
}

static NSString *
yb_display_menu_title(const struct menu_bar_display *display, size_t fallback_index)
{
    NSMutableString *title = [NSMutableString stringWithFormat:@"Display %u", yb_display_number(display, fallback_index)];
    NSString *label = yb_nsstring(display ? display->label : NULL);
    NSString *name = yb_nsstring(display ? display->name : NULL);

    if ([label length]) [title appendFormat:@" — %@", label];
    if ([name length] && ![name isEqualToString:label]) [title appendFormat:@" — %@", name];
    if (display && display->has_focus) [title appendString:@" — Focused"];

    return title;
}

static NSString *
yb_display_accessibility_title(const struct menu_bar_display *display, size_t fallback_index)
{
    NSMutableString *title = [NSMutableString stringWithFormat:@"Display %u", yb_display_number(display, fallback_index)];
    NSString *label = yb_nsstring(display ? display->label : NULL);
    NSString *name = yb_nsstring(display ? display->name : NULL);

    if ([label length]) [title appendFormat:@", %@", label];
    if ([name length] && ![name isEqualToString:label]) [title appendFormat:@", %@", name];
    if (display && display->has_focus) [title appendString:@", focused display"];

    return title;
}

static NSString *
yb_app_summary(const struct menu_bar_space *space, NSUInteger limit)
{
    if (!space || space->app_count == 0) {
        if (space && space->membership == MENU_BAR_SPACE_MEMBERSHIP_UNKNOWN) return @"apps unknown";
        return @"empty";
    }

    NSMutableArray *parts = [NSMutableArray array];
    size_t shown = 0;
    for (size_t i = 0; i < space->app_count && shown < limit; ++i) {
        NSString *name = yb_nsstring(space->apps[i].name);
        if (![name length]) continue;

        if (space->apps[i].window_count > 1) {
            [parts addObject:[NSString stringWithFormat:@"%@ (%u)", name, space->apps[i].window_count]];
        } else {
            [parts addObject:name];
        }
        ++shown;
    }

    if (![parts count]) return @"empty";

    NSMutableString *summary = [NSMutableString stringWithString:[parts componentsJoinedByString:@", "]];
    if (space->app_count > shown) [summary appendFormat:@", +%zu", space->app_count - shown];
    return summary;
}

static NSString *
yb_space_display_label(const struct menu_bar_space *space)
{
    if (!space) return @"—";

    NSString *label = yb_nsstring(space->label);
    if ([label length]) return [NSString stringWithFormat:@"%u %@", space->mission_control_index, label];
    return [NSString stringWithFormat:@"%u", space->mission_control_index];
}

static NSString *
yb_space_menu_title(const struct menu_bar_space *space)
{
    NSMutableString *title = [NSMutableString stringWithString:yb_space_display_label(space)];
    if (space && space->is_native_fullscreen) [title appendString:@" — Native Full Screen"];
    [title appendFormat:@" — %@", yb_app_summary(space, 4)];
    return title;
}

static NSString *
yb_space_accessibility_title(const struct menu_bar_display *display, size_t display_index, const struct menu_bar_space *space)
{
    NSMutableString *title = [NSMutableString stringWithFormat:@"%@, space %@", yb_display_accessibility_title(display, display_index), yb_space_display_label(space)];
    if (space && space->is_visible) [title appendString:@", visible on this display"];
    if (space && space->is_native_fullscreen) [title appendString:@", native full screen"];
    [title appendFormat:@", %@", yb_app_summary(space, 4)];
    return title;
}

static const struct menu_bar_display *
yb_find_focused_display(const struct menu_bar_snapshot *snapshot, size_t *display_index)
{
    if (!snapshot || !snapshot->has_focused_display) return NULL;

    for (size_t i = 0; i < snapshot->display_count; ++i) {
        const struct menu_bar_display *display = &snapshot->displays[i];
        if (display->has_focus || display->display_id == snapshot->focused_display_id) {
            if (display_index) *display_index = i;
            return display;
        }
    }

    return NULL;
}

static const struct menu_bar_space *
yb_find_visible_space(const struct menu_bar_display *display, enum menu_bar_space_filter filter)
{
    if (!display) return NULL;

    for (size_t i = 0; i < display->space_count; ++i) {
        const struct menu_bar_space *space = &display->spaces[i];
        if (space->is_visible && yb_space_matches_filter(space, filter)) return space;
    }

    for (size_t i = 0; i < display->space_count; ++i) {
        const struct menu_bar_space *space = &display->spaces[i];
        if (space->is_visible) return space;
    }

    return NULL;
}

static CGFloat
yb_title_width(NSString *title, NSFont *font)
{
    if (![title length]) return 0.0;
    NSDictionary *attributes = [NSDictionary dictionaryWithObject:font forKey:NSFontAttributeName];
    return ceil([title sizeWithAttributes:attributes].width);
}

static YBMenuBarActionTarget *
yb_action_target(enum yb_menu_bar_control_kind kind, uint64_t generation)
{
    YBMenuBarActionTarget *target = [[[YBMenuBarActionTarget alloc] init] autorelease];
    target->yb_kind = kind;
    target->yb_generation = generation;
    return target;
}

static NSMenuItem *
yb_menu_item(NSString *title, SEL action, id target, YBMenuBarActionTarget *represented_object)
{
    NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:@""] autorelease];
    [item setTarget:target];
    if (represented_object) [item setRepresentedObject:represented_object];
    return item;
}

struct yb_screen_entry
{
    const struct menu_bar_display *display;
    const struct menu_bar_space *space;
    size_t display_index;
};

struct yb_screen_selection
{
    size_t start;
    size_t count;
};

static CGFloat
yb_overflow_width(size_t omitted)
{
    if (!omitted) return 0.0;
    NSString *title = [NSString stringWithFormat:@"…+%zu", omitted];
    return MIN(64.0, MAX(34.0, yb_title_width(title, [NSFont menuBarFontOfSize:0.0]) + 14.0));
}

static CGFloat
yb_screen_interval_width(const struct yb_screen_entry *entries, size_t start, size_t count, size_t omitted)
{
    CGFloat width = YB_MENU_BAR_OUTER_INSET;
    for (size_t i = 0; i < count; ++i) {
        if (i) {
            width += entries[start + i - 1].display == entries[start + i].display
                   ? YB_MENU_BAR_GAP : YB_MENU_BAR_DISPLAY_GAP;
        }
        const struct menu_bar_space *space = entries[start + i].space;
        width += yb_screen_cell_width(space ? space->mission_control_index : 0);
    }
    if (omitted) width += YB_MENU_BAR_GAP + yb_overflow_width(omitted);
    return width + YB_MENU_BAR_OUTER_INSET;
}

static bool
yb_screen_interval_fits(const struct yb_screen_entry *entries,
                        size_t total,
                        size_t start,
                        size_t count)
{
    return yb_screen_interval_width(entries, start, count, total - count) <= YB_MENU_BAR_MAX_EXPANDED_WIDTH;
}

static struct yb_screen_selection
yb_select_screen_interval(const struct yb_screen_entry *entries, size_t total, size_t active)
{
    struct yb_screen_selection selection = { 0, 0 };
    if (!entries || !total) return selection;
    if (yb_screen_interval_width(entries, 0, total, 0) <= YB_MENU_BAR_MAX_EXPANDED_WIDTH) {
        selection.count = total;
        return selection;
    }

    if (active >= total) active = 0;
    selection.start = active;
    selection.count = 1;

    bool prefer_left = true;
    while (selection.count < total) {
        bool added = false;
        for (int attempt = 0; attempt < 2; ++attempt) {
            bool try_left = attempt == 0 ? prefer_left : !prefer_left;
            size_t candidate_start = selection.start;
            if (try_left) {
                if (selection.start == 0) continue;
                --candidate_start;
            } else if (selection.start + selection.count >= total) {
                continue;
            }

            if (!yb_screen_interval_fits(entries, total, candidate_start, selection.count + 1)) continue;
            selection.start = candidate_start;
            ++selection.count;
            prefer_left = !try_left;
            added = true;
            break;
        }
        if (!added) break;
    }

    return selection;
}

static NSMenuItem *
yb_informational_menu_item(NSString *title, NSInteger indentation, NSString *tool_tip)
{
    NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""] autorelease];
    [item setEnabled:YES];
    [item setIndentationLevel:indentation];
    [item setToolTip:tool_tip];
    [item setAccessibilityRole:NSAccessibilityStaticTextRole];
    [item setAccessibilityLabel:title];
    return item;
}

@implementation YBMenuBarController

- (id)initWithCallbacks:(const struct menu_bar_callbacks *)callbacks
{
    self = [super init];
    if (!self) return nil;

    if (callbacks) _callbacks = *callbacks;
    return self;
}

- (void)dealloc
{
    [self shutdown];
    [super dealloc];
}

- (void)shutdown
{
    [self clearStatusItem];
}

- (void)beginMenuTracking
{
    if (_is_tracking_menu) return;

    _is_tracking_menu = true;
    _tracking_generation = _snapshot ? _snapshot->generation : 0;

    struct menu_bar_action action = {0};
    action.type = MENU_BAR_ACTION_MENU_TRACKING_CHANGED;
    action.generation = _tracking_generation;
    action.data.menu_tracking.is_tracking = true;
    action.data.menu_tracking.focused_display_id = _snapshot && _snapshot->has_focused_display
                                                 ? _snapshot->focused_display_id
                                                 : 0;
    [self sendAction:action];
}

- (void)endMenuTrackingApplyDeferred:(bool)apply_deferred
{
    if (!_is_tracking_menu) return;

    uint64_t generation = _tracking_generation;
    _is_tracking_menu = false;
    _tracking_generation = 0;

    struct menu_bar_action action = {0};
    action.type = MENU_BAR_ACTION_MENU_TRACKING_CHANGED;
    action.generation = generation;
    action.data.menu_tracking.is_tracking = false;
    [self sendAction:action];

    if (apply_deferred && _deferred_snapshot) {
        struct menu_bar_snapshot *snapshot = _deferred_snapshot;
        _deferred_snapshot = NULL;
        [self receiveSnapshot:snapshot clear:false];
    }
}

- (void)clearStatusItem
{
    _is_tearing_down_status_item = true;

    if (_menu) {
        [_menu cancelTracking];
        [self endMenuTrackingApplyDeferred:false];
        [_menu setDelegate:nil];
    } else {
        [self endMenuTrackingApplyDeferred:false];
    }

    if (_status_item) {
        [_status_item setMenu:nil];
        [_status_item setView:nil];
        [[NSStatusBar systemStatusBar] removeStatusItem:_status_item];
        [_status_item release];
        _status_item = nil;
    }

    [_strip_view release];
    _strip_view = nil;

    [_menu release];
    _menu = nil;

    if (_snapshot) {
        menu_bar_model_snapshot_destroy(_snapshot);
        _snapshot = NULL;
    }

    if (_deferred_snapshot) {
        menu_bar_model_snapshot_destroy(_deferred_snapshot);
        _deferred_snapshot = NULL;
    }

    _is_tracking_menu = false;
    _tracking_generation = 0;
    _is_tearing_down_status_item = false;
}

- (void)ensureStatusItem
{
    if (_status_item) return;

    _status_item = [[[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength] retain];
}

- (void)receiveSnapshot:(struct menu_bar_snapshot *)snapshot clear:(bool)clear
{
    if (clear || !snapshot || snapshot->settings.enabled != MENU_BAR_ENABLED) {
        if (snapshot) menu_bar_model_snapshot_destroy(snapshot);
        [self clearStatusItem];
        return;
    }

    if (_is_tracking_menu) {
        if (_deferred_snapshot) menu_bar_model_snapshot_destroy(_deferred_snapshot);
        _deferred_snapshot = snapshot;
        return;
    }

    if (_snapshot) menu_bar_model_snapshot_destroy(_snapshot);
    _snapshot = snapshot;
    [self renderSnapshot];
}

- (void)renderSnapshot
{
    if (!_snapshot) {
        [self clearStatusItem];
        return;
    }

    [self ensureStatusItem];
    [self rebuildMenu];

    if (_snapshot->settings.style == MENU_BAR_STYLE_EXPANDED) {
        [self renderExpandedStatusItem];
    } else {
        [self renderCompactStatusItem];
    }
}

- (void)rebuildMenu
{
    if (_menu) {
        [_menu cancelTracking];
        [_menu setDelegate:nil];
    }

    [_menu release];
    _menu = [[NSMenu alloc] initWithTitle:@"yabai"];
    [_menu setDelegate:self];
    [_menu setAutoenablesItems:NO];

    NSMenuItem *title = yb_informational_menu_item(@"yabai Spaces — Read Only",
                                                   0,
                                                   @"Space and application information; use the controls below to change the indicator layout.");
    [_menu addItem:title];

    NSString *failure = yb_nsstring(_snapshot->action_failure_text);
    if ([failure length]) {
        NSMenuItem *failure_item = yb_informational_menu_item([NSString stringWithFormat:@"Last action failed: %@", failure],
                                                              0,
                                                              @"The most recent menu bar setting change failed.");
        [_menu addItem:failure_item];
    }

    [_menu addItem:[NSMenuItem separatorItem]];

    bool added_display = false;
    for (size_t display_index = 0; display_index < _snapshot->display_count; ++display_index) {
        const struct menu_bar_display *display = &_snapshot->displays[display_index];
        NSMenuItem *display_item = yb_informational_menu_item(yb_display_menu_title(display, display_index),
                                                              0,
                                                              yb_display_accessibility_title(display, display_index));
        if (display->has_focus) [display_item setState:NSControlStateValueMixed];
        [_menu addItem:display_item];

        bool added_space = false;
        for (size_t space_index = 0; space_index < display->space_count; ++space_index) {
            const struct menu_bar_space *space = &display->spaces[space_index];
            if (!yb_space_matches_filter(space, _snapshot->settings.space_filter)) continue;

            NSMenuItem *space_item = yb_informational_menu_item(yb_space_menu_title(space),
                                                                1,
                                                                yb_space_accessibility_title(display, display_index, space));
            [space_item setState:space->is_visible ? NSControlStateValueOn : NSControlStateValueOff];
            [_menu addItem:space_item];
            added_space = true;
        }

        if (!added_space) {
            [_menu addItem:yb_informational_menu_item(@"No matching spaces", 1, @"No spaces match the current filter.")];
        }

        [_menu addItem:[NSMenuItem separatorItem]];
        added_display = true;
    }

    if (!added_display) {
        [_menu addItem:yb_informational_menu_item(@"No displays available", 0, @"macOS did not report any displays.")];
        [_menu addItem:[NSMenuItem separatorItem]];
    }

    enum menu_bar_space_filter next_filter = _snapshot->settings.space_filter == MENU_BAR_SPACE_FILTER_ALL
                                           ? MENU_BAR_SPACE_FILTER_OCCUPIED
                                           : MENU_BAR_SPACE_FILTER_ALL;
    YBMenuBarActionTarget *filter_target = yb_action_target(YB_MENU_BAR_CONTROL_SET_FILTER, _snapshot->generation);
    filter_target->yb_filter = next_filter;
    NSMenuItem *filter_item = yb_menu_item(@"Show empty spaces", @selector(performMenuAction:), self, filter_target);
    [filter_item setState:_snapshot->settings.space_filter == MENU_BAR_SPACE_FILTER_ALL ? NSControlStateValueOn : NSControlStateValueOff];
    [filter_item setToolTip:@"Toggle between occupied-or-visible spaces and all spaces."];
    [_menu addItem:filter_item];

    NSMenuItem *style_root = [[[NSMenuItem alloc] initWithTitle:@"Menu bar layout" action:nil keyEquivalent:@""] autorelease];
    NSMenu *style_menu = [[[NSMenu alloc] initWithTitle:@"Menu bar layout"] autorelease];
    [style_menu setAutoenablesItems:NO];

    YBMenuBarActionTarget *compact_target = yb_action_target(YB_MENU_BAR_CONTROL_SET_STYLE, _snapshot->generation);
    compact_target->yb_style = MENU_BAR_STYLE_COMPACT;
    NSMenuItem *compact = yb_menu_item(@"Compact", @selector(performMenuAction:), self, compact_target);
    [compact setState:_snapshot->settings.style == MENU_BAR_STYLE_COMPACT ? NSControlStateValueOn : NSControlStateValueOff];
    [style_menu addItem:compact];

    YBMenuBarActionTarget *expanded_target = yb_action_target(YB_MENU_BAR_CONTROL_SET_STYLE, _snapshot->generation);
    expanded_target->yb_style = MENU_BAR_STYLE_EXPANDED;
    NSMenuItem *expanded = yb_menu_item(@"Expanded", @selector(performMenuAction:), self, expanded_target);
    [expanded setState:_snapshot->settings.style == MENU_BAR_STYLE_EXPANDED ? NSControlStateValueOn : NSControlStateValueOff];
    [style_menu addItem:expanded];

    [style_root setSubmenu:style_menu];
    [_menu addItem:style_root];
}

- (YBMenuBarScreenCell *)screenCellForDisplay:(const struct menu_bar_display *)display
                                 displayIndex:(size_t)display_index
                                        space:(const struct menu_bar_space *)space
                                        frame:(NSRect)frame
{
    YBMenuBarScreenCell *cell = [[[YBMenuBarScreenCell alloc] initWithFrame:frame] autorelease];
    [cell setTitle:@""];
    [cell setImage:nil];
    [cell setBordered:NO];
    [cell setButtonType:NSButtonTypeMomentaryPushIn];
    [cell setTarget:self];
    [cell setAction:@selector(performStripAction:)];
    cell->yb_kind = YB_MENU_BAR_CONTROL_OPEN_MENU;
    cell->yb_generation = _snapshot ? _snapshot->generation : 0;
    cell->yb_state = yb_screen_state(display, space);
    cell->yb_index = space ? space->mission_control_index : 0;

    NSString *description = display && space
                          ? [NSString stringWithFormat:@"%@; read-only; opens yabai Spaces menu",
                             yb_space_accessibility_title(display, display_index, space)]
                          : @"Focused display or visible space unresolved; read-only; opens yabai Spaces menu";
    [cell setToolTip:description];
    [cell setAccessibilityRole:NSAccessibilityButtonRole];
    [cell setAccessibilityLabel:description];
    return cell;
}

- (NSView *)buildCompactStrip
{
    size_t display_index = 0;
    const struct menu_bar_display *display = yb_find_focused_display(_snapshot, &display_index);
    if (!display && _snapshot && _snapshot->display_count == 1) display = &_snapshot->displays[0];
    const struct menu_bar_space *space = display
                                      ? yb_find_visible_space(display, _snapshot->settings.space_filter)
                                      : NULL;
    CGFloat cell_width = yb_screen_cell_width(space ? space->mission_control_index : 0);
    CGFloat width = YB_MENU_BAR_OUTER_INSET * 2.0 + cell_width;
    YBMenuBarStripView *strip = [[[YBMenuBarStripView alloc] initWithFrame:NSMakeRect(0.0, 0.0, width, YB_MENU_BAR_STATUS_HEIGHT)] autorelease];

    YBMenuBarScreenCell *cell = [self screenCellForDisplay:display
                                              displayIndex:display_index
                                                     space:space
                                                     frame:NSMakeRect(YB_MENU_BAR_OUTER_INSET, 0.0,
                                                                      cell_width,
                                                                      YB_MENU_BAR_STATUS_HEIGHT)];
    [strip addSubview:cell];
    return strip;
}

- (void)renderCompactStatusItem
{
    [_status_item setMenu:nil];
    [_status_item setView:nil];
    [_strip_view release];
    _strip_view = [[self buildCompactStrip] retain];
    [_status_item setLength:NSWidth([_strip_view frame])];
    [_status_item setView:_strip_view];
}

- (YBMenuBarButton *)buttonWithTitle:(NSString *)title frame:(NSRect)frame kind:(enum yb_menu_bar_control_kind)kind
{
    YBMenuBarButton *button = [[[YBMenuBarButton alloc] initWithFrame:frame] autorelease];
    [button setTitle:title];
    [button setButtonType:NSButtonTypeMomentaryPushIn];
    [button setBezelStyle:NSBezelStyleTexturedRounded];
    [button setBordered:NO];
    [button setFont:[NSFont menuBarFontOfSize:0.0]];
    [button setTarget:self];
    [button setAction:@selector(performStripAction:)];
    button->yb_kind = kind;
    button->yb_generation = _snapshot ? _snapshot->generation : 0;
    return button;
}

- (NSView *)buildExpandedStrip
{
    size_t total = 0;
    for (size_t display_index = 0; _snapshot && display_index < _snapshot->display_count; ++display_index) {
        const struct menu_bar_display *display = &_snapshot->displays[display_index];
        for (size_t space_index = 0; space_index < display->space_count; ++space_index) {
            if (yb_space_matches_filter(&display->spaces[space_index], _snapshot->settings.space_filter)) ++total;
        }
    }

    struct yb_screen_entry *entries = total ? calloc(total, sizeof(struct yb_screen_entry)) : NULL;
    size_t output = 0;
    size_t active = total;
    if (!total || entries) {
        for (size_t display_index = 0; _snapshot && display_index < _snapshot->display_count; ++display_index) {
            const struct menu_bar_display *display = &_snapshot->displays[display_index];
            for (size_t space_index = 0; space_index < display->space_count; ++space_index) {
                const struct menu_bar_space *space = &display->spaces[space_index];
                if (!yb_space_matches_filter(space, _snapshot->settings.space_filter)) continue;
                entries[output] = (struct yb_screen_entry) { display, space, display_index };
                if (display->has_focus && space->is_visible) active = output;
                ++output;
            }
        }
    }
    total = output;

    struct yb_screen_selection selection = yb_select_screen_interval(entries, total, active);
    size_t omitted = total - selection.count;
    CGFloat strip_width = total
                        ? yb_screen_interval_width(entries, selection.start, selection.count, omitted)
                        : YB_MENU_BAR_OUTER_INSET * 2.0 + YB_MENU_BAR_CELL_WIDTH;
    YBMenuBarStripView *strip = [[[YBMenuBarStripView alloc] initWithFrame:NSMakeRect(0.0, 0.0, strip_width, YB_MENU_BAR_STATUS_HEIGHT)] autorelease];

    CGFloat x = YB_MENU_BAR_OUTER_INSET;
    if (!selection.count) {
        YBMenuBarScreenCell *placeholder = [self screenCellForDisplay:NULL
                                                         displayIndex:0
                                                                space:NULL
                                                                frame:NSMakeRect(x, 0.0,
                                                                                 YB_MENU_BAR_CELL_WIDTH,
                                                                                 YB_MENU_BAR_STATUS_HEIGHT)];
        [strip addSubview:placeholder];
        x += YB_MENU_BAR_CELL_WIDTH;
    }

    for (size_t i = 0; i < selection.count; ++i) {
        size_t entry_index = selection.start + i;
        const struct yb_screen_entry *entry = &entries[entry_index];
        if (i) {
            x += entries[entry_index - 1].display == entry->display
               ? YB_MENU_BAR_GAP : YB_MENU_BAR_DISPLAY_GAP;
        }
        CGFloat cell_width = yb_screen_cell_width(entry->space->mission_control_index);
        YBMenuBarScreenCell *cell = [self screenCellForDisplay:entry->display
                                                  displayIndex:entry->display_index
                                                         space:entry->space
                                                         frame:NSMakeRect(x, 0.0,
                                                                          cell_width,
                                                                          YB_MENU_BAR_STATUS_HEIGHT)];
        [strip addSubview:cell];
        x += cell_width;
    }

    if (omitted) {
        x += YB_MENU_BAR_GAP;
        NSString *title = [NSString stringWithFormat:@"…+%zu", omitted];
        CGFloat width = yb_overflow_width(omitted);
        YBMenuBarButton *overflow = [self buttonWithTitle:title
                                                    frame:NSMakeRect(x, 0.0, width, YB_MENU_BAR_STATUS_HEIGHT)
                                                     kind:YB_MENU_BAR_CONTROL_OPEN_MENU];
        [overflow setToolTip:@"Open menu for omitted spaces"];
        [overflow setAccessibilityLabel:@"Open yabai Spaces menu for omitted spaces"];
        [strip addSubview:overflow];
    }

    free(entries);
    return strip;
}

- (void)renderExpandedStatusItem
{
    [_status_item setMenu:nil];
    [_status_item setView:nil];
    [_strip_view release];
    _strip_view = [[self buildExpandedStrip] retain];
    [_status_item setLength:NSWidth([_strip_view frame])];
    [_status_item setView:_strip_view];
}

- (void)sendAction:(struct menu_bar_action)action
{
    if (_callbacks.action) _callbacks.action(&action, _callbacks.context);
}

- (void)performActionKind:(enum yb_menu_bar_control_kind)kind generation:(uint64_t)generation style:(enum menu_bar_style)style filter:(enum menu_bar_space_filter)filter
{
    struct menu_bar_action action = {0};
    action.generation = generation;

    switch (kind) {
    case YB_MENU_BAR_CONTROL_SET_STYLE:
        action.type = MENU_BAR_ACTION_SET_STYLE;
        action.data.set_style.style = style;
        break;
    case YB_MENU_BAR_CONTROL_SET_FILTER:
        action.type = MENU_BAR_ACTION_SET_SPACE_FILTER;
        action.data.set_space_filter.filter = filter;
        break;
    case YB_MENU_BAR_CONTROL_OPEN_MENU:
        [self openMenu:nil];
        return;
    default:
        return;
    }

    [self sendAction:action];
}

- (void)performMenuAction:(id)sender
{
    YBMenuBarActionTarget *target = [sender representedObject];
    if (![target isKindOfClass:[YBMenuBarActionTarget class]]) return;
    [self performActionKind:target->yb_kind
                 generation:target->yb_generation
                      style:target->yb_style
                     filter:target->yb_filter];
}

- (void)performStripAction:(id)sender
{
    if (![sender isKindOfClass:[YBMenuBarButton class]]) return;
    YBMenuBarButton *button = sender;
    if (button->yb_kind == YB_MENU_BAR_CONTROL_OPEN_MENU) {
        [self openMenu:sender];
        return;
    }

    [self performActionKind:button->yb_kind
                 generation:button->yb_generation
                      style:button->yb_style
                     filter:button->yb_filter];
}

- (void)openMenu:(id)sender
{
    if (!_status_item || !_menu) return;

    NSView *view = [sender isKindOfClass:[NSView class]] ? (NSView *) sender : _strip_view;
    if (!view) view = [_status_item button];
    if (!view) return;

    NSPoint point = NSMakePoint(NSMidX([view bounds]), NSMinY([view bounds]));
    [_menu popUpMenuPositioningItem:nil atLocation:point inView:view];
}

- (void)menuWillOpen:(NSMenu *)menu
{
    (void) menu;
    [self beginMenuTracking];
}

- (void)menuDidClose:(NSMenu *)menu
{
    (void) menu;
    [self endMenuTrackingApplyDeferred:!_is_tearing_down_status_item];
}

#if defined(YB_MENU_BAR_TEST)
- (void)testSetSnapshot:(struct menu_bar_snapshot *)snapshot
{
    if (_snapshot) menu_bar_model_snapshot_destroy(_snapshot);
    _snapshot = snapshot;
}

- (void)testSetMenu:(NSMenu *)menu
{
    if (_menu == menu) return;

    [_menu setDelegate:nil];
    [_menu release];
    _menu = [menu retain];
    [_menu setDelegate:self];
}

- (void)testRebuildMenu
{
    [self rebuildMenu];
}

- (NSMenu *)testMenu
{
    return _menu;
}

- (bool)testHasSnapshot
{
    return _snapshot != NULL;
}

- (bool)testHasDeferredSnapshot
{
    return _deferred_snapshot != NULL;
}

- (bool)testHasStatusItem
{
    return _status_item != nil;
}

- (NSUInteger)testStatusItemCount
{
    return _status_item ? 1 : 0;
}

- (NSUInteger)testStatusButtonCount
{
    return [[_strip_view subviews] count];
}

- (bool)testStatusItemOwnsStripView
{
    return _status_item && [_status_item view] == _strip_view;
}

- (bool)testIsTrackingMenu
{
    return _is_tracking_menu;
}

- (uint64_t)testTrackingGeneration
{
    return _tracking_generation;
}

- (NSView *)testBuildCompactStrip
{
    return [self buildCompactStrip];
}

- (NSView *)testBuildExpandedStrip
{
    return [self buildExpandedStrip];
}
#endif

@end

static void
menu_bar_destroy_pending_locked(void)
{
    if (g_menu_bar_pending_snapshot) {
        menu_bar_model_snapshot_destroy(g_menu_bar_pending_snapshot);
        g_menu_bar_pending_snapshot = NULL;
    }
    g_menu_bar_pending_has_value = false;
    g_menu_bar_pending_clear = false;
}

static void
menu_bar_delivery_main(void *context)
{
    uint64_t epoch = (uint64_t)(uintptr_t) context;
    YBMenuBarController *controller = nil;
    struct menu_bar_snapshot *snapshot = NULL;
    bool clear = false;
    bool should_deliver = false;

    pthread_mutex_lock(&g_menu_bar_lock);
    if (g_menu_bar_initialized &&
        g_menu_bar_lifecycle_epoch == epoch &&
        g_menu_bar_delivery_scheduled &&
        g_menu_bar_delivery_epoch == epoch &&
        g_menu_bar_pending_has_value) {
        controller = g_menu_bar_controller;
        if (controller) [controller retain];
        snapshot = g_menu_bar_pending_snapshot;
        clear = g_menu_bar_pending_clear;
        g_menu_bar_pending_snapshot = NULL;
        g_menu_bar_pending_has_value = false;
        g_menu_bar_pending_clear = false;
        g_menu_bar_delivery_scheduled = false;
        g_menu_bar_delivery_epoch = 0;
        should_deliver = true;
    }
    pthread_mutex_unlock(&g_menu_bar_lock);

    if (!should_deliver) return;

    if (controller) {
        [controller receiveSnapshot:snapshot clear:clear];
        [controller release];
    } else if (snapshot) {
        menu_bar_model_snapshot_destroy(snapshot);
    }
}

static void
menu_bar_controller_shutdown_main(void *context)
{
    YBMenuBarController *controller = (YBMenuBarController *) context;
    [controller shutdown];
    [controller release];
}

bool
menu_bar_init(const struct menu_bar_callbacks *callbacks)
{
    if (!pthread_main_np()) return false;

    YBMenuBarController *controller = [[YBMenuBarController alloc] initWithCallbacks:callbacks];
    if (!controller) return false;

    pthread_mutex_lock(&g_menu_bar_lock);
    if (g_menu_bar_initialized || g_menu_bar_controller) {
        pthread_mutex_unlock(&g_menu_bar_lock);
        [controller release];
        return false;
    }

    g_menu_bar_controller = controller;
    g_menu_bar_initialized = true;
    ++g_menu_bar_lifecycle_epoch;
    if (g_menu_bar_lifecycle_epoch == 0) ++g_menu_bar_lifecycle_epoch;
    g_menu_bar_latest_generation = 0;
    menu_bar_destroy_pending_locked();
    g_menu_bar_delivery_scheduled = false;
    g_menu_bar_delivery_epoch = 0;
    pthread_mutex_unlock(&g_menu_bar_lock);

    return true;
}

void
menu_bar_shutdown(void)
{
    YBMenuBarController *controller = nil;

    pthread_mutex_lock(&g_menu_bar_lock);
    ++g_menu_bar_lifecycle_epoch;
    if (g_menu_bar_lifecycle_epoch == 0) ++g_menu_bar_lifecycle_epoch;
    g_menu_bar_initialized = false;
    g_menu_bar_latest_generation = 0;
    menu_bar_destroy_pending_locked();
    g_menu_bar_delivery_scheduled = false;
    g_menu_bar_delivery_epoch = 0;
    controller = g_menu_bar_controller;
    g_menu_bar_controller = nil;
    pthread_mutex_unlock(&g_menu_bar_lock);

    if (!controller) return;

    if (pthread_main_np()) {
        menu_bar_controller_shutdown_main(controller);
    } else {
        dispatch_async_f(dispatch_get_main_queue(), controller, menu_bar_controller_shutdown_main);
    }
}

void
menu_bar_publish_snapshot(struct menu_bar_snapshot *snapshot)
{
    bool should_schedule = false;
    uint64_t epoch = 0;

    pthread_mutex_lock(&g_menu_bar_lock);
    if (!g_menu_bar_initialized || !g_menu_bar_controller) {
        pthread_mutex_unlock(&g_menu_bar_lock);
        if (snapshot) menu_bar_model_snapshot_destroy(snapshot);
        return;
    }

    if (snapshot && snapshot->generation < g_menu_bar_latest_generation) {
        pthread_mutex_unlock(&g_menu_bar_lock);
        menu_bar_model_snapshot_destroy(snapshot);
        return;
    }

    if (snapshot) g_menu_bar_latest_generation = snapshot->generation;

    if (g_menu_bar_pending_snapshot) {
        menu_bar_model_snapshot_destroy(g_menu_bar_pending_snapshot);
        g_menu_bar_pending_snapshot = NULL;
    }

    g_menu_bar_pending_snapshot = snapshot;
    g_menu_bar_pending_clear = snapshot == NULL;
    g_menu_bar_pending_has_value = true;

    if (!g_menu_bar_delivery_scheduled) {
        g_menu_bar_delivery_scheduled = true;
        g_menu_bar_delivery_epoch = g_menu_bar_lifecycle_epoch;
        epoch = g_menu_bar_delivery_epoch;
        should_schedule = true;
    }
    pthread_mutex_unlock(&g_menu_bar_lock);

    if (should_schedule) {
        dispatch_async_f(dispatch_get_main_queue(), (void *)(uintptr_t) epoch, menu_bar_delivery_main);
    }
}
