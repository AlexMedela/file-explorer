// Headless regression checks: no SDL, display server or session bus required.
/* cc -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-format-truncation
   tools/appearance-selftest.c -o /tmp/fe-appearance-test -lm -ldl -pthread */
#include "../src/base.h"
#include <assert.h>
#include <dlfcn.h>
#include "../src/desktop_config.c"
#include "../src/theme.c"
#include "../src/system_icons.c"
static void themes_dir(char *out, size_t cap) { snprintf(out, cap, "/nonexistent/fe-appearance-test"); }
static struct { bool selftest, screenshot, redraw; } A = {.selftest = true};
static void main_post(void (*fn)(void *), void *arg) { fn(arg); }
static void main_drain(void) {}
#include "../src/dbus.c"
static void icons_flush(void) {}
#include "../src/system_appearance.c"

static void test_surfaces(void) {
    themes_scan();
    assert(g_show_separators && g_unified_surfaces);
    for (int i = 0; i < g_nthemes; i++) {
        g_theme_cur = i;
        g_unified_surfaces = true;
        memset(g_surface_brightness, 0, sizeof g_surface_brightness);
        theme_apply();
        assert(T.bg == T.sidebar && T.bg == T.preview_bg && T.bg == T.toolbar && T.bg == T.status);
        Theme base = T;
        u32 container = tab_container_color();
        if (rgb_luma(T.toolbar) >= 0.025f) assert(rgb_luma(container) < rgb_luma(T.toolbar));
        u32 hover = tab_surface_color(false, true), focus = tab_surface_color(true, true);
        assert(hover != container && focus != container);
        assert(hover != T.toolbar && focus != hover);
        assert(focus == navigation_surface_color(container, true));
        assert(fabsf(rgb_luma(focus) - rgb_luma(container)) > fabsf(rgb_luma(hover) - rgb_luma(container)));
        assert(fabsf(rgb_luma(surface_foreground_color(container, false, false)) - rgb_luma(container)) >= 0.4f);
        assert(fabsf(rgb_luma(surface_foreground_color(focus, true, false)) - rgb_luma(focus)) >= 0.4f);
        for (int k = 0; k < SURFACE_COUNT; k++) {
            g_surface_brightness[k] = -30;
            theme_apply();
            assert(k == SURFACE_MAIN || T.bg == base.bg);
            assert(k == SURFACE_SIDEBAR || T.sidebar == base.sidebar);
            assert(k == SURFACE_HEADER || T.toolbar == base.toolbar);
            assert(k == SURFACE_STATUS || T.status == base.status);
            g_surface_brightness[k] = 0;
            theme_apply();
            assert(!memcmp(&base, &T, sizeof T));
        }
        // Reapplying cannot accumulate brightness changes.
        g_surface_brightness[SURFACE_HEADER] = 30;
        theme_apply();
        Theme bright = T;
        theme_apply();
        assert(!memcmp(&bright, &T, sizeof T));
        memset(g_surface_brightness, 0, sizeof g_surface_brightness);
        g_unified_surfaces = false;
        theme_apply();
        if (strcmp(g_themes[i].name, "System")) assert(!memcmp(&T, &g_themes[i].t, sizeof T));
    }
    g_theme_cur = 0;
    g_unified_surfaces = true;
    g_accent_custom = 0x01123456;
    g_accent_system = true;
    g_system_accent = 0;
    theme_apply();
    assert(T.accent == g_theme_dark.accent); // theme fallback, no stale manual override
    g_system_accent = system_accent_rgb((double[]){0.25, 0.5, 1});
    theme_apply();
    assert(T.accent == 0x4080FF);
    g_accent_system = false;
    theme_apply();
    assert(T.accent == 0x123456); // previous manual choice survives system mode
    assert(system_accent_rgb((double[]){0, 0, 0}) == 0x01000000); // black is valid
    assert(system_accent_rgb((double[]){1, 1, 1}) == 0x01FFFFFF);
    assert(!system_accent_rgb((double[]){NAN, 0, 0}));
    assert(!system_accent_rgb((double[]){INFINITY, 0, 0}));
    assert(!system_accent_rgb((double[]){0, -0.01, 0}));
    assert(!system_accent_rgb((double[]){0, 0, 1.01}));
}

