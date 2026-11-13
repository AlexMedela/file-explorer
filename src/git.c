// git.c - git status for the file list: finds the repository of a folder, runs `git status --porcelain=v2` for the
// whole repository on git's own worker threads (on the server over ssh in SFTP folders) and parses it into per-path
// states (changed files + the folders containing them), branch, ahead/behind, remotes, last commit and stashes. One
// result serves every folder of the repository. Also which subfolders of a folder are repositories (branch + remote
// for the pills on folder rows, read from their .git without running git). The main-thread side (cache, refresh,
// drawing, the path bar chip) is the "git status" section of app.c.

// States, in increasing priority (a folder shows the strongest state found inside it).
enum { GS_NONE, GS_IGNORED, GS_UNTRACKED, GS_STAGED, GS_ADDED, GS_RENAMED, GS_MODIFIED, GS_DELETED, GS_CONFLICT };

typedef struct {
    char *path; // relative to the repository root ("a", "sub/b")
    u8 st;      // GS_*
    bool self;  // reported by git (not just a folder containing changes): untracked/ignored folders pass it on
} GitRec;

#define GIT_MAX_REMOTES 4

typedef struct GitResult {
    bool repo;
    char branch[128]; // branch name, or the short commit when detached ("" before the first commit when detached)
    bool detached;
    char upstream[160]; // "origin/main", "" without one
    int ahead, behind;
    int nstaged, nmodified, nuntracked, nconflicts; // whole repository (a file can be staged and modified)
    GitRec *recs; // sorted by path
    u32 n;
    char last_hash[16], last_subject[200], last_author[80]; // last commit ("" before the first one)
    i64 last_time;
    struct { char name[40], url[300]; } remotes[GIT_MAX_REMOTES];
    int nremotes, nstash;
    u64 sig;    // index / HEAD / FETCH_HEAD / config times when it ran (a change means: run again)
    u64 dur_ns; // how long `git status` took (paces the periodic re-check)
    bool has_details; // last commit / remotes / stashes were read (else the main thread keeps the previous ones)
} GitResult;

static void git_result_free(GitResult *g) {
    if (!g) return;
    for (u32 i = 0; i < g->n; i++) free(g->recs[i].path);
    free(g->recs);
    free(g);
}

static int git_rec_cmp(const void *a, const void *b) { return strcmp(((const GitRec *)a)->path, ((const GitRec *)b)->path); }

static void git_add(GitRec **v, u32 *n, u32 *cap, const char *path, size_t len, u8 st, bool self) {
    if (*n == *cap) { *cap = *cap ? *cap * 2 : 64; *v = xrealloc(*v, *cap * sizeof **v); }
    (*v)[(*n)++] = (GitRec){strndup(path, len), st, self};
}

// XY of a "1"/"2" record -> GS_*
static u8 git_xy_state(char x, char y) {
    if (x == 'D' || y == 'D') return GS_DELETED;
    if (y != '.') return GS_MODIFIED;
    if (x == 'A') return GS_ADDED;
    if (x == 'R' || x == 'C') return GS_RENAMED;
    return x != '.' ? GS_STAGED : GS_NONE;
}

