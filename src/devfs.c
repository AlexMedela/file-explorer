// devfs.c - a phone or camera as a folder: `file-explorer --device-mount SYSNAME DIR` claims the USB device,
// speaks its protocol (iphone.c: AFC; mtp.c: MTP / PTP) and serves it with FUSE at DIR/<device name> until it's
// unmounted or unplugged. It tells the app how it's going on stdout: "wait trust" / "wait unlock" while the
// user has to do something, then "ok PATH" or "error KIND: text" (KIND: busy, perm, io, unsupported, denied,
// locked, trust).

#define FUSE_USE_VERSION 31
#include <fuse.h>

// --- stat cache (paths on the device; listings fill it, so the stat() after a readdir doesn't ask again) -------

#define DFS_TTL_NS 3000000000ull
typedef struct DfsEnt { struct DfsEnt *next; char *path; struct stat st; u64 t; } DfsEnt;
static DfsEnt *g_dfs_cache[4096];
static pthread_mutex_t g_dfs_cm = PTHREAD_MUTEX_INITIALIZER;

static u32 dfs_hash(const char *s) { u32 h = 2166136261u; for (; *s; s++) h = (h ^ (u8)*s) * 16777619u; return h % ARRAY_COUNT(g_dfs_cache); }

static void dfs_cache_put(const char *path, const struct stat *st) {
    pthread_mutex_lock(&g_dfs_cm);
    u32 h = dfs_hash(path);
    DfsEnt *e = g_dfs_cache[h];
    while (e && strcmp(e->path, path)) e = e->next;
    if (!e) { e = xcalloc(1, sizeof *e); e->path = strdup(path); e->next = g_dfs_cache[h]; g_dfs_cache[h] = e; }
    e->st = *st;
    e->t = now_ns();
    pthread_mutex_unlock(&g_dfs_cm);
}

static bool dfs_cache_get(const char *path, struct stat *st) {
    pthread_mutex_lock(&g_dfs_cm);
    DfsEnt *e = g_dfs_cache[dfs_hash(path)];
    while (e && strcmp(e->path, path)) e = e->next;
    bool ok = e && now_ns() - e->t < DFS_TTL_NS;
    if (ok) *st = e->st;
    pthread_mutex_unlock(&g_dfs_cm);
    return ok;
}

// Forget a path and everything under it (after delete / rename / write).
static void dfs_cache_drop(const char *path) {
    size_t l = strlen(path);
    pthread_mutex_lock(&g_dfs_cm);
    for (size_t i = 0; i < ARRAY_COUNT(g_dfs_cache); i++)
        for (DfsEnt **pe = &g_dfs_cache[i]; *pe;) {
            DfsEnt *e = *pe;
            if (!strncmp(e->path, path, l) && (e->path[l] == 0 || e->path[l] == '/' || path[l - 1] == '/')) { *pe = e->next; free(e->path); free(e); }
            else pe = &e->next;
        }
    pthread_mutex_unlock(&g_dfs_cm);
}

static void dfs_split(const char *path, char *parent, size_t pcap, const char **name) {
    const char *sl = strrchr(path, '/');
    snprintf(parent, pcap, "%.*s", sl && sl != path ? (int)(sl - path) : 1, sl && sl != path ? path : "/");
    *name = sl ? sl + 1 : path;
}

typedef struct { // an open file
    pthread_mutex_t mtx; // FUSE may read one file from several threads at once (readahead)
    u64 h;           // AFC handle
    void *afc;       // its AFC connection
    u64 pos;         // AFC: where the device's file position is
    // MTP: what it is, a read-ahead buffer, the local copy being written
    u32 storage, handle, parent;
    u64 size;
    char name[256];
    u8 *ra;
    u64 ra_off;
    size_t ra_len;
    int tmp;         // local copy (writes, or a whole download on devices without partial reads), -1 none
    bool dirty;
    time_t mtime;
} DfsFile;

static uid_t g_dfs_uid;
static gid_t g_dfs_gid;
static char g_dfs_kind;    // 'a' AFC, 'm' / 'c' MTP / PTP
static _Atomic bool g_dfs_dead;

static void dfs_fill_owner(struct stat *st) {
    st->st_uid = g_dfs_uid;
    st->st_gid = g_dfs_gid;
    if (!st->st_blocks) st->st_blocks = (blkcnt_t)((st->st_size + 511) / 512);
    st->st_blksize = 65536;
}

// --- iPhone: AFC (media area), plus "App Documents/<app>" for apps that share files ---------------------------

#define DFS_APPS "App Documents"
#define AFC_POOL 4

static struct {
    Usb usb;
    Mux *mux;
    Lockdown ld;
    Afc *pool[AFC_POOL];
    int npool;
    _Atomic u32 rr;
    pthread_mutex_t am;   // apps
    IosApp *apps;
    int napps;
    u64 apps_t;
    struct { char id[160]; Afc *afc; } appafc[64];
    int nappafc;
    Afc *old[64];        // replaced broken connections
    int nold;
} g_ios;

static pthread_mutex_t g_ios_pm = PTHREAD_MUTEX_INITIALIZER;

// A media-area connection, in turn. One that broke (an answer went missing) is replaced by a new one; the old one
// is kept (open files may point at it) and freed at exit.
static Afc *ios_pick(void) {
    u32 i = atomic_fetch_add(&g_ios.rr, 1) % (u32)g_ios.npool;
    Afc *a = g_ios.pool[i];
    if (!a->broken || atomic_load(&g_ios.mux->dead)) return a;
    pthread_mutex_lock(&g_ios_pm);
    if (g_ios.pool[i]->broken && g_ios.nold < (int)ARRAY_COUNT(g_ios.old)) {
        Svc s;
        char err[256];
        if (ld_service(&g_ios.ld, "com.apple.afc", &s, err, sizeof err)) {
            g_ios.old[g_ios.nold++] = g_ios.pool[i];
            g_ios.pool[i] = afc_new(&s);
            dev_log("afc: connection %u replaced (%p -> %p)", i, (void *)g_ios.old[g_ios.nold - 1], (void *)g_ios.pool[i]);
        }
    }
    a = g_ios.pool[i];
    pthread_mutex_unlock(&g_ios_pm);
    return a;
}

static void ios_apps_refresh(void) { // (am held)
    if (g_ios.apps && now_ns() - g_ios.apps_t < 30000000000ull) return;
    IosApp *a = NULL;
    char err[256];
    int n = ios_list_apps(&g_ios.ld, &a, err, sizeof err);
    if (n < 0) return;
    free(g_ios.apps);
    g_ios.apps = a;
    g_ios.napps = n;
    g_ios.apps_t = now_ns();
}

