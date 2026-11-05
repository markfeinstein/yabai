#import <ApplicationServices/ApplicationServices.h>
#import <Cocoa/Cocoa.h>
#import <fcntl.h>
#import <mach/mach_time.h>
#import <pthread.h>
#import <stdatomic.h>

extern int SLSMainConnectionID(void);
extern CGError SLSGetWindowBounds(int cid, uint32_t wid, CGRect *frame);
extern AXError _AXUIElementGetWindow(AXUIElementRef ref, uint32_t *wid);

typedef struct {
  pid_t pid;
  uint32_t wid;
} fixture_info;
typedef struct {
  uint32_t kind;
  uint64_t sequence;
  uint64_t received_at;
  uint64_t event_timestamp;
  double x;
  double y;
} raw_record;
typedef struct {
  uint64_t at;
  uint32_t kind;
} ax_record;
typedef struct {
  uint64_t at;
  CGRect frame;
} ws_record;
typedef struct {
  uint64_t at;
  uint64_t sequence;
  double x;
} post_record;
typedef struct {
  double values[1024];
  size_t count;
} sample_set;

static mach_timebase_info_data_t g_timebase;
static uint64_t now_ticks(void) { return mach_continuous_time(); }
static double to_ms(uint64_t delta) {
  return ((double)delta * g_timebase.numer / g_timebase.denom) / 1e6;
}
static int cmp_double(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return x < y ? -1 : x > y ? 1 : 0;
}
static double pct(sample_set *s, double p) {
  if (!s->count)
    return 0;
  qsort(s->values, s->count, sizeof(double), cmp_double);
  return s->values[(size_t)llround((s->count - 1) * p)];
}
static void add_sample(sample_set *s, double v) {
  if (s->count < 1024)
    s->values[s->count++] = v;
}
static bool frame_equal(CGRect a, CGRect b) {
  return fabs(a.origin.x - b.origin.x) < 0.1 &&
         fabs(a.origin.y - b.origin.y) < 0.1 &&
         fabs(a.size.width - b.size.width) < 0.1 &&
         fabs(a.size.height - b.size.height) < 0.1;
}