static void test_follow_system_and_scrollbars(void) {
    themes_scan();
    theme_select(theme_find("Nord"));
    assert(!g_match_system_background);
    theme_follow_system(true);
    assert(!strcmp(g_themes[g_theme_cur].name, "System") && g_match_system_background);
    assert(!strcmp(g_previous_theme, "Nord"));
    theme_follow_system(false);
    assert(!strcmp(g_themes[g_theme_cur].name, "Nord") && !g_match_system_background);
    theme_select(theme_find("System"));
    assert(g_match_system_background);
    theme_select(theme_find("Light"));
    assert(!g_match_system_background);
    // Older configs could set follow mode without changing the stored theme.
    g_match_system_background = true;
    theme_restore_selection("Monokai");
    assert(!strcmp(g_themes[g_theme_cur].name, "System") && !strcmp(g_previous_theme, "Monokai"));
    theme_follow_system(false);
    assert(!strcmp(g_themes[g_theme_cur].name, "Monokai"));
    snprintf(g_previous_theme, sizeof g_previous_theme, "System");
    theme_follow_system(false);
    assert(g_theme_cur == 0 && !g_match_system_background);
    u32 value = 123;
    assert(scrollbar_color_parse("auto", &value) && value == 0);
    assert(scrollbar_color_parse("", &value) && value == 0);
    assert(scrollbar_color_parse("#000", &value) && value == 0x01000000);
    assert(scrollbar_color_parse("#aBc", &value) && value == 0x01AABBCC);
    assert(scrollbar_color_parse("123456", &value) && value == 0x01123456);
    const char *invalid[] = {"#1234567", "#12", "#GGGGGG", "#123456 trailing", "#123456\"", "auto!"};
    for (size_t i = 0; i < ARRAY_COUNT(invalid); i++) {
        value = 0x01123456;
        assert(!scrollbar_color_parse(invalid[i], &value) && value == 0x01123456);
    }
    const char *themes[] = {"Dark", "Light", "Black", "Nord"};
    for (size_t i = 0; i < ARRAY_COUNT(themes); i++) {
        theme_select(theme_find(themes[i]));
        g_scroll_custom = g_system_accent = 0;
        theme_apply();
        assert(T.scroll == g_themes[g_theme_cur].t.scroll); // no desktop color: theme fallback
        g_system_accent = 0x01000000 | 0x35AA80;
        theme_apply();
        u32 automatic = T.scroll;
        assert(T.scroll_hover != automatic);
        assert(scrollbar_color_parse("#D040B0", &g_scroll_custom));
        theme_apply();
        assert(T.scroll == 0xD040B0);
        g_system_accent = 0x01000000 | 0xE08020;
        theme_apply();
        assert(T.scroll == 0xD040B0); // desktop changes cannot overwrite the custom color
        assert(scrollbar_color_parse("Auto", &g_scroll_custom));
        theme_apply();
        assert(T.scroll != 0xD040B0 && T.scroll != automatic);
    }
    // Extreme but valid desktop accents still produce a visible automatic thumb.
    const char *extreme_themes[] = {"Black", "Light"};
    const u32 accents[] = {0x01000000, 0x01FFFFFF};
    for (int i = 0; i < 2; i++) {
        theme_select(theme_find(extreme_themes[i]));
        g_system_accent = accents[i];
        theme_apply();
        assert(fabsf(rgb_luma(T.scroll) - rgb_luma(T.bg)) >= 0.10f);
    }
    g_system_accent = g_scroll_custom = 0;
    theme_select(0);
    theme_apply();
}

