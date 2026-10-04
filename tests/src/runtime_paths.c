#define _DARWIN_C_SOURCE
#ifndef TESTS
#define TESTS 1
#endif
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/acl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../src/runtime_paths.h"

static int failures;
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "runtime_paths:%d: %s\n", __LINE__, #value); ++failures; } } while (0)

static struct {
    bool fail_acl_fd;
    bool fail_acl_file;
    bool null_acl_enoent_fd;
    bool null_acl_enoent_file;
    bool malformed_acl;
    bool replace_during_fd_acl;
    bool foreign_fstat_owner;
    bool fail_fstat;
    bool fail_openat;
    bool fail_fchmod;
    const char *target_name;
    int fstatat_calls;
    int fail_fstatat_call;
    enum yabai_runtime_test_event replace_event;
    int replacement_fd;
} injection = { .replace_event = (enum yabai_runtime_test_event) -1, .replacement_fd = -1 };

static bool fd_has_suffix(int fd, const char *suffix)
{
    char path[PATH_MAX];
    return fcntl(fd, F_GETPATH, path) == 0 && strlen(path) >= strlen(suffix) &&
           strcmp(path + strlen(path) - strlen(suffix), suffix) == 0;
}

static acl_t test_acl_get_fd(int fd, acl_type_t type)
{
    const char *suffix = injection.target_name ? injection.target_name : "";
    if (injection.replace_during_fd_acl && fd_has_suffix(fd, suffix)) {
        char path[PATH_MAX], old_path[PATH_MAX];
        if (fcntl(fd, F_GETPATH, path) == 0) {
            snprintf(old_path, sizeof(old_path), "%s.old", path);
            rename(path, old_path);
            struct stat st;
            if (fstat(fd, &st) == 0) {
                if (S_ISDIR(st.st_mode)) mkdir(path, 0700);
                else { int replacement = open(path, O_CREAT | O_WRONLY, 0600); if (replacement != -1) close(replacement); }
            }
        }
        injection.replace_during_fd_acl = false;
    }
    if (injection.fail_acl_fd && fd_has_suffix(fd, suffix)) {
        errno = EIO;
        return NULL;
    }
    if (injection.null_acl_enoent_fd && fd_has_suffix(fd, suffix)) {
        errno = ENOENT;
        return NULL;
    }
    if (injection.malformed_acl && fd_has_suffix(fd, suffix)) {
        acl_t acl = acl_init(1);
        acl_entry_t entry;
        if (!acl || acl_create_entry(&acl, &entry) != 0) {
            if (acl) acl_free(acl);
            errno = EIO;
            return NULL;
        }
        return acl;
    }
    errno = 0;
    acl_t acl = acl_get_fd_np(fd, type);
    if (!acl && errno == ENOENT) acl = acl_init(0);
    return acl;
}

static acl_t test_acl_get_file(const char *path, acl_type_t type)
{
    (void) path;
    if (injection.fail_acl_file) {
        errno = EIO;
        return NULL;
    }
    if (injection.null_acl_enoent_file) {
        errno = ENOENT;
        return NULL;
    }
    errno = 0;
    acl_t acl = acl_get_file(path, type);
    if (!acl && errno == ENOENT) acl = acl_init(0);
    return acl;
}

static int test_openat(int fd, const char *name, int flags)
{
    if (injection.fail_openat && injection.target_name && strcmp(name, injection.target_name) == 0) {
        injection.fail_openat = false;
        errno = EIO;
        return -1;
    }
    return openat(fd, name, flags);
}

static int test_fchmod(int fd, mode_t mode)
{
    const char *suffix = injection.target_name ? injection.target_name : "";
    if (injection.fail_fchmod && fd_has_suffix(fd, suffix)) {
        injection.fail_fchmod = false;
        errno = EIO;
        return -1;
    }
    return fchmod(fd, mode);
}

static int test_fstat(int fd, struct stat *st)
{
    const char *suffix = injection.target_name ? injection.target_name : "";
    if (injection.fail_fstat && fd_has_suffix(fd, suffix)) {
        injection.fail_fstat = false;
        errno = EIO;
        return -1;
    }
    int result = fstat(fd, st);
    if (result == 0 && injection.foreign_fstat_owner && fd_has_suffix(fd, suffix)) {
        st->st_uid = getuid() + 1;
    }
    return result;
}