// Parse `git status --porcelain=v2 -z --branch` output (paths relative to the repository root).
static GitResult *git_parse(const char *out, size_t len) {
    GitResult *g = xcalloc(1, sizeof *g);
    GitRec *v = NULL;
    u32 n = 0, cap = 0;
    char oid[48] = "";
    for (size_t at = 0; at < len;) {
        const char *t = out + at;
        size_t tl = strnlen(t, len - at);
        at += tl + 1;
        if (t == out) g->repo = tl > 13 && !strncmp(t, "# branch.oid ", 13); // always the first line with --branch
        if (!g->repo) break;
        if (t[0] == '#') {
            if (!strncmp(t, "# branch.oid ", 13)) snprintf(oid, sizeof oid, "%.*s", (int)MIN(tl - 13, 7), t + 13);
            else if (!strncmp(t, "# branch.head ", 14)) snprintf(g->branch, sizeof g->branch, "%.*s", (int)(tl - 14), t + 14);
            else if (!strncmp(t, "# branch.upstream ", 18)) snprintf(g->upstream, sizeof g->upstream, "%.*s", (int)(tl - 18), t + 18);
            else if (!strncmp(t, "# branch.ab ", 12)) sscanf(t + 12, "+%d -%d", &g->ahead, &g->behind);
            continue;
        }
        int skip; // fields before the path
        u8 st;
        char x = tl > 3 ? t[2] : '.', y = tl > 3 ? t[3] : '.';
        switch (t[0]) {
        case '1': skip = 8; st = git_xy_state(x, y); break;
        case '2': skip = 9; st = git_xy_state(x, y); break;
        case 'u': skip = 10; st = GS_CONFLICT; break;
        case '?': skip = 1; st = GS_UNTRACKED; break;
        case '!': skip = 1; st = GS_IGNORED; break;
        default: continue;
        }
        if (t[0] == '2' && at < len) at += strnlen(out + at, len - at) + 1; // the original path of a rename
        const char *p = t;
        for (int k = 0; k < skip && p; k++) {
            p = memchr(p, ' ', tl - (size_t)(p - t));
            if (p) p++;
        }
        if (!p || st == GS_NONE) continue;
        size_t pl = tl - (size_t)(p - t);
        if (pl && p[pl - 1] == '/') pl--; // untracked / ignored folder
        if (!pl) continue;
        if (t[0] == 'u') g->nconflicts++;
        else if (t[0] == '?') g->nuntracked++;
        else if (t[0] != '!') { g->nstaged += x != '.'; g->nmodified += y != '.'; }
        git_add(&v, &n, &cap, p, pl, st, true);
        if (st == GS_IGNORED) continue;
        for (size_t k = pl; k-- > 0;) // the folders it's in
            if (p[k] == '/') git_add(&v, &n, &cap, p, k, st, false);
    }
    if (g->repo && (!g->branch[0] || !strcmp(g->branch, "(detached)"))) {
        g->detached = true;
        snprintf(g->branch, sizeof g->branch, "%s", oid[0] != '(' ? oid : ""); // "(initial)": no commit yet
    }
    // merge duplicates: strongest state; "self" wins so untracked folders still pass their state on
    if (n) qsort(v, n, sizeof *v, git_rec_cmp);
    u32 o = 0;
    for (u32 i = 0; i < n; i++) {
        if (o && !strcmp(v[o - 1].path, v[i].path)) {
            if (v[i].st > v[o - 1].st) v[o - 1].st = v[i].st;
            v[o - 1].self |= v[i].self;
            free(v[i].path);
        } else {
            v[o++] = v[i];
        }
    }
    g->recs = v;
    g->n = o;
    return g;
}

// First record >= key (key of len bytes, compared like strcmp on the NUL-terminated version).
static u32 git_lower(const GitResult *g, const char *key, size_t len) {
    u32 lo = 0, hi = g->n;
    while (lo < hi) {
        u32 mid = (lo + hi) / 2;
        const char *p = g->recs[mid].path;
        int c = strncmp(p, key, len);
        if (!c) c = p[len] ? 1 : 0;
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static const GitRec *git_find(const GitResult *g, const char *key, size_t len) {
    u32 i = git_lower(g, key, len);
    return i < g->n && !strncmp(g->recs[i].path, key, len) && !g->recs[i].path[len] ? &g->recs[i] : NULL;
}

// State of an entry: prefix = its folder relative to the root ("" or "a/b/"), name relative to that folder ("x" or
// "sub/x" in the tree view and recursive search).
static u8 git_state(const GitResult *g, const char *prefix, const char *name, size_t len) {
    if (!g || !g->repo) return GS_NONE;
    char key[PATH_MAX * 2];
    size_t pl = strlen(prefix);
    if (pl + len >= sizeof key) return GS_NONE;
    memcpy(key, prefix, pl);
    memcpy(key + pl, name, len);
    len += pl;
    key[len] = 0;
    const GitRec *r = git_find(g, key, len);
    if (r) return r->st;
    for (size_t k = len; k-- > 0;) // inside an untracked / ignored folder?
        if (key[k] == '/' && (r = git_find(g, key, k)) && r->self && (r->st == GS_UNTRACKED || r->st == GS_IGNORED)) return r->st;
    return GS_NONE;
}

// Changed (not ignored) paths git reported in a folder and below (prefix "" = the whole repository).
static int git_count_under(const GitResult *g, const char *prefix) {
    size_t pl = strlen(prefix);
    int n = 0;
    for (u32 i = pl ? git_lower(g, prefix, pl) : 0; i < g->n && !strncmp(g->recs[i].path, prefix, pl); i++)
        n += g->recs[i].self && g->recs[i].st != GS_IGNORED;
    return n;
}

// --- running git (git's own worker threads) ----------------------------------

// Separate from the shared pool so a busy pool (metadata, device or unit jobs) never delays the badges.
static JobQueue g_gitq = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, NULL, NULL};

static void *git_thread(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&g_gitq.mtx);
        while (!g_gitq.head) pthread_cond_wait(&g_gitq.cv, &g_gitq.mtx);
        Job *j = g_gitq.head;
        g_gitq.head = j->next;
        if (!g_gitq.head) g_gitq.tail = NULL;
        pthread_mutex_unlock(&g_gitq.mtx);
        j->fn(j->arg);
        free(j);
    }
    return NULL;
}

