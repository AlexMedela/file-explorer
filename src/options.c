// options.c - Options window (Ctrl+,): every setting in one searchable sheet (included from ui_tools.c)

static const char *g_opt_cats[OC_COUNT] = {"Appearance", "Behaviour", "Views", "Keyboard", "Layouts", "Integration", "Updates", "Advanced"};

enum { OK_BOOL, OK_CHOICE, OK_INT, OK_ACCENT, OK_BUTTON };

enum {
    OPT_THEME, OPT_ACCENT, OPT_ICON_THEME, OPT_FONT, OPT_FONT_SIZE, OPT_ROW_SPACING, OPT_ZOOM, OPT_DIM_INACTIVE,
    OPT_ALT_ROWS, OPT_SIZE_BARS, OPT_LOAD_TIMES, OPT_SMOOTH, OPT_THEME_NEW, OPT_THEME_FOLDER, OPT_ICON_FOLDER, OPT_TERM_LOOK,
    OPT_HIDDEN, OPT_FOLDER_SIZES, OPT_GIT_STATUS, OPT_SEARCH_SUB, OPT_RESTORE, OPT_PREVIEW, OPT_SINGLE_CLICK, OPT_DBLCLICK_UP, OPT_DROP_ASK, OPT_TRANSFERS, OPT_VERIFY, OPT_KEYS_RESET, OPT_SINGLE_INSTANCE, OPT_AUTOSTART, OPT_TRAY,
    OPT_DEF_VIEW, OPT_VIEW_PER_FOLDER, OPT_DEF_SORT, OPT_SORT_DESC, OPT_DEF_GROUP,
    OPT_RENDERER, OPT_SETTINGS_FILE, OPT_REINDEX, OPT_TITLE_BAR, OPT_IG_DEFAULT_FM, OPT_IG_FM1, OPT_IG_PICKER, OPT_IG_UPDATES,
    OPT_UPD_NOW, OPT_UPD_HOW, OPT_UPD_NOTES, OPT_UPD_AUTO,
};

typedef struct {
    int cat, kind, id;
    const char *label, *desc;
} OptDef;

static const OptDef g_opts[] = {
    {OC_APPEARANCE, OK_CHOICE, OPT_THEME, "Theme", "Colour scheme (add your own JSON schemes below)"},
    {OC_APPEARANCE, OK_ACCENT, OPT_ACCENT, "Accent colour", "Selection, focus and highlight colour"},
    {OC_APPEARANCE, OK_CHOICE, OPT_ICON_THEME, "File icons", "Icon set for files and folders"},
    {OC_APPEARANCE, OK_CHOICE, OPT_FONT, "Font", "Interface font family"},
    {OC_APPEARANCE, OK_INT, OPT_FONT_SIZE, "Font size", "Text size at 100% zoom"},
    {OC_APPEARANCE, OK_CHOICE, OPT_ROW_SPACING, "Row spacing", "Height of rows in the details and list views"},
    {OC_APPEARANCE, OK_INT, OPT_ZOOM, "Interface zoom", "Scales the whole window (Ctrl+= / Ctrl+-)"},
    {OC_APPEARANCE, OK_BOOL, OPT_DIM_INACTIVE, "Dim inactive panels", "Shade panels that don't have focus"},
    {OC_APPEARANCE, OK_BOOL, OPT_ALT_ROWS, "Alternating row colours", "Zebra stripes in the details view"},
    {OC_APPEARANCE, OK_BOOL, OPT_SIZE_BARS, "Folder size bars", "Bars behind folder sizes, relative to the largest"},
    {OC_APPEARANCE, OK_BOOL, OPT_LOAD_TIMES, "Load times", "How long the folder took to list, in the status bar"},
    {OC_APPEARANCE, OK_BOOL, OPT_SMOOTH, "Smooth scrolling", "Animate scrolling instead of jumping"},
    {OC_APPEARANCE, OK_CHOICE, OPT_TITLE_BAR, "Title bar", "Window buttons and dragging: Explorer's own on every desktop (Automatic), or the desktop's title bar (System)"},
    {OC_APPEARANCE, OK_CHOICE, OPT_TERM_LOOK, "Terminal appearance", "Font, colours, padding and cursor of terminal tabs (F4)"},
    {OC_APPEARANCE, OK_BUTTON, OPT_THEME_NEW, "Create theme from current colours", "Writes a JSON scheme you can edit"},
    {OC_APPEARANCE, OK_BUTTON, OPT_THEME_FOLDER, "Themes folder", "Open the folder and reload theme files"},
    {OC_APPEARANCE, OK_BUTTON, OPT_ICON_FOLDER, "Icon themes folder", "Add your own file icons (SVG); opens the folder and reloads them"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_HIDDEN, "Show hidden files", "Files and folders starting with a dot (Ctrl+H)"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_FOLDER_SIZES, "Calculate folder sizes", "Fill the Size column for folders in the background"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_GIT_STATUS, "Git status", "Mark changed, new and ignored files in Git repositories; branch in the status bar"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_SEARCH_SUB, "Search in subfolders", "Default for the search bar toggle (Alt+R)"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_PREVIEW, "Preview panel", "Show the preview panel (Space / Ctrl+I)"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_RESTORE, "Restore tabs on startup", "Reopen the panels and tabs from last time"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_SINGLE_INSTANCE, "Open in the running window", "Folders opened from other apps or the terminal become tabs here"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_AUTOSTART, "Start Explorer at login", "Starts in the background when you log in, ready to open instantly"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_TRAY, "Keep running in the tray when closed", "Closing the window hides it; quit from the tray icon or Ctrl+Q (needs a tray in your panel)"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_SINGLE_CLICK, "Single-click to open", "Open files and folders with one click"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_DBLCLICK_UP, "Double-click empty space to go up", "Like File Pilot: opens the parent folder"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_DROP_ASK, "Ask what to do when dropping files", "Move, copy or link: a menu after each drop (holding Ctrl, Shift or both still decides at once)"},
    {OC_BEHAVIOUR, OK_CHOICE, OPT_TRANSFERS, "Big transfers", "Copies and moves wait their turn in the transfer queue (Ctrl+J); small ones never wait"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_VERIFY, "Verify after copy", "Read each copied file back and compare checksums; a copy that differs is removed"},
    {OC_KEYBOARD, OK_BUTTON, OPT_KEYS_RESET, "Reset all shortcuts", "Click a key to change it, + to add one"},
    {OC_VIEWS, OK_CHOICE, OPT_DEF_VIEW, "Default view", "New tabs open in this view; a tab keeps the view you pick while you browse"},
    {OC_VIEWS, OK_BOOL, OPT_VIEW_PER_FOLDER, "Each folder keeps its own view", "Opening a folder switches to the view last used there (else the default)"},
    {OC_VIEWS, OK_CHOICE, OPT_DEF_SORT, "Default sort", "Sort column for new tabs"},
    {OC_VIEWS, OK_BOOL, OPT_SORT_DESC, "Sort descending", "Reverse the default sort order"},
    {OC_VIEWS, OK_CHOICE, OPT_DEF_GROUP, "Default grouping", "Group items in the details view"},
    {OC_INTEGRATION, OK_BOOL, OPT_IG_DEFAULT_FM, "Default file manager", "Folders from other apps open here"},
    {OC_INTEGRATION, OK_BOOL, OPT_IG_FM1, "\"Show in folder\" opens here", "From browsers and other apps"},
    {OC_INTEGRATION, OK_BOOL, OPT_IG_PICKER, "File chooser for other apps", "Their Open and Save dialogs open here"},
    {OC_UPDATES, OK_BUTTON, OPT_UPD_NOW, APP_LABEL " " APP_VERSION, "How this copy updates"},
    {OC_UPDATES, OK_BUTTON, OPT_UPD_HOW, "How it updates", "Where new versions come from"},
    {OC_UPDATES, OK_BUTTON, OPT_UPD_NOTES, "What's new", "Release notes of every version, on GitHub"},
    {OC_UPDATES, OK_BOOL, OPT_IG_UPDATES, "Check for updates", "Once a day; a new version shows a card once"},
    {OC_UPDATES, OK_BOOL, OPT_UPD_AUTO, "Download updates by themselves", "Then the card only asks you to restart"},
    {OC_ADVANCED, OK_CHOICE, OPT_RENDERER, "Renderer", "GPU backend (takes effect after a restart)"},
    {OC_ADVANCED, OK_BUTTON, OPT_SETTINGS_FILE, "Settings file", "Open settings.conf in your text editor"},
    {OC_ADVANCED, OK_BUTTON, OPT_REINDEX, "Re-index folders", "Rebuild the folder index used by Go to (Ctrl+P)"},
};
#define OPT_COUNT ((int)ARRAY_COUNT(g_opts))