static int test_fstatat(int fd, const char *name, struct stat *st, int flags)
{
    if (injection.target_name && strcmp(name, injection.target_name) == 0 &&
        ++injection.fstatat_calls == injection.fail_fstatat_call) {
        errno = EIO;
        return -1;
    }
    return fstatat(fd, name, st, flags);
}

static bool bind_socket(const char *path, int *fd)
{
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    if (strlen(path) >= sizeof(address.sun_path)) return false;
    strcpy(address.sun_path, path);
    *fd = socket(AF_UNIX, SOCK_STREAM, 0);
    return *fd != -1 && bind(*fd, (struct sockaddr *) &address, sizeof(address)) == 0;
}

static void test_event(enum yabai_runtime_test_event event, const char *path)
{
    if (event != injection.replace_event) return;
    injection.replace_event = (enum yabai_runtime_test_event) -1;
    if (event == YABAI_RUNTIME_TEST_BEFORE_RUNTIME_ROLLBACK || event == YABAI_RUNTIME_TEST_BEFORE_YABAI_ROLLBACK) {
        rmdir(path);
        mkdir(path, 0700);
        return;
    }
    unlink(path);
    injection.replacement_fd = -1;
    if (bind_socket(path, &injection.replacement_fd)) chmod(path, 0600);
}

static struct yabai_runtime_test_hooks hooks = {
    .acl_get_fd = test_acl_get_fd,
    .acl_get_file = test_acl_get_file,
    .openat_call = test_openat,
    .fchmod_call = test_fchmod,
    .fstat_call = test_fstat,
    .fstatat_call = test_fstatat,
    .event = test_event
};

static void reset_injection(void)
{
    if (injection.replacement_fd != -1) close(injection.replacement_fd);
    memset(&injection, 0, sizeof(injection));
    injection.replace_event = (enum yabai_runtime_test_event) -1;
    injection.replacement_fd = -1;
    yabai_runtime_set_test_hooks(&hooks);
}

static int remove_entry(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
    (void) st; (void) type; (void) ftw;
    return remove(path);
}

static bool make_dir(const char *path, mode_t mode)
{
    return mkdir(path, mode) == 0 && chmod(path, mode) == 0;
}

static bool make_fixture(char root[PATH_MAX], char home[PATH_MAX])
{
    snprintf(root, PATH_MAX, "/private/tmp/yabai-runtime-tests.XXXXXX");
    if (!mkdtemp(root) || chmod(root, 0700) != 0) return false;
    snprintf(home, PATH_MAX, "%s/home", root);
    char library[PATH_MAX], caches[PATH_MAX];
    snprintf(library, sizeof(library), "%s/Library", home);
    snprintf(caches, sizeof(caches), "%s/Caches", library);
    return make_dir(home, 0700) && make_dir(library, 0700) && make_dir(caches, 0700);
}

static bool set_acl(const char *path, const char *entry)
{
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/chmod", "chmod", "+a", entry, path, (char *) NULL);
        _exit(127);
    }
    int status;
    return pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void cleanup_fixture(const char *root)
{
    reset_injection();
    nftw(root, remove_entry, 32, FTW_DEPTH | FTW_PHYS);
}

