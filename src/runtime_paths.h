#ifndef YABAI_RUNTIME_PATHS_H
#define YABAI_RUNTIME_PATHS_H

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/acl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef O_NOFOLLOW
#error "secure runtime paths require O_NOFOLLOW"
#endif

#ifndef YABAI_RUNTIME_GETEUID
#define YABAI_RUNTIME_GETEUID() geteuid()
#endif

#define YABAI_RUNTIME_DAEMON_SOCKET "daemon.socket"
#define YABAI_RUNTIME_SA_SOCKET     "sa.socket"
#define YABAI_RUNTIME_DAEMON_LOCK   "daemon.lock"
#define YABAI_RUNTIME_SA_LOCK       "sa.lock"

struct yabai_runtime_paths {
    uid_t uid;
    char home[PATH_MAX];
    char directory[PATH_MAX];
    char daemon_socket[PATH_MAX];
    char sa_socket[PATH_MAX];
    char daemon_lock[PATH_MAX];
    char sa_lock[PATH_MAX];
};

struct yabai_bound_entry {
    dev_t device;
    ino_t inode;
    uid_t uid;
    bool valid;
};

enum yabai_runtime_test_event {
    YABAI_RUNTIME_TEST_AFTER_STALE_VALIDATE,
    YABAI_RUNTIME_TEST_AFTER_BIND_CAPTURE,
    YABAI_RUNTIME_TEST_BEFORE_SOCKET_ACL,
    YABAI_RUNTIME_TEST_BEFORE_RUNTIME_ROLLBACK,
    YABAI_RUNTIME_TEST_BEFORE_YABAI_ROLLBACK
};

#ifdef TESTS
struct yabai_runtime_test_hooks {
    acl_t (*acl_get_fd)(int, acl_type_t);
    acl_t (*acl_get_file)(const char *, acl_type_t);
    int (*openat_call)(int, const char *, int);
    int (*fchmod_call)(int, mode_t);
    int (*fstat_call)(int, struct stat *);
    int (*fstatat_call)(int, const char *, struct stat *, int);
    void (*event)(enum yabai_runtime_test_event, const char *);
};
static struct yabai_runtime_test_hooks *yabai_runtime_test_hooks;
static inline void yabai_runtime_set_test_hooks(struct yabai_runtime_test_hooks *hooks)
{
    yabai_runtime_test_hooks = hooks;
}
#endif

static inline acl_t yabai_runtime_acl_get_fd(int fd)
{
#ifdef TESTS
    if (yabai_runtime_test_hooks && yabai_runtime_test_hooks->acl_get_fd) {
        return yabai_runtime_test_hooks->acl_get_fd(fd, ACL_TYPE_EXTENDED);
    }
#endif
    errno = 0;
    acl_t acl = acl_get_fd_np(fd, ACL_TYPE_EXTENDED);
    /* Darwin reports an ACL-free object as ENOENT. Normalize that platform
     * result at the adapter boundary; validators still reject every NULL. */
    if (!acl && errno == ENOENT) acl = acl_init(0);
    return acl;
}

static inline acl_t yabai_runtime_acl_get_file(const char *path)
{
#ifdef TESTS
    if (yabai_runtime_test_hooks && yabai_runtime_test_hooks->acl_get_file) {
        return yabai_runtime_test_hooks->acl_get_file(path, ACL_TYPE_EXTENDED);
    }
#endif
    errno = 0;
    acl_t acl = acl_get_file(path, ACL_TYPE_EXTENDED);
    if (!acl && errno == ENOENT) acl = acl_init(0);
    return acl;
}

static inline int yabai_runtime_openat(int fd, const char *name, int flags)
{
#ifdef TESTS
    if (yabai_runtime_test_hooks && yabai_runtime_test_hooks->openat_call) {
        return yabai_runtime_test_hooks->openat_call(fd, name, flags);
    }
#endif
    return openat(fd, name, flags);
}

static inline int yabai_runtime_fchmod(int fd, mode_t mode)
{
#ifdef TESTS
    if (yabai_runtime_test_hooks && yabai_runtime_test_hooks->fchmod_call) {
        return yabai_runtime_test_hooks->fchmod_call(fd, mode);
    }
#endif
    return fchmod(fd, mode);
}