// Section headings: each starts a card with the option it names and runs until the next one.
static const struct { int id; const char *title; } g_opt_sections[] = {
    {OPT_THEME, "Theme"}, {OPT_FONT, "Text"}, {OPT_DIM_INACTIVE, "Display"}, {OPT_TERM_LOOK, "Terminal"}, {OPT_THEME_NEW, "Custom themes"},
    {OPT_HIDDEN, "Files and folders"}, {OPT_RESTORE, "Startup"}, {OPT_SINGLE_CLICK, "Mouse"}, {OPT_TRANSFERS, "Copying and moving"},
    {OPT_KEYS_RESET, "Shortcuts"}, {OPT_DEF_VIEW, "New folders"}, {OPT_RENDERER, "Rendering"}, {OPT_SETTINGS_FILE, "Maintenance"},
    {OPT_IG_DEFAULT_FM, "Desktop roles (yours to give: installing never takes them)"},
    {OPT_UPD_NOW, "Version"}, {OPT_IG_UPDATES, "Automatic updates"},
};

static const char *opt_section_title(int id) {
    for (size_t k = 0; k < ARRAY_COUNT(g_opt_sections); k++)
        if (g_opt_sections[k].id == id) return g_opt_sections[k].title;
    return NULL;
}

static const char *g_sort_labels[SORT_COUNT] = {"Name", "Date modified", "Type", "Size", "Date created", "Extension",
                                                "Permissions", "Owner"};
static const char *g_group_labels[GROUP_COUNT] = {"None", "Date", "Type", "Size"};
static const char *g_spacing_labels[3] = {"Compact", "Normal", "Comfortable"};
static const char *g_title_bar_labels[] = {"Automatic", "System", "Explorer's own"}; // TITLEBAR_*
static const char *g_renderers[] = {"auto", "vulkan", "opengl", "software"};
static const char *g_renderer_labels[] = {"Automatic", "Vulkan", "OpenGL", "Software"};
static const int g_transfer_counts[] = {1, 2, 4, 0}; // A.transfers_at_once (0 = no limit)
static const char *g_transfer_labels[] = {"One at a time", "Two at a time", "Four at a time", "All at once"};

// hit parts recorded while drawing, tested on click
enum { HP_NONE, HP_NAV, HP_TOGGLE, HP_SEG, HP_DROP, HP_MINUS, HP_PLUS, HP_SWATCH, HP_BUTTON, HP_DROPITEM, HP_SEARCH, HP_CLOSE,
       HP_KEYCHIP, HP_KEYDEL, HP_KEYADD, HP_KEYRESET, HP_LNAME, HP_LSAVE, HP_LOPEN, HP_LUPDATE, HP_LDELETE };
typedef struct { Rect r; int part, opt, arg; } OptHit;

static struct {
    int cat;
    TextEdit search;
    f32 scroll, max_scroll;
    OptHit hits[256];
    int nhits;
    int drop_opt;   // option whose dropdown is open (-1 none)
    Rect drop_r;
    int drop_sel, drop_top;
    TextEdit drop_filter;
    int drop_map[1024]; // filtered index -> choice index
    int drop_n;
    char **families; // font families (loaded on first use)
    int nfamilies;
    int hover_row;
    // shortcut recorder
    int rec_def, rec_slot; // keydef index (-1 idle), slot to replace (-1 = add)
    KeySeq rec;
    // layouts page
    TextEdit lname;
    bool lname_focus;
} g_opt;

// The description, with live state for the integration switches.
static const char *opt_desc(const OptDef *o) {
    static char buf[320];
    switch (o->id) {
    case OPT_IG_DEFAULT_FM: {
        const char *id = ig_folder_app();
        DesktopApp *a = id[0] ? app_by_id(id) : NULL;
        if (!ig_default_fm_on()) { snprintf(buf, sizeof buf, "Folders open in %s now", a ? a->name : "no app"); return buf; }
        break;
    }
    case OPT_IG_FM1:
        if (A.ig_fm1 && g_fm1.state == IG_FM1_TAKEN) return "Another file manager has it now";
        break;
    case OPT_IG_PICKER:
        if (!ig_picker_portal_installed()) return "Needs the termfilechooser portal";
        break;
    case OPT_UPD_NOW: return upd_status();
    case OPT_UPD_HOW: return upd_how();
    }
    return o->desc;
}

static void opt_hit(Rect r, int part, int opt, int arg) {
    if (g_opt.nhits < (int)ARRAY_COUNT(g_opt.hits)) g_opt.hits[g_opt.nhits++] = (OptHit){r, part, opt, arg};
}

// --- model ---------------------------------------------------------------------

static void opt_load_families(void) {
    if (!g_opt.families) g_opt.nfamilies = font_families(&g_opt.families);
}

static int opt_choice_count(int id) {
    switch (id) {
    case OPT_THEME: return g_nthemes;
    case OPT_ICON_THEME: return icon_theme_count();
    case OPT_TERM_LOOK: return 2;
    case OPT_TITLE_BAR: return (int)ARRAY_COUNT(g_title_bar_labels);
    case OPT_FONT: opt_load_families(); return 1 + g_opt.nfamilies;
    case OPT_ROW_SPACING: return 3;
    case OPT_DEF_VIEW: return ZOOM_COUNT;
    case OPT_DEF_SORT: return SORT_COUNT;
    case OPT_DEF_GROUP: return GROUP_COUNT;
    case OPT_RENDERER: return (int)ARRAY_COUNT(g_renderers);
    case OPT_TRANSFERS: return (int)ARRAY_COUNT(g_transfer_counts);
    }
    return 0;
}

static const char *opt_choice_label(int id, int i) {
    switch (id) {
    case OPT_THEME: return g_themes[i].name;
    case OPT_ICON_THEME: return icon_theme_name(i);
    case OPT_TERM_LOOK: {
        static char lab[96];
        if (i == 1) return "Explorer theme";
        char which[32];
        tc_default_terminal(which, sizeof which);
        if (which[0]) snprintf(lab, sizeof lab, "Match default terminal (%s)", which);
        else snprintf(lab, sizeof lab, "Match default terminal");
        return lab;
    }
    case OPT_FONT: return i == 0 ? "Automatic" : g_opt.families[i - 1];
    case OPT_ROW_SPACING: return g_spacing_labels[i];
    case OPT_DEF_VIEW: return g_zoom[i].label;
    case OPT_DEF_SORT: return g_sort_labels[i];
    case OPT_DEF_GROUP: return g_group_labels[i];
    case OPT_RENDERER: return g_renderer_labels[i];
    case OPT_TITLE_BAR: return g_title_bar_labels[i];
    case OPT_TRANSFERS: return g_transfer_labels[i];
    }
    return "";
}

static int opt_choice_get(int id) {
    switch (id) {
    case OPT_THEME: return g_theme_cur;
    case OPT_ICON_THEME: return g_icon_theme;
    case OPT_TERM_LOOK: return g_term_look_setting;
    case OPT_TITLE_BAR: return A.title_bar;
    case OPT_FONT:
        if (!A.ui_font[0]) return 0;
        opt_load_families();
        for (int i = 0; i < g_opt.nfamilies; i++)
            if (str_ieq(g_opt.families[i], A.ui_font)) return i + 1;
        return 0;
    case OPT_ROW_SPACING: return A.row_spacing;
    case OPT_DEF_VIEW:
        for (int i = 0; i < ZOOM_COUNT; i++)
            if (g_zoom[i].view == A.def_view && (A.def_view != VIEW_GRID || g_zoom[i].px == A.def_icon)) return i;
        return A.def_view == VIEW_GRID ? 4 : 0;
    case OPT_DEF_SORT: return A.def_sort_key;
    case OPT_DEF_GROUP: return A.def_group;
    case OPT_RENDERER:
        for (int i = 0; i < (int)ARRAY_COUNT(g_renderers); i++)
            if (!strcmp(A.renderer, g_renderers[i])) return i;
        return 0;
    case OPT_TRANSFERS:
        for (int i = 0; i < (int)ARRAY_COUNT(g_transfer_counts); i++)
            if (A.transfers_at_once == g_transfer_counts[i]) return i;
        return 0;
    }
    return 0;
}

// Apply a UI font family ("" = automatic). Returns false if it can't be loaded.
static bool apply_ui_font(const char *family) {
    char *path = family[0] ? font_family_file(family) : find_font_file(g_ui_families, (int)ARRAY_COUNT(g_ui_families));
    if (!path) return false;
    if (!font_replace(&A.font, path) || !font_replace(&A.font_small, path)) { free(path); return false; }
    char *bold = font_weight_file(path, FC_WEIGHT_SEMIBOLD);
    font_replace(&A.font_brand, bold ? bold : path);
    free(bold);
    free(A.font_path);
    A.font_path = path;
    snprintf(A.ui_font, sizeof A.ui_font, "%s", family);
    atlas_reset();
    return true;
}

