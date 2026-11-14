// term.c - terminal tabs: a shell (or ssh) in a pseudo-terminal, emulated by libvterm, drawn with the UI's
// GPU text renderer. Looks like the user's default terminal: font, colours, padding, cursor and scrollback are
// read from its config (foot, kitty, Alacritty, Ghostty), and follow it when that file changes.
// (included from app.c)

#include <vterm.h>
#include <sys/ioctl.h>
#include <termios.h>

// --- appearance: the default terminal's config ----------------------------------------------------------

typedef struct {
    char source[32];     // "foot", "kitty", ... ("" = none found: the app's look)
    char family[128];
    f32 size_pt, line_pt; // line height in points (0 = the font's)
    f32 yoff_px;          // vertical letter offset
    f32 padx, pady;       // padding (px at 1x)
    u32 fg, bg, sel_fg, sel_bg, cur, cur_text;
    bool has_fg, has_bg, has_sel_fg, has_sel_bg, has_cur, has_cur_text;
    u32 pal[16];
    bool has_pal[16];
    int cursor_shape;     // VTERM_PROP_CURSORSHAPE_*
    int scrollback;
    f32 wheel_lines;
    char term[48];
    char files[12][PATH_MAX]; // config files read (watched for changes)
    time_t mtimes[12];
    int nfiles;
} TermConfig;

static TermConfig g_tc;
static bool g_tc_loaded;
static u64 g_tc_checked;
#define g_term_look g_term_look_setting // 0: match the default terminal, 1: the app's theme (Options)

static void tc_watch(const char *path) {
    struct stat st;
    if (g_tc.nfiles >= (int)ARRAY_COUNT(g_tc.files) || stat(path, &st) != 0) return;
    snprintf(g_tc.files[g_tc.nfiles], sizeof g_tc.files[0], "%s", path);
    g_tc.mtimes[g_tc.nfiles++] = st.st_mtime;
}

static void tc_expand(const char *in, const char *base_dir, char *out, size_t cap) {
    const char *home = getenv("HOME") ? getenv("HOME") : "/";
    if (in[0] == '~' && (in[1] == '/' || !in[1])) snprintf(out, cap, "%s%s", home, in + 1);
    else if (in[0] == '/' || !base_dir) snprintf(out, cap, "%s", in);
    else snprintf(out, cap, "%s/%s", base_dir, in);
}

static char *tc_trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t l = strlen(s);
    while (l && (s[l - 1] == ' ' || s[l - 1] == '\t' || s[l - 1] == '\r' || s[l - 1] == '\n')) s[--l] = 0;
    if (l >= 2 && ((s[0] == '"' && s[l - 1] == '"') || (s[0] == '\'' && s[l - 1] == '\''))) { s[l - 1] = 0; s++; }
    return s;
}

static bool tc_color(const char *v, u32 *out) {
    while (*v == ' ' || *v == '"' || *v == '\'') v++;
    if (*v == '#') v++;
    else if (!strncmp(v, "0x", 2)) v += 2;
    int n = 0;
    while (isxdigit((u8)v[n])) n++;
    if (n != 6 && n != 3) return false;
    char h[8];
    if (n == 3) snprintf(h, sizeof h, "%c%c%c%c%c%c", v[0], v[0], v[1], v[1], v[2], v[2]);
    else snprintf(h, sizeof h, "%.6s", v);
    *out = (u32)strtoul(h, NULL, 16);
    return true;
}

static void tc_set_pal(int i, const char *v) {
    if (i >= 0 && i < 16 && tc_color(v, &g_tc.pal[i])) g_tc.has_pal[i] = true;
}

