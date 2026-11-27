// system_icons.c - File icons > System: icons from the desktop's icon theme (freedesktop icon theme spec), without
// GTK or Qt. The theme's folders are indexed, icons looked up and drawn on a worker; until one arrives (or when the
// theme has none) the bundled Material icon shows, and the desktop's is copied over it once it's there.

// --- index of installed themes (workers, under g_sysi.mtx) ---------------------------------------------------------

typedef struct { char *path; int size, scale, min, max, threshold, type; } SysIconDir; // path: full; type: SYSI_DIR_*
typedef struct { char name[64], dir[64]; char inherits[8][64]; int ninherits; SysIconDir *dirs; int ndirs; } SysIconTheme;
enum { SYSI_DIR_FIXED, SYSI_DIR_SCALABLE, SYSI_DIR_THRESHOLD };
static char *icon_read_svg(const char *path); // icons.c
#define SYSI_ROOTS 16

static struct {
    pthread_mutex_t mtx;
    SysIconTheme *themes; // loaded on first use, also ones without index.theme (no folders: nothing found there)
    int nthemes;
    char roots[SYSI_ROOTS][PATH_MAX]; // $XDG_DATA_HOME/icons, ~/.icons, <each of $XDG_DATA_DIRS>/icons
    int nroots;
} g_sysi = {.mtx = PTHREAD_MUTEX_INITIALIZER};

static bool sysi_component(const char *s) { return s[0] && !strchr(s, '/') && strcmp(s, ".") && strcmp(s, ".."); }

static void sysi_root(const char *path) {
    if (path[0] != '/' || g_sysi.nroots >= SYSI_ROOTS) return;
    for (int i = 0; i < g_sysi.nroots; i++) if (!strcmp(g_sysi.roots[i], path)) return;
    snprintf(g_sysi.roots[g_sysi.nroots++], PATH_MAX, "%s", path);
}

static void sysi_roots(void) {
    if (g_sysi.nroots) return;
    char path[PATH_MAX];
    const char *home = getenv("HOME"), *data = getenv("XDG_DATA_HOME"), *dirs = getenv("XDG_DATA_DIRS");
    if (data && data[0] == '/') snprintf(path, sizeof path, "%s/icons", data);
    else snprintf(path, sizeof path, "%s/.local/share/icons", home ? home : "/tmp");
    sysi_root(path);
    snprintf(path, sizeof path, "%s/.icons", home ? home : "/tmp");
    sysi_root(path);
    char *copy = strdup(dirs && dirs[0] ? dirs : "/usr/local/share:/usr/share"), *p = copy, *part;
    while ((part = strsep(&p, ":"))) {
        snprintf(path, sizeof path, "%s/icons", part);
        sysi_root(path);
    }
    free(copy);
}

// Forget every theme and root (the desktop switched themes, or a test changed the folders).
static void sysi_index_free(void) {
    pthread_mutex_lock(&g_sysi.mtx);
    for (int i = 0; i < g_sysi.nthemes; i++) {
        for (int k = 0; k < g_sysi.themes[i].ndirs; k++) free(g_sysi.themes[i].dirs[k].path);
        free(g_sysi.themes[i].dirs);
    }
    free(g_sysi.themes);
    g_sysi.themes = NULL;
    g_sysi.nthemes = g_sysi.nroots = 0;
    pthread_mutex_unlock(&g_sysi.mtx);
}

static char *sysi_trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    return s;
}

// Is dir one of the comma-separated entries of list?
static bool sysi_listed(const char *list, const char *dir) {
    size_t n = strlen(dir);
    for (const char *p = list; p && *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : NULL) {
        while (*p == ' ') p++;
        if (!strncmp(p, dir, n) && (p[n] == ',' || p[n] == ' ' || !p[n])) return true;
    }
    return false;
}