static void opt_choice_set(int id, int i) {
    switch (id) {
    case OPT_THEME: g_theme_cur = i; theme_apply(); break;
    case OPT_ICON_THEME: icon_set_theme(i); break;
    case OPT_TERM_LOOK: g_term_look_setting = i; tc_reload_all(); break;
    case OPT_TITLE_BAR: A.title_bar = i; titlebar_apply(); break;
    case OPT_FONT:
        if (!apply_ui_font(i == 0 ? "" : g_opt.families[i - 1])) toast("Couldn't load that font");
        break;
    case OPT_ROW_SPACING: A.row_spacing = i; break;
    case OPT_DEF_VIEW:
        A.def_view = g_zoom[i].view;
        if (g_zoom[i].view == VIEW_GRID) A.def_icon = g_zoom[i].px;
        break;
    case OPT_DEF_SORT: A.def_sort_key = i; break;
    case OPT_DEF_GROUP: A.def_group = i; break;
    case OPT_RENDERER:
        snprintf(A.renderer, sizeof A.renderer, "%s", g_renderers[i]);
        toast("Renderer: %s (after restart)", g_renderer_labels[i]);
        break;
    case OPT_TRANSFERS: A.transfers_at_once = g_transfer_counts[i]; break;
    }
    A.settings_dirty = true;
}

static bool opt_bool_get(int id) {
    switch (id) {
    case OPT_DIM_INACTIVE: return A.dim_inactive;
    case OPT_ALT_ROWS: return A.alt_rows;
    case OPT_SIZE_BARS: return A.size_bars;
    case OPT_LOAD_TIMES: return A.load_times;
    case OPT_SMOOTH: return A.smooth_scroll;
    case OPT_HIDDEN: return A.show_hidden;
    case OPT_FOLDER_SIZES: return A.folder_sizes;
    case OPT_GIT_STATUS: return A.git_status;
    case OPT_SEARCH_SUB: return A.search_recursive;
    case OPT_RESTORE: return A.restore_session;
    case OPT_PREVIEW: return A.preview_on;
    case OPT_SORT_DESC: return A.def_sort_desc;
    case OPT_VIEW_PER_FOLDER: return A.view_per_folder;
    case OPT_SINGLE_CLICK: return A.single_click;
    case OPT_DBLCLICK_UP: return A.dblclick_up;
    case OPT_DROP_ASK: return A.drop_ask;
    case OPT_SINGLE_INSTANCE: return A.single_instance;
    case OPT_AUTOSTART: return autostart_enabled();
    case OPT_TRAY: return A.close_to_tray;
    case OPT_VERIFY: return A.verify_copies;
    case OPT_IG_DEFAULT_FM: return ig_default_fm_on();
    case OPT_IG_FM1: return A.ig_fm1;
    case OPT_IG_PICKER: return ig_picker_on();
    case OPT_IG_UPDATES: return A.ig_check_updates;
    case OPT_UPD_AUTO: return A.upd_auto;
    }
    return false;
}

static void opt_bool_toggle(int id) {
    switch (id) {
    case OPT_DIM_INACTIVE: A.dim_inactive = !A.dim_inactive; break;
    case OPT_ALT_ROWS: A.alt_rows = !A.alt_rows; break;
    case OPT_SIZE_BARS: A.size_bars = !A.size_bars; break;
    case OPT_LOAD_TIMES: A.load_times = !A.load_times; break;
    case OPT_SMOOTH: A.smooth_scroll = !A.smooth_scroll; break;
    case OPT_HIDDEN: do_action(active_pane(), ACT_HIDDEN); break;
    case OPT_FOLDER_SIZES: A.folder_sizes = !A.folder_sizes; break;
    case OPT_GIT_STATUS: do_action(active_pane(), ACT_GIT_STATUS); break;
    case OPT_SEARCH_SUB: A.search_recursive = !A.search_recursive; break;
    case OPT_RESTORE: A.restore_session = !A.restore_session; break;
    case OPT_PREVIEW: preview_toggle(); break;
    case OPT_SORT_DESC: A.def_sort_desc = !A.def_sort_desc; break;
    case OPT_VIEW_PER_FOLDER: A.view_per_folder = !A.view_per_folder; break;
    case OPT_SINGLE_CLICK: A.single_click = !A.single_click; break;
    case OPT_DBLCLICK_UP: A.dblclick_up = !A.dblclick_up; break;
    case OPT_DROP_ASK: A.drop_ask = !A.drop_ask; break;
    case OPT_SINGLE_INSTANCE:
        A.single_instance = !A.single_instance;
        if (A.single_instance && g_ipc_fd < 0) ipc_listen(ipc_handle);
        break;
    case OPT_AUTOSTART: autostart_set(!autostart_enabled()); break;
    case OPT_VERIFY: A.verify_copies = !A.verify_copies; break;
    case OPT_IG_DEFAULT_FM: ig_default_fm_set(!ig_default_fm_on()); break;
    case OPT_IG_FM1:
        ig_fm1_set(!A.ig_fm1);
        if (g_fm1.state == IG_FM1_TAKEN) toast("Another file manager is answering now; this one takes over when it isn't running");
        else if (g_fm1.state == IG_FM1_NO_DBUS) toast("No D-Bus session bus (libdbus-1) found");
        break;
    case OPT_IG_PICKER: ig_picker_set(!ig_picker_on()); break;
    case OPT_IG_UPDATES: A.ig_check_updates = !A.ig_check_updates; break;
    case OPT_UPD_AUTO: A.upd_auto = !A.upd_auto; break;
    case OPT_TRAY:
        A.close_to_tray = !A.close_to_tray;
        tray_update();
        if (A.close_to_tray && !g_tray) toast("No tray found in your panel: closing the window will still quit");
        break;
    }
    A.settings_dirty = true;
}

static int opt_int_get(int id) {
    switch (id) {
    case OPT_FONT_SIZE: return A.font_size;
    case OPT_ZOOM: return (int)lroundf(A.zoom * 100);
    }
    return 0;
}

static void opt_int_step(int id, int dir) {
    switch (id) {
    case OPT_FONT_SIZE:
        A.font_size = CLAMP(A.font_size + dir, 9, 24);
        reload_fonts();
        break;
    case OPT_ZOOM: set_zoom(A.zoom + 0.1f * (f32)dir); break;
    }
    A.settings_dirty = true;
}

static void opt_int_fmt(int id, char *buf, size_t cap) {
    if (id == OPT_ZOOM) snprintf(buf, cap, "%d%%", opt_int_get(id));
    else snprintf(buf, cap, "%d px", opt_int_get(id));
}

static void opt_button(int id) {
    char dir[PATH_MAX];
    switch (id) {
    case OPT_THEME_NEW: {
        themes_dir(dir, sizeof dir);
        mkdir_p(dir, 0700);
        char name[64], path[PATH_MAX + 80];
        for (int n = 1; n < 1000; n++) {
            snprintf(name, sizeof name, n == 1 ? "My theme" : "My theme %d", n);
            snprintf(path, sizeof path, "%s/%s.json", dir, name);
            if (access(path, F_OK) != 0 && theme_find(name) < 0) break;
        }
        if (!theme_export(path, name)) { toast("Couldn't write %s", path); break; }
        themes_scan();
        int t = theme_find(name);
        if (t >= 0) { g_theme_cur = t; theme_apply(); A.settings_dirty = true; }
        toast("Created %s.json - edit it and reopen Options to reload", name);
        open_path(path);
        break;
    }
    case OPT_ICON_FOLDER:
        icon_themes_dir(dir, sizeof dir);
        mkdir_p(dir, 0700);
        icon_themes_readme(dir);
        icon_themes_reload();
        A.settings_dirty = true;
        toast(g_ncit ? "Reloaded %d custom icon theme%s" : "Add a folder of SVG icons here (see README.txt)", g_ncit, g_ncit == 1 ? "" : "s");
        open_path(dir);
        break;
    case OPT_THEME_FOLDER:
        themes_dir(dir, sizeof dir);
        mkdir_p(dir, 0700);
        themes_scan();
        theme_apply();
        open_path(dir);
        break;
    case OPT_SETTINGS_FILE: run_command(CMD_OPEN_SETTINGS); break;
    case OPT_REINDEX: run_command(CMD_REINDEX); break;
    case OPT_UPD_NOW: upd_button_press(); break;
    case OPT_UPD_NOTES: upd_notes(); break;
    case OPT_UPD_HOW:
        if (upd_how_cmd()) { SDL_SetClipboardText(upd_how_cmd()); toast("Copied: %s", upd_how_cmd()); }
        break;
    case OPT_KEYS_RESET:
        keys_defaults();
        A.settings_dirty = true;
        toast("All shortcuts reset to defaults");
        break;
    }
}