@interface FixtureDelegate : NSObject <NSApplicationDelegate>
@property int fd;
@property(retain) NSWindow *window;
@property(retain) id monitor;
@end
@implementation FixtureDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)note {
  (void)note;
  NSRect frame = NSMakeRect(240, 260, 480, 320);
  self.window = [[NSWindow alloc]
      initWithContentRect:frame
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable |
                          NSWindowStyleMaskClosable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  self.window.level = NSFloatingWindowLevel;
  self.window.releasedWhenClosed = NO;
  self.window.contentView.wantsLayer = YES;
  self.window.contentView.layer.backgroundColor =
      [NSColor colorWithRed:0.08 green:0.48 blue:0.82 alpha:1.0].CGColor;
  [self.window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
  int flags = fcntl(self.fd, F_GETFL, 0);
  if (flags >= 0)
    fcntl(self.fd, F_SETFL, flags | O_NONBLOCK);
  __block int output = self.fd;
  self.monitor = [NSEvent
      addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown |
                                           NSEventMaskLeftMouseDragged |
                                           NSEventMaskLeftMouseUp
                                   handler:^NSEvent *(NSEvent *event) {
                                     CGEventRef cg = event.CGEvent;
                                     raw_record record = {
                                         .kind = (uint32_t)event.type,
                                         .sequence =
                                             cg ? (uint64_t)
                                                      CGEventGetIntegerValueField(
                                                          cg,
                                                          kCGEventSourceUserData)
                                                : 0,
                                         .received_at = now_ticks(),
                                         .event_timestamp =
                                             cg ? CGEventGetTimestamp(cg) : 0,
                                         .x = cg ? CGEventGetLocation(cg).x : 0,
                                         .y =
                                             cg ? CGEventGetLocation(cg).y : 0};
                                     (void)write(output, &record,
                                                 sizeof(record));
                                     return event;
                                   }];
  fixture_info info = {.pid = getpid(),
                       .wid = (uint32_t)self.window.windowNumber};
  int old = fcntl(self.fd, F_GETFL, 0);
  if (old >= 0)
    fcntl(self.fd, F_SETFL, old & ~O_NONBLOCK);
  if (write(self.fd, &info, sizeof(info)) != sizeof(info))
    _exit(70);
  if (old >= 0)
    fcntl(self.fd, F_SETFL, old | O_NONBLOCK);
}
@end

static int run_fixture(int fd) {
  @autoreleasepool {
    NSApplication *app = [NSApplication sharedApplication];
    app.activationPolicy = NSApplicationActivationPolicyAccessory;
    FixtureDelegate *delegate = [FixtureDelegate new];
    delegate.fd = fd;
    app.delegate = delegate;
    [app run];
  }
  return 0;
}

struct pipeline {
  int raw_fd;
  pthread_mutex_t lock;
  raw_record raw[1024];
  size_t raw_count;
  ax_record ax[1024];
  size_t ax_count;
  ws_record ws[4096];
  size_t ws_count;
  post_record post[1024];
  size_t post_count;
  atomic_bool stop;
  uint32_t wid;
  CGRect baseline;
};
static struct pipeline *g_pipeline;

static ssize_t read_full(int fd, void *buffer, size_t size) {
  uint8_t *p = buffer;
  size_t total = 0;
  while (total < size) {
    ssize_t n = read(fd, p + total, size - total);
    if (n == 0)
      return total;
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    total += (size_t)n;
  }
  return (ssize_t)total;
}
static void *raw_reader(void *opaque) {
  struct pipeline *p = opaque;
  raw_record record;
  while (read_full(p->raw_fd, &record, sizeof(record)) == sizeof(record)) {
    pthread_mutex_lock(&p->lock);
    if (p->raw_count < 1024)
      p->raw[p->raw_count++] = record;
    pthread_mutex_unlock(&p->lock);
  }
  return NULL;
}
static void ax_callback(AXObserverRef observer, AXUIElementRef element,
                        CFStringRef notification, void *context) {
  (void)observer;
  (void)element;
  (void)context;
  struct pipeline *p = g_pipeline;
  if (!p)
    return;
  uint32_t kind = CFEqual(notification, kAXMovedNotification)     ? 1
                  : CFEqual(notification, kAXResizedNotification) ? 2
                                                                  : 0;
  pthread_mutex_lock(&p->lock);
  if (p->ax_count < 1024)
    p->ax[p->ax_count++] = (ax_record){.at = now_ticks(), .kind = kind};
  pthread_mutex_unlock(&p->lock);
}
static void *ws_poller(void *opaque) {
  struct pipeline *p = opaque;
  int cid = SLSMainConnectionID();
  CGRect previous = CGRectNull;
  while (!atomic_load_explicit(&p->stop, memory_order_acquire)) {
    CGRect frame;
    if (SLSGetWindowBounds(cid, p->wid, &frame) == kCGErrorSuccess &&
        !frame_equal(frame, previous)) {
      pthread_mutex_lock(&p->lock);
      if (p->ws_count < 4096)
        p->ws[p->ws_count++] = (ws_record){.at = now_ticks(), .frame = frame};
      pthread_mutex_unlock(&p->lock);
      previous = frame;
    }
    usleep(250);
  }
  return NULL;
}
static void post_mouse(CGEventType type, CGPoint point, uint64_t sequence) {
  CGEventRef event =
      CGEventCreateMouseEvent(NULL, type, point, kCGMouseButtonLeft);
  if (!event)
    abort();
  CGEventSetIntegerValueField(event, kCGEventSourceUserData, (int64_t)sequence);
  CGEventPost(kCGHIDEventTap, event);
  CFRelease(event);
}
static void *drag_poster(void *opaque) {
  struct pipeline *p = opaque;
  CGPoint down =
      CGPointMake(CGRectGetMaxX(p->baseline) - 1.0, CGRectGetMidY(p->baseline));
  post_mouse(kCGEventMouseMoved, down, 0);
  usleep(100000);
  post_mouse(kCGEventLeftMouseDown, down, 1);
  usleep(20000);
  const int count = 120;
  uint64_t period_ticks =
      (uint64_t)((1e9 / 120.0) * g_timebase.denom / g_timebase.numer);
  uint64_t next = mach_absolute_time();
  for (int i = 1; i <= count; ++i) {
    next += period_ticks;
    CGPoint point = CGPointMake(down.x + (180.0 * i / count), down.y);
    uint64_t at = now_ticks();
    pthread_mutex_lock(&p->lock);
    p->post[p->post_count++] =
        (post_record){.at = at, .sequence = (uint64_t)(i + 1), .x = point.x};
    pthread_mutex_unlock(&p->lock);
    post_mouse(kCGEventLeftMouseDragged, point, (uint64_t)(i + 1));
    mach_wait_until(next);
  }
  CGPoint up = CGPointMake(down.x + 180.0, down.y);
  post_mouse(kCGEventLeftMouseUp, up, 1000);
  usleep(250000);
  atomic_store_explicit(&p->stop, true, memory_order_release);
  return NULL;
}
static AXUIElementRef first_window(pid_t pid, uint32_t *wid) {
  AXUIElementRef app = AXUIElementCreateApplication(pid);
  if (!app)
    return NULL;
  CFTypeRef value = NULL;
  if (AXUIElementCopyAttributeValue(app, kAXWindowsAttribute, &value) !=
          kAXErrorSuccess ||
      !value) {
    CFRelease(app);
    return NULL;
  }
  CFRelease(app);
  AXUIElementRef result = NULL;
  CFArrayRef windows = value;
  for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
    AXUIElementRef candidate =
        (AXUIElementRef)CFArrayGetValueAtIndex(windows, i);
    uint32_t id = 0;
    if (_AXUIElementGetWindow(candidate, &id) == kAXErrorSuccess && id) {
      *wid = id;
      result = CFRetain(candidate);
      break;
    }
  }
  CFRelease(value);
  return result;
}

static AXUIElementRef find_window(pid_t pid, uint32_t wid) {
  AXUIElementRef app = AXUIElementCreateApplication(pid);
  if (!app)
    return NULL;
  CFTypeRef value = NULL;
  if (AXUIElementCopyAttributeValue(app, kAXWindowsAttribute, &value) !=
          kAXErrorSuccess ||
      !value) {
    CFRelease(app);
    return NULL;
  }
  CFRelease(app);
  AXUIElementRef result = NULL;
  CFArrayRef windows = value;
  for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
    AXUIElementRef candidate =
        (AXUIElementRef)CFArrayGetValueAtIndex(windows, i);
    uint32_t id = 0;
    if (_AXUIElementGetWindow(candidate, &id) == kAXErrorSuccess && id == wid) {
      result = CFRetain(candidate);
      break;
    }
  }
  CFRelease(value);
  return result;
}
static raw_record *find_raw(struct pipeline *p, uint64_t sequence) {
  for (size_t i = 0; i < p->raw_count; ++i)
    if (p->raw[i].sequence == sequence)
      return &p->raw[i];
  return NULL;
}