// index.theme of a theme, read whole (lines of any length: hicolor's Directories= is over 11 KB): its inherited
// themes and the listed folders that exist in some root. (under mtx)
static SysIconTheme *sysi_theme(const char *name) {
    for (int i = 0; i < g_sysi.nthemes; i++) if (!strcmp(g_sysi.themes[i].name, name)) return &g_sysi.themes[i];
    if (!sysi_component(name) || strlen(name) >= 64) return NULL;
    sysi_roots();
    g_sysi.themes = xrealloc(g_sysi.themes, (size_t)(g_sysi.nthemes + 1) * sizeof *g_sysi.themes);
    SysIconTheme *t = &g_sysi.themes[g_sysi.nthemes++];
    memset(t, 0, sizeof *t);
    snprintf(t->name, sizeof t->name, "%s", name);
    snprintf(t->dir, sizeof t->dir, "%s", name);
    FILE *f = NULL;
    char path[PATH_MAX + 80];
    for (int r = 0; r < g_sysi.nroots && !f; r++) {
        snprintf(path, sizeof path, "%s/%s/index.theme", g_sysi.roots[r], name);
        f = fopen(path, "re");
    }
    for (int r = 0; !f && r < g_sysi.nroots; r++) { // the folder's spelling may differ from the theme's name (KDE: "Breeze" is in .../icons/breeze)
        DIR *dh = opendir(g_sysi.roots[r]);
        struct dirent *de;
        while (dh && (de = readdir(dh))) {
            if (de->d_name[0] == '.' || strcasecmp(de->d_name, name)) continue;
            snprintf(path, sizeof path, "%s/%s/index.theme", g_sysi.roots[r], de->d_name);
            f = fopen(path, "re");
            if (f) { snprintf(t->dir, sizeof t->dir, "%s", de->d_name); break; }
        }
        if (dh) closedir(dh);
    }
    if (!f) return t;
    char *line = NULL, *list = NULL, *section = NULL;
    size_t cap = 0;
    // two passes: the [Icon Theme] lists first (they may come after the folder sections), then each folder
    for (int pass = 0; pass < 2; pass++) {
        rewind(f);
        free(section);
        section = NULL;
        SysIconDir d = {0};
        for (bool more = true; more;) {
            more = getline(&line, &cap, f) >= 0;
            char *s = more ? sysi_trim(line) : "[";
            if (s[0] == '[') {
                if (pass == 1 && section && d.size > 0)
                    for (int r = 0; r < g_sysi.nroots; r++) { // (the theme may be spread over several roots)
                        char dir[PATH_MAX + 320];
                        struct stat st;
                        snprintf(dir, sizeof dir, "%s/%s/%s", g_sysi.roots[r], t->dir, section);
                        if (stat(dir, &st) || !S_ISDIR(st.st_mode)) continue;
                        t->dirs = xrealloc(t->dirs, (size_t)(t->ndirs + 1) * sizeof *t->dirs);
                        t->dirs[t->ndirs] = d;
                        t->dirs[t->ndirs++].path = strdup(dir);
                    }
                free(section);
                section = NULL;
                char *end = strchr(s, ']');
                if (!end || !more) continue;
                *end = 0;
                section = strdup(s + 1);
                d = (SysIconDir){.scale = 1, .threshold = 2, .type = SYSI_DIR_THRESHOLD};
                if (pass == 1 && (!list || !sysi_listed(list, section) || strstr(section, ".."))) { free(section); section = NULL; }
                continue;
            }
            char *eq = strchr(s, '=');
            if (!section || !eq) continue;
            *eq = 0;
            char *key = sysi_trim(s), *val = sysi_trim(eq + 1);
            if (pass == 0) {
                if (strcmp(section, "Icon Theme")) continue;
                if (!strcmp(key, "Directories") || !strcmp(key, "ScaledDirectories")) {
                    size_t n = list ? strlen(list) : 0;
                    list = xrealloc(list, n + strlen(val) + 2);
                    snprintf(list + n, strlen(val) + 2, "%s%s", n ? "," : "", val);
                } else if (!strcmp(key, "Inherits")) {
                    char *p = val, *part;
                    while ((part = strsep(&p, ",")) && t->ninherits < 8) {
                        part = sysi_trim(part);
                        if (sysi_component(part) && strlen(part) < 64) snprintf(t->inherits[t->ninherits++], 64, "%s", part);
                    }
                }
            } else if (!strcmp(key, "Size")) d.size = CLAMP(atoi(val), 0, 4096);
            else if (!strcmp(key, "Scale")) d.scale = CLAMP(atoi(val), 1, 16);
            else if (!strcmp(key, "MinSize")) d.min = CLAMP(atoi(val), 1, 4096);
            else if (!strcmp(key, "MaxSize")) d.max = CLAMP(atoi(val), 1, 4096);
            else if (!strcmp(key, "Threshold")) d.threshold = CLAMP(atoi(val), 0, 4096);
            else if (!strcmp(key, "Type")) d.type = !strcmp(val, "Scalable") ? SYSI_DIR_SCALABLE : !strcmp(val, "Fixed") ? SYSI_DIR_FIXED : SYSI_DIR_THRESHOLD;
        }
    }
    free(section);
    free(line);
    free(list);
    fclose(f);
    return t;
}