// foot.ini: [main] font=Family:size=N, line-height, vertical-letter-offset, pad=WxH, include=, term=;
// [colors] / [colors-dark]: foreground, background, regular0-7, bright0-7, selection-*, cursor="text cursor";
// [cursor] style; [scrollback] lines, multiplier.
static void tc_foot(const char *path, int depth) {
    FILE *f = fopen(path, "r");
    if (!f || depth > 4) { if (f) fclose(f); return; }
    tc_watch(path);
    char dir[PATH_MAX], line[1024], sect[64] = "main";
    parent_of(path, dir, sizeof dir);
    while (fgets(line, sizeof line, f)) {
        char *l = tc_trim(line);
        if (!*l || *l == '#' || *l == ';') continue;
        if (*l == '[') { snprintf(sect, sizeof sect, "%.*s", (int)strcspn(l + 1, "]"), l + 1); continue; }
        char *eq = strchr(l, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = tc_trim(l), *v = tc_trim(eq + 1);
        if (!strcmp(sect, "main")) {
            if (!strcmp(k, "include")) { char p[PATH_MAX]; tc_expand(v, dir, p, sizeof p); tc_foot(p, depth + 1); }
            else if (!strcmp(k, "font")) {
                char fam[256];
                snprintf(fam, sizeof fam, "%.*s", (int)strcspn(v, ","), v); // the first font of a list
                char *sz = strstr(fam, ":size=");
                if (sz) { g_tc.size_pt = (f32)atof(sz + 6); *sz = 0; }
                char *c = strchr(fam, ':');
                if (c) *c = 0;
                snprintf(g_tc.family, sizeof g_tc.family, "%s", tc_trim(fam));
            } else if (!strcmp(k, "line-height")) g_tc.line_pt = (f32)atof(v);
            else if (!strcmp(k, "vertical-letter-offset")) g_tc.yoff_px = (f32)atof(v);
            else if (!strcmp(k, "pad")) { int w = 0, h = 0; if (sscanf(v, "%dx%d", &w, &h) >= 1) { g_tc.padx = (f32)w; g_tc.pady = (f32)(h ? h : w); } }
            else if (!strcmp(k, "term")) snprintf(g_tc.term, sizeof g_tc.term, "%s", v);
        } else if (!strcmp(sect, "colors") || !strcmp(sect, "colors-dark")) {
            if (!strcmp(k, "foreground")) g_tc.has_fg = tc_color(v, &g_tc.fg);
            else if (!strcmp(k, "background")) g_tc.has_bg = tc_color(v, &g_tc.bg);
            else if (!strcmp(k, "selection-foreground")) g_tc.has_sel_fg = tc_color(v, &g_tc.sel_fg);
            else if (!strcmp(k, "selection-background")) g_tc.has_sel_bg = tc_color(v, &g_tc.sel_bg);
            else if (!strcmp(k, "cursor")) { // "<text> <cursor>"
                char a[32] = "", b[32] = "";
                if (sscanf(v, "%31s %31s", a, b) == 2) { g_tc.has_cur_text = tc_color(a, &g_tc.cur_text); g_tc.has_cur = tc_color(b, &g_tc.cur); }
            } else if (!strncmp(k, "regular", 7)) tc_set_pal(atoi(k + 7), v);
            else if (!strncmp(k, "bright", 6)) tc_set_pal(8 + atoi(k + 6), v);
        } else if (!strcmp(sect, "cursor") && !strcmp(k, "style")) {
            g_tc.cursor_shape = !strcmp(v, "beam") ? VTERM_PROP_CURSORSHAPE_BAR_LEFT : !strcmp(v, "underline") ? VTERM_PROP_CURSORSHAPE_UNDERLINE : VTERM_PROP_CURSORSHAPE_BLOCK;
        } else if (!strcmp(sect, "scrollback")) {
            if (!strcmp(k, "lines")) g_tc.scrollback = atoi(v);
            else if (!strcmp(k, "multiplier")) g_tc.wheel_lines = 3.f * (f32)atof(v) / 3.f;
        }
    }
    fclose(f);
}

// kitty.conf: "key value"; include, font_family, font_size, colours (#rrggbb), color0-15, cursor_shape,
// window_padding_width (pt), scrollback_lines.
static void tc_kitty(const char *path, int depth) {
    FILE *f = fopen(path, "r");
    if (!f || depth > 4) { if (f) fclose(f); return; }
    tc_watch(path);
    char dir[PATH_MAX], line[1024];
    parent_of(path, dir, sizeof dir);
    while (fgets(line, sizeof line, f)) {
        char *l = tc_trim(line);
        if (!*l || *l == '#') continue;
        size_t kl = strcspn(l, " \t");
        if (!l[kl]) continue;
        l[kl] = 0;
        char *k = l, *v = tc_trim(l + kl + 1);
        if (!strcmp(k, "include") || !strcmp(k, "globinclude")) { char p[PATH_MAX]; tc_expand(v, dir, p, sizeof p); tc_kitty(p, depth + 1); }
        else if (!strcmp(k, "font_family")) snprintf(g_tc.family, sizeof g_tc.family, "%s", v);
        else if (!strcmp(k, "font_size")) g_tc.size_pt = (f32)atof(v);
        else if (!strcmp(k, "foreground")) g_tc.has_fg = tc_color(v, &g_tc.fg);
        else if (!strcmp(k, "background")) g_tc.has_bg = tc_color(v, &g_tc.bg);
        else if (!strcmp(k, "selection_foreground")) g_tc.has_sel_fg = tc_color(v, &g_tc.sel_fg);
        else if (!strcmp(k, "selection_background")) g_tc.has_sel_bg = tc_color(v, &g_tc.sel_bg);
        else if (!strcmp(k, "cursor")) g_tc.has_cur = tc_color(v, &g_tc.cur);
        else if (!strcmp(k, "cursor_text_color")) g_tc.has_cur_text = tc_color(v, &g_tc.cur_text);
        else if (!strncmp(k, "color", 5) && isdigit((u8)k[5])) tc_set_pal(atoi(k + 5), v);
        else if (!strcmp(k, "cursor_shape"))
            g_tc.cursor_shape = !strcmp(v, "beam") ? VTERM_PROP_CURSORSHAPE_BAR_LEFT : !strcmp(v, "underline") ? VTERM_PROP_CURSORSHAPE_UNDERLINE : VTERM_PROP_CURSORSHAPE_BLOCK;
        else if (!strcmp(k, "window_padding_width")) g_tc.padx = g_tc.pady = (f32)atof(v) * 96.f / 72.f;
        else if (!strcmp(k, "scrollback_lines")) g_tc.scrollback = atoi(v);
    }
    fclose(f);
}

// alacritty.toml: [font] size, normal = { family = ".." } / [font.normal] family; [colors.primary] background,
// foreground; [colors.normal]/[colors.bright] black..white; [colors.cursor] cursor/text; [colors.selection];
// [window.padding] x/y; [scrolling] history; import = [..] (top level or [general]).
static void tc_alacritty(const char *path, int depth) {
    FILE *f = fopen(path, "r");
    if (!f || depth > 4) { if (f) fclose(f); return; }
    tc_watch(path);
    static const char *names[8] = {"black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"};
    char dir[PATH_MAX], line[1024], sect[64] = "";
    parent_of(path, dir, sizeof dir);
    while (fgets(line, sizeof line, f)) {
        char *l = tc_trim(line);
        if (!*l || *l == '#') continue;
        if (*l == '[' && l[1] != '[') { snprintf(sect, sizeof sect, "%.*s", (int)strcspn(l + 1, "]"), l + 1); continue; }
        char *eq = strchr(l, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = tc_trim(l), *v = tc_trim(eq + 1);
        if (!strcmp(k, "import")) { // ["a.toml", "~/b.toml"]
            for (char *q = strchr(v, '"'); q; q = strchr(q, '"')) {
                char *e = strchr(q + 1, '"');
                if (!e) break;
                char one[PATH_MAX], p[PATH_MAX];
                snprintf(one, sizeof one, "%.*s", (int)(e - q - 1), q + 1);
                tc_expand(one, dir, p, sizeof p);
                tc_alacritty(p, depth + 1);
                q = e + 1;
            }
        } else if (!strcmp(sect, "font")) {
            if (!strcmp(k, "size")) g_tc.size_pt = (f32)atof(v);
            else if (!strcmp(k, "normal")) { char *fam = strstr(v, "family"); if (fam && (fam = strchr(fam, '"'))) snprintf(g_tc.family, sizeof g_tc.family, "%.*s", (int)strcspn(fam + 1, "\""), fam + 1); }
        } else if (!strcmp(sect, "font.normal") && !strcmp(k, "family")) snprintf(g_tc.family, sizeof g_tc.family, "%s", v);
        else if (!strcmp(sect, "colors.primary")) {
            if (!strcmp(k, "foreground")) g_tc.has_fg = tc_color(v, &g_tc.fg);
            else if (!strcmp(k, "background")) g_tc.has_bg = tc_color(v, &g_tc.bg);
        } else if (!strcmp(sect, "colors.normal") || !strcmp(sect, "colors.bright")) {
            for (int i = 0; i < 8; i++) if (!strcmp(k, names[i])) tc_set_pal(i + (sect[7] == 'b' ? 8 : 0), v);
        } else if (!strcmp(sect, "colors.cursor")) {
            if (!strcmp(k, "cursor")) g_tc.has_cur = tc_color(v, &g_tc.cur);
            else if (!strcmp(k, "text")) g_tc.has_cur_text = tc_color(v, &g_tc.cur_text);
        } else if (!strcmp(sect, "colors.selection")) {
            if (!strcmp(k, "background")) g_tc.has_sel_bg = tc_color(v, &g_tc.sel_bg);
            else if (!strcmp(k, "text")) g_tc.has_sel_fg = tc_color(v, &g_tc.sel_fg);
        } else if (!strcmp(sect, "window.padding")) {
            if (!strcmp(k, "x")) g_tc.padx = (f32)atof(v);
            else if (!strcmp(k, "y")) g_tc.pady = (f32)atof(v);
        } else if (!strcmp(sect, "scrolling") && !strcmp(k, "history")) g_tc.scrollback = atoi(v);
        else if (!strcmp(sect, "cursor.style") && !strcmp(k, "shape"))
            g_tc.cursor_shape = str_has_ci(v, "beam") ? VTERM_PROP_CURSORSHAPE_BAR_LEFT : str_has_ci(v, "underline") ? VTERM_PROP_CURSORSHAPE_UNDERLINE : VTERM_PROP_CURSORSHAPE_BLOCK;
    }
    fclose(f);
}

// Ghostty: "key = value"; font-family, font-size, background, foreground, palette = N=#hex, cursor-color,
// cursor-text, selection-*, cursor-style, window-padding-x/y (pt), config-file, theme (themes/<name>).
static void tc_ghostty(const char *path, int depth) {
    FILE *f = fopen(path, "r");
    if (!f || depth > 4) { if (f) fclose(f); return; }
    tc_watch(path);
    char dir[PATH_MAX], line[1024];
    parent_of(path, dir, sizeof dir);
    bool fam_set = false;
    while (fgets(line, sizeof line, f)) {
        char *l = tc_trim(line);
        if (!*l || *l == '#') continue;
        char *eq = strchr(l, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = tc_trim(l), *v = tc_trim(eq + 1);
        if (!strcmp(k, "font-family") && !fam_set) { snprintf(g_tc.family, sizeof g_tc.family, "%s", v); fam_set = true; }
        else if (!strcmp(k, "font-size")) g_tc.size_pt = (f32)atof(v);
        else if (!strcmp(k, "background")) g_tc.has_bg = tc_color(v, &g_tc.bg);
        else if (!strcmp(k, "foreground")) g_tc.has_fg = tc_color(v, &g_tc.fg);
        else if (!strcmp(k, "selection-background")) g_tc.has_sel_bg = tc_color(v, &g_tc.sel_bg);
        else if (!strcmp(k, "selection-foreground")) g_tc.has_sel_fg = tc_color(v, &g_tc.sel_fg);
        else if (!strcmp(k, "cursor-color")) g_tc.has_cur = tc_color(v, &g_tc.cur);
        else if (!strcmp(k, "cursor-text")) g_tc.has_cur_text = tc_color(v, &g_tc.cur_text);
        else if (!strcmp(k, "palette")) { char *e2 = strchr(v, '='); if (e2) tc_set_pal(atoi(v), e2 + 1); }
        else if (!strcmp(k, "cursor-style"))
            g_tc.cursor_shape = !strcmp(v, "bar") ? VTERM_PROP_CURSORSHAPE_BAR_LEFT : !strcmp(v, "underline") ? VTERM_PROP_CURSORSHAPE_UNDERLINE : VTERM_PROP_CURSORSHAPE_BLOCK;
        else if (!strcmp(k, "window-padding-x")) g_tc.padx = (f32)atof(v) * 96.f / 72.f;
        else if (!strcmp(k, "window-padding-y")) g_tc.pady = (f32)atof(v) * 96.f / 72.f;
        else if (!strcmp(k, "config-file")) { char p[PATH_MAX]; tc_expand(v[0] == '?' ? v + 1 : v, dir, p, sizeof p); tc_ghostty(p, depth + 1); }
        else if (!strcmp(k, "theme")) {
            char p[PATH_MAX + 80];
            snprintf(p, sizeof p, "%s/themes/%s", dir, v);
            if (access(p, R_OK)) snprintf(p, sizeof p, "/usr/share/ghostty/themes/%s", v);
            tc_ghostty(p, depth + 1);
        }
    }
    fclose(f);
}

// The default terminal: xdg-terminal-exec's list ($XDG_CONFIG_HOME/[<desktop>-]xdg-terminals.list, then the
// system ones), else $TERMINAL. Returns "foot", "kitty", "alacritty", "ghostty" or "".
static void tc_default_terminal(char *out, size_t cap) {
    out[0] = 0;
    const char *xch = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME") ? getenv("HOME") : "/";
    char cfg[PATH_MAX], desk[64] = "";
    if (xch && xch[0]) snprintf(cfg, sizeof cfg, "%s", xch);
    else snprintf(cfg, sizeof cfg, "%s/.config", home);
    const char *de = getenv("XDG_CURRENT_DESKTOP");
    if (de) { snprintf(desk, sizeof desk, "%.*s", (int)strcspn(de, ":"), de); for (char *c = desk; *c; c++) *c = (char)tolower((u8)*c); }
    char lists[6][PATH_MAX + 80];
    int nl = 0;
    if (desk[0]) snprintf(lists[nl++], sizeof lists[0], "%s/%s-xdg-terminals.list", cfg, desk);
    snprintf(lists[nl++], sizeof lists[0], "%s/xdg-terminals.list", cfg);
    if (desk[0]) snprintf(lists[nl++], sizeof lists[0], "/etc/xdg/%s-xdg-terminals.list", desk);
    snprintf(lists[nl++], sizeof lists[0], "/etc/xdg/xdg-terminals.list");
    if (desk[0]) snprintf(lists[nl++], sizeof lists[0], "/usr/share/xdg-terminal-exec/%s-xdg-terminals.list", desk);
    char id[128] = "";
    for (int i = 0; i < nl && !id[0]; i++) {
        FILE *f = fopen(lists[i], "r");
        char line[256];
        while (f && fgets(line, sizeof line, f)) {
            char *l = tc_trim(line);
            if (*l && *l != '#' && *l != '-' && *l != '/') { snprintf(id, sizeof id, "%s", l); break; }
        }
        if (f) fclose(f);
    }
    if (!id[0] && getenv("TERMINAL")) snprintf(id, sizeof id, "%s", path_basename(getenv("TERMINAL")));
    for (char *c = id; *c; c++) *c = (char)tolower((u8)*c);
    static const char *known[] = {"foot", "kitty", "alacritty", "ghostty"};
    for (size_t k = 0; k < ARRAY_COUNT(known); k++)
        if (strstr(id, known[k])) { snprintf(out, cap, "%s", known[k]); return; }
}

static void tc_reload_all(void);

static void tc_load(void) {
    memset(&g_tc, 0, sizeof g_tc);
    g_tc.size_pt = 10.5f;
    g_tc.padx = g_tc.pady = 8;
    g_tc.scrollback = 10000;
    g_tc.wheel_lines = 3;
    g_tc.cursor_shape = VTERM_PROP_CURSORSHAPE_BLOCK;
    snprintf(g_tc.term, sizeof g_tc.term, "xterm-256color");
    snprintf(g_tc.family, sizeof g_tc.family, "monospace");
    g_tc_loaded = true;
    if (g_term_look == 1) return; // the app's look
    const char *xch = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME") ? getenv("HOME") : "/";
    char cfg[PATH_MAX], p[PATH_MAX + 64], which[32];
    if (xch && xch[0]) snprintf(cfg, sizeof cfg, "%s", xch);
    else snprintf(cfg, sizeof cfg, "%s/.config", home);
    tc_default_terminal(which, sizeof which);
    struct { const char *name, *file; void (*fn)(const char *, int); } cands[] = {
        {"foot", "foot/foot.ini", tc_foot}, {"kitty", "kitty/kitty.conf", tc_kitty},
        {"alacritty", "alacritty/alacritty.toml", tc_alacritty}, {"ghostty", "ghostty/config", tc_ghostty},
    };
    for (int pass = 0; pass < 2 && !g_tc.source[0]; pass++) // the default terminal's config, else any we know
        for (size_t k = 0; k < ARRAY_COUNT(cands); k++) {
            if (pass == 0 && strcmp(which, cands[k].name)) continue;
            snprintf(p, sizeof p, "%s/%s", cfg, cands[k].file);
            if (access(p, R_OK)) continue;
            snprintf(g_tc.source, sizeof g_tc.source, "%s", cands[k].name);
            cands[k].fn(p, 0);
            break;
        }
    if (g_tc.scrollback <= 0 || g_tc.scrollback > 200000) g_tc.scrollback = 10000;
    if (g_tc.wheel_lines <= 0) g_tc.wheel_lines = 3;
}

// Did a config file change (Omarchy switching themes, an edit)? Checked about once a second.
static bool tc_changed(void) {
    for (int i = 0; i < g_tc.nfiles; i++) {
        struct stat st;
        if (stat(g_tc.files[i], &st) != 0 || st.st_mtime != g_tc.mtimes[i]) return true;
    }
    return false;
}

static const u32 g_xterm16[16] = {0x000000, 0xCD0000, 0x00CD00, 0xCDCD00, 0x0000EE, 0xCD00CD, 0x00CDCD, 0xE5E5E5,
                                  0x7F7F7F, 0xFF0000, 0x00FF00, 0xFFFF00, 0x5C5CFF, 0xFF00FF, 0x00FFFF, 0xFFFFFF};

static u32 tc_fg(void) { return g_tc.has_fg ? g_tc.fg : COL_TEXT; }
static u32 tc_bg(void) { return g_tc.has_bg ? g_tc.bg : COL_BG; }
static u32 tc_pal(int i) { return g_tc.has_pal[i] ? g_tc.pal[i] : g_xterm16[i]; }

static int utf8_encode(u32 cp, char *o) {
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

// --- terminals --------------------------------------------------------------------------------------------

typedef struct { u32 ch, fg, bg; u8 attr, width; } TCell; // attr: 1 bold 2 italic 4 underline 8 reverse 16 strike 32 fg default 64 bg default
typedef struct { int n; TCell c[]; } TLine;

struct Term {
    int fd;           // pty master
    pid_t pid;        // the shell / ssh (its own session and process group)
    VTerm *vt;
    VTermScreen *scr;
    int rows, cols;
    TLine **sb;       // scrollback ring (oldest first): sb_head .. sb_n
    int sb_n, sb_cap, sb_head;
    int sb_off;       // lines scrolled back (0 = live)
    f32 hint_rpad;    // room the owner keeps clear at the top right (the terminal panel's close button)
    char title[256], title_tmp[256];
    char cwd_label[NAME_MAX + 1];
    u64 label_t;
    char start_label[128];
    bool cursor_visible, altscreen, dead;
    int mouse_mode, cursor_shape;
    VTermPos cursor;
    // selection in absolute rows (scrollback rows negative, live screen rows from 0)
    bool sel_on, selecting;
    int sel_r0, sel_c0, sel_r1, sel_c1;
    Rect area;        // where it was drawn (input mapping)
    f32 cw, ch, ox, oy;
    // reader thread -> main thread
    pthread_t thr;
    int stop[2];
    pthread_mutex_t mtx;
    char *in;
    size_t in_n, in_cap;
    bool eof;
    u32 skip_text; // a Ctrl/Alt key just sent this character: drop the matching text event
    u64 born, last_out; // started / last output (screenshots wait for a quiet moment)
    // shell integration (terminal panel): cd's for the pane happen silently, see "shell integration" below
    int shint;              // SHINT_*
    bool shint_ready;       // the shell loaded our part (it made cd_file + ".ready")
    char cd_file[PATH_MAX]; // the folder to go to is written here before the shell is poked
};

static Font g_term_font, g_term_bold;
static bool g_term_font_ok;
static f32 g_term_font_scale;
static char g_term_font_family[128];

static void term_fonts(void) {
    if (g_term_font_ok && g_term_font_scale == A.scale && !strcmp(g_term_font_family, g_tc.family)) return;
    int px = MAX(6, (int)roundf(g_tc.size_pt * 96.f / 72.f * A.scale));
    char *path = font_family_file(g_tc.family);
    if (!path && A.mono_path) path = strdup(A.mono_path);
    if (!path) path = strdup(A.font_path);
    char *bold = font_weight_file(path, FC_WEIGHT_BOLD);
    if (!g_term_font_ok) {
        g_term_font_ok = font_init(&g_term_font, path, px) && font_init(&g_term_bold, bold ? bold : path, px);
    } else {
        font_replace(&g_term_font, path);
        font_replace(&g_term_bold, bold ? bold : path);
        font_set_px(&g_term_font, px);
        font_set_px(&g_term_bold, px);
    }
    free(path);
    free(bold);
    g_term_font_scale = A.scale;
    snprintf(g_term_font_family, sizeof g_term_font_family, "%s", g_tc.family);
    // Free the old glyphs' space, but not now: this runs mid-frame and text queued before it would vanish for a
    // frame (a blink). A "full" atlas makes frame() reset it and draw the frame again before presenting.
    g_atlas_full = true;
}

static void term_apply_colors(Term *t) {
    VTermColor fg, bg;
    u32 f = tc_fg(), b = tc_bg();
    vterm_color_rgb(&fg, (f >> 16) & 255, (f >> 8) & 255, f & 255);
    vterm_color_rgb(&bg, (b >> 16) & 255, (b >> 8) & 255, b & 255);
    VTermState *st = vterm_obtain_state(t->vt);
    for (int i = 0; i < 16; i++) {
        VTermColor c;
        u32 p = tc_pal(i);
        vterm_color_rgb(&c, (p >> 16) & 255, (p >> 8) & 255, p & 255);
        vterm_state_set_palette_color(st, i, &c);
    }
    vterm_screen_set_default_colors(t->scr, &fg, &bg);
    t->cursor_shape = g_tc.cursor_shape;
}

static void tline_free(TLine *l) { free(l); }

static TLine *sb_at(Term *t, int i) { return t->sb[(t->sb_head + i) % t->sb_cap]; } // i: 0 = oldest

static void term_cell_from(Term *t, const VTermScreenCell *c, TCell *o) {
    o->ch = c->chars[0];
    o->width = (u8)MAX(0, c->width);
    o->attr = (c->attrs.bold ? 1 : 0) | (c->attrs.italic ? 2 : 0) | (c->attrs.underline ? 4 : 0) | (c->attrs.reverse ? 8 : 0) |
              (c->attrs.strike ? 16 : 0);
    VTermColor fg = c->fg, bg = c->bg;
    if (VTERM_COLOR_IS_DEFAULT_FG(&fg)) o->attr |= 32;
    if (VTERM_COLOR_IS_DEFAULT_BG(&bg)) o->attr |= 64;
    vterm_screen_convert_color_to_rgb(t->scr, &fg);
    vterm_screen_convert_color_to_rgb(t->scr, &bg);
    o->fg = (u32)fg.rgb.red << 16 | (u32)fg.rgb.green << 8 | fg.rgb.blue;
    o->bg = (u32)bg.rgb.red << 16 | (u32)bg.rgb.green << 8 | bg.rgb.blue;
}

static int cb_pushline(int cols, const VTermScreenCell *cells, void *user) {
    Term *t = user;
    int n = cols;
    while (n > 0 && (cells[n - 1].chars[0] == 0 || cells[n - 1].chars[0] == ' ') && VTERM_COLOR_IS_DEFAULT_BG(&cells[n - 1].bg)) n--; // trailing blanks
    TLine *l = xmalloc(sizeof(TLine) + (size_t)n * sizeof(TCell));
    l->n = n;
    for (int i = 0; i < n; i++) term_cell_from(t, &cells[i], &l->c[i]);
    if (t->sb_n == t->sb_cap) { // full: drop the oldest
        tline_free(t->sb[t->sb_head]);
        t->sb[t->sb_head] = l;
        t->sb_head = (t->sb_head + 1) % t->sb_cap;
    } else {
        t->sb[(t->sb_head + t->sb_n) % t->sb_cap] = l;
        t->sb_n++;
    }
    if (t->sb_off) t->sb_off = MIN(t->sb_off + 1, t->sb_n); // reading back: keep the view still
    if (t->sel_on) { t->sel_r0--; t->sel_r1--; }
    return 1;
}

static int cb_popline(int cols, VTermScreenCell *cells, void *user) { // the screen grew: lines come back
    Term *t = user;
    if (!t->sb_n) return 0;
    int last = (t->sb_head + t->sb_n - 1) % t->sb_cap;
    TLine *l = t->sb[last];
    t->sb_n--;
    for (int i = 0; i < cols; i++) {
        memset(&cells[i], 0, sizeof cells[i]);
        cells[i].width = 1;
        if (i < l->n) {
            const TCell *c = &l->c[i];
            cells[i].chars[0] = c->ch;
            cells[i].width = (char)MAX(1, c->width);
            cells[i].attrs.bold = c->attr & 1;
            cells[i].attrs.italic = (c->attr >> 1) & 1;
            cells[i].attrs.underline = (c->attr >> 2) & 1;
            cells[i].attrs.reverse = (c->attr >> 3) & 1;
            cells[i].attrs.strike = (c->attr >> 4) & 1;
            vterm_color_rgb(&cells[i].fg, (c->fg >> 16) & 255, (c->fg >> 8) & 255, c->fg & 255);
            vterm_color_rgb(&cells[i].bg, (c->bg >> 16) & 255, (c->bg >> 8) & 255, c->bg & 255);
            if (c->attr & 32) cells[i].fg.type |= VTERM_COLOR_DEFAULT_FG;
            if (c->attr & 64) cells[i].bg.type |= VTERM_COLOR_DEFAULT_BG;
        } else {
            vterm_color_rgb(&cells[i].fg, 0, 0, 0);
            vterm_color_rgb(&cells[i].bg, 0, 0, 0);
            cells[i].fg.type |= VTERM_COLOR_DEFAULT_FG;
            cells[i].bg.type |= VTERM_COLOR_DEFAULT_BG;
        }
    }
    tline_free(l);
    if (t->sb_off) t->sb_off = MIN(t->sb_off, t->sb_n);
    return 1;
}

static int cb_sb_clear(void *user) {
    Term *t = user;
    for (int i = 0; i < t->sb_n; i++) tline_free(sb_at(t, i));
    t->sb_n = t->sb_head = t->sb_off = 0;
    return 1;
}

static int cb_settermprop(VTermProp prop, VTermValue *val, void *user) {
    Term *t = user;
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE: t->cursor_visible = val->boolean; break;
    case VTERM_PROP_ALTSCREEN: t->altscreen = val->boolean; break;
    case VTERM_PROP_MOUSE: t->mouse_mode = val->number; break;
    case VTERM_PROP_CURSORSHAPE: t->cursor_shape = val->number; break;
    case VTERM_PROP_TITLE: { // arrives in fragments
        if (val->string.initial) t->title_tmp[0] = 0;
        size_t l = strlen(t->title_tmp);
        snprintf(t->title_tmp + l, sizeof t->title_tmp - l, "%.*s", (int)val->string.len, val->string.str);
        if (val->string.final) snprintf(t->title, sizeof t->title, "%s", t->title_tmp);
        break;
    }
    default: break;
    }
    A.redraw = true;
    return 1;
}

static int cb_movecursor(VTermPos pos, VTermPos old, int visible, void *user) {
    Term *t = user;
    t->cursor = pos;
    return 1;
}

static int cb_damage(VTermRect r, void *user) { A.redraw = true; return 1; }

static void term_output(const char *s, size_t len, void *user) { // keys / replies to the program
    Term *t = user;
    while (len && t->fd >= 0) {
        ssize_t w = write(t->fd, s, len);
        if (w < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (w <= 0) break;
        s += w;
        len -= (size_t)w;
    }
}

static const VTermScreenCallbacks g_term_cbs = {
    .damage = cb_damage, .movecursor = cb_movecursor, .settermprop = cb_settermprop,
    .sb_pushline = cb_pushline, .sb_popline = cb_popline, .sb_clear = cb_sb_clear,
};

static void *term_reader(void *arg) {
    Term *t = arg;
    char buf[16384];
    for (;;) {
        struct pollfd pf[2] = {{t->fd, POLLIN, 0}, {t->stop[0], POLLIN, 0}};
        int r = poll(pf, 2, -1);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0 || pf[1].revents) break;
        ssize_t n = read(t->fd, buf, sizeof buf);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        pthread_mutex_lock(&t->mtx);
        if (n <= 0) t->eof = true; // EIO: the program (and everything in the session) has gone
        else {
            if (t->in_n + (size_t)n > t->in_cap) { t->in_cap = (t->in_n + (size_t)n) * 2; t->in = xrealloc(t->in, t->in_cap); }
            memcpy(t->in + t->in_n, buf, (size_t)n);
            t->in_n += (size_t)n;
        }
        pthread_mutex_unlock(&t->mtx);
        SDL_Event ev = {.type = g_wake_event};
        SDL_PushEvent(&ev);
        if (n <= 0) break;
    }
    return NULL;
}

static void term_winsize(Term *t) {
    struct winsize ws = {.ws_row = (unsigned short)t->rows, .ws_col = (unsigned short)t->cols,
                         .ws_xpixel = (unsigned short)(t->cols * t->cw), .ws_ypixel = (unsigned short)(t->rows * t->ch)};
    ioctl(t->fd, TIOCSWINSZ, &ws);
}

// --- shell integration: the terminal panel follows the folder without a visible cd ------------------------------
// The panel's shell loads a few lines of ours after the user's own startup files (bash: --rcfile that runs ~/.bashrc
// first; zsh: ZDOTDIR pointing here, whose files run the user's; fish: -C after its config). To move it, the app
// writes the folder to cd_file and pokes the shell: zsh and fish get SIGUSR1 (they cd and repaint the prompt, keeping
// what's typed); bash runs signal handlers only after Enter, so it gets a private key sequence bound to "save the
// typed line, cd, run an empty command (a fresh prompt), restore the line, remove the old prompt". Other shells, or
// one whose startup files never got to ours, are typed a cd instead (dock_follow).

enum { SHINT_NONE, SHINT_BASH, SHINT_ZSH, SHINT_FISH };

static const char g_shint_bash[] =
    "# File Explorer terminal panel: follows the folder without typing cd. Runs ~/.bashrc first.\n"
    "[ -f ~/.bashrc ] && . ~/.bashrc\n"
    "__fe_cd1() {\n"
    "    __fe_line=$READLINE_LINE __fe_point=$READLINE_POINT\n"
    "    READLINE_LINE= READLINE_POINT=0\n"
    "    local p=${PS1@P}; p=${p//[^$'\\n']/}; __fe_k=$(( ${#p} + 1 ))\n"
    "    builtin cd -- \"$(< \"$FE_CD_FILE\")\" 2>/dev/null\n"
    "}\n"
    "__fe_cd2() {\n"
    "    local p=${PS1@P}; p=${p//[^$'\\n']/}\n"
    "    printf '\\e[%dA\\e[%dM' $(( __fe_k + ${#p} )) \"$__fe_k\"; (( ${#p} )) && printf '\\e[%dB' \"${#p}\"\n"
    "    READLINE_LINE=$__fe_line READLINE_POINT=$__fe_point\n"
    "}\n"
    "for __fe_m in emacs vi-insert vi-command; do\n"
    "    bind -m $__fe_m -x '\"\\e[9872~\": __fe_cd1'\n"
    "    bind -m $__fe_m -x '\"\\e[9873~\": __fe_cd2'\n"
    "    bind -m $__fe_m '\"\\e[9871~\": \"\\e[9872~\\C-m\\e[9873~\"'\n"
    "done\n"
    "unset __fe_m\n"
    ": > \"$FE_CD_FILE.ready\"\n";

static const char g_shint_zshenv[] =
    "# File Explorer terminal panel: the user's own zsh files run as usual (ours: .zshrc here, after theirs)\n"
    "__fe_zdot=$ZDOTDIR\n"
    "ZDOTDIR=${FE_USER_ZDOTDIR:-$HOME}\n"
    "[[ -r $ZDOTDIR/.zshenv ]] && . $ZDOTDIR/.zshenv\n"
    "FE_USER_ZDOTDIR=$ZDOTDIR\n"
    "ZDOTDIR=$__fe_zdot\n"
    "unset __fe_zdot\n";

static const char g_shint_zshrc[] =
    "ZDOTDIR=$FE_USER_ZDOTDIR\n"
    "unset FE_USER_ZDOTDIR\n"
    "[[ -r $ZDOTDIR/.zshrc ]] && . $ZDOTDIR/.zshrc\n"
    "TRAPUSR1() {\n"
    "    builtin cd -- \"$(<$FE_CD_FILE)\" 2>/dev/null || return 0\n"
    "    local f; for f in precmd $precmd_functions; do (( $+functions[$f] )) && $f; done\n"
    "    zle && zle reset-prompt\n"
    "    return 0\n"
    "}\n"
    ": > \"$FE_CD_FILE.ready\"\n";

static const char g_shint_fish[] =
    "# File Explorer terminal panel: follows the folder without typing cd\n"
    "function __fe_cd --on-signal SIGUSR1\n"
    "    builtin cd -- (string collect < $FE_CD_FILE) 2>/dev/null; and commandline -f repaint\n"
    "end\n"
    "true > \"$FE_CD_FILE.ready\"\n";

// Where the scripts and the per-terminal files live (written once per run; private to the user).
static bool shint_dir(char *out, size_t cap) {
    static int done = -1;
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt && rt[0]) snprintf(out, cap, "%s/file-explorer/shell", rt);
    else snprintf(out, cap, "/tmp/file-explorer-%d/shell", (int)getuid());
    if (done >= 0) return done;
    char p[PATH_MAX];
    mkdir_p(out, 0700);
    snprintf(p, sizeof p, "%s/zsh", out);
    mkdir_p(p, 0700);
    bool ok = true;
    snprintf(p, sizeof p, "%s/fe.bash", out);
    ok &= write_file_atomic(p, g_shint_bash);
    snprintf(p, sizeof p, "%s/fe.fish", out);
    ok &= write_file_atomic(p, g_shint_fish);
    snprintf(p, sizeof p, "%s/zsh/.zshenv", out);
    ok &= write_file_atomic(p, g_shint_zshenv);
    snprintf(p, sizeof p, "%s/zsh/.zshrc", out);
    ok &= write_file_atomic(p, g_shint_zshrc);
    done = ok;
    return ok;
}

// Go to dir quietly. 1: done; 0: not possible (type a cd instead); -1: the shell hasn't loaded our part yet (ask again
// later - it gets a few seconds before it's given up on).
static int term_shint_cd(Term *t, const char *dir) {
    if (!t->shint) return 0;
    if (!t->shint_ready) {
        char rp[PATH_MAX + 8];
        snprintf(rp, sizeof rp, "%s.ready", t->cd_file);
        struct stat st;
        t->shint_ready = stat(rp, &st) == 0;
        if (!t->shint_ready) {
            if (now_ns() - t->born < 5000000000ull) return -1;
            t->shint = SHINT_NONE;
            return 0;
        }
    }
    if (!write_file_atomic(t->cd_file, dir)) return 0;
    if (t->shint == SHINT_BASH) term_output("\x1b[9871~", 7, t);
    else kill(t->pid, SIGUSR1);
    return 1;
}

// argv NULL: the user's shell (follow: with the integration above, for the terminal panel). Starts in dir (local).
static Term *term_new(const char *dir, char *const argv[], int rows, int cols, bool follow) {
    if (!g_tc_loaded) tc_load();
    int fd = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd < 0 || grantpt(fd) || unlockpt(fd)) { if (fd >= 0) close(fd); return NULL; }
    char slave[128];
    if (ptsname_r(fd, slave, sizeof slave)) { close(fd); return NULL; }
    Term *t = xcalloc(1, sizeof *t);
    t->fd = fd;
    t->rows = MAX(2, rows);
    t->cols = MAX(2, cols);
    t->cw = t->ch = 1;
    term_winsize(t);
    // the program: own session (the pty becomes its controlling terminal), default signal handling
    // (the app ignores SIGCHLD; a shell must not inherit that), the terminal's environment
    const char *shell = getenv("FE_TERM_SHELL");
    if (!shell || !shell[0]) shell = getenv("SHELL");
    if (!shell || !shell[0]) shell = "/bin/sh";
    char *sh_argv[4] = {(char *)shell, NULL};
    char *const *av = argv ? argv : sh_argv;
    // the terminal panel's shell: with our integration (see above) where we know the shell
    int shint = SHINT_NONE;
    char sdir[PATH_MAX], a1[PATH_MAX + 16], cdf[PATH_MAX], e_cdf[PATH_MAX + 16], e_zdot[PATH_MAX + 16], e_uzdot[PATH_MAX + 32];
    static unsigned shint_n;
    if (follow && !argv && shint_dir(sdir, sizeof sdir)) {
        const char *b = path_basename(shell);
        shint = !strcmp(b, "bash") ? SHINT_BASH : !strcmp(b, "zsh") ? SHINT_ZSH : !strcmp(b, "fish") ? SHINT_FISH : SHINT_NONE;
        snprintf(cdf, sizeof cdf, "%s/cd-%d-%u", sdir, (int)getpid(), ++shint_n);
        snprintf(e_cdf, sizeof e_cdf, "FE_CD_FILE=%s", cdf);
        if (shint == SHINT_BASH) {
            snprintf(a1, sizeof a1, "%s/fe.bash", sdir);
            sh_argv[1] = "--rcfile";
            sh_argv[2] = a1;
        } else if (shint == SHINT_FISH) {
            char q[PATH_MAX + 8];
            sh_quote(q, sizeof q, sdir);
            snprintf(a1, sizeof a1, "source %s/fe.fish", q);
            sh_argv[1] = "-C";
            sh_argv[2] = a1;
        } else if (shint == SHINT_ZSH) {
            const char *uz = getenv("ZDOTDIR");
            snprintf(e_zdot, sizeof e_zdot, "ZDOTDIR=%s/zsh", sdir);
            snprintf(e_uzdot, sizeof e_uzdot, "FE_USER_ZDOTDIR=%s", uz ? uz : "");
        }
    }
    int nenv = 0;
    while (environ[nenv]) nenv++;
    char **envp = xmalloc((size_t)(nenv + 12) * sizeof(char *));
    int ne = 0;
    for (int i = 0; i < nenv; i++)
        if (strncmp(environ[i], "TERM=", 5) && strncmp(environ[i], "COLORTERM=", 10) && strncmp(environ[i], "TERM_PROGRAM=", 13) &&
            !(shint == SHINT_ZSH && !strncmp(environ[i], "ZDOTDIR=", 8)))
            envp[ne++] = environ[i];
    if (shint) envp[ne++] = e_cdf;
    if (shint == SHINT_ZSH) { envp[ne++] = e_zdot; envp[ne++] = e_uzdot; }
    char e_term[64], e_ct[] = "COLORTERM=truecolor", e_tp[] = "TERM_PROGRAM=file-explorer";
    snprintf(e_term, sizeof e_term, "TERM=%s", g_tc.term[0] ? g_tc.term : "xterm-256color");
    envp[ne++] = e_term;
    envp[ne++] = e_ct;
    envp[ne++] = e_tp;
    envp[ne] = NULL;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, slave, O_RDWR, 0);
    posix_spawn_file_actions_adddup2(&fa, 0, 1);
    posix_spawn_file_actions_adddup2(&fa, 0, 2);
    if (dir && dir[0]) posix_spawn_file_actions_addchdir_np(&fa, dir);
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    sigset_t all, none;
    sigfillset(&all);
    sigemptyset(&none);
    posix_spawnattr_setsigdefault(&at, &all);
    posix_spawnattr_setsigmask(&at, &none);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    int rc = posix_spawnp(&t->pid, av[0], &fa, &at, av, envp);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    free(envp);
    if (rc) { close(fd); free(t); toast("Couldn't start %s: %s", av[0], strerror(rc)); return NULL; }
    snprintf(t->start_label, sizeof t->start_label, "%s", path_basename(av[0]));
    t->shint = shint;
    if (shint) snprintf(t->cd_file, sizeof t->cd_file, "%s", cdf);
    if (argv && !strcmp(path_basename(av[0]), "ssh")) // "ssh host"
        for (int i = 1; argv[i]; i++)
            if (argv[i][0] != '-' && (i == 1 || strcmp(argv[i - 1], "-p"))) { snprintf(t->start_label, sizeof t->start_label, "ssh %s", argv[i]); break; }
    t->vt = vterm_new(t->rows, t->cols);
    vterm_set_utf8(t->vt, 1);
    vterm_output_set_callback(t->vt, term_output, t);
    t->scr = vterm_obtain_screen(t->vt);
    vterm_screen_set_callbacks(t->scr, &g_term_cbs, t);
    vterm_screen_enable_altscreen(t->scr, 1);
    vterm_screen_enable_reflow(t->scr, true);
    vterm_screen_reset(t->scr, 1);
    t->cursor_visible = true;
    t->born = now_ns();
    t->sb_cap = g_tc.scrollback;
    t->sb = xcalloc((size_t)t->sb_cap, sizeof(TLine *));
    term_apply_colors(t);
    pthread_mutex_init(&t->mtx, NULL);
    if (pipe2(t->stop, O_CLOEXEC)) t->stop[0] = t->stop[1] = -1;
    pthread_create(&t->thr, NULL, term_reader, t);
    return t;
}