static void test_uid_and_pure_derivation(void)
{
    uid_t parsed = 0;
    CHECK(!yabai_runtime_parse_uid(NULL, &parsed));
    CHECK(!yabai_runtime_parse_uid("", &parsed));
    CHECK(!yabai_runtime_parse_uid("0", &parsed));
    CHECK(!yabai_runtime_parse_uid("-1", &parsed));
    CHECK(!yabai_runtime_parse_uid("12x", &parsed));
    CHECK(!yabai_runtime_parse_uid("999999999999999999999999999", &parsed));
    CHECK(yabai_runtime_parse_uid("501", &parsed) && parsed == 501);

    char root[PATH_MAX], home[PATH_MAX], yabai[PATH_MAX];
    CHECK(make_fixture(root, home));
    struct yabai_runtime_paths paths;
    CHECK(yabai_runtime_derive_for_uid(getuid(), home, &paths));
    snprintf(yabai, sizeof(yabai), "%s/Library/Caches/yabai", home);
    CHECK(access(yabai, F_OK) == -1 && errno == ENOENT);
    CHECK(strstr(paths.directory, "/Library/Caches/yabai/runtime") != NULL);
    CHECK(strstr(paths.daemon_socket, "daemon.socket") != NULL);
    CHECK(strstr(paths.sa_socket, "sa.socket") != NULL);
    CHECK(strstr(paths.daemon_lock, "daemon.lock") != NULL);
    CHECK(strstr(paths.sa_lock, "sa.lock") != NULL);
    cleanup_fixture(root);
}

static void check_no_leaf(const char *home)
{
    char leaf[PATH_MAX];
    snprintf(leaf, sizeof(leaf), "%s/Library/Caches/yabai", home);
    CHECK(access(leaf, F_OK) == -1 && errno == ENOENT);
}

static void test_production_acl_adapter_accepts_real_acl_free_objects(void)
{
    char root[PATH_MAX], home[PATH_MAX];
    CHECK(make_fixture(root, home));
    yabai_runtime_set_test_hooks(NULL);
    struct yabai_runtime_paths paths;
    CHECK(yabai_runtime_prepare_for_uid(getuid(), home, true, &paths));
    int lock = yabai_runtime_open_lock(paths.daemon_lock, getuid());
    CHECK(lock != -1); if (lock != -1) close(lock);
    int socket_fd = -1;
    CHECK(bind_socket(paths.daemon_socket, &socket_fd));
    CHECK(chmod(paths.daemon_socket, 0600) == 0);
    struct yabai_bound_entry bound = {0};
    CHECK(yabai_runtime_capture_bound_socket(paths.daemon_socket, getuid(), &bound));
    CHECK(yabai_runtime_validate_bound_socket(paths.daemon_socket, &bound));
    CHECK(yabai_runtime_remove_stale_socket(paths.daemon_socket, getuid()));
    if (socket_fd != -1) close(socket_fd);
    yabai_runtime_set_test_hooks(&hooks);
    cleanup_fixture(root);
}

static void test_pure_derivation_rejects_unsafe_ancestors(void)
{
    for (size_t i = 0; i < 2; ++i) {
        char root[PATH_MAX], home[PATH_MAX], path[PATH_MAX], moved[PATH_MAX];
        struct yabai_runtime_paths paths;

        CHECK(make_fixture(root, home));
        snprintf(path, sizeof(path), i ? "%s/Library/Caches" : "%s/Library", home);
        CHECK(chmod(path, 0770) == 0);
        CHECK(!yabai_runtime_derive_for_uid(getuid(), home, &paths));
        check_no_leaf(home);
        cleanup_fixture(root);

        CHECK(make_fixture(root, home));
        snprintf(path, sizeof(path), i ? "%s/Library/Caches" : "%s/Library", home);
        snprintf(moved, sizeof(moved), "%s.real", path);
        CHECK(rename(path, moved) == 0 && symlink(moved, path) == 0);
        CHECK(!yabai_runtime_derive_for_uid(getuid(), home, &paths));
        check_no_leaf(home);
        cleanup_fixture(root);

        CHECK(make_fixture(root, home));
        snprintf(path, sizeof(path), i ? "%s/Library/Caches" : "%s/Library", home);
        struct passwd *account = getpwuid(getuid());
        char allow[256];
        snprintf(allow, sizeof(allow), "user:%s allow add_file,delete_child", account ? account->pw_name : "nobody");
        CHECK(set_acl(path, allow));
        CHECK(!yabai_runtime_derive_for_uid(getuid(), home, &paths));
        check_no_leaf(home);
        cleanup_fixture(root);

        CHECK(make_fixture(root, home));
        reset_injection();
        injection.foreign_fstat_owner = true;
        injection.target_name = i ? "/Caches" : "/Library";
        CHECK(!yabai_runtime_derive_for_uid(getuid(), home, &paths));
        check_no_leaf(home);
        cleanup_fixture(root);

        CHECK(make_fixture(root, home));
        reset_injection();
        injection.fail_acl_fd = true;
        injection.target_name = i ? "/Caches" : "/Library";
        CHECK(!yabai_runtime_derive_for_uid(getuid(), home, &paths));
        check_no_leaf(home);
        cleanup_fixture(root);

        CHECK(make_fixture(root, home));
        reset_injection();
        injection.null_acl_enoent_fd = true;
        injection.target_name = i ? "/Caches" : "/Library";
        CHECK(!yabai_runtime_derive_for_uid(getuid(), home, &paths));
        check_no_leaf(home);
        cleanup_fixture(root);
    }
}

