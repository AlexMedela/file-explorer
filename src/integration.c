// integration.c - desktop roles, all opt-in (Options > Integration): the default file manager (inode/directory),
// "Show in folder" from browsers and other apps (org.freedesktop.FileManager1 on the session bus) and the file
// chooser of other apps (xdg-desktop-portal-termfilechooser). Installing never takes any of them; turning one off
// puts back what was there. Also updates (Options > Updates). Included from app.c before ui_tools.c (the Options page uses it).

static Tab *open_target(const char *in, bool split, bool activate);
static void open_path(const char *full);
static void window_show(void);

static void ig_config_dir(char *out, size_t cap) { // $XDG_CONFIG_HOME
    const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
    if (x && x[0] == '/') snprintf(out, cap, "%s", x);
    else snprintf(out, cap, "%s/.config", h ? h : "/tmp");
}

static void ig_data_dir(char *out, size_t cap) { // $XDG_DATA_HOME
    const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    if (x && x[0] == '/') snprintf(out, cap, "%s", x);
    else snprintf(out, cap, "%s/.local/share", h ? h : "/tmp");
}

static bool ig_write_file(const char *path, const char *text, mode_t mode) {
    char dir[PATH_MAX], tmp[PATH_MAX + 16];
    snprintf(dir, sizeof dir, "%s", path);
    char *sl = strrchr(dir, '/');
    if (sl) { *sl = 0; mkdir_p(dir, 0755); }
    snprintf(tmp, sizeof tmp, "%s.fe-tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return false;
    bool ok = fputs(text, f) >= 0;
    if (fclose(f) != 0 || !ok) { unlink(tmp); return false; }
    chmod(tmp, mode);
    return rename(tmp, path) == 0;
}

// Run a helper command, or in self-tests only record it (never launch anything then).
static void ig_run(char *const argv[]) {
    if (A.selftest) { snprintf(A.last_opened, sizeof A.last_opened, "run:%s %s", argv[0], argv[1] ? argv[1] : ""); return; }
    pid_t pid;
    posix_spawnp(&pid, argv[0], NULL, spawn_attr(), argv, environ);
}

// --- default file manager ---------------------------------------------------------------------------------------

#define IG_OUR_ID "dev.fileexplorer.desktop"

static const char *ig_folder_app(void) { // the app folders open with now ("" none)
    DesktopApp *a = app_default_for("inode/directory");
    return a ? a->id : "";
}

static bool ig_default_fm_on(void) { return !strcmp(ig_folder_app(), IG_OUR_ID); }

static void ig_default_fm_set(bool on) {
    if (on == ig_default_fm_on()) return;
    if (on) {
        snprintf(A.ig_prev_fm, sizeof A.ig_prev_fm, "%s", ig_folder_app()); // to put back when turned off
        mimeapps_set_default("inode/directory", IG_OUR_ID);
    } else {
        // the one it replaced, if it's still installed; else whichever other app handles folders (none: unset)
        DesktopApp *prev = A.ig_prev_fm[0] && strcmp(A.ig_prev_fm, IG_OUR_ID) ? app_by_id(A.ig_prev_fm) : NULL;
        if (!prev) {
            DesktopApp *list[16];
            int n = apps_for_mime("inode/directory", list, 16);
            for (int i = 0; i < n && !prev; i++) if (strcmp(list[i]->id, IG_OUR_ID)) prev = list[i];
        }
        mimeapps_set_default("inode/directory", prev ? prev->id : NULL);
        A.ig_prev_fm[0] = 0;
    }
    A.settings_dirty = true;
}

// --- "Show in folder": org.freedesktop.FileManager1 on the session bus --------------------------------------------
// Browsers' "Show in folder", download panels and many apps call ShowItems / ShowFolders on this name. libdbus is
// loaded when the switch is on (no build dependency); a thread answers and hands the paths to the main loop.

typedef struct { const char *name, *message; unsigned dummy; void *pad; void *more[4]; } IgDBusError;
typedef struct { void *p[16]; } IgDBusIter; // (libdbus's DBusMessageIter is smaller; it only writes its own part)

static struct {
    void *lib, *conn;
    void (*error_init)(IgDBusError *);
    void (*error_free)(IgDBusError *);
    void *(*bus_get_private)(int, IgDBusError *);
    int (*request_name)(void *, const char *, unsigned, IgDBusError *);
    int (*release_name)(void *, const char *, IgDBusError *);
    unsigned (*read_write)(void *, int);
    void *(*pop_message)(void *);
    unsigned (*is_method_call)(void *, const char *, const char *);
    unsigned (*iter_init)(void *, IgDBusIter *);
    int (*iter_get_arg_type)(IgDBusIter *);
    void (*iter_recurse)(IgDBusIter *, IgDBusIter *);
    void (*iter_get_basic)(IgDBusIter *, void *);
    unsigned (*iter_next)(IgDBusIter *);
    void *(*new_method_return)(void *);
    unsigned (*append_args)(void *, int, ...);
    unsigned (*send)(void *, void *, unsigned *);
    void (*flush)(void *);
    void (*message_unref)(void *);
    void (*connection_close)(void *);
    void (*connection_unref)(void *);
    atomic_bool stop, running;
    int state; // IG_FM1_*
} g_fm1;

enum { IG_FM1_OFF, IG_FM1_SERVING, IG_FM1_TAKEN, IG_FM1_NO_DBUS };

static bool ig_dbus_load(void) {
    if (g_fm1.lib) return true;
    void *l = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!l) return false;
    #define IG_SYM(field, name) *(void **)&g_fm1.field = dlsym(l, name); if (!g_fm1.field) { dlclose(l); return false; }
    IG_SYM(error_init, "dbus_error_init")
    IG_SYM(error_free, "dbus_error_free")
    IG_SYM(bus_get_private, "dbus_bus_get_private")
    IG_SYM(request_name, "dbus_bus_request_name")
    IG_SYM(release_name, "dbus_bus_release_name")
    IG_SYM(read_write, "dbus_connection_read_write")
    IG_SYM(pop_message, "dbus_connection_pop_message")
    IG_SYM(is_method_call, "dbus_message_is_method_call")
    IG_SYM(iter_init, "dbus_message_iter_init")
    IG_SYM(iter_get_arg_type, "dbus_message_iter_get_arg_type")
    IG_SYM(iter_recurse, "dbus_message_iter_recurse")
    IG_SYM(iter_get_basic, "dbus_message_iter_get_basic")
    IG_SYM(iter_next, "dbus_message_iter_next")
    IG_SYM(new_method_return, "dbus_message_new_method_return")
    IG_SYM(append_args, "dbus_message_append_args")
    IG_SYM(send, "dbus_connection_send")
    IG_SYM(flush, "dbus_connection_flush")
    IG_SYM(message_unref, "dbus_message_unref")
    IG_SYM(connection_close, "dbus_connection_close")
    IG_SYM(connection_unref, "dbus_connection_unref")
    #undef IG_SYM
    g_fm1.lib = l;
    return true;
}