static void term_free(Term *t) {
    if (!t) return;
    if (t->pid > 0) kill(-t->pid, SIGHUP); // the whole session: shell and what runs in it
    if (t->stop[1] >= 0) { ssize_t w = write(t->stop[1], "x", 1); (void)w; }
    pthread_join(t->thr, NULL);
    if (t->stop[0] >= 0) close(t->stop[0]);
    if (t->stop[1] >= 0) close(t->stop[1]);
    close(t->fd);
    vterm_free(t->vt);
    for (int i = 0; i < t->sb_n; i++) tline_free(sb_at(t, i));
    free(t->sb);
    free(t->in);
    pthread_mutex_destroy(&t->mtx);
    if (t->cd_file[0]) {
        char rp[PATH_MAX + 8];
        snprintf(rp, sizeof rp, "%s.ready", t->cd_file);
        unlink(t->cd_file);
        unlink(rp);
    }
    free(t);
}

// Feed what the program wrote; returns false once it has exited.
static bool term_pump(Term *t) {
    pthread_mutex_lock(&t->mtx);
    char *data = t->in;
    size_t n = t->in_n;
    bool eof = t->eof;
    t->in = NULL;
    t->in_n = t->in_cap = 0;
    pthread_mutex_unlock(&t->mtx);
    if (n) {
        t->last_out = now_ns();
        vterm_input_write(t->vt, data, n);
        vterm_screen_flush_damage(t->scr);
        A.redraw = true;
    }
    free(data);
    if (eof) t->dead = true;
    return !t->dead;
}