static void test_secure_creation_and_agreement(void)
{
    char root[PATH_MAX], home[PATH_MAX];
    CHECK(make_fixture(root, home));
    struct yabai_runtime_paths daemon, client, payload, controller;
    uid_t uid = getuid();
    CHECK(yabai_runtime_prepare_for_uid(uid, home, true, &daemon));
    CHECK(yabai_runtime_prepare_for_uid(uid, home, false, &client));
    CHECK(yabai_runtime_prepare_for_uid(uid, home, false, &payload));
    CHECK(yabai_runtime_derive_for_uid(uid, home, &controller));
    CHECK(strcmp(daemon.daemon_socket, client.daemon_socket) == 0);
    CHECK(strcmp(payload.sa_socket, controller.sa_socket) == 0);
    struct stat st;
    CHECK(lstat(daemon.directory, &st) == 0 && (st.st_mode & 0777) == 0700 && st.st_uid == uid);

    char legacy[PATH_MAX], legacy_dir[PATH_MAX];
    snprintf(legacy_dir, sizeof(legacy_dir), "%s/tmp", root);
    snprintf(legacy, sizeof(legacy), "%s/yabai_foreign.socket", legacy_dir);
    CHECK(make_dir(legacy_dir, 0700));
    int fd = open(legacy, O_CREAT | O_WRONLY, 0600);
    CHECK(fd != -1); if (fd != -1) close(fd);
    CHECK(strcmp(legacy, daemon.daemon_socket) != 0);
    cleanup_fixture(root);
}

static void test_creation_rollback(void)
{
    enum captured_failure { CAPTURED_FCHMOD, CAPTURED_FSTATAT, CAPTURED_ACL };
    for (int leaf = 0; leaf < 2; ++leaf) {
        for (int failure = CAPTURED_FCHMOD; failure <= CAPTURED_ACL; ++failure) {
            char root[PATH_MAX], home[PATH_MAX], yabai[PATH_MAX], target[PATH_MAX];
            CHECK(make_fixture(root, home));
            snprintf(yabai, sizeof(yabai), "%s/Library/Caches/yabai", home);
            if (leaf) CHECK(make_dir(yabai, 0700));
            snprintf(target, sizeof(target), "%s%s", yabai, leaf ? "/runtime" : "");
            struct yabai_runtime_paths paths;
            CHECK(yabai_runtime_derive_for_uid(getuid(), home, &paths));
            reset_injection();
            injection.target_name = leaf ? "runtime" : "yabai";
            if (failure == CAPTURED_FCHMOD) injection.fail_fchmod = true;
            if (failure == CAPTURED_FSTATAT) injection.fail_fstatat_call = 2;
            if (failure == CAPTURED_ACL) {
                injection.fail_acl_fd = true;
                injection.target_name = leaf ? "/runtime" : "/yabai";
            }
            CHECK(!yabai_runtime_prepare_directories(&paths, true));
            CHECK(access(target, F_OK) == -1 && errno == ENOENT);
            if (leaf) CHECK(access(yabai, F_OK) == 0);
            cleanup_fixture(root);
        }
    }

    /* If immediate open/fstat cannot establish identity, retain the unknown
     * name rather than risk deleting a replacement. */
    for (int leaf = 0; leaf < 2; ++leaf) {
        for (int failure = 0; failure < 2; ++failure) {
            char root[PATH_MAX], home[PATH_MAX], yabai[PATH_MAX], target[PATH_MAX];
            CHECK(make_fixture(root, home));
            snprintf(yabai, sizeof(yabai), "%s/Library/Caches/yabai", home);
            if (leaf) CHECK(make_dir(yabai, 0700));
            snprintf(target, sizeof(target), "%s%s", yabai, leaf ? "/runtime" : "");
            struct yabai_runtime_paths paths;
            CHECK(yabai_runtime_derive_for_uid(getuid(), home, &paths));
            reset_injection();
            injection.target_name = leaf ? "runtime" : "yabai";
            if (failure == 0) injection.fail_openat = true;
            else { injection.fail_fstat = true; injection.target_name = leaf ? "/runtime" : "/yabai"; }
            CHECK(!yabai_runtime_prepare_directories(&paths, true));
            CHECK(access(target, F_OK) == 0);
            cleanup_fixture(root);
        }
    }

    char root[PATH_MAX], home[PATH_MAX], yabai[PATH_MAX];
    CHECK(make_fixture(root, home));
    snprintf(yabai, sizeof(yabai), "%s/Library/Caches/yabai", home);
    CHECK(make_dir(yabai, 0700));
    struct yabai_runtime_paths paths;
    CHECK(yabai_runtime_derive_for_uid(getuid(), home, &paths));
    reset_injection();
    injection.target_name = "runtime";
    injection.fail_fchmod = true;
    injection.replace_event = YABAI_RUNTIME_TEST_BEFORE_RUNTIME_ROLLBACK;
    CHECK(!yabai_runtime_prepare_directories(&paths, true));
    CHECK(lstat(paths.directory, &(struct stat){0}) == 0);
    CHECK(access(yabai, F_OK) == 0);
    cleanup_fixture(root);
}

