// theme.c - colour palette: built-in themes, accent colour, JSON colour schemes

typedef struct {
    u32 bg, sidebar, tabbar, toolbar, field, field_hover, border, hover, sel, sel_focus, sel_idle;
    u32 text, text_dim, text_faint, text2, text_strong, text_hidden;
    u32 accent, accent_hover, on_accent, folder, folder_tab;
    u32 sel_text, sel_dim, sel_hl;
    u32 popup, popup_border, control, control_hover, control_off, chip, tab_hover, sunken, divider, track;
    u32 scroll, scroll_hover, knob_off, alt_row, group_text, danger, danger_text, warn, ok, preview_bg;
} Theme;

static const struct { const char *key; size_t off; } g_theme_keys[] = {
#define TK(f) {#f, offsetof(Theme, f)}
    TK(bg), TK(sidebar), TK(tabbar), TK(toolbar), TK(field), TK(field_hover), TK(border), TK(hover), TK(sel),
    TK(sel_focus), TK(sel_idle), TK(text), TK(text_dim), TK(text_faint), TK(text2), TK(text_strong), TK(text_hidden),
    TK(accent), TK(accent_hover), TK(on_accent), TK(folder), TK(folder_tab), TK(sel_text), TK(sel_dim), TK(sel_hl),
    TK(popup), TK(popup_border), TK(control), TK(control_hover), TK(control_off), TK(chip), TK(tab_hover), TK(sunken),
    TK(divider), TK(track), TK(scroll), TK(scroll_hover), TK(knob_off), TK(alt_row), TK(group_text), TK(danger),
    TK(danger_text), TK(warn), TK(ok), TK(preview_bg),
#undef TK
};

static const Theme g_theme_dark = {
    .bg = 0x1B1B20, .sidebar = 0x161619, .tabbar = 0x131316, .toolbar = 0x1F1F25, .field = 0x2A2A31,
    .field_hover = 0x303038, .border = 0x2B2B32, .hover = 0x26262D, .sel = 0x24436E, .sel_focus = 0x2C5A97,
    .sel_idle = 0x30343E, .text = 0xE6E6EA, .text_dim = 0x9A9CA5, .text_faint = 0x6A6C75, .text2 = 0xC9CBD2,
    .text_strong = 0xFFFFFF, .text_hidden = 0x8E9099, .accent = 0x4C9BFF, .accent_hover = 0x5AA6FF,
    .on_accent = 0xFFFFFF, .folder = 0xE9B44C, .folder_tab = 0xC9952F, .sel_text = 0xFFFFFF, .sel_dim = 0xC4CFE0,
    .sel_hl = 0xFFE08A, .popup = 0x26262D, .popup_border = 0x383842, .control = 0x34343D, .control_hover = 0x3E3E48,
    .control_off = 0x2C2C33, .chip = 0x3A3A44, .tab_hover = 0x1E1E24, .sunken = 0x121215, .divider = 0x0E0E11,
    .track = 0x2E2E36, .scroll = 0x3E3F48, .scroll_hover = 0x5A5B66, .knob_off = 0xB8BAC2, .alt_row = 0x1F1F25,
    .group_text = 0xCFE2FF, .danger = 0xF0627C, .danger_text = 0xF59AA9, .warn = 0xF2A33A, .ok = 0x4FD18B,
    .preview_bg = 0x18181C,
};

static const Theme g_theme_light = {
    .bg = 0xFBFBFC, .sidebar = 0xF0F1F4, .tabbar = 0xE4E5EA, .toolbar = 0xF6F6F8, .field = 0xE9EAEE,
    .field_hover = 0xE0E2E7, .border = 0xDCDDE2, .hover = 0xEDEFF3, .sel = 0xCCE1FB, .sel_focus = 0xB5D3FA,
    .sel_idle = 0xE0E3E9, .text = 0x1D1E22, .text_dim = 0x5D6068, .text_faint = 0x8E9199, .text2 = 0x3A3C42,
    .text_strong = 0x000000, .text_hidden = 0x8A8D95, .accent = 0x2F7BE5, .accent_hover = 0x4C8FF0,
    .on_accent = 0xFFFFFF, .folder = 0xE3A73B, .folder_tab = 0xC48A22, .sel_text = 0x0B1B33, .sel_dim = 0x3B4B63,
    .sel_hl = 0x9A5B00, .popup = 0xFFFFFF, .popup_border = 0xD3D5DB, .control = 0xE6E7EB, .control_hover = 0xDADCE1,
    .control_off = 0xEFEFF2, .chip = 0xDADCE1, .tab_hover = 0xEDEEF1, .sunken = 0xF1F2F4, .divider = 0xCFD1D7,
    .track = 0xDDDFE4, .scroll = 0xC4C7CE, .scroll_hover = 0xA6AAB3, .knob_off = 0xFFFFFF, .alt_row = 0xF3F4F6,
    .group_text = 0x1F4E8C, .danger = 0xD93A55, .danger_text = 0xC62E48, .warn = 0xD98A1C, .ok = 0x23A867,
    .preview_bg = 0xF5F6F8,
};

