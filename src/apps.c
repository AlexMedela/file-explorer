// apps.c - MIME types (shared-mime-info), desktop entries, default apps, launching,
// checksums and background folder statistics (for Properties).

// --- MIME types from globs2 ("weight:type:glob") -----------------------------

typedef struct {
    int weight;
    char *mime;
    char *glob;   // pattern (lowercase)
    char *suffix; // for "*.ext" patterns: ".ext" (fast path)
} MimeGlob;

static MimeGlob *g_globs;
static int g_nglobs;
static pthread_once_t g_mime_once = PTHREAD_ONCE_INIT;

static void mime_load_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        line[strcspn(line, "\r\n")] = 0;
        char *w = strtok(line, ":"), *m = strtok(NULL, ":"), *g = strtok(NULL, ":");
        if (!w || !m || !g) continue;
        g_globs = xrealloc(g_globs, (size_t)(g_nglobs + 1) * sizeof(MimeGlob));
        MimeGlob *e = &g_globs[g_nglobs++];
        e->weight = atoi(w);
        e->mime = strdup(m);
        for (char *q = g; *q; q++) *q = (char)ascii_lower((u8)*q);
        e->glob = strdup(g);
        e->suffix = (g[0] == '*' && g[1] == '.' && !strpbrk(g + 1, "*?[")) ? strdup(g + 1) : NULL;
    }
    fclose(f);
}

static void mime_init(void) {
    char p[PATH_MAX];
    const char *xd = getenv("XDG_DATA_HOME");
    if (xd && xd[0] == '/') snprintf(p, sizeof p, "%s/mime/globs2", xd);
    else snprintf(p, sizeof p, "%s/.local/share/mime/globs2", getenv("HOME") ? getenv("HOME") : "/");
    mime_load_file(p);
    char dirs[32][PATH_MAX];
    int n = xdg_dirs("XDG_DATA_DIRS", XDG_DATA_DEF, dirs, 32); // (NixOS, Guix, Flatpak: not under /usr)
    for (int i = 0; i < n; i++) {
        snprintf(p, sizeof p, "%s/mime/globs2", dirs[i]);
        mime_load_file(p);
    }
}

// MIME type of a file by name (directories: inode/directory).
static const char *mime_for_file(const char *name, bool is_dir) {
    if (is_dir) return "inode/directory";
    pthread_once(&g_mime_once, mime_init);
    char low[NAME_MAX + 1];
    size_t n = 0;
    for (; name[n] && n < NAME_MAX; n++) low[n] = (char)ascii_lower((u8)name[n]);
    low[n] = 0;
    const char *best = NULL;
    int bw = -1;
    size_t blen = 0;
    for (int i = 0; i < g_nglobs; i++) {
        MimeGlob *g = &g_globs[i];
        bool hit;
        size_t len;
        if (g->suffix) {
            len = strlen(g->suffix);
            hit = n > len && !strcmp(low + n - len, g->suffix);
        } else {
            len = strlen(g->glob);
            hit = fnmatch(g->glob, low, 0) == 0;
        }
        // longer patterns win over shorter ones (".tar.gz" over ".gz"), then weight
        if (hit && (len > blen || (len == blen && g->weight > bw))) {
            best = g->mime;
            bw = g->weight;
            blen = len;
        }
    }
    return best ? best : "application/octet-stream";
}

// --- desktop entries -------------------------------------------------------

typedef struct {
    char *id;    // e.g. "org.gnome.TextEditor.desktop"
    char *path;  // full path of the .desktop file
    char *name;
    char *exec;
    char *icon;
    char *mimes; // ";type;type;"
    bool nodisplay, terminal;
} DesktopApp;

static DesktopApp *g_apps;
static int g_napps;
static char **g_def_mime, **g_def_apps; // Default Applications: mime -> "a.desktop;b.desktop;"
static int g_ndef;
static char **g_add_mime, **g_add_apps; // Added Associations
static int g_nadd;
static char **g_rem_mime, **g_rem_apps; // Removed Associations: never offered for that type
static int g_nrem;
static bool g_apps_loaded;

