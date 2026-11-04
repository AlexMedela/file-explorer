// base.h - core types, memory arenas, string helpers
#pragma once
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <pwd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/statvfs.h>
#ifdef __GLIBC__
#include <malloc.h> // mallopt
#endif

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef float    f32;
typedef double   f64;

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) MIN(MAX(x, lo), hi)

static inline u64 now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); abort(); }
    return p;
}

static void *xcalloc(size_t c, size_t n) {
    void *p = calloc(c ? c : 1, n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); abort(); }
    return p;
}

static void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); abort(); }
    return p;
}

// ---------------------------------------------------------------------------
// Arena: bump allocator made of chained blocks, freed all at once.

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t cap, used, pad; // 32-byte header keeps data 16-byte aligned
} ArenaBlock;

typedef struct {
    ArenaBlock *head;
    size_t block_size;
} Arena;

static void *arena_alloc(Arena *a, size_t n, size_t align) {
    ArenaBlock *b = a->head;
    if (b) {
        size_t off = (b->used + align - 1) & ~(align - 1);
        if (off + n <= b->cap) {
            b->used = off + n;
            return (u8 *)(b + 1) + off;
        }
    }
    size_t bs = a->block_size ? a->block_size : (1u << 20);
    if (n + align > bs) bs = n + align;
    b = xmalloc(sizeof(ArenaBlock) + bs);
    b->next = a->head;
    b->cap = bs;
    b->used = n;
    a->head = b;
    return b + 1;
}

static void arena_free(Arena *a) {
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *n = b->next;
        free(b);
        b = n;
    }
    a->head = NULL;
}

// ---------------------------------------------------------------------------
// Strings

static inline bool is_dot_or_dotdot(const char *n) { return n[0] == '.' && (!n[1] || (n[1] == '.' && !n[2])); }
static inline bool is_digit(u8 c) { return c >= '0' && c <= '9'; }
static inline u8 ascii_lower(u8 c) { return (c >= 'A' && c <= 'Z') ? (u8)(c + 32) : c; }

// Natural, case-insensitive compare: "file2" < "file10".
static int natcmp(const char *a, const char *b) {
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    for (;;) {
        u8 ca = *x, cb = *y;
        if (!ca || !cb) return (int)ca - (int)cb;
        if (is_digit(ca) && is_digit(cb)) {
            while (*x == '0') x++;
            while (*y == '0') y++;
            const u8 *dx = x, *dy = y;
            while (is_digit(*x)) x++;
            while (is_digit(*y)) y++;
            size_t lx = (size_t)(x - dx), ly = (size_t)(y - dy);
            if (lx != ly) return lx < ly ? -1 : 1;
            int c = memcmp(dx, dy, lx);
            if (c) return c;
            continue;
        }
        ca = ascii_lower(ca);
        cb = ascii_lower(cb);
        if (ca != cb) return (int)ca - (int)cb;
        x++;
        y++;
    }
}