static void term_resize(Term *t, int rows, int cols) {
    rows = MAX(2, rows);
    cols = MAX(2, cols);
    if (rows == t->rows && cols == t->cols) return;
    t->rows = rows;
    t->cols = cols;
    vterm_set_size(t->vt, rows, cols);
    vterm_screen_flush_damage(t->scr);
    term_winsize(t);
    t->sel_on = false;
}

// A cell at absolute row r (scrollback < 0 <= screen).
static bool term_cell(Term *t, int r, int c, TCell *o) {
    if (r >= 0) {
        if (r >= t->rows || c >= t->cols) return false;
        VTermScreenCell cell;
        if (!vterm_screen_get_cell(t->scr, (VTermPos){r, c}, &cell)) return false;
        term_cell_from(t, &cell, o);
        return true;
    }
    int i = t->sb_n + r;
    if (i < 0) return false;
    TLine *l = sb_at(t, i);
    if (c >= l->n) { *o = (TCell){0, tc_fg(), tc_bg(), 32 | 64, 1}; return true; }
    *o = l->c[c];
    return true;
}

// Label for the tab: the program's title, else the folder it's in, else what was started.
static const char *term_label(Term *t) {
    if (t->title[0]) return t->title;
    u64 now = now_ns();
    if (now - t->label_t > 1000000000ull) {
        t->label_t = now;
        t->cwd_label[0] = 0;
        pid_t pg = tcgetpgrp(t->fd);
        char lp[64], target[PATH_MAX];
        snprintf(lp, sizeof lp, "/proc/%d/cwd", (int)(pg > 0 ? pg : t->pid));
        ssize_t k = readlink(lp, target, sizeof target - 1);
        if (k > 0) {
            target[k] = 0;
            const char *home = getenv("HOME");
            snprintf(t->cwd_label, sizeof t->cwd_label, "%s", home && !strcmp(target, home) ? "~" : target[1] ? path_basename(target) : "/");
        }
    }
    if (!strncmp(t->start_label, "ssh ", 4)) return t->start_label;
    return t->cwd_label[0] ? t->cwd_label : t->start_label;
}

