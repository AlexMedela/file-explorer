// ops.c - file operations on background threads: copy, move, trash, delete, restore.
//
// Each Op runs on its own thread (they can be long and may block on a conflict
// question), reports progress through atomics, and hands results back to the
// main thread via main_post(op_finished, op). Transfers (copy / move / sync) wait
// for their turn in the UI's queue after counting, can be paused, and can verify
// each copied file by checksum.

enum { OP_COPY, OP_MOVE, OP_TRASH, OP_DELETE, OP_RESTORE, OP_EXTRACT, OP_COMPRESS, OP_SYNC, OP_RENAME, OP_MKDIR, OP_MKFILE };
// conflict answers; CF_RENAME = rename what's already there, CF_NEWER = replace only with something newer
enum { CF_ASK, CF_REPLACE, CF_SKIP, CF_KEEP_BOTH, CF_CANCEL, CF_RENAME, CF_NEWER };
enum { OW_NONE, OW_ALL, OW_NEWER }; // copying over what exists: never, always, only when the source is newer
enum { OPS_COUNTING, OPS_QUEUED, OPS_RUNNING, OPS_DONE };

typedef struct Op {
    int kind;
    char **src;
    int nsrc;
    char **dst;      // OP_RESTORE / OP_SYNC / OP_RENAME: explicit destination per src (OP_MKDIR / OP_MKFILE: src is created)
    bool admin;      // runs as the administrator: through pkexec, in a helper (this program with --admin-op)
    bool trash_first; // OP_SYNC: what's at a destination goes to the Trash before it's replaced (restoring a version)
    int perm_errs;   // errors that were "permission denied" / "not permitted" (the card offers to retry as admin)
    char **aux_in;   // OP_RESTORE: .trashinfo files to remove after success
    char *dst_dir;   // OP_COPY / OP_MOVE / OP_EXTRACT
    char *arc;       // OP_EXTRACT: archive to read; OP_COMPRESS: archive to write
    bool open_after; // OP_EXTRACT of one file: open it when done
    bool is_undo;    // don't push onto the undo stack
    _Atomic u64 bytes_total, bytes_done;
    _Atomic u32 files_total, files_done;
    _Atomic int phase, cancel;
    pthread_mutex_t mtx;
    pthread_cond_t cv;
    char cur[NAME_MAX + 1];
    // conflict handshake (guarded by mtx)
    bool conflict_pending, conflict_dir;
    char conflict_name[NAME_MAX + 1];
    int answer;
    bool answer_all;
    int policy;
    bool conflict_batch;  // the question is about all of cf_names at once (answer: a policy, or CF_ASK = one by one)
    int conflicts_left;   // asking one by one: conflicts still to come after this one
    // conflicts found before starting (written by the op thread before it asks)
    char **cf_names;
    signed char *cf_newer; // per conflict: the incoming item is newer (1), older (-1), same date (0)
    bool *cf_dirs;
    int ncf;
    // transfer queue (copy / move / sync)
    bool queued;          // set by the UI: big ones wait for their turn after counting (the admin helper never waits)
    bool go;              // guarded by mtx, set by the UI: its turn came
    _Atomic int paused;   // set by the UI (under mtx): the thread waits at the next chunk or item
    _Atomic bool verify;  // checksum each copied file against its original (taken when the op starts running)
    bool verify_on;       // what verify was when it started (op thread; the UI reads it once running)
    _Atomic u64 bytes_verified;
    _Atomic u32 files_verified;
    _Atomic bool verifying; // reading a copy back right now
    int inner_skips;      // op thread: files left alone inside a folder merge ("newer only")
    u64 t_run;            // when it started running (after counting and waiting)
    // results, read by the main thread after OPS_DONE
    int nerr;
    char err[512];
    char **in, **out, **aux; // in = original path, out = resulting path, aux = trashinfo
    int nout, capout;
    u64 t_start, t_end;
    // UI bookkeeping (main thread)
    bool dismissed, finished, from_cut;
    f64 bar; u64 bar_t;  // the card's bar as drawn: eases toward the real fraction between progress updates
    f64 rate; u64 rate_work, rate_t; // speed: bytes per second, smoothed; the work done at the last sample
    bool conflict_shown; // guarded by mtx
} Op;

static void op_finished(void *arg); // app.c
static void run_extract(struct Op *op);  // archive.c
static void run_compress(struct Op *op); // archive.c

static char **strv_dup(char **v, int n) {
    if (!v) return NULL;
    char **r = xmalloc((size_t)MAX(n, 1) * sizeof(char *));
    for (int i = 0; i < n; i++) r[i] = v[i] ? strdup(v[i]) : NULL;
    return r;
}

static void strv_free(char **v, int n) {
    if (!v) return;
    for (int i = 0; i < n; i++) free(v[i]);
    free(v);
}

static Op *op_new(int kind, char **src, int nsrc, const char *dst_dir) {
    Op *op = xcalloc(1, sizeof *op);
    op->kind = kind;
    op->src = strv_dup(src, nsrc);
    op->nsrc = nsrc;
    op->dst_dir = dst_dir ? strdup(dst_dir) : NULL;
    pthread_mutex_init(&op->mtx, NULL);
    pthread_cond_init(&op->cv, NULL);
    op->policy = CF_ASK;
    return op;
}

static void op_free(Op *op) {
    strv_free(op->src, op->nsrc);
    strv_free(op->dst, op->nsrc);
    strv_free(op->aux_in, op->nsrc);
    strv_free(op->in, op->nout);
    strv_free(op->out, op->nout);
    strv_free(op->aux, op->nout);
    free(op->dst_dir);
    free(op->arc);
    strv_free(op->cf_names, op->ncf);
    free(op->cf_newer);
    free(op->cf_dirs);
    pthread_mutex_destroy(&op->mtx);
    pthread_cond_destroy(&op->cv);
    free(op);
}

static bool op_cancelled(Op *op) { return atomic_load_explicit(&op->cancel, memory_order_relaxed); }

static void op_error(Op *op, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    pthread_mutex_lock(&op->mtx);
    if (strstr(buf, strerror(EACCES)) || strstr(buf, strerror(EPERM))) op->perm_errs++;
    if (!op->nerr++) snprintf(op->err, sizeof op->err, "%s", buf);
    pthread_mutex_unlock(&op->mtx);
}

static void op_set_cur(Op *op, const char *name) {
    pthread_mutex_lock(&op->mtx);
    snprintf(op->cur, sizeof op->cur, "%s", name);
    pthread_mutex_unlock(&op->mtx);
}

