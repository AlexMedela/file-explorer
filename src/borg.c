// borg.c - BorgBackup repositories (Borg 1.x and Borg 2) browsed as read-only folders: recognised by the files borg
// keeps at their top, mounted with `borg mount` (one folder per archive, dated like the archive) under the runtime
// folder, the passphrase going through our own askpass (never argv or the environment). Remote Borg 1 repositories
// found in SFTP folders are reached with `ssh://` and borg on the server (over the shared ssh connection).

enum { BORG_NONE, BORG_V1, BORG_V2 };

// A folder's kind: Borg 1 keeps a README and a config file at its top, Borg 2 (borgstore) a config folder with a
// readme in it. Both start "This is a Borg Backup repository." A few stats, a read only for likely ones (workers).
static int borg_repo_kind(const char *dir) {
    char p[PATH_MAX + 16], line[64];
    struct stat st;
    int kind = BORG_NONE;
    snprintf(p, sizeof p, "%s/config", dir);
    if (lstat(p, &st)) return BORG_NONE;
    if (S_ISREG(st.st_mode)) {
        snprintf(p, sizeof p, "%s/data", dir);
        if (lstat(p, &st) || !S_ISDIR(st.st_mode)) return BORG_NONE;
        snprintf(p, sizeof p, "%s/README", dir);
        kind = BORG_V1;
    } else if (S_ISDIR(st.st_mode)) {
        snprintf(p, sizeof p, "%s/archives", dir);
        if (lstat(p, &st) || !S_ISDIR(st.st_mode)) return BORG_NONE;
        snprintf(p, sizeof p, "%s/config/readme", dir);
        kind = BORG_V2;
    } else return BORG_NONE;
    int fd = open(p, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return BORG_NONE;
    ssize_t n = read(fd, line, sizeof line - 1);
    close(fd);
    line[n > 0 ? n : 0] = 0;
    return !strncmp(line, "This is a Borg Backup repository", 32) ? kind : BORG_NONE;
}

// Where our mounts live: <runtime>/fe-borg/<repo name>-<hash of where it is>.
static void borg_mount_dir(char *out, size_t cap) {
    const char *ov = getenv("FE_BORG_MNT"); // tests
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (ov && ov[0]) snprintf(out, cap, "%s", ov);
    else snprintf(out, cap, "%s/fe-borg", rt && rt[0] ? rt : "/tmp");
}

static void borg_mount_point(const char *repo, char *out, size_t cap) {
    u64 h = 1469598103934665603ull; // FNV-1a
    for (const u8 *c = (const u8 *)repo; *c; c++) h = (h ^ *c) * 1099511628211ull;
    char base[PATH_MAX], name[96];
    snprintf(base, sizeof base, "%s", repo);
    size_t l = strlen(base);
    while (l > 1 && base[l - 1] == '/') base[--l] = 0;
    snprintf(name, sizeof name, "%.60s", path_basename(base));
    for (char *c = name; *c; c++) if (*c == '-' || (u8)*c < 0x20) *c = '_'; // (the last '-' parts the hash off)
    char dir[PATH_MAX];
    borg_mount_dir(dir, sizeof dir);
    snprintf(out, cap, "%s/%s-%08x", dir, name[0] ? name : "repo", (unsigned)(h ^ (h >> 32)));
}

// The mount table: /proc/self/mounts, or the file FE_MOUNTS names (the tests).
static const char *mounts_file(void) {
    const char *p = getenv("FE_MOUNTS");
    return p && p[0] ? p : "/proc/self/mounts";
}

// A Borg filesystem at (or holding) the path: borg's own FUSE mount, borgmatic's too. A mounted repository shows
// its archives, not its own layout, so the marker files are not there to be found.
static bool borgfs_mount_at(const char *path) {
    if (!path || path[0] != '/') return false;
    FILE *f = fopen(mounts_file(), "re");
    if (!f) return false;
    char line[8192];
    bool found = false;
    while (fgets(line, sizeof line, f)) {
        char type[64], mp[PATH_MAX];
        if (sscanf(line, "%63s %4095s", type, mp) != 2 || !strstr(type, "borg")) continue; // borgfs, borgfs2, fuse.borgfs...
        size_t ml = strlen(mp);
        if (!strncmp(path, mp, ml) && (!path[ml] || path[ml] == '/')) { found = true; break; }
    }
    fclose(f);
    return found;
}

// The mount a path is in (out: its folder), when it's one of ours.
static bool borg_mount_of(const char *path, char *mp, size_t cap) {
    char dir[PATH_MAX];
    borg_mount_dir(dir, sizeof dir);
    size_t dl = strlen(dir);
    if (strncmp(path, dir, dl) || path[dl] != '/' || !path[dl + 1]) return false;
    const char *e = strchr(path + dl + 1, '/');
    snprintf(mp, cap, "%.*s", (int)(e ? e - path : (long)strlen(path)), path);
    return true;
}

// Inside an archive of one of our mounts: "<mp>/<archive>[/rest]". Borg keeps absolute paths without their leading
// '/', so rest is where the item lives on this computer. Plain string work (every frame), like snap_where.
static bool borg_where(const char *path, char *root, size_t rcap, char *live, size_t lcap, char *name, size_t ncap) {
    char mp[PATH_MAX];
    if (!borg_mount_of(path, mp, sizeof mp)) return false;
    const char *a = path + strlen(mp);
    if (*a != '/' || !a[1]) return false;
    a++;
    const char *e = strchr(a, '/');
    size_t al = e ? (size_t)(e - a) : strlen(a);
    snprintf(root, rcap, "%.*s", (int)(a + al - path), path);
    snprintf(live, lcap, "%s", e ? e : "/");
    snprintf(name, ncap, "%.*s", (int)MIN(al, ncap - 1), a);
    return true;
}

// Mounted right now? (/proc/self/mounts; FE_BORG_MOUNTS: tests)
static bool borg_is_mounted(const char *mp) {
    const char *ov = getenv("FE_BORG_MOUNTS");
    FILE *f = fopen(ov ? ov : "/proc/self/mounts", "r");
    if (!f) return false;
    char line[PATH_MAX * 2], src[PATH_MAX], at[PATH_MAX], real[PATH_MAX];
    bool yes = false;
    while (!yes && fgets(line, sizeof line, f)) {
        if (sscanf(line, "%4095s %4095s", src, at) != 2) continue;
        mount_unescape(at, real, sizeof real);
        yes = !strcmp(real, mp);
    }
    fclose(f);
    return yes;
}

// --- opening a repository (worker thread) ----------------------------------------------------------------------

enum { BORG_OK, BORG_E_PASS, BORG_E_NOBORG, BORG_E_VERSION, BORG_E_FUSE, BORG_E_LOCK, BORG_E_NOREMOTE, BORG_E_LOGIN, BORG_E_OTHER };

typedef struct BorgOpen {
    char dir[PATH_MAX];     // the folder it was opened from (local, or an SFTP folder's GVfs / sshfs path)
    char repo[PATH_MAX];    // what borg is given: dir, or ssh://user@host:port/path for a Borg 1 repository on a server
    char key[PATH_MAX + 8]; // keyring entry: "borg:" + where the repository is
    char mp[PATH_MAX];
    char pass[256];         // typed or from the keyring ("" first: unencrypted, keyfile without passphrase, user's env)
    char err[400];          // what went wrong (BORG_E_OTHER: borg's own words)
    char prog[PATH_MAX];
    int kind, rc;
    bool store;             // keep pass in the keyring once it worked
    bool from_keyring;      // pass came from the keyring (wrong: forget it, ask)
    bool retry;             // asked again after a wrong one
    bool remote_tried;      // ssh:// failed for want of borg on the server: went through the folder instead
    bool sftp;              // dir is in an SFTP folder on server t (filled by the main thread, which knows the mounts)
    bool quiet;             // mounted in the background (kept mounted): no prompt, no messages
    SshTarget t;
    void *pane;             // where it opens (app.c checks it's still there; NULL: nowhere)
} BorgOpen;

static bool tool_abs(const char *s, char *out, size_t cap); // units.c
static void borg_open_done(void *arg); // app.c

// Which program runs a repository of this kind: Borg 2 can't read Borg 1 repositories and the other way round, so
// a Borg 2 next to a Borg 1 is usually installed as borg2. Their version is asked once.
static int borg_version(const char *prog) {
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    static char seen[4][PATH_MAX];
    static int ver[4], n;
    pthread_mutex_lock(&mtx);
    for (int i = 0; i < n; i++)
        if (!strcmp(seen[i], prog)) { int v = ver[i]; pthread_mutex_unlock(&mtx); return v; }
    pthread_mutex_unlock(&mtx);
    char out[256];
    char *av[] = {(char *)prog, "--version", NULL};
    int v = 0;
    if (run_capture(av, out, sizeof out) == 0) { // "borg 1.4.5", "borg2 2.0.0b24"
        const char *s = out;
        while (*s && !isdigit((u8)*s)) s++;
        v = atoi(s);
    }
    pthread_mutex_lock(&mtx);
    if (n < 4) { snprintf(seen[n], PATH_MAX, "%s", prog); ver[n++] = v; }
    pthread_mutex_unlock(&mtx);
    return v;
}

static bool borg_program(int kind, char *out, size_t cap) {
    const char *names[2] = {kind == BORG_V2 ? tool("FE_BORG2", "borg2") : tool("FE_BORG1", "borg1"), tool("FE_BORG", "borg")};
    for (int i = 0; i < 2; i++)
        if (tool_abs(names[i], out, cap) && borg_version(out) == (kind == BORG_V2 ? 2 : 1)) return true;
    out[0] = 0;
    return false;
}

// shlex-quoted (borg splits BORG_PASSCOMMAND and BORG_RSH like a shell)
static void borg_quote(const char *s, char *out, size_t cap) {
    size_t o = 0;
    if (o + 1 < cap) out[o++] = '\'';
    for (; *s && o + 5 < cap; s++) {
        if (*s == '\'') { memcpy(out + o, "'\\''", 4); o += 4; }
        else out[o++] = *s;
    }
    if (o + 1 < cap) out[o++] = '\'';
    out[o] = 0;
}

// What borg said: the msgid of its JSON log lines, else words of its text.
static int borg_classify(const char *out, char *err, size_t cap) {
    err[0] = 0;
    for (const char *l = out; l && *l; l = strchr(l, '\n'), l = l ? l + 1 : NULL) { // its last ERROR message
        const char *nl = strchr(l, '\n');
        size_t ll = nl ? (size_t)(nl - l) : strlen(l);
        const char *m = strstr(l, "\"message\": \"");
        if (!m || m > l + ll || !memmem(l, ll, "\"ERROR\"", 7)) continue;
        m += 12;
        size_t k = 0;
        for (; m < l + ll && *m != '"' && k + 1 < cap; m++) {
            if (*m == '\\' && m + 1 < l + ll) m++;
            err[k++] = *m;
        }
        err[k] = 0;
    }
    if (!err[0]) { // plain text: its last line
        const char *last = out, *s = out;
        for (; *s; s++) if (*s == '\n' && s[1] && s[1] != '\n') last = s + 1;
        snprintf(err, cap, "%.*s", (int)strcspn(last, "\n"), last);
    }
    if (strstr(out, "PassphraseWrong") || strstr(out, "NoPassphraseFailure") || strstr(out, "PasscommandFailure")) return BORG_E_PASS;
    if (strstr(out, "FUSE support") || strstr(out, "No module named 'llfuse'") || strstr(out, "No module named 'pyfuse3'")) return BORG_E_FUSE;
    if (strstr(out, "LockTimeout") || strstr(out, "LockFailed") || strstr(out, "LockError")) return BORG_E_LOCK;
    if (strstr(out, "command not found") || strstr(out, "borg: not found") || strstr(out, "ConnectionClosedWithHint")) return BORG_E_NOREMOTE;
    if (strstr(out, "Permission denied (") || strstr(out, "Host key verification failed")) return BORG_E_LOGIN;
    if (strstr(out, "UnsupportedRepoVersion") || strstr(out, "Unsupported repository")) return BORG_E_VERSION;
    return BORG_E_OTHER;
}

// A Borg 1 repository in an SFTP folder: borg on the server ("borg serve") reads it, much faster than through the
// mounted folder. The shared connection (devices.c) or an agent / key logs in; never a password prompt.
static void borg_rsh(char *rsh, size_t rcap) {
    char ctl[PATH_MAX], q[PATH_MAX + 16];
    ssh_control_path(ctl, sizeof ctl);
    borg_quote(ctl, q, sizeof q);
    char sshq[PATH_MAX + 8];
    borg_quote(tool("FE_SSH", "ssh"), sshq, sizeof sshq);
    snprintf(rsh, rcap, "%s -o BatchMode=yes -o ConnectTimeout=10 -o ControlMaster=auto -o %s -o ControlPersist=60", sshq, q);
}

static bool borg_remote_url(const BorgOpen *o, char *url, size_t cap, char *rsh, size_t rcap) {
    if (!o->sftp) return false;
    const SshTarget *t = &o->t;
    borg_rsh(rsh, rcap);
    // ssh://user@host:port/abs/path (Borg 1: "/./x" is relative to the home folder)
    snprintf(url, cap, "ssh://%s%s%s%s%s%s%s", t->user, t->user[0] ? "@" : "", t->host, t->port[0] ? ":" : "", t->port,
             t->path[0] == '/' ? "" : "/./", t->path);
    return true;
}

static int borg_mount_run(BorgOpen *o, const char *repo, const char *rsh) {
    char exe[PATH_MAX], file[PATH_MAX] = "", ev_cmd[PATH_MAX * 2 + 32], ev_file[PATH_MAX + 20], ev_rsh[PATH_MAX * 2];
    ssize_t el = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[el > 0 ? el : 0] = 0;
    const char *av[40];
    int n = 0;
    av[n++] = "env"; // (its -u options before any NAME=value)
    av[n++] = "-u"; av[n++] = "BORG_REPO";
    if (o->pass[0] && el > 0) { // through our own binary as the pass command (devices.c askpass), from a private file
        static atomic_int seq;
        const char *rt = getenv("XDG_RUNTIME_DIR");
        snprintf(file, sizeof file, "%s/fe-askpass-%d-b%d", rt && rt[0] ? rt : "/tmp", (int)getpid(), atomic_fetch_add(&seq, 1));
        int fd = open(file, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        size_t sl = strlen(o->pass);
        if (fd < 0 || write(fd, o->pass, sl) != (ssize_t)sl) file[0] = 0;
        if (fd >= 0) close(fd);
        char q[PATH_MAX + 16];
        borg_quote(exe, q, sizeof q);
        snprintf(ev_cmd, sizeof ev_cmd, "BORG_PASSCOMMAND=%s passphrase", q);
        snprintf(ev_file, sizeof ev_file, "FE_ASKPASS_FILE=%s", file);
        av[n++] = "-u"; av[n++] = "BORG_PASSPHRASE";
        av[n++] = "-u"; av[n++] = "BORG_PASSPHRASE_FD";
        av[n++] = ev_cmd;
        av[n++] = ev_file;
    } else if (!getenv("BORG_PASSPHRASE") && !getenv("BORG_PASSCOMMAND") && !getenv("BORG_PASSPHRASE_FD")) {
        av[n++] = "BORG_PASSPHRASE="; // an encrypted one says so instead of waiting for a terminal
    }
    av[n++] = "BORG_RELOCATED_REPO_ACCESS_IS_OK=yes";        // a USB drive mounted somewhere else this time
    av[n++] = "BORG_UNKNOWN_UNENCRYPTED_REPO_ACCESS_IS_OK=yes"; // (only reading)
    av[n++] = "BORG_DISPLAY_PASSPHRASE=no";
    if (rsh) { snprintf(ev_rsh, sizeof ev_rsh, "BORG_RSH=%s", rsh); av[n++] = ev_rsh; }
    av[n++] = o->prog;
    av[n++] = "--log-json";
    if (o->kind == BORG_V2) { av[n++] = "-r"; av[n++] = repo; }
    av[n++] = "mount";
    av[n++] = "-o";
    av[n++] = "ignore_permissions"; // files of other users (a system backup): whoever can read the repository may
    if (o->kind != BORG_V2) av[n++] = repo;
    av[n++] = o->mp;
    av[n] = NULL;
    char out[8192];
    int rc = run_capture((char *const *)av, out, sizeof out); // (borg returns once the mount is up, then serves it)
    if (file[0]) unlink(file);
    if (rc == 0 && borg_is_mounted(o->mp)) return BORG_OK;
    if (rc == -1 || rc == 127) { snprintf(o->err, sizeof o->err, "Couldn't run %s", o->prog); return BORG_E_NOBORG; }
    int k = borg_classify(out, o->err, sizeof o->err);
    if (!o->err[0]) snprintf(o->err, sizeof o->err, "borg mount didn't work (exit code %d)", rc);
    return k;
}

static void borg_open_job(void *arg) {
    BorgOpen *o = arg;
    o->err[0] = 0;
    bool url = !strncmp(o->dir, "ssh://", 6); // a saved remote one: its address (the kind was saved with it)
    if (!o->kind && !url) o->kind = borg_repo_kind(o->dir);
    if (!o->kind) { o->rc = BORG_E_OTHER; snprintf(o->err, sizeof o->err, "This folder isn't a Borg repository"); goto done; }
    if (!borg_program(o->kind, o->prog, sizeof o->prog)) { o->rc = BORG_E_NOBORG; goto done; }
    if (!o->key[0]) snprintf(o->key, sizeof o->key, "borg:%s", o->dir);
    char rsh[PATH_MAX * 2];
    bool remote = o->kind == BORG_V1 && !o->remote_tried && borg_remote_url(o, o->repo, sizeof o->repo, rsh, sizeof rsh);
    if (!remote) snprintf(o->repo, sizeof o->repo, "%s", o->dir);
    if (url) borg_rsh(rsh, sizeof rsh);
    borg_mount_point(o->repo, o->mp, sizeof o->mp);
    if (borg_is_mounted(o->mp)) { o->rc = BORG_OK; goto done; } // open already (another tab)
    mkdir_p(o->mp, 0700);
    if (!o->pass[0] && !o->retry && secret_available() && secret_lookup(o->key, o->pass, sizeof o->pass)) {
        size_t l = strlen(o->pass);
        if (l && o->pass[l - 1] == '\n') o->pass[l - 1] = 0;
        o->from_keyring = true;
    }
    o->rc = borg_mount_run(o, o->repo, remote || url ? rsh : NULL);
    if (remote && (o->rc == BORG_E_NOREMOTE || o->rc == BORG_E_LOGIN)) { // no borg on the server: through the folder
        rmdir(o->mp);
        o->remote_tried = true;
        snprintf(o->repo, sizeof o->repo, "%s", o->dir);
        borg_mount_point(o->repo, o->mp, sizeof o->mp);
        if (borg_is_mounted(o->mp)) { o->rc = BORG_OK; goto done; }
        mkdir_p(o->mp, 0700);
        o->rc = borg_mount_run(o, o->repo, NULL);
    }
    if (o->rc == BORG_E_PASS && o->from_keyring) secret_clear(o->key); // changed since: ask
    if (o->rc == BORG_OK && o->store && o->pass[0]) secret_store(o->key, o->pass);
    if (o->rc != BORG_OK) rmdir(o->mp);
done:
    explicit_bzero(o->pass, sizeof o->pass);
    main_post(borg_open_done, o);
}

// Unmount (worker): borg's own umount is fusermount underneath.
static void borg_umount_job(void *arg) {
    char *mp = arg;
    char out[512];
    const char *fum = tool("FE_FUSERMOUNT", "fusermount3");
    char *av[] = {(char *)fum, "-u", mp, NULL};
    if (run_capture(av, out, sizeof out) != 0) {
        char *av2[] = {"fusermount", "-u", mp, NULL};
        if (!app_selftest()) run_capture(av2, out, sizeof out);
    }
    if (!borg_is_mounted(mp)) rmdir(mp);
    free(mp);
}