// File of icon name for px pixels in theme t or the themes it inherits (closest size wins within a theme). (under mtx)
static bool sysi_lookup_in(const char *theme, const char *name, int px, int depth, char *out, size_t cap) {
    SysIconTheme *t = depth < 8 ? sysi_theme(theme) : NULL;
    if (!t) return false;
    int best = INT_MAX;
    for (int i = 0; i < t->ndirs && best; i++) {
        SysIconDir *d = &t->dirs[i];
        int lo = d->size, hi = d->size;
        if (d->type == SYSI_DIR_SCALABLE) { lo = d->min ? d->min : d->size; hi = d->max ? d->max : d->size; }
        else if (d->type == SYSI_DIR_THRESHOLD) { lo -= d->threshold; hi += d->threshold; }
        lo *= d->scale;
        hi *= d->scale;
        int dist = px < lo ? lo - px : px > hi ? px - hi : 0;
        if (dist >= best) continue;
        static const char *exts[] = {"png", "svg"};
        for (int e = 0; e < 2; e++) {
            char path[PATH_MAX];
            snprintf(path, sizeof path, "%s/%s.%s", d->path, name, exts[e]);
            if (!access(path, R_OK)) { snprintf(out, cap, "%s", path); best = dist; break; }
        }
    }
    if (best != INT_MAX) return true;
    // (t may move when a theme is added: copy the names first)
    char inherits[8][64];
    int n = t->ninherits;
    memcpy(inherits, t->inherits, sizeof inherits);
    for (int i = 0; i < n; i++)
        if (strcmp(inherits[i], theme) && sysi_lookup_in(inherits[i], name, px, depth + 1, out, cap)) return true;
    return false;
}

// Theme, then what it inherits, then hicolor, then /usr/share/pixmaps (the spec's order). (worker)
static bool sysi_lookup(const char *theme, const char *name, int px, char *out, size_t cap) {
    if (!sysi_component(name)) return false;
    pthread_mutex_lock(&g_sysi.mtx);
    bool ok = sysi_lookup_in(theme, name, px, 0, out, cap) || (strcmp(theme, "hicolor") && sysi_lookup_in("hicolor", name, px, 0, out, cap));
    pthread_mutex_unlock(&g_sysi.mtx);
    for (int e = 0; !ok && e < 2; e++) {
        snprintf(out, cap, "/usr/share/pixmaps/%s.%s", name, e ? "svg" : "png");
        ok = !access(out, R_OK);
    }
    return ok;
}

// --- drawing an icon file (workers) --------------------------------------------------------------------------------

