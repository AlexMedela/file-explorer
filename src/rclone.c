// rclone.c - cloud storage (rclone remotes: Google Drive, OneDrive, S3, Dropbox...) as sidebar places.
//
// The remotes are what `rclone listremotes --long` reports; they're set up with `rclone config` (the section's +
// runs it in a terminal tab). Connecting one writes a systemd user unit
// ~/.config/systemd/user/file-explorer-cloud-<name>.service running `rclone mount <name>: <folder>` (units.c):
// it keeps running when the app is closed, restarts after failures and can start at login.
// Type=notify: the unit is active once the folder is mounted. The folders are $XDG_DATA_HOME/file-explorer/cloud/<name>.
// A remote mounted some other way (a fuse.rclone mount of "<name>:") shows as connected, at its own folder.

#define MAX_CLOUDS 32
enum { CS_OFF, CS_STARTING, CS_UP, CS_RETRY, CS_FAILED };

typedef struct {
    char name[96];        // remote name (no colon)
    char type[32];        // backend: drive, onedrive, s3...
    bool listed;          // in rclone's config (else only a remembered setting)
    bool login;           // connect at login (setting)
    // live state (main thread)
    int state;            // CS_*
    bool unit;            // our unit file exists
    char mount[PATH_MAX]; // where it's mounted ("" = not)
    bool ours;            // mounted by our unit (else by something else: unmounted with fusermount)
    char err[240];        // last problem, in plain words
    int open_after;       // once connected: 1 open where the user is, 2 in a new tab
    u64 asked;            // when the user asked to connect (ns)
    u64 notify_until;     // a connect the user asked for: report a failure until then (ns)
    bool notified;
} Cloud;

static Cloud g_cloud[MAX_CLOUDS];
static int g_ncloud;
static bool g_cloud_collapsed;
static int g_rclone = -1;          // rclone installed: -1 not known yet, 0 no, 1 yes
static atomic_int g_cloud_job;     // status / command jobs in flight
static u64 g_cloud_polled, g_cloud_fast_until;
static bool g_cloud_relist = true; // ask rclone for its remotes on the next poll (start, config changed, Refresh)
static i64 g_cloud_conf_mtime = -1;
static char g_cloud_term_cmd[600]; // self-tests: the terminal command that would have opened

static const char *cloud_rclone(void) { return tool("FE_RCLONE", "rclone"); }

static void cloud_base(char *out, size_t cap) {
    const char *ov = getenv("FE_CLOUD_DIR"), *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    if (ov && ov[0] == '/') snprintf(out, cap, "%s", ov);
    else if (x && x[0] == '/') snprintf(out, cap, "%s/file-explorer/cloud", x);
    else snprintf(out, cap, "%s/.local/share/file-explorer/cloud", h ? h : "/tmp");
}

static void cloud_dir(const char *name, char *out, size_t cap) {
    char b[PATH_MAX];
    cloud_base(b, sizeof b);
    snprintf(out, cap, "%s/%s", b, name);
}

// Under our mount folders: treated like any network mount (the main thread never touches it).
static bool path_in_cloud(const char *path) {
    char b[PATH_MAX];
    cloud_base(b, sizeof b);
    size_t l = strlen(b);
    return !strncmp(path, b, l) && (path[l] == 0 || path[l] == '/');
}

// Unit names keep letters, digits, - and .; anything else becomes _xx (hex), so every name maps to one unit.
static void cloud_unit_name(const char *name, char *out, size_t cap) {
    size_t o = (size_t)snprintf(out, cap, "file-explorer-cloud-");
    for (const char *c = name; *c && o + 12 < cap; c++)
        o += isalnum((u8)*c) || *c == '-' || *c == '.' ? (size_t)snprintf(out + o, cap - o, "%c", *c) : (size_t)snprintf(out + o, cap - o, "_%02x", (u8)*c);
    snprintf(out + o, cap - o, ".service");
}

// rclone remote names: letters, digits, _ - . + @ and spaces (not at the ends).
static bool cloud_name_ok(const char *s) {
    if (!s[0] || s[0] == '-' || s[0] == ' ' || s[strlen(s) - 1] == ' ') return false;
    for (const char *c = s; *c; c++)
        if (!isalnum((u8)*c) && !strchr("_-.+@ ", *c)) return false;
    return true;
}