static int run_external(const char *path) {
  pid_t child = fork();
  if (child == 0) {
    setsid();
    int nullfd = open("/dev/null", O_WRONLY);
    if (nullfd >= 0) {
      dup2(nullfd, STDOUT_FILENO);
      dup2(nullfd, STDERR_FILENO);
      close(nullfd);
    }
    execl(path, path, "--no-sandbox", NULL);
    _exit(72);
  }
  AXUIElementRef window = NULL;
  uint32_t wid = 0;
  for (int i = 0; i < 80 && !window; ++i) {
    usleep(100000);
    window = first_window(child, &wid);
  }
  if (!window) {
    kill(-child, SIGTERM);
    waitpid(child, NULL, 0);
    return 76;
  }
  struct pipeline p = {.raw_fd = -1, .wid = wid};
  pthread_mutex_init(&p.lock, NULL);
  atomic_init(&p.stop, false);
  g_pipeline = &p;
  if (SLSGetWindowBounds(SLSMainConnectionID(), wid, &p.baseline) !=
      kCGErrorSuccess) {
    CFRelease(window);
    kill(-child, SIGTERM);
    return 77;
  }
  AXObserverRef observer = NULL;
  AXError create = AXObserverCreate(child, ax_callback, &observer);
  AXError moved = kAXErrorFailure, resized = kAXErrorFailure;
  if (create == kAXErrorSuccess) {
    moved =
        AXObserverAddNotification(observer, window, kAXMovedNotification, NULL);
    resized = AXObserverAddNotification(observer, window,
                                        kAXResizedNotification, NULL);
    CFRunLoopAddSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(observer),
                       kCFRunLoopDefaultMode);
  }
  pthread_t poller, poster;
  pthread_create(&poller, NULL, ws_poller, &p);
  pthread_create(&poster, NULL, drag_poster, &p);
  while (!atomic_load_explicit(&p.stop, memory_order_acquire))
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
  pthread_join(poster, NULL);
  pthread_join(poller, NULL);
  if (observer) {
    CFRunLoopRemoveSource(CFRunLoopGetMain(),
                          AXObserverGetRunLoopSource(observer),
                          kCFRunLoopDefaultMode);
    CFRunLoopSourceInvalidate(AXObserverGetRunLoopSource(observer));
    CFRelease(observer);
  }
  CFRelease(window);
  g_pipeline = NULL;
  kill(-child, SIGTERM);
  waitpid(child, NULL, 0);
  sample_set ax_intervals = {0}, ws_intervals = {0}, post_to_ws = {0};
  size_t ax_move = 0, ax_resize = 0;
  uint64_t last = 0;
  for (size_t i = 0; i < p.ax_count; ++i) {
    if (p.ax[i].kind == 1)
      ax_move++;
    if (p.ax[i].kind == 2)
      ax_resize++;
    if (last)
      add_sample(&ax_intervals, to_ms(p.ax[i].at - last));
    last = p.ax[i].at;
  }
  last = 0;
  for (size_t i = 0; i < p.ws_count; ++i) {
    if (last)
      add_sample(&ws_intervals, to_ms(p.ws[i].at - last));
    last = p.ws[i].at;
  }
  for (size_t i = 0; i < p.post_count; ++i) {
    for (size_t j = 0; j < p.ws_count; ++j) {
      if (p.ws[j].at > p.post[i].at) {
        add_sample(&post_to_ws, to_ms(p.ws[j].at - p.post[i].at));
        break;
      }
    }
  }
  CGRect final = p.ws_count ? p.ws[p.ws_count - 1].frame : p.baseline;
  printf("{\"case\":\"external-native-drag-pipeline\",\"observer_create\":%d,"
         "\"observer_move\":%d,\"observer_resize\":%d,\"posted_drag\":%zu,\"ax_"
         "move\":%zu,\"ax_resize\":%zu,\"ws_changes\":%zu,\"ax_interval_p50_"
         "ms\":%.3f,\"ax_interval_p95_ms\":%.3f,\"ws_interval_p50_ms\":%.3f,"
         "\"ws_interval_p95_ms\":%.3f,\"post_to_next_ws_p50_ms\":%.3f,\"post_"
         "to_next_ws_p95_ms\":%.3f,\"final_delta_w\":%.1f}\n",
         create, moved, resized, p.post_count, ax_move, ax_resize, p.ws_count,
         pct(&ax_intervals, .5), pct(&ax_intervals, .95),
         pct(&ws_intervals, .5), pct(&ws_intervals, .95), pct(&post_to_ws, .5),
         pct(&post_to_ws, .95), final.size.width - p.baseline.size.width);
  pthread_mutex_destroy(&p.lock);
  return 0;
}