static const Theme g_theme_black = {
    .bg = 0x000000, .sidebar = 0x0A0A0B, .tabbar = 0x000000, .toolbar = 0x0E0E10, .field = 0x1A1A1E,
    .field_hover = 0x222227, .border = 0x1E1E22, .hover = 0x16161A, .sel = 0x1D3A63, .sel_focus = 0x24508C,
    .sel_idle = 0x24272F, .text = 0xE6E6EA, .text_dim = 0x9A9CA5, .text_faint = 0x63656D, .text2 = 0xC9CBD2,
    .text_strong = 0xFFFFFF, .text_hidden = 0x86888F, .accent = 0x4C9BFF, .accent_hover = 0x5AA6FF,
    .on_accent = 0xFFFFFF, .folder = 0xE9B44C, .folder_tab = 0xC9952F, .sel_text = 0xFFFFFF, .sel_dim = 0xC4CFE0,
    .sel_hl = 0xFFE08A, .popup = 0x141417, .popup_border = 0x2A2A30, .control = 0x26262C, .control_hover = 0x303037,
    .control_off = 0x1C1C20, .chip = 0x2C2C33, .tab_hover = 0x111114, .sunken = 0x050506, .divider = 0x000000,
    .track = 0x222227, .scroll = 0x2E2F36, .scroll_hover = 0x4A4B55, .knob_off = 0xB8BAC2, .alt_row = 0x0B0B0D,
    .group_text = 0xCFE2FF, .danger = 0xF0627C, .danger_text = 0xF59AA9, .warn = 0xF2A33A, .ok = 0x4FD18B,
    .preview_bg = 0x060607,
};

static const Theme g_theme_monokai = {
    .bg = 0x272822, .sidebar = 0x1E1F1C, .tabbar = 0x1A1B17, .toolbar = 0x2D2E27, .field = 0x3E3D32,
    .field_hover = 0x49483E, .border = 0x3B3C35, .hover = 0x34352D, .sel = 0x49483E, .sel_focus = 0x5E5C4A,
    .sel_idle = 0x3E3D32, .text = 0xF8F8F2, .text_dim = 0xA59F85, .text_faint = 0x75715E, .text2 = 0xD6D3C2,
    .text_strong = 0xFFFFFF, .text_hidden = 0x8F8B76, .accent = 0x66D9EF, .accent_hover = 0x8BE4F4,
    .on_accent = 0x1E1F1C, .folder = 0xE6DB74, .folder_tab = 0xC4B94F, .sel_text = 0xFFFFFF, .sel_dim = 0xD6D3C2,
    .sel_hl = 0xE6DB74, .popup = 0x2F302A, .popup_border = 0x49483E, .control = 0x3E3D32, .control_hover = 0x49483E,
    .control_off = 0x34352D, .chip = 0x49483E, .tab_hover = 0x23241F, .sunken = 0x1E1F1C, .divider = 0x161713,
    .track = 0x3E3D32, .scroll = 0x49483E, .scroll_hover = 0x75715E, .knob_off = 0xD6D3C2, .alt_row = 0x2B2C26,
    .group_text = 0xA6E22E, .danger = 0xF92672, .danger_text = 0xFF6E9C, .warn = 0xFD971F, .ok = 0xA6E22E,
    .preview_bg = 0x22231E,
};

