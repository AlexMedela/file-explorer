// diskuse.c - disk usage map: one folder tree measured on worker threads and kept as a tree of items (folders,
// their biggest files, the rest of their files summed up) that the disk usage view draws while it grows

// --- file categories (colours in lists and in the map) --------------------------------------------------------

enum { DC_OTHER, DC_IMAGE, DC_VIDEO, DC_AUDIO, DC_ARCHIVE, DC_CODE, DC_DOC, DC_EXEC, DC_COUNT };
static const struct { u32 col; const char *label, *exts; } g_dcat[DC_COUNT] = {
    {0x8C8F99, "Other", ""},
    {0xA77BF3, "Images", " png jpg jpeg gif webp svg bmp avif heic tiff ico psd xcf kra raw cr2 nef dng "},
    {0xF0627C, "Video", " mp4 mkv webm avi mov wmv flv m4v mpg mpeg "},
    {0x4FD18B, "Audio", " mp3 flac wav ogg opus m4a aac wma "},
    {0xF2A33A, "Archives", " zip tar gz xz zst 7z rar bz2 tgz deb rpm iso appimage img qcow2 vdi vmdk pkg "},
    {0x4C9BFF, "Code", " c h cpp hpp cc rs py js ts tsx jsx go java kt sh fish zig lua rb php cs swift json toml yaml yml xml html css scss sql o a so pyc class jar "},
    {0x3FC1C9, "Documents", " pdf doc docx odt txt md rtf xls xlsx ods ppt pptx odp csv epub log "},
    {0x8FD45A, "Programs", ""},
};

static int file_category(const char *name, bool exec) {
    if (exec) return DC_EXEC;
    const char *x = strrchr(name, '.');
    if (!x || x == name || !x[1]) return DC_OTHER;
    x++;
    char key[24];
    size_t n = strlen(x);
    if (n > 20) return DC_OTHER;
    key[0] = ' ';
    for (size_t i = 0; i < n; i++) key[i + 1] = (char)ascii_lower((u8)x[i]);
    key[n + 1] = ' ';
    key[n + 2] = 0;
    for (int c = 1; c < DC_COUNT; c++)
        if (g_dcat[c].exts[0] && strstr(g_dcat[c].exts, key)) return c;
    return DC_OTHER;
}

// --- the scan --------------------------------------------------------------------------------------------------

// DK_SMALL: the files of a folder too small to keep one by one, summed. DK_MOUNT: a folder on another filesystem
// (or a snapshot) that isn't measured.
enum { DK_DIR, DK_FILE, DK_SMALL, DK_MOUNT };
#define DU_KEEP 24 // files kept by name per folder (the biggest, and only those >= 1% of the folder's files)

typedef struct DuItem {
    struct DuItem *parent;
    struct DuItem *_Atomic kids; // folders: their children, published once the folder has been read
    u32 nkids;                   // valid once kids is set
    char *name;                  // NULL for DK_SMALL
    _Atomic u64 size;            // bytes on disk (folders: grows while their subtree is read)
    _Atomic u64 files;           // files inside (a file: 1; DK_SMALL: how many it sums up)
    _Atomic u32 pending;         // folders: their own read + subfolders not finished
    _Atomic u64 *cats;           // folders: bytes per file category inside (DC_COUNT; the colour of a folder)
    _Atomic u8 done;             // folders: fully measured
    u8 kind, cat;                // DK_*, DC_*
    bool err;                    // couldn't be read
    bool removed;                // deleted or trashed since (main thread)
} DuItem;

typedef struct { u64 dev; char *root, *mnt, *fstype, *src; } DuMount;

