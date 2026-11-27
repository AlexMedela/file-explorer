// Local share roots, not client mounts. Read-only, main-thread cache.
// Configuration indicates a configured share; it does not prove the server is running.
enum { LOCAL_SHARE_SMB = 1, LOCAL_SHARE_NFS = 2, LOCAL_SHARE_OTHER = 4 };
typedef struct { char paths[512][PATH_MAX]; unsigned kinds[512]; int count; } LocalShares;
static LocalShares g_local_shares;
static u64 g_local_shares_checked;

static char *share_trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    return s;
}

// Quoted paths and the octal escapes used by exports/etab are supported.
static void share_add(LocalShares *s, const char *value, unsigned kind) {
    char path[PATH_MAX]; size_t n = 0;
    char quote = (*value == '"' || *value == '\'') ? *value++ : 0;
    while (*value && (!quote || *value != quote) && n + 1 < sizeof path) {
        if (*value == '\\' && value[1] >= '0' && value[1] <= '7' &&
            value[2] >= '0' && value[2] <= '7' && value[3] >= '0' && value[3] <= '7') {
            path[n++] = (char)((value[1] - '0') * 64 + (value[2] - '0') * 8 + value[3] - '0');
            value += 4;
        } else path[n++] = *value++;
    }
    path[n] = 0;
    while (n > 1 && path[n - 1] == '/') path[--n] = 0;
    // Dynamic Samba substitutions cannot identify one fixed local directory.
    if (path[0] != '/' || strchr(path, '%') || strchr(path, '$') || !n) return;
    for (int i = 0; i < s->count; i++) if (!strcmp(s->paths[i], path)) { s->kinds[i] |= kind; return; }
    if (s->count < (int)ARRAY_COUNT(s->paths)) {
        strcpy(s->paths[s->count], path); s->kinds[s->count++] = kind;
    }
}

enum { SHARE_SAMBA, SHARE_USER, SHARE_NFS, SHARE_APACHE };
static void shares_read_dir_at(LocalShares *s, const char *dir, int kind, const char *suffix, const char *root);
static void shares_read_at(LocalShares *s, const char *file, int kind, int depth, const char *root) {
    if (depth > 8) return;
    FILE *f = fopen(file, "re");
    if (!f) return;
    char line[8192], section[256] = "", pending[PATH_MAX] = "";
    bool dav = false;
    while (fgets(line, sizeof line, f)) {
        char *p = share_trim(line);
        if (!*p || *p == '#' || *p == ';') continue;
        if (kind == SHARE_NFS) {
            char *end = p;
            if (*end == '"') { end++; while (*end && *end != '"') end++; if (*end) end++; }
            else while (*end && !isspace((unsigned char)*end)) end++;
            if (!*end) continue; // an export needs a client/options field
            *end = 0; share_add(s, p, LOCAL_SHARE_NFS);
        } else if (kind == SHARE_APACHE) {
            if (!strncasecmp(p, "<Directory ", 11)) {
                char *end = strrchr(p, '>'); if (end) *end = 0;
                snprintf(pending, sizeof pending, "%s", share_trim(p + 11)); dav = false;
            } else if (!strncasecmp(p, "Dav ", 4)) {
                char *v = share_trim(p + 4); dav = !strcasecmp(v, "On");
            } else if (!strcasecmp(p, "</Directory>")) {
                if (dav && !strpbrk(pending, "*?")) share_add(s, pending, LOCAL_SHARE_OTHER);
                pending[0] = 0; dav = false;
            }
        } else {
            if (*p == '[') { snprintf(section, sizeof section, "%s", p); continue; }
            char *eq = strchr(p, '='); if (!eq) continue;
            *eq++ = 0; char *key = share_trim(p), *v = share_trim(eq);
            if (kind == SHARE_SAMBA && !strcasecmp(key, "include") && *v == '/' && !strchr(v, '%')) {
                char included[PATH_MAX];
                snprintf(included, sizeof included, "%s%s", root, v);
                shares_read_at(s, included, kind, depth + 1, root);
            }
            if (kind == SHARE_SAMBA && !strcasecmp(key, "usershare path") && *v == '/') {
                // The directory is scanned after the main configuration below.
                char configured[PATH_MAX];
                snprintf(configured, sizeof configured, "%s%s", root, v);
                shares_read_dir_at(s, configured, SHARE_USER, "", root);
            }
            if (!strcasecmp(key, "path") && (kind == SHARE_USER ||
                (section[0] && strcasecmp(section, "[global]") && strcasecmp(section, "[printers]") && strcasecmp(section, "[print$]"))))
                share_add(s, v, LOCAL_SHARE_SMB);
        }
    }
    fclose(f);
}