static void test_descriptor_acl_and_owner_validation(void)
{
    char root[PATH_MAX], home[PATH_MAX], caches[PATH_MAX];
    struct yabai_runtime_paths paths;
    CHECK(make_fixture(root, home));
    snprintf(caches, sizeof(caches), "%s/Library/Caches", home);
    CHECK(set_acl(caches, "group:everyone deny delete"));
    CHECK(yabai_runtime_prepare_for_uid(getuid(), home, true, &paths));
    struct passwd *account = getpwuid(getuid());
    char allow[256];
    snprintf(allow, sizeof(allow), "user:%s allow add_file,delete_child", account ? account->pw_name : "nobody");
    CHECK(set_acl(paths.directory, allow));
    reset_injection();
    injection.replace_during_fd_acl = true; injection.target_name = "/runtime";
    CHECK(!yabai_runtime_prepare_directories(&paths, false));
    CHECK(access(paths.directory, F_OK) == 0);
    cleanup_fixture(root);

    CHECK(make_fixture(root, home));
    CHECK(yabai_runtime_derive_for_uid(getuid(), home, &paths));
    reset_injection();
    injection.fail_acl_fd = true; injection.target_name = "/Library";
    CHECK(!yabai_runtime_prepare_directories(&paths, true));
    reset_injection();
    injection.malformed_acl = true; injection.target_name = "/Library";
    CHECK(!yabai_runtime_prepare_directories(&paths, true));
    reset_injection();
    injection.foreign_fstat_owner = true; injection.target_name = "/Library";
    CHECK(!yabai_runtime_prepare_directories(&paths, true));
    cleanup_fixture(root);
}