// Where a path lives: which AFC connection, the path there. 1 = a real path in the media area, 2 = in an app's
// Documents, 0 = "/App Documents" itself, -ENOENT otherwise.
static int ios_resolve(const char *path, Afc **afc, char *real, size_t cap) {
    size_t al = strlen("/" DFS_APPS);
    if (strncmp(path, "/" DFS_APPS, al) || (path[al] && path[al] != '/')) {
        *afc = ios_pick();
        snprintf(real, cap, "%s", path);
        return 1;
    }
    if (!path[al] || !path[al + 1]) return 0; // "/App Documents"
    const char *app = path + al + 1, *rest = strchr(app, '/');
    size_t nl = rest ? (size_t)(rest - app) : strlen(app);
    pthread_mutex_lock(&g_ios.am);
    ios_apps_refresh();
    const IosApp *found = NULL;
    for (int i = 0; i < g_ios.napps && !found; i++)
        if (strlen(g_ios.apps[i].name) == nl && !strncmp(g_ios.apps[i].name, app, nl)) found = &g_ios.apps[i];
    Afc *a = NULL;
    for (int i = 0; found && i < g_ios.nappafc && !a; i++) if (!strcmp(g_ios.appafc[i].id, found->id)) a = g_ios.appafc[i].afc;
    if (found && !a && g_ios.nappafc < (int)ARRAY_COUNT(g_ios.appafc)) {
        char err[256];
        a = ios_app_documents(&g_ios.ld, found->id, err, sizeof err);
        if (a) {
            snprintf(g_ios.appafc[g_ios.nappafc].id, sizeof g_ios.appafc[0].id, "%s", found->id);
            g_ios.appafc[g_ios.nappafc++].afc = a;
        }
    }
    pthread_mutex_unlock(&g_ios.am);
    if (!found || !a) return -ENOENT;
    *afc = a;
    snprintf(real, cap, "/Documents%s", rest ? rest : "");
    return 2;
}

static void dfs_dir_stat(struct stat *st) {
    memset(st, 0, sizeof *st);
    st->st_mode = S_IFDIR | 0755;
    st->st_nlink = 2;
    st->st_mtim.tv_sec = time(NULL);
    dfs_fill_owner(st);
}

static int ios_getattr(const char *path, struct stat *st) {
    if (dfs_cache_get(path, st)) return 0;
    Afc *a;
    char real[PATH_MAX];
    int r = ios_resolve(path, &a, real, sizeof real);
    if (r < 0) return r;
    if (r == 0) { dfs_dir_stat(st); return 0; }
    r = afc_info(a, real, st);
    if (r) return r;
    if (S_ISLNK(st->st_mode)) st->st_mode = S_IFREG | 0644; // links aren't followed: shown as small files
    dfs_fill_owner(st);
    dfs_cache_put(path, st);
    return 0;
}

typedef struct { Afc *afc; const char *dir; char **names; int n, first, step; struct stat *st; int *ok; } IosStatJob;

static void *ios_stat_worker(void *arg) {
    IosStatJob *j = arg;
    char p[PATH_MAX];
    for (int i = j->first; i < j->n; i += j->step) {
        snprintf(p, sizeof p, "%s%s%s", j->dir, strcmp(j->dir, "/") ? "/" : "", j->names[i]);
        j->ok[i] = afc_info(j->afc, p, &j->st[i]) == 0;
    }
    return NULL;
}

static int ios_readdir(const char *path, void *buf, fuse_fill_dir_t filler) {
    Afc *a;
    char real[PATH_MAX];
    int r = ios_resolve(path, &a, real, sizeof real);
    if (r < 0) return r;
    struct stat ds;
    dfs_dir_stat(&ds);
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    if (r == 0) { // "/App Documents": the apps
        pthread_mutex_lock(&g_ios.am);
        ios_apps_refresh();
        for (int i = 0; i < g_ios.napps; i++) if (!strchr(g_ios.apps[i].name, '/')) filler(buf, g_ios.apps[i].name, &ds, 0, FUSE_FILL_DIR_PLUS);
        pthread_mutex_unlock(&g_ios.am);
        return 0;
    }
    char *names;
    size_t nl;
    int n = afc_list(a, real, &names, &nl);
    if (n < 0) return n;
    char **v = xmalloc((size_t)(n ? n : 1) * sizeof *v);
    int k = 0;
    for (size_t o = 0; o < nl && k < n; o += strlen(names + o) + 1) v[k++] = names + o;
    n = k;
    struct stat *st = xcalloc((size_t)(n ? n : 1), sizeof *st);
    int *ok = xcalloc((size_t)(n ? n : 1), sizeof *ok);
    // stat every entry, spread over the connections (each one answers one request at a time; an app has one)
    bool app = r == 2;
    int workers = app ? 1 : MIN(g_ios.npool, MAX(1, n / 8));
    IosStatJob jobs[AFC_POOL];
    pthread_t th[AFC_POOL];
    for (int w = 0; w < workers; w++) {
        jobs[w] = (IosStatJob){app ? a : ios_pick(), real, v, n, w, workers, st, ok};
        if (w && pthread_create(&th[w], NULL, ios_stat_worker, &jobs[w])) { jobs[w].step = 0; }
    }
    ios_stat_worker(&jobs[0]);
    for (int w = 1; w < workers; w++) if (jobs[w].step) pthread_join(th[w], NULL);
    if (workers > 1) for (int w = 1; w < workers; w++) if (!jobs[w].step) { jobs[w].step = workers; ios_stat_worker(&jobs[w]); }
    char p[PATH_MAX];
    if (!strcmp(path, "/")) filler(buf, DFS_APPS, &ds, 0, FUSE_FILL_DIR_PLUS);
    for (int i = 0; i < n; i++) {
        snprintf(p, sizeof p, "%s%s%s", path, strcmp(path, "/") ? "/" : "", v[i]);
        if (ok[i]) {
            if (S_ISLNK(st[i].st_mode)) st[i].st_mode = S_IFREG | 0644;
            dfs_fill_owner(&st[i]);
            dfs_cache_put(p, &st[i]);
        }
        filler(buf, v[i], ok[i] ? &st[i] : NULL, 0, ok[i] ? FUSE_FILL_DIR_PLUS : 0);
    }
    free(st); free(ok); free(v); free(names);
    return 0;
}