static void git_submit(JobFn fn, void *arg) {
    static bool started;
    if (!started) { // two: finding a repository never waits behind a long `git status`
        started = true;
        for (int i = 0; i < 2; i++) {
            pthread_t t;
            pthread_create(&t, NULL, git_thread, NULL);
            pthread_setname_np(t, "fe-git");
            pthread_detach(t);
        }
    }
    jq_push(&g_gitq, fn, arg);
}

// Run git with stdout captured (stderr dropped). Returns the output (NUL bytes kept), NULL if it couldn't start.
static char *git_capture(char *const argv[], size_t *len) {
    int pfd[2];
    *len = 0;
    if (pipe2(pfd, O_CLOEXEC)) return NULL;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], &fa, spawn_attr(), argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(pfd[1]);
    if (rc) { close(pfd[0]); return NULL; }
    size_t o = 0, cap = 16384;
    char *buf = xmalloc(cap);
    for (;;) {
        if (o + 4096 > cap) {
            if (cap >= (64u << 20)) break; // absurdly large: keep what we have
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
        ssize_t r = read(pfd[0], buf + o, cap - o - 1);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        o += (size_t)r;
    }
    close(pfd[0]); // (SIGCHLD is ignored app-wide: the child reaps itself)
    buf[o] = 0;
    *len = o;
    return buf;
}

// 'text' for a POSIX shell (inner ' becomes '\''); also the ssh terminals in app.c.
static void sh_quote(char *out, size_t cap, const char *in) {
    size_t o = 0;
    if (o + 1 < cap) out[o++] = '\'';
    for (const char *c = in; *c && o + 5 < cap; c++) {
        if (*c == '\'') { memcpy(out + o, "'\\''", 4); o += 4; }
        else out[o++] = *c;
    }
    if (o + 1 < cap) out[o++] = '\'';
    out[o] = 0;
}

// Like git_capture, but on host over ssh when host isn't "" (a repository in an SFTP folder: one round trip instead
// of a stat per file through the mount).
static char *git_capture_at(const char *host, const char *port, char *const argv[], size_t *len) {
    if (!host || !host[0]) return git_capture(argv, len);
    char cmd[PATH_MAX * 2] = "", q[PATH_MAX + 64];
    for (int i = 0; argv[i]; i++) {
        sh_quote(q, sizeof q, argv[i]);
        size_t o = strlen(cmd);
        snprintf(cmd + o, sizeof cmd - o, "%s%s", o ? " " : "", q);
    }
    char ctl[PATH_MAX], *ssh[24];
    ssh_argv(ssh, ctl, sizeof ctl, host, port, cmd); // the connection opened when the server was connected (devices.c), if any
    return git_capture(ssh, len);
}

static void git_read_line(const char *path, char *out, size_t cap) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (!fgets(out, (int)cap, f)) out[0] = 0;
    fclose(f);
    out[strcspn(out, "\r\n")] = 0;
}

