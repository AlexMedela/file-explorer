// system_icons.c - read installed freedesktop icon themes without GTK/Qt.
// Metadata is cached; SVG/PNG raster results use the normal icon atlas/LRU.
#define SYS_ICON_THEMES 128
#define SYS_ICON_ROOTS 24
#define SYS_ICON_DIRS 512

typedef struct {
    char path[256];
    int size, scale, min, max, threshold, type; // fixed=0, scalable=1, threshold=2
} SystemIconDir;
typedef struct {
    char name[56], label[64];
    char inherits[16][56];
    int ninherits, ndirs;
    bool loaded;
    SystemIconDir *dirs;
} SystemIconTheme;
static SystemIconTheme g_system_icons[SYS_ICON_THEMES];
static int g_nsystem_icons, g_nicon_roots;
static char g_icon_roots[SYS_ICON_ROOTS][PATH_MAX];
static char g_system_icon_current[64];

static bool system_icon_component(const char *name) {
    return name && *name && !strchr(name, '/') && strcmp(name, ".") && strcmp(name, "..");
}
static bool system_icon_relative(const char *path) {
    if (!path || !*path || *path == '/') return false;
    char copy[256];
    if (strlen(path) >= sizeof copy) return false;
    snprintf(copy, sizeof copy, "%s", path);
    char *p = copy, *part;
    while ((part = strsep(&p, "/"))) if (!system_icon_component(part)) return false;
    return true;
}
static void system_icon_root(const char *path) {
    if (!path || path[0] != '/' || g_nicon_roots >= SYS_ICON_ROOTS) return;
    for (int i = 0; i < g_nicon_roots; i++) if (!strcmp(g_icon_roots[i], path)) return;
    snprintf(g_icon_roots[g_nicon_roots++], PATH_MAX, "%s", path);
}
static int system_icon_theme_find(const char *name) {
    for (int i = 0; i < g_nsystem_icons; i++)
        if (str_ieq(g_system_icons[i].name, name)) return i;
    return -1;
}
static int system_icon_cmp(const void *a, const void *b) {
    return natcmp(((const SystemIconTheme *)a)->name, ((const SystemIconTheme *)b)->name);
}
static void system_icon_detect(char *out, size_t cap) {
    char path[PATH_MAX];
    desktop_config_path(path, sizeof path, "kdeglobals");
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    if ((!desktop || !*desktop || strcasestr(desktop, "kde") || strcasestr(desktop, "plasma")) &&
        desktop_ini(path, "Icons", "Theme", out, cap) && system_icon_component(out)) return;
    const char *files[] = {"gtk-4.0/settings.ini", "gtk-3.0/settings.ini"};
    for (size_t i = 0; i < ARRAY_COUNT(files); i++) {
        desktop_config_path(path, sizeof path, files[i]);
        if (desktop_ini(path, "Settings", "gtk-icon-theme-name", out, cap) && system_icon_component(out)) return;
    }
    snprintf(out, cap, "hicolor");
}
static void system_icons_scan(void) {
    for (int i = 0; i < g_nsystem_icons; i++) free(g_system_icons[i].dirs);
    memset(g_system_icons, 0, sizeof g_system_icons);
    g_nsystem_icons = g_nicon_roots = 0;
    char path[PATH_MAX];
    const char *home = getenv("HOME"), *data = getenv("XDG_DATA_HOME");
    if (data && *data == '/') snprintf(path, sizeof path, "%s/icons", data);
    else snprintf(path, sizeof path, "%s/.local/share/icons", home ? home : "/tmp");
    system_icon_root(path);
    snprintf(path, sizeof path, "%s/.icons", home ? home : "/tmp");
    system_icon_root(path);
    const char *dirs = getenv("XDG_DATA_DIRS");
    char *copy = strdup(dirs && *dirs ? dirs : "/usr/local/share:/usr/share"), *p = copy, *part;
    while ((part = strsep(&p, ":"))) {
        if (*part != '/') continue;
        snprintf(path, sizeof path, "%s/icons", part);
        system_icon_root(path);
    }
    free(copy);
    // Distrobox exposes host resources here when they are not installed inside.
    if (!access("/run/.containerenv", F_OK) || !access("/.dockerenv", F_OK))
        system_icon_root("/run/host/usr/share/icons");
    for (int r = 0; r < g_nicon_roots; r++) {
        DIR *d = opendir(g_icon_roots[r]);
        struct dirent *entry;
        while (d && (entry = readdir(d))) {
            const char *name = entry->d_name;
            if (*name == '.' || strlen(name) >= sizeof g_system_icons[0].name || !system_icon_component(name) ||
                system_icon_theme_find(name) >= 0 || g_nsystem_icons >= SYS_ICON_THEMES) continue;
            snprintf(path, sizeof path, "%s/%s/index.theme", g_icon_roots[r], name);
            char listed[8192];
            if (!desktop_ini(path, "Icon Theme", "Directories", listed, sizeof listed) || !*listed) continue;
            SystemIconTheme *theme = &g_system_icons[g_nsystem_icons++];
            snprintf(theme->name, sizeof theme->name, "%s", name);
            // Keep the folder name stable in settings, even when Name is translated.
            snprintf(theme->label, sizeof theme->label, "System: %s", name);
        }
        if (d) closedir(d);
    }
    qsort(g_system_icons, (size_t)g_nsystem_icons, sizeof g_system_icons[0], system_icon_cmp);
    system_icon_detect(g_system_icon_current, sizeof g_system_icon_current);
}