static void test_theme_capture(void) {
    theme_select(theme_find("Dark"));
    g_unified_surfaces = true;
    g_surface_brightness[SURFACE_SIDEBAR] = -10;
    g_surface_brightness[SURFACE_HEADER] = 5;
    g_surface_brightness[SURFACE_MAIN] = 10;
    g_surface_brightness[SURFACE_STATUS] = -5;
    theme_apply();
    Theme expected = T;
    char path[] = "/tmp/fe-theme-capture-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    assert(theme_export(path, "Captured test"));
    ThemeEntry entry;
    assert(theme_load_json(path, &entry));
    assert(!unlink(path));
    int index = g_nthemes++;
    g_themes[index] = entry;
    theme_use_capture(index);
    assert(!memcmp(&expected, &T, sizeof T));
    assert(!g_unified_surfaces);
    for (int i = 0; i < SURFACE_COUNT; i++) assert(g_surface_brightness[i] == 0);
    assert(!g_match_system_background);
    theme_select(0);
    g_nthemes--;
    g_unified_surfaces = true;
    theme_apply();
}

static void test_sidebar_accent(void) {
    assert(g_navigation_selection_opacity == 40);
    int saved_theme = g_theme_cur;
    u32 custom = g_accent_custom, system = g_system_accent;
    bool follow = g_accent_system;
    const char *themes[] = {"Dark", "Light", "Black", "Nord"};
    const u32 accents[] = {0x4080FF, 0xA050D0, 0x25B070, 0xEF8C30, 0xFFFFFF, 0x000000};
    for (size_t t = 0; t < ARRAY_COUNT(themes); t++) {
        g_theme_cur = theme_find(themes[t]);
        for (size_t i = 0; i < ARRAY_COUNT(accents); i++) {
            g_accent_system = false;
            g_accent_custom = 0x01000000u | accents[i];
            theme_apply();
            u32 manual = sidebar_surface_color(true), hover = sidebar_surface_color(false);
            assert(manual == rgb_mix(T.sidebar, accents[i], 0.40f));
            assert(hover == rgb_mix(T.sidebar, accents[i], 0.08f));
            assert(navigation_surface_color(T.popup, true) == rgb_mix(T.popup, accents[i], 0.40f));
            assert(navigation_surface_color(T.popup, false) == rgb_mix(T.popup, accents[i], 0.08f));
            // Selection moves farther toward the same accent than hover.
            assert(fabsf(rgb_luma(manual) - rgb_luma(T.sidebar)) + 0.005f >=
                   fabsf(rgb_luma(hover) - rgb_luma(T.sidebar)));
            assert(fabsf(rgb_luma(surface_foreground_color(manual, false, false)) - rgb_luma(manual)) >= 0.4f);
            g_accent_system = true;
            g_system_accent = 0x01000000u | accents[i];
            g_accent_custom = 0x01123456; // system mode must ignore this manual color
            theme_apply();
            assert(sidebar_surface_color(true) == manual && sidebar_surface_color(false) == hover);
        }
    }
    // Overrides affect both navigation areas and leave hover independent.
    const int strengths[] = {0, 20, 40, 80, 100};
    for (size_t i = 0; i < ARRAY_COUNT(strengths); i++) {
        g_navigation_selection_opacity = strengths[i];
        assert(sidebar_surface_color(true) == rgb_mix(T.sidebar, T.accent, strengths[i] / 100.f));
        assert(navigation_surface_color(T.popup, true) == rgb_mix(T.popup, T.accent, strengths[i] / 100.f));
        assert(sidebar_surface_color(false) == rgb_mix(T.sidebar, T.accent, 0.08f));
    }
    g_navigation_selection_opacity = NAV_SELECTION_DEFAULT;
    g_theme_cur = saved_theme;
    g_accent_custom = custom;
    g_system_accent = system;
    g_accent_system = follow;
    theme_apply();
}