// Is one of $XDG_CURRENT_DESKTOP's names in list (";GNOME;KDE;")?
static bool desktop_listed(const char *list) {
    const char *cur = getenv("XDG_CURRENT_DESKTOP");
    if (!cur || !cur[0]) return false;
    char buf[256], key[96];
    snprintf(buf, sizeof buf, "%s", cur);
    char *save = NULL;
    for (char *d = strtok_r(buf, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
        snprintf(key, sizeof key, ";%s;", d);
        if (strstr(list, key)) return true;
    }
    return false;
}

static DesktopApp *app_by_id(const char *id) {
    for (int i = 0; i < g_napps; i++)
        if (!strcmp(g_apps[i].id, id)) return &g_apps[i];
    return NULL;
}

static void apps_scan_dir(const char *dir, const char *prefix) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st)) continue;
        if (S_ISDIR(st.st_mode)) { // subdirectories contribute "sub-" to the desktop id
            char pre[PATH_MAX];
            snprintf(pre, sizeof pre, "%s%s-", prefix, e->d_name);
            apps_scan_dir(path, pre);
            continue;
        }
        size_t l = strlen(e->d_name);
        if (l < 9 || strcmp(e->d_name + l - 8, ".desktop")) continue;
        char id[PATH_MAX];
        snprintf(id, sizeof id, "%s%s", prefix, e->d_name);
        if (app_by_id(id)) continue; // earlier directories take precedence
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char line[4096], name[256] = "", exec[2048] = "", icon[512] = "", mimes[4096] = "", type[64] = "", tryexec[512] = "";
        char only[512] = "", notin[512] = "";
        bool in_main = false, nodisplay = false, hidden = false, terminal = false;
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (line[0] == '[') { in_main = !strcmp(line, "[Desktop Entry]"); continue; }
            if (!in_main) continue;
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = 0;
            const char *k = line, *v = eq + 1;
            if (!strcmp(k, "Name")) snprintf(name, sizeof name, "%s", v);
            else if (!strcmp(k, "Exec")) snprintf(exec, sizeof exec, "%s", v);
            else if (!strcmp(k, "Icon")) snprintf(icon, sizeof icon, "%s", v);
            else if (!strcmp(k, "MimeType")) snprintf(mimes, sizeof mimes, ";%s", v);
            else if (!strcmp(k, "Type")) snprintf(type, sizeof type, "%s", v);
            else if (!strcmp(k, "NoDisplay")) nodisplay = !strcmp(v, "true");
            else if (!strcmp(k, "Hidden")) hidden = !strcmp(v, "true");
            else if (!strcmp(k, "Terminal")) terminal = !strcmp(v, "true");
            else if (!strcmp(k, "TryExec")) snprintf(tryexec, sizeof tryexec, "%s", v);
            else if (!strcmp(k, "OnlyShowIn")) snprintf(only, sizeof only, ";%s", v);
            else if (!strcmp(k, "NotShowIn")) snprintf(notin, sizeof notin, ";%s", v);
        }
        fclose(f);
        if (hidden || strcmp(type, "Application") || !exec[0]) continue;
        if (tryexec[0] && !in_path(tryexec)) continue; // its program isn't installed
        if ((only[0] && !desktop_listed(only)) || (notin[0] && desktop_listed(notin))) nodisplay = true; // other desktops' tools
        size_t ml = strlen(mimes);
        if (ml && mimes[ml - 1] != ';' && ml + 1 < sizeof mimes) strcat(mimes, ";");
        g_apps = xrealloc(g_apps, (size_t)(g_napps + 1) * sizeof(DesktopApp));
        g_apps[g_napps++] = (DesktopApp){strdup(id), strdup(path), strdup(name[0] ? name : id), strdup(exec),
                                         strdup(icon), strdup(mimes), nodisplay, terminal};
    }
    closedir(d);
}

static void mimeapps_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[8192];
    int group = 0; // 1 default, 2 added, 3 removed
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') {
            group = !strcmp(line, "[Default Applications]") ? 1 : !strcmp(line, "[Added Associations]") ? 2
                  : !strcmp(line, "[Removed Associations]") ? 3 : 0;
            continue;
        }
        char *eq = strchr(line, '=');
        if (!group || !eq) continue;
        *eq = 0;
        if (group == 1) {
            g_def_mime = xrealloc(g_def_mime, (size_t)(g_ndef + 1) * sizeof(char *));
            g_def_apps = xrealloc(g_def_apps, (size_t)(g_ndef + 1) * sizeof(char *));
            g_def_mime[g_ndef] = strdup(line);
            g_def_apps[g_ndef++] = strdup(eq + 1);
        } else if (group == 3) {
            g_rem_mime = xrealloc(g_rem_mime, (size_t)(g_nrem + 1) * sizeof(char *));
            g_rem_apps = xrealloc(g_rem_apps, (size_t)(g_nrem + 1) * sizeof(char *));
            g_rem_mime[g_nrem] = strdup(line);
            g_rem_apps[g_nrem++] = strdup(eq + 1);
        } else {
            g_add_mime = xrealloc(g_add_mime, (size_t)(g_nadd + 1) * sizeof(char *));
            g_add_apps = xrealloc(g_add_apps, (size_t)(g_nadd + 1) * sizeof(char *));
            g_add_mime[g_nadd] = strdup(line);
            g_add_apps[g_nadd++] = strdup(eq + 1);
        }
    }
    fclose(f);
}