static const struct { const char *type, *label, *icon; } g_cloud_types[] = {
    {"drive", "Google Drive", "gdrive"}, {"onedrive", "OneDrive", "onedrive"}, {"dropbox", "Dropbox", "dropbox"},
    {"s3", "S3", "aws"}, {"azureblob", "Azure Blob", "azure"}, {"azurefiles", "Azure Files", "azure"},
    {"google cloud storage", "Google Cloud Storage", "gcp"}, {"gphotos", "Google Photos", "gdrive"},
    {"sftp", "SFTP", "server"}, {"webdav", "WebDAV", "network"}, {"smb", "SMB", "nas"}, {"ftp", "FTP", "network"},
    {"crypt", "Encrypted", "cloud"}, {"local", "Local", "disk"},
};

static int cloud_type_find(const char *type) {
    for (size_t i = 0; i < ARRAY_COUNT(g_cloud_types); i++)
        if (!strcmp(type, g_cloud_types[i].type)) return (int)i;
    return -1;
}
static const char *cloud_icon(const Cloud *c) { int k = cloud_type_find(c->type); return k >= 0 ? g_cloud_types[k].icon : "cloud"; }
static const char *cloud_type_label(const Cloud *c) { int k = cloud_type_find(c->type); return k >= 0 ? g_cloud_types[k].label : c->type; }

static Cloud *cloud_find(const char *name) {
    for (int i = 0; i < g_ncloud; i++)
        if (!strcmp(g_cloud[i].name, name)) return &g_cloud[i];
    return NULL;
}