static void test_tab_highlight(void) {
    int theme = g_theme_cur;
    u32 manual = g_accent_custom, system = g_system_accent;
    bool follow = g_accent_system;
    const u32 highlights[] = {0x3DAEE9, 0xA050D0, 0x25B070, 0xEF8C30};
    g_theme_cur = theme_find("Dark");
    g_accent_system = true;
    u32 container = 0;
    for (size_t i = 0; i < ARRAY_COUNT(highlights); i++) {
        g_system_accent = 0x01000000u | highlights[i];
        theme_apply();
        if (!i) container = tab_container_color();
        else assert(tab_container_color() == container); // highlight does not tint the enclosure
        g_tab_style = TAB_PILLS;
        assert(tab_surface_color(true, true) == navigation_surface_color(container, true));
        assert(tab_surface_color(true, false) == rgb_mix(container, T.accent, 0.24f));
        assert(tab_surface_color(false, true) == navigation_surface_color(container, false));
        g_tab_style = TAB_CLASSIC;
        assert(tab_surface_color(true, true) == T.toolbar);
        assert(tab_surface_color(true, false) == T.toolbar);
        assert(classic_tab_color(true, false) == T.toolbar);
        assert(rgb_luma(classic_tab_color(false, false)) < rgb_luma(T.toolbar));
        assert(rgb_luma(classic_tab_color(false, true)) > rgb_luma(classic_tab_color(false, false)));
    }
    // A changed header tint/brightness changes the enclosure, with no accent dependency.
    g_surface_brightness[SURFACE_HEADER] = 15;
    theme_apply();
    assert(tab_container_color() != container && tab_container_color() == rgb_mix(T.toolbar, 0x000000, 0.28f));
    g_surface_brightness[SURFACE_HEADER] = 0;
    g_theme_cur = theme;
    g_accent_custom = manual;
    g_system_accent = system;
    g_accent_system = follow;
    tab_preferences_defaults();
    theme_apply();
}

static void test_tab_preferences(void) {
    tab_preferences_defaults();
    assert(g_tab_position == TAB_BELOW && g_tab_style == TAB_PILLS);
    for (int position = TAB_BELOW; position <= TAB_ABOVE; position++) {
        for (int style = TAB_PILLS; style <= TAB_CLASSIC; style++) {
            g_tab_position = position;
            g_tab_style = style;
            char *text = NULL;
            size_t length = 0;
            FILE *f = open_memstream(&text, &length);
            assert(f);
            tab_preferences_write(f);
            assert(!fclose(f));
            tab_preferences_defaults();
            char *p = text, *line;
            while ((line = strsep(&p, "\n"))) {
                char *eq = strchr(line, '=');
                if (!eq) continue;
                *eq++ = 0;
                assert(tab_preferences_read(line, eq));
            }
            free(text);
            assert(g_tab_position == position && g_tab_style == style);
            const float scales[] = {1.f, 1.25f, 2.f};
            for (size_t s = 0; s < ARRAY_COUNT(scales); s++) {
                float scale = scales[s], y = 53.f, nav = roundf(46.f * scale);
                TabBands single = tab_bands(y, scale, 1), pair = tab_bands(y, scale, 2);
                assert(single.tabs_height == 0 && single.navigation_y == y && single.content_y == y + nav);
                assert(pair.content_y == y + nav + pair.tabs_height);
                assert(pair.content_y - single.content_y == pair.tabs_height);
                if (position == TAB_ABOVE) {
                    assert(pair.tabs_y == y && pair.navigation_y == y + pair.tabs_height);
                    assert(pair.navigation_y + nav == pair.content_y);
                } else {
                    assert(pair.navigation_y == y && pair.tabs_y == y + nav);
                    assert(pair.tabs_y + pair.tabs_height == pair.content_y);
                }
            }
        }
    }
    assert(!tab_preferences_read("other_setting", "above"));
    assert(tab_preferences_read("tab_position", "invalid") && g_tab_position == TAB_BELOW);
    assert(tab_preferences_read("tab_style", "invalid") && g_tab_style == TAB_PILLS);
    tab_preferences_defaults();
}