// Rows that don't apply to this copy (updating by itself: AppImage only).
static bool opt_hidden(int id) {
    if (id == OPT_UPD_HOW) return upd_can_install(); // (the AppImage's own rows say it)
    return (id == OPT_IG_UPDATES || id == OPT_UPD_AUTO) && !upd_can_install();
}

static bool opt_matches(const OptDef *o, const char *q) {
    if (opt_hidden(o->id)) return false;
    if (!q[0]) return true;
    if (strcasestr(o->label, q) || strcasestr(o->desc, q) || strcasestr(g_opt_cats[o->cat], q)) return true;
    if (o->kind == OK_CHOICE && o->id != OPT_FONT) // match choice values too ("light", "vulkan")
        for (int i = 0; i < opt_choice_count(o->id); i++)
            if (strcasestr(opt_choice_label(o->id, i), q)) return true;
    return false;
}

// --- open / close ----------------------------------------------------------------

static void options_open(void) {
    if (g_sheet.kind) sheet_close();
    g_sheet.kind = SHEET_OPTIONS;
    int keep = g_opt.cat;
    char **fam = g_opt.families;
    int nfam = g_opt.nfamilies;
    memset(&g_opt, 0, sizeof g_opt);
    g_opt.cat = keep;
    g_opt.families = fam;
    g_opt.nfamilies = nfam;
    g_opt.drop_opt = -1;
    g_opt.hover_row = -1;
    g_opt.rec_def = -1;
    themes_scan(); // pick up new/edited theme files
    theme_apply();
    A.redraw = true;
}

static void opt_drop_filter(void) {
    int id = g_opt.drop_opt, n = opt_choice_count(id);
    g_opt.drop_n = 0;
    for (int i = 0; i < n && g_opt.drop_n < (int)ARRAY_COUNT(g_opt.drop_map); i++)
        if (!g_opt.drop_filter.buf[0] || strcasestr(opt_choice_label(id, i), g_opt.drop_filter.buf))
            g_opt.drop_map[g_opt.drop_n++] = i;
    g_opt.drop_sel = 0;
    int cur = opt_choice_get(id);
    for (int k = 0; k < g_opt.drop_n; k++)
        if (g_opt.drop_map[k] == cur) g_opt.drop_sel = k;
    g_opt.drop_top = MAX(0, g_opt.drop_sel - 3);
}

static void opt_drop_open(int id, Rect r) {
    g_opt.drop_opt = id;
    g_opt.drop_r = r;
    te_set(&g_opt.drop_filter, "", 0, 0);
    opt_drop_filter();
}

static void opt_drop_pick(int k) {
    if (k >= 0 && k < g_opt.drop_n) opt_choice_set(g_opt.drop_opt, g_opt.drop_map[k]);
    g_opt.drop_opt = -1;
}

// --- drawing ---------------------------------------------------------------------

static void opt_toggle_draw(Rect t, bool on, bool hv) {
    f32 kr = t.h * 0.5f - S(2.5f);
    draw_rrect(t.x, t.y, t.w, t.h, t.h * 0.5f, on ? C(hv ? COL_ACCENT_HOVER : COL_ACCENT) : C(hv ? COL_CONTROL_HOVER : COL_CHIP));
    f32 kx = on ? t.x + t.w - t.h * 0.5f : t.x + t.h * 0.5f;
    draw_rrect(kx - kr, t.y + t.h * 0.5f - kr, kr * 2, kr * 2, kr, C(on ? COL_ON_ACCENT : COL_KNOB_OFF));
}

static bool opt_hover(Rect r) { return hot(r); }

// Control area of one row (right-aligned inside [x, x + w]).
static void opt_draw_control(const OptDef *o, f32 x, f32 cy, f32 w) {
    Font *f = &A.font, *fs = &A.font_small;
    f32 ch = roundf(S(30));
    f32 y = roundf(cy - ch * 0.5f);
    char buf[64];
    switch (o->kind) {
    case OK_BOOL: {
        Rect t = {x + w - roundf(S(40)), roundf(cy - S(10)), roundf(S(40)), roundf(S(20))};
        opt_toggle_draw(t, opt_bool_get(o->id), opt_hover(t));
        opt_hit(t, HP_TOGGLE, o->id, 0);
        break;
    }
    case OK_INT: {
        f32 bw = roundf(S(30)), vw = roundf(S(70));
        Rect plus = {x + w - bw, y, bw, ch}, val = {plus.x - vw, y, vw, ch}, minus = {val.x - bw, y, bw, ch};
        draw_rrect(minus.x, y, bw * 2 + vw, ch, S(7), C(COL_FIELD));
        if (opt_hover(minus)) draw_rrect(minus.x, y, bw, ch, S(7), C(COL_CONTROL_HOVER));
        if (opt_hover(plus)) draw_rrect(plus.x, y, bw, ch, S(7), C(COL_CONTROL_HOVER));
        draw_minus(minus.x + bw * 0.5f, cy, S(10), MAX(2.f, S(1.6f)), C(COL_TEXT));
        draw_plus(plus.x + bw * 0.5f, cy, S(10), MAX(2.f, S(1.6f)), C(COL_TEXT));
        opt_int_fmt(o->id, buf, sizeof buf);
        draw_text_fit(f, val.x, text_y(f, y, ch), vw, buf, (int)strlen(buf), C(COL_TEXT), 2);
        opt_hit(minus, HP_MINUS, o->id, 0);
        opt_hit(plus, HP_PLUS, o->id, 0);
        break;
    }
    case OK_CHOICE: {
        int n = opt_choice_count(o->id), cur = opt_choice_get(o->id);
        if (n <= 3 && o->id != OPT_THEME && o->id != OPT_FONT) { // segmented
            f32 sw = roundf(S(96)), x0 = x + w - sw * (f32)n;
            draw_rrect(x0, y, sw * (f32)n, ch, S(7), C(COL_FIELD));
            for (int i = 0; i < n; i++) {
                Rect r = {x0 + sw * (f32)i, y, sw, ch};
                bool on = i == cur;
                if (on) draw_rrect(r.x + S(2), r.y + S(2), r.w - S(4), r.h - S(4), S(6), C(COL_ACCENT));
                else if (opt_hover(r)) draw_rrect(r.x + S(2), r.y + S(2), r.w - S(4), r.h - S(4), S(6), C(COL_CONTROL_HOVER));
                const char *l = opt_choice_label(o->id, i);
                draw_text_fit(fs, r.x + S(4), text_y(fs, y, ch), r.w - S(8), l, (int)strlen(l), C(on ? COL_ON_ACCENT : COL_TEXT), 2);
                opt_hit(r, HP_SEG, o->id, i);
            }
        } else { // dropdown button
            f32 dw = roundf(S(220));
            Rect r = {x + w - dw, y, dw, ch};
            bool open = g_opt.drop_opt == o->id;
            draw_rrect(r.x, r.y, r.w, r.h, S(7), C(open || opt_hover(r) ? COL_FIELD_HOVER : COL_FIELD));
            if (open) draw_rrect_outline(r.x, r.y, r.w, r.h, S(7), MAX(1.f, S(1.2f)), C(COL_ACCENT));
            const char *l = cur < n ? opt_choice_label(o->id, cur) : "";
            if (o->id == OPT_FONT && A.ui_font[0] && cur == 0) l = A.ui_font; // not in the list (yet)
            draw_text_fit(f, r.x + S(10), text_y(f, y, ch), r.w - S(36), l, (int)strlen(l), C(COL_TEXT), 0);
            draw_chevron(r.x + r.w - S(16), cy, S(6), 3, MAX(1.f, S(1.4f)), C(COL_TEXT_DIM));
            opt_hit(r, HP_DROP, o->id, 0);
        }
        break;
    }
    case OK_ACCENT: {
        f32 sz = roundf(S(22)), gap = roundf(S(8));
        // swatch 0 = the theme's own accent (dotted); hidden when that is one of the presets (Dark/Black: Blue),
        // which then stands for "theme default" itself
        u32 tdef = g_themes[g_theme_cur].t.accent;
        int same = -1;
        for (int i = 0; i < ACCENT_COUNT; i++)
            if (g_accents[i].rgb == tdef) same = i;
        int n = ACCENT_COUNT + (same < 0);
        f32 x0 = x + w - (sz + gap) * (f32)n + gap;
        for (int k = 0; k < n; k++) {
            int i = same < 0 ? k : k + 1; // 0 = theme default swatch, i >= 1 = preset i - 1
            Rect r = {x0 + (sz + gap) * (f32)k, roundf(cy - sz * 0.5f), sz, sz};
            u32 col = i == 0 ? tdef : g_accents[i - 1].rgb;
            bool is_default = i == 0 || i - 1 == same;
            bool on = is_default && !g_accent_custom ? true : i > 0 && g_accent_custom == (0x01000000u | g_accents[i - 1].rgb);
            if (on || opt_hover(r))
                draw_rrect_outline(r.x - S(3), r.y - S(3), sz + S(6), sz + S(6), (sz + S(6)) * 0.5f, MAX(1.f, S(1.5f)),
                                   C(on ? COL_TEXT : COL_TEXT_FAINT));
            draw_rrect(r.x, r.y, sz, sz, sz * 0.5f, C(col));
            if (i == 0) // the theme's own accent (not a preset): small dot
                draw_rrect(r.x + sz * 0.35f, r.y + sz * 0.35f, sz * 0.3f, sz * 0.3f, sz * 0.15f, C(COL_POPUP));
            opt_hit(r, HP_SWATCH, o->id, is_default ? 0 : i); // the theme default's preset clears the custom accent
        }
        if (g_accent_custom && !(g_accent_custom & 0x02000000u)) {
            bool preset = false;
            for (int i = 0; i < ACCENT_COUNT; i++) preset |= g_accent_custom == (0x01000000u | g_accents[i].rgb);
            if (!preset) {
                snprintf(buf, sizeof buf, "#%06X", g_accent_custom & 0xFFFFFF);
                f32 tw = text_width(fs, buf, (int)strlen(buf));
                draw_text(fs, x0 - tw - S(12), text_y(fs, y, ch), buf, (int)strlen(buf), C(COL_TEXT_DIM));
            }
        }
        break;
    }
    case OK_BUTTON: {
        bool primary = false, enabled = true;
        const char *l = o->id == OPT_THEME_NEW ? "Create" : o->id == OPT_REINDEX ? "Re-index" : o->id == OPT_KEYS_RESET ? "Reset"
                      : o->id == OPT_UPD_NOW ? upd_button(&primary, &enabled) : o->id == OPT_UPD_HOW ? (upd_how_cmd() ? "Copy" : NULL) : "Open";
        if (!l) break; // (nothing to press)
        f32 bw = MAX(roundf(S(96)), text_width(f, l, (int)strlen(l)) + S(28));
        Rect r = {x + w - bw, y, bw, ch};
        sheet_button(r, l, primary, enabled);
        if (enabled) opt_hit(r, HP_BUTTON, o->id, 0);
        break;
    }
    }
}

