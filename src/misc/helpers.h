#ifndef HELPERS_H
#define HELPERS_H

#include "socket_io.h"

#define ANIMATION_EASING_TYPE_LIST \
    ANIMATION_EASING_TYPE_ENTRY(ease_in_sine) \
    ANIMATION_EASING_TYPE_ENTRY(ease_out_sine) \
    ANIMATION_EASING_TYPE_ENTRY(ease_in_out_sine) \
    ANIMATION_EASING_TYPE_ENTRY(ease_in_quad) \
    ANIMATION_EASING_TYPE_ENTRY(ease_out_quad) \
    ANIMATION_EASING_TYPE_ENTRY(ease_in_out_quad) \
    ANIMATION_EASING_TYPE_ENTRY(ease_in_cubic) \
    ANIMATION_EASING_TYPE_ENTRY(ease_out_cubic) \
    ANIMATION_EASING_TYPE_ENTRY(ease_in_out_cubic) \
    ANIMATION_EASING_TYPE_ENTRY(ease_in_quart) \
    ANIMATION_EASING_TYPE_ENTRY(ease_out_quart) \
    ANIMATION_EASING_TYPE_ENTRY(ease_in_out_quart) \
    ANIMATION_EASING_TYPE_ENTRY(ease_in_quint) \
    ANIMATION_EASING_TYPE_ENTRY(ease_out_quint) \
    ANIMATION_EASING_TYPE_ENTRY(ease_in_out_quint) \
    ANIMATION_EASING_TYPE_ENTRY(ease_in_expo) \
    ANIMATION_EASING_TYPE_ENTRY(ease_out_expo) \
    ANIMATION_EASING_TYPE_ENTRY(ease_in_out_expo) \
    ANIMATION_EASING_TYPE_ENTRY(ease_in_circ) \
    ANIMATION_EASING_TYPE_ENTRY(ease_out_circ) \
    ANIMATION_EASING_TYPE_ENTRY(ease_in_out_circ)

enum animation_easing_type
{
#define ANIMATION_EASING_TYPE_ENTRY(value) value##_type,
    ANIMATION_EASING_TYPE_LIST
#undef ANIMATION_EASING_TYPE_ENTRY
    EASING_TYPE_COUNT
};

static char *animation_easing_type_str[] =
{
#define ANIMATION_EASING_TYPE_ENTRY(value) [value##_type] = #value,
    ANIMATION_EASING_TYPE_LIST
#undef ANIMATION_EASING_TYPE_ENTRY
};

static inline float ease_in_sine(float t)
{
    return 1.0f - cosf((t * M_PI) / 2.0f);
}

static inline float ease_out_sine(float t)
{
    return sinf((t * M_PI) / 2.0f);
}

static inline float ease_in_out_sine(float t)
{
    return -(cosf(M_PI * t) - 1.0f) / 2.0f;
}

static inline float ease_in_quad(float t)
{
    return t * t;
}

static inline float ease_out_quad(float t)
{
    return 1.0f - (1.0f - t) * (1.0f - t);
}