static int ios_open(const char *path, int flags, bool create, DfsFile **out) {
    Afc *a;
    char real[PATH_MAX];
    int r = ios_resolve(path, &a, real, sizeof real);
    if (r < 0) return r;
    if (r == 0) return -EISDIR;
    int acc = flags & O_ACCMODE;
    u64 mode = acc == O_RDONLY ? AFC_RDONLY
             : flags & O_APPEND ? AFC_APPEND
             : (flags & O_TRUNC) || create ? (acc == O_WRONLY ? AFC_WRONLY_TRUNC : AFC_RW_TRUNC)
                                           : AFC_RW;
    if (create && (flags & O_EXCL)) {
        struct stat st;
        if (afc_info(a, real, &st) == 0) return -EEXIST;
    }
    DfsFile *f = xcalloc(1, sizeof *f);
    f->tmp = -1;
    f->afc = a;
    r = afc_open(a, real, mode, &f->h);
    if (r) { free(f); return r; }
    if (mode != AFC_RDONLY) dfs_cache_drop(path);
    *out = f;
    return 0;
}

static int ios_read(DfsFile *f, char *buf, size_t size, off_t off) {
    Afc *a = f->afc;
    if (f->pos != (u64)off) {
        int r = afc_seek(a, f->h, off);
        if (r) return r;
        f->pos = (u64)off;
    }
    ssize_t n = afc_read(a, f->h, buf, size);
    if (n > 0) f->pos += (u64)n;
    return (int)n;
}

static int ios_write(DfsFile *f, const char *buf, size_t size, off_t off) {
    Afc *a = f->afc;
    if (f->pos != (u64)off) {
        int r = afc_seek(a, f->h, off);
        if (r) return r;
        f->pos = (u64)off;
    }
    ssize_t n = afc_write(a, f->h, buf, size);
    if (n > 0) f->pos += (u64)n;
    return (int)n;
}

static int ios_path_op(const char *path, int op) { // remove / mkdir
    Afc *a;
    char real[PATH_MAX];
    int r = ios_resolve(path, &a, real, sizeof real);
    if (r <= 0) return r < 0 ? r : -EACCES;
    dfs_cache_drop(path);
    return -afc_errno(afc_op(a, (u64)op, real, strlen(real) + 1, NULL, 0, NULL, NULL));
}

static int ios_rename(const char *from, const char *to) {
    Afc *a, *b;
    char rf[PATH_MAX], rt[PATH_MAX];
    int r1 = ios_resolve(from, &a, rf, sizeof rf), r2 = ios_resolve(to, &b, rt, sizeof rt);
    if (r1 < 0) return r1;
    if (r2 < 0) return r2;
    if (r1 == 0 || r2 == 0) return -EACCES;
    if (!(r1 == 1 && r2 == 1) && a != b) return -EXDEV; // between the media area and an app, or two apps: copied instead
    struct stat st;
    if (afc_info(a, rt, &st) == 0 && !S_ISDIR(st.st_mode)) afc_path_op(a, AFC_REMOVE, rt); // rename() replaces
    dfs_cache_drop(from);
    dfs_cache_drop(to);
    return afc_rename(a, rf, rt);
}

// Talk to the iPhone on g_ios.usb (claimed, or a stand-in): usbmux, lockdown (pairing if needed), AFC connections.
static bool ios_start_on(char *name, size_t ncap, char *err, size_t ecap, ProgressFn progress) {
    g_ios.mux = mux_open(&g_ios.usb, err, ecap);
    if (!g_ios.mux) return false;
    if (!ld_open(g_ios.mux, &g_ios.ld, progress, 120, err, ecap)) return false;
    pthread_mutex_init(&g_ios.am, NULL);
    for (int i = 0; i < AFC_POOL; i++) {
        Svc s;
        if (!ld_service(&g_ios.ld, "com.apple.afc", &s, err, ecap)) break;
        g_ios.pool[g_ios.npool++] = afc_new(&s);
    }
    if (!g_ios.npool) return false;
    snprintf(name, ncap, "%s", g_ios.ld.name);
    return true;
}

static bool ios_start(const UsbDevInfo *info, char *name, size_t ncap, char *err, size_t ecap, ProgressFn progress) {
    return usb_open(info, &g_ios.usb, true, err, ecap) && ios_start_on(name, ncap, err, ecap, progress); // (usbmuxd lets go)
}

// --- Android / cameras: MTP / PTP objects by path ---------------------------------------------------------------

typedef struct DfsDir { struct DfsDir *next; char *path; PtpObj *kids; int n; u64 t; u32 storage, handle; } DfsDir;

static struct {
    Usb usb;
    Ptp ptp;
    PtpStorage st[8];
    int nst;
    pthread_mutex_t m;   // listings
    DfsDir *dirs;
    char cache_dir[PATH_MAX];
} g_mtp;

typedef struct { u32 storage, handle; bool dir; u64 size; time_t mtime; } MtpNode; // handle PTP_ROOT: a storage's top

static void mtp_dir_free(DfsDir *d) { free(d->path); free(d->kids); free(d); }

static void mtp_forget(const char *path) { // a listing (and those below it) is stale
    size_t l = strlen(path);
    pthread_mutex_lock(&g_mtp.m);
    for (DfsDir **pd = &g_mtp.dirs; *pd;) {
        DfsDir *d = *pd;
        if (!strncmp(d->path, path, l) && (d->path[l] == 0 || d->path[l] == '/')) { *pd = d->next; mtp_dir_free(d); }
        else pd = &d->next;
    }
    pthread_mutex_unlock(&g_mtp.m);
    dfs_cache_drop(path);
}

static void mtp_forget_parent(const char *path) {
    char parent[PATH_MAX];
    const char *nm;
    dfs_split(path, parent, sizeof parent, &nm);
    pthread_mutex_lock(&g_mtp.m);
    for (DfsDir **pd = &g_mtp.dirs; *pd;) {
        DfsDir *d = *pd;
        if (!strcmp(d->path, parent)) { *pd = d->next; mtp_dir_free(d); }
        else pd = &d->next;
    }
    pthread_mutex_unlock(&g_mtp.m);
    dfs_cache_drop(path);
}