static inline int yabai_runtime_fstat(int fd, struct stat *st)
{
#ifdef TESTS
    if (yabai_runtime_test_hooks && yabai_runtime_test_hooks->fstat_call) {
        return yabai_runtime_test_hooks->fstat_call(fd, st);
    }
#endif
    return fstat(fd, st);
}

static inline int yabai_runtime_fstatat(int fd, const char *name, struct stat *st, int flags)
{
#ifdef TESTS
    if (yabai_runtime_test_hooks && yabai_runtime_test_hooks->fstatat_call) {
        return yabai_runtime_test_hooks->fstatat_call(fd, name, st, flags);
    }
#endif
    return fstatat(fd, name, st, flags);
}

static inline void yabai_runtime_test_event(enum yabai_runtime_test_event event, const char *path)
{
#ifdef TESTS
    if (yabai_runtime_test_hooks && yabai_runtime_test_hooks->event) yabai_runtime_test_hooks->event(event, path);
#else
    (void) event; (void) path;
#endif
}

static inline bool yabai_runtime_parse_uid(const char *text, uid_t *uid)
{
    if (!text || !*text || !uid) return false;
    uintmax_t value = 0;
    const uintmax_t maximum = (uintmax_t) ((uid_t) -1);
    for (const unsigned char *cursor = (const unsigned char *) text; *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return false;
        unsigned digit = *cursor - '0';
        if (value > (maximum - digit) / 10) return false;
        value = value * 10 + digit;
    }
    if (value == 0) return false;
    *uid = (uid_t) value;
    return true;
}

static inline bool yabai_runtime_acl_is_deny_only(acl_t acl)
{
    acl_entry_id_t entry_id = ACL_FIRST_ENTRY;
    for (;;) {
        acl_entry_t entry;
        errno = 0;
        if (acl_get_entry(acl, entry_id, &entry) != 0) return errno == EINVAL;
        acl_tag_t tag;
        if (acl_get_tag_type(entry, &tag) != 0 || tag != ACL_EXTENDED_DENY) return false;
        entry_id = ACL_NEXT_ENTRY;
    }
}

static inline bool yabai_runtime_safe_fd_acl(int fd)
{
    errno = 0;
    acl_t acl = yabai_runtime_acl_get_fd(fd);
    if (!acl) return false;
    bool safe = yabai_runtime_acl_is_deny_only(acl);
    acl_free(acl);
    return safe;
}

static inline bool yabai_runtime_same_identity(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

static inline bool yabai_runtime_safe_socket_acl(const char *path, const struct stat *expected)
{
    struct stat before, after;
    if (lstat(path, &before) != 0 || !yabai_runtime_same_identity(&before, expected)) return false;
    yabai_runtime_test_event(YABAI_RUNTIME_TEST_BEFORE_SOCKET_ACL, path);
    errno = 0;
    acl_t acl = yabai_runtime_acl_get_file(path);
    bool safe = acl && yabai_runtime_acl_is_deny_only(acl);
    if (acl) acl_free(acl);
    if (lstat(path, &after) != 0 || !yabai_runtime_same_identity(&before, &after)) return false;
    return safe;
}

static inline bool yabai_runtime_safe_directory_stat(const struct stat *st, uid_t uid, mode_t exact_mode)
{
    return S_ISDIR(st->st_mode) && st->st_uid == uid && (st->st_mode & 022) == 0 &&
           (!exact_mode || (st->st_mode & 0777) == exact_mode);
}

static inline bool yabai_runtime_validate_directory_path(const char *path, uid_t uid, mode_t exact_mode)
{
    struct stat before, after;
    if (lstat(path, &before) != 0 || !yabai_runtime_safe_directory_stat(&before, uid, exact_mode)) return false;
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd == -1) return false;
    bool valid = yabai_runtime_fstat(fd, &after) == 0 && yabai_runtime_same_identity(&before, &after) &&
                 yabai_runtime_safe_directory_stat(&after, uid, exact_mode) && yabai_runtime_safe_fd_acl(fd);
    close(fd);
    return valid;
}