typedef struct DuScan {
    DuItem root;
    char *rootpath;
    u64 dev;               // the root's filesystem
    char *fstype, *src;    // ... its type and source (btrfs subvolumes of the same source are measured too)
    DuMount *mounts;
    int nmounts;
    u64 used;              // bytes used on the filesystem when the root is its mount point (progress), else 0
    pthread_mutex_t mtx;
    pthread_cond_t cv;
    DuItem **q;            // folders waiting to be read (FIFO: the top levels fill in first)
    u32 qhead, qtail, qcap;
    int active;            // workers reading a folder
    _Atomic int cancel;
    _Atomic int threads;   // live workers
    _Atomic u64 ndirs;     // folders read
    _Atomic u32 nerr;      // folders that couldn't be read
    _Atomic u64 last_wake;
    u64 t_start, t_done;   // t_done: main thread, once finished
    _Atomic int finished;
    pthread_mutex_t hl_mtx; // files with several hard links are counted once
    u64 *hl;
    u32 hl_cap, hl_n;
    // main thread only
    int refs;
    bool exited;  // the workers are gone (their exit callback ran)
    bool stale;   // something was added inside that the map doesn't know about: rescan to see it
    u64 t_used;   // last use (cache eviction)
} DuScan;

static void du_scan_exit_cb(void *arg); // app.c

// path is root or inside it.
static bool du_under(const char *path, const char *root) {
    size_t l = strlen(root);
    if (strncmp(path, root, l)) return false;
    return !path[l] || path[l] == '/' || (l == 1 && root[0] == '/');
}


static bool du_mount_is_snapshot(const char *s) { return s && strcasestr(s, "snapshot") != NULL; }

// /proc/self/mountinfo: "id parent major:minor root mountpoint options [optional...] - fstype source superoptions"
static void du_mounts_parse(DuScan *s, const char *text) {
    int cap = 0;
    for (const char *l = text; l && *l;) {
        const char *nl = strchr(l, '\n');
        size_t len = nl ? (size_t)(nl - l) : strlen(l);
        char line[4096];
        if (len < sizeof line) {
            memcpy(line, l, len);
            line[len] = 0;
            char *f[12] = {0};
            int nf = 0;
            for (char *tok = strtok(line, " "); tok && nf < 12; tok = strtok(NULL, " ")) {
                if (!strcmp(tok, "-")) { // the fields after the separator
                    char *fs = strtok(NULL, " "), *src = strtok(NULL, " ");
                    unsigned ma, mi;
                    if (nf >= 5 && fs && src && sscanf(f[2], "%u:%u", &ma, &mi) == 2) {
                        if (s->nmounts == cap) { cap = cap ? cap * 2 : 32; s->mounts = xrealloc(s->mounts, (size_t)cap * sizeof(DuMount)); }
                        s->mounts[s->nmounts++] = (DuMount){makedev(ma, mi), strdup(f[3]), strdup(f[4]), strdup(fs), strdup(src)};
                    }
                    break;
                }
                f[nf++] = tok;
            }
        }
        l = nl ? nl + 1 : NULL;
    }
}

static const DuMount *du_mount_of(const DuScan *s, u64 dev) {
    const DuMount *m = NULL;
    for (int i = 0; i < s->nmounts; i++)
        if (s->mounts[i].dev == dev) m = &s->mounts[i]; // the last one wins (mounted over)
    return m;
}

// A subfolder on another device: measure it when it's the same filesystem (a btrfs subvolume of the same disk),
// not when it's another drive, a virtual filesystem, a network mount or a snapshot (those share their data).
static bool du_crosses(const DuScan *s, u64 dev, const char *name) {
    if (du_mount_is_snapshot(name)) return false;
    const DuMount *m = du_mount_of(s, dev);
    if (!m) return s->fstype && !strcmp(s->fstype, "btrfs"); // a subvolume that isn't mounted on its own
    return s->src && s->fstype && !strcmp(m->src, s->src) && !strcmp(m->fstype, s->fstype) && !du_mount_is_snapshot(m->root);
}

static bool du_hardlink_seen(DuScan *s, u64 dev, u64 ino) {
    u64 key = (ino * 0x9E3779B97F4A7C15ull) ^ dev;
    if (!key) key = 1;
    pthread_mutex_lock(&s->hl_mtx);
    if ((s->hl_n + 1) * 10 >= s->hl_cap * 7) {
        u32 oc = s->hl_cap;
        u64 *old = s->hl;
        s->hl_cap = oc ? oc * 2 : 256;
        s->hl = xcalloc(s->hl_cap, sizeof(u64));
        for (u32 i = 0; i < oc; i++) {
            if (!old[i]) continue;
            u32 h = (u32)(old[i] >> 32) & (s->hl_cap - 1);
            while (s->hl[h]) h = (h + 1) & (s->hl_cap - 1);
            s->hl[h] = old[i];
        }
        free(old);
    }
    u32 h = (u32)(key >> 32) & (s->hl_cap - 1);
    bool seen = false;
    for (; s->hl[h]; h = (h + 1) & (s->hl_cap - 1))
        if (s->hl[h] == key) { seen = true; break; }
    if (!seen) { s->hl[h] = key; s->hl_n++; }
    pthread_mutex_unlock(&s->hl_mtx);
    return seen;
}