// The listing of a folder node (cached a few seconds). Caller frees nothing; holds g_mtp.m.
static DfsDir *mtp_listing(const char *path, u32 storage, u32 handle, int *err) {
    for (DfsDir *d = g_mtp.dirs; d; d = d->next)
        if (!strcmp(d->path, path) && now_ns() - d->t < DFS_TTL_NS) return d;
    for (DfsDir **pd = &g_mtp.dirs; *pd;) { // a stale one goes
        if (!strcmp((*pd)->path, path)) { DfsDir *d = *pd; *pd = d->next; mtp_dir_free(d); }
        else pd = &(*pd)->next;
    }
    PtpObj *kids;
    int n = ptp_list(&g_mtp.ptp, storage, handle, &kids);
    if (n < 0) { *err = n; return NULL; }
    DfsDir *d = xcalloc(1, sizeof *d);
    d->path = strdup(path);
    d->kids = kids;
    d->n = n;
    d->t = now_ns();
    d->storage = storage;
    d->handle = handle;
    d->next = g_mtp.dirs;
    g_mtp.dirs = d;
    return d;
}

// "/" (one storage: its top; several: a folder per storage), then objects by name.
static int mtp_resolve(const char *path, MtpNode *out) {
    memset(out, 0, sizeof *out);
    const char *p = path;
    while (*p == '/') p++;
    if (g_mtp.nst == 1) { out->storage = g_mtp.st[0].id; out->handle = PTP_ROOT; out->dir = true; }
    else if (!*p) { out->dir = true; return 0; } // the storages
    else {
        size_t l = strcspn(p, "/");
        int si = -1;
        for (int i = 0; i < g_mtp.nst && si < 0; i++) if (strlen(g_mtp.st[i].name) == l && !strncmp(g_mtp.st[i].name, p, l)) si = i;
        if (si < 0) return -ENOENT;
        out->storage = g_mtp.st[si].id;
        out->handle = PTP_ROOT;
        out->dir = true;
        p += l;
        while (*p == '/') p++;
    }
    char sofar[PATH_MAX];
    snprintf(sofar, sizeof sofar, "%.*s", (int)(p - path), path);
    size_t sl = strlen(sofar);
    while (sl > 1 && sofar[sl - 1] == '/') sofar[--sl] = 0;
    if (!sl) snprintf(sofar, sizeof sofar, "/");
    pthread_mutex_lock(&g_mtp.m);
    while (*p) {
        size_t l = strcspn(p, "/");
        if (!out->dir) { pthread_mutex_unlock(&g_mtp.m); return -ENOTDIR; }
        int err = 0;
        DfsDir *d = mtp_listing(sofar, out->storage, out->handle, &err);
        if (!d) { pthread_mutex_unlock(&g_mtp.m); return err; }
        const PtpObj *o = NULL;
        for (int i = 0; i < d->n && !o; i++) if (strlen(d->kids[i].name) == l && !strncmp(d->kids[i].name, p, l)) o = &d->kids[i];
        if (!o) { pthread_mutex_unlock(&g_mtp.m); return -ENOENT; }
        out->storage = o->storage;
        out->handle = o->handle;
        out->dir = o->format == PTP_FMT_FOLDER;
        out->size = o->size;
        out->mtime = o->mtime;
        size_t k = strlen(sofar);
        snprintf(sofar + k, sizeof sofar - k, "%s%.*s", k > 1 ? "/" : "", (int)l, p);
        p += l;
        while (*p == '/') p++;
    }
    pthread_mutex_unlock(&g_mtp.m);
    return 0;
}

static void mtp_node_stat(const MtpNode *n, struct stat *st) {
    memset(st, 0, sizeof *st);
    st->st_mode = n->dir ? S_IFDIR | 0755 : S_IFREG | 0644;
    st->st_nlink = n->dir ? 2 : 1;
    st->st_size = (off_t)n->size;
    st->st_mtim.tv_sec = n->mtime ? n->mtime : time(NULL);
    st->st_atim = st->st_ctim = st->st_mtim;
    dfs_fill_owner(st);
}

static int mtp_getattr(const char *path, struct stat *st) {
    if (dfs_cache_get(path, st)) return 0;
    MtpNode n;
    int r = mtp_resolve(path, &n);
    if (r) return r;
    mtp_node_stat(&n, st);
    dfs_cache_put(path, st);
    return 0;
}

static int mtp_readdir(const char *path, void *buf, fuse_fill_dir_t filler) {
    MtpNode n;
    int r = mtp_resolve(path, &n);
    if (r) return r;
    if (!n.dir) return -ENOTDIR;
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    struct stat st;
    if (!n.storage) { // the storages
        MtpNode sn = {.dir = true};
        mtp_node_stat(&sn, &st);
        for (int i = 0; i < g_mtp.nst; i++) filler(buf, g_mtp.st[i].name, &st, 0, FUSE_FILL_DIR_PLUS);
        return 0;
    }
    pthread_mutex_lock(&g_mtp.m);
    int err = 0;
    DfsDir *d = mtp_listing(path, n.storage, n.handle, &err);
    if (!d) { pthread_mutex_unlock(&g_mtp.m); return err; }
    char p[PATH_MAX];
    for (int i = 0; i < d->n; i++) {
        const PtpObj *o = &d->kids[i];
        if (strchr(o->name, '/')) continue;
        MtpNode kn = {o->storage, o->handle, o->format == PTP_FMT_FOLDER, o->size, o->mtime};
        mtp_node_stat(&kn, &st);
        snprintf(p, sizeof p, "%s%s%s", path, strcmp(path, "/") ? "/" : "", o->name);
        dfs_cache_put(p, &st);
        filler(buf, o->name, &st, 0, FUSE_FILL_DIR_PLUS);
    }
    pthread_mutex_unlock(&g_mtp.m);
    return 0;
}

static int mtp_tmpfile(void) { // a local copy for a file being written (unlinked at once)
    char t[PATH_MAX];
    mkdir_parents(g_mtp.cache_dir);
    snprintf(t, sizeof t, "%s/upload-XXXXXX", g_mtp.cache_dir);
    int fd = mkostemp(t, O_CLOEXEC);
    if (fd >= 0) unlink(t);
    return fd;
}

