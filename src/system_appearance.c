// system_appearance.c - desktop accent through the distribution-neutral Settings
// portal. Uses the existing optional libdbus loader; no toolkit/build dependency.
// All bus work stays off the UI thread. Palette changes run on the main queue.

static struct {
    void *(*new_call)(const char *, const char *, const char *, const char *);
    void *(*call_block)(void *, void *, int, IgDBusError *);
    unsigned (*threads_init)(void);
    void (*set_exit)(void *, unsigned);
    void (*add_match)(void *, const char *, IgDBusError *);
    unsigned (*is_signal)(void *, const char *, const char *);
    pthread_t thread;
    atomic_bool stop, running;
    bool started;
} g_system_appearance;

#define AP_DEST "org.freedesktop.portal.Desktop"
#define AP_PATH "/org/freedesktop/portal/desktop"
#define AP_IFACE "org.freedesktop.portal.Settings"

// The portal specifies finite sRGB channels in [0,1]; invalid means unset.
static u32 system_accent_rgb(const double rgb[3]) {
    for (int i = 0; i < 3; i++)
        if (!isfinite(rgb[i]) || rgb[i] < 0 || rgb[i] > 1) return 0;
    return 0x01000000u | (u32)lround(rgb[0] * 255) << 16 |
           (u32)lround(rgb[1] * 255) << 8 | (u32)lround(rgb[2] * 255);
}

static u32 system_accent_value(IgDBusIter *value) {
    IgDBusIter it = *value;
    // ReadOne returns one variant; older Read implementations wrap twice.
    for (int depth = 0; depth < 4 && g_fm1.iter_get_arg_type(&it) == 'v'; depth++) {
        IgDBusIter child;
        g_fm1.iter_recurse(&it, &child);
        it = child;
    }
    if (g_fm1.iter_get_arg_type(&it) != 'r') return 0; // DBUS_TYPE_STRUCT
    IgDBusIter channels;
    g_fm1.iter_recurse(&it, &channels);
    double rgb[3];
    for (int i = 0; i < 3; i++) {
        if (g_fm1.iter_get_arg_type(&channels) != 'd') return 0;
        g_fm1.iter_get_basic(&channels, &rgb[i]);
        if (i < 2 && !g_fm1.iter_next(&channels)) return 0;
    }
    if (g_fm1.iter_next(&channels)) return 0;
    return system_accent_rgb(rgb);
}

typedef struct {
    u32 accent;
    int scheme;
    bool palette_valid;
    Theme palette;
    char icons[64];
} SystemAppearance;

static bool system_appearance_equal(const SystemAppearance *a, const SystemAppearance *b) {
    return a->accent == b->accent && a->scheme == b->scheme && a->palette_valid == b->palette_valid &&
           (!a->palette_valid || !memcmp(&a->palette, &b->palette, sizeof a->palette)) && !strcmp(a->icons, b->icons);
}

static void system_appearance_apply(void *arg) {
    SystemAppearance *state = arg;
    g_system_accent = state->accent;
    g_system_color_scheme = state->scheme;
    g_system_palette_valid = state->palette_valid;
    g_system_palette = state->palette;
    if (strcmp(g_system_icon_current, state->icons)) {
        snprintf(g_system_icon_current, sizeof g_system_icon_current, "%s", state->icons);
        icons_flush(); // Trash and window controls follow the desktop even with a bundled file-icon set
    }
    free(state);
    if (!g_scroll_custom || g_accent_system || g_match_system_background || !strcmp(g_themes[g_theme_cur].name, "System"))
        theme_apply();
    A.redraw = true;
}
static void system_appearance_post(const SystemAppearance *state) {
    SystemAppearance *copy = xmalloc(sizeof *copy);
    *copy = *state;
    main_post(system_appearance_apply, copy);
}

