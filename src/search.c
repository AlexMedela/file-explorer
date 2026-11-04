// search.c - fuzzy matching, folder index ("go to anywhere"), recursive search,
// recursive folder sizes, and inotify change notifications.

#include <fnmatch.h>
#include <sys/inotify.h>
#include <sys/resource.h>
#include <sys/sysmacros.h>

// Lower a background thread's CPU and I/O priority so it never competes with the UI.
static void thread_background_priority(void) {
    pid_t tid = (pid_t)syscall(SYS_gettid);
    setpriority(PRIO_PROCESS, (id_t)tid, 10);
    syscall(SYS_ioprio_set, 1 /* IOPRIO_WHO_PROCESS */, tid, 3 << 13 /* IOPRIO_CLASS_IDLE */);
}


// --- fuzzy matching --------------------------------------------------------

#define FZ_MAXPOS 32

// Subsequence match of q in s (case-insensitive), scored like fzf v1: rewards matches at
// word/path boundaries and consecutive runs, penalizes gaps and long strings.
// Returns -1 if q doesn't match. pos receives up to FZ_MAXPOS matched indices.
static int fuzzy_match(const char *s, int n, const char *q, int qn, u16 *pos) {
    if (qn <= 0) return 0;
    int qi = 0, end = -1;
    for (int i = 0; i < n; i++) {
        if (ascii_lower((u8)s[i]) == ascii_lower((u8)q[qi]) && ++qi == qn) { end = i; break; }
    }
    if (end < 0) return -1;
    qi = qn - 1;
    int start = end;
    for (int i = end; i >= 0; i--) {
        if (ascii_lower((u8)s[i]) == ascii_lower((u8)q[qi]) && --qi < 0) { start = i; break; }
    }
    int score = 0, last = -2;
    qi = 0;
    for (int i = start; i <= end && qi < qn; i++) {
        if (ascii_lower((u8)s[i]) != ascii_lower((u8)q[qi])) continue;
        int b = 16;
        char prev = i ? s[i - 1] : '/';
        if (prev == '/') b += 30;
        else if (prev == ' ' || prev == '_' || prev == '-' || prev == '.') b += 20;
        else if (prev >= 'a' && prev <= 'z' && s[i] >= 'A' && s[i] <= 'Z') b += 16;
        if (last == i - 1) b += 18;
        if (s[i] == q[qi]) b += 1;
        score += b;
        if (pos && qi < FZ_MAXPOS) pos[qi] = (u16)i;
        last = i;
        qi++;
    }
    score -= (end - start + 1 - qn);
    score -= n / 4;
    return MAX(score, 1);
}

// How many leading chars of q appear in order in s (case-insensitive); == qn means q is a subsequence.
static int fz_consume(const char *s, const char *q, int qn) {
    int qi = 0;
    for (; *s && qi < qn; s++)
        if (ascii_lower((u8)*s) == ascii_lower((u8)q[qi])) qi++;
    return qi;
}

// Path scoring: a match inside the last component beats one spread over the path.
static int fuzzy_path(const char *path, const char *q, int qn, u16 *pos, int *npos) {
    int n = (int)strlen(path);
    const char *base = strrchr(path, '/');
    int boff = base && base[1] ? (int)(base - path) + 1 : 0;
    int s = fuzzy_match(path + boff, n - boff, q, qn, pos);
    if (s >= 0) {
        if (pos)
            for (int i = 0; i < MIN(qn, FZ_MAXPOS); i++) pos[i] = (u16)(pos[i] + boff);
        s += 60;
    } else {
        s = fuzzy_match(path, n, q, qn, pos);
    }
    if (npos) *npos = s >= 0 ? MIN(qn, FZ_MAXPOS) : 0;
    return s;
}

// --- folder index (all folders and files under $HOME and mounts) ----------

// Folders are stored as full paths; files as their name plus the index of their folder
// (parent[i]), which keeps a home with a million files to a few tens of MB.
#define INDEX_NOPARENT 0xFFFFFFFFu

typedef struct DirIndex {
    _Atomic int refs;
    char *buf;
    size_t len, cap;
    u32 *offs, *parent; // parent = INDEX_NOPARENT for folders
    u32 n, capn, nfiles;
    u64 t_built;
} DirIndex;

static pthread_mutex_t g_index_mtx = PTHREAD_MUTEX_INITIALIZER;
static DirIndex *g_index;
static _Atomic int g_index_building;

static DirIndex *index_acquire(void) {
    pthread_mutex_lock(&g_index_mtx);
    DirIndex *d = g_index;
    if (d) atomic_fetch_add(&d->refs, 1);
    pthread_mutex_unlock(&g_index_mtx);
    return d;
}

static void index_release(DirIndex *d) {
    if (!d || atomic_fetch_sub(&d->refs, 1) != 1) return;
    free(d->buf);
    free(d->offs);
    free(d->parent);
    free(d);
}

static bool index_is_file(const DirIndex *d, u32 i) { return d->parent[i] != INDEX_NOPARENT; }

// Full path of entry i (folders point into the index, files are joined into out).
static const char *index_path(const DirIndex *d, u32 i, char *out, size_t cap) {
    if (!index_is_file(d, i)) return d->buf + d->offs[i];
    path_join(out, cap, d->buf + d->offs[d->parent[i]], d->buf + d->offs[i]);
    return out;
}

static u32 index_add(DirIndex *d, const char *s, u32 parent) {
    size_t l = strlen(s) + 1;
    if (d->len + l > UINT32_MAX) return INDEX_NOPARENT;
    if (d->len + l > d->cap) {
        d->cap = MAX(d->cap * 2, d->len + l + (1u << 20));
        d->buf = xrealloc(d->buf, d->cap);
    }
    if (d->n == d->capn) {
        d->capn = d->capn ? d->capn * 2 : 65536;
        d->offs = xrealloc(d->offs, d->capn * sizeof(u32));
        d->parent = xrealloc(d->parent, d->capn * sizeof(u32));
    }
    memcpy(d->buf + d->len, s, l);
    d->offs[d->n] = (u32)d->len;
    d->parent[d->n] = parent;
    d->len += l;
    if (parent != INDEX_NOPARENT) d->nfiles++;
    return d->n++;
}

static bool index_prune(const char *name) {
    if (name[0] == '.') return true; // hidden trees (caches, git objects, ...)
    return !strcmp(name, "node_modules") || !strcmp(name, "__pycache__");
}

#define INDEX_MAX 400000         // folders
#define INDEX_MAX_FILES 2000000

typedef struct {
    char **roots;
    int nroots;
} IndexJob;