static inline bool yabai_runtime_home_for_uid(uid_t uid, const char *override, char home[PATH_MAX])
{
    const char *source = override;
    char buffer[16384];
    struct passwd pwd, *result = NULL;
    if (!source) {
        if (getpwuid_r(uid, &pwd, buffer, sizeof(buffer), &result) != 0 || !result || !result->pw_dir) return false;
        source = result->pw_dir;
    }
    if (source[0] != '/') return false;
    size_t length = strlen(source);
    if (!length || length >= PATH_MAX) return false;
    char canonical[PATH_MAX];
    if (!realpath(source, canonical) || strcmp(source, canonical) != 0) return false;
    memcpy(home, source, length + 1);
    return yabai_runtime_validate_directory_path(home, uid, 0);
}

static inline bool yabai_runtime_join(char output[PATH_MAX], const char *base, const char *leaf)
{
    int count = snprintf(output, PATH_MAX, "%s/%s", base, leaf);
    return count > 0 && count < PATH_MAX;
}

static inline bool yabai_runtime_open_existing_ancestor(int parent_fd, const char *name, uid_t uid, int *child_fd)
{
    struct stat path_stat, descriptor_stat;
    if (yabai_runtime_fstatat(parent_fd, name, &path_stat, AT_SYMLINK_NOFOLLOW) != 0 ||
        !yabai_runtime_safe_directory_stat(&path_stat, uid, 0)) return false;
    int fd = yabai_runtime_openat(parent_fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd == -1) return false;
    bool valid = yabai_runtime_fstat(fd, &descriptor_stat) == 0 &&
                 yabai_runtime_same_identity(&path_stat, &descriptor_stat) &&
                 yabai_runtime_safe_directory_stat(&descriptor_stat, uid, 0) &&
                 yabai_runtime_safe_fd_acl(fd);
    if (!valid) {
        close(fd);
        return false;
    }
    *child_fd = fd;
    return true;
}