static void test_integrated_header(void) {
    header_preferences_defaults();
    assert(!strcmp(header_label_text(), "File-Explorer") && g_show_window_controls);
    assert(header_preferences_read("show_window_controls", "0") && !g_show_window_controls);
    assert(header_preferences_read("header_label", "hostname"));
    assert(*header_label_text());
    assert(!header_text_valid("") && !header_text_valid("   ") && !header_text_valid("bad\nlabel"));
    char long_label[120]; memset(long_label, 'x', sizeof long_label - 1); long_label[sizeof long_label - 1] = 0;
    assert(!header_text_valid(long_label));
    assert(header_preferences_read("header_text", "Workstation files"));
    assert(header_preferences_read("header_label", "custom"));
    assert(!strcmp(header_label_text(), "Workstation files"));
    char *text = NULL; size_t n = 0;
    FILE *f = open_memstream(&text, &n); assert(f);
    header_preferences_write(f); assert(!fclose(f));
    header_preferences_defaults();
    char *p = text, *line;
    while ((line = strsep(&p, "\n"))) {
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq++ = 0; assert(header_preferences_read(line, eq));
    }
    free(text);
    assert(g_header_label == HEADER_CUSTOM && !strcmp(header_label_text(), "Workstation files") && !g_show_window_controls);
    header_label_defaults(); // resetting only the label must not re-enable window controls
    assert(!g_show_window_controls && !strcmp(header_label_text(), "File-Explorer"));
    assert(header_preferences_read("header_label", "hidden") && !*header_label_text());
    header_preferences_defaults();
    const float scales[] = {1.f, 1.25f, 2.f};
    for (size_t i = 0; i < ARRAY_COUNT(scales); i++) {
        float scale = scales[i], row = roundf(46.f * scale);
        float left = 216.f * scale, width = 1000.f * scale, controls = left + width - row * 3;
        for (int position = TAB_BELOW; position <= TAB_ABOVE; position++) {
            g_tab_position = position;
            for (int count = 1; count <= 2; count++) {
                TabBands bands = tab_bands(0, scale, count);
                assert(bands.content_y == row * count); // no separate title-row height
                float top = count > 1 && position == TAB_ABOVE ? bands.tabs_y : bands.navigation_y;
                assert(top == 0);
                float usable = header_available_width(left, top, width, true, controls);
                assert(left + usable == controls);
                assert(header_available_width(left, row, width, true, controls) == width);
                assert(header_available_width(left, 0, width, false, controls) == width);
                assert(header_available_width(left, 0, 200 * scale, true, controls) == 200 * scale); // pane before controls
                assert(header_available_width(controls + 10, 0, 20, true, controls) == 0); // very narrow pane
            }
        }
    }
    // Pill hover/focus share the configured selection strength and accent.
    g_tab_style = TAB_PILLS;
    for (int strength = 20; strength <= 80; strength += 20) {
        g_navigation_selection_opacity = strength;
        assert(tab_surface_color(true, true) == navigation_surface_color(tab_container_color(), true));
        assert(tab_surface_color(false, true) == navigation_surface_color(tab_container_color(), false));
    }
    g_navigation_selection_opacity = NAV_SELECTION_DEFAULT;
    tab_preferences_defaults();
}