static void *index_thread(void *arg) {
    IndexJob *job = arg;
    thread_background_priority();
    DirIndex *d = xcalloc(1, sizeof *d);
    atomic_init(&d->refs, 1);
    int cap = 4096, top = 0;
    char **stack = xmalloc((size_t)cap * sizeof(char *));
    for (int i = job->nroots - 1; i >= 0; i--) stack[top++] = strdup(job->roots[i]);
    while (top && d->n - d->nfiles < INDEX_MAX) {
        char *dir = stack[--top];
        u32 di = index_add(d, dir, INDEX_NOPARENT);
        int fd = di == INDEX_NOPARENT ? -1 : open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        DIR *dp = fd >= 0 ? fdopendir(fd) : NULL;
        if (!dp && fd >= 0) close(fd);
        if (dp) {
            struct dirent *e;
            size_t dl = strlen(dir);
            while ((e = readdir(dp))) {
                if (is_dot_or_dotdot(e->d_name) || index_prune(e->d_name)) continue;
                bool isdir = e->d_type == DT_DIR;
                if (e->d_type == DT_UNKNOWN) { // some filesystems (NFS, older XFS) don't fill d_type
                    struct stat st;
                    isdir = fstatat(fd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(st.st_mode);
                }
                if (!isdir) {
                    if (d->nfiles < INDEX_MAX_FILES) index_add(d, e->d_name, di);
                    continue;
                }
                if (top == cap) { cap *= 2; stack = xrealloc(stack, (size_t)cap * sizeof(char *)); }
                char *p = xmalloc(dl + strlen(e->d_name) + 2);
                sprintf(p, "%s%s%s", dir, dl > 1 ? "/" : "", e->d_name);
                stack[top++] = p;
            }
            closedir(dp);
        }
        free(dir);
    }
    while (top) free(stack[--top]);
    free(stack);
    strv_free(job->roots, job->nroots);
    free(job);
    d->t_built = now_ns();
    pthread_mutex_lock(&g_index_mtx);
    DirIndex *old = g_index;
    g_index = d;
    pthread_mutex_unlock(&g_index_mtx);
    index_release(old);
    atomic_store(&g_index_building, 0);
    return NULL;
}

static void index_rebuild(char **roots, int nroots) {
    if (atomic_exchange(&g_index_building, 1)) return;
    IndexJob *job = xcalloc(1, sizeof *job);
    job->roots = strv_dup(roots, nroots);
    job->nroots = nroots;
    pthread_t t;
    pthread_create(&t, NULL, index_thread, job);
    pthread_setname_np(t, "fe-index");
    pthread_detach(t);
}

// --- recursive search ------------------------------------------------------

typedef struct {
    u32 rel; // offset of the relative path in the batch's strs
    u8 flags, mflags;
    u16 mode;
    u32 uid;
    u64 size;
    i64 mtime, btime;
} SearchHit;

typedef struct {
    Listing *L;
    SearchHit *hits;
    char *strs; // the hits' relative paths, NUL-separated: one allocation per batch, not per hit
    int n;
    bool done;
} SearchBatch;

typedef struct {
    Listing *L;
    char root[PATH_MAX];
    char q[256];
    bool glob, hidden, recursive;
    dev_t dev;
    int rootfd; // folders are opened relative to it: the kernel doesn't walk the root's path every time
    pthread_mutex_t mtx;
    pthread_cond_t cv;
    char **queue;
    int qn, qcap, active;
    _Atomic int alive;
} SearchCtx;

static void search_batch_cb(void *arg); // app.c

typedef struct {
    SearchHit *hits;
    int n, cap;
    char *strs;
    u32 slen, scap;
} SearchOut;

static void search_flush(SearchCtx *c, SearchOut *o, bool done) {
    if (!o->n && !done) return;
    SearchBatch *b = xcalloc(1, sizeof *b);
    b->L = c->L;
    listing_retain(c->L);
    b->hits = o->hits;
    b->strs = o->strs;
    b->n = o->n;
    b->done = done;
    *o = (SearchOut){0};
    main_post(search_batch_cb, b);
}

static bool search_match(SearchCtx *c, const char *name) { return name_matches(c->q, name, (int)strlen(name)); }

static void *search_worker(void *arg) {
    SearchCtx *c = arg;
    thread_background_priority();
    SearchOut out = {0};
    u64 last_flush = now_ns();
    char *buf = xmalloc(256 * 1024);
    for (;;) {
        pthread_mutex_lock(&c->mtx);
        while (!c->qn && c->active && !atomic_load(&c->L->cancel)) pthread_cond_wait(&c->cv, &c->mtx);
        if (!c->qn || atomic_load(&c->L->cancel)) {
            pthread_cond_broadcast(&c->cv);
            pthread_mutex_unlock(&c->mtx);
            break;
        }
        char *rel = c->queue[--c->qn];
        c->active++;
        pthread_mutex_unlock(&c->mtx);

        int fd = openat(c->rootfd, rel[0] ? rel : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd >= 0) {
            atomic_fetch_add(&c->L->search_dirs, 1);
            for (;;) {
                if (atomic_load_explicit(&c->L->cancel, memory_order_relaxed)) break;
                long nread = syscall(SYS_getdents64, fd, buf, 256 * 1024);
                if (nread <= 0) break;
                for (long off = 0; off < nread;) {
                    struct linux_dirent64_ *d = (struct linux_dirent64_ *)(buf + off);
                    off += d->d_reclen;
                    const char *nm = d->d_name;
                    if (is_dot_or_dotdot(nm)) continue;
                    bool hidden = nm[0] == '.';
                    if (hidden && !c->hidden) continue;
                    bool match = search_match(c, nm);
                    bool is_dir = d->d_type == DT_DIR;
                    if (!match && !is_dir && d->d_type != DT_UNKNOWN) continue;
                    struct statx sx;
                    bool have = statx(fd, nm, AT_SYMLINK_NOFOLLOW | AT_STATX_DONT_SYNC | AT_NO_AUTOMOUNT,
                                      STATX_TYPE | STATX_MODE | STATX_SIZE | STATX_MTIME | STATX_UID | STATX_BTIME, &sx) == 0;
                    if (have && d->d_type == DT_UNKNOWN) is_dir = S_ISDIR(sx.stx_mode);
                    char r2[PATH_MAX];
                    if (rel[0]) snprintf(r2, sizeof r2, "%s/%s", rel, nm);
                    else snprintf(r2, sizeof r2, "%s", nm);
                    if (match) {
                        if (out.n == out.cap) {
                            out.cap = out.cap ? out.cap * 2 : 64;
                            out.hits = xrealloc(out.hits, (size_t)out.cap * sizeof(SearchHit));
                        }
                        u32 l = (u32)strlen(r2) + 1;
                        if (out.slen + l > out.scap) {
                            out.scap = MAX(out.scap * 2, out.slen + l + 16384);
                            out.strs = xrealloc(out.strs, out.scap);
                        }
                        SearchHit *h = &out.hits[out.n++];
                        memcpy(out.strs + out.slen, r2, l);
                        h->rel = out.slen;
                        out.slen += l;
                        h->flags = (is_dir ? ENT_DIR : 0) | (hidden ? ENT_HIDDEN : 0) | (d->d_type == DT_LNK ? ENT_LINK : 0);
                        h->mflags = have && S_ISREG(sx.stx_mode) && (sx.stx_mode & 0111) ? MF_EXEC : 0;
                        h->size = have ? sx.stx_size : 0;
                        h->mtime = have ? sx.stx_mtime.tv_sec : 0;
                        h->btime = have && (sx.stx_mask & STATX_BTIME) ? sx.stx_btime.tv_sec : 0;
                        h->mode = have ? sx.stx_mode : 0;
                        h->uid = have ? sx.stx_uid : 0;
                    }
                    // descend, staying on the same filesystem
                    if (c->recursive && is_dir && have && makedev(sx.stx_dev_major, sx.stx_dev_minor) == c->dev) {
                        pthread_mutex_lock(&c->mtx);
                        if (c->qn == c->qcap) {
                            c->qcap = c->qcap ? c->qcap * 2 : 256;
                            c->queue = xrealloc(c->queue, (size_t)c->qcap * sizeof(char *));
                        }
                        c->queue[c->qn++] = strdup(r2);
                        pthread_cond_signal(&c->cv);
                        pthread_mutex_unlock(&c->mtx);
                    }
                }
                u64 t = now_ns();
                if (out.n >= 512 || (out.n && t - last_flush > 60000000ull)) {
                    search_flush(c, &out, false);
                    last_flush = t;
                }
            }
            close(fd);
        }
        free(rel);
        pthread_mutex_lock(&c->mtx);
        c->active--;
        if (!c->qn && !c->active) pthread_cond_broadcast(&c->cv);
        pthread_mutex_unlock(&c->mtx);
    }
    free(buf);
    search_flush(c, &out, false);
    if (atomic_fetch_sub(&c->alive, 1) == 1) { // last worker out
        search_flush(c, &out, true);
        for (int i = 0; i < c->qn; i++) free(c->queue[i]);
        free(c->queue);
        if (c->rootfd >= 0) close(c->rootfd);
        pthread_mutex_destroy(&c->mtx);
        pthread_cond_destroy(&c->cv);
        listing_release(c->L);
        free(c);
    }
    return NULL;
}

static void search_start(Listing *L, const char *root, const char *q, bool hidden, bool recursive) {
    SearchCtx *c = xcalloc(1, sizeof *c);
    c->L = L;
    listing_retain(L);
    snprintf(c->root, sizeof c->root, "%s", root);
    snprintf(c->q, sizeof c->q, "%s", q);
    c->glob = strpbrk(q, "*?[") != NULL;
    c->hidden = hidden;
    c->recursive = recursive;
    struct stat st;
    c->rootfd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    c->dev = c->rootfd >= 0 && fstat(c->rootfd, &st) == 0 ? st.st_dev : 0;
    pthread_mutex_init(&c->mtx, NULL);
    pthread_cond_init(&c->cv, NULL);
    c->qcap = 256;
    c->queue = xmalloc((size_t)c->qcap * sizeof(char *));
    c->queue[c->qn++] = strdup("");
    int n = recursive ? CLAMP(g_nworkers, 2, 8) : 1;
    atomic_store(&c->alive, n);
    for (int i = 0; i < n; i++) {
        pthread_t t;
        pthread_create(&t, NULL, search_worker, c);
        pthread_setname_np(t, "fe-search");
        pthread_detach(t);
    }
}

// --- contents search (text inside files: ripgrep when installed, else built in) ------------------------------
// Results stream to the main thread in batches: one record per matching file plus its matching lines (at most
// CS_FILE_LINES each, CS_MAX_LINES in all). ripgrep also skips what .gitignore excludes; the built-in search skips
// .git, binary files and files over 50 MB.

#define CS_FILE_LINES 100
#define CS_MAX_LINES 20000
#define CS_MAX_FILE (50ull << 20)
#define CS_TEXT_MAX 300 // bytes of a line kept around the match

// Smart case: case-insensitive unless the query has a capital (or match case is on).
static bool cq_init(CQuery *c, const char *q, bool match_case, bool word, bool regex) {
    memset(c, 0, sizeof *c);
    snprintf(c->q, sizeof c->q, "%s", q);
    bool upper = false;
    for (const char *k = q; *k; k++) upper |= *k >= 'A' && *k <= 'Z';
    c->icase = !match_case && !upper;
    c->word = word;
    c->regex = regex;
    if (!q[0]) return false;
    if (regex) {
        char pat[300];
        snprintf(pat, sizeof pat, word ? "\\b(%s)\\b" : "%s", q);
        c->re_ok = regcomp(&c->re, pat, REG_EXTENDED | REG_NEWLINE | (c->icase ? REG_ICASE : 0)) == 0;
        return c->re_ok;
    }
    return true;
}

static void cq_free(CQuery *c) {
    if (c->re_ok) regfree(&c->re);
    c->re_ok = false;
}

static inline bool cq_wordch(u8 ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch >= 0x80; }

// First match in s[from..n): start and length. s needs no terminator (regexes use REG_STARTEND).
static bool cq_find(const CQuery *c, const char *s, size_t n, size_t from, size_t *ms, size_t *ml) {
    if (from > n) return false;
    if (c->regex) {
        if (!c->re_ok) return false;
        regmatch_t m = {(regoff_t)from, (regoff_t)n};
        if (regexec(&c->re, s, 1, &m, REG_STARTEND | (from ? REG_NOTBOL : 0))) return false;
        *ms = (size_t)m.rm_so;
        *ml = (size_t)(m.rm_eo - m.rm_so);
        return true;
    }
    size_t ql = strlen(c->q);
    if (!ql || ql > n - from) return false;
    u8 f0 = (u8)c->q[0], lo = (u8)ascii_lower(f0), up = (u8)toupper(f0);
    for (size_t i = from; i + ql <= n;) {
        const char *hit;
        if (c->icase && lo != up) { // the next occurrence of either case of the first letter
            const char *a = memchr(s + i, lo, n - ql + 1 - i), *b = memchr(s + i, up, n - ql + 1 - i);
            hit = !a ? b : !b ? a : MIN(a, b);
        } else {
            hit = memchr(s + i, f0, n - ql + 1 - i);
        }
        if (!hit) return false;
        size_t at = (size_t)(hit - s);
        bool eq = c->icase ? !strncasecmp(hit, c->q, ql) : !memcmp(hit, c->q, ql);
        if (eq && c->word && ((at > 0 && cq_wordch((u8)s[at - 1]) && cq_wordch((u8)c->q[0])) ||
                              (at + ql < n && cq_wordch((u8)s[at + ql]) && cq_wordch((u8)c->q[ql - 1]))))
            eq = false;
        if (eq) { *ms = at; *ml = ql; return true; }
        i = at + 1;
    }
    return false;
}

typedef struct { u32 rel; u8 flags, mflags; u16 mode; u32 uid; u64 size; i64 mtime, btime; u32 l0, nl; } CHitFile;
typedef struct { u32 line, col, len, text, rcol; bool elided; } CHitLine; // col: in the kept text, rcol: in the line

typedef struct ContentBatch {
    Listing *L;
    CHitFile *f;
    u32 nf, fcap;
    CHitLine *l;
    u32 nl, lcap;
    char *strs;
    u32 slen, scap;
    bool done, trunc;
    u64 t; // when it was started (flush timing)
} ContentBatch;

typedef struct {
    Listing *L;
    char root[PATH_MAX];
    CQuery cq;
    char glob[128]; // only files matching it ("" = all)
    bool hidden, recursive, use_rg;
    dev_t dev;
    int rootfd;
    pthread_mutex_t mtx;
    pthread_cond_t cv;
    char **queue;
    int qn, qcap, active;
    _Atomic int alive;
    _Atomic u32 lines;
    _Atomic int trunc;
} ContentCtx;

static void content_batch_cb(void *arg); // app.c

static u32 cb_str(ContentBatch *b, const char *s, size_t n) {
    if (b->slen + n + 1 > b->scap) {
        b->scap = MAX(b->scap * 2, b->slen + (u32)n + 1 + 16384);
        b->strs = xrealloc(b->strs, b->scap);
    }
    u32 at = b->slen;
    memcpy(b->strs + at, s, n);
    b->strs[at + n] = 0;
    b->slen += (u32)n + 1;
    return at;
}

static void cb_flush(ContentCtx *c, ContentBatch **bp, bool done) {
    ContentBatch *b = *bp;
    if (!b) {
        if (!done) return;
        b = xcalloc(1, sizeof *b);
    }
    if (!b->nf && !done) return;
    b->L = c->L;
    listing_retain(c->L);
    b->done = done;
    b->trunc = atomic_load(&c->trunc);
    *bp = NULL;
    main_post(content_batch_cb, b);
}

static CHitFile *cb_file(ContentBatch **bp, const char *rel, const struct statx *sx, bool hidden) {
    if (!*bp) { *bp = xcalloc(1, sizeof **bp); (*bp)->t = now_ns(); }
    ContentBatch *b = *bp;
    if (b->nf == b->fcap) { b->fcap = b->fcap ? b->fcap * 2 : 64; b->f = xrealloc(b->f, b->fcap * sizeof *b->f); }
    CHitFile *f = &b->f[b->nf++];
    memset(f, 0, sizeof *f);
    f->rel = cb_str(b, rel, strlen(rel));
    f->flags = hidden ? ENT_HIDDEN : 0;
    if (sx) {
        f->mode = sx->stx_mode;
        f->uid = sx->stx_uid;
        f->size = sx->stx_size;
        f->mtime = sx->stx_mtime.tv_sec;
        f->btime = (sx->stx_mask & STATX_BTIME) ? sx->stx_btime.tv_sec : 0;
        f->mflags = (sx->stx_mode & 0111) ? MF_EXEC : 0;
    }
    f->l0 = b->nl;
    return f;
}

// Add one matching line (s[0..n), match at col, length len) to the file just added. False: the result limit is hit.
static bool cb_line(ContentCtx *c, ContentBatch *b, u32 lineno, const char *s, size_t n, size_t col, size_t len) {
    if (atomic_fetch_add(&c->lines, 1) >= CS_MAX_LINES) { atomic_store(&c->trunc, 1); return false; }
    if (b->nl == b->lcap) { b->lcap = b->lcap ? b->lcap * 2 : 256; b->l = xrealloc(b->l, b->lcap * sizeof *b->l); }
    CHitLine *h = &b->l[b->nl++];
    size_t a = 0;
    while (a < n && (s[a] == ' ' || s[a] == '\t') && a < col) a++; // indentation isn't interesting
    bool elided = false;
    if (col > a + 80) { a = col - 60; elided = true; } // a match far to the right: show what's around it
    while (a < col && ((u8)s[a] & 0xC0) == 0x80) a++; // don't start inside a UTF-8 sequence
    size_t e = MIN(n, a + CS_TEXT_MAX);
    while (e > col + len && e < n && ((u8)s[e] & 0xC0) == 0x80) e--;
    char tmp[CS_TEXT_MAX + 8];
    size_t k = 0;
    for (size_t i = a; i < e && k < CS_TEXT_MAX; i++) tmp[k++] = (u8)s[i] < 0x20 ? ' ' : s[i]; // tabs, control chars
    h->text = cb_str(b, tmp, k);
    h->line = lineno;
    h->rcol = (u32)col;
    h->col = (u32)(col >= a ? col - a : 0);
    h->len = (u32)MIN(len, k > h->col ? k - h->col : 0);
    h->elided = elided;
    b->f[b->nf - 1].nl++;
    return true;
}

// Search one file's bytes; adds the file and its lines to the batch when it matches.
static void content_scan(ContentCtx *c, ContentBatch **bp, const char *rel, const char *buf, size_t n, const struct statx *sx, bool hidden) {
    if (memchr(buf, 0, MIN(n, (size_t)65536))) return; // binary
    size_t ms, ml, from = 0, line_start = 0;
    u32 lineno = 1;
    bool added = false;
    int nlines = 0;
    while (nlines < CS_FILE_LINES && cq_find(&c->cq, buf, n, from, &ms, &ml)) {
        for (const char *q = buf + line_start; (q = memchr(q, '\n', (size_t)(buf + ms - q))); q++) { // line number
            lineno++;
            line_start = (size_t)(q - buf) + 1;
        }
        const char *eol = memchr(buf + ms, '\n', n - ms);
        size_t le = eol ? (size_t)(eol - buf) : n;
        if (!added) { cb_file(bp, rel, sx, hidden); added = true; }
        if (!cb_line(c, *bp, lineno, buf + line_start, le - line_start, ms - line_start, ml)) {
            if (!(*bp)->f[(*bp)->nf - 1].nl) (*bp)->nf--; // the limit came first: no empty file record
            break;
        }
        nlines++;
        from = le + 1; // one row per line
        if (from > n) break;
    }
}

static bool content_glob_ok(const ContentCtx *c, const char *rel, const char *base) {
    if (!c->glob[0]) return true;
    return fnmatch(c->glob, strchr(c->glob, '/') ? rel : base, FNM_CASEFOLD) == 0;
}

static void content_ctx_release(ContentCtx *c) {
    if (atomic_fetch_sub(&c->alive, 1) != 1) return;
    ContentBatch *b = NULL;
    cb_flush(c, &b, true);
    for (int i = 0; i < c->qn; i++) free(c->queue[i]);
    free(c->queue);
    if (c->rootfd >= 0) close(c->rootfd);
    cq_free(&c->cq);
    pthread_mutex_destroy(&c->mtx);
    pthread_cond_destroy(&c->cv);
    listing_release(c->L);
    free(c);
}

static void *content_worker(void *arg) { // built-in search: folders from a shared queue, like search_worker
    ContentCtx *c = arg;
    thread_background_priority();
    ContentBatch *b = NULL;
    char *dbuf = xmalloc(256 * 1024);
    size_t fcap = 1 << 20;
    char *fbuf = xmalloc(fcap);
    for (;;) {
        pthread_mutex_lock(&c->mtx);
        while (!c->qn && c->active && !atomic_load(&c->L->cancel) && !atomic_load(&c->trunc)) pthread_cond_wait(&c->cv, &c->mtx);
        if (!c->qn || atomic_load(&c->L->cancel) || atomic_load(&c->trunc)) {
            pthread_cond_broadcast(&c->cv);
            pthread_mutex_unlock(&c->mtx);
            break;
        }
        char *rel = c->queue[--c->qn];
        c->active++;
        pthread_mutex_unlock(&c->mtx);
        int fd = openat(c->rootfd, rel[0] ? rel : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd >= 0) {
            atomic_fetch_add(&c->L->search_dirs, 1);
            for (;;) {
                if (atomic_load_explicit(&c->L->cancel, memory_order_relaxed) || atomic_load(&c->trunc)) break;
                long nread = syscall(SYS_getdents64, fd, dbuf, 256 * 1024);
                if (nread <= 0) break;
                for (long off = 0; off < nread;) {
                    struct linux_dirent64_ *d = (struct linux_dirent64_ *)(dbuf + off);
                    off += d->d_reclen;
                    const char *nm = d->d_name;
                    if (is_dot_or_dotdot(nm) || d->d_type == DT_LNK) continue;
                    bool hidden = nm[0] == '.';
                    if ((hidden && !c->hidden) || !strcmp(nm, ".git")) continue;
                    char r2[PATH_MAX];
                    if (rel[0]) snprintf(r2, sizeof r2, "%s/%s", rel, nm);
                    else snprintf(r2, sizeof r2, "%s", nm);
                    bool is_dir = d->d_type == DT_DIR;
                    if (!is_dir && d->d_type != DT_REG && d->d_type != DT_UNKNOWN) continue;
                    if (!is_dir && !content_glob_ok(c, r2, nm)) continue;
                    struct statx sx;
                    if (statx(fd, nm, AT_SYMLINK_NOFOLLOW | AT_STATX_DONT_SYNC | AT_NO_AUTOMOUNT,
                              STATX_TYPE | STATX_MODE | STATX_SIZE | STATX_MTIME | STATX_UID | STATX_BTIME, &sx)) continue;
                    if (S_ISDIR(sx.stx_mode)) {
                        if (!c->recursive || makedev(sx.stx_dev_major, sx.stx_dev_minor) != c->dev) continue;
                        pthread_mutex_lock(&c->mtx);
                        if (c->qn == c->qcap) { c->qcap = c->qcap ? c->qcap * 2 : 256; c->queue = xrealloc(c->queue, (size_t)c->qcap * sizeof(char *)); }
                        c->queue[c->qn++] = strdup(r2);
                        pthread_cond_signal(&c->cv);
                        pthread_mutex_unlock(&c->mtx);
                        continue;
                    }
                    if (!S_ISREG(sx.stx_mode) || !sx.stx_size || sx.stx_size > CS_MAX_FILE) continue;
                    int ffd = openat(fd, nm, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NOCTTY);
                    if (ffd < 0) continue;
                    size_t want = (size_t)sx.stx_size;
                    if (want + 1 > fcap) { fcap = want + 1; fbuf = xrealloc(fbuf, fcap); }
                    size_t got = 0;
                    while (got < want) {
                        ssize_t r = read(ffd, fbuf + got, want - got);
                        if (r < 0 && errno == EINTR) continue;
                        if (r <= 0) break;
                        got += (size_t)r;
                    }
                    close(ffd);
                    if (got) content_scan(c, &b, r2, fbuf, got, &sx, hidden);
                    if (b && (b->nf >= 256 || now_ns() - b->t > 60000000ull)) cb_flush(c, &b, false);
                }
            }
            close(fd);
        }
        free(rel);
        pthread_mutex_lock(&c->mtx);
        c->active--;
        if (!c->qn && !c->active) pthread_cond_broadcast(&c->cv);
        pthread_mutex_unlock(&c->mtx);
    }
    cb_flush(c, &b, false);
    free(dbuf);
    free(fbuf);
    content_ctx_release(c);
    return NULL;
}

static void content_start_builtin(ContentCtx *c) {
    c->qcap = 256;
    c->queue = xmalloc((size_t)c->qcap * sizeof(char *));
    c->queue[c->qn++] = strdup("");
    int n = c->recursive ? CLAMP(g_nworkers, 2, 8) : 1;
    atomic_store(&c->alive, n);
    for (int i = 0; i < n; i++) {
        pthread_t t;
        pthread_create(&t, NULL, content_worker, c);
        pthread_setname_np(t, "fe-grep");
        pthread_detach(t);
    }
}

// ripgrep: `rg --null --line-number --column ...` prints "path\0line:col:text" per matching line, a file's lines together.
static void *content_rg_thread(void *arg) {
    ContentCtx *c = arg;
    thread_background_priority();
    const char *rg = getenv("FE_RG");
    if (!rg || !rg[0]) rg = "rg";
    char *argv[40];
    int na = 0;
    argv[na++] = (char *)rg;
    const char *fixed[] = {"--null", "--line-number", "--column", "--no-heading", "--color", "never", "--no-messages",
                           "--max-columns", "4000", "--max-columns-preview", "--max-filesize", "50M", "--one-file-system"};
    for (size_t i = 0; i < ARRAY_COUNT(fixed); i++) argv[na++] = (char *)fixed[i];
    char maxc[16];
    snprintf(maxc, sizeof maxc, "%d", CS_FILE_LINES);
    argv[na++] = "--max-count";
    argv[na++] = maxc;
    argv[na++] = c->cq.icase ? "--ignore-case" : "--case-sensitive";
    if (c->cq.word) argv[na++] = "--word-regexp";
    if (!c->cq.regex) argv[na++] = "--fixed-strings";
    if (c->hidden) { argv[na++] = "--hidden"; argv[na++] = "--glob"; argv[na++] = "!.git"; }
    if (!c->recursive) { argv[na++] = "--max-depth"; argv[na++] = "1"; }
    char g2[140];
    if (c->glob[0]) { snprintf(g2, sizeof g2, "%s", c->glob); argv[na++] = "--iglob"; argv[na++] = g2; }
    argv[na++] = "--regexp";
    argv[na++] = c->cq.q;
    argv[na++] = "--";
    argv[na++] = "."; // explicit: without a path rg may read stdin instead (paths then start with "./")
    argv[na] = NULL;
    int pfd[2];
    pid_t pid = -1;
    bool ok = pipe2(pfd, O_CLOEXEC) == 0;
    if (ok) {
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);
        posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addchdir_np(&fa, c->root);
        ok = posix_spawnp(&pid, argv[0], &fa, spawn_attr(), argv, environ) == 0;
        posix_spawn_file_actions_destroy(&fa);
        close(pfd[1]);
        if (!ok) close(pfd[0]);
    }
    if (!ok) { // not installed: the built-in search takes over (this thread's reference passes to its workers)
        content_start_builtin(c);
        return NULL;
    }
    FILE *f = fdopen(pfd[0], "r");
    ContentBatch *b = NULL;
    char *line = NULL, cur[PATH_MAX] = "";
    size_t lcap = 0;
    ssize_t ll;
    bool skip_file = false;
    while ((ll = getline(&line, &lcap, f)) > 0) {
        if (atomic_load_explicit(&c->L->cancel, memory_order_relaxed) || atomic_load(&c->trunc)) break;
        if (line[ll - 1] == '\n') line[--ll] = 0;
        char *nul = memchr(line, 0, (size_t)ll);
        if (!nul) continue;
        const char *path = line, *rest = nul + 1;
        if (!strncmp(path, "./", 2)) path += 2;
        char *end;
        unsigned long lno = strtoul(rest, &end, 10);
        if (*end != ':') continue;
        unsigned long col = strtoul(end + 1, &end, 10);
        if (*end != ':' || !col) continue;
        const char *text = end + 1;
        size_t tl = (size_t)(line + ll - text);
        if (strcmp(path, cur)) { // a new file
            snprintf(cur, sizeof cur, "%s", path);
            struct statx sx;
            bool have = statx(c->rootfd, path, AT_SYMLINK_NOFOLLOW | AT_STATX_DONT_SYNC, STATX_TYPE | STATX_MODE | STATX_SIZE |
                              STATX_MTIME | STATX_UID | STATX_BTIME, &sx) == 0;
            bool hidden = false;
            for (const char *q = path; q; q = strchr(q, '/'), q = q ? q + 1 : NULL) hidden |= *q == '.';
            skip_file = false;
            if (b && (b->nf >= 256 || now_ns() - b->t > 60000000ull)) cb_flush(c, &b, false);
            cb_file(&b, path, have ? &sx : NULL, hidden);
        }
        if (skip_file) continue;
        size_t ms = col - 1, ml = 0, m2, l2; // ripgrep's column is 1-based; our matcher finds the length
        if (cq_find(&c->cq, text, tl, 0, &m2, &l2)) { ms = m2; ml = l2; }
        if (ms > tl) ms = tl;
        if (!cb_line(c, b, (u32)lno, text, tl, ms, ml)) {
            if (!b->f[b->nf - 1].nl) b->nf--;
            break;
        }
        if (b->f[b->nf - 1].nl >= CS_FILE_LINES) skip_file = true;
    }
    if (pid > 0 && (atomic_load(&c->L->cancel) || atomic_load(&c->trunc))) kill(pid, SIGTERM);
    free(line);
    fclose(f); // (SIGCHLD is ignored app-wide: rg reaps itself)
    cb_flush(c, &b, false);
    content_ctx_release(c);
    return NULL;
}