static const Theme g_theme_nord = {
    .bg = 0x2E3440, .sidebar = 0x292E39, .tabbar = 0x242933, .toolbar = 0x323946, .field = 0x3B4252,
    .field_hover = 0x434C5E, .border = 0x3B4252, .hover = 0x363D4A, .sel = 0x3B4F6B, .sel_focus = 0x4C6A92,
    .sel_idle = 0x3B4252, .text = 0xECEFF4, .text_dim = 0xA3ACBD, .text_faint = 0x6F7A8E, .text2 = 0xD8DEE9,
    .text_strong = 0xFFFFFF, .text_hidden = 0x7B8598, .accent = 0x88C0D0, .accent_hover = 0x9FD0DE,
    .on_accent = 0x2E3440, .folder = 0xEBCB8B, .folder_tab = 0xD0AE6A, .sel_text = 0xFFFFFF, .sel_dim = 0xD8DEE9,
    .sel_hl = 0xEBCB8B, .popup = 0x353C4A, .popup_border = 0x4C566A, .control = 0x434C5E, .control_hover = 0x4C566A,
    .control_off = 0x3B4252, .chip = 0x4C566A, .tab_hover = 0x2B303B, .sunken = 0x272C36, .divider = 0x1F232B,
    .track = 0x434C5E, .scroll = 0x4C566A, .scroll_hover = 0x616E88, .knob_off = 0xD8DEE9, .alt_row = 0x313744,
    .group_text = 0x88C0D0, .danger = 0xBF616A, .danger_text = 0xD08770, .warn = 0xD08770, .ok = 0xA3BE8C,
    .preview_bg = 0x2A2F3A,
};

#define MAX_THEMES 32
typedef struct {
    char name[64];
    char file[PATH_MAX]; // JSON file ("" = built-in)
    Theme t;
} ThemeEntry;
static ThemeEntry g_themes[MAX_THEMES];
static int g_nthemes;

static Theme T = g_theme_dark; // the palette everything draws with
static int g_theme_cur;
static u32 g_accent_custom; // 0 = theme accent, else 0x01000000 | rgb
static bool g_accent_desktop; // Options: the desktop's accent with every theme, where it has one (it wins over g_accent_custom)
static bool g_unified_surfaces; // Options: sidebar, header, preview and status bar take the file area's background
static char g_theme_pick[2][64] = {"Light", "Dark"}; // Theme > System: the theme while the desktop is light / dark (by name)
// Options > Surfaces brightness: -30..+30 % per part (darker / lighter), 0 = the theme's own colour.
enum { SURF_SIDEBAR, SURF_HEADER, SURF_FILES, SURF_PREVIEW, SURF_STATUS, SURF_PATHBAR, SURF_SEARCHBAR, SURF_COUNT };
static int g_surface_bright[SURF_COUNT];
static bool g_transparent_pathbar; // Options: remove only the path field's fill (pills and search stay filled)
static u32 g_scroll_custom; // Options: scrollbar colour; 0 = Auto, else 0x01000000 | rgb
static bool g_separators = true; // Options: the lines between the window's parts (a hovered / dragged edge always shows)
static u32 g_status_bg; // the status bar: the toolbar's colour, unless its brightness moves it
// The desktop's look, read off the UI thread (integration.c, "desktop look") only while Theme or File icons is System.
// accent and kde[]: 0 = none, else 0x01000000 | rgb; kde[]: KDE's colour scheme (all 0 elsewhere).
enum { KDE_VIEW, KDE_WINDOW, KDE_TEXT, KDE_SELECTION, KDE_COLORS };
static struct { bool known, light; u32 accent, kde[KDE_COLORS]; char icons[64]; } g_desktop;

static const struct { const char *name; u32 rgb; } g_accents[] = {
    {"Blue", 0x4C9BFF}, {"Purple", 0x9D7CF4}, {"Pink", 0xE86AB8}, {"Red", 0xEF5B5B}, {"Orange", 0xF2913A},
    {"Yellow", 0xE8C547}, {"Green", 0x4FC57A}, {"Teal", 0x3FC1C9}, {"Graphite", 0x8E939E},
};
#define ACCENT_COUNT ((int)ARRAY_COUNT(g_accents))