static Cloud *cloud_add(const char *name) {
    Cloud *c = cloud_find(name);
    if (c || g_ncloud >= MAX_CLOUDS) return c;
    c = &g_cloud[g_ncloud++];
    memset(c, 0, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", name);
    return c;
}

// The connected remote a mount folder belongs to (ours or the user's own rclone mount), or NULL.
static Cloud *cloud_for_mount(const char *path) {
    for (int i = 0; i < g_ncloud; i++)
        if (g_cloud[i].listed && g_cloud[i].mount[0] && !strcmp(g_cloud[i].mount, path)) return &g_cloud[i];
    return NULL;
}

static bool cloud_busy(const Cloud *c) { return c->state == CS_STARTING || c->state == CS_RETRY; }

// --- settings: cloud=name \t flags ----------------------------------------------------------------------------

static bool cloud_settings_line(const char *k, const char *v) {
    if (!strcmp(k, "cloud_collapsed")) { g_cloud_collapsed = atoi(v); return true; }
    if (strcmp(k, "cloud")) return false;
    char nm[96];
    snprintf(nm, sizeof nm, "%.*s", (int)strcspn(v, "\t"), v);
    const char *fl = strchr(v, '\t');
    if (!cloud_name_ok(nm)) return true;
    Cloud *c = cloud_add(nm);
    if (c) c->login = fl && strstr(fl, "login");
    return true;
}

static void cloud_settings_save(FILE *f) {
    fprintf(f, "cloud_collapsed=%d\n", g_cloud_collapsed);
    for (int i = 0; i < g_ncloud; i++)
        if (g_cloud[i].login) fprintf(f, "cloud=%s\tlogin\n", g_cloud[i].name);
}

// --- the unit --------------------------------------------------------------------------------------------------

static void cloud_conf_path(char *out, size_t cap) {
    const char *rc = getenv("RCLONE_CONFIG"), *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
    if (rc && rc[0]) snprintf(out, cap, "%s", rc);
    else if (x && x[0] == '/') snprintf(out, cap, "%s/rclone/rclone.conf", x);
    else snprintf(out, cap, "%s/.config/rclone/rclone.conf", h ? h : "/tmp");
}

// Only uses its arguments and the environment: also written from worker threads (the sync after a listing).
static void cloud_unit_text(const char *name, const char *type_label, char *out, size_t cap) {
    char rclone[PATH_MAX], mkd[PATH_MAX], fum[PATH_MAX], dir[PATH_MAX], e_rc[PATH_MAX * 2], e_dir[PATH_MAX * 2], e_nm[200], e_conf[PATH_MAX * 2] = "";
    if (!tool_abs(cloud_rclone(), rclone, sizeof rclone)) snprintf(rclone, sizeof rclone, "/usr/bin/rclone");
    if (!tool_abs("mkdir", mkd, sizeof mkd)) snprintf(mkd, sizeof mkd, "/usr/bin/mkdir");
    if (!tool_abs("fusermount3", fum, sizeof fum) && !tool_abs("fusermount", fum, sizeof fum)) fum[0] = 0;
    cloud_dir(name, dir, sizeof dir);
    unit_escape(rclone, e_rc, sizeof e_rc);
    unit_escape(dir, e_dir, sizeof e_dir);
    unit_escape(name, e_nm, sizeof e_nm);
    const char *rcc = getenv("RCLONE_CONFIG"); // the app's own config: the user manager may not have this variable
    if (rcc && rcc[0]) {
        char t[PATH_MAX * 2];
        unit_escape(rcc, t, sizeof t);
        snprintf(e_conf, sizeof e_conf, " --config \"%s\"", t);
    }
    char stop[PATH_MAX * 3] = ""; // a mount left behind by a crash would block the next start
    if (fum[0]) snprintf(stop, sizeof stop, "ExecStopPost=-\"%s\" -uz \"%s\"\n", fum, e_dir);
    snprintf(out, cap,
             "# Written by file-explorer for the cloud remote \"%s\". Edit it in the app: this file is replaced.\n"
             "[Unit]\n"
             "Description=Files cloud: %s (%s)\n"
             "StartLimitIntervalSec=0\n\n"
             "[Service]\n"
             "Type=notify\n"
             "ExecStartPre=\"%s\" -p \"%s\"\n"
             "ExecStart=\"%s\" mount \"%s:\" \"%s\"%s --vfs-cache-mode full\n"
             "%s"
             "Restart=on-failure\n"
             "RestartSec=15\n\n"
             "[Install]\n"
             "WantedBy=default.target\n",
             e_nm, e_nm, type_label, mkd, e_dir, e_rc, e_nm, e_dir, e_conf, stop);
}

static bool cloud_unit_ours(const char *path) {
    char head[64] = "";
    FILE *f = fopen(path, "r");
    if (!f) return false;
    if (!fgets(head, sizeof head, f)) head[0] = 0;
    fclose(f);
    return !strncmp(head, "# Written by file-explorer for the cloud", 40);
}

// --- commands (worker thread) ----------------------------------------------------------------------------------

enum { COP_MOUNT, COP_STOP, COP_LOGIN, COP_FUSE_UNMOUNT };
typedef struct {
    int op;
    char name[96], unit[160], path[PATH_MAX + 200], dir[PATH_MAX], text[8192];
    bool login;
    char out[600];
    int rc;
} CloudOp;

static void cloud_op_done(void *arg);

static void cloud_op_job(void *arg) {
    CloudOp *o = arg;
    char udir[PATH_MAX];
    units_dir(udir, sizeof udir);
    switch (o->op) {
    case COP_MOUNT: case COP_LOGIN: { // the unit as it should be, enabled or not; then started (connect)
        mkdir_p(udir, 0755);
        if (o->op == COP_MOUNT) mkdir_p(o->dir, 0700);
        o->rc = 0;
        if (!file_equals(o->path, o->text)) {
            if (!write_file_atomic(o->path, o->text)) { o->rc = -2; snprintf(o->out, sizeof o->out, "Couldn't write %s", o->path); break; }
            o->rc = units_ctl((char *[]){"daemon-reload", NULL}, o->out, sizeof o->out);
        }
        if (o->rc == 0) o->rc = units_ctl((char *[]){o->login ? "enable" : "disable", o->unit, NULL}, o->out, sizeof o->out);
        if (o->rc == 0 && o->op == COP_MOUNT) {
            units_ctl((char *[]){"reset-failed", o->unit, NULL}, NULL, 0);
            o->rc = units_ctl((char *[]){"--no-block", "start", o->unit, NULL}, o->out, sizeof o->out);
        }
        break;
    }
    case COP_STOP: o->rc = units_ctl((char *[]){"stop", o->unit, NULL}, o->out, sizeof o->out); break;
    case COP_FUSE_UNMOUNT: { // someone else's rclone mount
        char fum[PATH_MAX];
        if (!tool_abs(tool("FE_FUSERMOUNT", "fusermount3"), fum, sizeof fum)) snprintf(fum, sizeof fum, "fusermount");
        o->rc = run_capture((char *[]){fum, "-u", o->dir, NULL}, o->out, sizeof o->out);
        break;
    }
    }
    main_post(cloud_op_done, o);
}

static void cloud_poll_now(void) { g_cloud_polled = 0; main_wake(); }

static void cloud_op_done(void *arg) {
    CloudOp *o = arg;
    atomic_fetch_sub(&g_cloud_job, 1);
    Cloud *c = cloud_find(o->name);
    if (o->rc == -1 && o->op != COP_FUSE_UNMOUNT) {
        if (g_units_systemd) toast("Cloud folders need systemd user services (systemctl --user isn't available)");
        g_units_systemd = false;
        if (c) { c->state = CS_OFF; c->open_after = 0; }
    } else if (o->rc != 0) {
        char *nl = strchr(o->out, '\n');
        if (nl) *nl = 0;
        toast("%s: %s", o->name, o->out[0] ? o->out : "couldn't do that");
        if (c && o->op == COP_MOUNT) { c->state = CS_OFF; c->open_after = 0; }
    }
    if (c && o->op == COP_MOUNT && o->rc == 0) c->unit = true;
    free(o);
    cloud_poll_now();
    A.redraw = true;
}

static CloudOp *cloud_op_new(int op, const Cloud *c) {
    CloudOp *o = xcalloc(1, sizeof *o);
    char udir[PATH_MAX];
    o->op = op;
    snprintf(o->name, sizeof o->name, "%s", c->name);
    units_dir(udir, sizeof udir);
    cloud_unit_name(c->name, o->unit, sizeof o->unit);
    snprintf(o->path, sizeof o->path, "%s/%s", udir, o->unit);
    if (op == COP_FUSE_UNMOUNT) snprintf(o->dir, sizeof o->dir, "%s", c->mount);
    else cloud_dir(c->name, o->dir, sizeof o->dir);
    o->login = c->login;
    if (op == COP_MOUNT || op == COP_LOGIN) cloud_unit_text(c->name, cloud_type_label(c), o->text, sizeof o->text);
    return o;
}

static void cloud_submit(CloudOp *o) {
    atomic_fetch_add(&g_cloud_job, 1);
    g_cloud_fast_until = now_ns() + 20000000000ull; // poll quickly while it settles
    pool_submit(cloud_op_job, o);
}

static void init_places(void);

// Connect (mount) and, once it's there, open it: how = 1 where the user is, 2 in a new tab, 0 not at all.
static void cloud_connect(int i, int how) {
    if (i < 0 || i >= g_ncloud) return;
    Cloud *c = &g_cloud[i];
    if (c->mount[0]) {
        if (how == 2) tab_new(c->mount, A.cur + 1, true);
        else if (how == 1) open_folder(c->mount);
        return;
    }
    if (!g_units_systemd) { toast("Cloud folders need systemd user services (systemctl --user isn't available)"); return; }
    c->open_after = how;
    if (cloud_busy(c)) return;
    c->state = CS_STARTING;
    c->err[0] = 0;
    c->asked = now_ns();
    c->notify_until = c->asked + 60000000000ull; // report a failure of this connect
    c->notified = false;
    cloud_submit(cloud_op_new(COP_MOUNT, c));
    A.redraw = true;
}

static void cloud_disconnect(int i) {
    if (i < 0 || i >= g_ncloud) return;
    Cloud *c = &g_cloud[i];
    if (c->mount[0]) panes_leave(c->mount); // a mount in use can't go away
    c->open_after = 0;
    c->notify_until = 0;
    c->err[0] = 0;
    if (c->mount[0] && !c->ours) cloud_submit(cloud_op_new(COP_FUSE_UNMOUNT, c));
    else cloud_submit(cloud_op_new(COP_STOP, c));
    c->state = CS_OFF;
    A.redraw = true;
}

static void cloud_set_login(int i, bool on) {
    if (i < 0 || i >= g_ncloud) return;
    Cloud *c = &g_cloud[i];
    c->login = on;
    A.settings_dirty = true;
    if (g_units_systemd) cloud_submit(cloud_op_new(COP_LOGIN, c));
}

static Tab *term_tab_open(const char *dir, char *const argv[]);

// A terminal tab running rclone (config, reconnect a login); self-tests only note the command.
static void cloud_terminal(char *const argv[]) {
    if (A.selftest) {
        size_t o = 0;
        g_cloud_term_cmd[0] = 0;
        for (int k = 0; argv[k] && o + 2 < sizeof g_cloud_term_cmd; k++)
            o += (size_t)snprintf(g_cloud_term_cmd + o, sizeof g_cloud_term_cmd - o, "%s%s", k ? " " : "", argv[k]);
        return;
    }
    term_tab_open(NULL, argv);
    g_cloud_relist = true; // the config may change in there: look at it again (its time stamp tells, too)
}

static void cloud_config_terminal(void) { cloud_terminal((char *[]){(char *)cloud_rclone(), "config", NULL}); }

static void cloud_reconnect_terminal(const char *name) {
    char rn[120];
    snprintf(rn, sizeof rn, "%s:", name);
    cloud_terminal((char *[]){(char *)cloud_rclone(), "config", "reconnect", rn, NULL});
}

static void cloud_log_terminal(const char *name) {
    char unit[160];
    cloud_unit_name(name, unit, sizeof unit);
    cloud_terminal((char *[]){(char *)tool("FE_JOURNALCTL", "journalctl"), "--user", "-u", unit, "-n", "200", "-f", "-o", "short-iso", NULL});
}

// --- status (worker: the remotes, the mounts, systemctl show, the journal of failing units) --------------------

typedef struct {
    // in
    bool relist;
    i64 conf_mtime;
    int n;
    char name[MAX_CLOUDS][96], unit[MAX_CLOUDS][160];
    // out
    int have_rclone; // 0 / 1
    bool listed;     // the remotes below are rclone's current list
    int nr;
    char rname[MAX_CLOUDS][96], rtype[MAX_CLOUDS][32];
    char mount[MAX_CLOUDS][PATH_MAX];   // per name (in) / rname (when listed)
    bool ours[MAX_CLOUDS], unit_exists[MAX_CLOUDS];
    int state[MAX_CLOUDS];
    char err[MAX_CLOUDS][240];
    bool no_systemd; // systemctl couldn't run
} CloudStatus;

static void cloud_status_done(void *arg);

// Where remote name is mounted (fuse.rclone, source "name:" or "name:sub/path"); ours preferred. false: nowhere.
static bool cloud_mount_lookup(const char *name, char *out, size_t cap, bool *ours) {
    const char *ov = getenv("FE_PROC_MOUNTS"); // tests
    FILE *f = fopen(ov ? ov : "/proc/self/mounts", "r");
    if (!f) return false;
    char line[PATH_MAX * 2 + 200], src[PATH_MAX], mp[PATH_MAX], type[64], mine[PATH_MAX];
    cloud_dir(name, mine, sizeof mine);
    size_t nl = strlen(name);
    bool found = false;
    *ours = false;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%4095s %4095s %63s", src, mp, type) != 3 || strcmp(type, "fuse.rclone")) continue;
        char s2[PATH_MAX], m2[PATH_MAX];
        mount_unescape(src, s2, sizeof s2);
        mount_unescape(mp, m2, sizeof m2);
        if (strncmp(s2, name, nl) || s2[nl] != ':') continue;
        bool mn = !strcmp(m2, mine);
        if (found && !mn) continue;
        snprintf(out, cap, "%s", m2);
        found = true;
        *ours = mn;
        if (mn) break;
    }
    fclose(f);
    return found;
}

