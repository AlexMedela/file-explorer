// options.c - Options window (Ctrl+,): every setting in one searchable sheet (included from ui_tools.c)

static const char *g_opt_cats[OC_COUNT] = {"Appearance", "Behaviour", "Views", "Keyboard", "Layouts", "Actions", "Integration", "Updates", "Advanced"};

enum { OK_BOOL, OK_CHOICE, OK_INT, OK_ACCENT, OK_BUTTON, OK_TEXT };

enum {
    OPT_THEME, OPT_THEME_LIGHT, OPT_THEME_DARK, OPT_ACCENT, OPT_ACCENT_DESKTOP, OPT_SCROLL_COLOR, OPT_ICON_THEME, OPT_APP_ICON, OPT_TITLE_ICON, OPT_FONT, OPT_FONT_SIZE, OPT_ROW_SPACING, OPT_ZOOM, OPT_DIM_INACTIVE,
    OPT_BRAND_LOGO, OPT_BRAND_NAME, OPT_BRAND_TEXT, OPT_ALT_ROWS, OPT_SIZE_BARS, OPT_LOAD_TIMES, OPT_SMOOTH, OPT_THEME_NEW, OPT_THEME_FOLDER, OPT_ICON_FOLDER, OPT_TERM_LOOK,
    OPT_UNIFIED, OPT_BRIGHT_SIDEBAR, OPT_BRIGHT_HEADER, OPT_BRIGHT_FILES, OPT_BRIGHT_PREVIEW, OPT_BRIGHT_POPUP, OPT_BRIGHT_STATUS, OPT_BRIGHT_PATHBAR, OPT_BRIGHT_SEARCHBAR, OPT_TRANSPARENT_PATHBAR, OPT_PATH_TEXT, OPT_RIBBON, OPT_RIBBON_POS, OPT_RIBBON_OVERFLOW, OPT_SEPARATORS, OPT_NAV_SELECTION_OPACITY, OPT_NAV_SELECTOR_MARK, // (OPT_BRIGHT_*: SURF_* order)
    OPT_HIDDEN, OPT_FOLDER_SIZES, OPT_GIT_STATUS, OPT_SEARCH_SUB, OPT_RESTORE, OPT_PREVIEW, OPT_SINGLE_CLICK, OPT_DBLCLICK_UP, OPT_DROP_ASK, OPT_TRANSFERS, OPT_VERIFY, OPT_EXEC_OPEN, OPT_KEYS_RESET, OPT_SINGLE_INSTANCE, OPT_AUTOSTART, OPT_TRAY, OPT_HIDE_SINGLE_TAB,
    OPT_DEF_VIEW, OPT_VIEW_PER_FOLDER, OPT_DEF_SORT, OPT_SORT_DESC, OPT_FOLDERS_FIRST, OPT_DEF_GROUP,
    OPT_RENDERER, OPT_SETTINGS_FILE, OPT_REINDEX, OPT_TAB_STYLE, OPT_TITLE_BAR, OPT_WINDOW_BUTTONS, OPT_ROUND_CORNERS, OPT_IG_DEFAULT_FM, OPT_IG_FM1, OPT_IG_PICKER, OPT_IG_UPDATES, OPT_TERMINAL_PATH,
    OPT_UPD_NOW, OPT_UPD_HOW, OPT_UPD_NOTES, OPT_UPD_AUTO,
};

typedef struct {
    int cat, kind, id;
    const char *label, *desc;
} OptDef;

static const OptDef g_opts[] = {
    {OC_APPEARANCE, OK_CHOICE, OPT_THEME, "Theme", "Colour scheme; System: light or dark as the desktop prefers, with its accent (KDE: its colours) (add your own JSON schemes below)"},
    {OC_APPEARANCE, OK_CHOICE, OPT_THEME_LIGHT, "Light theme", "Used by System while the desktop is light"},
    {OC_APPEARANCE, OK_CHOICE, OPT_THEME_DARK, "Dark theme", "Used by System while the desktop is dark"},
    {OC_APPEARANCE, OK_ACCENT, OPT_ACCENT, "Accent colour", "Selection, focus and highlight colour"},
    {OC_APPEARANCE, OK_BOOL, OPT_ACCENT_DESKTOP, "Desktop's accent colour", "Use the desktop's accent with every theme, where it has one (else the colour above)"},
    {OC_APPEARANCE, OK_TEXT, OPT_SCROLL_COLOR, "Scrollbar colour", "Empty: Auto, the theme's own (System: the desktop's accent); or #RRGGBB / #RGB, then Apply"},
    {OC_APPEARANCE, OK_CHOICE, OPT_ICON_THEME, "File icons", "Icon set for files and folders; System: the desktop's icon theme"},
    {OC_APPEARANCE, OK_TEXT, OPT_APP_ICON, "App icon", "Absolute SVG/PNG path: Apply uses it, File-Icon follows the current File icons folder icon, Default restores the built-in icon and clears the path"},
    {OC_APPEARANCE, OK_TEXT, OPT_TITLE_ICON, "Title bar icon", "Icon at the left of the title bar: absolute SVG/PNG path; Apply uses it, Default restores the built-in logo. Logo off hides it"},
    {OC_APPEARANCE, OK_CHOICE, OPT_FONT, "Font", "Interface font family"},
    {OC_APPEARANCE, OK_INT, OPT_FONT_SIZE, "Font size", "Text size at 100% zoom"},
    {OC_APPEARANCE, OK_CHOICE, OPT_ROW_SPACING, "Row spacing", "Height of rows in the details and list views"},
    {OC_APPEARANCE, OK_INT, OPT_ZOOM, "Interface zoom", "Scales the whole window (Ctrl+= / Ctrl+-)"},
    {OC_APPEARANCE, OK_BOOL, OPT_DIM_INACTIVE, "Dim inactive panels", "Shade panels that don't have focus"},
    {OC_APPEARANCE, OK_BOOL, OPT_ALT_ROWS, "Alternating row colours", "Zebra stripes in the details view"},
    {OC_APPEARANCE, OK_BOOL, OPT_SIZE_BARS, "Folder size bars", "Bars behind folder sizes, relative to the largest"},
    {OC_APPEARANCE, OK_BOOL, OPT_LOAD_TIMES, "Load times", "How long the folder took to list, in the status bar"},
    {OC_APPEARANCE, OK_BOOL, OPT_SMOOTH, "Smooth scrolling", "Ease scrolling, go further on quick wheel turns, let touchpad swipes glide on"},
    {OC_APPEARANCE, OK_CHOICE, OPT_TAB_STYLE, "Tab style", "Classic: the open tab joins the folder below it, above the navigation bar; Pills: rounded tabs centred in a box below it"},
    {OC_APPEARANCE, OK_CHOICE, OPT_TITLE_BAR, "Title bar", "Window buttons and dragging: Explorer's own on every desktop (Automatic), or the desktop's title bar (System)"},
    {OC_APPEARANCE, OK_BOOL, OPT_WINDOW_BUTTONS, "Window buttons", "Minimise, maximise and close at the top right of Explorer's own title bar; without them, the desktop's keys close it"},
    {OC_APPEARANCE, OK_BOOL, OPT_ROUND_CORNERS, "Rounded corners", "Round the window's corners with a drop shadow"},
    {OC_APPEARANCE, OK_CHOICE, OPT_BRAND_NAME, "Name above the sidebar", "Explorer, this computer's name, your own text, or none (the sidebar then starts at the top)"},
    {OC_APPEARANCE, OK_BOOL, OPT_BRAND_LOGO, "Logo", "Show the icon before the name above the sidebar; off hides a custom title bar icon too"},
    {OC_APPEARANCE, OK_TEXT, OPT_BRAND_TEXT, "Your own name", "Shown above the sidebar instead of Explorer"},
    {OC_APPEARANCE, OK_BOOL, OPT_UNIFIED, "Unified surfaces", "Sidebar, header, preview and status bar take the file area's background; the lines between them stay"},
    {OC_APPEARANCE, OK_BOOL, OPT_SEPARATORS, "Separator lines", "Lines between the sidebar, header, columns, preview and status bar (an edge you can drag shows on hover)"},
    {OC_APPEARANCE, OK_BOOL, OPT_TRANSPARENT_PATHBAR, "Transparent path bar", "Remove only the path field fill; pills and search stay filled"},
    {OC_APPEARANCE, OK_BOOL, OPT_RIBBON, "Ribbon", "A row of icons for common file actions in each panel (cut, copy, paste, rename, new folder, terminal...)"},
    {OC_APPEARANCE, OK_CHOICE, OPT_RIBBON_POS, "Ribbon position", "Where it sits in each panel; or drag it by its grip (the dots at its end)"},
    {OC_APPEARANCE, OK_CHOICE, OPT_RIBBON_OVERFLOW, "When the ribbon doesn't fit", "The rest in a \xE2\x80\xA6 menu, on more rows, or slid along with arrows (and the wheel)"},
    {OC_APPEARANCE, OK_BOOL, OPT_PATH_TEXT, "Path as text", "Show the folder's path as plain text instead of clickable parts; a click puts it in edit mode (paste a path, Enter goes)"},
    {OC_APPEARANCE, OK_INT, OPT_NAV_SELECTION_OPACITY, "Navigation selection opacity", "Accent mix for sidebar, Options categories and pill tab selections, 0% to 100%"},
    {OC_APPEARANCE, OK_BOOL, OPT_NAV_SELECTOR_MARK, "Toggle navigation selector mark", "Show the small accent edge on selected sidebar and Options navigation rows"},
    {OC_APPEARANCE, OK_INT, OPT_BRIGHT_SIDEBAR, "Sidebar brightness", "With the name above it; darker or lighter than the theme's own colour, -30% to +30%"},
    {OC_APPEARANCE, OK_INT, OPT_BRIGHT_HEADER, "Header brightness", "Tab strip and toolbar, -30% to +30%"},
    {OC_APPEARANCE, OK_INT, OPT_BRIGHT_PATHBAR, "Path bar and pills enclosure brightness", "Path field and pill-tab enclosure: -30% to +30%"},
    {OC_APPEARANCE, OK_INT, OPT_BRIGHT_SEARCHBAR, "Folder search background brightness", "Folder search field only: -30% to +30%"},
    {OC_APPEARANCE, OK_INT, OPT_BRIGHT_FILES, "File area brightness", "The background of the files, -30% to +30%"},
    {OC_APPEARANCE, OK_INT, OPT_BRIGHT_PREVIEW, "Preview panel brightness", "-30% to +30%"},
    {OC_APPEARANCE, OK_INT, OPT_BRIGHT_POPUP, "Pop-up panel brightness", "Options and other popup panels: -30% to +30%"},
    {OC_APPEARANCE, OK_INT, OPT_BRIGHT_STATUS, "Status bar brightness", "-30% to +30%"},
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
    {OC_BEHAVIOUR, OK_BOOL, OPT_HIDE_SINGLE_TAB, "Hide tab bar with one tab", "Give the tab row back to the folder while only one tab is open; it returns as soon as you open another"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_SINGLE_INSTANCE, "Open in the running window", "Folders opened from other apps or the terminal become tabs here"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_AUTOSTART, "Start Explorer at login", "Starts in the background when you log in, ready to open instantly"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_TRAY, "Keep running in the tray when closed", "Closing the window hides it; quit from the tray icon or Ctrl+Q (needs a tray in your panel)"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_SINGLE_CLICK, "Single-click to open", "Open files and folders with one click"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_DBLCLICK_UP, "Double-click empty space to go up", "Like File Pilot: opens the parent folder"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_DROP_ASK, "Ask what to do when dropping files", "Move, copy or link: a menu after each drop (holding Ctrl, Shift or both still decides at once)"},
    {OC_BEHAVIOUR, OK_CHOICE, OPT_TRANSFERS, "Big transfers", "Copies and moves wait their turn in the transfer queue (Ctrl+J); small ones never wait"},
    {OC_BEHAVIOUR, OK_BOOL, OPT_VERIFY, "Verify after copy", "Read each copied file back and compare checksums; a copy that differs is removed"},
    {OC_BEHAVIOUR, OK_CHOICE, OPT_EXEC_OPEN, "Opening programs and scripts", "Executable files (AppImages, programs, scripts with #!): ask first, or always do one thing; right-click > Run works either way"},
    {OC_KEYBOARD, OK_BUTTON, OPT_KEYS_RESET, "Reset all shortcuts", "Click a key to change it, + to add one"},
    {OC_VIEWS, OK_CHOICE, OPT_DEF_VIEW, "Default view", "New tabs open in this view; a tab keeps the view you pick while you browse"},
    {OC_VIEWS, OK_BOOL, OPT_VIEW_PER_FOLDER, "Each folder keeps its own view", "Opening a folder switches to the view last used there (else the default)"},
    {OC_VIEWS, OK_CHOICE, OPT_DEF_SORT, "Default sort", "Sort column for new tabs"},
    {OC_VIEWS, OK_BOOL, OPT_SORT_DESC, "Sort descending", "Reverse the default sort order"},
    {OC_VIEWS, OK_BOOL, OPT_FOLDERS_FIRST, "Folders first", "Folders above files, whatever the sort; off: mixed together"},
    {OC_VIEWS, OK_CHOICE, OPT_DEF_GROUP, "Default grouping", "Group items in the details view"},
    {OC_INTEGRATION, OK_BOOL, OPT_IG_DEFAULT_FM, "Default file manager", "Folders from other apps open here"},
    {OC_INTEGRATION, OK_BOOL, OPT_IG_FM1, "\"Show in folder\" opens here", "From browsers and other apps"},
    {OC_INTEGRATION, OK_BOOL, OPT_IG_PICKER, "File chooser for other apps", "Their Open and Save dialogs open here"},
    {OC_INTEGRATION, OK_TEXT, OPT_TERMINAL_PATH, "Terminal Executable", "Executable name or absolute path (no arguments). Apply saves it; Default uses $TERMINAL or the desktop default. The name appears in the Open in menu."},
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
    {OPT_THEME, "Theme"}, {OPT_FONT, "Text"}, {OPT_DIM_INACTIVE, "Display"}, {OPT_UNIFIED, "Surfaces"}, {OPT_TERM_LOOK, "Terminal"}, {OPT_THEME_NEW, "Custom themes"},
    {OPT_HIDDEN, "Files and folders"}, {OPT_RESTORE, "Startup"}, {OPT_SINGLE_CLICK, "Mouse"}, {OPT_TRANSFERS, "Copying and moving"}, {OPT_EXEC_OPEN, "Programs"},
    {OPT_KEYS_RESET, "Shortcuts"}, {OPT_DEF_VIEW, "New folders"}, {OPT_RENDERER, "Rendering"}, {OPT_SETTINGS_FILE, "Maintenance"},
    {OPT_IG_DEFAULT_FM, "Desktop roles (yours to give: installing never takes them)"}, {OPT_TERMINAL_PATH, "External terminal"},
    {OPT_UPD_NOW, "Version"}, {OPT_IG_UPDATES, "Automatic updates"},
};