static void opt_draw_dropdown(void) {
    int id = g_opt.drop_opt;
    if (id < 0) return;
    Font *f = &A.font;
    Rect a = g_opt.drop_r;
    bool filter = opt_choice_count(id) > 12;
    f32 rh = roundf(S(28)), fh = filter ? roundf(S(34)) : 0;
    int vis = MIN(g_opt.drop_n, 10);
    f32 h = fh + rh * (f32)MAX(vis, 1) + S(8);
    f32 y = a.y + a.h + S(4);
    if (y + h > A.H - S(8)) y = a.y - h - S(4);
    Rect r = {a.x, y, a.w, h};
    draw_rrect(r.x, r.y, r.w, r.h, S(9), C(COL_POPUP));
    draw_rrect_outline(r.x, r.y, r.w, r.h, S(9), 1, C(COL_POPUP_BORDER));
    opt_hit(r, HP_NONE, id, 0); // swallow clicks on the popup frame
    // the highlight follows the mouse when it moves (like menus); arrow keys move it while the mouse rests
    static f32 last_mx = -1, last_my = -1;
    bool moved = A.mx != last_mx || A.my != last_my;
    last_mx = A.mx;
    last_my = A.my;
    f32 yy = r.y + S(4);
    if (filter) {
        Rect fr = {r.x + S(6), yy, r.w - S(12), fh - S(6)};
        draw_rrect(fr.x, fr.y, fr.w, fr.h, S(6), C(COL_FIELD));
        te_draw(&g_opt.drop_filter, f, fr, roundf(S(8)), C(COL_TEXT));
        if (!g_opt.drop_filter.buf[0])
            draw_text(f, fr.x + S(10), text_y(f, fr.y, fr.h), "Type to filter", 14, C(COL_TEXT_FAINT));
        yy += fh;
    }
    g_opt.drop_top = CLAMP(g_opt.drop_top, 0, MAX(0, g_opt.drop_n - vis));
    if (g_opt.drop_sel < g_opt.drop_top) g_opt.drop_top = g_opt.drop_sel;
    if (g_opt.drop_sel >= g_opt.drop_top + vis) g_opt.drop_top = g_opt.drop_sel - vis + 1;
    int cur = opt_choice_get(id);
    for (int k = g_opt.drop_top; k < g_opt.drop_top + vis && k < g_opt.drop_n; k++) {
        Rect ir = {r.x + S(4), yy, r.w - S(8), rh};
        if (moved && opt_hover(ir)) g_opt.drop_sel = k;
        bool sel = k == g_opt.drop_sel;
        if (sel) draw_rrect(ir.x, ir.y, ir.w, ir.h, S(6), C(COL_SEL_FOCUS));
        int ci = g_opt.drop_map[k];
        const char *l = opt_choice_label(id, ci);
        draw_text_fit(f, ir.x + S(26), text_y(f, ir.y, rh), ir.w - S(34), l, (int)strlen(l), C(sel ? COL_SEL_TEXT : COL_TEXT), 0);
        if (ci == cur) { // check mark
            f32 cx = ir.x + S(9), cyy = ir.y + rh * 0.5f, t = MAX(1.5f, S(1.6f));
            Color cc = C(sel ? COL_SEL_TEXT : COL_ACCENT);
            draw_line(cx, cyy, cx + S(3.5f), cyy + S(3.5f), t, cc);
            draw_line(cx + S(3.5f), cyy + S(3.5f), cx + S(10), cyy - S(4), t, cc);
        }
        opt_hit(ir, HP_DROPITEM, id, k);
        yy += rh;
    }
    if (!g_opt.drop_n) draw_text(f, r.x + S(14), text_y(f, yy, rh), "No matches", 10, C(COL_TEXT_FAINT));
}

// --- keyboard page --------------------------------------------------------------

static bool keydef_matches(const KeyDef *d, const char *q) {
    if (!q[0]) return true;
    if (strcasestr(cmd_label(d->id), q) || strcasestr(d->slug, q)) return true;
    for (int k = 0; k < d->nseq; k++) {
        char buf[96];
        seq_format(&d->seq[k], buf, sizeof buf);
        if (strcasestr(buf, q)) return true;
    }
    return false;
}

static f32 key_chip(f32 xr, f32 cy, const char *text, bool hot, bool recording) {
    Font *fs = &A.font_small;
    f32 tw = text_width(fs, text, (int)strlen(text)), h = roundf(S(24)), w = roundf(tw + S(18));
    Rect r = {xr - w, roundf(cy - h * 0.5f), w, h};
    draw_rrect(r.x, r.y, r.w, r.h, S(5), C(recording ? COL_FIELD_HOVER : hot ? COL_CONTROL_HOVER : COL_CONTROL));
    if (recording) draw_rrect_outline(r.x, r.y, r.w, r.h, S(5), MAX(1.f, S(1.3f)), C(COL_ACCENT));
    else draw_rect(r.x + S(3), r.y + r.h - 1, r.w - S(6), 1, C(COL_POPUP_BORDER)); // keycap edge
    draw_text(fs, r.x + S(9), text_y(fs, r.y, r.h), text, (int)strlen(text), C(recording ? COL_ACCENT : COL_TEXT));
    return r.x;
}