// The journal's last rclone complaint, in plain words.
static void cloud_explain(const char *log, char *out, size_t cap) {
    out[0] = 0;
    struct { const char *needle, *msg; } known[] = {
        {"didn't find section in config file", "This remote isn't in rclone's configuration any more"},
        {"invalid_grant", "The sign-in expired: reconnect the account"},
        {"couldn't fetch token", "The sign-in expired: reconnect the account"},
        {"token expired", "The sign-in expired: reconnect the account"},
        {"empty token found", "Not signed in: reconnect the account"},
        {"fusermount", "FUSE isn't installed (the fuse3 package)"},
        {"/dev/fuse", "FUSE isn't available (the fuse3 package, the fuse kernel module)"},
        {"directory already mounted", "Its folder is already in use by another mount"},
        {"is not empty", "Its folder isn't empty"},
        {"no such host", "No network or the service can't be reached (retrying)"},
        {"network is unreachable", "No network (retrying)"},
        {"i/o timeout", "The service didn't answer (retrying)"},
        {"AccessDenied", "Access denied: check the remote's keys and permissions"},
        {"InvalidAccessKeyId", "The access key isn't valid"},
        {"SignatureDoesNotMatch", "The secret key isn't valid"},
        {"NoSuchBucket", "That bucket doesn't exist"},
    };
    const char *best = NULL;
    for (size_t k = 0; k < ARRAY_COUNT(known); k++) {
        const char *at = NULL;
        for (const char *q = log; (q = strstr(q, known[k].needle)); q++) at = q;
        if (at && (!best || at > best)) { best = at; snprintf(out, cap, "%s", known[k].msg); }
    }
    if (out[0] || !log[0]) return;
    const char *last = NULL; // unknown: the last error line, without rclone's time stamp
    for (const char *q = log; *q; ) {
        size_t l = strcspn(q, "\n");
        if (memmem(q, l, "ERROR", 5) || memmem(q, l, "CRITICAL", 8) || memmem(q, l, "Fatal", 5)) last = q;
        q += l + (q[l] ? 1 : 0);
    }
    if (!last) return;
    size_t l = strcspn(last, "\n");
    const char *c = memmem(last, l, " : ", 3);
    if (c) { l -= (size_t)(c + 3 - last); last = c + 3; }
    snprintf(out, cap, "%.*s", (int)MIN(l, cap - 1), last);
}

