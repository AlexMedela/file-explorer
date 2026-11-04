// integration.c - desktop roles, all opt-in (Options > Integration): the default file manager (inode/directory),
// "Show in folder" from browsers and other apps (org.freedesktop.FileManager1 on the session bus) and the file
// chooser of other apps (xdg-desktop-portal-termfilechooser). Installing never takes any of them; turning one off
// puts back what was there. Included from app.c before ui_tools.c (the Options page uses it).

static Tab *open_target(const char *in, bool split, bool activate);
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