static void op_record(Op *op, const char *in, const char *out, const char *aux) {
    if (op->nout == op->capout) {
        op->capout = op->capout ? op->capout * 2 : 16;
        op->in = xrealloc(op->in, (size_t)op->capout * sizeof(char *));
        op->out = xrealloc(op->out, (size_t)op->capout * sizeof(char *));
        op->aux = xrealloc(op->aux, (size_t)op->capout * sizeof(char *));
    }
    op->in[op->nout] = in ? strdup(in) : NULL;
    op->out[op->nout] = out ? strdup(out) : NULL;
    op->aux[op->nout] = aux ? strdup(aux) : NULL;
    op->nout++;
}

// Blocks the op thread until the user answers (or cancels).
static int op_ask_conflict(Op *op, const char *name, bool is_dir) {
    pthread_mutex_lock(&op->mtx);
    if (op->policy != CF_ASK) {
        int p = op->policy;
        pthread_mutex_unlock(&op->mtx);
        return p;
    }
    snprintf(op->conflict_name, sizeof op->conflict_name, "%s", name);
    op->conflict_dir = is_dir;
    op->answer = -1;
    op->conflict_pending = true;
    pthread_mutex_unlock(&op->mtx);
    main_wake();
    pthread_mutex_lock(&op->mtx);
    while (op->answer < 0 && !op_cancelled(op)) pthread_cond_wait(&op->cv, &op->mtx);
    int a = op_cancelled(op) ? CF_CANCEL : op->answer;
    if (op->answer_all && a != CF_CANCEL) op->policy = a;
    op->conflict_pending = false;
    pthread_mutex_unlock(&op->mtx);
    if (a == CF_CANCEL) atomic_store(&op->cancel, 1);
    return a;
}

// Conflicts found before starting are asked about together: one answer for all of them, or CF_ASK (one by one).
static int op_ask_batch(Op *op) {
    pthread_mutex_lock(&op->mtx);
    if (!op->ncf || op->policy != CF_ASK) {
        int p = op->policy;
        pthread_mutex_unlock(&op->mtx);
        return p;
    }
    snprintf(op->conflict_name, sizeof op->conflict_name, "%s", op->cf_names[0]);
    op->conflict_dir = op->cf_dirs[0];
    op->conflict_batch = true;
    op->answer = -1;
    op->conflict_pending = true;
    pthread_mutex_unlock(&op->mtx);
    main_wake();
    pthread_mutex_lock(&op->mtx);
    while (op->answer < 0 && !op_cancelled(op)) pthread_cond_wait(&op->cv, &op->mtx);
    int a = op_cancelled(op) ? CF_CANCEL : op->answer;
    if (a != CF_CANCEL) op->policy = a;
    op->conflict_pending = op->conflict_batch = false;
    pthread_mutex_unlock(&op->mtx);
    if (a == CF_CANCEL) atomic_store(&op->cancel, 1);
    return a;
}

// Paused from the UI: wait here (between chunks and items) until resumed or cancelled.
static void op_pause_point(Op *op) {
    if (!atomic_load_explicit(&op->paused, memory_order_relaxed)) return;
    pthread_mutex_lock(&op->mtx);
    while (atomic_load(&op->paused) && !op_cancelled(op)) pthread_cond_wait(&op->cv, &op->mtx);
    pthread_mutex_unlock(&op->mtx);
}

// Transfers take turns: once counted, a big one waits until the UI's queue lets it run (ops_schedule); small ones
// (a few files, a rename-only move) go straight through so they don't sit behind a long copy.
#define OP_QUEUE_BYTES (32ull << 20)
#define OP_QUEUE_FILES 1000u
static void op_wait_turn(Op *op) {
    if (op->queued && (atomic_load(&op->bytes_total) >= OP_QUEUE_BYTES || atomic_load(&op->files_total) >= OP_QUEUE_FILES)) {
        atomic_store(&op->phase, OPS_QUEUED);
        main_wake();
        pthread_mutex_lock(&op->mtx);
        while (!op->go && !op_cancelled(op)) pthread_cond_wait(&op->cv, &op->mtx);
        pthread_mutex_unlock(&op->mtx);
    }
    op_pause_point(op); // paused while it waited
    op->verify_on = atomic_load(&op->verify);
    op->t_run = now_ns();
    atomic_store(&op->phase, OPS_RUNNING);
}

// --- verify (checksum compare after copying): XXH64, fast enough to keep up with an SSD ----------------------------

#define XXP1 0x9E3779B185EBCA87ull
#define XXP2 0xC2B2AE3D27D4EB4Full
#define XXP3 0x165667B19E3779F9ull
#define XXP4 0x85EBCA77C2B2AE63ull
#define XXP5 0x27D4EB2F165667C5ull

typedef struct { u64 v[4], len; u8 buf[32]; u32 n; } Xxh64;

static u64 xxh_rotl(u64 x, int r) { return (x << r) | (x >> (64 - r)); }
static u64 xxh_round(u64 acc, u64 in) { return xxh_rotl(acc + in * XXP2, 31) * XXP1; }
static u64 xxh_merge(u64 h, u64 v) { return (h ^ xxh_round(0, v)) * XXP1 + XXP4; }
static u64 xxh_rd64(const u8 *p) { u64 v; memcpy(&v, p, 8); return v; } // (little-endian hosts)
static u32 xxh_rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }

static void xxh64_init(Xxh64 *s) {
    memset(s, 0, sizeof *s);
    s->v[0] = XXP1 + XXP2;
    s->v[1] = XXP2;
    s->v[3] = 0 - XXP1;
}

static void xxh64_stripe(Xxh64 *s, const u8 *p) {
    for (int i = 0; i < 4; i++) s->v[i] = xxh_round(s->v[i], xxh_rd64(p + i * 8));
}

static void xxh64_update(Xxh64 *s, const u8 *p, size_t n) {
    s->len += n;
    if (s->n) {
        size_t k = MIN(n, 32 - (size_t)s->n);
        memcpy(s->buf + s->n, p, k);
        s->n += (u32)k;
        p += k;
        n -= k;
        if (s->n < 32) return;
        xxh64_stripe(s, s->buf);
        s->n = 0;
    }
    for (; n >= 32; p += 32, n -= 32) xxh64_stripe(s, p);
    memcpy(s->buf, p, n);
    s->n = (u32)n;
}

static u64 xxh64_final(const Xxh64 *s) {
    u64 h;
    if (s->len >= 32) {
        h = xxh_rotl(s->v[0], 1) + xxh_rotl(s->v[1], 7) + xxh_rotl(s->v[2], 12) + xxh_rotl(s->v[3], 18);
        for (int i = 0; i < 4; i++) h = xxh_merge(h, s->v[i]);
    } else {
        h = s->v[2] + XXP5;
    }
    h += s->len;
    const u8 *p = s->buf;
    u32 n = s->n;
    for (; n >= 8; p += 8, n -= 8) h = xxh_rotl(h ^ xxh_round(0, xxh_rd64(p)), 27) * XXP1 + XXP4;
    if (n >= 4) { h = xxh_rotl(h ^ (u64)xxh_rd32(p) * XXP1, 23) * XXP2 + XXP3; p += 4; n -= 4; }
    for (; n; p++, n--) h = xxh_rotl(h ^ *p * XXP5, 11) * XXP1;
    h ^= h >> 33;
    h *= XXP2;
    h ^= h >> 29;
    h *= XXP3;
    return h ^ (h >> 32);
}