static void apps_load(void) {
    if (g_apps_loaded) return;
    g_apps_loaded = true;
    const char *home = getenv("HOME") ? getenv("HOME") : "/";
    char p[PATH_MAX];
    const char *xd = getenv("XDG_DATA_HOME");
    if (xd && xd[0] == '/') snprintf(p, sizeof p, "%s/applications", xd);
    else snprintf(p, sizeof p, "%s/.local/share/applications", home);
    apps_scan_dir(p, "");
    snprintf(p, sizeof p, "%s/.local/share/flatpak/exports/share/applications", home);
    apps_scan_dir(p, "");
    apps_scan_dir("/var/lib/flatpak/exports/share/applications", "");
    const char *dd = getenv("XDG_DATA_DIRS");
    char dirs[4096];
    snprintf(dirs, sizeof dirs, "%s", dd && dd[0] ? dd : "/usr/local/share:/usr/share");
    for (char *s = strtok(dirs, ":"); s; s = strtok(NULL, ":")) {
        snprintf(p, sizeof p, "%s/applications", s);
        apps_scan_dir(p, "");
    }
    // mimeapps.list in the spec's order (earlier wins): config home, config dirs, data home, data dirs; in each,
    // the desktop's own "<desktop>-mimeapps.list" (kde-, gnome-...) before the shared one
    char where[40][PATH_MAX];
    int nw = 0;
    const char *xc = getenv("XDG_CONFIG_HOME");
    if (xc && xc[0] == '/') snprintf(where[nw++], PATH_MAX, "%s", xc);
    else snprintf(where[nw++], PATH_MAX, "%s/.config", home);
    nw += xdg_dirs("XDG_CONFIG_DIRS", "/etc/xdg", where + nw, 16);
    if (xd && xd[0] == '/') snprintf(where[nw++], PATH_MAX, "%s/applications", xd);
    else snprintf(where[nw++], PATH_MAX, "%s/.local/share/applications", home);
    char dd2[16][PATH_MAX];
    int nd = xdg_dirs("XDG_DATA_DIRS", XDG_DATA_DEF, dd2, 16);
    for (int i = 0; i < nd && nw < 40; i++) snprintf(where[nw++], PATH_MAX, "%s/applications", dd2[i]);
    const char *cur = getenv("XDG_CURRENT_DESKTOP");
    for (int i = 0; i < nw; i++) {
        char desks[256];
        snprintf(desks, sizeof desks, "%s", cur ? cur : "");
        char *save = NULL;
        for (char *d = strtok_r(desks, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
            for (char *c = d; *c; c++) *c = (char)ascii_lower((u8)*c);
            snprintf(p, sizeof p, "%s/%s-mimeapps.list", where[i], d);
            mimeapps_load(p);
        }
        snprintf(p, sizeof p, "%s/mimeapps.list", where[i]);
        mimeapps_load(p);
    }
}

// Has the user removed this app from the type's list ([Removed Associations])?
static bool app_removed(const DesktopApp *a, const char *mime) {
    for (int i = 0; i < g_nrem; i++) {
        if (strcmp(g_rem_mime[i], mime)) continue;
        char key[PATH_MAX + 2];
        snprintf(key, sizeof key, "%s;", a->id);
        const char *s = g_rem_apps[i];
        for (const char *h = strstr(s, key); h; h = strstr(h + 1, key))
            if (h == s || h[-1] == ';') return true;
        size_t l = strlen(a->id), sl = strlen(s); // (last entry without the ';')
        if (sl >= l && !strcmp(s + sl - l, a->id) && (sl == l || s[sl - l - 1] == ';')) return true;
    }
    return false;
}

static bool app_handles(const DesktopApp *a, const char *mime) {
    char key[256];
    snprintf(key, sizeof key, ";%s;", mime);
    return strstr(a->mimes, key) != NULL;
}

// Default app for a MIME type (falls back to text/plain for text/*), or NULL.
static DesktopApp *app_default_for(const char *mime) {
    apps_load();
    for (int pass = 0; pass < 2; pass++) {
        const char *m = pass == 0 ? mime : (!strncmp(mime, "text/", 5) ? "text/plain" : NULL);
        if (!m) break;
        for (int i = 0; i < g_ndef; i++) {
            if (strcmp(g_def_mime[i], m)) continue;
            char buf[8192];
            snprintf(buf, sizeof buf, "%s", g_def_apps[i]);
            for (char *id = strtok(buf, ";"); id; id = strtok(NULL, ";")) {
                DesktopApp *a = app_by_id(id);
                if (a) return a;
            }
        }
        for (int i = 0; i < g_napps; i++)
            if (!g_apps[i].nodisplay && app_handles(&g_apps[i], m) && !app_removed(&g_apps[i], m)) return &g_apps[i];
    }
    return NULL;
}

// Apps for a MIME type: default first, then added associations, then declared handlers.
static int apps_for_mime(const char *mime, DesktopApp **out, int max) {
    apps_load();
    int n = 0;
    DesktopApp *d = app_default_for(mime);
    if (d && n < max) out[n++] = d;
    for (int pass = 0; pass < 2; pass++) {
        const char *m = pass == 0 ? mime : (!strncmp(mime, "text/", 5) && strcmp(mime, "text/plain") ? "text/plain" : NULL);
        if (!m) break;
        for (int i = 0; i < g_nadd; i++) {
            if (strcmp(g_add_mime[i], m)) continue;
            char buf[8192];
            snprintf(buf, sizeof buf, "%s", g_add_apps[i]);
            for (char *id = strtok(buf, ";"); id; id = strtok(NULL, ";")) {
                DesktopApp *a = app_by_id(id);
                bool dup = false;
                for (int k = 0; k < n; k++) dup |= out[k] == a;
                if (a && !dup && n < max) out[n++] = a;
            }
        }
        for (int i = 0; i < g_napps; i++) {
            DesktopApp *a = &g_apps[i];
            if (a->nodisplay || !app_handles(a, m) || app_removed(a, m)) continue;
            bool dup = false;
            for (int k = 0; k < n; k++) dup |= out[k] == a;
            if (!dup && n < max) out[n++] = a;
        }
    }
    return n;
}

static void spawn_terminal(const char *dir, char *const cmd[]); // (app.c)

// Launch an app with files, expanding the Exec field codes.
static bool app_launch(const DesktopApp *a, char **files, int nfiles) {
    char *argv[256];
    int argc = 0;
    char buf[4096];
    snprintf(buf, sizeof buf, "%s", a->exec);
    char *uris[64];
    int nuri = 0;
    for (int i = 0; i < nfiles && i < 64; i++) {
        if (strstr(files[i], "://")) { uris[nuri++] = strdup(files[i]); continue; } // already a URL (a web link)
        char enc[PATH_MAX * 3];
        url_encode_path(files[i], enc, sizeof enc);
        uris[nuri] = xmalloc(strlen(enc) + 8);
        sprintf(uris[nuri++], "file://%s", enc);
    }
    // tokenize with double quotes and backslash escapes
    char *p = buf;
    char *tokens[256];
    int nt = 0;
    while (*p && nt < 255) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char *start = p, *w = p;
        bool q = false;
        while (*p && (q || (*p != ' ' && *p != '\t'))) {
            if (*p == '"') { q = !q; p++; continue; }
            if (*p == '\\' && p[1]) { p++; }
            *w++ = *p++;
        }
        if (*p) p++;
        *w = 0;
        tokens[nt++] = start;
    }
    bool used = false;
    for (int i = 0; i < nt && argc < 250; i++) {
        char *t = tokens[i];
        if (!strcmp(t, "%f") || !strcmp(t, "%u")) {
            if (nfiles) argv[argc++] = !strcmp(t, "%u") ? uris[0] : files[0];
            used = true;
        } else if (!strcmp(t, "%F") || !strcmp(t, "%U")) {
            for (int k = 0; k < nfiles && argc < 250; k++) argv[argc++] = !strcmp(t, "%U") ? uris[k] : files[k];
            used = true;
        } else if (!strcmp(t, "%i")) {
            if (a->icon[0] && argc < 249) { argv[argc++] = "--icon"; argv[argc++] = a->icon; }
        } else if (!strcmp(t, "%c")) {
            argv[argc++] = a->name;
        } else if (!strcmp(t, "%k")) {
            argv[argc++] = a->path;
        } else if (t[0] == '%' && t[1] && !t[2]) {
            // %d %D %n %N %v %m: deprecated, dropped
        } else {
            // "%%" -> "%"
            char *pp;
            while ((pp = strstr(t, "%%"))) memmove(pp, pp + 1, strlen(pp));
            argv[argc++] = t;
        }
    }
    if (!used) // app without file codes: append the files
        for (int k = 0; k < nfiles && argc < 250; k++) argv[argc++] = files[k];
    argv[argc] = NULL;
    bool ok = false;
    if (argc && a->terminal) { // a console program (vim, htop...): in a terminal window
        spawn_terminal(NULL, argv);
        ok = true;
    } else if (argc) {
        pid_t pid;
        ok = posix_spawnp(&pid, argv[0], NULL, spawn_attr(), argv, environ) == 0;
    }
    for (int i = 0; i < nuri; i++) free(uris[i]);
    return ok;
}