static inline bool yabai_runtime_validate_existing_ancestors(const char *home, uid_t uid)
{
    int home_fd = open(home, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (home_fd == -1) return false;
    struct stat home_path, home_descriptor;
    bool valid_home = lstat(home, &home_path) == 0 && yabai_runtime_fstat(home_fd, &home_descriptor) == 0 &&
                      yabai_runtime_same_identity(&home_path, &home_descriptor) &&
                      yabai_runtime_safe_directory_stat(&home_descriptor, uid, 0) &&
                      yabai_runtime_safe_fd_acl(home_fd);
    int library_fd = -1, caches_fd = -1;
    bool valid = valid_home && yabai_runtime_open_existing_ancestor(home_fd, "Library", uid, &library_fd) &&
                 yabai_runtime_open_existing_ancestor(library_fd, "Caches", uid, &caches_fd);
    if (caches_fd != -1) close(caches_fd);
    if (library_fd != -1) close(library_fd);
    close(home_fd);
    return valid;
}

static inline bool yabai_runtime_derive_for_uid(uid_t uid, const char *home_override, struct yabai_runtime_paths *paths)
{
    if (!paths || uid == 0) return false;
    memset(paths, 0, sizeof(*paths));
    if (!yabai_runtime_home_for_uid(uid, home_override, paths->home) ||
        !yabai_runtime_validate_existing_ancestors(paths->home, uid) ||
        !yabai_runtime_join(paths->directory, paths->home, "Library/Caches/yabai/runtime") ||
        !yabai_runtime_join(paths->daemon_socket, paths->directory, YABAI_RUNTIME_DAEMON_SOCKET) ||
        !yabai_runtime_join(paths->sa_socket, paths->directory, YABAI_RUNTIME_SA_SOCKET) ||
        !yabai_runtime_join(paths->daemon_lock, paths->directory, YABAI_RUNTIME_DAEMON_LOCK) ||
        !yabai_runtime_join(paths->sa_lock, paths->directory, YABAI_RUNTIME_SA_LOCK)) return false;
    paths->uid = uid;
    return strlen(paths->daemon_socket) < sizeof(((struct sockaddr_un *) 0)->sun_path) &&
           strlen(paths->sa_socket) < sizeof(((struct sockaddr_un *) 0)->sun_path);
}

enum yabai_runtime_directory_status {
    YABAI_RUNTIME_DIRECTORY_VALID,
    YABAI_RUNTIME_DIRECTORY_ABSENT,
    YABAI_RUNTIME_DIRECTORY_UNSAFE
};

static inline enum yabai_runtime_directory_status
    yabai_runtime_open_readonly_component(int parent_fd, const char *name, uid_t uid,
                                          mode_t exact_mode, bool absence_is_safe,
                                          int *child_fd)
{
    struct stat path_stat, descriptor_stat;
    if (yabai_runtime_fstatat(parent_fd, name, &path_stat, AT_SYMLINK_NOFOLLOW) != 0) {
        return errno == ENOENT && absence_is_safe ?
               YABAI_RUNTIME_DIRECTORY_ABSENT : YABAI_RUNTIME_DIRECTORY_UNSAFE;
    }
    if (!yabai_runtime_safe_directory_stat(&path_stat, uid, exact_mode)) {
        return YABAI_RUNTIME_DIRECTORY_UNSAFE;
    }
    int fd = yabai_runtime_openat(parent_fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd == -1) return YABAI_RUNTIME_DIRECTORY_UNSAFE;
    bool valid = yabai_runtime_fstat(fd, &descriptor_stat) == 0 &&
                 yabai_runtime_same_identity(&path_stat, &descriptor_stat) &&
                 yabai_runtime_safe_directory_stat(&descriptor_stat, uid, exact_mode) &&
                 yabai_runtime_safe_fd_acl(fd);
    if (!valid) {
        close(fd);
        return YABAI_RUNTIME_DIRECTORY_UNSAFE;
    }
    *child_fd = fd;
    return YABAI_RUNTIME_DIRECTORY_VALID;
}

/* Strictly read-only validation for doctor/controller use. Unlike directory
 * preparation this intentionally has no effective-UID equality requirement:
 * ownership and safety are checked against paths->uid at every descriptor
 * boundary, and no leaf is created or chmodded. */
static inline enum yabai_runtime_directory_status
    yabai_runtime_validate_directories_readonly(const struct yabai_runtime_paths *paths)
{
    if (!paths || paths->uid == 0) return YABAI_RUNTIME_DIRECTORY_UNSAFE;
    int home_fd = open(paths->home, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (home_fd == -1) return YABAI_RUNTIME_DIRECTORY_UNSAFE;
    struct stat home_path, home_descriptor;
    bool valid_home = lstat(paths->home, &home_path) == 0 &&
                      yabai_runtime_fstat(home_fd, &home_descriptor) == 0 &&
                      yabai_runtime_same_identity(&home_path, &home_descriptor) &&
                      yabai_runtime_safe_directory_stat(&home_descriptor, paths->uid, 0) &&
                      yabai_runtime_safe_fd_acl(home_fd);
    if (!valid_home) {
        close(home_fd);
        return YABAI_RUNTIME_DIRECTORY_UNSAFE;
    }

    int library_fd = -1, caches_fd = -1, yabai_fd = -1, runtime_fd = -1;
    enum yabai_runtime_directory_status status =
        yabai_runtime_open_readonly_component(home_fd, "Library", paths->uid, 0, false, &library_fd);
    if (status == YABAI_RUNTIME_DIRECTORY_VALID) {
        status = yabai_runtime_open_readonly_component(library_fd, "Caches", paths->uid, 0, false, &caches_fd);
    }
    if (status == YABAI_RUNTIME_DIRECTORY_VALID) {
        status = yabai_runtime_open_readonly_component(caches_fd, "yabai", paths->uid, 0700, true, &yabai_fd);
    }
    if (status == YABAI_RUNTIME_DIRECTORY_VALID) {
        status = yabai_runtime_open_readonly_component(yabai_fd, "runtime", paths->uid, 0700, true, &runtime_fd);
    }
    if (runtime_fd != -1) close(runtime_fd);
    if (yabai_fd != -1) close(yabai_fd);
    if (caches_fd != -1) close(caches_fd);
    if (library_fd != -1) close(library_fd);
    close(home_fd);
    return status;
}

struct yabai_created_directory {
    int parent_fd;
    const char *name;
    char path[PATH_MAX];
    dev_t device;
    ino_t inode;
    bool created;
};

static inline bool yabai_runtime_open_child_directory(int parent_fd, const char *parent_path, const char *name,
                                                       uid_t uid, bool create, mode_t exact_mode, char output[PATH_MAX],
                                                       int *child_fd, struct yabai_created_directory *created)
{
    if (!yabai_runtime_join(output, parent_path, name)) return false;
    struct stat lst;
    bool made_directory = false;
    if (yabai_runtime_fstatat(parent_fd, name, &lst, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno != ENOENT || !create || YABAI_RUNTIME_GETEUID() != uid || mkdirat(parent_fd, name, 0700) != 0) return false;
        made_directory = true;
    }
    int fd = yabai_runtime_openat(parent_fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd == -1) return false; /* Identity is unknown: retain the new name. */
    if (made_directory) {
        struct stat captured;
        if (yabai_runtime_fstat(fd, &captured) != 0 || !S_ISDIR(captured.st_mode) || captured.st_uid != uid) {
            close(fd);
            return false; /* Identity is unknown: retain the new name. */
        }
        created->parent_fd = parent_fd;
        created->name = name;
        snprintf(created->path, sizeof(created->path), "%s", output);
        created->device = captured.st_dev;
        created->inode = captured.st_ino;
        created->created = true;
        lst = captured;
    } else if (!yabai_runtime_safe_directory_stat(&lst, uid, exact_mode)) {
        close(fd);
        return false;
    }
    if (created->created && yabai_runtime_fchmod(fd, 0700) != 0) {
        close(fd);
        return false;
    }
    struct stat final_path, final_fd;
    bool valid = yabai_runtime_fstatat(parent_fd, name, &final_path, AT_SYMLINK_NOFOLLOW) == 0 &&
                 yabai_runtime_fstat(fd, &final_fd) == 0 && yabai_runtime_same_identity(&lst, &final_path) &&
                 yabai_runtime_same_identity(&final_path, &final_fd) &&
                 yabai_runtime_safe_directory_stat(&final_fd, uid, exact_mode) && yabai_runtime_safe_fd_acl(fd);
    if (!valid) {
        close(fd);
        return false;
    }
    *child_fd = fd;
    return true;
}

static inline void yabai_runtime_rollback_created(struct yabai_created_directory *created,
                                                   enum yabai_runtime_test_event event)
{
    if (!created->created) return;
    yabai_runtime_test_event(event, created->path);
    struct stat current;
    if (yabai_runtime_fstatat(created->parent_fd, created->name, &current, AT_SYMLINK_NOFOLLOW) == 0 &&
        current.st_dev == created->device && current.st_ino == created->inode && S_ISDIR(current.st_mode)) {
        unlinkat(created->parent_fd, created->name, AT_REMOVEDIR);
    }
}

static inline bool yabai_runtime_prepare_directories(const struct yabai_runtime_paths *paths, bool create)
{
    if (!paths || paths->uid == 0 || YABAI_RUNTIME_GETEUID() != paths->uid) return false;
    int home_fd = open(paths->home, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (home_fd == -1) return false;
    struct stat home_path, home_descriptor;
    if (lstat(paths->home, &home_path) != 0 || yabai_runtime_fstat(home_fd, &home_descriptor) != 0 ||
        !yabai_runtime_same_identity(&home_path, &home_descriptor) ||
        !yabai_runtime_safe_directory_stat(&home_descriptor, paths->uid, 0) ||
        !yabai_runtime_safe_fd_acl(home_fd)) {
        close(home_fd);
        return false;
    }
    int library_fd = -1, caches_fd = -1, yabai_fd = -1, runtime_fd = -1;
    char library[PATH_MAX], caches[PATH_MAX], yabai[PATH_MAX], runtime[PATH_MAX];
    struct yabai_created_directory made_yabai = {0}, made_runtime = {0}, none = {0};
    bool ok = yabai_runtime_open_child_directory(home_fd, paths->home, "Library", paths->uid, false, 0,
                                                  library, &library_fd, &none) &&
              yabai_runtime_open_child_directory(library_fd, library, "Caches", paths->uid, false, 0,
                                                  caches, &caches_fd, &none) &&
              yabai_runtime_open_child_directory(caches_fd, caches, "yabai", paths->uid, create, 0700,
                                                  yabai, &yabai_fd, &made_yabai) &&
              yabai_runtime_open_child_directory(yabai_fd, yabai, "runtime", paths->uid, create, 0700,
                                                  runtime, &runtime_fd, &made_runtime) &&
              strcmp(runtime, paths->directory) == 0;
    if (!ok) {
        yabai_runtime_rollback_created(&made_runtime, YABAI_RUNTIME_TEST_BEFORE_RUNTIME_ROLLBACK);
        yabai_runtime_rollback_created(&made_yabai, YABAI_RUNTIME_TEST_BEFORE_YABAI_ROLLBACK);
    }
    if (runtime_fd != -1) close(runtime_fd);
    if (yabai_fd != -1) close(yabai_fd);
    if (caches_fd != -1) close(caches_fd);
    if (library_fd != -1) close(library_fd);
    close(home_fd);
    return ok;
}

static inline bool yabai_runtime_prepare_for_uid(uid_t uid, const char *home_override, bool create,
                                                  struct yabai_runtime_paths *paths)
{
    return yabai_runtime_derive_for_uid(uid, home_override, paths) && yabai_runtime_prepare_directories(paths, create);
}

static inline bool yabai_runtime_prepare_for_real_uid(const char *home_override, bool create,
                                                       struct yabai_runtime_paths *paths)
{
    return yabai_runtime_prepare_for_uid(getuid(), home_override, create, paths);
}

static inline bool yabai_runtime_safe_socket_stat(const struct stat *st, uid_t uid, bool exact_mode)
{
    return S_ISSOCK(st->st_mode) && st->st_uid == uid && st->st_nlink == 1 && (st->st_mode & 022) == 0 &&
           (!exact_mode || (st->st_mode & 0777) == 0600);
}

static inline bool yabai_runtime_remove_stale_socket(const char *path, uid_t uid)
{
    struct stat before, after;
    if (lstat(path, &before) != 0) return errno == ENOENT;
    if (!yabai_runtime_safe_socket_stat(&before, uid, false) || !yabai_runtime_safe_socket_acl(path, &before)) return false;
    yabai_runtime_test_event(YABAI_RUNTIME_TEST_AFTER_STALE_VALIDATE, path);
    if (lstat(path, &after) != 0 || !yabai_runtime_same_identity(&before, &after)) return false;
    return unlink(path) == 0;
}

static inline bool yabai_runtime_capture_bound_socket(const char *path, uid_t uid, struct yabai_bound_entry *entry)
{
    struct stat before, after;
    if (!entry || lstat(path, &before) != 0 || !S_ISSOCK(before.st_mode) || before.st_uid != uid || before.st_nlink != 1) return false;
    entry->device = before.st_dev;
    entry->inode = before.st_ino;
    entry->uid = uid;
    entry->valid = true;
    yabai_runtime_test_event(YABAI_RUNTIME_TEST_AFTER_BIND_CAPTURE, path);
    if (lstat(path, &after) != 0 || !yabai_runtime_same_identity(&before, &after)) {
        entry->valid = false;
        return false;
    }
    return true;
}

static inline bool yabai_runtime_validate_bound_socket(const char *path, const struct yabai_bound_entry *entry)
{
    struct stat st;
    return path && entry && entry->valid && lstat(path, &st) == 0 && st.st_dev == entry->device &&
           st.st_ino == entry->inode && yabai_runtime_safe_socket_stat(&st, entry->uid, true) &&
           yabai_runtime_safe_socket_acl(path, &st);
}

static inline void yabai_runtime_cleanup_bound_socket(const char *path, struct yabai_bound_entry *entry)
{
    struct stat st;
    if (path && entry && entry->valid && lstat(path, &st) == 0 && st.st_dev == entry->device &&
        st.st_ino == entry->inode && S_ISSOCK(st.st_mode) && st.st_uid == entry->uid && st.st_nlink == 1) unlink(path);
    if (entry) entry->valid = false;
}

static inline int yabai_runtime_open_lock(const char *path, uid_t uid)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd == -1) return -1;
    struct stat st;
    if (yabai_runtime_fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != uid || st.st_nlink != 1 ||
        (st.st_mode & 0777) != 0600 || !yabai_runtime_safe_fd_acl(fd)) {
        close(fd);
        return -1;
    }
    return fd;
}

static inline int yabai_runtime_acquire_lock(const char *path, uid_t uid)
{
    int fd = yabai_runtime_open_lock(path, uid);
    if (fd == -1) return -1;
    struct flock lock = { .l_start = 0, .l_len = 0, .l_type = F_WRLCK, .l_whence = SEEK_SET };
    if (fcntl(fd, F_SETLK, &lock) == -1) {
        close(fd);
        return -1;
    }
    return fd;
}

#endif