static void du_item_path(const DuScan *s, const DuItem *it, char *out, size_t cap) {
    const DuItem *chain[512];
    int n = 0;
    for (const DuItem *a = it; a && a != &s->root && n < 512; a = a->parent) chain[n++] = a;
    snprintf(out, cap, "%s", s->rootpath);
    for (int i = n - 1; i >= 0; i--) {
        size_t l = strlen(out);
        snprintf(out + l, cap - l, "%s%s", l && out[l - 1] == '/' ? "" : "/", chain[i]->name ? chain[i]->name : "");
    }
}

// A folder and its subfolders are all measured: tell its parent.
static void du_finish(DuItem *it) {
    while (it && atomic_fetch_sub(&it->pending, 1) == 1) {
        atomic_store(&it->done, 1);
        it = it->parent;
    }
}

static void du_push(DuScan *s, DuItem *it) { // (locked)
    if (s->qtail - s->qhead == s->qcap) {
        u32 oc = s->qcap, n = s->qtail - s->qhead;
        DuItem **nq = xmalloc((size_t)(oc ? oc * 2 : 256) * sizeof(DuItem *));
        for (u32 i = 0; i < n; i++) nq[i] = s->q[(s->qhead + i) & (oc - 1)];
        free(s->q);
        s->q = nq;
        s->qcap = oc ? oc * 2 : 256;
        s->qhead = 0;
        s->qtail = n;
    }
    s->q[s->qtail++ & (s->qcap - 1)] = it;
}

typedef struct { u32 name; u64 bytes; u8 cat; } DuFile;
typedef struct { u32 name; u64 bytes, dev; u8 kind; } DuDir;

static int du_file_cmp(const void *a, const void *b) {
    u64 x = ((const DuFile *)a)->bytes, y = ((const DuFile *)b)->bytes;
    return x < y ? 1 : x > y ? -1 : 0;
}

