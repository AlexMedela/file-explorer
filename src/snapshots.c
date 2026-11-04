// snapshots.c - previous versions of a file or folder from filesystem snapshots (Snapper on btrfs, Timeshift in
// btrfs or rsync mode, ZFS), found on a worker thread for Properties

typedef struct {
    char path[PATH_MAX];  // the item inside the snapshot
    char label[160];      // "Snapper #12: before update", "Timeshift", "ZFS daily-2026-11-01"
    i64 when;             // when the snapshot was taken (unix seconds)
    i64 mtime;            // the item there
    u64 size;
    bool dir;
    bool same_as_now;     // nothing changed since
    int also;             // older snapshots that have this very version too
} SnapVer;

typedef struct SnapJob {
    char path[PATH_MAX];
    _Atomic int done, refs;
    SnapVer *v;           // newest first, one per distinct version
    int n, cap;
    int nsnap;            // snapshots that have the item at all
    char note[300];       // why there's nothing to show (e.g. only root can read the snapshots)
    bool noaccess;        // there are snapshots only the administrator can read
    bool admin;           // listed as the administrator (pkexec): what's in them is restored / opened that way too
} SnapJob;

static void snap_release(SnapJob *j) {
    if (atomic_fetch_sub(&j->refs, 1) == 1) { free(j->v); free(j); }
}

static void snap_add(SnapJob *j, const char *path, const char *label, i64 when) {
    struct stat st;
    if (lstat(path, &st)) return;
    if (j->n == j->cap) { j->cap = j->cap ? j->cap * 2 : 16; j->v = xrealloc(j->v, (size_t)j->cap * sizeof(SnapVer)); }
    SnapVer *v = &j->v[j->n++];
    memset(v, 0, sizeof *v);
    snprintf(v->path, sizeof v->path, "%s", path);
    snprintf(v->label, sizeof v->label, "%s", label);
    v->when = when;
    v->mtime = st.st_mtime;
    v->size = S_ISDIR(st.st_mode) ? 0 : (u64)st.st_size;
    v->dir = S_ISDIR(st.st_mode);
}

static void snap_noaccess(SnapJob *j, const char *what, const char *dir) {
    j->noaccess = true;
    if (!j->note[0]) snprintf(j->note, sizeof j->note, "%s keeps snapshots in %s, but only the administrator can read them", what, dir);
}

// "2026-11-01 10:00:00" (UTC, Snapper's info.xml) or "2026-11-01_10-00-01" (Timeshift's folder names, local time).
static i64 snap_parse_time(const char *s, bool utc) {
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    if (sscanf(s, "%d-%d-%d%*c%d%*c%d%*c%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) < 5) return 0;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = -1;
    return utc ? (i64)timegm(&tm) : (i64)mktime(&tm);
}

static void snap_xml_tag(const char *xml, const char *tag, char *out, size_t cap) {
    out[0] = 0;
    char open[48];
    snprintf(open, sizeof open, "<%s>", tag);
    const char *a = strstr(xml, open);
    if (!a) return;
    a += strlen(open);
    const char *b = strchr(a, '<');
    size_t n = b ? (size_t)(b - a) : strlen(a);
    snprintf(out, cap, "%.*s", (int)MIN(n, cap - 1), a);
}

// Snapper: <mount>/.snapshots/<N>/snapshot/<rel> with <N>/info.xml (date, description).
static void snap_snapper(SnapJob *j, const char *mnt, const char *rel) {
    char base[PATH_MAX];
    snprintf(base, sizeof base, "%s%s.snapshots", mnt, mnt[1] ? "/" : "");
    DIR *d = opendir(base);
    if (!d) { if (errno == EACCES) snap_noaccess(j, "Snapper", base); return; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!isdigit((u8)e->d_name[0])) continue;
        char p[PATH_MAX], info[PATH_MAX], xml[4096] = "", date[64], desc[100], label[160];
        snprintf(info, sizeof info, "%s/%s/info.xml", base, e->d_name);
        FILE *f = fopen(info, "r");
        if (f) { size_t k = fread(xml, 1, sizeof xml - 1, f); xml[k] = 0; fclose(f); }
        snap_xml_tag(xml, "date", date, sizeof date);
        snap_xml_tag(xml, "description", desc, sizeof desc);
        snprintf(label, sizeof label, "Snapper #%s%s%s", e->d_name, desc[0] ? ": " : "", desc);
        snprintf(p, sizeof p, "%s/%s/snapshot%s%s", base, e->d_name, rel[0] ? "/" : "", rel);
        if (access(p, F_OK) && errno == EACCES) { snap_noaccess(j, "Snapper", base); continue; }
        snap_add(j, p, label, date[0] ? snap_parse_time(date, true) : 0);
    }
    closedir(d);
}

// ZFS: <mount>/.zfs/snapshot/<name>/<rel> (the snapshot folder's time is when it was taken).
static void snap_zfs(SnapJob *j, const char *mnt, const char *rel) {
    char base[PATH_MAX];
    snprintf(base, sizeof base, "%s%s.zfs/snapshot", mnt, mnt[1] ? "/" : "");
    DIR *d = opendir(base);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char root[PATH_MAX], p[PATH_MAX + 8], label[160];
        snprintf(root, sizeof root, "%s/%s", base, e->d_name);
        snprintf(p, sizeof p, "%s%s%s", root, rel[0] ? "/" : "", rel);
        snprintf(label, sizeof label, "ZFS %s", e->d_name);
        struct stat st;
        snap_add(j, p, label, stat(root, &st) == 0 ? (i64)st.st_mtime : 0);
    }
    closedir(d);
}

