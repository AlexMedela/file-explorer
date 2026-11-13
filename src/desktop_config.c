// Small read-only INI helpers for installed desktop settings and icon themes.
static char *desktop_trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    return s;
}

static bool desktop_ini(const char *path, const char *group, const char *key, char *out, size_t cap) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[8192];
    bool in = false, found = false;
    while (fgets(line, sizeof line, f)) {
        char *p = desktop_trim(line);
        if (*p == '[') {
            char *end = strchr(p + 1, ']');
            if (end) { *end = 0; in = !strcmp(p + 1, group); }
            continue;
        }
        if (!in || !*p || *p == '#' || *p == ';') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq++ = 0;
        if (!strcmp(desktop_trim(p), key)) {
            snprintf(out, cap, "%s", desktop_trim(eq));
            found = true;
        }
    }
    fclose(f);
    return found;
}

static void desktop_config_path(char *out, size_t cap, const char *file) {
    const char *dir = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (dir && dir[0] == '/') snprintf(out, cap, "%s/%s", dir, file);
    else snprintf(out, cap, "%s/.config/%s", home ? home : "/tmp", file);
}

static bool desktop_rgb(const char *s, u32 *out) {
    int r, g, b;
    char extra;
    if (sscanf(s, " %d , %d , %d %c", &r, &g, &b, &extra) != 3 ||
        r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return false;
    *out = (u32)r << 16 | (u32)g << 8 | (u32)b;
    return true;
}