typedef struct { char **paths; int n; bool items; char token[256]; } IgShow;

// On the main loop: folders open as tabs; items open their folder with the item selected (open_target does both).
static void ig_show_apply(void *arg) {
    IgShow *s = arg;
    if (s->token[0]) setenv("XDG_ACTIVATION_TOKEN", s->token, 1); // lets Wayland raise us
    bool first = true;
    for (int i = 0; i < s->n; i++) {
        if (open_target(s->paths[i], false, first)) first = false;
        free(s->paths[i]);
    }
    free(s->paths);
    free(s);
    if (!A.selftest) window_show();
}

// ShowFolders / ShowItems / ShowItemProperties (as uris, s startup_id): file:// URIs (or plain paths) to open.
static IgShow *ig_show_parse(void *msg, bool items) {
    IgDBusIter it, arr;
    if (!g_fm1.iter_init(msg, &it) || g_fm1.iter_get_arg_type(&it) != 'a') return NULL;
    IgShow *s = xcalloc(1, sizeof *s);
    s->items = items;
    g_fm1.iter_recurse(&it, &arr);
    while (g_fm1.iter_get_arg_type(&arr) == 's' && s->n < MAX_TABS) {
        const char *u = NULL;
        g_fm1.iter_get_basic(&arr, &u);
        char p[PATH_MAX];
        if (u && (uri_to_path(u, p, sizeof p) || (u[0] == '/' && snprintf(p, sizeof p, "%s", u) > 0))) {
            s->paths = xrealloc(s->paths, (size_t)(s->n + 1) * sizeof(char *));
            s->paths[s->n++] = strdup(p);
        }
        g_fm1.iter_next(&arr);
    }
    if (g_fm1.iter_next(&it) && g_fm1.iter_get_arg_type(&it) == 's') {
        const char *t = NULL;
        g_fm1.iter_get_basic(&it, &t);
        if (t && !strchr(t, '\n')) snprintf(s->token, sizeof s->token, "%s", t);
    }
    return s;
}

static const char g_fm1_xml[] =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n<node>\n"
    " <interface name=\"org.freedesktop.FileManager1\">\n"
    "  <method name=\"ShowFolders\"><arg type=\"as\" name=\"URIs\" direction=\"in\"/><arg type=\"s\" name=\"StartupId\" direction=\"in\"/></method>\n"
    "  <method name=\"ShowItems\"><arg type=\"as\" name=\"URIs\" direction=\"in\"/><arg type=\"s\" name=\"StartupId\" direction=\"in\"/></method>\n"
    "  <method name=\"ShowItemProperties\"><arg type=\"as\" name=\"URIs\" direction=\"in\"/><arg type=\"s\" name=\"StartupId\" direction=\"in\"/></method>\n"
    " </interface>\n"
    " <interface name=\"org.freedesktop.DBus.Introspectable\"><method name=\"Introspect\"><arg type=\"s\" name=\"xml\" direction=\"out\"/></method></interface>\n"
    "</node>\n";

static void *ig_fm1_thread(void *arg) {
    (void)arg;
    static const char *iface = "org.freedesktop.FileManager1";
    while (!atomic_load(&g_fm1.stop) && g_fm1.read_write(g_fm1.conn, 250)) {
        void *m;
        while ((m = g_fm1.pop_message(g_fm1.conn))) {
            IgShow *s = NULL;
            bool known = true;
            if (g_fm1.is_method_call(m, iface, "ShowFolders")) s = ig_show_parse(m, false);
            else if (g_fm1.is_method_call(m, iface, "ShowItems") || g_fm1.is_method_call(m, iface, "ShowItemProperties"))
                s = ig_show_parse(m, true);
            else if (g_fm1.is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect")) {
                void *r = g_fm1.new_method_return(m);
                const char *x = g_fm1_xml;
                if (r) { g_fm1.append_args(r, 's', &x, 0); g_fm1.send(g_fm1.conn, r, NULL); g_fm1.message_unref(r); }
                known = false;
            } else known = false;
            if (known) {
                void *r = g_fm1.new_method_return(m); // (answer first: callers wait for it)
                if (r) { g_fm1.send(g_fm1.conn, r, NULL); g_fm1.message_unref(r); }
                if (s) main_post(ig_show_apply, s);
            }
            g_fm1.message_unref(m);
        }
        g_fm1.flush(g_fm1.conn);
    }
    atomic_store(&g_fm1.running, false);
    return NULL;
}

// The D-Bus activation file: starts us when an app asks for the name and no file manager is running. Only written
// while the switch is on (a user file: the system's own file managers' entries are left alone).
static void ig_fm1_service_path(char *out, size_t cap) {
    char d[PATH_MAX];
    ig_data_dir(d, sizeof d);
    snprintf(out, cap, "%s/dbus-1/services/org.freedesktop.FileManager1.service", d);
}

static void ig_fm1_stop(void) {
    if (!g_fm1.conn) { g_fm1.state = IG_FM1_OFF; return; }
    atomic_store(&g_fm1.stop, true);
    for (int i = 0; i < 100 && atomic_load(&g_fm1.running); i++) usleep(10000); // (read_write wakes every 250 ms)
    IgDBusError e;
    g_fm1.error_init(&e);
    g_fm1.release_name(g_fm1.conn, "org.freedesktop.FileManager1", &e);
    g_fm1.error_free(&e);
    g_fm1.connection_close(g_fm1.conn);
    g_fm1.connection_unref(g_fm1.conn);
    g_fm1.conn = NULL;
    g_fm1.state = IG_FM1_OFF;
}