static void cloud_status_job(void *arg) {
    CloudStatus *s = arg;
    char rclone[PATH_MAX], udir[PATH_MAX];
    units_dir(udir, sizeof udir);
    s->have_rclone = tool_abs(cloud_rclone(), rclone, sizeof rclone) && access(rclone, X_OK) == 0;
    { // rclone's configuration changed (rclone config in a terminal, another program): list again
        char cp[PATH_MAX];
        struct stat st;
        cloud_conf_path(cp, sizeof cp);
        i64 m = stat(cp, &st) == 0 ? (i64)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec : 0;
        if (m != s->conf_mtime) s->relist = true;
        s->conf_mtime = m;
    }
    if (s->relist && s->have_rclone) {
        size_t cap = 64 * 1024;
        char *out = xmalloc(cap);
        if (run_capture((char *[]){rclone, "listremotes", "--long", NULL}, out, cap) == 0) {
            s->listed = true;
            for (char *p = out, *line; (line = strsep(&p, "\n")) && s->nr < MAX_CLOUDS;) {
                // "name:   type" (names may hold spaces; types don't hold colons)
                char *colon = NULL;
                for (char *q = line; (q = strchr(q, ':')); q++) colon = q;
                if (!colon) continue;
                *colon = 0;
                char *ty = colon + 1;
                while (*ty == ' ' || *ty == '\t') ty++;
                ty[strcspn(ty, " \t\r")] = 0;
                if (!cloud_name_ok(line)) continue;
                snprintf(s->rname[s->nr], sizeof s->rname[0], "%s", line);
                snprintf(s->rtype[s->nr], sizeof s->rtype[0], "%s", ty);
                s->nr++;
            }
        }
        free(out);
        if (s->listed) { // units match the list: ours rewritten where they differ (the app or rclone moved), orphans removed
            bool changed = false;
            for (int i = 0; i < s->nr; i++) {
                char u[160], p[PATH_MAX + 200], text[8192];
                cloud_unit_name(s->rname[i], u, sizeof u);
                snprintf(p, sizeof p, "%s/%s", udir, u);
                if (!cloud_unit_ours(p)) continue;
                Cloud tmp = {0};
                snprintf(tmp.type, sizeof tmp.type, "%s", s->rtype[i]);
                cloud_unit_text(s->rname[i], cloud_type_label(&tmp), text, sizeof text);
                if (!file_equals(p, text)) changed |= write_file_atomic(p, text);
            }
            DIR *d = opendir(udir);
            struct dirent *de;
            while (d && (de = readdir(d)) != NULL) {
                if (strncmp(de->d_name, "file-explorer-cloud-", 20) || !strstr(de->d_name, ".service") || strstr(de->d_name, ".tmp")) continue;
                bool keep = false;
                for (int i = 0; i < s->nr && !keep; i++) {
                    char u[160];
                    cloud_unit_name(s->rname[i], u, sizeof u);
                    keep = !strcmp(u, de->d_name);
                }
                char p[PATH_MAX + 200];
                snprintf(p, sizeof p, "%s/%s", udir, de->d_name);
                if (keep || !cloud_unit_ours(p)) continue;
                units_ctl((char *[]){"disable", "--now", de->d_name, NULL}, NULL, 0);
                unlink(p);
                changed = true;
            }
            if (d) closedir(d);
            if (changed) units_ctl((char *[]){"daemon-reload", NULL}, NULL, 0);
            s->n = s->nr; // the status below is for the new list
            for (int i = 0; i < s->nr; i++) {
                snprintf(s->name[i], sizeof s->name[0], "%s", s->rname[i]);
                cloud_unit_name(s->rname[i], s->unit[i], sizeof s->unit[0]);
            }
        }
    }
    // mounts, then the units of the remotes that have one
    char *argv[MAX_CLOUDS + 8];
    int na = 0;
    argv[na++] = (char *)units_systemctl();
    argv[na++] = "--user";
    argv[na++] = "show";
    argv[na++] = "-p";
    argv[na++] = "Id,ActiveState,SubState";
    int first_unit = na;
    for (int i = 0; i < s->n; i++) {
        s->state[i] = CS_OFF;
        s->mount[i][0] = 0;
        if (cloud_mount_lookup(s->name[i], s->mount[i], sizeof s->mount[i], &s->ours[i])) s->state[i] = CS_UP;
        char p[PATH_MAX + 200];
        snprintf(p, sizeof p, "%s/%s", udir, s->unit[i]);
        s->unit_exists[i] = access(p, F_OK) == 0;
        if (s->unit_exists[i]) argv[na++] = s->unit[i];
    }
    argv[na] = NULL;
    if (na > first_unit && g_units_systemd) {
        size_t cap = 512 + (size_t)s->n * 256;
        char *out = xmalloc(cap);
        s->no_systemd = run_capture(argv, out, cap) == -1;
        char id[200] = "", active[32] = "", sub[32] = "";
        for (char *p = out, *line;;) {
            line = strsep(&p, "\n");
            if (!line || !line[0]) { // end of a unit's block
                for (int i = 0; i < s->n && id[0]; i++) {
                    if (strcmp(s->unit[i], id) || (s->state[i] == CS_UP && s->ours[i])) continue;
                    if (!strcmp(active, "activating") && strcmp(sub, "auto-restart")) s->state[i] = s->state[i] == CS_UP ? CS_UP : CS_STARTING;
                    else if (!strcmp(sub, "auto-restart")) s->state[i] = CS_RETRY;
                    else if (!strcmp(active, "failed")) s->state[i] = s->state[i] == CS_UP ? CS_UP : CS_FAILED;
                    else if (!strcmp(active, "active") && s->state[i] != CS_UP) s->state[i] = CS_STARTING; // (mounted any moment)
                }
                id[0] = active[0] = sub[0] = 0;
                if (!line) break;
                continue;
            }
            if (!strncmp(line, "Id=", 3)) snprintf(id, sizeof id, "%s", line + 3);
            else if (!strncmp(line, "ActiveState=", 12)) snprintf(active, sizeof active, "%s", line + 12);
            else if (!strncmp(line, "SubState=", 9)) snprintf(sub, sizeof sub, "%s", line + 9);
        }
        free(out);
    }
    for (int i = 0; i < s->n; i++) { // why it isn't up: the last lines of its log
        s->err[i][0] = 0;
        if (s->state[i] != CS_RETRY && s->state[i] != CS_FAILED) continue;
        char log[6000];
        char *jv[] = {(char *)tool("FE_JOURNALCTL", "journalctl"), "--user", "-u", s->unit[i], "-n", "15", "-o", "cat", "--no-pager", NULL};
        if (run_capture(jv, log, sizeof log) == 0) cloud_explain(log, s->err[i], sizeof s->err[i]);
    }
    main_post(cloud_status_done, s);
}