// Timeshift: btrfs mode keeps <base>/timeshift-btrfs/snapshots/<date>/<subvolume>/<rel>, rsync mode
// <base>/timeshift/snapshots/<date>/localhost/<absolute path>; <base> is where Timeshift mounted the backup disk.
static void snap_timeshift(SnapJob *j, const char *abs, const char *subvol, const char *rel) {
    const char *run = getenv("FE_TIMESHIFT") ? getenv("FE_TIMESHIFT") : "/run/timeshift";
    char bases[8][PATH_MAX];
    int nb = 0;
    snprintf(bases[nb++], PATH_MAX, "/");
    DIR *r = opendir(run);
    if (r) {
        struct dirent *e;
        while ((e = readdir(r)) && nb < 8)
            if (e->d_name[0] != '.') snprintf(bases[nb++], PATH_MAX, "%s/%s/backup", run, e->d_name);
        closedir(r);
    }
    for (int b = 0; b < nb; b++)
        for (int mode = 0; mode < 2; mode++) {
            if (mode == 0 && !subvol) continue;
            char dir[PATH_MAX + 64];
            snprintf(dir, sizeof dir, "%s%s%s/snapshots", bases[b], bases[b][1] ? "/" : "", mode ? "timeshift" : "timeshift-btrfs");
            DIR *d = opendir(dir);
            if (!d) { if (errno == EACCES) snap_noaccess(j, "Timeshift", dir); continue; }
            struct dirent *e;
            while ((e = readdir(d))) {
                if (!isdigit((u8)e->d_name[0])) continue;
                char p[PATH_MAX * 2];
                if (mode == 0) snprintf(p, sizeof p, "%s/%s/%s%s%s", dir, e->d_name, subvol, rel[0] ? "/" : "", rel);
                else snprintf(p, sizeof p, "%s/%s/localhost%s", dir, e->d_name, abs);
                snap_add(j, p, "Timeshift", snap_parse_time(e->d_name, false));
            }
            closedir(d);
        }
}

static int snap_cmp(const void *a, const void *b) {
    i64 x = ((const SnapVer *)a)->when, y = ((const SnapVer *)b)->when;
    return x < y ? 1 : x > y ? -1 : 0;
}