// Answer for the name now (while running). Another file manager that holds it keeps it.
static void ig_fm1_start(void) {
    if (g_fm1.conn || A.selftest || picker_active()) return;
    if (!ig_dbus_load()) { g_fm1.state = IG_FM1_NO_DBUS; return; }
    IgDBusError e;
    g_fm1.error_init(&e);
    void *c = g_fm1.bus_get_private(0 /* session */, &e);
    g_fm1.error_free(&e);
    if (!c) { g_fm1.state = IG_FM1_NO_DBUS; return; }
    g_fm1.error_init(&e);
    int r = g_fm1.request_name(c, "org.freedesktop.FileManager1", 4 /* DO_NOT_QUEUE */, &e);
    g_fm1.error_free(&e);
    if (r != 1 && r != 4) { // (1 primary owner, 4 already ours)
        g_fm1.connection_close(c);
        g_fm1.connection_unref(c);
        g_fm1.state = IG_FM1_TAKEN;
        return;
    }
    g_fm1.conn = c;
    atomic_store(&g_fm1.stop, false);
    atomic_store(&g_fm1.running, true);
    pthread_t t;
    if (pthread_create(&t, NULL, ig_fm1_thread, NULL) != 0) { atomic_store(&g_fm1.running, false); ig_fm1_stop(); return; }
    pthread_detach(t);
    pthread_setname_np(t, "fe-dbus");
    g_fm1.state = IG_FM1_SERVING;
}

static void ig_fm1_set(bool on) {
    char svc[PATH_MAX + 64], exe[PATH_MAX], text[PATH_MAX + 256];
    ig_fm1_service_path(svc, sizeof svc);
    A.ig_fm1 = on;
    A.settings_dirty = true;
    if (on) {
        exe_path(exe, sizeof exe);
        snprintf(text, sizeof text, "[D-BUS Service]\nName=org.freedesktop.FileManager1\nExec=\"%s\" --background\n", exe);
        ig_write_file(svc, text, 0644);
        ig_fm1_start();
    } else {
        unlink(svc);
        ig_fm1_stop();
    }
}

// --- file chooser of other apps (xdg-desktop-portal-termfilechooser) ----------------------------------------------
// The portal runs a command for every Open / Save / Choose folder dialog; ours is a small wrapper that turns its
// arguments into `file-explorer --pick ...`. Turning it on writes three user files (files that were there are kept
// as *.fe-backup and put back when it's turned off): the wrapper, termfilechooser's config, and the desktop's
// portals.conf with FileChooser=termfilechooser.

static bool ig_picker_portal_installed(void) {
    char dirs[16][PATH_MAX], p[PATH_MAX + 64];
    int n = xdg_dirs("XDG_DATA_DIRS", XDG_DATA_DEF, dirs, 16);
    for (int i = 0; i < n; i++) {
        snprintf(p, sizeof p, "%s/xdg-desktop-portal/portals/termfilechooser.portal", dirs[i]);
        if (access(p, R_OK) == 0) return true;
    }
    return getenv("FE_TEST_TERMFILECHOOSER") != NULL;
}

static void ig_picker_paths(char *wrapper, char *tfc, char *portals, size_t cap) {
    char c[PATH_MAX], d[PATH_MAX], desk[64] = "";
    ig_config_dir(c, sizeof c);
    ig_data_dir(d, sizeof d);
    snprintf(wrapper, cap, "%s/file-explorer/termfilechooser-wrapper.sh", d);
    snprintf(tfc, cap, "%s/xdg-desktop-portal-termfilechooser/config", c);
    const char *cur = getenv("XDG_CURRENT_DESKTOP"); // its first name, lowercase: "<desktop>-portals.conf"
    size_t k = 0;
    for (; cur && cur[k] && cur[k] != ':' && k < sizeof desk - 1; k++) desk[k] = (char)ascii_lower((u8)cur[k]);
    desk[k] = 0;
    if (desk[0]) snprintf(portals, cap, "%s/xdg-desktop-portal/%s-portals.conf", c, desk);
    else snprintf(portals, cap, "%s/xdg-desktop-portal/portals.conf", c);
}

static bool ig_picker_on(void) {
    char w[PATH_MAX + 64], t[PATH_MAX + 64], p[PATH_MAX + 64];
    ig_picker_paths(w, t, p, PATH_MAX + 64);
    char *pc = file_read_all(p), *tc = file_read_all(t);
    bool on = pc && tc && strstr(pc, "org.freedesktop.impl.portal.FileChooser=termfilechooser") && strstr(tc, w);
    free(pc);
    free(tc);
    return on;
}

// Keep what's at path as path.fe-backup (once), so turning off can put it back.
static void ig_backup(const char *path) {
    char b[PATH_MAX + 16];
    snprintf(b, sizeof b, "%s.fe-backup", path);
    if (access(path, F_OK) == 0 && access(b, F_OK) != 0) rename(path, b);
}

static void ig_restore(const char *path) {
    char b[PATH_MAX + 16];
    snprintf(b, sizeof b, "%s.fe-backup", path);
    if (access(b, F_OK) == 0) rename(b, path);
    else unlink(path);
}

static void ig_portal_restart(void) { // the portal reads its config at start
    char *argv[] = {"systemctl", "--user", "try-restart", "xdg-desktop-portal.service", NULL};
    ig_run(argv);
}