// Read one folder: its files are summed (the biggest kept by name), its subfolders become items and are queued.
static void du_read(DuScan *s, DuItem *it, char *buf) {
    char path[PATH_MAX];
    du_item_path(s, it, path, sizeof path);
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | (it == &s->root ? 0 : O_NOFOLLOW));
    if (fd < 0) {
        it->err = true;
        atomic_fetch_add(&s->nerr, 1);
        atomic_store_explicit(&it->kids, NULL, memory_order_release);
        du_finish(it);
        return;
    }
    char *names = NULL;
    u32 nlen = 0, ncap = 0;
    DuFile *fv = NULL;
    DuDir *dv = NULL;
    u32 nf = 0, fcap = 0, nd = 0, dcap = 0;
    u64 direct = 0, nfiles = 0, cbytes[DC_COUNT] = {0};
    bool complete = false;
    for (;;) {
        if (atomic_load_explicit(&s->cancel, memory_order_relaxed)) break;
        long nread = syscall(SYS_getdents64, fd, buf, 64 * 1024);
        if (nread <= 0) { complete = nread == 0; break; }
        for (long off = 0; off < nread;) {
            struct linux_dirent64_ *d = (struct linux_dirent64_ *)(buf + off);
            off += d->d_reclen;
            const char *nm = d->d_name;
            if (is_dot_or_dotdot(nm)) continue;
            struct statx sx;
            if (statx(fd, nm, AT_SYMLINK_NOFOLLOW | AT_STATX_DONT_SYNC | AT_NO_AUTOMOUNT,
                      STATX_TYPE | STATX_MODE | STATX_BLOCKS | STATX_INO | STATX_NLINK, &sx)) continue;
            u64 bytes = (u64)sx.stx_blocks * 512;
            u32 l = (u32)strlen(nm) + 1;
            if (nlen + l > ncap) { ncap = MAX(ncap * 2, nlen + l + 1024); names = xrealloc(names, ncap); }
            if (S_ISDIR(sx.stx_mode)) {
                u64 dev = makedev(sx.stx_dev_major, sx.stx_dev_minor);
                u8 kind = dev == s->dev || du_crosses(s, dev, nm) ? DK_DIR : DK_MOUNT;
                if (nd == dcap) { dcap = dcap ? dcap * 2 : 16; dv = xrealloc(dv, dcap * sizeof(DuDir)); }
                memcpy(names + nlen, nm, l);
                dv[nd++] = (DuDir){nlen, kind == DK_DIR ? bytes : 0, dev, kind};
                nlen += l;
                if (kind == DK_DIR) direct += bytes; // the folder itself (its entries)
                continue;
            }
            if (sx.stx_nlink > 1 && du_hardlink_seen(s, makedev(sx.stx_dev_major, sx.stx_dev_minor), sx.stx_ino)) bytes = 0;
            if (nf == fcap) { fcap = fcap ? fcap * 2 : 32; fv = xrealloc(fv, fcap * sizeof(DuFile)); }
            memcpy(names + nlen, nm, l);
            fv[nf++] = (DuFile){nlen, bytes, (u8)file_category(nm, S_ISREG(sx.stx_mode) && (sx.stx_mode & 0111))};
            cbytes[fv[nf - 1].cat] += bytes;
            nlen += l;
            direct += bytes;
            nfiles++;
        }
    }
    close(fd);
    (void)complete;
    u64 fsum = 0;
    for (u32 i = 0; i < nf; i++) fsum += fv[i].bytes;
    if (nf) qsort(fv, nf, sizeof(DuFile), du_file_cmp);
    u32 keep = 0;
    while (keep < nf && keep < DU_KEEP && fv[keep].bytes && fv[keep].bytes * 100 >= fsum) keep++;
    u32 nsmall = nf - keep;
    u32 n = nd + keep + (nsmall ? 1 : 0);
    DuItem *kids = n ? xcalloc(n, sizeof(DuItem)) : NULL;
    u32 k = 0, ndesc = 0;
    for (u32 i = 0; i < nd; i++, k++) {
        DuItem *c = &kids[k];
        c->parent = it;
        c->name = strdup(names + dv[i].name);
        c->kind = dv[i].kind;
        atomic_store_explicit(&c->size, dv[i].bytes, memory_order_relaxed);
        if (c->kind == DK_DIR) {
            atomic_store_explicit(&c->pending, 1, memory_order_relaxed);
            c->cats = xcalloc(DC_COUNT, sizeof(_Atomic u64));
            ndesc++;
        }
        else atomic_store_explicit(&c->done, 1, memory_order_relaxed);
    }
    for (u32 i = 0; i < keep; i++, k++) {
        DuItem *c = &kids[k];
        c->parent = it;
        c->name = strdup(names + fv[i].name);
        c->kind = DK_FILE;
        c->cat = fv[i].cat;
        atomic_store_explicit(&c->size, fv[i].bytes, memory_order_relaxed);
        atomic_store_explicit(&c->files, 1, memory_order_relaxed);
        atomic_store_explicit(&c->done, 1, memory_order_relaxed);
    }
    if (nsmall) {
        DuItem *c = &kids[k++];
        c->parent = it;
        c->kind = DK_SMALL;
        u64 sum = 0;
        for (u32 i = keep; i < nf; i++) sum += fv[i].bytes;
        atomic_store_explicit(&c->size, sum, memory_order_relaxed);
        atomic_store_explicit(&c->files, nsmall, memory_order_relaxed);
        atomic_store_explicit(&c->done, 1, memory_order_relaxed);
    }
    free(names);
    free(fv);
    free(dv);
    it->nkids = n;
    atomic_store_explicit(&it->kids, kids, memory_order_release);
    for (DuItem *a = it; a; a = a->parent) { // the folder's own share counts in every folder above it
        atomic_fetch_add_explicit(&a->size, direct, memory_order_relaxed);
        atomic_fetch_add_explicit(&a->files, nfiles, memory_order_relaxed);
        for (int c = 0; c < DC_COUNT; c++)
            if (cbytes[c]) atomic_fetch_add_explicit(&a->cats[c], cbytes[c], memory_order_relaxed);
    }
    atomic_fetch_add_explicit(&it->pending, ndesc, memory_order_relaxed); // before a subfolder can finish
    if (ndesc) {
        pthread_mutex_lock(&s->mtx);
        for (u32 i = 0; i < n; i++)
            if (kids[i].kind == DK_DIR) du_push(s, &kids[i]);
        pthread_cond_broadcast(&s->cv);
        pthread_mutex_unlock(&s->mtx);
    }
    atomic_fetch_add_explicit(&s->ndirs, 1, memory_order_relaxed);
    du_finish(it);
    u64 t = now_ns(), lw = atomic_load_explicit(&s->last_wake, memory_order_relaxed);
    if (t - lw > 100000000ull && atomic_compare_exchange_strong(&s->last_wake, &lw, t)) main_wake();
}