static ssize_t read_full_at(int fd, u8 *buf, size_t cap, off_t off) {
    size_t got = 0;
    while (got < cap) {
        ssize_t n = pread(fd, buf + got, cap - got, off + (off_t)got);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 0) break;
        got += (size_t)n;
    }
    return (ssize_t)got;
}

// Read the original (in) and the new copy (ddir/target, flushed to disk and dropped from the cache first, so it's
// read back from the disk) and compare their checksums. false: they differ, can't be read, or cancelled.
static bool verify_copy(Op *op, int in, int ddir, const char *target, const char *dname) {
    int v = openat(ddir, target, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (v < 0) { op_error(op, "Can't read back \"%s\" to verify it: %s", dname, strerror(errno)); return false; }
    posix_fadvise(v, 0, 0, POSIX_FADV_DONTNEED);
    atomic_store(&op->verifying, true);
    enum { BUF = 1 << 20 };
    u8 *a = xmalloc(BUF), *b = xmalloc(BUF);
    Xxh64 ha, hb;
    xxh64_init(&ha);
    xxh64_init(&hb);
    bool ok = true;
    for (off_t off = 0;; off += BUF) {
        op_pause_point(op);
        if (op_cancelled(op)) { ok = false; break; }
        ssize_t n = read_full_at(in, a, BUF, off), m = read_full_at(v, b, BUF, off);
        if (n < 0 || m < 0) {
            op_error(op, "Can't read %s \"%s\" to verify it: %s", n < 0 ? "the original of" : "back", dname, strerror(errno));
            ok = false;
            break;
        }
        xxh64_update(&ha, a, (size_t)n);
        xxh64_update(&hb, b, (size_t)m);
        atomic_fetch_add(&op->bytes_verified, (u64)n);
        if (n < BUF || m < BUF) break;
    }
    if (ok && xxh64_final(&ha) != xxh64_final(&hb)) {
        op_error(op, "\"%s\" failed verification: the copy differs from the original, so it was removed", dname);
        ok = false;
    }
    if (ok) atomic_fetch_add(&op->files_verified, 1);
    atomic_store(&op->verifying, false);
    free(a);
    free(b);
    close(v);
    return ok;
}

// --- path helpers ----------------------------------------------------------

static void path_split(const char *path, char *parent, size_t pcap, char *base, size_t bcap) {
    const char *s = strrchr(path, '/');
    if (!s) { snprintf(parent, pcap, "."); snprintf(base, bcap, "%s", path); return; }
    if (s == path) snprintf(parent, pcap, "/");
    else snprintf(parent, pcap, "%.*s", (int)(s - path), path);
    snprintf(base, bcap, "%s", s + 1);
}

static bool exists_at(int dfd, const char *name) {
    struct stat st;
    return fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW) == 0;
}

// "name (copy).ext", "name (copy 2).ext" ... or "name (2).ext", "name (3).ext" ...
static void unique_name(int dfd, const char *base, bool is_dir, bool copy_style, char *out, size_t cap) {
    const char *dot = is_dir ? NULL : strrchr(base, '.');
    if (dot == base) dot = NULL;
    int stem = dot ? (int)(dot - base) : (int)strlen(base);
    const char *ext = dot ? dot : "";
    for (int k = copy_style ? 1 : 2; k < 100000; k++) {
        if (copy_style) {
            if (k == 1) snprintf(out, cap, "%.*s (copy)%s", stem, base, ext);
            else snprintf(out, cap, "%.*s (copy %d)%s", stem, base, k, ext);
        } else {
            snprintf(out, cap, "%.*s (%d)%s", stem, base, k, ext);
        }
        if (!exists_at(dfd, out)) return;
    }
}

// --- counting --------------------------------------------------------------

// sub: take it off the totals again (an item that was skipped)
static void count_tree_by(Op *op, int dfd, const char *name, bool sub) {
    if (op_cancelled(op)) return;
    struct stat st;
    if (fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW)) return;
    atomic_fetch_add(&op->files_total, sub ? (u32)-1 : 1u);
    if (S_ISREG(st.st_mode)) atomic_fetch_add(&op->bytes_total, sub ? 0 - (u64)st.st_size : (u64)st.st_size);
    if (!S_ISDIR(st.st_mode)) return;
    int fd = openat(dfd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return;
    DIR *d = fdopendir(fd);
    if (!d) { close(fd); return; }
    struct dirent *e;
    while ((e = readdir(d)) && !op_cancelled(op)) {
        if (e->d_name[0] == '.' && (!e->d_name[1] || (e->d_name[1] == '.' && !e->d_name[2]))) continue;
        count_tree_by(op, dirfd(d), e->d_name, sub);
    }
    closedir(d);
}

static void count_tree(Op *op, int dfd, const char *name) { count_tree_by(op, dfd, name, false); }

static bool ts_newer(const struct timespec *a, const struct timespec *b) {
    return a->tv_sec != b->tv_sec ? a->tv_sec > b->tv_sec : a->tv_nsec > b->tv_nsec;
}

// --- copy ------------------------------------------------------------------