static void snap_find(SnapJob *j) {
    // the filesystem the item is on: the longest mount point that contains it
    DuScan t;
    memset(&t, 0, sizeof t);
    const char *mi = getenv("FE_MOUNTINFO");
    FILE *f = fopen(mi ? mi : "/proc/self/mountinfo", "r");
    if (f) {
        size_t cap = 1 << 16, len = 0;
        char *txt = xmalloc(cap);
        for (size_t k; (k = fread(txt + len, 1, cap - len - 1, f)) > 0;) {
            len += k;
            if (len + 1 >= cap) txt = xrealloc(txt, cap *= 2);
        }
        txt[len] = 0;
        fclose(f);
        du_mounts_parse(&t, txt);
        free(txt);
    }
    const DuMount *m = NULL;
    for (int i = 0; i < t.nmounts; i++)
        if (du_under(j->path, t.mounts[i].mnt) && (!m || strlen(t.mounts[i].mnt) >= strlen(m->mnt))) m = &t.mounts[i];
    if (m) {
        size_t ml = strlen(m->mnt);
        const char *rel = j->path + (ml == 1 ? 1 : ml);
        while (*rel == '/') rel++;
        if (!strcmp(m->fstype, "zfs")) snap_zfs(j, m->mnt, rel);
        else snap_snapper(j, m->mnt, rel);
        const char *sv = !strcmp(m->fstype, "btrfs") && m->root[0] == '/' && m->root[1] ? m->root + 1 : NULL; // "/@home" -> "@home"
        snap_timeshift(j, j->path, sv, rel);
    }
    for (int i = 0; i < t.nmounts; i++) { free(t.mounts[i].root); free(t.mounts[i].mnt); free(t.mounts[i].fstype); free(t.mounts[i].src); }
    free(t.mounts);
    // newest first; a run of snapshots with the very same version is one row
    j->nsnap = j->n;
    if (j->n) qsort(j->v, (size_t)j->n, sizeof(SnapVer), snap_cmp);
    struct stat now;
    bool have = lstat(j->path, &now) == 0;
    int k = 0;
    for (int i = 0; i < j->n; i++) {
        SnapVer *v = &j->v[i];
        if (k && j->v[k - 1].mtime == v->mtime && j->v[k - 1].size == v->size) { j->v[k - 1].also++; continue; }
        v->same_as_now = have && v->mtime == now.st_mtime && (v->dir || v->size == (u64)now.st_size);
        if (k != i) j->v[k] = *v;
        k++;
    }
    j->n = k;
}

static void *snap_thread(void *arg) {
    SnapJob *j = arg;
    thread_background_priority();
    snap_find(j);
    atomic_store(&j->done, 1);
    main_wake();
    snap_release(j);
    return NULL;
}

// The administrator helper (`--admin-op versions PATH`, as root): the same search, one "v" line per version.
static int snap_admin_main(const char *path) {
    static SnapJob j; // (static: a big struct)
    snprintf(j.path, sizeof j.path, "%s", path);
    snap_find(&j);
    for (int i = 0; i < j.n; i++) {
        SnapVer *v = &j.v[i];
        printf("v %lld\t%lld\t%llu\t%d\t%d\t%d\t%s\t%s\n", (long long)v->when, (long long)v->mtime, (unsigned long long)v->size, v->dir,
               v->same_as_now, v->also, v->label, v->path);
    }
    fflush(stdout);
    return 0;
}

static void *snap_admin_thread(void *arg) {
    SnapJob *j = arg;
    char exe[PATH_MAX];
    ssize_t el = readlink("/proc/self/exe", exe, sizeof exe - 1);
    const char *pk = getenv("FE_PKEXEC") ? getenv("FE_PKEXEC") : "pkexec";
    int out[2] = {-1, -1};
    pid_t pid = 0;
    int rc = -1;
    if (el > 0 && pipe2(out, O_CLOEXEC) == 0) {
        exe[el] = 0;
        char *av[] = {"sh", "-c", "\"$@\"; printf '\\n@rc=%d\\n' $?", "sh", (char *)pk, exe, "--admin-op", "versions", j->path, NULL};
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, out[1], 1);
        posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
        rc = posix_spawnp(&pid, "sh", &fa, spawn_attr(), av, environ);
        posix_spawn_file_actions_destroy(&fa);
        close(out[1]);
    }
    if (rc) snprintf(j->note, sizeof j->note, "Can't run %s (it comes with polkit)", pk);
    else {
        FILE *f = fdopen(out[0], "r");
        char line[2 * PATH_MAX + 512];
        int code = -1;
        while (f && fgets(line, sizeof line, f)) {
            line[strcspn(line, "\n")] = 0;
            if (!strncmp(line, "@rc=", 4)) code = atoi(line + 4);
            if (line[0] != 'v' || line[1] != ' ') continue;
            char *fld[8];
            int nf = 0;
            for (char *p = line + 2; nf < 8; nf++) {
                fld[nf] = p;
                char *t = nf < 7 ? strchr(p, '\t') : NULL;
                if (!t) { nf++; break; }
                *t = 0;
                p = t + 1;
            }
            if (nf < 8) continue;
            if (j->n == j->cap) { j->cap = j->cap ? j->cap * 2 : 16; j->v = xrealloc(j->v, (size_t)j->cap * sizeof(SnapVer)); }
            SnapVer *v = &j->v[j->n++];
            memset(v, 0, sizeof *v);
            v->when = atoll(fld[0]);
            v->mtime = atoll(fld[1]);
            v->size = strtoull(fld[2], NULL, 10);
            v->dir = atoi(fld[3]);
            v->same_as_now = atoi(fld[4]);
            v->also = atoi(fld[5]);
            snprintf(v->label, sizeof v->label, "%s", fld[6]);
            snprintf(v->path, sizeof v->path, "%s", fld[7]);
        }
        if (f) fclose(f);
        else close(out[0]);
        waitpid(pid, NULL, 0);
        if (code == 126 || code == 127) snprintf(j->note, sizeof j->note, "Not authorized (the password prompt was cancelled or refused, or no polkit agent is running to show it)");
        else if (!j->n) snprintf(j->note, sizeof j->note, "No snapshots of it");
    }
    j->nsnap = j->n;
    atomic_store(&j->done, 1);
    main_wake();
    snap_release(j);
    return NULL;
}