static void test_socket_identity_and_acl_races(void)
{
    char root[PATH_MAX], home[PATH_MAX];
    struct yabai_runtime_paths paths;
    CHECK(make_fixture(root, home));
    CHECK(yabai_runtime_prepare_for_uid(getuid(), home, true, &paths));
    int fd = -1;
    CHECK(bind_socket(paths.daemon_socket, &fd));
    CHECK(chmod(paths.daemon_socket, 0600) == 0);
    reset_injection();
    injection.replace_event = YABAI_RUNTIME_TEST_AFTER_STALE_VALIDATE;
    CHECK(!yabai_runtime_remove_stale_socket(paths.daemon_socket, getuid()));
    struct stat st;
    CHECK(lstat(paths.daemon_socket, &st) == 0 && S_ISSOCK(st.st_mode));
    close(fd); fd = -1;
    unlink(paths.daemon_socket);
    reset_injection();

    CHECK(bind_socket(paths.daemon_socket, &fd));
    struct yabai_bound_entry bound = {0};
    injection.replace_event = YABAI_RUNTIME_TEST_AFTER_BIND_CAPTURE;
    CHECK(!yabai_runtime_capture_bound_socket(paths.daemon_socket, getuid(), &bound));
    CHECK(!bound.valid);
    close(fd); fd = -1;
    unlink(paths.daemon_socket);
    reset_injection();

    CHECK(bind_socket(paths.daemon_socket, &fd));
    CHECK(chmod(paths.daemon_socket, 0600) == 0);
    CHECK(yabai_runtime_capture_bound_socket(paths.daemon_socket, getuid(), &bound));
    injection.replace_event = YABAI_RUNTIME_TEST_BEFORE_SOCKET_ACL;
    CHECK(!yabai_runtime_validate_bound_socket(paths.daemon_socket, &bound));
    CHECK(lstat(paths.daemon_socket, &st) == 0 && S_ISSOCK(st.st_mode));
    close(fd); fd = -1;
    unlink(paths.daemon_socket);
    reset_injection();

    CHECK(bind_socket(paths.daemon_socket, &fd));
    CHECK(chmod(paths.daemon_socket, 0600) == 0);
    CHECK(yabai_runtime_capture_bound_socket(paths.daemon_socket, getuid(), &bound));
    injection.fail_acl_file = true;
    CHECK(!yabai_runtime_validate_bound_socket(paths.daemon_socket, &bound));
    close(fd); unlink(paths.daemon_socket);
    reset_injection();

    CHECK(bind_socket(paths.daemon_socket, &fd));
    CHECK(chmod(paths.daemon_socket, 0600) == 0);
    injection.null_acl_enoent_file = true;
    CHECK(!yabai_runtime_remove_stale_socket(paths.daemon_socket, getuid()));
    CHECK(lstat(paths.daemon_socket, &st) == 0);
    reset_injection();
    CHECK(yabai_runtime_capture_bound_socket(paths.daemon_socket, getuid(), &bound));
    injection.null_acl_enoent_file = true;
    CHECK(!yabai_runtime_validate_bound_socket(paths.daemon_socket, &bound));
    close(fd); unlink(paths.daemon_socket);
    cleanup_fixture(root);
}

static void test_socket_entry_policy_and_cleanup(void)
{
    char root[PATH_MAX], home[PATH_MAX];
    struct yabai_runtime_paths paths;
    CHECK(make_fixture(root, home));
    CHECK(yabai_runtime_prepare_for_uid(getuid(), home, true, &paths));
    int file = open(paths.daemon_socket, O_CREAT | O_WRONLY, 0600);
    CHECK(file != -1); if (file != -1) close(file);
    CHECK(!yabai_runtime_remove_stale_socket(paths.daemon_socket, getuid()));
    unlink(paths.daemon_socket);
    CHECK(symlink("missing", paths.daemon_socket) == 0);
    CHECK(!yabai_runtime_remove_stale_socket(paths.daemon_socket, getuid()));
    unlink(paths.daemon_socket);

    int fd = -1;
    CHECK(bind_socket(paths.daemon_socket, &fd));
    CHECK(chmod(paths.daemon_socket, 0600) == 0);
    CHECK(!yabai_runtime_remove_stale_socket(paths.daemon_socket, getuid() + 1));
    CHECK(yabai_runtime_remove_stale_socket(paths.daemon_socket, getuid()));
    close(fd);

    CHECK(bind_socket(paths.daemon_socket, &fd));
    CHECK(chmod(paths.daemon_socket, 0600) == 0);
    struct yabai_bound_entry bound = {0};
    CHECK(yabai_runtime_capture_bound_socket(paths.daemon_socket, getuid(), &bound));
    CHECK(unlink(paths.daemon_socket) == 0);
    close(fd);
    int replacement = -1;
    CHECK(bind_socket(paths.daemon_socket, &replacement));
    CHECK(chmod(paths.daemon_socket, 0600) == 0);
    yabai_runtime_cleanup_bound_socket(paths.daemon_socket, &bound);
    struct stat st;
    CHECK(lstat(paths.daemon_socket, &st) == 0 && S_ISSOCK(st.st_mode));
    close(replacement);
    unlink(paths.daemon_socket);
    cleanup_fixture(root);
}