static int copy_file(Op *op, int sdir, const char *sname, int ddir, const char *dname, const struct stat *st, bool overwrite) {
    op_pause_point(op);
    int in = openat(sdir, sname, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (in < 0) { op_error(op, "Can't read \"%s\": %s", sname, strerror(errno)); return -1; }
    char tmp[NAME_MAX + 1];
    const char *target = dname;
    if (overwrite && exists_at(ddir, dname)) {
        snprintf(tmp, sizeof tmp, ".%.200s.fe-part", dname);
        unlinkat(ddir, tmp, 0);
        target = tmp;
    }
    int out = openat(ddir, target, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, st->st_mode & 07777);
    if (out < 0) {
        op_error(op, "Can't create \"%s\": %s", dname, strerror(errno));
        close(in);
        return -1;
    }
    int rc = 0;
    bool fallback = false;
    u64 copied = 0;
    while (!op_cancelled(op)) {
        op_pause_point(op);
        ssize_t n = copy_file_range(in, NULL, out, NULL, 8u << 20, 0);
        if (n < 0) {
            if (copied == 0 && (errno == EXDEV || errno == EINVAL || errno == ENOSYS || errno == EOPNOTSUPP || errno == EBADF)) {
                fallback = true;
                break;
            }
            op_error(op, "Error copying \"%s\": %s", sname, strerror(errno));
            rc = -1;
            break;
        }
        if (n == 0) break;
        copied += (u64)n;
        atomic_fetch_add(&op->bytes_done, (u64)n);
    }
    if (fallback) {
        enum { BUF = 1 << 20 };
        char *buf = xmalloc(BUF);
        while (!op_cancelled(op)) {
            op_pause_point(op);
            ssize_t n = read(in, buf, BUF);
            if (n < 0) { if (errno == EINTR) continue; op_error(op, "Error reading \"%s\": %s", sname, strerror(errno)); rc = -1; break; }
            if (n == 0) break;
            for (ssize_t w = 0; w < n;) {
                ssize_t k = write(out, buf + w, (size_t)(n - w));
                if (k < 0) { if (errno == EINTR) continue; op_error(op, "Error writing \"%s\": %s", dname, strerror(errno)); rc = -1; break; }
                w += k;
            }
            if (rc) break;
            atomic_fetch_add(&op->bytes_done, (u64)n);
        }
        free(buf);
    }
    if (op_cancelled(op)) rc = -1;
    if (!rc) {
        fchmod(out, st->st_mode & 07777);
        struct timespec ts[2] = {st->st_atim, st->st_mtim};
        futimens(out, ts);
    }
    // verifying reads the copy back from the disk: it has to be there first (some filesystems can't sync: fine)
    if (!rc && op->verify_on && fdatasync(out) && errno != EINVAL && errno != EROFS && errno != ENOSYS) {
        op_error(op, "Error writing \"%s\": %s", dname, strerror(errno));
        rc = -1;
    }
    if (close(out) && !rc) { op_error(op, "Error writing \"%s\": %s", dname, strerror(errno)); rc = -1; }
    // (before it replaces anything: a copy that doesn't match never takes the place of what was there)
    if (!rc && op->verify_on && !verify_copy(op, in, ddir, target, dname)) rc = -1;
    close(in);
    if (rc) { unlinkat(ddir, target, 0); return -1; }
    if (target != dname && renameat(ddir, target, ddir, dname) &&
        (unlinkat(ddir, dname, 0) || renameat(ddir, target, ddir, dname))) { // MTP won't rename over a file: remove it first
        op_error(op, "Can't replace \"%s\": %s", dname, strerror(errno));
        unlinkat(ddir, target, 0);
        return -1;
    }
    return 0;
}

// ow: OW_* for what's already at the destination (folders merge unless OW_NONE)
static int copy_tree(Op *op, int sdir, const char *sname, int ddir, const char *dname, int ow) {
    op_pause_point(op);
    if (op_cancelled(op)) return -1;
    struct stat st;
    if (fstatat(sdir, sname, &st, AT_SYMLINK_NOFOLLOW)) {
        op_error(op, "Can't read \"%s\": %s", sname, strerror(errno));
        return -1;
    }
    op_set_cur(op, sname);
    struct stat ds;
    if (ow == OW_NEWER && !S_ISDIR(st.st_mode) && fstatat(ddir, dname, &ds, AT_SYMLINK_NOFOLLOW) == 0 &&
        !ts_newer(&st.st_mtim, &ds.st_mtim)) { // what's there is as new or newer: leave it
        if (S_ISREG(st.st_mode)) atomic_fetch_add(&op->bytes_total, 0 - (u64)st.st_size);
        atomic_fetch_add(&op->files_done, 1);
        op->inner_skips++;
        return 0;
    }
    bool overwrite = ow != OW_NONE;
    int rc = 0;
    if (S_ISDIR(st.st_mode)) {
        if (mkdirat(ddir, dname, 0700)) {
            struct stat dst;
            if (!(errno == EEXIST && overwrite && fstatat(ddir, dname, &dst, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(dst.st_mode))) {
                op_error(op, "Can't create folder \"%s\": %s", dname, strerror(errno));
                return -1;
            }
        }
        int s = openat(sdir, sname, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        int d = openat(ddir, dname, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (s < 0 || d < 0) {
            op_error(op, "Can't open folder \"%s\": %s", sname, strerror(errno));
            if (s >= 0) close(s);
            if (d >= 0) close(d);
            return -1;
        }
        DIR *dp = fdopendir(dup(s));
        if (dp) {
            struct dirent *e;
            while ((e = readdir(dp)) && !op_cancelled(op)) {
                if (e->d_name[0] == '.' && (!e->d_name[1] || (e->d_name[1] == '.' && !e->d_name[2]))) continue;
                if (copy_tree(op, s, e->d_name, d, e->d_name, ow)) rc = -1;
            }
            closedir(dp);
        }
        fchmod(d, st.st_mode & 07777);
        struct timespec ts[2] = {st.st_atim, st.st_mtim};
        futimens(d, ts);
        close(s);
        close(d);
    } else if (S_ISLNK(st.st_mode)) {
        char target[PATH_MAX];
        ssize_t n = readlinkat(sdir, sname, target, sizeof target - 1);
        if (n < 0) { op_error(op, "Can't read link \"%s\": %s", sname, strerror(errno)); return -1; }
        target[n] = 0;
        if (overwrite) unlinkat(ddir, dname, 0);
        if (symlinkat(target, ddir, dname)) { op_error(op, "Can't create link \"%s\": %s", dname, strerror(errno)); return -1; }
    } else if (S_ISREG(st.st_mode)) {
        rc = copy_file(op, sdir, sname, ddir, dname, &st, overwrite);
    } else {
        op_error(op, "Skipped special file \"%s\"", sname);
        rc = -1;
    }
    atomic_fetch_add(&op->files_done, 1);
    return rc;
}

// --- delete ----------------------------------------------------------------

static int delete_tree(Op *op, int dfd, const char *name) {
    op_pause_point(op);
    struct stat st;
    if (fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW)) {
        if (errno == ENOENT) return 0;
        op_error(op, "Can't delete \"%s\": %s", name, strerror(errno));
        return -1;
    }
    op_set_cur(op, name);
    int rc = 0;
    if (S_ISDIR(st.st_mode)) {
        int fd = openat(dfd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) { op_error(op, "Can't open folder \"%s\": %s", name, strerror(errno)); return -1; }
        DIR *d = fdopendir(fd);
        if (!d) { close(fd); return -1; }
        struct dirent *e;
        while ((e = readdir(d)) && !op_cancelled(op)) {
            if (e->d_name[0] == '.' && (!e->d_name[1] || (e->d_name[1] == '.' && !e->d_name[2]))) continue;
            if (delete_tree(op, dirfd(d), e->d_name)) rc = -1;
        }
        closedir(d);
        if (!rc && !op_cancelled(op) && unlinkat(dfd, name, AT_REMOVEDIR)) {
            op_error(op, "Can't delete \"%s\": %s", name, strerror(errno));
            rc = -1;
        }
    } else if (unlinkat(dfd, name, 0)) {
        op_error(op, "Can't delete \"%s\": %s", name, strerror(errno));
        rc = -1;
    }
    atomic_fetch_add(&op->files_done, 1);
    return rc;
}

// --- trash (freedesktop.org Trash spec) ------------------------------------

static void url_encode_path(const char *s, char *out, size_t cap) {
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (; *s && o + 4 < cap; s++) {
        u8 c = (u8)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("/-._~", c)) out[o++] = (char)c;
        else { out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15]; }
    }
    out[o] = 0;
}

static int mkdir_p(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(tmp, mode) && errno != EEXIST) return -1;
        *p = '/';
    }
    if (mkdir(tmp, mode) && errno != EEXIST) return -1;
    return 0;
}