static void opt_draw_keys(int di, f32 x, f32 cy, f32 w) {
    KeyDef *d = &g_keys[di];
    f32 xr = x + w;
    f32 h = roundf(S(24));
    // reset (only when changed)
    if (!keydef_is_default(d)) {
        Font *fs = &A.font_small;
        const char *t = "Reset";
        f32 tw = text_width(fs, t, 5);
        Rect rr = {xr - tw - S(12), roundf(cy - h * 0.5f), tw + S(12), h};
        if (opt_hover(rr)) draw_rrect(rr.x, rr.y, rr.w, rr.h, S(5), C(COL_CHIP));
        draw_text(fs, rr.x + S(6), text_y(fs, rr.y, rr.h), t, 5, C(COL_ACCENT));
        opt_hit(rr, HP_KEYRESET, di, 0);
        xr = rr.x - S(8);
    }
    bool rec_here = g_opt.rec_def == di;
    // add
    if (d->nseq < MAX_KEYS_PER_CMD || (rec_here && g_opt.rec_slot < 0)) {
        char buf[96];
        bool adding = rec_here && g_opt.rec_slot < 0;
        if (adding && g_opt.rec.n) seq_format(&g_opt.rec, buf, sizeof buf);
        const char *t = adding ? (g_opt.rec.n ? buf : "Press keys\xE2\x80\xA6") : "+";
        f32 x0 = key_chip(xr, cy, t, false, adding);
        Rect ar = {x0, roundf(cy - h * 0.5f), xr - x0, h};
        if (!adding && opt_hover(ar)) key_chip(xr, cy, t, true, false);
        opt_hit(ar, HP_KEYADD, di, 0);
        xr = x0 - S(6);
    }
    for (int k = d->nseq - 1; k >= 0; k--) {
        char buf[96];
        bool rec = rec_here && g_opt.rec_slot == k;
        if (rec && g_opt.rec.n) seq_format(&g_opt.rec, buf, sizeof buf);
        else if (rec) snprintf(buf, sizeof buf, "Press keys\xE2\x80\xA6");
        else seq_format(&d->seq[k], buf, sizeof buf);
        Font *fs = &A.font_small;
        f32 cw = roundf(text_width(fs, buf, (int)strlen(buf)) + S(18));
        Rect cr = {xr - cw, roundf(cy - h * 0.5f), cw, h};
        bool hv = opt_hover(cr) && !rec;
        key_chip(xr, cy, buf, hv, rec);
        opt_hit(cr, HP_KEYCHIP, di, k);
        if (hv) { // remove button on the chip's corner
            Rect xb = {cr.x + cr.w - S(8), cr.y - S(6), roundf(S(14)), roundf(S(14))};
            draw_rrect(xb.x, xb.y, xb.w, xb.h, xb.w * 0.5f, C(COL_DANGER));
            draw_cross(xb.x + xb.w * 0.5f, xb.y + xb.h * 0.5f, S(3), MAX(1.f, S(1.3f)), C(COL_ON_ACCENT));
            opt_hit(xb, HP_KEYDEL, di, k);
        }
        xr = cr.x - S(6);
    }
    if (rec_here) {
        const char *hint = "Enter to save \xC2\xB7 Esc to cancel";
        Font *fs = &A.font_small;
        f32 tw = text_width(fs, hint, (int)strlen(hint));
        draw_text(fs, xr - tw - S(4), text_y(fs, cy - S(12), S(24)), hint, (int)strlen(hint), C(COL_TEXT_FAINT));
    }
}

static void opt_rec_start(int di, int slot) {
    g_opt.rec_def = di;
    g_opt.rec_slot = slot;
    memset(&g_opt.rec, 0, sizeof g_opt.rec);
}

static void opt_rec_commit(void) {
    if (g_opt.rec_def < 0) return;
    KeyDef *d = &g_keys[g_opt.rec_def];
    if (g_opt.rec.n) {
        int taken = keys_assign(d, g_opt.rec_slot, &g_opt.rec);
        if (taken >= 0) toast("Removed from \xE2\x80\x9C%s\xE2\x80\x9D", cmd_label(taken));
        A.settings_dirty = true;
    }
    g_opt.rec_def = -1;
}

// Recorder gets every key while active. Enter (with something recorded) saves, Esc cancels.
static void opt_rec_key(SDL_Keycode k, SDL_Keymod mod) {
    if (chord_is_modifier(k)) return;
    Chord c = {key_normalize(k), km_from_sdl(mod)};
    if (c.key == SDLK_ESCAPE && !c.mod) { g_opt.rec_def = -1; return; }
    if (c.key == SDLK_RETURN && !c.mod && g_opt.rec.n) { opt_rec_commit(); return; }
    if (g_opt.rec.n >= 2) g_opt.rec.n = 0; // third key: start over
    g_opt.rec.c[g_opt.rec.n++] = c;
}

// --- layouts page ----------------------------------------------------------------

static void opt_layout_save(void) {
    char name[64];
    snprintf(name, sizeof name, "%s", g_opt.lname.buf);
    bool existed = layout_find(name) >= 0;
    if (!layout_save_named(name)) { toast("Type a name for the layout first"); return; }
    toast(existed ? "Updated layout \xE2\x80\x9C%s\xE2\x80\x9D" : "Saved layout \xE2\x80\x9C%s\xE2\x80\x9D", name);
    te_set(&g_opt.lname, "", 0, 0);
}

static void layout_delete_cb(int choice, bool check, void *ud) {
    if (choice == 1) layout_delete((int)(intptr_t)ud);
}

static f32 opt_draw_layouts(f32 x, f32 y, f32 w, Rect list, const char *q) {
    Font *f = &A.font, *fs = &A.font_small;
    f32 rh = roundf(S(58)), ch = roundf(S(30)), gap = roundf(S(18));
    if (!q[0]) { // save row
        y = props_heading(x, y, "New layout");
        props_card(x, y, w, 1, rh, 0);
        if (y + rh > list.y && y < list.y + list.h) {
            draw_text(f, x + S(14), roundf(y + rh * 0.5f - f->line_h + S(2)), "Save current layout", 19, C(COL_TEXT));
            const char *d = "Panels, splits and tabs; open it again from here or Ctrl+P";
            draw_text_fit(fs, x + S(14), roundf(y + rh * 0.5f + S(3)), w * 0.45f, d, (int)strlen(d), C(COL_TEXT_DIM), 0);
            f32 bw = roundf(S(80)), fw = MIN(roundf(S(240)), w * 0.4f);
            Rect br = {x + w - S(12) - bw, roundf(y + (rh - ch) * 0.5f), bw, ch};
            Rect fr = {br.x - S(8) - fw, br.y, fw, ch};
            sheet_field(fr, &g_opt.lname, g_opt.lname_focus, "Layout name");
            sheet_button(br, layout_find(g_opt.lname.buf) >= 0 ? "Update" : "Save", true, g_opt.lname.buf[0] != 0);
            opt_hit(fr, HP_LNAME, -1, 0);
            opt_hit(br, HP_LSAVE, -1, 0);
        }
        y += rh + gap;
    }
    int nl = 0;
    for (int i = 0; i < g_nlayouts; i++) nl += !q[0] || strcasestr(g_layouts[i].name, q) || strcasestr("layouts", q);
    if (!q[0] && nl) y = props_heading(x, y, "Saved layouts");
    if (nl) props_card(x, y, w, nl, rh, 0);
    for (int i = 0; i < g_nlayouts; i++) {
        SavedLayout *l = &g_layouts[i];
        if (q[0] && !strcasestr(l->name, q) && !strcasestr("layouts", q)) continue;
        if (y + rh > list.y && y < list.y + list.h) {
            draw_text_fit(f, x + S(14), roundf(y + rh * 0.5f - f->line_h + S(2)), w * 0.45f, l->name, (int)strlen(l->name), C(COL_TEXT), 0);
            char d[96];
            snprintf(d, sizeof d, "%d panel%s \xC2\xB7 %d tab%s", l->npanels, l->npanels == 1 ? "" : "s", l->ntabs, l->ntabs == 1 ? "" : "s");
            draw_text(fs, x + S(14), roundf(y + rh * 0.5f + S(3)), d, (int)strlen(d), C(COL_TEXT_DIM));
            f32 bw = roundf(S(80)), by = roundf(y + (rh - ch) * 0.5f), bx = x + w - S(12) - bw;
            Rect rdel = {bx, by, bw, ch}, rupd = {bx - bw - S(8), by, bw, ch}, ropen = {rupd.x - bw - S(8), by, bw, ch};
            sheet_button(ropen, "Open", true, true);
            sheet_button(rupd, "Update", false, true);
            sheet_button(rdel, "Delete", false, true);
            opt_hit(ropen, HP_LOPEN, -1, i);
            opt_hit(rupd, HP_LUPDATE, -1, i);
            opt_hit(rdel, HP_LDELETE, -1, i);
        }
        y += rh;
    }
    if (!q[0] && !g_nlayouts)
        draw_text(fs, x + S(14), y + S(10), "No saved layouts yet", 20, C(COL_TEXT_FAINT));
    return y;
}