// Walk up from dir to the folder holding ".git" (a folder, or a file pointing at it for worktrees/submodules).
static bool git_find_root(const char *dir, char *root, size_t cap, char *gitdir, size_t gcap) {
    char cur[PATH_MAX], probe[PATH_MAX + 8];
    snprintf(cur, sizeof cur, "%s", dir);
    for (;;) {
        snprintf(probe, sizeof probe, "%s/.git", strcmp(cur, "/") ? cur : "");
        struct stat st;
        if (lstat(probe, &st) == 0) {
            snprintf(root, cap, "%s", cur);
            snprintf(gitdir, gcap, "%s", probe);
            if (S_ISREG(st.st_mode)) { // "gitdir: <path>" (relative to the root)
                char line[PATH_MAX + 16];
                git_read_line(probe, line, sizeof line);
                if (!strncmp(line, "gitdir: ", 8)) {
                    if (line[8] == '/') snprintf(gitdir, gcap, "%s", line + 8);
                    else snprintf(gitdir, gcap, "%s/%s", cur, line + 8);
                }
            }
            return true;
        }
        char *s = strrchr(cur, '/');
        if (!s || !strcmp(cur, "/")) return false;
        if (s == cur) s[1] = 0;
        else *s = 0;
    }
}

// The shared part of a worktree's git folder (config, stash), from its "commondir" file.
static void git_common_dir(const char *gitdir, char *out, size_t cap) {
    char p[PATH_MAX + 16], line[PATH_MAX];
    snprintf(p, sizeof p, "%s/commondir", gitdir);
    git_read_line(p, line, sizeof line);
    if (!line[0]) snprintf(out, cap, "%s", gitdir);
    else if (line[0] == '/') snprintf(out, cap, "%s", line);
    else snprintf(out, cap, "%s/%s", gitdir, line);
}

static u64 git_sig(const char *gitdir) {
    static const char *const files[] = {"index", "HEAD", "FETCH_HEAD", "config", "logs/refs/stash"};
    char common[PATH_MAX];
    git_common_dir(gitdir, common, sizeof common);
    u64 h = 1469598103934665603ull;
    for (int i = 0; i < 5; i++) {
        char p[PATH_MAX + 32];
        snprintf(p, sizeof p, "%s/%s", i < 3 ? gitdir : common, files[i]);
        struct stat st;
        u64 v = stat(p, &st) == 0 ? (u64)st.st_mtim.tv_sec * 1000000000ull + (u64)st.st_mtim.tv_nsec + (u64)st.st_size : 0;
        h = (h ^ v) * 1099511628211ull;
    }
    return h;
}

// Branch from HEAD into out ("" if unreadable); true when detached (out is then the short commit).
static bool git_read_head(const char *gitdir, char *out, size_t cap) {
    char p[PATH_MAX + 8], line[256];
    snprintf(p, sizeof p, "%s/HEAD", gitdir);
    git_read_line(p, line, sizeof line);
    out[0] = 0;
    if (!strncmp(line, "ref: refs/heads/", 16)) snprintf(out, cap, "%s", line + 16);
    else if (line[0] && strncmp(line, "ref: ", 5)) { snprintf(out, cap, "%.7s", line); return true; }
    return false;
}

// --- jobs ----------------------------------------------------------------------

// Find the repository of a folder (a few stats; no git): root, the folder's place in it, the branch from HEAD.
typedef struct GitResolve {
    char path[PATH_MAX];
    bool repo;
    char root[PATH_MAX], prefix[PATH_MAX], gitdir[PATH_MAX];
    char head[128]; // branch from HEAD, or the short commit when detached
    bool detached;
} GitResolve;

static void git_resolve_done(void *arg); // app.c

static void git_resolve_job(void *arg) {
    GitResolve *r = arg;
    char real[PATH_MAX];
    if (realpath(r->path, real) && git_find_root(real, r->root, sizeof r->root, r->gitdir, sizeof r->gitdir)) {
        r->repo = true;
        size_t rl = strcmp(r->root, "/") ? strlen(r->root) : 0;
        if (real[rl] == '/' && real[rl + 1]) snprintf(r->prefix, sizeof r->prefix, "%s/", real + rl + 1);
        r->detached = git_read_head(r->gitdir, r->head, sizeof r->head);
    }
    main_post(git_resolve_done, r);
}

typedef struct GitJob {
    char root[PATH_MAX], gitdir[PATH_MAX];
    char host[400], port[8], rroot[PATH_MAX]; // in an SFTP folder: "user@host", port and the root there (git runs over ssh)
    u64 sig;        // of the result the main thread has (0: none)
    bool force;     // run even if .git looks unchanged (a folder was reloaded, or the periodic re-check)
    GitResult *res; // NULL: nothing changed
} GitJob;