static bool child_cannot_acquire(const char *path, uid_t uid)
{
    pid_t pid = fork();
    if (pid == 0) {
        int fd = yabai_runtime_acquire_lock(path, uid);
        if (fd != -1) close(fd);
        _exit(fd == -1 ? 0 : 1);
    }
    int status;
    return pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void test_lock_policy_and_lifecycle(void)
{
    char root[PATH_MAX], home[PATH_MAX];
    struct yabai_runtime_paths paths;
    CHECK(make_fixture(root, home));
    CHECK(yabai_runtime_prepare_for_uid(getuid(), home, true, &paths));
    CHECK(symlink("missing", paths.daemon_lock) == 0);
    CHECK(yabai_runtime_open_lock(paths.daemon_lock, getuid()) == -1);
    unlink(paths.daemon_lock);
    CHECK(mkdir(paths.daemon_lock, 0700) == 0);
    CHECK(yabai_runtime_open_lock(paths.daemon_lock, getuid()) == -1);
    rmdir(paths.daemon_lock);
    int hard = open(paths.daemon_lock, O_CREAT | O_WRONLY, 0600);
    CHECK(hard != -1); if (hard != -1) close(hard);
    char other[PATH_MAX]; snprintf(other, sizeof(other), "%s/hardlink", paths.directory);
    CHECK(link(paths.daemon_lock, other) == 0);
    CHECK(yabai_runtime_open_lock(paths.daemon_lock, getuid()) == -1);
    unlink(other); unlink(paths.daemon_lock);
    const mode_t rejected[] = { 0644, 0640, 0400 };
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        int fd = open(paths.daemon_lock, O_CREAT | O_WRONLY | O_TRUNC, 0600);
        CHECK(fd != -1); if (fd != -1) close(fd);
        CHECK(chmod(paths.daemon_lock, rejected[i]) == 0);
        CHECK(yabai_runtime_open_lock(paths.daemon_lock, getuid()) == -1);
        unlink(paths.daemon_lock);
    }
    int fd = yabai_runtime_open_lock(paths.daemon_lock, getuid());
    CHECK(fd != -1); if (fd != -1) close(fd);
    int daemon_lock = yabai_runtime_acquire_lock(paths.daemon_lock, getuid());
    CHECK(daemon_lock != -1 && child_cannot_acquire(paths.daemon_lock, getuid()));
    int sa_lock = yabai_runtime_acquire_lock(paths.sa_lock, getuid());
    CHECK(sa_lock != -1 && child_cannot_acquire(paths.sa_lock, getuid()));
    if (daemon_lock != -1) close(daemon_lock);
    if (sa_lock != -1) close(sa_lock);

    struct passwd *account = getpwuid(getuid());
    char allow[256];
    snprintf(allow, sizeof(allow), "user:%s allow write", account ? account->pw_name : "nobody");
    CHECK(set_acl(paths.daemon_lock, allow));
    reset_injection();
    injection.replace_during_fd_acl = true; injection.target_name = "daemon.lock";
    CHECK(yabai_runtime_open_lock(paths.daemon_lock, getuid()) == -1);
    CHECK(access(paths.daemon_lock, F_OK) == 0);

    reset_injection();
    injection.fail_acl_fd = true; injection.target_name = "daemon.lock";
    CHECK(yabai_runtime_open_lock(paths.daemon_lock, getuid()) == -1);
    reset_injection();
    injection.null_acl_enoent_fd = true; injection.target_name = "daemon.lock";
    CHECK(yabai_runtime_open_lock(paths.daemon_lock, getuid()) == -1);
    cleanup_fixture(root);
}