static void cloud_reconnect_cb(int choice, bool check, void *ud) {
    (void)check;
    if (choice == 0) cloud_reconnect_terminal(ud);
}

static void cloud_status_done(void *arg) {
    CloudStatus *s = arg;
    atomic_fetch_sub(&g_cloud_job, 1);
    bool rows = false; // the sidebar rows change (remotes, connected or not)
    bool installed = s->have_rclone && g_rclone != 1; // (just installed: list its remotes next)
    if (g_rclone != s->have_rclone) { g_rclone = s->have_rclone; rows = true; }
    g_cloud_conf_mtime = s->conf_mtime;
    if (s->no_systemd) g_units_systemd = false;
    if (s->relist) g_cloud_relist = false; // (a broken config: asked again when it changes, or on Refresh)
    if (installed && !s->listed) g_cloud_relist = true;
    if (s->listed) {
        for (int i = 0; i < g_ncloud; i++) g_cloud[i].listed = false;
        for (int k = 0; k < s->nr; k++) {
            Cloud *c = cloud_add(s->rname[k]);
            if (!c) break;
            c->listed = true;
            snprintf(c->type, sizeof c->type, "%s", s->rtype[k]);
        }
        int o = 0; // forget remotes gone from the config (their settings too)
        for (int i = 0; i < g_ncloud; i++)
            if (g_cloud[i].listed) g_cloud[o++] = g_cloud[i];
            else if (g_cloud[i].login) A.settings_dirty = true;
        g_ncloud = o;
        rows = true;
    }
    u64 now = now_ns();
    for (int k = 0; k < s->n; k++) {
        Cloud *c = cloud_find(s->name[k]);
        if (!c) continue;
        int st = s->state[k];
        if (st == CS_OFF && cloud_busy(c) && now < c->asked + 3000000000ull) st = c->state; // a connect on its way to systemd
        c->unit = s->unit_exists[k];
        if (strcmp(c->mount, s->mount[k]) || c->ours != s->ours[k]) {
            snprintf(c->mount, sizeof c->mount, "%s", s->mount[k]);
            c->ours = s->ours[k];
            rows = true;
        }
        if ((st == CS_UP) != (c->state == CS_UP)) rows = true;
        c->state = st;
        if (st == CS_RETRY || st == CS_FAILED) snprintf(c->err, sizeof c->err, "%s", s->err[k]);
        else if (st == CS_UP || st == CS_OFF) c->err[0] = 0;
        if ((st == CS_RETRY || st == CS_FAILED) && c->err[0] && !c->notified && now < c->notify_until) {
            c->notified = true; // the connect the user asked for failed: say why, once
            c->open_after = 0;
            if (strstr(c->err, "reconnect the account")) {
                static char rn[96];
                snprintf(rn, sizeof rn, "%s", c->name);
                static const char *btn[] = {"Reconnect account", "Close"};
                char msg[600];
                snprintf(msg, sizeof msg, "%s. Reconnecting opens rclone in a terminal tab; sign in in the browser it opens, "
                         "and the folder connects by itself.", c->err);
                dialog_open(c->name, msg, btn, 2, 0, 1, NULL, cloud_reconnect_cb, rn);
            } else toast("%s: %s", c->name, c->err);
        }
    }
    if (rows) init_places();
    for (int k = 0; k < g_ncloud; k++) { // connected as asked: open it
        Cloud *c = &g_cloud[k];
        if (c->state != CS_UP || !c->mount[0] || !c->open_after) continue;
        int how = c->open_after;
        c->open_after = 0;
        if (how == 2) tab_new(c->mount, A.cur + 1, true);
        else open_folder(c->mount);
    }
    free(s);
    A.redraw = true;
}