static void data_home(char *out, size_t cap) {
    const char *x = getenv("XDG_DATA_HOME");
    if (x && x[0] == '/') snprintf(out, cap, "%s", x);
    else snprintf(out, cap, "%s/.local/share", getenv("HOME") ? getenv("HOME") : "");
}

// Mount point of the filesystem containing path (path must exist).
static void find_topdir(const char *path, dev_t dev, char *out, size_t cap) {
    char cur[PATH_MAX];
    snprintf(cur, sizeof cur, "%s", path);
    for (;;) {
        char parent[PATH_MAX], base[NAME_MAX + 1];
        path_split(cur, parent, sizeof parent, base, sizeof base);
        struct stat st;
        if (!strcmp(cur, "/") || stat(parent, &st) || st.st_dev != dev) break;
        snprintf(cur, sizeof cur, "%s", parent);
    }
    snprintf(out, cap, "%s", cur);
}

static int trash_one(Op *op, const char *src) {
    struct stat st;
    if (lstat(src, &st)) { op_error(op, "Can't trash \"%s\": %s", path_basename(src), strerror(errno)); return -1; }
    char dh[PATH_MAX], trash[PATH_MAX], info_path_value[PATH_MAX];
    data_home(dh, sizeof dh);
    mkdir_p(dh, 0700);
    struct stat hs;
    if (stat(dh, &hs) == 0 && hs.st_dev == st.st_dev) {
        snprintf(trash, sizeof trash, "%s/Trash", dh);
        snprintf(info_path_value, sizeof info_path_value, "%s", src);
    } else {
        char top[PATH_MAX];
        find_topdir(src, st.st_dev, top, sizeof top);
        snprintf(trash, sizeof trash, "%s/.Trash-%u", strcmp(top, "/") ? top : "", (unsigned)getuid());
        size_t tl = strlen(top);
        snprintf(info_path_value, sizeof info_path_value, "%s", strcmp(top, "/") ? src + tl + 1 : src + 1);
    }
    char files[PATH_MAX], info[PATH_MAX];
    snprintf(files, sizeof files, "%s/files", trash);
    snprintf(info, sizeof info, "%s/info", trash);
    if (mkdir_p(files, 0700) || mkdir_p(info, 0700)) {
        op_error(op, "Can't use the trash for \"%s\": %s", path_basename(src), strerror(errno));
        return -1;
    }
    char enc[PATH_MAX * 3], date[64];
    url_encode_path(info_path_value, enc, sizeof enc);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", &tm);
    const char *base = path_basename(src);
    for (int k = 1; k < 10000; k++) {
        char name[NAME_MAX + 1], ipath[PATH_MAX], fpath[PATH_MAX];
        if (k == 1) snprintf(name, sizeof name, "%s", base);
        else snprintf(name, sizeof name, "%.200s.%d", base, k);
        snprintf(ipath, sizeof ipath, "%s/%s.trashinfo", info, name);
        snprintf(fpath, sizeof fpath, "%s/%s", files, name);
        int fd = open(ipath, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            op_error(op, "Can't trash \"%s\": %s", base, strerror(errno));
            return -1;
        }
        char content[PATH_MAX * 3 + 128];
        int n = snprintf(content, sizeof content, "[Trash Info]\nPath=%s\nDeletionDate=%s\n", enc, date);
        bool ok = write(fd, content, (size_t)n) == n;
        close(fd);
        if (!ok) { unlink(ipath); op_error(op, "Can't write trash info for \"%s\"", base); return -1; }
        if (rename_noreplace(AT_FDCWD, src, AT_FDCWD, fpath) == 0) {
            op_record(op, src, fpath, ipath);
            atomic_fetch_add(&op->files_done, 1);
            return 0;
        }
        int e = errno;
        unlink(ipath);
        if (e == EEXIST) continue;
        op_error(op, "Can't move \"%s\" to the trash: %s", base, strerror(e));
        return -1;
    }
    return -1;
}

// --- op drivers ------------------------------------------------------------

// Where src goes in a copy / move to dst_dir: its parent, base name, whether it's skipped outright (moving onto
// itself) and whether it's copied beside itself (same folder: "name (copy)").
static bool cm_target(Op *op, const char *src, char *parent, size_t pcap, char *base, size_t bcap, bool *beside) {
    path_split(src, parent, pcap, base, bcap);
    bool same_parent = !strcmp(parent, op->dst_dir);
    *beside = op->kind == OP_COPY && same_parent;
    return !(op->kind == OP_MOVE && same_parent);
}

// Before anything is copied: which items are already at the destination (asked about together).
static void cm_find_conflicts(Op *op, int dfd) {
    for (int i = 0; i < op->nsrc && !op_cancelled(op); i++) {
        char parent[PATH_MAX], base[NAME_MAX + 1];
        bool beside;
        struct stat st, ds;
        if (!cm_target(op, op->src[i], parent, sizeof parent, base, sizeof base, &beside) || beside) continue;
        if (lstat(op->src[i], &st) || fstatat(dfd, base, &ds, AT_SYMLINK_NOFOLLOW)) continue;
        if (!(op->ncf & (op->ncf - 1))) { // grow at powers of two
            int cap = op->ncf ? op->ncf * 2 : 4;
            op->cf_names = xrealloc(op->cf_names, (size_t)cap * sizeof(char *));
            op->cf_newer = xrealloc(op->cf_newer, (size_t)cap);
            op->cf_dirs = xrealloc(op->cf_dirs, (size_t)cap * sizeof(bool));
        }
        op->cf_names[op->ncf] = strdup(base);
        op->cf_newer[op->ncf] = ts_newer(&st.st_mtim, &ds.st_mtim) ? 1 : ts_newer(&ds.st_mtim, &st.st_mtim) ? -1 : 0;
        op->cf_dirs[op->ncf] = S_ISDIR(st.st_mode) && S_ISDIR(ds.st_mode);
        op->ncf++;
    }
}