static void ig_picker_set(bool on) {
    char w[PATH_MAX + 64], t[PATH_MAX + 64], p[PATH_MAX + 64], exe[PATH_MAX];
    ig_picker_paths(w, t, p, PATH_MAX + 64);
    if (!on) {
        unlink(w);
        ig_restore(t);
        ig_restore(p);
        ig_portal_restart();
        return;
    }
    if (!ig_picker_portal_installed()) {
        toast("Install xdg-desktop-portal-termfilechooser first (it lets other apps use this file chooser)");
        return;
    }
    exe_path(exe, sizeof exe);
    char text[PATH_MAX * 2 + 1024];
    snprintf(text, sizeof text,
             "#!/bin/sh\n# Written by File Explorer (Options > Integration). xdg-desktop-portal-termfilechooser runs it for\n"
             "# every Open / Save / Choose folder dialog: $1 multiple, $2 directory, $3 save, $4 path, $5 output file.\n"
             "if [ \"$3\" = 1 ]; then mode=save; elif [ \"$2\" = 1 ]; then mode=dir; elif [ \"$1\" = 1 ]; then mode=files; else mode=file; fi\n"
             "exec \"%s\" --pick \"$mode\" --out \"$5\" \"$4\"\n", exe);
    ig_write_file(w, text, 0755);
    // termfilechooser's own config
    ig_backup(t);
    snprintf(text, sizeof text, "# Written by File Explorer (Options > Integration)\n[filechooser]\ncmd=%s\ndefault_dir=$HOME\n"
             "create_help_file=0\nopen_mode=suggested\nsave_mode=last\n", w);
    ig_write_file(t, text, 0644);
    // the desktop's portals.conf: the one there (or the system's for this desktop) with FileChooser pointed at it
    char *base = file_read_all(p);
    if (!base) {
        char dirs[16][PATH_MAX], sp[PATH_MAX + 64];
        int n = xdg_dirs("XDG_DATA_DIRS", XDG_DATA_DEF, dirs, 16);
        const char *name = strrchr(p, '/') + 1;
        for (int i = 0; i < n && !base; i++) {
            snprintf(sp, sizeof sp, "%s/xdg-desktop-portal/%s", dirs[i], name);
            base = file_read_all(sp);
        }
    }
    ig_backup(p);
    char *out = NULL;
    size_t olen = 0;
    FILE *o = open_memstream(&out, &olen);
    bool in_pref = false, done = false;
    for (const char *l = base ? base : ""; *l;) {
        const char *e = strchr(l, '\n');
        size_t len = e ? (size_t)(e - l) + 1 : strlen(l);
        if (l[0] == '[') {
            if (in_pref && !done) { fputs("org.freedesktop.impl.portal.FileChooser=termfilechooser\n", o); done = true; }
            in_pref = !strncmp(l, "[preferred]", 11);
        } else if (in_pref && !strncmp(l, "org.freedesktop.impl.portal.FileChooser=", 40)) {
            if (!done) fputs("org.freedesktop.impl.portal.FileChooser=termfilechooser\n", o);
            done = true;
            l += len;
            continue;
        }
        fwrite(l, 1, len, o);
        if (l[len - 1] != '\n') fputc('\n', o);
        l += len;
    }
    if (!done) fputs(in_pref ? "org.freedesktop.impl.portal.FileChooser=termfilechooser\n"
                             : "[preferred]\norg.freedesktop.impl.portal.FileChooser=termfilechooser\n", o);
    fclose(o);
    ig_write_file(p, out, 0644);
    free(out);
    free(base);
    ig_portal_restart();
}

// --- first run: offer (once) to become the default file manager -----------------------------------------------------

static void ig_ask_cb(int choice, bool check, void *ud) {
    (void)check;
    (void)ud;
    if (choice == 0) { ig_default_fm_set(true); toast("Folders now open in " APP_NAME " (Options > Integration to change)"); }
}

static void ig_first_run(void) {
    if (A.ig_asked || A.selftest || A.screenshot || picker_active() || A.win_hidden) return;
    A.ig_asked = true;
    A.settings_dirty = true;
    if (ig_default_fm_on()) return;
    static const char *b[] = {"Make default", "Not now"};
    dialog_open("Use " APP_NAME " for folders?",
                "Folders opened from other apps and the desktop would open here. You can change this, and make it the "
                "file chooser or the target of \"Show in folder\", in Options > Integration.",
                b, 2, 0, 1, NULL, ig_ask_cb, NULL);
}

// --- updates (Options > Updates: the AppImage updates itself, packages come with the system's updates) -------------
// At most once a day, in the background, asks GitHub for the latest release (curl or wget, whichever is there). A
// newer one shows a card once (bottom right); "Update now" downloads the AppImage next to this one, checks it against
// the release's SHA-256 and swaps it in, so the next start runs it ("Restart now" does that at once). Other installs
// can check by hand and are told how they update. Never in self-tests.

#define IG_RELEASES "https://github.com/AlexMedela/file-explorer/releases"
#if defined(__x86_64__)
#define UPD_ARCH "x86_64"
#elif defined(__aarch64__)
#define UPD_ARCH "aarch64"
#else
#define UPD_ARCH "unknown"
#endif

enum { UK_SOURCE, UK_APPIMAGE, UK_ARCH, UK_DEB, UK_RPM }; // how this copy was installed
enum { UPD_IDLE, UPD_CHECKING, UPD_LATEST, UPD_AVAILABLE, UPD_DOWNLOADING, UPD_READY, UPD_FAILED };

static struct {
    int state;
    bool manual;         // asked for in Options (no card then: the page shows how it goes)
    bool card;           // the card is up (bottom right)
    char tag[64];        // the newest release ("v1.2.3")
    i64 size, got;       // its AppImage in bytes (0 = unknown), downloaded so far
    char part[PATH_MAX]; // the download, next to the AppImage
    atomic_bool cancel;
    char err[200];
    Rect r_card, r_main, r_alt, r_close; // card hit areas, from the last frame
} g_upd;