// RGBA of an SVG / PNG icon file fitted (centred) into px x px; NULL if unreadable, empty or absurd. Symbolic SVGs
// (currentColor) get ink. Caller frees.
static u8 *sysi_raster(const char *path, int px, u32 ink) {
    const char *ext = strrchr(path, '.');
    if (ext && !strcasecmp(ext, ".svg")) {
        char *src = icon_read_svg(path);
        char *root = src ? strstr(src, "<svg") : NULL;
        if (!root) { free(src); return NULL; }
        char *svg = NULL; // the root's color (what currentColor means) unless the file sets its own
        if (asprintf(&svg, "%.*s<svg color=\"#%06X\"%s", (int)(root - src), src, (unsigned)(ink & 0xFFFFFF), root + 4) < 0) svg = NULL;
        free(src);
        NSVGimage *img = svg ? nsvgParse(svg, "px", 96.f) : NULL;
        free(svg);
        if (!img) return NULL;
        // width="1e999" parses as infinity: scale 0, and NaN offsets for the rasterizer
        if (!isfinite(img->width) || !isfinite(img->height) || img->width <= 0 || img->height <= 0 ||
            img->width > 100000 || img->height > 100000) { nsvgDelete(img); return NULL; }
        NSVGrasterizer *rast = nsvgCreateRasterizer();
        u8 *out = rast ? xcalloc((size_t)px * px, 4) : NULL;
        if (rast) {
            f32 sc = MIN(px / img->width, px / img->height);
            nsvgRasterize(rast, img, (px - img->width * sc) * 0.5f, (px - img->height * sc) * 0.5f, sc, out, px, px, px * 4);
            nsvgDeleteRasterizer(rast);
        }
        nsvgDelete(img);
        return out;
    }
    int iw, ih, n;
    if (!stbi_info(path, &iw, &ih, &n) || iw <= 0 || ih <= 0 || iw > 4096 || ih > 4096) return NULL;
    u8 *src = stbi_load(path, &iw, &ih, &n, 4);
    if (!src) return NULL;
    f32 sc = MIN((f32)px / iw, (f32)px / ih);
    int dw = CLAMP((int)roundf(iw * sc), 1, px), dh = CLAMP((int)roundf(ih * sc), 1, px);
    u8 *out = xcalloc((size_t)px * px, 4);
    for (int y = 0; y < dh; y++)
        for (int x = 0; x < dw; x++) { // bilinear, on premultiplied colours (no dark fringes from transparent pixels)
            f32 fx = (x + 0.5f) * iw / dw - 0.5f, fy = (y + 0.5f) * ih / dh - 0.5f;
            int x0 = CLAMP((int)floorf(fx), 0, iw - 1), y0 = CLAMP((int)floorf(fy), 0, ih - 1);
            int x1 = MIN(x0 + 1, iw - 1), y1 = MIN(y0 + 1, ih - 1);
            f32 tx = CLAMP(fx - x0, 0.f, 1.f), ty = CLAMP(fy - y0, 0.f, 1.f);
            const u8 *s[4] = {src + ((size_t)y0 * iw + x0) * 4, src + ((size_t)y0 * iw + x1) * 4,
                              src + ((size_t)y1 * iw + x0) * 4, src + ((size_t)y1 * iw + x1) * 4};
            f32 w[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty}, a = 0, rgb[3] = {0};
            for (int i = 0; i < 4; i++) {
                f32 cov = s[i][3] * w[i];
                a += cov;
                for (int c = 0; c < 3; c++) rgb[c] += s[i][c] * cov;
            }
            u8 *d = out + ((size_t)(y + (px - dh) / 2) * px + x + (px - dw) / 2) * 4;
            d[3] = (u8)lroundf(a);
            for (int c = 0; c < 3; c++) d[c] = a > 0 ? (u8)lroundf(rgb[c] / a) : 0;
        }
    stbi_image_free(src);
    return out;
}

// --- which desktop icon for which bundled one ----------------------------------------------------------------------