static void *du_worker(void *arg) {
    DuScan *s = arg;
    thread_background_priority();
    char *buf = xmalloc(64 * 1024);
    for (;;) {
        pthread_mutex_lock(&s->mtx);
        while (s->qhead == s->qtail && s->active > 0 && !atomic_load(&s->cancel)) pthread_cond_wait(&s->cv, &s->mtx);
        if (s->qhead == s->qtail || atomic_load(&s->cancel)) {
            pthread_cond_broadcast(&s->cv);
            pthread_mutex_unlock(&s->mtx);
            break;
        }
        DuItem *it = s->q[s->qhead++ & (s->qcap - 1)];
        s->active++;
        pthread_mutex_unlock(&s->mtx);
        du_read(s, it, buf);
        pthread_mutex_lock(&s->mtx);
        s->active--;
        if (s->qhead == s->qtail && !s->active) pthread_cond_broadcast(&s->cv);
        pthread_mutex_unlock(&s->mtx);
    }
    free(buf);
    if (atomic_fetch_sub(&s->threads, 1) == 1) {
        atomic_store(&s->finished, 1);
        main_post(du_scan_exit_cb, s); // the last one out: the main thread frees the scan once nobody shows it
    }
    return NULL;
}

// Before the first folder is read (on a worker: a stalled network mount mustn't block the window): the root's
// filesystem, the mount table, and the used space when the root is a mount point (for the progress).
static void du_setup(DuScan *s) {
    const char *root = s->rootpath;
    struct statx sx;
    if (statx(AT_FDCWD, root, AT_STATX_DONT_SYNC, STATX_BLOCKS, &sx) == 0) {
        s->dev = makedev(sx.stx_dev_major, sx.stx_dev_minor);
        atomic_store(&s->root.size, (u64)sx.stx_blocks * 512);
    }
    const char *mi = getenv("FE_MOUNTINFO"); // tests: a made-up mount table
    int fd = open(mi ? mi : "/proc/self/mountinfo", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        size_t cap = 1 << 16, len = 0;
        char *t = xmalloc(cap);
        for (ssize_t r; (r = read(fd, t + len, cap - len - 1)) > 0;) {
            len += (size_t)r;
            if (len + 1 >= cap) t = xrealloc(t, cap *= 2);
        }
        t[len] = 0;
        close(fd);
        du_mounts_parse(s, t);
        free(t);
    }
    const DuMount *m = du_mount_of(s, s->dev);
    if (m) {
        s->fstype = strdup(m->fstype);
        s->src = strdup(m->src);
        struct statvfs vs;
        if (!strcmp(m->mnt, root) && statvfs(root, &vs) == 0) s->used = (u64)(vs.f_blocks - vs.f_bfree) * vs.f_frsize;
    }
}

static void *du_first_worker(void *arg) {
    DuScan *s = arg;
    du_setup(s);
    pthread_mutex_lock(&s->mtx);
    du_push(s, &s->root);
    s->active--; // (the setup counted as busy: the others wait for the root instead of leaving)
    pthread_cond_broadcast(&s->cv);
    pthread_mutex_unlock(&s->mtx);
    return du_worker(s);
}