static void run_copy_move(Op *op) {
    int dfd = open(op->dst_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) { op_error(op, "Can't open \"%s\": %s", op->dst_dir, strerror(errno)); return; }
    struct stat dst_st;
    fstat(dfd, &dst_st);
    bool *same_dev = xcalloc((size_t)MAX(op->nsrc, 1), sizeof(bool));
    for (int i = 0; i < op->nsrc && !op_cancelled(op); i++) {
        struct stat st;
        if (lstat(op->src[i], &st)) continue;
        same_dev[i] = st.st_dev == dst_st.st_dev;
        if (op->kind == OP_MOVE && same_dev[i]) atomic_fetch_add(&op->files_total, 1);
        else count_tree(op, AT_FDCWD, op->src[i]);
    }
    cm_find_conflicts(op, dfd);
    if (op_ask_batch(op) != CF_CANCEL) op_wait_turn(op);
    size_t dl = strlen(op->dst_dir);
    int conflicts_seen = 0;

    for (int i = 0; i < op->nsrc && !op_cancelled(op); i++) {
        op_pause_point(op);
        const char *src = op->src[i];
        char parent[PATH_MAX], base[NAME_MAX + 1], name[NAME_MAX + 1], out[PATH_MAX];
        bool beside;
        if (!cm_target(op, src, parent, sizeof parent, base, sizeof base, &beside)) continue;
        struct stat st;
        if (lstat(src, &st)) { op_error(op, "\"%s\" no longer exists", base); continue; }
        bool is_dir = S_ISDIR(st.st_mode);
        // a folder can't go inside itself
        size_t sl = strlen(src);
        if (is_dir && !strncmp(op->dst_dir, src, sl) && (op->dst_dir[sl] == '/' || op->dst_dir[sl] == 0)) {
            op_error(op, "Can't put \"%s\" inside itself", base);
            continue;
        }
        snprintf(name, sizeof name, "%s", base);
        int ow = OW_NONE;
        if (beside) {
            unique_name(dfd, base, is_dir, true, name, sizeof name);
        } else if (exists_at(dfd, name)) {
            struct stat ds;
            fstatat(dfd, name, &ds, AT_SYMLINK_NOFOLLOW);
            bool both_dirs = S_ISDIR(ds.st_mode) && is_dir;
            conflicts_seen++;
            pthread_mutex_lock(&op->mtx);
            op->conflicts_left = MAX(op->ncf - conflicts_seen, 0);
            pthread_mutex_unlock(&op->mtx);
            int a = op_ask_conflict(op, name, both_dirs);
            if (a == CF_CANCEL) break;
            if (a == CF_NEWER) { // folders merge, replacing older files inside; anything else: replace if newer
                if (both_dirs) ow = OW_NEWER;
                else a = ts_newer(&st.st_mtim, &ds.st_mtim) ? CF_REPLACE : CF_SKIP;
            }
            if (a == CF_SKIP) { // no longer part of the totals
                if (op->kind == OP_MOVE && same_dev[i]) atomic_fetch_sub(&op->files_total, 1);
                else count_tree_by(op, AT_FDCWD, src, true);
                continue;
            }
            if (a == CF_KEEP_BOTH) unique_name(dfd, base, is_dir, false, name, sizeof name);
            if (a == CF_RENAME) { // what's there steps aside: "name (2)"
                char old[NAME_MAX + 1];
                unique_name(dfd, name, S_ISDIR(ds.st_mode), false, old, sizeof old);
                if (rename_noreplace(dfd, name, dfd, old)) {
                    op_error(op, "Can't rename the existing \"%s\": %s", name, strerror(errno));
                    continue;
                }
            }
            if (a == CF_REPLACE) {
                if (S_ISDIR(ds.st_mode) != is_dir) {
                    op_error(op, "Can't replace \"%s\": one is a folder and the other isn't", name);
                    continue;
                }
                ow = OW_ALL;
            }
        }
        snprintf(out, sizeof out, "%s%s%s", op->dst_dir, dl > 1 ? "/" : "", name);
        int sp = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (sp < 0) { op_error(op, "Can't open \"%s\": %s", parent, strerror(errno)); continue; }
        op_set_cur(op, base);
        bool need_copy = op->kind == OP_COPY;
        if (op->kind == OP_MOVE) {
            if (ow != OW_NONE && is_dir) {
                need_copy = true; // merge folders: copy over, then remove the source
            } else {
                int r = ow != OW_NONE ? renameat(sp, base, dfd, name) : rename_noreplace(sp, base, dfd, name);
                if (r == 0) {
                    op_record(op, src, out, NULL);
                    atomic_fetch_add(&op->files_done, 1);
                } else if (errno == EXDEV) {
                    need_copy = true;
                    if (same_dev[i]) count_tree(op, sp, base);
                } else {
                    op_error(op, "Can't move \"%s\": %s", base, strerror(errno));
                }
            }
        }
        if (need_copy) {
            int skips = op->inner_skips;
            int r = copy_tree(op, sp, base, dfd, name, ow);
            if (r == 0 && op->kind == OP_MOVE && !op_cancelled(op)) {
                if (op->inner_skips != skips) { // removing the source would lose the files that weren't moved
                    op_error(op, "Left \"%s\" where it was: it has files older than the ones already there, which weren't moved", base);
                    r = -1;
                } else {
                    r = delete_tree(op, sp, base);
                }
            }
            if (r == 0) op_record(op, src, out, NULL);
        }
        close(sp);
    }
    free(same_dev);
    close(dfd);
}

static void run_restore(Op *op) {
    atomic_store(&op->files_total, (u32)op->nsrc);
    atomic_store(&op->phase, OPS_RUNNING);
    for (int i = 0; i < op->nsrc && !op_cancelled(op); i++) {
        const char *from = op->src[i], *to = op->dst[i];
        op_set_cur(op, path_basename(to));
        char parent[PATH_MAX], base[NAME_MAX + 1];
        path_split(to, parent, sizeof parent, base, sizeof base);
        mkdir_p(parent, 0755);
        if (rename_noreplace(AT_FDCWD, from, AT_FDCWD, to) == 0) {
            if (op->aux_in && op->aux_in[i]) unlink(op->aux_in[i]);
            op_record(op, from, to, NULL);
        } else if (errno == EXDEV) {
            char fp[PATH_MAX], fb[NAME_MAX + 1];
            path_split(from, fp, sizeof fp, fb, sizeof fb);
            int sfd = open(fp, O_RDONLY | O_DIRECTORY | O_CLOEXEC), dfd = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (sfd >= 0 && dfd >= 0 && copy_tree(op, sfd, fb, dfd, base, OW_NONE) == 0 && delete_tree(op, sfd, fb) == 0) {
                if (op->aux_in && op->aux_in[i]) unlink(op->aux_in[i]);
                op_record(op, from, to, NULL);
            }
            if (sfd >= 0) close(sfd);
            if (dfd >= 0) close(dfd);
        } else {
            op_error(op, "Can't restore \"%s\": %s", base, errno == EEXIST ? "an item with that name already exists" : strerror(errno));
        }
        atomic_fetch_add(&op->files_done, 1);
    }
}