static void app_set_default(const DesktopApp *a, const char *mime) {
    char *argv[] = {"xdg-mime", "default", a->id, (char *)mime, NULL};
    pid_t pid;
    posix_spawnp(&pid, "xdg-mime", NULL, spawn_attr(), argv, environ);
    // update our view right away
    for (int i = 0; i < g_ndef; i++)
        if (!strcmp(g_def_mime[i], mime)) {
            free(g_def_apps[i]);
            g_def_apps[i] = xmalloc(strlen(a->id) + 2);
            sprintf(g_def_apps[i], "%s;", a->id);
            return;
        }
    g_def_mime = xrealloc(g_def_mime, (size_t)(g_ndef + 1) * sizeof(char *));
    g_def_apps = xrealloc(g_def_apps, (size_t)(g_ndef + 1) * sizeof(char *));
    g_def_mime[g_ndef] = strdup(mime);
    g_def_apps[g_ndef] = xmalloc(strlen(a->id) + 2);
    sprintf(g_def_apps[g_ndef++], "%s;", a->id);
}

// --- SHA-256 (FIPS 180-4) ----------------------------------------------------

typedef struct { u32 h[8]; u64 len; u8 buf[64]; size_t n; } Sha256;

static void sha256_block(Sha256 *s, const u8 *p) {
    static const u32 K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
        0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
        0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
        0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
    u32 w[64];
    for (int i = 0; i < 16; i++) w[i] = (u32)p[i * 4] << 24 | (u32)p[i * 4 + 1] << 16 | (u32)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        u32 s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        u32 s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    u32 a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        u32 t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        u32 t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
#undef ROR
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha256_init(Sha256 *s) {
    static const u32 iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, iv, sizeof iv);
    s->len = 0;
    s->n = 0;
}