// Start a contents search of root (glob: limit to matching file names). Takes a reference on L.
static void content_start(Listing *L, const char *root, const char *q, const char *glob, bool match_case, bool word,
                          bool regex, bool hidden, bool recursive, bool use_rg) {
    ContentCtx *c = xcalloc(1, sizeof *c);
    c->L = L;
    listing_retain(L);
    snprintf(c->root, sizeof c->root, "%s", root);
    cq_init(&c->cq, q, match_case, word, regex);
    snprintf(c->glob, sizeof c->glob, "%s", glob ? glob : "");
    c->hidden = hidden;
    c->recursive = recursive;
    c->use_rg = use_rg;
    struct stat st;
    c->rootfd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    c->dev = c->rootfd >= 0 && fstat(c->rootfd, &st) == 0 ? st.st_dev : 0;
    pthread_mutex_init(&c->mtx, NULL);
    pthread_cond_init(&c->cv, NULL);
    if (!use_rg) { content_start_builtin(c); return; }
    atomic_store(&c->alive, 1);
    pthread_t t;
    pthread_create(&t, NULL, content_rg_thread, c);
    pthread_setname_np(t, "fe-rg");
    pthread_detach(t);
}

// --- recursive folder sizes ------------------------------------------------

#define DSCACHE_TTL 30000000000ull // 30 s