#define COL_BG            (T.bg)
#define COL_SIDEBAR       (T.sidebar)
#define COL_TABBAR        (T.tabbar)
#define COL_TOOLBAR       (T.toolbar)
#define COL_FIELD         (T.field)
#define COL_FIELD_HOVER   (T.field_hover)
#define COL_BORDER        (T.border)
#define COL_HOVER         (T.hover)
#define COL_SEL           (T.sel)
#define COL_SEL_FOCUS     (T.sel_focus)
#define COL_SEL_IDLE      (T.sel_idle)
#define COL_TEXT          (T.text)
#define COL_TEXT_DIM      (T.text_dim)
#define COL_TEXT_FAINT    (T.text_faint)
#define COL_TEXT2         (T.text2)
#define COL_TEXT_STRONG   (T.text_strong)
#define COL_TEXT_HIDDEN   (T.text_hidden)
#define COL_ACCENT        (T.accent)
#define COL_ACCENT_HOVER  (T.accent_hover)
#define COL_ON_ACCENT     (T.on_accent)
#define COL_FOLDER        (T.folder)
#define COL_FOLDER_TAB    (T.folder_tab)
#define COL_SEL_TEXT      (T.sel_text)
#define COL_SEL_DIM       (T.sel_dim)
#define COL_SEL_HL        (T.sel_hl)
#define COL_POPUP         (T.popup)
#define COL_POPUP_BORDER  (T.popup_border)
#define COL_CONTROL       (T.control)
#define COL_CONTROL_HOVER (T.control_hover)
#define COL_CONTROL_OFF   (T.control_off)
#define COL_CHIP          (T.chip)
#define COL_TAB_HOVER     (T.tab_hover)
#define COL_SUNKEN        (T.sunken)
#define COL_DIVIDER       (T.divider)
#define COL_TRACK         (T.track)
#define COL_SCROLL        (T.scroll)
#define COL_SCROLL_HOVER  (T.scroll_hover)
#define COL_KNOB_OFF      (T.knob_off)
#define COL_ALT_ROW       (T.alt_row)
#define COL_GROUP_TEXT    (T.group_text)
#define COL_DANGER        (T.danger)
#define COL_DANGER_TEXT   (T.danger_text)
#define COL_DANGER_FILL   0xD93636u // destructive buttons: a plain red (the theme dangers lean pink)
#define COL_WARN          (T.warn)
#define COL_OK            (T.ok)
#define COL_PREVIEW_BG    (T.preview_bg)
#define COL_STATUS        (g_status_bg)

static u32 rgb_mix(u32 a, u32 b, f32 t) {
    u32 out = 0;
    for (int s = 0; s <= 16; s += 8) {
        f32 x = (f32)((a >> s) & 255), y = (f32)((b >> s) & 255);
        out |= (u32)CLAMP(lroundf(x + (y - x) * t), 0, 255) << s;
    }
    return out;
}

static f32 rgb_luma(u32 c) {
    return (0.2126f * ((c >> 16) & 255) + 0.7152f * ((c >> 8) & 255) + 0.0722f * (c & 255)) / 255.f;
}

static bool theme_is_light(const Theme *t) { return rgb_luma(t->bg) > 0.5f; }

// "#RRGGBB" / "RRGGBB" / "#RGB" -> rgb; false if malformed.
static bool parse_hex_color(const char *s, u32 *out) {
    if (*s == '#') s++;
    size_t n = strspn(s, "0123456789abcdefABCDEF");
    if (s[n] && s[n] != '"' && s[n] != ' ') return false;
    if (n == 6) { *out = (u32)strtoul(s, NULL, 16) & 0xFFFFFF; return true; }
    if (n == 3) {
        u32 v = (u32)strtoul(s, NULL, 16);
        *out = ((v >> 8 & 15) * 0x110000) | ((v >> 4 & 15) * 0x1100) | ((v & 15) * 0x11);
        return true;
    }
    return false;
}

// Theme "System": Light or Dark as the desktop prefers (Dark until it says), with its accent unless one is picked.
static bool theme_is_system(void) {
    return g_theme_cur >= 0 && g_theme_cur < g_nthemes && !g_themes[g_theme_cur].file[0] && !strcmp(g_themes[g_theme_cur].name, "System");
}