static void options_draw(void) {
    Sheet *s = &g_sheet;
    g_opt.nhits = 0;
    Font *f = &A.font, *fs = &A.font_small;
    f32 w = MIN((f32)A.W - S(60), roundf(S(860))), h = MIN((f32)A.H - S(60), roundf(S(640)));
    Rect r = {roundf((A.W - w) * 0.5f), roundf((A.H - h) * 0.5f), w, h};
    s->r = r;
    sheet_frame(r, "Options", NULL);
    f32 pad = roundf(S(20));
    // close button
    Rect cr = {r.x + r.w - pad - roundf(S(26)), r.y + pad - S(4), roundf(S(26)), roundf(S(26))};
    if (opt_hover(cr)) draw_rrect(cr.x, cr.y, cr.w, cr.h, S(6), C(COL_CHIP));
    draw_cross(cr.x + cr.w * 0.5f, cr.y + cr.h * 0.5f, S(7), MAX(1.f, S(1.5f)), C(opt_hover(cr) ? COL_TEXT : COL_TEXT_DIM));
    opt_hit(cr, HP_CLOSE, -1, 0);
    // search field
    f32 sw = MIN(roundf(S(300)), w * 0.45f), sh = roundf(S(30));
    Rect sr = {cr.x - S(10) - sw, r.y + pad - S(6), sw, sh};
    bool sfocus = g_opt.drop_opt < 0 && !g_opt.lname_focus;
    sheet_field(sr, &g_opt.search, sfocus, "Search settings");
    opt_hit(sr, HP_SEARCH, -1, 0);
    // nav (same active style as the sidebar: accent-tinted row with a full-height accent edge)
    f32 top = r.y + pad + roundf(S(44));
    f32 navw = roundf(S(176));
    const char *q = g_opt.search.buf;
    f32 ny = top;
    for (int c = 0; c < OC_COUNT; c++) {
        Rect nr = {r.x + S(12), ny, navw - S(32), roundf(S(30))}; // clear of the divider
        bool on = !q[0] && g_opt.cat == c;
        if (on) {
            draw_rrect(nr.x, nr.y, nr.w, nr.h, S(7), C(rgb_mix(COL_SEL_IDLE, COL_ACCENT, 0.16f)));
            ClipSave ac = draw_clip_push((SDL_Rect){(int)nr.x, (int)nr.y, (int)roundf(S(3)), (int)nr.h});
            draw_rrect(nr.x, nr.y, nr.w, nr.h, S(7), C(COL_ACCENT));
            draw_clip_pop(ac);
        } else if (opt_hover(nr)) draw_rrect(nr.x, nr.y, nr.w, nr.h, S(7), C(COL_HOVER));
        draw_text(f, nr.x + S(14), text_y(f, nr.y, nr.h), g_opt_cats[c], (int)strlen(g_opt_cats[c]), C(on ? COL_TEXT : COL_TEXT_DIM));
        opt_hit(nr, HP_NAV, -1, c);
        ny += nr.h + S(2);
    }
    // content: page title, then sections of cards
    Rect list = {r.x + navw, top, r.w - navw - pad + S(6), r.y + r.h - top - S(14)};
    draw_rect(list.x - S(8), top, 1, list.h, C(rgb_mix(COL_POPUP, COL_TEXT, 0.08f)));
    ClipSave cs = draw_clip_push((SDL_Rect){(int)list.x, (int)list.y, (int)list.w, (int)list.h});
    f32 rh = roundf(S(58)), krh = roundf(S(44)), gap = roundf(S(18));
    f32 cx = list.x + S(8), cwid = list.w - S(8) - roundf(S(16)); // cards (room for the scrollbar on the right)
    f32 y = list.y - g_opt.scroll + roundf(S(4));
    char ptitle[160];
    if (q[0]) snprintf(ptitle, sizeof ptitle, "Results for \xE2\x80\x9C%s\xE2\x80\x9D", q);
    else snprintf(ptitle, sizeof ptitle, "%s", g_opt_cats[g_opt.cat]);
    draw_text_fit(&A.font_brand, cx + S(2), y, cwid, ptitle, (int)strlen(ptitle), C(COL_TEXT_STRONG), 0);
    y += roundf(A.font_brand.line_h + S(14));
    int shown = 0;
    g_opt.hover_row = -1;
    // options: consecutive visible rows of one section share a card
    for (int i = 0; i < OPT_COUNT;) {
        const OptDef *o = &g_opts[i];
        if (q[0] ? !opt_matches(o, q) : o->cat != g_opt.cat || opt_hidden(o->id)) { i++; continue; }
        // the section: from here until the next section start (or category change in search results)
        const char *title = q[0] ? g_opt_cats[o->cat] : opt_section_title(o->id);
        int n = 0, j = i;
        for (; j < OPT_COUNT; j++) {
            const OptDef *oj = &g_opts[j];
            if (j > i && (q[0] ? oj->cat != o->cat : opt_section_title(oj->id) != NULL)) break;
            if (!q[0] && oj->cat != g_opt.cat) break;
            n += q[0] ? opt_matches(oj, q) : !opt_hidden(oj->id);
        }
        if (title) y = props_heading(cx, y, title);
        props_card(cx, y, cwid, n, rh, 0);
        for (int k = i; k < j; k++) {
            const OptDef *ok = &g_opts[k];
            if (q[0] ? !opt_matches(ok, q) : opt_hidden(ok->id)) continue;
            Rect rr = {cx, y, cwid, rh};
            if (y + rh > list.y && y < list.y + list.h) {
                bool hv = opt_hover(rr) && opt_hover(list) && g_opt.drop_opt < 0;
                if (hv) g_opt.hover_row = k; // (no highlight: rows are static, their controls react)
                f32 tx = rr.x + S(16), ctlw = MIN(roundf(S(380)), rr.w * 0.55f), lw = rr.w - ctlw - S(44);
                if (ok->kind == OK_BOOL || ok->kind == OK_BUTTON) lw = rr.w - roundf(S(200)); // (narrow controls: more room for text)
                draw_text_fit(f, tx, roundf(y + rh * 0.5f - f->line_h + S(2)), lw, ok->label, (int)strlen(ok->label), C(COL_TEXT), 0);
                const char *desc = opt_desc(ok);
                draw_text_fit(fs, tx, roundf(y + rh * 0.5f + S(3)), lw, desc, (int)strlen(desc), C(COL_TEXT_DIM), 0);
                opt_draw_control(ok, rr.x + rr.w - ctlw - S(14), y + rh * 0.5f, ctlw);
            }
            y += rh;
            shown++;
        }
        y += gap;
        i = j;
    }
    if (!q[0] && g_opt.cat == OC_LAYOUTS) {
        y = opt_draw_layouts(cx, y, cwid, list, q) + gap;
        shown++;
    } else if (q[0]) { // matching layouts in search results
        int nm = 0;
        for (int i = 0; i < g_nlayouts; i++) nm += strcasestr(g_layouts[i].name, q) || strcasestr("layouts", q);
        if (nm) {
            y = props_heading(cx, y, "Layouts");
            y = opt_draw_layouts(cx, y, cwid, list, q) + gap;
            shown++;
        }
    }
    if (q[0] || g_opt.cat == OC_KEYBOARD) {
        int nk = 0;
        for (int i = 0; i < KEYDEF_COUNT; i++) nk += keydef_matches(&g_keys[i], q);
        if (nk) {
            y = props_heading(cx, y, q[0] ? "Keyboard" : "Commands");
            props_card(cx, y, cwid, nk, krh, 0);
        }
        for (int i = 0; i < KEYDEF_COUNT; i++) {
            KeyDef *d = &g_keys[i];
            if (!keydef_matches(d, q)) continue;
            Rect rr = {cx, y, cwid, krh};
            if (y + krh > list.y && y < list.y + list.h) {
                bool hv = opt_hover(rr) && opt_hover(list) && g_opt.drop_opt < 0;
                (void)hv;
                if (g_opt.rec_def == i) draw_rrect(rr.x + S(3), rr.y + S(3), rr.w - S(6), rr.h - S(6), S(6), C(COL_HOVER)); // recording
                const char *l = cmd_label(d->id);
                f32 kw = rr.w * 0.6f;
                draw_text_fit(f, rr.x + S(16), text_y(f, y, krh), rr.w - kw - S(30), l, (int)strlen(l), C(COL_TEXT), 0);
                opt_draw_keys(i, rr.x + rr.w - kw - S(14), y + krh * 0.5f, kw);
            }
            y += krh;
            shown++;
        }
        if (nk) y += gap;
    }
    if (!shown)
        draw_text(f, cx + S(4), y, "No settings match your search", 29, C(COL_TEXT_FAINT));
    g_opt.max_scroll = MAX(0.f, y - gap + g_opt.scroll - list.y - list.h + S(8));
    g_opt.scroll = CLAMP(g_opt.scroll, 0.f, g_opt.max_scroll);
    draw_clip_pop(cs);
    sheet_fade_edges(list, g_opt.scroll, g_opt.max_scroll, S(8));
    if (g_opt.max_scroll > 0) { // scrollbar
        f32 content = list.h + g_opt.max_scroll, tw = roundf(S(4)), th = MAX(S(24), list.h * list.h / content);
        f32 ty = list.y + (list.h - th) * (g_opt.scroll / g_opt.max_scroll);
        draw_rrect(list.x + list.w - S(6) - tw * 0.5f, ty, tw, th, tw * 0.5f, C(COL_SCROLL));
    }
    // hits inside the list are only valid where the list is visible
    for (int k = 0; k < g_opt.nhits; k++) {
        OptHit *hp = &g_opt.hits[k];
        if (hp->part == HP_NAV || hp->part == HP_SEARCH || hp->part == HP_CLOSE) continue;
        f32 y0 = MAX(hp->r.y, list.y), y1 = MIN(hp->r.y + hp->r.h, list.y + list.h);
        if (y1 <= y0) hp->part = HP_NONE, hp->r.w = 0;
        else hp->r.y = y0, hp->r.h = y1 - y0;
    }
    opt_draw_dropdown();
}