static bool file_contains_all(const char *path, const char *first, const char *second, const char *third)
{
    FILE *file = fopen(path, "r");
    if (!file) return false;
    char *content = NULL; size_t size = 0; bool a = false, b = false, c = false;
    while (getline(&content, &size, file) != -1) {
        if (strstr(content, first)) a = true;
        if (strstr(content, second)) b = true;
        if (strstr(content, third)) c = true;
    }
    free(content);
    fclose(file);
    return a && b && c;
}

static void test_symlinks_modes_bounds_and_docs(void)
{
    char root[PATH_MAX], home[PATH_MAX], path[PATH_MAX];
    struct yabai_runtime_paths paths;
    CHECK(make_fixture(root, home));
    snprintf(path, sizeof(path), "%s/Library", home);
    CHECK(chmod(path, 0770) == 0);
    CHECK(!yabai_runtime_prepare_for_uid(getuid(), home, true, &paths));
    cleanup_fixture(root);

    CHECK(make_fixture(root, home));
    snprintf(path, sizeof(path), "%s/Library/Caches/yabai", home);
    CHECK(symlink(root, path) == 0);
    CHECK(!yabai_runtime_prepare_for_uid(getuid(), home, true, &paths));
    cleanup_fixture(root);

    CHECK(make_fixture(root, home));
    snprintf(path, sizeof(path), "%s/Library", home);
    char moved[PATH_MAX]; snprintf(moved, sizeof(moved), "%s.real", path);
    CHECK(rename(path, moved) == 0 && symlink(moved, path) == 0);
    CHECK(!yabai_runtime_prepare_for_uid(getuid(), home, true, &paths));
    cleanup_fixture(root);

    CHECK(make_fixture(root, home));
    snprintf(path, sizeof(path), "%s/Library/Caches", home);
    snprintf(moved, sizeof(moved), "%s.real", path);
    CHECK(rename(path, moved) == 0 && symlink(moved, path) == 0);
    CHECK(!yabai_runtime_prepare_for_uid(getuid(), home, true, &paths));
    cleanup_fixture(root);

    CHECK(make_fixture(root, home));
    snprintf(moved, sizeof(moved), "%s/home.real", root);
    CHECK(rename(home, moved) == 0 && symlink(moved, home) == 0);
    CHECK(!yabai_runtime_derive_for_uid(getuid(), home, &paths));
    cleanup_fixture(root);

    CHECK(make_fixture(root, home));
    snprintf(path, sizeof(path), "%s/Library/Caches/yabai", home);
    CHECK(make_dir(path, 0700));
    char runtime[PATH_MAX]; snprintf(runtime, sizeof(runtime), "%s/runtime", path);
    CHECK(symlink(root, runtime) == 0);
    CHECK(!yabai_runtime_prepare_for_uid(getuid(), home, true, &paths));
    cleanup_fixture(root);

    CHECK(make_fixture(root, home));
    snprintf(path, sizeof(path), "%s/Library/Caches/yabai", home);
    CHECK(make_dir(path, 0700));
    snprintf(runtime, sizeof(runtime), "%s/runtime", path);
    CHECK(make_dir(runtime, 0755));
    CHECK(!yabai_runtime_prepare_for_uid(getuid(), home, false, &paths));
    cleanup_fixture(root);

    char base[PATH_MAX]; memset(base, 'a', sizeof(base)); base[0] = '/'; base[PATH_MAX - 1] = '\0';
    CHECK(!yabai_runtime_join(path, base, "runtime"));

    CHECK(file_contains_all("../doc/yabai.asciidoc", "Library/Caches/yabai/runtime", "same UID", "outside this isolation boundary"));
    CHECK(file_contains_all("../doc/yabai.1", "Library/Caches/yabai/runtime", "same UID", "outside this isolation boundary"));
}

int main(void)
{
    reset_injection();
    test_uid_and_pure_derivation();
    test_production_acl_adapter_accepts_real_acl_free_objects();
    test_pure_derivation_rejects_unsafe_ancestors();
    test_secure_creation_and_agreement();
    test_creation_rollback();
    test_descriptor_acl_and_owner_validation();
    test_socket_identity_and_acl_races();
    test_socket_entry_policy_and_cleanup();
    test_lock_policy_and_lifecycle();
    test_symlinks_modes_bounds_and_docs();
    reset_injection();
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