// A part's background moved towards black (negative) or white by its brightness setting; always from the theme's
// own colour, so changing it twice doesn't add up.
static u32 surface_adjust(u32 c, int part) {
    int pct = CLAMP(g_surface_bright[part], -30, 30);
    return pct ? rgb_mix(c, pct < 0 ? 0x000000 : 0xFFFFFF, (f32)abs(pct) / 100.f) : c;
}

// Field colours are independent of the header strip: only the path/pill and search brightness settings move them.
// T.field itself is never adjusted in theme_apply().
static u32 pathbar_field_color(void) { return surface_adjust(T.field, SURF_PATHBAR); }
static u32 searchbar_field_color(void) { return surface_adjust(T.field, SURF_SEARCHBAR); }

// Pills enclosure: darker default than field; "Path bar and pills enclosure brightness" moves only this enclosure.
static u32 tab_container_color(void) {
    u32 base = rgb_mix(T.field, COL_BG, theme_is_light(&T) ? 0.18f : 0.14f);
    return surface_adjust(base, SURF_PATHBAR);
}

// Pill tab fill: only drawn on hover/focus. Derived from enclosure so enclosure brightness control affects it.
static u32 tab_surface_color(bool cur, bool primary) {
    u32 base = tab_container_color();
    return rgb_mix(base, COL_ACCENT, !cur ? 0.08f : primary ? 0.40f : 0.24f);
}

// Final safety net: if fg and bg are too close in luma, swap to dark or white for readable text/icons.
static u32 surface_foreground_color(u32 fg, u32 bg) {
    f32 fgl = rgb_luma(fg), bgl = rgb_luma(bg);
    if (fabsf(fgl - bgl) >= 0.45f) return fg;
    return bgl > 0.5f ? 0x111111u : 0xFFFFFFu;
}

static int theme_find(const char *name);

// Theme "System": the theme picked for light or for dark (Options: Light theme / Dark theme), as the desktop prefers.
// Left on the built-in Light / Dark, on KDE it's built from its colour scheme (view = the file area, window = sidebar,
// header and preview, text, selection = the accent).
static Theme theme_system(void) {
    int light = g_desktop.light ? 0 : 1, i = theme_find(g_theme_pick[light]);
    bool builtin = i < 0 || (!g_themes[i].file[0] && (str_ieq(g_themes[i].name, light == 0 ? "Light" : "Dark") || str_ieq(g_themes[i].name, "System")));
    if (!builtin) return g_themes[i].t;
    Theme t = g_desktop.light ? g_theme_light : g_theme_dark;
    if (!g_desktop.kde[KDE_VIEW]) return t;
    u32 bg = g_desktop.kde[KDE_VIEW] & 0xFFFFFF, win = g_desktop.kde[KDE_WINDOW] ? g_desktop.kde[KDE_WINDOW] & 0xFFFFFF : bg;
    u32 text = g_desktop.kde[KDE_TEXT] ? g_desktop.kde[KDE_TEXT] & 0xFFFFFF : t.text;
    t.bg = bg;
    t.sidebar = t.toolbar = t.preview_bg = t.popup = win;
    t.tabbar = rgb_mix(win, bg, 0.5f);
    t.text = t.text_strong = text;
    t.text2 = rgb_mix(text, bg, 0.12f);
    t.text_dim = rgb_mix(text, bg, 0.35f);
    t.text_faint = t.text_hidden = rgb_mix(text, bg, 0.55f);
    t.field = rgb_mix(win, bg, 0.65f);
    t.field_hover = t.hover = t.control = rgb_mix(win, text, 0.08f);
    t.control_hover = t.chip = rgb_mix(win, text, 0.14f);
    t.control_off = t.tab_hover = t.field;
    t.border = t.popup_border = t.divider = rgb_mix(win, text, 0.18f);
    t.sel_idle = rgb_mix(bg, text, 0.12f);
    t.alt_row = rgb_mix(bg, text, 0.025f);
    t.scroll = rgb_mix(bg, text, 0.30f);
    t.scroll_hover = rgb_mix(bg, text, 0.45f);
    return t;
}