// Is the shell waiting at its prompt? (Nothing else in the foreground, no full-screen program.)
static bool term_idle(Term *t) {
    if (t->dead || t->altscreen) return false;
    pid_t pg = tcgetpgrp(t->fd);
    return pg > 0 && pg == t->pid;
}

// The shell's current folder (symlinks resolved).
static bool term_cwd(Term *t, char *out, size_t cap) {
    char lp[64];
    snprintf(lp, sizeof lp, "/proc/%d/cwd", (int)t->pid);
    ssize_t k = readlink(lp, out, cap - 1);
    if (k <= 0) return false;
    out[k] = 0;
    return true;
}

// --- drawing ----------------------------------------------------------------------------------------------

static bool term_in_sel(Term *t, int r, int c) {
    if (!t->sel_on) return false;
    int r0 = t->sel_r0, c0 = t->sel_c0, r1 = t->sel_r1, c1 = t->sel_c1;
    if (r0 > r1 || (r0 == r1 && c0 > c1)) { int tr = r0, tc = c0; r0 = r1; c0 = c1; r1 = tr; c1 = tc; }
    if (r < r0 || r > r1) return false;
    if (r == r0 && c < c0) return false;
    if (r == r1 && c > c1) return false;
    return true;
}

static void term_metrics(f32 *cw, f32 *ch) {
    Glyph *g = font_glyph(&g_term_font, 'M');
    *cw = MAX(1.f, roundf(g->adv));
    f32 lh = g_tc.line_pt > 0 ? roundf(g_tc.line_pt * 96.f / 72.f * A.scale) : roundf(g_term_font.line_h);
    *ch = MAX(2.f, lh);
}