static int mtp_open(const char *path, int flags, bool create, DfsFile **out) {
    MtpNode n;
    int r = mtp_resolve(path, &n);
    if (r && !(r == -ENOENT && create)) return r;
    if (!r && n.dir) return -EISDIR;
    if (!r && create && (flags & O_EXCL)) return -EEXIST;
    char parent[PATH_MAX];
    const char *nm;
    dfs_split(path, parent, sizeof parent, &nm);
    MtpNode pn;
    if (mtp_resolve(parent, &pn) || !pn.dir || !pn.storage) return r ? -EACCES : -EIO;
    DfsFile *f = xcalloc(1, sizeof *f);
    f->tmp = -1;
    f->storage = pn.storage;
    f->parent = pn.handle;
    f->handle = r ? 0 : n.handle;
    f->size = r ? 0 : n.size;
    f->mtime = r ? 0 : n.mtime;
    snprintf(f->name, sizeof f->name, "%s", nm);
    if ((flags & O_ACCMODE) != O_RDONLY || create) {
        f->tmp = mtp_tmpfile();
        if (f->tmp < 0) { free(f); return -EIO; }
        if (f->handle && !(flags & O_TRUNC)) { // changing an existing file: work on a copy of it
            int g = ptp_get_object(&g_mtp.ptp, f->handle, f->tmp);
            if (g) { close(f->tmp); free(f); return g; }
        } else f->size = 0;
        f->dirty = create || (flags & O_TRUNC);
    }
    *out = f;
    return 0;
}

static int mtp_read(DfsFile *f, char *buf, size_t size, off_t off) {
    if (f->tmp >= 0) return (int)pread(f->tmp, buf, size, off);
    if ((u64)off >= f->size) return 0;
    size = (size_t)MIN((u64)size, f->size - (u64)off);
    size_t done = 0;
    while (done < size) {
        u64 at = (u64)off + done;
        if (f->ra && at >= f->ra_off && at < f->ra_off + f->ra_len) {
            size_t k = (size_t)MIN((u64)(size - done), f->ra_off + f->ra_len - at);
            memcpy(buf + done, f->ra + (at - f->ra_off), k);
            done += k;
            continue;
        }
        if (!f->ra) f->ra = xmalloc(1u << 20);
        size_t want = (size_t)MIN((u64)(1u << 20), f->size - at);
        ssize_t n = ptp_read(&g_mtp.ptp, f->handle, at, f->ra, want);
        if (n == -ENOTSUP) { // no partial reads (some cameras): fetch it whole once
            f->tmp = mtp_tmpfile();
            if (f->tmp < 0) return -EIO;
            int g = ptp_get_object(&g_mtp.ptp, f->handle, f->tmp);
            if (g) { close(f->tmp); f->tmp = -1; return g; }
            return (int)pread(f->tmp, buf, size, off);
        }
        if (n <= 0) return done ? (int)done : (int)(n < 0 ? n : 0);
        f->ra_off = at;
        f->ra_len = (size_t)n;
    }
    return (int)done;
}

static int mtp_write(DfsFile *f, const char *buf, size_t size, off_t off) {
    if (f->tmp < 0) return -EBADF;
    ssize_t n = pwrite(f->tmp, buf, size, off);
    if (n < 0) return -errno;
    f->dirty = true;
    if ((u64)off + (u64)n > f->size) f->size = (u64)off + (u64)n;
    return (int)n;
}

// Upload the local copy (MTP can't change a file in place: the old one is replaced).
static int mtp_flush_file(DfsFile *f, const char *path) {
    if (!f->dirty || f->tmp < 0) return 0;
    struct stat st;
    if (fstat(f->tmp, &st)) return -errno;
    if (f->handle) {
        int r = ptp_delete(&g_mtp.ptp, f->handle);
        if (r && r != -ENOENT) return r;
        f->handle = 0;
    }
    u32 h = 0;
    int r = ptp_create(&g_mtp.ptp, f->storage, f->parent, f->name, false, f->tmp, (u64)st.st_size, f->mtime, &h);
    mtp_forget_parent(path);
    if (r) return r;
    f->handle = h;
    f->size = (u64)st.st_size;
    f->dirty = false;
    return 0;
}

static int mtp_remove(const char *path, bool dir) {
    MtpNode n;
    int r = mtp_resolve(path, &n);
    if (r) return r;
    if (dir != n.dir) return dir ? -ENOTDIR : -EISDIR;
    if (n.handle == PTP_ROOT || !n.storage) return -EACCES;
    if (dir) {
        pthread_mutex_lock(&g_mtp.m);
        int err = 0;
        DfsDir *d = mtp_listing(path, n.storage, n.handle, &err);
        int kids = d ? d->n : 0;
        pthread_mutex_unlock(&g_mtp.m);
        if (!d) return err;
        if (kids) return -ENOTEMPTY;
    }
    r = ptp_delete(&g_mtp.ptp, n.handle);
    mtp_forget(path);
    mtp_forget_parent(path);
    return r;
}

static int mtp_mkdir(const char *path) {
    MtpNode n;
    if (mtp_resolve(path, &n) == 0) return -EEXIST;
    char parent[PATH_MAX];
    const char *nm;
    dfs_split(path, parent, sizeof parent, &nm);
    MtpNode pn;
    int r = mtp_resolve(parent, &pn);
    if (r) return r;
    if (!pn.dir || !pn.storage) return -EACCES;
    u32 h;
    r = ptp_create(&g_mtp.ptp, pn.storage, pn.handle, nm, true, -1, 0, 0, &h);
    mtp_forget_parent(path);
    return r;
}

static int mtp_rename(const char *from, const char *to) {
    MtpNode n, dn, pn;
    int r = mtp_resolve(from, &n);
    if (r) return r;
    if (n.handle == PTP_ROOT || !n.storage) return -EACCES;
    char fp[PATH_MAX], tp[PATH_MAX];
    const char *fname, *tname;
    dfs_split(from, fp, sizeof fp, &fname);
    dfs_split(to, tp, sizeof tp, &tname);
    if ((r = mtp_resolve(tp, &pn))) return r;
    if (!pn.dir || !pn.storage) return -EACCES;
    if (mtp_resolve(to, &dn) == 0) { // rename() replaces a file
        if (dn.dir) return -EEXIST;
        if ((r = ptp_delete(&g_mtp.ptp, dn.handle))) return r;
    }
    if (strcmp(fp, tp)) {
        if (pn.storage != n.storage) return -EXDEV;
        if ((r = ptp_move(&g_mtp.ptp, n.handle, pn.storage, pn.handle))) return r;
    }
    if (strcmp(fname, tname)) r = ptp_rename(&g_mtp.ptp, n.handle, tname);
    mtp_forget(from);
    mtp_forget_parent(from);
    mtp_forget_parent(to);
    return r == -ENOTSUP ? -EACCES : r;
}