// Recompute T from the selected theme, surfaces and accent override.
static void theme_apply(void) {
    if (g_theme_cur < 0 || g_theme_cur >= g_nthemes) g_theme_cur = 0;
    T = theme_is_system() ? theme_system() : g_themes[g_theme_cur].t;
    if (g_unified_surfaces) T.sidebar = T.tabbar = T.toolbar = T.preview_bg = T.bg; // (the lines between them stay)
    g_status_bg = surface_adjust(T.toolbar, SURF_STATUS);
    T.sidebar = surface_adjust(T.sidebar, SURF_SIDEBAR);
    T.tabbar = surface_adjust(T.tabbar, SURF_HEADER);
    T.toolbar = surface_adjust(T.toolbar, SURF_HEADER);
    T.bg = surface_adjust(T.bg, SURF_FILES);
    T.alt_row = surface_adjust(T.alt_row, SURF_FILES);
    T.preview_bg = surface_adjust(T.preview_bg, SURF_PREVIEW);
    u32 accent = g_accent_desktop && g_desktop.accent ? g_desktop.accent : g_accent_custom ? g_accent_custom : theme_is_system() ? g_desktop.accent : 0;
    if (accent) {
        u32 a = accent & 0xFFFFFF;
        bool light = theme_is_light(&T);
        T.accent = a;
        T.accent_hover = rgb_mix(a, light ? 0x000000 : 0xFFFFFF, 0.15f);
        T.on_accent = rgb_luma(a) > 0.62f ? 0x111111 : 0xFFFFFF;
        T.sel = rgb_mix(T.bg, a, light ? 0.22f : 0.36f);
        T.sel_focus = rgb_mix(T.bg, a, light ? 0.34f : 0.52f);
        T.group_text = rgb_mix(T.text, a, 0.55f);
    }
    // Scrollbars: a picked colour, else (Auto) the theme's own; System tints them with the desktop's accent, kept
    // visible against the background when that accent is near black or white.
    if (g_scroll_custom) {
        T.scroll = g_scroll_custom & 0xFFFFFF;
        T.scroll_hover = rgb_mix(T.scroll, T.text, 0.25f);
    } else if (theme_is_system() && g_desktop.accent) {
        u32 a = g_desktop.accent & 0xFFFFFF;
        T.scroll = rgb_mix(T.bg, a, 0.55f);
        T.scroll_hover = rgb_mix(T.bg, a, 0.80f);
        if (fabsf(rgb_luma(T.scroll) - rgb_luma(T.bg)) < 0.10f) T.scroll = rgb_mix(T.bg, T.text, 0.35f);
        if (fabsf(rgb_luma(T.scroll_hover) - rgb_luma(T.bg)) < 0.15f) T.scroll_hover = rgb_mix(T.bg, T.text, 0.55f);
    }
}

// Scrollbar colour as typed or saved: "" / "auto" = Auto (*out 0), "#RRGGBB" / "#RGB" (the # optional); false if neither.
static bool scroll_color_parse(const char *s, u32 *out) {
    while (*s == ' ') s++;
    if (!*s || str_ieq(s, "auto")) { *out = 0; return true; }
    const char *h = *s == '#' ? s + 1 : s;
    size_t n = strspn(h, "0123456789abcdefABCDEF");
    u32 c;
    if ((n != 3 && n != 6) || h[n + strspn(h + n, " ")] || !parse_hex_color(h, &c)) return false;
    *out = 0x01000000u | c;
    return true;
}

