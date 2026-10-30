#include "menu_bar.h"
#include "accessibility_preferences.h"

#import <AppKit/AppKit.h>
#include <dispatch/dispatch.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 270000
#define YB_MENU_BAR_HAS_EXPANDED_INTERFACE_SDK 1
#else
#define YB_MENU_BAR_HAS_EXPANDED_INTERFACE_SDK 0
@class NSStatusItemExpandedInterfaceSession;
@interface NSStatusItem (YBExpandedInterfaceCompatibility)
- (void)setExpandedInterfaceDelegate:(id)delegate;
@end
@interface NSStatusItemExpandedInterfaceSession : NSObject
- (void)cancel;
@end
#endif

/* The expanded-interface contract is Objective-C message based. Compile the
 * runtime-guarded implementation with older SDKs as well so macOS 27 hosts do
 * not silently fall back to the native menu in Nix/CI builds. */
#define YB_MENU_BAR_HAS_EXPANDED_INTERFACE 1

#define YB_MENU_BAR_MAX_EXPANDED_WIDTH  520.0
#define YB_MENU_BAR_MAX_AUTOMATIC_WIDTH 260.0
#define YB_MENU_BAR_STATUS_HEIGHT       22.0
#define YB_MENU_BAR_BUTTON_PAD_X         7.0
#define YB_MENU_BAR_GAP                  0.0
#define YB_MENU_BAR_DISPLAY_GAP          8.0
#define YB_MENU_BAR_DISPLAY_NEUTRAL_ALPHA 0.035
#define YB_MENU_BAR_DISPLAY_TINT_ALPHA    0.075
#define YB_MENU_BAR_SEPARATOR_WIDTH       2.0
#define YB_MENU_BAR_SEPARATOR_ALPHA       0.85
#define YB_MENU_BAR_SEPARATOR_CONTRAST_ALPHA 1.0
#define YB_MENU_BAR_OUTER_INSET          2.0
#define YB_MENU_BAR_CELL_WIDTH          25.0
#define YB_MENU_BAR_SCREEN_WIDTH        22.0
#define YB_MENU_BAR_SCREEN_HEIGHT       18.0
#define YB_MENU_BAR_SCREEN_RADIUS        3.0
#define YB_MENU_BAR_INDEX_FONT_SIZE     12.0
#define YB_MENU_BAR_INDEX_PAD_X          8.0
#define YB_MENU_BAR_LONG_PRESS_DURATION  0.45
#define YB_MENU_BAR_DRAG_TOLERANCE       4.0

static int g_yb_menu_bar_contrast_override = -1;
#if defined(YB_MENU_BAR_TEST)
static bool g_yb_menu_bar_accessibility_override = true;
static struct accessibility_display_preferences g_yb_menu_bar_accessibility_preferences;
static int g_yb_menu_bar_panel_capability_override = -1;
#endif

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
    YB_MENU_BAR_CONTROL_FOCUS_SPACE = 0,
    YB_MENU_BAR_CONTROL_OPEN_MENU,
    YB_MENU_BAR_CONTROL_SET_STYLE,
    YB_MENU_BAR_CONTROL_SET_FILTER
};

@class YBMenuBarController;

@interface YBMenuBarExpandedPanel : NSPanel {
    YBMenuBarController *_yb_controller;
}
@property(assign) YBMenuBarController *yb_controller;
@end

@interface YBMenuBarSpaceCard : NSButton
@end

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
    size_t yb_display_index;
    uint64_t yb_space_id;
    bool yb_pressed;
}
- (void)setYBPressed:(bool)pressed;
@end

@interface YBMenuBarActionTarget : NSObject {
@public
    enum yb_menu_bar_control_kind yb_kind;
    uint64_t yb_generation;
    uint64_t yb_space_id;
    enum menu_bar_style yb_style;
    enum menu_bar_space_filter yb_filter;
}
@end

@implementation YBMenuBarActionTarget
@end

@interface YBMenuBarStripView : NSView {
@public
    YBMenuBarController *yb_controller;
    NSTimer *yb_long_press_timer;
    NSPoint yb_mouse_down_point;
    bool yb_long_press_fired;
    bool yb_click_cancelled;
    bool yb_expanded_interface;
}
- (void)activatePoint:(NSPoint)point;
@end

static bool yb_increased_contrast(void);

static struct accessibility_display_preferences
yb_accessibility_preferences(void)
{
#if defined(YB_MENU_BAR_TEST)
    if (g_yb_menu_bar_accessibility_override) return g_yb_menu_bar_accessibility_preferences;
#endif
    return accessibility_preferences_snapshot();
}

static void
yb_draw_display_group(NSRect group_rect, size_t display_index,
                      struct accessibility_display_preferences preferences)
{
    bool alternate = display_index % 2;
    if (preferences.reduce_transparency) {
        NSColor *color = alternate ? [NSColor selectedContentBackgroundColor]
                                   : [NSColor controlBackgroundColor];
        [color setFill];
    } else {
        NSColor *color = alternate ? [NSColor controlAccentColor] : [NSColor labelColor];
        CGFloat alpha = alternate
                      ? (yb_increased_contrast() ? 0.12 : YB_MENU_BAR_DISPLAY_TINT_ALPHA)
                      : (yb_increased_contrast() ? 0.07 : YB_MENU_BAR_DISPLAY_NEUTRAL_ALPHA);
        [[color colorWithAlphaComponent:alpha] setFill];
    }
    [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(group_rect, -1.0, 2.0)
                                      xRadius:4.0
                                      yRadius:4.0] fill];
}

static void
yb_draw_display_separator(NSView *strip, NSRect left_cell, NSRect right_cell)
{
    CGFloat x = floor((NSMaxX(left_cell) + NSMinX(right_cell)) * 0.5);
    NSBezierPath *separator = [NSBezierPath bezierPath];
    [separator moveToPoint:NSMakePoint(x, 4.0)];
    [separator lineToPoint:NSMakePoint(x, NSHeight([strip bounds]) - 4.0)];
    [separator setLineWidth:YB_MENU_BAR_SEPARATOR_WIDTH];
    [[[NSColor labelColor] colorWithAlphaComponent:yb_increased_contrast()
                                                   ? YB_MENU_BAR_SEPARATOR_CONTRAST_ALPHA
                                                   : YB_MENU_BAR_SEPARATOR_ALPHA] setStroke];
    [separator stroke];
}