static bool str_ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (ascii_lower((u8)*a) != ascii_lower((u8)*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

static bool str_iprefix(const char *s, const char *prefix, size_t plen) {
    for (size_t i = 0; i < plen; i++) {
        if (!s[i] || ascii_lower((u8)s[i]) != ascii_lower((u8)prefix[i])) return false;
    }
    return true;
}

static u32 utf8_decode(const char **ps, const char *end) {
    const u8 *s = (const u8 *)*ps;
    u32 c = *s;
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (!n || (const char *)s + n > end) { *ps += 1; return 0xFFFD; }
    if (n == 1) { *ps += 1; return c; }
    u32 cp = c & (0x7Fu >> n);
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) { *ps += 1; return 0xFFFD; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *ps += n;
    return cp;
}

// Remove the last UTF-8 character from a NUL-terminated buffer.
static void utf8_pop(char *s) {
    size_t n = strlen(s);
    while (n > 0) {
        n--;
        if (((u8)s[n] & 0xC0) != 0x80) break;
    }
    s[n] = 0;
}

static void fmt_count(char *out, u64 n) {
    char tmp[32];
    int len = snprintf(tmp, sizeof tmp, "%llu", (unsigned long long)n);
    int o = 0;
    for (int i = 0; i < len; i++) {
        out[o++] = tmp[i];
        int rem = len - i - 1;
        if (rem > 0 && rem % 3 == 0) out[o++] = ',';
    }
    out[o] = 0;
}

static void fmt_size(char *out, size_t cap, u64 s) {
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    if (s < 1024) { snprintf(out, cap, "%llu B", (unsigned long long)s); return; }
    f64 v = (f64)s;
    int i = 0;
    while (v >= 1024.0 && i < 5) { v /= 1024.0; i++; }
    snprintf(out, cap, v < 10.0 ? "%.1f %s" : "%.0f %s", v, units[i]);
}

// ---------------------------------------------------------------------------
// Paths

// Lexically normalize an absolute path in place: collapse "//", ".", "..".
static void path_normalize(char *p) {
    char out[PATH_MAX];
    size_t n = 1;
    out[0] = '/';
    const char *s = p;
    while (*s) {
        while (*s == '/') s++;
        if (!*s) break;
        const char *e = s;
        while (*e && *e != '/') e++;
        size_t len = (size_t)(e - s);
        if (len == 1 && s[0] == '.') {
        } else if (len == 2 && s[0] == '.' && s[1] == '.') {
            if (n > 1) {
                n--;
                while (n > 1 && out[n - 1] != '/') n--;
                if (n > 1) n--;
            }
        } else if (n + len + 2 < sizeof out) {
            if (n > 1) out[n++] = '/';
            memcpy(out + n, s, len);
            n += len;
        }
        s = e;
    }
    out[n] = 0;
    memcpy(p, out, n + 1);
}

static void path_join(char *out, size_t cap, const char *dir, const char *name) {
    if (dir[0] == '/' && dir[1] == 0) snprintf(out, cap, "/%s", name);
    else snprintf(out, cap, "%s/%s", dir, name);
}

static const char *path_basename(const char *p) {
    const char *s = strrchr(p, '/');
    return (s && s[1]) ? s + 1 : p;
}

// ---------------------------------------------------------------------------
// Starting programs
// ---------------------------------------------------------------------------

// SIGCHLD is ignored app-wide (children reap themselves), and an ignored signal survives exec: every program we
// start gets it back at its default, or ones that wait for their own children (Python's subprocess, shells) break.
// flags: extra POSIX_SPAWN_* flags (setsid, process group...).
static void spawn_attr_init(posix_spawnattr_t *at, short flags) {
    posix_spawnattr_init(at);
    sigset_t def;
    sigemptyset(&def);
    sigaddset(&def, SIGCHLD);
    sigaddset(&def, SIGPIPE);
    posix_spawnattr_setsigdefault(at, &def);
    posix_spawnattr_setflags(at, (short)(flags | POSIX_SPAWN_SETSIGDEF));
}

static posix_spawnattr_t g_spawn_at;
static pthread_once_t g_spawn_once = PTHREAD_ONCE_INIT;
static void spawn_at_make(void) { spawn_attr_init(&g_spawn_at, 0); }

// The attributes for a plain posix_spawn (shared, read-only once made).
static const posix_spawnattr_t *spawn_attr(void) {
    pthread_once(&g_spawn_once, spawn_at_make);
    return &g_spawn_at;
}

// Is the program there (a path, or a name looked up in $PATH)?
static bool in_path(const char *name) {
    if (strchr(name, '/')) return access(name, X_OK) == 0;
    const char *path = getenv("PATH");
    char dirs[4096], p[PATH_MAX];
    snprintf(dirs, sizeof dirs, "%s", path && path[0] ? path : "/usr/local/bin:/usr/bin:/bin");
    char *save = NULL;
    for (char *d = strtok_r(dirs, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
        snprintf(p, sizeof p, "%s/%s", d[0] ? d : ".", name);
        if (access(p, X_OK) == 0) return true;
    }
    return false;
}

// The directories of an XDG search path ($XDG_DATA_DIRS, $XDG_CONFIG_DIRS; def when unset), most important first.
static int xdg_dirs(const char *env, const char *def, char (*out)[PATH_MAX], int max) {
    const char *v = getenv(env);
    char buf[8192];
    snprintf(buf, sizeof buf, "%s", v && v[0] ? v : def);
    int n = 0;
    char *save = NULL;
    for (char *d = strtok_r(buf, ":", &save); d && n < max; d = strtok_r(NULL, ":", &save)) {
        if (d[0] != '/') continue; // relative entries are invalid
        size_t l = strlen(d);
        while (l > 1 && d[l - 1] == '/') d[--l] = 0;
        bool dup = false;
        for (int i = 0; i < n; i++) dup |= !strcmp(out[i], d);
        if (!dup) snprintf(out[n++], PATH_MAX, "%s", d);
    }
    return n;
}
#define XDG_DATA_DEF "/usr/local/share:/usr/share"

// Rename without replacing what's there. Filesystems without RENAME_NOREPLACE (some FUSE, CIFS, NFS mounts, our
// phone folders) answer EINVAL: there, look first and rename plainly (racy, the best they allow).
static int rename_noreplace(int ofd, const char *from, int nfd, const char *to) {
    if (renameat2(ofd, from, nfd, to, RENAME_NOREPLACE) == 0) return 0;
    if (errno != EINVAL && errno != ENOSYS && errno != ENOTSUP) return -1;
    struct stat a, b;
    if (fstatat(ofd, from, &a, AT_SYMLINK_NOFOLLOW)) return -1;
    if (fstatat(nfd, to, &b, AT_SYMLINK_NOFOLLOW) == 0 && (a.st_dev != b.st_dev || a.st_ino != b.st_ino)) {
        errno = EEXIST; // (the same file: a case-only rename on a case-insensitive share)
        return -1;
    }
    return renameat(ofd, from, nfd, to);
}