typedef struct { char *key; u64 val, t; } DsSlot;
static struct {
    pthread_mutex_t mtx;
    DsSlot *slots;
    u32 cap, n;
} g_ds = {PTHREAD_MUTEX_INITIALIZER, NULL, 0, 0};
static _Atomic u64 g_ds_epoch; // results measured before this (now_ns) are stale: F5 asks for exact sizes

static u32 str_hash(const char *s) {
    u32 h = 2166136261u;
    while (*s) h = (h ^ (u8)*s++) * 16777619u;
    return h;
}

static bool dscache_get(const char *path, u64 *val) {
    bool ok = false;
    pthread_mutex_lock(&g_ds.mtx);
    if (g_ds.cap) {
        for (u32 h = str_hash(path) & (g_ds.cap - 1); g_ds.slots[h].key; h = (h + 1) & (g_ds.cap - 1)) {
            if (!strcmp(g_ds.slots[h].key, path)) {
                u64 t = g_ds.slots[h].t;
                if (now_ns() - t < DSCACHE_TTL && t >= atomic_load(&g_ds_epoch)) { *val = g_ds.slots[h].val; ok = true; }
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_ds.mtx);
    return ok;
}

static void dscache_put(const char *path, u64 val) {
    pthread_mutex_lock(&g_ds.mtx);
    if ((g_ds.n + 1) * 10 >= g_ds.cap * 7) {
        u32 oc = g_ds.cap;
        DsSlot *old = g_ds.slots;
        g_ds.cap = oc ? oc * 2 : 1024;
        g_ds.slots = xcalloc(g_ds.cap, sizeof(DsSlot));
        u64 now = now_ns();
        g_ds.n = 0;
        for (u32 i = 0; i < oc; i++) {
            if (!old[i].key) continue;
            if (now - old[i].t >= DSCACHE_TTL) { free(old[i].key); continue; } // expired: don't grow forever
            g_ds.n++;
            u32 h = str_hash(old[i].key) & (g_ds.cap - 1);
            while (g_ds.slots[h].key) h = (h + 1) & (g_ds.cap - 1);
            g_ds.slots[h] = old[i];
        }
        free(old);
    }
    u32 h = str_hash(path) & (g_ds.cap - 1);
    while (g_ds.slots[h].key && strcmp(g_ds.slots[h].key, path)) h = (h + 1) & (g_ds.cap - 1);
    if (!g_ds.slots[h].key) { g_ds.slots[h].key = strdup(path); g_ds.n++; }
    g_ds.slots[h].val = val;
    g_ds.slots[h].t = now_ns();
    pthread_mutex_unlock(&g_ds.mtx);
}

// A change inside `path` changes the recursive size of it and every ancestor.
static void dscache_invalidate_up(const char *path) {
    char cur[PATH_MAX];
    snprintf(cur, sizeof cur, "%s", path);
    pthread_mutex_lock(&g_ds.mtx);
    for (;;) {
        if (g_ds.cap) {
            for (u32 h = str_hash(cur) & (g_ds.cap - 1); g_ds.slots[h].key; h = (h + 1) & (g_ds.cap - 1))
                if (!strcmp(g_ds.slots[h].key, cur)) { g_ds.slots[h].t = 0; break; }
        }
        char *s = strrchr(cur, '/');
        if (!s || s == cur) break;
        *s = 0;
    }
    pthread_mutex_unlock(&g_ds.mtx);
}

// Per-folder cache under du_tree. A folder whose mtime hasn't changed still has the same entries, so
// measuring it again only revisits its subfolders (open + statx each) instead of reading the folder and
// statx'ing every file. Files rewritten in place don't touch the folder's mtime: entries are re-read
// after DU_RESCAN anyway (and on F5).
#define DU_RESCAN 300000000000ull // 5 min
#define DU_MAX 65536              // folders remembered (~4 MB); past that the cache starts over

typedef struct {
    u64 dev, ino; // ino 0: empty slot
    i64 mtime;    // ns
    u64 files;    // bytes of the regular files directly inside
    u64 t;        // when the entries were read (now_ns)
    char *kids;   // subfolder names on the same filesystem, NUL-separated
    u32 kids_len;
} DuNode;

static struct {
    pthread_mutex_t mtx;
    DuNode *slots;
    u32 cap, n;
} g_du = {PTHREAD_MUTEX_INITIALIZER, NULL, 0, 0};

static u32 du_hash(u64 dev, u64 ino) { return (u32)(((ino ^ (dev << 40)) * 0x9E3779B97F4A7C15ull) >> 32); }

// On a hit, *files and a copy of the subfolder names (free it) are returned.
static bool du_lookup(u64 dev, u64 ino, i64 mtime, u64 *files, char **kids, u32 *kids_len) {
    bool ok = false;
    pthread_mutex_lock(&g_du.mtx);
    if (g_du.cap) {
        for (u32 h = du_hash(dev, ino) & (g_du.cap - 1); g_du.slots[h].ino; h = (h + 1) & (g_du.cap - 1)) {
            DuNode *d = &g_du.slots[h];
            if (d->ino != ino || d->dev != dev) continue;
            if (d->mtime == mtime && now_ns() - d->t < DU_RESCAN && d->t >= atomic_load(&g_ds_epoch)) {
                *files = d->files;
                *kids = d->kids_len ? xmalloc(d->kids_len) : NULL;
                if (d->kids_len) memcpy(*kids, d->kids, d->kids_len);
                *kids_len = d->kids_len;
                ok = true;
            }
            break;
        }
    }
    pthread_mutex_unlock(&g_du.mtx);
    return ok;
}

static void du_store(u64 dev, u64 ino, i64 mtime, u64 files, char *kids, u32 kids_len, u64 t) {
    pthread_mutex_lock(&g_du.mtx);
    if ((g_du.n + 1) * 10 >= g_du.cap * 7) {
        u32 oc = g_du.cap;
        DuNode *old = g_du.slots;
        if (g_du.n >= DU_MAX) { // full: start over rather than track ages
            for (u32 i = 0; i < oc; i++) free(old[i].kids);
            memset(old, 0, oc * sizeof(DuNode));
            g_du.n = 0;
        } else {
            g_du.cap = oc ? oc * 2 : 1024;
            g_du.slots = xcalloc(g_du.cap, sizeof(DuNode));
            for (u32 i = 0; i < oc; i++) {
                if (!old[i].ino) continue;
                u32 h = du_hash(old[i].dev, old[i].ino) & (g_du.cap - 1);
                while (g_du.slots[h].ino) h = (h + 1) & (g_du.cap - 1);
                g_du.slots[h] = old[i];
            }
            free(old);
        }
    }
    u32 h = du_hash(dev, ino) & (g_du.cap - 1);
    while (g_du.slots[h].ino && (g_du.slots[h].ino != ino || g_du.slots[h].dev != dev)) h = (h + 1) & (g_du.cap - 1);
    DuNode *d = &g_du.slots[h];
    if (!d->ino) g_du.n++;
    free(d->kids);
    *d = (DuNode){dev, ino, mtime, files, t, kids, kids_len};
    pthread_mutex_unlock(&g_du.mtx);
}

// F5: measure everything again instead of trusting cached sizes.
static void dirsize_cache_expire(void) { atomic_store(&g_ds_epoch, now_ns()); }

// Recursive size of folder `name` (in dfd), staying on filesystem `dev`.
static u64 du_tree(int dfd, const char *name, dev_t dev, _Atomic int *cancel, char *buf) {
    int fd = openat(dfd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return 0;
    struct statx sx;
    if (statx(fd, "", AT_EMPTY_PATH | AT_STATX_DONT_SYNC, STATX_INO | STATX_MTIME, &sx) ||
        makedev(sx.stx_dev_major, sx.stx_dev_minor) != dev) { // a mount point
        close(fd);
        return 0;
    }
    u64 ino = sx.stx_ino;
    i64 mtime = sx.stx_mtime.tv_sec * 1000000000ll + sx.stx_mtime.tv_nsec;
    u64 t = now_ns(), files = 0;
    char *kids = NULL;
    u32 kl = 0;
    bool cached = du_lookup(dev, ino, mtime, &files, &kids, &kl);
    if (!cached) { // read the folder: sum its files, collect its subfolders
        u32 kcap = 0;
        bool complete = false;
        for (;;) {
            if (atomic_load_explicit(cancel, memory_order_relaxed)) break;
            long nread = syscall(SYS_getdents64, fd, buf, 64 * 1024);
            if (nread <= 0) { complete = nread == 0; break; }
            for (long off = 0; off < nread;) {
                struct linux_dirent64_ *d = (struct linux_dirent64_ *)(buf + off);
                off += d->d_reclen;
                const char *nm = d->d_name;
                if (is_dot_or_dotdot(nm)) continue;
                if (d->d_type == DT_DIR) { // the subfolder's own statx (in its du_tree) checks the filesystem
                    u32 l = (u32)strlen(nm) + 1;
                    if (kl + l > kcap) { kcap = MAX(kcap * 2, kl + l + 256); kids = xrealloc(kids, kcap); }
                    memcpy(kids + kl, nm, l);
                    kl += l;
                    continue;
                }
                if (d->d_type != DT_REG && d->d_type != DT_UNKNOWN) continue; // links, devices, ...: no size
                if (statx(fd, nm, AT_SYMLINK_NOFOLLOW | AT_STATX_DONT_SYNC | AT_NO_AUTOMOUNT, STATX_TYPE | STATX_SIZE, &sx)) continue;
                if (S_ISREG(sx.stx_mode)) files += sx.stx_size;
                else if (S_ISDIR(sx.stx_mode)) {
                    u32 l = (u32)strlen(nm) + 1;
                    if (kl + l > kcap) { kcap = MAX(kcap * 2, kl + l + 256); kids = xrealloc(kids, kcap); }
                    memcpy(kids + kl, nm, l);
                    kl += l;
                }
            }
        }
        // A change in the same clock tick as our read could leave mtime unchanged: don't trust a very recent one.
        struct timespec rt;
        clock_gettime(CLOCK_REALTIME, &rt);
        bool racy = (rt.tv_sec * 1000000000ll + rt.tv_nsec) - mtime < 2000000000ll;
        if (complete && !racy) {
            char *copy = kl ? xmalloc(kl) : NULL;
            if (kl) memcpy(copy, kids, kl);
            du_store(dev, ino, mtime, files, copy, kl, t);
        }
    }
    u64 total = files;
    for (u32 o = 0; o < kl; o += (u32)strlen(kids + o) + 1) {
        if (atomic_load_explicit(cancel, memory_order_relaxed)) break;
        total += du_tree(fd, kids + o, dev, cancel, buf);
    }
    free(kids);
    close(fd);
    return total;
}

typedef struct {
    Listing *L;
    u32 *dirs;
    u32 n;
    _Atomic u32 cursor;
    _Atomic int alive;
} DirSizeCtx;

static void dirsize_done_cb(void *arg); // app.c

static void *dirsize_worker(void *arg) {
    DirSizeCtx *c = arg;
    thread_background_priority();
    Listing *L = c->L;
    char *buf = xmalloc(64 * 1024);
    u64 last_wake = now_ns();
    for (;;) {
        u32 i = atomic_fetch_add(&c->cursor, 1);
        if (i >= c->n || atomic_load(&L->cancel)) break;
        Entry *e = &L->entries[c->dirs[i]];
        char full[PATH_MAX];
        path_join(full, sizeof full, L->path, e->name);
        u64 sz;
        if (!dscache_get(full, &sz)) {
            struct stat st;
            if (fstatat(L->dirfd, e->name, &st, AT_SYMLINK_NOFOLLOW)) continue;
            sz = du_tree(L->dirfd, e->name, st.st_dev, &L->cancel, buf);
            if (atomic_load(&L->cancel)) break;
            dscache_put(full, sz);
        }
        __atomic_store_n(&e->size, sz, __ATOMIC_RELAXED);
        __atomic_or_fetch(&e->mflags, (u8)MF_DIRSIZE, __ATOMIC_RELEASE);
        u64 t = now_ns();
        if (t - last_wake > 100000000ull) { main_wake(); last_wake = t; }
    }
    free(buf);
    if (atomic_fetch_sub(&c->alive, 1) == 1) {
        free(c->dirs);
        free(c);
        main_post(dirsize_done_cb, L); // passes our reference on
    }
    return NULL;
}

// Compute recursive sizes for the folders of a completed listing (name order = screen order).
// Hidden folders only when they are shown: in $HOME they (.cache, .local, ...) are most of the tree.
static void dirsize_start(Listing *L, bool visible, bool hidden) {
    DirSizeCtx *c = xcalloc(1, sizeof *c);
    c->dirs = xmalloc((size_t)MAX(L->count, 1u) * sizeof(u32));
    for (u32 i = 0; i < L->count; i++) {
        Entry *e = &L->entries[i];
        if ((e->flags & ENT_DIR) && !(e->flags & ENT_LINK) && ((e->flags & ENT_HIDDEN) ? hidden : visible)) c->dirs[c->n++] = i;
    }
    if (!c->n) { free(c->dirs); free(c); return; }
    SortCtx ctx = {L->entries, SORT_NAME, false, false, GROUP_NONE, 0};
    qsort_r(c->dirs, c->n, sizeof(u32), cmp_entries, &ctx);
    c->L = L;
    listing_retain(L);
    int n = (int)MIN(c->n, 3u);
    atomic_store(&c->alive, n);
    for (int i = 0; i < n; i++) {
        pthread_t t;
        pthread_create(&t, NULL, dirsize_worker, c);
        pthread_setname_np(t, "fe-dirsize");
        pthread_detach(t);
    }
}

// --- inotify ---------------------------------------------------------------

static int g_ino = -1;
static pthread_mutex_t g_ino_mtx = PTHREAD_MUTEX_INITIALIZER;
static int g_ino_dirty[256];
static int g_ino_ndirty; // -1 = overflow: everything is dirty

static void *ino_thread(void *arg) {
    (void)arg;
    char buf[16384] __attribute__((aligned(__alignof__(struct inotify_event))));
    for (;;) {
        ssize_t n = read(g_ino, buf, sizeof buf);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        pthread_mutex_lock(&g_ino_mtx);
        for (char *p = buf; p < buf + n;) {
            struct inotify_event *ev = (struct inotify_event *)p;
            p += sizeof *ev + ev->len;
            if (g_ino_ndirty < 0) continue;
            if (ev->mask & IN_Q_OVERFLOW) { g_ino_ndirty = -1; continue; }
            bool seen = false;
            for (int i = 0; i < g_ino_ndirty; i++) seen |= g_ino_dirty[i] == ev->wd;
            if (seen) continue;
            if (g_ino_ndirty == (int)ARRAY_COUNT(g_ino_dirty)) g_ino_ndirty = -1;
            else g_ino_dirty[g_ino_ndirty++] = ev->wd;
        }
        pthread_mutex_unlock(&g_ino_mtx);
        main_wake();
    }
    return NULL;
}

static void watch_init(void) {
    g_ino = inotify_init1(IN_CLOEXEC);
    if (g_ino < 0) return;
    pthread_t t;
    pthread_create(&t, NULL, ino_thread, NULL);
    pthread_setname_np(t, "fe-inotify");
    pthread_detach(t);
}

// Returns the number of dirty watch descriptors, or -1 if everything should be considered dirty.
static int watch_take_dirty(int *out, int max) {
    pthread_mutex_lock(&g_ino_mtx);
    int n = g_ino_ndirty;
    if (n > 0) memcpy(out, g_ino_dirty, (size_t)MIN(n, max) * sizeof(int));
    g_ino_ndirty = 0;
    pthread_mutex_unlock(&g_ino_mtx);
    return n;
}
