// compare.c - folder compare: walks two folder trees side by side on a worker thread and records what differs
// (paths relative to the two roots; what isn't recorded is the same on both sides)

// From side A's point of view; side B swaps ONLY_A/ONLY_B and A_NEWER/B_NEWER. INSIDE: a folder on both sides
// with differences somewhere below it. DIFF: same date, different size (or contents). TYPE: a file on one side,
// a folder on the other.
enum { CMP_SAME, CMP_ONLY_A, CMP_ONLY_B, CMP_A_NEWER, CMP_B_NEWER, CMP_DIFF, CMP_TYPE, CMP_INSIDE, CMP_UNKNOWN = 255 };
#define CMP_MTIME_SLACK 2000000000ll // FAT keeps times to 2 s: closer than that is the same time

typedef struct { char *rel; u8 st; bool dir; } CmpRec;

typedef struct CmpJob {
    char *ra, *rb;
    bool contents;            // files of the same size are compared byte by byte
    _Atomic int cancel, done;
    _Atomic u64 nitems;       // entries compared so far (progress)
    CmpRec *recs;             // built by the worker, read by the main thread once done
    u32 n, cap;
    u32 *ht, hcap;            // rel -> recs index + 1
    u32 nerr;                 // folders that couldn't be read
    u64 t0, t1;
    bool delivered;           // main thread: its done callback ran (until then only that callback may free it)
    u64 dev[2];               // the roots' filesystems (folders on others aren't walked)
} CmpJob;

typedef struct { char *name; u32 mode; u64 size; i64 mtime; u64 dev; } CmpEnt;

static int cmp_ent_cmp(const void *a, const void *b) { return strcmp(((const CmpEnt *)a)->name, ((const CmpEnt *)b)->name); }

static CmpEnt *cmp_read(int fd, u32 *n, bool *failed) {
    *n = 0;
    if (fd < 0) return NULL;
    int dfd = dup(fd);
    DIR *d = dfd >= 0 ? fdopendir(dfd) : NULL;
    if (!d) { if (dfd >= 0) close(dfd); *failed = true; return NULL; }
    u32 cap = 0;
    CmpEnt *v = NULL;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (is_dot_or_dotdot(de->d_name)) continue;
        struct statx sx;
        if (statx(fd, de->d_name, AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT, STATX_TYPE | STATX_SIZE | STATX_MTIME, &sx)) continue;
        if (*n == cap) { cap = cap ? cap * 2 : 64; v = xrealloc(v, cap * sizeof(CmpEnt)); }
        v[(*n)++] = (CmpEnt){strdup(de->d_name), sx.stx_mode, sx.stx_size, sx.stx_mtime.tv_sec * 1000000000ll + sx.stx_mtime.tv_nsec,
                             makedev(sx.stx_dev_major, sx.stx_dev_minor)};
    }
    closedir(d);
    if (*n) qsort(v, *n, sizeof(CmpEnt), cmp_ent_cmp);
    return v;
}

static void cmp_record(CmpJob *j, const char *rel, u8 st, bool dir) {
    if (j->n == j->cap) { j->cap = j->cap ? j->cap * 2 : 256; j->recs = xrealloc(j->recs, j->cap * sizeof(CmpRec)); }
    j->recs[j->n++] = (CmpRec){strdup(rel), st, dir};
}