static SnapJob *snap_start_admin(const char *path) {
    SnapJob *j = xcalloc(1, sizeof *j);
    snprintf(j->path, sizeof j->path, "%s", path);
    j->admin = true;
    atomic_store(&j->refs, 2);
    pthread_t t;
    pthread_create(&t, NULL, snap_admin_thread, j);
    pthread_setname_np(t, "fe-snapshots");
    pthread_detach(t);
    return j;
}

static SnapJob *snap_start(const char *path) {
    SnapJob *j = xcalloc(1, sizeof *j);
    snprintf(j->path, sizeof j->path, "%s", path);
    atomic_store(&j->refs, 2);
    pthread_t t;
    pthread_create(&t, NULL, snap_thread, j);
    pthread_setname_np(t, "fe-snapshots");
    pthread_detach(t);
    return j;
}

// --- all snapshots of a drive (the Snapshots view): list them, what changed between two, create / delete ---------

typedef struct {
    int num;              // Snapper's number (0 for others)
    char name[96];        // "#41", a ZFS snapshot's name, a Timeshift date
    char type[16];        // single / pre / post / timeline (from cleanup) ...
    int pre;              // post: its pre snapshot
    i64 when;
    char desc[200], user[64], cleanup[24];
    char root[PATH_MAX];  // where the drive's root is inside it
} SnapInfo;

typedef struct SnapList {
    char path[PATH_MAX];  // what it was asked for (any folder on the drive)
    char mnt[PATH_MAX], fstype[32], config[64];
    const char *tool;     // "Snapper", "ZFS", "Timeshift" (NULL: none found)
    _Atomic int done, refs;
    SnapInfo *s;
    int n, cap;
    bool noaccess;        // Snapper is set up but this user may not read it
    bool manage;          // snapper works for this user: create / delete / what changed
    char note[300];
} SnapList;

static void snaplist_release(SnapList *l) {
    if (atomic_fetch_sub(&l->refs, 1) == 1) { free(l->s); free(l); }
}

static SnapInfo *snaplist_add(SnapList *l) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 32; l->s = xrealloc(l->s, (size_t)l->cap * sizeof(SnapInfo)); }
    SnapInfo *s = &l->s[l->n++];
    memset(s, 0, sizeof *s);
    return s;
}

// Run a program, its output (stdout) in out; the exit code (-1: couldn't run).
// (Under sh, which prints the exit code as a last "@rc=N" line: SIGCHLD is ignored app-wide, so waitpid can't.)
static int snap_run(char *const argv[], char *out, size_t cap) {
    int fd[2];
    if (out) out[0] = 0;
    if (pipe2(fd, O_CLOEXEC)) return -1;
    int na = 0;
    while (argv[na]) na++;
    char **av = xmalloc((size_t)(na + 5) * sizeof(char *));
    av[0] = "sh";
    av[1] = "-c";
    av[2] = "\"$@\"; printf '\\n@rc=%d\\n' $?";
    av[3] = "sh";
    for (int i = 0; i <= na; i++) av[4 + i] = argv[i];
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fd[1], 1);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid;
    int rc = posix_spawnp(&pid, "sh", &fa, spawn_attr(), av, environ);
    posix_spawn_file_actions_destroy(&fa);
    free(av);
    close(fd[1]);
    if (rc) { close(fd[0]); return -1; }
    size_t len = 0;
    char tmp[4096], tail[64] = ""; // tail: the last bytes, where the exit code is (even when out is full or NULL)
    size_t tl = 0;
    for (ssize_t r; (r = read(fd[0], tmp, sizeof tmp)) > 0;) {
        if (out && len + 1 < cap) {
            size_t k = MIN((size_t)r, cap - 1 - len);
            memcpy(out + len, tmp, k);
            len += k;
        }
        for (ssize_t i = 0; i < r; i++) {
            if (tl + 1 >= sizeof tail) { memmove(tail, tail + 1, sizeof tail - 2); tl = sizeof tail - 2; }
            tail[tl++] = tmp[i];
        }
    }
    tail[tl] = 0;
    if (out) out[len] = 0;
    close(fd[0]);
    waitpid(pid, NULL, 0);
    char *m = strstr(tail, "\n@rc=");
    for (char *q = m; q && (q = strstr(q + 1, "\n@rc="));) m = q;
    int status = m ? atoi(m + 5) : -1;
    if (out) { // drop the marker from the output
        char *om = NULL;
        for (char *q = out; (q = strstr(q, "\n@rc=")); q++) om = q;
        if (om) *om = 0;
    }
    return status == 127 ? -1 : status;
}

