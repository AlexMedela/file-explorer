// A system palette when the desktop exposes full colors. On KDE these values
// are available both in shared kdeglobals and through the Settings portal.
static Theme desktop_palette_build(u32 bg, u32 window, u32 text, u32 accent) {
    Theme t = rgb_luma(bg) > 0.5f ? g_theme_light : g_theme_dark;
    t.bg = bg;
    t.sidebar = t.preview_bg = t.toolbar = t.tabbar = t.status = window;
    t.text = t.text_strong = text;
    t.text2 = rgb_mix(text, bg, 0.12f);
    t.text_dim = rgb_mix(text, bg, 0.35f);
    t.text_faint = t.text_hidden = rgb_mix(text, bg, 0.55f);
    t.field = rgb_mix(window, bg, 0.65f);
    t.field_hover = t.hover = t.control = rgb_mix(window, text, 0.08f);
    t.control_hover = t.chip = rgb_mix(window, text, 0.14f);
    t.control_off = t.tab_hover = t.field;
    t.popup = window;
    t.border = t.popup_border = rgb_mix(window, text, 0.18f);
    t.divider = t.border;
    t.alt_row = rgb_mix(bg, text, 0.025f);
    t.accent = accent;
    t.accent_hover = rgb_mix(accent, text, 0.15f);
    t.on_accent = rgb_luma(accent) > 0.62f ? 0x111111 : 0xFFFFFF;
    t.sel = rgb_mix(bg, accent, rgb_luma(bg) > 0.5f ? 0.22f : 0.36f);
    t.sel_focus = rgb_mix(bg, accent, 0.5f);
    t.sel_text = rgb_luma(t.sel) > 0.55f ? 0x111111 : 0xFFFFFF;
    t.sel_dim = rgb_mix(t.sel_text, t.sel, 0.22f);
    t.scroll = rgb_mix(bg, text, 0.35f);
    t.scroll_hover = rgb_mix(bg, accent, 0.80f);
    return t;
}
static bool desktop_palette_read(Theme *out) {
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    if (desktop && *desktop && !strcasestr(desktop, "kde") && !strcasestr(desktop, "plasma")) return false;
    char path[PATH_MAX], value[256];
    desktop_config_path(path, sizeof path, "kdeglobals");
    u32 bg, window, text, accent;
    if (!desktop_ini(path, "Colors:View", "BackgroundNormal", value, sizeof value) || !desktop_rgb(value, &bg)) return false;
    bool light = rgb_luma(bg) > 0.5f;
    Theme base = light ? g_theme_light : g_theme_dark;
    window = bg; text = base.text; accent = base.accent;
    if (desktop_ini(path, "Colors:Window", "BackgroundNormal", value, sizeof value)) desktop_rgb(value, &window);
    if (desktop_ini(path, "Colors:View", "ForegroundNormal", value, sizeof value)) desktop_rgb(value, &text);
    if (desktop_ini(path, "Colors:Selection", "BackgroundNormal", value, sizeof value)) desktop_rgb(value, &accent);
    *out = desktop_palette_build(bg, window, text, accent);
    return true;
}