static void term_layout(Term *t, Rect area) {
    term_fonts();
    term_metrics(&t->cw, &t->ch);
    f32 px = roundf(g_tc.padx * A.scale), py = roundf(g_tc.pady * A.scale);
    t->area = area;
    t->ox = area.x + px;
    t->oy = area.y + py;
    int cols = (int)((area.w - px * 2) / t->cw), rows = (int)((area.h - py * 2) / t->ch);
    term_resize(t, rows, cols);
}

static void term_draw(Term *t, Rect area, bool focused) {
    term_layout(t, area);
    u32 dbg = tc_bg();
    draw_rect(area.x, area.y, area.w, area.h, C(dbg));
    ClipSave cs = draw_clip_push((SDL_Rect){(int)area.x, (int)area.y, (int)area.w, (int)area.h});
    f32 base = roundf((t->ch - (g_term_font.ascent + g_term_font.descent)) * 0.5f + g_term_font.ascent + g_tc.yoff_px * A.scale);
    u32 sel_bg = g_tc.has_sel_bg ? g_tc.sel_bg : rgb_mix(dbg, COL_ACCENT, 0.45f);
    for (int vr = 0; vr < t->rows; vr++) {
        int r = vr - t->sb_off;
        f32 y = t->oy + vr * t->ch;
        // backgrounds (runs of one colour), then text
        for (int pass = 0; pass < 2; pass++) {
            for (int c = 0; c < t->cols; c++) {
                TCell cell;
                if (!term_cell(t, r, c, &cell)) break;
                bool sel = term_in_sel(t, r, c);
                u32 fg = cell.fg, bg = cell.bg;
                bool bgdef = cell.attr & 64;
                if (cell.attr & 8) { u32 tmp = fg; fg = bg; bg = tmp; bgdef = false; }
                if (sel) { bg = sel_bg; bgdef = false; if (g_tc.has_sel_fg) fg = g_tc.sel_fg; }
                f32 x = t->ox + c * t->cw, w = t->cw * MAX(1, cell.width);
                if (pass == 0) {
                    if (!bgdef) draw_rect(x, y, w, t->ch, C(bg));
                    continue;
                }
                if (cell.ch && cell.ch != (u32)-1 && cell.ch != ' ') {
                    Font *f = cell.attr & 1 ? &g_term_bold : &g_term_font;
                    Glyph *g = font_glyph(f, cell.ch);
                    if (g->w) draw_glyph(g, roundf(x) + g->bx, roundf(y + base) - g->by, g->color ? (Color){1, 1, 1, 1} : C(fg));
                }
                if (cell.attr & 4) draw_rect(x, roundf(y + base + MAX(1.f, S(1.5f))), w, MAX(1.f, roundf(S(1))), C(fg));
                if (cell.attr & 16) draw_rect(x, roundf(y + t->ch * 0.55f), w, MAX(1.f, roundf(S(1))), C(fg));
                if (cell.width > 1) c += cell.width - 1;
            }
        }
    }
    // cursor (live view only); hollow when the panel isn't focused
    if (t->sb_off == 0 && t->cursor_visible && t->cursor.row < t->rows && t->cursor.col < t->cols) {
        f32 x = t->ox + t->cursor.col * t->cw, y = t->oy + t->cursor.row * t->ch;
        u32 cc = g_tc.has_cur ? g_tc.cur : tc_fg();
        f32 th = MAX(1.f, roundf(S(1.5f)));
        if (!focused) draw_rrect_outline(x, y, t->cw, t->ch, 0, MAX(1.f, roundf(S(1))), C(cc));
        else if (t->cursor_shape == VTERM_PROP_CURSORSHAPE_BAR_LEFT) draw_rect(x, y, th, t->ch, C(cc));
        else if (t->cursor_shape == VTERM_PROP_CURSORSHAPE_UNDERLINE) draw_rect(x, y + t->ch - th, t->cw, th, C(cc));
        else {
            draw_rect(x, y, t->cw, t->ch, C(cc));
            TCell cell;
            if (term_cell(t, t->cursor.row, t->cursor.col, &cell) && cell.ch && cell.ch != ' ' && cell.ch != (u32)-1) {
                Glyph *g = font_glyph(cell.attr & 1 ? &g_term_bold : &g_term_font, cell.ch);
                u32 tc = g_tc.has_cur_text ? g_tc.cur_text : dbg;
                if (g->w) draw_glyph(g, roundf(x) + g->bx, roundf(y + base) - g->by, C(tc));
            }
        }
    }
    if (t->sb_off > 0) { // scrolled back: a small hint
        char m[48];
        snprintf(m, sizeof m, "\xE2\x86\x91 %d", t->sb_off);
        Font *fs = &A.font_small;
        f32 tw = text_width(fs, m, (int)strlen(m)) + S(16), hh = roundf(S(22));
        Rect b = {area.x + area.w - tw - S(10) - t->hint_rpad, area.y + S(8), tw, hh};
        draw_rrect(b.x, b.y, b.w, b.h, hh * 0.5f, C(COL_POPUP));
        draw_text(fs, b.x + S(8), text_y(fs, b.y, b.h), m, (int)strlen(m), C(COL_TEXT_DIM));
    }
    draw_clip_pop(cs);
}