static const char *snapper_bin(void) { return getenv("FE_SNAPPER") ? getenv("FE_SNAPPER") : "snapper"; }

// The value of "key" in one JSON object (a string, number, null...), copied without quotes; false when absent.
static bool snap_json_val(const char *obj, const char *end, const char *key, char *out, size_t cap) {
    char k[48];
    snprintf(k, sizeof k, "\"%s\"", key);
    const char *p = obj;
    while ((p = strstr(p, k)) && p < end) {
        const char *q = p + strlen(k);
        while (*q == ' ' || *q == '\t' || *q == '\n') q++;
        if (*q != ':') { p = q; continue; }
        q++;
        while (*q == ' ' || *q == '\t' || *q == '\n') q++;
        size_t o = 0;
        if (*q == '"') {
            for (q++; *q && *q != '"' && q < end; q++) {
                char c = *q;
                if (c == '\\' && q[1]) { q++; c = *q == 'n' ? ' ' : *q; }
                if (o + 1 < cap) out[o++] = c;
            }
        } else {
            while (*q && *q != ',' && *q != '}' && *q != '\n' && q < end) { if (o + 1 < cap) out[o++] = *q; q++; }
            while (o && out[o - 1] == ' ') o--;
        }
        out[o] = 0;
        return true;
    }
    return false;
}

// `snapper --jsonout -c CONFIG list`: {"CONFIG": [ {"number": 1, "type": "pre", ...}, ... ]}
static void snap_parse_snapper(SnapList *l, const char *js) {
    const char *arr = strchr(js, '[');
    if (!arr) return;
    for (const char *p = arr + 1; (p = strchr(p, '{'));) {
        int depth = 0;
        const char *e = p;
        for (; *e; e++) {
            if (*e == '{') depth++;
            else if (*e == '}' && --depth == 0) break;
        }
        if (!*e) break;
        char v[256];
        int num = snap_json_val(p, e, "number", v, sizeof v) ? atoi(v) : -1;
        if (num > 0) {
            SnapInfo *s = snaplist_add(l);
            s->num = num;
            snprintf(s->name, sizeof s->name, "#%d", num);
            if (snap_json_val(p, e, "type", v, sizeof v)) snprintf(s->type, sizeof s->type, "%s", v);
            if (snap_json_val(p, e, "pre-number", v, sizeof v)) s->pre = atoi(v);
            if (snap_json_val(p, e, "date", v, sizeof v)) s->when = snap_parse_time(v, false); // (local time in snapper's output)
            if (snap_json_val(p, e, "description", v, sizeof v)) snprintf(s->desc, sizeof s->desc, "%s", v);
            if (snap_json_val(p, e, "user", v, sizeof v)) snprintf(s->user, sizeof s->user, "%s", v);
            if (snap_json_val(p, e, "cleanup", v, sizeof v)) snprintf(s->cleanup, sizeof s->cleanup, "%s", v);
            snprintf(s->root, sizeof s->root, "%s%s.snapshots/%d/snapshot", l->mnt, l->mnt[1] ? "/" : "", num);
        }
        p = e + 1;
    }
}

static int snapinfo_cmp(const void *a, const void *b) {
    const SnapInfo *x = a, *y = b;
    if (x->when != y->when) return x->when < y->when ? 1 : -1;
    return y->num - x->num;
}