// Freedesktop names for a bundled icon id, most specific first (the generic folder / document last).
static int sysi_names(int id, const char **out) {
    // the badges: the desktop theme's own emblems (the Git one is never taken from a theme: the app's logo is its own)
    if (id >= ICON_BADGE_BASE && id < ICON_BADGE_BASE + BADGE_COUNT) {
        static const char *const badge_names[BADGE_COUNT][4] = {
            {"emblem-symbolic-link", "symbolic-link", NULL, NULL},
            {NULL, NULL, NULL, NULL},
            {"emblem-shared", "folder-publicshare", "folder-remote", "folder-network"},
            {"emblem-borg", NULL, NULL, NULL},
        };
        int b = id - ICON_BADGE_BASE, n = 0;
        for (int i = 0; i < 4 && badge_names[b][i]; i++) out[n++] = badge_names[b][i];
        return n;
    }
    if (id >= ICON_EXTRA_BASE && id < ICON_EXTRA_BASE + EXTRA_COUNT) {
        static const char *const extra_names[EXTRA_COUNT][6] = {
            {"folder-remote", "folder-publicshare", "network-server", NULL, NULL, NULL},
            {"folder-publicshare", "folder-remote", "network-server", NULL, NULL, NULL},
            {"folder-network", "network-workgroup", "folder-remote", NULL, NULL, NULL},
            {"drive-harddisk-system", "drive-harddisk-root", "drive-harddisk", "drive-harddisk-solidstate", "drive-harddisk-symbolic", NULL},
            {"drive-removable-media-usb", "drive-removable-media", "drive-harddisk-usb", "drive-harddisk", "drive-removable-media-symbolic", NULL},
            {"drive-optical", "drive-cdrom", "media-optical", "drive-removable-media", "media-optical-symbolic", NULL},
            {"folder-cloud", "folder-gdrive", "folder-remote", "folder-network", NULL, NULL},
            {"network-server", "network-workgroup", "folder-remote", "network-server-symbolic", NULL, NULL},
        };
        int e = id - ICON_EXTRA_BASE, n = 0;
        for (int i = 0; i < 6 && extra_names[e][i]; i++) out[n++] = extra_names[e][i];
        return n;
    }
    static const IconMap map[] = {
        {"folder-shared", "folder-remote"}, {"folder-home", "user-home"}, {"folder-docs", "folder-documents"},
        {"folder-images", "folder-pictures"}, {"folder-audio", "folder-music"}, {"folder-video", "folder-videos"},
        {"folder-download", "folder-download"}, {"folder-desktop", "user-desktop"}, {"folder-trash", "user-trash"},
        {"trash", "user-trash"}, {"trash-full", "user-trash-full"}, {"image", "image-x-generic"},
        {"svg", "image-svg+xml"}, {"audio", "audio-x-generic"}, {"video", "video-x-generic"},
        {"zip", "application-x-compressed-tar"}, {"pdf", "application-pdf"}, {"word", "x-office-document"},
        {"table", "x-office-spreadsheet"}, {"powerpoint", "x-office-presentation"}, {"python", "text-x-python"},
        {"javascript", "application-javascript"}, {"typescript", "text-x-typescript"}, {"c", "text-x-csrc"},
        {"h", "text-x-chdr"}, {"cpp", "text-x-c++src"}, {"hpp", "text-x-c++hdr"}, {"html", "text-html"},
        {"css", "text-css"}, {"json", "application-json"}, {"xml", "application-xml"}, {"console", "text-x-script"},
        {"exe", "application-x-executable"}, {"font", "font-x-generic"}, {"document", "text-x-generic"},
        {"markdown", "text-markdown"}, {"database", "application-x-sqlite3"},
    };
    const char *name = g_icon_names[id];
    int n = 0;
    for (size_t i = 0; i < ARRAY_COUNT(map); i++) if (!strcmp(name, map[i].key)) out[n++] = map[i].icon;
    bool bin = !strncmp(name, "trash", 5) || !strcmp(name, "folder-trash"); // (no theme bin: keep the bundled one)
    if (!strcmp(name, "trash-full")) out[n++] = "user-trash"; // a theme without a full bin
    else if (!bin) out[n++] = !strncmp(name, "folder", 6) ? "folder" : "text-x-generic";
    return n;
}