// --- input ------------------------------------------------------------------------------------------------

static void term_to_bottom(Term *t) { if (t->sb_off) { t->sb_off = 0; A.redraw = true; } }

// Type s into the terminal as if from the keyboard (raw bytes, no bracketed paste).
static void term_send(Term *t, const char *s) {
    term_to_bottom(t);
    term_output(s, strlen(s), t);
}

static VTermModifier term_mod(SDL_Keymod m) {
    int r = 0;
    if (m & SDL_KMOD_SHIFT) r |= VTERM_MOD_SHIFT;
    if (m & SDL_KMOD_ALT) r |= VTERM_MOD_ALT;
    if (m & SDL_KMOD_CTRL) r |= VTERM_MOD_CTRL;
    return (VTermModifier)r;
}

static char *term_selection_text(Term *t) {
    if (!t->sel_on) return NULL;
    int r0 = t->sel_r0, c0 = t->sel_c0, r1 = t->sel_r1, c1 = t->sel_c1;
    if (r0 > r1 || (r0 == r1 && c0 > c1)) { int tr = r0, tc = c0; r0 = r1; c0 = c1; r1 = tr; c1 = tc; }
    size_t cap = 256, n = 0;
    char *out = xmalloc(cap);
    for (int r = r0; r <= r1; r++) {
        size_t line_start = n;
        for (int c = r == r0 ? c0 : 0; c < (r == r1 ? c1 + 1 : t->cols); c++) {
            TCell cell;
            if (!term_cell(t, r, c, &cell)) break;
            if (cell.ch == (u32)-1) continue;
            u32 cp = cell.ch ? cell.ch : ' ';
            if (n + 8 > cap) { cap *= 2; out = xrealloc(out, cap); }
            n += (size_t)utf8_encode(cp, out + n);
            if (cell.width > 1) c += cell.width - 1;
        }
        while (n > line_start && out[n - 1] == ' ') n--; // trailing blanks
        if (r != r1) { if (n + 2 > cap) { cap *= 2; out = xrealloc(out, cap); } out[n++] = '\n'; }
    }
    out[n] = 0;
    return out;
}

static void term_copy(Term *t) {
    char *s = term_selection_text(t);
    if (s && s[0]) { SDL_SetClipboardText(s); toast("Copied"); }
    free(s);
}

static void term_paste_text(Term *t, const char *s) {
    if (!s || !s[0]) return;
    term_to_bottom(t);
    vterm_keyboard_start_paste(t->vt); // bracketed paste when the program asked for it
    const char *p = s, *end = s + strlen(s);
    while (p < end) {
        u32 cp = utf8_decode(&p, end);
        if (cp == '\n') cp = '\r';
        vterm_keyboard_unichar(t->vt, cp, VTERM_MOD_NONE);
    }
    vterm_keyboard_end_paste(t->vt);
}

static void term_paste(Term *t) {
    char *s = SDL_GetClipboardText();
    term_paste_text(t, s);
    SDL_free(s);
}

// Keys for a focused terminal. Returns false for the few the app keeps (tab switching...).
static bool term_key(Term *t, SDL_Keycode k, SDL_Keymod mod) {
    bool ctrl = mod & SDL_KMOD_CTRL, shift = mod & SDL_KMOD_SHIFT, alt = mod & SDL_KMOD_ALT;
    if (ctrl && shift && k == SDLK_C) { term_copy(t); return true; }
    if (ctrl && shift && k == SDLK_V) { term_paste(t); return true; }
    if (ctrl && !shift && k == SDLK_INSERT) { term_copy(t); return true; }
    if (shift && !ctrl && k == SDLK_INSERT) { term_paste(t); return true; }
    if (shift && !ctrl && (k == SDLK_PAGEUP || k == SDLK_PAGEDOWN)) {
        int d = MAX(1, t->rows / 2);
        t->sb_off = CLAMP(t->sb_off + (k == SDLK_PAGEUP ? d : -d), 0, t->sb_n);
        A.redraw = true;
        return true;
    }
    if (shift && !ctrl && (k == SDLK_HOME || k == SDLK_END) && !t->altscreen) {
        t->sb_off = k == SDLK_HOME ? t->sb_n : 0;
        A.redraw = true;
        return true;
    }
    VTermModifier vm = term_mod(mod);
    VTermKey vk = VTERM_KEY_NONE;
    switch (k) {
    case SDLK_RETURN: case SDLK_KP_ENTER: vk = VTERM_KEY_ENTER; break;
    case SDLK_TAB: vk = VTERM_KEY_TAB; break;
    case SDLK_BACKSPACE: vk = VTERM_KEY_BACKSPACE; break;
    case SDLK_ESCAPE: vk = VTERM_KEY_ESCAPE; break;
    case SDLK_UP: vk = VTERM_KEY_UP; break;
    case SDLK_DOWN: vk = VTERM_KEY_DOWN; break;
    case SDLK_LEFT: vk = VTERM_KEY_LEFT; break;
    case SDLK_RIGHT: vk = VTERM_KEY_RIGHT; break;
    case SDLK_INSERT: vk = VTERM_KEY_INS; break;
    case SDLK_DELETE: vk = VTERM_KEY_DEL; break;
    case SDLK_HOME: vk = VTERM_KEY_HOME; break;
    case SDLK_END: vk = VTERM_KEY_END; break;
    case SDLK_PAGEUP: vk = VTERM_KEY_PAGEUP; break;
    case SDLK_PAGEDOWN: vk = VTERM_KEY_PAGEDOWN; break;
    default:
        if (k >= SDLK_F1 && k <= SDLK_F12) vk = (VTermKey)VTERM_KEY_FUNCTION((int)(k - SDLK_F1) + 1);
        break;
    }
    if (vk != VTERM_KEY_NONE) {
        term_to_bottom(t);
        vterm_keyboard_key(t->vt, vk, vm);
        return true;
    }
    // Ctrl / Alt + a character: no text event comes for Ctrl (and Alt's must not double it)
    if ((ctrl || alt) && k < 128 && k >= 32) {
        u32 cp = (u32)k;
        if (shift && cp >= 'a' && cp <= 'z') cp -= 32;
        term_to_bottom(t);
        vterm_keyboard_unichar(t->vt, cp, (VTermModifier)(vm & ~VTERM_MOD_SHIFT));
        t->skip_text = cp;
        return true;
    }
    return !(mod & SDL_KMOD_GUI); // everything else belongs to the terminal (text arrives as text events)
}