static void sha256_update(Sha256 *s, const u8 *p, size_t n) {
    s->len += n;
    while (n) {
        size_t k = MIN(n, 64 - s->n);
        memcpy(s->buf + s->n, p, k);
        s->n += k;
        p += k;
        n -= k;
        if (s->n == 64) { sha256_block(s, s->buf); s->n = 0; }
    }
}

static void sha256_final(Sha256 *s, char out[65]) {
    u64 bits = s->len * 8;
    u8 pad = 0x80;
    sha256_update(s, &pad, 1);
    u8 z = 0;
    while (s->n != 56) sha256_update(s, &z, 1);
    u8 lb[8];
    for (int i = 0; i < 8; i++) lb[i] = (u8)(bits >> (56 - i * 8));
    sha256_update(s, lb, 8);
    for (int i = 0; i < 8; i++) sprintf(out + i * 8, "%08x", s->h[i]);
    out[64] = 0;
}

// --- background jobs for Properties: folder statistics and checksums ---------

typedef struct {
    char **paths;
    int n;
    _Atomic u64 size;
    _Atomic u32 files, dirs;
    _Atomic int cancel, done;
    _Atomic int refs;
} StatsJob;

static void stats_release(StatsJob *j) {
    if (atomic_fetch_sub(&j->refs, 1) != 1) return;
    strv_free(j->paths, j->n);
    free(j);
}