static void shares_read_dir_at(LocalShares *s, const char *dir, int kind, const char *suffix, const char *root) {
    DIR *d = opendir(dir); if (!d) return;
    struct dirent *e; int files = 0;
    while ((e = readdir(d)) && files < 512) {
        if (e->d_name[0] == '.') continue;
        size_t n = strlen(e->d_name), z = strlen(suffix);
        if (n < z || strcmp(e->d_name + n - z, suffix)) continue;
        char path[PATH_MAX]; path_join(path, sizeof path, dir, e->d_name);
        struct stat st;
        if (!stat(path, &st) && S_ISREG(st.st_mode)) { shares_read_at(s, path, kind, 0, root); files++; }
    }
    closedir(d);
}

static unsigned local_share_kind(const LocalShares *s, const char *path) {
    if (!path || path[0] != '/') return false;
    size_t n = strlen(path); while (n > 1 && path[n - 1] == '/') n--;
    for (int i = 0; i < s->count; i++)
        if (strlen(s->paths[i]) == n && !strncmp(s->paths[i], path, n)) return s->kinds[i];
    // A parent directory can be a symlink (for example /mnt -> /var/mnt).
    // Only resolve plausible share roots, since this lookup runs while drawing
    // folders and resolving arbitrary network paths could stall the window.
    const char *name = path + n;
    while (name > path && name[-1] != '/') name--;
    size_t name_len = (size_t)(path + n - name);
    bool candidate = false;
    for (int i = 0; i < s->count; i++)
        if (strlen(path_basename(s->paths[i])) == name_len &&
            !strncmp(path_basename(s->paths[i]), name, name_len)) { candidate = true; break; }
    if (candidate) {
        char resolved[PATH_MAX];
        if (realpath(path, resolved))
            for (int i = 0; i < s->count; i++)
                if (!strcmp(s->paths[i], resolved)) return s->kinds[i];
    }
    return false;
}

static bool local_share_contains(const LocalShares *s, const char *path) {
    return local_share_kind(s, path) != 0;
}

static void shares_read(LocalShares *s, const char *file, int kind, int depth) {
    shares_read_at(s, file, kind, depth, "");
}

static void shares_scan_root(LocalShares *s, const char *root) {
    static const struct { const char *path; int kind; const char *suffix; } sources[] = {
        {"/etc/samba/smb.conf", SHARE_SAMBA, NULL},
        {"/var/lib/samba/usershares", SHARE_USER, ""},
        {"/etc/exports", SHARE_NFS, NULL},
        {"/etc/exports.d", SHARE_NFS, ".exports"},
        {"/var/lib/nfs/etab", SHARE_NFS, NULL},
        {"/etc/httpd/conf/httpd.conf", SHARE_APACHE, NULL},
        {"/etc/httpd/conf.d", SHARE_APACHE, ".conf"},
        {"/etc/apache2/sites-enabled", SHARE_APACHE, ".conf"},
        {"/etc/apache2/conf-enabled", SHARE_APACHE, ".conf"},
    };
    for (size_t i = 0; i < ARRAY_COUNT(sources); i++) {
        char path[PATH_MAX]; snprintf(path, sizeof path, "%s%s", root, sources[i].path);
        if (sources[i].suffix) shares_read_dir_at(s, path, sources[i].kind, sources[i].suffix, root);
        else shares_read_at(s, path, sources[i].kind, 0, root);
    }
}

static void local_shares_refresh(void) {
    g_local_shares_checked = now_ns();
    g_local_shares.count = 0;
    shares_scan_root(&g_local_shares, "");
    // Distrobox exposes host configuration here. Share paths themselves
    // remain host paths (the user's home is shared with the container).
    if (!access("/run/host/etc", F_OK)) shares_scan_root(&g_local_shares, "/run/host");
}

static unsigned local_share_path(const char *path) {
    u64 now = now_ns();
    if (!g_local_shares_checked || now - g_local_shares_checked > 10ull * 1000000000ull)
        local_shares_refresh();
    return local_share_kind(&g_local_shares, path);
}

// The roots are re-read every ten seconds (once a frame, not once a row: the files are small, the answer is not).
static void local_shares_tick(void) {
    u64 now = now_ns();
    if (!g_local_shares_checked || now - g_local_shares_checked > 10ull * 1000000000ull) local_shares_refresh();
}