// Minimal JSON reader for colour schemes: collects every "key": "value" string pair (any nesting).
// {"name": "My theme", "base": "dark", "colors": {"bg": "#1b1b20", "accent": "#ff8800"}}
static bool theme_load_json(const char *path, ThemeEntry *out) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char buf[16384];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    char base[16] = "dark";
    char name[64] = "";
    int ncolors = 0;
    Theme t;
    // first pass: base theme; second: values
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) t = !strcmp(base, "light") ? g_theme_light : !strcmp(base, "black") ? g_theme_black : g_theme_dark;
        const char *p = buf;
        char key[64] = "";
        bool have_key = false;
        while (*p) {
            if (*p != '"') {
                if (*p == '{' || *p == '}' || *p == ',') have_key = false;
                p++;
                continue;
            }
            p++;
            char s[128];
            size_t k = 0;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p++;
                if (k + 1 < sizeof s) s[k++] = *p;
                p++;
            }
            s[k] = 0;
            if (*p == '"') p++;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            if (*p == ':' && !have_key) {
                snprintf(key, sizeof key, "%s", s);
                have_key = true;
                p++;
                continue;
            }
            if (have_key) {
                if (pass == 0) {
                    if (!strcmp(key, "base")) snprintf(base, sizeof base, "%s", s);
                    else if (!strcmp(key, "name")) snprintf(name, sizeof name, "%s", s);
                } else {
                    u32 c;
                    for (size_t i = 0; i < ARRAY_COUNT(g_theme_keys); i++)
                        if (!strcmp(key, g_theme_keys[i].key) && parse_hex_color(s, &c)) {
                            *(u32 *)((u8 *)&t + g_theme_keys[i].off) = c;
                            ncolors++;
                        }
                }
                have_key = false;
            }
        }
    }
    if (!name[0]) {
        const char *b = path_basename(path);
        snprintf(name, sizeof name, "%.*s", (int)(strcspn(b, ".")), b);
    }
    snprintf(out->name, sizeof out->name, "%s", name);
    snprintf(out->file, sizeof out->file, "%s", path);
    out->t = t;
    return ncolors > 0 || strcmp(base, "dark");
}

static void themes_dir(char *out, size_t cap);

static int cmp_theme_entry(const void *a, const void *b) {
    return natcmp(((const ThemeEntry *)a)->name, ((const ThemeEntry *)b)->name);
}

// Built-ins first, then *.json files from the themes folder (sorted by name).
static void themes_scan(void) {
    char keep[64];
    snprintf(keep, sizeof keep, "%s", g_nthemes ? g_themes[g_theme_cur].name : "Dark");
    g_nthemes = 0;
    static const struct { const char *name; const Theme *t; } builtin[] = {
        {"Dark", &g_theme_dark}, {"Light", &g_theme_light}, {"Black", &g_theme_black},
        {"Monokai", &g_theme_monokai}, {"Nord", &g_theme_nord}, {"System", &g_theme_dark}, // (System: theme_apply)
    };
    for (size_t i = 0; i < ARRAY_COUNT(builtin); i++) {
        ThemeEntry *e = &g_themes[g_nthemes++];
        snprintf(e->name, sizeof e->name, "%s", builtin[i].name);
        e->file[0] = 0;
        e->t = *builtin[i].t;
    }
    int nbuiltin = g_nthemes;
    char dir[PATH_MAX];
    themes_dir(dir, sizeof dir);
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) && g_nthemes < MAX_THEMES) {
            size_t l = strlen(de->d_name);
            if (l < 6 || strcmp(de->d_name + l - 5, ".json")) continue;
            char path[PATH_MAX];
            path_join(path, sizeof path, dir, de->d_name);
            ThemeEntry e;
            if (!theme_load_json(path, &e)) continue;
            bool dup = false;
            for (int i = 0; i < g_nthemes; i++) dup |= str_ieq(g_themes[i].name, e.name);
            if (dup) snprintf(e.name + strlen(e.name), sizeof e.name - strlen(e.name), " (file)");
            g_themes[g_nthemes++] = e;
        }
        closedir(d);
    }
    qsort(g_themes + nbuiltin, (size_t)(g_nthemes - nbuiltin), sizeof(ThemeEntry), cmp_theme_entry);
    g_theme_cur = 0;
    for (int i = 0; i < g_nthemes; i++)
        if (str_ieq(g_themes[i].name, keep)) g_theme_cur = i;
}

static int theme_find(const char *name) {
    for (int i = 0; i < g_nthemes; i++)
        if (str_ieq(g_themes[i].name, name)) return i;
    return -1;
}

// Write the current palette as a JSON scheme (a starting point for custom themes).
static bool theme_export(const char *path, const char *name) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "{\n  \"name\": \"%s\",\n  \"base\": \"%s\",\n  \"colors\": {\n", name, theme_is_light(&T) ? "light" : "dark");
    for (size_t i = 0; i < ARRAY_COUNT(g_theme_keys); i++)
        fprintf(f, "    \"%s\": \"#%06X\"%s\n", g_theme_keys[i].key, *(const u32 *)((const u8 *)&T + g_theme_keys[i].off),
                i + 1 < ARRAY_COUNT(g_theme_keys) ? "," : "");
    fprintf(f, "  }\n}\n");
    return fclose(f) == 0;
}