static bool cmp_same_bytes(CmpJob *j, int fa, int fb, const char *name) {
    int a = openat(fa, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW), b = openat(fb, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    bool same = a >= 0 && b >= 0;
    char *x = xmalloc(1 << 17), *y = x + (1 << 16);
    while (same && !atomic_load_explicit(&j->cancel, memory_order_relaxed)) {
        ssize_t na = read(a, x, 1 << 16), nb = read(b, y, 1 << 16);
        if (na != nb || na < 0) same = false;
        else if (!na) break;
        else if (memcmp(x, y, (size_t)na)) same = false;
    }
    free(x);
    if (a >= 0) close(a);
    if (b >= 0) close(b);
    return same;
}

// Compare folder `rel` (open as fa / fb); true when anything below differs.
static bool cmp_walk(CmpJob *j, int fa, int fb, const char *rel, int depth) {
    u32 na, nb;
    bool failed = false;
    CmpEnt *va = cmp_read(fa, &na, &failed), *vb = cmp_read(fb, &nb, &failed);
    if (failed) j->nerr++;
    bool any = false;
    char sub[PATH_MAX];
    for (u32 i = 0, k = 0; (i < na || k < nb) && !atomic_load_explicit(&j->cancel, memory_order_relaxed);) {
        int c = i >= na ? 1 : k >= nb ? -1 : strcmp(va[i].name, vb[k].name);
        const char *name = c <= 0 ? va[i].name : vb[k].name;
        snprintf(sub, sizeof sub, "%s%s%s", rel, rel[0] ? "/" : "", name);
        atomic_fetch_add_explicit(&j->nitems, 1, memory_order_relaxed);
        if (c < 0) { cmp_record(j, sub, CMP_ONLY_A, S_ISDIR(va[i].mode)); any = true; i++; continue; }
        if (c > 0) { cmp_record(j, sub, CMP_ONLY_B, S_ISDIR(vb[k].mode)); any = true; k++; continue; }
        CmpEnt *ea = &va[i++], *eb = &vb[k++];
        bool da = S_ISDIR(ea->mode), db = S_ISDIR(eb->mode);
        if (da != db) { cmp_record(j, sub, CMP_TYPE, false); any = true; continue; }
        if (da && (ea->dev != j->dev[0] || eb->dev != j->dev[1])) continue; // another filesystem (/proc, a snapshot, ...): not walked
        if (da) {
            int ca = depth < 128 ? openat(fa, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
            int cb = depth < 128 ? openat(fb, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
            bool d = (ca >= 0 && cb >= 0) && cmp_walk(j, ca, cb, sub, depth + 1);
            if (ca >= 0) close(ca);
            if (cb >= 0) close(cb);
            if (d) { cmp_record(j, sub, CMP_INSIDE, true); any = true; }
            continue;
        }
        i64 dt = ea->mtime - eb->mtime;
        u8 st = CMP_SAME;
        if (j->contents && ea->size == eb->size && S_ISREG(ea->mode)) {
            if (!cmp_same_bytes(j, fa, fb, name)) st = dt > CMP_MTIME_SLACK ? CMP_A_NEWER : dt < -CMP_MTIME_SLACK ? CMP_B_NEWER : CMP_DIFF;
        } else if (dt > CMP_MTIME_SLACK) st = CMP_A_NEWER;
        else if (dt < -CMP_MTIME_SLACK) st = CMP_B_NEWER;
        else if (ea->size != eb->size) st = CMP_DIFF;
        if (st != CMP_SAME) { cmp_record(j, sub, st, false); any = true; }
    }
    for (u32 i = 0; i < na; i++) free(va[i].name);
    for (u32 i = 0; i < nb; i++) free(vb[i].name);
    free(va);
    free(vb);
    return any;
}

static u32 cmp_hash(const char *s) {
    u32 h = 2166136261u;
    while (*s) h = (h ^ (u8)*s++) * 16777619u;
    return h;
}

static void cmp_job_done_cb(void *arg); // app.c

static void *cmp_thread(void *arg) {
    CmpJob *j = arg;
    thread_background_priority();
    int fa = open(j->ra, O_RDONLY | O_DIRECTORY | O_CLOEXEC), fb = open(j->rb, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    struct stat sa, sb;
    if (fa < 0 || fb < 0 || fstat(fa, &sa) || fstat(fb, &sb)) j->nerr++;
    else {
        j->dev[0] = sa.st_dev;
        j->dev[1] = sb.st_dev;
        cmp_walk(j, fa, fb, "", 0);
    }
    if (fa >= 0) close(fa);
    if (fb >= 0) close(fb);
    j->hcap = 64;
    while (j->hcap < j->n * 2) j->hcap *= 2;
    j->ht = xcalloc(j->hcap, sizeof(u32));
    for (u32 i = 0; i < j->n; i++) {
        u32 h = cmp_hash(j->recs[i].rel) & (j->hcap - 1);
        while (j->ht[h]) h = (h + 1) & (j->hcap - 1);
        j->ht[h] = i + 1;
    }
    j->t1 = now_ns();
    atomic_store(&j->done, 1);
    main_post(cmp_job_done_cb, j); // the main thread frees it (also when cancelled)
    return NULL;
}

static CmpJob *cmp_job_start(const char *ra, const char *rb, bool contents) {
    CmpJob *j = xcalloc(1, sizeof *j);
    j->ra = strdup(ra);
    j->rb = strdup(rb);
    j->contents = contents;
    j->t0 = now_ns();
    pthread_t t;
    pthread_create(&t, NULL, cmp_thread, j);
    pthread_setname_np(t, "fe-compare");
    pthread_detach(t);
    return j;
}

static void cmp_job_free(CmpJob *j) {
    for (u32 i = 0; i < j->n; i++) free(j->recs[i].rel);
    free(j->recs);
    free(j->ht);
    free(j->ra);
    free(j->rb);
    free(j);
}

// The record of a path relative to the roots (side A's view); NULL when nothing was recorded. Done jobs only.
static const CmpRec *cmp_lookup(const CmpJob *j, const char *rel) {
    if (!j->hcap) return NULL;
    for (u32 h = cmp_hash(rel) & (j->hcap - 1); j->ht[h]; h = (h + 1) & (j->hcap - 1))
        if (!strcmp(j->recs[j->ht[h] - 1].rel, rel)) return &j->recs[j->ht[h] - 1];
    return NULL;
}

// Its state: inside a folder that's only on one side (not walked into) everything is only there too.
static u8 cmp_state_of(const CmpJob *j, const char *rel) {
    const CmpRec *r = cmp_lookup(j, rel);
    if (r) return r->st;
    char up[PATH_MAX];
    snprintf(up, sizeof up, "%s", rel);
    for (char *s = strrchr(up, '/'); s; s = strrchr(up, '/')) {
        *s = 0;
        if ((r = cmp_lookup(j, up))) return r->st == CMP_ONLY_A || r->st == CMP_ONLY_B ? r->st : CMP_SAME;
    }
    return CMP_SAME;
}

// Side B sees the mirror image.
static u8 cmp_for_side(u8 st, int side) {
    if (!side) return st;
    switch (st) {
    case CMP_ONLY_A: return CMP_ONLY_B;
    case CMP_ONLY_B: return CMP_ONLY_A;
    case CMP_A_NEWER: return CMP_B_NEWER;
    case CMP_B_NEWER: return CMP_A_NEWER;
    default: return st;
    }
}