static void snaplist_find(SnapList *l) {
    DuScan t;
    memset(&t, 0, sizeof t);
    const char *mi = getenv("FE_MOUNTINFO");
    FILE *f = fopen(mi ? mi : "/proc/self/mountinfo", "r");
    if (f) {
        size_t cap = 1 << 16, len = 0;
        char *txt = xmalloc(cap);
        for (size_t k; (k = fread(txt + len, 1, cap - len - 1, f)) > 0;) {
            len += k;
            if (len + 1 >= cap) txt = xrealloc(txt, cap *= 2);
        }
        txt[len] = 0;
        fclose(f);
        du_mounts_parse(&t, txt);
        free(txt);
    }
    const DuMount *m = NULL;
    for (int i = 0; i < t.nmounts; i++)
        if (du_under(l->path, t.mounts[i].mnt) && (!m || strlen(t.mounts[i].mnt) >= strlen(m->mnt))) m = &t.mounts[i];
    if (!m) goto out;
    snprintf(l->mnt, sizeof l->mnt, "%s", m->mnt);
    snprintf(l->fstype, sizeof l->fstype, "%s", m->fstype);
    if (!strcmp(m->fstype, "zfs")) { // ZFS: <mount>/.zfs/snapshot/<name>
        char base[PATH_MAX + 16];
        snprintf(base, sizeof base, "%s%s.zfs/snapshot", l->mnt, l->mnt[1] ? "/" : "");
        DIR *d = opendir(base);
        for (struct dirent *e; d && (e = readdir(d));) {
            if (e->d_name[0] == '.') continue;
            SnapInfo *s = snaplist_add(l);
            snprintf(s->name, sizeof s->name, "%s", e->d_name);
            snprintf(s->type, sizeof s->type, "zfs");
            snprintf(s->root, sizeof s->root, "%s/%s", base, e->d_name);
            struct stat st;
            if (stat(s->root, &st) == 0) s->when = st.st_mtime;
        }
        if (d) closedir(d);
        if (l->n) l->tool = "ZFS";
    } else {
        // Snapper: the config for this mount point, then its list (works once the user is allowed: ALLOW_USERS)
        static char out[1 << 20];
        char *a1[] = {(char *)snapper_bin(), "--jsonout", "list-configs", NULL};
        if (snap_run(a1, out, sizeof out) == 0) {
            for (const char *p = out; (p = strchr(p, '{'));) {
                const char *e = strchr(p, '}');
                if (!e) break;
                char cfg[64], sub[PATH_MAX];
                if (snap_json_val(p, e, "config", cfg, sizeof cfg) && snap_json_val(p, e, "subvolume", sub, sizeof sub) && !strcmp(sub, l->mnt))
                    snprintf(l->config, sizeof l->config, "%s", cfg);
                p = e + 1;
            }
        }
        if (l->config[0]) {
            l->tool = "Snapper";
            char *a2[] = {(char *)snapper_bin(), "--jsonout", "-c", l->config, "list", NULL};
            int rc = snap_run(a2, out, sizeof out);
            if (rc == 0) { snap_parse_snapper(l, out); l->manage = true; }
            else {
                char sd[PATH_MAX + 16];
                snprintf(sd, sizeof sd, "%s%s.snapshots", l->mnt, l->mnt[1] ? "/" : "");
                DIR *d = opendir(sd);
                if (!d) { l->noaccess = true; snprintf(l->note, sizeof l->note, "Snapper keeps snapshots of %s, but only the administrator can see them.", l->mnt); }
                for (struct dirent *e; d && (e = readdir(d));) { // readable without snapper: info.xml
                    if (!isdigit((u8)e->d_name[0])) continue;
                    SnapInfo *s = snaplist_add(l);
                    s->num = atoi(e->d_name);
                    snprintf(s->name, sizeof s->name, "#%d", s->num);
                    char info[PATH_MAX + 40], xml[4096] = "", v[200];
                    snprintf(info, sizeof info, "%s/%s/info.xml", sd, e->d_name);
                    FILE *fx = fopen(info, "r");
                    if (fx) { size_t k = fread(xml, 1, sizeof xml - 1, fx); xml[k] = 0; fclose(fx); }
                    snap_xml_tag(xml, "type", s->type, sizeof s->type);
                    snap_xml_tag(xml, "description", s->desc, sizeof s->desc);
                    snap_xml_tag(xml, "cleanup", s->cleanup, sizeof s->cleanup);
                    snap_xml_tag(xml, "pre_num", v, sizeof v);
                    s->pre = atoi(v);
                    snap_xml_tag(xml, "date", v, sizeof v);
                    s->when = v[0] ? snap_parse_time(v, true) : 0;
                    snprintf(s->root, sizeof s->root, "%s/%s/snapshot", sd, e->d_name);
                }
                if (d) closedir(d);
            }
        }
        // Timeshift (btrfs mode): .../timeshift-btrfs/snapshots/<date>/<subvolume>
        const char *sv = !strcmp(m->fstype, "btrfs") && m->root[0] == '/' && m->root[1] ? m->root + 1 : NULL;
        const char *run = getenv("FE_TIMESHIFT") ? getenv("FE_TIMESHIFT") : "/run/timeshift";
        DIR *r = sv && !l->n ? opendir(run) : NULL;
        for (struct dirent *e; r && (e = readdir(r));) {
            if (e->d_name[0] == '.') continue;
            char dir[PATH_MAX];
            snprintf(dir, sizeof dir, "%s/%s/backup/timeshift-btrfs/snapshots", run, e->d_name);
            DIR *d = opendir(dir);
            for (struct dirent *s2; d && (s2 = readdir(d));) {
                if (!isdigit((u8)s2->d_name[0])) continue;
                char sr[PATH_MAX * 2];
                snprintf(sr, sizeof sr, "%s/%s/%s", dir, s2->d_name, sv);
                struct stat st;
                if (stat(sr, &st) || !S_ISDIR(st.st_mode)) continue; // (a snapshot of other subvolumes only)
                SnapInfo *s = snaplist_add(l);
                snprintf(s->name, sizeof s->name, "%s", s2->d_name);
                snprintf(s->type, sizeof s->type, "timeshift");
                s->when = snap_parse_time(s2->d_name, false);
                snprintf(s->root, sizeof s->root, "%s", sr);
            }
            if (d) closedir(d);
            if (l->n) l->tool = "Timeshift";
        }
        if (r) closedir(r);
    }
    if (l->n) qsort(l->s, (size_t)l->n, sizeof(SnapInfo), snapinfo_cmp);
out:
    if (!l->tool && !l->note[0]) snprintf(l->note, sizeof l->note, "No snapshots of this drive (Snapper, Timeshift or ZFS).");
    for (int i = 0; i < t.nmounts; i++) { free(t.mounts[i].root); free(t.mounts[i].mnt); free(t.mounts[i].fstype); free(t.mounts[i].src); }
    free(t.mounts);
}