// "1.2.3" vs "1.10" -> <0 / 0 / >0 (a leading 'v' and anything after a '-' or '~' ignored)
static int ig_vercmp(const char *a, const char *b) {
    if (*a == 'v') a++;
    if (*b == 'v') b++;
    while (*a || *b) {
        if (*a == '-' || *a == '~') a = "";
        if (*b == '-' || *b == '~') b = "";
        if (!*a && !*b) break;
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

// The tag in GitHub's "latest release" answer ("tag_name": "v1.2.3"), or "".
static void ig_tag_from_json(const char *json, char *tag, size_t cap) {
    tag[0] = 0;
    const char *k = strstr(json, "\"tag_name\"");
    if (!k || !(k = strchr(k + 10, '"'))) return;
    const char *e = strchr(k + 1, '"');
    if (e && (size_t)(e - k - 1) < cap) snprintf(tag, cap, "%.*s", (int)(e - k - 1), k + 1);
}

// The AppImage of a release for this machine, as the release workflow names it.
static void upd_asset_name(const char *tag, char *out, size_t cap) {
    snprintf(out, cap, "file-explorer-%s-" UPD_ARCH ".AppImage", tag[0] == 'v' ? tag + 1 : tag);
}

// Size of the asset called name in GitHub's answer (its "size" follows its "name"), or 0.
static i64 upd_asset_size(const char *json, const char *name) {
    char q[200];
    snprintf(q, sizeof q, "\"%s\"", name);
    const char *p = strstr(json, q);
    if (!p || !(p = strstr(p, "\"size\"")) || !(p = strchr(p + 6, ':'))) return 0;
    return strtoll(p + 1, NULL, 10);
}

// The hash at the start of a sha256sum line ("<64 hex>  name") into out (lower case), else "".
static void upd_sha_from_line(const char *s, char out[65]) {
    out[0] = 0;
    for (int i = 0; i < 64; i++) {
        char c = (char)tolower((unsigned char)s[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return;
        out[i] = c;
    }
    out[64] = 0;
    if (s[64] && s[64] != ' ' && s[64] != '\t' && s[64] != '\n') out[0] = 0;
}

static bool upd_sha256_file(const char *path, char out[65]) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    Sha256 s;
    sha256_init(&s);
    u8 *buf = xmalloc(1 << 20);
    ssize_t n;
    while ((n = read(fd, buf, 1 << 20)) > 0) sha256_update(&s, buf, (size_t)n);
    free(buf);
    close(fd);
    if (n < 0) return false;
    sha256_final(&s, out);
    return true;
}

// How this copy got here, so it can say how it updates. Packages are found by their database knowing about us.
static int upd_kind(void) {
    const char *k = getenv("FE_UPDATE_KIND"); // (tests, screenshots: appimage, arch, deb, rpm, source)
    if (k) return !strcmp(k, "appimage") ? UK_APPIMAGE : !strcmp(k, "arch") ? UK_ARCH : !strcmp(k, "deb") ? UK_DEB
                 : !strcmp(k, "rpm") ? UK_RPM : UK_SOURCE;
    static int kind = -1;
    if (kind >= 0) return kind;
    kind = UK_SOURCE;
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = 0;
    if (appimage_path()) kind = UK_APPIMAGE;
    else if (!strncmp(exe, "/usr/bin/", 9)) {
        if (access("/var/lib/dpkg/info/file-explorer.list", F_OK) == 0 || access("/var/lib/dpkg/info/file-explorer:amd64.list", F_OK) == 0 ||
            access("/var/lib/dpkg/info/file-explorer:arm64.list", F_OK) == 0) kind = UK_DEB;
        else if (access("/var/lib/pacman/local", F_OK) == 0) kind = UK_ARCH; // (built with makepkg: no Arch repository)
        else if (access("/var/lib/rpm", F_OK) == 0 || access("/usr/lib/sysimage/rpm", F_OK) == 0) kind = UK_RPM;
    }
    return kind;
}

static bool upd_can_install(void) { return upd_kind() == UK_APPIMAGE; }

// How this kind of install gets a new version (Options > Updates), and the command for it to copy (NULL: none).
static const char *upd_how(void) {
    switch (upd_kind()) {
    case UK_APPIMAGE: return "AppImage: updates itself from GitHub";
    case UK_ARCH: return "makepkg: copy the command, run it in packaging/arch";
    case UK_DEB: return "With your system updates (apt)";
    case UK_RPM: return "With your system updates (dnf / zypper)";
    }
    return "Built from source: copy the command, run it in the checkout";
}

static const char *upd_how_cmd(void) {
    int k = upd_kind();
    return k == UK_ARCH ? "git pull && makepkg -sif" : k == UK_SOURCE ? "git pull && ./build.sh" : NULL;
}

static int upd_fetch(const char *url, char *out, size_t cap) {
    char *curl[] = {"curl", "-fsSL", "--max-time", "20", (char *)url, NULL};
    char *wget[] = {"wget", "-qO-", "--timeout=20", (char *)url, NULL};
    return in_path("curl") ? run_capture(curl, out, cap) : in_path("wget") ? run_capture(wget, out, cap) : -1;
}

typedef struct { char tag[64]; i64 size; bool ok; } UpdFound;

static void upd_download(void);

static void upd_check_done(void *arg) {
    UpdFound *r = arg;
    if (g_upd.state == UPD_CHECKING) {
        if (!r->ok) {
            g_upd.state = g_upd.manual ? UPD_FAILED : UPD_IDLE;
            snprintf(g_upd.err, sizeof g_upd.err, "Couldn't reach GitHub (needs curl or wget, and the network)");
        } else if (!r->tag[0] || ig_vercmp(r->tag, APP_VERSION) <= 0) g_upd.state = UPD_LATEST;
        else {
            snprintf(g_upd.tag, sizeof g_upd.tag, "%s", r->tag);
            g_upd.size = r->size;
            g_upd.state = UPD_AVAILABLE;
            if (!g_upd.manual && strcmp(A.upd_seen, g_upd.tag)) { // once per version
                snprintf(A.upd_seen, sizeof A.upd_seen, "%s", g_upd.tag);
                A.settings_dirty = true;
                if (A.upd_auto && upd_can_install()) upd_download(); // (the card comes when it's ready)
                else g_upd.card = true;
            }
        }
        A.ig_update_checked = (i64)time(NULL);
        A.settings_dirty = true;
    }
    free(r);
    A.redraw = true;
}

static void *upd_check_thread(void *arg) {
    UpdFound *r = arg;
    size_t cap = 256 << 10;
    char *out = xmalloc(cap), name[160];
    if (upd_fetch("https://api.github.com/repos/AlexMedela/file-explorer/releases/latest", out, cap) == 0) {
        ig_tag_from_json(out, r->tag, sizeof r->tag);
        upd_asset_name(r->tag, name, sizeof name);
        r->size = upd_asset_size(out, name);
        r->ok = true;
    }
    free(out);
    main_post(upd_check_done, r);
    return NULL;
}

// Ask GitHub now. manual: from Options (shows the outcome there, not as a card).
static void upd_check_now(bool manual) {
    if (A.selftest || A.screenshot || g_upd.state == UPD_CHECKING || g_upd.state == UPD_DOWNLOADING || g_upd.state == UPD_READY) return;
    g_upd.state = UPD_CHECKING;
    g_upd.manual = manual;
    pthread_t t;
    if (pthread_create(&t, NULL, upd_check_thread, xcalloc(1, sizeof(UpdFound))) == 0) pthread_detach(t);
    else g_upd.state = UPD_IDLE;
}

// At start: once a day, AppImage only (packages leave updating to the system).
static void ig_update_check(void) {
    if (!appimage_path() || !A.ig_check_updates || A.selftest || A.screenshot) return;
    if ((i64)time(NULL) - A.ig_update_checked < 24 * 3600) return;
    upd_check_now(false);
}

typedef struct { char tag[64], part[PATH_MAX], target[PATH_MAX], err[200]; bool ok; } UpdJob;

static void upd_tick(void *arg) { // (while downloading: how far it got)
    (void)arg;
    struct stat st;
    if (g_upd.state == UPD_DOWNLOADING && stat(g_upd.part, &st) == 0) g_upd.got = st.st_size;
    A.redraw = true;
}

static void upd_dl_done(void *arg) {
    UpdJob *j = arg;
    if (j->ok) {
        g_upd.state = UPD_READY;
        if (!g_upd.manual) g_upd.card = true;
    } else if (atomic_load(&g_upd.cancel)) g_upd.state = UPD_AVAILABLE;
    else {
        g_upd.state = UPD_FAILED;
        snprintf(g_upd.err, sizeof g_upd.err, "%s", j->err);
    }
    free(j);
    A.redraw = true;
}

// Worker: the release's checksum, then the AppImage into j->part (curl / wget as a child we can stop), checked and
// renamed over the running one (which keeps running from its open file until the restart).
static void *upd_dl_thread(void *arg) {
    UpdJob *j = arg;
    char name[160], url[512], sums[1024], want[65], got[65];
    upd_asset_name(j->tag, name, sizeof name);
    snprintf(url, sizeof url, IG_RELEASES "/download/%s/%s.sha256", j->tag, name);
    want[0] = 0;
    if (upd_fetch(url, sums, sizeof sums) == 0) upd_sha_from_line(sums, want);
    if (!want[0]) { snprintf(j->err, sizeof j->err, "No %s for this release on GitHub", name); goto out; }
    snprintf(url, sizeof url, IG_RELEASES "/download/%s/%s", j->tag, name);
    char *curl[] = {"curl", "-fsSL", "--max-time", "3600", "-o", j->part, url, NULL};
    char *wget[] = {"wget", "-q", "--timeout=30", "-O", j->part, url, NULL};
    bool c = in_path("curl");
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    for (int fd = 0; fd < 3; fd++) posix_spawn_file_actions_addopen(&fa, fd, "/dev/null", fd ? O_WRONLY : O_RDONLY, 0);
    pid_t pid;
    int rc = posix_spawnp(&pid, c ? "curl" : "wget", &fa, spawn_attr(), c ? curl : wget, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc) { snprintf(j->err, sizeof j->err, "Needs curl or wget to download"); goto out; }
    // (children reap themselves here: the pid is gone once it exits; the checksum tells whether it worked)
    while (kill(pid, 0) == 0) {
        if (atomic_load(&g_upd.cancel)) kill(pid, SIGTERM);
        usleep(250 * 1000);
        main_post(upd_tick, NULL);
    }
    if (atomic_load(&g_upd.cancel)) { unlink(j->part); goto out; }
    if (!upd_sha256_file(j->part, got) || strcmp(got, want)) {
        unlink(j->part);
        snprintf(j->err, sizeof j->err, "The download didn't arrive whole (checksum differs): try again later");
        goto out;
    }
    chmod(j->part, 0755);
    if (rename(j->part, j->target) != 0) {
        snprintf(j->err, sizeof j->err, "Couldn't replace the AppImage: %s", strerror(errno));
        unlink(j->part);
        goto out;
    }
    j->ok = true;
out:
    main_post(upd_dl_done, j);
    return NULL;
}

static void upd_fail(const char *fmt, const char *arg) {
    g_upd.state = UPD_FAILED;
    snprintf(g_upd.err, sizeof g_upd.err, fmt, arg);
}

// Download and install the new AppImage (from the card, or Options).
static void upd_download(void) {
    const char *ai = appimage_path();
    if (A.selftest || A.screenshot || g_upd.state != UPD_AVAILABLE) return;
    if (!ai) { upd_fail("%s", "Not running from an AppImage"); return; }
    // next to the AppImage (a rename can't cross file systems), so its folder must be ours to write
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", ai);
    char *sl = strrchr(dir, '/');
    if (sl) *sl = 0;
    if (access(dir, W_OK) != 0) { upd_fail("Can't write to %s: download the new version from the releases page", dir); return; }
    UpdJob *j = xcalloc(1, sizeof *j);
    snprintf(j->tag, sizeof j->tag, "%s", g_upd.tag);
    snprintf(j->target, sizeof j->target, "%s", ai);
    snprintf(j->part, sizeof j->part, "%s/.file-explorer-update.part", dir);
    snprintf(g_upd.part, sizeof g_upd.part, "%s", j->part);
    g_upd.state = UPD_DOWNLOADING;
    g_upd.got = 0;
    atomic_store(&g_upd.cancel, false);
    pthread_t t;
    if (pthread_create(&t, NULL, upd_dl_thread, j) == 0) pthread_detach(t);
    else { free(j); g_upd.state = UPD_AVAILABLE; }
}

static void upd_cancel(void) {
    if (g_upd.state == UPD_DOWNLOADING) atomic_store(&g_upd.cancel, true);
}

// Save, then become another program (argv[0]) in this process: the new version after an update.
static void relaunch(char **argv) {
    settings_save();
    ipc_shutdown();
    SDL_DestroyRenderer(A.ren);
    SDL_DestroyWindow(A.win);
    SDL_Quit();
    execv(argv[0], argv);
    _exit(127); // (the window is gone: nothing sensible to go back to)
}

static void upd_relaunch(void) {
    const char *ai = appimage_path();
    if (!ai || A.selftest || A.screenshot) return;
    char *argv[MAX_TABS + 2];
    int argc = 0;
    argv[argc++] = (char *)ai;
    if (!A.restore_session) // the current tabs as arguments, the session won't bring them back
        for (int i = 0; i < A.ntabs && argc < MAX_TABS + 1; i++) argv[argc++] = A.tabs[i]->pane[0].path;
    argv[argc] = NULL;
    relaunch(argv);
}

static void upd_restart_cb(int choice, bool check, void *ud) {
    (void)check;
    (void)ud;
    if (choice == 0) upd_relaunch();
}

// "Restart now": not while it would lose something (transfers; terminal tabs only after asking).
static void upd_restart(void) {
    if (A.nops > 0) { toast("Transfers are running: restart once they're done (the new version also starts next time)"); return; }
    int nterm = 0;
    for (int i = 0; i < A.ntabs; i++) nterm += A.tabs[i]->term != NULL;
    if (nterm) {
        static const char *b[] = {"Restart", "Cancel"};
        dialog_open("Restart " APP_LABEL "?", "Terminal tabs close and their programs stop. Folder tabs come back.", b, 2, 0, 1,
                    NULL, upd_restart_cb, NULL);
        return;
    }
    upd_relaunch();
}

static void upd_notes(void) { open_path(g_upd.tag[0] ? IG_RELEASES "/latest" : IG_RELEASES); }

// "3 hours ago" for the Options page.
static void upd_ago(i64 t, char *out, size_t cap) {
    i64 d = (i64)time(NULL) - t;
    if (d < 90) snprintf(out, cap, "just now");
    else if (d < 5400) snprintf(out, cap, "%lld minutes ago", (long long)((d + 30) / 60));
    else if (d < 36 * 3600) snprintf(out, cap, "%lld hours ago", (long long)((d + 1800) / 3600));
    else snprintf(out, cap, "%lld days ago", (long long)((d + 43200) / 86400));
}

// The state in a line (Options > Updates: the version row).
static const char *upd_status(void) {
    static char buf[320];
    char ago[48], sz[32] = "";
    const char *v = g_upd.tag[0] == 'v' ? g_upd.tag + 1 : g_upd.tag;
    if (g_upd.size > 0) fmt_size(sz, sizeof sz, (u64)g_upd.size);
    switch (g_upd.state) {
    case UPD_CHECKING: return "Asking GitHub for the newest version\xE2\x80\xA6";
    case UPD_LATEST: return "You have the newest version";
    case UPD_AVAILABLE:
        if (upd_can_install()) snprintf(buf, sizeof buf, "%s is available%s%s%s", v, sz[0] ? " (" : "", sz, sz[0] ? ")" : "");
        else snprintf(buf, sizeof buf, "%s is out: see How it updates", v);
        return buf;
    case UPD_DOWNLOADING:
        if (g_upd.size > 0) snprintf(buf, sizeof buf, "Downloading %s\xE2\x80\xA6 %d%% of %s", v, (int)(g_upd.got * 100 / g_upd.size), sz);
        else snprintf(buf, sizeof buf, "Downloading %s\xE2\x80\xA6", v);
        return buf;
    case UPD_READY: snprintf(buf, sizeof buf, "%s is installed: restart to use it", v); return buf;
    case UPD_FAILED: return g_upd.err;
    }
    if (!A.ig_update_checked) return upd_can_install() ? upd_how() : "Not checked yet";
    upd_ago(A.ig_update_checked, ago, sizeof ago);
    snprintf(buf, sizeof buf, "Last checked %s", ago);
    return buf;
}

// The version row's button: label, and whether it's the main one / can be pressed.
static const char *upd_button(bool *primary, bool *enabled) {
    *primary = false;
    *enabled = true;
    switch (g_upd.state) {
    case UPD_CHECKING: *enabled = false; return "Checking\xE2\x80\xA6";
    case UPD_AVAILABLE:
        if (!upd_can_install()) return "Check again";
        *primary = true;
        return "Update now";
    case UPD_DOWNLOADING: return "Cancel";
    case UPD_READY: *primary = true; return "Restart now";
    }
    return "Check now";
}

static void upd_button_press(void) {
    switch (g_upd.state) {
    case UPD_AVAILABLE:
        if (upd_can_install()) { g_upd.manual = true; upd_download(); }
        else upd_check_now(true);
        break;
    case UPD_DOWNLOADING: upd_cancel(); break;
    case UPD_READY: upd_restart(); break;
    case UPD_CHECKING: break;
    default: upd_check_now(true);
    }
}

// The card (bottom right, above transfer cards / the queue): a new version, its download, then "Restart now".
static void upd_card_draw(void) {
    g_upd.r_card = g_upd.r_main = g_upd.r_alt = g_upd.r_close = (Rect){0, 0, 0, 0};
    if (!g_upd.card || g_upd.state < UPD_AVAILABLE) return;
    Font *f = &A.font, *fs = &A.font_small;
    f32 w = roundf(S(380)), x = A.W - w - roundf(S(16)), bottom = A.r_status.y - roundf(S(10));
    for (int i = 0; i < A.nops; i++) // above the transfer cards
        if (A.op_card[i].w > 0) bottom = MIN(bottom, A.op_card[i].y - roundf(S(8)));
    if (A.q_r.w > 0) bottom = MIN(bottom, A.q_r.y - roundf(S(8)));
    bool dl = g_upd.state == UPD_DOWNLOADING;
    f32 h = roundf(S(dl ? 74 : 108)), y = bottom - h, pad = roundf(S(14)), cs = roundf(S(20));
    g_upd.r_card = (Rect){x, y, w, h};
    draw_rrect(x + S(2), y + S(4), w, h, S(10), CA(0x000000, 0.35f));
    draw_rrect(x, y, w, h, S(10), C(COL_POPUP));
    draw_rrect_outline(x, y, w, h, S(10), 1, C(COL_CONTROL));
    const char *v = g_upd.tag[0] == 'v' ? g_upd.tag + 1 : g_upd.tag;
    char title[160], det[320], sz[32] = "", got[32];
    if (g_upd.size > 0) fmt_size(sz, sizeof sz, (u64)g_upd.size);
    switch (g_upd.state) {
    case UPD_AVAILABLE:
        snprintf(title, sizeof title, APP_LABEL " %s is available", v);
        snprintf(det, sizeof det, "You have %s%s%s", APP_VERSION, sz[0] ? "  \xC2\xB7  " : "", sz);
        break;
    case UPD_DOWNLOADING:
        snprintf(title, sizeof title, "Updating to %s", v);
        fmt_size(got, sizeof got, (u64)g_upd.got);
        if (g_upd.size > 0) snprintf(det, sizeof det, "%s of %s", got, sz);
        else snprintf(det, sizeof det, "%s", got);
        break;
    case UPD_READY:
        snprintf(title, sizeof title, APP_LABEL " %s is ready", v);
        snprintf(det, sizeof det, "Restart to use it now (your tabs come back), or it starts next time.");
        break;
    default:
        snprintf(title, sizeof title, "Couldn't update to %s", v);
        snprintf(det, sizeof det, "%s", g_upd.err);
    }
    // logo, title, close (cancels a download)
    f32 lh = roundf(S(14)), lw = roundf(lh * 624.f / 456.f), lx = x + pad, ty = y + S(10);
    Glyph *g = icon_glyph(ICON_LOGO_SMALL, (int)lw);
    if (g) draw_glyph(g, lx, roundf(ty + (f->line_h - lh) * 0.5f - (lw - lh) * 0.5f), (Color){1, 1, 1, 1});
    f32 tx = lx + lw + roundf(S(8));
    draw_text_fit(f, tx, ty, x + w - tx - cs - S(16), title, (int)strlen(title), C(COL_TEXT), 0);
    g_upd.r_close = op_icon_button(x + w - cs - S(8), y + S(8), cs, 0, false, true);
    if (dl) {
        f32 bx = x + pad, bw = w - pad * 2, bh = roundf(S(5)), by = y + S(36);
        draw_rrect(bx, by, bw, bh, bh * 0.5f, C(COL_CONTROL));
        if (g_upd.size > 0 && g_upd.got > 0)
            draw_rrect(bx, by, MAX(bh, bw * (f32)MIN(1.0, (f64)g_upd.got / (f64)g_upd.size)), bh, bh * 0.5f, C(COL_ACCENT));
        draw_text_fit(fs, x + pad, y + S(48), w - pad * 2, det, (int)strlen(det), C(COL_TEXT_DIM), 0);
        return;
    }
    int st[2], ln[2], nl = wrap_text(fs, det, x + w - pad - tx, st, ln, 2);
    for (int k = 0; k < nl; k++)
        draw_text_fit(fs, tx, y + S(32) + fs->line_h * (f32)k, x + w - pad - tx, det + st[k], k == nl - 1 ? (int)strlen(det + st[k]) : ln[k],
                      C(g_upd.state == UPD_FAILED ? COL_DANGER_TEXT : COL_TEXT_DIM), 0);
    // buttons, right-aligned: the main one, and beside it the release notes / later
    const char *ml = g_upd.state == UPD_AVAILABLE ? "Update now" : g_upd.state == UPD_READY ? "Restart now" : "Releases page";
    const char *al = g_upd.state == UPD_READY ? "Later" : g_upd.state == UPD_AVAILABLE ? "What's new" : NULL;
    f32 bh = roundf(S(28)), by = y + h - pad - bh + S(2);
    f32 mw = roundf(text_width(f, ml, (int)strlen(ml)) + S(28));
    g_upd.r_main = (Rect){x + w - pad - mw, by, mw, bh};
    sheet_button(g_upd.r_main, ml, g_upd.state != UPD_FAILED, true);
    if (al) {
        f32 aw = roundf(text_width(f, al, (int)strlen(al)) + S(28));
        g_upd.r_alt = (Rect){g_upd.r_main.x - S(8) - aw, by, aw, bh};
        sheet_button(g_upd.r_alt, al, false, true);
    }
}

// A left click on the card; true when it took it.
static bool upd_card_click(f32 x, f32 y) {
    if (!g_upd.card || g_upd.r_card.w <= 0 || !in_rect(g_upd.r_card, x, y)) return false;
    if (in_rect(g_upd.r_close, x, y)) {
        upd_cancel();
        g_upd.card = false;
    } else if (in_rect(g_upd.r_main, x, y)) {
        g_upd.manual = false;
        if (g_upd.state == UPD_AVAILABLE) upd_download();
        else if (g_upd.state == UPD_READY) upd_restart();
        else { upd_notes(); g_upd.card = false; }
    } else if (in_rect(g_upd.r_alt, x, y)) {
        if (g_upd.state == UPD_READY) g_upd.card = false;
        else upd_notes();
    }
    A.redraw = true;
    return true;
}

// Visual checks (--screenshot): FE_SHOT_UPDATE=available|downloading|ready|failed puts up the card for 0.99.0.
static void upd_shot(void) {
    const char *s = getenv("FE_SHOT_UPDATE");
    if (!A.screenshot || !s) return;
    snprintf(g_upd.tag, sizeof g_upd.tag, "v0.99.0");
    g_upd.size = 46 << 20;
    g_upd.card = true;
    g_upd.state = !strcmp(s, "downloading") ? UPD_DOWNLOADING : !strcmp(s, "ready") ? UPD_READY : !strcmp(s, "failed") ? UPD_FAILED
                : !strcmp(s, "latest") ? UPD_LATEST : UPD_AVAILABLE;
    g_upd.got = g_upd.size * 37 / 100;
    snprintf(g_upd.err, sizeof g_upd.err, "Can't write to /opt/apps: download the new version from the releases page");
    A.ig_update_checked = (i64)time(NULL) - 3 * 3600;
}
