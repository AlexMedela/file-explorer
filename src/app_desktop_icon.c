// Taskbars resolve desktop entries independently of SDL window icons. Keep the
// actual user launcher (including Distrobox's prefixed entry) pointed at artwork
// in the host's data directory, using a content-based name to avoid stale caches.
enum { APP_DESKTOP_ERROR = -1, APP_DESKTOP_MISSING, APP_DESKTOP_UNCHANGED, APP_DESKTOP_UPDATED };
typedef struct {
    char id[256], source[PATH_MAX], target[PATH_MAX];
    char data[PATH_MAX], host_data[PATH_MAX];
} AppDesktop;

static bool app_desktop_is_export(const char *path, const char *container) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[8192];
    bool matches = false;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "Exec=", 5) || !strstr(line, "distrobox")) continue;
        const char *name = strstr(line, "-n ");
        if (name) name += 3;
        else { name = strstr(line, "--name "); if (name) name += 7; }
        if (!name) continue;
        name += strspn(name, " \t\"'");
        size_t n = strlen(container);
        if (!strncmp(name, container, n) && (!name[n] || strchr(" \t\r\n\"'", name[n]))) { matches = true; break; }
    }
    fclose(f);
    return matches;
}

static bool app_desktop_find(const char *data, AppDesktop *out) {
    memset(out, 0, sizeof *out);
    snprintf(out->id, sizeof out->id, "dev.fileexplorer");
    snprintf(out->data, sizeof out->data, "%s", data);
    snprintf(out->host_data, sizeof out->host_data, "%s", data);
    const char *container = getenv("CONTAINER_ID");
    if (container && *container) {
        if (strchr(container, '/') || strlen(container) > 160) return false;
        const char *home = getenv("DISTROBOX_HOST_HOME");
        if (!home || home[0] != '/') home = getenv("HOME");
        if (!home || home[0] != '/') return false;
        snprintf(out->host_data, sizeof out->host_data, "%s/.local/share", home);
        snprintf(out->data, sizeof out->data, "%s", out->host_data);
        if (access(home, F_OK)) snprintf(out->data, sizeof out->data, "/run/host%s/.local/share", home);
        snprintf(out->id, sizeof out->id, "%s-dev.fileexplorer", container);
        snprintf(out->target, sizeof out->target, "%s/applications/%s.desktop", out->data, out->id);
        if (!access(out->target, R_OK)) {
            snprintf(out->source, sizeof out->source, "%s", out->target);
            return true;
        }
        // Also support an exported launcher the user has renamed to the app ID.
        snprintf(out->id, sizeof out->id, "dev.fileexplorer");
    }
    snprintf(out->target, sizeof out->target, "%s/applications/%s.desktop", out->data, out->id);
    if (!access(out->target, R_OK) && (!(container && *container) || app_desktop_is_export(out->target, container))) {
        snprintf(out->source, sizeof out->source, "%s", out->target);
        return true;
    }
    if (container && *container) return false; // a container-only Exec is unusable on the host
    const char *dirs = getenv("XDG_DATA_DIRS");
    char *copy = strdup(dirs && *dirs ? dirs : "/usr/local/share:/usr/share"), *p = copy, *dir;
    bool found = false;
    while ((dir = strsep(&p, ":"))) {
        if (*dir != '/') continue;
        snprintf(out->source, sizeof out->source, "%s/applications/dev.fileexplorer.desktop", dir);
        if (!access(out->source, R_OK)) { found = true; break; }
    }
    free(copy);
    return found;
}