static void *snaplist_thread(void *arg) {
    SnapList *l = arg;
    thread_background_priority();
    snaplist_find(l);
    atomic_store(&l->done, 1);
    main_wake();
    snaplist_release(l);
    return NULL;
}

static SnapList *snaplist_start(const char *path) {
    SnapList *l = xcalloc(1, sizeof *l);
    snprintf(l->path, sizeof l->path, "%s", path);
    atomic_store(&l->refs, 2);
    pthread_t t;
    pthread_create(&t, NULL, snaplist_thread, l);
    pthread_setname_np(t, "fe-snapshots");
    pthread_detach(t);
    return l;
}

// A snapper command in the background (create, delete, what changed): its output and exit code when done.
typedef struct SnapCmd {
    char *argv[12];
    char *out;
    size_t cap;
    int rc;
    _Atomic int done, refs;
} SnapCmd;

static void snapcmd_release(SnapCmd *c) {
    if (atomic_fetch_sub(&c->refs, 1) == 1) {
        for (int i = 0; c->argv[i]; i++) free(c->argv[i]);
        free(c->out);
        free(c);
    }
}

static void *snapcmd_thread(void *arg) {
    SnapCmd *c = arg;
    c->rc = snap_run(c->argv, c->out, c->cap);
    atomic_store(&c->done, 1);
    main_wake();
    snapcmd_release(c);
    return NULL;
}

static SnapCmd *snapcmd_start(const char *const *argv, size_t cap) {
    SnapCmd *c = xcalloc(1, sizeof *c);
    for (int i = 0; argv[i] && i < 11; i++) c->argv[i] = strdup(argv[i]);
    c->cap = cap;
    c->out = xmalloc(cap);
    atomic_store(&c->refs, 2);
    pthread_t t;
    pthread_create(&t, NULL, snapcmd_thread, c);
    pthread_detach(t);
    return c;
}

