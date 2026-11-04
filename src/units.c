// units.c - systemd user services the app writes and controls (cloud mounts, add-ons)
//
// Unit files go in ~/.config/systemd/user; `systemctl --user` starts / stops / enables them. Commands run on worker
// threads (run_capture); g_units_systemd turns false once systemctl --user turned out to be unusable.

static bool g_units_systemd = true;

static const char *units_systemctl(void) { return tool("FE_SYSTEMCTL", "systemctl"); }

static void units_dir(char *out, size_t cap) {
    const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
    if (x && x[0] == '/') snprintf(out, cap, "%s/systemd/user", x);
    else snprintf(out, cap, "%s/.config/systemd/user", h ? h : "/tmp");
}

static void exe_path(char *out, size_t cap) {
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    out[n > 0 ? n : 0] = 0;
}

// A program's absolute path (systemd wants one): as given when it has a /, else found in $PATH. false: not found.
static bool tool_abs(const char *s, char *out, size_t cap) {
    if (strchr(s, '/')) { snprintf(out, cap, "%s", s); return true; }
    const char *path = getenv("PATH");
    char pb[4096];
    snprintf(pb, sizeof pb, "%s", path ? path : "/usr/bin:/bin");
    for (char *p = pb, *d; (d = strsep(&p, ":"));) {
        char c[PATH_MAX];
        snprintf(c, sizeof c, "%s/%s", d, s);
        if (d[0] == '/' && access(c, X_OK) == 0) { snprintf(out, cap, "%s", c); return true; }
    }
    out[0] = 0;
    return false;
}

// systemd unit text: specifiers (%) and variables ($) doubled, values quoted.
static void unit_escape(const char *in, char *out, size_t cap) {
    size_t o = 0;
    for (const char *c = in; *c && o + 3 < cap; c++) {
        if (*c == '\n' || *c == '"' || *c == '\\') continue;
        if (*c == '%' || *c == '$') out[o++] = *c;
        out[o++] = *c;
    }
    out[o] = 0;
}

static int units_ctl(char **args, char *out, size_t cap) { // systemctl --user <args...>
    char *argv[16];
    int n = 0;
    argv[n++] = (char *)units_systemctl();
    argv[n++] = "--user";
    argv[n++] = "--no-ask-password";
    for (int i = 0; args[i] && n < 15; i++) argv[n++] = args[i];
    argv[n] = NULL;
    return run_capture(argv, out, cap);
}

// --- files ---------------------------------------------------------------------------------------------------

static bool write_file_atomic(const char *path, const char *text) {
    char tmp[PATH_MAX + 80];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return false;
    bool ok = fputs(text, f) >= 0;
    ok = fclose(f) == 0 && ok;
    return ok && rename(tmp, path) == 0;
}

static bool file_equals(const char *path, const char *text) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return !strcmp(buf, text);
}