static void git_job_done(void *arg); // app.c

static void git_details(GitResult *g, const char *host, const char *port, const char *root, const char *gitdir) {
    const char *gitb = tool("FE_GIT", "git");
    size_t len;
    if (g->branch[0] || !g->detached) { // last commit (none before the first one)
        char *argv[] = {(char *)gitb, "-c", "core.fsmonitor=false", "--no-optional-locks", "-C", (char *)root, "log", "-1",
                        "--format=%h%x1f%ct%x1f%an%x1f%s", NULL};
        char *out = git_capture_at(host, port, argv, &len);
        char *f[4] = {0};
        int nf = 0;
        for (char *s = out, *e; s && nf < 4; s = e ? e + 1 : NULL) {
            e = strchr(s, '\x1f');
            if (e) *e = 0;
            f[nf++] = s;
        }
        if (nf == 4) {
            f[3][strcspn(f[3], "\n")] = 0;
            snprintf(g->last_hash, sizeof g->last_hash, "%s", f[0]);
            g->last_time = atoll(f[1]);
            snprintf(g->last_author, sizeof g->last_author, "%s", f[2]);
            snprintf(g->last_subject, sizeof g->last_subject, "%s", f[3]);
        }
        free(out);
    }
    { // remotes: "remote.origin.url git@host:me/x.git" lines
        char *argv[] = {(char *)gitb, "-C", (char *)root, "config", "--get-regexp", "^remote\\..*\\.url$", NULL};
        char *out = git_capture_at(host, port, argv, &len);
        for (char *s = out, *e; s && *s && g->nremotes < GIT_MAX_REMOTES; s = e ? e + 1 : NULL) {
            e = strchr(s, '\n');
            if (e) *e = 0;
            char *sp = strchr(s, ' ');
            if (!sp || strncmp(s, "remote.", 7) || sp - s < 12) continue;
            snprintf(g->remotes[g->nremotes].name, sizeof g->remotes[0].name, "%.*s", (int)(sp - s - 11), s + 7);
            snprintf(g->remotes[g->nremotes].url, sizeof g->remotes[0].url, "%s", sp + 1);
            g->nremotes++;
        }
        free(out);
    }
    char common[PATH_MAX], p[PATH_MAX + 32];
    git_common_dir(gitdir, common, sizeof common);
    snprintf(p, sizeof p, "%s/logs/refs/stash", common); // one line per stash entry
    FILE *f = fopen(p, "r");
    if (f) {
        int c, prev = '\n';
        while ((c = fgetc(f)) != EOF) { if (c == '\n') g->nstash++; prev = c; }
        if (prev != '\n') g->nstash++;
        fclose(f);
    }
}

static void git_job_run(void *arg) {
    GitJob *j = arg;
    GitResult *g = NULL;
    u64 sig = git_sig(j->gitdir);
    if (j->force || sig != j->sig) {
        // core.fsmonitor could name a program in an untrusted repo's config: never let `status` start it
        char *argv[] = {(char *)tool("FE_GIT", "git"), "-c", "core.fsmonitor=false", "--no-optional-locks", "-C",
                        j->host[0] ? j->rroot : j->root, "status", "--porcelain=v2", "-z", "--branch", "--untracked-files=normal", "--ignored=matching", NULL};
        size_t len;
        u64 t0 = now_ns();
        char *out = git_capture_at(j->host, j->port, argv, &len);
        g = out ? git_parse(out, len) : xcalloc(1, sizeof *g);
        g->dur_ns = now_ns() - t0;
        free(out);
        // commits, remotes and stashes only change with .git: a forced re-check of the files skips them
        if (g->repo && (sig != j->sig || !j->sig)) { git_details(g, j->host, j->port, j->host[0] ? j->rroot : j->root, j->gitdir); g->has_details = true; }
        g->sig = sig;
    }
    j->res = g;
    main_post(git_job_done, j);
}

// --- repositories among a folder's subfolders (the branch pill on folder rows) -------------------------------
// No git: a stat for ".git" in each subfolder, then HEAD and the config file of the ones that are repositories.

typedef struct {
    char *name;       // as the listing spells it ("x", "sub/x" in the tree view)
    char branch[64];  // branch, or the short commit when detached ("" before the first commit)
    bool detached;
    char remote[300]; // origin's URL (else the first remote's), "" without one
    char disp[320];   // the remote shortened for display ("github.com/me/x"), filled by the main thread
} GitSub;