// Folder sync: each item copied to its own destination, replacing what's there (an older copy) - folders merge.
static void run_sync(Op *op) {
    for (int i = 0; i < op->nsrc && !op_cancelled(op); i++) count_tree(op, AT_FDCWD, op->src[i]);
    op_wait_turn(op);
    for (int i = 0; i < op->nsrc && !op_cancelled(op); i++) {
        char sp[PATH_MAX], sb[NAME_MAX + 1], dp[PATH_MAX], db[NAME_MAX + 1];
        path_split(op->src[i], sp, sizeof sp, sb, sizeof sb);
        path_split(op->dst[i], dp, sizeof dp, db, sizeof db);
        mkdir_p(dp, 0755);
        struct stat ss, ds;
        if (lstat(op->src[i], &ss)) { op_error(op, "\"%s\" no longer exists", sb); continue; }
        if (lstat(op->dst[i], &ds) == 0 && S_ISDIR(ds.st_mode) != S_ISDIR(ss.st_mode)) {
            op_error(op, "Can't replace \"%s\": one is a folder and the other isn't", db);
            continue;
        }
        if (op->trash_first && lstat(op->dst[i], &ds) == 0 && trash_one(op, op->dst[i])) continue;
        int sfd = open(sp, O_RDONLY | O_DIRECTORY | O_CLOEXEC), dfd = open(dp, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (sfd < 0 || dfd < 0) op_error(op, "Can't open \"%s\": %s", sfd < 0 ? sp : dp, strerror(errno));
        else {
            op_set_cur(op, sb);
            if (copy_tree(op, sfd, sb, dfd, db, OW_ALL) == 0) op_record(op, op->src[i], op->dst[i], NULL);
        }
        if (sfd >= 0) close(sfd);
        if (dfd >= 0) close(dfd);
    }
}

// One-item operations (the administrator helper runs them too; the UI does them directly when it may).
static void run_simple(Op *op) {
    atomic_store(&op->files_total, (u32)op->nsrc);
    atomic_store(&op->phase, OPS_RUNNING);
    for (int i = 0; i < op->nsrc && !op_cancelled(op); i++) {
        const char *a = op->src[i], *b = op->dst ? op->dst[i] : NULL;
        op_set_cur(op, path_basename(a));
        int r = 0, fd;
        if (op->kind == OP_RENAME) r = b ? rename_noreplace(AT_FDCWD, a, AT_FDCWD, b) : -1;
        else if (op->kind == OP_MKDIR) r = mkdir(a, 0755);
        else r = (fd = open(a, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0644)) < 0 ? -1 : close(fd);
        if (r) op_error(op, "Can't %s \"%s\": %s", op->kind == OP_RENAME ? "rename" : "create", path_basename(a),
                        errno == EEXIST ? "an item with that name already exists" : strerror(errno));
        else op_record(op, a, b ? b : a, NULL);
        atomic_fetch_add(&op->files_done, 1);
    }
}

// --- administrator helper: the same operations as root, through pkexec ----------------------------------------------
// The app runs `pkexec <this program> --admin-op KIND [--replace] [--dst DIR] -- ARGS...` (ARGS: the sources; for
// sync/rename source and destination pairs). The helper reports on stdout, one line each: "t bytes files" totals,
// "p bytes files" progress, "o in<TAB>out" an item done, "e count<TAB>first error"; its stdin closing means cancel.

static const char *g_admin_kinds[] = {"copy", "move", "trash", "delete", "restore", "extract", "compress", "sync", "rename", "mkdir", "mkfile"};

static void *admin_stdin_watch(void *arg) { // the app went away or cancelled: stop
    Op *op = arg;
    char c;
    while (read(0, &c, 1) > 0) {}
    atomic_store(&op->cancel, 1);
    return NULL;
}

static _Atomic int g_admin_done;

static void *admin_progress(void *arg) {
    Op *op = arg;
    while (!atomic_load(&g_admin_done)) {
        printf("t %llu %u\np %llu %u\n", (unsigned long long)atomic_load(&op->bytes_total), atomic_load(&op->files_total),
               (unsigned long long)atomic_load(&op->bytes_done), atomic_load(&op->files_done));
        fflush(stdout);
        usleep(100000);
    }
    return NULL;
}

static int admin_main(int argc, char **argv) {
    if (argc < 4) return 2;
    int kind = -1;
    for (int k = 0; k < (int)ARRAY_COUNT(g_admin_kinds); k++)
        if (!strcmp(argv[2], g_admin_kinds[k])) kind = k;
    if (kind < 0 || kind == OP_TRASH || kind == OP_RESTORE || kind == OP_EXTRACT || kind == OP_COMPRESS) return 2;
    const char *dst = NULL;
    bool replace = false, verify = false;
    int i = 3;
    for (; i < argc && strcmp(argv[i], "--"); i++) {
        if (!strcmp(argv[i], "--replace")) replace = true;
        else if (!strcmp(argv[i], "--verify")) verify = true;
        else if (!strcmp(argv[i], "--dst") && i + 1 < argc) dst = argv[++i];
    }
    i++;
    bool pairs = kind == OP_SYNC || kind == OP_RENAME;
    int n = argc - i;
    if (n <= 0 || (pairs && n % 2)) return 2;
    if (pairs) n /= 2;
    char **src = xmalloc((size_t)n * sizeof(char *)), **dv = pairs ? xmalloc((size_t)n * sizeof(char *)) : NULL;
    for (int k = 0; k < n; k++) {
        src[k] = argv[i + (pairs ? 2 * k : k)];
        if (pairs) dv[k] = argv[i + 2 * k + 1];
    }
    static Op *g_admin_op; // (lives until exit: the stdin watcher may still look at it)
    Op *op = g_admin_op = op_new(kind, src, n, dst);
    if (pairs) op->dst = strv_dup(dv, n);
    free(src);
    free(dv);
    op->policy = replace ? CF_REPLACE : CF_SKIP; // nobody to ask
    op->verify = verify;
    pthread_t tw, tp;
    pthread_create(&tw, NULL, admin_stdin_watch, op);
    pthread_detach(tw);
    pthread_create(&tp, NULL, admin_progress, op);
    switch (kind) {
    case OP_COPY: case OP_MOVE: run_copy_move(op); break;
    case OP_SYNC: run_sync(op); break;
    case OP_DELETE:
        for (int k = 0; k < op->nsrc && !op_cancelled(op); k++) count_tree(op, AT_FDCWD, op->src[k]);
        atomic_store(&op->phase, OPS_RUNNING);
        for (int k = 0; k < op->nsrc && !op_cancelled(op); k++)
            if (delete_tree(op, AT_FDCWD, op->src[k]) == 0) op_record(op, op->src[k], NULL, NULL);
        break;
    default: run_simple(op); break;
    }
    atomic_store(&g_admin_done, 1);
    pthread_join(tp, NULL);
    printf("t %llu %u\np %llu %u\n", (unsigned long long)atomic_load(&op->bytes_total), atomic_load(&op->files_total),
           (unsigned long long)atomic_load(&op->bytes_done), atomic_load(&op->files_done));
    for (int k = 0; k < op->nout; k++) printf("o %s\t%s\n", op->in[k] ? op->in[k] : "", op->out[k] ? op->out[k] : "");
    if (op->nerr) printf("e %d\t%s\n", op->nerr, op->err);
    fflush(stdout);
    return op_cancelled(op) ? 1 : 0;
}

// The app side: run the operation in the helper and mirror what it reports into this Op.
static void run_admin(Op *op) {
    char exe[PATH_MAX];
    ssize_t el = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (el <= 0) { op_error(op, "Can't find this program to run it as administrator"); return; }
    exe[el] = 0;
    const char *pk = getenv("FE_PKEXEC") ? getenv("FE_PKEXEC") : "pkexec";
    bool pairs = op->kind == OP_SYNC || op->kind == OP_RENAME;
    int na = 0;
    char **av = xmalloc((size_t)(op->nsrc * 2 + 16) * sizeof(char *));
    // under sh, which prints pkexec's exit code last: SIGCHLD is ignored app-wide, so waitpid can't tell it
    av[na++] = "sh";
    av[na++] = "-c";
    av[na++] = "\"$@\"; printf '\\n@rc=%d\\n' $?";
    av[na++] = "sh";
    av[na++] = (char *)pk;
    av[na++] = exe;
    av[na++] = "--admin-op";
    av[na++] = (char *)g_admin_kinds[op->kind];
    if (op->policy == CF_REPLACE) av[na++] = "--replace";
    if (atomic_load(&op->verify)) av[na++] = "--verify";
    if (op->dst_dir) { av[na++] = "--dst"; av[na++] = op->dst_dir; }
    av[na++] = "--";
    for (int i = 0; i < op->nsrc; i++) {
        av[na++] = op->src[i];
        if (pairs) av[na++] = op->dst[i];
    }
    av[na] = NULL;
    int in[2], out[2];
    if (pipe2(in, O_CLOEXEC) || pipe2(out, O_CLOEXEC)) { op_error(op, "Can't start: %s", strerror(errno)); free(av); return; }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in[0], 0);
    posix_spawn_file_actions_adddup2(&fa, out[1], 1);
    pid_t pid;
    int rc = posix_spawnp(&pid, "sh", &fa, spawn_attr(), av, environ);
    posix_spawn_file_actions_destroy(&fa);
    free(av);
    close(in[0]);
    close(out[1]);
    if (rc) {
        op_error(op, "Can't run %s (%s): it comes with polkit", pk, strerror(rc));
        close(in[1]);
        close(out[0]);
        return;
    }
    atomic_store(&op->phase, OPS_RUNNING);
    FILE *f = fdopen(out[0], "r");
    char line[2 * PATH_MAX + 64];
    int herr = 0, code = -1;
    char herrmsg[512] = "";
    while (f && fgets(line, sizeof line, f)) {
        if (op_cancelled(op) && in[1] >= 0) { close(in[1]); in[1] = -1; } // the helper stops when its stdin closes
        line[strcspn(line, "\n")] = 0;
        unsigned long long a;
        unsigned b;
        if (line[0] == 't' && sscanf(line + 1, "%llu %u", &a, &b) == 2) { atomic_store(&op->bytes_total, a); atomic_store(&op->files_total, b); }
        else if (line[0] == 'p' && sscanf(line + 1, "%llu %u", &a, &b) == 2) { atomic_store(&op->bytes_done, a); atomic_store(&op->files_done, b); }
        else if (!strncmp(line, "@rc=", 4)) code = atoi(line + 4);
        else if (line[0] == 'o' && line[1] == ' ') {
            char *tab = strchr(line + 2, '\t');
            if (tab) { *tab = 0; op_record(op, line + 2, tab[1] ? tab + 1 : NULL, NULL); }
        } else if (line[0] == 'e' && line[1] == ' ') {
            char *tab = strchr(line + 2, '\t');
            herr = atoi(line + 2);
            if (tab) snprintf(herrmsg, sizeof herrmsg, "%s", tab + 1);
        }
    }
    if (f) fclose(f);
    if (in[1] >= 0) close(in[1]);
    waitpid(pid, NULL, 0); // (usually already gone: ECHILD)
    if (code == 127 && !herr && !in_path(pk)) { op_error(op, "Can't run %s: it comes with polkit", pk); return; }
    if (herr) {
        pthread_mutex_lock(&op->mtx);
        if (!op->nerr) snprintf(op->err, sizeof op->err, "%s", herrmsg);
        op->nerr += herr;
        pthread_mutex_unlock(&op->mtx);
    } else if (code == 126 || code == 127) op_error(op, "Not authorized (the password prompt was cancelled or refused, or no polkit agent is running to show it)");
    else if (code != 0 && !op_cancelled(op)) op_error(op, "The administrator helper failed (exit code %d)", code);
}