// Talk to the device on g_mtp.usb (claimed, or a stand-in): session, storages (waiting for an unlock).
static bool mtp_start_on(const char *fallback_name, char *name, size_t ncap, char *err, size_t ecap, ProgressFn progress) {
    if (!ptp_open(&g_mtp.usb, &g_mtp.ptp, err, ecap)) return false;
    pthread_mutex_init(&g_mtp.m, NULL);
    const char *ch = getenv("XDG_CACHE_HOME");
    if (ch && ch[0]) snprintf(g_mtp.cache_dir, sizeof g_mtp.cache_dir, "%s/file-explorer", ch);
    else snprintf(g_mtp.cache_dir, sizeof g_mtp.cache_dir, "%s/.cache/file-explorer", getenv("HOME") ? getenv("HOME") : "/tmp");
    u64 until = now_ns() + 90000000000ull;
    bool told = false;
    for (;;) { // a locked Android phone shows no storage until it's unlocked
        g_mtp.nst = ptp_storages(&g_mtp.ptp, g_mtp.st, (int)ARRAY_COUNT(g_mtp.st));
        if (g_mtp.nst > 0) break;
        if (now_ns() > until) { snprintf(err, ecap, "locked: the phone didn't show its storage (unlock it)"); return false; }
        if (!told && progress) progress("unlock");
        told = true;
        usleep(1000000);
    }
    for (int i = 0; i < g_mtp.nst; i++) // names are folder names: no slashes, no duplicates
        for (char *c = g_mtp.st[i].name; *c; c++) if (*c == '/') *c = '-';
    for (int i = 1; i < g_mtp.nst; i++)
        for (int k = 0; k < i; k++)
            if (!strcmp(g_mtp.st[i].name, g_mtp.st[k].name)) { size_t l = strlen(g_mtp.st[i].name); snprintf(g_mtp.st[i].name + MIN(l, 120), 8, " %d", i + 1); }
    snprintf(name, ncap, "%s", g_mtp.ptp.model[0] ? g_mtp.ptp.model : fallback_name[0] ? fallback_name : "Phone");
    return true;
}

static bool mtp_start(const UsbDevInfo *info, char *name, size_t ncap, char *err, size_t ecap, ProgressFn progress) {
    return usb_open(info, &g_mtp.usb, false, err, ecap) && mtp_start_on(info->product, name, ncap, err, ecap, progress);
}

// --- FUSE operations ------------------------------------------------------------------------------------------

static int dfs_getattr(const char *path, struct stat *st, struct fuse_file_info *fi) {
    (void)fi;
    if (!strcmp(path, "/")) { dfs_dir_stat(st); return 0; }
    return g_dfs_kind == 'a' ? ios_getattr(path, st) : mtp_getattr(path, st);
}

static int dfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t off, struct fuse_file_info *fi, enum fuse_readdir_flags fl) {
    (void)off; (void)fi; (void)fl;
    return g_dfs_kind == 'a' ? ios_readdir(path, buf, filler) : mtp_readdir(path, buf, filler);
}

static int dfs_open_common(const char *path, struct fuse_file_info *fi, bool create) {
    DfsFile *f = NULL;
    int r = g_dfs_kind == 'a' ? ios_open(path, fi->flags, create, &f) : mtp_open(path, fi->flags, create, &f);
    if (r) return r;
    pthread_mutex_init(&f->mtx, NULL);
    fi->fh = (u64)(uintptr_t)f;
    fi->direct_io = 0;
    fi->keep_cache = 0;
    return 0;
}

static int dfs_open(const char *path, struct fuse_file_info *fi) { return dfs_open_common(path, fi, false); }
static int dfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void)mode;
    int r = dfs_open_common(path, fi, true);
    if (!r && g_dfs_kind != 'a') { // shows up at once (uploaded when it's closed)
        DfsFile *f = (DfsFile *)(uintptr_t)fi->fh;
        MtpNode n = {f->storage, 0, false, 0, time(NULL)};
        struct stat st;
        mtp_node_stat(&n, &st);
        dfs_cache_put(path, &st);
    }
    return r;
}

static int dfs_read(const char *path, char *buf, size_t size, off_t off, struct fuse_file_info *fi) {
    (void)path;
    DfsFile *f = (DfsFile *)(uintptr_t)fi->fh;
    pthread_mutex_lock(&f->mtx);
    int r = g_dfs_kind == 'a' ? ios_read(f, buf, size, off) : mtp_read(f, buf, size, off);
    pthread_mutex_unlock(&f->mtx);
    return r;
}

static int dfs_write(const char *path, const char *buf, size_t size, off_t off, struct fuse_file_info *fi) {
    DfsFile *f = (DfsFile *)(uintptr_t)fi->fh;
    pthread_mutex_lock(&f->mtx);
    int r = g_dfs_kind == 'a' ? ios_write(f, buf, size, off) : mtp_write(f, buf, size, off);
    pthread_mutex_unlock(&f->mtx);
    if (r > 0) { // its size as seen meanwhile
        struct stat st;
        if (dfs_cache_get(path, &st) && (off_t)(off + r) > st.st_size) { st.st_size = off + r; dfs_cache_put(path, &st); }
    }
    return r;
}

static int dfs_flush(const char *path, struct fuse_file_info *fi) {
    DfsFile *f = (DfsFile *)(uintptr_t)fi->fh;
    if (g_dfs_kind == 'a' || !f) return 0;
    pthread_mutex_lock(&f->mtx);
    int r = mtp_flush_file(f, path);
    pthread_mutex_unlock(&f->mtx);
    return r;
}

static int dfs_release(const char *path, struct fuse_file_info *fi) {
    DfsFile *f = (DfsFile *)(uintptr_t)fi->fh;
    if (!f) return 0;
    if (g_dfs_kind == 'a') { afc_close(f->afc, f->h); dfs_cache_drop(path); }
    else {
        mtp_flush_file(f, path);
        if (f->tmp >= 0) close(f->tmp);
        free(f->ra);
    }
    pthread_mutex_destroy(&f->mtx);
    free(f);
    return 0;
}