static void term_text(Term *t, const char *s) {
    const char *p = s, *end = s + strlen(s);
    term_to_bottom(t);
    while (p < end) {
        u32 cp = utf8_decode(&p, end);
        if (t->skip_text && (cp == t->skip_text || cp == (u32)tolower((int)t->skip_text))) { t->skip_text = 0; continue; }
        vterm_keyboard_unichar(t->vt, cp, VTERM_MOD_NONE);
    }
    t->skip_text = 0;
}

static void term_pos(Term *t, f32 x, f32 y, int *r, int *c) {
    *c = CLAMP((int)((x - t->ox) / t->cw), 0, t->cols - 1);
    *r = CLAMP((int)((y - t->oy) / t->ch), 0, t->rows - 1) - t->sb_off;
}

static bool term_word_ch(u32 c) { return c > ' ' && !strchr(" \t\"'`()[]{}<>|;,", (int)(c < 128 ? c : 'a')); }

static void term_mouse_down(Term *t, int button, int clicks, f32 x, f32 y, SDL_Keymod mod) {
    int r, c;
    term_pos(t, x, y, &r, &c);
    if (t->mouse_mode && !(mod & SDL_KMOD_SHIFT) && r >= 0) { // the program wants the mouse (Shift: select anyway)
        vterm_mouse_move(t->vt, r, c, term_mod(mod));
        vterm_mouse_button(t->vt, button == SDL_BUTTON_LEFT ? 1 : button == SDL_BUTTON_MIDDLE ? 2 : 3, true, term_mod(mod));
        return;
    }
    if (button != SDL_BUTTON_LEFT) return;
    t->sel_on = clicks >= 2;
    t->selecting = true;
    t->sel_r0 = t->sel_r1 = r;
    t->sel_c0 = t->sel_c1 = c;
    if (clicks == 2) { // a word
        TCell cell;
        while (t->sel_c0 > 0 && term_cell(t, r, t->sel_c0 - 1, &cell) && term_word_ch(cell.ch)) t->sel_c0--;
        while (t->sel_c1 < t->cols - 1 && term_cell(t, r, t->sel_c1 + 1, &cell) && term_word_ch(cell.ch)) t->sel_c1++;
    } else if (clicks >= 3) { t->sel_c0 = 0; t->sel_c1 = t->cols - 1; } // the line
    A.redraw = true;
}

static void term_mouse_move(Term *t, f32 x, f32 y, bool held) {
    int r, c;
    term_pos(t, x, y, &r, &c);
    if (t->mouse_mode >= VTERM_PROP_MOUSE_DRAG && !t->selecting && r >= 0) { vterm_mouse_move(t->vt, r, c, VTERM_MOD_NONE); return; }
    if (!t->selecting || !held) return;
    if (r != t->sel_r1 || c != t->sel_c1) { t->sel_r1 = r; t->sel_c1 = c; t->sel_on = true; A.redraw = true; }
}

static void term_mouse_up(Term *t, int button) {
    if (t->mouse_mode && !t->selecting) {
        vterm_mouse_button(t->vt, button == SDL_BUTTON_LEFT ? 1 : button == SDL_BUTTON_MIDDLE ? 2 : 3, false, VTERM_MOD_NONE);
        return;
    }
    if (t->selecting) {
        t->selecting = false;
        char *s = term_selection_text(t);
        if (s && s[0]) SDL_SetPrimarySelectionText(s);
        free(s);
    }
}

static void term_wheel(Term *t, f32 dy) {
    int steps = (int)roundf(fabsf(dy) * g_tc.wheel_lines);
    if (!steps) steps = 1;
    if (t->mouse_mode) { // the program scrolls
        for (int i = 0; i < MAX(1, (int)fabsf(dy)); i++) { vterm_mouse_button(t->vt, dy > 0 ? 4 : 5, true, VTERM_MOD_NONE); vterm_mouse_button(t->vt, dy > 0 ? 4 : 5, false, VTERM_MOD_NONE); }
    } else if (t->altscreen) { // full-screen program without mouse: arrows, like other terminals
        for (int i = 0; i < MAX(1, (int)fabsf(dy) * 3); i++) vterm_keyboard_key(t->vt, dy > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
    } else {
        t->sb_off = CLAMP(t->sb_off + (dy > 0 ? steps : -steps), 0, t->sb_n);
        A.redraw = true;
    }
}

static void term_clear_scrollback(Term *t) {
    cb_sb_clear(t);
    t->sel_on = false;
    A.redraw = true;
}

// Every loop: feed the terminals what their programs wrote, close tabs whose program ended, and follow the
// terminal config when it changes (an edit, Omarchy switching themes).
static void term_tick(void) {
    bool any = false;
    for (int i = 0; i < A.ntabs; i++) {
        Tab *tb = A.tabs[i];
        any |= tb->dock != NULL;
        if (!tb->term) continue;
        any = true;
        if (!term_pump(tb->term)) { tab_close(i); i--; }
    }
    dock_tick();
    if (!any || now_ns() - g_tc_checked < 1000000000ull) return;
    g_tc_checked = now_ns();
    if (!tc_changed()) return;
    tc_reload_all();
}

static void tc_reload_all(void) {
    tc_load();
    for (int i = 0; i < A.ntabs; i++)
        if (A.tabs[i]->term) term_apply_colors(A.tabs[i]->term);
    for (int i = 0; i < A.ntabs; i++)
        if (A.tabs[i]->dock) term_apply_colors(A.tabs[i]->dock);
    A.redraw = true;
}

static void draw_status_term(Term *t) {
    Rect s = A.r_status;
    draw_rect(s.x, s.y, s.w, s.h, C(COL_TOOLBAR));
    draw_rect(s.x, s.y, s.w, 1, C(COL_BORDER));
    Font *f = &A.font_small;
    char left[300], right[96];
    snprintf(left, sizeof left, "Terminal \xC2\xB7 %s", term_label(t));
    snprintf(right, sizeof right, "%d\xC3\x97%d%s%s", t->cols, t->rows, g_tc.source[0] ? " \xC2\xB7 " : "", g_tc.source[0] ? g_tc.source : "");
    f32 ty = text_y(f, s.y, s.h);
    draw_text_fit(f, s.x + S(14), ty, s.w * 0.6f, left, (int)strlen(left), C(COL_TEXT_DIM), 0);
    f32 rw = text_width(f, right, (int)strlen(right));
    draw_text(f, s.x + s.w - S(60) - rw, ty, right, (int)strlen(right), C(COL_TEXT_FAINT));
    // the gear (Options) stays where it always is
    f32 gs = roundf(S(22));
    A.r_gear = (Rect){roundf(s.x + s.w - gs - S(4)), roundf(s.y + (s.h - gs) * 0.5f), gs, gs};
    if (hot(A.r_gear)) draw_rrect(A.r_gear.x, A.r_gear.y, gs, gs, S(5), C(COL_CHIP));
    draw_gear_icon(A.r_gear.x + gs * 0.5f, A.r_gear.y + gs * 0.5f, S(7), C(COL_TEXT_DIM), C(COL_TOOLBAR));
}

static bool term_wants_mouse(Term *t, SDL_Keymod mod) { return t->mouse_mode && !(mod & SDL_KMOD_SHIFT); }
static bool term_has_sel(Term *t) { return t->sel_on; }

static u32 term_bg_color(void) { if (!g_tc_loaded) tc_load(); return tc_bg(); }
static u32 term_fg_color(void) { if (!g_tc_loaded) tc_load(); return tc_fg(); }
static bool term_settled(Term *t) { // output has started and been quiet for a bit (or nothing for 3 s)
    u64 now = now_ns();
    return (t->last_out && now - t->last_out > 400000000ull) || now - t->born > 3000000000ull;
}