#if YB_MENU_BAR_HAS_EXPANDED_INTERFACE_SDK
@interface YBMenuBarController : NSObject <NSMenuDelegate, NSStatusItemExpandedInterfaceDelegate> {
#else
@interface YBMenuBarController : NSObject <NSMenuDelegate> {
#endif
    struct menu_bar_callbacks _callbacks;
    NSStatusItem *_status_item;
    NSMenu *_menu;
    NSView *_strip_view;
    struct menu_bar_snapshot *_snapshot;
    struct menu_bar_snapshot *_deferred_snapshot;
    id _expanded_session;
    YBMenuBarExpandedPanel *_expanded_panel;
    id _expanded_event_monitor;
    bool _panel_disabled;
    bool _is_tracking_menu;
    bool _is_tearing_down_status_item;
    uint64_t _tracking_generation;
#if defined(YB_MENU_BAR_TEST)
    NSUInteger _test_accessibility_redraw_count;
#endif
}
- (id)initWithCallbacks:(const struct menu_bar_callbacks *)callbacks;
- (void)shutdown;
- (void)receiveSnapshot:(struct menu_bar_snapshot *)snapshot clear:(bool)clear;
- (void)showStatusMenu:(id)sender;
- (void)performScreenAction:(id)sender;
- (void)performMenuAction:(id)sender;
- (void)performActionKind:(enum yb_menu_bar_control_kind)kind generation:(uint64_t)generation space:(uint64_t)space_id style:(enum menu_bar_style)style filter:(enum menu_bar_space_filter)filter;
- (void)clearNativeMenusFromStatusViews;
- (void)installStripView:(NSView *)strip;
- (void)installStatusStrip:(NSView *)strip;
- (void)accessibilityPreferencesChanged;
- (void)cancelExpandedInterface;
- (void)performPanelFilterAction:(id)sender;
- (void)performPanelStyleAction:(id)sender;
- (void)beginExpandedPanelWithSession:(id)session;
- (void)endExpandedPanel;
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
- (bool)testStatusButtonOwnsStripView;
- (bool)testStatusViewsUseManualMenu;
- (NSStatusBarButton *)testStatusButton;
- (NSView *)testStripView;
- (bool)testIsTrackingMenu;
- (uint64_t)testTrackingGeneration;
- (uint64_t)testSnapshotGeneration;
- (uint64_t)testDeferredSnapshotGeneration;
- (CGFloat)testStatusItemLength;
- (NSUInteger)testAccessibilityRedrawCount;
- (NSView *)testBuildCompactStrip;
- (NSView *)testBuildExpandedStrip;
- (bool)testWantsExpandedPanel;
- (void)testBeginExpandedInterfaceWithSession:(id)session;
- (void)testEndExpandedInterface;
- (NSPanel *)testExpandedPanel;
- (NSUInteger)testExpandedSpaceCardCount;
- (bool)testStatusItemHasMenu;
- (NSStatusBarButton *)testStatusItemButton;
#endif
@end

@implementation YBMenuBarExpandedPanel
@synthesize yb_controller = _yb_controller;
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return NO; }
- (void)keyDown:(NSEvent *)event
{
    if ([event keyCode] == 53) {
        [_yb_controller cancelExpandedInterface];
        return;
    }
    [super keyDown:event];
}
@end

@implementation YBMenuBarSpaceCard
- (void)keyDown:(NSEvent *)event
{
    if ([event keyCode] == 123 || [event keyCode] == 126) {
        [[self window] selectPreviousKeyView:self];
    } else if ([event keyCode] == 124 || [event keyCode] == 125) {
        [[self window] selectNextKeyView:self];
    } else if ([event keyCode] != 36 && [event keyCode] != 49) {
        [super keyDown:event];
    }
}
@end

@implementation YBMenuBarStripView
- (void)dealloc
{
    [yb_long_press_timer invalidate];
    [yb_long_press_timer release];
    [super dealloc];
}

- (void)cancelLongPressTimer
{
    [yb_long_press_timer invalidate];
    [yb_long_press_timer release];
    yb_long_press_timer = nil;
}

- (void)longPressTimerFired:(NSTimer *)timer
{
    if (timer != yb_long_press_timer || yb_click_cancelled) return;

    yb_long_press_fired = true;
    [self cancelLongPressTimer];
    [yb_controller showStatusMenu:self];
}

- (NSView *)hitTest:(NSPoint)point
{
    // Route the entire strip through one tracking view. Nested NSButton tracking
    // inside NSStatusBarButton is unreliable after the status item is rebuilt.
    return !yb_expanded_interface && NSPointInRect(point, [self bounds]) ? self : nil;
}

- (void)activatePoint:(NSPoint)point
{
    for (NSView *view in [[self subviews] reverseObjectEnumerator]) {
        if (!NSPointInRect(point, [view frame])) continue;
        if ([view isKindOfClass:[YBMenuBarScreenCell class]] &&
            [(NSButton *) view action] == @selector(performScreenAction:)) {
            [yb_controller performScreenAction:view];
        } else {
            [yb_controller showStatusMenu:view];
        }
        return;
    }

    [yb_controller showStatusMenu:self];
}

- (void)mouseDown:(NSEvent *)event
{
    [self cancelLongPressTimer];
    yb_mouse_down_point = [self convertPoint:[event locationInWindow] fromView:nil];
    yb_long_press_fired = false;
    yb_click_cancelled = false;

    yb_long_press_timer = [[NSTimer timerWithTimeInterval:YB_MENU_BAR_LONG_PRESS_DURATION
                                                   target:self
                                                 selector:@selector(longPressTimerFired:)
                                                 userInfo:nil
                                                  repeats:NO] retain];
    [[NSRunLoop mainRunLoop] addTimer:yb_long_press_timer forMode:NSRunLoopCommonModes];
}

- (void)mouseDragged:(NSEvent *)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (hypot(point.x - yb_mouse_down_point.x, point.y - yb_mouse_down_point.y) <= YB_MENU_BAR_DRAG_TOLERANCE) return;

    yb_click_cancelled = true;
    [self cancelLongPressTimer];
}

- (void)mouseUp:(NSEvent *)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    [self cancelLongPressTimer];
    if (!yb_long_press_fired && !yb_click_cancelled && NSPointInRect(point, [self bounds])) {
        [self activatePoint:point];
    }
    yb_long_press_fired = false;
    yb_click_cancelled = false;
}

- (void)rightMouseDown:(NSEvent *)event
{
    (void) event;
    [self cancelLongPressTimer];
    yb_click_cancelled = true;
    [yb_controller showStatusMenu:self];
}