static void stats_walk(StatsJob *j, int dfd, const char *name, dev_t dev, char *buf) {
    if (atomic_load_explicit(&j->cancel, memory_order_relaxed)) return;
    struct statx sx;
    if (statx(dfd, name, AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT, STATX_TYPE | STATX_SIZE, &sx)) return;
    if (!S_ISDIR(sx.stx_mode)) {
        atomic_fetch_add(&j->files, 1);
        if (S_ISREG(sx.stx_mode)) atomic_fetch_add(&j->size, sx.stx_size);
        return;
    }
    atomic_fetch_add(&j->dirs, 1);
    if (makedev(sx.stx_dev_major, sx.stx_dev_minor) != dev) return;
    int fd = openat(dfd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return;
    for (;;) {
        long nread = syscall(SYS_getdents64, fd, buf, 32 * 1024);
        if (nread <= 0) break;
        char *copy = xmalloc((size_t)nread);
        memcpy(copy, buf, (size_t)nread);
        for (long off = 0; off < nread;) {
            struct linux_dirent64_ *d = (struct linux_dirent64_ *)(copy + off);
            off += d->d_reclen;
            if (!is_dot_or_dotdot(d->d_name)) stats_walk(j, fd, d->d_name, dev, buf);
        }
        free(copy);
    }
    close(fd);
}

static void *stats_thread(void *arg) {
    StatsJob *j = arg;
    thread_background_priority();
    char *buf = xmalloc(32 * 1024);
    for (int i = 0; i < j->n && !atomic_load(&j->cancel); i++) {
        struct stat st;
        if (lstat(j->paths[i], &st)) continue;
        stats_walk(j, AT_FDCWD, j->paths[i], st.st_dev, buf);
    }
    free(buf);
    atomic_store(&j->done, 1);
    main_wake();
    stats_release(j);
    return NULL;
}

static StatsJob *stats_start(char **paths, int n) {
    StatsJob *j = xcalloc(1, sizeof *j);
    j->paths = strv_dup(paths, n);
    j->n = n;
    atomic_init(&j->refs, 2); // UI + thread
    pthread_t t;
    pthread_create(&t, NULL, stats_thread, j);
    pthread_setname_np(t, "fe-stats");
    pthread_detach(t);
    return j;
}

typedef struct {
    char path[PATH_MAX];
    char md5[33], sha256[65];
    _Atomic int cancel, done;
    _Atomic u64 progress, total;
    _Atomic int refs;
} SumJob;

static void sum_release(SumJob *j) {
    if (atomic_fetch_sub(&j->refs, 1) == 1) free(j);
}

static void *sum_thread(void *arg) {
    SumJob *j = arg;
    thread_background_priority();
    int fd = open(j->path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        struct stat st;
        fstat(fd, &st);
        atomic_store(&j->total, (u64)st.st_size);
        Sha256 sh;
        sha256_init(&sh);
        // MD5 needs the whole message in our tiny implementation: only for files up to 256 MB
        bool do_md5 = st.st_size <= (256ll << 20);
        u8 *all = do_md5 ? malloc((size_t)MAX(st.st_size, 1)) : NULL;
        size_t got = 0;
        u8 *buf = xmalloc(1 << 20);
        for (;;) {
            if (atomic_load(&j->cancel)) break;
            ssize_t n = read(fd, buf, 1 << 20);
            if (n <= 0) break;
            sha256_update(&sh, buf, (size_t)n);
            if (all && got + (size_t)n <= (size_t)st.st_size) { memcpy(all + got, buf, (size_t)n); got += (size_t)n; }
            atomic_fetch_add(&j->progress, (u64)n);
        }
        free(buf);
        close(fd);
        if (!atomic_load(&j->cancel)) {
            sha256_final(&sh, j->sha256);
            if (all) md5_hex(all, got, j->md5);
            else snprintf(j->md5, sizeof j->md5, "(file too large)");
        }
        free(all);
    } else {
        snprintf(j->sha256, sizeof j->sha256, "%s", strerror(errno));
    }
    atomic_store(&j->done, 1);
    main_wake();
    sum_release(j);
    return NULL;
}

static SumJob *sum_start(const char *path) {
    SumJob *j = xcalloc(1, sizeof *j);
    snprintf(j->path, sizeof j->path, "%s", path);
    atomic_init(&j->refs, 2);
    pthread_t t;
    pthread_create(&t, NULL, sum_thread, j);
    pthread_setname_np(t, "fe-sha256");
    pthread_detach(t);
    return j;
}