static int dfs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    DfsFile *f = fi ? (DfsFile *)(uintptr_t)fi->fh : NULL;
    dfs_cache_drop(path);
    if (g_dfs_kind == 'a') {
        if (f) return afc_set_size(f->afc, f->h, (u64)size);
        Afc *a;
        char real[PATH_MAX];
        int r = ios_resolve(path, &a, real, sizeof real);
        if (r <= 0) return r < 0 ? r : -EISDIR;
        return afc_truncate(a, real, (u64)size);
    }
    if (f && f->tmp >= 0) {
        pthread_mutex_lock(&f->mtx);
        int r = ftruncate(f->tmp, size) ? -errno : 0;
        if (!r) { f->size = (u64)size; f->dirty = true; }
        pthread_mutex_unlock(&f->mtx);
        return r;
    }
    struct fuse_file_info tfi = {.flags = O_WRONLY | (size == 0 ? O_TRUNC : 0)};
    int r = dfs_open_common(path, &tfi, false);
    if (r) return r;
    DfsFile *t = (DfsFile *)(uintptr_t)tfi.fh;
    if (ftruncate(t->tmp, size)) r = -errno;
    t->dirty = true;
    int fr = mtp_flush_file(t, path);
    close(t->tmp);
    free(t->ra);
    free(t);
    return r ? r : fr;
}

static int dfs_unlink(const char *path) { return g_dfs_kind == 'a' ? ios_path_op(path, AFC_REMOVE) : mtp_remove(path, false); }
static int dfs_rmdir(const char *path) {
    if (g_dfs_kind != 'a') return mtp_remove(path, true);
    Afc *a;
    char real[PATH_MAX];
    int r = ios_resolve(path, &a, real, sizeof real);
    if (r <= 0) return r < 0 ? r : -EACCES;
    char *names;
    size_t nl;
    int n = afc_list(a, real, &names, &nl);
    if (n > 0) { free(names); return -ENOTEMPTY; }
    if (n == 0) free(names);
    return ios_path_op(path, AFC_REMOVE);
}
static int dfs_mkdir(const char *path, mode_t mode) {
    (void)mode;
    return g_dfs_kind == 'a' ? ios_path_op(path, AFC_MKDIR) : mtp_mkdir(path);
}
static int dfs_rename(const char *from, const char *to, unsigned int flags) {
    if (flags) return -EINVAL; // (no RENAME_NOREPLACE / EXCHANGE)
    return g_dfs_kind == 'a' ? ios_rename(from, to) : mtp_rename(from, to);
}

static int dfs_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
    (void)fi;
    if (g_dfs_kind != 'a') return 0; // MTP keeps its own dates
    Afc *a;
    char real[PATH_MAX];
    int r = ios_resolve(path, &a, real, sizeof real);
    if (r <= 0) return r < 0 ? r : 0;
    struct timespec t = tv ? tv[1] : (struct timespec){time(NULL), 0};
    if (t.tv_nsec == UTIME_NOW) t = (struct timespec){time(NULL), 0};
    if (t.tv_nsec == UTIME_OMIT) return 0;
    dfs_cache_drop(path);
    r = afc_set_mtime(a, real, (u64)t.tv_sec * 1000000000ull + (u64)t.tv_nsec);
    return r == -ENOTSUP ? 0 : r;
}

static int dfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) { (void)path; (void)mode; (void)fi; return 0; } // no permissions there
static int dfs_chown(const char *path, uid_t u, gid_t g, struct fuse_file_info *fi) { (void)path; (void)u; (void)g; (void)fi; return 0; }

static int dfs_statfs(const char *path, struct statvfs *sv) {
    (void)path;
    memset(sv, 0, sizeof *sv);
    u64 total = 0, avail = 0, bs = 4096;
    if (g_dfs_kind == 'a') afc_devinfo(ios_pick(), &total, &avail, &bs);
    else {
        PtpStorage st[8];
        int n = ptp_storages(&g_mtp.ptp, st, 8);
        for (int i = 0; i < n; i++) { total += st[i].total; avail += st[i].avail; }
    }
    if (!bs) bs = 4096;
    sv->f_bsize = sv->f_frsize = (unsigned long)bs;
    sv->f_blocks = (fsblkcnt_t)(total / bs);
    sv->f_bfree = sv->f_bavail = (fsblkcnt_t)(avail / bs);
    sv->f_namemax = 255;
    return 0;
}

static void *dfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
    cfg->attr_timeout = 1.0;
    cfg->entry_timeout = 1.0;
    cfg->negative_timeout = 0.5;
    cfg->use_ino = 0;
    cfg->kernel_cache = 0;
    conn->max_readahead = 1u << 20;
    conn->max_write = 1u << 20;
    return NULL;
}

static const struct fuse_operations g_dfs_ops = {
    .getattr = dfs_getattr, .readdir = dfs_readdir, .open = dfs_open, .create = dfs_create, .read = dfs_read,
    .write = dfs_write, .flush = dfs_flush, .release = dfs_release, .truncate = dfs_truncate, .unlink = dfs_unlink,
    .rmdir = dfs_rmdir, .mkdir = dfs_mkdir, .rename = dfs_rename, .utimens = dfs_utimens, .chmod = dfs_chmod,
    .chown = dfs_chown, .statfs = dfs_statfs, .init = dfs_init,
};

// --- the helper process -------------------------------------------------------------------------------------

static FILE *g_dfs_out;

static void dfs_report(const char *fmt, ...) {
    if (!g_dfs_out) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_dfs_out, fmt, ap);
    va_end(ap);
    fputc('\n', g_dfs_out);
    fflush(g_dfs_out);
}

static void dfs_progress(const char *what) { dfs_report("wait %s", what); }

static char g_dfs_mp[PATH_MAX];

// The device went away: unmount (lazily: whatever has it open gets errors), which ends the FUSE loop.
static void *dfs_watch(void *arg) {
    (void)arg;
    bool gone = false;
    while (!gone && !atomic_load(&g_dfs_dead)) { // (g_dfs_dead: unmounted normally, the main thread cleans up)
        sleep(1);
        if (g_dfs_kind == 'a' && g_ios.mux && atomic_load(&g_ios.mux->dead)) gone = true;
        if (g_dfs_kind != 'a') {
            struct usbdevfs_connectinfo ci; // fails with ENODEV once it's unplugged
            if (ioctl(g_mtp.usb.fd, USBDEVFS_CONNECTINFO, &ci) && errno == ENODEV) gone = true;
        }
    }
    if (!gone) return NULL;
    dev_log("devfs: the device is gone, unmounting");
    pid_t pid;
    char *av[] = {"fusermount3", "-u", "-z", g_dfs_mp, NULL};
    if (posix_spawnp(&pid, "fusermount3", NULL, spawn_attr(), av, environ) == 0) waitpid(pid, NULL, 0);
    // a tab still in the folder keeps a lazy unmount from finishing, and the FUSE loop waiting: just go (the kernel
    // ends the connection, so anything still in there gets errors rather than a hang)
    rmdir(g_dfs_mp);
    _exit(0);
}