- (void)drawRect:(NSRect)dirty_rect
{
    (void) dirty_rect;

    size_t group_count = 0;
    size_t previous_display_index = SIZE_MAX;
    for (NSView *view in [self subviews]) {
        if (![view isKindOfClass:[YBMenuBarScreenCell class]]) continue;
        size_t display_index = ((YBMenuBarScreenCell *) view)->yb_display_index;
        if (display_index != previous_display_index) {
            ++group_count;
            previous_display_index = display_index;
        }
    }
    if (group_count < 2) return;

    NSAppearance *appearance = [self effectiveAppearance];
    struct accessibility_display_preferences preferences = yb_accessibility_preferences();
    void (^draw_groups)(void) = ^{
        NSRect group_rect = NSZeroRect;
        size_t group_display_index = SIZE_MAX;
        NSRect previous_cell_frame = NSZeroRect;
        bool has_group = false;

        for (NSView *view in [self subviews]) {
            if (![view isKindOfClass:[YBMenuBarScreenCell class]]) continue;
            YBMenuBarScreenCell *cell = (YBMenuBarScreenCell *) view;
            NSRect cell_frame = [cell frame];

            if (has_group && cell->yb_display_index != group_display_index) {
                yb_draw_display_group(group_rect, group_display_index, preferences);
                yb_draw_display_separator(self, previous_cell_frame, cell_frame);
                group_rect = cell_frame;
                group_display_index = cell->yb_display_index;
            } else if (!has_group) {
                group_rect = cell_frame;
                group_display_index = cell->yb_display_index;
                has_group = true;
            } else {
                group_rect = NSUnionRect(group_rect, cell_frame);
            }
            previous_cell_frame = cell_frame;
        }

        if (has_group) yb_draw_display_group(group_rect, group_display_index, preferences);
    };

    if (appearance) {
        [appearance performAsCurrentDrawingAppearance:draw_groups];
    } else {
        draw_groups();
    }
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
    struct accessibility_display_preferences preferences = yb_accessibility_preferences();
    if (preferences.differentiate_without_color) {
        switch (state) {
        case YB_MENU_BAR_SCREEN_ACTIVE:        return 2.5;
        case YB_MENU_BAR_SCREEN_VISIBLE_OTHER: return 1.75;
        case YB_MENU_BAR_SCREEN_OCCUPIED:      return 1.25;
        case YB_MENU_BAR_SCREEN_UNKNOWN:
        case YB_MENU_BAR_SCREEN_EMPTY:
        case YB_MENU_BAR_SCREEN_PLACEHOLDER:   return 1.0;
        }
    }

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

static bool
yb_screen_has_noncolor_marker(enum yb_menu_bar_screen_state state)
{
    return yb_accessibility_preferences().differentiate_without_color &&
           state == YB_MENU_BAR_SCREEN_VISIBLE_OTHER;
}

static NSColor *
yb_screen_pressed_color(void)
{
    return yb_accessibility_preferences().differentiate_without_color
         ? [NSColor labelColor]
         : [NSColor systemOrangeColor];
}

static CGFloat
yb_screen_pressed_fill_alpha(void)
{
    if (yb_accessibility_preferences().reduce_transparency) return 1.0;
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

- (void)setYBPressed:(bool)pressed
{
    if (yb_pressed == pressed) return;
    yb_pressed = pressed;
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirty_rect
{
    (void) dirty_rect;
    NSAppearance *appearance = [self effectiveAppearance];
    struct accessibility_display_preferences preferences = yb_accessibility_preferences();
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

        bool pressed = yb_pressed;
        NSColor *pressed_ink = yb_screen_pressed_color();
        if (pressed) {
            if (preferences.reduce_transparency) {
                [[NSColor selectedContentBackgroundColor] setFill];
            } else {
                [[pressed_ink colorWithAlphaComponent:yb_screen_pressed_fill_alpha()] setFill];
            }
            [outline fill];
        }

        NSColor *outline_ink = pressed
                             ? pressed_ink
                             : [[NSColor labelColor] colorWithAlphaComponent:yb_screen_alpha(yb_state)];
        [outline_ink setStroke];
        [outline stroke];

        if (yb_screen_has_noncolor_marker(yb_state)) {
            NSBezierPath *marker = [NSBezierPath bezierPath];
            [marker moveToPoint:NSMakePoint(NSMinX(screen) + 5.0, NSMinY(screen) + 3.0)];
            [marker lineToPoint:NSMakePoint(NSMaxX(screen) - 5.0, NSMinY(screen) + 3.0)];
            [marker setLineWidth:1.5];
            [[NSColor labelColor] setStroke];
            [marker stroke];
        }

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

static NSString *
yb_status_accessibility_value(const struct menu_bar_snapshot *snapshot)
{
    if (!snapshot || snapshot->display_count == 0) return @"No displays available";

    NSMutableArray *summaries = [NSMutableArray arrayWithCapacity:snapshot->display_count];
    for (size_t display_index = 0; display_index < snapshot->display_count; ++display_index) {
        const struct menu_bar_display *display = &snapshot->displays[display_index];
        NSMutableString *summary = [NSMutableString stringWithString:yb_display_accessibility_title(display, display_index)];
        const struct menu_bar_space *space = yb_find_visible_space(display, snapshot->settings.space_filter);
        if (space) {
            [summary appendFormat:@", visible space %@", yb_space_display_label(space)];
        } else {
            [summary appendString:@", visible space unresolved"];
        }
        [summaries addObject:summary];
    }
    return [summaries componentsJoinedByString:@"; "];
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

static NSString *
yb_overflow_title(size_t omitted, bool leading)
{
    if (!omitted) return @"";
    return leading
         ? [NSString stringWithFormat:@"←+%zu", omitted]
         : [NSString stringWithFormat:@"+%zu→", omitted];
}

static CGFloat
yb_overflow_width(size_t omitted, bool leading)
{
    if (!omitted) return 0.0;
    NSString *title = yb_overflow_title(omitted, leading);
    return MIN(64.0, MAX(34.0, yb_title_width(title, [NSFont menuBarFontOfSize:0.0]) + 14.0));
}

static CGFloat
yb_screen_interval_width(const struct yb_screen_entry *entries, size_t total, size_t start, size_t count)
{
    size_t omitted_before = start;
    size_t omitted_after = total - start - count;
    CGFloat width = YB_MENU_BAR_OUTER_INSET;

    if (omitted_before) {
        width += yb_overflow_width(omitted_before, true);
        if (count) width += YB_MENU_BAR_GAP;
    }

    for (size_t i = 0; i < count; ++i) {
        if (i) {
            width += entries[start + i - 1].display == entries[start + i].display
                   ? YB_MENU_BAR_GAP : YB_MENU_BAR_DISPLAY_GAP;
        }
        const struct menu_bar_space *space = entries[start + i].space;
        width += yb_screen_cell_width(space ? space->mission_control_index : 0);
    }

    if (omitted_after) {
        if (count || omitted_before) width += YB_MENU_BAR_GAP;
        width += yb_overflow_width(omitted_after, false);
    }
    return width + YB_MENU_BAR_OUTER_INSET;
}

static bool
yb_screen_interval_fits(const struct yb_screen_entry *entries,
                        size_t total,
                        size_t start,
                        size_t count)
{
    return yb_screen_interval_width(entries, total, start, count) <= YB_MENU_BAR_MAX_EXPANDED_WIDTH;
}

static struct yb_screen_selection
yb_select_screen_interval(const struct yb_screen_entry *entries, size_t total, size_t active)
{
    struct yb_screen_selection selection = { 0, 0 };
    if (!entries || !total) return selection;
    if (yb_screen_interval_width(entries, total, 0, total) <= YB_MENU_BAR_MAX_EXPANDED_WIDTH) {
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

static NSMenuItem *
yb_section_header_menu_item(NSString *title, NSString *tool_tip)
{
    NSMenuItem *item = nil;
    if (@available(macOS 14.0, *)) {
        item = [NSMenuItem sectionHeaderWithTitle:title];
    } else {
        item = yb_informational_menu_item(title, 0, tool_tip);
    }
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

    if (_expanded_session) [_expanded_session cancel];
    [self closeExpandedPanel];
    if (_menu) {
        [_menu cancelTracking];
        [self endMenuTrackingApplyDeferred:false];
        [_menu setDelegate:nil];
    } else {
        [self endMenuTrackingApplyDeferred:false];
    }

    [_strip_view removeFromSuperview];

    if (_status_item) {
        NSStatusBarButton *button = [_status_item button];
        [button setMenu:nil];
        [button setTarget:nil];
        [button setAction:NULL];
#if YB_MENU_BAR_HAS_EXPANDED_INTERFACE
        if (@available(macOS 27.0, *)) [_status_item setExpandedInterfaceDelegate:nil];
#endif
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

- (bool)expandedPanelCapabilityAvailable
{
#if defined(YB_MENU_BAR_TEST)
    if (g_yb_menu_bar_panel_capability_override >= 0) {
        return g_yb_menu_bar_panel_capability_override != 0;
    }
#endif
#if YB_MENU_BAR_HAS_EXPANDED_INTERFACE
    if (@available(macOS 27.0, *)) {
        return _status_item && [_status_item respondsToSelector:@selector(setExpandedInterfaceDelegate:)];
    }
#endif
    return false;
}

- (bool)wantsExpandedPanel
{
    if (!_snapshot || _panel_disabled ||
        _snapshot->settings.interface == MENU_BAR_INTERFACE_MENU) return false;
    return [self expandedPanelCapabilityAvailable];
}

- (void)configureStatusItemInterface
{
#if YB_MENU_BAR_HAS_EXPANDED_INTERFACE
    if (@available(macOS 27.0, *)) {
        [_status_item setExpandedInterfaceDelegate:[self wantsExpandedPanel] ? self : nil];
    }
#endif
}

- (NSTextField *)panelLabel:(NSString *)text font:(NSFont *)font frame:(NSRect)frame
{
    NSTextField *label = [NSTextField labelWithString:text ?: @""];
    [label setFrame:frame];
    [label setFont:font];
    [label setLineBreakMode:NSLineBreakByWordWrapping];
    [label setUsesSingleLineMode:NO];
    return label;
}

- (YBMenuBarExpandedPanel *)buildExpandedPanel
{
    if (!_snapshot) return nil;
    const CGFloat width = 420.0;
    const CGFloat footer_height = 76.0;
    const CGFloat header_height = 52.0;
    const CGFloat card_height = 52.0;
    const CGFloat display_height = 30.0;
    CGFloat document_height = 12.0;
    for (size_t display_index = 0; display_index < _snapshot->display_count; ++display_index) {
        const struct menu_bar_display *display = &_snapshot->displays[display_index];
        document_height += display_height;
        bool any = false;
        for (size_t space_index = 0; space_index < display->space_count; ++space_index) {
            if (!yb_space_matches_filter(&display->spaces[space_index], _snapshot->settings.space_filter)) continue;
            document_height += card_height + 6.0;
            any = true;
        }
        if (!any) document_height += card_height;
        document_height += 8.0;
    }
    if (!_snapshot->display_count) document_height += card_height;
    CGFloat scroll_height = MIN(420.0, MAX(120.0, document_height));
    CGFloat height = header_height + scroll_height + footer_height;

    YBMenuBarExpandedPanel *panel = [[[YBMenuBarExpandedPanel alloc]
        initWithContentRect:NSMakeRect(0, 0, width, height)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskFullSizeContentView
        backing:NSBackingStoreBuffered defer:NO] autorelease];
    if (!panel) return nil;
    [panel setYb_controller:self];
    [panel setTitleVisibility:NSWindowTitleHidden];
    [panel setTitlebarAppearsTransparent:YES];
    [panel setFloatingPanel:YES];
    [panel setHidesOnDeactivate:NO];
    [panel setReleasedWhenClosed:NO];
    [panel setAccessibilityTitle:@"yabai Spaces — Read Only"];

    NSView *root = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, width, height)] autorelease];
    [panel setContentView:root];
    [root addSubview:[self panelLabel:@"yabai Spaces — Read Only"
                                   font:[NSFont boldSystemFontOfSize:15.0]
                                  frame:NSMakeRect(16, height - 34, width - 32, 22)]];
    NSString *failure = yb_nsstring(_snapshot->action_failure_text);
    if ([failure length]) {
        NSTextField *failure_label = [self panelLabel:[NSString stringWithFormat:@"Last action failed: %@", failure]
                                                  font:[NSFont systemFontOfSize:11.0]
                                                 frame:NSMakeRect(16, height - 51, width - 32, 17)];
        [failure_label setTextColor:[NSColor secondaryLabelColor]];
        [root addSubview:failure_label];
    }

    NSScrollView *scroll = [[[NSScrollView alloc] initWithFrame:NSMakeRect(12, footer_height, width - 24, scroll_height)] autorelease];
    [scroll setHasVerticalScroller:YES];
    [scroll setBorderType:NSBezelBorder];
    [scroll setDrawsBackground:!yb_accessibility_preferences().reduce_transparency];
    NSView *document = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, width - 42, document_height)] autorelease];
    [document setAccessibilityRole:NSAccessibilityListRole];
    [document setAccessibilityLabel:@"Displays and Spaces"];
    [scroll setDocumentView:document];
    [root addSubview:scroll];

    CGFloat y = document_height - 28.0;
    NSView *first_key_view = nil;
    NSView *previous_key_view = nil;
    for (size_t display_index = 0; display_index < _snapshot->display_count; ++display_index) {
        const struct menu_bar_display *display = &_snapshot->displays[display_index];
        NSString *display_title = yb_display_menu_title(display, display_index);
        NSTextField *display_label = [self panelLabel:display_title
                                                font:[NSFont boldSystemFontOfSize:12.0]
                                               frame:NSMakeRect(10, y, width - 64, 20)];
        [display_label setAccessibilityRole:NSAccessibilityGroupRole];
        [display_label setAccessibilityLabel:yb_display_accessibility_title(display, display_index)];
        [document addSubview:display_label];
        y -= 28.0;
        bool any = false;
        for (size_t space_index = 0; space_index < display->space_count; ++space_index) {
            const struct menu_bar_space *space = &display->spaces[space_index];
            if (!yb_space_matches_filter(space, _snapshot->settings.space_filter)) continue;
            YBMenuBarSpaceCard *card = [[[YBMenuBarSpaceCard alloc]
                initWithFrame:NSMakeRect(10, y - card_height + 8.0, width - 64, card_height)] autorelease];
            [card setTitle:yb_space_menu_title(space)];
            [card setButtonType:NSButtonTypeMomentaryPushIn];
            [card setBezelStyle:NSBezelStyleRounded];
            [card setAlignment:NSTextAlignmentLeft];
            [card setTarget:nil];
            [card setAction:nil];
            [card setAccessibilityRole:NSAccessibilityGroupRole];
            [card setAccessibilityLabel:yb_space_accessibility_title(display, display_index, space)];
            [card setAccessibilitySelected:space->is_visible];
            [document addSubview:card];
            if (!first_key_view) first_key_view = card;
            if (previous_key_view) [previous_key_view setNextKeyView:card];
            previous_key_view = card;
            y -= card_height + 6.0;
            any = true;
        }
        if (!any) {
            NSTextField *empty = [self panelLabel:@"No matching spaces"
                                                font:[NSFont systemFontOfSize:12.0]
                                               frame:NSMakeRect(20, y - 22.0, width - 84, 20)];
            [empty setTextColor:[NSColor secondaryLabelColor]];
            [document addSubview:empty];
            y -= card_height;
        }
        y -= 8.0;
    }
    if (!_snapshot->display_count) {
        [document addSubview:[self panelLabel:@"No displays available"
                                           font:[NSFont systemFontOfSize:12.0]
                                          frame:NSMakeRect(14, y - 22.0, width - 70, 20)]];
    }

    NSButton *filter = [[[NSButton alloc] initWithFrame:NSMakeRect(16, 42, 190, 24)] autorelease];
    [filter setButtonType:NSButtonTypeSwitch];
    [filter setTitle:@"Show empty spaces"];
    [filter setState:_snapshot->settings.space_filter == MENU_BAR_SPACE_FILTER_ALL
                   ? NSControlStateValueOn : NSControlStateValueOff];
    [filter setTarget:self];
    [filter setAction:@selector(performPanelFilterAction:)];
    [filter setAccessibilityLabel:@"Show empty spaces"];
    [root addSubview:filter];

    NSPopUpButton *style = [[[NSPopUpButton alloc] initWithFrame:NSMakeRect(215, 40, 188, 28)
                                                        pullsDown:NO] autorelease];
    [style addItemsWithTitles:@[ @"Compact", @"Expanded", @"Automatic" ]];
    [style selectItemAtIndex:_snapshot->settings.style == MENU_BAR_STYLE_COMPACT ? 0 :
                             _snapshot->settings.style == MENU_BAR_STYLE_AUTOMATIC ? 2 : 1];
    [style setTarget:self];
    [style setAction:@selector(performPanelStyleAction:)];
    [style setAccessibilityLabel:@"Menu bar layout"];
    [root addSubview:style];
    if (previous_key_view) [previous_key_view setNextKeyView:filter];
    [filter setNextKeyView:style];
    [style setNextKeyView:first_key_view ?: filter];
    if (!first_key_view) first_key_view = filter;
    [panel setInitialFirstResponder:first_key_view];
    return panel;
}

- (bool)positionExpandedPanel
{
#if defined(YB_MENU_BAR_TEST)
    if (g_yb_menu_bar_panel_capability_override == 2) {
        [_expanded_panel setFrameOrigin:NSMakePoint(100.0, 100.0)];
        return true;
    }
#endif
    NSView *anchor_view = [_strip_view window] ? _strip_view : [_status_item button];
    NSWindow *anchor_window = [anchor_view window];
    NSScreen *screen = [anchor_window screen] ?: [NSScreen mainScreen];
    if (!anchor_view || !anchor_window || !screen) return false;
    NSRect window_rect = [anchor_view convertRect:[anchor_view bounds] toView:nil];
    NSRect anchor = [anchor_window convertRectToScreen:window_rect];
    NSRect visible = [screen visibleFrame];
    NSRect panel_frame = [_expanded_panel frame];
    CGFloat x = NSMidX(anchor) - NSWidth(panel_frame) * 0.5;
    CGFloat y = NSMinY(anchor) - NSHeight(panel_frame);
    x = MIN(MAX(x, NSMinX(visible)), NSMaxX(visible) - NSWidth(panel_frame));
    y = MIN(MAX(y, NSMinY(visible)), NSMaxY(visible) - NSHeight(panel_frame));
    if (!isfinite(x) || !isfinite(y)) return false;
    [_expanded_panel setFrameOrigin:NSMakePoint(x, y)];
    return true;
}

- (void)closeExpandedPanel
{
    if (_expanded_event_monitor) {
        [NSEvent removeMonitor:_expanded_event_monitor];
        [_expanded_event_monitor release];
        _expanded_event_monitor = nil;
    }
    if (_expanded_panel) {
        [_expanded_panel orderOut:nil];
        [_expanded_panel close];
        [_expanded_panel release];
        _expanded_panel = nil;
    }
    [_expanded_session release];
    _expanded_session = nil;
}

- (void)accessibilityPreferencesChanged
{
#if defined(YB_MENU_BAR_TEST)
    ++_test_accessibility_redraw_count;
#endif
    [_strip_view setNeedsDisplay:YES];
    for (NSView *view in [_strip_view subviews]) [view setNeedsDisplay:YES];
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
    [self configureStatusItemInterface];

    switch (_snapshot->settings.style) {
    case MENU_BAR_STYLE_EXPANDED:
        [self renderExpandedStatusItem];
        break;
    case MENU_BAR_STYLE_COMPACT:
        [self renderCompactStatusItem];
        break;
    case MENU_BAR_STYLE_AUTOMATIC:
        [self renderAutomaticStatusItem];
        break;
    }
}

- (void)rebuildMenu
{
    if (_menu) {
        if (_status_item && [_status_item menu] == _menu) [_status_item setMenu:nil];
        [_menu cancelTracking];
        [_menu setDelegate:nil];
    }

    [_menu release];
    _menu = [[NSMenu alloc] initWithTitle:@"yabai"];
    [_menu setDelegate:self];
    [_menu setAutoenablesItems:NO];

    NSMenuItem *title = yb_section_header_menu_item(@"yabai Spaces",
                                                    @"Choose a Space to focus it; use the controls below to change the indicator layout.");
    [_menu addItem:title];

    NSString *failure = yb_nsstring(_snapshot->action_failure_text);
    if ([failure length]) {
        NSMenuItem *failure_item = yb_informational_menu_item([NSString stringWithFormat:@"Last action failed: %@", failure],
                                                              0,
                                                              @"The most recent menu bar action failed.");
        [_menu addItem:failure_item];
    }

    [_menu addItem:[NSMenuItem separatorItem]];

    bool added_display = false;
    for (size_t display_index = 0; display_index < _snapshot->display_count; ++display_index) {
        const struct menu_bar_display *display = &_snapshot->displays[display_index];
        NSString *display_title = yb_display_menu_title(display, display_index);
        NSString *display_accessibility = yb_display_accessibility_title(display, display_index);

        if (added_display) [_menu addItem:[NSMenuItem separatorItem]];
        [_menu addItem:yb_section_header_menu_item(display_title, display_accessibility)];

        bool added_space = false;
        for (size_t space_index = 0; space_index < display->space_count; ++space_index) {
            const struct menu_bar_space *space = &display->spaces[space_index];
            if (!yb_space_matches_filter(space, _snapshot->settings.space_filter)) continue;

            NSString *space_accessibility = yb_space_accessibility_title(display, display_index, space);
            YBMenuBarActionTarget *space_target = yb_action_target(YB_MENU_BAR_CONTROL_FOCUS_SPACE, _snapshot->generation);
            space_target->yb_space_id = space->space_id;
            NSMenuItem *space_item = yb_menu_item(yb_space_menu_title(space),
                                                  @selector(performMenuAction:),
                                                  self,
                                                  space_target);
            [space_item setState:space->is_visible ? NSControlStateValueOn : NSControlStateValueOff];
            [space_item setIndentationLevel:1];
            [space_item setToolTip:[NSString stringWithFormat:@"%@. Choose to focus this space.", space_accessibility]];
            [space_item setAccessibilityLabel:space_accessibility];
            [_menu addItem:space_item];
            added_space = true;
        }

        if (!added_space) {
            [_menu addItem:yb_informational_menu_item(@"No matching spaces", 1, @"No spaces match the current filter.")];
        }

        added_display = true;
    }

    if (!added_display) {
        [_menu addItem:yb_informational_menu_item(@"No displays available", 0, @"macOS did not report any displays.")];
    }
    [_menu addItem:[NSMenuItem separatorItem]];

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

    YBMenuBarActionTarget *automatic_target = yb_action_target(YB_MENU_BAR_CONTROL_SET_STYLE, _snapshot->generation);
    automatic_target->yb_style = MENU_BAR_STYLE_AUTOMATIC;
    NSMenuItem *automatic = yb_menu_item(@"Automatic", @selector(performMenuAction:), self, automatic_target);
    [automatic setState:_snapshot->settings.style == MENU_BAR_STYLE_AUTOMATIC ? NSControlStateValueOn : NSControlStateValueOff];
    [automatic setToolTip:@"Use expanded layout up to 260 points, otherwise compact layout."];
    [style_menu addItem:automatic];

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
    cell->yb_state = yb_screen_state(display, space);
    cell->yb_index = space ? space->mission_control_index : 0;
    cell->yb_display_index = display ? display_index : 0;
    cell->yb_generation = _snapshot ? _snapshot->generation : 0;
    cell->yb_space_id = space ? space->space_id : 0;

    bool can_focus = display && space && !(display->has_focus && space->is_visible);
    [cell setTarget:self];
    [cell setAction:can_focus ? @selector(performScreenAction:) : @selector(showStatusMenu:)];
    cell->yb_kind = can_focus ? YB_MENU_BAR_CONTROL_FOCUS_SPACE : YB_MENU_BAR_CONTROL_OPEN_MENU;
    cell->yb_style = _snapshot->settings.style;
    cell->yb_filter = _snapshot->settings.space_filter;

    NSString *description = display && space
                          ? yb_space_accessibility_title(display, display_index, space)
                          : @"Focused display or visible space unresolved";
    [cell setToolTip:description];
    [cell setAccessibilityRole:NSAccessibilityButtonRole];
    [cell setAccessibilityLabel:description];
    [cell setAccessibilityHelp:can_focus
                              ? @"Click to focus this Space. Right-click or press and hold to open the Spaces menu."
                              : @"Click, right-click or press and hold to open the Spaces menu."];
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
    strip->yb_controller = self;
    strip->yb_expanded_interface = [self wantsExpandedPanel];

    YBMenuBarScreenCell *cell = [self screenCellForDisplay:display
                                              displayIndex:display_index
                                                     space:space
                                                     frame:NSMakeRect(YB_MENU_BAR_OUTER_INSET, 0.0,
                                                                      cell_width,
                                                                      YB_MENU_BAR_STATUS_HEIGHT)];
    [strip addSubview:cell];
    return strip;
}

- (NSImage *)imageForStatusStrip:(NSView *)strip
{
    if (!strip) return nil;
    NSRect bounds = [strip bounds];
    NSBitmapImageRep *representation = [strip bitmapImageRepForCachingDisplayInRect:bounds];
    if (!representation) return nil;
    [strip cacheDisplayInRect:bounds toBitmapImageRep:representation];
    NSImage *image = [[[NSImage alloc] initWithSize:bounds.size] autorelease];
    [image addRepresentation:representation];
    [image setTemplate:YES];
    return image;
}

- (void)installStatusStrip:(NSView *)strip
{
    if (!_status_item || !strip) return;
    if (![self wantsExpandedPanel]) {
        [self installStripView:strip];
        return;
    }

    [_status_item setMenu:nil];
    [_strip_view removeFromSuperview];
    [_status_item setView:nil];
    [_strip_view release];
    _strip_view = [strip retain];
    [_status_item setLength:NSWidth([_strip_view frame])];

    NSStatusBarButton *button = [_status_item button];
    [button setTarget:nil];
    [button setAction:NULL];
    [button setImage:[self imageForStatusStrip:_strip_view]];
    [button setImagePosition:NSImageOnly];
    [button setToolTip:@"Open yabai Spaces — Read Only"];
    [button setAccessibilityLabel:@"Open yabai Spaces — Read Only"];
    [button setAccessibilityValue:yb_status_accessibility_value(_snapshot)];
}

- (void)clearNativeMenusFromStatusViews
{
    if (!_status_item) return;

    // NSButton.menu captures the primary click before the custom strip sees it.
    // Keep every native menu property empty and present _menu only through the
    // explicit active/gap/right-click/long-press handlers.
    NSStatusBarButton *button = [_status_item button];
    [button setMenu:nil];
    [_strip_view setMenu:nil];
    for (NSView *view in [_strip_view subviews]) {
        if ([view isKindOfClass:[NSButton class]]) [(NSButton *) view setMenu:nil];
    }
}

- (void)installStripView:(NSView *)strip
{
    if (!_status_item || !strip) return;

    [_status_item setMenu:nil];
    [_strip_view removeFromSuperview];
    [_strip_view release];
    _strip_view = [strip retain];

    [_status_item setView:nil];
    [_status_item setLength:NSWidth([_strip_view frame])];

    // Host the custom strip inside the standard status button instead of
    // replacing the status-item view. This preserves native menu anchoring,
    // tracking, highlighting and keyboard behavior.
    NSStatusBarButton *button = [_status_item button];
    if (button) {
        [button setTitle:@""];
        [button setImage:nil];
        [button setTarget:self];
        [button setAction:@selector(showStatusMenu:)];
        [button setToolTip:@"yabai Spaces — click an inactive Space to focus it; use the active Space, right-click or press and hold for the menu."];
        [button setAccessibilityLabel:@"yabai Spaces"];
        [button setAccessibilityValue:yb_status_accessibility_value(_snapshot)];
        [button setAccessibilityHelp:@"Click an inactive Space to focus it. Click the active Space, right-click or press and hold to open the menu."];

        NSRect frame = [_strip_view frame];
        CGFloat host_height = NSHeight([button bounds]);
        frame.origin = NSMakePoint(0.0,
                                   host_height > NSHeight(frame)
                                 ? floor((host_height - NSHeight(frame)) * 0.5)
                                 : 0.0);
        [_strip_view setFrame:frame];
        [_strip_view setAutoresizingMask:NSViewMinYMargin | NSViewMaxYMargin];
        [button addSubview:_strip_view];
    } else {
        [_status_item setView:_strip_view];
    }

    [self clearNativeMenusFromStatusViews];
}

- (void)renderCompactStatusItem
{
    [self installStatusStrip:[self buildCompactStrip]];
}

- (YBMenuBarButton *)buttonWithTitle:(NSString *)title frame:(NSRect)frame
{
    YBMenuBarButton *button = [[[YBMenuBarButton alloc] initWithFrame:frame] autorelease];
    [button setTitle:title];
    [button setButtonType:NSButtonTypeMomentaryPushIn];
    [button setBezelStyle:NSBezelStyleTexturedRounded];
    [button setBordered:NO];
    [button setFont:[NSFont menuBarFontOfSize:0.0]];
    [button setAccessibilityRole:NSAccessibilityButtonRole];
    return button;
}

- (YBMenuBarButton *)overflowButtonForOmitted:(size_t)omitted leading:(bool)leading frame:(NSRect)frame
{
    YBMenuBarButton *overflow = [self buttonWithTitle:yb_overflow_title(omitted, leading) frame:frame];
    NSString *direction = leading ? @"before" : @"after";
    [overflow setToolTip:[NSString stringWithFormat:@"%zu spaces %@ the visible interval are listed in the yabai Spaces menu",
                                                        omitted, direction]];
    [overflow setAccessibilityLabel:[NSString stringWithFormat:@"%zu spaces %@ the visible interval",
                                                               omitted, direction]];
    [overflow setAccessibilityHelp:@"Open the Spaces menu to view omitted Spaces."];
    [overflow setTarget:self];
    [overflow setAction:@selector(showStatusMenu:)];
    return overflow;
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
    size_t omitted_before = selection.start;
    size_t omitted_after = total - selection.start - selection.count;
    CGFloat strip_width = total
                        ? yb_screen_interval_width(entries, total, selection.start, selection.count)
                        : YB_MENU_BAR_OUTER_INSET * 2.0 + YB_MENU_BAR_CELL_WIDTH;
    YBMenuBarStripView *strip = [[[YBMenuBarStripView alloc] initWithFrame:NSMakeRect(0.0, 0.0, strip_width, YB_MENU_BAR_STATUS_HEIGHT)] autorelease];
    strip->yb_controller = self;
    strip->yb_expanded_interface = [self wantsExpandedPanel];

    CGFloat x = YB_MENU_BAR_OUTER_INSET;
    if (omitted_before) {
        CGFloat width = yb_overflow_width(omitted_before, true);
        YBMenuBarButton *overflow = [self overflowButtonForOmitted:omitted_before
                                                           leading:true
                                                             frame:NSMakeRect(x, 0.0, width, YB_MENU_BAR_STATUS_HEIGHT)];
        [strip addSubview:overflow];
        x += width;
        if (selection.count) x += YB_MENU_BAR_GAP;
    }

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

    if (omitted_after) {
        if (selection.count || omitted_before) x += YB_MENU_BAR_GAP;
        CGFloat width = yb_overflow_width(omitted_after, false);
        YBMenuBarButton *overflow = [self overflowButtonForOmitted:omitted_after
                                                           leading:false
                                                             frame:NSMakeRect(x, 0.0, width, YB_MENU_BAR_STATUS_HEIGHT)];
        [strip addSubview:overflow];
    }

    free(entries);
    return strip;
}

- (void)renderExpandedStatusItem
{
    [self installStatusStrip:[self buildExpandedStrip]];
}

- (void)renderAutomaticStatusItem
{
    NSView *expanded = [self buildExpandedStrip];
    if (NSWidth([expanded frame]) <= YB_MENU_BAR_MAX_AUTOMATIC_WIDTH) {
        [self installStatusStrip:expanded];
    } else {
        [self installStatusStrip:[self buildCompactStrip]];
    }
}

- (void)showStatusMenu:(id)sender
{
    if ([self wantsExpandedPanel] || !_status_item || !_menu || _is_tracking_menu) return;

    YBMenuBarScreenCell *pressed = [sender isKindOfClass:[YBMenuBarScreenCell class]]
                                 ? (YBMenuBarScreenCell *) sender : nil;
    [pressed retain];
    [pressed setYBPressed:true];
    [pressed displayIfNeeded];
    @try {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [_status_item popUpStatusItemMenu:_menu];
#pragma clang diagnostic pop
    } @finally {
        [pressed setYBPressed:false];
        [pressed setNeedsDisplay:YES];
        [pressed release];
    }
}

- (void)sendAction:(struct menu_bar_action)action
{
    if (_callbacks.action) _callbacks.action(&action, _callbacks.context);
}

- (void)performScreenAction:(id)sender
{
    if (![sender isKindOfClass:[YBMenuBarScreenCell class]]) return;

    YBMenuBarScreenCell *cell = sender;
    if (!cell->yb_generation || !cell->yb_space_id) return;
    [self performActionKind:YB_MENU_BAR_CONTROL_FOCUS_SPACE
                 generation:cell->yb_generation
                      space:cell->yb_space_id
                      style:MENU_BAR_STYLE_EXPANDED
                     filter:MENU_BAR_SPACE_FILTER_OCCUPIED];
}

- (void)performActionKind:(enum yb_menu_bar_control_kind)kind generation:(uint64_t)generation space:(uint64_t)space_id style:(enum menu_bar_style)style filter:(enum menu_bar_space_filter)filter
{
    struct menu_bar_action action = {0};
    action.generation = generation;

    switch (kind) {
    case YB_MENU_BAR_CONTROL_FOCUS_SPACE:
        action.type = MENU_BAR_ACTION_FOCUS_SPACE;
        action.data.focus_space.space_id = space_id;
        break;
    case YB_MENU_BAR_CONTROL_SET_STYLE:
        action.type = MENU_BAR_ACTION_SET_STYLE;
        action.data.set_style.style = style;
        break;
    case YB_MENU_BAR_CONTROL_SET_FILTER:
        action.type = MENU_BAR_ACTION_SET_SPACE_FILTER;
        action.data.set_space_filter.filter = filter;
        break;
    default:
        return;
    }

    [self sendAction:action];
}

- (void)performMenuAction:(id)sender
{
    YBMenuBarActionTarget *target = [sender representedObject];
    if (![target isKindOfClass:[YBMenuBarActionTarget class]]) return;

    enum yb_menu_bar_control_kind kind = target->yb_kind;
    [self performActionKind:kind
                 generation:target->yb_generation
                      space:target->yb_space_id
                      style:target->yb_style
                     filter:target->yb_filter];
    if (kind == YB_MENU_BAR_CONTROL_FOCUS_SPACE && _is_tracking_menu) {
        [_menu cancelTrackingWithoutAnimation];
    }
}

- (void)performPanelFilterAction:(id)sender
{
    (void) sender;
    enum menu_bar_space_filter filter = _snapshot->settings.space_filter == MENU_BAR_SPACE_FILTER_ALL
                                      ? MENU_BAR_SPACE_FILTER_OCCUPIED
                                      : MENU_BAR_SPACE_FILTER_ALL;
    [self performActionKind:YB_MENU_BAR_CONTROL_SET_FILTER
                 generation:_tracking_generation
                      space:0
                      style:_snapshot->settings.style
                     filter:filter];
    [self cancelExpandedInterface];
}

- (void)performPanelStyleAction:(id)sender
{
    if (![sender isKindOfClass:[NSPopUpButton class]]) return;
    NSInteger selection = [(NSPopUpButton *) sender indexOfSelectedItem];
    enum menu_bar_style style = selection == 0 ? MENU_BAR_STYLE_COMPACT :
                                selection == 2 ? MENU_BAR_STYLE_AUTOMATIC : MENU_BAR_STYLE_EXPANDED;
    [self performActionKind:YB_MENU_BAR_CONTROL_SET_STYLE
                 generation:_tracking_generation
                      space:0
                      style:style
                     filter:_snapshot->settings.space_filter];
    [self cancelExpandedInterface];
}

- (void)cancelExpandedInterface
{
    if (_expanded_session) [_expanded_session cancel];
}

- (void)beginExpandedPanelWithSession:(id)session
{
    if (![self wantsExpandedPanel] || _expanded_session) {
        [session cancel];
        return;
    }
    _expanded_session = [session retain];
    [self beginMenuTracking];
    _expanded_panel = [[self buildExpandedPanel] retain];
    if (!_expanded_panel || ![self positionExpandedPanel]) {
        _panel_disabled = true;
        [self cancelExpandedInterface];
        return;
    }
    __block YBMenuBarController *controller = self;
    _expanded_event_monitor = [[NSEvent addLocalMonitorForEventsMatchingMask:
        NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown
        handler:^NSEvent *(NSEvent *event) {
            if ([event window] != controller->_expanded_panel) [controller cancelExpandedInterface];
            return event;
        }] retain];
    [_expanded_panel orderFront:nil];
    [_expanded_panel makeKeyWindow];
}

- (void)endExpandedPanel
{
    [self closeExpandedPanel];
    [self endMenuTrackingApplyDeferred:!_is_tearing_down_status_item];
    if (_panel_disabled && _snapshot) [self renderSnapshot];
}

#if YB_MENU_BAR_HAS_EXPANDED_INTERFACE
- (void)statusItem:(NSStatusItem *)statusItem
    didBeginExpandedInterfaceSession:(NSStatusItemExpandedInterfaceSession *)expandedInterfaceSession API_AVAILABLE(macos(27.0))
{
    if (statusItem != _status_item) { [expandedInterfaceSession cancel]; return; }
    [self beginExpandedPanelWithSession:expandedInterfaceSession];
}

- (void)statusItemDidEndExpandedInterfaceSession:(NSStatusItem *)statusItem
                                         animated:(BOOL)animated API_AVAILABLE(macos(27.0))
{
    (void) animated;
    if (statusItem != _status_item && statusItem != nil) return;
    [self endExpandedPanel];
}
#endif

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
    [self clearNativeMenusFromStatusViews];
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

- (bool)testStatusButtonOwnsStripView
{
    NSStatusBarButton *button = _status_item ? [_status_item button] : nil;
    return button && [_status_item view] == nil && [_strip_view superview] == button;
}

- (bool)testStatusViewsUseManualMenu
{
    if (!_status_item || !_menu || [_status_item menu] != nil) return false;

    NSStatusBarButton *button = [_status_item button];
    if (!button || [button menu] != nil || [_strip_view menu] != nil) return false;
    for (NSView *view in [_strip_view subviews]) {
        if ([view isKindOfClass:[NSButton class]] && [(NSButton *) view menu] != nil) return false;
    }
    return true;
}

- (NSStatusBarButton *)testStatusButton
{
    return _status_item ? [_status_item button] : nil;
}

- (NSView *)testStripView
{
    return _strip_view;
}

- (bool)testIsTrackingMenu
{
    return _is_tracking_menu;
}

- (uint64_t)testTrackingGeneration
{
    return _tracking_generation;
}

- (uint64_t)testSnapshotGeneration
{
    return _snapshot ? _snapshot->generation : 0;
}

- (uint64_t)testDeferredSnapshotGeneration
{
    return _deferred_snapshot ? _deferred_snapshot->generation : 0;
}

- (CGFloat)testStatusItemLength
{
    return _status_item ? [_status_item length] : 0.0;
}

- (NSUInteger)testAccessibilityRedrawCount
{
    return _test_accessibility_redraw_count;
}

- (NSView *)testBuildCompactStrip
{
    return [self buildCompactStrip];
}

- (NSView *)testBuildExpandedStrip
{
    return [self buildExpandedStrip];
}

- (bool)testWantsExpandedPanel
{
    return [self wantsExpandedPanel];
}

- (void)testBeginExpandedInterfaceWithSession:(id)session
{
    [self beginExpandedPanelWithSession:session];
}

- (void)testEndExpandedInterface
{
    [self endExpandedPanel];
}

- (NSPanel *)testExpandedPanel
{
    return _expanded_panel;
}

- (bool)testStatusItemHasMenu
{
    return _status_item && [_status_item menu] != nil;
}

- (NSStatusBarButton *)testStatusItemButton
{
    return [_status_item button];
}

- (NSUInteger)testExpandedSpaceCardCount
{
    if (!_expanded_panel) return 0;
    NSUInteger count = 0;
    NSMutableArray *stack = [NSMutableArray arrayWithObject:[_expanded_panel contentView]];
    while ([stack count]) {
        NSView *view = [[stack lastObject] retain];
        [stack removeLastObject];
        if ([view isKindOfClass:[YBMenuBarSpaceCard class]]) ++count;
        [stack addObjectsFromArray:[view subviews]];
        [view release];
    }
    return count;
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

static void
menu_bar_accessibility_preferences_changed_main(void *context)
{
    YBMenuBarController *controller = (YBMenuBarController *) context;
    [controller accessibilityPreferencesChanged];
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
menu_bar_accessibility_preferences_changed(void)
{
    YBMenuBarController *controller = nil;

    pthread_mutex_lock(&g_menu_bar_lock);
    if (g_menu_bar_initialized && g_menu_bar_controller) {
        controller = [g_menu_bar_controller retain];
    }
    pthread_mutex_unlock(&g_menu_bar_lock);

    if (!controller) return;
    if (pthread_main_np()) {
        menu_bar_accessibility_preferences_changed_main(controller);
    } else {
        dispatch_async_f(dispatch_get_main_queue(), controller,
                         menu_bar_accessibility_preferences_changed_main);
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
