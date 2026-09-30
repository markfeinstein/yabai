#include "sa.h"
#include "sa_message.h"

extern int csr_get_active_config(uint32_t *config);
#define CSR_ALLOW_UNRESTRICTED_FS 0x02
#define CSR_ALLOW_TASK_FOR_PID    0x04

extern char g_sa_socket_file[MAXLEN];

static char osax_base_dir[MAXLEN];
static char osax_contents_dir[MAXLEN];
static char osax_contents_macos_dir[MAXLEN];
static char osax_contents_res_dir[MAXLEN];
static char osax_info_plist[MAXLEN];
static char osax_payload_dir[MAXLEN];
static char osax_payload_contents_dir[MAXLEN];
static char osax_payload_contents_macos_dir[MAXLEN];
static char osax_payload_plist[MAXLEN];
static char osax_bin_payload[MAXLEN];
static char osax_bin_loader[MAXLEN];

static char sa_plist[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\">\n"
    "<dict>\n"
    "<key>CFBundleDevelopmentRegion</key>\n"
    "<string>en</string>\n"
    "<key>CFBundleExecutable</key>\n"
    "<string>loader</string>\n"
    "<key>CFBundleIdentifier</key>\n"
    "<string>com.asmvik.yabai-osax</string>\n"
    "<key>CFBundleInfoDictionaryVersion</key>\n"
    "<string>6.0</string>\n"
    "<key>CFBundleName</key>\n"
    "<string>yabai</string>\n"
    "<key>CFBundlePackageType</key>\n"
    "<string>osax</string>\n"
    "<key>CFBundleShortVersionString</key>\n"
    "<string>"OSAX_VERSION"</string>\n"
    "<key>CFBundleVersion</key>\n"
    "<string>"OSAX_VERSION"</string>\n"
    "<key>NSHumanReadableCopyright</key>\n"
    "<string>Copyright © 2019 Åsmund Vikane. All rights reserved.</string>\n"
    "<key>OSAXHandlers</key>\n"
    "<dict>\n"
    "</dict>\n"
    "</dict>\n"
    "</plist>";

static char sa_bundle_plist[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\">\n"
    "<dict>\n"
    "<key>CFBundleDevelopmentRegion</key>\n"
    "<string>en</string>\n"
    "<key>CFBundleExecutable</key>\n"
    "<string>payload</string>\n"
    "<key>CFBundleIdentifier</key>\n"
    "<string>com.asmvik.yabai-sa</string>\n"
    "<key>CFBundleInfoDictionaryVersion</key>\n"
    "<string>6.0</string>\n"
    "<key>CFBundleName</key>\n"
    "<string>payload</string>\n"
    "<key>CFBundlePackageType</key>\n"
    "<string>BNDL</string>\n"
    "<key>CFBundleShortVersionString</key>\n"
    "<string>"OSAX_VERSION"</string>\n"
    "<key>CFBundleVersion</key>\n"
    "<string>"OSAX_VERSION"</string>\n"
    "<key>NSHumanReadableCopyright</key>\n"
    "<string>Copyright © 2019 Åsmund Vikane. All rights reserved.</string>\n"
    "<key>NSPrincipalClass</key>\n"
    "<string></string>\n"
    "</dict>\n"
    "</plist>";

static void scripting_addition_set_path(void)
{
    snprintf(osax_base_dir, sizeof(osax_base_dir), "%s", "/Library/ScriptingAdditions/yabai.osax");

    snprintf(osax_contents_dir, sizeof(osax_contents_dir), "%s/%s", osax_base_dir, "Contents");
    snprintf(osax_contents_macos_dir, sizeof(osax_contents_macos_dir), "%s/%s", osax_contents_dir, "MacOS");
    snprintf(osax_contents_res_dir, sizeof(osax_contents_res_dir), "%s/%s", osax_contents_dir, "Resources");
    snprintf(osax_info_plist, sizeof(osax_info_plist), "%s/%s", osax_contents_dir, "Info.plist");

    snprintf(osax_payload_dir, sizeof(osax_payload_dir), "%s/%s", osax_contents_res_dir, "payload.bundle");
    snprintf(osax_payload_contents_dir, sizeof(osax_payload_contents_dir), "%s/%s", osax_payload_dir, "Contents");
    snprintf(osax_payload_contents_macos_dir, sizeof(osax_payload_contents_macos_dir), "%s/%s", osax_payload_contents_dir, "MacOS");
    snprintf(osax_payload_plist, sizeof(osax_payload_plist), "%s/%s", osax_payload_contents_dir, "Info.plist");

    snprintf(osax_bin_loader, sizeof(osax_bin_loader), "%s/%s", osax_contents_macos_dir, "loader");
    snprintf(osax_bin_payload, sizeof(osax_bin_payload), "%s/%s", osax_payload_contents_macos_dir, "payload");
}

static bool scripting_addition_create_directory(void)
{
    if (mkdir(osax_base_dir, 0755))                   goto err;
    if (mkdir(osax_contents_dir, 0755))               goto err;
    if (mkdir(osax_contents_macos_dir, 0755))         goto err;
    if (mkdir(osax_contents_res_dir, 0755))           goto err;
    if (mkdir(osax_payload_dir, 0755))                goto err;
    if (mkdir(osax_payload_contents_dir, 0755))       goto err;
    if (mkdir(osax_payload_contents_macos_dir, 0755)) goto err;
    return true;
err:
    return false;
}

static bool scripting_addition_write_file(char *buffer, unsigned int size, char *file, char *file_mode)
{
    FILE *handle = fopen(file, file_mode);
    if (!handle) return false;

    size_t bytes = fwrite(buffer, size, 1, handle);
    bool result = bytes == 1;
    fclose(handle);

    return result;
}

static bool scripting_addition_prepare_binary(char *path)
{
    if (chmod(path, 0755) != 0) return false;

    const char *const args[] = { "/usr/bin/codesign", "-f", "-s", "-", path, NULL };
    return safe_exec((char *const *) args, true) == 0;
}

static bool scripting_addition_prepare_binaries(void)
{
    return scripting_addition_prepare_binary(osax_bin_loader) &&
           scripting_addition_prepare_binary(osax_bin_payload);
}

static void scripting_addition_restart_dock(void)
{
    NSArray *dock = [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.dock"];
    [dock makeObjectsPerformSelector:@selector(terminate)];
}

static bool scripting_addition_socket_path_for_sudo_uid(const char *sudo_uid, char *socket_path, size_t socket_path_size)
{
    if (!sudo_uid || !*sudo_uid || !socket_path || !socket_path_size) return false;

    uintmax_t uid_value = 0;
    uintmax_t uid_max = (uintmax_t) ((uid_t) -1);
    for (const unsigned char *cursor = (const unsigned char *) sudo_uid; *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return false;

        unsigned int digit = *cursor - '0';
        if (uid_value > (uid_max - digit) / 10) return false;
        uid_value = uid_value * 10 + digit;
    }

    if (uid_value == 0) return false;

    struct passwd *pw = getpwuid((uid_t) uid_value);
    if (!pw || !pw->pw_name || !*pw->pw_name) return false;

    int length = snprintf(socket_path, socket_path_size, SA_SOCKET_PATH_FMT, pw->pw_name);
    return length >= 0 && (size_t) length < socket_path_size;
}

static bool scripting_addition_set_socket_path(void)
{
    return scripting_addition_socket_path_for_sudo_uid(getenv("SUDO_UID"), g_sa_socket_file, sizeof(g_sa_socket_file));
}

static bool scripting_addition_is_installed(void)
{
    if (osax_base_dir[0] == 0) scripting_addition_set_path();

    DIR *dir = opendir(osax_base_dir);
    if (!dir) return false;

    closedir(dir);
    return true;
}

static int scripting_addition_check(void)
{
    bool result = 0;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    if (scripting_addition_is_installed()) {
        NSString *payload_path = [NSString stringWithUTF8String:osax_payload_dir];
        NSBundle *payload_bundle = [NSBundle bundleWithPath:payload_path];
        NSString *ns_version = [payload_bundle objectForInfoDictionaryKey:@"CFBundleVersion"];

        bool status = string_equals([ns_version UTF8String], OSAX_VERSION);
        result = status ? 0 : 1;
    } else {
        result = 1;
    }

    [pool drain];
    return result;
}

static bool scripting_addition_remove_path(char *path)
{
    const char *const args[] = { "/bin/rm", "-rf", path, NULL };
    return safe_exec((char *const *) args, true) == 0;
}

static bool scripting_addition_remove(void)
{
    return scripting_addition_remove_path(osax_base_dir);
}

static int scripting_addition_install(void)
{
    umask(S_IWGRP | S_IWOTH);

    if ((scripting_addition_is_installed()) && (!scripting_addition_remove())) {
        return 1;
    }

    if (!scripting_addition_create_directory()) {
        goto cleanup;
    }

    if (!scripting_addition_write_file(sa_plist, strlen(sa_plist), osax_info_plist, "w")) {
        goto cleanup;
    }

    if (!scripting_addition_write_file(sa_bundle_plist, strlen(sa_bundle_plist), osax_payload_plist, "w")) {
        goto cleanup;
    }

    if (!scripting_addition_write_file((char *) __src_osax_loader, __src_osax_loader_len, osax_bin_loader, "wb")) {
        goto cleanup;
    }

    if (!scripting_addition_write_file((char *) __src_osax_payload, __src_osax_payload_len, osax_bin_payload, "wb")) {
        goto cleanup;
    }

    if (!scripting_addition_prepare_binaries()) {
        goto cleanup;
    }

    scripting_addition_restart_dock();
    return 0;

cleanup:
    scripting_addition_remove();
    return 2;
}

static bool scripting_addition_handshake_fd(int sockfd, char *version, size_t version_capacity,
                                            uint32_t *attrib, uint64_t deadline)
{
    const char request[] = { 0x01, 0x00, SA_OPCODE_HANDSHAKE };
    return socket_io_write_all_until(sockfd, request, sizeof(request), deadline) &&
           socket_io_read_cstring_until(sockfd, version, version_capacity, deadline) &&
           socket_io_read_exact_until(sockfd, attrib, sizeof(*attrib), deadline);
}

static bool scripting_addition_request_handshake(char *version, size_t version_capacity, uint32_t *attrib)
{
    int sockfd;
    bool result = false;

    if (socket_open(&sockfd)) {
        if (socket_connect(sockfd, g_sa_socket_file)) {
            uint64_t deadline = socket_io_deadline_after_ms(SOCKET_IO_PRODUCTION_TIMEOUT_MS);
            result = scripting_addition_handshake_fd(sockfd, version, version_capacity, attrib, deadline);
        }

        socket_close(sockfd);
    }

    return result;
}

static int scripting_addition_perform_validation(void)
{
    uint32_t attrib = 0;
    char version[SA_SOCKET_BUFF_LEN] = {0};
    bool is_latest_version_installed = scripting_addition_check() == 0;

    if (!scripting_addition_request_handshake(version, sizeof(version), &attrib)) {
        notify("scripting-addition", "connection failed!");
        return 1;
    }

    if (string_equals(version, OSAX_VERSION)) {
        NSOperatingSystemVersion os_version = [[NSProcessInfo processInfo] operatingSystemVersion];
        uint32_t required_attrib = os_version.majorVersion == 27
                                 ? OSAX_ATTRIB_DOCK_SPACES
                                 : OSAX_ATTRIB_ALL;

        if ((attrib & required_attrib) == required_attrib) {
            notify("scripting-addition", "payload v%s", version);
            return 0;
        }

        notify("scripting-addition", "payload (0x%X) doesn't support this macOS version!", attrib);
        return 1;
    }

    if (!is_latest_version_installed) {
        notify("scripting-addition", "payload is outdated, updating..");
        return scripting_addition_install();
    }

    notify("scripting-addition", "payload is outdated, restarting Dock.app..");
    scripting_addition_restart_dock();
    return 0;
}

static bool scripting_addition_is_sip_friendly(void)
{
    uint32_t config = 0;
    csr_get_active_config(&config);

    if (!(config & CSR_ALLOW_UNRESTRICTED_FS)) {
        return false;
    }

    if (!(config & CSR_ALLOW_TASK_FOR_PID)) {
        return false;
    }

    return true;
}

#ifdef __arm64__
static bool scripting_addition_is_arm64e_enabled(void)
{
    char bootargs[2048];
    size_t len = sizeof(bootargs) - 1;

    if (sysctlbyname("kern.bootargs", bootargs, &len, NULL, 0) == 0) {
        if (strnstr(bootargs, "-arm64e_preview_abi", len)) {
            return true;
        }
    }

    return false;
}
#endif

static bool mach_loader_inject_payload(void)
{
    FILE *handle = popen("/Library/ScriptingAdditions/yabai.osax/Contents/MacOS/loader", "r");
    if (!handle) return false;

    int result = pclose(handle);
    if (WIFEXITED(result)) {
        return WEXITSTATUS(result) == 0;
    } else if (WIFSIGNALED(result)) {
        return false;
    } else if (WIFSTOPPED(result)) {
        return false;
    }

    return false;
}

int scripting_addition_uninstall(void)
{
    if (!scripting_addition_is_sip_friendly()) {
        warn("yabai: System Integrity Protection: Filesystem Protections and Debugging Restrictions must be disabled!\n");
        notify("scripting-addition", "System Integrity Protection: Filesystem Protections and Debugging Restrictions must be disabled!");
        return 1;
    }

    if (!is_root()) {
        warn("yabai: scripting-addition must be uninstalled as root!\n");
        notify("scripting-addition", "must be uninstalled as root!");
        return 1;
    }

    if (!scripting_addition_is_installed()) return  0;
    if (!scripting_addition_remove())       return -1;
    return 0;
}

int scripting_addition_load(void)
{
    int result = 0;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    if (!is_root()) {
        warn("yabai: scripting-addition must be loaded as root!\n");
        notify("scripting-addition", "must be loaded as root!");
        result = 1;
        goto out;
    }

    if (!scripting_addition_set_socket_path()) {
        warn("yabai: scripting-addition could not determine socket path from SUDO_UID!\n");
        notify("scripting-addition", "could not determine socket path from SUDO_UID!");
        result = 1;
        goto out;
    }

    if (!scripting_addition_is_sip_friendly()) {
        warn("yabai: System Integrity Protection: Filesystem Protections and Debugging Restrictions must be disabled!\n");
        notify("scripting-addition", "System Integrity Protection: Filesystem Protections and Debugging Restrictions must be disabled!");
        result = 1;
        goto out;
    }

    if (scripting_addition_check() != 0) {
        result = scripting_addition_install();
        goto out;
    }

#ifdef __arm64__
    if (!scripting_addition_is_arm64e_enabled()) {
        warn("yabai: missing required nvram boot-arg '-arm64e_preview_abi'!\n");
        notify("scripting-addition", "missing required nvram boot-arg '-arm64e_preview_abi'!");
        result = 1;
        goto out;
    }
#endif

    if (!mach_loader_inject_payload()) {
        warn("yabai: scripting-addition failed to inject payload into Dock.app!\n");
        notify("scripting-addition", "failed to inject payload into Dock.app!");
        result = 1;
        goto out;
    }

    result = scripting_addition_perform_validation();

out:
    [pool drain];
    return result;
}

static bool scripting_addition_send_bytes_fd(int sockfd, char *bytes, size_t length, uint64_t deadline)
{
    uint8_t status = 0;
    return socket_io_write_all_until(sockfd, bytes, length, deadline) &&
           socket_io_read_exact_until(sockfd, &status, sizeof(status), deadline) &&
           status != 0;
}

static bool scripting_addition_send_bytes(char *bytes, size_t length)
{
    int sockfd;
    bool result = false;

    if (socket_open(&sockfd)) {
        if (socket_connect(sockfd, g_sa_socket_file)) {
            uint64_t deadline = socket_io_deadline_after_ms(SOCKET_IO_PRODUCTION_TIMEOUT_MS);
            result = scripting_addition_send_bytes_fd(sockfd, bytes, length, deadline);
        }

        socket_close(sockfd);
    }

    return result;
}

static bool scripting_addition_send_payload(struct sa_payload_builder *builder)
{
    size_t length;
    return sa_payload_builder_finish(builder, &length) &&
           scripting_addition_send_bytes((char *) builder->bytes, length);
}

#define SA_PAYLOAD_BEGIN(op) \
    uint8_t bytes[SA_SOCKET_BUFF_LEN]; \
    struct sa_payload_builder builder; \
    if (!sa_payload_builder_init(&builder, bytes, sizeof(bytes), (op))) return false
#define SA_PAYLOAD_APPEND(value) \
    do { if (!sa_payload_builder_append(&builder, &(value), sizeof(value))) return false; } while (0)

bool scripting_addition_focus_space(uint64_t sid)
{
    SA_PAYLOAD_BEGIN(SA_OPCODE_SPACE_FOCUS);
    SA_PAYLOAD_APPEND(sid);
    return scripting_addition_send_payload(&builder);
}

bool scripting_addition_create_space(uint64_t sid)
{
    SA_PAYLOAD_BEGIN(SA_OPCODE_SPACE_CREATE);
    SA_PAYLOAD_APPEND(sid);
    return scripting_addition_send_payload(&builder);
}

bool scripting_addition_destroy_space(uint64_t sid)
{
    SA_PAYLOAD_BEGIN(SA_OPCODE_SPACE_DESTROY);
    SA_PAYLOAD_APPEND(sid);
    return scripting_addition_send_payload(&builder);
}

bool scripting_addition_move_space_to_display(uint64_t src_sid, uint64_t dst_sid, uint64_t src_prev_sid, bool focus)
{
    SA_PAYLOAD_BEGIN(SA_OPCODE_SPACE_MOVE);
    SA_PAYLOAD_APPEND(src_sid);
    SA_PAYLOAD_APPEND(dst_sid);
    SA_PAYLOAD_APPEND(src_prev_sid);
    SA_PAYLOAD_APPEND(focus);
    return scripting_addition_send_payload(&builder);
}

bool scripting_addition_move_space_after_space(uint64_t src_sid, uint64_t dst_sid, bool focus)
{
    uint64_t dummy_sid = 0;
    SA_PAYLOAD_BEGIN(SA_OPCODE_SPACE_MOVE);
    SA_PAYLOAD_APPEND(src_sid);
    SA_PAYLOAD_APPEND(dst_sid);
    SA_PAYLOAD_APPEND(dummy_sid);
    SA_PAYLOAD_APPEND(focus);
    return scripting_addition_send_payload(&builder);
}

bool scripting_addition_move_window(uint32_t wid, int x, int y)
{
    SA_PAYLOAD_BEGIN(SA_OPCODE_WINDOW_MOVE);
    SA_PAYLOAD_APPEND(wid);
    SA_PAYLOAD_APPEND(x);
    SA_PAYLOAD_APPEND(y);
    return scripting_addition_send_payload(&builder);
}

bool scripting_addition_set_opacity(uint32_t wid, float opacity, float duration)
{
    SA_PAYLOAD_BEGIN(duration > 0.0f ? SA_OPCODE_WINDOW_OPACITY_FADE : SA_OPCODE_WINDOW_OPACITY);
    SA_PAYLOAD_APPEND(wid);
    SA_PAYLOAD_APPEND(opacity);
    SA_PAYLOAD_APPEND(duration);
    return scripting_addition_send_payload(&builder);
}

bool scripting_addition_set_layer(uint32_t wid, int layer)
{
    SA_PAYLOAD_BEGIN(SA_OPCODE_WINDOW_LAYER);
    SA_PAYLOAD_APPEND(wid);
    SA_PAYLOAD_APPEND(layer);
    return scripting_addition_send_payload(&builder);
}

bool scripting_addition_set_sticky(uint32_t wid, bool sticky)
{
    SA_PAYLOAD_BEGIN(SA_OPCODE_WINDOW_STICKY);
    SA_PAYLOAD_APPEND(wid);
    SA_PAYLOAD_APPEND(sticky);
    return scripting_addition_send_payload(&builder);
}

bool scripting_addition_set_shadow(uint32_t wid, bool shadow)
{
    SA_PAYLOAD_BEGIN(SA_OPCODE_WINDOW_SHADOW);
    SA_PAYLOAD_APPEND(wid);
    SA_PAYLOAD_APPEND(shadow);
    return scripting_addition_send_payload(&builder);
}

bool scripting_addition_focus_window(uint32_t wid)
{
    SA_PAYLOAD_BEGIN(SA_OPCODE_WINDOW_FOCUS);
    SA_PAYLOAD_APPEND(wid);
    return scripting_addition_send_payload(&builder);
}

bool scripting_addition_scale_window(uint32_t wid, float x, float y, float w, float h)
{
    SA_PAYLOAD_BEGIN(SA_OPCODE_WINDOW_SCALE);
    SA_PAYLOAD_APPEND(wid);
    SA_PAYLOAD_APPEND(x);
    SA_PAYLOAD_APPEND(y);
    SA_PAYLOAD_APPEND(w);
    SA_PAYLOAD_APPEND(h);
    return scripting_addition_send_payload(&builder);
}

static bool scripting_addition_swap_window_proxy(uint8_t op, struct window_animation *animation_list, int animation_count)
{
    SA_PAYLOAD_BEGIN(op);
    if (!sa_payload_builder_append_count_max(&builder, animation_count, 1022)) return false;
    for (int i = 0; i < animation_count; ++i) {
        bool skip = __atomic_load_n(&animation_list[i].skip, __ATOMIC_RELAXED);
        if (!sa_payload_builder_append_swap_record(&builder, skip, animation_list[i].wid,
                                                   animation_list[i].proxy.id)) return false;
    }
    return scripting_addition_send_payload(&builder);
}

bool scripting_addition_swap_window_proxy_in(struct window_animation *animation_list, int animation_count)
{
    return scripting_addition_swap_window_proxy(SA_OPCODE_WINDOW_SWAP_PROXY_IN, animation_list, animation_count);
}

bool scripting_addition_swap_window_proxy_out(struct window_animation *animation_list, int animation_count)
{
    return scripting_addition_swap_window_proxy(SA_OPCODE_WINDOW_SWAP_PROXY_OUT, animation_list, animation_count);
}

bool scripting_addition_order_window(uint32_t a_wid, int order, uint32_t b_wid)
{
    SA_PAYLOAD_BEGIN(SA_OPCODE_WINDOW_ORDER);
    SA_PAYLOAD_APPEND(a_wid);
    SA_PAYLOAD_APPEND(order);
    SA_PAYLOAD_APPEND(b_wid);
    return scripting_addition_send_payload(&builder);
}

extern int g_connection;
bool scripting_addition_order_window_in(uint32_t *window_list, int window_count)
{
    uint32_t dummy_wid = 0;
    uint8_t ordered_in = 0;

    SA_PAYLOAD_BEGIN(SA_OPCODE_WINDOW_ORDER_IN);
    if (!sa_payload_builder_append_count_max(&builder, window_count, 1022)) return false;
    for (int i = 0; i < window_count; ++i) {
        SLSWindowIsOrderedIn(g_connection, window_list[i], &ordered_in);
        uint32_t wid = ordered_in ? dummy_wid : window_list[i];
        SA_PAYLOAD_APPEND(wid);
    }
    return scripting_addition_send_payload(&builder);
}

bool scripting_addition_move_window_list_to_space(uint64_t sid, uint32_t *window_list, int window_count)
{
    SA_PAYLOAD_BEGIN(SA_OPCODE_WINDOW_LIST_TO_SPACE);
    SA_PAYLOAD_APPEND(sid);
    if (!sa_payload_builder_append_count_max(&builder, window_count, 1020)) return false;
    for (int i = 0; i < window_count; ++i) {
        SA_PAYLOAD_APPEND(window_list[i]);
    }
    return scripting_addition_send_payload(&builder);
}

bool scripting_addition_move_window_to_space(uint64_t sid, uint32_t wid)
{
    SA_PAYLOAD_BEGIN(SA_OPCODE_WINDOW_TO_SPACE);
    SA_PAYLOAD_APPEND(sid);
    SA_PAYLOAD_APPEND(wid);
    return scripting_addition_send_payload(&builder);
}

#undef SA_PAYLOAD_BEGIN
#undef SA_PAYLOAD_APPEND