static int run_controller(const char *path) {
  int fds[2];
  if (pipe(fds) != 0)
    return 71;
  pid_t child = fork();
  if (child == 0) {
    close(fds[0]);
    char fd[32];
    snprintf(fd, sizeof(fd), "%d", fds[1]);
    execl(path, path, "--fixture", fd, NULL);
    _exit(72);
  }
  close(fds[1]);
  fixture_info info = {0};
  if (read_full(fds[0], &info, sizeof(info)) != sizeof(info)) {
    kill(child, SIGTERM);
    return 73;
  }
  usleep(400000);
  AXUIElementRef window = find_window(info.pid, info.wid);
  if (!window || !AXIsProcessTrusted()) {
    printf("{\"case\":\"pipeline-preflight\",\"ready\":false}\n");
    kill(child, SIGTERM);
    return 74;
  }
  struct pipeline p = {.raw_fd = fds[0], .wid = info.wid};
  pthread_mutex_init(&p.lock, NULL);
  atomic_init(&p.stop, false);
  g_pipeline = &p;
  if (SLSGetWindowBounds(SLSMainConnectionID(), info.wid, &p.baseline) !=
      kCGErrorSuccess) {
    kill(child, SIGTERM);
    return 75;
  }
  AXObserverRef observer = NULL;
  AXError create = AXObserverCreate(info.pid, ax_callback, &observer);
  AXError moved = kAXErrorFailure, resized = kAXErrorFailure;
  if (create == kAXErrorSuccess) {
    moved =
        AXObserverAddNotification(observer, window, kAXMovedNotification, NULL);
    resized = AXObserverAddNotification(observer, window,
                                        kAXResizedNotification, NULL);
    CFRunLoopAddSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(observer),
                       kCFRunLoopDefaultMode);
  }
  pthread_t reader, poller, poster;
  pthread_create(&reader, NULL, raw_reader, &p);
  pthread_create(&poller, NULL, ws_poller, &p);
  pthread_create(&poster, NULL, drag_poster, &p);
  while (!atomic_load_explicit(&p.stop, memory_order_acquire))
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
  pthread_join(poster, NULL);
  pthread_join(poller, NULL);
  kill(child, SIGTERM);
  int status = 0;
  waitpid(child, &status, 0);
  close(fds[0]);
  pthread_join(reader, NULL);
  if (observer) {
    CFRunLoopRemoveSource(CFRunLoopGetMain(),
                          AXObserverGetRunLoopSource(observer),
                          kCFRunLoopDefaultMode);
    CFRunLoopSourceInvalidate(AXObserverGetRunLoopSource(observer));
    CFRelease(observer);
  }
  CFRelease(window);
  g_pipeline = NULL;

  sample_set raw_intervals = {0}, ax_intervals = {0}, ws_intervals = {0},
             post_to_raw = {0}, raw_to_ws = {0};
  size_t raw_drag = 0, raw_unique = 0, ax_move = 0, ax_resize = 0;
  uint64_t last = 0;
  for (size_t i = 0; i < p.raw_count; ++i) {
    if (p.raw[i].kind != NSEventTypeLeftMouseDragged)
      continue;
    raw_drag++;
    if (last)
      add_sample(&raw_intervals, to_ms(p.raw[i].received_at - last));
    last = p.raw[i].received_at;
  }
  for (size_t i = 0; i < p.post_count; ++i) {
    raw_record *r = find_raw(&p, p.post[i].sequence);
    if (!r)
      continue;
    raw_unique++;
    add_sample(&post_to_raw, to_ms(r->received_at - p.post[i].at));
    uint64_t next_ws = 0;
    for (size_t j = 0; j < p.ws_count; ++j) {
      if (p.ws[j].at > r->received_at) {
        next_ws = p.ws[j].at;
        break;
      }
    }
    if (next_ws)
      add_sample(&raw_to_ws, to_ms(next_ws - r->received_at));
  }
  last = 0;
  for (size_t i = 0; i < p.ax_count; ++i) {
    if (p.ax[i].kind == 1)
      ax_move++;
    if (p.ax[i].kind == 2)
      ax_resize++;
    if (last)
      add_sample(&ax_intervals, to_ms(p.ax[i].at - last));
    last = p.ax[i].at;
  }
  last = 0;
  for (size_t i = 0; i < p.ws_count; ++i) {
    if (last)
      add_sample(&ws_intervals, to_ms(p.ws[i].at - last));
    last = p.ws[i].at;
  }
  CGRect final = p.ws_count ? p.ws[p.ws_count - 1].frame : p.baseline;
  printf("{\"case\":\"native-drag-pipeline\",\"observer_create\":%d,\"observer_"
         "move\":%d,\"observer_resize\":%d,\"posted_drag\":%zu,\"raw_drag\":%"
         "zu,\"raw_unique\":%zu,\"ax_move\":%zu,\"ax_resize\":%zu,\"ws_"
         "changes\":%zu,\"raw_interval_p50_ms\":%.3f,\"raw_interval_p95_ms\":%."
         "3f,\"ax_interval_p50_ms\":%.3f,\"ax_interval_p95_ms\":%.3f,\"ws_"
         "interval_p50_ms\":%.3f,\"ws_interval_p95_ms\":%.3f,\"post_to_raw_p50_"
         "ms\":%.3f,\"post_to_raw_p95_ms\":%.3f,\"raw_to_next_ws_p50_ms\":%.3f,"
         "\"raw_to_next_ws_p95_ms\":%.3f,\"final_delta_w\":%.1f}\n",
         create, moved, resized, p.post_count, raw_drag, raw_unique, ax_move,
         ax_resize, p.ws_count, pct(&raw_intervals, .5),
         pct(&raw_intervals, .95), pct(&ax_intervals, .5),
         pct(&ax_intervals, .95), pct(&ws_intervals, .5),
         pct(&ws_intervals, .95), pct(&post_to_raw, .5), pct(&post_to_raw, .95),
         pct(&raw_to_ws, .5), pct(&raw_to_ws, .95),
         final.size.width - p.baseline.size.width);
  pthread_mutex_destroy(&p.lock);
  return 0;
}
int main(int argc, char **argv) {
  mach_timebase_info(&g_timebase);
  if (argc == 3 && !strcmp(argv[1], "--fixture"))
    return run_fixture(atoi(argv[2]));
  if (argc == 3 && !strcmp(argv[1], "--external"))
    return run_external(argv[2]);
  return run_controller(argv[0]);
}