static void dfs_name_clean(char *s) { for (; *s; s++) if (*s == '/' || (u8)*s < 32) *s = '-'; }

static bool dfs_is_mount(const char *path) {
    FILE *f = fopen("/proc/self/mounts", "r");
    char line[PATH_MAX * 2], src[PATH_MAX], mp[PATH_MAX];
    bool found = false;
    while (f && !found && fgets(line, sizeof line, f))
        found = sscanf(line, "%4095s %4095s", src, mp) == 2 && !strcmp(mp, path);
    if (f) fclose(f);
    return found;
}

// file-explorer --device-mount SYSNAME DIR
static int devfs_main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: file-explorer --device-mount SYSNAME DIR\n"); return 2; }
    signal(SIGPIPE, SIG_IGN);
    g_dfs_out = stdout;
    g_dfs_uid = getuid();
    g_dfs_gid = getgid();
    UsbDevInfo devs[16], *info = NULL;
    int nd = usb_list_phones(devs, 16);
    for (int i = 0; i < nd && !info; i++) if (!strcmp(devs[i].sys, argv[2])) info = &devs[i];
    if (!info) { dfs_report("error io: the device isn't plugged in any more"); return 1; }
    g_dfs_kind = info->kind;
    { // already a folder for it: still served (another helper has it) -> that one; a dead one (its helper is gone) goes
        char src[200], line[PATH_MAX * 2], s[PATH_MAX], m[PATH_MAX], t[64];
        snprintf(src, sizeof src, "fe-device:%s", info->serial[0] ? info->serial : info->sys);
        for (char *c = src; *c; c++) if (*c == ' ') *c = '_';
        FILE *mf = fopen("/proc/self/mounts", "r");
        while (mf && fgets(line, sizeof line, mf)) {
            if (sscanf(line, "%4095s %4095s %63s", s, m, t) != 3 || strcmp(s, src) || strcmp(t, "fuse.fe-device")) continue;
            char mp[PATH_MAX];
            size_t o = 0;
            for (const char *c = m; *c && o + 1 < sizeof mp; c++) {
                if (c[0] == '\\' && isdigit((u8)c[1]) && isdigit((u8)c[2]) && isdigit((u8)c[3])) { mp[o++] = (char)((c[1] - '0') * 64 + (c[2] - '0') * 8 + (c[3] - '0')); c += 3; }
                else mp[o++] = *c;
            }
            mp[o] = 0;
            struct statvfs sv;
            if (statvfs(mp, &sv) == 0) { fclose(mf); dfs_report("ok %s", mp); return 0; }
            pid_t pid;
            char *av[] = {"fusermount3", "-u", "-z", mp, NULL};
            if (posix_spawnp(&pid, "fusermount3", NULL, spawn_attr(), av, environ) == 0) waitpid(pid, NULL, 0);
            rmdir(mp);
        }
        if (mf) fclose(mf);
    }
    char name[160], err[512] = "";
    bool ok = info->kind == 'a' ? ios_start(info, name, sizeof name, err, sizeof err, dfs_progress)
                                : mtp_start(info, name, sizeof name, err, sizeof err, dfs_progress);
    if (!ok) { dfs_report("error %s", err[0] ? err : "io: couldn't connect"); return 1; }
    dfs_name_clean(name);
    mkdir_parents(argv[3]);
    for (int k = 1; k < 10; k++) { // a free folder for it
        if (k == 1) snprintf(g_dfs_mp, sizeof g_dfs_mp, "%s/%s", argv[3], name);
        else snprintf(g_dfs_mp, sizeof g_dfs_mp, "%s/%s (%d)", argv[3], name, k);
        if (dfs_is_mount(g_dfs_mp)) continue;
        mkdir(g_dfs_mp, 0700);
        DIR *d = opendir(g_dfs_mp);
        bool empty = d != NULL;
        struct dirent *de;
        while (d && empty && (de = readdir(d))) empty = !strcmp(de->d_name, ".") || !strcmp(de->d_name, "..");
        if (d) closedir(d);
        if (empty) break;
    }
    char opts[400];
    snprintf(opts, sizeof opts, "fsname=fe-device:%s,subtype=fe-device,noatime", info->serial[0] ? info->serial : info->sys);
    for (char *c = opts; *c; c++) if (*c == ' ') *c = '_';
    char *fav[] = {"file-explorer", "-o", opts, NULL};
    struct fuse_args args = FUSE_ARGS_INIT(3, fav);
    struct fuse *f = fuse_new(&args, &g_dfs_ops, sizeof g_dfs_ops, NULL);
    if (!f || fuse_mount(f, g_dfs_mp)) {
        dfs_report("error io: couldn't make the folder for it (FUSE: is fuse3 installed?)");
        rmdir(g_dfs_mp);
        return 1;
    }
    dfs_report("ok %s", g_dfs_mp);
    int nul = open("/dev/null", O_RDWR);
    char logp[PATH_MAX + 16];
    snprintf(logp, sizeof logp, "%s/.device.log", argv[3]); // what happens from now on (FE_DEVICE_DEBUG: stays on stderr)
    int logfd = getenv("FE_DEVICE_DEBUG") ? -1 : open(logp, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); if (logfd >= 0) dup2(logfd, 2); else if (!getenv("FE_DEVICE_DEBUG")) dup2(nul, 2); close(nul); }
    if (logfd >= 0) close(logfd);
    dev_log("devfs: serving %s (%s) at %s", name, info->sys, g_dfs_mp);
    g_dfs_out = NULL;
    pthread_t w;
    pthread_create(&w, NULL, dfs_watch, NULL);
    pthread_detach(w);
    fuse_loop_mt(f, 0);
    atomic_store(&g_dfs_dead, true);
    fuse_unmount(f);
    fuse_destroy(f);
    rmdir(g_dfs_mp);
    if (g_dfs_kind == 'a') { // let go of the iPhone (unplugging and plugging it back in gives it to usbmuxd again)
        for (int i = 0; i < g_ios.npool; i++) afc_free(g_ios.pool[i]);
        for (int i = 0; i < g_ios.nappafc; i++) afc_free(g_ios.appafc[i].afc);
        for (int i = 0; i < g_ios.nold; i++) afc_free(g_ios.old[i]);
        ld_close(&g_ios.ld);
        mux_free(g_ios.mux);
        usb_close(&g_ios.usb);
    } else {
        ptp_close(&g_mtp.ptp);
        usb_close(&g_mtp.usb);
    }
    return 0;
}