// --- input -----------------------------------------------------------------------

static void options_click(f32 x, f32 y, int clicks) {
    if (g_opt.drop_opt >= 0) { // dropdown is modal: pick, or close when clicking elsewhere
        for (int k = g_opt.nhits - 1; k >= 0; k--) {
            OptHit *hp = &g_opt.hits[k];
            if (hp->r.w <= 0 || !in_rect(hp->r, x, y)) continue;
            if (hp->part == HP_DROPITEM) opt_drop_pick(hp->arg);
            if (hp->part == HP_NONE && hp->opt == g_opt.drop_opt) return; // popup frame / filter
            if (hp->part == HP_DROP && hp->opt == g_opt.drop_opt) g_opt.drop_opt = -1;
            if (hp->part == HP_DROPITEM || hp->part == HP_DROP) return;
            break;
        }
        g_opt.drop_opt = -1;
        return;
    }
    if (g_opt.rec_def >= 0) { // clicking anywhere ends recording (saves what was typed)
        bool on_rec = false;
        for (int k = 0; k < g_opt.nhits; k++)
            if ((g_opt.hits[k].part == HP_KEYCHIP || g_opt.hits[k].part == HP_KEYADD) && g_opt.hits[k].opt == g_opt.rec_def &&
                in_rect(g_opt.hits[k].r, x, y))
                on_rec = true;
        opt_rec_commit();
        if (on_rec) return;
    }
    if (!in_rect(g_sheet.r, x, y)) { sheet_close(); return; }
    g_opt.lname_focus = false; // a click elsewhere gives the keyboard back to the search field
    for (int k = g_opt.nhits - 1; k >= 0; k--) {
        OptHit *hp = &g_opt.hits[k];
        if (hp->r.w <= 0 || !in_rect(hp->r, x, y)) continue;
        switch (hp->part) {
        case HP_CLOSE: sheet_close(); return;
        case HP_NAV:
            g_opt.cat = hp->arg;
            g_opt.lname_focus = hp->arg == OC_LAYOUTS;
            g_opt.scroll = 0;
            te_set(&g_opt.search, "", 0, 0);
            return;
        case HP_TOGGLE: opt_bool_toggle(hp->opt); return;
        case HP_SEG: opt_choice_set(hp->opt, hp->arg); return;
        case HP_DROP: opt_drop_open(hp->opt, hp->r); return;
        case HP_MINUS: opt_int_step(hp->opt, -1); return;
        case HP_PLUS: opt_int_step(hp->opt, +1); return;
        case HP_SWATCH:
            g_accent_custom = hp->arg == 0 ? 0 : 0x01000000u | g_accents[hp->arg - 1].rgb;
            theme_apply();
            A.settings_dirty = true;
            return;
        case HP_BUTTON: opt_button(hp->opt); return;
        case HP_LNAME: g_opt.lname_focus = true; return;
        case HP_LSAVE: opt_layout_save(); return;
        case HP_LOPEN: {
            int li = hp->arg;
            sheet_close();
            if (layout_open(li)) toast("Layout \xE2\x80\x9C%s\xE2\x80\x9D", g_layouts[li].name);
            return;
        }
        case HP_LUPDATE:
            if (layout_save_named(g_layouts[hp->arg].name)) toast("Updated layout \xE2\x80\x9C%s\xE2\x80\x9D", g_layouts[hp->arg].name);
            return;
        case HP_LDELETE: {
            static const char *btn[] = {"Cancel", "Delete"};
            char msg[160];
            snprintf(msg, sizeof msg, "The layout \xE2\x80\x9C%s\xE2\x80\x9D will be removed. Your files aren't affected.", g_layouts[hp->arg].name);
            dialog_open("Delete layout?", msg, btn, 2, 1, 0, NULL, layout_delete_cb, (void *)(intptr_t)hp->arg);
            return;
        }
        case HP_KEYCHIP: opt_rec_start(hp->opt, hp->arg); return;
        case HP_KEYADD: opt_rec_start(hp->opt, -1); return;
        case HP_KEYDEL:
            keydef_remove_seq(&g_keys[hp->opt], hp->arg);
            A.settings_dirty = true;
            return;
        case HP_KEYRESET:
            keydef_reset(&g_keys[hp->opt]);
            for (int s2 = 0; s2 < g_keys[hp->opt].nseq; s2++) { // defaults win over conflicting custom keys
                KeySeq sq = g_keys[hp->opt].seq[s2];
                keys_assign(&g_keys[hp->opt], s2, &sq);
            }
            A.settings_dirty = true;
            return;
        default: break;
        }
    }
    // clicking a row's label toggles booleans (like a checkbox label)
    if (g_opt.hover_row >= 0 && g_opts[g_opt.hover_row].kind == OK_BOOL) opt_bool_toggle(g_opts[g_opt.hover_row].id);
    (void)clicks;
}

static void options_key(SDL_Keycode k, SDL_Keymod mod) {
    if (g_opt.rec_def >= 0) { opt_rec_key(k, mod); return; }
    if (g_opt.lname_focus && g_opt.drop_opt < 0) {
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) opt_layout_save();
        else if (k == SDLK_ESCAPE) g_opt.lname_focus = false;
        else te_key(&g_opt.lname, k, mod);
        return;
    }
    if (g_opt.drop_opt >= 0) {
        int vis = 10;
        switch (k) {
        case SDLK_ESCAPE: g_opt.drop_opt = -1; return;
        case SDLK_RETURN: case SDLK_KP_ENTER: opt_drop_pick(g_opt.drop_sel); return;
        case SDLK_DOWN: g_opt.drop_sel = MIN(g_opt.drop_sel + 1, g_opt.drop_n - 1); return;
        case SDLK_UP: g_opt.drop_sel = MAX(g_opt.drop_sel - 1, 0); return;
        case SDLK_PAGEDOWN: g_opt.drop_sel = MIN(g_opt.drop_sel + vis, g_opt.drop_n - 1); return;
        case SDLK_PAGEUP: g_opt.drop_sel = MAX(g_opt.drop_sel - vis, 0); return;
        default:
            if (te_key(&g_opt.drop_filter, k, mod)) opt_drop_filter();
            return;
        }
    }
    switch (k) {
    case SDLK_ESCAPE:
        if (g_opt.search.buf[0]) { te_set(&g_opt.search, "", 0, 0); g_opt.scroll = 0; }
        else sheet_close();
        return;
    case SDLK_TAB:
        if (!g_opt.search.buf[0]) { g_opt.cat = (g_opt.cat + (mod & SDL_KMOD_SHIFT ? OC_COUNT - 1 : 1)) % OC_COUNT; g_opt.scroll = 0; g_opt.lname_focus = g_opt.cat == OC_LAYOUTS; }
        return;
    case SDLK_PAGEDOWN: g_opt.scroll += S(300); return;
    case SDLK_PAGEUP: g_opt.scroll -= S(300); return;
    default:
        if (te_key(&g_opt.search, k, mod)) g_opt.scroll = 0;
    }
}

static void options_text(const char *text) {
    if (g_opt.rec_def >= 0) return;
    if (g_opt.lname_focus && g_opt.drop_opt < 0) { te_insert(&g_opt.lname, text); return; }
    if (g_opt.drop_opt >= 0) {
        te_insert(&g_opt.drop_filter, text);
        opt_drop_filter();
        return;
    }
    te_insert(&g_opt.search, text);
    g_opt.scroll = 0;
}

static void options_wheel(f32 dy) {
    if (g_opt.drop_opt >= 0) { g_opt.drop_top = MAX(0, g_opt.drop_top - (int)dy * 3); g_opt.drop_sel = CLAMP(g_opt.drop_sel, g_opt.drop_top, g_opt.drop_top + 9); return; }
    g_opt.scroll = CLAMP(g_opt.scroll - dy * S(60), 0.f, g_opt.max_scroll);
}

static void options_open_page(int cat) {
    options_open();
    g_opt.cat = cat;
    g_opt.lname_focus = cat == OC_LAYOUTS;
}

static bool options_is_open(void) { return g_sheet.kind == SHEET_OPTIONS; }