static inline float ease_in_out_quad(float t)
{
    return t < 0.5f ? 2.0f * t * t : 1.0f - powf(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

static inline float ease_in_cubic(float t)
{
    return t * t * t;
}

static inline float ease_out_cubic(float t)
{
    return 1.0f - powf(1.0f - t, 3);
}

static inline float ease_in_out_cubic(float t)
{
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

static inline float ease_in_quart(float t)
{
    return t * t * t * t;
}

static inline float ease_out_quart(float t)
{
    return 1.0f - powf(1.0f - t, 4);
}

static inline float ease_in_out_quart(float t)
{
    return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 4.0f) / 2.0f;
}

static inline float ease_in_quint(float t)
{
    return t * t * t * t * t;
}

static inline float ease_out_quint(float t)
{
    return 1.0f - powf(1.0f - t, 5);
}

static inline float ease_in_out_quint(float t)
{
    return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 5.0f) / 2.0f;
}

static inline float ease_in_expo(float t)
{
    return t == 0.0f ? 0.0f : powf(2.0f, 10.0f * t - 10.0f);
}

static inline float ease_out_expo(float t)
{
    return t == 1.0f ? 1.0f : 1.0f - powf(2.0f, -10.0f * t);
}

static inline float ease_in_out_expo(float t)
{
    return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f : t < 0.5f ? powf(2.0f, 20.0f * t - 10.0f) / 2.0f : (2.0f - powf(2.0f, -20.0f * t + 10.0f)) / 2.0f;
}

static inline float ease_in_circ(float t)
{
    return 1.0f - sqrtf(1.0f - powf(t, 2.0f));
}

static inline float ease_out_circ(float t)
{
    return sqrtf(1.0f - powf(t - 1.0f, 2.0f));
}

static inline float ease_in_out_circ(float t)
{
    return t < 0.5f ? (1.0f - sqrtf(1.0f - powf(2.0f * t, 2.0f))) / 2.0f : (sqrtf(1.0f - powf(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static inline uint64_t read_os_timer(void)
{
    uint64_t result = mach_absolute_time();
    Nanoseconds nano = AbsoluteToNanoseconds(*(AbsoluteTime *) &result);
    return *(uint64_t *) &nano;
}
#pragma clang diagnostic pop

static inline uint64_t read_os_freq(void)
{
    return 1000000000;
}

// A regressed timestamp is treated as no elapsed time. This also avoids
// interpreting an unsigned underflow (including timer wrap) as a long delay.
static inline double os_timer_elapsed_seconds(uint64_t start, uint64_t end)
{
    if (end < start) return 0.0;
    uint64_t elapsed = end - start;
    return (double) elapsed / (double) read_os_freq();
}

static inline double os_timer_elapsed_milliseconds(uint64_t start, uint64_t end)
{
    return os_timer_elapsed_seconds(start, end) * 1000.0;
}

static inline bool os_timer_cmd_tab_should_fence(uint64_t start, uint64_t end)
{
    return os_timer_elapsed_milliseconds(start, end) > 1500.0;
}

static inline bool os_timer_gesture_suppression_active(uint64_t start, uint64_t end)
{
    return os_timer_elapsed_milliseconds(start, end) < 1250.0;
}

static inline bool os_timer_mouse_resize_ready(uint64_t start, uint64_t end)
{
    return os_timer_elapsed_milliseconds(start, end) >= (1000.0 / 60.0);
}

struct rgba_color
{
    uint32_t p;
    float r;
    float g;
    float b;
    float a;
};

static const CFStringRef kAXEnhancedUserInterface = CFSTR("AXEnhancedUserInterface");

static const char *bool_str[] = { "off", "on" };

static const char *layer_str[] =
{
    [LAYER_AUTO]   = "auto",
    [LAYER_BELOW]  = "below",
    [LAYER_NORMAL] = "normal",
    [LAYER_ABOVE]  = "above"
};

static inline bool socket_open(int *sockfd)
{
    *sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (*sockfd == -1) return false;

    if (!socket_set_cloexec(*sockfd)) {
        close(*sockfd);
        *sockfd = -1;
        return false;
    }

    int no_sigpipe = 1;
    if (setsockopt(*sockfd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) != 0) {
        close(*sockfd);
        *sockfd = -1;
        return false;
    }
    return true;
}

static inline bool socket_connect(int sockfd, char *socket_path)
{
    struct sockaddr_un socket_address;
    if (!socket_address_init(&socket_address, socket_path)) return false;

    return connect(sockfd, (struct sockaddr *) &socket_address, sizeof(socket_address)) != -1;
}

static inline void socket_close(int sockfd)
{
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
}

#define MACH_SEND_SHORT_TIMEOUT_MS 50

#ifdef TESTS
static mach_msg_return_t (*mach_send_message)(mach_msg_header_t *, mach_msg_option_t, mach_msg_size_t,
                                               mach_msg_size_t, mach_port_name_t, mach_msg_timeout_t,
                                               mach_port_name_t) = mach_msg;
#else
#define mach_send_message mach_msg
#endif

static inline kern_return_t mach_send(mach_port_t port, void *data, uint32_t size)
{
    struct {
        mach_msg_header_t header;
        mach_msg_size_t descriptor_count;
        mach_msg_ool_descriptor_t descriptor;
    } msg = {0};

    msg.header.msgh_bits        = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND & MACH_MSGH_BITS_REMOTE_MASK, 0, 0, MACH_MSGH_BITS_COMPLEX);
    msg.header.msgh_size        = sizeof(msg);
    msg.header.msgh_remote_port = port;
    msg.descriptor_count        = 1;
    msg.descriptor.address      = data;
    msg.descriptor.size         = size;
    msg.descriptor.copy         = MACH_MSG_VIRTUAL_COPY;
    msg.descriptor.deallocate   = false;
    msg.descriptor.type         = MACH_MSG_OOL_DESCRIPTOR;

    return mach_send_message(&msg.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(msg), 0, 0,
                             MACH_SEND_SHORT_TIMEOUT_MS, 0);
}

static inline char *json_optional_bool(int value)
{
    if (value == 0) return "null";
    if (value == 1) return "true";

    return "false";
}

static inline char *json_bool(bool value)
{
    return value ? "true" : "false";
}

static inline struct rgba_color rgba_color_from_hex(uint32_t color)
{
    struct rgba_color result;
    result.p = color;
    result.r = ((color >> 0x10) & 0xff) / 255.0f;
    result.g = ((color >> 0x08) & 0xff) / 255.0f;
    result.b = ((color >> 0x00) & 0xff) / 255.0f;
    result.a = ((color >> 0x18) & 0xff) / 255.0f;
    return result;
}

static inline bool is_root(void)
{
    return getuid() == 0 || geteuid() == 0;
}

static inline bool string_equals(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static inline size_t utf8_valid_sequence_length(const unsigned char *bytes, size_t length)
{
    if (length == 0) return 0;

    unsigned char fst = bytes[0];
    if (fst <= 0x7f) return 1;

    if (fst >= 0xc2 && fst <= 0xdf) {
        return length >= 2 && bytes[1] >= 0x80 && bytes[1] <= 0xbf ? 2 : 0;
    }

    if (fst >= 0xe0 && fst <= 0xef) {
        if (length < 3 || bytes[2] < 0x80 || bytes[2] > 0xbf) return 0;
        if (fst == 0xe0) return bytes[1] >= 0xa0 && bytes[1] <= 0xbf ? 3 : 0;
        if (fst == 0xed) return bytes[1] >= 0x80 && bytes[1] <= 0x9f ? 3 : 0;
        return bytes[1] >= 0x80 && bytes[1] <= 0xbf ? 3 : 0;
    }

    if (fst >= 0xf0 && fst <= 0xf4) {
        if (length < 4 ||
            bytes[2] < 0x80 || bytes[2] > 0xbf ||
            bytes[3] < 0x80 || bytes[3] > 0xbf) return 0;
        if (fst == 0xf0) return bytes[1] >= 0x90 && bytes[1] <= 0xbf ? 4 : 0;
        if (fst == 0xf4) return bytes[1] >= 0x80 && bytes[1] <= 0x8f ? 4 : 0;
        return bytes[1] >= 0x80 && bytes[1] <= 0xbf ? 4 : 0;
    }

    return 0;
}

static inline bool utf8_validate_bytes(const void *data, size_t length)
{
    const unsigned char *bytes = data;
    for (size_t offset = 0; offset < length;) {
        size_t sequence_length = utf8_valid_sequence_length(bytes + offset, length - offset);
        if (sequence_length == 0) return false;
        offset += sequence_length;
    }
    return true;
}

static inline bool utf8_validate_string(const char *s)
{
    return utf8_validate_bytes(s, strlen(s));
}

static inline char *ts_string_escape(char *s)
{
    const unsigned char *bytes = (const unsigned char *) s;
    size_t length = strlen(s);
    size_t extra = 0;

    for (size_t offset = 0; offset < length;) {
        unsigned char c = bytes[offset];
        size_t increment = 0;
        size_t sequence_length = 1;

        if (c >= 0x80) {
            sequence_length = utf8_valid_sequence_length(bytes + offset, length - offset);
            if (sequence_length == 0) {
                increment = 5;
                sequence_length = 1;
            }
        } else if (c == '"' || c == '\\' || c == '\b' || c == '\f' ||
                   c == '\n' || c == '\r' || c == '\t') {
            increment = 1;
        } else if (c <= 0x1f) {
            increment = 5;
        }

        if (extra > SIZE_MAX - increment) {
            fprintf(stderr, "fatal error: JSON string escaping size overflow\n");
            exit(EXIT_FAILURE);
        }
        extra += increment;
        offset += sequence_length;
    }

    if (extra == 0) return NULL;
    if (extra == SIZE_MAX || length > SIZE_MAX - extra - 1) {
        fprintf(stderr, "fatal error: JSON string escaping size overflow\n");
        exit(EXIT_FAILURE);
    }

    size_t size_in_bytes = length + extra;
    char *result = ts_alloc_unaligned(size_in_bytes + 1);
    char *dst = result;

    for (size_t offset = 0; offset < length;) {
        unsigned char c = bytes[offset];
        if (c >= 0x80) {
            size_t sequence_length = utf8_valid_sequence_length(bytes + offset, length - offset);
            if (sequence_length != 0) {
                memcpy(dst, bytes + offset, sequence_length);
                dst += sequence_length;
                offset += sequence_length;
            } else {
                memcpy(dst, "\\ufffd", 6);
                dst += 6;
                ++offset;
            }
        } else {
            ++offset;
            if (c == '"') {
                *dst++ = '\\';
                *dst++ = '"';
            } else if (c == '\\') {
                *dst++ = '\\';
                *dst++ = '\\';
            } else if (c == '\b') {
                *dst++ = '\\';
                *dst++ = 'b';
            } else if (c == '\f') {
                *dst++ = '\\';
                *dst++ = 'f';
            } else if (c == '\n') {
                *dst++ = '\\';
                *dst++ = 'n';
            } else if (c == '\r') {
                *dst++ = '\\';
                *dst++ = 'r';
            } else if (c == '\t') {
                *dst++ = '\\';
                *dst++ = 't';
            } else if (c <= 0x1f) {
                static const char hex[] = "0123456789abcdef";
                *dst++ = '\\';
                *dst++ = 'u';
                *dst++ = '0';
                *dst++ = '0';
                *dst++ = hex[c >> 4];
                *dst++ = hex[c & 0x0f];
            } else {
                *dst++ = (char) c;
            }
        }
    }

    *dst = '\0';
    return result;
}


static inline CFStringRef CFSTRINGNUM32(int32_t num)
{
    char num_str[255] = {0};
    snprintf(num_str, sizeof(num_str), "%d", num);
    return CFStringCreateWithCString(NULL, num_str, kCFStringEncodingMacRoman);
}

#ifndef YABAI_CFNUMBER_CREATE
#define YABAI_CFNUMBER_CREATE CFNumberCreate
#endif
#ifndef YABAI_CFARRAY_CREATE_MUTABLE
#define YABAI_CFARRAY_CREATE_MUTABLE CFArrayCreateMutable
#endif
#ifndef YABAI_CFDICTIONARY_CREATE
#define YABAI_CFDICTIONARY_CREATE CFDictionaryCreate
#endif

static inline CFNumberRef CFNUM32(int32_t num)
{
    return YABAI_CFNUMBER_CREATE(NULL, kCFNumberSInt32Type, &num);
}

static inline bool sls_window_disable_shadow(uint32_t id)
{
    CFNumberRef density = CFNUM32(0);
    if (!density) return false;

    const void *keys[1] = { CFSTR("com.apple.WindowShadowDensity") };
    const void *values[1] = { density };
    CFDictionaryRef options = YABAI_CFDICTIONARY_CREATE(NULL, keys, values, 1,
                                                        &kCFTypeDictionaryKeyCallBacks,
                                                        &kCFTypeDictionaryValueCallBacks);
    CFRelease(density);
    if (!options) return false;

    bool result = SLSWindowSetShadowProperties(id, options) == kCGErrorSuccess;
    CFRelease(options);
    return result;
}

// An empty input is a successful conversion and returns an empty array.
static inline CFArrayRef cfarray_of_cfnumbers(void *values, size_t size, int count, CFNumberType type)
{
    if (count < 0 || (count && (!values || !size))) return NULL;

    CFMutableArrayRef result = YABAI_CFARRAY_CREATE_MUTABLE(NULL, count, &kCFTypeArrayCallBacks);
    if (!result) return NULL;

    for (int i = 0; i < count; ++i) {
        CFNumberRef number = YABAI_CFNUMBER_CREATE(NULL, type, ((char *) values) + (size * i));
        if (!number) {
            CFRelease(result);
            return NULL;
        }
        CFArrayAppendValue(result, number);
        CFRelease(number);
    }

    return result;
}

static inline char *ts_cfstring_copy(CFStringRef string)
{
    CFIndex num_bytes = CFStringGetMaximumSizeForEncoding(CFStringGetLength(string), kCFStringEncodingUTF8);
    char *result = ts_alloc_unaligned(num_bytes + 1);

    if (!CFStringGetCString(string, result, num_bytes + 1, kCFStringEncodingUTF8)) {
        result = NULL;
    }

    return result;
}

static inline char *cfstring_copy(CFStringRef string)
{
    CFIndex num_bytes = CFStringGetMaximumSizeForEncoding(CFStringGetLength(string), kCFStringEncodingUTF8);
    char *result = malloc(num_bytes + 1);
    if (!result) return NULL;

    if (!CFStringGetCString(string, result, num_bytes + 1, kCFStringEncodingUTF8)) {
        free(result);
        result = NULL;
    }

    return result;
}

static inline char *ts_string_copy(char *s)
{
    int length = strlen(s);
    char *result = ts_alloc_unaligned(length + 1);

    memcpy(result, s, length);
    result[length] = '\0';
    return result;
}

static inline char *string_copy(char *s)
{
    int length = strlen(s);
    char *result = malloc(length + 1);
    if (!result) return NULL;

    memcpy(result, s, length);
    result[length] = '\0';
    return result;
}

static inline bool directory_exists(char *filename)
{
    struct stat buffer;

    if (stat(filename, &buffer) != 0) {
        return false;
    }

    return S_ISDIR(buffer.st_mode);
}

static inline bool file_exists(char *filename)
{
    struct stat buffer;

    if (stat(filename, &buffer) != 0) {
        return false;
    }

    if (buffer.st_mode & S_IFDIR) {
        return false;
    }

    return true;
}

static inline bool file_can_execute(char *filename)
{
    struct stat buffer;

    if (stat(filename, &buffer) != 0) {
        return false;
    }

    return S_ISREG(buffer.st_mode) && access(filename, X_OK) == 0;
}

static bool get_config_file(char *restrict filename, char *restrict buffer, int buffer_size)
{
    char *xdg_home = getenv("XDG_CONFIG_HOME");
    if (xdg_home && *xdg_home) {
        snprintf(buffer, buffer_size, "%s/yabai/%s", xdg_home, filename);
        if (file_exists(buffer)) return true;
    }

    char *home = getenv("HOME");
    if (!home) return false;

    snprintf(buffer, buffer_size, "%s/.config/yabai/%s", home, filename);
    if (file_exists(buffer)) return true;

    snprintf(buffer, buffer_size, "%s/.%s", home, filename);
    return file_exists(buffer);
}

static void config_file_exec_argv(char *config_file, bool executable, char **argv)
{
    if (executable) {
        argv[0] = config_file;
        argv[1] = NULL;
    } else {
        argv[0] = "/usr/bin/env";
        argv[1] = "sh";
        argv[2] = config_file;
        argv[3] = NULL;
    }
}

static int config_file_spawn(char *config_file, bool executable, pid_t *pid)
{
    extern char **environ;
    char *argv[4];
    config_file_exec_argv(config_file, executable, argv);
    int status = posix_spawn(pid, argv[0], NULL, NULL, argv, environ);

    if (status == ENOEXEC && executable) {
        config_file_exec_argv(config_file, false, argv);
        status = posix_spawn(pid, argv[0], NULL, NULL, argv, environ);
    }

    return status;
}

static void exec_config_file(char *config_file, int config_file_size)
{
    if (config_file[0] == '\0' && !get_config_file("yabairc", config_file, config_file_size)) {
        warn("yabai: could not locate config file..\n");
        notify("configuration", "could not locate config file..");
        return;
    }

    if (!file_exists(config_file)) {
        warn("yabai: configuration file '%s' does not exist..\n", config_file);
        notify("configuration", "file '%s' does not exist..", config_file);
        return;
    }

    pid_t pid;
    int status = config_file_spawn(config_file, file_can_execute(config_file), &pid);
    if (status != 0) {
        warn("yabai: failed to load config file '%s': %s\n", config_file, strerror(status));
        notify("configuration", "failed to load file '%s'", config_file);
    }
}

static inline bool ax_privilege(void)
{
    const void *keys[] = { kAXTrustedCheckOptionPrompt };
    const void *values[] = { kCFBooleanTrue };
    CFDictionaryRef options = YABAI_CFDICTIONARY_CREATE(NULL, keys, values, array_count(keys),
                                                        &kCFCopyStringDictionaryKeyCallBacks,
                                                        &kCFTypeDictionaryValueCallBacks);
    if (!options) return false;

    bool result = AXIsProcessTrustedWithOptions(options);
    CFRelease(options);
    return result;
}

static inline uint32_t ax_window_id(AXUIElementRef ref)
{
    uint32_t wid = 0;
    _AXUIElementGetWindow(ref, &wid);
    return wid;
}

static inline pid_t ax_window_pid(AXUIElementRef ref)
{
    return *(pid_t *)((void *) ref + 0x10);
}

static inline bool ax_enhanced_userinterface(AXUIElementRef ref)
{
    Boolean result = 0;
    CFTypeRef value;

    if (AXUIElementCopyAttributeValue(ref, kAXEnhancedUserInterface, &value) == kAXErrorSuccess) {
        result = CFBooleanGetValue(value);
        CFRelease(value);
    }

    return result;
}

#define AX_ENHANCED_UI_WORKAROUND(r, c) \
{\
    bool eui = ax_enhanced_userinterface(r); \
    if (eui) AXUIElementSetAttributeValue(r, kAXEnhancedUserInterface, kCFBooleanFalse); \
    c \
    if (eui) AXUIElementSetAttributeValue(r, kAXEnhancedUserInterface, kCFBooleanTrue); \
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static inline bool psn_equals(ProcessSerialNumber *a, ProcessSerialNumber *b)
{
    Boolean result;
    SameProcess(a, b, &result);
    return result == 1;
}
#pragma clang diagnostic pop

static inline float cgrect_clamp_x_radius(CGRect frame, float radius)
{
    if (radius * 2 > CGRectGetWidth(frame)) {
        radius = CGRectGetWidth(frame) / 2;
    }
    return radius;
}

static inline float cgrect_clamp_y_radius(CGRect frame, float radius)
{
    if (radius * 2 > CGRectGetHeight(frame)) {
        radius = CGRectGetHeight(frame) / 2;
    }
    return radius;
}

static inline bool cgrect_contains_point(CGRect r, CGPoint p)
{
    return p.x >= r.origin.x && p.x <= r.origin.x + r.size.width &&
           p.y >= r.origin.y && p.y <= r.origin.y + r.size.height;
}

static inline bool triangle_contains_point(CGPoint t[3], CGPoint p)
{
    float l1 = (p.x - t[0].x) * (t[2].y - t[0].y) - (t[2].x - t[0].x) * (p.y - t[0].y);
    float l2 = (p.x - t[1].x) * (t[0].y - t[1].y) - (t[0].x - t[1].x) * (p.y - t[1].y);
    float l3 = (p.x - t[2].x) * (t[1].y - t[2].y) - (t[1].x - t[2].x) * (p.y - t[2].y);

    return (l1 > 0.0f && l2 > 0.0f && l3 > 0.0f) || (l1 < 0.0f && l2 < 0.0f && l3 < 0.0f);
}

static inline int regex_match(bool valid, regex_t *regex, const char *match)
{
    if (!valid) return REGEX_MATCH_UD;

    int result = regexec(regex, match, 0, NULL, 0);
    return result == 0 ? REGEX_MATCH_YES : REGEX_MATCH_NO;
}

static inline float clampf_range(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static CGImageRef cgimage_restore_alpha(CGImageRef image)
{
    size_t width = CGImageGetWidth(image);
    size_t height = CGImageGetHeight(image);
    if (!width || !height || width > SIZE_MAX / 4) return NULL;

    size_t pitch = width * 4;
    if (height > SIZE_MAX / pitch) return NULL;
    uint8_t *data = (uint8_t *) calloc(height, pitch);
    if (!data) return NULL;

    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
    if (!color_space) {
        free(data);
        return NULL;
    }

    CGContextRef context = CGBitmapContextCreate(data, width, height, 8, pitch, color_space, kCGBitmapByteOrder32Big | kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(color_space);
    if (!context) {
        free(data);
        return NULL;
    }
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);

#ifdef __x86_64__
    __m128      inv255 = _mm_set1_ps(1.0f / 255.0f);
    __m128      one255 = _mm_set1_ps(255.0f);
    __m128        zero = _mm_set1_ps(0.0f);
    __m128i    mask_ff = _mm_set1_epi32(0xff);
#elif __arm64__
    float32x4_t inv255 = vdupq_n_f32(1.0f / 255.0f);
    float32x4_t one255 = vdupq_n_f32(255.0f);
    float32x4_t   zero = vdupq_n_f32(0.0f);
    int32x4_t  mask_ff = vdupq_n_s32(0xff);
#endif

    size_t pixel_count = height * width;
    size_t simd_count = pixel_count & ~(size_t) 3;
    uint32_t *pixel = (uint32_t *) data;
    for (size_t i = 0; i < simd_count; i += 4) {
#ifdef __x86_64__
        __m128i source = _mm_loadu_si128((__m128i *) pixel);
        __m128 r = _mm_cvtepi32_ps(_mm_and_si128(source, mask_ff));
        __m128 g = _mm_cvtepi32_ps(_mm_and_si128(_mm_srli_epi32(source,  8), mask_ff));
        __m128 b = _mm_cvtepi32_ps(_mm_and_si128(_mm_srli_epi32(source, 16), mask_ff));
        __m128 a = _mm_cvtepi32_ps(_mm_and_si128(_mm_srli_epi32(source, 24), mask_ff));
        __m128i mask = _mm_castps_si128(_mm_cmpgt_ps(a, zero));

        r = _mm_mul_ps(one255, _mm_div_ps(r, a));
        g = _mm_mul_ps(one255, _mm_div_ps(g, a));
        b = _mm_mul_ps(one255, _mm_div_ps(b, a));

        a = one255;

        r = _mm_mul_ps(inv255, _mm_mul_ps(r, a));
        g = _mm_mul_ps(inv255, _mm_mul_ps(g, a));
        b = _mm_mul_ps(inv255, _mm_mul_ps(b, a));

        __m128 half = _mm_set1_ps(0.5f);
        __m128i sr = _mm_cvttps_epi32(_mm_add_ps(r, half));
        __m128i sg = _mm_slli_epi32(_mm_cvttps_epi32(_mm_add_ps(g, half)),  8);
        __m128i sb = _mm_slli_epi32(_mm_cvttps_epi32(_mm_add_ps(b, half)), 16);
        __m128i sa = _mm_slli_epi32(_mm_cvttps_epi32(_mm_add_ps(a, half)), 24);

        __m128i color = _mm_or_si128(_mm_or_si128(_mm_or_si128(sr, sg), sb), sa);
        __m128i masked_color = _mm_or_si128(_mm_and_si128(mask, color), _mm_andnot_si128(mask, source));
        _mm_storeu_si128((__m128i *) pixel, masked_color);
#elif __arm64__
        int32x4_t source = vld1q_s32((int32_t *) pixel);
        float32x4_t r = vcvtq_f32_s32(vandq_s32(source, mask_ff));
        float32x4_t g = vcvtq_f32_s32(vandq_s32(vshlq_u32(source, vdupq_n_s32(-8)),  mask_ff));
        float32x4_t b = vcvtq_f32_s32(vandq_s32(vshlq_u32(source, vdupq_n_s32(-16)), mask_ff));
        float32x4_t a = vcvtq_f32_s32(vandq_s32(vshlq_u32(source, vdupq_n_s32(-24)), mask_ff));
        int32x4_t mask = vreinterpretq_s32_f32(vcgtq_f32(a, zero));

        r = vmulq_f32(one255, vdivq_f32(r, a));
        g = vmulq_f32(one255, vdivq_f32(g, a));
        b = vmulq_f32(one255, vdivq_f32(b, a));

        a = one255;

        r = vmulq_f32(inv255, vmulq_f32(r, a));
        g = vmulq_f32(inv255, vmulq_f32(g, a));
        b = vmulq_f32(inv255, vmulq_f32(b, a));

        float32x4_t half = vdupq_n_f32(0.5f);
        int32x4_t sr = vcvtq_s32_f32(vaddq_f32(r, half));
        int32x4_t sg = vshlq_s32(vcvtq_s32_f32(vaddq_f32(g, half)), vdupq_n_s32(8));
        int32x4_t sb = vshlq_s32(vcvtq_s32_f32(vaddq_f32(b, half)), vdupq_n_s32(16));
        int32x4_t sa = vshlq_s32(vcvtq_s32_f32(vaddq_f32(a, half)), vdupq_n_s32(24));

        int32x4_t color = vorrq_s32(vorrq_s32(vorrq_s32(sr, sg), sb), sa);
        int32x4_t masked_color = vorrq_s32(vandq_s32(color, mask), vbicq_s32(source, mask));
        vst1q_s32((int32_t *) pixel, masked_color);
#endif
        pixel += 4;
    }

    for (size_t i = simd_count; i < pixel_count; ++i) {
        uint32_t source = ((uint32_t *) data)[i];
        uint32_t alpha = (source >> 24) & 0xff;
        if (!alpha) continue;

        uint32_t red = source & 0xff;
        uint32_t green = (source >> 8) & 0xff;
        uint32_t blue = (source >> 16) & 0xff;
        red = (uint32_t) lroundf(255.0f * (float) red / (float) alpha);
        green = (uint32_t) lroundf(255.0f * (float) green / (float) alpha);
        blue = (uint32_t) lroundf(255.0f * (float) blue / (float) alpha);
        ((uint32_t *) data)[i] = (red & 0xff) | ((green & 0xff) << 8) |
                                 ((blue & 0xff) << 16) | 0xff000000;
    }

    CGImageRef result = CGBitmapContextCreateImage(context);
    CGContextRelease(context);

    free(data);
    return result;
}

#endif