static const char *opt_section_title(int id) {
    for (size_t k = 0; k < ARRAY_COUNT(g_opt_sections); k++)
        if (g_opt_sections[k].id == id) return g_opt_sections[k].title;
    return NULL;
}

static const char *g_sort_labels[SORT_COUNT] = {"Name", "Date modified", "Type", "Size", "Date created", "Extension",
                                                "Permissions", "Owner", "Date accessed", "Group", "Link target"};
static const char *g_group_labels[GROUP_COUNT] = {"None", "Date", "Type", "Size"};
static const char *g_spacing_labels[3] = {"Compact", "Normal", "Comfortable"};
static const char *g_title_bar_labels[] = {"Automatic", "System", "Explorer's own"}; // TITLEBAR_*
static const char *g_renderers[] = {"auto", "vulkan", "opengl", "software"};
static const char *g_renderer_labels[] = {"Automatic", "Vulkan", "OpenGL", "Software"};
static bool g_renderer_auto_run; // this run started on Automatic (set in main): its label names what it picked

// "Automatic (OpenGL)": the backend Automatic runs on now (plain "Automatic" after starting on a forced one).
static const char *renderer_auto_label(void) {
    static char lab[48];
    const char *rn = g_renderer_auto_run && A.ren ? SDL_GetRendererName(A.ren) : NULL;
    if (!rn) return "Automatic";
    static const char *names[][2] = {{"opengl", "OpenGL"}, {"opengles2", "OpenGL ES"}, {"vulkan", "Vulkan"}, {"gpu", "SDL GPU"},
                                     {"software", "Software"}};
    const char *nice = rn;
    for (size_t k = 0; k < ARRAY_COUNT(names); k++)
        if (!strcmp(rn, names[k][0])) nice = names[k][1];
    snprintf(lab, sizeof lab, "Automatic (%s)", nice);
    return lab;
}
static const int g_transfer_counts[] = {1, 2, 4, 0}; // A.transfers_at_once (0 = no limit)
static const char *g_transfer_labels[] = {"One at a time", "Two at a time", "Four at a time", "All at once"};
static const char *g_exec_labels[] = {"Ask", "Run", "Run in terminal", "Open as a document (programs still ask)"}; // EXEC_*
static const char *g_tab_style_labels[] = {"Classic", "Pills"}; // TABS_*
static const char *g_ribbon_pos_labels[] = {"Below the path bar", "Above the path bar", "Bottom", "Left", "Right"}; // RP_*
static const char *g_ribbon_overflow_labels[] = {"More menu", "Wrap onto more rows", "Scroll"}; // RO_*
static const char *g_brand_labels[] = {APP_LABEL, "Computer name", "Your own", "None"}; // BRAND_*

// Options > Actions: its controls (not rows of g_opts), what "Shows for" writes, and the templates
enum { OPT_ACT_SEL = 1000, OPT_ACT_TERM, OPT_ACT_TEMPLATE, OPT_ACT_ICON, OPT_ACT_GROUP, OPT_ACT_GICON = 1100 }; // (OPT_ACT_GICON + group index)
static bool opt_is_gicon(int id) { return id >= OPT_ACT_GICON && id < OPT_ACT_GICON + UA_MAX; }
static const char *g_act_sel_labels[] = {"One or more items", "One item", "Several items", "Empty space (folder menu)"};
static const char *g_act_sel_values[] = {"any", "s", "m", "none"};
static const struct { const char *name, *exec, *exts, *deps, *icon; int sel; bool term; } g_act_templates[] = {
    {"Copy image to clipboard", "sh -c \"wl-copy < %F\"", "png; jpg; jpeg; webp; gif; svg", "wl-copy", "copy", 1, false},
    {"Open in VS Code", "code %F", "any", "code", "code", 0, false},
    {"SHA-256 checksums", "sh -c \"sha256sum %F | zenity --text-info --title=SHA-256 --width=800 --height=300\"", "nodirs", "sha256sum; zenity", "hash", 0, false},
    {"Convert to PNG", "sh -c \"for f in %F; do magick \\\"$f\\\" \\\"${f%%.*}.png\\\"; done\"", "jpg; jpeg; webp; gif; bmp; tiff; heic; avif", "magick", "image", 0, false},
    {"Make executable", "chmod +x %F", "nodirs", "", "play", 0, false},
    {"File details in a terminal", "sh -c \"stat %F; printf '\\\\nPress Enter to close'; read -r _\"", "any", "", "terminal", 0, true},
};
static void opt_act_template(int t);
// "Common types": buttons that add a kind of file to File types
static const struct { const char *label, *exts; } g_act_types[] = {
    {"Images", "png; jpg; jpeg; gif; webp; svg; bmp; tiff; heic; avif"}, {"Video", "mp4; mkv; webm; mov; avi"},
    {"Audio", "mp3; flac; ogg; opus; wav; m4a"}, {"Documents", "pdf; txt; md; odt; docx"}, {"Archives", "zip; tar; gz; xz; zst; 7z; rar"},
    {"Folders", "dir"}, {"Files only", "nodirs"},
};

// hit parts recorded while drawing, tested on click
enum { HP_NONE, HP_NAV, HP_TOGGLE, HP_SEG, HP_DROP, HP_MINUS, HP_PLUS, HP_SWATCH, HP_BUTTON, HP_DROPITEM, HP_SEARCH, HP_CLOSE,
       HP_KEYCHIP, HP_KEYDEL, HP_KEYADD, HP_KEYRESET, HP_LNAME, HP_LSAVE, HP_LOPEN, HP_LUPDATE, HP_LDELETE, HP_TEXT,
       HP_ACT_NEW, HP_ACT_FOLDER, HP_ACT_TOGGLE, HP_ACT_EDIT, HP_ACT_UP, HP_ACT_DOWN, HP_ACT_DELETE, HP_ACT_FIELD, HP_ACT_TYPE, HP_ACT_GROUPS, HP_ACT_NEEDS,
       HP_ACT_TEXT, HP_ACT_CANCEL, HP_ACT_SAVE,
       HP_EDIT_APPLY, HP_EDIT_RESET };
typedef struct { Rect r; int part, opt, arg; } OptHit;