static DuScan *du_scan_start(const char *root) {
    DuScan *s = xcalloc(1, sizeof *s);
    s->rootpath = strdup(root);
    s->root.name = s->rootpath;
    s->root.kind = DK_DIR;
    s->root.cats = xcalloc(DC_COUNT, sizeof(_Atomic u64));
    pthread_mutex_init(&s->mtx, NULL);
    pthread_cond_init(&s->cv, NULL);
    pthread_mutex_init(&s->hl_mtx, NULL);
    s->t_start = now_ns();
    atomic_store(&s->root.pending, 1);
    s->active = 1;
    int n = 4;
    atomic_store(&s->threads, n);
    for (int i = 0; i < n; i++) {
        pthread_t t;
        pthread_create(&t, NULL, i ? du_worker : du_first_worker, s);
        pthread_setname_np(t, "fe-du");
        pthread_detach(t);
    }
    return s;
}

static void du_item_free(DuItem *it) {
    DuItem *k = atomic_load(&it->kids);
    for (u32 i = 0; k && i < it->nkids; i++) du_item_free(&k[i]);
    free(k);
    free((void *)it->cats);
    if (it->parent) free(it->name);
}

static void du_scan_free(DuScan *s) {
    du_item_free(&s->root);
    free(s->rootpath);
    for (int i = 0; i < s->nmounts; i++) {
        free(s->mounts[i].root);
        free(s->mounts[i].mnt);
        free(s->mounts[i].fstype);
        free(s->mounts[i].src);
    }
    free(s->mounts);
    free(s->fstype);
    free(s->src);
    free(s->q);
    free(s->hl);
    pthread_mutex_destroy(&s->mtx);
    pthread_cond_destroy(&s->cv);
    pthread_mutex_destroy(&s->hl_mtx);
    free(s);
}

// The item for `path` inside the scan; NULL when it's outside or not read yet.
static DuItem *du_find(DuScan *s, const char *path) {
    size_t rl = strlen(s->rootpath);
    if (strncmp(path, s->rootpath, rl)) return NULL;
    const char *rest = path + rl;
    if (rl == 1) rest = path + 1; // root "/"
    else if (*rest == '/') rest++;
    else if (*rest) return NULL;
    DuItem *it = &s->root;
    while (*rest) {
        size_t l = strcspn(rest, "/");
        DuItem *k = atomic_load_explicit(&it->kids, memory_order_acquire), *hit = NULL;
        for (u32 i = 0; k && i < it->nkids; i++)
            if (k[i].name && !k[i].removed && strlen(k[i].name) == l && !memcmp(k[i].name, rest, l)) { hit = &k[i]; break; }
        if (!hit) return NULL;
        it = hit;
        rest += l;
        while (*rest == '/') rest++;
    }
    return it;
}

// Deleted or trashed: out of the map, and its size out of every folder above it (main thread).
static void du_remove(DuItem *it) {
    if (it->removed || !it->parent) return;
    it->removed = true;
    u64 sz = atomic_load(&it->size), nf = atomic_load(&it->files), cb[DC_COUNT] = {0};
    if (it->cats) for (int c = 0; c < DC_COUNT; c++) cb[c] = atomic_load(&it->cats[c]);
    else if (it->kind == DK_FILE) cb[it->cat] = sz;
    for (DuItem *a = it->parent; a; a = a->parent) {
        atomic_fetch_sub(&a->size, MIN(sz, atomic_load(&a->size)));
        atomic_fetch_sub(&a->files, MIN(nf, atomic_load(&a->files)));
        for (int c = 0; c < DC_COUNT; c++)
            if (cb[c]) atomic_fetch_sub(&a->cats[c], MIN(cb[c], atomic_load(&a->cats[c])));
    }
}

// The file category that fills most of a folder (DC_OTHER when none has half of it) and its share.
static int du_main_category(const DuItem *it, f32 *share) {
    *share = 0;
    if (!it->cats) return DC_OTHER;
    u64 sum = 0, best = 0;
    int bc = DC_OTHER;
    for (int c = 0; c < DC_COUNT; c++) {
        u64 b = atomic_load_explicit(&it->cats[c], memory_order_relaxed);
        sum += b;
        if (b > best) { best = b; bc = c; }
    }
    if (!sum || best * 2 < sum) return DC_OTHER;
    *share = (f32)((f64)best / (f64)sum);
    return bc;
}
