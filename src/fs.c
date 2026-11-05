// fs.c - directory listing: threaded getdents64 scan, parallel statx, sorting
//
// Phase 1 (one worker): getdents64 -> names + type, then initial sort, publish.
// Phase 2 (N workers):  statx per entry for size/mtime, visible rows first.

struct linux_dirent64_ {
    u64 d_ino;
    i64 d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

enum { ENT_DIR = 1, ENT_LINK = 2, ENT_HIDDEN = 4, ENT_MATCH = 8 }; // ENT_MATCH: a matching line under its file (contents search)

// Contents search query: literal (case folded unless it matters), whole word, or a POSIX extended regex.
typedef struct CQuery {
    char q[256];
    bool icase, word, regex;
    bool re_ok;
    regex_t re;
} CQuery;
enum { MF_EXEC = 1, MF_DIRSIZE = 2, MF_NOSIZE = 4 }; // MF_DIRSIZE: size holds the recursive folder size (atomic access)
// MF_NOSIZE (with MF_DIRSIZE, size 0): a kernel pseudo-filesystem (/proc, /sys, ...) whose file sizes mean nothing
enum { META_PENDING, META_CLAIMED, META_READY };
enum { SORT_NAME, SORT_DATE, SORT_TYPE, SORT_SIZE, SORT_CREATED, SORT_EXT, SORT_PERMS, SORT_OWNER, SORT_COUNT };
enum { LST_SCANNING, LST_READY, LST_COMPLETE, LST_ERROR };

typedef struct {
    const char *name;
    u32 name_len;
    u16 ext;          // offset of extension (after the dot), 0 if none
    u8 flags;         // ENT_*; written in phase 1 only
    _Atomic(u8) meta; // META_*; size/mtime/mflags valid once META_READY
    u8 mflags;
    u16 mode;  // st_mode (valid once META_READY)
    u32 uid;
    u64 size;
    i64 mtime;
    i64 btime; // creation time, 0 if the filesystem doesn't report it
} Entry;

#define PRIO_MAX 256

typedef struct Listing {
    _Atomic int refs;
    _Atomic int cancel;
    _Atomic int state;
    int err;
    char *path;
    int dirfd;
    Arena names;
    Entry *entries; // valid once state >= LST_READY
    u32 count;
    _Atomic u32 scanned;
    _Atomic u32 meta_cursor;
    _Atomic u32 meta_done;
    _Atomic int meta_jobs;
    u32 *order;     // initial sorted order; main thread takes ownership
    u32 order_count;
    int sort_key;
    bool sort_desc, show_hidden;
    pthread_mutex_t prio_mtx;
    u32 prio[PRIO_MAX];
    u32 prio_count;
    _Atomic u32 prio_gen;
    u64 t_start, t_names, t_meta;
    // recursive search results: entries are appended by the main thread only
    bool is_search;
    u32 cap;
    _Atomic u32 search_dirs;
    bool search_recursive;
    char filter[256]; // fuzzy/wildcard name filter applied when building the order
    int group;        // GROUP_* used for the initial order
    bool dirsize_started, dirsize_hidden; // recursive sizes started for the visible / the hidden folders
    bool is_archive;  // read-only view of a folder inside an archive ("file::inner")
    int tf_mode;      // type filter: 0 all, 1 files only, 2 folders only
    char tf_exts[512]; // hidden extensions "|png|jpg|" ("||" = files without extension)
    char *tree;        // tree view: expanded folders as "\nsub\nsub/deeper\n" (entries then use relative names); NULL = flat
    // contents search: each matching file is followed by its ENT_MATCH entries (line in size, column | length << 32
    // in btime, the file's name); cinfo: for a file the number of its match entries (| CI_COLLAPSED), for a match its
    // distance back to the file; ctext: the line of a match entry
    bool is_content;
    u32 *cinfo;
    char **ctext;
    u32 nmatches, nfiles; // so far
    bool ctrunc;          // stopped at the result limit
    CQuery *cq;
} Listing;

#define CI_COLLAPSED 0x80000000u

// Is the relative folder rel (len bytes) expanded in this tree string?
static bool tree_has(const char *tree, const char *rel, size_t len) {
    if (!tree) return false;
    for (const char *p = tree; (p = strstr(p, rel)); p++)
        if (p > tree && p[-1] == '\n' && p[len] == '\n') return true;
    return false;
}

static inline int rel_depth(const char *name) {
    int d = 0;
    for (const char *c = name; *c; c++) d += *c == '/';
    return d;
}

static inline u64 entry_dir_size(const Entry *e, bool *known) {
    bool k = __atomic_load_n(&e->mflags, __ATOMIC_ACQUIRE) & MF_DIRSIZE;
    if (known) *known = k;
    return k ? __atomic_load_n(&e->size, __ATOMIC_RELAXED) : 0;
}

static Listing *listing_new(const char *path, int key, bool desc, bool hidden) {
    Listing *L = xcalloc(1, sizeof *L);
    atomic_init(&L->refs, 1);
    L->path = strdup(path);
    L->dirfd = -1;
    L->names.block_size = 256u << 10;
    L->sort_key = key;
    L->sort_desc = desc;
    L->show_hidden = hidden;
    pthread_mutex_init(&L->prio_mtx, NULL);
    L->t_start = now_ns();
    return L;
}

static void listing_retain(Listing *L) { atomic_fetch_add(&L->refs, 1); }

static void listing_release(Listing *L) {
    if (atomic_fetch_sub(&L->refs, 1) != 1) return;
    if (L->dirfd >= 0) close(L->dirfd);
    arena_free(&L->names);
    free(L->entries);
    free(L->order);
    free(L->path);
    free(L->tree);
    free(L->cinfo);
    free(L->ctext);
    if (L->cq) {
        if (L->cq->re_ok) regfree(&L->cq->re);
        free(L->cq);
    }
    pthread_mutex_destroy(&L->prio_mtx);
    free(L);
}

static void listing_set_prio(Listing *L, const u32 *idx, u32 n) {
    n = MIN(n, PRIO_MAX);
    pthread_mutex_lock(&L->prio_mtx);
    memcpy(L->prio, idx, n * sizeof(u32));
    L->prio_count = n;
    pthread_mutex_unlock(&L->prio_mtx);
    atomic_fetch_add(&L->prio_gen, 1);
}

// --- sorting ---------------------------------------------------------------

static int fuzzy_match(const char *s, int n, const char *q, int qn, u16 *pos); // search.c

// Name filter used by the folder filter (Ctrl+F) and the recursive search:
// wildcards when the query has * ? [, otherwise fuzzy (subsequence) matching.
static bool name_matches(const char *q, const char *name, int len) {
    if (!q || !q[0]) return true;
    if (strpbrk(q, "*?[")) return fnmatch(q, name, FNM_CASEFOLD) == 0;
    return fuzzy_match(name, len, q, (int)strlen(q), NULL) >= 0;
}

enum { GROUP_NONE, GROUP_DATE, GROUP_TYPE, GROUP_SIZE, GROUP_COUNT };

typedef struct {
    const Entry *ents;
    int key;
    bool desc, meta;
    int group;
    i64 day0; // local midnight today (date groups)
} SortCtx;

static i64 local_midnight(time_t now) {
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    return (i64)mktime(&tm);
}

// Group of an entry: rank orders the groups; label (optional) names it.
static int group_rank(const Entry *e, int group, bool meta, i64 day0, char *label, size_t cap) {
    bool dir = e->flags & ENT_DIR;
    if (group == GROUP_TYPE) {
        if (dir) { if (label) snprintf(label, cap, "Folders"); return 0; }
        if (label) {
            if (e->ext && e->name_len - e->ext <= 10) {
                size_t k = 0;
                for (const char *q = e->name + e->ext; *q && k + 6 < cap; q++) label[k++] = (char)toupper((u8)*q);
                snprintf(label + k, cap - k, " files");
            } else snprintf(label, cap, "Other files");
        }
        return 1; // within type groups the extension decides (see cmp_group)
    }
    if (!meta) { if (label) snprintf(label, cap, "Reading\xE2\x80\xA6"); return 99; }
    if (group == GROUP_SIZE) {
        static const char *names[] = {"Folders", "Empty", "Tiny (under 16 KB)", "Small (16 KB \xE2\x80\x93 1 MB)",
                                      "Medium (1 \xE2\x80\x93 128 MB)", "Large (128 MB \xE2\x80\x93 1 GB)",
                                      "Huge (1 \xE2\x80\x93 4 GB)", "Gigantic (over 4 GB)"};
        int r;
        if (dir) r = 0;
        else if (!e->size) r = 1;
        else if (e->size < (16u << 10)) r = 2;
        else if (e->size < (1u << 20)) r = 3;
        else if (e->size < (128u << 20)) r = 4;
        else if (e->size < (1ull << 30)) r = 5;
        else if (e->size < (4ull << 30)) r = 6;
        else r = 7;
        if (label) snprintf(label, cap, "%s", names[r]);
        return r;
    }
    // GROUP_DATE (modification time), newest first
    static const char *names[] = {"Today", "Yesterday", "Earlier this week", "Last week", "Earlier this month",
                                  "Last month", "Earlier this year", "A long time ago", "In the future"};
    struct tm t0;
    time_t d0 = (time_t)day0;
    localtime_r(&d0, &t0);
    int wday = (t0.tm_wday + 6) % 7; // Monday = 0
    i64 week0 = day0 - (i64)wday * 86400, lastweek0 = week0 - 7 * 86400;
    struct tm m = t0;
    m.tm_mday = 1;
    i64 month0 = (i64)mktime(&m);
    m.tm_mon -= 1;
    i64 lastmonth0 = (i64)mktime(&m);
    struct tm y = t0;
    y.tm_mday = 1;
    y.tm_mon = 0;
    i64 year0 = (i64)mktime(&y);
    i64 t = e->mtime;
    int r = t >= day0 + 86400 ? 8 : t >= day0 ? 0 : t >= day0 - 86400 ? 1 : t >= week0 ? 2 : t >= lastweek0 ? 3
          : t >= month0 ? 4 : t >= lastmonth0 ? 5 : t >= year0 ? 6 : 7;
    if (label) snprintf(label, cap, "%s", names[r]);
    return r == 8 ? -1 : r;
}

static int cmp_group(const SortCtx *c, const Entry *x, const Entry *y) {
    int a = group_rank(x, c->group, c->meta, c->day0, NULL, 0), b = group_rank(y, c->group, c->meta, c->day0, NULL, 0);
    if (a != b) return a < b ? -1 : 1;
    if (c->group == GROUP_TYPE && a == 1) return natcmp(x->ext ? x->name + x->ext : "", y->ext ? y->name + y->ext : "");
    return 0;
}

static int cmp_entries(const void *pa, const void *pb, void *pctx) {
    const SortCtx *c = pctx;
    const Entry *x = &c->ents[*(const u32 *)pa];
    const Entry *y = &c->ents[*(const u32 *)pb];
    if (c->group) {
        int g = cmp_group(c, x, y);
        if (g) return g; // groups keep their own order regardless of the sort direction
    }
    int dx = x->flags & ENT_DIR, dy = y->flags & ENT_DIR;
    if (dx != dy) return dx ? -1 : 1; // folders first, always
    int r = 0;
    switch (c->key) {
    case SORT_DATE:
        if (c->meta) r = (x->mtime > y->mtime) - (x->mtime < y->mtime);
        break;
    case SORT_SIZE:
        if (c->meta) {
            u64 a = dx ? entry_dir_size(x, NULL) : x->size, b = dx ? entry_dir_size(y, NULL) : y->size;
            r = (a > b) - (a < b);
        }
        break;
    case SORT_TYPE:
    case SORT_EXT:
        r = natcmp(x->ext ? x->name + x->ext : "", y->ext ? y->name + y->ext : "");
        break;
    case SORT_CREATED:
        if (c->meta) r = (x->btime > y->btime) - (x->btime < y->btime);
        break;
    case SORT_PERMS:
        if (c->meta) r = (int)(x->mode & 07777) - (int)(y->mode & 07777);
        break;
    case SORT_OWNER:
        if (c->meta) r = (x->uid > y->uid) - (x->uid < y->uid);
        break;
    }
    if (!r) r = natcmp(x->name, y->name);
    if (!r) r = strcmp(x->name, y->name);
    return c->desc ? -r : r;
}

typedef struct {
    int key;
    bool desc, hidden, meta;
    const char *filter;  // name filter (Ctrl+F)
    int group;
    int tf_mode;         // type filter
    const char *tf_exts; // hidden extensions "|png|"
    bool tree;           // tree view: children follow their folder (depth-first)
} OrderOpts;

// Does the type filter hide this entry?
static bool type_hidden(const Entry *e, int mode, const char *exts) {
    bool dir = e->flags & ENT_DIR;
    if ((mode == 1 && dir) || (mode == 2 && !dir)) return true;
    if (dir || !exts || !exts[0]) return false;
    char key[24];
    if (!e->ext) snprintf(key, sizeof key, "||");
    else {
        size_t n = e->name_len - e->ext;
        if (n > 16) return false;
        key[0] = '|';
        for (size_t i = 0; i < n; i++) key[1 + i] = (char)ascii_lower((u8)e->name[e->ext + i]);
        key[1 + n] = '|';
        key[2 + n] = 0;
    }
    return strstr(exts, key) != NULL;
}

// Reorder a sorted, filtered order into tree order: every folder is followed by its (visible) children,
// siblings keep the sort order. Entries whose parent isn't visible are dropped. Returns the new count.
static u32 tree_order(const Listing *L, u32 *o, u32 m) {
    const u32 NONE = 0xFFFFFFFFu;
    u32 hcap = 16;
    while (hcap < L->count * 2) hcap <<= 1;
    u32 *ht = xmalloc((size_t)hcap * sizeof(u32)); // relative name -> entry index (visible entries only)
    memset(ht, 0xFF, (size_t)hcap * sizeof(u32));
    for (u32 k = 0; k < m; k++) {
        const Entry *e = &L->entries[o[k]];
        u32 h = 2166136261u;
        for (u32 c = 0; c < e->name_len; c++) h = (h ^ (u8)e->name[c]) * 16777619u;
        while (ht[h & (hcap - 1)] != NONE) h++;
        ht[h & (hcap - 1)] = o[k];
    }
    u32 *first = xmalloc((size_t)L->count * sizeof(u32)), *last = xmalloc((size_t)L->count * sizeof(u32));
    u32 *next = xmalloc((size_t)L->count * sizeof(u32));
    for (u32 k = 0; k < m; k++) first[o[k]] = last[o[k]] = next[o[k]] = NONE;
    u32 root_first = NONE, root_last = NONE;
    for (u32 k = 0; k < m; k++) { // children lists, in sorted order
        u32 i = o[k];
        const Entry *e = &L->entries[i];
        const char *slash = memrchr(e->name, '/', e->name_len);
        u32 par = NONE;
        if (slash) {
            u32 pl = (u32)(slash - e->name), h = 2166136261u;
            for (u32 c = 0; c < pl; c++) h = (h ^ (u8)e->name[c]) * 16777619u;
            for (;; h++) {
                u32 j = ht[h & (hcap - 1)];
                if (j == NONE) break;
                if (L->entries[j].name_len == pl && !memcmp(L->entries[j].name, e->name, pl)) { par = j; break; }
            }
            if (par == NONE) continue; // parent hidden or filtered out
        }
        u32 *f = par == NONE ? &root_first : &first[par], *l = par == NONE ? &root_last : &last[par];
        if (*f == NONE) *f = i;
        else next[*l] = i;
        *l = i;
    }
    // depth-first walk (explicit stack of "next sibling to visit")
    u32 n = 0, sp = 0;
    u32 *stack = xmalloc((size_t)(m + 1) * sizeof(u32));
    u32 cur = root_first;
    for (;;) {
        while (cur != NONE) {
            o[n++] = cur;
            if (first[cur] != NONE) {
                stack[sp++] = next[cur];
                cur = first[cur];
            } else {
                cur = next[cur];
            }
        }
        if (!sp) break;
        cur = stack[--sp];
    }
    free(stack);
    free(first);
    free(last);
    free(next);
    free(ht);
    return n;
}

static void listing_build_order_ex(const Listing *L, const OrderOpts *op, u32 **out, u32 *out_count) {
    u32 *o = xmalloc((size_t)MAX(L->count, 1u) * sizeof(u32));
    u32 m = 0;
    for (u32 i = 0; i < L->count; i++) {
        const Entry *e = &L->entries[i];
        if (e->flags & ENT_MATCH) continue; // contents search: placed under their file afterwards (content_rows)
        if (!op->hidden && (e->flags & ENT_HIDDEN)) continue;
        if (op->filter && op->filter[0] && !name_matches(op->filter, e->name, (int)e->name_len)) continue;
        if ((op->tf_mode || (op->tf_exts && op->tf_exts[0])) && type_hidden(e, op->tf_mode, op->tf_exts)) continue;
        o[m++] = i;
    }
    int key = op->key, group = op->group;
    bool desc = op->desc, meta = op->meta;
    SortCtx ctx = {L->entries, key, desc, meta, group, group ? local_midnight(time(NULL)) : 0};
    qsort_r(o, m, sizeof(u32), cmp_entries, &ctx);
    if (op->tree && m) m = tree_order(L, o, m);
    *out = o;
    *out_count = m;
}

static void listing_build_order(const Listing *L, int key, bool desc, bool hidden, bool meta, const char *filter,
                                int group, u32 **out, u32 *out_count) {
    OrderOpts o = {key, desc, hidden, meta, filter, group, 0, NULL, L->tree != NULL};
    listing_build_order_ex(L, &o, out, out_count);
}

// --- phase 2: metadata -----------------------------------------------------

static void meta_one(Listing *L, u32 i) {
    Entry *e = &L->entries[i];
    u8 expected = META_PENDING;
    if (!atomic_compare_exchange_strong(&e->meta, &expected, META_CLAIMED)) return;
    struct statx sx;
    if (statx(L->dirfd, e->name, AT_SYMLINK_NOFOLLOW | AT_STATX_DONT_SYNC | AT_NO_AUTOMOUNT,
              STATX_SIZE | STATX_MTIME | STATX_MODE | STATX_UID | STATX_BTIME, &sx) == 0) {
        e->size = sx.stx_size;
        e->mtime = sx.stx_mtime.tv_sec;
        e->mode = sx.stx_mode;
        e->uid = sx.stx_uid;
        e->btime = (sx.stx_mask & STATX_BTIME) ? sx.stx_btime.tv_sec : 0;
        if (S_ISREG(sx.stx_mode) && (sx.stx_mode & 0111)) e->mflags |= MF_EXEC;
    }
    atomic_store_explicit(&e->meta, META_READY, memory_order_release);
    atomic_fetch_add_explicit(&L->meta_done, 1, memory_order_relaxed);
}

static void meta_job(void *arg) {
    Listing *L = arg;
    u32 seen_gen = 0;
    const u32 chunk = 512;
    for (;;) {
        if (atomic_load_explicit(&L->cancel, memory_order_relaxed)) break;
        u32 g = atomic_load(&L->prio_gen);
        if (g != seen_gen) { // visible rows first
            seen_gen = g;
            u32 tmp[PRIO_MAX], n;
            pthread_mutex_lock(&L->prio_mtx);
            n = L->prio_count;
            memcpy(tmp, L->prio, n * sizeof(u32));
            pthread_mutex_unlock(&L->prio_mtx);
            for (u32 i = 0; i < n; i++) meta_one(L, tmp[i]);
        }
        u32 start = atomic_fetch_add(&L->meta_cursor, chunk);
        if (start >= L->count) break;
        u32 end = MIN(start + chunk, L->count);
        for (u32 i = start; i < end; i++) meta_one(L, i);
    }
    if (atomic_fetch_sub(&L->meta_jobs, 1) == 1 && !atomic_load(&L->cancel)) {
        L->t_meta = now_ns();
        atomic_store(&L->state, LST_COMPLETE);
        main_wake();
    }
    listing_release(L);
}

// --- phase 1: scan ---------------------------------------------------------

static void arc_scan(Listing *L); // archive.c

// getdents64 one folder into ents; prefix (relative folder) makes names "prefix/child" for the tree view.
static void scan_dir(Listing *L, int fd, const char *prefix, char *buf, size_t bufsz, Entry **pents, u32 *pn, u32 *pcap) {
    Entry *ents = *pents;
    u32 n = *pn, cap = *pcap;
    size_t pl = prefix ? strlen(prefix) : 0;
    for (;;) {
        if (atomic_load_explicit(&L->cancel, memory_order_relaxed)) break;
        long nread = syscall(SYS_getdents64, fd, buf, bufsz);
        if (nread <= 0) {
            if (nread < 0 && !prefix) L->err = errno;
            break;
        }
        for (long off = 0; off < nread;) {
            struct linux_dirent64_ *d = (struct linux_dirent64_ *)(buf + off);
            off += d->d_reclen;
            const char *nm = d->d_name;
            if (nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0))) continue;
            if (n == cap) {
                cap *= 2;
                ents = xrealloc(ents, cap * sizeof(Entry));
            }
            Entry *e = &ents[n++];
            size_t len = strlen(nm), tot = pl ? pl + 1 + len : len;
            char *s = arena_alloc(&L->names, tot + 1, 1);
            if (pl) {
                memcpy(s, prefix, pl);
                s[pl] = '/';
            }
            memcpy(s + (pl ? pl + 1 : 0), nm, len + 1);
            const char *base = s + (pl ? pl + 1 : 0);
            e->name = s;
            e->name_len = (u32)tot;
            e->flags = nm[0] == '.' ? ENT_HIDDEN : 0;
            e->mflags = 0;
            e->size = 0;
            e->mtime = 0;
            e->btime = 0;
            e->mode = 0;
            e->uid = 0;
            atomic_init(&e->meta, META_PENDING);
            u8 t = d->d_type;
            if (t == DT_DIR) {
                e->flags |= ENT_DIR;
            } else if (t == DT_LNK || t == DT_UNKNOWN) {
                if (t == DT_LNK) e->flags |= ENT_LINK;
                struct stat st; // follow links so linked folders sort/open as folders
                if (fstatat(fd, nm, &st, 0) == 0 && S_ISDIR(st.st_mode)) e->flags |= ENT_DIR;
            }
            e->ext = 0;
            if (!(e->flags & ENT_DIR)) {
                const char *dot = strrchr(base, '.');
                if (dot && dot != base && dot[1]) e->ext = (u16)(dot - s + 1);
            }
        }
        atomic_store_explicit(&L->scanned, n, memory_order_relaxed);
    }
    *pents = ents;
    *pn = n;
    *pcap = cap;
}

