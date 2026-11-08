#import <Foundation/Foundation.h>
#import <objc/runtime.h>

@class NotifyDelegate;

static bool g_notify_init;
static NSImage *g_notify_img;
static NotifyDelegate *g_notify_delegate;

/* Full-bleed artwork matching assets/icon/icon-app.svg. Notification Center
 * applies the rounded app-icon mask itself; artwork with its own rounded
 * plate would be drawn inset on a grey backing. */
static const char g_notify_icon_svg[] =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 512 512'>"
    "<rect fill='#fff' width='512' height='512'/>"
    "<path fill='#f36864' d='m460.9 213-64.85-64.85-22.65 22.64 64.87 64.87a28.63 28.63 0 0 1 0 40.5l-162 162a28.63 28.63 0 0 1-40.5 0l-65.07-65.07L148 395.74l65.09 65.09a60.65 60.65 0 0 0 85.76 0l162-162a60.65 60.65 0 0 0 0-85.76Z'/>"
    "<path fill='#5abe5c' d='M235.73 72.86a28.63 28.63 0 0 1 40.5 0L341.41 138 364 115.41l-65.14-65.18a60.65 60.65 0 0 0-85.76 0l-64.63 64.63 22.62 22.63Z'/>"
    "<path fill='#fec547' d='M73.69 275.4a28.64 28.64 0 0 1 0-40.51l65-65-22.63-22.63-65 65a60.65 60.65 0 0 0 0 85.76L116 363l22.67-22.58Z'/>"
    "<path fill='#00364b' d='M406.87 105a22.92 22.92 0 0 0-32.4 0L256.53 222.92 138.57 105a22.92 22.92 0 0 0-32.41 32.41L224.09 255.3 114.74 364.22l-9.61 9.57a22.91 22.91 0 1 0 32.34 32.47l269.4-268.87a23 23 0 0 0 0-32.39Z'/>"
    "</svg>";

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

@interface NotifyDelegate : NSObject <NSUserNotificationCenterDelegate>
@end

@implementation NotifyDelegate
- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center shouldPresentNotification:(NSUserNotification *)notification
{
  return YES;
}
@end

static bool notify_init(void)
{
    g_notify_delegate = [[NotifyDelegate alloc] init];
    [[NSUserNotificationCenter defaultUserNotificationCenter] setDelegate:g_notify_delegate];
    NSData *icon_data = [NSData dataWithBytes:g_notify_icon_svg length:sizeof(g_notify_icon_svg) - 1];
    g_notify_img = [[NSImage alloc] initWithData:icon_data];
    if (!g_notify_img) {
        g_notify_img = [[[NSWorkspace sharedWorkspace]
            iconForFile:[[[NSBundle mainBundle] executablePath] stringByResolvingSymlinksInPath]] retain];
    }
    g_notify_init = true;

    return true;
}

static bool notify_set_identity_image(NSUserNotification *notification, NSImage *image)
{
    if (!notification || !image) return false;
    @try {
        [notification setValue:image forKey:@"_identityImage"];
        [notification setValue:@(false) forKey:@"_identityImageHasBorder"];
        return true;
    } @catch (NSException *exception) {
        (void) exception;
        return false;
    }
}

static NSUserNotification *notify_create_notification(const char *subtitle,
                                                       NSString *informative_text)
{
    if (!g_notify_init) notify_init();
    NSUserNotification *notification = [[NSUserNotification alloc] init];
    notification.title = @"yabai";
    if (subtitle && subtitle[0]) notification.subtitle = [NSString stringWithUTF8String:subtitle];
    notification.informativeText = informative_text;
    (void) notify_set_identity_image(notification, g_notify_img);
    return notification;
}

static void notify(const char *subtitle, const char *format, ...)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    va_list args;
    va_start(args, format);
    NSString *informative_text = [[[NSString alloc]
        initWithFormat:[NSString stringWithUTF8String:format] arguments:args] autorelease];
    NSUserNotification *notification = notify_create_notification(subtitle, informative_text);
    [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:notification];
    [notification release];
    va_end(args);

    [pool drain];
}

#pragma clang diagnostic pop