static struct {
    int cat;
    TextEdit search;
    bool search_focus; // the search field has the keyboard (a click on it, or typing a letter)
    f32 scroll, max_scroll;
    OptHit hits[256];
    int nhits;
    int drop_opt;   // option whose dropdown is open (-1 none)
    Rect drop_r;
    int drop_sel;
    f32 drop_scroll;  // its list (pixels)
    bool drop_reveal; // keep the highlight in view on the next draw (arrow keys)
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
    // the name above the sidebar (OK_TEXT, applied as it's typed)
    TextEdit brand;
    bool brand_focus;
    // the scrollbar colour (OK_TEXT, applied with Enter or Apply)
    TextEdit scroll_color;
    bool scroll_focus;
    // actions page: the list, or (act_edit >= 0: an index in g_ua, UA_MAX for a new one) the editor
    int act_edit;
    bool act_new;
    TextEdit act_te[5]; // name, command, file types (Extensions), needed programs (Dependencies), group
    int act_focus;      // the field with the keyboard (-1: none)
    bool act_reveal;    // scroll it into view (Tab moved there)
    Rect act_group_r;   // the Group field + its button (where the list of groups opens)
    int act_sel;        // g_act_sel_labels
    bool act_term;
    int act_icon;           // in g_ua_icons (-1: the default)
    char act_init[5][1024]; // the fields as the editor opened: only what changed is written
    int act_init_sel, act_init_icon;
    bool act_init_term;
    // the application and title bar icon paths (OK_TEXT, applied with Enter or Apply)
    TextEdit app_icon;
    bool app_icon_focus;
    TextEdit title_icon;
    bool title_icon_focus;
    // the external terminal executable (OK_TEXT, applied with Apply or cleared with Default)
    TextEdit terminal_path;
    bool terminal_path_focus;
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

// Light theme / Dark theme rows: every theme but System. Choice i -> theme index, and back.
static int opt_pick_sys(void) {
    for (int k = 0; k < g_nthemes; k++)
        if (!g_themes[k].file[0] && !strcmp(g_themes[k].name, "System")) return k;
    return g_nthemes;
}
static int opt_pick_theme(int i) { return i >= opt_pick_sys() ? i + 1 : i; }
static int opt_pick_choice(int light) {
    int k = theme_find(g_theme_pick[light]);
    if (k < 0 || k == opt_pick_sys()) k = MAX(0, theme_find(light == 0 ? "Light" : "Dark"));
    return k > opt_pick_sys() ? k - 1 : k;
}

static int opt_choice_count(int id) {
    if (opt_is_gicon(id)) return 1 + (int)ARRAY_COUNT(g_ua_icons);
    switch (id) {
    case OPT_THEME: return g_nthemes;
    case OPT_THEME_LIGHT: case OPT_THEME_DARK: return g_nthemes - (opt_pick_sys() < g_nthemes);
    case OPT_ICON_THEME: return icon_theme_count();
    case OPT_TERM_LOOK: return 2;
    case OPT_TITLE_BAR: return (int)ARRAY_COUNT(g_title_bar_labels);
    case OPT_BRAND_NAME: return (int)ARRAY_COUNT(g_brand_labels);
    case OPT_TAB_STYLE: return (int)ARRAY_COUNT(g_tab_style_labels);
    case OPT_RIBBON_POS: return (int)ARRAY_COUNT(g_ribbon_pos_labels);
    case OPT_RIBBON_OVERFLOW: return (int)ARRAY_COUNT(g_ribbon_overflow_labels);
    case OPT_FONT: opt_load_families(); return 1 + g_opt.nfamilies;
    case OPT_ROW_SPACING: return 3;
    case OPT_DEF_VIEW: return ZOOM_COUNT;
    case OPT_DEF_SORT: return SORT_COUNT;
    case OPT_DEF_GROUP: return GROUP_COUNT;
    case OPT_RENDERER: return (int)ARRAY_COUNT(g_renderers);
    case OPT_TRANSFERS: return (int)ARRAY_COUNT(g_transfer_counts);
    case OPT_EXEC_OPEN: return (int)ARRAY_COUNT(g_exec_labels);
    case OPT_ACT_SEL: return (int)ARRAY_COUNT(g_act_sel_labels);
    case OPT_ACT_ICON: return 1 + (int)ARRAY_COUNT(g_ua_icons);
    case OPT_ACT_GROUP: return 1 + g_nuag;
    case OPT_ACT_TEMPLATE: return 1 + (int)ARRAY_COUNT(g_act_templates);
    }
    return 0;
}

static const char *opt_choice_label(int id, int i) {
    if (opt_is_gicon(id)) return i ? g_ua_icons[i - 1].label : "Default";
    switch (id) {
    case OPT_THEME: return g_themes[i].name;
    case OPT_THEME_LIGHT: case OPT_THEME_DARK: return g_themes[opt_pick_theme(i)].name;
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
    case OPT_RENDERER: return i == 0 ? renderer_auto_label() : g_renderer_labels[i];
    case OPT_TITLE_BAR: return g_title_bar_labels[i];
    case OPT_BRAND_NAME: return g_brand_labels[i];
    case OPT_TAB_STYLE: return g_tab_style_labels[i];
    case OPT_RIBBON_POS: return g_ribbon_pos_labels[i];
    case OPT_RIBBON_OVERFLOW: return g_ribbon_overflow_labels[i];
    case OPT_TRANSFERS: return g_transfer_labels[i];
    case OPT_EXEC_OPEN: return g_exec_labels[i];
    case OPT_ACT_SEL: return g_act_sel_labels[i];
    case OPT_ACT_TEMPLATE: return i ? g_act_templates[i - 1].name : "Choose a template\xE2\x80\xA6";
    case OPT_ACT_ICON: return i ? g_ua_icons[i - 1].label : "Default";
    case OPT_ACT_GROUP: return i ? g_uag[i - 1].name : "None (in the menu itself)";
    }
    return "";
}

static int opt_choice_get(int id) {
    if (opt_is_gicon(id)) return id - OPT_ACT_GICON < g_nuag ? g_uag[id - OPT_ACT_GICON].icon + 1 : 0;
    switch (id) {
    case OPT_THEME: return g_theme_cur;
    case OPT_THEME_LIGHT: return opt_pick_choice(0);
    case OPT_THEME_DARK: return opt_pick_choice(1);
    case OPT_ICON_THEME: return g_icon_theme;
    case OPT_TERM_LOOK: return g_term_look_setting;
    case OPT_TITLE_BAR: return A.title_bar;
    case OPT_BRAND_NAME: return A.brand_name;
    case OPT_TAB_STYLE: return A.tab_style;
    case OPT_RIBBON_POS: return A.ribbon_pos;
    case OPT_RIBBON_OVERFLOW: return A.ribbon_overflow;
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
    case OPT_EXEC_OPEN: return A.exec_open;
    case OPT_TRANSFERS:
        for (int i = 0; i < (int)ARRAY_COUNT(g_transfer_counts); i++)
            if (A.transfers_at_once == g_transfer_counts[i]) return i;
        return 0;
    case OPT_ACT_SEL: return g_opt.act_sel;
    case OPT_ACT_ICON: return g_opt.act_icon + 1;
    case OPT_ACT_GROUP: return ua_group_find(g_opt.act_te[4].buf) + 1;
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
    if (id == OPT_ACT_SEL) { g_opt.act_sel = i; return; } // (the editor's: saved with Save)
    if (id == OPT_ACT_ICON) { g_opt.act_icon = i - 1; return; }
    if (id == OPT_ACT_GROUP) {
        const char *g = i > 0 && i <= g_nuag ? g_uag[i - 1].name : "";
        te_set(&g_opt.act_te[4], g, (int)strlen(g), (int)strlen(g));
        return;
    }
    if (opt_is_gicon(id)) { // a group's: saved at once (groups.conf)
        if (!ua_group_set_icon(id - OPT_ACT_GICON, i - 1)) toast("Couldn't save groups.conf: %s", strerror(errno));
        return;
    }
    if (id == OPT_ACT_TEMPLATE) { if (i > 0) opt_act_template(i - 1); return; }
    switch (id) {
    case OPT_THEME: g_theme_cur = i; theme_apply(); desktop_look_sync(); break;
    case OPT_THEME_LIGHT: case OPT_THEME_DARK:
        snprintf(g_theme_pick[id == OPT_THEME_DARK], sizeof g_theme_pick[0], "%s", g_themes[opt_pick_theme(i)].name);
        theme_apply();
        break;
    case OPT_ICON_THEME:
        icon_set_theme(i);
        desktop_look_sync();
        if (g_app_icon_mode == APP_ICON_FILE_ICON) window_icon_update();
        break;
    case OPT_TERM_LOOK: g_term_look_setting = i; tc_reload_all(); break;
    case OPT_TITLE_BAR: A.title_bar = i; titlebar_apply(); break;
    case OPT_TAB_STYLE: A.tab_style = i; layout(); break;
    case OPT_RIBBON_POS: A.ribbon_pos = i; layout(); break;
    case OPT_RIBBON_OVERFLOW: A.ribbon_overflow = i; layout(); break;
    case OPT_BRAND_NAME:
        A.brand_name = i;
        g_opt.brand_focus = i == BRAND_CUSTOM; // its field appears: type the name straight away
        g_opt.lname_focus = false;
        layout();
        break;
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
    case OPT_EXEC_OPEN: A.exec_open = i; break;
    }
    A.settings_dirty = true;
}

static bool opt_bool_get(int id) {
    switch (id) {
    case OPT_DIM_INACTIVE: return A.dim_inactive;
    case OPT_ROUND_CORNERS: return A.round_corners;
    case OPT_WINDOW_BUTTONS: return A.window_buttons;
    case OPT_BRAND_LOGO: return A.brand_logo;
    case OPT_UNIFIED: return g_unified_surfaces;
    case OPT_SEPARATORS: return g_separators;
    case OPT_TRANSPARENT_PATHBAR: return g_transparent_pathbar;
    case OPT_PATH_TEXT: return A.path_text;
    case OPT_RIBBON: return A.ribbon;
    case OPT_NAV_SELECTOR_MARK: return g_nav_selector_mark;
    case OPT_ACCENT_DESKTOP: return g_accent_desktop;
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
    case OPT_FOLDERS_FIRST: return atomic_load(&g_folders_first);
    case OPT_VIEW_PER_FOLDER: return A.view_per_folder;
    case OPT_SINGLE_CLICK: return A.single_click;
    case OPT_DBLCLICK_UP: return A.dblclick_up;
    case OPT_DROP_ASK: return A.drop_ask;
    case OPT_SINGLE_INSTANCE: return A.single_instance;
    case OPT_HIDE_SINGLE_TAB: return g_hide_single_tab;
    case OPT_AUTOSTART: return autostart_enabled();
    case OPT_TRAY: return A.close_to_tray;
    case OPT_VERIFY: return A.verify_copies;
    case OPT_IG_DEFAULT_FM: return ig_default_fm_on();
    case OPT_IG_FM1: return A.ig_fm1;
    case OPT_IG_PICKER: return ig_picker_on();
    case OPT_IG_UPDATES: return A.ig_check_updates;
    case OPT_UPD_AUTO: return A.upd_auto;
    case OPT_ACT_TERM: return g_opt.act_term;
    }
    return false;
}

static void opt_bool_toggle(int id) {
    if (id == OPT_ACT_TERM) { g_opt.act_term = !g_opt.act_term; return; }
    switch (id) {
    case OPT_DIM_INACTIVE: A.dim_inactive = !A.dim_inactive; break;
    case OPT_ROUND_CORNERS:
        A.round_corners = !A.round_corners;
        if (A.round_corners && !A.win_alpha && A.csd) toast("Rounded corners: after a restart");
        A.redraw = true;
        break;
    case OPT_WINDOW_BUTTONS:
        A.window_buttons = !A.window_buttons;
        layout();
        break;
    case OPT_BRAND_LOGO: A.brand_logo = !A.brand_logo; break;
    case OPT_SEPARATORS: g_separators = !g_separators; break;
    case OPT_TRANSPARENT_PATHBAR: g_transparent_pathbar = !g_transparent_pathbar; break;
    case OPT_PATH_TEXT: A.path_text = !A.path_text; break;
    case OPT_RIBBON: A.ribbon = !A.ribbon; break;
    case OPT_NAV_SELECTOR_MARK: g_nav_selector_mark = !g_nav_selector_mark; break;
    case OPT_ACCENT_DESKTOP:
        g_accent_desktop = !g_accent_desktop;
        theme_apply();
        desktop_look_sync();
        break;
    case OPT_UNIFIED:
        g_unified_surfaces = !g_unified_surfaces;
        theme_apply();
        break;
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
    case OPT_FOLDERS_FIRST: do_action(active_pane(), ACT_FOLDERS_FIRST); break;
    case OPT_VIEW_PER_FOLDER: A.view_per_folder = !A.view_per_folder; break;
    case OPT_SINGLE_CLICK: A.single_click = !A.single_click; break;
    case OPT_DBLCLICK_UP: A.dblclick_up = !A.dblclick_up; break;
    case OPT_DROP_ASK: A.drop_ask = !A.drop_ask; break;
    case OPT_HIDE_SINGLE_TAB: g_hide_single_tab = !g_hide_single_tab; layout(); break;
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
    case OPT_NAV_SELECTION_OPACITY: return g_navigation_selection_opacity;
    }
    if (id >= OPT_BRIGHT_SIDEBAR && id <= OPT_BRIGHT_SEARCHBAR) return g_surface_bright[id - OPT_BRIGHT_SIDEBAR];
    return 0;
}

static void opt_int_step(int id, int dir) {
    switch (id) {
    case OPT_FONT_SIZE:
        A.font_size = CLAMP(A.font_size + dir, 9, 24);
        reload_fonts();
        break;
    case OPT_ZOOM: set_zoom(A.zoom + 0.1f * (f32)dir); break;
    case OPT_NAV_SELECTION_OPACITY:
        g_navigation_selection_opacity = CLAMP(g_navigation_selection_opacity + dir * 5, 0, 100);
        A.settings_dirty = true;
        return;
    }
    if (id >= OPT_BRIGHT_SIDEBAR && id <= OPT_BRIGHT_SEARCHBAR) {
        int *b = &g_surface_bright[id - OPT_BRIGHT_SIDEBAR];
        *b = CLAMP(*b + dir * 5, -30, 30);
        theme_apply();
    }
    A.settings_dirty = true;
}

static void opt_int_fmt(int id, char *buf, size_t cap) {
    if (id == OPT_NAV_SELECTION_OPACITY) snprintf(buf, cap, "%d%%", opt_int_get(id));
    else if (id >= OPT_BRIGHT_SIDEBAR && id <= OPT_BRIGHT_SEARCHBAR) snprintf(buf, cap, opt_int_get(id) ? "%+d%%" : "%d%%", opt_int_get(id));
    else if (id == OPT_ZOOM) snprintf(buf, cap, "%d%%", opt_int_get(id));
    else snprintf(buf, cap, "%d px", opt_int_get(id));
}

static bool opt_scroll_color_auto(void) { return g_scroll_custom == 0; }

// Scrollbar colour: Enter or Apply takes what's typed (empty: Auto); the field then shows it as saved.
static void opt_scroll_color_apply(void) {
    u32 c;
    if (!scroll_color_parse(g_opt.scroll_color.buf, &c)) { toast("Scrollbar colour: #RRGGBB or #RGB (empty: Auto)"); return; }
    g_scroll_custom = c;
    theme_apply();
    A.settings_dirty = true;
    char buf[16] = "";
    if (c) snprintf(buf, sizeof buf, "#%06X", c & 0xFFFFFF);
    te_set(&g_opt.scroll_color, buf, (int)strlen(buf), (int)strlen(buf));
    g_opt.scroll_focus = false;
    if (c) toast_success("Scrollbar colour applied");
    else toast_success("Scrollbar colour set to Auto");
}

// App icon: an absolute .svg/.png keeps its own colours; empty leaves the built-in logo in effect.
static bool opt_app_icon_chosen(const char *path) {
    if (!app_icon_path_ok(path)) { toast("Didn't apply, fallback to previous option"); return false; }
    snprintf(g_app_icon_path, sizeof g_app_icon_path, "%s", path ? path : "");
    g_app_icon_mode = APP_ICON_CUSTOM;
    g_logo_changed = true;
    window_icon_update();
    app_icon_sync();
    A.settings_dirty = true;
    te_set(&g_opt.app_icon, g_app_icon_path, (int)strlen(g_app_icon_path), (int)strlen(g_app_icon_path));
    g_opt.app_icon_focus = false;
    toast_success("App icon applied");
    return true;
}

static void opt_app_icon_apply(void) {
    if (!g_opt.app_icon.buf[0]) return; // Apply needs a typed path; Default clears it
    opt_app_icon_chosen(g_opt.app_icon.buf);
}

// Title bar icon: an absolute .svg/.png replaces the header logo; empty restores it.
static bool opt_title_icon_chosen(const char *path) {
    if (!app_icon_path_ok(path)) { toast("Didn't apply, fallback to previous option"); return false; }
    snprintf(g_title_icon_path, sizeof g_title_icon_path, "%s", path ? path : "");
    A.settings_dirty = true;
    A.redraw = true;
    te_set(&g_opt.title_icon, g_title_icon_path, (int)strlen(g_title_icon_path), (int)strlen(g_title_icon_path));
    g_opt.title_icon_focus = false;
    toast_success("Title bar icon applied");
    return true;
}

static void opt_title_icon_apply(void) { opt_title_icon_chosen(g_opt.title_icon.buf); }

static void opt_app_icon_set_mode(AppIconMode m) {
    g_app_icon_mode = m;
    g_logo_changed = true;
    window_icon_update();
    app_icon_sync();
    A.settings_dirty = true;
}

// Default forgets the custom artwork completely: the field falls back to its "Default" placeholder.
static void opt_app_icon_default(void) {
    g_app_icon_path[0] = 0;
    opt_app_icon_set_mode(APP_ICON_DEFAULT);
    A.redraw = true;
    te_set(&g_opt.app_icon, "", 0, 0);
    g_opt.app_icon_focus = false;
    toast_success("App icon set to Default");
}

static void opt_title_icon_default(void) {
    g_title_icon_path[0] = 0;
    A.settings_dirty = true;
    A.redraw = true;
    te_set(&g_opt.title_icon, "", 0, 0);
    g_opt.title_icon_focus = false;
    toast_success("Title bar icon set to Default");
}

static void opt_icon_button(int opt, int arg) {
    if (opt == OPT_APP_ICON) {
        if (arg == 0) opt_app_icon_apply();
        else if (arg == 1) { opt_app_icon_set_mode(APP_ICON_FILE_ICON); toast_success("App icon following File icons"); }
        else if (arg == 2) opt_app_icon_default();
    } else if (opt == OPT_TITLE_ICON) {
        if (arg == 0) opt_title_icon_apply();
        else if (arg == 1) opt_title_icon_default();
    }
}

static bool opt_terminal_path_valid(const char *s) {
    return strlen(s) < sizeof g_external_terminal && !strpbrk(s, "\n\r");
}

static char *opt_terminal_basename(char *s) {
    char *base = s;
    for (char *p = s; *p; p++) if (*p == '/') base = p + 1;
    return base;
}

static void opt_terminal_path_apply(void) {
    char raw[PATH_MAX];
    snprintf(raw, sizeof raw, "%s", g_opt.terminal_path.buf);
    char *s = raw;
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    if (!opt_terminal_path_valid(s)) { toast("Didn't apply, fallback to previous option"); return; }
    char *base = opt_terminal_basename(s);
    if (s[0] == '/' || strchr(s, '/')) {
        if (!*base) { toast("Didn't apply, fallback to previous option"); return; }
        memmove(s, base, strlen(base) + 1);
    }
    char old[PATH_MAX];
    snprintf(old, sizeof old, "%s", g_external_terminal);
    snprintf(g_external_terminal, sizeof g_external_terminal, "%s", s);
    char exe[PATH_MAX];
    const char *name = NULL;
    if (*s && !find_terminal(exe, sizeof exe, &name)) {
        snprintf(g_external_terminal, sizeof g_external_terminal, "%s", old);
        toast("Didn't apply, fallback to previous option");
        te_set(&g_opt.terminal_path, g_external_terminal, (int)strlen(g_external_terminal), (int)strlen(g_external_terminal));
        return;
    }
    te_set(&g_opt.terminal_path, g_external_terminal, (int)strlen(g_external_terminal), (int)strlen(g_external_terminal));
    g_opt.terminal_path_focus = false;
    A.settings_dirty = true;
    if (*s) toast_success("Terminal applied: Open in %s", path_basename(g_external_terminal));
    else toast_success("Terminal set to Default");
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
        for (int k = 0; k < SURF_COUNT; k++) // the scheme has these now (applied again they'd double); not the status bar's
            if (k != SURF_STATUS) g_surface_bright[k] = 0;
        themes_scan();
        int t = theme_find(name);
        if (t >= 0) { g_theme_cur = t; theme_apply(); desktop_look_sync(); A.settings_dirty = true; }
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
    case OPT_SCROLL_COLOR: opt_scroll_color_apply(); break;
    case OPT_APP_ICON: opt_app_icon_apply(); break;
    case OPT_TITLE_ICON: opt_title_icon_apply(); break;
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

// Typing in "Your own name": applied at once, one line, cut at a whole character.
static void opt_brand_sync(void) {
    for (char *s = g_opt.brand.buf; *s; s++)
        if ((unsigned char)*s < 32) *s = ' '; // (a pasted line break)
    char *d = A.brand_text;
    size_t n = 0;
    for (const char *s = g_opt.brand.buf; *s && n < sizeof A.brand_text - 1; s++) d[n++] = *s;
    if (g_opt.brand.buf[n]) while (n > 0 && ((unsigned char)d[n] & 0xC0) == 0x80) n--; // (cut inside a character)
    d[n] = 0;
    A.settings_dirty = true;
}

// Rows that don't apply to this copy (updating by itself: AppImage only), or to the current choice.
static bool opt_hidden(int id) {
    if (id == OPT_RIBBON_POS || id == OPT_RIBBON_OVERFLOW) return !A.ribbon;
    if (id == OPT_BRAND_TEXT) return A.brand_name != BRAND_CUSTOM;
    if (id == OPT_THEME_LIGHT || id == OPT_THEME_DARK) return !theme_is_system();
    if (id == OPT_UPD_HOW) return !upd_how_cmd(); // (only a command to copy; the version row says how it updates)
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
    g_opt.act_edit = g_opt.act_focus = -1;
    ua_load(); // (Options > Actions; edited files show again)
    te_set(&g_opt.brand, A.brand_text, (int)strlen(A.brand_text), (int)strlen(A.brand_text));
    char sc[16] = "";
    if (g_scroll_custom) snprintf(sc, sizeof sc, "#%06X", g_scroll_custom & 0xFFFFFF);
    te_set(&g_opt.scroll_color, sc, (int)strlen(sc), (int)strlen(sc));
    te_set(&g_opt.app_icon, g_app_icon_path, (int)strlen(g_app_icon_path), (int)strlen(g_app_icon_path));
    te_set(&g_opt.title_icon, g_title_icon_path, (int)strlen(g_title_icon_path), (int)strlen(g_title_icon_path));
    te_set(&g_opt.terminal_path, g_external_terminal, (int)strlen(g_external_terminal), (int)strlen(g_external_terminal));
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
    g_opt.drop_scroll = (f32)MAX(0, g_opt.drop_sel - 3) * roundf(S(28)); // (rows as drawn)
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

// Apply/reset actions use a visible accent hover/active state, independent of the subtle control shades.
static u32 g_opt_action_bg, g_opt_action_fg; // selftest probe: last colours drawn

static void opt_action_button(Rect r, const char *label, bool enabled, bool active) {
    bool hv = hand_if(enabled && A.mouse_in && in_rect(r, A.mx, A.my));
    u32 bg = !enabled ? COL_CONTROL_OFF : active ? (hv ? COL_ACCENT_HOVER : COL_ACCENT) : hv ? COL_ACCENT_HOVER : COL_CONTROL;
    u32 fg = !enabled ? COL_TEXT_FAINT : (active || hv) ? COL_ON_ACCENT : COL_TEXT;
    draw_rrect(r.x, r.y, r.w, r.h, S(7), C(bg));
    Font *f = &A.font;
    f32 tw = text_width(f, label, (int)strlen(label));
    draw_text(f, r.x + (r.w - tw) * 0.5f, text_y(f, r.y, r.h), label, (int)strlen(label), C(fg));
    if (A.selftest) {
        g_opt_action_bg = bg;
        g_opt_action_fg = fg;
    }
}

static bool opt_hover(Rect r) { return hot(r); }

// The icon of choice i of a dropdown (-1: none): the custom actions' icons.
static int opt_choice_icon(int id, int i) {
    if (id == OPT_ACT_GROUP) return i > 0 && i <= g_nuag ? ua_icon_ui(g_uag[i - 1].icon, true) : -1;
    if (id != OPT_ACT_ICON && !opt_is_gicon(id)) return -1;
    return ua_icon_ui(i - 1, id != OPT_ACT_ICON);
}

// The last-set active mode owns the primary button; title Apply is active while a custom path is set.
static const char *opt_icon_button_label(int opt, int arg) {
    if (opt == OPT_APP_ICON) return arg == 0 ? "Apply" : arg == 1 ? "File-Icon" : "Default";
    return arg == 0 ? "Apply" : "Default";
}

static bool opt_icon_button_active(int opt, int arg) {
    if (opt == OPT_APP_ICON)
        return (arg == 0 && g_app_icon_mode == APP_ICON_CUSTOM) || (arg == 1 && g_app_icon_mode == APP_ICON_FILE_ICON)
            || (arg == 2 && g_app_icon_mode == APP_ICON_DEFAULT);
    return (arg == 0 && g_title_icon_path[0]) || (arg == 1 && !g_title_icon_path[0]);
}

static f32 opt_icon_row_button_w(int n) {
    static const char *labels[3] = {"Apply", "File-Icon", "Default"};
    f32 w = 0;
    for (int i = 0; i < MIN(n, 3); i++) w = MAX(w, text_width(&A.font, labels[i], (int)strlen(labels[i])));
    return w;
}

// An icon-path row's controls, left to right, inside a strip of w pixels. Shrink the gaps, then the preview and the
// buttons before letting the path field vanish: even a narrow window keeps every button usable on one row.
static void opt_icon_row_layout(f32 w, f32 ch, int n, Rect *p, Rect *r, Rect *b) {
    if (n < 1) n = 1;
    if (n > 3) n = 3;
    f32 maxlab = opt_icon_row_button_w(n);
    f32 gap = S(6), ps = ch, bw = MAX(roundf(S(52)), maxlab + S(14));
    f32 nf = (f32)n, ng = (f32)(n + 1);
    f32 fw = w - ps - nf * bw - ng * gap;
    if (fw < roundf(S(110))) {
        gap = S(4);
        bw = MAX(roundf(S(48)), maxlab + S(10));
        fw = w - ps - nf * bw - ng * gap;
    }
    if (fw < roundf(S(40))) {
        ps = MIN(ps, MAX(roundf(S(20)), w - nf * bw - ng * gap - roundf(S(40))));
        fw = w - ps - nf * bw - ng * gap;
    }
    if (fw < S(1)) {
        gap = S(2);
        ps = MIN(ps, roundf(S(16)));
        f32 avail = (w - ps - S(1) - ng * gap) / nf;
        bw = MAX(S(1), MIN(bw, avail));
        fw = w - ps - nf * bw - ng * gap;
    }
    if (fw < S(1)) fw = S(1);
    if (ps < S(1)) ps = S(1);
    *p = (Rect){0, 0, ps, ch};
    *r = (Rect){ps + gap, 0, fw, ch};
    f32 bx = r->x + fw + gap;
    for (int k = 0; k < n; k++) {
        b[k] = (Rect){bx, 0, bw, ch};
        bx += bw + gap;
    }
}

// Leave the controls enough room, taking it from the label first; at narrow widths the label simply truncates.
static f32 opt_icon_row_ctlw(f32 row_w, int n) {
    f32 maxlab = opt_icon_row_button_w(n);
    f32 bw = MAX(roundf(S(48)), maxlab + S(10));
    f32 need = roundf(S(30)) + roundf(S(40)) + (f32)n * bw + (f32)(n + 1) * S(4);
    return MAX(MIN(roundf(S(600)), row_w * 0.75f), MIN(row_w - S(16), need));
}

// Control area of one row (right-aligned inside [x, x + w]).
static void opt_draw_control(const OptDef *o, f32 x, f32 cy, f32 w) {
    Font *f = &A.font, *fs = &A.font_small;
    f32 ch = roundf(S(30));
    f32 y = roundf(cy - ch * 0.5f);
    char buf[64];
    switch (o->kind) {
    case OK_BOOL: {
        Rect t = {x + w - roundf(S(40)), roundf(cy - S(10)), roundf(S(40)), roundf(S(20))};
        if (o->id == OPT_ACCENT_DESKTOP) {
            f32 sz = roundf(S(22)), gap = roundf(S(8));
            Rect c = {t.x - gap - sz, roundf(cy - sz * 0.5f), sz, sz};
            bool on = g_accent_desktop;
            bool known = g_desktop.accent != 0;
            u32 col = known ? (g_desktop.accent & 0xFFFFFF) : 0;
            if (on || opt_hover(c))
                draw_rrect_outline(c.x - S(3), c.y - S(3), sz + S(6), sz + S(6), (sz + S(6)) * 0.5f, MAX(1.f, S(1.5f)),
                                   C(on ? COL_TEXT : COL_TEXT_FAINT));
            if (on && known)
                draw_rrect(c.x, c.y, sz, sz, sz * 0.5f, C(col));
            else
                draw_rrect_outline(c.x, c.y, sz, sz, sz * 0.5f, MAX(1.f, S(1.2f)), C(COL_TEXT_FAINT));
        }
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
            f32 lx = r.x + S(10);
            int ic = opt_choice_icon(o->id, cur);
            if (ic >= 0) {
                draw_ui_icon(ic, lx, roundf(cy - S(8)), roundf(S(16)), C(COL_TEXT));
                lx += S(24);
            }
            draw_text_fit(f, lx, text_y(f, y, ch), r.x + r.w - S(26) - lx, l, (int)strlen(l), C(COL_TEXT), 0);
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
            // The selected swatch is the one showing the accent in use; with the desktop's accent driving, none is.
            bool on = col == COL_ACCENT && !(g_accent_desktop && g_desktop.accent);
            if (opt_hover(r) || on)
                draw_rrect_outline(r.x - S(3), r.y - S(3), sz + S(6), sz + S(6), (sz + S(6)) * 0.5f, MAX(1.f, S(1.5f)),
                                   C(on ? COL_TEXT : COL_TEXT_FAINT));
            draw_rrect(r.x, r.y, sz, sz, sz * 0.5f, C(col));
            if (i == 0) // the theme's own accent (not a preset): small dot
                draw_rrect(r.x + sz * 0.35f, r.y + sz * 0.35f, sz * 0.3f, sz * 0.3f, sz * 0.15f, C(COL_POPUP));
            // The System theme takes the desktop's accent, so a preset that happens to be the theme's own accent
            // (Blue) is a plain preset there: clicking it picks that colour, not "the desktop's accent".
            opt_hit(r, HP_SWATCH, o->id, is_default && !(theme_is_system() && i > 0) ? 0 : i);
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
                      : o->id == OPT_APP_ICON || o->id == OPT_TITLE_ICON ? "Apply"
                      : o->id == OPT_UPD_NOW ? upd_button(&primary, &enabled) : o->id == OPT_UPD_HOW ? (upd_how_cmd() ? "Copy" : NULL) : "Open";
        if (!l) break; // (nothing to press)
        f32 bw = MAX(roundf(S(96)), text_width(f, l, (int)strlen(l)) + S(28));
        Rect r = {x + w - bw, y, bw, ch};
        sheet_button(r, l, primary, enabled);
        if (enabled) opt_hit(r, HP_BUTTON, o->id, 0);
        break;
    }
    case OK_TEXT: { // the name above the sidebar, the scrollbar colour, the icon paths (with their Apply buttons)
        if (o->id == OPT_APP_ICON || o->id == OPT_TITLE_ICON) {
            bool header = o->id == OPT_TITLE_ICON;
            TextEdit *field = header ? &g_opt.title_icon : &g_opt.app_icon;
            bool focused = header ? g_opt.title_icon_focus : g_opt.app_icon_focus;
            int nb = header ? 2 : 3;
            Rect p, r, b[3];
            opt_icon_row_layout(w, ch, nb, &p, &r, b);
            p.x += x; r.x += x;
            p.y = r.y = y;
            for (int k = 0; k < nb; k++) { b[k].x += x; b[k].y = y; }
            if (header) draw_title_icon_preview(p.x, p.y, p.w);
            else draw_app_icon_preview(p.x, p.y, p.w);
            sheet_field(r, field, focused, "Default");
            opt_hit(r, HP_TEXT, o->id, header ? 3 : 2);
            for (int k = 0; k < nb; k++) {
                const char *l = opt_icon_button_label(o->id, k);
                bool enabled = !(k == 0 && !field->buf[0]); // Apply needs a typed path; Default/File-Icon do not
                bool active = opt_icon_button_active(o->id, k);
                opt_action_button(b[k], l, enabled, active);
                if (enabled) opt_hit(b[k], HP_BUTTON, o->id, k);
            }
            break;
        }
        if (o->id == OPT_SCROLL_COLOR) {
            f32 gap = roundf(S(6)), bw = MAX(roundf(S(80)), text_width(f, "Apply", 5) + S(28)), fw = roundf(S(140));
            Rect reset = {x + w - bw, y, bw, ch}, apply = {reset.x - gap - bw, y, bw, ch};
            Rect r = {apply.x - gap - fw, y, fw, ch};
            bool is_auto = opt_scroll_color_auto();
            sheet_field(r, &g_opt.scroll_color, g_opt.scroll_focus, "Auto");
            opt_hit(r, HP_TEXT, o->id, 1);
            opt_action_button(apply, "Apply", true, !is_auto);
            opt_action_button(reset, "Auto", true, is_auto);
            opt_hit(apply, HP_EDIT_APPLY, o->id, 0);
            opt_hit(reset, HP_EDIT_RESET, o->id, 0);
            break;
        }
        if (o->id == OPT_TERMINAL_PATH) {
            f32 gap = roundf(S(6));
            f32 bw = MAX(roundf(S(80)), MAX(text_width(f, "Apply", 5), text_width(f, "Default", 7)) + S(28));
            Rect reset = {x + w - bw, y, bw, ch}, apply = {reset.x - gap - bw, y, bw, ch};
            Rect r = {x, y, MAX(0.f, apply.x - gap - x), ch};
            sheet_field(r, &g_opt.terminal_path, g_opt.terminal_path_focus, "Name or path (no arguments)");
            opt_hit(r, HP_TEXT, o->id, 0);
            bool valid = opt_terminal_path_valid(g_opt.terminal_path.buf);
            bool is_default = g_external_terminal[0] == 0;
            opt_action_button(apply, "Apply", valid, !is_default);
            opt_action_button(reset, "Default", true, is_default);
            if (valid) opt_hit(apply, HP_EDIT_APPLY, o->id, 0);
            opt_hit(reset, HP_EDIT_RESET, o->id, 0);
            break;
        }
        Rect r = {x + w - MIN(roundf(S(260)), w), y, MIN(roundf(S(260)), w), ch};
        sheet_field(r, &g_opt.brand, g_opt.brand_focus, APP_LABEL);
        opt_hit(r, HP_TEXT, o->id, 0);
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
    Rect lr = {r.x, yy, r.w, rh * (f32)vis}; // the rows
    if (g_opt.drop_reveal) {
        f32 st = rh * (f32)g_opt.drop_sel;
        if (st < g_opt.drop_scroll) g_opt.drop_scroll = st;
        if (st + rh > g_opt.drop_scroll + lr.h) g_opt.drop_scroll = st + rh - lr.h;
        g_opt.drop_reveal = false;
    }
    g_opt.drop_scroll = CLAMP(g_opt.drop_scroll, 0.f, rh * (f32)MAX(0, g_opt.drop_n - vis));
    int cur = opt_choice_get(id);
    ClipSave cl = draw_clip_push((SDL_Rect){(int)lr.x, (int)lr.y, (int)lr.w, (int)ceilf(lr.h)});
    for (int k = (int)(g_opt.drop_scroll / rh); k < g_opt.drop_n; k++) {
        Rect ir = {r.x + S(4), yy + rh * (f32)k - g_opt.drop_scroll, r.w - S(8), rh};
        if (ir.y >= lr.y + lr.h) break;
        Rect hr = ir; // (what shows of a row cut by the edge)
        hr.y = MAX(ir.y, lr.y), hr.h = MIN(ir.y + rh, lr.y + lr.h) - hr.y;
        if (moved && opt_hover(hr)) g_opt.drop_sel = k;
        bool sel = k == g_opt.drop_sel;
        if (sel) draw_rrect(ir.x, ir.y, ir.w, ir.h, S(6), C(COL_SEL_FOCUS));
        int ci = g_opt.drop_map[k];
        const char *l = opt_choice_label(id, ci);
        f32 lx = ir.x + S(26);
        int ic = opt_choice_icon(id, ci);
        if (ic >= 0) {
            draw_ui_icon(ic, lx, roundf(ir.y + (rh - S(16)) * 0.5f), roundf(S(16)), C(sel ? COL_SEL_TEXT : COL_TEXT));
            lx += S(24);
        }
        draw_text_fit(f, lx, text_y(f, ir.y, rh), ir.x + ir.w - S(8) - lx, l, (int)strlen(l), C(sel ? COL_SEL_TEXT : COL_TEXT), 0);
        if (ci == cur) { // check mark
            f32 cx = ir.x + S(9), cyy = ir.y + rh * 0.5f, t = MAX(1.5f, S(1.6f));
            Color cc = C(sel ? COL_SEL_TEXT : COL_ACCENT);
            draw_line(cx, cyy, cx + S(3.5f), cyy + S(3.5f), t, cc);
            draw_line(cx + S(3.5f), cyy + S(3.5f), cx + S(10), cyy - S(4), t, cc);
        }
        opt_hit(hr, HP_DROPITEM, id, k);
    }
    draw_clip_pop(cl);
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

// --- actions page ----------------------------------------------------------------------------------------------

static int opt_act_sel_of(const UserAction *a) {
    if (a->sel_max == 0) return 3;
    if (a->sel_min == 1 && a->sel_max == 1) return 1;
    if (a->sel_min == 2 && a->sel_max == INT_MAX) return 2;
    return 0; // (one or more; an exact count other than one also shows here, and stays unless changed)
}

// One line about an action under its name: what it's for, and why it doesn't show when it doesn't.
static const char *opt_act_summary(const UserAction *a) {
    static char b[480];
    char sel[32], miss[256] = "";
    if (!a->name[0] || !a->exec[0]) return "Incomplete: needs a name and a command";
    if (a->sel_max == 0) snprintf(sel, sizeof sel, "Folder menu");
    else if (a->sel_min == a->sel_max) snprintf(sel, sizeof sel, a->sel_min == 1 ? "One item" : "%d items", a->sel_min);
    else snprintf(sel, sizeof sel, a->sel_min >= 2 ? "Several items" : "One or more items");
    if (!a->deps_ok) snprintf(miss, sizeof miss, "Hidden, needs %s \xC2\xB7 ", a->deps);
    const char *types = a->sel_max == 0 ? "" : a->exts[0] ? a->exts : a->mimes[0] ? a->mimes : "any type";
    char grp[96] = "";
    if (a->group[0]) snprintf(grp, sizeof grp, "In %s \xC2\xB7 ", a->group);
    snprintf(b, sizeof b, "%s%s%s%s%s%s", miss, grp, sel, types[0] ? " \xC2\xB7 " : "", types, a->terminal ? " \xC2\xB7 in the terminal" : "");
    return b;
}

static void opt_act_fill(const char *name, const char *exec, const char *exts, const char *deps, const char *group, int sel, int icon, bool term) {
    const char *v[5] = {name, exec, exts, deps, group};
    for (int k = 0; k < 5; k++) {
        te_set(&g_opt.act_te[k], v[k], (int)strlen(v[k]), (int)strlen(v[k]));
        snprintf(g_opt.act_init[k], sizeof g_opt.act_init[k], "%s", v[k]);
    }
    g_opt.act_sel = g_opt.act_init_sel = sel;
    g_opt.act_icon = g_opt.act_init_icon = icon;
    g_opt.act_term = g_opt.act_init_term = term;
    g_opt.act_focus = 0;
    g_opt.scroll = 0;
}

// The editor: action i, or (i < 0) a new one.
static void opt_act_edit(int i) {
    if (i >= 0 && i < g_nua) {
        const UserAction *a = &g_ua[i];
        g_opt.act_edit = i;
        g_opt.act_new = false;
        opt_act_fill(a->name, a->exec, a->exts, a->deps, a->group, opt_act_sel_of(a), a->icon, a->terminal);
    } else {
        g_opt.act_edit = UA_MAX;
        g_opt.act_new = true;
        opt_act_fill("", "", "", "", "", 0, -1, false);
    }
}

static void opt_act_template(int t) {
    if (t < 0 || t >= (int)ARRAY_COUNT(g_act_templates)) return;
    opt_act_edit(-1); // (new: all its values get written)
    te_set(&g_opt.act_te[0], g_act_templates[t].name, (int)strlen(g_act_templates[t].name), (int)strlen(g_act_templates[t].name));
    te_set(&g_opt.act_te[1], g_act_templates[t].exec, 0, 0);
    te_set(&g_opt.act_te[2], g_act_templates[t].exts, 0, 0);
    te_set(&g_opt.act_te[3], g_act_templates[t].deps, 0, 0);
    g_opt.act_sel = g_act_templates[t].sel;
    g_opt.act_icon = ua_icon_find(g_act_templates[t].icon, false);
    g_opt.act_term = g_act_templates[t].term;
}

static void opt_act_close(void) {
    g_opt.act_edit = -1;
    g_opt.act_focus = -1;
    g_opt.scroll = 0;
}

// The editor's action file (path), saved: what changed since it opened (a new one: everything). False: not saved.
static bool opt_act_write(char *path, size_t cap) {
    const char *name = g_opt.act_te[0].buf, *exec = g_opt.act_te[1].buf;
    if (!name[0] || !exec[0]) { toast("An action needs a name and a command"); return false; }
    if (g_opt.act_new) ua_new_file(name, path, cap);
    else if (g_opt.act_edit < g_nua) path_join(path, cap, g_ua_dir, g_ua[g_opt.act_edit].file);
    else return false;
    static const char *keys[5] = {"Name", "Exec", "Extensions", "Dependencies", "Group"};
    const char *k[10], *v[10];
    int n = 0;
    for (int i = 0; i < 5; i++) {
        const char *val = g_opt.act_te[i].buf;
        if (i == 2 && !val[0]) val = "any;"; // (Nemo wants Extensions or Mimetypes)
        if (g_opt.act_new ? i < 3 || val[0] : strcmp(g_opt.act_te[i].buf, g_opt.act_init[i])) k[n] = keys[i], v[n++] = val;
    }
    if (g_opt.act_new || g_opt.act_sel != g_opt.act_init_sel) k[n] = "Selection", v[n++] = g_act_sel_values[g_opt.act_sel];
    if (g_opt.act_new ? g_opt.act_term : g_opt.act_term != g_opt.act_init_term) k[n] = "Terminal", v[n++] = g_opt.act_term ? "true" : "false";
    if (g_opt.act_new ? g_opt.act_icon >= 0 : g_opt.act_icon != g_opt.act_init_icon) k[n] = "Icon", v[n++] = g_opt.act_icon >= 0 ? g_ua_icons[g_opt.act_icon].name : "";
    if (n && !ua_file_set(path, NULL, k, v, n)) { toast("Couldn't save %s: %s", path_basename(path), strerror(errno)); return false; }
    if (g_opt.act_new) ua_put_last(path, cap); // after the others (the files get numbered)
    return true;
}

static void opt_act_save(void) {
    char path[PATH_MAX];
    if (!opt_act_write(path, sizeof path)) return;
    toast("Saved \xE2\x80\x9C%s\xE2\x80\x9D", g_opt.act_te[0].buf);
    ua_load();
    opt_act_close();
}

// An action file in the text editor (the default app for text: .action files have no type of their own).
static void opt_act_open_file(const char *path) {
    if (A.selftest) { snprintf(A.last_opened, sizeof A.last_opened, "edit:%s", path); return; }
    DesktopApp *a = (apps_load(), app_default_for("text/plain"));
    if (!a || !app_launch(a, (char *[]){(char *)path}, 1)) open_path(path);
}

// "Edit as text": the form saved first (nothing typed is lost), then the file.
static void opt_act_edit_text(void) {
    char path[PATH_MAX];
    if (!opt_act_write(path, sizeof path)) return;
    ua_load();
    opt_act_close();
    opt_act_open_file(path);
}

static void opt_act_toggle(int i) {
    if (i < 0 || i >= g_nua) return;
    char path[PATH_MAX];
    path_join(path, sizeof path, g_ua_dir, g_ua[i].file);
    const char *k[1] = {"Active"}, *v[1] = {g_ua[i].active ? "false" : "true"};
    if (ua_file_set(path, NULL, k, v, 1)) g_ua[i].active = !g_ua[i].active;
    else toast("Couldn't save %s: %s", g_ua[i].file, strerror(errno));
}

static void opt_act_delete_cb(int choice, bool check, void *ud) {
    (void)check;
    int i = (int)(intptr_t)ud;
    if (choice != 1 || i < 0 || i >= g_nua) return;
    char path[PATH_MAX];
    path_join(path, sizeof path, g_ua_dir, g_ua[i].file);
    ops_start(op_new(OP_TRASH, (char *[]){path}, 1, NULL));
    memmove(&g_ua[i], &g_ua[i + 1], (size_t)(g_nua - i - 1) * sizeof g_ua[0]); // (the trash takes it on a worker)
    g_nua--;
    opt_act_close();
}

static void opt_act_delete(int i) {
    if (i < 0 || i >= g_nua) return;
    static const char *btn[] = {"Cancel", "Delete"};
    char msg[400];
    snprintf(msg, sizeof msg, "\xE2\x80\x9C%s\xE2\x80\x9D (%s) moves to the Trash.", g_ua[i].name[0] ? g_ua[i].name : g_ua[i].file, g_ua[i].file);
    dialog_open("Delete action?", msg, btn, 2, 1, 0, NULL, opt_act_delete_cb, (void *)(intptr_t)i);
}

static void opt_row_text(f32 x, f32 y, f32 rh, f32 lw, const char *label, const char *desc, bool dim) {
    Font *f = &A.font, *fs = &A.font_small;
    draw_text_fit(f, x + S(14), roundf(y + rh * 0.5f - f->line_h + S(2)), lw, label, (int)strlen(label), C(dim ? COL_TEXT_DIM : COL_TEXT), 0);
    draw_text_fit(fs, x + S(14), roundf(y + rh * 0.5f + S(3)), lw, desc, (int)strlen(desc), C(COL_TEXT_DIM), 0);
}

// Is this program there (a name on $PATH, or a path)? The last answer is kept: asked on every frame of the editor.
static bool opt_act_prog_ok(const char *prog) {
    static char last[256];
    static bool ok;
    if (strcmp(prog, last)) {
        snprintf(last, sizeof last, "%s", prog);
        ok = strchr(prog, '/') ? access(prog, X_OK) == 0 : in_path(prog);
    }
    return ok;
}

// What the command would run - for the files selected in the window, else an example - or what's wrong with it.
// Returns true for a warning; prog: a program that isn't installed (else "").
static bool opt_act_preview(char *out, size_t cap, char *prog, size_t pcap, bool *for_sel) {
    const char *exec = g_opt.act_te[1].buf;
    out[0] = prog[0] = 0;
    *for_sel = false;
    if (!exec[0]) return false;
    char q = 0;
    for (const char *c = exec; *c; c++) {
        if (*c == '\\' && c[1] && q != '\'') c++;
        else if (!q && (*c == '\'' || *c == '"')) q = *c;
        else if (q && *c == q) q = 0;
    }
    if (q) { snprintf(out, cap, "A %s quote isn't closed", q == '"' ? "double" : "single"); return true; }
    for (const char *c = exec; *c; c++) {
        if (*c != '%') continue;
        if (c[1] == '%') { c++; continue; }
        if (!ua_is_code(c[1])) {
            snprintf(out, cap, "%%%c isn't a code: %%F %%U %%f %%e %%P %%p, or %%%% for a %%", c[1] ? c[1] : ' ');
            return true;
        }
        c++;
    }
    Pane *p = active_pane();
    int n = 0;
    bool *dirs = NULL;
    char **paths = NULL, ex[PATH_MAX];
    const char *home = getenv("HOME") ? getenv("HOME") : "/";
    if (g_opt.act_sel != 3) { // (the folder's menu: no items)
        paths = p ? ua_selection(p, &n, &dirs) : NULL;
        *for_sel = n > 0;
        if (!n) { // an example of the file types (the first one)
            char t[64] = "";
            sscanf(g_opt.act_te[2].buf, " %63[^; ]", t);
            if (!strcmp(t, "dir")) snprintf(ex, sizeof ex, "%s/Documents", home);
            else snprintf(ex, sizeof ex, "%s/example.%s", home, t[0] && strcmp(t, "any") && strcmp(t, "nodirs") && strcmp(t, "none") ? t : "txt");
            free(paths);
            paths = strv_dup((char *[]){ex}, 1);
            n = 1;
        }
    }
    UserAction a = {0};
    snprintf(a.exec, sizeof a.exec, "%s", exec);
    UaCtx c = {paths, n, p ? p->path : home};
    int argc = 0;
    char **argv = ua_argv(&a, &c, &argc);
    bool warn = false;
    if (argc && !opt_act_prog_ok(argv[0])) {
        snprintf(prog, pcap, "%s", argv[0]);
        snprintf(out, cap, "\xE2\x80\x9C%s\xE2\x80\x9D isn't installed", path_basename(argv[0]));
        warn = true;
    } else { // the arguments as a shell would take them: quoted where they need it
        size_t o = 0;
        for (int i = 0; i < argc && o < cap; i++) {
            char qa[PATH_MAX * 2];
            bool plain = argv[i][0] != 0;
            for (const char *k = argv[i]; *k && plain; k++) plain = isalnum((unsigned char)*k) || strchr("_./%:=,+@-", *k);
            if (plain) snprintf(qa, sizeof qa, "%s", argv[i]);
            else sh_quote(qa, sizeof qa, argv[i]);
            o += (size_t)snprintf(out + o, cap - o, "%s%s", i ? " " : "", qa);
        }
    }
    strv_free(argv, argc);
    strv_free(paths, n);
    free(dirs);
    return warn;
}

// A preset's file types added to the field (those it has already stay once).
static void opt_act_add_types(int k) {
    char cur[1024], add[256], *save = NULL;
    snprintf(cur, sizeof cur, "%s", g_opt.act_te[2].buf);
    snprintf(add, sizeof add, "%s", g_act_types[k].exts);
    for (char *t = strtok_r(add, "; ", &save); t; t = strtok_r(NULL, "; ", &save)) {
        bool have = false;
        char scan[1024], *s2 = NULL;
        snprintf(scan, sizeof scan, "%s", cur);
        for (char *u = strtok_r(scan, "; ", &s2); u && !have; u = strtok_r(NULL, "; ", &s2)) have = !strcasecmp(u, t) || !strcasecmp(u, "any");
        if (have) continue;
        size_t l = strlen(cur);
        while (l && (cur[l - 1] == ' ' || cur[l - 1] == ';')) cur[--l] = 0;
        snprintf(cur + l, sizeof cur - l, "%s%s", l ? "; " : "", t);
    }
    te_set(&g_opt.act_te[2], cur, (int)strlen(cur), (int)strlen(cur));
}

static f32 opt_draw_act_editor(f32 x, f32 y, f32 w, Rect list) {
    Font *f = &A.font;
    f32 rh = roundf(S(58)), ch = roundf(S(30)), ctlw = MIN(roundf(S(440)), w * 0.58f), lw = w - ctlw - S(44);
    static const char *labels[] = {"Name", "Icon", "Command", "Runs", "Shows for", "File types", "Common types", "Needs programs", "Group", "Run in terminal"};
    static const char *descs[] = {"Its label in the menu", "Next to it in the menu", "%F files \xC2\xB7 %f name \xC2\xB7 %P folder", "",
                                  "How many items selected", "png; jpg \xC2\xB7 dir \xC2\xB7 nodirs", "Add to File types",
                                  "Hidden while one is missing", "Same group, same submenu", "For commands that print or ask"};
    static const char *ph[] = {"Copy image", NULL, "sh -c \"wl-copy < %F\"", NULL, NULL, "Any", NULL, "None", "None: in the menu itself", NULL};
    static const int field[] = {0, -1, 1, -1, -1, 2, -1, 3, 4, -1};
    enum { R_ICON = 1, R_RUNS = 3, R_SEL = 4, R_TYPES = 6, R_GROUP = 8, R_TERM = 9 };
    char pv[1200], prog[256];
    bool for_sel, warn = opt_act_preview(pv, sizeof pv, prog, sizeof prog, &for_sel);
    y = props_heading(x, y, g_opt.act_new ? "New action" : "Edit action");
    int nrows = (int)ARRAY_COUNT(labels);
    props_card(x, y, w, nrows, rh, 0);
    for (int r = 0; r < nrows; r++, y += rh) {
        if (g_opt.act_reveal && field[r] == g_opt.act_focus) { // (applied on the next frame)
            if (y < list.y) g_opt.scroll -= list.y - y;
            else if (y + rh > list.y + list.h) g_opt.scroll += y + rh - list.y - list.h;
            g_opt.act_reveal = false;
            A.redraw = true;
        }
        if (y + rh <= list.y || y >= list.y + list.h) continue;
        const char *desc = r != R_RUNS ? descs[r] : !g_opt.act_te[1].buf[0] ? "What the command will do"
                         : warn ? "Check the command" : for_sel ? "For the selected files" : "For an example file";
        opt_row_text(x, y, rh, r == R_TYPES ? roundf(S(110)) : lw, labels[r], desc, false);
        f32 cx = x + w - ctlw - S(14), cy = y + rh * 0.5f, fy = roundf(cy - ch * 0.5f);
        if (r == R_GROUP) { // the field, and a button listing the groups there are
            Rect br = {cx + ctlw - ch, fy, ch, ch}, fr = {cx, fy, ctlw - ch - S(6), ch};
            sheet_field(fr, &g_opt.act_te[4], g_opt.act_focus == 4 && g_opt.drop_opt < 0, ph[r]);
            opt_hit(fr, HP_ACT_FIELD, -1, 4);
            sheet_button(br, "", false, g_nuag > 0);
            draw_chevron(br.x + ch * 0.5f, cy, S(6), 3, MAX(1.f, S(1.4f)), C(g_nuag ? COL_TEXT : COL_TEXT_FAINT));
            g_opt.act_group_r = (Rect){cx, fy, ctlw, ch};
            if (g_nuag) opt_hit(br, HP_ACT_GROUPS, -1, 0);
        } else if (r == R_RUNS) { // the preview, or a warning (a missing program: a button adds it to Needs programs)
            Font *fs = &A.font_small;
            f32 tx = cx, tw = ctlw;
            bool listed = false;
            if (prog[0]) {
                char deps[512], *save = NULL;
                snprintf(deps, sizeof deps, "%s", g_opt.act_te[3].buf);
                for (char *t = strtok_r(deps, "; ", &save); t && !listed; t = strtok_r(NULL, "; ", &save)) listed = !strcmp(t, prog);
            }
            if (prog[0] && !listed) {
                const char *bl = "Add to Needs programs";
                f32 bw = text_width(f, bl, (int)strlen(bl)) + S(24);
                Rect br = {cx + ctlw - bw, fy, bw, ch};
                sheet_button(br, bl, false, true);
                opt_hit(br, HP_ACT_NEEDS, -1, 0);
                tw -= bw + S(10);
            }
            if (prog[0] && listed) snprintf(pv + strlen(pv), sizeof pv - strlen(pv), " (hidden until it is)");
            if (pv[0]) draw_text_fit(fs, tx, text_y(fs, fy, ch), tw, pv, (int)strlen(pv), C(warn ? COL_WARN : COL_TEXT_DIM), 0);
        } else if (r == R_TYPES) { // presets, right-aligned
            f32 bx = x + w - S(14);
            for (int k = (int)ARRAY_COUNT(g_act_types) - 1; k >= 0; k--) {
                f32 bw = text_width(f, g_act_types[k].label, (int)strlen(g_act_types[k].label)) + S(18);
                bx -= bw;
                Rect br = {bx, fy, bw, ch};
                sheet_button(br, g_act_types[k].label, false, true);
                opt_hit(br, HP_ACT_TYPE, -1, k);
                bx -= S(6);
            }
        } else if (field[r] >= 0) {
            Rect fr = {cx, fy, ctlw, ch};
            sheet_field(fr, &g_opt.act_te[field[r]], g_opt.act_focus == field[r] && g_opt.drop_opt < 0, ph[r]);
            opt_hit(fr, HP_ACT_FIELD, -1, field[r]);
        } else if (r == R_ICON) opt_draw_control(&(OptDef){OC_ACTIONS, OK_CHOICE, OPT_ACT_ICON, "", ""}, cx, cy, ctlw);
        else if (r == R_SEL) opt_draw_control(&(OptDef){OC_ACTIONS, OK_CHOICE, OPT_ACT_SEL, "", ""}, cx, cy, ctlw);
        else if (r == R_TERM) opt_draw_control(&(OptDef){OC_ACTIONS, OK_BOOL, OPT_ACT_TERM, "", ""}, cx, cy, ctlw);
    }
    // buttons: Edit as text (and Delete) on the left, Cancel + Save on the right
    y += roundf(S(14));
    if (y + ch > list.y && y < list.y + list.h) {
        f32 bw = roundf(S(96)), tw = text_width(f, "Edit as text", 12) + S(28);
        Rect rtext = {x, y, tw, ch}, rsave = {x + w - bw, y, bw, ch}, rcancel = {rsave.x - S(8) - bw, y, bw, ch};
        bool ok = g_opt.act_te[0].buf[0] && g_opt.act_te[1].buf[0];
        sheet_button(rtext, "Edit as text", false, ok);
        if (ok) opt_hit(rtext, HP_ACT_TEXT, -1, 0);
        if (!g_opt.act_new) {
            Rect rdel = {rtext.x + rtext.w + S(8), y, bw, ch};
            sheet_button(rdel, "Delete", false, true);
            opt_hit(rdel, HP_ACT_DELETE, -1, g_opt.act_edit);
        }
        sheet_button(rcancel, "Cancel", false, true);
        opt_hit(rcancel, HP_ACT_CANCEL, -1, 0);
        sheet_button(rsave, "Save", true, ok);
        if (ok) opt_hit(rsave, HP_ACT_SAVE, -1, 0);
    }
    return y + ch;
}

// Options > Actions: adding, then the actions (search: the matching ones), or the editor.
static f32 opt_draw_actions(f32 x, f32 y, f32 w, Rect list, const char *q) {
    if (!q[0] && g_opt.act_edit >= 0) return opt_draw_act_editor(x, y, w, list);
    f32 rh = roundf(S(58)), ch = roundf(S(30)), gap = roundf(S(18)), bw = roundf(S(80)), aw = roundf(S(34));
    if (!q[0]) {
        y = props_heading(x, y, "Add");
        props_card(x, y, w, 3, rh, 0);
        static const char *l[] = {"New action", "From a template", "Actions folder"};
        const char *d[] = {"Your own command in the right-click menu", "Ready-made actions to start from", g_ua_dir};
        for (int r = 0; r < 3; r++, y += rh) {
            if (y + rh <= list.y || y >= list.y + list.h) continue;
            opt_row_text(x, y, rh, w - roundf(S(300)), l[r], d[r], false);
            Rect br = {x + w - S(14) - bw, roundf(y + (rh - ch) * 0.5f), bw, ch};
            if (r == 1) opt_draw_control(&(OptDef){OC_ACTIONS, OK_CHOICE, OPT_ACT_TEMPLATE, "", ""}, x + w - S(14) - roundf(S(260)), y + rh * 0.5f, roundf(S(260)));
            else {
                sheet_button(br, r ? "Open" : "New", !r, true);
                opt_hit(br, r ? HP_ACT_FOLDER : HP_ACT_NEW, -1, 0);
            }
        }
        y += gap;
    }
    int nm = 0;
    for (int i = 0; i < g_nua; i++) nm += !q[0] || strcasestr(g_ua[i].name, q) || strcasestr("actions", q);
    if (!q[0] && nm) y = props_heading(x, y, "Your actions");
    if (nm) props_card(x, y, w, nm, rh, 0);
    for (int i = 0; i < g_nua; i++) {
        const UserAction *a = &g_ua[i];
        if (q[0] && !strcasestr(a->name, q) && !strcasestr("actions", q)) continue;
        if (y + rh > list.y && y < list.y + list.h) {
            f32 by = roundf(y + (rh - ch) * 0.5f), bx = x + w - S(12) - bw;
            Rect rdel = {bx, by, bw, ch}, rdown = {rdel.x - S(8) - aw, by, aw, ch}, rup = {rdown.x - S(4) - aw, by, aw, ch};
            Rect redit = {rup.x - S(8) - bw, by, bw, ch}, rtog = {redit.x - S(14) - roundf(S(40)), roundf(y + rh * 0.5f - S(10)), roundf(S(40)), roundf(S(20))};
            draw_ui_icon(ua_icon_ui(a->icon, false), x + S(16), roundf(y + (rh - S(18)) * 0.5f), roundf(S(18)), C(ua_usable(a) ? COL_TEXT : COL_TEXT_DIM));
            opt_row_text(x + S(30), y, rh, rtog.x - x - S(54), a->name[0] ? a->name : a->file, opt_act_summary(a), !ua_usable(a));
            opt_toggle_draw(rtog, a->active, opt_hover(rtog));
            opt_hit(rtog, HP_ACT_TOGGLE, -1, i);
            sheet_button(redit, "Edit", false, true);
            opt_hit(redit, HP_ACT_EDIT, -1, i);
            sheet_button(rup, "", false, i > 0);
            draw_chevron(rup.x + aw * 0.5f, rup.y + ch * 0.5f, S(9), 2, MAX(1.f, S(1.5f)), C(i > 0 ? COL_TEXT : COL_TEXT_FAINT));
            if (i > 0) opt_hit(rup, HP_ACT_UP, -1, i);
            sheet_button(rdown, "", false, i < g_nua - 1);
            draw_chevron(rdown.x + aw * 0.5f, rdown.y + ch * 0.5f, S(9), 3, MAX(1.f, S(1.5f)), C(i < g_nua - 1 ? COL_TEXT : COL_TEXT_FAINT));
            if (i < g_nua - 1) opt_hit(rdown, HP_ACT_DOWN, -1, i);
            sheet_button(rdel, "Delete", false, true);
            opt_hit(rdel, HP_ACT_DELETE, -1, i);
        }
        y += rh;
    }
    if (!q[0] && !g_nua) draw_text(&A.font_small, x + S(14), y + S(10), "No actions yet", 14, C(COL_TEXT_FAINT));
    if (!q[0] && g_nuag) { // the groups (Group= in the editor): their icons
        y = props_heading(x, y + gap, "Groups");
        props_card(x, y, w, g_nuag, rh, 0);
        for (int g = 0; g < g_nuag; g++, y += rh) {
            if (y + rh <= list.y || y >= list.y + list.h) continue;
            int na = 0;
            for (int i = 0; i < g_nua; i++) na += !strcmp(g_ua[i].group, g_uag[g].name);
            char d[64];
            snprintf(d, sizeof d, "%d action%s \xC2\xB7 a submenu", na, na == 1 ? "" : "s");
            draw_ui_icon(ua_icon_ui(g_uag[g].icon, true), x + S(16), roundf(y + (rh - S(18)) * 0.5f), roundf(S(18)), C(COL_TEXT));
            opt_row_text(x + S(30), y, rh, w - roundf(S(320)), g_uag[g].name, d, false);
            opt_draw_control(&(OptDef){OC_ACTIONS, OK_CHOICE, OPT_ACT_GICON + g, "", ""}, x + w - S(14) - roundf(S(260)), y + rh * 0.5f, roundf(S(260)));
        }
    }
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
    bool sfocus = g_opt.search_focus && g_opt.drop_opt < 0 && !g_opt.lname_focus && !g_opt.brand_focus && !g_opt.scroll_focus && g_opt.act_focus < 0 && !g_opt.app_icon_focus && !g_opt.title_icon_focus && !g_opt.terminal_path_focus;
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
            draw_rrect(nr.x, nr.y, nr.w, nr.h, S(7), C(navigation_surface_color(COL_POPUP, true)));
            if (g_nav_selector_mark) {
                ClipSave ac = draw_clip_push((SDL_Rect){(int)nr.x, (int)nr.y, (int)roundf(S(3)), (int)nr.h});
                draw_rrect(nr.x, nr.y, nr.w, nr.h, S(7), C(COL_ACCENT));
                draw_clip_pop(ac);
            }
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
                if (ok->id == OPT_APP_ICON || ok->id == OPT_TITLE_ICON) // preview + field + mode buttons
                    ctlw = opt_icon_row_ctlw(rr.w, ok->id == OPT_APP_ICON ? 3 : 2);
                if (ok->kind == OK_BOOL || ok->kind == OK_BUTTON) lw = rr.w - roundf(S(200)); // (narrow controls: more room for text)
                else lw = rr.w - ctlw - S(44);
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
    if (!q[0] && g_opt.cat == OC_ACTIONS) {
        y = opt_draw_actions(cx, y, cwid, list, q) + gap;
        shown++;
    } else if (q[0]) { // matching actions in search results
        int nm = 0;
        for (int i = 0; i < g_nua; i++) nm += strcasestr(g_ua[i].name, q) || strcasestr("actions", q);
        if (nm) {
            y = props_heading(cx, y, "Actions");
            y = opt_draw_actions(cx, y, cwid, list, q) + gap;
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
        bool sh;
        f32 ty = sheet_scrollbar(&g_opt.scroll, g_opt.max_scroll, (Rect){list.x + list.w - S(14), list.y, S(14), list.h}, list.y, list.h, th, &sh);
        draw_rrect(list.x + list.w - S(6) - tw * 0.5f, ty, tw, th, tw * 0.5f, C(sh ? COL_SCROLL_HOVER : COL_SCROLL));
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

// (sheet_text_focus) A click on a text field: the search field, the layout name, the name above the sidebar or the
// open dropdown's filter.
static bool options_text_focus(TextEdit *edit, bool focused) {
    if (g_opt.rec_def >= 0) return false; // the click ends recording a shortcut first (options_click)
    if (g_opt.drop_opt >= 0) return edit == &g_opt.drop_filter;
    int act = -1;
    for (int k = 0; k < 5; k++)
        if (edit == &g_opt.act_te[k]) act = k;
    if (edit != &g_opt.search && edit != &g_opt.lname && edit != &g_opt.brand && edit != &g_opt.scroll_color && edit != &g_opt.app_icon &&
        edit != &g_opt.title_icon && edit != &g_opt.terminal_path && act < 0)
        return focused;
    g_opt.search_focus = edit == &g_opt.search;
    g_opt.lname_focus = edit == &g_opt.lname;
    g_opt.brand_focus = edit == &g_opt.brand;
    g_opt.scroll_focus = edit == &g_opt.scroll_color;
    g_opt.app_icon_focus = edit == &g_opt.app_icon;
    g_opt.title_icon_focus = edit == &g_opt.title_icon;
    g_opt.terminal_path_focus = edit == &g_opt.terminal_path;
    g_opt.act_focus = act;
    return true;
}

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
    if (sheet_scrollbar_press(x, y)) return;
    g_opt.act_focus = -1;
    g_opt.search_focus = g_opt.lname_focus = g_opt.brand_focus = g_opt.scroll_focus = g_opt.app_icon_focus = g_opt.title_icon_focus = g_opt.terminal_path_focus = false; // a click elsewhere drops the keyboard (typing a letter still searches)
    for (int k = g_opt.nhits - 1; k >= 0; k--) {
        OptHit *hp = &g_opt.hits[k];
        if (hp->r.w <= 0 || !in_rect(hp->r, x, y)) continue;
        switch (hp->part) {
        case HP_CLOSE: sheet_close(); return;
        case HP_SEARCH: g_opt.search_focus = true; return;
        case HP_NAV:
            g_opt.cat = hp->arg;
            g_opt.lname_focus = hp->arg == OC_LAYOUTS;
            g_opt.scroll = 0;
            opt_act_close();
            te_set(&g_opt.search, "", 0, 0);
            return;
        case HP_TOGGLE: opt_bool_toggle(hp->opt); return;
        case HP_SEG: opt_choice_set(hp->opt, hp->arg); return;
        case HP_DROP: opt_drop_open(hp->opt, hp->r); return;
        case HP_MINUS: opt_int_step(hp->opt, -1); return;
        case HP_PLUS: opt_int_step(hp->opt, +1); return;
        case HP_SWATCH:
            g_accent_desktop = false;
            g_accent_custom = hp->arg == 0 ? 0 : 0x01000000u | g_accents[hp->arg - 1].rgb;
            theme_apply();
            desktop_look_sync();
            A.settings_dirty = true;
            return;
        case HP_BUTTON:
            if (hp->opt == OPT_APP_ICON || hp->opt == OPT_TITLE_ICON) opt_icon_button(hp->opt, hp->arg);
            else opt_button(hp->opt);
            return;
        case HP_LNAME: g_opt.lname_focus = true; return;
        case HP_TEXT:
            if (hp->opt == OPT_TERMINAL_PATH) g_opt.terminal_path_focus = true;
            else if (hp->arg == 3) g_opt.title_icon_focus = true;
            else if (hp->arg == 2) g_opt.app_icon_focus = true;
            else if (hp->arg) g_opt.scroll_focus = true;
            else g_opt.brand_focus = true;
            return;
        case HP_EDIT_APPLY:
            if (hp->opt == OPT_TERMINAL_PATH) opt_terminal_path_apply();
            else opt_scroll_color_apply();
            return;
        case HP_EDIT_RESET:
            if (hp->opt == OPT_TERMINAL_PATH) {
                te_set(&g_opt.terminal_path, "", 0, 0); // Default: $TERMINAL / the desktop's own
                opt_terminal_path_apply();
            } else {
                te_set(&g_opt.scroll_color, "", 0, 0); // empty is Auto: the theme's own colour
                opt_scroll_color_apply();
            }
            return;
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
        case HP_ACT_NEW: opt_act_edit(-1); return;
        case HP_ACT_FOLDER:
            mkdir_p(g_ua_dir, 0755);
            open_path(g_ua_dir);
            return;
        case HP_ACT_TOGGLE: opt_act_toggle(hp->arg); return;
        case HP_ACT_EDIT: opt_act_edit(hp->arg); return;
        case HP_ACT_UP: ua_move(hp->arg, -1); return;
        case HP_ACT_DOWN: ua_move(hp->arg, +1); return;
        case HP_ACT_DELETE: opt_act_delete(hp->arg); return;
        case HP_ACT_FIELD: g_opt.act_focus = hp->arg; return;
        case HP_ACT_TEXT: opt_act_edit_text(); return;
        case HP_ACT_TYPE: opt_act_add_types(hp->arg); return;
        case HP_ACT_GROUPS: opt_drop_open(OPT_ACT_GROUP, g_opt.act_group_r); return;
        case HP_ACT_NEEDS: { // the missing program into Needs programs
            char pv[1200], prog[256], v[1024];
            bool fs;
            opt_act_preview(pv, sizeof pv, prog, sizeof prog, &fs);
            if (!prog[0]) return;
            const char *cur = g_opt.act_te[3].buf;
            snprintf(v, sizeof v, "%s%s%s", cur, cur[0] ? "; " : "", prog); // (a path stays one: Dependencies checks it)
            te_set(&g_opt.act_te[3], v, (int)strlen(v), (int)strlen(v));
            return;
        }
        case HP_ACT_CANCEL: opt_act_close(); return;
        case HP_ACT_SAVE: opt_act_save(); return;
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
    if (g_opt.brand_focus && g_opt.drop_opt < 0) {
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_ESCAPE) g_opt.brand_focus = false;
        else if (te_key(&g_opt.brand, k, mod)) opt_brand_sync();
        return;
    }
    if (g_opt.scroll_focus && g_opt.drop_opt < 0) {
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) opt_scroll_color_apply();
        else if (k == SDLK_ESCAPE) g_opt.scroll_focus = false;
        else te_key(&g_opt.scroll_color, k, mod);
        return;
    }
    if (g_opt.act_focus >= 0 && g_opt.drop_opt < 0) { // the action editor: Tab between its fields, Enter saves
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) opt_act_save();
        else if (k == SDLK_ESCAPE) g_opt.act_focus = -1;
        else if (k == SDLK_TAB) g_opt.act_focus = (g_opt.act_focus + (mod & SDL_KMOD_SHIFT ? 4 : 1)) % 5, g_opt.act_reveal = true;
        else te_key(&g_opt.act_te[g_opt.act_focus], k, mod);
        return;
    }
    if (g_opt.app_icon_focus && g_opt.drop_opt < 0) {
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) opt_app_icon_apply();
        else if (k == SDLK_ESCAPE) g_opt.app_icon_focus = false;
        else te_key(&g_opt.app_icon, k, mod);
        return;
    }
    if (g_opt.title_icon_focus && g_opt.drop_opt < 0) {
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) opt_title_icon_apply();
        else if (k == SDLK_ESCAPE) g_opt.title_icon_focus = false;
        else te_key(&g_opt.title_icon, k, mod);
        return;
    }
    if (g_opt.terminal_path_focus && g_opt.drop_opt < 0) {
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) opt_terminal_path_apply();
        else if (k == SDLK_ESCAPE) g_opt.terminal_path_focus = false;
        else te_key(&g_opt.terminal_path, k, mod);
        return;
    }
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
        case SDLK_DOWN: g_opt.drop_sel = MIN(g_opt.drop_sel + 1, g_opt.drop_n - 1), g_opt.drop_reveal = true; return;
        case SDLK_UP: g_opt.drop_sel = MAX(g_opt.drop_sel - 1, 0), g_opt.drop_reveal = true; return;
        case SDLK_PAGEDOWN: g_opt.drop_sel = MIN(g_opt.drop_sel + vis, g_opt.drop_n - 1), g_opt.drop_reveal = true; return;
        case SDLK_PAGEUP: g_opt.drop_sel = MAX(g_opt.drop_sel - vis, 0), g_opt.drop_reveal = true; return;
        default:
            if (te_key(&g_opt.drop_filter, k, mod)) opt_drop_filter();
            return;
        }
    }
    switch (k) {
    case SDLK_ESCAPE:
        if (g_opt.search.buf[0]) { te_set(&g_opt.search, "", 0, 0); g_opt.scroll = 0; g_opt.search_focus = false; }
        else if (g_opt.search_focus) g_opt.search_focus = false;
        else if (g_opt.act_edit >= 0) opt_act_close(); // (Esc leaves the action editor first, unsaved)
        else sheet_close();
        return;
    case SDLK_TAB:
        if (!g_opt.search.buf[0]) { g_opt.cat = (g_opt.cat + (mod & SDL_KMOD_SHIFT ? OC_COUNT - 1 : 1)) % OC_COUNT; g_opt.scroll = 0; g_opt.lname_focus = g_opt.cat == OC_LAYOUTS; opt_act_close(); }
        return;
    case SDLK_PAGEDOWN: glide_by(&g_opt.scroll, S(300), g_opt.max_scroll); return;
    case SDLK_PAGEUP: glide_by(&g_opt.scroll, -S(300), g_opt.max_scroll); return;
    default: // editing keys only reach the search field once it has the keyboard (letters focus it: options_text)
        if (g_opt.search_focus && te_key(&g_opt.search, k, mod)) g_opt.scroll = 0;
    }
}

static void options_text(const char *text) {
    if (g_opt.rec_def >= 0) return;
    if (g_opt.brand_focus && g_opt.drop_opt < 0) { te_insert(&g_opt.brand, text); opt_brand_sync(); return; }
    if (g_opt.lname_focus && g_opt.drop_opt < 0) { te_insert(&g_opt.lname, text); return; }
    if (g_opt.act_focus >= 0 && g_opt.drop_opt < 0) { te_insert(&g_opt.act_te[g_opt.act_focus], text); return; }
    if (g_opt.scroll_focus && g_opt.drop_opt < 0) { te_insert(&g_opt.scroll_color, text); return; }
    if (g_opt.app_icon_focus && g_opt.drop_opt < 0) { te_insert(&g_opt.app_icon, text); return; }
    if (g_opt.title_icon_focus && g_opt.drop_opt < 0) { te_insert(&g_opt.title_icon, text); return; }
    if (g_opt.terminal_path_focus && g_opt.drop_opt < 0) { te_insert(&g_opt.terminal_path, text); return; }
    if (g_opt.drop_opt >= 0) {
        te_insert(&g_opt.drop_filter, text);
        opt_drop_filter();
        return;
    }
    g_opt.search_focus = true; // type to search
    te_insert(&g_opt.search, text);
    g_opt.scroll = 0;
}

static void options_wheel(f32 dy) {
    if (g_opt.drop_opt >= 0) { glide_by(&g_opt.drop_scroll, -dy * 3 * roundf(S(28)), 1e9f); return; } // (clamped when drawn)
    glide_by(&g_opt.scroll, -dy * S(60), g_opt.max_scroll);
}

static void options_open_page(int cat) {
    options_open();
    g_opt.cat = cat;
    g_opt.lname_focus = cat == OC_LAYOUTS;
}

static bool options_is_open(void) { return g_sheet.kind == SHEET_OPTIONS; }