// --- results for the UI (main thread) -------------------------------------------------------------------------------

enum { SYSI_PENDING, SYSI_READY, SYSI_NONE };
typedef struct { int id, px, state; u8 *rgba; } SysIcon;
static SysIcon *g_sysicons;
static int g_nsysicons;
static size_t g_sysi_bytes;
static u32 g_sysi_gen; // bumped when the desktop's theme changes: older answers are dropped

typedef struct { u32 gen; int id, px; u32 ink; char theme[64]; u8 *rgba; } SysIconReq;

static void icons_replace(int id, int px, const u8 *rgba); // icons.c: put the arrived pixels over the bundled copy

static void sysi_done(void *arg) { // (main thread)
    SysIconReq *r = arg;
    for (int i = 0; r->gen == g_sysi_gen && i < g_nsysicons; i++) {
        SysIcon *s = &g_sysicons[i];
        if (s->id != r->id || s->px != r->px) continue;
        s->state = r->rgba ? SYSI_READY : SYSI_NONE;
        s->rgba = r->rgba;
        r->rgba = NULL;
        if (s->rgba) {
            g_sysi_bytes += (size_t)s->px * s->px * 4;
            icons_replace(s->id, s->px, s->rgba);
        }
        break;
    }
    free(r->rgba);
    free(r);
}

static void sysi_job(void *arg) { // (worker)
    SysIconReq *r = arg;
    const char *names[6];
    int n = sysi_names(r->id, names);
    char path[PATH_MAX];
    for (int i = 0; i < n && !r->rgba; i++)
        if (sysi_lookup(r->theme, names[i], r->px, path, sizeof path)) r->rgba = sysi_raster(path, r->px, r->ink);
    main_post(sysi_done, r);
}

// The desktop's pixels for bundled icon id at px, NULL while they're on their way or if it has none (draw the
// bundled one). The first ask starts the lookup.
static const u8 *sysi_pixels(int id, int px) {
    if (id < 0 || (id >= ICON_COUNT && (id < ICON_BADGE_BASE || id >= ICON_BADGE_BASE + BADGE_COUNT) && (id < ICON_EXTRA_BASE || id >= ICON_EXTRA_BASE + EXTRA_COUNT)) || !g_desktop.icons[0]) return NULL;
    for (int i = 0; i < g_nsysicons; i++)
        if (g_sysicons[i].id == id && g_sysicons[i].px == px) return g_sysicons[i].rgba;
    if (g_sysi_bytes > (32u << 20)) { // keep the memory bounded: drop what's drawn (asked again after an atlas reset)
        int k = 0;
        for (int i = 0; i < g_nsysicons; i++)
            if (g_sysicons[i].state == SYSI_READY) free(g_sysicons[i].rgba);
            else g_sysicons[k++] = g_sysicons[i];
        g_nsysicons = k;
        g_sysi_bytes = 0;
    }
    g_sysicons = xrealloc(g_sysicons, (size_t)(g_nsysicons + 1) * sizeof *g_sysicons);
    g_sysicons[g_nsysicons++] = (SysIcon){id, px, SYSI_PENDING, NULL};
    SysIconReq *r = xcalloc(1, sizeof *r);
    *r = (SysIconReq){.gen = g_sysi_gen, .id = id, .px = px, .ink = COL_TEXT2};
    snprintf(r->theme, sizeof r->theme, "%s", g_desktop.icons);
    pool_submit(sysi_job, r);
    return NULL;
}

// Forget every answer (another desktop theme, or File icons switched away); the index is read again on next use.
static void sysi_reset(void) {
    for (int i = 0; i < g_nsysicons; i++) free(g_sysicons[i].rgba);
    free(g_sysicons);
    g_sysicons = NULL;
    g_nsysicons = 0;
    g_sysi_bytes = 0;
    g_sysi_gen++;
    sysi_index_free();
}