static void *op_thread(void *arg) {
    Op *op = arg;
    if (op->admin) { run_admin(op); goto done; }
    switch (op->kind) {
    case OP_COPY:
    case OP_MOVE: run_copy_move(op); break;
    case OP_TRASH:
        atomic_store(&op->files_total, (u32)op->nsrc);
        atomic_store(&op->phase, OPS_RUNNING);
        for (int i = 0; i < op->nsrc && !op_cancelled(op); i++) { op_pause_point(op); trash_one(op, op->src[i]); }
        break;
    case OP_DELETE:
        for (int i = 0; i < op->nsrc && !op_cancelled(op); i++) count_tree(op, AT_FDCWD, op->src[i]);
        atomic_store(&op->phase, OPS_RUNNING);
        for (int i = 0; i < op->nsrc && !op_cancelled(op); i++) {
            if (delete_tree(op, AT_FDCWD, op->src[i]) == 0) {
                if (op->aux_in && op->aux_in[i]) unlink(op->aux_in[i]); // trash items: drop their .trashinfo
                op_record(op, op->src[i], NULL, NULL);
            }
        }
        break;
    case OP_RESTORE: run_restore(op); break;
    case OP_SYNC: run_sync(op); break;
    case OP_RENAME: case OP_MKDIR: case OP_MKFILE: run_simple(op); break;
    case OP_EXTRACT: run_extract(op); break;
    case OP_COMPRESS: run_compress(op); break;
    }
done:
    op->t_end = now_ns();
    atomic_store(&op->phase, OPS_DONE);
    main_post(op_finished, op);
    return NULL;
}

static void op_launch(Op *op) {
    op->t_start = now_ns();
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &attr, op_thread, op);
    pthread_setname_np(t, "fe-op");
    pthread_attr_destroy(&attr);
}
