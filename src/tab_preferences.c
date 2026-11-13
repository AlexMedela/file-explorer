// Tab preferences and shared band geometry, independent of the renderer.
enum { TAB_BELOW, TAB_ABOVE };
enum { TAB_PILLS, TAB_CLASSIC };
static int g_tab_position = TAB_BELOW, g_tab_style = TAB_PILLS;

static void tab_preferences_defaults(void) {
    g_tab_position = TAB_BELOW;
    g_tab_style = TAB_PILLS;
}
static bool tab_preferences_read(const char *key, const char *value) {
    if (!strcmp(key, "tab_position")) {
        // Accept legacy settings; placement now follows the tab style.
        g_tab_position = g_tab_style == TAB_CLASSIC ? TAB_ABOVE : TAB_BELOW;
        return true;
    }
    if (!strcmp(key, "tab_style")) {
        g_tab_style = !strcmp(value, "classic") ? TAB_CLASSIC : TAB_PILLS;
        g_tab_position = g_tab_style == TAB_CLASSIC ? TAB_ABOVE : TAB_BELOW;
        return true;
    }
    return false;
}
static void tab_preferences_write(FILE *f) {
    fprintf(f, "tab_position=%s\ntab_style=%s\n",
            g_tab_position == TAB_ABOVE ? "above" : "below", g_tab_style == TAB_CLASSIC ? "classic" : "pills");
}

typedef struct { f32 navigation_y, tabs_y, content_y, tabs_height; } TabBands;
static TabBands tab_bands(f32 y, f32 scale, int count) {
    g_tab_position = g_tab_style == TAB_CLASSIC ? TAB_ABOVE : TAB_BELOW;
    f32 nav = roundf(46.f * scale), tabs = count > 1 ? roundf(46.f * scale) : 0;
    return (TabBands){y + (g_tab_position == TAB_ABOVE ? tabs : 0),
                      y + (g_tab_position == TAB_BELOW ? nav : 0), y + nav + tabs, tabs};
}

// Only the first row shares window controls. Rows below retain full pane width.
static f32 header_available_width(f32 x, f32 y, f32 width, bool controls_visible, f32 controls_x) {
    return controls_visible && y < 0.5f ? MAX(0.f, MIN(width, controls_x - x)) : width;
}