static bool cloud_pending(void) {
    for (int i = 0; i < g_ncloud; i++)
        if (cloud_busy(&g_cloud[i]) || g_cloud[i].open_after) return true;
    return false;
}

static void cloud_tick(void) {
    // (the picker too: a file can be opened from / saved to a cloud folder; the mount outlives the picker, so the app
    // that asked can still read or write the path it got)
    if (A.screenshot || atomic_load(&g_cloud_job) > 0) return;
    if (A.selftest && !getenv("FE_RCLONE")) return; // tests only ever run a stand-in rclone
    u64 now = now_ns();
    u64 every = g_rclone == 0 ? 30000000000ull : now < g_cloud_fast_until || cloud_pending() ? 700000000ull : A.win_hidden ? 10000000000ull : 2500000000ull;
    if (g_cloud_polled && now - g_cloud_polled < every) return;
    g_cloud_polled = now;
    CloudStatus *s = xcalloc(1, sizeof *s);
    s->relist = g_cloud_relist;
    s->conf_mtime = g_cloud_conf_mtime;
    for (int i = 0; i < g_ncloud && s->n < MAX_CLOUDS; i++) {
        if (!g_cloud[i].listed) continue;
        snprintf(s->name[s->n], sizeof s->name[0], "%s", g_cloud[i].name);
        cloud_unit_name(g_cloud[i].name, s->unit[s->n], sizeof s->unit[0]);
        s->n++;
    }
    atomic_fetch_add(&g_cloud_job, 1);
    pool_submit(cloud_status_job, s);
}

// Main loop: how long it may sleep before the next status poll (ms).
static int cloud_wait_ms(void) {
    if (g_rclone == 0) return 30000;
    return now_ns() < g_cloud_fast_until || cloud_pending() ? 300 : 2500;
}