static bool system_icon_listed(const char *list, const char *value) {
    size_t n = strlen(value);
    for (const char *p = list; *p;) {
        while (*p == ',' || *p == ' ' || *p == '\t') p++;
        const char *end = strchr(p, ',');
        if (!end) end = p + strlen(p);
        const char *trim = end;
        while (trim > p && (trim[-1] == ' ' || trim[-1] == '\t')) trim--;
        if ((size_t)(trim - p) == n && !strncmp(p, value, n)) return true;
        p = end;
    }
    return false;
}
static void system_icon_load(int idx) {
    SystemIconTheme *theme = &g_system_icons[idx];
    if (theme->loaded) return;
    theme->loaded = true;
    char path[PATH_MAX], list[16384] = "", more[8192], inherited[1024];
    FILE *f = NULL;
    for (int r = 0; r < g_nicon_roots && !f; r++) {
        snprintf(path, sizeof path, "%s/%s/index.theme", g_icon_roots[r], theme->name);
        f = fopen(path, "r");
    }
    if (!f) return;
    desktop_ini(path, "Icon Theme", "Directories", list, sizeof list);
    if (desktop_ini(path, "Icon Theme", "ScaledDirectories", more, sizeof more)) {
        size_t n = strlen(list);
        snprintf(list + n, sizeof list - n, ",%s", more);
    }
    if (desktop_ini(path, "Icon Theme", "Inherits", inherited, sizeof inherited)) {
        char *p = inherited, *part;
        while ((part = strsep(&p, ",")) && theme->ninherits < 16) {
            part = desktop_trim(part);
            if (system_icon_component(part))
                snprintf(theme->inherits[theme->ninherits++], sizeof theme->inherits[0], "%s", part);
        }
    }
    char line[8192];
    SystemIconDir current = {0};
    bool selected = false;
    while (fgets(line, sizeof line, f)) {
        char *p = desktop_trim(line);
        if (*p == '[') {
            if (selected && current.size > 0 && theme->ndirs < SYS_ICON_DIRS) {
                theme->dirs = xrealloc(theme->dirs, (size_t)(theme->ndirs + 1) * sizeof current);
                theme->dirs[theme->ndirs++] = current;
            }
            char *end = strchr(p, ']');
            selected = false;
            if (!end) continue;
            *end = 0;
            if (!system_icon_relative(p + 1) || !system_icon_listed(list, p + 1)) continue;
            memset(&current, 0, sizeof current);
            snprintf(current.path, sizeof current.path, "%s", p + 1);
            current.scale = 1; current.threshold = 2; current.type = 2;
            selected = true;
            continue;
        }
        char *eq = strchr(p, '=');
        if (!selected || !eq) continue;
        *eq++ = 0;
        char *key = desktop_trim(p), *value = desktop_trim(eq);
        if (!strcmp(key, "Size")) current.size = CLAMP(atoi(value), 1, 4096);
        else if (!strcmp(key, "Scale")) current.scale = CLAMP(atoi(value), 1, 16);
        else if (!strcmp(key, "MinSize")) current.min = CLAMP(atoi(value), 1, 4096);
        else if (!strcmp(key, "MaxSize")) current.max = CLAMP(atoi(value), 1, 4096);
        else if (!strcmp(key, "Threshold")) current.threshold = CLAMP(atoi(value), 0, 4096);
        else if (!strcmp(key, "Type")) current.type = !strcmp(value, "Scalable") ? 1 : !strcmp(value, "Fixed") ? 0 : 2;
    }
    if (selected && current.size > 0 && theme->ndirs < SYS_ICON_DIRS) {
        theme->dirs = xrealloc(theme->dirs, (size_t)(theme->ndirs + 1) * sizeof current);
        theme->dirs[theme->ndirs++] = current;
    }
    fclose(f);
}
static bool system_icon_lookup_inner(int idx, const char *name, int px, bool *visited, char *out, size_t cap) {
    if (idx < 0 || visited[idx]) return false;
    visited[idx] = true;
    system_icon_load(idx);
    SystemIconTheme *theme = &g_system_icons[idx];
    int best = INT_MAX;
    char path[PATH_MAX];
    for (int i = 0; i < theme->ndirs; i++) {
        SystemIconDir *d = &theme->dirs[i];
        int lo = d->size, hi = d->size;
        if (d->type == 1) { lo = d->min ? d->min : d->size; hi = d->max ? d->max : d->size; }
        else if (d->type == 2) { lo -= d->threshold; hi += d->threshold; }
        lo *= d->scale; hi *= d->scale;
        int distance = px < lo ? lo - px : px > hi ? px - hi : 0;
        if (distance >= best) continue;
        for (int r = 0; r < g_nicon_roots; r++) {
            const char *exts[] = {"png", "svg"};
            for (int e = 0; e < 2; e++) {
                snprintf(path, sizeof path, "%s/%s/%s/%s.%s", g_icon_roots[r], theme->name, d->path, name, exts[e]);
                if (!access(path, R_OK)) {
                    snprintf(out, cap, "%s", path);
                    best = distance;
                    break;
                }
            }
            if (best == distance) break;
        }
    }
    if (best != INT_MAX) return true;
    for (int i = 0; i < theme->ninherits; i++)
        if (system_icon_lookup_inner(system_icon_theme_find(theme->inherits[i]), name, px, visited, out, cap)) return true;
    return false;
}
static bool system_icon_lookup(const char *theme, const char *name, int px, char *out, size_t cap) {
    if (!system_icon_component(name)) return false;
    bool visited[SYS_ICON_THEMES] = {0};
    if (system_icon_lookup_inner(system_icon_theme_find(theme), name, px, visited, out, cap)) return true;
    if (system_icon_lookup_inner(system_icon_theme_find("hicolor"), name, px, visited, out, cap)) return true;
    const char *exts[] = {"png", "svg"};
    for (int i = 0; i < 2; i++) {
        snprintf(out, cap, "/usr/share/pixmaps/%s.%s", name, exts[i]);
        if (!access(out, R_OK)) return true;
    }
    return false;
}