static u8 *app_desktop_read(const char *path, size_t *length) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0 || size > 16 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    u8 *bytes = xmalloc((size_t)size + 1);
    bool ok = fread(bytes, 1, (size_t)size, f) == (size_t)size && !ferror(f);
    fclose(f);
    if (!ok) { free(bytes); return NULL; }
    bytes[size] = 0;
    *length = (size_t)size;
    return bytes;
}
static int app_desktop_write(const char *path, const void *bytes, size_t length) {
    size_t old_length;
    u8 *old = app_desktop_read(path, &old_length);
    bool same = old && old_length == length && !memcmp(old, bytes, length);
    free(old);
    if (same) return 0;
    char tmp[PATH_MAX + 32];
    snprintf(tmp, sizeof tmp, "%s.tmp-XXXXXX", path);
    int fd = mkstemp(tmp);
    if (fd < 0) return -1;
    struct stat st;
    fchmod(fd, !stat(path, &st) ? st.st_mode & 0777 : 0644);
    FILE *f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(tmp); return -1; }
    bool ok = fwrite(bytes, 1, length, f) == length;
    ok = fclose(f) == 0 && ok;
    if (ok) ok = rename(tmp, path) == 0;
    if (!ok) unlink(tmp);
    return ok ? 1 : -1;
}

static char *app_desktop_rewrite(const char *text, const char *icon, const char *id) {
    // Desktop-entry strings escape backslashes; spaces inside Icon= are valid.
    if (strpbrk(icon, "\r\n") || strpbrk(id, "\r\n")) return NULL;
    char escaped[PATH_MAX * 2], *to = escaped;
    for (const char *p = icon; *p && to < escaped + sizeof escaped - 3; p++) {
        if (*p == '\\') *to++ = '\\';
        *to++ = *p;
    }
    *to = 0;
    char *result = NULL;
    size_t length;
    FILE *out = open_memstream(&result, &length);
    if (!out) return NULL;
    bool main = false, seen = false;
    const char *p = text;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        size_t clean = n && p[n - 1] == '\r' ? n - 1 : n;
        if (clean && p[0] == '[') {
            if (main) fprintf(out, "Icon=%s\nStartupWMClass=%s\n", escaped, id);
            main = clean == 15 && !memcmp(p, "[Desktop Entry]", 15);
            seen |= main;
        }
        if (!(main && ((clean >= 5 && !memcmp(p, "Icon=", 5)) ||
                       (clean >= 15 && !memcmp(p, "StartupWMClass=", 15)))))
            fprintf(out, "%.*s\n", (int)n, p);
        if (!end) break;
        p = end + 1;
    }
    if (main) fprintf(out, "Icon=%s\nStartupWMClass=%s\n", escaped, id);
    bool ok = fclose(out) == 0 && seen;
    if (!ok) { free(result); return NULL; }
    return result;
}

static int app_desktop_install_icon(const char *data, const void *bytes, size_t length, const char *ext) {
    AppDesktop desktop;
    if (!app_desktop_find(data, &desktop)) return APP_DESKTOP_MISSING;
    if (strcmp(ext, "svg") && strcmp(ext, "png")) return APP_DESKTOP_ERROR;
    size_t source_length;
    u8 *source = app_desktop_read(desktop.source, &source_length);
    if (!source) return APP_DESKTOP_ERROR;
    u64 hash = 14695981039346656037ull;
    const u8 *b = bytes;
    for (size_t i = 0; i < length; i++) { hash ^= b[i]; hash *= 1099511628211ull; }
    char folder[PATH_MAX], path[PATH_MAX], host_path[PATH_MAX], applications[PATH_MAX];
    snprintf(folder, sizeof folder, "%s/file-explorer/app-icons", desktop.data);
    snprintf(path, sizeof path, "%s/icon-%016llx.%s", folder, (unsigned long long)hash, ext);
    snprintf(host_path, sizeof host_path, "%s/file-explorer/app-icons/icon-%016llx.%s",
             desktop.host_data, (unsigned long long)hash, ext);
    char *updated = app_desktop_rewrite((char *)source, host_path, desktop.id);
    free(source);
    if (!updated) return APP_DESKTOP_ERROR;
    mkdir_p(folder, 0755);
    int asset = app_desktop_write(path, bytes, length);
    snprintf(applications, sizeof applications, "%s/applications", desktop.data);
    mkdir_p(applications, 0755);
    int entry = asset < 0 ? -1 : app_desktop_write(desktop.target, updated, strlen(updated));
    free(updated);
    if (asset < 0 || entry < 0) return APP_DESKTOP_ERROR;
    return asset || entry ? APP_DESKTOP_UPDATED : APP_DESKTOP_UNCHANGED;
}