// Helper as root (`--admin-op snapper-allow CONFIG USER`): add the user to the config's ALLOW_USERS and let snapper
// give them read access to the snapshots (SYNC_ACL).
static int snap_allow_main(const char *cfg, const char *user) {
    for (const char *c = cfg; *c; c++)
        if (!isalnum((u8)*c) && *c != '_' && *c != '-') { printf("e 1\tBad config name\n"); return 2; }
    if (!getpwnam(user)) { printf("e 1\tNo such user\n"); return 2; }
    char path[PATH_MAX], line[1024], users[900] = "";
    snprintf(path, sizeof path, "%s/%s", getenv("FE_SNAPPER_CONFIGS") ? getenv("FE_SNAPPER_CONFIGS") : "/etc/snapper/configs", cfg);
    FILE *f = fopen(path, "r");
    while (f && fgets(line, sizeof line, f)) {
        if (strncmp(line, "ALLOW_USERS=", 12)) continue;
        char *v = line + 12;
        if (*v == '"') v++;
        v[strcspn(v, "\"\n")] = 0;
        snprintf(users, sizeof users, "%s", v);
    }
    if (f) fclose(f);
    bool has = false;
    char copy[900];
    snprintf(copy, sizeof copy, "%s", users);
    for (char *tok = strtok(copy, " "); tok; tok = strtok(NULL, " ")) has |= !strcmp(tok, user);
    char au[1024];
    snprintf(au, sizeof au, "ALLOW_USERS=%s%s%s", users, users[0] && !has ? " " : "", has ? "" : user);
    char *av[] = {(char *)snapper_bin(), "-c", (char *)cfg, "set-config", au, "SYNC_ACL=yes", NULL};
    int rc = snap_run(av, NULL, 0);
    if (rc) printf("e 1\tsnapper set-config failed (%d)\n", rc);
    fflush(stdout);
    return rc ? 1 : 0;
}

static void *snap_allow_thread(void *arg) { // the app side: pkexec the helper, then the list is read again
    SnapCmd *c = arg;
    c->rc = snap_run(c->argv, c->out, c->cap);
    atomic_store(&c->done, 1);
    main_wake();
    snapcmd_release(c);
    return NULL;
}

static SnapCmd *snap_allow_start(const char *cfg) {
    char exe[PATH_MAX];
    ssize_t el = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[el > 0 ? el : 0] = 0;
    struct passwd *pw = getpwuid(getuid());
    const char *pk = getenv("FE_PKEXEC") ? getenv("FE_PKEXEC") : "pkexec";
    const char *av[] = {pk, exe, "--admin-op", "snapper-allow", cfg, pw ? pw->pw_name : "", NULL};
    SnapCmd *c = xcalloc(1, sizeof *c);
    for (int i = 0; av[i]; i++) c->argv[i] = strdup(av[i]);
    c->cap = 4096;
    c->out = xmalloc(c->cap);
    atomic_store(&c->refs, 2);
    pthread_t t;
    pthread_create(&t, NULL, snap_allow_thread, c);
    pthread_detach(t);
    return c;
}

// Where a path is when it's inside a snapshot: the snapshot's root and the live folder that root stands for.
// "<mnt>/.snapshots/<N>/snapshot[/rest]", "<mnt>/.zfs/snapshot/<name>[/rest]".
static bool snap_where(const char *path, char *root, size_t rcap, char *live, size_t lcap, char *name, size_t ncap) {
    const char *k = strstr(path, "/.snapshots/"), *z = strstr(path, "/.zfs/snapshot/");
    const char *mk = k ? k : z;
    if (!mk) return false;
    const char *p = mk + (k ? 12 : 15);
    const char *e = p;
    while (*e && *e != '/') e++;
    if (e == p) return false;
    const char *rest;
    if (k) {
        for (const char *d = p; d < e; d++)
            if (!isdigit((u8)*d)) return false;
        if (strncmp(e, "/snapshot", 9) || (e[9] && e[9] != '/')) return false;
        rest = e + 9;
        snprintf(name, ncap, "#%.*s", (int)(e - p), p);
    } else {
        rest = e;
        snprintf(name, ncap, "%.*s", (int)(e - p), p);
    }
    snprintf(root, rcap, "%.*s", (int)(rest - path), path);
    char mnt[PATH_MAX];
    snprintf(mnt, sizeof mnt, "%.*s", (int)(mk - path), path);
    snprintf(live, lcap, "%s%s", mnt[0] ? mnt : "", rest[0] ? rest : (mnt[0] ? "" : "/"));
    if (!live[0]) snprintf(live, lcap, "/");
    return true;
}