static void test_system_palette(void) {
    assert(theme_find("Darkly inspired") < 0 && theme_find("System") >= 0);
    char dir[] = "/tmp/fe-system-palette-XXXXXX", path[PATH_MAX];
    assert(mkdtemp(dir));
    setenv("XDG_CONFIG_HOME", dir, 1);
    setenv("XDG_CURRENT_DESKTOP", "KDE", 1);
    snprintf(path, sizeof path, "%s/kdeglobals", dir);
    FILE *f = fopen(path, "w");
    assert(f);
    fputs("[Colors:View]\nBackgroundNormal=36,37,38\nForegroundNormal=230,231,232\n"
          "[Colors:Window]\nBackgroundNormal=30,31,32\n"
          "[Colors:Selection]\nBackgroundNormal=80,120,180\n[Icons]\nTheme=Reversal\n", f);
    fclose(f);
    SystemAppearance state = system_appearance_read(NULL);
    assert(state.palette_valid && state.palette.bg == 0x242526 && state.palette.toolbar == 0x1E1F20);
    assert(state.palette.text == 0xE6E7E8 && state.accent == 0x015078B4);
    assert(!strcmp(state.icons, "Reversal"));
    system_appearance_post(&state);
    g_theme_cur = theme_find("System");
    g_accent_system = false;
    g_accent_custom = 0;
    g_unified_surfaces = true;
    theme_apply();
    assert(T.bg == 0x242526 && T.toolbar == T.bg && T.status == T.bg);
    g_unified_surfaces = false;
    theme_apply();
    assert(T.toolbar == 0x1E1F20 && T.sidebar == T.toolbar);
    // A desktop that provides only the standard light/dark + accent settings.
    g_system_palette_valid = false;
    g_system_color_scheme = 2;
    g_system_accent = 0;
    theme_apply();
    assert(T.bg == g_theme_light.bg);
    g_system_accent = 0x014080FF;
    theme_apply();
    assert(T.bg != g_theme_light.bg && T.accent == 0x4080FF);
    g_match_system_background = true;
    g_theme_cur = theme_find("Nord");
    theme_apply();
    assert(T.bg != g_theme_nord.bg); // override works with any selected theme
    assert(unlink(path) == 0 && rmdir(dir) == 0);
    g_match_system_background = false;
    g_unified_surfaces = true;
    g_system_accent = 0;
    g_system_color_scheme = 0;
    g_theme_cur = 0;
    theme_apply();
}

static void test_portal_messages(void) {
    assert(ig_dbus_load());
    void *lib = g_fm1.lib;
    void *(*new_message)(int) = dlsym(lib, "dbus_message_new");
    void (*init_append)(void *, IgDBusIter *) = dlsym(lib, "dbus_message_iter_init_append");
    unsigned (*open)(IgDBusIter *, int, const char *, IgDBusIter *) = dlsym(lib, "dbus_message_iter_open_container");
    unsigned (*close)(IgDBusIter *, IgDBusIter *) = dlsym(lib, "dbus_message_iter_close_container");
    unsigned (*append)(IgDBusIter *, int, const void *) = dlsym(lib, "dbus_message_iter_append_basic");
    assert(new_message && init_append && open && close && append);
    for (int depth = 0; depth <= 2; depth++) {
        void *msg = new_message(2); // method return
        assert(msg);
        IgDBusIter it, outer, inner, channels;
        init_append(msg, &it);
        IgDBusIter *parent = &it;
        if (depth == 2) { assert(open(parent, 'v', "v", &outer)); parent = &outer; }
        if (depth) { assert(open(parent, 'v', "(ddd)", &inner)); parent = &inner; }
        assert(open(parent, 'r', NULL, &channels));
        double rgb[] = {0.25, 0.5, 1.0};
        for (int i = 0; i < 3; i++) assert(append(&channels, 'd', &rgb[i]));
        assert(close(parent, &channels));
        if (depth) assert(close(depth == 2 ? &outer : &it, &inner));
        if (depth == 2) assert(close(&it, &outer));
        assert(g_fm1.iter_init(msg, &it));
        assert(system_accent_value(&it) == 0x014080FF);
        g_fm1.message_unref(msg);
    }
    void *msg = new_message(2);
    const char *bad = "not a color";
    assert(g_fm1.append_args(msg, 's', &bad, 0));
    IgDBusIter it;
    assert(g_fm1.iter_init(msg, &it));
    assert(system_accent_value(&it) == 0);
    g_fm1.message_unref(msg);
    // Self-tests must never query a real desktop session.
    g_accent_system = true;
    system_accent_start();
    assert(!g_system_appearance.started);
}

int main(void) {
    test_follow_system_and_scrollbars();
    test_tab_preferences();
    test_tab_highlight();
    test_sidebar_accent();
    test_theme_capture();
    test_surfaces();
    test_integrated_header();
    test_system_palette();
    test_portal_messages();
    puts("Appearance checks passed: independent/reversible surfaces, tab/header preferences, shared-row geometry, control visibility, accent contrast, system palette/tint, accent fallback, RGB validation, real D-Bus decoding.");
    return 0;
}