typedef struct GitSubsJob {
    char path[PATH_MAX];
    char **names; // the listing's folders (freed by the job)
    int n;
    GitSub *subs; // the repositories among them, sorted by name
    int nsubs;
} GitSubsJob;

static void git_subs_done(void *arg); // app.c

// url of [remote "origin"], else of the first remote in a git config file.
static void git_config_remote(const char *config, char *url, size_t cap) {
    url[0] = 0;
    FILE *f = fopen(config, "r");
    if (!f) return;
    char line[1024];
    bool in_remote = false, origin = false;
    while (fgets(line, sizeof line, f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        s[strcspn(s, "\r\n")] = 0;
        if (*s == '[') {
            in_remote = !strncmp(s, "[remote \"", 9);
            origin = !strncmp(s, "[remote \"origin\"]", 17);
            continue;
        }
        if (!in_remote || strncmp(s, "url", 3) || (s[3] != ' ' && s[3] != '\t' && s[3] != '=')) continue;
        char *v = strchr(s, '=');
        if (!v) continue;
        for (v++; *v == ' ' || *v == '\t'; v++) {}
        size_t l = strlen(v);
        while (l && (v[l - 1] == ' ' || v[l - 1] == '\t')) v[--l] = 0;
        if (l > 1 && v[0] == '"' && v[l - 1] == '"') { v[l - 1] = 0; v++; }
        if (origin || !url[0]) snprintf(url, cap, "%s", v);
        if (origin) break;
    }
    fclose(f);
}

static int git_sub_cmp(const void *a, const void *b) { return strcmp(((const GitSub *)a)->name, ((const GitSub *)b)->name); }

static void git_subs_job(void *arg) {
    GitSubsJob *j = arg;
    int cap = 0;
    for (int i = 0; i < j->n; i++) {
        char dir[PATH_MAX], probe[PATH_MAX + 8], gitdir[PATH_MAX];
        path_join(dir, sizeof dir, j->path, j->names[i]);
        snprintf(probe, sizeof probe, "%s/.git", dir);
        struct stat st;
        if (lstat(probe, &st)) continue;
        snprintf(gitdir, sizeof gitdir, "%s", probe);
        if (S_ISREG(st.st_mode)) { // worktree / submodule: "gitdir: <path>"
            char line[PATH_MAX + 16];
            git_read_line(probe, line, sizeof line);
            if (strncmp(line, "gitdir: ", 8)) continue;
            if (line[8] == '/') snprintf(gitdir, sizeof gitdir, "%s", line + 8);
            else snprintf(gitdir, sizeof gitdir, "%s/%s", dir, line + 8);
        } else if (!S_ISDIR(st.st_mode)) continue;
        GitSub g = {0};
        g.detached = git_read_head(gitdir, g.branch, sizeof g.branch);
        if (!g.branch[0] && !g.detached) { // HEAD unreadable: not a repository after all (or an unborn detached one)
            snprintf(probe, sizeof probe, "%s/HEAD", gitdir);
            if (access(probe, R_OK)) continue;
        }
        char common[PATH_MAX], cfg[PATH_MAX + 16];
        git_common_dir(gitdir, common, sizeof common);
        snprintf(cfg, sizeof cfg, "%s/config", common);
        git_config_remote(cfg, g.remote, sizeof g.remote);
        g.name = j->names[i];
        j->names[i] = NULL;
        if (j->nsubs == cap) { cap = cap ? cap * 2 : 8; j->subs = xrealloc(j->subs, (size_t)cap * sizeof *j->subs); }
        j->subs[j->nsubs++] = g;
    }
    for (int i = 0; i < j->n; i++) free(j->names[i]);
    free(j->names);
    j->names = NULL;
    if (j->nsubs) qsort(j->subs, (size_t)j->nsubs, sizeof *j->subs, git_sub_cmp);
    main_post(git_subs_done, j);
}

// The repository a folder entry is (subs sorted by name), or NULL.
static const GitSub *git_sub_find(const GitSub *subs, int n, const char *name) {
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2, c = strcmp(subs[mid].name, name);
        if (!c) return &subs[mid];
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}