static void scan_job(void *arg) {
    Listing *L = arg;
    if (strstr(L->path, "::")) { // folder inside an archive
        arc_scan(L);
        listing_release(L);
        return;
    }
    int fd = open(L->path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        L->err = errno;
        atomic_store(&L->state, LST_ERROR);
        main_wake();
        listing_release(L);
        return;
    }
    L->dirfd = fd;

    enum { BUFSZ = 512 * 1024 };
    char *buf = xmalloc(BUFSZ);
    u32 cap = 1024, n = 0;
    Entry *ents = xmalloc(cap * sizeof(Entry));
    scan_dir(L, fd, NULL, buf, BUFSZ, &ents, &n, &cap);
    if (L->tree) // tree view: expanded folders (and expanded folders inside them, as they get appended)
        for (u32 i = 0; i < n && !atomic_load_explicit(&L->cancel, memory_order_relaxed); i++) {
            if (!(ents[i].flags & ENT_DIR) || !tree_has(L->tree, ents[i].name, ents[i].name_len)) continue;
            int sfd = openat(fd, ents[i].name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (sfd < 0) continue;
            char prefix[PATH_MAX];
            snprintf(prefix, sizeof prefix, "%s", ents[i].name);
            scan_dir(L, sfd, prefix, buf, BUFSZ, &ents, &n, &cap);
            close(sfd);
        }
    free(buf);

    L->entries = ents;
    L->count = n;
    OrderOpts oo = {L->sort_key, L->sort_desc, L->show_hidden, false, L->filter, L->group, L->tf_mode, L->tf_exts, L->tree != NULL};
    listing_build_order_ex(L, &oo, &L->order, &L->order_count);
    L->t_names = now_ns();
    if (!n) L->t_meta = L->t_names; // before publishing the state
    atomic_store(&L->state, n ? LST_READY : LST_COMPLETE);
    main_wake();

    if (n && !atomic_load(&L->cancel)) {
        int k = (int)MIN((u32)g_nworkers, 1 + n / 2048);
        atomic_store(&L->meta_jobs, k);
        for (int i = 0; i < k; i++) {
            listing_retain(L);
            pool_submit(meta_job, L);
        }
    }
    listing_release(L);
}

static void listing_start(Listing *L) {
    listing_retain(L);
    pool_submit(scan_job, L);
}