static void *system_setting_reply(void *conn, const char *ns, const char *key) {
    if (!conn || atomic_load(&g_system_appearance.stop)) return NULL;
    static bool legacy;
    const char *methods[] = {"ReadOne", "Read"};
    for (int i = legacy ? 1 : 0; i < 2 && !atomic_load(&g_system_appearance.stop); i++) {
        void *msg = g_system_appearance.new_call(AP_DEST, AP_PATH, AP_IFACE, methods[i]);
        if (!msg) return NULL;
        if (!g_fm1.append_args(msg, 's', &ns, 's', &key, 0)) {
            g_fm1.message_unref(msg);
            return NULL;
        }
        IgDBusError error;
        g_fm1.error_init(&error);
        void *reply = g_system_appearance.call_block(conn, msg, 1000, &error);
        g_fm1.message_unref(msg);
        g_fm1.error_free(&error);
        if (reply) return reply;
        if (i == 0) legacy = true; // Read works with both old and new portals
    }
    return NULL;
}
static bool system_setting_iter(void *reply, IgDBusIter *it) {
    if (!reply || !g_fm1.iter_init(reply, it)) return false;
    for (int depth = 0; depth < 4 && g_fm1.iter_get_arg_type(it) == 'v'; depth++) {
        IgDBusIter child;
        g_fm1.iter_recurse(it, &child);
        *it = child;
    }
    return true;
}
static bool system_setting_string(void *conn, const char *ns, const char *key, char *out, size_t cap) {
    void *reply = system_setting_reply(conn, ns, key);
    IgDBusIter it;
    bool ok = system_setting_iter(reply, &it) && g_fm1.iter_get_arg_type(&it) == 's';
    if (ok) {
        const char *value;
        g_fm1.iter_get_basic(&it, &value);
        snprintf(out, cap, "%s", value);
    }
    if (reply) g_fm1.message_unref(reply);
    return ok;
}
static bool system_setting_color(void *conn, const char *group, const char *key, u32 *color) {
    char value[128], ns[128];
    snprintf(ns, sizeof ns, "org.kde.kdeglobals.%s", group);
    return system_setting_string(conn, ns, key, value, sizeof value) && desktop_rgb(value, color);
}
static SystemAppearance system_appearance_read(void *conn) {
    SystemAppearance state = {0};
    state.palette_valid = desktop_palette_read(&state.palette);
    system_icon_detect(state.icons, sizeof state.icons);
    void *reply = system_setting_reply(conn, "org.freedesktop.appearance", "accent-color");
    IgDBusIter it;
    if (reply) {
        if (g_fm1.iter_init(reply, &it)) state.accent = system_accent_value(&it);
        g_fm1.message_unref(reply);
    }
    reply = system_setting_reply(conn, "org.freedesktop.appearance", "color-scheme");
    if (system_setting_iter(reply, &it) && g_fm1.iter_get_arg_type(&it) == 'u') {
        unsigned scheme;
        g_fm1.iter_get_basic(&it, &scheme);
        if (scheme <= 2) state.scheme = (int)scheme;
    }
    if (reply) g_fm1.message_unref(reply);
    char icon[64] = "";
    bool kde = system_setting_string(conn, "org.kde.kdeglobals.Icons", "Theme", icon, sizeof icon);
    if (!kde) system_setting_string(conn, "org.gnome.desktop.interface", "icon-theme", icon, sizeof icon);
    else {
        u32 bg, window, text, accent;
        if (system_setting_color(conn, "Colors:View", "BackgroundNormal", &bg)) {
            Theme base = rgb_luma(bg) > 0.5f ? g_theme_light : g_theme_dark;
            window = bg; text = base.text; accent = base.accent;
            system_setting_color(conn, "Colors:Window", "BackgroundNormal", &window);
            system_setting_color(conn, "Colors:View", "ForegroundNormal", &text);
            system_setting_color(conn, "Colors:Selection", "BackgroundNormal", &accent);
            state.palette = desktop_palette_build(bg, window, text, accent);
            state.palette_valid = true;
        }
    }
    if (system_icon_component(icon)) snprintf(state.icons, sizeof state.icons, "%s", icon);
    if (!state.accent && state.palette_valid) state.accent = 0x01000000u | state.palette.accent;
    return state;
}
static u64 system_style_stamp(void) {
    const char *files[] = {"kdeglobals", "gtk-3.0/settings.ini", "gtk-4.0/settings.ini"};
    u64 stamp = 0;
    for (size_t i = 0; i < ARRAY_COUNT(files); i++) {
        char path[PATH_MAX];
        struct stat st;
        desktop_config_path(path, sizeof path, files[i]);
        if (!stat(path, &st)) {
            // Ordered mixing avoids cancellation when GTK 3/4 files change together.
            stamp = (stamp ^ (u64)st.st_mtim.tv_sec) * 1099511628211ull;
            stamp = (stamp ^ (u64)st.st_mtim.tv_nsec) * 1099511628211ull;
            stamp = (stamp ^ (u64)st.st_size) * 1099511628211ull;
        }
    }
    return stamp;
}
static void *system_accent_worker(void *unused) {
    (void)unused;
    IgDBusError error;
    g_fm1.error_init(&error);
    void *conn = g_fm1.bus_get_private(0 /* DBUS_BUS_SESSION */, &error);
    g_fm1.error_free(&error);
    if (conn) {
        g_system_appearance.set_exit(conn, 0);
        g_fm1.error_init(&error);
        g_system_appearance.add_match(conn,
            "type='signal',sender='org.freedesktop.portal.Desktop',"
            "interface='org.freedesktop.portal.Settings',member='SettingChanged',"
            "path='/org/freedesktop/portal/desktop'", &error);
        g_fm1.error_free(&error);
        g_fm1.error_init(&error);
        g_system_appearance.add_match(conn,
            "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
            "member='NameOwnerChanged',arg0='org.freedesktop.portal.Desktop'", &error);
        g_fm1.error_free(&error);
    }
    SystemAppearance state = system_appearance_read(conn);
    system_appearance_post(&state);
    u64 stamp = system_style_stamp(), next = now_ns() + 2000000000ull;
    while (!atomic_load(&g_system_appearance.stop)) {
        bool refresh = false;
        if (conn && !g_fm1.read_write(conn, 150)) break;
        if (!conn) usleep(150000);
        void *msg;
        while (conn && (msg = g_fm1.pop_message(conn))) {
            if (g_system_appearance.is_signal(msg, AP_IFACE, "SettingChanged")) {
                IgDBusIter it;
                const char *ns;
                if (g_fm1.iter_init(msg, &it) && g_fm1.iter_get_arg_type(&it) == 's') {
                    g_fm1.iter_get_basic(&it, &ns);
                    refresh |= !strcmp(ns, "org.freedesktop.appearance") ||
                               !strncmp(ns, "org.kde.kdeglobals.", 19) || !strcmp(ns, "org.gnome.desktop.interface");
                }
            }
            if (g_system_appearance.is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged")) refresh = true;
            g_fm1.message_unref(msg);
        }
        if (now_ns() >= next) {
            u64 changed = system_style_stamp();
            refresh |= changed != stamp;
            stamp = changed;
            next = now_ns() + 2000000000ull;
        }
        if (refresh) {
            SystemAppearance updated = system_appearance_read(conn);
            if (!system_appearance_equal(&state, &updated)) system_appearance_post(&updated);
            state = updated;
        }
    }
    if (conn) { g_fm1.connection_close(conn); g_fm1.connection_unref(conn); }
    if (!atomic_load(&g_system_appearance.stop)) {
        SystemAppearance fallback = system_appearance_read(NULL);
        if (!system_appearance_equal(&state, &fallback)) system_appearance_post(&fallback);
    }
    atomic_store(&g_system_appearance.running, false);
    return NULL;
}

static void system_accent_start(void) {
    if (A.selftest || A.screenshot) return;
    if (g_system_appearance.started) {
        if (atomic_load(&g_system_appearance.running)) return;
        pthread_join(g_system_appearance.thread, NULL);
        g_system_appearance.started = false;
    }
    Theme local;
    if (desktop_palette_read(&local)) {
        g_system_palette = local;
        g_system_palette_valid = true;
        g_system_accent = 0x01000000u | local.accent;
        theme_apply(); // local colors remain usable without the optional D-Bus library
    }
    if (!ig_dbus_load()) return;
#define AP_SYM(field, symbol) \
    *(void **)&g_system_appearance.field = dlsym(g_fm1.lib, symbol); \
    if (!g_system_appearance.field) return;
    AP_SYM(new_call, "dbus_message_new_method_call")
    AP_SYM(call_block, "dbus_connection_send_with_reply_and_block")
    AP_SYM(threads_init, "dbus_threads_init_default")
    AP_SYM(set_exit, "dbus_connection_set_exit_on_disconnect")
    AP_SYM(add_match, "dbus_bus_add_match")
    AP_SYM(is_signal, "dbus_message_is_signal")
#undef AP_SYM
    if (!g_system_appearance.threads_init()) return;
    atomic_store(&g_system_appearance.stop, false);
    atomic_store(&g_system_appearance.running, true);
    if (pthread_create(&g_system_appearance.thread, NULL, system_accent_worker, NULL)) {
        atomic_store(&g_system_appearance.running, false);
        return;
    }
    g_system_appearance.started = true;
    pthread_setname_np(g_system_appearance.thread, "fe-appearance");
}

static void system_accent_stop(void) {
    if (!g_system_appearance.started) return;
    atomic_store(&g_system_appearance.stop, true);
    pthread_join(g_system_appearance.thread, NULL);
    g_system_appearance.started = false;
    main_drain();
}
