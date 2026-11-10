// app.c - file-explorer: window, tabs, split panes, layout, input, rendering

extern char **environ;

#define S(x) ((x) * A.scale)
#define APP_NAME "File Explorer" // system-facing name: window title, launcher, tray
#define APP_LABEL "Explorer"   // shown next to the logo inside the app
#define APP_VERSION "0.10.0"    // bumped with each tagged release

typedef struct { f32 x, y, w, h; } Rect;
static inline bool in_rect(Rect r, f32 x, f32 y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

// --- single-line text editing (path bar, inline rename) --------------------

typedef struct {
    char buf[PATH_MAX];
    int caret, anchor; // selection is [min, max) of the two
    f32 scroll;
} TextEdit;

static void te_set(TextEdit *t, const char *s, int anchor, int caret) {
    snprintf(t->buf, sizeof t->buf, "%s", s);
    int n = (int)strlen(t->buf);
    t->anchor = CLAMP(anchor, 0, n);
    t->caret = CLAMP(caret, 0, n);
    t->scroll = 0;
}

static int te_prev(const TextEdit *t, int i) {
    if (i <= 0) return 0;
    i--;
    while (i > 0 && ((u8)t->buf[i] & 0xC0) == 0x80) i--;
    return i;
}

static int te_next(const TextEdit *t, int i) {
    int n = (int)strlen(t->buf);
    if (i >= n) return n;
    i++;
    while (i < n && ((u8)t->buf[i] & 0xC0) == 0x80) i++;
    return i;
}

static bool te_sep(char c) { return c == ' ' || c == '/' || c == '.' || c == '-' || c == '_'; }

static int te_word_left(const TextEdit *t, int i) {
    while (i > 0 && te_sep(t->buf[i - 1])) i--;
    while (i > 0 && !te_sep(t->buf[i - 1])) i--;
    return i;
}

static int te_word_right(const TextEdit *t, int i) {
    int n = (int)strlen(t->buf);
    while (i < n && te_sep(t->buf[i])) i++;
    while (i < n && !te_sep(t->buf[i])) i++;
    return i;
}

static void te_delete_sel(TextEdit *t) {
    int lo = MIN(t->anchor, t->caret), hi = MAX(t->anchor, t->caret);
    if (lo == hi) return;
    memmove(t->buf + lo, t->buf + hi, strlen(t->buf + hi) + 1);
    t->anchor = t->caret = lo;
}

static void te_insert(TextEdit *t, const char *s) {
    te_delete_sel(t);
    size_t n = strlen(t->buf), k = strlen(s);
    if (n + k + 1 > sizeof t->buf) return;
    memmove(t->buf + t->caret + k, t->buf + t->caret, n - (size_t)t->caret + 1);
    memcpy(t->buf + t->caret, s, k);
    t->caret += (int)k;
    t->anchor = t->caret;
}

static bool te_key(TextEdit *t, SDL_Keycode key, SDL_Keymod mod) {
    bool ctrl = mod & SDL_KMOD_CTRL, shift = mod & SDL_KMOD_SHIFT;
    int n = (int)strlen(t->buf), lo = MIN(t->anchor, t->caret), hi = MAX(t->anchor, t->caret);
    switch (key) {
    case SDLK_LEFT:
        if (lo != hi && !shift) t->caret = lo;
        else t->caret = ctrl ? te_word_left(t, t->caret) : te_prev(t, t->caret);
        if (!shift) t->anchor = t->caret;
        return true;
    case SDLK_RIGHT:
        if (lo != hi && !shift) t->caret = hi;
        else t->caret = ctrl ? te_word_right(t, t->caret) : te_next(t, t->caret);
        if (!shift) t->anchor = t->caret;
        return true;
    case SDLK_HOME: t->caret = 0; if (!shift) t->anchor = 0; return true;
    case SDLK_END: t->caret = n; if (!shift) t->anchor = n; return true;
    case SDLK_BACKSPACE:
        if (lo == hi) t->anchor = ctrl ? te_word_left(t, t->caret) : te_prev(t, t->caret);
        te_delete_sel(t);
        return true;
    case SDLK_DELETE:
        if (lo == hi) t->anchor = ctrl ? te_word_right(t, t->caret) : te_next(t, t->caret);
        te_delete_sel(t);
        return true;
    case SDLK_A:
        if (ctrl) { t->anchor = 0; t->caret = n; return true; }
        break;
    case SDLK_C: case SDLK_X:
        if (!ctrl) break;
        if (lo != hi) {
            char tmp[PATH_MAX];
            snprintf(tmp, sizeof tmp, "%.*s", hi - lo, t->buf + lo);
            SDL_SetClipboardText(tmp);
            if (key == SDLK_X) te_delete_sel(t);
        }
        return true;
    case SDLK_V:
        if (!ctrl) break;
        {
            char *c = SDL_GetClipboardText();
            if (c) {
                for (char *q = c; *q; q++)
                    if (*q == '\n' || *q == '\r' || *q == '\t') *q = ' ';
                te_insert(t, c);
                SDL_free(c);
            }
        }
        return true;
    default: break;
    }
    return false;
}

// --- state -----------------------------------------------------------------

typedef struct {
    Rect r;
    int end; // byte offset in path where this crumb's prefix ends
} Crumb;

// --- views -------------------------------------------------------------------
enum { VIEW_DETAILS, VIEW_LIST, VIEW_GRID };
enum { TITLEBAR_AUTO, TITLEBAR_SYSTEM, TITLEBAR_APP }; // A.title_bar
static const struct { int view, px; const char *label; } g_zoom[] = {
    {VIEW_DETAILS, 0, "Details"},       {VIEW_LIST, 0, "List"},          {VIEW_GRID, 48, "Small icons"},
    {VIEW_GRID, 72, "Small+ icons"},    {VIEW_GRID, 96, "Medium icons"}, {VIEW_GRID, 128, "Medium+ icons"},
    {VIEW_GRID, 176, "Large icons"},    {VIEW_GRID, 256, "Extra large icons"},
};
#define ZOOM_COUNT ((int)ARRAY_COUNT(g_zoom))

// --- columns -----------------------------------------------------------------
enum { COL_NAME, COL_DATE, COL_CREATED, COL_TYPE, COL_SIZE, COL_EXT, COL_PERMS, COL_OWNER, COL_COUNT };
static const struct {
    const char *label, *key;
    f32 defw; // logical px (Name is flexible)
    bool right;
    int sort;
} g_coldef[COL_COUNT] = {
    {"Name", "name", 0, false, SORT_NAME},
    {"Date modified", "date", 140, false, SORT_DATE},
    {"Date created", "created", 140, false, SORT_CREATED},
    {"Type", "type", 110, false, SORT_TYPE},
    {"Size", "size", 88, true, SORT_SIZE},
    {"Extension", "ext", 80, false, SORT_EXT},
    {"Permissions", "perms", 96, false, SORT_PERMS},
    {"Owner", "owner", 90, false, SORT_OWNER},
};

typedef struct {
    Rect area, toolbar, back, fwd, up, pathbar, filterbtn, header, list, track, thumb, trashbar, rrestore, rempty;
    bool has_trashbar;
    f32 col_name;              // x of the icon inside the Name column
    f32 name_end;              // right edge of the Name column
    f32 col_end;
    int ncols;                 // visible columns in display order
    int cid[COL_COUNT];
    f32 cx0[COL_COUNT], cx1[COL_COUNT];
    f32 row_h;
    f32 cell_w, cell_h, pad; // item cells (details: one full-width row per item)
    int per_row;
    bool has_scroll;
    Rect gitchip;  // branch chip at the end of the path bar
    bool has_gitchip;
    Rect sbar;     // bottom search/filter bar (when open)
    bool has_sbar;
    Rect cmpbar;   // folder compare bar (while this pane is compared)
    bool has_cmpbar;
    Rect snapbar;  // inside a snapshot: where this is, Compare with now, Restore
    bool has_snapbar;
    Crumb crumbs[128];
    int ncrumbs;
    bool crumbs_trash; // first crumb is the trash (not the root)
    bool crumbs_elided;
    i64 hover; // row under the mouse in the last frame (-1 none)
    int drawn; // item rows drawn in the last frame (tests: catches blank / flickering frames)
} PaneLayout;

typedef struct Pane {
    char path[PATH_MAX];
    Listing *L;
    u32 *order;     // visible entries (entry indices) in display order
    u32 count;
    u8 *sel;        // per-entry selection flags
    u32 sel_count;
    i64 cursor, anchor;
    f64 scroll, scroll_target;
    int sort_key;
    bool sort_desc;
    u32 sort_gen;
    bool sort_pending;
    bool complete_seen;
    char select_name[PATH_MAX]; // relative path in the tree view
    char **hist;
    int hist_n, hist_pos, hist_cap;
    i64 prio_first;
    char **select_names; // sorted; applied (selected) when the next scan lands
    int nselect_names;
    bool reload_no_select; // reload: restore cursor without selecting it
    Listing *L_next;       // background reload; swapped in when ready (no flicker)
    Listing *L_prev;       // the listing a reload replaced: its folder sizes carry over once L completes
    bool keep_scroll;      // the reload is a refresh: the view stays where it is, even if the cursor is off screen
    bool from_reload;
    char search_q[256];    // non-empty while showing recursive search results
    char search_next_q[256]; // query of a staged search (L_next) not yet shown
    bool resort_on_complete;
    int view, icon_px; // VIEW_*, grid icon size (logical px)
    int group;         // GROUP_*
    int tf_mode;       // type filter: 0 all, 1 files only, 2 folders only (kept while navigating)
    char tf_exts[512]; // hidden extensions "|png|"
    // grouping (details view): order holds the visible items, gall the full sorted order
    u32 *gall, gall_n;
    u32 *row_of;       // display row of each visible item
    u32 nrows;
    struct GroupHdr *hdr;
    int nhdr;
    char **collapsed;  // labels of collapsed groups
    int ncollapsed;
    // tree view (details): folders expand in place
    bool tree;
    char **expanded; // relative folder paths, sorted
    u64 git_lt;      // start time of the listing git was last asked for (a new listing = ask again)
    u64 gitsub_lt;   // same, for the repositories among its folders (branch pills)
    int cs_view;     // contents search switched the view to Details: the view to restore + 1 (0 = none)
    bool du;         // disk usage view (a map of what takes the space) instead of the file list
    struct DuView *dv;
    bool snapv;      // snapshots view: the drive's snapshots instead of the file list
    struct SnapView *sv;
    int nexpanded;
    PaneLayout ly;
} Pane;

typedef struct FolderView {
    char *path;
    int view, icon, sort, group;
    bool desc, recursive; // recursive: also applies to subfolders without their own setting
} FolderView;

typedef struct GroupHdr {
    u32 row, count;
    char label[64];
    bool collapsed;
} GroupHdr;

typedef struct Panel Panel;

typedef struct Term Term; // term.c

typedef struct Tab {
    Pane pane[2]; // only pane[0] is used (npanes == 1); kept as an array so per-tab loops stay uniform
    int npanes, active;
    Panel *panel; // the panel whose tab bar shows this tab
    Rect r;       // rect in the panel's tab bar
    Term *term;   // a terminal tab (its pane just tracks the start folder)
} Tab;

static void term_free(Term *t);
static const char *term_label(Term *t);
static bool term_key(Term *t, SDL_Keycode k, SDL_Keymod mod);
static void term_text(Term *t, const char *s);
static void term_mouse_down(Term *t, int button, int clicks, f32 x, f32 y, SDL_Keymod mod);
static void term_mouse_move(Term *t, f32 x, f32 y, bool held);
static void term_mouse_up(Term *t, int button);
static void term_wheel(Term *t, f32 dy);
static void term_copy(Term *t);
static void term_paste(Term *t);
static void term_paste_text(Term *t, const char *s);
static void term_draw(Term *t, Rect area, bool focused);
static Term *term_new(const char *dir, char *const argv[], int rows, int cols);
static void term_tick(void);
static int g_term_look_setting; // Options: 0 match the default terminal, 1 the app's own theme
static void term_clear_scrollback(Term *t);
static bool term_wants_mouse(Term *t, SDL_Keymod mod);
static bool term_has_sel(Term *t);
static u32 term_bg_color(void);
static u32 term_fg_color(void);
static bool term_settled(Term *t);

// The window is a tree of panels (File Pilot style): every panel has its own tab bar.
struct Panel {
    Tab *cur; // visible tab
    Rect area, bar, plus, splitbtn;
};

enum { NODE_PANEL, NODE_HSPLIT, NODE_VSPLIT }; // HSPLIT: a | b, VSPLIT: a over b
typedef struct Node {
    int kind;
    f32 ratio;
    struct Node *a, *b, *parent;
    Panel *panel;
    Rect r, div;
} Node;

enum { ZONE_LEFT, ZONE_RIGHT, ZONE_TOP, ZONE_BOTTOM, ZONE_CENTER };
enum { TD_NONE, TD_BAR, TD_CENTER, TD_EDGE };
#define MAX_PANELS 16

// Quick Access: user groups of folders in the sidebar (Devices follow, automatically)
typedef struct { char label[64]; char path[PATH_MAX]; } QaItem; // label "" = folder name
typedef struct { char name[64]; bool collapsed; QaItem *items; int n; } QaGroup;

enum { PK_FOLDER, PK_HOME, PK_DRIVE, PK_PINNED, PK_TRASH, PK_GROUP, PK_DEVICES, PK_UNMOUNTED, PK_NETWORK, PK_NETHDR, PK_NETSAVED, PK_NETEMPTY,
       PK_PHONE, PK_CLOUDHDR, PK_CLOUD, PK_CLOUDEMPTY, PK_ADDONHDR, PK_ADDON, PK_ADDONEMPTY };
typedef struct { // one sidebar row
    char label[64];
    char path[PATH_MAX]; // "" for headers
    int kind;            // PK_*
    int group, item;     // Quick Access position (-1 for devices)
    int dev;             // index in A.devs (drives), -1 otherwise
    int phone;           // index in A.devs.ph (phones and cameras; path "" until connected), -1 otherwise
    int server;          // saved server (A.servers) of a network row, -1 otherwise
    int addon;           // add-on rows (PK_ADDON*): which of their sections (addons.c)
    bool removable;      // can be ejected / disconnected
    f32 y, h;            // content-space position (see places_layout)
    Rect eject;          // eject button (hover), set while drawing
} Place;

enum { DRAG_NONE, DRAG_SCROLL, DRAG_BAND_PENDING, DRAG_BAND, DRAG_SPLIT, DRAG_ITEMS_PENDING, DRAG_ITEMS, DRAG_PREVIEW, DRAG_SIDEBAR,
       DRAG_TAB_PENDING, DRAG_TAB, DRAG_COL_RESIZE, DRAG_COL_PENDING, DRAG_COL, DRAG_PLACE_PENDING, DRAG_PLACE };

enum { U_RENAME, U_MOVE, U_COPY, U_TRASH, U_CREATE };
typedef struct {
    int kind, n;
    char **a, **b, **c; // a = original paths, b = current paths, c = trashinfo files
} UndoRec;
#define UNDO_MAX 64

typedef void (*DialogFn)(int choice, bool check, void *ud);
enum { DK_NONE, DK_COPY, DK_REPLACE, DK_TRASH }; // dialog rows (rich): copied, replacing an older copy, to the Trash
enum { DI_NONE, DI_QUESTION, DI_WARN, DI_DANGER };
typedef struct {
    bool open;
    int icon;      // DI_*: badge next to the title
    bool danger;   // the default button does something destructive (drawn red)
    char *items[5]; // optional names listed in a card (the first few of a selection)
    bool item_dir[5];
    bool rich;          // rows of what will happen: item (from) -> item_sub (to), the operation (DK_*) at the end
    u8 item_kind[5];
    char *item_sub[5];
    int nitems, more; // more: how many weren't listed
    char title[200], msg[700];
    const char *buttons[4];
    int nbuttons, def, cancel, focus;
    bool has_check, check;
    const char *check_label;
    char check_buf[96]; // (check_label may point here)
    // optional choices above the buttons, one picked (Up/Down, 1-6, click); the callback reads g_dlg_opt
    const char *opts[6];
    char opt_desc[6][120];
    int nopts, opt;
    DialogFn fn;
    void *ud;
    Rect r, rb[4], rcheck, ropt[6];
} Dialog;
static int g_dlg_opt; // the choice picked in the dialog that just closed

enum {
    ACT_OPEN, ACT_OPEN_TAB, ACT_CUT, ACT_COPY, ACT_PASTE, ACT_RENAME, ACT_TRASH, ACT_DELETE, ACT_COPY_PATH,
    ACT_TERMINAL, ACT_TERMINAL_LOCAL, ACT_SSH_TERMINAL, ACT_TERMINAL_EXTERNAL, ACT_TERM_COPY, ACT_TERM_PASTE, ACT_TERM_CLEAR,
    ACT_NEW_FOLDER, ACT_NEW_FILE, ACT_REFRESH, ACT_HIDDEN, ACT_SELECT_ALL, ACT_UNDO,
    ACT_PIN, ACT_UNPIN_PLACE, ACT_PLACE_NEW_TAB, ACT_PREVIEW,
    ACT_OPEN_SPLIT_RIGHT, ACT_OPEN_SPLIT_DOWN, ACT_TAB_NEW, ACT_TAB_DUP, ACT_TAB_CLOSE, ACT_TAB_CLOSE_OTHERS,
    ACT_TAB_CLOSE_LEFT, ACT_TAB_CLOSE_RIGHT, ACT_TAB_REOPEN, ACT_TAB_SPLIT_RIGHT, ACT_TAB_SPLIT_DOWN, ACT_TAB_WINDOW,
    ACT_COL_RESET, ACT_ALT_ROWS, ACT_SIZE_BARS, ACT_LOAD_TIMES, ACT_OPEN_WITH, ACT_PROPERTIES, ACT_BATCH_RENAME, ACT_TREE_VIEW,
    ACT_FOLDER_SIZES, ACT_GIT_STATUS, ACT_GIT_OPEN_REMOTE, ACT_GIT_COPY_REMOTE, ACT_GIT_COPY_BRANCH, ACT_GIT_ROOT, ACT_GIT_REFRESH,
    ACT_CS_HERE, ACT_CS_FOLD_ALL, ACT_CS_UNFOLD_ALL, ACT_CS_OPEN_LINE,
    ACT_DU_TOGGLE, ACT_DU_NEW_TAB, ACT_DU_PLACE, ACT_DU_OPEN, ACT_DU_OPEN_TAB, ACT_DU_SHOW, ACT_DU_TREEMAP, ACT_DU_SUNBURST,
    ACT_DU_LIST, ACT_DU_RESCAN, ACT_DU_UP,
    ACT_CMP_START, ACT_CMP_END, ACT_CMP_TAB, ACT_CMP_SYNC_A, ACT_CMP_SYNC_B, ACT_CMP_SYNC_BOTH, ACT_CMP_MIRROR_A, ACT_CMP_MIRROR_B,
    ACT_CMP_DIFF_ONLY, ACT_CMP_CONTENTS, ACT_CMP_AGAIN,
    ACT_SNAP_TOGGLE, ACT_SNAP_PLACE, ACT_SNAP_REFRESH, ACT_SNAP_BACK, ACT_SNAP_OPEN, ACT_SNAP_COMPARE, ACT_SNAP_CHANGES,
    ACT_SNAP_DELETE, ACT_SNAP_CREATE, ACT_SNAP_ALLOW, ACT_SNAP_RESTORE,
    ACT_FIRST_FREE_, // (keep below 100: shortcuts share ids with CMD_*)
    ACT_COL_BASE = 200,  // + column id: toggle that column
    ACT_VIEW_BASE = 300, // + zoom level index
    ACT_SORT_BASE = 320, // + SORT_* key
    ACT_GROUP_BASE = 340, // + GROUP_*
    ACT_FV_SUBFOLDERS = 360, ACT_FV_ALL, ACT_FV_RESET,
    ACT_TF_BASE = 370, ACT_TF_RESET = 373, ACT_TF_EXT_BASE = 380, // type filter
    ACT_TRASH_RESTORE = 420, ACT_TRASH_RESTORE_ALL, ACT_TRASH_EMPTY,
    ACT_QA_RENAME = 460, ACT_QA_RESET_NAME, ACT_QA_UP, ACT_QA_DOWN, ACT_QA_REMOVE, ACT_QA_NEW_GROUP, ACT_QA_GROUP_RENAME,
    ACT_QA_GROUP_UP, ACT_QA_GROUP_DOWN, ACT_QA_GROUP_DELETE, ACT_QA_GROUP_TOGGLE,
    ACT_QA_MOVE_BASE = 480, // + group index (up to 16)
    ACT_DEV_MOUNT = 500, ACT_DEV_UNMOUNT, ACT_DEV_EJECT, ACT_NET_DISCONNECT, ACT_DEV_OPEN_TAB,
    ACT_DEV_RENAME, ACT_DEV_RESET_NAME, ACT_DEV_SECTION_TOGGLE, ACT_NET_CONNECT_SAVED, ACT_NET_FORGET, ACT_NET_EDIT_SAVED, ACT_NET_NEW,
    ACT_CMP_PICK_BASE = 560, // + "Compare with..." folder index (up to 12)
    ACT_ADDON_BASE = 580, ACT_ADDON_LAST = 599, // add-ons' actions (addons.c)
    ACT_CLOUD_NEW = 540, ACT_CLOUD_REFRESH, ACT_CLOUD_SECTION_TOGGLE, ACT_CLOUD_CONNECT, ACT_CLOUD_CONNECT_TAB, ACT_CLOUD_DISCONNECT,
    ACT_CLOUD_LOGIN, ACT_CLOUD_RECONNECT, ACT_CLOUD_LOG, ACT_CLOUD_COPY_PATH,
    ACT_ARC_BASE = 440, // archive actions (see arc_action)
    ACT_EXTRACT_HERE = ACT_ARC_BASE, ACT_EXTRACT_SEL, ACT_EXTRACT_ALL, ACT_COMPRESS_ZIP, ACT_COMPRESS_TGZ,
};
_Static_assert(ACT_FIRST_FREE_ <= 100, "ACT_* ids must stay below the CMD_* ids");
enum {
    CMD_NEW_TAB = 100, CMD_CLOSE_TAB, CMD_SPLIT, CMD_SWITCH_PANE, CMD_BACK, CMD_FORWARD, CMD_UP, CMD_SEARCH,
    CMD_SORT_NAME, CMD_SORT_DATE, CMD_SORT_TYPE, CMD_SORT_SIZE, CMD_FOLDER_SIZES, CMD_EDIT_PATH, CMD_OVERLAY,
    CMD_REINDEX, CMD_QUIT, CMD_HOME, CMD_SEARCH_SUBFOLDERS, CMD_ZOOM_IN, CMD_ZOOM_OUT, CMD_ZOOM_RESET,
    CMD_RESTORE_SESSION, CMD_OPEN_SETTINGS, CMD_SPLIT_DOWN, CMD_COPY_OTHER, CMD_MOVE_OTHER, CMD_NEW_WINDOW,
    CMD_CLOSE_PANEL, CMD_OPTIONS, CMD_NEXT_TAB, CMD_PREV_TAB, CMD_GOTO, CMD_PALETTE, CMD_PREV_PANE,
    CMD_SAVE_LAYOUT, CMD_NEW_QA_GROUP, CMD_TREE_VIEW, CMD_TABS_POPUP, CMD_CONNECT, CMD_GIT_STATUS, CMD_SEARCH_CONTENTS, CMD_CS_TOGGLE,
    CMD_DISK_USAGE, CMD_COMPARE, CMD_SNAPSHOTS, CMD_TRANSFERS,
    CMD_ICON_THEME_BASE = 150, // + theme index
    CMD_LAYOUT_BASE = 600,     // + saved layout index
};
typedef struct {
    const char *label, *shortcut;
    int act;
    bool enabled, sep;
    bool checkable, checked;
    bool info; // key/value row (shortcut holds the value)
} MenuItem;
typedef struct {
    bool open;
    struct Pane *pane;
    Rect r;
    MenuItem items[40];
    char scbuf[40][48];
    char infobuf[16][160]; // values of info rows
    int ninfo;
    int n, hover;
    bool keep_open;              // toggles don't close the menu (Types popup)
    f32 scroll, content_h;       // menus taller than the window scroll
    void (*reopen)(struct Pane *, f32, f32);
    f32 ox, oy;
} Menu;

#define MAX_OPS 32

#define MAX_TABS 64

static struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Cursor *cur_default, *cur_ew, *cur_hand, *cur_text;
    int cursor_ew;    // resize pointer wanted (mouse motion): 0 none, 1 ew-resize, 2 ns-resize
    int cursor_shown; // what SDL shows: those, 3 hand, 4 I-beam
    bool ptr_hand, ptr_text; // this frame's drawing found something clickable / a text field under the mouse
    int W, H;
    f32 scale;
    char *font_path;
    Font font, font_small;
    Font font_brand; // app name in the sidebar header (semibold when the family has one)
    f32 mx, my;
    bool mouse_in;
    bool show_hidden, show_overlay;
    // path editing (one pane at a time)
    Pane *edit_pane;
    TextEdit edit;
    // inline rename
    Pane *ren_pane;
    Listing *ren_L;
    u32 ren_entry;
    TextEdit ren_te;
    Pane *pending_rename_pane;
    char pending_rename[NAME_MAX + 1];
    // file operations
    Op *ops[MAX_OPS];
    Rect op_close[MAX_OPS];
    Rect op_retry[MAX_OPS]; // "Retry as administrator" on a card that failed for lack of permission
    Rect op_pause[MAX_OPS], op_card[MAX_OPS]; // a card's pause / resume button; the card (click: the transfer queue)
    int nops;
    // transfer queue panel (Ctrl+J): every operation in queue order, with pause, reorder, speed and time left
    bool q_open;
    int q_sel;            // highlighted row (index in A.ops)
    Rect q_r, q_verify;   // the panel; its "Verify after copy" switch
    Rect q_row[MAX_OPS], q_pause[MAX_OPS], q_up[MAX_OPS], q_down[MAX_OPS], q_close[MAX_OPS];
    bool verify_copies;   // setting: checksum every copied file against its original
    int transfers_at_once; // setting: big transfers running together (0 = no limit, all at once)
    int conflict_last;    // the conflict answer picked last time (CF_*), offered first next time
    UndoRec undo[UNDO_MAX];
    int nundo;
    Dialog dlg;
    Menu menu;
    char toast[300];
    u64 toast_t;
    // drag & drop
    char drop_dir[PATH_MAX];
    Pane *drop_pane;
    i64 drop_row;
    int drop_place;
    bool drop_copy;
    Panel *drop_split_panel; // dragged folders onto a panel edge: open them in a new split on side drop_split
    int drop_split;
    Panel *drop_newtab;      // ... onto the empty part of a tab bar: open them as new tabs there
    char **ext_drop;
    int next_drop;
    f32 ext_x, ext_y;
    Rect ren_rect;
    i64 press_collapse; // row to collapse the selection to on mouse-up (no drag happened)
    // command palette
    bool pal_open, pal_pending;
    TextEdit pal_te;
    char pal_last[PATH_MAX];
    u32 pal_gen;
    int pal_n, pal_sel, pal_top;
    Rect pal_r, pal_list;
    int def_view, def_icon, def_group;
    bool view_per_folder; // off: a tab keeps its view (layout, icon size) while browsing; new tabs start in def_view
    char icon_theme_name[64]; // from settings, applied once the renderer exists (custom themes are scanned first)
    Rect r_viewbtn[3], r_gear, r_feat, r_xfer;
    // columns (shared by all panes)
    int cols[COL_COUNT], ncols;
    f32 colw[COL_COUNT];
    bool alt_rows, size_bars;
    bool load_times;      // "listed in ... ms" in the status bar
    int drag_col;         // column being resized / moved
    f32 drag_col_w0;
    int drag_col2;        // resize: neighbour that trades width with drag_col (-1: Name absorbs the change)
    f32 drag_col2_w0;
    int drag_edge_col;    // resize: column left of the dragged edge (for the highlight)
    int col_insert;       // reorder target (index in A.cols), -1 none
    // settings / M5
    f32 dpi_scale, zoom;
    int def_sort_key;
    bool def_sort_desc;
    bool restore_session, settings_dirty;
    u64 settings_saved;
    char renderer[16];
    int win_w, win_h;
    bool maximized;
    char **pins;
    int npins;
    int menu_place;
    // session restore (from settings)
    char **sess_tabs; // "panel\tis_current\tpath"
    int nsess_tabs, sess_cur;
    char sess_layout[1024];
    // preview panel
    bool preview_on;
    f32 preview_w; // logical px
    f32 side_w;    // sidebar width, logical px (drag its edge)
    Rect r_sdiv;   // the sidebar's draggable edge
    Rect r_preview, r_pdiv;
    char pv_want[PATH_MAX], pv_path[PATH_MAX];
    i64 pv_mtime;
    int pv_line; // contents search: the line the preview shows
    u64 pv_due;
    u32 pv_gen;
    PreviewResult *pv;
    SDL_Texture *pv_tex;
    Font font_mono;
    char *mono_path;
    // recursive search editing
    Pane *search_pane;
    TextEdit search_te;
    u64 search_due;
    bool search_recursive; // include subfolders (default on)
    bool search_contents;  // the search bar searches inside files
    bool cs_dirty;         // options changed: search again even for the same query
    char cs_error[64];     // shown in the search bar (bad regex)
    bool cs_case, cs_word, cs_regex; // contents search options: match case (else smart case), whole word, regex
    bool folder_sizes;
    bool git_status; // git badges in the file list + branch in the status bar
    int du_style;    // disk usage view: 0 treemap, 1 sunburst
    bool du_list;    // ... with the list of the biggest items beside the map
    // type-to-jump
    char jump[128];
    u64 jump_t;
    // dragging
    int drag;
    Pane *drag_pane;
    f32 drag_off;
    f32 band_x0, band_mx0, band_my0;
    f64 band_y0;      // content-space y of band start
    i64 band_r0, band_r1; // currently applied row range (r0 > r1 = empty)
    u8 *band_base;    // selection before the band (Ctrl), or NULL
    // tabs: one global list (order = order within each panel's bar); A.cur = the active panel's tab
    Tab *tabs[MAX_TABS];
    int ntabs, cur;
    // panels
    Panel *panels[MAX_PANELS];
    int npanels;
    Node *root;
    Panel *ap, *prev_panel; // active / previously active panel
    Node *drag_node;
    Tab *drag_tab, *menu_tab, *drop_tab;
    int tdrop_kind, tdrop_zone;
    Panel *tdrop_panel;
    Tab *tdrop_before;
    char *closed[32]; // recently closed tab paths (Ctrl+Shift+T)
    int nclosed;
    bool dim_inactive;
    int title_bar; // TITLEBAR_*: who draws the window's title bar (setting)
    char ig_prev_fm[256]; // integration.c: the folder handler before we became the default (put back when turned off)
    bool ig_fm1;          // answer "Show in folder" (org.freedesktop.FileManager1)
    bool ig_asked;        // the first-run "use for folders?" question was shown
    bool ig_check_updates; // AppImage: look for a new release once a day
    i64 ig_update_checked; // when it last looked (unix time)
    bool csd;      // we do right now: buttons top right, the tab strips drag the window
    SDL_Cursor *cur_ns;
    // global layout
    Rect r_sidebar, r_content, r_status;
    Rect r_brand; // logo + app name above the sidebar
    Rect r_wctl, r_wstrip; // own title bar: minimise / maximise / close (top right), the strip above the preview
    f32 side_row_h, side_top;
    // misc
    bool redraw;
    bool win_hidden; // minimised / hidden / occluded: don't render (presenting could block until it's visible again)
    u64 last_frame;
    f32 frame_ms;
    Place places[128];
    int nplaces, nplaces_user; // rows before nplaces_user are Quick Access
    QaGroup *qa;
    int nqa;
    f32 side_scroll, side_scroll_target, side_content_h; // the wheel moves the target, frames ease toward it
    int qa_edit_g, qa_edit_i; // inline rename: group, item (-1 = group name); qa_edit_g < 0 idle
                              // (QA_EDIT_DEV: a device row, identified by dev_edit_key)
    char dev_edit_key[PATH_MAX];
    struct { char *key, *name; } dev_names[64]; // custom device names (key: see place_dev_key)
    int ndev_names;
    bool devs_collapsed, nets_collapsed;
    TextEdit qa_te;
    int qa_drag_row, qa_ins_g, qa_ins_i; // sidebar drag: row being moved, insertion point
    int drop_qa;              // files dragged onto a group header: pin into that group
    DevList devs;             // block devices (lsblk), refreshed on hotplug / mount changes
    NetMount nets[16];
    int nnets;
    struct { char name[64]; char uri[1024]; char icon[160]; } servers[32]; // saved servers (Connect to server; never passwords)
    StatsJob *trash_job; // measuring the trash (sidebar shows its size)
    Term *term_mouse;    // the terminal a mouse button went down in (gets the drag and the release)
    int term_mouse_btn;
    u64 trash_size;
    bool trash_known, trash_again;
    int nservers;
    bool devs_enabled;        // off in self-tests unless faked
    bool dev_scanning, dev_rescan;
    bool devs_known;          // the first drive list has landed
    int dev_busy;             // mount/unmount jobs running
    bool dev_open_tab;        // the pending mount opens in a new tab
    char dev_connecting[32];  // our connector is connecting to this USB device (sysfs name)
    struct { char serial[96], rel[PATH_MAX]; u64 t; bool active, reconnecting; } dev_back; // a phone that dropped off: where we were

    const char *screenshot;
    char shot_once[PATH_MAX]; // save the next frame here (tests)
    int screenshot_frames;
    bool quit;
    bool selftest;                 // never launch external programs while testing
    // appearance (M9)
    char theme_name[64];
    char ui_font[96];  // family ("" = automatic)
    int font_size;     // UI font px at 100% zoom
    int row_spacing;   // 0 compact, 1 normal, 2 comfortable
    bool smooth_scroll;
    bool def_tree;     // tree view for new tabs
    bool single_instance; // later launches open in this window
    bool close_to_tray;   // closing the window hides it (tray icon: open, quit)
    bool ipc_test;        // self-test: listen on a private socket
    int ipc_count;        // messages handled (tests)
    int pick_done;        // picker (tests): 1 chose, -1 cancelled
    // FE_RECORD=dir: save the frames shown shortly after each click + a state log (diagnostics)
    const char *rec_dir;
    u64 rec_until;
    int rec_n;
    FILE *rec_log;
    // mouse
    bool single_click;   // open items with one click
    bool dblclick_up;    // double-click empty space = parent folder
    i64 click_open;      // row to open on mouse-up (single-click mode), -1 none
    char last_opened[PATH_MAX];    // what open_path would have opened (tests)
} A;

static inline Tab *cur_tab(void) { return A.tabs[A.cur]; }
static inline Pane *active_pane(void) { return &cur_tab()->pane[0]; }

static inline bool pane_is_archive(const Pane *p) { return strstr(p->path, "::") != NULL; }
static inline bool pane_tree_on(const Pane *p) { return p->tree && p->view == VIEW_DETAILS && !pane_is_archive(p) && !p->search_q[0]; }
static inline int pane_group_eff(const Pane *p) { return p->view == VIEW_DETAILS && !pane_tree_on(p) ? p->group : GROUP_NONE; }

// Expanded folders as the listing's tree string ("\na\na/b\n"); NULL when the tree view is off.
static char *pane_tree_string(const Pane *p) {
    if (!pane_tree_on(p)) return NULL;
    size_t len = 2;
    for (int i = 0; i < p->nexpanded; i++) len += strlen(p->expanded[i]) + 1;
    char *t = xmalloc(len + 1), *o = t;
    *o++ = '\n';
    for (int i = 0; i < p->nexpanded; i++) o += sprintf(o, "%s\n", p->expanded[i]);
    *o = 0;
    return t;
}

// Display name of a path ("photos.zip::" shows as "photos.zip").
static bool is_trash_dir(const char *path);

static const char *disp_name(const char *path, char *buf, size_t cap) {
    if (is_trash_dir(path)) return "Trash";
    const char *b = path[1] ? path_basename(path) : "/";
    size_t l = strlen(b);
    if (l > 2 && !strcmp(b + l - 2, "::")) { snprintf(buf, cap, "%.*s", (int)(l - 2), b); return buf; }
    return b;
}

static int tab_index(const Tab *t) {
    for (int i = 0; i < A.ntabs; i++)
        if (A.tabs[i] == t) return i;
    return -1;
}

static int panel_ntabs(const Panel *pn) {
    int n = 0;
    for (int i = 0; i < A.ntabs; i++) n += A.tabs[i]->panel == pn;
    return n;
}

// k-th tab of a panel (k < 0: last)
static Tab *panel_tab(const Panel *pn, int k) {
    Tab *last = NULL;
    int idx = 0;
    for (int i = 0; i < A.ntabs; i++) {
        if (A.tabs[i]->panel != pn) continue;
        if (idx++ == k) return A.tabs[i];
        last = A.tabs[i];
    }
    return k < 0 ? last : NULL;
}

static bool panel_alive(const Panel *pn) {
    for (int i = 0; i < A.npanels; i++)
        if (A.panels[i] == pn) return true;
    return false;
}

static int panel_index(const Panel *pn) {
    for (int i = 0; i < A.npanels; i++)
        if (A.panels[i] == pn) return i;
    return -1;
}

static bool pane_exists(const Pane *p) {
    for (int i = 0; i < A.ntabs; i++)
        for (int j = 0; j < A.tabs[i]->npanes; j++)
            if (&A.tabs[i]->pane[j] == p) return true;
    return false;
}

static Tab *tab_of(const Pane *p) {
    for (int i = 0; i < A.ntabs; i++)
        for (int j = 0; j < A.tabs[i]->npanes; j++)
            if (&A.tabs[i]->pane[j] == p) return A.tabs[i];
    return NULL;
}

// --- layout ----------------------------------------------------------------

static i64 item_rows(const Pane *p) {
    if (p->nhdr) return p->nrows;
    int pr = MAX(1, p->ly.per_row);
    return ((i64)p->count + pr - 1) / pr;
}

// Display row of a visible item (groups add header rows).
static i64 pos_row(const Pane *p, i64 pos) { return p->nhdr ? (i64)p->row_of[pos] : pos / MAX(1, p->ly.per_row); }

// First visible item whose row is >= row (grouped panes).
static i64 grp_first_pos(const Pane *p, i64 row) {
    i64 lo = 0, hi = p->count;
    while (lo < hi) {
        i64 mid = (lo + hi) / 2;
        if ((i64)p->row_of[mid] < row) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static int grp_first_hdr(const Pane *p, i64 row) {
    int k = 0;
    while (k < p->nhdr && (i64)p->hdr[k].row < row) k++;
    return k;
}

static bool pane_is_trash(const Pane *p);
static void git_refresh_all(void);
static void content_rows(Pane *p);
static void search_focus_contents(Pane *p);
static void search_set_contents(Pane *p, bool on);
static void content_fold(Pane *p, i64 pos, int want);
static void content_fold_all(Pane *p, bool fold);
static void content_open_line(Pane *p, i64 pos);
static u32 content_file_of(const Listing *L, u32 ei);
static void git_action(Pane *p, int act);
static f32 git_chip_width(Pane *p);
static bool pane_du_on(const Pane *p);
static void du_view_close(Pane *p);
static char **du_selection_paths(Pane *p, int *n);
static void du_action(Pane *p, int act);
static void du_rescan(Pane *p);
static void du_after_op(Op *op);
static void search_exit(Pane *p);
static int cmp_side(const Pane *p);
static bool pane_snap_on(const Pane *p);
static void snapview_close(Pane *p);
static bool snap_in(const char *path);
static void snap_action(Pane *p, int act);
static void cmp_filter(Pane *p);
static void cmp_after_op(Op *op);
static void cmp_action(Pane *p, int act);
static void cmp_run(void);
static void menu_open_cmp_pick(Pane *p, f32 x, f32 y);
static f64 content_h(const Pane *p) { return (f64)item_rows(p) * p->ly.cell_h + p->ly.pad * 2; }

static f64 max_scroll(const Pane *p) { return MAX(0.0, content_h(p) - p->ly.list.h); }

// Item rectangle on screen (y includes scrolling).
static Rect item_rect(const Pane *p, i64 i) {
    const PaneLayout *ly = &p->ly;
    int pr = MAX(1, ly->per_row);
    i64 r = pos_row(p, i), c = p->nhdr ? 0 : i % pr;
    return (Rect){ly->list.x + ly->pad + (f32)c * ly->cell_w, (f32)((f64)ly->list.y + ly->pad + (f64)r * ly->cell_h - p->scroll),
                  ly->cell_w, ly->cell_h};
}

static void layout_pane(Pane *p, Rect area) {
    PaneLayout *ly = &p->ly;
    ly->area = area;
    f32 tb = roundf(S(46));
    ly->toolbar = (Rect){area.x, area.y, area.w, tb};
    f32 bs = roundf(S(30)), by = area.y + roundf((tb - bs) * 0.5f), gap = roundf(S(2));
    ly->back = (Rect){area.x + roundf(S(8)), by, bs, bs};
    ly->fwd = (Rect){ly->back.x + bs + gap, by, bs, bs};
    ly->up = (Rect){ly->fwd.x + bs + gap, by, bs, bs};
    f32 px = ly->up.x + bs + roundf(S(8));
    ly->filterbtn = (Rect){area.x + area.w - bs - roundf(S(8)), by, bs, bs};
    ly->pathbar = (Rect){px, by, ly->filterbtn.x - px - roundf(S(4)), bs};
    f32 hh = p->view == VIEW_DETAILS && !pane_du_on(p) && !pane_snap_on(p) ? roundf(S(32)) : 0; // list/grid sort from the menu, no header
    ly->header = (Rect){area.x, area.y + tb, area.w, hh};
    ly->list = (Rect){area.x, area.y + tb + hh, area.w, area.h - tb - hh};
    ly->has_trashbar = pane_is_trash(p);
    if (ly->has_trashbar) {
        f32 tbh = roundf(S(44));
        ly->list.h -= tbh;
        ly->trashbar = (Rect){area.x, ly->list.y + ly->list.h, area.w, tbh};
        f32 bw = roundf(S(120)), bh2 = roundf(S(30)), byy = ly->trashbar.y + roundf((tbh - bh2) * 0.5f);
        ly->rempty = (Rect){area.x + area.w - bw - roundf(S(10)), byy, bw, bh2};
        ly->rrestore = (Rect){ly->rempty.x - bw - roundf(S(8)), byy, bw, bh2};
    }
    ly->has_snapbar = !p->snapv && snap_in(p->path);
    if (ly->has_snapbar) {
        f32 sbh2 = roundf(S(40));
        ly->list.h -= sbh2;
        ly->snapbar = (Rect){area.x, ly->list.y + ly->list.h, area.w, sbh2};
    }
    ly->has_cmpbar = cmp_side(p) >= 0;
    if (ly->has_cmpbar) {
        f32 cbh = roundf(S(40));
        ly->list.h -= cbh;
        ly->cmpbar = (Rect){area.x, ly->list.y + ly->list.h, area.w, cbh};
    }
    ly->has_sbar = A.search_pane == p || p->search_q[0];
    if (ly->has_sbar) {
        f32 sbh = roundf(S(44));
        ly->list.h -= sbh;
        ly->sbar = (Rect){area.x, ly->list.y + ly->list.h, area.w, sbh};
    }
    static const f32 row_px[3] = {22, 26, 32};
    ly->row_h = roundf(S(row_px[CLAMP(A.row_spacing, 0, 2)]));
    {
        f32 avail = ly->list.w - roundf(S(12));
        if (p->view == VIEW_GRID) {
            f32 isz = roundf(S(p->icon_px));
            f32 cw = MAX(isz + roundf(S(28)), roundf(S(96)));
            ly->pad = roundf(S(8));
            ly->per_row = MAX(1, (int)((avail - ly->pad * 2) / cw));
            ly->cell_w = floorf((avail - ly->pad * 2) / ly->per_row);
            ly->cell_h = isz + roundf(S(16)) + roundf(A.font.line_h * 2.3f) + roundf(S(6));
        } else if (p->view == VIEW_LIST) {
            ly->pad = roundf(S(6));
            ly->per_row = MAX(1, (int)((avail - ly->pad * 2) / roundf(S(250))));
            ly->cell_w = floorf((avail - ly->pad * 2) / ly->per_row);
            ly->cell_h = ly->row_h + roundf(S(2));
        } else {
            ly->pad = 0;
            ly->per_row = 1;
            ly->cell_w = avail;
            ly->cell_h = ly->row_h;
        }
    }

    f32 sbw = roundf(S(12));
    ly->track = (Rect){ly->list.x + ly->list.w - sbw, ly->list.y, sbw, ly->list.h};
    f64 content = content_h(p);
    ly->has_scroll = content > ly->list.h;
    if (ly->has_scroll) {
        f32 th = MAX(roundf(S(32)), (f32)(ly->list.h * ly->list.h / content));
        f64 maxs = content - ly->list.h;
        f32 ty = ly->list.y + (f32)((ly->list.h - th) * (p->scroll / maxs));
        ly->thumb = (Rect){ly->track.x, ty, sbw, th};
    }

    // columns: fixed widths, Name takes the rest; drop columns from the right when it gets too narrow
    f32 end = ly->list.x + ly->list.w - sbw - roundf(S(8));
    ly->col_end = end;
    bool vis[COL_COUNT] = {0};
    f32 fixed = 0;
    for (int i = 0; i < A.ncols; i++) {
        vis[A.cols[i]] = true;
        if (A.cols[i] != COL_NAME) fixed += roundf(S(A.colw[A.cols[i]]));
    }
    f32 min_name = roundf(S(150));
    for (int pass = 0; pass < 2; pass++) // drop other columns right-to-left first; Size only as a last resort
        for (int i = A.ncols - 1; i >= 0 && end - ly->list.x - fixed < min_name; i--) {
            int c = A.cols[i];
            if (c == COL_NAME || !vis[c] || (pass == 0 && c == COL_SIZE)) continue;
            vis[c] = false;
            fixed -= roundf(S(A.colw[c]));
        }
    f32 name_w = MAX(end - ly->list.x - fixed, roundf(S(60)));
    f32 x = ly->list.x;
    ly->ncols = 0;
    for (int i = 0; i < A.ncols; i++) {
        int c = A.cols[i];
        if (!vis[c]) continue;
        f32 w = c == COL_NAME ? name_w : roundf(S(A.colw[c]));
        ly->cid[ly->ncols] = c;
        ly->cx0[ly->ncols] = x;
        ly->cx1[ly->ncols] = x + w;
        ly->ncols++;
        x += w;
        if (c == COL_NAME) {
            ly->col_name = ly->cx0[ly->ncols - 1] + roundf(S(14));
            ly->name_end = ly->cx1[ly->ncols - 1];
        }
    }
}

// Visible span of a column; false if hidden.
static bool col_span(const PaneLayout *ly, int id, f32 *x0, f32 *x1) {
    for (int i = 0; i < ly->ncols; i++)
        if (ly->cid[i] == id) {
            if (x0) *x0 = ly->cx0[i];
            if (x1) *x1 = ly->cx1[i];
            return true;
        }
    return false;
}

// Header geometry: index of the visible column whose right edge is near x (resize handle), or -1.
static int header_edge_at(const PaneLayout *ly, f32 x) {
    for (int i = 0; i < ly->ncols; i++)
        if (fabsf(x - ly->cx1[i]) <= S(4) && i < ly->ncols - 1) return i;
    return -1;
}

static int header_col_at(const PaneLayout *ly, f32 x) {
    for (int i = 0; i < ly->ncols; i++)
        if (x >= ly->cx0[i] && x < ly->cx1[i]) return i;
    return -1;
}

static void cols_default(void) {
    static const int def[] = {COL_NAME, COL_DATE, COL_TYPE, COL_SIZE};
    A.ncols = (int)ARRAY_COUNT(def);
    memcpy(A.cols, def, sizeof def);
    for (int i = 0; i < COL_COUNT; i++) A.colw[i] = g_coldef[i].defw;
}

static bool col_visible(int id) {
    for (int i = 0; i < A.ncols; i++)
        if (A.cols[i] == id) return true;
    return false;
}

static void col_toggle(int id) {
    if (id == COL_NAME || id < 0 || id >= COL_COUNT) return;
    for (int i = 0; i < A.ncols; i++)
        if (A.cols[i] == id) {
            memmove(&A.cols[i], &A.cols[i + 1], (size_t)(A.ncols - i - 1) * sizeof(int));
            A.ncols--;
            A.settings_dirty = true;
            return;
        }
    A.cols[A.ncols++] = id;
    A.settings_dirty = true;
}

static void layout_panel(Panel *pn, Rect r) {
    pn->area = r;
    f32 tb = roundf(S(38));
    pn->bar = (Rect){r.x, r.y, r.w, tb};
    f32 bw = roundf(S(26)), by = r.y + roundf(S(6));
    f32 right = r.x + r.w; // a panel at the top right ends its tabs before the window buttons
    if (A.r_wctl.w > 0 && r.y < 1 && right > A.r_wctl.x) right = MAX(r.x + S(140), A.r_wctl.x);
    pn->splitbtn = (Rect){right - bw - roundf(S(6)), by, bw, bw};
    int n = panel_ntabs(pn);
    f32 x0 = r.x + roundf(S(6));
    f32 avail = pn->splitbtn.x - roundf(S(6)) - (bw + roundf(S(4))) - x0;
    f32 tw = n ? MAX(roundf(S(48)), MIN(roundf(S(200)), roundf(avail / n))) : 0;
    Rect body = {r.x, r.y + tb, r.w, r.h - tb};
    int k = 0;
    for (int i = 0; i < A.ntabs; i++) {
        Tab *t = A.tabs[i];
        if (t->panel != pn) continue;
        t->r = (Rect){x0 + k * tw, r.y + roundf(S(6)), tw, tb - roundf(S(6))};
        k++;
        layout_pane(&t->pane[0], body);
    }
    // + : a round button after the last tab, centred on the tab labels (clear of the active tab's flare)
    f32 ps = roundf(S(26)), pcy = r.y + roundf(S(6)) + (tb - roundf(S(6))) * 0.5f;
    pn->plus = (Rect){MIN(x0 + k * tw + roundf(S(10)), pn->splitbtn.x - ps - roundf(S(4))), roundf(pcy - ps * 0.5f), ps, ps};
}

static void layout_node(Node *n, Rect r) {
    n->r = r;
    if (n->kind == NODE_PANEL) {
        layout_panel(n->panel, r);
        n->div = (Rect){0, 0, 0, 0};
        return;
    }
    f32 g = roundf(S(3));
    if (n->kind == NODE_HSPLIT) {
        f32 lw = roundf(r.w * n->ratio);
        layout_node(n->a, (Rect){r.x, r.y, lw, r.h});
        layout_node(n->b, (Rect){r.x + lw + 1, r.y, r.w - lw - 1, r.h});
        n->div = (Rect){r.x + lw - g, r.y, 1 + 2 * g, r.h};
    } else {
        f32 th = roundf(r.h * n->ratio);
        layout_node(n->a, (Rect){r.x, r.y, r.w, th});
        layout_node(n->b, (Rect){r.x, r.y + th + 1, r.w, r.h - th - 1});
        n->div = (Rect){r.x, r.y + th - g, r.w, 1 + 2 * g};
    }
}

static Node *node_div_at(Node *n, f32 x, f32 y) {
    if (!n || n->kind == NODE_PANEL) return NULL;
    if (in_rect(n->div, x, y)) return n;
    Node *r = node_div_at(n->a, x, y);
    return r ? r : node_div_at(n->b, x, y);
}

static void trash_dirs(char *files, char *info, size_t cap);
static void places_layout(void);

// picker.c (the app as the system file chooser)
static bool picker_active(void);
static f32 picker_bar_h(void);
static f32 picker_tab_h(void);
static void draw_picker_bar(Rect s);
static bool picker_mouse(f32 x, f32 y);
static bool picker_key(SDL_Keycode k, SDL_Keymod mod);
static bool picker_text(const char *text);
static bool picker_open_file(struct Pane *p, const char *full);
static void picker_cancel(void);
static const char *picker_title(void);

static void layout(void) {
    f32 W = (f32)A.W, H = (f32)A.H;
    // the sidebar keeps >= 520 px for the panels when the window is narrow
    f32 tb = 0, st = picker_active() ? picker_bar_h() : roundf(S(28)), sb = roundf(MIN(S(CLAMP(A.side_w, 150.f, 480.f)), MAX(S(150), W - S(520))));
    f32 bh = roundf(S(44)); // logo centred on the tab strip
    A.r_brand = (Rect){0, 0, sb, bh};
    A.r_sidebar = (Rect){0, bh, sb, H - st - bh};
    if (picker_active()) A.r_sidebar.h -= picker_tab_h(); // the picker bar's title tab sits under the sidebar
    A.r_sdiv = (Rect){sb - roundf(S(5)), 0, roundf(S(5)) + 1, H - st}; // on the sidebar side: clicks at the list's edge stay the list's
    A.r_content = (Rect){sb, tb, W - sb, H - tb - st};
    if (A.preview_on) {
        f32 pw = roundf(CLAMP(S(A.preview_w), S(220), MAX(S(220), (W - sb) - S(380)))); // keep >= 380 px for files
        A.r_preview = (Rect){W - pw, tb, pw, H - tb - st};
        A.r_pdiv = (Rect){A.r_preview.x - roundf(S(3)), tb, roundf(S(6)), H - tb - st};
        A.r_content.w -= pw;
    } else {
        A.r_pdiv = (Rect){0, 0, 0, 0};
    }
    A.r_status = (Rect){0, H - st, W, st};
    // own title bar: the buttons sit at the right end of the tab strips; with the preview open they get a strip of
    // tab-bar colour above it (the preview moves down to the panels' toolbars)
    A.r_wctl = A.r_wstrip = (Rect){0, 0, 0, 0};
    if (A.csd) {
        f32 bw = roundf(S(46)), bh2 = roundf(S(38));
        A.r_wctl = (Rect){W - 3 * bw, 0, 3 * bw, bh2};
        if (A.preview_on) {
            A.r_wstrip = (Rect){A.r_preview.x, 0, A.r_preview.w, bh2};
            A.r_preview.y += bh2;
            A.r_preview.h -= bh2;
        }
    }
    A.side_row_h = roundf(S(30));
    A.side_top = roundf(S(8));
    places_layout();

    if (A.root) layout_node(A.root, A.r_content);
}

// --- own title bar ---------------------------------------------------------
// The same look on every desktop: the window is borderless and draws its own title bar - the buttons at the right
// end of the top tab strip, the empty parts of the strips and the logo move the window, the edges resize it (SDL
// hit test). Tiling compositors get it too: there the buttons are the only visible way to close the window, and
// guessing which desktops tile would leave some without one.

static void window_close_requested(void);

static bool titlebar_wanted(void) {
    int m = A.title_bar;
    const char *e = getenv("FE_TITLE_BAR"); // (tests, screenshots)
    if (e && !strcmp(e, "app")) m = TITLEBAR_APP;
    else if (e && !strcmp(e, "system")) m = TITLEBAR_SYSTEM;
    if (m != TITLEBAR_AUTO) return m == TITLEBAR_APP;
    const char *drv = SDL_GetCurrentVideoDriver();
    return !drv || strcmp(drv, "offscreen"); // (not in self-tests and screenshots: FE_TITLE_BAR=app shows it there)
}

// 0 minimise, 1 maximise / restore, 2 close
static Rect wctl_button(int i) {
    f32 bw = roundf(A.r_wctl.w / 3);
    return (Rect){A.r_wctl.x + bw * (f32)i, A.r_wctl.y, i == 2 ? A.r_wctl.w - 2 * bw : bw, A.r_wctl.h};
}

static bool window_maximized(void) {
    return A.win && (SDL_GetWindowFlags(A.win) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN));
}

// The parts of the title bar that move the window: the logo, the strip above the preview, and the tab strips
// along the top edge outside their tabs and buttons.
static bool titlebar_drag_at(f32 x, f32 y) {
    if (!A.csd || in_rect(A.r_wctl, x, y)) return false;
    if (in_rect(A.r_brand, x, y) || in_rect(A.r_wstrip, x, y)) return true;
    for (int i = 0; i < A.npanels; i++) {
        Panel *pn = A.panels[i];
        if (pn->bar.y > 0.5f || !in_rect(pn->bar, x, y)) continue;
        if (in_rect(pn->plus, x, y) || in_rect(pn->splitbtn, x, y)) return false;
        for (int k = 0; k < A.ntabs; k++)
            if (A.tabs[k]->panel == pn && in_rect(A.tabs[k]->r, x, y)) return false;
        return true;
    }
    return false;
}

static SDL_HitTestResult window_hit_test(SDL_Window *w, const SDL_Point *pt, void *ud) {
    (void)ud;
    if (!A.csd || A.drag != DRAG_NONE) return SDL_HITTEST_NORMAL;
    f32 d = SDL_GetWindowPixelDensity(w);
    f32 x = (f32)pt->x * d, y = (f32)pt->y * d, W = (f32)A.W, H = (f32)A.H;
    if (!window_maximized()) {
        f32 e = MAX(3.f, roundf(S(5))), c = roundf(S(14)); // edge, and the corners' longer grab along it
        bool l = x < e, r = x >= W - e, t = y < e, b = y >= H - e;
        if ((t && x < c) || (l && y < c)) return SDL_HITTEST_RESIZE_TOPLEFT;
        if ((t && x >= W - c) || (r && y < c)) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if ((b && x < c) || (l && y >= H - c)) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if ((b && x >= W - c) || (r && y >= H - c)) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (t) return SDL_HITTEST_RESIZE_TOP;
        if (b) return SDL_HITTEST_RESIZE_BOTTOM;
        if (l) return SDL_HITTEST_RESIZE_LEFT;
        if (r) return SDL_HITTEST_RESIZE_RIGHT;
    }
    return titlebar_drag_at(x, y) ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
}

// Switch between our title bar and the system's (startup, and when the setting changes).
static void titlebar_apply(void) {
    bool csd = titlebar_wanted();
    if (A.win && !A.selftest) {
        SDL_SetWindowBordered(A.win, !csd);
        SDL_SetWindowHitTest(A.win, csd ? window_hit_test : NULL, NULL);
    }
    A.csd = csd;
    layout();
    A.redraw = true;
}

static void window_toggle_maximized(void) {
    if (A.selftest) return;
    if (window_maximized()) SDL_RestoreWindow(A.win);
    else SDL_MaximizeWindow(A.win);
}

// A press on the title bar: the window buttons; right-click on the empty bar, the window menu. True if used.
static bool titlebar_mouse_down(SDL_MouseButtonEvent *b, f32 x, f32 y) {
    if (!A.csd) return false;
    if (in_rect(A.r_wctl, x, y)) {
        if (b->button != SDL_BUTTON_LEFT) return true;
        for (int i = 0; i < 3; i++) {
            if (!in_rect(wctl_button(i), x, y)) continue;
            if (A.selftest) snprintf(A.last_opened, sizeof A.last_opened, "window:%s", i == 0 ? "minimise" : i == 1 ? "maximise" : "close");
            else if (i == 0) SDL_MinimizeWindow(A.win);
            else if (i == 1) window_toggle_maximized();
            else window_close_requested();
        }
        return true;
    }
    if (!titlebar_drag_at(x, y)) return false;
    // (left presses here normally start a move in the compositor and never reach us)
    if (b->button == SDL_BUTTON_LEFT && b->clicks >= 2) { window_toggle_maximized(); return true; }
    if (b->button == SDL_BUTTON_RIGHT) {
        f32 d = SDL_GetWindowPixelDensity(A.win);
        if (!A.selftest) SDL_ShowWindowSystemMenu(A.win, (int)(x / d), (int)(y / d));
        return true;
    }
    return b->button == SDL_BUTTON_LEFT;
}

// Breadcrumbs: root drive icon + one crumb per path component, elided from the left.
static void layout_crumbs(Pane *p) {
    PaneLayout *ly = &p->ly;
    const char *path = p->path;
    Font *f = &A.font;
    f32 pad = roundf(S(8)), sep = roundf(S(16)), x0 = ly->pathbar.x + roundf(S(4));
    f32 avail = ly->pathbar.w - roundf(S(8));
    f32 gcw = git_chip_width(p); // the branch chip takes the right end
    if (gcw > 0 && gcw < ly->pathbar.w * 0.5f) avail -= gcw + roundf(S(6));
    enum { MAXC = 128 };
    int starts[MAXC], ends[MAXC], n = 0;
    starts[n] = 0; ends[n] = 1; n++; // root
    for (const char *s = path + 1; *s;) { // very deep paths: keep the last components (the head gets elided anyway)
        const char *e = strchr(s, '/');
        if (!e) e = s + strlen(s);
        if (n == MAXC) {
            memmove(&starts[1], &starts[2], (MAXC - 2) * sizeof(int));
            memmove(&ends[1], &ends[2], (MAXC - 2) * sizeof(int));
            n--;
        }
        starts[n] = (int)(s - path);
        ends[n] = (int)(e - path);
        n++;
        s = *e ? e + 1 : e;
    }
    // Inside the trash: one "Trash" crumb instead of home > me > .local > share > Trash > files
    ly->crumbs_trash = false;
    char tf[PATH_MAX], ti[PATH_MAX];
    trash_dirs(tf, ti, sizeof tf);
    size_t tl = strlen(tf);
    if (!strncmp(path, tf, tl) && (path[tl] == 0 || path[tl] == '/')) {
        int k = 1;
        while (k < n && ends[k] != (int)tl) k++;
        if (k < n) { // components 1..k are the trash folder itself
            memmove(&starts[1], &starts[k + 1], (size_t)(n - k - 1) * sizeof(int));
            memmove(&ends[1], &ends[k + 1], (size_t)(n - k - 1) * sizeof(int));
            n -= k;
            ly->crumbs_trash = true;
        }
    }
    f32 widths[MAXC];
    widths[0] = ly->crumbs_trash ? roundf(S(16) + S(8) + text_width(f, "Trash", 5)) + pad * 2 : roundf(S(30));
    for (int i = 1; i < n; i++) widths[i] = roundf(text_width(f, path + starts[i], ends[i] - starts[i])) + pad * 2;
    int first = 1;
    f32 ell = roundf(text_width(f, ELLIPSIS, 3)) + pad * 2;
    for (;;) {
        f32 total = widths[0];
        for (int i = first; i < n; i++) total += sep + widths[i];
        if (first > 1) total += sep + ell;
        if (total <= avail || first >= n - 1) break;
        first++;
    }
    ly->crumbs_elided = first > 1;
    ly->ncrumbs = 0;
    f32 x = x0;
    ly->crumbs[ly->ncrumbs++] = (Crumb){{x, ly->pathbar.y + 3, widths[0], ly->pathbar.h - 6}, ly->crumbs_trash ? (int)tl : 1};
    x += widths[0];
    if (first > 1) x += sep + ell;
    for (int i = first; i < n; i++) {
        x += sep;
        ly->crumbs[ly->ncrumbs++] = (Crumb){{x, ly->pathbar.y + 3, widths[i], ly->pathbar.h - 6}, ends[i]};
        x += widths[i];
    }
}

// --- places ----------------------------------------------------------------

static void devices_refresh(void);
static void init_places(void);
static bool uri_to_path(const char *uri, char *out, size_t cap);
static void pane_navigate(Pane *p, const char *path_in, bool push);
static Tab *tab_new(const char *path, int at, bool activate);
static void toast(const char *fmt, ...);

// Network filesystems (NFS, SMB, sshfs, GVfs...): a dead server makes stat() hang, so the main thread
// never touches paths under these mount points (the list is read from /proc/self/mounts, no syscalls on them).
static char **g_remote;
static int g_nremote;

// /proc/self/mounts writes spaces, tabs, newlines and backslashes as \ooo octal escapes (also used by rclone.c).
static void mount_unescape(const char *s, char *out, size_t cap) {
    size_t o = 0;
    for (const char *c = s; *c && o + 1 < cap; c++) {
        if (c[0] == '\\' && c[1] >= '0' && c[1] <= '3' && c[2] >= '0' && c[2] <= '7' && c[3] >= '0' && c[3] <= '7') {
            out[o++] = (char)((c[1] - '0') * 64 + (c[2] - '0') * 8 + (c[3] - '0'));
            c += 3;
        } else out[o++] = *c;
    }
    out[o] = 0;
}

static void remote_mounts_load(void) {
    strv_free(g_remote, g_nremote);
    g_remote = NULL;
    g_nremote = 0;
    FILE *f = fopen("/proc/self/mounts", "r");
    if (!f) return;
    char line[PATH_MAX * 2], src[PATH_MAX], mp[PATH_MAX], type[64];
    static const char *types[] = {"nfs", "nfs4", "cifs", "smb3", "smbfs", "fuse.sshfs", "fuse.rclone", "fuse.gvfsd-fuse", "fuse.fe-device",
                                  "9p", "davfs", "fuse.s3fs", "afs", "ceph", "glusterfs", "fuse.glusterfs"};
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%4095s %4095s %63s", src, mp, type) != 3) continue;
        bool remote = false;
        for (size_t i = 0; i < ARRAY_COUNT(types) && !remote; i++) remote = !strcmp(type, types[i]);
        if (!remote) continue;
        char clean[PATH_MAX];
        mount_unescape(mp, clean, sizeof clean);
        g_remote = xrealloc(g_remote, (size_t)(g_nremote + 1) * sizeof(char *));
        g_remote[g_nremote++] = strdup(clean);
    }
    fclose(f);
}

static bool path_in_cloud(const char *path); // rclone.c

static bool path_is_remote(const char *path) {
    if (path_in_cloud(path)) return true; // cloud folders (mounted or about to be)
    for (int i = 0; i < g_nremote; i++) {
        size_t l = strlen(g_remote[i]);
        if (!strncmp(path, g_remote[i], l) && (path[l] == 0 || path[l] == '/')) return true;
    }
    const char *gv = getenv("FE_GVFS_DIR"); // tests
    return gv && gv[0] && !strncmp(path, gv, strlen(gv));
}

// Inside a connected phone or camera (they have no trash).
static bool path_on_phone(const char *path) {
    for (int i = 0; i < A.devs.nph; i++) {
        size_t l = strlen(A.devs.ph[i].mount);
        if (l && !strncmp(path, A.devs.ph[i].mount, l) && (path[l] == 0 || path[l] == '/')) return true;
    }
    return false;
}

static int qa_add_group(const char *name) {
    A.qa = xrealloc(A.qa, (size_t)(A.nqa + 1) * sizeof(QaGroup));
    QaGroup *g = &A.qa[A.nqa];
    memset(g, 0, sizeof *g);
    snprintf(g->name, sizeof g->name, "%s", name);
    return A.nqa++;
}

static void qa_insert(int gi, int at, const char *label, const char *path) {
    if (gi < 0 || gi >= A.nqa) return;
    QaGroup *g = &A.qa[gi];
    at = CLAMP(at < 0 ? g->n : at, 0, g->n);
    g->items = xrealloc(g->items, (size_t)(g->n + 1) * sizeof(QaItem));
    memmove(&g->items[at + 1], &g->items[at], (size_t)(g->n - at) * sizeof(QaItem));
    snprintf(g->items[at].label, sizeof g->items[at].label, "%s", label ? label : "");
    snprintf(g->items[at].path, sizeof g->items[at].path, "%s", path);
    g->n++;
}

static void qa_remove(int gi, int i) {
    QaGroup *g = &A.qa[gi];
    memmove(&g->items[i], &g->items[i + 1], (size_t)(g->n - i - 1) * sizeof(QaItem));
    g->n--;
}

static void qa_free(void) {
    for (int i = 0; i < A.nqa; i++) free(A.qa[i].items);
    free(A.qa);
    A.qa = NULL;
    A.nqa = 0;
}

static bool qa_find(const char *path, int *gi, int *ii) {
    for (int g = 0; g < A.nqa; g++)
        for (int i = 0; i < A.qa[g].n; i++)
            if (!strcmp(A.qa[g].items[i].path, path)) {
                if (gi) *gi = g;
                if (ii) *ii = i;
                return true;
            }
    return false;
}

// First run (or no Quick Access lines in settings): the usual folders, old pins, Trash.
static void qa_defaults(void) {
    qa_free();
    const char *home = getenv("HOME") ? getenv("HOME") : "/";
    int g = qa_add_group("Quick access");
    qa_insert(g, -1, "Home", home);
    static const char *dirs[] = {"Desktop", "Documents", "Downloads", "Pictures", "Music", "Videos", "Projects"};
    char buf[PATH_MAX];
    for (size_t i = 0; i < ARRAY_COUNT(dirs); i++) {
        path_join(buf, sizeof buf, home, dirs[i]);
        struct stat st;
        if (stat(buf, &st) == 0 && S_ISDIR(st.st_mode)) qa_insert(g, -1, "", buf);
    }
    for (int i = 0; i < A.npins; i++)
        if (!qa_find(A.pins[i], NULL, NULL)) qa_insert(g, -1, "", A.pins[i]);
    char tf[PATH_MAX], ti[PATH_MAX];
    trash_dirs(tf, ti, sizeof tf);
    qa_insert(g, -1, "Trash", tf);
}

static bool is_std_folder(const char *path) {
    const char *home = getenv("HOME") ? getenv("HOME") : "/";
    size_t hl = strlen(home);
    if (strncmp(path, home, hl) || path[hl] != '/') return false;
    static const char *dirs[] = {"Desktop", "Documents", "Downloads", "Pictures", "Music", "Videos", "Projects"};
    for (size_t i = 0; i < ARRAY_COUNT(dirs); i++)
        if (!strcmp(path + hl + 1, dirs[i])) return true;
    return false;
}

static Place *place_row(void) {
    if (A.nplaces >= (int)ARRAY_COUNT(A.places)) return NULL;
    Place *p = &A.places[A.nplaces++];
    memset(p, 0, sizeof *p);
    p->group = p->item = p->dev = p->phone = p->server = -1;
    return p;
}

// --- drives (devices.c does the work on worker threads) -------------------------

static void dev_start_phone(const PhoneDev *ph);
static void panes_leave(const char *path);

// One of our phones lost its folder (unplugged, or it dropped off USB): tabs in it leave, and if it comes back soon
// it's connected again by itself (its pairing is kept) and the active tab goes back where it was.
static void dev_phones_follow(const DevList *old, const DevList *now) {
    u64 t = now_ns();
    for (int i = 0; i < old->nph; i++) {
        const PhoneDev *o = &old->ph[i];
        if (!o->own || !o->mount[0]) continue;
        bool still = false;
        for (int k = 0; k < now->nph && !still; k++) still = now->ph[k].own && !strcmp(now->ph[k].serial, o->serial) && now->ph[k].mount[0];
        if (still) continue;
        const char *cur = active_pane()->path;
        size_t ml = strlen(o->mount);
        snprintf(A.dev_back.serial, sizeof A.dev_back.serial, "%s", o->serial);
        snprintf(A.dev_back.rel, sizeof A.dev_back.rel, "%s", !strncmp(cur, o->mount, ml) && (cur[ml] == '/' || !cur[ml]) ? cur + ml : "");
        A.dev_back.t = t;
        A.dev_back.active = !strncmp(cur, o->mount, ml) && (cur[ml] == '/' || !cur[ml]);
        panes_leave(o->mount);
        toast("\xE2\x80\x9C%s\xE2\x80\x9D disconnected%s", o->name, o->kind == 'a' ? " (if it comes back, it's reconnected)" : "");
    }
    for (int k = 0; k < now->nph; k++) { // back within a minute: connect again
        const PhoneDev *n = &now->ph[k];
        if (!n->own || n->mount[0] || !A.dev_back.serial[0] || strcmp(n->serial, A.dev_back.serial) || t - A.dev_back.t > 60000000000ull) continue;
        if (A.dev_connecting[0]) continue;
        A.dev_back.reconnecting = true;
        A.dev_back.serial[0] = 0;
        dev_start_phone(n);
    }
}

static void ssh_master_close_job(void *arg) { ssh_master_close(arg); free(arg); }

// Re-read the network mounts; SFTP folders that went away (disconnected here or by another app) take their shared
// ssh connection with them.
static void nets_reload(void) {
    char gone[ARRAY_COUNT(A.nets)][PATH_MAX];
    int ng = 0;
    for (int i = 0; i < A.nnets; i++) snprintf(gone[ng++], PATH_MAX, "%s", A.nets[i].path);
    A.nnets = network_mounts(A.nets, (int)ARRAY_COUNT(A.nets));
    for (int i = 0; i < ng; i++) {
        bool still = !strstr(gone[i], "/sftp:");
        for (int k = 0; k < A.nnets && !still; k++) still = !strcmp(A.nets[k].path, gone[i]);
        if (!still) pool_submit(ssh_master_close_job, strdup(gone[i]));
    }
}

static void dev_scan_done(void *arg) {
    DevList *L = arg;
    DevList *old = xmalloc(sizeof *old);
    *old = A.devs;
    A.devs = *L;
    free(L);
    dev_phones_follow(old, &A.devs);
    free(old);
    A.dev_scanning = false;
    nets_reload();
    init_places();
    if (A.dev_rescan) { A.dev_rescan = false; devices_refresh(); }
}

// The drives land first, so they don't wait for the slow phone scan (and are there in the first frame, see main).
static void dev_drives_done(void *arg) {
    DevList *L = arg;
    memcpy(A.devs.d, L->d, sizeof L->d);
    A.devs.n = L->n;
    free(L);
    A.devs_known = true;
    nets_reload();
    init_places();
}

static void dev_scan_job(void *arg) {
    DevList *L = arg;
    drives_scan(L);
    DevList *D = xmalloc(sizeof *D);
    *D = *L;
    main_post(dev_drives_done, D);
    phones_scan(L);
    main_post(dev_scan_done, L);
}

static void devices_refresh(void) {
    if (!A.devs_enabled) return;
    if (A.dev_scanning) { A.dev_rescan = true; return; }
    A.dev_scanning = true;
    pool_submit(dev_scan_job, xcalloc(1, sizeof(DevList)));
}

// Panes showing something inside path go home first (an unmount fails while it's in use).
static void panes_leave(const char *path) {
    size_t l = strlen(path);
    const char *home = getenv("HOME") ? getenv("HOME") : "/";
    for (int i = 0; i < A.ntabs; i++) {
        Pane *p = &A.tabs[i]->pane[0];
        if (!strncmp(p->path, path, l) && (p->path[l] == 0 || p->path[l] == '/')) pane_navigate(p, home, true);
    }
}

static void dev_start(int kind, const char *target, const char *disk, bool open_after) {
    DevJob *j = xcalloc(1, sizeof *j);
    j->kind = kind;
    snprintf(j->target, sizeof j->target, "%s", target);
    if (disk) snprintf(j->disk, sizeof j->disk, "%s", disk);
    j->open_after = open_after;
    A.dev_busy++;
    pool_submit(dev_job, j);
}

// Connect a phone or camera (gio mount, like a network location; it asks nothing) and open it.
// Ours (devfs.c, started as a helper) when our connector handles it, else GVfs (uri).
static void dev_start_phone_uri(char kind, const char *uri) {
    DevJob *j = xcalloc(1, sizeof *j);
    j->kind = DEV_NET_MOUNT;
    j->phone = kind;
    snprintf(j->target, sizeof j->target, "%s", uri);
    pthread_mutex_init(&j->qmtx, NULL);
    pthread_cond_init(&j->qcond, NULL);
    j->open_after = true;
    A.dev_busy++;
    pool_submit(dev_job, j);
}

static void dev_start_phone(const PhoneDev *ph) {
    if (!ph->own) { dev_start_phone_uri(ph->kind, ph->uri); return; }
    if (A.dev_connecting[0] && !strcmp(A.dev_connecting, ph->sys)) { toast("Still connecting to \xE2\x80\x9C%s\xE2\x80\x9D\xE2\x80\xA6", ph->name); return; }
    DevJob *j = xcalloc(1, sizeof *j);
    j->kind = DEV_OWN_MOUNT;
    j->phone = ph->kind;
    snprintf(j->target, sizeof j->target, "%s", ph->sys);
    snprintf(j->fallback, sizeof j->fallback, "%s", ph->gvfs_uri);
    snprintf(A.dev_connecting, sizeof A.dev_connecting, "%s", ph->sys);
    pthread_mutex_init(&j->qmtx, NULL);
    pthread_cond_init(&j->qcond, NULL);
    j->open_after = true;
    A.dev_busy++;
    pool_submit(dev_job, j);
}

// Show a folder where the user is: the active folder tab, or - when the active tab is a terminal (its folder
// pane is hidden) - a new folder tab next to it.
static void open_folder(const char *path) {
    if (A.ap && A.ap->cur && A.ap->cur->term) tab_new(path, A.cur + 1, true);
    else pane_navigate(active_pane(), path, true);
}

// Mount / unmount / eject the drive (or disconnect the network place) of a sidebar row.
static const char *place_open_path(const Place *p, char *buf, size_t cap); // remote servers
static void net_server_folder(const char *uri, const char *mount, char *out, size_t cap); // remote servers

static void place_dev_action(int row, int act) {
    if (row < 0 || row >= A.nplaces) return;
    Place *pl = &A.places[row];
    BlockDev *d = pl->dev >= 0 && pl->dev < A.devs.n ? &A.devs.d[pl->dev] : NULL;
    PhoneDev *ph = pl->phone >= 0 && pl->phone < A.devs.nph ? &A.devs.ph[pl->phone] : NULL;
    switch (act) {
    case ACT_DEV_MOUNT:
    case ACT_DEV_OPEN_TAB:
        if (ph && !ph->mount[0]) {
            if (ph->need[0]) { toast("To open an iPhone, install %s, then unplug it and plug it back in", ph->need); break; }
            toast("Connecting to \xE2\x80\x9C%s\xE2\x80\x9D\xE2\x80\xA6", pl->label);
            dev_start_phone(ph);
            if (act == ACT_DEV_OPEN_TAB) A.dev_open_tab = true;
        } else if (d && !d->mount[0]) {
            toast("Mounting \xE2\x80\x9C%s\xE2\x80\x9D\xE2\x80\xA6", pl->label);
            dev_start(DEV_MOUNT, d->dev, NULL, true);
            if (act == ACT_DEV_OPEN_TAB) A.dev_open_tab = true;
        } else if (pl->path[0]) {
            char op[PATH_MAX];
            if (act == ACT_DEV_OPEN_TAB) tab_new(place_open_path(pl, op, sizeof op), A.cur + 1, true);
            else open_folder(place_open_path(pl, op, sizeof op));
        }
        break;
    case ACT_DEV_UNMOUNT:
    case ACT_DEV_EJECT:
        if (pl->kind == PK_NETWORK || pl->kind == PK_PHONE) { place_dev_action(row, ACT_NET_DISCONNECT); break; }
        if (!pl->path[0]) break;
        panes_leave(pl->path);
        if (d) dev_start(act == ACT_DEV_EJECT ? DEV_EJECT : DEV_UNMOUNT, d->dev, act == ACT_DEV_EJECT && d->removable ? d->disk : NULL, false);
        else dev_start(DEV_NET_UNMOUNT, pl->path, NULL, false); // a media mount lsblk didn't report
        toast(act == ACT_DEV_EJECT ? "Ejecting \xE2\x80\x9C%s\xE2\x80\x9D\xE2\x80\xA6" : "Unmounting \xE2\x80\x9C%s\xE2\x80\x9D\xE2\x80\xA6", pl->label);
        break;
    case ACT_NET_DISCONNECT:
        if (!pl->path[0]) break;
        panes_leave(pl->path);
        dev_start(DEV_NET_UNMOUNT, pl->path, NULL, false);
        toast("Disconnecting \xE2\x80\x9C%s\xE2\x80\x9D\xE2\x80\xA6", pl->label);
        break;
    }
}

static const char *dev_name_get(const char *key);
static bool net_server_is_mount(const char *uri, const char *mount);
static void dialog_open(const char *title, const char *msg, const char **buttons, int nb, int def, int cancel,
                        const char *check_label, DialogFn fn, void *ud);
static void sheet_check(Rect r, const char *label, bool on);

// udisks errors read "Error mounting /dev/x at /run/media/...: wrong fs type, ... (udisks-error-quark, 0)":
// keep the part after the location, drop the error-domain suffix.
static void udisks_tidy(char *s) {
    char *q = strstr(s, "(udisks-error-quark");
    if (q) *q = 0;
    if (!strncmp(s, "Error ", 6)) {
        char *c = strstr(s, ": ");
        if (c) memmove(s, c + 2, strlen(c + 2) + 1);
    }
    for (char *c = s; *c; c++) if (*c == '\n') *c = ' ';
    for (size_t l = strlen(s); l && (s[l - 1] == ' ' || s[l - 1] == '.'); ) s[--l] = 0;
}

static void ntfs_ro_cb(int choice, bool check, void *ud) {
    (void)check;
    DevJob *j = ud;
    if (choice == 0) {
        DevJob *k = xcalloc(1, sizeof *k);
        k->kind = DEV_MOUNT;
        snprintf(k->target, sizeof k->target, "%s", j->target);
        snprintf(k->opts, sizeof k->opts, "ro");
        k->open_after = true;
        A.dev_busy++;
        toast("Opening read-only\xE2\x80\xA6");
        pool_submit(dev_job, k);
    }
    free(j);
}

static void dev_job_done(void *arg) {
    DevJob *j = arg;
    A.dev_busy--;
    char first[600];
    snprintf(first, sizeof first, "%s", j->out);
    if (j->kind == DEV_MOUNT || j->kind == DEV_UNMOUNT || j->kind == DEV_EJECT) udisks_tidy(first);
    else first[strcspn(first, "\n")] = 0;
    if (j->rc != 0 && j->kind == DEV_MOUNT && !j->opts[0]) { // a Windows volume that wasn't shut down cleanly
        const BlockDev *bd = NULL;
        for (int k = 0; k < A.devs.n; k++) if (!strcmp(A.devs.d[k].dev, j->target)) bd = &A.devs.d[k];
        if (bd && !strncmp(bd->fstype, "ntfs", 4) && (strstr(j->out, "wrong fs type") || strstr(j->out, "dirty"))) {
            char nm[96], msg[700];
            const char *dn = dev_name_get(bd->uuid[0] ? bd->uuid : bd->dev);
            if (dn) snprintf(nm, sizeof nm, "%s", dn);
            else if (bd->label[0]) snprintf(nm, sizeof nm, "%s", bd->label);
            else { char sz[32]; fmt_size(sz, sizeof sz, bd->size); snprintf(nm, sizeof nm, "%s Volume", sz); }
            snprintf(msg, sizeof msg, "\xE2\x80\x9C%s\xE2\x80\x9D is still marked as in use by Windows (Fast Startup, hibernation or an "
                     "unexpected shutdown), so Linux won't write to it. To use it normally, start Windows and shut it down "
                     "fully (hold Shift while choosing Shut down) or turn off Fast Startup. You can open it read-only now.", nm);
            static const char *b[] = {"Open read-only", "Cancel"};
            dialog_open("Windows didn\xE2\x80\x99t shut down fully", msg, b, 2, 0, 1, NULL, ntfs_ro_cb, j);
            A.dlg.icon = DI_WARN;
            A.dev_open_tab = false;
            return; // j is freed by the callback
        }
    }
    if (j->rc != 0 && (strstr(j->out, "Not authorized") || strstr(j->out, "dismissed")))
        snprintf(first, sizeof first, "the system asked for your password and it wasn't given");
    if (j->kind == DEV_OWN_MOUNT) {
        A.dev_connecting[0] = 0;
        const char *what = j->phone == 'a' ? "iPhone" : j->phone == 'c' ? "camera" : "phone";
        char kind[16] = "";
        const char *colon = strchr(j->out, ':');
        if (colon && colon - j->out < (int)sizeof kind) snprintf(kind, sizeof kind, "%.*s", (int)(colon - j->out), j->out);
        const char *msg = colon ? colon + 2 : j->out;
        bool gvfs_can = j->fallback[0] && (!strcmp(kind, "busy") || !strcmp(kind, "perm") || !strcmp(kind, "unsupported") || !strcmp(kind, "io"));
        if (j->rc == 0 && A.dev_back.reconnecting) { // it came back: where we were (if we were in it)
            A.dev_back.reconnecting = false;
            char p[PATH_MAX * 2];
            snprintf(p, sizeof p, "%s%s", j->mount, A.dev_back.rel);
            if (A.dev_back.active) open_folder(p);
            toast("Reconnected");
        } else if (j->rc == 0) {
            if (A.dev_open_tab) tab_new(j->mount, A.cur + 1, true);
            else open_folder(j->mount);
        } else if (gvfs_can) { // ours couldn't have it (another program has it, no permission...): GVfs may
            toast("Opening the %s through GVfs\xE2\x80\xA6", what);
            dev_start_phone_uri(j->phone, j->fallback);
        } else if (!strcmp(kind, "perm"))
            toast("Couldn't open the %s: no permission to use it over USB. Reinstall the app (it adds a udev rule), then plug it in again", what);
        else if (!strcmp(kind, "busy"))
            toast("Couldn't open the %s: another program is using it. Close it (or disconnect it there) and try again", what);
        else if (!strcmp(kind, "trust"))
            toast("Couldn't open the iPhone: Trust wasn't tapped. Unlock it, click it again and tap Trust when it asks");
        else if (!strcmp(kind, "denied"))
            toast("Couldn't open the iPhone: it was told not to trust this computer. Unplug it, plug it back in and tap Trust");
        else if (!strcmp(kind, "locked"))
            toast("Couldn't open the %s: unlock it, then try again", what);
        else toast("Couldn't open the %s: %s", what, msg[0] ? msg : "unknown error");
        if (j->rc != 0) { A.dev_open_tab = false; A.dev_back.reconnecting = false; }
    } else if (j->rc != 0 && j->phone && j->kind == DEV_NET_MOUNT) {
        char hint[600];
        phone_error_hint(j->phone, first, hint, sizeof hint);
        toast("Couldn't open the %s: %s", j->phone == 'a' ? "iPhone" : j->phone == 'c' ? "camera" : "phone", hint);
    } else if (j->rc != 0) {
        const char *what = j->kind == DEV_MOUNT || j->kind == DEV_NET_MOUNT ? "mount" : "unmount";
        if (j->rc == -1 && !first[0]) toast("Couldn't %s: the helper tool isn't installed", what);
        else toast("Couldn't %s: %s", what, first[0] ? first : "unknown error");
    } else if (j->kind == DEV_EJECT || j->kind == DEV_UNMOUNT) {
        toast(j->kind == DEV_EJECT ? "Safe to remove" : "Unmounted");
    } else if (j->kind == DEV_NET_UNMOUNT) {
        bool phone = phone_kind(path_basename(j->target)) != 0;
        for (int k = 0; k < A.devs.nph && !phone; k++) phone = !strcmp(A.devs.ph[k].mount, j->target);
        toast(phone ? "Safe to unplug" : "Disconnected");
    }
    if (j->rc == 0 && j->kind == DEV_MOUNT && j->open_after && j->mount[0]) {
        if (A.dev_open_tab) tab_new(j->mount, A.cur + 1, true);
        else open_folder(j->mount);
    }
    if (j->rc == 0 && j->kind == DEV_NET_MOUNT && j->phone) { // a phone: its folder in the FUSE bridge
        if (!j->mount[0]) toast(j->fuse_ok ? "Connected, but its folder didn't show up" :
                                "Connected, but GVfs's file bridge (gvfsd-fuse) isn't running, so its files can't be shown");
        else if (A.dev_open_tab) tab_new(j->mount, A.cur + 1, true);
        else open_folder(j->mount);
    } else if (j->rc == 0 && j->kind == DEV_NET_MOUNT) { // open the network folder: the new one, else the one for this server
        NetMount now[16];
        int n = network_mounts(now, 16);
        const char *open = NULL;
        for (int k = 0; k < n && !open; k++) {
            bool old = false;
            for (int m = 0; m < A.nnets; m++) old |= !strcmp(A.nets[m].path, now[k].path);
            if (!old) open = now[k].path;
        }
        for (int k = 0; k < n && !open; k++)
            if (net_server_is_mount(j->target, now[k].path)) open = now[k].path; // already connected (or reconnected)
        if (open) {
            char dir[PATH_MAX];
            net_server_folder(j->target, open, dir, sizeof dir); // the folder in the address, not the server's root
            open_folder(dir);
        } else if (!j->fuse_ok) toast("Connected, but GVfs's file bridge (gvfsd-fuse) isn't running, so its files can't be shown");
        else toast("Connected");
        devices_refresh(); // the Network section shows it
    }
    A.dev_open_tab = false;
    if (j->kind == DEV_NET_MOUNT || j->kind == DEV_OWN_MOUNT) {
        pthread_mutex_destroy(&j->qmtx);
        pthread_cond_destroy(&j->qcond);
    }
    free(j);
    devices_refresh();
}


// Our connector waits for the user: say what to do on the device.
static void dev_phone_wait(void *arg) {
    char *what = arg;
    if (!strcmp(what, "trust")) toast("Unlock the iPhone and tap Trust (then enter its passcode)\xE2\x80\xA6");
    else if (!strcmp(what, "unlock")) toast("Unlock the device to let this computer see its files\xE2\x80\xA6");
    free(what);
}

#define QA_EDIT_DEV 1000 // qa_edit_g value while a device row is being renamed

// Stable identity of a device row for custom names: filesystem UUID, else the device, else the mount path.
static const char *place_dev_key(const Place *p) {
    if (p->dev >= 0 && p->dev < A.devs.n) {
        const BlockDev *d = &A.devs.d[p->dev];
        return d->uuid[0] ? d->uuid : d->dev;
    }
    if (p->phone >= 0 && p->phone < A.devs.nph) return A.devs.ph[p->phone].uri[0] ? A.devs.ph[p->phone].uri : A.devs.ph[p->phone].name;
    return p->path;
}

static bool place_is_device(const Place *p) {
    return p->kind == PK_DRIVE || p->kind == PK_UNMOUNTED || p->kind == PK_NETWORK || p->kind == PK_PHONE;
}

static const char *dev_name_get(const char *key) {
    for (int i = 0; i < A.ndev_names; i++)
        if (!strcmp(A.dev_names[i].key, key)) return A.dev_names[i].name;
    return NULL;
}

// name "" = back to the device's own name.
static void dev_name_set(const char *key, const char *name) {
    if (!key[0] || strpbrk(key, "\n\t") || strpbrk(name, "\n\t")) return;
    for (int i = 0; i < A.ndev_names; i++)
        if (!strcmp(A.dev_names[i].key, key)) {
            free(A.dev_names[i].name);
            if (name[0]) { A.dev_names[i].name = strdup(name); return; }
            free(A.dev_names[i].key);
            A.dev_names[i] = A.dev_names[--A.ndev_names];
            return;
        }
    if (!name[0] || A.ndev_names >= (int)ARRAY_COUNT(A.dev_names)) return;
    A.dev_names[A.ndev_names].key = strdup(key);
    A.dev_names[A.ndev_names].name = strdup(name);
    A.ndev_names++;
}

static void dev_edit_begin(const Place *p);
static void connect_open(int saved);
typedef struct NetAddr NetAddr;
static void connect_open_addr(const NetAddr *a);
static void dialog_open(const char *title, const char *msg, const char **buttons, int nb, int def, int cancel,
                        const char *check_label, DialogFn fn, void *ud);

// --- remote servers (Connect to server) ----------------------------------------------------------------

typedef struct { const char *label, *scheme, *backend; int port; } NetProto;
static const NetProto g_net_protos[] = {
    {"SFTP", "sftp", "sftp", 22},  {"SMB", "smb", "smb", 445},     {"FTP", "ftp", "ftp", 21},   {"FTPS", "ftps", "ftp", 21},
    {"WebDAV", "dav", "dav", 80}, {"WebDAVS", "davs", "dav", 443}, {"NFS", "nfs", "nfs", 2049},
};
#define NET_PROTO_COUNT ((int)ARRAY_COUNT(g_net_protos))

// GVfs backend for the protocol installed? (fake tools in tests: yes)
static bool net_proto_available(int i) {
    if (getenv("FE_GIO")) return true;
    return gvfs_backend_installed(g_net_protos[i].backend);
}

static void url_enc(char *out, size_t cap, const char *in, bool keep_slash) {
    size_t o = 0;
    for (const u8 *c = (const u8 *)in; *c && o + 4 < cap; c++) {
        if (isalnum(*c) || strchr("-._~", *c) || (keep_slash && *c == '/')) out[o++] = (char)*c;
        else o += (size_t)snprintf(out + o, cap - o, "%%%02X", *c);
    }
    out[o] = 0;
}

static void url_dec(char *out, size_t cap, const char *in, size_t n) {
    size_t o = 0;
    for (size_t i = 0; i < n && in[i] && o + 1 < cap; i++) {
        if (in[i] == '%' && i + 2 < n && isxdigit((u8)in[i + 1]) && isxdigit((u8)in[i + 2])) {
            char h[3] = {in[i + 1], in[i + 2], 0};
            out[o++] = (char)strtol(h, NULL, 16);
            i += 2;
        } else out[o++] = in[i];
    }
    out[o] = 0;
}

struct NetAddr { int proto; char host[256], port[8], path[512], user[128], domain[128]; };

static void net_uri(const NetAddr *a, char *out, size_t cap) {
    char ui[400] = "", u[200], d[200], pth[1600], host[300];
    url_enc(u, sizeof u, a->user, false);
    url_enc(d, sizeof d, a->domain, false);
    const NetProto *np = &g_net_protos[a->proto];
    if (a->user[0] && strcmp(np->scheme, "nfs")) {
        if (a->domain[0] && !strcmp(np->scheme, "smb")) snprintf(ui, sizeof ui, "%s;%s@", d, u);
        else snprintf(ui, sizeof ui, "%s@", u);
    }
    url_enc(pth, sizeof pth, a->path[0] == '/' ? a->path + 1 : a->path, true);
    if (strchr(a->host, ':') && a->host[0] != '[') snprintf(host, sizeof host, "[%s]", a->host); // IPv6
    else snprintf(host, sizeof host, "%s", a->host);
    snprintf(out, cap, "%s://%s%s%s%s/%s", np->scheme, ui, host, a->port[0] ? ":" : "", a->port, pth);
}

static bool net_parse(const char *uri, NetAddr *a) {
    memset(a, 0, sizeof *a);
    const char *sep = strstr(uri, "://");
    if (!sep) return false;
    a->proto = -1;
    for (int i = 0; i < NET_PROTO_COUNT; i++)
        if ((size_t)(sep - uri) == strlen(g_net_protos[i].scheme) && !strncmp(uri, g_net_protos[i].scheme, (size_t)(sep - uri))) a->proto = i;
    if (a->proto < 0) return false;
    const char *p = sep + 3, *slash = strchr(p, '/'), *end = slash ? slash : p + strlen(p);
    const char *at = memchr(p, '@', (size_t)(end - p));
    if (at) {
        const char *semi = memchr(p, ';', (size_t)(at - p));
        if (semi) { url_dec(a->domain, sizeof a->domain, p, (size_t)(semi - p)); p = semi + 1; }
        url_dec(a->user, sizeof a->user, p, (size_t)(at - p));
        p = at + 1;
    }
    const char *hend = end, *colon = NULL;
    if (*p == '[') { const char *rb = memchr(p, ']', (size_t)(end - p)); if (rb) { snprintf(a->host, sizeof a->host, "%.*s", (int)(rb - p - 1), p + 1); colon = rb + 1 < end && rb[1] == ':' ? rb + 1 : NULL; hend = NULL; } }
    if (hend) {
        colon = memchr(p, ':', (size_t)(end - p));
        snprintf(a->host, sizeof a->host, "%.*s", (int)((colon ? colon : end) - p), p);
    }
    if (colon) snprintf(a->port, sizeof a->port, "%.*s", (int)MIN(6, end - colon - 1), colon + 1);
    if (slash && slash[1]) url_dec(a->path, sizeof a->path, slash + 1, strlen(slash + 1));
    return a->host[0] != 0;
}

static bool ssh_target_uri(const char *uri, SshTarget *t) {
    NetAddr a;
    memset(t, 0, sizeof *t);
    if (!net_parse(uri, &a) || strcmp(g_net_protos[a.proto].scheme, "sftp")) return false;
    snprintf(t->user, sizeof t->user, "%s", a.user);
    snprintf(t->host, sizeof t->host, "%s", a.host);
    snprintf(t->port, sizeof t->port, "%s", a.port);
    return t->host[0] != 0;
}

// Is this mount (GVfs names them "sftp:host=box,user=me", "smb-share:server=nas,share=x") the saved server's?
static bool net_server_is_mount(const char *uri, const char *mount) {
    NetAddr a;
    if (!net_parse(uri, &a)) return false;
    char h1[300], h2[300];
    snprintf(h1, sizeof h1, "host=%s", a.host);
    snprintf(h2, sizeof h2, "server=%s", a.host);
    const char *b = path_basename(mount);
    if (!strcasestr(b, h1) && !strcasestr(b, h2)) return false;
    if (!strcmp(g_net_protos[a.proto].scheme, "smb") && a.path[0]) {
        char sh[600];
        snprintf(sh, sizeof sh, "share=%.*s", (int)strcspn(a.path, "/"), a.path);
        if (!strcasestr(b, sh)) return false;
    }
    return true;
}

static bool net_server_connected(const char *uri) {
    for (int k = 0; k < A.nnets; k++)
        if (net_server_is_mount(uri, A.nets[k].path)) return true;
    return false;
}

// The folder a server's address points at, inside its mount: sftp://box/srv/app -> <mount>/srv/app (GVfs mounts the
// server's root). SMB mounts are the share itself, WebDAV ones ("prefix=") already start at the folder.
static void net_server_folder(const char *uri, const char *mount, char *out, size_t cap) {
    NetAddr a;
    char rel[512] = "";
    if (net_parse(uri, &a) && !strcasestr(path_basename(mount), "prefix=")) {
        const char *r = a.path;
        if (!strcmp(g_net_protos[a.proto].scheme, "smb")) r += strcspn(r, "/"); // past the share
        while (*r == '/') r++;
        snprintf(rel, sizeof rel, "%s", r);
        for (size_t l = strlen(rel); l && rel[l - 1] == '/'; l--) rel[l - 1] = 0;
    }
    if (rel[0]) snprintf(out, cap, "%s/%s", mount, rel);
    else snprintf(out, cap, "%s", mount);
}

// Where a sidebar row opens: its path, or for a connected saved server the folder in its address.
static const char *place_open_path(const Place *p, char *buf, size_t cap) {
    if (p->kind != PK_NETWORK || p->server < 0 || p->server >= A.nservers) return p->path;
    net_server_folder(A.servers[p->server].uri, p->path, buf, cap);
    return buf;
}

// icon: a network icon key (icons.c; "" = the default globe)
static void net_server_save(const char *name, const char *uri, const char *icon) {
    if (!icon || strpbrk(icon, "\t\n")) icon = "";
    for (int i = 0; i < A.nservers; i++)
        if (!strcmp(A.servers[i].uri, uri)) {
            snprintf(A.servers[i].name, sizeof A.servers[i].name, "%s", name);
            snprintf(A.servers[i].icon, sizeof A.servers[i].icon, "%s", icon);
            A.settings_dirty = true;
            return;
        }
    if (A.nservers >= (int)ARRAY_COUNT(A.servers) || strpbrk(uri, "\t\n") || strpbrk(name, "\t\n")) return;
    snprintf(A.servers[A.nservers].name, sizeof A.servers[0].name, "%s", name);
    snprintf(A.servers[A.nservers].uri, sizeof A.servers[0].uri, "%s", uri);
    snprintf(A.servers[A.nservers].icon, sizeof A.servers[0].icon, "%s", icon);
    A.nservers++;
    A.settings_dirty = true;
}

static void secret_clear_job(void *arg) { secret_clear(arg); free(arg); }

typedef struct { char from[1024], to[1024]; } SecretMove;
static void secret_move_job(void *arg) { // the server's address changed: its stored password follows
    SecretMove *m = arg;
    char pw[256];
    if (secret_lookup(m->from, pw, sizeof pw)) {
        size_t l = strlen(pw);
        if (l && pw[l - 1] == '\n') pw[l - 1] = 0;
        secret_store(m->to, pw);
        secret_clear(m->from);
    }
    explicit_bzero(pw, sizeof pw);
    free(m);
}

typedef struct { char uri[1024], pw[256]; } SecretPut;
static void secret_put_job(void *arg) { // keyring calls can block (unlock prompt): off the main thread
    SecretPut *m = arg;
    secret_store(m->uri, m->pw);
    explicit_bzero(m->pw, sizeof m->pw);
    free(m);
}

static void secret_put_async(const char *uri, const char *pw) {
    SecretPut *m = xcalloc(1, sizeof *m);
    snprintf(m->uri, sizeof m->uri, "%s", uri);
    snprintf(m->pw, sizeof m->pw, "%s", pw);
    pool_submit(secret_put_job, m);
}

static void net_server_update(int i, const char *name, const char *uri, const char *icon) {
    if (i < 0 || i >= A.nservers || strpbrk(uri, "\t\n") || strpbrk(name, "\t\n")) return;
    if (!icon || strpbrk(icon, "\t\n")) icon = "";
    snprintf(A.servers[i].icon, sizeof A.servers[i].icon, "%s", icon);
    if (strcmp(A.servers[i].uri, uri) && secret_available()) {
        SecretMove *m = xcalloc(1, sizeof *m);
        snprintf(m->from, sizeof m->from, "%s", A.servers[i].uri);
        snprintf(m->to, sizeof m->to, "%s", uri);
        pool_submit(secret_move_job, m);
    }
    snprintf(A.servers[i].name, sizeof A.servers[i].name, "%s", name);
    snprintf(A.servers[i].uri, sizeof A.servers[i].uri, "%s", uri);
    A.settings_dirty = true;
}

static void net_server_forget(int i) {
    if (i < 0 || i >= A.nservers) return;
    if (secret_available()) pool_submit(secret_clear_job, strdup(A.servers[i].uri)); // its password goes too
    memmove(&A.servers[i], &A.servers[i + 1], (size_t)(A.nservers - i - 1) * sizeof A.servers[0]);
    A.nservers--;
    A.settings_dirty = true;
}

static void dev_start_net(const char *uri, const char *user, const char *domain, const char *password, bool lookup, bool store) {
    DevJob *j = xcalloc(1, sizeof *j);
    j->lookup_secret = lookup;
    j->store_secret = store;
    j->kind = DEV_NET_MOUNT;
    snprintf(j->target, sizeof j->target, "%s", uri);
    snprintf(j->user, sizeof j->user, "%s", user ? user : "");
    snprintf(j->domain, sizeof j->domain, "%s", domain ? domain : "");
    snprintf(j->password, sizeof j->password, "%s", password ? password : "");
    pthread_mutex_init(&j->qmtx, NULL);
    pthread_cond_init(&j->qcond, NULL);
    j->open_after = true;
    A.dev_busy++;
    pool_submit(dev_job, j);
}

static void secret_prompt_open(DevJob *j); // ui_tools.c

// A saved server: connect straight away (keyring password if one was remembered); a password is asked only
// if the server wants one.
static void net_connect_saved(int i) {
    NetAddr a;
    if (i < 0 || i >= A.nservers || !net_parse(A.servers[i].uri, &a)) return;
    toast("Connecting to %s\xE2\x80\xA6", A.servers[i].name);
    dev_start_net(A.servers[i].uri, a.user, a.domain, "", true, false);
}

#include "addons.c"
#include "units.c"
#include "rclone.c"

// GVfs asked something (unknown host key...): the user decides; never answered on their behalf.
static void net_question_cb(int choice, bool check, void *ud) {
    (void)check;
    dev_net_answer(ud, choice);
}

static void dev_net_question(void *arg) {
    DevJob *j = arg;
    if (j->qkind == 1) { secret_prompt_open(j); return; }
    if (!j->nchoices) { dev_net_answer(j, -1); return; }
    static const char *btn[4];
    for (int k = 0; k < j->nchoices; k++) btn[k] = j->choices[k];
    char msg[1024]; // dialogs wrap their text but don't break lines: join gio's lines with spaces
    snprintf(msg, sizeof msg, "%s", j->question);
    for (char *c = msg; *c; c++) if (*c == '\n') *c = ' ';
    dialog_open("The server asks", msg, btn, j->nchoices, j->nchoices - 1, j->nchoices - 1, NULL, net_question_cb, j);
    A.dlg.icon = DI_WARN;
}

// Rebuild the sidebar rows from the Quick Access groups + mounted drives.
static void init_places(void) {
    remote_mounts_load();
    if (!A.qa) qa_defaults();
    A.nplaces = 0;
    const char *home = getenv("HOME") ? getenv("HOME") : "/";
    char tf[PATH_MAX], ti[PATH_MAX];
    trash_dirs(tf, ti, sizeof tf);
    mkdir_p(tf, 0700);
    mkdir_p(ti, 0700);
    for (int g = 0; g < A.nqa; g++) {
        Place *h = place_row();
        if (!h) break;
        h->kind = PK_GROUP;
        h->group = g;
        snprintf(h->label, sizeof h->label, "%s", A.qa[g].name);
        if (A.qa[g].collapsed) continue;
        for (int i = 0; i < A.qa[g].n; i++) {
            QaItem *it = &A.qa[g].items[i];
            struct stat st; // unplugged / deleted: hidden, not forgotten (network folders: assumed there)
            if (!path_is_remote(it->path) && (stat(it->path, &st) != 0 || !S_ISDIR(st.st_mode))) continue;
            Place *p = place_row();
            if (!p) break;
            p->group = g;
            p->item = i;
            snprintf(p->path, sizeof p->path, "%s", it->path);
            snprintf(p->label, sizeof p->label, "%s", it->label[0] ? it->label : it->path[1] ? path_basename(it->path) : "/");
            p->kind = !strcmp(it->path, home) ? PK_HOME
                    : !strcmp(it->path, tf)   ? PK_TRASH
                    : is_std_folder(it->path) ? PK_FOLDER
                                              : PK_PINNED;
        }
    }
    A.nplaces_user = A.nplaces;
    Place *dh = place_row();
    if (dh) { dh->kind = PK_DEVICES; snprintf(dh->label, sizeof dh->label, "Devices"); }
    int dev_first = A.nplaces;
    Place *fs = place_row();
    if (fs) { fs->kind = PK_DRIVE; snprintf(fs->label, sizeof fs->label, "File System"); snprintf(fs->path, sizeof fs->path, "/"); }
    // block devices: removable / media mounts, and partitions that aren't mounted yet (click to mount)
    for (int k = 0; k < A.devs.n; k++) {
        BlockDev *d = &A.devs.d[k];
        bool media = !strncmp(d->mount, "/run/media/", 11) || !strncmp(d->mount, "/media/", 7) || !strncmp(d->mount, "/mnt/", 5);
        if (d->mount[0] && !media && !d->removable) continue; // system mounts (/, /boot, ...)
        if (d->mount[0] && !strcmp(d->mount, "/")) continue;
        if (!d->mount[0] && d->uuid[0]) { // another partition of a filesystem that's already mounted (multi-device btrfs)
            bool member = false;
            for (int o = 0; o < A.devs.n && !member; o++)
                member = o != k && A.devs.d[o].mount[0] && !strcmp(A.devs.d[o].uuid, d->uuid);
            if (member) continue;
        }
        Place *p = place_row();
        if (!p) break;
        p->kind = d->mount[0] ? PK_DRIVE : PK_UNMOUNTED;
        p->dev = k;
        p->removable = d->removable;
        snprintf(p->path, sizeof p->path, "%s", d->mount);
        if (d->label[0]) snprintf(p->label, sizeof p->label, "%s", d->label);
        else if (d->mount[0]) snprintf(p->label, sizeof p->label, "%s", path_basename(d->mount));
        else {
            char sz[32];
            fmt_size(sz, sizeof sz, d->size);
            snprintf(p->label, sizeof p->label, "%s Volume", sz);
        }
    }
    FILE *fp = fopen("/proc/self/mounts", "r");
    if (fp) {
        char line[1024], dev[512], mnt[512];
        while (fgets(line, sizeof line, fp)) {
            if (sscanf(line, "%511s %511s", dev, mnt) != 2) continue;
            if (!strncmp(mnt, "/run/media/", 11) || !strncmp(mnt, "/media/", 7) || !strncmp(mnt, "/mnt/", 5)) {
                char clean[512];
                size_t o = 0;
                for (const char *c = mnt; *c && o < sizeof clean - 1; c++) {
                    if (c[0] == '\\' && c[1] == '0' && c[2] == '4' && c[3] == '0') { clean[o++] = ' '; c += 3; }
                    else clean[o++] = *c;
                }
                clean[o] = 0;
                bool dup = false; // already listed from lsblk
                for (int r = A.nplaces_user; r < A.nplaces && !dup; r++) dup = !strcmp(A.places[r].path, clean);
                if (dup) continue;
                Place *p = place_row();
                if (!p) break;
                p->kind = PK_DRIVE;
                p->removable = true;
                snprintf(p->path, sizeof p->path, "%s", clean);
                snprintf(p->label, sizeof p->label, "%s", path_basename(clean));
            }
        }
        fclose(fp);
    }
    for (int k = 0; k < A.devs.nph; k++) { // phones and cameras on a cable: click to connect
        Place *p = place_row();
        if (!p) break;
        PhoneDev *ph = &A.devs.ph[k];
        p->kind = PK_PHONE;
        p->phone = k;
        p->removable = ph->mount[0] != 0;
        snprintf(p->path, sizeof p->path, "%s", ph->mount);
        snprintf(p->label, sizeof p->label, "%s", ph->name[0] ? ph->name : ph->kind == 'a' ? "iPhone" : ph->kind == 'c' ? "Camera" : "Phone");
    }
    for (int r = dev_first; r < A.nplaces; r++) { // custom names
        const char *nm = dev_name_get(place_dev_key(&A.places[r]));
        if (nm) snprintf(A.places[r].label, sizeof A.places[r].label, "%s", nm);
    }
    if (A.devs_collapsed) A.nplaces = dev_first; // collapsed: just the header
    { // the Network section is always there (its + opens Connect to server)
        Place *nh = place_row();
        if (nh) { nh->kind = PK_NETHDR; snprintf(nh->label, sizeof nh->label, "Network"); }
        int first_net = A.nplaces;
        for (int k = 0; k < A.nnets && !A.nets_collapsed; k++) {
            if (path_in_cloud(A.nets[k].path) || cloud_for_mount(A.nets[k].path)) continue; // listed under Cloud
            Place *p = place_row();
            if (!p) break;
            p->kind = PK_NETWORK;
            p->removable = true;
            snprintf(p->path, sizeof p->path, "%s", A.nets[k].path);
            snprintf(p->label, sizeof p->label, "%s", A.nets[k].label);
            for (int sv = 0; sv < A.nservers; sv++) // a saved server: its display name
                if (net_server_is_mount(A.servers[sv].uri, p->path)) { snprintf(p->label, sizeof p->label, "%s", A.servers[sv].name); p->server = sv; break; }
            const char *nm = dev_name_get(p->path);
            if (nm) snprintf(p->label, sizeof p->label, "%s", nm);
        }
        for (int k = 0; k < A.nservers && !A.nets_collapsed; k++) { // saved servers not connected: click to connect
            if (net_server_connected(A.servers[k].uri)) continue;
            Place *p = place_row();
            if (!p) break;
            p->kind = PK_NETSAVED;
            p->item = k;
            p->server = k;
            snprintf(p->label, sizeof p->label, "%s", A.servers[k].name);
        }
        if (A.nplaces == first_net && !A.nets_collapsed) { // nothing yet: say so
            Place *p = place_row();
            if (p) { p->kind = PK_NETEMPTY; snprintf(p->label, sizeof p->label, "No network locations"); }
        }
    }
    if (g_rclone == 1) { // Cloud: rclone remotes (rclone.c); + runs rclone config, a click connects and opens one
        Place *ch = place_row();
        if (ch) { ch->kind = PK_CLOUDHDR; snprintf(ch->label, sizeof ch->label, "Cloud"); }
        int first = A.nplaces;
        for (int k = 0; k < g_ncloud && !g_cloud_collapsed; k++) {
            Cloud *c = &g_cloud[k];
            if (!c->listed) continue;
            Place *p = place_row();
            if (!p) break;
            p->kind = PK_CLOUD;
            p->item = k;
            p->removable = c->mount[0] != 0;
            snprintf(p->path, sizeof p->path, "%s", c->mount);
            snprintf(p->label, sizeof p->label, "%s", c->name);
        }
        if (A.nplaces == first && !g_cloud_collapsed) {
            Place *p = place_row();
            if (p) { p->kind = PK_CLOUDEMPTY; snprintf(p->label, sizeof p->label, "No rclone remotes yet"); }
        }
    }
    addon_places(); // add-ons' sections (addons.c)
    places_layout();
    A.redraw = true;
}

static bool place_is_header(const Place *p) { return p->kind == PK_GROUP || p->kind == PK_DEVICES || p->kind == PK_NETHDR || p->kind == PK_CLOUDHDR || p->kind == PK_ADDONHDR; }

// Row positions (content space; the sidebar scrolls by A.side_scroll).
static void places_layout(void) {
    f32 y = A.side_top;
    for (int i = 0; i < A.nplaces; i++) {
        Place *p = &A.places[i];
        if (place_is_header(p)) {
            if (i) y += roundf(S(8));
            p->h = roundf(S(26));
        } else {
            p->h = A.side_row_h;
        }
        p->y = y;
        y += p->h;
    }
    A.side_content_h = y + roundf(S(12));
    A.side_scroll = CLAMP(A.side_scroll, 0.f, MAX(0.f, A.side_content_h - A.r_sidebar.h));
    A.side_scroll_target = CLAMP(A.side_scroll_target, 0.f, MAX(0.f, A.side_content_h - A.r_sidebar.h));
}

static Rect place_rect(int i) {
    Place *p = &A.places[i];
    f32 pad = roundf(S(10));
    return (Rect){A.r_sidebar.x + pad * 0.6f, A.r_sidebar.y + p->y - roundf(A.side_scroll), A.r_sidebar.w - pad * 1.2f, p->h};
}



// --- pane / navigation -----------------------------------------------------

static void begin_rename(Pane *p, i64 pos);
static void toast(const char *fmt, ...);
static void folder_view_apply(Pane *p, const char *path);
static void folder_view_remember(Pane *p);
static void groups_clear(Pane *p);
static void pane_set_group(Pane *p, int g);
static void folder_view_action(Pane *p, int act);
static FolderView *folder_view_find(const char *path, bool exact);
static void recent_push(const char *path);

static void sel_clear(Pane *p) {
    if (p->sel && p->L) memset(p->sel, 0, MAX(p->L->count, 1u));
    p->sel_count = 0;
}

static void sel_set(Pane *p, i64 pos, bool v) {
    u32 e = p->order[pos];
    if (p->sel[e] != v) {
        p->sel[e] = v;
        p->sel_count += v ? 1 : (u32)-1;
    }
}

static void update_title(void) {
    if (!A.ntabs) return;
    Pane *p = active_pane();
    char t[PATH_MAX + 64];
    char dnb[NAME_MAX + 1];
    const char *base = disp_name(p->path, dnb, sizeof dnb);
    if (picker_active()) snprintf(t, sizeof t, "%s \xE2\x80\x94 %s", picker_title(), base);
    else snprintf(t, sizeof t, "%s \xE2\x80\x94 " APP_NAME, base);
    SDL_SetWindowTitle(A.win, t);
}

static void drag_cancel(void) {
    free(A.band_base);
    A.band_base = NULL;
    A.drag = DRAG_NONE;
    A.drag_pane = NULL;
    A.drop_dir[0] = 0;
    A.drop_pane = NULL;
    A.drop_row = -1;
    A.drop_place = -1;
    A.press_collapse = -1;
    A.click_open = -1;
    A.qa_drag_row = -1;
    A.drop_qa = -1;
    A.drag_tab = NULL;
    A.drop_tab = NULL;
    A.drop_split_panel = NULL;
    A.drop_newtab = NULL;
    A.drag_node = NULL;
    A.tdrop_kind = TD_NONE;
    A.tdrop_panel = NULL;
    A.tdrop_before = NULL;
}

static int cmp_strp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void pane_drop_prev(Pane *p) {
    if (p->L_prev) listing_release(p->L_prev);
    p->L_prev = NULL;
}

static void pane_load_ex(Pane *p, const char *path_in, bool scan) {
    char path[PATH_MAX]; // path_in may alias p->path
    snprintf(path, sizeof path, "%s", path_in);
    pane_drop_prev(p);
    if (p->L_next) {
        atomic_store(&p->L_next->cancel, 1);
        listing_release(p->L_next);
        p->L_next = NULL;
    }
    p->from_reload = false;
    p->reload_no_select = false;
    p->keep_scroll = false;
    p->resort_on_complete = false;
    strv_free(p->select_names, p->nselect_names);
    p->select_names = NULL;
    p->nselect_names = 0;
    if (A.ren_pane == p) A.ren_pane = NULL;
    if (A.menu.pane == p) A.menu.open = false;
    if (p->L) {
        atomic_store(&p->L->cancel, 1);
        listing_release(p->L);
    }
    free(p->order);
    free(p->sel);
    p->order = NULL;
    p->sel = NULL;
    p->count = p->sel_count = 0;
    groups_clear(p);
    free(p->gall);
    p->gall = NULL;
    p->cursor = p->anchor = -1;
    p->scroll = p->scroll_target = 0;
    p->sort_pending = false;
    p->complete_seen = false;
    p->prio_first = -1;
    snprintf(p->path, sizeof p->path, "%s", path);
    p->L = NULL;
    if (scan) {
        p->L = listing_new(path, p->sort_key, p->sort_desc, A.show_hidden);
        snprintf(p->L->filter, sizeof p->L->filter, "%s", p->search_q);
        p->L->group = pane_group_eff(p);
        p->L->tf_mode = p->tf_mode;
        snprintf(p->L->tf_exts, sizeof p->L->tf_exts, "%s", p->tf_exts);
        p->L->tree = pane_tree_string(p);
        listing_start(p->L);
    }
    if (A.drag_pane == p) drag_cancel();
    if (A.edit_pane == p) A.edit_pane = NULL;
    A.jump[0] = 0;
}

static void pane_load(Pane *p, const char *path) { pane_load_ex(p, path, true); }

static bool search_q_matches(const char *q, const char *name) { return name_matches(q, name, (int)strlen(name)); }

static const char *rel_base(const char *rel) {
    const char *s = strrchr(rel, '/');
    return s ? s + 1 : rel;
}

static Listing *search_listing_new(Pane *p, const char *q) {
    Listing *L = listing_new(p->path, p->sort_key, p->sort_desc, A.show_hidden);
    L->is_search = true;
    L->cap = 1024;
    L->entries = xmalloc(L->cap * sizeof(Entry));
    L->t_names = L->t_start;
    atomic_store(&L->state, LST_READY);
    L->search_recursive = A.search_recursive;
    search_start(L, p->path, q, A.show_hidden, A.search_recursive);
    return L;
}

// Make L (a search listing) the pane's listing.
static void search_install(Pane *p, Listing *L, const char *q) {
    char qq[256];
    snprintf(qq, sizeof qq, "%s", q);
    bool focus = A.search_pane == p;
    pane_load_ex(p, p->path, false);
    if (focus) A.search_pane = p;
    snprintf(p->search_q, sizeof p->search_q, "%s", qq);
    p->L = L;
    p->order = xmalloc(L->cap * sizeof(u32));
    p->sel = xcalloc(L->cap, 1);
    p->count = 0;
}

static void search_swap(Pane *p) {
    Listing *L = p->L_next;
    p->L_next = NULL;
    char q[256];
    snprintf(q, sizeof q, "%s", p->search_next_q);
    search_install(p, L, q);
}

// Start a streaming recursive search under the current folder. If results are
// already on screen, they stay until the new search produces its first hits.
static void pane_load_search(Pane *p, const char *q) {
    char qq[256];
    snprintf(qq, sizeof qq, "%s", q);
    if (p->L && p->count) {
        if (p->L_next) {
            atomic_store(&p->L_next->cancel, 1);
            listing_release(p->L_next);
        }
        snprintf(p->search_next_q, sizeof p->search_next_q, "%s", qq);
        p->L_next = search_listing_new(p, qq);
        return;
    }
    search_install(p, search_listing_new(p, qq), qq);
}

// Narrow the visible results in place (q is a refinement of the current query):
// every match of q is already in the list, so no rescan is needed.
static void search_refine(Pane *p, const char *q) {
    Listing *L = p->L;
    u32 cur_e = (p->cursor >= 0 && p->cursor < (i64)p->count) ? p->order[p->cursor] : UINT32_MAX;
    u32 w = 0;
    p->cursor = -1;
    for (u32 i = 0; i < p->count; i++) {
        u32 e = p->order[i];
        if (search_q_matches(q, rel_base(L->entries[e].name))) {
            if (e == cur_e) p->cursor = w;
            p->order[w++] = e;
        } else if (p->sel[e]) {
            p->sel[e] = 0;
            p->sel_count--;
        }
    }
    p->count = w;
    if (p->cursor < 0 && w) p->cursor = 0;
    p->anchor = p->cursor;
    snprintf(p->search_q, sizeof p->search_q, "%s", q);
    p->scroll_target = CLAMP(p->scroll_target, 0.0, max_scroll(p));
    p->scroll = CLAMP(p->scroll, 0.0, max_scroll(p));
    A.redraw = true;
}

static void pane_free(Pane *p) {
    pane_drop_prev(p);
    if (p->L_next) {
        atomic_store(&p->L_next->cancel, 1);
        listing_release(p->L_next);
        p->L_next = NULL;
    }
    if (A.drag_pane == p) drag_cancel();
    if (A.edit_pane == p) A.edit_pane = NULL;
    du_view_close(p);
    snapview_close(p);
    if (p->L) {
        atomic_store(&p->L->cancel, 1);
        listing_release(p->L);
    }
    free(p->order);
    free(p->sel);
    for (int i = 0; i < p->hist_n; i++) free(p->hist[i]);
    free(p->hist);
    strv_free(p->select_names, p->nselect_names);
    groups_clear(p);
    free(p->gall);
    strv_free(p->collapsed, p->ncollapsed);
    strv_free(p->expanded, p->nexpanded);
    if (A.ren_pane == p) A.ren_pane = NULL;
    if (A.menu.pane == p) A.menu.open = false;
    if (A.pending_rename_pane == p) A.pending_rename_pane = NULL;
    if (A.drop_pane == p) A.drop_pane = NULL;
    if (A.search_pane == p) A.search_pane = NULL;
    memset(p, 0, sizeof *p);
}

static void tree_clear(Pane *p);

static void pane_navigate(Pane *p, const char *path_in, bool push) {
    A.settings_dirty = true; // session changed
    p->search_q[0] = 0; // leaving the folder ends its filter/search
    if (A.search_pane == p) A.search_pane = NULL;
    if (p->cs_view) { p->view = p->cs_view - 1; p->cs_view = 0; } // contents results switched to Details
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s", path_in);
    path_normalize(path);
    if (strcmp(path, p->path)) folder_view_apply(p, path);
    // Going to an ancestor: preselect the child we came from.
    p->select_name[0] = 0;
    size_t n = strlen(path);
    if (p->path[0] && strcmp(p->path, path)) {
        const char *rest = NULL;
        if (n == 1 && p->path[1]) rest = p->path + 1;
        else if (!strncmp(p->path, path, n) && p->path[n] == '/') rest = p->path + n + 1;
        if (rest) {
            size_t k = strcspn(rest, "/");
            if (k > 2 && !strncmp(rest + k - 2, "::", 2)) k -= 2; // leaving an archive selects the archive file
            k = MIN(k, (size_t)NAME_MAX);
            memcpy(p->select_name, rest, k);
            p->select_name[k] = 0;
        }
    }
    pane_load(p, path);
    recent_push(path);
    if (push) {
        for (int i = p->hist_pos + 1; i < p->hist_n; i++) free(p->hist[i]);
        p->hist_n = p->hist_pos + 1;
        if (p->hist_n == p->hist_cap) {
            p->hist_cap = p->hist_cap ? p->hist_cap * 2 : 32;
            p->hist = xrealloc(p->hist, (size_t)p->hist_cap * sizeof(char *));
        }
        p->hist[p->hist_n++] = strdup(path);
        p->hist_pos = p->hist_n - 1;
    }
    update_title();
    A.redraw = true;
}

static void pane_init(Pane *p, const char *path, int sort_key, bool sort_desc) {
    memset(p, 0, sizeof *p);
    p->view = A.def_view;
    p->icon_px = A.def_icon ? A.def_icon : 96;
    p->group = A.def_group;
    p->tree = A.def_tree;
    p->sort_key = sort_key;
    p->sort_desc = sort_desc;
    p->hist_pos = -1;
    pane_navigate(p, path, true);
}

// Rescan the current folder, then select `names` (and put the cursor on cursor_name).
static void pane_reload(Pane *p, char **names, int n, const char *cursor_name) {
    if (p->L && p->L->is_search) {
        if (p->search_q[0]) {
            char q[256];
            snprintf(q, sizeof q, "%s", p->search_q);
            pane_load_search(p, q);
        }
        return;
    }
    char keep[PATH_MAX] = {0};
    if (cursor_name) snprintf(keep, sizeof keep, "%s", cursor_name);
    char **copy = strv_dup(names, n);
    f64 scroll = p->scroll;
    bool live = p->L && p->order && atomic_load(&p->L->state) != LST_ERROR;
    if (live) { // keep showing the current listing until the new scan is ready
        if (p->L_next) {
            atomic_store(&p->L_next->cancel, 1);
            listing_release(p->L_next);
        }
        p->L_next = listing_new(p->path, p->sort_key, p->sort_desc, A.show_hidden);
        snprintf(p->L_next->filter, sizeof p->L_next->filter, "%s", p->search_q);
        p->L_next->group = pane_group_eff(p);
        p->L_next->tf_mode = p->tf_mode;
        snprintf(p->L_next->tf_exts, sizeof p->L_next->tf_exts, "%s", p->tf_exts);
        p->L_next->tree = pane_tree_string(p);
        listing_start(p->L_next);
        strv_free(p->select_names, p->nselect_names);
        p->select_names = NULL;
        p->nselect_names = 0;
    } else {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s", p->path);
        pane_load(p, path);
    }
    p->reload_no_select = true;
    memcpy(p->select_name, keep, sizeof keep);
    if (copy && n) {
        qsort(copy, (size_t)n, sizeof(char *), cmp_strp);
        p->select_names = copy;
        p->nselect_names = n;
    } else {
        strv_free(copy, n);
    }
    p->scroll = p->scroll_target = scroll;
}

static const char *rel_base(const char *rel);
static void set_zoom_level(Pane *p, int lvl);

static void tree_clear(Pane *p) {
    strv_free(p->expanded, p->nexpanded);
    p->expanded = NULL;
    p->nexpanded = 0;
}

static int tree_find(const Pane *p, const char *rel) {
    for (int i = 0; i < p->nexpanded; i++)
        if (!strcmp(p->expanded[i], rel)) return i;
    return -1;
}

static char **pane_selected_names(Pane *p, int *n);

// Expand (1), collapse (0) or toggle (-1) the folder at display position pos; keeps selection and cursor.
static void tree_set(Pane *p, i64 pos, int want) {
    if (!pane_tree_on(p) || !p->order || pos < 0 || pos >= (i64)p->count) return;
    Entry *e = &p->L->entries[p->order[pos]];
    if (!(e->flags & ENT_DIR)) return;
    int k = tree_find(p, e->name);
    bool open = k >= 0;
    if (want == -1) want = !open;
    if (want == open) return;
    if (want) {
        p->expanded = xrealloc(p->expanded, (size_t)(p->nexpanded + 1) * sizeof(char *));
        p->expanded[p->nexpanded++] = strdup(e->name);
        qsort(p->expanded, (size_t)p->nexpanded, sizeof(char *), cmp_strp);
    } else { // collapse it and everything below it
        size_t l = e->name_len;
        int o = 0;
        for (int i = 0; i < p->nexpanded; i++) {
            char *x = p->expanded[i];
            if (!strcmp(x, e->name) || (!strncmp(x, e->name, l) && x[l] == '/')) free(x);
            else p->expanded[o++] = x;
        }
        p->nexpanded = o;
    }
    char cur[PATH_MAX];
    snprintf(cur, sizeof cur, "%s", e->name);
    int n;
    char **names = pane_selected_names(p, &n);
    pane_reload(p, names, n, cur);
    strv_free(names, n);
    A.redraw = true;
}

static void tree_toggle_mode(Pane *p) {
    p->tree = !p->tree;
    A.def_tree = p->tree;
    A.settings_dirty = true;
    if (p->tree && p->view != VIEW_DETAILS) set_zoom_level(p, 0); // tree view lives in Details
    tree_clear(p);
    char cur[PATH_MAX] = "";
    if (p->cursor >= 0 && p->order) snprintf(cur, sizeof cur, "%s", rel_base(p->L->entries[p->order[p->cursor]].name));
    int n;
    char **names = pane_selected_names(p, &n);
    for (int i = 0; i < n; i++) memmove(names[i], rel_base(names[i]), strlen(rel_base(names[i])) + 1);
    pane_reload(p, names, n, cur[0] ? cur : NULL);
    strv_free(names, n);
    toast(p->tree ? "Tree view on (\xE2\x86\x92 expands, \xE2\x86\x90 collapses)" : "Tree view off");
}

// Indent of a row's icon in the tree view (chevron column + depth).
static f32 tree_indent(const Listing *L, const Entry *e) {
    if (!L->tree) return 0;
    return roundf(S(16)) * (f32)(rel_depth(e->name) + 1);
}

static char **pane_selected_names(Pane *p, int *n) {
    *n = 0;
    if (!p->order || !p->sel_count) return NULL;
    char **v = xmalloc(p->sel_count * sizeof(char *));
    for (u32 i = 0; i < p->count && *n < (int)p->sel_count; i++)
        if (p->sel[p->order[i]]) v[(*n)++] = strdup(p->L->entries[p->order[i]].name);
    return v;
}

// Rescan, keeping selection and cursor by name.
static void pane_refresh(Pane *p) {
    int n;
    char **names = pane_selected_names(p, &n);
    const char *cur = (p->order && p->cursor >= 0) ? p->L->entries[p->order[p->cursor]].name : NULL;
    char curbuf[PATH_MAX];
    if (cur) snprintf(curbuf, sizeof curbuf, "%s", cur);
    pane_reload(p, names, n, cur ? curbuf : NULL);
    p->keep_scroll = true;
    strv_free(names, n);
}

static void pane_history(Pane *p, int delta) {
    int np = p->hist_pos + delta;
    if (np < 0 || np >= p->hist_n) return;
    p->hist_pos = np;
    pane_navigate(p, p->hist[np], false);
}

static void pane_up(Pane *p) {
    if (!p->path[1]) return;
    char parent[PATH_MAX];
    path_join(parent, sizeof parent, p->path, "..");
    pane_navigate(p, parent, true);
}

static void ensure_visible(Pane *p, i64 pos) {
    const PaneLayout *ly = &p->ly;
    int pr = MAX(1, ly->per_row);
    (void)pr;
    f64 ch = ly->cell_h, top = (f64)pos_row(p, pos) * ch, h = ly->list.h, pad = ly->pad;
    if (top < p->scroll_target) p->scroll_target = top;
    else if (top + ch + pad * 2 > p->scroll_target + h) p->scroll_target = top + ch + pad * 2 - h;
    p->scroll_target = CLAMP(p->scroll_target, 0.0, max_scroll(p));
}

static void move_cursor(Pane *p, i64 pos, bool shift, bool ctrl) {
    if (!p->order || !p->count) return;
    pos = CLAMP(pos, 0, (i64)p->count - 1);
    if (shift && p->anchor >= 0) {
        sel_clear(p);
        i64 a = MIN(p->anchor, pos), b = MAX(p->anchor, pos);
        for (i64 i = a; i <= b; i++) sel_set(p, i, true);
    } else {
        if (!ctrl) {
            sel_clear(p);
            sel_set(p, pos, true);
        }
        p->anchor = pos;
    }
    p->cursor = pos;
    ensure_visible(p, pos);
    A.redraw = true;
}

// --- sorting (off the UI thread) -------------------------------------------

typedef struct {
    Pane *pane;
    Listing *L;
    int key;
    bool desc, hidden;
    char filter[256];
    int group;
    int tf_mode;
    char tf_exts[512];
    u32 gen;
    u32 *order;
    u32 count;
} SortReq;

static bool group_collapsed(const Pane *p, const char *label) {
    for (int i = 0; i < p->ncollapsed; i++)
        if (!strcmp(p->collapsed[i], label)) return true;
    return false;
}

static void groups_clear(Pane *p) {
    free(p->row_of);
    free(p->hdr);
    p->row_of = NULL;
    p->hdr = NULL;
    p->nhdr = 0;
    p->nrows = 0;
}

// p->order is a freshly sorted (grouped) order: split it into headers + visible items.
static void groups_build(Pane *p) {
    groups_clear(p);
    free(p->gall);
    p->gall = NULL;
    p->gall_n = 0;
    if (!p->group || p->view != VIEW_DETAILS || !p->L || p->L->is_search || !p->order) return;
    Listing *L = p->L;
    u32 cur_e = p->cursor >= 0 && p->cursor < (i64)p->count ? p->order[p->cursor] : UINT32_MAX;
    u32 anc_e = p->anchor >= 0 && p->anchor < (i64)p->count ? p->order[p->anchor] : UINT32_MAX;
    bool meta = atomic_load(&L->state) == LST_COMPLETE;
    i64 day0 = local_midnight(time(NULL));
    u32 n = p->count;
    p->gall = p->order;
    p->gall_n = n;
    u32 *ord = xmalloc((size_t)MAX(n, 1u) * sizeof(u32)), *row_of = xmalloc((size_t)MAX(n, 1u) * sizeof(u32));
    GroupHdr *hdr = NULL;
    int nh = 0, caph = 0, prev_rank = INT_MIN;
    char prev_label[64] = "";
    u32 rows = 0, m = 0;
    for (u32 i = 0; i < n; i++) {
        u32 ei = p->gall[i];
        char label[64];
        int rk = group_rank(&L->entries[ei], p->group, meta, day0, label, sizeof label);
        if (!nh || rk != prev_rank || strcmp(label, prev_label)) {
            if (nh == caph) {
                caph = caph ? caph * 2 : 16;
                hdr = xrealloc(hdr, (size_t)caph * sizeof(GroupHdr));
            }
            GroupHdr *h = &hdr[nh++];
            h->row = rows++;
            h->count = 0;
            snprintf(h->label, sizeof h->label, "%s", label);
            h->collapsed = group_collapsed(p, label);
            prev_rank = rk;
            snprintf(prev_label, sizeof prev_label, "%s", label);
        }
        GroupHdr *h = &hdr[nh - 1];
        h->count++;
        if (!h->collapsed) {
            ord[m] = ei;
            row_of[m] = rows++;
            m++;
        } else if (p->sel[ei]) {
            p->sel[ei] = 0;
            p->sel_count--;
        }
    }
    p->order = ord;
    p->count = m;
    p->row_of = row_of;
    p->hdr = hdr;
    p->nhdr = nh;
    p->nrows = rows;
    p->cursor = p->anchor = -1;
    for (u32 i = 0; i < m; i++) {
        if (ord[i] == cur_e) p->cursor = i;
        if (ord[i] == anc_e) p->anchor = i;
    }
    if (p->cursor < 0 && m) p->cursor = 0;
}

// Collapse state changed: rebuild from the full order without re-sorting.
static void groups_rebuild(Pane *p) {
    if (!p->gall) return;
    u32 *full = p->gall, fn = p->gall_n;
    p->gall = NULL;
    free(p->order);
    p->order = full;
    p->count = fn;
    groups_build(p);
    p->scroll_target = CLAMP(p->scroll_target, 0.0, max_scroll(p));
    p->scroll = CLAMP(p->scroll, 0.0, max_scroll(p));
    A.redraw = true;
}

static void group_toggle(Pane *p, int h) {
    if (h < 0 || h >= p->nhdr) return;
    const char *label = p->hdr[h].label;
    for (int i = 0; i < p->ncollapsed; i++)
        if (!strcmp(p->collapsed[i], label)) {
            free(p->collapsed[i]);
            p->collapsed[i] = p->collapsed[--p->ncollapsed];
            groups_rebuild(p);
            return;
        }
    p->collapsed = xrealloc(p->collapsed, (size_t)(p->ncollapsed + 1) * sizeof(char *));
    p->collapsed[p->ncollapsed++] = strdup(label);
    groups_rebuild(p);
}

static void sort_done(void *arg) {
    SortReq *r = arg;
    Pane *p = r->pane;
    // r->L is retained, so a live pane holding the same L really is the requester.
    if (pane_exists(p) && p->L == r->L && p->sort_gen == r->gen && p->order) {
        u32 cur_e = p->cursor >= 0 ? p->order[p->cursor] : UINT32_MAX;
        u32 anc_e = p->anchor >= 0 ? p->order[p->anchor] : UINT32_MAX;
        u8 *ns = xcalloc(MAX(p->L->count, 1u), 1);
        u32 nsel = 0;
        p->cursor = p->anchor = -1;
        for (u32 i = 0; i < r->count; i++) {
            u32 e = r->order[i];
            if (p->sel[e]) { ns[e] = 1; nsel++; }
            if (e == cur_e) p->cursor = i;
            if (e == anc_e) p->anchor = i;
        }
        free(p->sel);
        p->sel = ns;
        p->sel_count = nsel;
        free(p->order);
        p->order = r->order;
        p->count = r->count;
        r->order = NULL;
        if (p->cursor < 0 && p->count) p->cursor = 0;
        cmp_filter(p); // folder compare: "Differences only"
        groups_build(p);
        content_rows(p); // contents search: each file's lines go back under it
        if (A.drag_pane == p && A.drag >= DRAG_BAND_PENDING) drag_cancel();
        p->scroll_target = CLAMP(p->scroll_target, 0.0, max_scroll(p));
        p->scroll = CLAMP(p->scroll, 0.0, max_scroll(p));
        p->sort_pending = false;
        A.redraw = true;
    }
    free(r->order);
    listing_release(r->L);
    free(r);
}

static void sort_job(void *arg) {
    SortReq *r = arg;
    bool meta = atomic_load(&r->L->state) == LST_COMPLETE;
    OrderOpts oo = {r->key, r->desc, r->hidden, meta, r->filter, r->group, r->tf_mode, r->tf_exts, r->L->tree != NULL};
    listing_build_order_ex(r->L, &oo, &r->order, &r->count);
    main_post(sort_done, r);
}

static void pane_resort(Pane *p) {
    if (!p->order) return;
    if (p->L->is_search && atomic_load(&p->L->state) != LST_COMPLETE) {
        p->resort_on_complete = true;
        return;
    }
    SortReq *r = xcalloc(1, sizeof *r);
    r->pane = p;
    r->L = p->L;
    listing_retain(p->L);
    r->key = p->sort_key;
    r->desc = p->sort_desc;
    r->hidden = A.show_hidden;
    if (!p->L->is_search) snprintf(r->filter, sizeof r->filter, "%s", p->search_q);
    r->group = !p->L->is_search ? pane_group_eff(p) : GROUP_NONE;
    r->tf_mode = p->tf_mode;
    snprintf(r->tf_exts, sizeof r->tf_exts, "%s", p->tf_exts);
    r->gen = ++p->sort_gen;
    p->sort_pending = true;
    if (p->L->count <= 20000) { // small folder: sort/filter right now, no frame of lag
        bool meta = atomic_load(&r->L->state) == LST_COMPLETE;
        OrderOpts oo = {r->key, r->desc, r->hidden, meta, r->filter, r->group, r->tf_mode, r->tf_exts, r->L->tree != NULL};
    listing_build_order_ex(r->L, &oo, &r->order, &r->count);
        sort_done(r);
        return;
    }
    pool_submit(sort_job, r);
}

static void pane_set_sort(Pane *p, int key) {
    if (p->sort_key == key) p->sort_desc = !p->sort_desc;
    else {
        p->sort_key = key;
        p->sort_desc = key == SORT_DATE || key == SORT_SIZE; // newest / largest first
    }
    folder_view_remember(p);
    pane_resort(p);
}

// Called every frame: adopt finished scans, kick off follow-up sorts.
static void pane_poll(Pane *p) {
    if (p->L_next && p->L_next->is_search) {
        if (atomic_load(&p->L_next->state) == LST_COMPLETE || now_ns() - p->L_next->t_start > 400000000ull) search_swap(p);
    } else if (p->L_next) {
        int s2 = atomic_load(&p->L_next->state);
        // The old listing is complete and still on screen: wait for the new one's details too (sizes, dates, thumbnails
        // would blank out for a moment otherwise), unless that takes long (a huge folder, a slow drive).
        if (s2 == LST_COMPLETE || s2 == LST_ERROR || (s2 == LST_READY && now_ns() - p->L_next->t_start > 1500000000ull)) {
            Listing *old = p->L;
            p->L = p->L_next;
            p->L_next = NULL;
            pane_drop_prev(p);
            if (old) {
                atomic_store(&old->cancel, 1);
                if (!old->is_search && !p->L->is_search && !strcmp(old->path, p->L->path)) p->L_prev = old; // a reload
                else listing_release(old);
            }
            free(p->order);
            free(p->sel);
            p->order = NULL;
            p->sel = NULL;
            p->count = p->sel_count = 0;
            groups_clear(p);
            free(p->gall);
            p->gall = NULL;
            p->sort_pending = false;
            p->complete_seen = false;
            p->prio_first = -1;
            // drags that refer to items of the old listing end; column/scroll/tab/split drags continue
            if (A.drag_pane == p && (A.drag == DRAG_BAND || A.drag == DRAG_BAND_PENDING || A.drag == DRAG_ITEMS ||
                                     A.drag == DRAG_ITEMS_PENDING))
                drag_cancel();
            if (A.ren_pane == p) A.ren_pane = NULL;
            // open menus stay open: their actions use the pane's state at the time of the click
            p->from_reload = true;
        }
    }
    Listing *L = p->L;
    if (!L) return;
    PaneLayout *ly = &p->ly;
    int st = atomic_load(&L->state);
    if ((st == LST_READY || st == LST_COMPLETE) && !p->order) {
        p->order = L->order;
        p->count = L->order_count;
        L->order = NULL;
        p->sel = xcalloc(MAX(L->count, 1u), 1);
        if (L->group == (pane_group_eff(p))) groups_build(p);
        i64 pos = -1, first_sel = -1, ren_pos = -1;
        bool pending = A.pending_rename_pane == p && A.pending_rename[0];
        if (p->select_name[0] || p->nselect_names || pending) {
            for (u32 i = 0; i < p->count; i++) {
                const char *nm = L->entries[p->order[i]].name;
                if (pos < 0 && p->select_name[0] && !strcmp(nm, p->select_name)) pos = i;
                if (p->nselect_names &&
                    bsearch(&nm, p->select_names, (size_t)p->nselect_names, sizeof(char *), cmp_strp)) {
                    sel_set(p, i, true);
                    if (first_sel < 0) first_sel = i;
                }
                if (pending && ren_pos < 0 && !strcmp(nm, A.pending_rename)) ren_pos = i;
            }
        }
        bool focus = ren_pos >= 0 || (!p->keep_scroll && (pos >= 0 || first_sel >= 0));
        p->keep_scroll = false;
        if (ren_pos >= 0) pos = ren_pos;
        else if (pos < 0) pos = first_sel;
        if (pos >= 0 && !p->nselect_names && !p->reload_no_select) sel_set(p, pos, true);
        p->reload_no_select = false;
        if (pos < 0 && p->count) pos = 0;
        p->cursor = p->anchor = pos;
        f64 ptop = (f64)(pos / MAX(1, ly->per_row)) * ly->cell_h;
        if (focus && (ptop < p->scroll || ptop + ly->cell_h > p->scroll + ly->list.h))
            p->scroll = ptop - ly->list.h * 0.5;
        p->scroll = p->scroll_target = CLAMP(p->scroll, 0.0, max_scroll(p));
        strv_free(p->select_names, p->nselect_names);
        p->select_names = NULL;
        p->nselect_names = 0;
        if (pending) {
            A.pending_rename_pane = NULL;
            A.pending_rename[0] = 0;
            if (ren_pos >= 0) {
                sel_clear(p);
                sel_set(p, ren_pos, true);
                begin_rename(p, ren_pos);
            }
        }
        if (L->show_hidden != A.show_hidden || (!L->is_search && strcmp(L->filter, p->search_q)) ||
            L->group != (pane_group_eff(p)) ||
            (!L->is_search && (L->tf_mode != p->tf_mode || strcmp(L->tf_exts, p->tf_exts))))
            pane_resort(p);
        A.redraw = true;
    }
    // Folder sizes first, so a sort by size right below already has the ones known. After a reload the old
    // listing's sizes stand in until measured again: the Size column and bars don't blank out (blink) meanwhile.
    if (st == LST_COMPLETE && p->L_prev) {
        if (A.folder_sizes && !L->is_archive) dirsize_carry(L, p->L_prev);
        pane_drop_prev(p);
    } else if (st == LST_ERROR) pane_drop_prev(p);
    if (st == LST_COMPLETE && A.folder_sizes && !L->is_search && !L->is_archive &&
        (!L->dirsize_started || (A.show_hidden && !L->dirsize_hidden))) {
        bool visible = !L->dirsize_started, hidden = A.show_hidden && !L->dirsize_hidden;
        L->dirsize_started = true;
        L->dirsize_hidden |= hidden;
        if (!path_is_remote(L->path)) dirsize_start(L, visible, hidden); // walking a network share would be slow and costly
    }
    if (st == LST_COMPLETE && !p->complete_seen && p->order) {
        p->complete_seen = true;
        if (L->is_search) {
            if (p->resort_on_complete) pane_resort(p);
        } else if (p->sort_key == SORT_DATE || p->sort_key == SORT_SIZE || p->sort_key >= SORT_CREATED ||
                   p->group == GROUP_DATE || p->group == GROUP_SIZE) {
            pane_resort(p);
        }
        A.redraw = true;
    }
    // The folder vanished while we were looking at it: go to the nearest existing parent.
    if (st == LST_ERROR && p->from_reload && L->err == ENOENT) {
        p->from_reload = false;
        char up[PATH_MAX];
        snprintf(up, sizeof up, "%s", p->path);
        struct stat sst;
        do {
            char tmp[PATH_MAX];
            path_join(tmp, sizeof tmp, up, "..");
            path_normalize(tmp);
            snprintf(up, sizeof up, "%s", tmp);
        } while (strcmp(up, "/") && (stat(up, &sst) || !S_ISDIR(sst.st_mode)));
        pane_navigate(p, up, true);
        return;
    }
    // Ask metadata workers to prioritize what's on screen.
    if (st == LST_READY && p->order && ly->cell_h > 0 && !L->is_search) {
        int pr = MAX(1, ly->per_row);
        i64 first = (i64)MAX(0.0, (p->scroll - ly->pad) / ly->cell_h) * pr;
        if (first != p->prio_first) {
            p->prio_first = first;
            u32 tmp[PRIO_MAX], n = 0;
            i64 rows = ((i64)(ly->list.h / ly->cell_h) + 2) * pr;
            for (i64 i = first; i < (i64)p->count && i < first + rows && n < PRIO_MAX; i++) {
                u32 e = p->order[i];
                if (atomic_load_explicit(&L->entries[e].meta, memory_order_relaxed) != META_READY) tmp[n++] = e;
            }
            if (n) listing_set_prio(L, tmp, n);
        }
    }
}

// --- panels & tabs ---------------------------------------------------------

static Node *node_leaf(Panel *pn) {
    Node *n = xcalloc(1, sizeof *n);
    n->kind = NODE_PANEL;
    n->panel = pn;
    n->ratio = 0.5f;
    return n;
}

static Node *node_find(Node *n, const Panel *pn) {
    if (!n) return NULL;
    if (n->kind == NODE_PANEL) return n->panel == pn ? n : NULL;
    Node *r = node_find(n->a, pn);
    return r ? r : node_find(n->b, pn);
}

static Panel *node_first_panel(Node *n) {
    while (n && n->kind != NODE_PANEL) n = n->a;
    return n ? n->panel : NULL;
}

static void node_free(Node *n) {
    if (!n) return;
    node_free(n->a);
    node_free(n->b);
    free(n);
}

static Panel *panel_new(void) {
    if (A.npanels >= MAX_PANELS) return NULL;
    Panel *pn = xcalloc(1, sizeof *pn);
    A.panels[A.npanels++] = pn;
    return pn;
}

static void panel_activate(Panel *pn) {
    if (!pn || !pn->cur) return;
    if (A.ap != pn) {
        if (A.ap && panel_alive(A.ap)) A.prev_panel = A.ap;
        A.ap = pn;
    }
    A.cur = tab_index(pn->cur);
    if (A.edit_pane && A.edit_pane != active_pane()) A.edit_pane = NULL;
    update_title();
    A.redraw = true;
}

// Remove an (empty) panel; its sibling takes over the space.
static void panel_remove(Panel *pn) {
    Node *n = node_find(A.root, pn);
    if (n && n->parent) {
        Node *par = n->parent, *sib = par->a == n ? par->b : par->a;
        sib->parent = par->parent;
        if (!par->parent) A.root = sib;
        else if (par->parent->a == par) par->parent->a = sib;
        else par->parent->b = sib;
        free(par);
        free(n);
        if (A.ap == pn) A.ap = node_first_panel(sib);
    }
    int k = panel_index(pn);
    if (k >= 0) {
        memmove(&A.panels[k], &A.panels[k + 1], (size_t)(A.npanels - k - 1) * sizeof(Panel *));
        A.npanels--;
    }
    if (A.prev_panel == pn) A.prev_panel = NULL;
    if (A.tdrop_panel == pn) A.tdrop_panel = NULL;
    if (A.ap == pn) A.ap = A.npanels ? A.panels[0] : NULL;
    free(pn);
}

static void tabs_insert(Tab *t, int at) {
    at = CLAMP(at, 0, A.ntabs);
    memmove(&A.tabs[at + 1], &A.tabs[at], (size_t)(A.ntabs - at) * sizeof(Tab *));
    A.tabs[at] = t;
    A.ntabs++;
}

static void tabs_remove(Tab *t) {
    int i = tab_index(t);
    if (i < 0) return;
    memmove(&A.tabs[i], &A.tabs[i + 1], (size_t)(A.ntabs - i - 1) * sizeof(Tab *));
    A.ntabs--;
}

// Nearest other tab in the same panel (prefer the right side).
static Tab *panel_neighbor(const Panel *pn, const Tab *t) {
    int i = tab_index(t);
    for (int j = i + 1; j < A.ntabs; j++)
        if (A.tabs[j]->panel == pn) return A.tabs[j];
    for (int j = i - 1; j >= 0; j--)
        if (A.tabs[j]->panel == pn) return A.tabs[j];
    return NULL;
}

// Index just after the last tab of a panel (where "append" inserts).
static int panel_end_index(const Panel *pn) {
    int last = -1;
    for (int i = 0; i < A.ntabs; i++)
        if (A.tabs[i]->panel == pn) last = i;
    return last < 0 ? A.ntabs : last + 1;
}

static Tab *tab_new_in(Panel *pn, const char *path, int at, bool activate) {
    A.settings_dirty = true;
    if (A.ntabs >= MAX_TABS) { toast("Too many tabs"); return NULL; }
    if (!pn) { // very first tab: create the root panel
        pn = panel_new();
        A.root = node_leaf(pn);
        A.ap = pn;
    }
    Tab *t = xcalloc(1, sizeof *t);
    t->npanes = 1;
    t->panel = pn;
    tabs_insert(t, at);
    if (!pn->cur || activate) pn->cur = t;
    if (activate || !A.ap || !A.ap->cur) panel_activate(pn);
    else A.cur = tab_index(A.ap->cur);
    layout();
    pane_init(&t->pane[0], path, A.def_sort_key, A.def_sort_desc);
    update_title();
    A.redraw = true;
    return t;
}

// New tab in the active panel at a global index (A.cur + 1 = right after the current tab).
static Tab *tab_new(const char *path, int at, bool activate) {
    if (A.ap && at >= A.ntabs) at = panel_end_index(A.ap);
    return tab_new_in(A.ap, path, at, activate);
}

static void closed_push(const char *path) {
    if (A.nclosed == (int)ARRAY_COUNT(A.closed)) {
        free(A.closed[0]);
        memmove(&A.closed[0], &A.closed[1], (ARRAY_COUNT(A.closed) - 1) * sizeof(char *));
        A.nclosed--;
    }
    A.closed[A.nclosed++] = strdup(path);
}

static void tab_close(int i) {
    A.settings_dirty = true;
    if (i < 0 || i >= A.ntabs) return;
    if (A.ntabs == 1) { A.quit = true; return; }
    Tab *t = A.tabs[i];
    Panel *pn = t->panel;
    if (t->term) { // a terminal: its session ends with the tab
        if (A.term_mouse == t->term) A.term_mouse = NULL;
        term_free(t->term);
        t->term = NULL;
    } else closed_push(t->pane[0].path);
    Tab *nb = panel_neighbor(pn, t);
    if (A.menu_tab == t) { A.menu.open = false; A.menu_tab = NULL; }
    if (A.drag_tab == t || A.tdrop_before == t || A.drop_tab == t) drag_cancel();
    pane_free(&t->pane[0]);
    tabs_remove(t);
    free(t);
    if (nb) {
        if (pn->cur == t) pn->cur = nb;
    } else {
        panel_remove(pn); // closing a panel's last tab closes the panel
    }
    A.cur = tab_index(A.ap->cur);
    update_title();
    A.redraw = true;
}

static void tab_select(int i) {
    if (i < 0 || i >= A.ntabs) return;
    Tab *t = A.tabs[i];
    if (i == A.cur && t->panel == A.ap) return;
    t->panel->cur = t;
    panel_activate(t->panel);
    A.edit_pane = NULL;
    drag_cancel();
    A.redraw = true;
}

// Move tab t into panel dst, before tab `before` (NULL = at the end of dst).
static void tab_move(Tab *t, Panel *dst, Tab *before) {
    if (before == t) return;
    Panel *src = t->panel;
    Tab *nb = src != dst ? panel_neighbor(src, t) : NULL;
    tabs_remove(t);
    int at = before ? tab_index(before) : panel_end_index(dst);
    tabs_insert(t, at);
    t->panel = dst;
    dst->cur = t;
    if (src != dst) {
        if (nb) {
            if (src->cur == t) src->cur = nb;
        } else {
            panel_remove(src);
        }
    }
    panel_activate(dst);
    A.settings_dirty = true;
    layout();
}

// Split panel pn: the new panel goes on side `zone` and gets tab `move` (moved) or a new tab at `path`.
static Panel *panel_split(Panel *pn, int zone, Tab *move, const char *path) {
    if (!pn) return NULL;
    Panel *np = panel_new();
    if (!np) { toast("Too many panels"); return NULL; }
    Node *leaf = node_find(A.root, pn);
    Node *old = node_leaf(pn), *nn = node_leaf(np);
    leaf->kind = zone == ZONE_LEFT || zone == ZONE_RIGHT ? NODE_HSPLIT : NODE_VSPLIT;
    leaf->panel = NULL;
    leaf->ratio = 0.5f;
    bool first = zone == ZONE_LEFT || zone == ZONE_TOP;
    leaf->a = first ? nn : old;
    leaf->b = first ? old : nn;
    old->parent = nn->parent = leaf;
    if (move) tab_move(move, np, NULL);
    else tab_new_in(np, path, A.ntabs, true);
    A.settings_dirty = true;
    return np;
}

// Panels in reading order (left-to-right, top-to-bottom).
static int panels_ordered(Node *n, Panel **out, int k) {
    if (!n) return k;
    if (n->kind == NODE_PANEL) { out[k++] = n->panel; return k; }
    k = panels_ordered(n->a, out, k);
    return panels_ordered(n->b, out, k);
}

static void panel_cycle(int dir) {
    Panel *ord[MAX_PANELS];
    int n = panels_ordered(A.root, ord, 0);
    if (n < 2) return;
    int i = 0;
    while (i < n && ord[i] != A.ap) i++;
    panel_activate(ord[(i + dir + n) % n]);
}

// Tab k positions away from the current one within the active panel.
static void tab_cycle(int dir) {
    int n = panel_ntabs(A.ap);
    if (n < 2) return;
    int k = 0;
    for (int i = 0; i < n; i++)
        if (panel_tab(A.ap, i) == A.ap->cur) k = i;
    tab_select(tab_index(panel_tab(A.ap, (k + dir + n) % n)));
}

static void set_active_pane(Pane *p) {
    Tab *t = tab_of(p);
    if (t && t->panel->cur == t && (t->panel != A.ap || tab_index(t) != A.cur)) panel_activate(t->panel);
}

static Pane *pane_at(f32 x, f32 y) {
    for (int i = 0; i < A.npanels; i++) {
        Panel *pn = A.panels[i];
        if (pn->cur && in_rect(pn->cur->pane[0].ly.area, x, y)) return &pn->cur->pane[0];
    }
    return NULL;
}

static Panel *panel_at(f32 x, f32 y) {
    for (int i = 0; i < A.npanels; i++)
        if (in_rect(A.panels[i]->area, x, y)) return A.panels[i];
    return NULL;
}

// Free every tab, panel and node (session restore into a clean window).
static void layout_reset(void) {
    for (int i = 0; i < A.ntabs; i++) {
        pane_free(&A.tabs[i]->pane[0]);
        free(A.tabs[i]);
    }
    A.ntabs = 0;
    A.cur = 0;
    for (int i = 0; i < A.npanels; i++) free(A.panels[i]);
    A.npanels = 0;
    node_free(A.root);
    A.root = NULL;
    A.ap = A.prev_panel = NULL;
    drag_cancel();
}

static void new_window(const char *path) {
    if (A.selftest) { snprintf(A.last_opened, sizeof A.last_opened, "window:%s", path); return; }
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return;
    exe[n] = 0;
    char *argv[] = {exe, "--new-window", (char *)path, NULL};
    pid_t pid;
    posix_spawn(&pid, exe, NULL, spawn_attr(), argv, environ);
}

// --- opening ---------------------------------------------------------------

// No NUL byte in the first 4 KiB: text (files without a known extension: README, scripts, dotfiles).
static bool file_looks_text(const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return false;
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof buf);
    close(fd);
    return n > 0 && !memchr(buf, 0, (size_t)n);
}

// The default app of a local file, or NULL (folders and links go to xdg-open).
static DesktopApp *open_app_for(const char *full) {
    struct stat st;
    bool remote = path_is_remote(full); // (no stat / read on the UI thread there: a stalled server would freeze it)
    if (strstr(full, "://") || (!remote && (stat(full, &st) || !S_ISREG(st.st_mode)))) return NULL;
    const char *mime = mime_for_file(path_basename(full), false);
    DesktopApp *a = strcmp(mime, "application/octet-stream") ? app_default_for(mime) : NULL;
    if (!a && !remote && file_looks_text(full)) a = app_default_for("text/plain"); // (shell scripts, unknown text types)
    return a && strcmp(a->id, "dev.fileexplorer.desktop") ? a : NULL;
}

static void open_path(const char *full) {
    if (A.selftest) { snprintf(A.last_opened, sizeof A.last_opened, "%s", full); return; }
    // files: the default app from the desktop entries ourselves first. xdg-open outside GNOME / KDE / Xfce ignores
    // Terminal=true (a console editor such as nvim starts with no window: nothing happens) and sends unknown types
    // to the browser.
    DesktopApp *a = open_app_for(full);
    char *files[] = {(char *)full};
    if (a && app_launch(a, files, 1)) return;
    char *argv[] = {"xdg-open", (char *)full, NULL};
    pid_t pid;
    if (in_path("xdg-open") && posix_spawnp(&pid, "xdg-open", NULL, spawn_attr(), argv, environ) == 0) return;
    // no xdg-utils (minimal installs): links and folders from the desktop entries too
    const char *mime;
    if (!strncmp(full, "https://", 8)) mime = "x-scheme-handler/https";
    else if (!strncmp(full, "http://", 7)) mime = "x-scheme-handler/http";
    else {
        struct stat st;
        mime = mime_for_file(path_basename(full), stat(full, &st) == 0 && S_ISDIR(st.st_mode));
    }
    a = app_default_for(mime);
    if (!a) toast("No app is set to open %s (use Open with)", path_basename(full));
    else if (!app_launch(a, files, 1)) toast("Couldn't start %s", a->name);
}

static void arc_extract_open(Pane *p, const char *name);

static void open_entry(Pane *p, i64 pos, bool new_tab) {
    if (!p->order || pos < 0 || pos >= p->count) return;
    Entry *e = &p->L->entries[p->order[pos]];
    if (e->flags & ENT_MATCH) { content_open_line(p, pos); return; } // a matching line: the editor at that line
    char full[PATH_MAX];
    path_join(full, sizeof full, p->path, e->name);
    if (!(e->flags & ENT_DIR) && picker_open_file(p, full)) return; // picking: the file is the answer
    if (!(e->flags & ENT_DIR) && pane_is_archive(p)) { arc_extract_open(p, e->name); return; }
    if (!(e->flags & ENT_DIR) && !p->L->is_search && is_archive_name(e->name) && !new_tab) { // browse it like a folder
        char vp[PATH_MAX + 4];
        snprintf(vp, sizeof vp, "%s::", full);
        pane_navigate(p, vp, true);
        return;
    }
    if (e->flags & ENT_DIR) {
        if (new_tab) tab_new(full, A.cur + 1, false);
        else pane_navigate(p, full, true);
    } else open_path(full);
}

static void type_to_jump(Pane *p, const char *text) {
    u64 t = now_ns();
    if (t - A.jump_t > 900000000ull) A.jump[0] = 0;
    A.jump_t = t;
    size_t jl = strlen(A.jump);
    snprintf(A.jump + jl, sizeof A.jump - jl, "%s", text);
    jl = strlen(A.jump);
    if (!p->order || !p->count) return;
    // repeating one letter cycles through matches
    bool cycle = jl > 1;
    for (size_t i = 1; i < jl && cycle; i++) cycle = A.jump[i] == A.jump[0];
    size_t nl = cycle ? 1 : jl;
    i64 start = p->cursor < 0 ? 0 : p->cursor + (nl == 1 ? 1 : 0);
    for (u32 k = 0; k < p->count; k++) {
        i64 i = (start + k) % p->count;
        if (str_iprefix(rel_base(p->L->entries[p->order[i]].name), A.jump, nl)) {
            move_cursor(p, i, false, false);
            return;
        }
    }
}

static void begin_edit(Pane *p, const char *initial) {
    A.edit_pane = p;
    A.ren_pane = NULL;
    if (initial) te_set(&A.edit, initial, (int)strlen(initial), (int)strlen(initial));
    else te_set(&A.edit, p->path, 0, (int)strlen(p->path));
    A.redraw = true;
}

static void commit_edit(void) {
    Pane *p = A.edit_pane;
    if (!p) return;
    char path[PATH_MAX];
    const char *s = A.edit.buf;
    while (*s == ' ') s++;
    const char *sep = strstr(s, "://");
    if (sep && sep > s && strncmp(s, "file://", 7)) { // smb://, sftp://, ftp://, dav://...: mount with GVfs
        bool scheme = true;
        for (const char *c = s; c < sep; c++) scheme &= isalnum((u8)*c) || *c == '+' || *c == '-';
        if (scheme) {
            A.edit_pane = NULL;
            NetAddr na;
            if (net_parse(s, &na) && na.proto >= 0 && strcmp(g_net_protos[na.proto].scheme, "nfs")) {
                // a known protocol: the Connect dialog, prefilled, so a password can be typed
                A.edit_pane = NULL;
                connect_open_addr(&na);
                return;
            }
            toast("Connecting to %s\xE2\x80\xA6", s);
            dev_start_net(s, "", "", "", false, false);
            return;
        }
    }
    if (!strncmp(s, "file://", 7) && uri_to_path(s, path, sizeof path)) {
        A.edit_pane = NULL;
        pane_navigate(p, path, true);
        return;
    }
    if (s[0] == '~') {
        const char *home = getenv("HOME");
        snprintf(path, sizeof path, "%s%s", home ? home : "", s + 1);
    } else if (s[0] == '/') {
        snprintf(path, sizeof path, "%s", s);
    } else {
        path_join(path, sizeof path, p->path, s);
    }
    path_normalize(path);
    A.edit_pane = NULL;
    struct stat st;
    if (stat(path, &st) == 0 && !S_ISDIR(st.st_mode)) open_path(path);
    else pane_navigate(p, path, true);
}

// --- rubber-band selection -------------------------------------------------

static void band_update(Pane *p) {
    if (!p->order || !p->count) return;
    PaneLayout *ly = &p->ly;
    int pr = MAX(1, ly->per_row);
    f64 ch = ly->cell_h;
    f64 y1 = (f64)CLAMP(A.my, ly->list.y, ly->list.y + ly->list.h) - ly->list.y + p->scroll;
    f64 lo = MIN(A.band_y0, y1) - ly->pad, hi = MAX(A.band_y0, y1) - ly->pad;
    i64 nrows = item_rows(p);
    i64 r0 = MAX((i64)floor(lo / ch), 0), r1 = MIN((i64)floor(hi / ch), nrows - 1);
    i64 c0 = 0, c1 = 0;
    if (pr > 1) {
        f32 x1 = CLAMP(A.mx, ly->list.x, ly->track.x);
        f32 xa = MIN(A.band_x0, x1) - ly->list.x - ly->pad, xb = MAX(A.band_x0, x1) - ly->list.x - ly->pad;
        c0 = MAX((i64)floorf(xa / ly->cell_w), 0);
        c1 = MIN((i64)floorf(xb / ly->cell_w), (i64)pr - 1);
        if (xb < 0) c1 = -1;
    }
    // re-evaluate every row that is or was inside the band
    i64 ua = A.band_r1 >= A.band_r0 ? MIN(A.band_r0, r0) : r0, ub = A.band_r1 >= A.band_r0 ? MAX(A.band_r1, r1) : r1;
    if (p->nhdr) {
        i64 last = -1, firstsel = -1;
        for (i64 k = grp_first_pos(p, MAX(ua, 0)); k < (i64)p->count && (i64)p->row_of[k] <= ub; k++) {
            i64 row = p->row_of[k];
            bool in = row >= r0 && row <= r1;
            sel_set(p, k, in || (A.band_base && A.band_base[p->order[k]]));
            if (in) { if (firstsel < 0) firstsel = k; last = k; }
        }
        A.band_r0 = r0;
        A.band_r1 = r1;
        if (last >= 0) {
            bool down = y1 >= A.band_y0;
            p->cursor = down ? last : firstsel;
            p->anchor = down ? firstsel : last;
        }
        A.redraw = true;
        return;
    }
    for (i64 row = MAX(ua, 0); row <= ub && row < nrows; row++)
        for (i64 c = 0; c < pr; c++) {
            i64 i = row * pr + c;
            if (i >= (i64)p->count) break;
            bool in = row >= r0 && row <= r1 && c >= c0 && c <= c1;
            sel_set(p, i, in || (A.band_base && A.band_base[p->order[i]]));
        }
    A.band_r0 = r0;
    A.band_r1 = r1;
    if (r1 >= r0 && c1 >= c0) {
        bool down = y1 >= A.band_y0;
        i64 last = MIN((down ? r1 : r0) * pr + (down ? c1 : c0), (i64)p->count - 1);
        i64 first_i = MIN((down ? r0 : r1) * pr + (down ? c0 : c1), (i64)p->count - 1);
        p->cursor = last;
        p->anchor = first_i;
    }
    A.redraw = true;
}

static i64 item_at(Pane *p, f32 x, f32 y);
// ui_tools.c (included near the end of the drawing code)
static bool sheet_open(void);
static void sheet_key(SDL_Keycode k, SDL_Keymod mod);
static void sheet_text(const char *text);
static void sheet_mouse(SDL_MouseButtonEvent *b);
static void sheet_wheel(f32 dy);
static void sheet_draw(void);
static bool sheet_busy(void);
static void openwith_open(char **paths, int n);
static void options_open(void);
static void ipc_handle(char *msg);
static bool exe_link_replaced(char *link);
static bool options_is_open(void);
enum { OC_APPEARANCE, OC_BEHAVIOUR, OC_VIEWS, OC_KEYBOARD, OC_LAYOUTS, OC_INTEGRATION, OC_ADVANCED, OC_COUNT }; // Options pages
static void options_open_page(int cat);
typedef struct {
    char name[64];
    char *text; // session lines
    int npanels, ntabs;
} SavedLayout;
#define MAX_LAYOUTS 64
static SavedLayout g_layouts[MAX_LAYOUTS];
static int g_nlayouts;
static bool layout_open(int i);
static bool sheet_button(Rect r, const char *label, bool primary, bool enabled);
static void props_open(char **paths, int n);
static void rename_open(char **paths, int n, Pane *p);
static int rename_many(char **from, char **to, int n);
static void menu_open_types(Pane *p, f32 x, f32 y);
static void tf_toggle(Pane *p, const char *ext);
static void trash_restore(Pane *p, bool all);
static void trash_delete(Pane *p, bool all);
static char g_tf_keys[30][20]; // extensions offered in the Types popup ("" = no extension)
static char g_tf_labels[30][48];
static int g_ntf;
static void tilde_path(const char *path, char *out, size_t cap);

static void pin_toggle(const char *path);
static int pin_index(const char *path);
static void set_zoom_level(Pane *p, int lvl);
static void zoom_step(Pane *p, int dir);
static int zoom_level_of(const Pane *p);
static void pane_target_folder(Pane *p, char *out, size_t cap);
static void preview_toggle(void);

// --- toast -----------------------------------------------------------------

static void toast(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(A.toast, sizeof A.toast, fmt, ap);
    va_end(ap);
    A.toast_t = now_ns();
    A.redraw = true;
}

// --- clipboard (interoperable with GNOME/KDE file managers) ----------------

static struct {
    char **paths, **sorted;
    int n;
    bool cut;
    char *uri, *gnome, *plain;
} g_clip;

static const char *g_clip_mimes[] = {"x-special/gnome-copied-files", "text/uri-list", "application/x-kde-cutselection",
                                     "text/plain;charset=utf-8", "text/plain", "UTF8_STRING"};

static void clip_free_data(void) {
    strv_free(g_clip.paths, g_clip.n);
    strv_free(g_clip.sorted, g_clip.n);
    free(g_clip.uri);
    free(g_clip.gnome);
    free(g_clip.plain);
    memset(&g_clip, 0, sizeof g_clip);
}

static const void *clip_cb(void *ud, const char *mime, size_t *size) {
    (void)ud;
    const char *d = NULL;
    if (!mime || !g_clip.n) d = NULL;
    else if (!strcmp(mime, "x-special/gnome-copied-files")) d = g_clip.gnome;
    else if (!strcmp(mime, "text/uri-list")) d = g_clip.uri;
    else if (!strcmp(mime, "application/x-kde-cutselection")) d = g_clip.cut ? "1" : "0";
    else d = g_clip.plain;
    *size = d ? strlen(d) : 0;
    return d;
}

static void clip_set(char **paths, int n, bool cut) {
    clip_free_data();
    if (!n) return;
    g_clip.paths = strv_dup(paths, n);
    g_clip.sorted = strv_dup(paths, n);
    qsort(g_clip.sorted, (size_t)n, sizeof(char *), cmp_strp);
    g_clip.n = n;
    g_clip.cut = cut;
    size_t cap = 16;
    for (int i = 0; i < n; i++) cap += strlen(paths[i]) * 3 + 16;
    g_clip.uri = xmalloc(cap);
    g_clip.gnome = xmalloc(cap);
    g_clip.plain = xmalloc(cap);
    size_t u = 0, g = (size_t)sprintf(g_clip.gnome, "%s", cut ? "cut" : "copy"), t = 0;
    for (int i = 0; i < n; i++) {
        char enc[PATH_MAX * 3];
        url_encode_path(paths[i], enc, sizeof enc);
        u += (size_t)sprintf(g_clip.uri + u, "file://%s\r\n", enc);
        g += (size_t)sprintf(g_clip.gnome + g, "\nfile://%s", enc);
        t += (size_t)sprintf(g_clip.plain + t, "%s%s", i ? "\n" : "", paths[i]);
    }
    SDL_SetClipboardData(clip_cb, NULL, NULL, g_clip_mimes, ARRAY_COUNT(g_clip_mimes));
}

static void clip_clear(void) {
    bool had = g_clip.n > 0;
    clip_free_data();
    if (had) SDL_ClearClipboardData();
}

static bool uri_to_path(const char *uri, char *out, size_t cap) {
    if (strncmp(uri, "file://", 7)) return false;
    const char *p = uri + 7;
    if (*p != '/') {
        p = strchr(p, '/'); // skip host
        if (!p) return false;
    }
    size_t o = 0;
    for (; *p && o + 1 < cap; p++) {
        if (p[0] == '%' && isxdigit((u8)p[1]) && isxdigit((u8)p[2])) {
            char h[3] = {p[1], p[2], 0};
            out[o++] = (char)strtol(h, NULL, 16);
            p += 2;
        } else out[o++] = *p;
    }
    out[o] = 0;
    return o > 0;
}

// Files currently on the clipboard (system clipboard first, our own copy as fallback).
static char **clip_get(int *n, bool *cut) {
    *n = 0;
    *cut = false;
    size_t sz = 0;
    char *d = NULL;
    bool gnome = false;
    if (SDL_HasClipboardData("x-special/gnome-copied-files")) {
        d = SDL_GetClipboardData("x-special/gnome-copied-files", &sz);
        gnome = true;
    } else if (SDL_HasClipboardData("text/uri-list")) {
        d = SDL_GetClipboardData("text/uri-list", &sz);
        if (SDL_HasClipboardData("application/x-kde-cutselection")) {
            size_t ks = 0;
            char *k = SDL_GetClipboardData("application/x-kde-cutselection", &ks);
            if (k && ks && k[0] == '1') *cut = true;
            SDL_free(k);
        }
    }
    if (!d) {
        if (g_clip.n && !SDL_HasClipboardText()) {
            *n = g_clip.n;
            *cut = g_clip.cut;
            return strv_dup(g_clip.paths, g_clip.n);
        }
        return NULL;
    }
    char *buf = xmalloc(sz + 1);
    memcpy(buf, d, sz);
    buf[sz] = 0;
    SDL_free(d);
    int cap = 16;
    char **v = xmalloc((size_t)cap * sizeof(char *));
    bool first = true;
    for (char *line = strtok(buf, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        if (gnome && first) {
            first = false;
            if (!strcmp(line, "cut")) { *cut = true; continue; }
            if (!strcmp(line, "copy")) continue;
        }
        first = false;
        if (line[0] == '#') continue;
        char path[PATH_MAX];
        if (!uri_to_path(line, path, sizeof path)) continue;
        if (*n == cap) { cap *= 2; v = xrealloc(v, (size_t)cap * sizeof(char *)); }
        v[(*n)++] = strdup(path);
    }
    free(buf);
    if (!*n) { free(v); return NULL; }
    return v;
}

// An image on the clipboard (a screenshot, "Copy image" in a browser): the first type we know, in order of preference.
static const struct { const char *mime, *ext; } g_clip_images[] = {
    {"image/png", "png"}, {"image/jpeg", "jpg"}, {"image/webp", "webp"}, {"image/gif", "gif"}, {"image/avif", "avif"},
    {"image/bmp", "bmp"}, {"image/tiff", "tiff"}, {"image/svg+xml", "svg"},
};

static int clip_image_type(void) {
    size_t nm = 0;
    char **mt = SDL_GetClipboardMimeTypes(&nm);
    int best = -1;
    for (size_t i = 0; mt && i < nm; i++)
        for (int k = 0; k < (int)ARRAY_COUNT(g_clip_images); k++)
            if (!strcmp(mt[i], g_clip_images[k].mime) && (best < 0 || k < best)) best = k;
    SDL_free(mt);
    return best;
}

static bool clip_has_files(void) {
    return g_clip.n || SDL_HasClipboardData("x-special/gnome-copied-files") || SDL_HasClipboardData("text/uri-list");
}

static void start_transfer(char **paths, int n, const char *dst, bool move, bool from_cut);

// Folder for pasted images; removes the folders of earlier pastes, emptied by their move (rmdir keeps any that aren't).
static void clip_paste_tidy(char *dir, size_t cap) {
    const char *rt = getenv("XDG_RUNTIME_DIR");
    snprintf(dir, cap, "%s/file-explorer/paste", rt && rt[0] ? rt : "/tmp");
    DIR *d = opendir(dir);
    for (struct dirent *de; d && (de = readdir(d));)
        if (de->d_name[0] != '.') { char q[PATH_MAX]; path_join(q, sizeof q, dir, de->d_name); rmdir(q); }
    if (d) closedir(d);
}

// Pasting an image: its bytes go to a new file in a private folder, which is then moved into `dst` like any
// other pasted file (conflicts, progress, undo, remote folders and phones all work the same).
static bool clip_paste_image(const char *dst) {
    int k = clip_image_type();
    if (k < 0) return false;
    size_t sz = 0;
    void *d = SDL_GetClipboardData(g_clip_images[k].mime, &sz);
    if (!d || !sz) { SDL_free(d); return false; }
    char dir[PATH_MAX];
    clip_paste_tidy(dir, sizeof dir);
    mkdir_p(dir, 0700);
    strncat(dir, "/XXXXXX", sizeof dir - strlen(dir) - 1);
    if (!mkdtemp(dir)) { SDL_free(d); toast("Couldn't paste the image: %s", strerror(errno)); return true; }
    char name[96], path[PATH_MAX];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    size_t o = strftime(name, sizeof name, "Pasted image %Y-%m-%d %H-%M-%S", &tm);
    snprintf(name + o, sizeof name - o, ".%s", g_clip_images[k].ext);
    path_join(path, sizeof path, dir, name);
    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(d, 1, sz, f) == sz;
    if (f && fclose(f)) ok = false;
    SDL_free(d);
    if (!ok) { unlink(path); rmdir(dir); toast("Couldn't paste the image"); return true; }
    char *one[1] = {path};
    start_transfer(one, 1, dst, true, false);
    return true;
}

static bool clip_is_cut(const char *dir, const char *name) {
    if (!g_clip.cut || !g_clip.n) return false;
    char full[PATH_MAX];
    path_join(full, sizeof full, dir, name);
    const char *k = full;
    return bsearch(&k, g_clip.sorted, (size_t)g_clip.n, sizeof(char *), cmp_strp) != NULL;
}

// --- refreshing panes after changes ----------------------------------------

static void parent_of(const char *path, char *out, size_t cap) {
    char base[NAME_MAX + 1];
    path_split(path, out, cap, base, sizeof base);
}

// Panes that show the parent of a path in `items` are reloaded with those items selected;
// panes showing one of `dirs` are refreshed keeping their selection.
static void refresh_after(char **items, int nitems, char **dirs, int ndirs) {
    for (int t = 0; t < A.ntabs; t++)
        for (int j = 0; j < A.tabs[t]->npanes; j++) {
            Pane *p = &A.tabs[t]->pane[j];
            int nsel = 0;
            char **sel = nitems ? xmalloc((size_t)nitems * sizeof(char *)) : NULL;
            for (int i = 0; i < nitems; i++) {
                if (!items[i]) continue;
                char par[PATH_MAX];
                parent_of(items[i], par, sizeof par);
                if (!strcmp(par, p->path)) sel[nsel++] = (char *)path_basename(items[i]);
            }
            bool hit = nsel > 0;
            for (int i = 0; i < ndirs && !hit; i++)
                if (dirs[i] && !strcmp(dirs[i], p->path)) hit = true;
            if (nsel) pane_reload(p, sel, nsel, sel[0]);
            else if (hit) pane_refresh(p);
            free(sel);
        }
}

// --- undo ------------------------------------------------------------------

static void undo_free(UndoRec *u) {
    strv_free(u->a, u->n);
    strv_free(u->b, u->n);
    strv_free(u->c, u->n);
    memset(u, 0, sizeof *u);
}

static void undo_push(int kind, char **a, char **b, char **c, int n) {
    if (A.nundo == UNDO_MAX) {
        undo_free(&A.undo[0]);
        memmove(&A.undo[0], &A.undo[1], (UNDO_MAX - 1) * sizeof(UndoRec));
        A.nundo--;
    }
    UndoRec *u = &A.undo[A.nundo++];
    u->kind = kind;
    u->n = n;
    u->a = strv_dup(a, n);
    u->b = strv_dup(b, n);
    u->c = strv_dup(c, n);
}

static void ops_start(Op *op);

static void do_undo(void) {
    if (!A.nundo) { toast("Nothing to undo"); return; }
    UndoRec u = A.undo[--A.nundo];
    switch (u.kind) {
    case U_RENAME: {
        int done = rename_many(u.b, u.a, u.n);
        if (done) {
            refresh_after(u.a, u.n, NULL, 0);
            toast(u.n == 1 ? "Undid rename" : "Undid rename of %d items", u.n);
        }
        break;
    }
    case U_MOVE:
    case U_TRASH: {
        Op *op = op_new(OP_RESTORE, u.b, u.n, NULL);
        op->dst = strv_dup(u.a, u.n);
        op->aux_in = strv_dup(u.c, u.n);
        op->is_undo = true;
        ops_start(op);
        toast(u.kind == U_TRASH ? "Restoring from trash\xE2\x80\xA6" : "Moving back\xE2\x80\xA6");
        break;
    }
    case U_COPY: {
        Op *op = op_new(OP_TRASH, u.b, u.n, NULL);
        op->is_undo = true;
        ops_start(op);
        toast("Moving the copies to the trash\xE2\x80\xA6");
        break;
    }
    case U_CREATE: {
        struct stat st;
        char par[PATH_MAX];
        parent_of(u.b[0], par, sizeof par);
        char *dirs[1] = {par};
        bool empty = lstat(u.b[0], &st) == 0 && (S_ISDIR(st.st_mode) ? rmdir(u.b[0]) == 0 : (st.st_size == 0 && unlink(u.b[0]) == 0));
        if (!empty) {
            Op *op = op_new(OP_TRASH, u.b, 1, NULL);
            op->is_undo = true;
            ops_start(op);
        }
        refresh_after(NULL, 0, dirs, 1);
        toast("Undid create");
        break;
    }
    }
    undo_free(&u);
}

// --- operations: start / finish --------------------------------------------

static bool op_is_transfer(const Op *op) { return op->kind == OP_COPY || op->kind == OP_MOVE || op->kind == OP_SYNC; }

// Paused / resumed from the queue or a card (copies, moves, syncs, deletes; not the administrator helper's).
static bool op_pausable(const Op *op) {
    return !op->admin && (op_is_transfer(op) || op->kind == OP_DELETE || op->kind == OP_TRASH) && atomic_load(&op->phase) != OPS_DONE;
}

static void ops_start(Op *op) {
    if (A.nops >= MAX_OPS) {
        toast("Too many operations running");
        op_free(op);
        return;
    }
    if (op_is_transfer(op)) {
        op->queued = !op->admin; // (the helper runs on its own)
        atomic_store(&op->verify, A.verify_copies);
    }
    A.ops[A.nops++] = op;
    op_launch(op);
    A.redraw = true;
}

// Measure the trash in the background (after anything that may have changed it); a request while measuring
// runs again once that finishes.
static void trash_size_refresh(void) {
    if (A.trash_job) { A.trash_again = true; return; }
    char tf[PATH_MAX], ti[PATH_MAX];
    trash_dirs(tf, ti, sizeof tf);
    char *p[1] = {tf};
    A.trash_job = stats_start(p, 1);
}

static void trash_size_poll(void) {
    if (!A.trash_job || !atomic_load(&A.trash_job->done)) return;
    A.trash_size = atomic_load(&A.trash_job->size);
    A.trash_known = true;
    stats_release(A.trash_job);
    A.trash_job = NULL;
    A.redraw = true;
    if (A.trash_again) { A.trash_again = false; trash_size_refresh(); }
}

static void op_finished(void *arg) {
    Op *op = arg;
    op->finished = true;
    trash_size_refresh(); // trashed, deleted, restored, emptied...
    du_after_op(op);
    cmp_after_op(op);
    if (op->dst_dir) dscache_invalidate_up(op->dst_dir);
    for (int i = 0; i < op->nsrc; i++) dscache_invalidate_up(op->src[i]);
    for (int i = 0; i < op->nout; i++)
        if (op->out[i]) dscache_invalidate_up(op->out[i]);
    int nd = 0;
    char **dirs = xmalloc((size_t)(op->nsrc + 2) * sizeof(char *));
    if (op->dst_dir) dirs[nd++] = strdup(op->dst_dir);
    for (int i = 0; i < op->nsrc; i++) {
        char par[PATH_MAX];
        parent_of(op->src[i], par, sizeof par);
        dirs[nd++] = strdup(par);
    }
    bool select_out = op->kind == OP_COPY || op->kind == OP_MOVE || op->kind == OP_RESTORE || op->kind == OP_EXTRACT ||
                      op->kind == OP_COMPRESS;
    if (op->open_after && op->nout && op->out[0] && !op->nerr) open_path(op->out[0]);
    refresh_after(select_out ? op->out : NULL, select_out ? op->nout : 0, dirs, nd);
    strv_free(dirs, nd);
    if (!op->is_undo && !op->admin && op->nout) {
        if (op->kind == OP_COPY || op->kind == OP_EXTRACT || op->kind == OP_COMPRESS)
            undo_push(U_COPY, op->in, op->out, NULL, op->nout); // undo = move the created files to the trash
        else if (op->kind == OP_MOVE) undo_push(U_MOVE, op->in, op->out, NULL, op->nout);
        else if (op->kind == OP_TRASH) undo_push(U_TRASH, op->in, op->out, op->aux, op->nout);
    }
    if (op->from_cut && !op->nerr && !atomic_load(&op->cancel)) clip_clear();
    A.redraw = true;
}

static void dialog_items(char **paths, int n, bool full);

// --- running as administrator (pkexec + this program's --admin-op helper, see ops.c) ---

static bool op_admin_ok(int kind) {
    return kind == OP_COPY || kind == OP_MOVE || kind == OP_DELETE || kind == OP_TRASH || kind == OP_SYNC || kind == OP_RENAME ||
           kind == OP_MKDIR || kind == OP_MKFILE;
}

static bool admin_allowed(void) { // tests only ever use a stand-in pkexec
    if (A.selftest && !getenv("FE_PKEXEC")) { toast("pkexec is not run in tests"); return false; }
    return true;
}

static void admin_delete_cb(int choice, bool check, void *ud) {
    (void)check;
    Op *op = ud;
    if (choice == 0) ops_start(op);
    else op_free(op);
}

// Run what failed again as the administrator (what already worked isn't repeated). Trash -> delete: the system's
// files can't go to your Trash, so that one asks first.
static void op_retry_admin(Op *op) {
    if (!admin_allowed()) return;
    int n = 0;
    char **src = xmalloc((size_t)MAX(op->nsrc, 1) * sizeof(char *)), **dst = xmalloc((size_t)MAX(op->nsrc, 1) * sizeof(char *));
    for (int i = 0; i < op->nsrc; i++) {
        bool done = false;
        for (int k = 0; k < op->nout && !done; k++) done = op->in[k] && !strcmp(op->in[k], op->src[i]);
        if (done) continue;
        src[n] = op->src[i];
        dst[n++] = op->dst ? op->dst[i] : NULL;
    }
    if (!n) { free(src); free(dst); op->dismissed = true; return; }
    int kind = op->kind == OP_TRASH ? OP_DELETE : op->kind;
    Op *r = op_new(kind, src, n, op->dst_dir);
    if (op->dst) r->dst = strv_dup(dst, n);
    r->admin = true;
    r->policy = op->policy == CF_REPLACE || op->answer == CF_REPLACE ? CF_REPLACE : CF_SKIP;
    op->dismissed = true;
    if (op->kind == OP_TRASH) {
        char msg[600];
        if (n == 1) snprintf(msg, sizeof msg, "\xE2\x80\x9C%s\xE2\x80\x9D belongs to the system and can't go to your Trash. Delete it permanently as the administrator?", path_basename(src[0]));
        else snprintf(msg, sizeof msg, "These %d items belong to the system and can't go to your Trash. Delete them permanently as the administrator?", n);
        static const char *b[] = {"Delete", "Cancel"};
        dialog_open("Delete as administrator", msg, b, 2, 0, 1, NULL, admin_delete_cb, r);
        A.dlg.icon = DI_DANGER;
        A.dlg.danger = true;
        dialog_items(src, n, true);
    } else ops_start(r);
    free(src);
    free(dst);
}

// A rename or a new item that wasn't allowed: offer it as the administrator.
static Op *g_admin_pending;
static void admin_simple_cb(int choice, bool check, void *ud) {
    (void)check;
    (void)ud;
    Op *op = g_admin_pending;
    g_admin_pending = NULL;
    if (!op) return;
    if (choice == 0 && admin_allowed()) ops_start(op);
    else op_free(op);
}

static void admin_offer(int kind, const char *a, const char *b, const char *what, int err) {
    char *s1[1] = {(char *)a}, *s2[1] = {(char *)b};
    if (g_admin_pending) op_free(g_admin_pending);
    g_admin_pending = op_new(kind, s1, 1, NULL);
    if (b) g_admin_pending->dst = strv_dup(s2, 1);
    g_admin_pending->admin = true;
    char msg[600];
    snprintf(msg, sizeof msg, "%s: %s. The folder belongs to another user or the system; you can do it as the administrator (asks for the password).",
             what, strerror(err));
    static const char *btn[] = {"Do it as administrator", "Cancel"};
    dialog_open("Not allowed", msg, btn, 2, 0, 1, NULL, admin_simple_cb, NULL);
    A.dlg.icon = DI_WARN;
}

static void op_cancel(Op *op) {
    atomic_store(&op->cancel, 1);
    pthread_mutex_lock(&op->mtx);
    pthread_cond_broadcast(&op->cv);
    pthread_mutex_unlock(&op->mtx);
}

// --- transfer queue: turns, pause, order, speed ---

static void op_set_paused(Op *op, bool on) {
    if (!op_pausable(op)) return;
    pthread_mutex_lock(&op->mtx);
    atomic_store(&op->paused, on);
    pthread_cond_broadcast(&op->cv);
    pthread_mutex_unlock(&op->mtx);
    op->rate_t = 0; // the speed starts over from the next sample
    A.redraw = true;
}

// Waiting transfers get their turn in queue order while fewer than "transfers at once" are running (paused ones
// don't hold a turn). Runs every frame; the op thread wakes the UI when it starts waiting.
static void ops_schedule(void) {
    int running = 0, limit = A.transfers_at_once > 0 ? A.transfers_at_once : MAX_OPS;
    for (int i = 0; i < A.nops; i++) {
        Op *op = A.ops[i];
        if (op->go && atomic_load(&op->phase) != OPS_DONE && !atomic_load(&op->paused)) running++; // (holds its turn as soon as given)
    }
    for (int i = 0; i < A.nops && running < limit; i++) {
        Op *op = A.ops[i];
        if (op->go || atomic_load(&op->phase) != OPS_QUEUED || atomic_load(&op->paused)) continue;
        pthread_mutex_lock(&op->mtx);
        op->go = true;
        pthread_cond_broadcast(&op->cv);
        pthread_mutex_unlock(&op->mtx);
        running++;
        A.redraw = true;
    }
}

// Place in the queue: earlier rows run first (moving a waiting transfer up makes it next).
static void op_move_in_queue(int i, int dir) {
    int j = i + dir;
    if (i < 0 || j < 0 || i >= A.nops || j >= A.nops) return;
    Op *t = A.ops[i];
    A.ops[i] = A.ops[j];
    A.ops[j] = t;
    if (A.q_sel == i) A.q_sel = j;
    A.redraw = true;
}

static void queue_toggle(void) {
    A.q_open = !A.q_open;
    if (A.q_open) { // start on the first one still going
        A.q_sel = 0;
        while (A.q_sel < A.nops - 1 && atomic_load(&A.ops[A.q_sel]->phase) == OPS_DONE) A.q_sel++;
    }
    A.redraw = true;
}

// Position among the transfers waiting for a turn (1 = next), 0 when it isn't waiting.
static int op_queue_pos(const Op *op) {
    int k = 0;
    for (int i = 0; i < A.nops; i++) {
        const Op *o = A.ops[i];
        if (atomic_load(&o->phase) == OPS_QUEUED && !o->go) k++;
        if (o == op) return atomic_load(&o->phase) == OPS_QUEUED && !o->go ? k : 0;
    }
    return 0;
}

// The work an op has done and has to do, in bytes (verifying reads every copied byte again).
static void op_work(const Op *op, u64 *done, u64 *total) {
    u64 bt = atomic_load(&op->bytes_total), bd = atomic_load(&op->bytes_done);
    bool v = atomic_load(&op->phase) >= OPS_RUNNING ? op->verify_on : atomic_load(&op->verify);
    *done = bd + (v ? atomic_load(&op->bytes_verified) : 0);
    *total = v ? bt * 2 : bt;
}

// How far along an op is, 0..1: by bytes (copy + verify), or by items when there are no bytes to count (deleting).
static f64 op_frac(const Op *op) {
    if (atomic_load(&op->phase) == OPS_DONE) return 1.0;
    u32 ft = atomic_load(&op->files_total), fd = atomic_load(&op->files_done);
    u64 wd, wt;
    op_work(op, &wd, &wt);
    f64 f = op->kind == OP_DELETE || !wt ? (ft ? (f64)fd / ft : 0.0) : (f64)wd / (f64)wt;
    return CLAMP(f, 0.0, 1.0);
}

// Speed (bytes/s) and time left (s) of a running op, smoothed over half-second samples; false until it's known
// (still counting, waiting, paused, or nothing measured yet).
static bool op_speed(Op *op, u64 now, f64 *speed, f64 *eta) {
    u64 done, total;
    op_work(op, &done, &total);
    if (atomic_load(&op->phase) != OPS_RUNNING || atomic_load(&op->paused)) { op->rate_t = 0; return false; }
    if (!op->rate_t) { op->rate_t = now; op->rate_work = done; }
    f64 dt = (now - op->rate_t) / 1e9;
    if (dt >= 0.5) {
        f64 r = (f64)(done - op->rate_work) / dt;
        op->rate = op->rate > 0 ? op->rate * 0.7 + r * 0.3 : r;
        op->rate_t = now;
        op->rate_work = done;
    }
    if (op->rate <= 0) { // first half second: the average since it started running
        f64 el = op->t_run ? (now - op->t_run) / 1e9 : 0;
        if (el < 0.2 || !done) return false;
        *speed = (f64)done / el;
    } else *speed = op->rate;
    *eta = *speed > 1 && total > done ? (f64)(total - done) / *speed : 0;
    return true;
}

// "3 min left", "1 h 5 min left", "12 s left"
static void fmt_eta(char *out, size_t cap, f64 s) {
    if (s < 1) snprintf(out, cap, "almost done");
    else if (s < 60) snprintf(out, cap, "%d s left", (int)s);
    else if (s < 3600) snprintf(out, cap, "%d min left", (int)(s / 60 + 0.5));
    else snprintf(out, cap, "%d h %d min left", (int)(s / 3600), (int)fmod(s / 60, 60));
}

// Status line of an op for its card and its queue row: progress, speed, time left, or why it isn't moving.
static void op_detail(Op *op, u64 now, char *det, size_t cap) {
    int ph = atomic_load(&op->phase);
    bool done = ph == OPS_DONE;
    u64 bt = atomic_load(&op->bytes_total), bd = atomic_load(&op->bytes_done);
    u32 ft = atomic_load(&op->files_total), fd = atomic_load(&op->files_done);
    bool by_items = op->kind == OP_DELETE || !bt;
    int pct = (int)(op_frac(op) * 100);
    const char *vnote = atomic_load(&op->verify) ? "  \xC2\xB7  will be verified" : "";
    char a[32], b[32];
    fmt_size(a, sizeof a, bd);
    fmt_size(b, sizeof b, bt);
    u32 nv = atomic_load(&op->files_verified);
    if (done && op->nerr) snprintf(det, cap, "%s", op->err);
    else if (done && nv) snprintf(det, cap, "Done in %.1f s  \xC2\xB7  %u file%s verified", (op->t_end - op->t_start) / 1e9, nv, nv == 1 ? "" : "s");
    else if (done) snprintf(det, cap, "Done in %.1f s", (op->t_end - op->t_start) / 1e9);
    else if (ph == OPS_COUNTING) snprintf(det, cap, "Preparing\xE2\x80\xA6 %u items", ft);
    else if (ph == OPS_QUEUED && !op->go) {
        if (atomic_load(&op->paused)) snprintf(det, cap, "Paused before starting  \xC2\xB7  %s%s", b, vnote);
        else snprintf(det, cap, "Waiting for its turn (#%d in line)  \xC2\xB7  %s%s", op_queue_pos(op), b, vnote);
    } else if (atomic_load(&op->paused)) {
        if (by_items) snprintf(det, cap, "Paused  \xC2\xB7  %d%%  \xC2\xB7  %u of %u items", pct, fd, ft);
        else snprintf(det, cap, "Paused  \xC2\xB7  %d%%  \xC2\xB7  %s of %s", pct, a, b);
    } else {
        char cur[NAME_MAX + 1], sp[32], eta[48];
        pthread_mutex_lock(&op->mtx);
        snprintf(cur, sizeof cur, "%s", op->cur);
        pthread_mutex_unlock(&op->mtx);
        f64 speed = 0, left = 0;
        bool known = op_speed(op, now, &speed, &left);
        if (by_items) {
            snprintf(det, cap, "%d%%  \xC2\xB7  %u of %u items  \xC2\xB7  %s", pct, fd, ft, cur);
        } else {
            const char *what = atomic_load(&op->verifying) ? "verifying " : "";
            int k = snprintf(det, cap, "%d%%  \xC2\xB7  %s of %s  \xC2\xB7  ", pct, a, b);
            if (known) { // (nothing until it has been measured)
                fmt_size(sp, sizeof sp, (u64)speed);
                fmt_eta(eta, sizeof eta, left);
                k += snprintf(det + k, cap - (size_t)k, "%s/s  \xC2\xB7  %s  \xC2\xB7  ", sp, eta);
            }
            snprintf(det + k, cap - (size_t)k, "%s%s", what, cur);
        }
    }
}

static bool op_card_visible(Op *op, u64 now) {
    if (atomic_load(&op->phase) != OPS_DONE) return now - op->t_start > 250000000ull;
    if (op->nerr) return true;
    return op->t_end - op->t_start > 250000000ull && now - op->t_end < 2500000000ull;
}

static void ops_gc(void) {
    u64 now = now_ns();
    int j = 0, sel = A.q_sel, nsel = sel;
    for (int i = 0; i < A.nops; i++) {
        Op *op = A.ops[i];
        bool rm = op->finished && (op->dismissed || (!op->nerr && !op_card_visible(op, now)));
        if (rm) {
            if (A.dlg.open && A.dlg.ud == op) A.dlg.open = false;
            if (i < sel) nsel--; // the highlighted queue row stays on the same op
            op_free(op);
        } else {
            A.ops[j++] = op;
        }
    }
    A.nops = j;
    A.q_sel = MAX(nsel, 0);
}

// A card with a moving bar on screen: frames at display rate so it slides instead of stepping.
static bool ops_moving(void) {
    u64 now = now_ns();
    for (int i = 0; i < A.nops; i++)
        if (!A.ops[i]->dismissed && (atomic_load(&A.ops[i]->phase) == OPS_COUNTING || atomic_load(&A.ops[i]->phase) == OPS_RUNNING) &&
            !atomic_load(&A.ops[i]->paused) && (A.q_open || op_card_visible(A.ops[i], now))) return true; // (not waiting or paused)
    return false;
}

static bool ops_busy(void) {
    if (A.toast[0] && now_ns() - A.toast_t < 3000000000ull) return true;
    return A.nops > 0;
}

// --- dialogs ---------------------------------------------------------------

static void dialog_items_free(Dialog *d) {
    for (int i = 0; i < d->nitems; i++) { free(d->items[i]); free(d->item_sub[i]); d->item_sub[i] = NULL; }
    d->rich = false;
    d->nitems = d->more = 0;
}

// List up to 4 of these names in the dialog (call after dialog_open); full: each with its full path beside it.
static void dialog_items(char **paths, int n, bool full) {
    Dialog *d = &A.dlg;
    dialog_items_free(d);
    for (int i = 0; i < n && d->nitems < 4; i++) {
        struct stat st;
        d->item_dir[d->nitems] = lstat(paths[i], &st) == 0 && S_ISDIR(st.st_mode);
        if (full) {
            char tp[PATH_MAX];
            tilde_path(paths[i], tp, sizeof tp);
            d->item_sub[d->nitems] = strdup(tp);
        }
        d->items[d->nitems++] = strdup(paths[i][1] ? path_basename(paths[i]) : "/");
    }
    d->more = n - d->nitems;
}

static void dialog_open(const char *title, const char *msg, const char **buttons, int nb, int def, int cancel,
                        const char *check_label, DialogFn fn, void *ud) {
    Dialog *d = &A.dlg;
    dialog_items_free(d);
    memset(d, 0, sizeof *d);
    d->open = true;
    snprintf(d->title, sizeof d->title, "%s", title);
    snprintf(d->msg, sizeof d->msg, "%s", msg);
    for (int i = 0; i < nb && i < 4; i++) d->buttons[i] = buttons[i];
    d->nbuttons = MIN(nb, 4);
    d->def = d->focus = def;
    d->cancel = cancel;
    d->has_check = check_label != NULL;
    d->check_label = check_label;
    d->fn = fn;
    d->ud = ud;
    A.menu.open = false;
    A.redraw = true;
}

static void dialog_choose(int i) {
    Dialog d = A.dlg;
    g_dlg_opt = d.opt;
    A.dlg.open = false;
    dialog_items_free(&A.dlg);
    A.redraw = true;
    if (d.fn) d.fn(i, d.check, d.ud);
}

// The conflict question: what happens to items already at the destination. Asked once for all the conflicts found
// before starting ("Apply to all" / "One by one"), or for one item at a time.
static const int g_cf_choice[5] = {CF_SKIP, CF_REPLACE, CF_RENAME, CF_NEWER, CF_KEEP_BOTH};

static void conflict_cb(int choice, bool check, void *ud) {
    Op *op = ud;
    pthread_mutex_lock(&op->mtx);
    bool batch = op->conflict_batch, cancel = choice == A.dlg.cancel;
    int pick = g_cf_choice[CLAMP(g_dlg_opt, 0, 4)];
    if (cancel) op->answer = CF_CANCEL;
    else if (batch && choice == 1) op->answer = CF_ASK; // one by one
    else op->answer = pick;
    op->answer_all = batch || check;
    op->conflict_shown = false;
    pthread_cond_broadcast(&op->cv);
    pthread_mutex_unlock(&op->mtx);
    if (!cancel && !(batch && choice == 1)) A.conflict_last = pick;
}

static void check_conflicts(void) {
    if (A.dlg.open) return;
    for (int i = 0; i < A.nops; i++) {
        Op *op = A.ops[i];
        pthread_mutex_lock(&op->mtx);
        bool ask = op->conflict_pending && !op->conflict_shown && op->answer < 0;
        char name[NAME_MAX + 1];
        bool dir = op->conflict_dir, batch = op->conflict_batch;
        int left = op->conflicts_left;
        if (ask) {
            op->conflict_shown = true;
            snprintf(name, sizeof name, "%s", op->conflict_name);
        }
        pthread_mutex_unlock(&op->mtx);
        if (!ask) continue;
        // (cf_* were written before the op thread asked; it waits until answered)
        int n = batch ? op->ncf : 1, nnewer = 0, ndirs = 0;
        for (int k = 0; k < op->ncf; k++) { nnewer += op->cf_newer[k] > 0; ndirs += op->cf_dirs[k]; }
        if (!batch) { nnewer = -1; ndirs = dir; }
        bool all_dirs = ndirs == n;
        const char *where = op->dst_dir[1] ? path_basename(op->dst_dir) : "/";
        char title[200], msg[700];
        if (n == 1) snprintf(title, sizeof title, dir ? "Folder already exists" : "File already exists");
        else snprintf(title, sizeof title, "%d items already exist", n);
        if (n == 1) snprintf(msg, sizeof msg, "\xE2\x80\x9C%s\xE2\x80\x9D is already in \xE2\x80\x9C%s\xE2\x80\x9D. What should happen to it?", name, where);
        else snprintf(msg, sizeof msg, "These are already in \xE2\x80\x9C%s\xE2\x80\x9D. What should happen to them?", where);
        static const char *bb[] = {"Apply to all", "One by one", "Cancel"};
        static const char *b1[] = {"Apply", "Cancel"};
        bool multi = batch && n > 1;
        dialog_open(title, msg, multi ? bb : b1, multi ? 3 : 2, 0, multi ? 2 : 1, NULL, conflict_cb, op);
        Dialog *d = &A.dlg;
        d->icon = DI_QUESTION;
        if (!batch && left > 0) { // one by one: the rest can get the same answer
            snprintf(d->check_buf, sizeof d->check_buf, "Do this for the %d remaining conflict%s", left, left == 1 ? "" : "s");
            d->has_check = true;
            d->check_label = d->check_buf;
        }
        // the names, each with how its date compares to what's there
        char **paths = xmalloc((size_t)MAX(n, 1) * sizeof(char *));
        for (int k = 0; k < n; k++) {
            char full[PATH_MAX];
            path_join(full, sizeof full, op->dst_dir, batch ? op->cf_names[k] : name);
            paths[k] = strdup(full);
        }
        dialog_items(paths, n, false);
        strv_free(paths, n);
        if (batch)
            for (int k = 0; k < d->nitems; k++)
                d->item_sub[k] = strdup(op->cf_newer[k] > 0 ? "newer than what's there" : op->cf_newer[k] < 0 ? "older than what's there" : "same date as what's there");
        static const char *labels[5] = {"Skip", "Overwrite", "Rename existing", "Newer only", "Keep both"};
        for (int k = 0; k < 5; k++) d->opts[k] = labels[k];
        if (all_dirs) d->opts[1] = "Merge";
        d->nopts = 5;
        snprintf(d->opt_desc[0], sizeof d->opt_desc[0], "Leave what's there; don't %s %s", op->kind == OP_MOVE ? "move" : "copy", n == 1 ? "this one" : "these");
        snprintf(d->opt_desc[1], sizeof d->opt_desc[1], all_dirs ? "Put the contents together, replacing files with the same name" : ndirs ? "Replace what's there (folders merge)" : "Replace what's there");
        snprintf(d->opt_desc[2], sizeof d->opt_desc[2], "What's there now gets a new name, like \xE2\x80\x9C%s (2)\xE2\x80\x9D", n == 1 ? name : "name");
        if (nnewer >= 0 && !ndirs && n == 1) snprintf(d->opt_desc[3], sizeof d->opt_desc[3], nnewer ? "This one is newer, so it replaces what's there" : "What's there is as new or newer, so it stays");
        else if (nnewer >= 0 && !ndirs) snprintf(d->opt_desc[3], sizeof d->opt_desc[3], "Replace only what's older (%d of %d)", nnewer, n);
        else snprintf(d->opt_desc[3], sizeof d->opt_desc[3], "Replace only what's older%s", ndirs ? "; inside folders, file by file" : "");
        snprintf(d->opt_desc[4], sizeof d->opt_desc[4], "The new %s a name like \xE2\x80\x9C%s (2)\xE2\x80\x9D", n == 1 ? "one gets" : "ones get", n == 1 ? name : "name");
        d->opt = 4;
        for (int k = 0; k < 5; k++)
            if (g_cf_choice[k] == A.conflict_last) d->opt = k;
        return;
    }
}

static void delete_confirm_cb(int choice, bool check, void *ud) {
    (void)check;
    Op *op = ud;
    if (choice == 0) ops_start(op);
    else op_free(op);
}

// --- actions ---------------------------------------------------------------

static char **pane_selection_paths(Pane *p, int *n) {
    *n = 0;
    if (pane_du_on(p)) return du_selection_paths(p, n); // disk usage: the item picked in the map
    if (!p->order) return NULL;
    bool use_cursor = !p->sel_count && p->cursor >= 0;
    u32 cnt = use_cursor ? 1 : p->sel_count;
    if (!cnt) return NULL;
    char **v = xmalloc(cnt * sizeof(char *));
    char full[PATH_MAX];
    if (use_cursor) {
        path_join(full, sizeof full, p->path, p->L->entries[p->order[p->cursor]].name);
        v[(*n)++] = strdup(full);
        return v;
    }
    const char *last = NULL; // contents search: a file and its selected lines are one path
    for (u32 i = 0; i < p->count && *n < (int)cnt; i++) {
        if (!p->sel[p->order[i]]) continue;
        const Entry *e = &p->L->entries[p->order[i]];
        if (e->name == last) continue;
        last = e->name;
        path_join(full, sizeof full, p->path, e->name);
        v[(*n)++] = strdup(full);
    }
    return v;
}

static void start_transfer(char **paths, int n, const char *dst, bool move, bool from_cut) {
    if (!n) return;
    Op *op = op_new(move ? OP_MOVE : OP_COPY, paths, n, dst);
    op->from_cut = from_cut;
    ops_start(op);
}

static void begin_rename(Pane *p, i64 pos) {
    if (!p->order || pos < 0 || pos >= p->count) return;
    if (p->L->entries[p->order[pos]].flags & ENT_MATCH) { // a matching line: rename its file
        u32 fe = content_file_of(p->L, p->order[pos]);
        for (pos = 0; pos < (i64)p->count && p->order[pos] != fe; pos++) {}
        if (pos >= (i64)p->count) return;
    }
    Entry *e = &p->L->entries[p->order[pos]];
    A.ren_pane = p;
    A.ren_L = p->L;
    A.ren_entry = p->order[pos];
    const char *base = rel_base(e->name); // tree view: rename within the subfolder
    int boff = (int)(base - e->name);
    int stem = (int)e->name_len - boff;
    if (!(e->flags & ENT_DIR) && e->ext) stem = e->ext - 1 - boff;
    te_set(&A.ren_te, base, 0, stem);
    A.edit_pane = NULL;
    p->cursor = p->anchor = pos;
    ensure_visible(p, pos);
    A.redraw = true;
}

static void commit_rename(void) {
    Pane *p = A.ren_pane;
    A.ren_pane = NULL;
    A.redraw = true;
    if (!p || p->L != A.ren_L || !pane_exists(p)) return;
    Entry *e = &p->L->entries[A.ren_entry];
    char nn[PATH_MAX];
    snprintf(nn, sizeof nn, "%s", A.ren_te.buf);
    const char *base = rel_base(e->name);
    if (!strcmp(nn, base)) return;
    if (!nn[0] || strchr(nn, '/') || !strcmp(nn, ".") || !strcmp(nn, "..") || strlen(nn) > NAME_MAX) {
        toast("\xE2\x80\x9C%s\xE2\x80\x9D isn't a valid name", nn);
        return;
    }
    char from[PATH_MAX], to[PATH_MAX], dir[PATH_MAX];
    path_join(from, sizeof from, p->path, e->name);
    parent_of(from, dir, sizeof dir);
    path_join(to, sizeof to, dir, nn);
    if (rename_noreplace(AT_FDCWD, from, AT_FDCWD, to)) {
        int e = errno;
        char what[NAME_MAX * 2 + 64];
        snprintf(what, sizeof what, "Can't rename \xE2\x80\x9C%s\xE2\x80\x9D", base);
        if (e == EACCES || e == EPERM) admin_offer(OP_RENAME, from, to, what, e);
        else toast("Can't rename: %s", e == EEXIST ? "an item with that name already exists" : strerror(e));
        return;
    }
    char *a[1] = {from}, *b[1] = {to};
    undo_push(U_RENAME, a, b, NULL, 1);
    refresh_after(b, 1, NULL, 0);
}

static void new_item(Pane *p, bool folder) {
    int dfd = open(p->path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) { toast("Can't open folder: %s", strerror(errno)); return; }
    const char *base = folder ? "New folder" : "New file";
    char name[NAME_MAX + 1];
    snprintf(name, sizeof name, "%s", base);
    if (exists_at(dfd, name)) unique_name(dfd, base, true, false, name, sizeof name);
    int r;
    if (folder) r = mkdirat(dfd, name, 0777);
    else {
        int fd = openat(dfd, name, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0666);
        r = fd < 0 ? -1 : close(fd);
    }
    int e = errno;
    close(dfd);
    if (r) {
        char full[PATH_MAX], what[64];
        path_join(full, sizeof full, p->path, name);
        snprintf(what, sizeof what, "Can't create a %s here", folder ? "folder" : "file");
        if (e == EACCES || e == EPERM) admin_offer(folder ? OP_MKDIR : OP_MKFILE, full, NULL, what, e);
        else toast("Can't create %s: %s", folder ? "folder" : "file", strerror(e));
        return;
    }
    char full[PATH_MAX];
    path_join(full, sizeof full, p->path, name);
    char *b[1] = {full};
    undo_push(U_CREATE, NULL, b, NULL, 1);
    A.pending_rename_pane = p;
    snprintf(A.pending_rename, sizeof A.pending_rename, "%s", name);
    refresh_after(b, 1, NULL, 0);
}

// --- terminals: local, or SSH into the server an SFTP folder lives on ------------------------------------

// Is path inside an SFTP connection? GVfs (ssh_target_gvfs) or sshfs mounts: /proc/self/mounts source
// "user@host:/dir". Fills the server and the remote folder.
static bool ssh_target_for(const char *path, SshTarget *t) {
    if (ssh_target_gvfs(path, t)) return true;
    for (int k = 0; k < A.nnets; k++) { // sshfs: label is the source "user@host:/dir"
        const char *mp = A.nets[k].path, *src = A.nets[k].label, *colon = strchr(src, ':');
        size_t ml = strlen(mp);
        if (!colon || strncmp(path, mp, ml) || (path[ml] && path[ml] != '/') || strstr(src, "://")) continue;
        const char *at = memchr(src, '@', (size_t)(colon - src));
        if (at) snprintf(t->user, sizeof t->user, "%.*s", (int)(at - src), src);
        snprintf(t->host, sizeof t->host, "%.*s", (int)(colon - (at ? at + 1 : src)), at ? at + 1 : src);
        char base[PATH_MAX];
        snprintf(base, sizeof base, "%s", colon[1] ? colon + 1 : ".");
        snprintf(t->path, sizeof t->path, "%s%s", base, path + ml);
        return t->host[0] != 0;
    }
    return false;
}

static bool find_terminal(char *exe, size_t cap, const char **name) {
    // $TERMINAL, then xdg-terminal-exec (the user's preferred terminal, where set up), then the desktops' own
    static const char *cands[] = {NULL, "xdg-terminal-exec", "ptyxis", "kgx", "gnome-terminal", "konsole", "cosmic-term",
                                  "xfce4-terminal", "mate-terminal", "qterminal", "lxterminal", "tilix", "terminator",
                                  "ghostty", "kitty", "alacritty", "foot", "wezterm", "rio", "st", "urxvt", "xterm"};
    cands[0] = getenv("TERMINAL");
    const char *path_env = getenv("PATH");
    for (size_t i = 0; i < ARRAY_COUNT(cands); i++) {
        if (!cands[i] || !cands[i][0] || !path_env) continue;
        char pe[4096];
        snprintf(pe, sizeof pe, "%s", path_env);
        for (char *d = strtok(pe, ":"); d; d = strtok(NULL, ":")) {
            snprintf(exe, cap, "%s/%s", d, cands[i]);
            if (access(exe, X_OK) == 0) { *name = path_basename(exe); return true; }
        }
    }
    return false;
}

// Start the terminal in dir, optionally running cmd (argv, NULL-terminated) instead of a shell.
static void spawn_terminal(const char *dir, char *const cmd[]) {
    char exe[PATH_MAX];
    const char *name;
    if (!find_terminal(exe, sizeof exe, &name)) { toast("No terminal found (set $TERMINAL)"); return; }
    char *argv[32];
    int n = 0;
    argv[n++] = exe;
    char joined[PATH_MAX * 2] = "";
    if (cmd) { // how each terminal takes a command to run
        if (!strcmp(name, "wezterm")) { argv[n++] = "start"; argv[n++] = "--"; }
        else if (!strcmp(name, "kgx") || !strcmp(name, "gnome-terminal") || !strcmp(name, "ptyxis")) argv[n++] = "--";
        else if (!strcmp(name, "xfce4-terminal") || !strcmp(name, "mate-terminal") || !strcmp(name, "terminator")) argv[n++] = "-x";
        else if (!strcmp(name, "tilix") || !strcmp(name, "qterminal") || !strcmp(name, "lxterminal")) { // one string
            size_t o = 0;
            for (int i = 0; cmd[i]; i++) {
                char q[PATH_MAX + 64];
                sh_quote(q, sizeof q, cmd[i]);
                o += (size_t)snprintf(joined + o, sizeof joined - o, "%s%s", i ? " " : "", q);
            }
            argv[n++] = "-e";
            argv[n++] = joined;
            cmd = NULL;
        } else if (strcmp(name, "kitty") && strcmp(name, "foot") && strcmp(name, "xdg-terminal-exec"))
            argv[n++] = "-e"; // konsole, cosmic-term, alacritty, ghostty, rio, st, urxvt, xterm, other $TERMINAL (xdg-terminal-exec takes the command as is)
        for (int i = 0; cmd && cmd[i] && n < 30; i++) argv[n++] = cmd[i];
    }
    argv[n] = NULL;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (dir) posix_spawn_file_actions_addchdir_np(&fa, dir);
    pid_t pid;
    posix_spawn(&pid, exe, &fa, spawn_attr(), argv, environ);
    posix_spawn_file_actions_destroy(&fa);
}

// ssh to the server, starting the user's login shell in the remote folder (keys, ~/.ssh/config, agent and host-key
// checks are ssh's own; a password, if the server wants one, is typed in the terminal).
static void ext_ssh_terminal(const SshTarget *t) {
    char dest[400], rcmd[PATH_MAX * 2], qp[PATH_MAX + 64];
    snprintf(dest, sizeof dest, "%s%s%s", t->user, t->user[0] ? "@" : "", t->host);
    sh_quote(qp, sizeof qp, t->path[0] ? t->path : ".");
    snprintf(rcmd, sizeof rcmd, "cd %s 2>/dev/null || cd; exec \"${SHELL:-/bin/sh}\" -l", qp);
    char *cmd[8];
    int n = 0;
    cmd[n++] = "ssh";
    cmd[n++] = "-t";
    if (t->port[0]) { cmd[n++] = "-p"; cmd[n++] = (char *)t->port; }
    cmd[n++] = dest;
    cmd[n++] = rcmd;
    cmd[n] = NULL;
    if (A.selftest) { // record what would run
        size_t o = (size_t)snprintf(A.last_opened, sizeof A.last_opened, "ssh:");
        for (int i = 1; cmd[i] && o < sizeof A.last_opened; i++) o += (size_t)snprintf(A.last_opened + o, sizeof A.last_opened - o, "%s%s", i > 1 ? " " : "", cmd[i]);
        return;
    }
    spawn_terminal(NULL, cmd);
}

static void ext_terminal_local(const char *dir) {
    if (A.selftest) { snprintf(A.last_opened, sizeof A.last_opened, "terminal:%s", dir); return; }
    spawn_terminal(dir, NULL);
}

// The external terminal app (right-click > Open in external terminal): ssh for SFTP folders, else local.
static void ext_terminal(const char *dir) {
    SshTarget t;
    if (ssh_target_for(dir, &t)) ext_ssh_terminal(&t);
    else ext_terminal_local(dir);
}

// --- built-in terminal tabs ---

// A terminal tab right after the current one: the shell in dir, or argv (ssh ...).
static Tab *term_tab_open(const char *dir, char *const argv[]) {
    const char *home = getenv("HOME") ? getenv("HOME") : "/";
    Tab *t = tab_new(dir && dir[0] == '/' && !path_is_remote(dir) ? dir : home, A.cur + 1, true);
    if (!t) return NULL;
    t->term = term_new(dir && !path_is_remote(dir) ? dir : home, argv, 24, 80);
    if (!t->term) { tab_close(tab_index(t)); return NULL; }
    A.redraw = true;
    return t;
}

static void ssh_tab_open(const SshTarget *st) {
    char dest[400], rcmd[PATH_MAX * 2], qp[PATH_MAX + 64];
    snprintf(dest, sizeof dest, "%s%s%s", st->user, st->user[0] ? "@" : "", st->host);
    sh_quote(qp, sizeof qp, st->path[0] ? st->path : ".");
    snprintf(rcmd, sizeof rcmd, "cd %s 2>/dev/null || cd; exec \"${SHELL:-/bin/sh}\" -l", qp);
    const char *ssh = getenv("FE_SSH"); // tests: a stand-in
    char *argv[8];
    int n = 0;
    argv[n++] = (char *)(ssh && ssh[0] ? ssh : "ssh");
    argv[n++] = "-t";
    if (st->port[0]) { argv[n++] = "-p"; argv[n++] = (char *)st->port; }
    argv[n++] = dest;
    argv[n++] = rcmd;
    argv[n] = NULL;
    term_tab_open(NULL, argv);
}

// F4: a terminal tab - in an SFTP folder an SSH session on that server (same folder), elsewhere the shell here.
static void open_terminal(const char *dir) {
    SshTarget t;
    if (ssh_target_for(dir, &t)) ssh_tab_open(&t);
    else term_tab_open(dir, NULL);
}

static Term *active_term(void) { return A.ap && A.ap->cur ? A.ap->cur->term : NULL; }

static void arc_action(Pane *p, int act);
static void qa_action(int act);
static void menu_add(const char *label, const char *shortcut, int act, bool enabled);

static void menu_terminal_items(Pane *p) {
    SshTarget t;
    if (ssh_target_for(p->path, &t)) {
        menu_add("Open SSH terminal here", "F4", ACT_TERMINAL, true);
        menu_add("Open local terminal here", "", ACT_TERMINAL_LOCAL, true);
    } else menu_add("Open terminal here", "F4", ACT_TERMINAL, true);
    menu_add("Open in external terminal", "", ACT_TERMINAL_EXTERNAL, true);
}

// Where Ctrl+V puts things. Tree view: into the selected folder, or the subfolder a selected file sits in (rows are
// relative paths); otherwise (and with nothing selected) the pane's own folder.
static void paste_dir(Pane *p, char *out, size_t cap) {
    snprintf(out, cap, "%s", p->path);
    if (!pane_tree_on(p) || !p->L || !p->L->tree || !p->order || p->cursor < 0 || p->cursor >= (i64)p->count) return;
    if (!p->sel_count || !p->sel[p->order[p->cursor]]) return;
    const Entry *e = &p->L->entries[p->order[p->cursor]];
    path_join(out, cap, p->path, e->name);
    if (!(e->flags & ENT_DIR)) {
        char *sl = strrchr(e->name, '/') ? strrchr(out, '/') : NULL;
        if (sl) *sl = 0;
        else snprintf(out, cap, "%s", p->path);
    }
}

static void do_action(Pane *p, int act) {
    int n = 0;
    char **paths = NULL;
    if (pane_is_archive(p)) {
        switch (act) {
        case ACT_CUT: case ACT_COPY: case ACT_PASTE: case ACT_RENAME: case ACT_TRASH: case ACT_DELETE:
        case ACT_NEW_FOLDER: case ACT_NEW_FILE: case ACT_BATCH_RENAME: case ACT_OPEN_WITH: case ACT_PROPERTIES:
            toast("Archives are read-only here \xE2\x80\x94 drag items out or use Extract");
            return;
        default: break;
        }
    }
    if (act >= ACT_ARC_BASE && act < ACT_ARC_BASE + 8) { arc_action(p, act); return; }
    switch (act) {
    case ACT_OPEN: open_entry(p, p->cursor, false); break;
    case ACT_OPEN_TAB: open_entry(p, p->cursor, true); break;
    case ACT_CUT:
    case ACT_COPY:
        paths = pane_selection_paths(p, &n);
        if (n) {
            clip_set(paths, n, act == ACT_CUT);
            toast("%s %d item%s", act == ACT_CUT ? "Cut" : "Copied", n, n == 1 ? "" : "s");
        }
        break;
    case ACT_PASTE: {
        bool cut;
        char dst[PATH_MAX];
        paste_dir(p, dst, sizeof dst);
        paths = clip_get(&n, &cut);
        for (int i = 0; i < n; i++) // the copied folder itself is selected: paste next to it, not into it
            if (!strcmp(paths[i], dst) && strcmp(dst, p->path)) { *strrchr(dst, '/') = 0; break; }
        if (n) start_transfer(paths, n, dst, cut, cut);
        else if (!clip_paste_image(dst)) toast("Nothing to paste");
        break;
    }
    case ACT_RENAME:
        if (pane_du_on(p)) toast("Rename in the file list (Show in the file list)");
        else if (p->sel_count > 1) do_action(p, ACT_BATCH_RENAME);
        else if (p->L && p->L->is_search) toast("Open the containing folder to rename");
        else if (p->cursor >= 0) begin_rename(p, p->cursor);
        break;
    case ACT_TRASH:
        if (pane_snap_on(p)) { snap_action(p, ACT_SNAP_DELETE); break; } // snapshots view: Delete deletes the snapshot
        if (pane_is_trash(p)) { trash_delete(p, false); break; } // items in the trash go for good
        if (path_on_phone(p->path)) { do_action(p, ACT_DELETE); break; } // no trash there: asks first
        paths = pane_selection_paths(p, &n);
        if (n) ops_start(op_new(OP_TRASH, paths, n, NULL));
        break;
    case ACT_DELETE: {
        if (pane_is_trash(p)) { trash_delete(p, false); break; }
        paths = pane_selection_paths(p, &n);
        if (!n) break;
        char msg[600];
        const char *why = path_on_phone(p->path) ? "Phones and cameras have no trash, so this can't be undone." : "This can't be undone.";
        if (n == 1) snprintf(msg, sizeof msg, "Permanently delete \xE2\x80\x9C%s\xE2\x80\x9D? %s", path_basename(paths[0]), why);
        else snprintf(msg, sizeof msg, "Permanently delete these %d items? %s", n, why);
        static const char *b[] = {"Delete", "Cancel"};
        dialog_open("Delete permanently", msg, b, 2, 0, 1, NULL, delete_confirm_cb, op_new(OP_DELETE, paths, n, NULL));
        A.dlg.icon = DI_DANGER;
        A.dlg.danger = true;
        dialog_items(paths, n, true);
        break;
    }
    case ACT_COPY_PATH: {
        paths = pane_selection_paths(p, &n);
        size_t cap = strlen(p->path) + 2;
        for (int i = 0; i < n; i++) cap += strlen(paths[i]) + 1;
        char *txt = xmalloc(cap);
        size_t o = 0;
        txt[0] = 0;
        if (!n) o = (size_t)sprintf(txt, "%s", p->path);
        for (int i = 0; i < n; i++) o += (size_t)sprintf(txt + o, "%s%s", i ? "\n" : "", paths[i]);
        SDL_SetClipboardText(txt);
        free(txt);
        clip_free_data();
        toast(n > 1 ? "Copied %d paths" : "Copied path", n);
        break;
    }
    case ACT_TERMINAL: open_terminal(p->path); break;
    case ACT_TERMINAL_LOCAL: term_tab_open(p->path, NULL); break;
    case ACT_TERMINAL_EXTERNAL: ext_terminal(p->path); break;
    case ACT_TERM_COPY: if (active_term()) term_copy(active_term()); break;
    case ACT_TERM_PASTE: if (active_term()) term_paste(active_term()); break;
    case ACT_TERM_CLEAR: if (active_term()) term_clear_scrollback(active_term()); break;
    case ACT_SSH_TERMINAL: // sidebar: an SFTP connection or saved server
        if (A.menu_place >= 0 && A.menu_place < A.nplaces) {
            Place *pl = &A.places[A.menu_place];
            SshTarget t;
            NetAddr na;
            if (pl->kind == PK_NETWORK && ssh_target_for(pl->path, &t)) ssh_tab_open(&t);
            else if (pl->kind == PK_NETSAVED && pl->item < A.nservers && net_parse(A.servers[pl->item].uri, &na) &&
                     !strcmp(g_net_protos[na.proto].scheme, "sftp")) {
                memset(&t, 0, sizeof t);
                snprintf(t.user, sizeof t.user, "%s", na.user);
                snprintf(t.host, sizeof t.host, "%s", na.host);
                snprintf(t.port, sizeof t.port, "%s", na.port);
                snprintf(t.path, sizeof t.path, "/%s", na.path);
                ssh_tab_open(&t);
            }
        }
        break;
    case ACT_NEW_FOLDER: new_item(p, true); break;
    case ACT_NEW_FILE: new_item(p, false); break;
    case ACT_REFRESH:
        dirsize_cache_expire();
        if (pane_du_on(p)) du_rescan(p);
        if (cmp_side(p) >= 0) cmp_run(); // compared: compare again too
        pane_refresh(p);
        break; // F5: exact folder sizes, not cached ones
    case ACT_HIDDEN:
        A.show_hidden = !A.show_hidden;
        A.settings_dirty = true;
        for (int i = 0; i < A.ntabs; i++)
            for (int j = 0; j < A.tabs[i]->npanes; j++) pane_resort(&A.tabs[i]->pane[j]);
        break;
    case ACT_SELECT_ALL:
        if (p->order && !pane_du_on(p))
            for (u32 i = 0; i < p->count; i++) sel_set(p, i, true);
        break;
    case ACT_UNDO: do_undo(); break;
    case ACT_PIN: {
        char t[PATH_MAX];
        if (A.menu.open && A.menu_place >= 0 && A.menu_place < A.nplaces &&
            (A.places[A.menu_place].kind == PK_DRIVE || A.places[A.menu_place].kind == PK_NETWORK || A.places[A.menu_place].kind == PK_CLOUD))
            snprintf(t, sizeof t, "%s", A.places[A.menu_place].path);
        else pane_target_folder(p, t, sizeof t);
        pin_toggle(t);
        break;
    }
    case ACT_QA_RENAME: case ACT_QA_RESET_NAME: case ACT_QA_UP: case ACT_QA_DOWN: case ACT_QA_REMOVE:
    case ACT_QA_NEW_GROUP: case ACT_QA_GROUP_RENAME: case ACT_QA_GROUP_UP: case ACT_QA_GROUP_DOWN:
    case ACT_QA_GROUP_DELETE: case ACT_QA_GROUP_TOGGLE:
        qa_action(act);
        break;
    case ACT_DEV_MOUNT: case ACT_DEV_UNMOUNT: case ACT_DEV_EJECT: case ACT_NET_DISCONNECT: case ACT_DEV_OPEN_TAB:
        place_dev_action(A.menu_place, act);
        break;
    case ACT_NET_CONNECT_SAVED:
        if (A.menu_place >= 0 && A.menu_place < A.nplaces) net_connect_saved(A.places[A.menu_place].item);
        break;
    case ACT_NET_NEW: connect_open(-1); break;
    case ACT_CLOUD_NEW: cloud_config_terminal(); break;
    case ACT_CLOUD_REFRESH: g_cloud_relist = true; cloud_poll_now(); break;
    case ACT_CLOUD_SECTION_TOGGLE: g_cloud_collapsed = !g_cloud_collapsed; init_places(); A.settings_dirty = true; break;
    case ACT_CLOUD_CONNECT: case ACT_CLOUD_CONNECT_TAB: case ACT_CLOUD_DISCONNECT: case ACT_CLOUD_LOGIN: case ACT_CLOUD_RECONNECT:
    case ACT_CLOUD_LOG: case ACT_CLOUD_COPY_PATH: {
        int ci = A.menu_place >= 0 && A.menu_place < A.nplaces && A.places[A.menu_place].kind == PK_CLOUD ? A.places[A.menu_place].item : -1;
        if (ci < 0 || ci >= g_ncloud) break;
        Cloud *c = &g_cloud[ci];
        if (act == ACT_CLOUD_CONNECT || act == ACT_CLOUD_CONNECT_TAB) cloud_connect(ci, act == ACT_CLOUD_CONNECT_TAB ? 2 : 1);
        else if (act == ACT_CLOUD_DISCONNECT) cloud_disconnect(ci);
        else if (act == ACT_CLOUD_LOGIN) cloud_set_login(ci, !c->login);
        else if (act == ACT_CLOUD_RECONNECT) cloud_reconnect_terminal(c->name);
        else if (act == ACT_CLOUD_LOG) cloud_log_terminal(c->name);
        else if (act == ACT_CLOUD_COPY_PATH && c->mount[0]) { SDL_SetClipboardText(c->mount); toast("Copied %s", c->mount); }
        break;
    }
    case ACT_ADDON_BASE ... ACT_ADDON_LAST: addon_action(act); break;
    case ACT_NET_EDIT_SAVED:
        if (A.menu_place >= 0 && A.menu_place < A.nplaces) connect_open(A.places[A.menu_place].item);
        break;
    case ACT_NET_FORGET:
        if (A.menu_place >= 0 && A.menu_place < A.nplaces) { net_server_forget(A.places[A.menu_place].item); init_places(); }
        break;
    case ACT_DEV_RENAME:
        if (A.menu_place >= 0 && A.menu_place < A.nplaces) dev_edit_begin(&A.places[A.menu_place]);
        break;
    case ACT_DEV_RESET_NAME:
        if (A.menu_place >= 0 && A.menu_place < A.nplaces) {
            dev_name_set(place_dev_key(&A.places[A.menu_place]), "");
            init_places();
            A.settings_dirty = true;
        }
        break;
    case ACT_DEV_SECTION_TOGGLE:
        if (A.menu_place >= 0 && A.menu_place < A.nplaces) {
            if (A.places[A.menu_place].kind == PK_DEVICES) A.devs_collapsed = !A.devs_collapsed;
            else A.nets_collapsed = !A.nets_collapsed;
            init_places();
            A.settings_dirty = true;
        }
        break;
    case ACT_UNPIN_PLACE:
        if (A.menu_place >= 0 && A.menu_place < A.nplaces) pin_toggle(A.places[A.menu_place].path);
        break;
    case ACT_PLACE_NEW_TAB:
        if (A.menu_place >= 0 && A.menu_place < A.nplaces) {
            char op[PATH_MAX];
            tab_new(place_open_path(&A.places[A.menu_place], op, sizeof op), A.cur + 1, true);
        }
        break;
    case ACT_PREVIEW: preview_toggle(); break;
    case ACT_OPEN_WITH:
        paths = pane_selection_paths(p, &n);
        if (n) openwith_open(paths, n);
        break;
    case ACT_PROPERTIES:
        paths = pane_selection_paths(p, &n);
        if (n) props_open(paths, n);
        else props_open((char *[]){p->path}, 1);
        break;
    case ACT_BATCH_RENAME:
        paths = pane_selection_paths(p, &n);
        if (n) rename_open(paths, n, p);
        break;
    case ACT_COL_RESET: cols_default(); A.settings_dirty = true; break;
    case ACT_ALT_ROWS: A.alt_rows = !A.alt_rows; A.settings_dirty = true; break;
    case ACT_LOAD_TIMES: A.load_times = !A.load_times; A.settings_dirty = true; break;
    case ACT_TREE_VIEW: tree_toggle_mode(p); break;
    case ACT_FOLDER_SIZES: A.folder_sizes = !A.folder_sizes; A.settings_dirty = true; break;
    case ACT_GIT_STATUS: A.git_status = !A.git_status; A.settings_dirty = true; git_refresh_all(); break;
    case ACT_GIT_OPEN_REMOTE: case ACT_GIT_COPY_REMOTE: case ACT_GIT_COPY_BRANCH: case ACT_GIT_ROOT: case ACT_GIT_REFRESH:
        git_action(p, act);
        break;
    case ACT_CS_FOLD_ALL: case ACT_CS_UNFOLD_ALL: content_fold_all(p, act == ACT_CS_FOLD_ALL); break;
    case ACT_CS_OPEN_LINE: content_open_line(p, p->cursor); break;
    case ACT_CS_HERE: { // a folder: go in, search inside its files
        if (p->cursor < 0 || !p->order) break;
        Entry *e = &p->L->entries[p->order[p->cursor]];
        if (!(e->flags & ENT_DIR)) break;
        char full[PATH_MAX];
        path_join(full, sizeof full, p->path, e->name);
        pane_navigate(p, full, true);
        search_focus_contents(p);
        break;
    }
    case ACT_DU_TOGGLE: case ACT_DU_NEW_TAB: case ACT_DU_PLACE: case ACT_DU_OPEN: case ACT_DU_OPEN_TAB: case ACT_DU_SHOW:
    case ACT_DU_TREEMAP: case ACT_DU_SUNBURST: case ACT_DU_LIST: case ACT_DU_RESCAN: case ACT_DU_UP:
        du_action(p, act);
        break;
    case ACT_CMP_START: case ACT_CMP_END: case ACT_CMP_TAB: case ACT_CMP_SYNC_A: case ACT_CMP_SYNC_B: case ACT_CMP_SYNC_BOTH:
    case ACT_CMP_MIRROR_A: case ACT_CMP_MIRROR_B: case ACT_CMP_DIFF_ONLY: case ACT_CMP_CONTENTS: case ACT_CMP_AGAIN:
        cmp_action(p, act);
        break;
    case ACT_SNAP_TOGGLE: case ACT_SNAP_PLACE: case ACT_SNAP_REFRESH: case ACT_SNAP_BACK: case ACT_SNAP_OPEN: case ACT_SNAP_COMPARE:
    case ACT_SNAP_CHANGES: case ACT_SNAP_DELETE: case ACT_SNAP_CREATE: case ACT_SNAP_ALLOW: case ACT_SNAP_RESTORE:
        snap_action(p, act);
        break;
    case ACT_SIZE_BARS: A.size_bars = !A.size_bars; A.settings_dirty = true; break;
    case ACT_OPEN_SPLIT_RIGHT:
    case ACT_OPEN_SPLIT_DOWN: {
        char t[PATH_MAX];
        pane_target_folder(p, t, sizeof t);
        panel_split(A.ap, act == ACT_OPEN_SPLIT_RIGHT ? ZONE_RIGHT : ZONE_BOTTOM, NULL, t);
        break;
    }
    case ACT_TAB_REOPEN:
        if (A.nclosed) {
            char *path = A.closed[--A.nclosed];
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) tab_new(path, A.ntabs, true);
            else toast("That folder no longer exists");
            free(path);
        } else toast("No recently closed tabs");
        break;
    default:
        if (act >= ACT_COL_BASE && act < ACT_COL_BASE + COL_COUNT) col_toggle(act - ACT_COL_BASE);
        else if (act >= ACT_VIEW_BASE && act < ACT_VIEW_BASE + ZOOM_COUNT) set_zoom_level(p, act - ACT_VIEW_BASE);
        else if (act >= ACT_SORT_BASE && act < ACT_SORT_BASE + SORT_COUNT) pane_set_sort(p, act - ACT_SORT_BASE);
        else if (act >= ACT_GROUP_BASE && act < ACT_GROUP_BASE + GROUP_COUNT) pane_set_group(p, act - ACT_GROUP_BASE);
        else if (act >= ACT_QA_MOVE_BASE && act < ACT_QA_MOVE_BASE + 16) qa_action(act);
        else if (act >= ACT_FV_SUBFOLDERS && act <= ACT_FV_RESET) folder_view_action(p, act);
        else if (act >= ACT_TF_BASE && act < ACT_TF_BASE + 3) { p->tf_mode = act - ACT_TF_BASE; pane_resort(p); }
        else if (act == ACT_TF_RESET) { p->tf_mode = 0; p->tf_exts[0] = 0; pane_resort(p); }
        else if (act >= ACT_TF_EXT_BASE && act < ACT_TF_EXT_BASE + g_ntf) tf_toggle(p, g_tf_keys[act - ACT_TF_EXT_BASE]);
        else if (act >= ACT_CMP_PICK_BASE && act < ACT_CMP_PICK_BASE + 12) cmp_action(p, act);
        else if (act == ACT_TRASH_RESTORE) trash_restore(p, false);
        else if (act == ACT_TRASH_RESTORE_ALL) trash_restore(p, true);
        else if (act == ACT_TRASH_EMPTY) trash_delete(p, true);
        break;
    }
    // tab actions (from the tab context menu; default to the current tab)
    Tab *mt = A.menu_tab && tab_index(A.menu_tab) >= 0 ? A.menu_tab : cur_tab();
    A.menu_tab = NULL;
    if (act >= ACT_TAB_NEW && act <= ACT_TAB_WINDOW && act != ACT_TAB_REOPEN && mt) {
        Panel *pn = mt->panel;
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s", mt->pane[0].path);
        switch (act) {
        case ACT_TAB_NEW: tab_new_in(pn, path, panel_end_index(pn), true); break;
        case ACT_TAB_DUP: tab_new_in(pn, path, tab_index(mt) + 1, true); break;
        case ACT_TAB_CLOSE: tab_close(tab_index(mt)); break;
        case ACT_TAB_CLOSE_OTHERS:
        case ACT_TAB_CLOSE_LEFT:
        case ACT_TAB_CLOSE_RIGHT: {
            int mi = tab_index(mt);
            for (int i = A.ntabs - 1; i >= 0; i--) {
                Tab *o = A.tabs[i];
                if (o == mt || o->panel != pn) continue;
                if (act == ACT_TAB_CLOSE_LEFT && i > mi) continue;
                if (act == ACT_TAB_CLOSE_RIGHT && i < mi) continue;
                tab_close(i);
                mi = tab_index(mt);
            }
            mt->panel->cur = mt;
            panel_activate(mt->panel);
            break;
        }
        case ACT_TAB_SPLIT_RIGHT:
        case ACT_TAB_SPLIT_DOWN:
            if (panel_ntabs(pn) > 1) panel_split(pn, act == ACT_TAB_SPLIT_RIGHT ? ZONE_RIGHT : ZONE_BOTTOM, mt, path);
            break;
        case ACT_TAB_WINDOW: new_window(path); break;
        }
    }
    strv_free(paths, n);
    A.redraw = true;
}

// --- archives ------------------------------------------------------------------

static void cache_subdir(const char *sub, char *out, size_t cap) {
    const char *x = getenv("XDG_CACHE_HOME");
    if (x && x[0] == '/') snprintf(out, cap, "%s/file-explorer/%s", x, sub);
    else snprintf(out, cap, "%s/.cache/file-explorer/%s", getenv("HOME") ? getenv("HOME") : "/tmp", sub);
}

// Selected members of an archive pane as "inner/path" strings.
static char **arc_selection(Pane *p, int *n) {
    *n = 0;
    char file[PATH_MAX], inner[PATH_MAX];
    if (!p->order || !arc_split(p->path, file, sizeof file, inner, sizeof inner)) return NULL;
    char **v = xmalloc((size_t)MAX(p->sel_count, 1u) * sizeof(char *));
    for (u32 i = 0; i < p->count; i++) {
        u32 e = p->order[i];
        if (!p->sel[e] && !(p->sel_count == 0 && (i64)i == p->cursor)) continue;
        char m[PATH_MAX];
        if (inner[0]) snprintf(m, sizeof m, "%s/%s", inner, p->L->entries[e].name);
        else snprintf(m, sizeof m, "%s", p->L->entries[e].name);
        if (*n < (int)MAX(p->sel_count, 1u)) v[(*n)++] = strdup(m);
    }
    return v;
}

static void arc_extract_to(Pane *p, char **members, int n, const char *dst) {
    char file[PATH_MAX], inner[PATH_MAX];
    if (!arc_split(p->path, file, sizeof file, inner, sizeof inner)) return;
    Op *op = op_new(OP_EXTRACT, members, n, dst);
    op->arc = strdup(file);
    ops_start(op);
}

// Open a file inside an archive: extract it to the cache, then open it.
static void arc_extract_open(Pane *p, const char *name) {
    char file[PATH_MAX], inner[PATH_MAX], md5[33], dir[PATH_MAX], m[PATH_MAX];
    if (!arc_split(p->path, file, sizeof file, inner, sizeof inner)) return;
    md5_hex(file, strlen(file), md5);
    cache_subdir("extract", dir, sizeof dir);
    size_t dl = strlen(dir);
    snprintf(dir + dl, sizeof dir - dl, "/%s", md5);
    if (inner[0]) snprintf(m, sizeof m, "%s/%s", inner, name);
    else snprintf(m, sizeof m, "%s", name);
    char out[PATH_MAX * 2];
    snprintf(out, sizeof out, "%s/%s", dir, name);
    unlink(out); // always fresh
    char *mm[1] = {m};
    Op *op = op_new(OP_EXTRACT, mm, 1, dir);
    op->arc = strdup(file);
    op->open_after = true;
    op->is_undo = true;
    ops_start(op);
}

static void arc_action(Pane *p, int act) {
    int n = 0;
    char **paths = NULL;
    if (act == ACT_EXTRACT_HERE) { // archive file in a normal folder -> "<folder>/<name without ext>/"
        if (p->cursor < 0 || !p->order) return;
        Entry *e = &p->L->entries[p->order[p->cursor]];
        char arc[PATH_MAX], stem[NAME_MAX + 1], dst[PATH_MAX];
        path_join(arc, sizeof arc, p->path, e->name);
        snprintf(stem, sizeof stem, "%s", e->name);
        char *dot = strstr(stem, ".tar.");
        if (!dot) dot = strrchr(stem, '.');
        if (dot && dot != stem) *dot = 0;
        int dfd = open(p->path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        char nn[NAME_MAX + 1];
        snprintf(nn, sizeof nn, "%s", stem);
        if (dfd >= 0 && exists_at(dfd, nn)) unique_name(dfd, stem, true, false, nn, sizeof nn);
        if (dfd >= 0) close(dfd);
        path_join(dst, sizeof dst, p->path, nn);
        Op *op = op_new(OP_EXTRACT, NULL, 0, dst);
        op->arc = strdup(arc);
        ops_start(op);
        return;
    }
    if (act == ACT_EXTRACT_SEL || act == ACT_EXTRACT_ALL) { // inside an archive: next to the archive file
        char file[PATH_MAX], inner[PATH_MAX], par[PATH_MAX];
        if (!arc_split(p->path, file, sizeof file, inner, sizeof inner)) return;
        parent_of(file, par, sizeof par);
        if (act == ACT_EXTRACT_SEL) paths = arc_selection(p, &n);
        else if (inner[0]) { paths = xmalloc(sizeof(char *)); paths[0] = strdup(inner); n = 1; }
        arc_extract_to(p, paths, n, par);
        strv_free(paths, n);
        return;
    }
    if (act == ACT_COMPRESS_ZIP || act == ACT_COMPRESS_TGZ) {
        paths = pane_selection_paths(p, &n);
        if (!n) return;
        char base[NAME_MAX + 1], out[PATH_MAX];
        if (n == 1) snprintf(base, sizeof base, "%s", path_basename(paths[0]));
        else snprintf(base, sizeof base, "%s", p->path[1] ? path_basename(p->path) : "archive");
        const char *ext = act == ACT_COMPRESS_ZIP ? ".zip" : ".tar.gz";
        char want[NAME_MAX + 16], nn[NAME_MAX + 1];
        snprintf(want, sizeof want, "%s%s", base, ext);
        int dfd = open(p->path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        snprintf(nn, sizeof nn, "%s", want);
        if (dfd >= 0 && exists_at(dfd, nn)) unique_name(dfd, want, false, false, nn, sizeof nn);
        if (dfd >= 0) close(dfd);
        path_join(out, sizeof out, p->path, nn);
        Op *op = op_new(OP_COMPRESS, paths, n, p->path);
        op->arc = strdup(out);
        ops_start(op);
        strv_free(paths, n);
    }
}

// --- context menu ----------------------------------------------------------

static const char *keymap_label(int id);

static void menu_add(const char *label, const char *sc, int act, bool enabled) {
    Menu *m = &A.menu;
    if (m->n >= (int)ARRAY_COUNT(m->items)) return;
    // rebindable actions show their current key (tab-menu actions alias the tab commands)
    int cmd = act == ACT_TAB_NEW ? CMD_NEW_TAB : act == ACT_TAB_CLOSE ? CMD_CLOSE_TAB : act == ACT_TAB_WINDOW ? CMD_NEW_WINDOW : act;
    if (act == ACT_DELETE && !strcmp(sc, "Del")) cmd = ACT_TRASH; // trash view: the Trash key deletes permanently
    const char *k = keymap_label(cmd);
    if (k) {
        snprintf(m->scbuf[m->n], sizeof m->scbuf[m->n], "%s", k);
        sc = m->scbuf[m->n];
    }
    m->items[m->n++] = (MenuItem){label, sc, act, enabled, false, false, false, false};
}

static void menu_sep(void) {
    Menu *m = &A.menu;
    if (m->n && !m->items[m->n - 1].sep) m->items[m->n++] = (MenuItem){NULL, NULL, -1, false, true, false, false, false};
}

static f32 menu_item_h(const MenuItem *it) { return it->sep ? roundf(S(9)) : it->info ? roundf(S(24)) : roundf(S(28)); }

static void menu_finish(f32 x, f32 y);
static void menu_add_check_key(const char *label, int act, bool checked, int key_id);
static void menu_open_columns(Pane *p, f32 x, f32 y);
static void menu_add_check(const char *label, int act, bool checked, bool enabled);

static void menu_open(Pane *p, f32 x, f32 y, bool on_item) {
    Menu *m = &A.menu;
    m->keep_open = false;
    m->reopen = NULL;
    m->n = 0;
    m->hover = -1;
    m->pane = p;
    bool has_clip = clip_has_files() || clip_image_type() >= 0;
    if (on_item && p->cursor >= 0) {
        Entry *e = &p->L->entries[p->order[p->cursor]];
        bool dir = e->flags & ENT_DIR;
        if (pane_is_archive(p)) {
            menu_add("Extract next to the archive", "Drag out", ACT_EXTRACT_SEL, true);
            menu_sep();
        } else if (!dir && is_archive_name(e->name)) {
            menu_add("Extract here", "", ACT_EXTRACT_HERE, true);
        }
        if (pane_is_trash(p)) {
            menu_add("Restore", "", ACT_TRASH_RESTORE, true);
            menu_add("Delete permanently", "Del", ACT_DELETE, true);
            menu_sep();
        }
        if (e->flags & ENT_MATCH) menu_add("Open at this line", "Enter", ACT_CS_OPEN_LINE, true);
        else menu_add("Open", "Enter", ACT_OPEN, true);
        if (!dir) menu_add("Open with\xE2\x80\xA6", "", ACT_OPEN_WITH, true);
        if (p->L->is_content) {
            menu_add("Fold all matches", "", ACT_CS_FOLD_ALL, true);
            menu_add("Unfold all matches", "", ACT_CS_UNFOLD_ALL, true);
        }
        if (dir) {
            menu_add("Search inside files here\xE2\x80\xA6", "", ACT_CS_HERE, !pane_is_archive(p));
            menu_add("Disk usage\xE2\x80\xA6", "", ACT_DU_NEW_TAB, !pane_is_archive(p) && !pane_is_trash(p));
            menu_add("Open in new tab", "Ctrl+Enter", ACT_OPEN_TAB, true);
            menu_add("Open in right split", "", ACT_OPEN_SPLIT_RIGHT, true);
            menu_add("Open in bottom split", "", ACT_OPEN_SPLIT_DOWN, true);
        }
        menu_sep();
        menu_add("Cut", "Ctrl+X", ACT_CUT, true);
        menu_add("Copy", "Ctrl+C", ACT_COPY, true);
        menu_add("Paste", "Ctrl+V", ACT_PASTE, has_clip);
        menu_sep();
        menu_add(p->sel_count > 1 ? "Batch rename\xE2\x80\xA6" : "Rename", "F2", ACT_RENAME, true);
        menu_add("Move to Trash", "Del", ACT_TRASH, true);
        menu_add("Delete permanently", "Shift+Del", ACT_DELETE, true);
        menu_sep();
        if (dir) {
            char t[PATH_MAX];
            path_join(t, sizeof t, p->path, e->name);
            menu_add(pin_index(t) >= 0 ? "Unpin from sidebar" : "Pin to sidebar", "Ctrl+D", ACT_PIN, true);
        }
        if (!pane_is_archive(p)) {
            menu_add("Compress to ZIP", "", ACT_COMPRESS_ZIP, true);
            menu_add("Compress to tar.gz", "", ACT_COMPRESS_TGZ, true);
        }
        menu_add("Copy path", "Ctrl+Shift+C", ACT_COPY_PATH, true);
        if (snap_in(p->path)) menu_add("Restore to where it is now\xE2\x80\xA6", "", ACT_SNAP_RESTORE, true);
        menu_terminal_items(p);
        menu_add("Properties", "Alt+Enter", ACT_PROPERTIES, true);
    } else {
        if (pane_is_archive(p)) {
            menu_add("Extract this folder next to the archive", "", ACT_EXTRACT_ALL, true);
            menu_sep();
        }
        if (pane_is_trash(p)) {
            menu_add("Restore all", "", ACT_TRASH_RESTORE_ALL, p->count > 0);
            menu_add("Empty Trash", "", ACT_TRASH_EMPTY, p->order && p->L->count > 0);
            menu_sep();
        }
        menu_add("New folder", "Ctrl+Shift+N", ACT_NEW_FOLDER, true);
        menu_add("New file", "Ctrl+Alt+N", ACT_NEW_FILE, true);
        menu_sep();
        menu_add("Paste", "Ctrl+V", ACT_PASTE, has_clip);
        menu_add("Select all", "Ctrl+A", ACT_SELECT_ALL, p->count > 0);
        menu_add("Undo", "Ctrl+Z", ACT_UNDO, A.nundo > 0);
        menu_sep();
        {
            static const int lv[] = {0, 1, 2, 4, 6, 7};
            static const char *sc[] = {"Ctrl+Shift+6", "Ctrl+Shift+5", "Ctrl+Shift+4", "Ctrl+Shift+3", "Ctrl+Shift+2", "Ctrl+Shift+1"};
            int cur = zoom_level_of(p);
            for (size_t i = 0; i < ARRAY_COUNT(lv); i++) {
                menu_add_check(g_zoom[lv[i]].label, ACT_VIEW_BASE + lv[i], cur == lv[i], true);
                A.menu.items[A.menu.n - 1].shortcut = sc[i];
            }
            menu_add_check("Tree view", ACT_TREE_VIEW, p->tree, !pane_is_archive(p));
            const char *tk = keymap_label(CMD_TREE_VIEW);
            snprintf(A.menu.scbuf[A.menu.n - 1], sizeof A.menu.scbuf[0], "%s", tk ? tk : "");
            A.menu.items[A.menu.n - 1].shortcut = A.menu.scbuf[A.menu.n - 1];
            menu_add_check_key("Disk usage", ACT_DU_TOGGLE, p->du, CMD_DISK_USAGE);
            A.menu.items[A.menu.n - 1].enabled = !pane_is_archive(p) && !pane_is_trash(p);
            menu_add_check_key("Snapshots of this drive", ACT_SNAP_TOGGLE, p->snapv, CMD_SNAPSHOTS);
            A.menu.items[A.menu.n - 1].enabled = !pane_is_archive(p) && !pane_is_trash(p);
        }
        menu_sep();
        {
            static const int sk[] = {SORT_NAME, SORT_DATE, SORT_TYPE, SORT_SIZE};
            static const char *sl[] = {"Sort by name", "Sort by date modified", "Sort by type", "Sort by size"};
            for (int i = 0; i < 4; i++) menu_add_check(sl[i], ACT_SORT_BASE + sk[i], p->sort_key == sk[i], true);
        }
        menu_sep();
        {
            static const char *gl[] = {"No grouping", "Group by date", "Group by type", "Group by size"};
            for (int g = 0; g < GROUP_COUNT; g++)
                menu_add_check(gl[g], ACT_GROUP_BASE + g, p->group == g, p->view == VIEW_DETAILS || g == GROUP_NONE);
        }
        menu_sep();
        {
            FolderView *fv = folder_view_find(p->path, true);
            menu_add_check("Use this view in subfolders", ACT_FV_SUBFOLDERS, fv && fv->recursive, true);
            menu_add("Use this view everywhere", "", ACT_FV_ALL, true);
            menu_add("Reset folder view", "", ACT_FV_RESET, fv != NULL);
        }
        menu_sep();
        menu_add(A.show_hidden ? "Hide hidden files" : "Show hidden files", "Ctrl+H", ACT_HIDDEN, true);
        menu_add("Refresh", "F5", ACT_REFRESH, true);
        if (cmp_side(p) >= 0) menu_add("End compare", "", ACT_CMP_END, true);
        else {
            menu_add(A.npanels > 1 ? "Compare with the other panel" : "Compare with\xE2\x80\xA6", "", ACT_CMP_START, !pane_is_archive(p) && !pane_is_trash(p));
            const char *ck = keymap_label(CMD_COMPARE);
            snprintf(A.menu.scbuf[A.menu.n - 1], sizeof A.menu.scbuf[0], "%s", ck ? ck : "");
            A.menu.items[A.menu.n - 1].shortcut = A.menu.scbuf[A.menu.n - 1];
        }
        menu_sep();
        menu_add(pin_index(p->path) >= 0 ? "Unpin this folder" : "Pin this folder", "Ctrl+D", ACT_PIN, true);
        menu_add("Copy path", "Ctrl+Shift+C", ACT_COPY_PATH, true);
        menu_terminal_items(p);
        menu_add("Properties", "Alt+Enter", ACT_PROPERTIES, true);
    }
    menu_finish(x, y);
}

static void menu_add_check(const char *label, int act, bool checked, bool enabled) {
    Menu *m = &A.menu;
    if (m->n >= (int)ARRAY_COUNT(m->items)) return;
    m->items[m->n++] = (MenuItem){label, "", act, enabled, false, true, checked, false};
}

// Right-click on a column header.
static void menu_open_columns(Pane *p, f32 x, f32 y) {
    Menu *m = &A.menu;
    m->keep_open = false;
    m->reopen = NULL;
    m->n = 0;
    m->hover = -1;
    m->pane = p;
    for (int c = 0; c < COL_COUNT; c++) menu_add_check(g_coldef[c].label, ACT_COL_BASE + c, col_visible(c), c != COL_NAME);
    menu_sep();
    menu_add_check("Size bars", ACT_SIZE_BARS, A.size_bars, true);
    menu_add_check("Alternating row colors", ACT_ALT_ROWS, A.alt_rows, true);
    menu_sep();
    menu_add("Reset columns", "", ACT_COL_RESET, true);
    menu_finish(x, y);
}

// Status bar "features" button (left of the gear): on/off switches that otherwise only have a key or a command.
// Stays open while toggling; opens above the button.
static void menu_add_check_key(const char *label, int act, bool checked, int key_id) {
    menu_add_check(label, act, checked, true);
    Menu *m = &A.menu;
    const char *k = keymap_label(key_id);
    if (k && m->n) {
        snprintf(m->scbuf[m->n - 1], sizeof m->scbuf[0], "%s", k);
        m->items[m->n - 1].shortcut = m->scbuf[m->n - 1];
    }
}

static void menu_open_features(Pane *p, f32 x, f32 y) {
    (void)x; (void)y;
    Menu *m = &A.menu;
    m->keep_open = true;
    m->reopen = menu_open_features;
    m->n = 0;
    m->hover = -1;
    m->pane = p;
    menu_add_check_key("Preview panel", ACT_PREVIEW, A.preview_on, ACT_PREVIEW);
    menu_add_check_key("Tree view", ACT_TREE_VIEW, p->tree, CMD_TREE_VIEW);
    menu_add_check_key("Hidden files", ACT_HIDDEN, A.show_hidden, ACT_HIDDEN);
    menu_add_check_key("Folder sizes", ACT_FOLDER_SIZES, A.folder_sizes, CMD_FOLDER_SIZES);
    menu_add_check_key("Git status", ACT_GIT_STATUS, A.git_status, CMD_GIT_STATUS);
    menu_sep();
    menu_add_check("Size bars", ACT_SIZE_BARS, A.size_bars, true);
    menu_add_check("Alternating row colors", ACT_ALT_ROWS, A.alt_rows, true);
    menu_add_check("Load times", ACT_LOAD_TIMES, A.load_times, true);
    Rect b = A.r_feat;
    menu_finish(b.x + b.w, b.y - S(4)); // (no room below the status bar: it opens upwards, kept inside the window)
}

static bool features_menu_open(void) { return A.menu.open && A.menu.reopen == menu_open_features; }

// --- type filter (funnel button) ---------------------------------------------


static bool tf_is_hidden(const Pane *p, const char *ext) {
    char key[24];
    snprintf(key, sizeof key, "|%s|", ext);
    return strstr(p->tf_exts, key) != NULL;
}

static void tf_toggle(Pane *p, const char *ext) {
    char key[24];
    snprintf(key, sizeof key, "|%s|", ext);
    char *at = strstr(p->tf_exts, key);
    if (at) memmove(at, at + strlen(key) - 1, strlen(at + strlen(key) - 1) + 1); // keep the shared '|'
    else {
        size_t n = strlen(p->tf_exts);
        if (n + strlen(key) + 1 < sizeof p->tf_exts) snprintf(p->tf_exts + n, sizeof p->tf_exts - n, "%s", n ? key + 1 : key);
    }
    if (!strcmp(p->tf_exts, "|")) p->tf_exts[0] = 0;
    pane_resort(p);
}

static int cmp_tf(const void *a, const void *b) { return ((const int *)b)[1] - ((const int *)a)[1]; }

static void menu_open_types(Pane *p, f32 x, f32 y) {
    Menu *m = &A.menu;
    m->n = 0;
    m->hover = -1;
    m->pane = p;
    m->keep_open = true;
    m->reopen = menu_open_types;
    menu_add_check("Files and folders", ACT_TF_BASE + 0, p->tf_mode == 0, true);
    menu_add_check("Files only", ACT_TF_BASE + 1, p->tf_mode == 1, true);
    menu_add_check("Folders only", ACT_TF_BASE + 2, p->tf_mode == 2, true);
    menu_sep();
    // count extensions in this folder
    g_ntf = 0;
    Listing *L = p->L;
    if (L && !L->is_search && atomic_load(&L->state) >= LST_READY && atomic_load(&L->state) != LST_ERROR) {
        static char keys[256][20];
        static int counts[256][2];
        int nk = 0;
        for (u32 i = 0; i < L->count && i < 300000; i++) {
            Entry *e = &L->entries[i];
            if ((e->flags & ENT_DIR) || (!A.show_hidden && (e->flags & ENT_HIDDEN))) continue;
            char k[20] = "";
            if (e->ext && e->name_len - e->ext < 16)
                for (u32 c = 0; c < e->name_len - e->ext; c++) k[c] = (char)ascii_lower((u8)e->name[e->ext + c]);
            int f = -1;
            for (int j = 0; j < nk; j++)
                if (!strcmp(keys[j], k)) { f = j; break; }
            if (f < 0 && nk < 256) { f = nk++; snprintf(keys[f], 20, "%s", k); counts[f][0] = f; counts[f][1] = 0; }
            if (f >= 0) counts[f][1]++;
        }
        qsort(counts, (size_t)nk, sizeof counts[0], cmp_tf);
        for (int j = 0; j < nk && g_ntf < 30; j++) {
            const char *k = keys[counts[j][0]];
            snprintf(g_tf_keys[g_ntf], sizeof g_tf_keys[0], "%s", k);
            char up[20];
            size_t q = 0;
            for (; k[q] && q < sizeof up - 1; q++) up[q] = (char)toupper((u8)k[q]);
            up[q] = 0;
            snprintf(g_tf_labels[g_ntf], sizeof g_tf_labels[0], "%s  (%d)", k[0] ? up : "No extension", counts[j][1]);
            menu_add_check(g_tf_labels[g_ntf], ACT_TF_EXT_BASE + g_ntf, !tf_is_hidden(p, k), p->tf_mode != 2);
            g_ntf++;
        }
    }
    menu_sep();
    menu_add("Show all types", "", ACT_TF_RESET, p->tf_mode || p->tf_exts[0]);
    menu_finish(x, y);
}

// --- trash view --------------------------------------------------------------

static void trash_dirs(char *files, char *info, size_t cap) {
    char dh[PATH_MAX];
    data_home(dh, sizeof dh);
    snprintf(files, cap, "%s/Trash/files", dh);
    snprintf(info, cap, "%s/Trash/info", dh);
}

// The trash's files folder itself ($XDG_DATA_HOME/Trash/files), shown as "Trash".
static bool is_trash_dir(const char *path) {
    char f[PATH_MAX], i[PATH_MAX];
    trash_dirs(f, i, sizeof f);
    return !strcmp(path, f);
}

static bool pane_is_trash(const Pane *p) {
    char f[PATH_MAX], i[PATH_MAX];
    trash_dirs(f, i, sizeof f);
    return !strcmp(p->path, f);
}

// Original path and deletion date of an item in the home trash (small cache).
static bool trash_info(const char *name, char *orig, size_t cap, char *date, size_t dcap) {
    static struct { char name[NAME_MAX + 1]; char orig[PATH_MAX]; char date[32]; } cache[256];
    static int n;
    for (int i = 0; i < n; i++)
        if (!strcmp(cache[i].name, name)) {
            snprintf(orig, cap, "%s", cache[i].orig);
            snprintf(date, dcap, "%s", cache[i].date);
            return cache[i].orig[0] != 0;
        }
    char files[PATH_MAX], info[PATH_MAX], ip[PATH_MAX + NAME_MAX + 16];
    trash_dirs(files, info, sizeof files);
    snprintf(ip, sizeof ip, "%s/%s.trashinfo", info, name);
    int k = n < 256 ? n++ : (int)(str_hash(name) % 256);
    snprintf(cache[k].name, sizeof cache[k].name, "%s", name);
    cache[k].orig[0] = cache[k].date[0] = 0;
    FILE *f = fopen(ip, "r");
    if (f) {
        char line[PATH_MAX * 3];
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!strncmp(line, "Path=", 5)) {
                char tmp[PATH_MAX + 16];
                snprintf(tmp, sizeof tmp, "file://%s", line + 5);
                if (!uri_to_path(tmp, cache[k].orig, sizeof cache[k].orig)) snprintf(cache[k].orig, sizeof cache[k].orig, "%s", line + 5);
            } else if (!strncmp(line, "DeletionDate=", 13)) {
                snprintf(cache[k].date, sizeof cache[k].date, "%.16s", line + 13);
                for (char *q = cache[k].date; *q; q++) if (*q == 'T') *q = ' ';
            }
        }
        fclose(f);
    }
    snprintf(orig, cap, "%s", cache[k].orig);
    snprintf(date, dcap, "%s", cache[k].date);
    return cache[k].orig[0] != 0;
}

// Restore (or permanently delete) items of the trash view.
static void trash_restore(Pane *p, bool all) {
    char files[PATH_MAX], info[PATH_MAX];
    trash_dirs(files, info, sizeof files);
    int n = 0, cap = 64;
    char **src = xmalloc((size_t)cap * sizeof(char *)), **dst = xmalloc((size_t)cap * sizeof(char *)),
         **aux = xmalloc((size_t)cap * sizeof(char *));
    for (u32 i = 0; i < p->count; i++) {
        Entry *e = &p->L->entries[p->order[i]];
        if (!all && !p->sel[p->order[i]] && !(p->sel_count == 0 && (i64)i == p->cursor)) continue;
        char orig[PATH_MAX], date[32];
        if (!trash_info(e->name, orig, sizeof orig, date, sizeof date)) continue;
        if (n == cap) {
            cap *= 2;
            src = xrealloc(src, (size_t)cap * sizeof(char *));
            dst = xrealloc(dst, (size_t)cap * sizeof(char *));
            aux = xrealloc(aux, (size_t)cap * sizeof(char *));
        }
        char sp[PATH_MAX], ip[PATH_MAX + NAME_MAX + 16];
        path_join(sp, sizeof sp, files, e->name);
        snprintf(ip, sizeof ip, "%s/%s.trashinfo", info, e->name);
        src[n] = strdup(sp);
        dst[n] = strdup(orig);
        aux[n] = strdup(ip);
        n++;
    }
    if (n) {
        Op *op = op_new(OP_RESTORE, src, n, NULL);
        op->dst = strv_dup(dst, n);
        op->aux_in = strv_dup(aux, n);
        op->is_undo = true;
        ops_start(op);
    } else toast("Nothing to restore");
    strv_free(src, n);
    strv_free(dst, n);
    strv_free(aux, n);
}

static void empty_trash_cb(int choice, bool check, void *ud) {
    (void)check;
    Op *op = ud;
    if (choice == 0) ops_start(op);
    else op_free(op);
}

// Delete items permanently (with their .trashinfo); all = empty the trash.
static void trash_delete(Pane *p, bool all) {
    char files[PATH_MAX], info[PATH_MAX];
    trash_dirs(files, info, sizeof files);
    int n = 0, cap = 64;
    char **src = xmalloc((size_t)cap * sizeof(char *)), **aux = xmalloc((size_t)cap * sizeof(char *));
    Listing *L = p->L;
    for (u32 i = 0; L && i < (all ? L->count : p->count); i++) {
        u32 ei = all ? i : p->order[i];
        if (!all && !p->sel[ei] && !(p->sel_count == 0 && (i64)i == p->cursor)) continue;
        if (n == cap) {
            cap *= 2;
            src = xrealloc(src, (size_t)cap * sizeof(char *));
            aux = xrealloc(aux, (size_t)cap * sizeof(char *));
        }
        char sp[PATH_MAX], ip[PATH_MAX + NAME_MAX + 16];
        path_join(sp, sizeof sp, files, L->entries[ei].name);
        snprintf(ip, sizeof ip, "%s/%s.trashinfo", info, L->entries[ei].name);
        src[n] = strdup(sp);
        aux[n] = strdup(ip);
        n++;
    }
    if (!n) { free(src); free(aux); toast(all ? "The trash is already empty" : "Nothing selected"); return; }
    Op *op = op_new(OP_DELETE, src, n, NULL);
    op->aux_in = strv_dup(aux, n);
    char msg[200];
    snprintf(msg, sizeof msg, all ? "Permanently delete all %d items in the Trash? This can't be undone."
                                  : "Permanently delete %d item%s from the Trash? This can't be undone.", n, n == 1 ? "" : "s");
    static const char *b[] = {"Delete", "Cancel"};
    dialog_open(all ? "Empty Trash" : "Delete permanently", msg, b, 2, 0, 1, NULL, empty_trash_cb, op);
    A.dlg.icon = DI_DANGER;
    A.dlg.danger = true;
    { // list the trashed names (as they were), not their trash-internal paths
        char **names = xmalloc((size_t)MIN(n, 5) * sizeof(char *));
        for (int i = 0; i < n && i < 5; i++) names[i] = src[i];
        dialog_items(names, n, false); // (their trash-internal paths would mean nothing)
        free(names);
    }
    strv_free(src, n);
    strv_free(aux, n);
}

// Right-click on a tab.
static void menu_open_tab(Tab *t, f32 x, f32 y) {
    Menu *m = &A.menu;
    m->keep_open = false;
    m->reopen = NULL;
    m->n = 0;
    m->hover = -1;
    m->pane = &t->pane[0];
    A.menu_tab = t;
    bool many = panel_ntabs(t->panel) > 1;
    int idx = tab_index(t);
    bool left = false, right = false;
    for (int i = 0; i < A.ntabs; i++)
        if (A.tabs[i]->panel == t->panel && A.tabs[i] != t) {
            if (i < idx) left = true;
            else right = true;
        }
    menu_add("New tab", "Ctrl+T", ACT_TAB_NEW, true);
    menu_add("Duplicate tab", "Double-click", ACT_TAB_DUP, true);
    menu_add("Open in new window", "Ctrl+N", ACT_TAB_WINDOW, true);
    menu_sep();
    menu_add("Compare with current tab", "", ACT_CMP_TAB, A.ap && t != A.ap->cur && !t->term && !A.ap->cur->term);
    menu_sep();
    menu_add("Move to right split", "Drag to edge", ACT_TAB_SPLIT_RIGHT, many);
    menu_add("Move to bottom split", "Drag to edge", ACT_TAB_SPLIT_DOWN, many);
    menu_sep();
    menu_add("Close tab", "Ctrl+W", ACT_TAB_CLOSE, A.ntabs > 1);
    menu_add("Close other tabs", "", ACT_TAB_CLOSE_OTHERS, many);
    menu_add("Close tabs to the left", "", ACT_TAB_CLOSE_LEFT, left);
    menu_add("Close tabs to the right", "", ACT_TAB_CLOSE_RIGHT, right);
    menu_sep();
    menu_add("Reopen closed tab", "Ctrl+Shift+T", ACT_TAB_REOPEN, A.nclosed > 0);
    menu_finish(x, y);
}

// Right-click on a sidebar place.
static void menu_sep(void);
static void menu_open_place(int idx, f32 x, f32 y) {
    static char move_lbl[16][96];
    Menu *m = &A.menu;
    m->keep_open = false;
    m->reopen = NULL;
    m->n = 0;
    m->hover = -1;
    m->pane = active_pane();
    A.menu_place = idx;
    Place *pl = &A.places[idx];
    if (pl->kind == PK_GROUP) {
        QaGroup *g = &A.qa[pl->group];
        menu_add(g->collapsed ? "Expand" : "Collapse", "Click", ACT_QA_GROUP_TOGGLE, true);
        menu_add("Rename group", "", ACT_QA_GROUP_RENAME, true);
        menu_sep();
        menu_add("Move group up", "", ACT_QA_GROUP_UP, pl->group > 0);
        menu_add("Move group down", "", ACT_QA_GROUP_DOWN, pl->group < A.nqa - 1);
        menu_sep();
        menu_add("New group", "", ACT_QA_NEW_GROUP, A.nqa < 16);
        menu_add("Delete group", "", ACT_QA_GROUP_DELETE, A.nqa > 1);
    } else if (pl->kind == PK_NETEMPTY || pl->kind == PK_ADDONEMPTY) {
        return;
    } else if (pl->kind == PK_CLOUDHDR || pl->kind == PK_CLOUDEMPTY) {
        menu_add(g_cloud_collapsed ? "Expand" : "Collapse", "Click", ACT_CLOUD_SECTION_TOGGLE, true);
        menu_add("New remote (rclone config)\xE2\x80\xA6", "", ACT_CLOUD_NEW, true);
        menu_add("Refresh", "", ACT_CLOUD_REFRESH, true);
    } else if (pl->kind == PK_CLOUD && pl->item < g_ncloud) {
        Cloud *c = &g_cloud[pl->item];
        if (c->mount[0]) {
            menu_add("Open in new tab", "Middle-click", ACT_CLOUD_CONNECT_TAB, true);
            menu_add("Copy folder path", "", ACT_CLOUD_COPY_PATH, true);
            menu_add("Pin to Quick Access", "", ACT_PIN, true);
        } else {
            menu_add("Connect and open", "Click", ACT_CLOUD_CONNECT, g_units_systemd);
            menu_add("Connect and open in new tab", "Middle-click", ACT_CLOUD_CONNECT_TAB, g_units_systemd);
        }
        menu_add_check("Connect at login", ACT_CLOUD_LOGIN, c->login, g_units_systemd && !picker_active()); // (the picker saves no settings)
        menu_sep();
        menu_add("Reconnect account\xE2\x80\xA6", "", ACT_CLOUD_RECONNECT, true);
        menu_add("Show log", "", ACT_CLOUD_LOG, g_units_systemd && c->unit);
        if (c->mount[0] || cloud_busy(c)) {
            menu_sep();
            menu_add("Disconnect", "", ACT_CLOUD_DISCONNECT, true);
        }
    } else if (pl->kind == PK_ADDONHDR || pl->kind == PK_ADDON) {
        addon_place_menu(pl);
    } else if (pl->kind == PK_DEVICES || pl->kind == PK_NETHDR) {
        bool col = pl->kind == PK_DEVICES ? A.devs_collapsed : A.nets_collapsed;
        menu_add(col ? "Expand" : "Collapse", "Click", ACT_DEV_SECTION_TOGGLE, true);
        if (pl->kind == PK_NETHDR) menu_add("Connect to server\xE2\x80\xA6", "", ACT_NET_NEW, true);
    } else if (pl->kind == PK_NETSAVED) {
        NetAddr na;
        bool sftp = pl->item < A.nservers && net_parse(A.servers[pl->item].uri, &na) && !strcmp(g_net_protos[na.proto].scheme, "sftp");
        menu_add("Connect", "Click", ACT_NET_CONNECT_SAVED, true);
        if (sftp) menu_add("Open SSH terminal", "", ACT_SSH_TERMINAL, true);
        menu_add("Edit\xE2\x80\xA6", "", ACT_NET_EDIT_SAVED, true);
        menu_sep();
        menu_add("Forget server", "", ACT_NET_FORGET, true);
    } else if (pl->kind == PK_UNMOUNTED) {
        menu_add("Mount and open", "Click", ACT_DEV_MOUNT, true);
        menu_add("Mount and open in new tab", "Middle-click", ACT_DEV_OPEN_TAB, true);
        menu_sep();
        menu_add("Rename", "", ACT_DEV_RENAME, true);
        if (dev_name_get(place_dev_key(pl))) menu_add("Reset name", "", ACT_DEV_RESET_NAME, true);
    } else if (pl->kind == PK_PHONE) {
        if (pl->path[0]) {
            menu_add("Open in new tab", "Middle-click", ACT_PLACE_NEW_TAB, true);
            menu_add("Disk usage\xE2\x80\xA6", "", ACT_DU_PLACE, true);
        } else {
            menu_add("Connect and open", "Click", ACT_DEV_MOUNT, true);
            menu_add("Connect and open in new tab", "Middle-click", ACT_DEV_OPEN_TAB, true);
        }
        menu_sep();
        menu_add("Rename", "", ACT_DEV_RENAME, true);
        if (dev_name_get(place_dev_key(pl))) menu_add("Reset name", "", ACT_DEV_RESET_NAME, true);
        if (pl->path[0]) {
            menu_sep();
            menu_add("Disconnect", "", ACT_NET_DISCONNECT, true);
        }
    } else if (pl->kind == PK_NETWORK) {
        menu_add("Open in new tab", "Middle-click", ACT_PLACE_NEW_TAB, true);
        { SshTarget st; if (ssh_target_for(pl->path, &st)) menu_add("Open SSH terminal", "", ACT_SSH_TERMINAL, true); }
        menu_add("Pin to Quick Access", "", ACT_PIN, true);
        menu_sep();
        menu_add("Rename", "", ACT_DEV_RENAME, true);
        if (dev_name_get(place_dev_key(pl))) menu_add("Reset name", "", ACT_DEV_RESET_NAME, true);
        menu_sep();
        menu_add("Disconnect", "", ACT_NET_DISCONNECT, true);
    } else {
        menu_add("Open in new tab", "Middle-click", ACT_PLACE_NEW_TAB, true);
        menu_add("Disk usage\xE2\x80\xA6", "", ACT_DU_PLACE, pl->kind != PK_TRASH);
        if (pl->kind == PK_DRIVE) menu_add("Snapshots\xE2\x80\xA6", "", ACT_SNAP_PLACE, true);
        if (pl->group >= 0) {
            QaGroup *g = &A.qa[pl->group];
            menu_sep();
            menu_add("Rename", "", ACT_QA_RENAME, true);
            if (g->items[pl->item].label[0]) menu_add("Reset name", "", ACT_QA_RESET_NAME, true);
            menu_add("Move up", "", ACT_QA_UP, pl->item > 0);
            menu_add("Move down", "", ACT_QA_DOWN, pl->item < g->n - 1);
            for (int k = 0; k < A.nqa && k < 16; k++) {
                if (k == pl->group) continue;
                snprintf(move_lbl[k], sizeof move_lbl[k], "Move to \xE2\x80\x9C%s\xE2\x80\x9D", A.qa[k].name);
                menu_add(move_lbl[k], "", ACT_QA_MOVE_BASE + k, true);
            }
            menu_sep();
            menu_add("New group", "", ACT_QA_NEW_GROUP, A.nqa < 16);
            menu_add("Remove from Quick Access", "", ACT_QA_REMOVE, true);
        } else if (pl->kind == PK_DRIVE) {
            bool root = !strcmp(pl->path, "/");
            if (!root) menu_add("Pin to Quick Access", "", ACT_PIN, true);
            menu_sep();
            menu_add("Rename", "", ACT_DEV_RENAME, true);
            if (dev_name_get(place_dev_key(pl))) menu_add("Reset name", "", ACT_DEV_RESET_NAME, true);
            if (!root) {
                menu_sep();
                if (pl->removable) menu_add("Eject", "", ACT_DEV_EJECT, true);
                menu_add("Unmount", "", ACT_DEV_UNMOUNT, true);
            }
        }
    }
    menu_finish(x, y);
}

static void qa_edit_begin(int g, int i) {
    A.qa_edit_g = g;
    A.qa_edit_i = i;
    const char *t = i < 0 ? A.qa[g].name : A.qa[g].items[i].label[0] ? A.qa[g].items[i].label : path_basename(A.qa[g].items[i].path);
    te_set(&A.qa_te, t, 0, (int)strlen(t));
    A.redraw = true;
}

static void dev_edit_begin(const Place *p) {
    A.qa_edit_g = QA_EDIT_DEV;
    A.qa_edit_i = -1;
    snprintf(A.dev_edit_key, sizeof A.dev_edit_key, "%s", place_dev_key(p));
    te_set(&A.qa_te, p->label, 0, (int)strlen(p->label));
    A.redraw = true;
}

static bool place_editing(const Place *p) {
    if (A.qa_edit_g == QA_EDIT_DEV) return place_is_device(p) && !strcmp(place_dev_key(p), A.dev_edit_key);
    return A.qa_edit_g >= 0 && p->group == A.qa_edit_g && (p->kind == PK_GROUP ? A.qa_edit_i < 0 : p->item == A.qa_edit_i);
}

static void qa_edit_commit(void) {
    int g = A.qa_edit_g, i = A.qa_edit_i;
    A.qa_edit_g = -1;
    if (g == QA_EDIT_DEV) { // device: typing nothing (or its own name back) resets it
        char t[64];
        snprintf(t, sizeof t, "%s", A.qa_te.buf);
        const char *old = dev_name_get(A.dev_edit_key);
        char own[64] = "";
        if (old) { // what it's called without the custom name
            dev_name_set(A.dev_edit_key, "");
            init_places();
            for (int r = A.nplaces_user; r < A.nplaces; r++)
                if (place_is_device(&A.places[r]) && !strcmp(place_dev_key(&A.places[r]), A.dev_edit_key))
                    snprintf(own, sizeof own, "%s", A.places[r].label);
        } else {
            for (int r = A.nplaces_user; r < A.nplaces; r++)
                if (place_is_device(&A.places[r]) && !strcmp(place_dev_key(&A.places[r]), A.dev_edit_key))
                    snprintf(own, sizeof own, "%s", A.places[r].label);
        }
        dev_name_set(A.dev_edit_key, strcmp(t, own) ? t : "");
        init_places();
        A.settings_dirty = true;
        return;
    }
    if (g < 0 || g >= A.nqa) return;
    char t[64];
    snprintf(t, sizeof t, "%s", A.qa_te.buf);
    if (i < 0) {
        if (t[0]) snprintf(A.qa[g].name, sizeof A.qa[g].name, "%s", t);
    } else if (i < A.qa[g].n) {
        QaItem *it = &A.qa[g].items[i];
        bool same = !strcmp(t, path_basename(it->path)); // typing the folder name back = no custom label
        snprintf(it->label, sizeof it->label, "%s", same ? "" : t);
    }
    init_places();
    A.settings_dirty = true;
}

// Move item (g, i) to group dg before index di.
static void qa_move(int g, int i, int dg, int di) {
    if (g < 0 || g >= A.nqa || dg < 0 || dg >= A.nqa || i < 0 || i >= A.qa[g].n) return;
    QaItem it = A.qa[g].items[i];
    qa_remove(g, i);
    if (dg == g && di > i) di--;
    qa_insert(dg, di, it.label, it.path);
    A.qa[dg].collapsed = false;
    init_places();
    A.settings_dirty = true;
}

static void qa_action(int act) {
    int idx = A.menu_place;
    if (idx < 0 || idx >= A.nplaces) return;
    Place pl = A.places[idx];
    int g = pl.group, i = pl.item;
    if (g < 0 || g >= A.nqa) return;
    switch (act) {
    case ACT_QA_RENAME: qa_edit_begin(g, i); return;
    case ACT_QA_GROUP_RENAME: qa_edit_begin(g, -1); return;
    case ACT_QA_RESET_NAME: A.qa[g].items[i].label[0] = 0; break;
    case ACT_QA_UP: qa_move(g, i, g, i - 1); return;
    case ACT_QA_DOWN: qa_move(g, i, g, i + 2); return;
    case ACT_QA_REMOVE:
        toast("Removed \xE2\x80\x9C%s\xE2\x80\x9D from Quick Access", pl.label);
        qa_remove(g, i);
        break;
    case ACT_QA_NEW_GROUP: {
        int ng = qa_add_group("New group");
        if (i >= 0) { qa_move(g, i, ng, 0); } // started from an item: it moves into the new group
        init_places();
        qa_edit_begin(ng, -1);
        A.settings_dirty = true;
        return;
    }
    case ACT_QA_GROUP_TOGGLE: A.qa[g].collapsed = !A.qa[g].collapsed; break;
    case ACT_QA_GROUP_UP:
    case ACT_QA_GROUP_DOWN: {
        int o = act == ACT_QA_GROUP_UP ? g - 1 : g + 1;
        if (o < 0 || o >= A.nqa) return;
        QaGroup t = A.qa[g];
        A.qa[g] = A.qa[o];
        A.qa[o] = t;
        break;
    }
    case ACT_QA_GROUP_DELETE: { // folders move to the neighbouring group, nothing is lost
        if (A.nqa < 2) return;
        int to = g > 0 ? g - 1 : 1;
        for (int k = 0; k < A.qa[g].n; k++) qa_insert(to, -1, A.qa[g].items[k].label, A.qa[g].items[k].path);
        toast("Deleted group \xE2\x80\x9C%s\xE2\x80\x9D; its folders moved to \xE2\x80\x9C%s\xE2\x80\x9D", A.qa[g].name, A.qa[to].name);
        free(A.qa[g].items);
        memmove(&A.qa[g], &A.qa[g + 1], (size_t)(A.nqa - g - 1) * sizeof(QaGroup));
        A.nqa--;
        break;
    }
    default:
        if (act >= ACT_QA_MOVE_BASE && act < ACT_QA_MOVE_BASE + 16) { qa_move(g, i, act - ACT_QA_MOVE_BASE, -1); return; }
        return;
    }
    init_places();
    A.settings_dirty = true;
}

// Insertion point for a sidebar drag at y (rows of Quick Access only).
static void qa_drop_point(f32 y, int *dg, int *di) {
    *dg = -1;
    *di = -1;
    for (int r = 0; r < A.nplaces_user; r++) {
        Rect rr = place_rect(r);
        Place *pl = &A.places[r];
        if (y >= rr.y + rr.h && r != A.nplaces_user - 1) continue;
        if (pl->kind == PK_GROUP) { *dg = pl->group; *di = 0; }
        else { *dg = pl->group; *di = y < rr.y + rr.h * 0.5f ? pl->item : pl->item + 1; }
        if (y >= rr.y + rr.h) { *dg = pl->group; *di = A.qa[pl->group].n; } // below everything
        return;
    }
}

// Pin folders (dragged from a pane or another app) into a Quick Access group.
static void qa_pin_paths(char **paths, int n, int g) {
    int added = 0;
    for (int k = 0; k < n; k++) {
        struct stat st;
        if (stat(paths[k], &st) != 0 || !S_ISDIR(st.st_mode) || qa_find(paths[k], NULL, NULL)) continue;
        qa_insert(g, -1, "", paths[k]);
        added++;
    }
    A.qa[g].collapsed = false;
    if (added) toast("Pinned %d folder%s to \xE2\x80\x9C%s\xE2\x80\x9D", added, added == 1 ? "" : "s", A.qa[g].name);
    else toast("Only folders can be pinned");
    init_places();
    A.settings_dirty = true;
}

static void menu_finish(f32 x, f32 y) {
    Menu *m = &A.menu;
    bool reopen = m->open && m->ox == x && m->oy == y; // keep_open menus rebuild in place: keep the scroll
    if (!reopen) m->scroll = 0;
    m->ox = x;
    m->oy = y;
    Font *f = &A.font;
    f32 lw = 0, sw = 0, h = roundf(S(10));
    for (int i = 0; i < m->n; i++) {
        MenuItem *it = &m->items[i];
        h += menu_item_h(it);
        if (it->sep) continue;
        lw = MAX(lw, text_width(f, it->label, (int)strlen(it->label)));
        sw = MAX(sw, text_width(&A.font_small, it->shortcut, (int)strlen(it->shortcut)));
    }
    bool anycheck = false;
    for (int i = 0; i < m->n; i++) anycheck |= m->items[i].checkable;
    f32 w = roundf(lw + sw + S(64) + (anycheck ? S(14) : 0));
    x = MAX(S(4), MIN(x, A.W - w - S(4)));
    m->content_h = h;
    f32 avail = (f32)A.H - S(8);
    if (h > avail) h = avail; // too tall for the window: scroll inside
    if (y + h > A.H - S(4)) y = y - h >= S(4) ? y - h : MAX(S(4), A.H - S(4) - h);
    m->r = (Rect){roundf(x), roundf(y), w, h};
    m->scroll = CLAMP(m->scroll, 0.f, MAX(0.f, m->content_h - m->r.h));
    m->open = true;
    A.redraw = true;
}

static bool menu_scrollable(void) { return A.menu.content_h > A.menu.r.h + 0.5f; }

// Scroll arrows at the top/bottom edge of a scrolling menu: -1 up, 1 down, 0 none.
static int menu_arrow_at(f32 y) {
    Menu *m = &A.menu;
    if (!menu_scrollable()) return 0;
    f32 ah = roundf(S(18));
    if (m->scroll > 0 && y < m->r.y + ah) return -1;
    if (m->scroll < m->content_h - m->r.h && y >= m->r.y + m->r.h - ah) return 1;
    return 0;
}

static void menu_scroll_by(f32 dy) {
    Menu *m = &A.menu;
    m->scroll = CLAMP(m->scroll + dy, 0.f, MAX(0.f, m->content_h - m->r.h));
    A.redraw = true;
}

// Keep item i inside the visible part of the menu.
static void menu_reveal(int i) {
    Menu *m = &A.menu;
    if (!menu_scrollable() || i < 0) return;
    f32 top = roundf(S(5));
    for (int k = 0; k < i; k++) top += menu_item_h(&m->items[k]);
    f32 h = menu_item_h(&m->items[i]), ah = roundf(S(18));
    if (top - ah < m->scroll) m->scroll = top - ah;
    if (top + h + ah > m->scroll + m->r.h) m->scroll = top + h + ah - m->r.h;
    menu_scroll_by(0);
}

static int menu_item_at(f32 x, f32 y) {
    Menu *m = &A.menu;
    if (!in_rect(m->r, x, y)) return -1;
    if (menu_arrow_at(y)) return -1;
    f32 yy = m->r.y + roundf(S(5)) - m->scroll;
    for (int i = 0; i < m->n; i++) {
        f32 h = menu_item_h(&m->items[i]);
        if (y >= yy && y < yy + h) return (m->items[i].sep || !m->items[i].enabled) ? -1 : i;
        yy += h;
    }
    return -1;
}

static void menu_activate(int i) {
    Menu *m = &A.menu;
    m->open = false;
    A.redraw = true;
    if (i < 0 || i >= m->n || !m->items[i].enabled || m->items[i].sep) return;
    bool keep = m->keep_open && m->items[i].checkable;
    void (*reopen)(struct Pane *, f32, f32) = m->reopen;
    f32 ox = m->ox, oy = m->oy;
    Pane *mp = m->pane;
    if (mp && pane_exists(mp)) do_action(mp, m->items[i].act);
    if (keep && reopen && mp && pane_exists(mp)) {
        int hv = i;
        reopen(mp, ox, oy);
        A.menu.hover = hv;
    }
}

static void menu_key(SDL_Keycode k) {
    Menu *m = &A.menu;
    int dir = k == SDLK_DOWN ? 1 : k == SDLK_UP ? -1 : 0;
    if (dir) {
        int i = m->hover;
        for (int step = 0; step < m->n; step++) {
            i = (i + dir + m->n) % m->n;
            if (!m->items[i].sep && m->items[i].enabled) break;
        }
        m->hover = i;
        menu_reveal(i);
    } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
        menu_activate(m->hover);
    } else if (k == SDLK_ESCAPE) {
        m->open = false;
    } else if (k == SDLK_PAGEDOWN || k == SDLK_PAGEUP) {
        menu_scroll_by((k == SDLK_PAGEDOWN ? 1 : -1) * m->r.h * 0.8f);
    }
    A.redraw = true;
}

// --- drag & drop -----------------------------------------------------------

static int place_at(f32 y) {
    for (int i = 0; i < A.nplaces; i++) {
        Rect r = place_rect(i);
        if (y >= r.y && y < r.y + r.h) return i;
    }
    return -1;
}

// Is everything dragged out of p a folder? (Then it can be opened as tabs.)
static bool drag_all_dirs(Pane *p) {
    if (!p->order || !p->sel_count || pane_du_on(p)) return false;
    for (u32 i = 0; i < p->count; i++)
        if (p->sel[p->order[i]] && !(p->L->entries[p->order[i]].flags & ENT_DIR)) return false;
    return true;
}

// Thin strip along a panel's edges (below its tab bar) where dragged folders open in a new split; the rest of
// the panel stays a normal drop. ZONE_* or -1.
static int panel_edge_zone(Panel *pn, f32 x, f32 y) {
    Rect a = {pn->area.x, pn->bar.y + pn->bar.h, pn->area.w, pn->area.h - pn->bar.h};
    if (!in_rect(a, x, y)) return -1;
    f32 bw = MIN(S(44), a.w * 0.15f), bh = MIN(S(44), a.h * 0.15f);
    f32 d[4] = {(x - a.x) / bw, (a.x + a.w - x) / bw, (y - a.y) / bh, (a.y + a.h - y) / bh};
    int zone = -1;
    f32 best = 1.f;
    for (int k = 0; k < 4; k++)
        if (d[k] < best) { best = d[k]; zone = k; }
    return zone;
}

// Open folders dragged onto a panel edge (zone) in a new split there, or (zone < 0) as new tabs in panel pn.
static void open_dirs_at(Panel *pn, int zone, char **paths, int n) {
    if (!n || !panel_alive(pn)) return;
    Panel *np = zone >= 0 ? panel_split(pn, zone, NULL, paths[0]) : pn;
    if (!np) return;
    for (int i = zone >= 0 ? 1 : 0; i < n; i++) tab_new_in(np, paths[i], panel_end_index(np), zone < 0 && i == 0);
    panel_activate(np);
    layout();
}

// Is every item dragged out of src already directly inside dir? (Then dropping there would do nothing.) Tree
// view rows are relative paths, so an item from an expanded subfolder can be dropped onto the pane's own folder.
static bool drag_all_in(Pane *src, const char *dir) {
    if (pane_du_on(src) || !src->order) return !strcmp(dir, src->path);
    bool use_cursor = !src->sel_count && src->cursor >= 0;
    char parent[PATH_MAX];
    for (u32 i = 0; i < src->count; i++) {
        if (use_cursor ? (i64)i != src->cursor : !src->sel[src->order[i]]) continue;
        const char *name = src->L->entries[src->order[i]].name, *sl = strrchr(name, '/');
        if (!sl) { if (strcmp(dir, src->path)) return false; continue; }
        path_join(parent, sizeof parent, src->path, name);
        *strrchr(parent, '/') = 0;
        if (strcmp(dir, parent)) return false;
    }
    return true;
}

// Find the folder under (x,y) that items would be dropped into. src may be NULL (external drop).
static void drop_target(Pane *src, f32 x, f32 y) {
    A.drop_dir[0] = 0;
    A.drop_qa = -1;
    A.drop_pane = NULL;
    A.drop_row = -1;
    A.drop_place = -1;
    A.drop_tab = NULL;
    A.drop_split_panel = NULL;
    A.drop_newtab = NULL;
    bool dirs = src && drag_all_dirs(src);
    Panel *bp = panel_at(x, y);
    int zone = dirs && bp ? panel_edge_zone(bp, x, y) : -1;
    if (bp && in_rect(bp->bar, x, y)) {
        for (int i = 0; i < A.ntabs; i++)
            if (A.tabs[i]->panel == bp && in_rect(A.tabs[i]->r, x, y)) {
                A.drop_tab = A.tabs[i];
                snprintf(A.drop_dir, sizeof A.drop_dir, "%s", A.tabs[i]->pane[0].path);
            }
        if (!A.drop_tab && dirs) { A.drop_newtab = bp; return; }
    } else if (zone >= 0) {
        A.drop_split_panel = bp;
        A.drop_split = zone;
        return;
    } else if (in_rect(A.r_sidebar, x, y)) {
        int idx = place_at(y);
        if (idx >= 0 && A.places[idx].kind == PK_GROUP) { // onto a group header: pin the dragged folders there
            A.drop_place = idx;
            A.drop_qa = A.places[idx].group;
        } else if (idx >= 0 && A.places[idx].path[0]) {
            A.drop_place = idx;
            snprintf(A.drop_dir, sizeof A.drop_dir, "%s", A.places[idx].path);
        }
    } else {
        Pane *p = pane_at(x, y);
        if (p && p->order) {
            A.drop_pane = p;
            i64 r = in_rect(p->ly.list, x, y) ? item_at(p, x, y) : -1;
            if (r >= 0) {
                Entry *e = &p->L->entries[p->order[r]];
                bool dragged = p == src && p->sel[p->order[r]];
                if ((e->flags & ENT_DIR) && !dragged) {
                    A.drop_row = r;
                    path_join(A.drop_dir, sizeof A.drop_dir, p->path, e->name);
                }
            }
            if (!A.drop_dir[0]) snprintf(A.drop_dir, sizeof A.drop_dir, "%s", p->path);
        }
    }
    if (A.drop_dir[0] && strstr(A.drop_dir, "::")) { // can't drop into an archive
        A.drop_dir[0] = 0;
        A.drop_tab = NULL;
        A.drop_pane = NULL;
        A.drop_row = -1;
    }
    if (src && A.drop_dir[0] && drag_all_in(src, A.drop_dir)) {
        A.drop_dir[0] = 0;
        A.drop_tab = NULL;
        A.drop_pane = NULL;
        A.drop_row = -1;
        A.drop_place = -1;
    }
    if (!A.drop_dir[0]) return;
    SDL_Keymod mod = SDL_GetModState();
    if (mod & SDL_KMOD_CTRL) A.drop_copy = true;
    else if (mod & SDL_KMOD_SHIFT) A.drop_copy = false;
    else {
        struct stat a, b;
        A.drop_copy = !(src && stat(src->path, &a) == 0 && stat(A.drop_dir, &b) == 0 && a.st_dev == b.st_dev);
    }
}

// --- streaming search results / folder sizes (main thread) -----------------

static Pane *pane_by_listing(const Listing *L) {
    for (int i = 0; i < A.ntabs; i++)
        for (int j = 0; j < A.tabs[i]->npanes; j++)
            if (A.tabs[i]->pane[j].L == L) return &A.tabs[i]->pane[j];
    return NULL;
}

static struct { u64 hits, batches; bool done; } g_search_bench; // --bench DIR QUERY

static void search_batch_cb(void *arg) {
    SearchBatch *b = arg;
    g_search_bench.hits += (u64)b->n;
    g_search_bench.batches++;
    g_search_bench.done |= b->done;
    Pane *p = pane_by_listing(b->L);
    if (!p && (b->n || b->done)) { // first results of a staged search: show it now
        for (int i = 0; i < A.ntabs && !p; i++)
            for (int j = 0; j < A.tabs[i]->npanes; j++)
                if (A.tabs[i]->pane[j].L_next == b->L) {
                    p = &A.tabs[i]->pane[j];
                    search_swap(p);
                    break;
                }
    }
    if (p && b->n) {
        Listing *L = p->L;
        u32 need = L->count + (u32)b->n;
        if (need > L->cap) {
            u32 nc = MAX(need, L->cap * 2);
            L->entries = xrealloc(L->entries, nc * sizeof(Entry));
            p->order = xrealloc(p->order, nc * sizeof(u32));
            p->sel = xrealloc(p->sel, nc);
            memset(p->sel + L->cap, 0, nc - L->cap);
            L->cap = nc;
        }
        for (int i = 0; i < b->n; i++) {
            SearchHit *h = &b->hits[i];
            const char *rel = b->strs + h->rel;
            // the search may still run with a broader query than the one typed now
            if (!search_q_matches(p->search_q, rel_base(rel))) continue;
            Entry *e = &L->entries[L->count];
            size_t len = strlen(rel);
            char *s = arena_alloc(&L->names, len + 1, 1);
            memcpy(s, rel, len + 1);
            e->name = s;
            e->name_len = (u32)len;
            e->flags = h->flags;
            e->mflags = h->mflags;
            e->size = h->size;
            e->mtime = h->mtime;
            e->btime = h->btime;
            e->mode = h->mode;
            e->uid = h->uid;
            atomic_init(&e->meta, META_READY);
            e->ext = 0;
            if (!(e->flags & ENT_DIR)) {
                const char *base = strrchr(s, '/');
                base = base ? base + 1 : s;
                const char *dot = strrchr(base, '.');
                if (dot && dot != base && dot[1]) e->ext = (u16)(dot - s + 1);
            }
            p->order[p->count++] = L->count;
            L->count++;
        }
        if (p->cursor < 0 && p->count) p->cursor = p->anchor = 0;
        A.redraw = true;
    }
    if (p && b->done) {
        p->L->t_meta = now_ns();
        atomic_store(&p->L->state, LST_COMPLETE);
        A.redraw = true;
    }
    free(b->hits);
    free(b->strs);
    listing_release(b->L);
    free(b);
}

static void dirsize_done_cb(void *arg) {
    Listing *L = arg;
    Pane *p = pane_by_listing(L);
    if (p && p->sort_key == SORT_SIZE) pane_resort(p);
    A.redraw = true;
    listing_release(L);
}

// --- contents search (main thread; search.c does the work) -------------------------------------------------
// Ctrl+Shift+F, or the Contents switch in the search bar: text inside the files under the folder. Results are a
// search listing where each matching file is followed by its matching lines (ENT_MATCH rows, unfolded; Left/Right
// or the chevron fold them). "in:*.c" in the query limits the files. Enter on a line opens the editor at it.

// Match entries pack where the match is: column in the shown text, length, "text starts mid-line", column in the file.
#define CM_COL(e) ((u32)((e)->btime & 0xFFFF))
#define CM_LEN(e) ((u32)(((u64)(e)->btime >> 16) & 0xFFFF))
#define CM_ELIDED(e) ((((u64)(e)->btime >> 32) & 1) != 0)
#define CM_RCOL(e) ((u32)((u64)(e)->btime >> 33))

// "needle in:*.c" -> text "needle", glob "*.c"
static void content_split_query(const char *q, char *text, size_t tcap, char *glob, size_t gcap) {
    glob[0] = 0;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", q);
    char *at = NULL;
    for (char *k = buf; (k = strstr(k, "in:")); k++)
        if (k == buf || k[-1] == ' ') { at = k; break; }
    if (at) {
        char *e = at + 3;
        size_t gl = strcspn(e, " ");
        snprintf(glob, gcap, "%.*s", (int)gl, e);
        memmove(at, e + gl, strlen(e + gl) + 1);
    }
    char *b = buf;
    while (*b == ' ') b++;
    size_t l = strlen(b);
    while (l && b[l - 1] == ' ') b[--l] = 0;
    snprintf(text, tcap, "%s", b);
}

static bool content_query_ok(const char *q) {
    char text[256], glob[128];
    content_split_query(q, text, sizeof text, glob, sizeof glob);
    CQuery t;
    bool ok = cq_init(&t, text, A.cs_case, A.cs_word, A.cs_regex);
    cq_free(&t);
    return ok;
}

static Listing *content_listing_new(Pane *p, const char *q) {
    Listing *L = listing_new(p->path, p->sort_key, p->sort_desc, A.show_hidden);
    L->is_search = L->is_content = true;
    L->cap = 1024;
    L->entries = xmalloc(L->cap * sizeof(Entry));
    L->cinfo = xcalloc(L->cap, sizeof(u32));
    L->ctext = xcalloc(L->cap, sizeof(char *));
    L->t_names = L->t_start;
    atomic_store(&L->state, LST_READY);
    L->search_recursive = A.search_recursive;
    char text[256], glob[128];
    content_split_query(q, text, sizeof text, glob, sizeof glob);
    L->cq = xmalloc(sizeof *L->cq);
    cq_init(L->cq, text, A.cs_case, A.cs_word, A.cs_regex);
    bool use_rg = !A.selftest || getenv("FE_RG"); // tests: the built-in search, or a stand-in ripgrep
    content_start(L, p->path, text, glob, A.cs_case, A.cs_word, A.cs_regex, A.show_hidden, A.search_recursive, use_rg);
    return L;
}

static void pane_load_content(Pane *p, const char *q) {
    char qq[256];
    snprintf(qq, sizeof qq, "%s", q);
    if (p->view != VIEW_DETAILS) { p->cs_view = p->view + 1; p->view = VIEW_DETAILS; } // lines need rows (restored after)
    if (p->L && p->count) { // keep the results on screen until the new search finds something
        if (p->L_next) {
            atomic_store(&p->L_next->cancel, 1);
            listing_release(p->L_next);
        }
        snprintf(p->search_next_q, sizeof p->search_next_q, "%s", qq);
        p->L_next = content_listing_new(p, qq);
        return;
    }
    search_install(p, content_listing_new(p, qq), qq);
}

// Rebuild the visible rows: every file (in the sorted order) followed by its lines unless it is folded.
static void content_rows(Pane *p) {
    Listing *L = p->L;
    if (!L || !L->is_content || !p->order) return;
    u32 cur_e = p->cursor >= 0 && p->cursor < (i64)p->count ? p->order[p->cursor] : UINT32_MAX;
    u32 anc_e = p->anchor >= 0 && p->anchor < (i64)p->count ? p->order[p->anchor] : UINT32_MAX;
    u32 *o = xmalloc((size_t)MAX(L->count, 1u) * sizeof(u32)), m = 0;
    for (u32 i = 0; i < p->count; i++) {
        u32 ei = p->order[i];
        if (L->entries[ei].flags & ENT_MATCH) continue;
        o[m++] = ei;
        u32 ci = L->cinfo[ei];
        for (u32 k = 1; k <= (ci & ~CI_COLLAPSED); k++) {
            if (!(ci & CI_COLLAPSED)) o[m++] = ei + k;
            else if (p->sel[ei + k]) { p->sel[ei + k] = 0; p->sel_count--; }
        }
    }
    free(p->order);
    p->order = o;
    p->count = m;
    // the cursor stays on its row, or moves to the file when its line got folded away
    if (cur_e != UINT32_MAX && (L->entries[cur_e].flags & ENT_MATCH) && (L->cinfo[cur_e - L->cinfo[cur_e]] & CI_COLLAPSED))
        cur_e -= L->cinfo[cur_e];
    p->cursor = p->anchor = -1;
    for (u32 i = 0; i < m; i++) {
        if (o[i] == cur_e) p->cursor = i;
        if (o[i] == anc_e) p->anchor = i;
    }
    if (p->cursor < 0 && m) p->cursor = 0;
    if (p->anchor < 0) p->anchor = p->cursor;
    p->scroll_target = CLAMP(p->scroll_target, 0.0, max_scroll(p));
    p->scroll = CLAMP(p->scroll, 0.0, max_scroll(p));
    A.redraw = true;
}

static u32 content_file_of(const Listing *L, u32 ei) { return (L->entries[ei].flags & ENT_MATCH) ? ei - L->cinfo[ei] : ei; }

// Fold (want 0), unfold (1) or toggle (-1) the lines of the file at (or above) row pos.
static void content_fold(Pane *p, i64 pos, int want) {
    Listing *L = p->L;
    if (!L || !L->is_content || pos < 0 || pos >= (i64)p->count) return;
    u32 fe = content_file_of(L, p->order[pos]);
    bool folded = L->cinfo[fe] & CI_COLLAPSED;
    bool fold = want < 0 ? !folded : want == 0;
    if (fold == folded || !(L->cinfo[fe] & ~CI_COLLAPSED)) return;
    if (fold) L->cinfo[fe] |= CI_COLLAPSED;
    else L->cinfo[fe] &= ~CI_COLLAPSED;
    content_rows(p);
    for (u32 i = 0; i < p->count; i++)
        if (p->order[i] == fe) { if (fold || p->cursor < 0) p->cursor = p->anchor = i; break; }
    ensure_visible(p, p->cursor);
}

static void content_fold_all(Pane *p, bool fold) {
    Listing *L = p->L;
    if (!L || !L->is_content) return;
    for (u32 i = 0; i < L->count; i++)
        if (!(L->entries[i].flags & ENT_MATCH) && (L->cinfo[i] & ~CI_COLLAPSED)) {
            if (fold) L->cinfo[i] |= CI_COLLAPSED;
            else L->cinfo[i] &= ~CI_COLLAPSED;
        }
    content_rows(p);
    ensure_visible(p, p->cursor);
}

static void content_grow(Listing *L, Pane *p, u32 need) {
    if (need <= L->cap) return;
    u32 nc = MAX(need, L->cap * 2);
    L->entries = xrealloc(L->entries, nc * sizeof(Entry));
    L->cinfo = xrealloc(L->cinfo, nc * sizeof(u32));
    L->ctext = xrealloc(L->ctext, nc * sizeof(char *));
    p->order = xrealloc(p->order, nc * sizeof(u32));
    p->sel = xrealloc(p->sel, nc);
    memset(p->sel + L->cap, 0, nc - L->cap);
    L->cap = nc;
}

static void content_batch_cb(void *arg) {
    ContentBatch *b = arg;
    Pane *p = pane_by_listing(b->L);
    if (!p && (b->nf || b->done)) { // first results of a staged search: show it now
        for (int i = 0; i < A.ntabs && !p; i++)
            for (int j = 0; j < A.tabs[i]->npanes; j++)
                if (A.tabs[i]->pane[j].L_next == b->L) {
                    p = &A.tabs[i]->pane[j];
                    search_swap(p);
                    break;
                }
    }
    if (p && b->nf) {
        Listing *L = p->L;
        content_grow(L, p, L->count + b->nf + b->nl);
        for (u32 i = 0; i < b->nf; i++) {
            CHitFile *h = &b->f[i];
            const char *rel = b->strs + h->rel;
            u32 fe = L->count;
            Entry *e = &L->entries[fe];
            size_t len = strlen(rel);
            char *s = arena_alloc(&L->names, len + 1, 1);
            memcpy(s, rel, len + 1);
            *e = (Entry){.name = s, .name_len = (u32)len, .flags = h->flags, .mflags = h->mflags, .mode = h->mode, .uid = h->uid,
                         .size = h->size, .mtime = h->mtime, .btime = h->btime};
            atomic_init(&e->meta, META_READY);
            const char *base = strrchr(s, '/');
            base = base ? base + 1 : s;
            const char *dot = strrchr(base, '.');
            if (dot && dot != base && dot[1]) e->ext = (u16)(dot - s + 1);
            L->cinfo[fe] = h->nl;
            L->ctext[fe] = NULL;
            p->order[p->count++] = fe;
            L->count++;
            for (u32 k = 0; k < h->nl; k++) {
                CHitLine *hl = &b->l[h->l0 + k];
                u32 me = L->count;
                Entry *m = &L->entries[me];
                *m = *e;
                atomic_init(&m->meta, META_READY);
                m->flags = ENT_MATCH | (h->flags & ENT_HIDDEN);
                m->size = hl->line;
                m->btime = (i64)((u64)MIN(hl->col, 0xFFFFu) | ((u64)MIN(hl->len, 0xFFFFu) << 16) | ((u64)hl->elided << 32) |
                                 ((u64)MIN(hl->rcol, 0x7FFFFFFFu) << 33));
                const char *t = b->strs + hl->text;
                size_t tl = strlen(t);
                char *ts = arena_alloc(&L->names, tl + 1, 1);
                memcpy(ts, t, tl + 1);
                L->ctext[me] = ts;
                L->cinfo[me] = k + 1;
                p->order[p->count++] = me;
                L->count++;
            }
            L->nfiles++;
            L->nmatches += h->nl;
        }
        if (p->cursor < 0 && p->count) p->cursor = p->anchor = 0;
        A.redraw = true;
    }
    if (p) p->L->ctrunc |= b->trunc;
    if (p && b->done) {
        p->L->t_meta = now_ns();
        atomic_store(&p->L->state, LST_COMPLETE);
        pane_resort(p); // results came in as found: now in the pane's sort order (lines stay under their file)
        A.redraw = true;
    }
    free(b->f);
    free(b->l);
    free(b->strs);
    listing_release(b->L);
    free(b);
}

// --- opening a file at a line: the default editor when it understands line numbers, else $VISUAL / $EDITOR in a
// terminal tab, else the normal way.

typedef struct { const char *id; int style; } EditorStyle;
enum { ED_GOTO, ED_COLON, ED_KATE, ED_PLUS, ED_PLUS_COL };
static const EditorStyle g_editor_styles[] = { // matched against the .desktop file name, case-insensitively
    {"code", ED_GOTO}, {"codium", ED_GOTO}, {"cursor", ED_GOTO}, {"windsurf", ED_GOTO},
    {"zed", ED_COLON}, {"sublime", ED_COLON}, {"subl", ED_COLON},
    {"kate", ED_KATE}, {"kwrite", ED_KATE},
    {"texteditor", ED_PLUS_COL}, {"gedit", ED_PLUS}, {"geany", ED_PLUS}, {"pluma", ED_PLUS}, {"xed", ED_PLUS},
    {"gvim", ED_PLUS}, {"neovide", ED_PLUS}, {"emacs", ED_PLUS_COL},
};

static int editor_style(const char *name) {
    for (size_t i = 0; i < ARRAY_COUNT(g_editor_styles); i++)
        if (str_has_ci(name, g_editor_styles[i].id)) return g_editor_styles[i].style;
    return -1;
}

// argv for "open file at line:col" in style st (strings in buf).
static int editor_argv(int st, const char *exe, const char *file, u32 line, u32 col, char *argv[8], char buf[2][PATH_MAX + 32]) {
    int n = 0;
    argv[n++] = (char *)exe;
    switch (st) {
    case ED_GOTO: argv[n++] = "-g"; snprintf(buf[0], sizeof buf[0], "%s:%u:%u", file, line, col); argv[n++] = buf[0]; break;
    case ED_COLON: snprintf(buf[0], sizeof buf[0], "%s:%u:%u", file, line, col); argv[n++] = buf[0]; break;
    case ED_KATE:
        snprintf(buf[0], sizeof buf[0], "%u", line);
        snprintf(buf[1], sizeof buf[1], "%u", col ? col - 1 : 0);
        argv[n++] = "-l"; argv[n++] = buf[0]; argv[n++] = "-c"; argv[n++] = buf[1]; argv[n++] = (char *)file;
        break;
    case ED_PLUS_COL: snprintf(buf[0], sizeof buf[0], "+%u:%u", line, col); argv[n++] = buf[0]; argv[n++] = (char *)file; break;
    default: snprintf(buf[0], sizeof buf[0], "+%u", line); argv[n++] = buf[0]; argv[n++] = (char *)file; break;
    }
    argv[n] = NULL;
    return n;
}

static bool open_at_line(const char *path, u32 line, u32 col) {
    char *argv[8], buf[2][PATH_MAX + 32];
    // 1. the default app for the file, if it's an editor we know
    DesktopApp *a = app_default_for(mime_for_file(path_basename(path), false));
    if (!a) a = app_default_for("text/plain");
    int st = a && !a->terminal ? editor_style(a->id) : -1;
    if (st >= 0 && a->exec && strncmp(a->exec, "flatpak", 7)) {
        char exe[PATH_MAX];
        snprintf(exe, sizeof exe, "%s", a->exec);
        exe[strcspn(exe, " \t")] = 0;
        char *k = exe[0] == '"' ? exe + 1 : exe;
        k[strcspn(k, "\"")] = 0;
        if (!strcmp(path_basename(k), "env")) st = -1; // "env VAR=x app": too clever to rewrite, open normally
        if (st >= 0) {
            editor_argv(st, k, path, line, col, argv, buf);
            if (A.selftest) { snprintf(A.last_opened, sizeof A.last_opened, "%s %s %s", argv[0], argv[1], argv[2] ? argv[2] : ""); return true; }
            pid_t pid;
            if (posix_spawnp(&pid, argv[0], NULL, spawn_attr(), argv, environ) == 0) return true;
        }
    }
    // 2. a terminal editor from $VISUAL / $EDITOR, in a terminal tab
    const char *ed = getenv("VISUAL");
    if (!ed || !ed[0]) ed = getenv("EDITOR");
    if (ed && ed[0] && !strchr(ed, ' ')) {
        const char *b = path_basename(ed);
        static const char *const term_eds[] = {"nvim", "vim", "vi", "nano", "micro", "hx", "helix", "kak", "emacs", "ne", "joe"};
        bool known = false;
        for (size_t i = 0; i < ARRAY_COUNT(term_eds); i++) known |= !strcmp(b, term_eds[i]);
        if (known) {
            int n = 0;
            argv[n++] = (char *)ed;
            if (!strcmp(b, "hx") || !strcmp(b, "helix") || !strcmp(b, "micro")) {
                snprintf(buf[0], sizeof buf[0], "%s:%u:%u", path, line, col);
                argv[n++] = buf[0];
            } else {
                if (!strcmp(b, "emacs")) argv[n++] = "-nw";
                if (!strcmp(b, "nano")) snprintf(buf[0], sizeof buf[0], "+%u,%u", line, col);
                else if (!strcmp(b, "kak") || !strcmp(b, "emacs")) snprintf(buf[0], sizeof buf[0], "+%u:%u", line, col);
                else snprintf(buf[0], sizeof buf[0], "+%u", line);
                argv[n++] = buf[0];
                argv[n++] = (char *)path;
            }
            argv[n] = NULL;
            if (A.selftest) { snprintf(A.last_opened, sizeof A.last_opened, "term: %s %s %s", argv[0], argv[1], argv[2] ? argv[2] : ""); return true; }
            char dir[PATH_MAX];
            parent_of(path, dir, sizeof dir);
            if (term_tab_open(dir, argv)) return true;
        }
    }
    open_path(path); // 3. no line number then
    return false;
}

// Enter / double-click on a matching line.
static void content_open_line(Pane *p, i64 pos) {
    if (!p->L || !p->L->is_content || pos < 0 || pos >= (i64)p->count) return;
    Entry *e = &p->L->entries[p->order[pos]];
    if (!(e->flags & ENT_MATCH)) return;
    char full[PATH_MAX];
    path_join(full, sizeof full, p->path, e->name);
    open_at_line(full, (u32)e->size, CM_RCOL(e) + 1);
}

// --- live refresh (inotify) ------------------------------------------------

typedef struct {
    char path[PATH_MAX];
    int wd;
    bool used, dirty;
    u64 first, last, last_refresh;
} Watch;

#define MAX_WATCHES 128
static Watch g_w[MAX_WATCHES];
static int g_nw;

#define WATCH_MASK (IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR)

static bool path_is_remote(const char *path);
// inotify doesn't see changes on network filesystems, and adding a watch there can block on a dead server
static bool pane_watchable(const Pane *p) {
    return p->L && !p->L->is_search && p->path[0] && !pane_is_archive(p) && !path_is_remote(p->path);
}

// Does this pane show the contents of path (its folder, or an expanded subfolder in the tree view)?
static bool pane_watches(const Pane *p, const char *path) {
    if (!strcmp(p->path, path)) return true;
    if (!p->L || !p->L->tree) return false;
    size_t l = strlen(p->path);
    if (strncmp(path, p->path, l) || (path[l] != '/' && l > 1)) return false;
    const char *rel = path + (l > 1 ? l + 1 : 1);
    for (int i = 0; i < p->nexpanded; i++)
        if (!strcmp(p->expanded[i], rel)) return true;
    return false;
}

static void watch_sync(void) {
    if (g_ino < 0) return;
    for (int i = 0; i < g_nw; i++) g_w[i].used = false;
    for (int t = 0; t < A.ntabs; t++)
        for (int j = 0; j < A.tabs[t]->npanes; j++) {
            Pane *p = &A.tabs[t]->pane[j];
            if (!pane_watchable(p)) continue;
            int nw = 1 + (p->L->tree ? p->nexpanded : 0); // tree view: expanded subfolders too
            for (int x = 0; x < nw; x++) {
                char path[PATH_MAX];
                if (x == 0) snprintf(path, sizeof path, "%s", p->path);
                else path_join(path, sizeof path, p->path, p->expanded[x - 1]);
                int k = -1;
                for (int i = 0; i < g_nw; i++)
                    if (!strcmp(g_w[i].path, path)) k = i;
                if (k < 0 && g_nw < MAX_WATCHES) {
                    int wd = inotify_add_watch(g_ino, path, WATCH_MASK);
                    if (wd < 0) continue;
                    k = g_nw++;
                    memset(&g_w[k], 0, sizeof g_w[k]);
                    snprintf(g_w[k].path, sizeof g_w[k].path, "%s", path);
                    g_w[k].wd = wd;
                }
                if (k >= 0) g_w[k].used = true;
            }
        }
    int j = 0;
    for (int i = 0; i < g_nw; i++) {
        if (g_w[i].used) { g_w[j++] = g_w[i]; continue; }
        bool shared = false; // the same inode may be watched through another path
        for (int k = 0; k < g_nw; k++) shared |= k != i && g_w[k].used && g_w[k].wd == g_w[i].wd;
        if (!shared) inotify_rm_watch(g_ino, g_w[i].wd);
    }
    g_nw = j;
}

static void watch_process(void) {
    if (g_ino < 0) return;
    int wds[256];
    int n = watch_take_dirty(wds, 256);
    u64 now = now_ns();
    for (int i = 0; i < g_nw && n; i++) {
        bool hit = n < 0;
        for (int k = 0; k < n && !hit; k++) hit = wds[k] == g_w[i].wd;
        if (!hit) continue;
        dscache_invalidate_up(g_w[i].path);
        if (!g_w[i].dirty) { g_w[i].dirty = true; g_w[i].first = now; }
        g_w[i].last = now;
    }
    for (int i = 0; i < g_nw; i++) {
        Watch *w = &g_w[i];
        if (!w->dirty) continue;
        // Debounce bursts, and never rescan more often than the scan itself costs (x5).
        u64 gap = 150000000ull;
        bool blocked = false;
        for (int t = 0; t < A.ntabs; t++)
            for (int j = 0; j < A.tabs[t]->npanes; j++) {
                Pane *p = &A.tabs[t]->pane[j];
                if (!pane_watchable(p) || !pane_watches(p, w->path)) continue;
                if (atomic_load(&p->L->state) == LST_COMPLETE) gap = MAX(gap, (p->L->t_meta - p->L->t_start) * 5);
                if (A.ren_pane == p || p->L_next || (A.drag_pane == p && A.drag != DRAG_NONE)) blocked = true;
            }
        bool quiet = now - w->last > 120000000ull, overdue = now - w->first > 1000000000ull;
        if (blocked || !(quiet || overdue) || now - w->last_refresh < gap) continue;
        w->dirty = false;
        w->last_refresh = now;
        for (int t = 0; t < A.ntabs; t++)
            for (int j = 0; j < A.tabs[t]->npanes; j++) {
                Pane *p = &A.tabs[t]->pane[j];
                if (pane_watchable(p) && pane_watches(p, w->path)) pane_refresh(p);
            }
    }
}

static bool watch_pending(void) {
    for (int i = 0; i < g_nw; i++)
        if (g_w[i].dirty) return true;
    return false;
}

// --- git status (main thread; git.c runs git on its own workers) -------------
// One result per repository serves all of its folders. A folder is first resolved to its repository (a few stats,
// no git); until that lands, a folder under a known repository uses it straight away, so moving around inside a repo
// shows the badges in the first frame. git runs again when a shown folder gets a new listing (navigation, live
// refresh, F5), when the window gets focus, when .git changes (checked every 2 s) and, for files edited by other apps
// in folders nobody watches, every 2 s or 50x as long as git takes there (big repositories), while a repository is
// shown and the window isn't hidden.

#define GIT_REPOS 8
#define GIT_DIRS 48
static struct GitRepo {
    char root[PATH_MAX];  // real path
    char vroot[PATH_MAX]; // the root as the panes spell it (through symlinks), "" if unknown
    char gitdir[PATH_MAX];
    char head[128]; // branch from HEAD until git status lands
    bool head_detached;
    GitResult *res;
    bool busy, again, again_force;
    u64 used, checked, full; // last use, last request, last full (forced) status
    const GitResult *cnt_res; // memo for git_count_under
    char cnt_prefix[PATH_MAX];
    int cnt;
} g_repos[GIT_REPOS];
static struct GitDir {
    char path[PATH_MAX];
    char root[PATH_MAX]; // "" = not in a repository
    char prefix[PATH_MAX];
    u64 used;
} g_gdirs[GIT_DIRS];
static int g_git_runs, g_git_resolving; // status jobs started (tests), resolve jobs in flight
#define GIT_SUBDIRS 16
static struct GitSubs { // the repositories among a folder's subfolders (branch pills), per folder
    char path[PATH_MAX];
    GitSub *v;
    int n;
    u64 used;
} g_gsubs[GIT_SUBDIRS];
static int g_gsubs_busy; // jobs in flight

typedef struct { // what a pane shows: its repository and its place in it
    struct GitRepo *repo;
    const GitResult *g; // NULL until the first status of the repository lands
    char prefix[PATH_MAX];
} GitView;

static bool git_pane_eligible(Pane *p) {
    if (!A.git_status || !p->path[0] || pane_is_archive(p) || pane_is_trash(p)) return false;
    SshTarget st;
    // other network folders: git would stat every file through the mount; on SFTP it runs on the server instead
    if (path_is_remote(p->path) && !ssh_target_for(p->path, &st)) return false;
    Tab *t = tab_of(p);
    if (t && t->term) return false;
    return !A.selftest || getenv("FE_GIT"); // tests use a stand-in git, never the real one
}

static struct GitRepo *git_repo(const char *root, bool create) {
    struct GitRepo *lru = NULL;
    for (int i = 0; i < GIT_REPOS; i++) {
        struct GitRepo *r = &g_repos[i];
        if (r->root[0] && !strcmp(r->root, root)) return r;
        if (!r->busy && (!lru || r->used < lru->used)) lru = r;
    }
    if (!create || !lru) return NULL;
    git_result_free(lru->res);
    memset(lru, 0, sizeof *lru);
    snprintf(lru->root, sizeof lru->root, "%s", root);
    return lru;
}

static struct GitDir *git_dir(const char *path, bool create) {
    struct GitDir *lru = NULL;
    for (int i = 0; i < GIT_DIRS; i++) {
        struct GitDir *d = &g_gdirs[i];
        if (d->path[0] && !strcmp(d->path, path)) return d;
        if (!lru || d->used < lru->used) lru = d;
    }
    if (!create) return NULL;
    memset(lru, 0, sizeof *lru);
    snprintf(lru->path, sizeof lru->path, "%s", path);
    return lru;
}

static void git_request(struct GitRepo *r, bool force) {
    r->checked = now_ns();
    if (r->busy) { r->again = true; r->again_force |= force; return; }
    GitJob *j = xcalloc(1, sizeof *j);
    snprintf(j->root, sizeof j->root, "%s", r->root);
    snprintf(j->gitdir, sizeof j->gitdir, "%s", r->gitdir);
    SshTarget st;
    if (path_is_remote(r->root) && ssh_target_for(r->root, &st)) { // an SFTP folder (GVfs or sshfs): git runs on the server
        snprintf(j->host, sizeof j->host, "%s%s%s", st.user, st.user[0] ? "@" : "", st.host);
        snprintf(j->port, sizeof j->port, "%s", st.port);
        snprintf(j->rroot, sizeof j->rroot, "%s", st.path);
    }
    j->sig = r->res ? r->res->sig : 0;
    j->force = force || !r->res;
    if (j->force) r->full = r->checked;
    r->busy = true;
    g_git_runs++;
    git_submit(git_job_run, j);
}

static void git_job_done(void *arg) {
    GitJob *j = arg;
    struct GitRepo *r = git_repo(j->root, false);
    if (!r) { git_result_free(j->res); free(j); return; }
    r->busy = false;
    if (j->res) {
        if (!j->res->has_details && r->res) { // .git unchanged: keep the last commit, remotes and stashes
            GitResult *o = r->res, *n = j->res;
            memcpy(n->last_hash, o->last_hash, sizeof n->last_hash);
            memcpy(n->last_subject, o->last_subject, sizeof n->last_subject);
            memcpy(n->last_author, o->last_author, sizeof n->last_author);
            n->last_time = o->last_time;
            memcpy(n->remotes, o->remotes, sizeof n->remotes);
            n->nremotes = o->nremotes;
            n->nstash = o->nstash;
            n->has_details = o->has_details;
        }
        git_result_free(r->res);
        r->res = j->res;
        r->cnt_res = NULL;
        A.redraw = true;
    }
    free(j);
    if (r->again) {
        bool f = r->again_force;
        r->again = r->again_force = false;
        git_request(r, f);
    }
}

// The root as a folder's path spells it (through symlinks): the path minus the prefix; "" if the tail doesn't match.
static void git_vroot(const char *path, const char *prefix, char *out, size_t cap) {
    size_t pl = strlen(prefix), l = strlen(path);
    out[0] = 0;
    if (!pl) { snprintf(out, cap, "%s", path); return; }
    if (l < pl || strncmp(path + l - (pl - 1), prefix, pl - 1) || path[l - pl] != '/') return;
    if (l == pl) snprintf(out, cap, "/");
    else snprintf(out, cap, "%.*s", (int)(l - pl), path);
}

static void git_resolve_done(void *arg) {
    GitResolve *rs = arg;
    g_git_resolving--;
    struct GitDir *d = git_dir(rs->path, true);
    d->used = now_ns();
    snprintf(d->root, sizeof d->root, "%s", rs->repo ? rs->root : "");
    snprintf(d->prefix, sizeof d->prefix, "%s", rs->prefix);
    if (rs->repo && A.git_status) {
        struct GitRepo *r = git_repo(rs->root, true);
        if (r) {
            r->used = now_ns();
            snprintf(r->gitdir, sizeof r->gitdir, "%s", rs->gitdir);
            snprintf(r->head, sizeof r->head, "%s", rs->head);
            r->head_detached = rs->detached;
            git_vroot(rs->path, rs->prefix, r->vroot, sizeof r->vroot);
            git_request(r, true);
        }
    }
    A.redraw = true;
    free(rs);
}

// The repository a pane shows and its folder in it (false: off, not a repository, or not resolved yet).
static bool git_view(Pane *p, GitView *v) {
    v->repo = NULL;
    v->g = NULL;
    v->prefix[0] = 0;
    if (!git_pane_eligible(p)) return false;
    struct GitDir *d = git_dir(p->path, false);
    if (d) {
        if (!d->root[0]) return false;
        v->repo = git_repo(d->root, false);
        snprintf(v->prefix, sizeof v->prefix, "%s", d->prefix);
    } else { // not resolved yet: the deepest known repository it's under (resolving corrects nested repositories)
        size_t best = 0;
        for (int i = 0; i < GIT_REPOS; i++) {
            struct GitRepo *r = &g_repos[i];
            size_t l = strlen(r->vroot);
            if (!l || l < best || strncmp(p->path, r->vroot, l) || (p->path[l] && p->path[l] != '/' && strcmp(r->vroot, "/"))) continue;
            best = l;
            v->repo = r;
            const char *rest = p->path + l + (p->path[l] == '/');
            if (!strcmp(r->vroot, "/")) rest = p->path + 1;
            snprintf(v->prefix, sizeof v->prefix, "%s%s", rest, rest[0] ? "/" : "");
        }
    }
    if (!v->repo) return false;
    v->g = v->repo->res && v->repo->res->repo ? v->repo->res : NULL;
    return v->g || v->repo->head[0];
}

static u8 git_entry_state(const GitView *v, const Entry *e) {
    return v->g ? git_state(v->g, v->prefix, e->name, e->name_len) : GS_NONE;
}

static int git_view_count(GitView *v) {
    struct GitRepo *r = v->repo;
    if (!v->g) return 0;
    if (r->cnt_res != v->g || strcmp(r->cnt_prefix, v->prefix)) {
        r->cnt_res = v->g;
        snprintf(r->cnt_prefix, sizeof r->cnt_prefix, "%s", v->prefix);
        r->cnt = git_count_under(v->g, v->prefix);
    }
    return r->cnt;
}

static const char *git_view_branch(const GitView *v) {
    if (v->g) return v->g->branch[0] ? v->g->branch : "no commits";
    return v->repo->head;
}

static void git_remote_web(const char *url, char *disp, size_t dcap, char *web, size_t wcap);

static void git_subs_done(void *arg) {
    GitSubsJob *j = arg;
    g_gsubs_busy--;
    struct GitSubs *d = NULL, *lru = NULL;
    for (int i = 0; i < GIT_SUBDIRS && !d; i++) {
        if (g_gsubs[i].path[0] && !strcmp(g_gsubs[i].path, j->path)) d = &g_gsubs[i];
        else if (!lru || g_gsubs[i].used < lru->used) lru = &g_gsubs[i];
    }
    if (!d) d = lru;
    for (int i = 0; i < d->n; i++) free(d->v[i].name);
    free(d->v);
    snprintf(d->path, sizeof d->path, "%s", j->path);
    d->v = j->subs;
    d->n = j->nsubs;
    d->used = now_ns();
    for (int i = 0; i < d->n; i++)
        if (d->v[i].remote[0]) git_remote_web(d->v[i].remote, d->v[i].disp, sizeof d->v[i].disp, NULL, 0);
    free(j);
    A.redraw = true;
}

// Ask which of the listing's folders are repositories (once per listing; skipped for searches and huge folders).
static void git_subs_request(Pane *p) {
    Listing *L = p->L;
    p->gitsub_lt = L->t_start;
    if (L->is_search || L->is_content || L->is_archive) return;
    GitSubsJob *j = xcalloc(1, sizeof *j);
    snprintf(j->path, sizeof j->path, "%s", p->path);
    for (u32 i = 0; i < L->count && j->n < 20000; i++) {
        Entry *e = &L->entries[i];
        if (!(e->flags & ENT_DIR) || (e->flags & ENT_MATCH) || !strcmp(rel_base(e->name), ".git")) continue;
        if (!(j->n & (j->n + 1))) j->names = xrealloc(j->names, (size_t)(j->n * 2 + 1) * sizeof *j->names);
        j->names[j->n++] = strdup(e->name);
    }
    g_gsubs_busy++;
    git_submit(git_subs_job, j);
}

// The repository a folder entry of the pane is (branch pill), or NULL.
static const GitSub *git_entry_sub(Pane *p, const Entry *e) {
    if (!(e->flags & ENT_DIR) || !git_pane_eligible(p)) return NULL;
    static struct GitSubs *last; // rows of one pane are drawn together
    if (!last || strcmp(last->path, p->path)) {
        last = NULL;
        for (int i = 0; i < GIT_SUBDIRS && !last; i++)
            if (g_gsubs[i].path[0] && !strcmp(g_gsubs[i].path, p->path)) last = &g_gsubs[i];
        if (!last) return NULL;
    }
    last->used = now_ns();
    return last->n ? git_sub_find(last->v, last->n, e->name) : NULL;
}

// Everything shown is asked again (focus regained, the feature switched on, Refresh).
static void git_refresh_all(void) {
    for (int i = 0; i < A.ntabs; i++)
        for (int j = 0; j < A.tabs[i]->npanes; j++) A.tabs[i]->pane[j].git_lt = A.tabs[i]->pane[j].gitsub_lt = 0;
    A.redraw = true;
}

static void git_tick(void) {
    if (!A.git_status) return;
    u64 now = now_ns();
    for (int i = 0; i < A.npanels; i++) {
        Tab *t = A.panels[i]->cur;
        if (!t) continue;
        Pane *p = &t->pane[0];
        if (!p->L || !git_pane_eligible(p)) continue;
        if (p->git_lt != p->L->t_start) { // a new listing (also a reload of the same folder): resolve, then status
            p->git_lt = p->L->t_start;
            GitResolve *rs = xcalloc(1, sizeof *rs);
            snprintf(rs->path, sizeof rs->path, "%s", p->path);
            g_git_resolving++;
            git_submit(git_resolve_job, rs);
        }
        if (p->gitsub_lt != p->L->t_start && atomic_load(&p->L->state) >= LST_READY && atomic_load(&p->L->state) != LST_ERROR)
            git_subs_request(p);
        GitView v;
        if (!git_view(p, &v)) continue;
        v.repo->used = now;
        if (!v.g || v.repo->busy || A.win_hidden) continue;
        u64 every = MAX(2000000000ull, v.g->dur_ns * 50); // ~2% of a core at most, even in huge repositories
        if (now - v.repo->full >= every) git_request(v.repo, true); // files edited in folders nobody watches
        else if (now - v.repo->checked > 2000000000ull) git_request(v.repo, false); // a few stats unless .git changed
    }
}

static bool git_busy(void) {
    if (g_git_resolving || g_gsubs_busy) return true;
    for (int i = 0; i < GIT_REPOS; i++)
        if (g_repos[i].busy) return true;
    return false;
}

// Main loop wake-up for the .git poll: only while a repository is on screen.
static int git_wait_ms(void) {
    if (!A.git_status || A.win_hidden) return -1;
    u64 now = now_ns();
    for (int i = 0; i < GIT_REPOS; i++)
        if (g_repos[i].res && g_repos[i].res->repo && now - g_repos[i].used < 3000000000ull) return 1000;
    return -1;
}

static u32 git_color(u8 st) {
    switch (st) {
    case GS_IGNORED: return COL_TEXT_FAINT;
    case GS_UNTRACKED: case GS_ADDED: case GS_RENAMED: return COL_OK;
    case GS_STAGED: return rgb_mix(COL_OK, COL_TEXT, 0.35f);
    case GS_MODIFIED: return COL_WARN;
    case GS_DELETED: case GS_CONFLICT: return COL_DANGER;
    }
    return COL_TEXT;
}

static const char *git_letter(u8 st) {
    static const char *const l[] = {"", "", "U", "M", "A", "R", "M", "D", "!"};
    return st < ARRAY_COUNT(l) ? l[st] : "";
}

// Status bar: branch ("main") and the rest (" up2 down1 . 3 changed"); false when the pane isn't in a repository.
static bool git_status_label(Pane *p, char *branch, size_t bcap, char *rest, size_t rcap) {
    GitView v;
    if (!git_view(p, &v)) return false;
    const GitResult *g = v.g;
    snprintf(branch, bcap, "%s%s", git_view_branch(&v), g && g->detached && g->branch[0] ? " (detached)" : "");
    rest[0] = 0;
    if (!g) return true;
    int k = 0;
    if (g->ahead) k += snprintf(rest + k, rcap - (size_t)k, " \xE2\x86\x91%d", g->ahead);
    if (g->behind && k < (int)rcap) k += snprintf(rest + k, rcap - (size_t)k, " \xE2\x86\x93%d", g->behind);
    int n = git_view_count(&v);
    if (k < (int)rcap) snprintf(rest + k, rcap - (size_t)k, n ? "   \xC2\xB7   %d changed" : "   \xC2\xB7   clean", n);
    return true;
}

// Path bar chip: "main up2 down1" (ahead/behind only when there are some).
static bool git_chip_text(Pane *p, char *out, size_t cap, bool *dirty) {
    GitView v;
    if (A.edit_pane == p || !git_view(p, &v)) return false;
    int k = snprintf(out, cap, "%s", git_view_branch(&v));
    if (v.g && v.g->ahead && k < (int)cap) k += snprintf(out + k, cap - (size_t)k, " \xE2\x86\x91%d", v.g->ahead);
    if (v.g && v.g->behind && k < (int)cap) snprintf(out + k, cap - (size_t)k, " \xE2\x86\x93%d", v.g->behind);
    *dirty = v.g && (v.g->nstaged || v.g->nmodified || v.g->nuntracked || v.g->nconflicts);
    return true;
}

static f32 git_chip_width(Pane *p) {
    char t[200];
    bool dirty;
    if (!git_chip_text(p, t, sizeof t, &dirty)) return 0;
    return roundf(MIN(text_width(&A.font_small, t, (int)strlen(t)), S(180)) + S(42)); // icon + text + a gap + the status dot
}

// "git@github.com:me/x.git", "ssh://git@host:22/me/x", "https://u@host/me/x.git" -> "host/me/x" (+ https web URL).
static void git_remote_web(const char *url, char *disp, size_t dcap, char *web, size_t wcap) {
    char u[300];
    snprintf(u, sizeof u, "%s", url);
    const char *s = u;
    bool scp = false;
    const char *sch = strstr(s, "://");
    if (sch) s = sch + 3;
    else if (strchr(s, ':') && (!strchr(s, '/') || strchr(s, ':') < strchr(s, '/'))) scp = true; // user@host:path
    else { snprintf(disp, dcap, "%s", url); if (web) web[0] = 0; return; } // a local path
    const char *at = strchr(s, '@'), *sl = strchr(s, scp ? ':' : '/');
    if (at && (!sl || at < sl)) s = at + 1;
    char host[256], path[300] = "";
    size_t hl = strcspn(s, scp ? ":" : ":/");
    snprintf(host, sizeof host, "%.*s", (int)hl, s);
    const char *rest = s + hl;
    if (!scp && *rest == ':') rest += strcspn(rest, "/"); // port
    if (*rest == ':' || *rest == '/') rest++;
    snprintf(path, sizeof path, "%s", rest);
    size_t pl = strlen(path);
    while (pl && path[pl - 1] == '/') path[--pl] = 0;
    if (pl > 4 && !strcmp(path + pl - 4, ".git")) path[pl - 4] = 0;
    snprintf(disp, dcap, "%s/%s", host, path);
    if (web) {
        bool webby = !sch || !strncmp(u, "http", 4) || !strncmp(u, "ssh", 3) || !strncmp(u, "git", 3);
        if (webby && host[0] && !strchr(host, ' ')) snprintf(web, wcap, "https://%s/%s", host, path);
        else web[0] = 0;
    }
}

static void git_ago(i64 t, char *out, size_t cap) {
    i64 d = (i64)time(NULL) - t;
    if (d < 60) snprintf(out, cap, "just now");
    else if (d < 3600) snprintf(out, cap, "%d min ago", (int)(d / 60));
    else if (d < 86400) snprintf(out, cap, "%d hour%s ago", (int)(d / 3600), d < 7200 ? "" : "s");
    else if (d < 86400 * 60) snprintf(out, cap, "%d day%s ago", (int)(d / 86400), d < 2 * 86400 ? "" : "s");
    else if (d < 86400 * 730) snprintf(out, cap, "%d months ago", (int)(d / (86400 * 30)));
    else snprintf(out, cap, "%d years ago", (int)(d / (86400 * 365)));
}

// Info row of the repository popup: dim key on the left, value on the right.
static void menu_add_info(const char *key, const char *fmt, ...) {
    Menu *m = &A.menu;
    if (m->n >= (int)ARRAY_COUNT(m->items) || m->ninfo >= (int)ARRAY_COUNT(m->infobuf)) return;
    char *buf = m->infobuf[m->ninfo++];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof m->infobuf[0], fmt, ap);
    va_end(ap);
    if (strlen(buf) > 72) memcpy(buf + 69, "\xE2\x80\xA6", 4); // long subjects / URLs
    m->items[m->n++] = (MenuItem){key, buf, -1, false, false, false, false, true};
}

// Click on the path bar's branch chip: what the repository is, where it syncs to, and a few actions.
static void menu_open_git(Pane *p, f32 x, f32 y) {
    GitView v;
    if (!git_view(p, &v)) return;
    Menu *m = &A.menu;
    m->keep_open = false;
    m->reopen = NULL;
    m->n = m->ninfo = 0;
    m->hover = -1;
    m->pane = p;
    const GitResult *g = v.g;
    menu_add_info("Repository", "%s", path_basename(v.repo->root));
    if (v.prefix[0]) menu_add_info("Folder", "%.*s", (int)strlen(v.prefix) - 1, v.prefix);
    const char *br = git_view_branch(&v);
    if (g && g->detached) menu_add_info("Branch", g->branch[0] ? "detached at %s" : "%s", g->branch[0] ? g->branch : "no commits");
    else if (g && g->upstream[0]) menu_add_info("Branch", "%s \xE2\x86\x92 %s", br, g->upstream);
    else menu_add_info("Branch", "%s%s", br, g ? "  (not tracking a remote)" : "");
    if (g && g->upstream[0]) {
        if (!g->ahead && !g->behind) menu_add_info("Sync", "up to date with %s", g->upstream);
        else if (g->ahead && g->behind) menu_add_info("Sync", "%d to push \xC2\xB7 %d to pull", g->ahead, g->behind);
        else if (g->ahead) menu_add_info("Sync", "%d commit%s to push", g->ahead, g->ahead == 1 ? "" : "s");
        else menu_add_info("Sync", "%d commit%s to pull", g->behind, g->behind == 1 ? "" : "s");
    }
    if (g) {
        char ch[160] = "";
        int k = 0;
        if (g->nmodified) k += snprintf(ch + k, sizeof ch - (size_t)k, "%d modified", g->nmodified);
        if (g->nstaged) k += snprintf(ch + k, sizeof ch - (size_t)k, "%s%d staged", k ? " \xC2\xB7 " : "", g->nstaged);
        if (g->nuntracked) k += snprintf(ch + k, sizeof ch - (size_t)k, "%s%d untracked", k ? " \xC2\xB7 " : "", g->nuntracked);
        if (g->nconflicts) snprintf(ch + k, sizeof ch - (size_t)k, "%s%d conflict%s", k ? " \xC2\xB7 " : "", g->nconflicts, g->nconflicts == 1 ? "" : "s");
        menu_add_info("Changes", "%s", ch[0] ? ch : "none, working tree clean");
        if (g->nstash) menu_add_info("Stashes", "%d", g->nstash);
        if (g->last_hash[0]) {
            char ago[48];
            git_ago(g->last_time, ago, sizeof ago);
            menu_add_info("Last commit", "%s \xC2\xB7 %s \xC2\xB7 %s", g->last_hash, ago, g->last_author);
            menu_add_info("", "%s", g->last_subject);
        }
        for (int i = 0; i < g->nremotes; i++) {
            char disp[320];
            git_remote_web(g->remotes[i].url, disp, sizeof disp, NULL, 0);
            menu_add_info(i ? "" : "Remote", "%s  %s", g->remotes[i].name, disp);
        }
    }
    menu_sep();
    if (g && g->nremotes) {
        char disp[320], web[340];
        git_remote_web(g->remotes[0].url, disp, sizeof disp, web, sizeof web);
        if (web[0]) menu_add("Open remote in browser", "", ACT_GIT_OPEN_REMOTE, true);
        menu_add("Copy remote URL", "", ACT_GIT_COPY_REMOTE, true);
    }
    menu_add("Copy branch name", "", ACT_GIT_COPY_BRANCH, br[0] != 0);
    if (v.prefix[0]) menu_add("Go to repository root", "", ACT_GIT_ROOT, true);
    menu_add("Refresh Git status", "", ACT_GIT_REFRESH, true);
    menu_finish(x, y);
}

static void git_action(Pane *p, int act) {
    GitView v;
    if (!git_view(p, &v)) return;
    const GitResult *g = v.g;
    switch (act) {
    case ACT_GIT_OPEN_REMOTE:
    case ACT_GIT_COPY_REMOTE:
        if (!g || !g->nremotes) break;
        if (act == ACT_GIT_COPY_REMOTE) {
            SDL_SetClipboardText(g->remotes[0].url);
            toast("Copied %s", g->remotes[0].url);
        } else {
            char disp[320], web[340];
            git_remote_web(g->remotes[0].url, disp, sizeof disp, web, sizeof web);
            if (web[0]) open_path(web);
        }
        break;
    case ACT_GIT_COPY_BRANCH: {
        const char *b = git_view_branch(&v);
        SDL_SetClipboardText(b);
        toast("Copied %s", b);
        break;
    }
    case ACT_GIT_ROOT: {
        char root[PATH_MAX]; // the root as this pane spells it (keeps symlinked paths)
        git_vroot(p->path, v.prefix, root, sizeof root);
        pane_navigate(p, root[0] ? root : v.repo->root, true);
        break;
    }
    case ACT_GIT_REFRESH: git_refresh_all(); break;
    }
}

// --- disk usage view (main thread; diskuse.c measures the tree) ----------------------------------------------
// A view of a normal tab: the pane still has its path, history and listing; the map shows the item for that path
// inside a scan of it or of a folder above it (drilling in reuses the scan). Scans are kept for reuse.

// kind: a folder with its children drawn inside, a folder too small for that, a file, a folder's small files
// grouped, a sunburst segment of any of those (r0..r1, a0..a1)
enum { DUC_FRAME, DUC_FOLDER, DUC_FILE, DUC_SMALL };
typedef struct { Rect r; DuItem *it, *parent; u64 size, files; u8 kind, depth; bool ring; f32 r0, r1, a0, a1; } DuCell;

typedef struct DuView {
    DuScan *s;
    DuItem *cur;  // the item for the pane's folder (NULL until that part is read)
    DuItem *pick; // selected in the map or the list
    char cur_path[PATH_MAX];
    DuCell *cells; // what the last frame drew, parents before children (hit testing)
    int ncells, capcells;
    DuItem **rows; // the list beside the map: cur's children, biggest first
    int nrows, caprows;
    int lcur;      // pick's row (-1: none or deeper)
    f64 lscroll;
    Rect hdr, map, list, legend, b_tree, b_sun, b_list, b_close;
    f32 cx, cy, hole; // sunburst centre and the radius of the middle circle
} DuView;

#define DU_CACHE 4
#define DU_REUSE 300000000000ull // a finished scan nobody shows is measured again after 5 min
static DuScan *g_duscans[DU_CACHE]; // each holds one reference

static bool pane_du_on(const Pane *p) {
    return p->du && p->path[0] && !pane_is_archive(p) && !pane_is_trash(p) && !p->search_q[0] && !(p->L && p->L->is_search);
}


static void du_scan_unref(DuScan *s) {
    if (--s->refs > 0) return;
    atomic_store(&s->cancel, 1);
    pthread_mutex_lock(&s->mtx);
    pthread_cond_broadcast(&s->cv);
    pthread_mutex_unlock(&s->mtx);
    if (s->exited) du_scan_free(s);
}

static void du_scan_exit_cb(void *arg) {
    DuScan *s = arg;
    s->exited = true;
    s->t_done = now_ns();
    if (!s->refs) du_scan_free(s);
    A.redraw = true;
}

static bool du_scan_running(const DuScan *s) { return !s->exited; }

static void du_cache_put(DuScan *s) {
    int slot = -1;
    for (int i = 0; i < DU_CACHE && slot < 0; i++)
        if (!g_duscans[i]) slot = i;
    if (slot < 0) { // the least recently used one only the cache holds
        for (int i = 0; i < DU_CACHE; i++)
            if (g_duscans[i]->refs == 1 && (slot < 0 || g_duscans[i]->t_used < g_duscans[slot]->t_used)) slot = i;
        if (slot < 0) return; // all on screen: this one isn't kept
        du_scan_unref(g_duscans[slot]);
    }
    g_duscans[slot] = s;
    s->refs++;
}

static void du_cache_drop(DuScan *s) {
    for (int i = 0; i < DU_CACHE; i++)
        if (g_duscans[i] == s) { g_duscans[i] = NULL; du_scan_unref(s); }
}

// A scan that contains `path` (the deepest cached one), else a new one rooted there. The caller owns a reference.
static DuScan *du_scan_for(const char *path) {
    DuScan *best = NULL;
    for (int i = 0; i < DU_CACHE; i++) {
        DuScan *s = g_duscans[i];
        if (!s || !du_under(path, s->rootpath) || (best && strlen(s->rootpath) <= strlen(best->rootpath))) continue;
        DuItem *f = du_find(s, path); // not one that skipped it (another drive) or finished without it (made since)
        if (f ? f->kind == DK_MOUNT : s->exited) continue;
        best = s;
    }
    u64 now = now_ns();
    if (best && (best->refs > 1 || !best->exited || now - best->t_done < DU_REUSE) && !best->stale) {
        best->refs++;
        best->t_used = now;
        return best;
    }
    if (best) du_cache_drop(best);
    DuScan *s = du_scan_start(path);
    s->refs = 1;
    s->t_used = now;
    du_cache_put(s);
    return s;
}

static void du_view_close(Pane *p) {
    DuView *v = p->dv;
    if (!v) return;
    if (v->s) du_scan_unref(v->s);
    free(v->cells);
    free(v->rows);
    free(v);
    p->dv = NULL;
}

static void du_view_attach(Pane *p, DuScan *s) {
    DuView *v = p->dv;
    if (v->s) du_scan_unref(v->s);
    v->s = s;
    v->cur = v->pick = NULL;
    v->cur_path[0] = 0;
    v->ncells = v->nrows = 0;
    v->lcur = -1;
}

// Every loop: give disk usage panes a scan that contains their folder, follow navigation.
static void du_tick(void) {
    for (int i = 0; i < A.ntabs; i++)
        for (int j = 0; j < A.tabs[i]->npanes; j++) {
            Pane *p = &A.tabs[i]->pane[j];
            if (!p->du) { if (p->dv) du_view_close(p); continue; }
            if (!pane_du_on(p)) continue; // a search for now: the map comes back after it
            if (!p->dv) { p->dv = xcalloc(1, sizeof(DuView)); p->dv->lcur = -1; }
            DuView *v = p->dv;
            if (!v->s || !du_under(p->path, v->s->rootpath)) du_view_attach(p, du_scan_for(p->path));
            else if (v->s->exited && !v->cur && !du_find(v->s, p->path)) du_view_attach(p, du_scan_for(p->path)); // not in it
            else if (v->cur && v->cur->kind == DK_MOUNT) du_view_attach(p, du_scan_for(p->path)); // into another drive
            v->s->t_used = now_ns();
            if (strcmp(v->cur_path, p->path)) { // went somewhere else in the same scan
                snprintf(v->cur_path, sizeof v->cur_path, "%s", p->path);
                v->cur = NULL;
                v->pick = NULL;
                v->lcur = -1;
                v->lscroll = 0;
            }
            if (!v->cur || v->cur->removed) v->cur = du_find(v->s, p->path); // NULL until the scan gets there
            if (v->pick && v->pick->removed) { v->pick = NULL; v->lcur = -1; }
        }
}

static bool du_busy(void) { // screenshots: wait for scans on screen
    for (int i = 0; i < A.npanels; i++) {
        if (!A.panels[i]->cur) continue;
        Pane *p = &A.panels[i]->cur->pane[0];
        if (pane_du_on(p) && (!p->dv || !p->dv->s || du_scan_running(p->dv->s) || !p->dv->cur)) return true;
    }
    return false;
}

static void du_toggle(Pane *p) {
    if (!p->du && (pane_is_archive(p) || pane_is_trash(p))) { toast("Disk usage works on folders on a disk"); return; }
    if (!p->du && p->search_q[0]) search_exit(p);
    p->du = !p->du;
    if (!p->du) du_view_close(p);
    else du_tick();
    layout();
    A.redraw = true;
}

static void du_open_tab(const char *path) {
    Tab *t = tab_new(path, A.cur + 1, true);
    if (!t) return;
    t->pane[0].du = true;
    du_tick();
    A.redraw = true;
}

static void du_rescan(Pane *p) {
    DuView *v = p->dv;
    if (!v || !v->s) return;
    char root[PATH_MAX];
    snprintf(root, sizeof root, "%s", v->s->rootpath);
    du_cache_drop(v->s);
    DuScan *s = du_scan_start(root);
    s->refs = 1;
    s->t_used = now_ns();
    du_cache_put(s);
    du_view_attach(p, s);
    du_tick();
    A.redraw = true;
}

// Children of the pane's folder for the list, biggest first (other drives last).
static int du_row_cmp(const void *a, const void *b) {
    const DuItem *x = *(DuItem *const *)a, *y = *(DuItem *const *)b;
    if ((x->kind == DK_MOUNT) != (y->kind == DK_MOUNT)) return x->kind == DK_MOUNT ? 1 : -1;
    u64 sx = atomic_load_explicit(&x->size, memory_order_relaxed), sy = atomic_load_explicit(&y->size, memory_order_relaxed);
    if (sx != sy) return sx < sy ? 1 : -1;
    return x->name && y->name ? strcmp(x->name, y->name) : x->name ? -1 : 1;
}

static void du_rows(DuView *v) {
    v->nrows = 0;
    DuItem *k = v->cur ? atomic_load_explicit(&v->cur->kids, memory_order_acquire) : NULL;
    if (!k) { v->lcur = -1; return; }
    if ((int)v->cur->nkids > v->caprows) {
        v->caprows = (int)v->cur->nkids;
        v->rows = xrealloc(v->rows, (size_t)v->caprows * sizeof(DuItem *));
    }
    for (u32 i = 0; i < v->cur->nkids; i++)
        if (!k[i].removed && (k[i].kind != DK_SMALL || atomic_load(&k[i].size) || atomic_load(&k[i].files))) v->rows[v->nrows++] = &k[i];
    if (v->nrows) qsort(v->rows, (size_t)v->nrows, sizeof(DuItem *), du_row_cmp);
    v->lcur = -1;
    for (int i = 0; i < v->nrows; i++)
        if (v->rows[i] == v->pick) v->lcur = i;
}

static f32 du_row_h(void) { return roundf(S(38)); }

static void du_list_visible(Pane *p) { // keep the picked row in view
    DuView *v = p->dv;
    if (!v || v->lcur < 0) return;
    f32 rh = du_row_h();
    f64 top = (f64)v->lcur * rh, view = MAX(rh, v->list.h - roundf(S(30)));
    if (top < v->lscroll) v->lscroll = top;
    else if (top + rh > v->lscroll + view) v->lscroll = top + rh - view;
}

// Pick an item; one directly in the folder also gets the file list's cursor (the preview and Properties follow it).
static void du_pick(Pane *p, DuItem *it) {
    DuView *v = p->dv;
    if (!v) return;
    v->pick = it;
    v->lcur = -1;
    for (int i = 0; i < v->nrows; i++)
        if (v->rows[i] == it) v->lcur = i;
    du_list_visible(p);
    sel_clear(p);
    if (it && it->parent == v->cur && it->name && p->order)
        for (u32 i = 0; i < p->count; i++)
            if (!strcmp(p->L->entries[p->order[i]].name, it->name)) { move_cursor(p, i, false, false); break; }
    A.redraw = true;
}

static bool du_pick_path(Pane *p, char *out, size_t cap) {
    DuView *v = p->dv;
    if (!v || !v->s || !v->pick || v->pick->removed || (v->pick->kind != DK_DIR && v->pick->kind != DK_FILE && v->pick->kind != DK_MOUNT))
        return false;
    du_item_path(v->s, v->pick, out, cap);
    return true;
}

static char **du_selection_paths(Pane *p, int *n) {
    *n = 0;
    char full[PATH_MAX];
    if (!du_pick_path(p, full, sizeof full)) return NULL;
    char **r = xmalloc(sizeof(char *));
    r[0] = strdup(full);
    *n = 1;
    return r;
}

// Enter / double-click: into a folder (the same scan), a file opens.
static void du_open(Pane *p, DuItem *it, bool new_tab) {
    DuView *v = p->dv;
    if (!v || !it || it->removed) return;
    char full[PATH_MAX];
    if (it->kind == DK_SMALL) { // the folder's own file list, where they are
        if (!it->parent) return;
        du_item_path(v->s, it->parent, full, sizeof full);
        p->du = false;
        du_view_close(p);
        pane_navigate(p, full, true);
        return;
    }
    du_item_path(v->s, it, full, sizeof full);
    if (it->kind == DK_FILE) { open_path(full); return; }
    if (new_tab) du_open_tab(full);
    else pane_navigate(p, full, true);
}

// Back to the file list, in the item's folder with the item selected.
static void du_show(Pane *p) {
    char full[PATH_MAX], par[PATH_MAX];
    bool has = du_pick_path(p, full, sizeof full);
    p->du = false;
    du_view_close(p);
    if (!has) { pane_refresh(p); layout(); return; }
    parent_of(full, par, sizeof par);
    pane_navigate(p, par, strcmp(par, p->path) != 0);
    snprintf(p->select_name, sizeof p->select_name, "%s", path_basename(full));
    layout();
}

// After an operation: what's gone leaves the map; what arrived inside a measured folder needs a rescan to show.
static void du_after_op(Op *op) {
    DuScan *ss[DU_CACHE + 2 * MAX_TABS];
    int ns = 0;
    for (int i = 0; i < DU_CACHE; i++)
        if (g_duscans[i]) ss[ns++] = g_duscans[i];
    for (int i = 0; i < A.ntabs; i++)
        for (int j = 0; j < A.tabs[i]->npanes; j++)
            if (A.tabs[i]->pane[j].dv && A.tabs[i]->pane[j].dv->s && ns < (int)ARRAY_COUNT(ss)) ss[ns++] = A.tabs[i]->pane[j].dv->s;
    for (int k = 0; k < ns; k++) {
        DuScan *s = ss[k];
        for (int i = 0; i < op->nsrc; i++) {
            struct stat st;
            if (!op->src[i] || lstat(op->src[i], &st) == 0) continue;
            DuItem *it = du_find(s, op->src[i]);
            if (it) du_remove(it);
        }
        if (op->kind == OP_COPY || op->kind == OP_MOVE || op->kind == OP_RESTORE || op->kind == OP_EXTRACT || op->kind == OP_COMPRESS) {
            if (op->dst_dir && du_under(op->dst_dir, s->rootpath)) s->stale = true;
            for (int i = 0; i < op->nout; i++)
                if (op->out[i] && du_under(op->out[i], s->rootpath)) s->stale = true;
        }
    }
    A.redraw = true;
}

static DuCell *du_cell_at(DuView *v, f32 x, f32 y) {
    if (!v) return NULL;
    for (int i = v->ncells - 1; i >= 0; i--) { // the deepest first
        DuCell *c = &v->cells[i];
        if (c->ring) {
            f32 dx = x - v->cx, dy = y - v->cy, r = sqrtf(dx * dx + dy * dy);
            f32 a = atan2f(dy, dx);
            if (a < -(f32)M_PI * 0.5f) a += 2 * (f32)M_PI; // angles run from -90 degrees (the top)
            if (r >= c->r0 && r < c->r1 && a >= c->a0 && a < c->a1) return c;
        } else if (in_rect(c->r, x, y)) return c;
    }
    return NULL;
}

static int du_list_row_at(DuView *v, f32 x, f32 y) {
    if (!v || !A.du_list || v->list.w <= 0 || !in_rect(v->list, x, y)) return -1;
    f32 top = v->list.y + roundf(S(30));
    if (y < top) return -1;
    int r = (int)((y - top + v->lscroll) / du_row_h());
    return r >= 0 && r < v->nrows ? r : -1;
}

static void menu_open_du(Pane *p, f32 x, f32 y) {
    DuView *v = p->dv;
    Menu *m = &A.menu;
    m->keep_open = false;
    m->reopen = NULL;
    m->n = 0;
    m->hover = -1;
    m->pane = p;
    DuItem *it = v ? v->pick : NULL;
    if (it && (it->kind == DK_DIR || it->kind == DK_FILE || it->kind == DK_MOUNT)) {
        menu_add("Open", "Enter", ACT_DU_OPEN, true);
        if (it->kind != DK_FILE) menu_add("Open in new tab", "Ctrl+Enter", ACT_DU_OPEN_TAB, true);
        menu_add("Show in the file list", "", ACT_DU_SHOW, true);
        menu_sep();
        menu_add("Cut", "Ctrl+X", ACT_CUT, it->kind != DK_MOUNT);
        menu_add("Copy", "Ctrl+C", ACT_COPY, true);
        menu_sep();
        menu_add("Move to Trash", "Del", ACT_TRASH, it->kind != DK_MOUNT);
        menu_add("Delete permanently", "Shift+Del", ACT_DELETE, it->kind != DK_MOUNT);
        menu_sep();
        menu_add("Copy path", "Ctrl+Shift+C", ACT_COPY_PATH, true);
        menu_add("Properties", "Alt+Enter", ACT_PROPERTIES, true);
    } else if (it && it->kind == DK_SMALL) {
        menu_add("Show these files", "Enter", ACT_DU_OPEN, true);
    }
    menu_sep();
    menu_add_check("Treemap", ACT_DU_TREEMAP, A.du_style == 0, true);
    menu_add_check("Sunburst", ACT_DU_SUNBURST, A.du_style == 1, true);
    menu_add_check("List of the biggest items", ACT_DU_LIST, A.du_list, true);
    menu_sep();
    menu_add("Up one folder", "Backspace", ACT_DU_UP, p->path[1] != 0);
    menu_add("Measure again", "F5", ACT_DU_RESCAN, v && v->s);
    menu_add("Back to the file list", "", ACT_DU_TOGGLE, true);
    menu_finish(x, y);
}

static void du_action(Pane *p, int act) {
    DuView *v = p->dv;
    switch (act) {
    case ACT_DU_TOGGLE: du_toggle(p); break;
    case ACT_DU_NEW_TAB: { // a folder in the file list
        if (p->cursor < 0 || !p->order) break;
        Entry *e = &p->L->entries[p->order[p->cursor]];
        char full[PATH_MAX];
        if (!(e->flags & ENT_DIR)) break;
        path_join(full, sizeof full, p->path, e->name);
        du_open_tab(full);
        break;
    }
    case ACT_DU_PLACE:
        if (A.menu_place >= 0 && A.menu_place < A.nplaces && A.places[A.menu_place].path[0]) du_open_tab(A.places[A.menu_place].path);
        break;
    case ACT_DU_OPEN: if (v) du_open(p, v->pick, false); break;
    case ACT_DU_OPEN_TAB: if (v) du_open(p, v->pick, true); break;
    case ACT_DU_SHOW: du_show(p); break;
    case ACT_DU_TREEMAP: case ACT_DU_SUNBURST:
        A.du_style = act == ACT_DU_SUNBURST;
        A.settings_dirty = true;
        break;
    case ACT_DU_LIST: A.du_list = !A.du_list; A.settings_dirty = true; break;
    case ACT_DU_RESCAN: du_rescan(p); break;
    case ACT_DU_UP: pane_up(p); break;
    default: break;
    }
    A.redraw = true;
}

static void du_mouse_down(Pane *p, SDL_MouseButtonEvent *b, f32 x, f32 y) {
    DuView *v = p->dv;
    if (!v) return;
    bool ctrl = SDL_GetModState() & SDL_KMOD_CTRL;
    if (b->button == SDL_BUTTON_LEFT) {
        if (in_rect(v->b_tree, x, y)) { du_action(p, ACT_DU_TREEMAP); return; }
        if (in_rect(v->b_sun, x, y)) { du_action(p, ACT_DU_SUNBURST); return; }
        if (in_rect(v->b_list, x, y)) { du_action(p, ACT_DU_LIST); return; }
        if (in_rect(v->b_close, x, y)) { du_toggle(p); return; }
    }
    if (in_rect(v->hdr, x, y) || in_rect(v->legend, x, y)) return;
    DuItem *it = NULL;
    bool hit = false, centre = false;
    int r = du_list_row_at(v, x, y);
    if (r >= 0) { it = v->rows[r]; hit = true; }
    else if (in_rect(v->map, x, y)) {
        DuCell *c = du_cell_at(v, x, y);
        if (c) { it = c->it; hit = true; }
        else if (A.du_style == 1 && hypotf(x - v->cx, y - v->cy) < v->hole) centre = true; // the middle: up a folder
    }
    if (b->button == SDL_BUTTON_MIDDLE) { if (it && it->kind != DK_FILE) du_open(p, it, true); return; }
    if (b->button == SDL_BUTTON_RIGHT) {
        du_pick(p, hit ? it : NULL);
        menu_open_du(p, x, y);
        return;
    }
    if (b->button != SDL_BUTTON_LEFT) return;
    if (centre) { if (b->clicks >= 2 || A.single_click) pane_up(p); else du_pick(p, NULL); return; }
    if (!hit) { du_pick(p, NULL); return; }
    if ((b->clicks >= 2 && it == v->pick) || (A.single_click && it && it->kind != DK_FILE && it->kind != DK_SMALL)) {
        du_open(p, it, ctrl);
        return;
    }
    du_pick(p, it);
}

static void du_wheel(Pane *p, f32 x, f32 y, f32 dy) {
    DuView *v = p->dv;
    if (!v || !A.du_list || !in_rect(v->list, x, y)) return;
    f32 rh = du_row_h();
    f64 maxs = MAX(0.0, (f64)v->nrows * rh - (v->list.h - roundf(S(30))));
    v->lscroll = CLAMP(v->lscroll - (f64)dy * rh * 3, 0.0, maxs);
    A.redraw = true;
}

static bool du_key(Pane *p, SDL_Keycode k, SDL_Keymod mod) {
    DuView *v = p->dv;
    if (!v) return false;
    bool ctrl = mod & SDL_KMOD_CTRL;
    int page = MAX(1, (int)((v->list.h - roundf(S(30))) / du_row_h()) - 1);
    int to = -2;
    switch (k) {
    case SDLK_DOWN: to = v->lcur < 0 ? 0 : v->lcur + 1; break;
    case SDLK_UP: to = v->lcur < 0 ? 0 : v->lcur - 1; break;
    case SDLK_PAGEDOWN: to = v->lcur + page; break;
    case SDLK_PAGEUP: to = v->lcur - page; break;
    case SDLK_HOME: to = 0; break;
    case SDLK_END: to = v->nrows - 1; break;
    case SDLK_RETURN: case SDLK_KP_ENTER: du_open(p, v->pick, ctrl); return true;
    case SDLK_RIGHT: if (v->pick && v->pick->kind == DK_DIR) du_open(p, v->pick, false); return true;
    case SDLK_LEFT: case SDLK_BACKSPACE: pane_up(p); return true;
    case SDLK_ESCAPE: du_pick(p, NULL); return true;
    case SDLK_APPLICATION: menu_open_du(p, v->map.x + S(40), v->map.y + S(40)); return true;
    default: return false;
    }
    if (v->nrows) du_pick(p, v->rows[CLAMP(to, 0, v->nrows - 1)]);
    return true;
}

// --- recent folders (persisted in $XDG_STATE_HOME/file-explorer/recent) ----

#define MAX_RECENT 200
static char *g_recent[MAX_RECENT];
static int g_nrecent;
static bool g_recent_dirty;
static u64 g_recent_saved;

static void recent_file(char *out, size_t cap, bool dir_only) {
    const char *x = getenv("XDG_STATE_HOME");
    char base[PATH_MAX];
    if (x && x[0] == '/') snprintf(base, sizeof base, "%s/file-explorer", x);
    else snprintf(base, sizeof base, "%s/.local/state/file-explorer", getenv("HOME") ? getenv("HOME") : "/tmp");
    if (dir_only) snprintf(out, cap, "%s", base);
    else snprintf(out, cap, "%s/recent", base);
}

static void recent_load(void) {
    char path[PATH_MAX];
    recent_file(path, sizeof path, false);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[PATH_MAX];
    while (g_nrecent < MAX_RECENT && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == '/') g_recent[g_nrecent++] = strdup(line);
    }
    fclose(f);
}

static void recent_save(void) {
    if (!g_recent_dirty) return;
    char dir[PATH_MAX], path[PATH_MAX], tmp[PATH_MAX + 8];
    recent_file(dir, sizeof dir, true);
    recent_file(path, sizeof path, false);
    mkdir_p(dir, 0700);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    for (int i = 0; i < g_nrecent; i++)
        if (!strpbrk(g_recent[i], "\n\r")) fprintf(f, "%s\n", g_recent[i]);
    if (fclose(f) == 0) rename(tmp, path);
    g_recent_dirty = false;
    g_recent_saved = now_ns();
}

static void recent_push(const char *path) {
    int k = -1;
    for (int i = 0; i < g_nrecent; i++)
        if (!strcmp(g_recent[i], path)) k = i;
    char *s = k >= 0 ? g_recent[k] : strdup(path);
    if (k < 0) {
        if (g_nrecent == MAX_RECENT) free(g_recent[--g_nrecent]);
        k = g_nrecent++;
    }
    memmove(&g_recent[1], &g_recent[0], (size_t)k * sizeof(char *));
    g_recent[0] = s;
    g_recent_dirty = true;
}

static void index_rebuild_default(void) {
    char *roots[16];
    int n = 0;
    const char *home = getenv("HOME");
    roots[n++] = (char *)(home ? home : "/");
    for (int i = A.nplaces_user; i < A.nplaces && n < 16; i++)
        if (A.places[i].path[0] && strcmp(A.places[i].path, "/") && !path_is_remote(A.places[i].path)) roots[n++] = A.places[i].path;
    index_rebuild(roots, n);
}

// --- folder compare (main thread; compare.c walks the two trees) ---------------------------------------------
// Two panels side by side: every item is marked against the same path in the other panel (only here, newer,
// older, differs, changes inside). Going into a folder on one side takes the other side along. Sync copies what's
// new or newer across (OP_SYNC) and can mirror (the other side's extras go to the Trash).

static struct {
    bool on;
    Tab *t[2];                  // left, right
    char root[2][PATH_MAX];
    char seen[2][PATH_MAX];     // the panes' paths last time (to follow navigation)
    CmpJob *job;                // the current comparison (running or done)
    CmpJob *old;                // the previous one, shown until the new one is done
    bool diff_only, contents;
    u32 cnt[2][4];              // per side: only here, newer here, differ (same date), file vs folder
    char pick[12][PATH_MAX];    // "Compare with..." menu: folders offered
    int npick;
} g_cmp;

static int cmp_side(const Pane *p) {
    if (!g_cmp.on) return -1;
    for (int s = 0; s < 2; s++)
        if (g_cmp.t[s] && p == &g_cmp.t[s]->pane[0]) return s;
    return -1;
}

static CmpJob *cmp_shown(void) { // the job whose results are on screen
    if (g_cmp.job && g_cmp.job->delivered) return g_cmp.job;
    return g_cmp.old;
}

static bool cmp_ready(void) { return g_cmp.job && g_cmp.job->delivered; }

// Done with a job: free it now if its callback ran, else cancel it and let the callback free it.
static void cmp_job_drop(CmpJob *j) {
    if (!j) return;
    if (j->delivered) cmp_job_free(j);
    else atomic_store(&j->cancel, 1);
}

static bool cmp_rel(const Pane *p, int side, const char *name, char *out, size_t cap) {
    const char *root = g_cmp.root[side];
    size_t rl = strlen(root);
    if (strncmp(p->path, root, rl) || (p->path[rl] && p->path[rl] != '/')) return false;
    const char *rest = p->path[rl] ? p->path + rl + 1 : "";
    if (rl == 1) rest = p->path + 1;
    snprintf(out, cap, "%s%s%s", rest, rest[0] && name[0] ? "/" : "", name);
    return true;
}

// The state of an entry as its own side sees it; CMP_UNKNOWN while nothing has been compared yet.
static u8 cmp_entry_state(const Pane *p, const Entry *e) {
    int side = cmp_side(p);
    CmpJob *j = side >= 0 ? cmp_shown() : NULL;
    if (!j) return CMP_UNKNOWN;
    char rel[PATH_MAX];
    if (!cmp_rel(p, side, e->name, rel, sizeof rel)) return CMP_UNKNOWN;
    return cmp_for_side(cmp_state_of(j, rel), side);
}

static void cmp_counts(void) {
    memset(g_cmp.cnt, 0, sizeof g_cmp.cnt);
    CmpJob *j = cmp_shown();
    if (!j) return;
    for (u32 i = 0; i < j->n; i++) {
        u8 st = j->recs[i].st;
        if (st == CMP_ONLY_A) g_cmp.cnt[0][0]++;
        else if (st == CMP_ONLY_B) g_cmp.cnt[1][0]++;
        else if (st == CMP_A_NEWER) g_cmp.cnt[0][1]++;
        else if (st == CMP_B_NEWER) g_cmp.cnt[1][1]++;
        else if (st == CMP_DIFF) { g_cmp.cnt[0][2]++; g_cmp.cnt[1][2]++; }
        else if (st == CMP_TYPE) { g_cmp.cnt[0][3]++; g_cmp.cnt[1][3]++; }
    }
}

// "Differences only": the same items leave the list (after sorting, like the contents search rows).
static void cmp_filter(Pane *p) {
    if (!g_cmp.diff_only || cmp_side(p) < 0 || !cmp_shown() || !p->order) return;
    u32 cur_e = p->cursor >= 0 && p->cursor < (i64)p->count ? p->order[p->cursor] : UINT32_MAX, m = 0;
    i64 nc = -1;
    for (u32 i = 0; i < p->count; i++) {
        u32 ei = p->order[i];
        if (cmp_entry_state(p, &p->L->entries[ei]) == CMP_SAME) {
            if (p->sel[ei]) { p->sel[ei] = 0; p->sel_count--; }
            continue;
        }
        if (ei == cur_e) nc = m;
        p->order[m++] = ei;
    }
    p->count = m;
    p->cursor = nc >= 0 ? nc : m ? 0 : -1;
    p->anchor = p->cursor;
}

static void cmp_resort(void) {
    for (int s = 0; s < 2; s++)
        if (g_cmp.t[s] && tab_index(g_cmp.t[s]) >= 0) pane_resort(&g_cmp.t[s]->pane[0]);
    layout();
    A.redraw = true;
}

static void cmp_job_done_cb(void *arg) {
    CmpJob *j = arg;
    j->delivered = true;
    if (j != g_cmp.job) { cmp_job_free(j); return; } // ended or replaced meanwhile
    if (g_cmp.old) { cmp_job_free(g_cmp.old); g_cmp.old = NULL; }
    cmp_counts();
    cmp_resort();
}

static void cmp_run(void) { // (again) - the old results stay on screen until the new ones are in
    if (g_cmp.job) {
        if (g_cmp.job->delivered) {
            if (g_cmp.old) cmp_job_free(g_cmp.old);
            g_cmp.old = g_cmp.job;
        } else atomic_store(&g_cmp.job->cancel, 1); // its callback frees it
    }
    g_cmp.job = cmp_job_start(g_cmp.root[0], g_cmp.root[1], g_cmp.contents);
    A.redraw = true;
}

static void cmp_end(void) {
    if (!g_cmp.on) return;
    g_cmp.on = false;
    cmp_job_drop(g_cmp.job);
    if (g_cmp.old) cmp_job_free(g_cmp.old); // (an old one has always been delivered)
    g_cmp.job = g_cmp.old = NULL;
    Tab *t0 = g_cmp.t[0], *t1 = g_cmp.t[1];
    g_cmp.t[0] = g_cmp.t[1] = NULL;
    if (t0 && tab_index(t0) >= 0) pane_resort(&t0->pane[0]);
    if (t1 && tab_index(t1) >= 0) pane_resort(&t1->pane[0]);
    layout();
    A.redraw = true;
}

static bool cmp_pane_ok(const Pane *p) {
    return p->path[0] && !pane_is_archive(p) && !pane_is_trash(p) && !(p->L && p->L->is_search) && !p->search_q[0];
}

// Compare two tabs shown in two panels (the left / upper one is side A).
static void cmp_start(Tab *a, Tab *b) {
    if (!a || !b || a == b || a->term || b->term) return;
    Pane *pa = &a->pane[0], *pb = &b->pane[0];
    if (!cmp_pane_ok(pa) || !cmp_pane_ok(pb)) { toast("Compare works on two folders (not archives, the Trash or search results)"); return; }
    if (!strcmp(pa->path, pb->path)) { toast("Both panels show the same folder"); return; }
    struct stat sa, sb; // (a snapshot inside the drive it's of is another filesystem: fine)
    bool samefs = stat(pa->path, &sa) == 0 && stat(pb->path, &sb) == 0 && sa.st_dev == sb.st_dev;
    if (samefs && (du_under(pa->path, pb->path) || du_under(pb->path, pa->path))) { toast("One folder is inside the other"); return; }
    cmp_end();
    Panel *ord[MAX_PANELS];
    int n = panels_ordered(A.root, ord, 0), ia = -1, ib = -1;
    for (int i = 0; i < n; i++) {
        if (ord[i] == a->panel) ia = i;
        if (ord[i] == b->panel) ib = i;
    }
    if (ib >= 0 && ia > ib) { Tab *t = a; a = b; b = t; }
    pa->du = pb->du = false; // the file lists (not disk usage maps)
    g_cmp.on = true;
    g_cmp.t[0] = a;
    g_cmp.t[1] = b;
    for (int s = 0; s < 2; s++) {
        snprintf(g_cmp.root[s], sizeof g_cmp.root[s], "%s", g_cmp.t[s]->pane[0].path);
        snprintf(g_cmp.seen[s], sizeof g_cmp.seen[s], "%s", g_cmp.root[s]);
    }
    cmp_run();
    layout();
}

// Every loop: end when a side goes away or leaves its folder tree; going into a folder takes the other side along.
static void cmp_tick(void) {
    if (!g_cmp.on) return;
    for (int s = 0; s < 2; s++) {
        Tab *t = g_cmp.t[s];
        if (!t || tab_index(t) < 0 || !t->panel || t->panel->cur != t || t->term) { cmp_end(); return; }
    }
    for (int s = 0; s < 2; s++) {
        Pane *p = &g_cmp.t[s]->pane[0], *o = &g_cmp.t[!s]->pane[0];
        if (!strcmp(p->path, g_cmp.seen[s])) continue;
        char rel[PATH_MAX];
        if (!cmp_pane_ok(p) && p->search_q[0]) continue; // a filter in the folder: still comparing
        if (!cmp_rel(p, s, "", rel, sizeof rel) || pane_is_archive(p) || pane_is_trash(p)) {
            cmp_end();
            toast("Compare ended (left the compared folders)");
            return;
        }
        snprintf(g_cmp.seen[s], sizeof g_cmp.seen[s], "%s", p->path);
        char want[PATH_MAX];
        snprintf(want, sizeof want, "%s%s%s", g_cmp.root[!s], rel[0] && g_cmp.root[!s][1] ? "/" : "", rel);
        if (!rel[0]) snprintf(want, sizeof want, "%s", g_cmp.root[!s]);
        struct stat st;
        if (strcmp(o->path, want) && stat(want, &st) == 0 && S_ISDIR(st.st_mode)) {
            pane_navigate(o, want, true);
            snprintf(g_cmp.seen[!s], sizeof g_cmp.seen[!s], "%s", o->path);
        }
    }
}

// What a sync copies (src -> dst) and what a mirror moves to the Trash, from side `from` (-1: both ways).
// What a sync does, and the first items of it for the dialog (source and destination as "folder/sub/name").
#define CMP_SHOW 5
typedef struct {
    char **src, **dst, **del;
    int n, ndel, skipped;
    int nnew, nnewer, ndiff; // copies: new, replacing an older copy, replacing one with the same date
    struct { char from[PATH_MAX], to[PATH_MAX]; u8 kind; bool dir; } show[CMP_SHOW];
    int nshow;
} CmpPlan;

static void cmp_plan_add(char ***v, int *n, const char *s) {
    *v = xrealloc(*v, (size_t)(*n + 1) * sizeof(char *));
    (*v)[(*n)++] = strdup(s);
}

static void cmp_plan_free(CmpPlan *pl) {
    strv_free(pl->src, pl->n);
    strv_free(pl->dst, pl->n);
    strv_free(pl->del, pl->ndel);
    memset(pl, 0, sizeof *pl);
}

// side: where the item is (-1: nowhere yet); to: where it goes (-1: the Trash).
static void cmp_plan_show(CmpPlan *pl, int from, int to, u8 kind, bool dir, const char *rel) {
    if (pl->nshow >= CMP_SHOW) return;
    const char *rf = path_basename(g_cmp.root[from]);
    snprintf(pl->show[pl->nshow].from, PATH_MAX, "%s/%s", g_cmp.root[from][1] ? rf : "", rel);
    if (to < 0) snprintf(pl->show[pl->nshow].to, PATH_MAX, "Trash");
    else snprintf(pl->show[pl->nshow].to, PATH_MAX, "%s/%s", g_cmp.root[to][1] ? path_basename(g_cmp.root[to]) : "", rel);
    pl->show[pl->nshow].kind = kind;
    pl->show[pl->nshow].dir = dir;
    pl->nshow++;
}

static void cmp_join(char *out, size_t cap, const char *root, const char *rel) {
    snprintf(out, cap, "%s%s%s", root, root[1] ? "/" : "", rel);
}

static void cmp_plan(CmpPlan *pl, int from, bool mirror) {
    memset(pl, 0, sizeof *pl);
    CmpJob *j = cmp_shown();
    if (!j) return;
    char a[PATH_MAX], b[PATH_MAX];
    for (u32 i = 0; i < j->n; i++) {
        const CmpRec *r = &j->recs[i];
        int dir = -1; // copy A->B (0) or B->A (1)
        if (r->st == CMP_ONLY_A || r->st == CMP_A_NEWER) dir = 0;
        else if (r->st == CMP_ONLY_B || r->st == CMP_B_NEWER) dir = 1;
        else if (r->st == CMP_DIFF && from >= 0) dir = from; // same date: the side you sync from wins
        else if (r->st == CMP_DIFF || r->st == CMP_TYPE) { pl->skipped++; continue; }
        else continue;
        cmp_join(a, sizeof a, g_cmp.root[0], r->rel);
        cmp_join(b, sizeof b, g_cmp.root[1], r->rel);
        if (from >= 0 && dir != from) {
            // only on the other side: a mirror sends it to the Trash; newer on the other side: left alone
            if (mirror && (r->st == CMP_ONLY_A || r->st == CMP_ONLY_B)) {
                cmp_plan_add(&pl->del, &pl->ndel, dir ? b : a);
                cmp_plan_show(pl, dir, -1, DK_TRASH, r->dir, r->rel);
            } else if (r->st == CMP_A_NEWER || r->st == CMP_B_NEWER) pl->skipped++;
            continue;
        }
        cmp_plan_add(&pl->src, &pl->n, dir ? b : a);
        pl->n--;
        cmp_plan_add(&pl->dst, &pl->n, dir ? a : b);
        bool isnew = r->st == CMP_ONLY_A || r->st == CMP_ONLY_B;
        if (isnew) pl->nnew++;
        else if (r->st == CMP_DIFF) pl->ndiff++;
        else pl->nnewer++;
        cmp_plan_show(pl, dir, !dir, isnew ? DK_COPY : DK_REPLACE, r->dir, r->rel);
    }
}

static CmpPlan g_cmp_plan;

static void cmp_sync_cb(int choice, bool check, void *ud) {
    (void)check;
    (void)ud;
    if (choice == 0) {
        CmpPlan *pl = &g_cmp_plan;
        if (pl->n) {
            Op *op = op_new(OP_SYNC, pl->src, pl->n, NULL);
            op->dst = strv_dup(pl->dst, pl->n);
            op->policy = CF_REPLACE;
            ops_start(op);
        }
        if (pl->ndel) ops_start(op_new(OP_TRASH, pl->del, pl->ndel, NULL));
    }
    cmp_plan_free(&g_cmp_plan);
}

// from: the side that's copied from (-1 both ways); mirror: the other side's extras go to the Trash.
static void cmp_sync(int from, bool mirror) {
    CmpJob *j = cmp_shown();
    if (!j || !cmp_ready()) { toast("Still comparing\xE2\x80\xA6"); return; }
    cmp_plan_free(&g_cmp_plan);
    cmp_plan(&g_cmp_plan, from, mirror);
    CmpPlan *pl = &g_cmp_plan;
    if (!pl->n && !pl->ndel) {
        toast(pl->skipped ? "Nothing to copy (%d item%s differ and were left alone)" : "Nothing to copy: both sides have it all",
              pl->skipped, pl->skipped == 1 ? "" : "s");
        return;
    }
    // folder names for the text ("(left)" / "(right)" when both are called the same)
    char nm[2][NAME_MAX + 16];
    bool same = !strcmp(path_basename(g_cmp.root[0]), path_basename(g_cmp.root[1]));
    for (int k = 0; k < 2; k++)
        snprintf(nm[k], sizeof nm[k], "\xE2\x80\x9C%s\xE2\x80\x9D%s", g_cmp.root[k][1] ? path_basename(g_cmp.root[k]) : "/",
                 same ? (k ? " (right)" : " (left)") : "");
    char title[2 * NAME_MAX + 64], msg[700], what[300] = "", skip[200] = "";
    size_t o = 0;
    if (pl->nnew) o += (size_t)snprintf(what + o, sizeof what - o, "%d new", pl->nnew);
    if (pl->nnewer) o += (size_t)snprintf(what + o, sizeof what - o, "%s%d newer", o ? ", " : "", pl->nnewer);
    if (pl->ndiff) o += (size_t)snprintf(what + o, sizeof what - o, "%s%d with the same date", o ? ", " : "", pl->ndiff);
    if (pl->ndel) o += (size_t)snprintf(what + o, sizeof what - o, "%s%d to the Trash", o ? ", " : "", pl->ndel);
    if (pl->skipped)
        snprintf(skip, sizeof skip, " %d left alone (%s, or a file on one side and a folder on the other).", pl->skipped,
                 from < 0 ? "same date but different" : "newer on the other side");
    if (from < 0) {
        snprintf(title, sizeof title, "Sync %s and %s both ways", nm[0], nm[1]);
        snprintf(msg, sizeof msg, "Each side gets what is new or newer on the other (%s). Nothing is deleted.%s", what, skip);
    } else if (mirror) {
        snprintf(title, sizeof title, "Mirror %s onto %s", nm[from], nm[!from]);
        snprintf(msg, sizeof msg, "Afterwards %s has what %s has (%s); what only %s has goes to the Trash.%s", nm[!from], nm[from], what,
                 nm[!from], skip);
    } else {
        snprintf(title, sizeof title, "Copy new and newer from %s to %s", nm[from], nm[!from]);
        snprintf(msg, sizeof msg, "%s: older copies are replaced, nothing is deleted.%s", what, skip);
        msg[0] = (char)toupper((u8)msg[0]);
    }
    static const char *b[] = {"Sync", "Cancel"};
    dialog_open(title, msg, b, 2, 0, 1, NULL, cmp_sync_cb, NULL);
    Dialog *d = &A.dlg; // one row per item: from -> to, what happens at the end
    d->rich = true;
    for (int i = 0; i < pl->nshow; i++) {
        d->item_kind[d->nitems] = pl->show[i].kind;
        d->item_dir[d->nitems] = pl->show[i].dir;
        d->item_sub[d->nitems] = strdup(pl->show[i].to);
        d->items[d->nitems++] = strdup(pl->show[i].from);
    }
    d->more = pl->n + pl->ndel - d->nitems;
}

// After an operation in a compared folder: compare again (what was synced or changed shows right away).
static void cmp_after_op(Op *op) {
    if (!g_cmp.on) return;
    bool hit = false;
    for (int s = 0; s < 2 && !hit; s++) {
        if (op->dst_dir && du_under(op->dst_dir, g_cmp.root[s])) hit = true;
        for (int i = 0; i < op->nsrc && !hit; i++)
            if (op->src[i] && du_under(op->src[i], g_cmp.root[s])) hit = true;
        for (int i = 0; op->dst && i < op->nsrc && !hit; i++)
            if (op->dst[i] && du_under(op->dst[i], g_cmp.root[s])) hit = true;
    }
    if (hit) cmp_run();
}

// Start: two panels -> the active one and its neighbour; one panel -> pick the other folder (opens a split).
static void cmp_toggle(void) {
    if (g_cmp.on) { cmp_end(); return; }
    Panel *ord[MAX_PANELS];
    int n = panels_ordered(A.root, ord, 0), i = 0;
    while (i < n && ord[i] != A.ap) i++;
    if (n >= 2 && i < n) {
        Panel *o = ord[i + 1 < n ? i + 1 : i - 1];
        cmp_start(A.ap->cur, o->cur);
        return;
    }
    menu_open_cmp_pick(active_pane(), A.ap ? A.ap->cur->pane[0].ly.list.x + S(40) : S(200), A.ap ? A.ap->cur->pane[0].ly.list.y + S(10) : S(100));
}

static void cmp_pick_add(const char *path, const char *cur) {
    if (g_cmp.npick >= 12 || !path[0] || !strcmp(path, cur) || strstr(path, "::") || du_under(cur, path) || du_under(path, cur)) return;
    for (int i = 0; i < g_cmp.npick; i++)
        if (!strcmp(g_cmp.pick[i], path)) return;
    snprintf(g_cmp.pick[g_cmp.npick++], PATH_MAX, "%s", path);
}

static void menu_open_cmp_pick(Pane *p, f32 x, f32 y) {
    Menu *m = &A.menu;
    m->keep_open = false;
    m->reopen = NULL;
    m->n = 0;
    m->hover = -1;
    m->pane = p;
    g_cmp.npick = 0;
    for (int i = 0; i < A.ntabs; i++) // other tabs first, then drives, then recent folders
        if (!A.tabs[i]->term) cmp_pick_add(A.tabs[i]->pane[0].path, p->path);
    int ntabs = g_cmp.npick;
    for (int i = A.nplaces_user; i < A.nplaces; i++)
        if (A.places[i].kind == PK_DRIVE && A.places[i].removable) cmp_pick_add(A.places[i].path, p->path);
    int ndrives = g_cmp.npick;
    for (int i = 0; i < g_nrecent && g_cmp.npick < 12; i++) cmp_pick_add(g_recent[i], p->path);
    menu_add_info("Compare with", path_basename(p->path));
    for (int i = 0; i < g_cmp.npick; i++) {
        if (i == ntabs || i == ndrives) menu_sep();
        menu_add(g_cmp.pick[i], i < ntabs ? "Tab" : i < ndrives ? "Drive" : "", ACT_CMP_PICK_BASE + i, true);
    }
    if (!g_cmp.npick) menu_add("Open a second panel (Ctrl+\\) with the other folder first", "", -1, false);
    menu_finish(x, y);
}

static void cmp_pick(int i) {
    if (i < 0 || i >= g_cmp.npick || !A.ap) return;
    Tab *cur = A.ap->cur;
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s", g_cmp.pick[i]);
    Tab *other = NULL; // an open tab in this panel moves into the split; anything else opens there
    for (int k = 0; k < A.ntabs; k++)
        if (A.tabs[k] != cur && A.tabs[k]->panel == A.ap && !A.tabs[k]->term && !strcmp(A.tabs[k]->pane[0].path, path)) other = A.tabs[k];
    Panel *np = panel_split(A.ap, ZONE_RIGHT, other, path);
    if (!np) return;
    cmp_start(cur, np->cur);
}

// Right-click a tab -> "Compare with current tab".
static void cmp_with_tab(Tab *t) {
    Tab *cur = A.ap ? A.ap->cur : NULL;
    if (!t || !cur || t == cur) return;
    if (t->panel != cur->panel) { // shown in another panel
        t->panel->cur = t;
        cmp_start(cur, t);
        return;
    }
    Panel *np = panel_split(cur->panel, ZONE_RIGHT, t, t->pane[0].path); // same panel: move it next to the current one
    if (np) cmp_start(cur, np->cur);
}

static void menu_open_cmp(Pane *p, f32 x, f32 y) { // the bar's Sync... button
    static char l1[96], l2[96];
    int s = cmp_side(p);
    if (s < 0) return;
    Menu *m = &A.menu;
    m->keep_open = false;
    m->reopen = NULL;
    m->n = 0;
    m->hover = -1;
    m->pane = p;
    bool ready = cmp_ready();
    const char *on = s ? "left" : "right"; // the other side
    snprintf(l1, sizeof l1, "Copy new and newer to the %s side", on);
    snprintf(l2, sizeof l2, "Mirror onto the %s side (its extra items to the Trash)", on);
    menu_add(l1, "", s ? ACT_CMP_SYNC_B : ACT_CMP_SYNC_A, ready);
    menu_add("Copy both ways", "", ACT_CMP_SYNC_BOTH, ready);
    menu_add(l2, "", s ? ACT_CMP_MIRROR_B : ACT_CMP_MIRROR_A, ready);
    menu_sep();
    menu_add_check("Differences only", ACT_CMP_DIFF_ONLY, g_cmp.diff_only, true);
    menu_add_check("Compare contents (slower)", ACT_CMP_CONTENTS, g_cmp.contents, true);
    menu_add("Compare again", "F5", ACT_CMP_AGAIN, true);
    menu_add("End compare", "", ACT_CMP_END, true);
    menu_finish(x, y);
}

static void cmp_action(Pane *p, int act) {
    switch (act) {
    case ACT_CMP_START: cmp_toggle(); break;
    case ACT_CMP_END: cmp_end(); break;
    case ACT_CMP_TAB: cmp_with_tab(A.menu_tab && tab_index(A.menu_tab) >= 0 ? A.menu_tab : NULL); break;
    case ACT_CMP_SYNC_A: case ACT_CMP_SYNC_B: cmp_sync(act == ACT_CMP_SYNC_B, false); break;
    case ACT_CMP_MIRROR_A: case ACT_CMP_MIRROR_B: cmp_sync(act == ACT_CMP_MIRROR_B, true); break;
    case ACT_CMP_SYNC_BOTH: cmp_sync(-1, false); break;
    case ACT_CMP_DIFF_ONLY: g_cmp.diff_only = !g_cmp.diff_only; cmp_resort(); break;
    case ACT_CMP_CONTENTS: g_cmp.contents = !g_cmp.contents; if (g_cmp.on) cmp_run(); break;
    case ACT_CMP_AGAIN: if (g_cmp.on) cmp_run(); break;
    default:
        if (act >= ACT_CMP_PICK_BASE && act < ACT_CMP_PICK_BASE + 12) cmp_pick(act - ACT_CMP_PICK_BASE);
        break;
    }
    (void)p;
    A.redraw = true;
}

// The bar under a compared panel: what differs on this side, Differences only, Sync..., end.
static void cmp_bar_layout(Pane *p, Rect *sw, Rect *sync, Rect *close) {
    Rect b = p->ly.cmpbar;
    f32 bh = roundf(S(28)), by = b.y + roundf((b.h - bh) * 0.5f);
    *close = (Rect){b.x + b.w - bh - roundf(S(8)), by, bh, bh};
    *sync = (Rect){close->x - roundf(S(84)) - roundf(S(6)), by, roundf(S(84)), bh};
    f32 sww = roundf(S(b.w < S(620) ? 136 : 186)); // narrow panels: a shorter label, more room for the counts
    *sw = (Rect){sync->x - sww - roundf(S(8)), by, sww, bh};
}

static void cmp_bar_text(Pane *p, char *out, size_t cap) {
    int s = cmp_side(p);
    if (s < 0) { out[0] = 0; return; }
    if (!cmp_ready()) {
        char n[32];
        fmt_count(n, atomic_load(&g_cmp.job->nitems));
        snprintf(out, cap, "Comparing\xE2\x80\xA6 %s items", n);
        return;
    }
    u32 *c = g_cmp.cnt[s];
    size_t o = 0;
    static const char *what[4] = {"only here", "newer here", "differ", "file vs folder"};
    for (int k = 0; k < 4; k++)
        if (c[k]) o += (size_t)snprintf(out + o, cap - o, "%s%u %s", o ? "  \xC2\xB7  " : "", c[k], what[k]);
    if (!o) {
        u32 *oc = g_cmp.cnt[!s];
        if (oc[0] || oc[1]) snprintf(out, cap, "Nothing new here (the other side has %u new, %u newer)", oc[0], oc[1]);
        else snprintf(out, cap, "Same as the other side");
    }
}

static void cmp_bar_click(Pane *p, f32 x, f32 y) {
    Rect sw, sync, close;
    cmp_bar_layout(p, &sw, &sync, &close);
    if (in_rect(close, x, y)) cmp_end();
    else if (in_rect(sync, x, y)) menu_open_cmp(p, sync.x, sync.y - S(4));
    else if (in_rect(sw, x, y)) cmp_action(p, ACT_CMP_DIFF_ONLY);
}

// --- snapshots view (main thread; snapshots.c finds them) ------------------------------------------------------
// A pane mode like disk usage: the drive's snapshots (Snapper, Timeshift, ZFS), newest first. Open one to browse it
// read-only (a bar under the list says where you are, with Compare with now and Restore), see what changed between
// two (snapper status), create / delete them (Snapper). Snapper keeps them root-only by default: "Allow me to browse
// snapshots" asks for the password once and adds you to the config's ALLOW_USERS (with SYNC_ACL).

enum { SC_NONE, SC_CREATE, SC_DELETE, SC_ALLOW, SC_STATUS };
typedef struct { char st; char path[PATH_MAX]; } SnapChange;

typedef struct SnapView {
    SnapList *l;
    int cur;            // row
    int sel2;           // a second picked row for "between these two" (-1 none)
    f64 scroll;
    int mode;           // 0 the snapshots, 1 what changed
    SnapCmd *cmd;
    int cmd_kind;
    SnapChange *ch;     // what changed: from snapshot ca to cb (0 = now)
    int nch, chcur;
    int ca, cb;
    Rect hdr, list, bact[5], bback, ballow;
    int nact, act[5];
} SnapView;

static bool snap_in(const char *path) {
    char r[PATH_MAX], l[PATH_MAX], n[96];
    return snap_where(path, r, sizeof r, l, sizeof l, n, sizeof n);
}

static bool pane_snap_on(const Pane *p) { return p->snapv && p->path[0] && !pane_is_archive(p) && !pane_is_trash(p) && !p->search_q[0]; }

static void snapview_close(Pane *p) {
    SnapView *v = p->sv;
    if (!v) return;
    if (v->l) snaplist_release(v->l);
    if (v->cmd) snapcmd_release(v->cmd);
    free(v->ch);
    free(v);
    p->sv = NULL;
}

static void snapview_reload(Pane *p) {
    SnapView *v = p->sv;
    if (!v) return;
    if (v->l) snaplist_release(v->l);
    v->l = snaplist_start(p->path);
    v->cur = 0;
    v->sel2 = -1;
    A.redraw = true;
}

static void snapview_open_tab(const char *path) {
    Tab *t = tab_new(path, A.cur + 1, true);
    if (t) t->pane[0].snapv = true;
    A.redraw = true;
}

static void snapview_toggle(Pane *p) {
    if (!p->snapv && (pane_is_archive(p) || pane_is_trash(p))) return;
    char root[PATH_MAX], live[PATH_MAX], nm[96];
    if (!p->snapv && snap_where(p->path, root, sizeof root, live, sizeof live, nm, sizeof nm)) pane_navigate(p, live, true); // the drive's
    p->snapv = !p->snapv;
    p->du = false;
    if (!p->snapv) snapview_close(p);
    layout();
    A.redraw = true;
}

static bool snapview_busy(void) {
    for (int i = 0; i < A.ntabs; i++) {
        Pane *p = &A.tabs[i]->pane[0];
        if (p->sv && ((p->sv->l && !atomic_load(&p->sv->l->done)) || p->sv->cmd)) return true;
    }
    return false;
}

static void snap_parse_status(SnapView *v, const char *out) {
    free(v->ch);
    v->ch = NULL;
    v->nch = 0;
    int cap = 0;
    for (const char *l = out; *l;) {
        const char *nl = strchr(l, '\n');
        size_t len = nl ? (size_t)(nl - l) : strlen(l);
        const char *sp = memchr(l, ' ', len);
        if (sp && sp[1] == '/') { // "c..... /etc/foo", "+..... /new", "-..... /gone"
            if (v->nch == cap) { cap = cap ? cap * 2 : 256; v->ch = xrealloc(v->ch, (size_t)cap * sizeof(SnapChange)); }
            SnapChange *c = &v->ch[v->nch++];
            c->st = l[0];
            snprintf(c->path, sizeof c->path, "%.*s", (int)(len - (size_t)(sp + 1 - l)), sp + 1);
        }
        l = nl ? nl + 1 : l + len;
    }
}

// Every loop: list what's asked for, pick up finished commands.
static void snap_tick(void) {
    for (int i = 0; i < A.ntabs; i++) {
        Pane *p = &A.tabs[i]->pane[0];
        if (!p->snapv) { if (p->sv) snapview_close(p); continue; }
        if (!pane_snap_on(p)) continue;
        if (!p->sv) {
            p->sv = xcalloc(1, sizeof(SnapView));
            p->sv->sel2 = -1;
            snapview_reload(p);
        }
        SnapView *v = p->sv;
        if (v->cmd && atomic_load(&v->cmd->done)) {
            SnapCmd *c = v->cmd;
            int k = v->cmd_kind;
            v->cmd = NULL;
            if (k == SC_STATUS) {
                if (c->rc == 0) { snap_parse_status(v, c->out); v->mode = 1; v->chcur = 0; v->scroll = 0; }
                else toast("snapper status failed (%d)", c->rc);
            } else if (c->rc != 0) {
                toast(k == SC_ALLOW ? "Not changed (the password prompt was cancelled, or snapper refused)" :
                      k == SC_CREATE ? "Couldn't create the snapshot (%d)" : "Couldn't delete the snapshot (%d)", c->rc);
            } else {
                if (k == SC_CREATE) toast("Snapshot created");
                else if (k == SC_DELETE) toast("Snapshot deleted");
                else toast("You can browse the snapshots now");
                snapview_reload(p);
            }
            snapcmd_release(c);
            A.redraw = true;
        }
    }
}

static SnapInfo *snap_cur(SnapView *v) {
    if (!v || !v->l || !atomic_load(&v->l->done) || v->cur < 0 || v->cur >= v->l->n) return NULL;
    return &v->l->s[v->cur];
}

static void snap_cmd(Pane *p, int kind, const char *const *argv, size_t cap) {
    SnapView *v = p->sv;
    if (!v || v->cmd) return;
    if (A.selftest && !getenv("FE_SNAPPER")) { toast("snapper is not run in tests"); return; }
    v->cmd = snapcmd_start(argv, cap);
    v->cmd_kind = kind;
    A.redraw = true;
}

static void snap_open(Pane *p, const char *root, const char *select_name) {
    p->snapv = false;
    snapview_close(p);
    pane_navigate(p, root, true);
    if (select_name) snprintf(p->select_name, sizeof p->select_name, "%s", select_name);
    layout();
}

// The live folder and the same folder in a snapshot, side by side, compared.
static void snap_compare(Pane *p, const char *in_snapshot, const char *live) {
    char a[PATH_MAX], b[PATH_MAX];
    snprintf(a, sizeof a, "%s", in_snapshot);
    snprintf(b, sizeof b, "%s", live);
    p->snapv = false;
    snapview_close(p);
    Tab *t = NULL;
    for (int i = 0; i < A.ntabs; i++)
        if (&A.tabs[i]->pane[0] == p) t = A.tabs[i];
    if (!t) return;
    if (strcmp(p->path, a)) pane_navigate(p, a, true);
    Panel *np = panel_split(t->panel, ZONE_RIGHT, NULL, b);
    if (np) cmp_start(t, np->cur);
}

static void snap_changes(Pane *p) { // what changed: a post and its pre, the two picked, or one and now
    SnapView *v = p->sv;
    SnapInfo *s = snap_cur(v);
    if (!s || !v->l->manage || !s->num) { toast("Only Snapper can tell what changed"); return; }
    int a = s->num, b = 0;
    if (v->sel2 >= 0 && v->sel2 < v->l->n && v->l->s[v->sel2].num) { a = MIN(s->num, v->l->s[v->sel2].num); b = MAX(s->num, v->l->s[v->sel2].num); }
    else if (!strcmp(s->type, "post") && s->pre) { a = s->pre; b = s->num; }
    v->ca = a;
    v->cb = b;
    char range[48];
    snprintf(range, sizeof range, "%d..%d", a, b);
    const char *av[] = {snapper_bin(), "-c", v->l->config, "status", range, NULL};
    snap_cmd(p, SC_STATUS, av, 8u << 20);
}

static void snap_delete_cb(int choice, bool check, void *ud) {
    (void)check;
    Pane *p = ud;
    if (choice != 0 || !pane_exists(p) || !p->sv) return;
    SnapInfo *s = snap_cur(p->sv);
    if (!s) return;
    char n1[16], n2[16];
    snprintf(n1, sizeof n1, "%d", s->num);
    const char *av[] = {snapper_bin(), "-c", p->sv->l->config, "delete", n1, NULL, NULL};
    if (p->sv->sel2 >= 0 && p->sv->l->s[p->sv->sel2].num) { snprintf(n2, sizeof n2, "%d", p->sv->l->s[p->sv->sel2].num); av[5] = n2; }
    snap_cmd(p, SC_DELETE, av, 4096);
}

static void snap_create_cb(int choice, bool check, void *ud) {
    (void)check;
    Pane *p = ud;
    if (choice != 0 || !pane_exists(p) || !p->sv || !p->sv->l) return;
    const char *av[] = {snapper_bin(), "-c", p->sv->l->config, "create", "--description", "File Explorer", "--cleanup-algorithm", "number", NULL};
    snap_cmd(p, SC_CREATE, av, 4096);
}

// The bar under a folder inside a snapshot: restore what's selected (or this folder) to where it lives now.
static char **g_snap_rsrc, **g_snap_rdst;
static int g_snap_rn;

static void snap_restore_cb(int choice, bool check, void *ud) {
    (void)check;
    (void)ud;
    if (choice == 0 && g_snap_rn) {
        Op *op = op_new(OP_SYNC, g_snap_rsrc, g_snap_rn, NULL);
        op->dst = strv_dup(g_snap_rdst, g_snap_rn);
        op->policy = CF_REPLACE;
        op->trash_first = true; // the current ones go to the Trash first (system files: retry as administrator replaces)
        ops_start(op);
    }
    strv_free(g_snap_rsrc, g_snap_rn);
    strv_free(g_snap_rdst, g_snap_rn);
    g_snap_rsrc = g_snap_rdst = NULL;
    g_snap_rn = 0;
}

static void snap_restore(Pane *p) {
    char root[PATH_MAX], live[PATH_MAX], nm[96];
    if (!snap_where(p->path, root, sizeof root, live, sizeof live, nm, sizeof nm)) return;
    int n = 0;
    char **paths = pane_selection_paths(p, &n);
    if (!n) { strv_free(paths, n); paths = xmalloc(sizeof(char *)); paths[0] = strdup(p->path); n = 1; }
    strv_free(g_snap_rsrc, g_snap_rn);
    strv_free(g_snap_rdst, g_snap_rn);
    g_snap_rsrc = paths;
    g_snap_rdst = xmalloc((size_t)n * sizeof(char *));
    g_snap_rn = n;
    size_t rl = strlen(root);
    for (int i = 0; i < n; i++) {
        char r2[PATH_MAX], l2[PATH_MAX], n2[96];
        if (snap_where(paths[i], r2, sizeof r2, l2, sizeof l2, n2, sizeof n2)) g_snap_rdst[i] = strdup(l2);
        else g_snap_rdst[i] = strdup(paths[i] + MIN(rl, strlen(paths[i]))); // (not expected)
    }
    char msg[600];
    if (n == 1) snprintf(msg, sizeof msg, "Put \xE2\x80\x9C%s\xE2\x80\x9D back as it was in snapshot %s? The current one goes to the Trash first.",
                         g_snap_rdst[0][1] ? path_basename(g_snap_rdst[0]) : "/", nm);
    else snprintf(msg, sizeof msg, "Put these %d items back as they were in snapshot %s? The current ones go to the Trash first.", n, nm);
    static const char *b[] = {"Restore", "Cancel"};
    dialog_open("Restore from snapshot", msg, b, 2, 0, 1, NULL, snap_restore_cb, NULL);
    A.dlg.icon = DI_QUESTION;
    Dialog *d = &A.dlg; // where each goes
    for (int i = 0; i < n && d->nitems < 4; i++) {
        char tp[PATH_MAX];
        tilde_path(g_snap_rdst[i], tp, sizeof tp);
        struct stat st;
        d->item_dir[d->nitems] = lstat(paths[i], &st) == 0 && S_ISDIR(st.st_mode);
        d->item_sub[d->nitems] = strdup(tp);
        d->items[d->nitems++] = strdup(path_basename(paths[i]));
    }
    d->more = n - d->nitems;
}

static void snapbar_layout(Pane *p, Rect *bcmp, Rect *brest, Rect *ball) {
    Rect b = p->ly.snapbar;
    f32 bh = roundf(S(28)), by = b.y + roundf((b.h - bh) * 0.5f), x = b.x + b.w - roundf(S(10));
    f32 w3 = roundf(S(112)), w2 = roundf(S(92)), w1 = roundf(S(140));
    *ball = (Rect){x - w3, by, w3, bh};
    x -= w3 + roundf(S(6));
    *brest = (Rect){x - w2, by, w2, bh};
    x -= w2 + roundf(S(6));
    *bcmp = (Rect){x - w1, by, w1, bh};
}

static void snapbar_click(Pane *p, f32 x, f32 y) {
    Rect bc, br, ba;
    snapbar_layout(p, &bc, &br, &ba);
    char root[PATH_MAX], live[PATH_MAX], nm[96];
    if (!snap_where(p->path, root, sizeof root, live, sizeof live, nm, sizeof nm)) return;
    if (in_rect(bc, x, y)) snap_compare(p, p->path, live);
    else if (in_rect(br, x, y)) snap_restore(p);
    else if (in_rect(ba, x, y)) { pane_navigate(p, live, true); p->snapv = true; layout(); }
}

static void menu_open_snap(Pane *p, f32 x, f32 y) {
    SnapView *v = p->sv;
    Menu *m = &A.menu;
    m->keep_open = false;
    m->reopen = NULL;
    m->n = 0;
    m->hover = -1;
    m->pane = p;
    SnapInfo *s = snap_cur(v);
    bool mg = v && v->l && v->l->manage;
    if (v && v->mode == 1) {
        menu_add("Show in the snapshot", "Enter", ACT_SNAP_OPEN, v->nch > 0);
        menu_add("Back to the snapshots", "Esc", ACT_SNAP_BACK, true);
    } else {
        menu_add("Open", "Enter", ACT_SNAP_OPEN, s != NULL);
        menu_add("Compare with now", "", ACT_SNAP_COMPARE, s != NULL);
        menu_add(v && v->sel2 >= 0 ? "What changed between these two" : s && !strcmp(s->type, "post") ? "What this change did" : "What changed since",
                 "", ACT_SNAP_CHANGES, s && mg);
        menu_sep();
        menu_add(v && v->sel2 >= 0 ? "Delete these two snapshots\xE2\x80\xA6" : "Delete snapshot\xE2\x80\xA6", "Del", ACT_SNAP_DELETE, s && mg);
        menu_add("Create snapshot now\xE2\x80\xA6", "", ACT_SNAP_CREATE, mg);
        menu_sep();
        menu_add("Refresh", "F5", ACT_SNAP_REFRESH, true);
    }
    menu_add("Back to the files", "", ACT_SNAP_TOGGLE, true);
    menu_finish(x, y);
}

static void snap_action(Pane *p, int act) {
    SnapView *v = p->sv;
    SnapInfo *s = snap_cur(v);
    switch (act) {
    case ACT_SNAP_TOGGLE: snapview_toggle(p); break;
    case ACT_SNAP_PLACE:
        if (A.menu_place >= 0 && A.menu_place < A.nplaces && A.places[A.menu_place].path[0]) snapview_open_tab(A.places[A.menu_place].path);
        break;
    case ACT_SNAP_REFRESH: snapview_reload(p); break;
    case ACT_SNAP_BACK: if (v) v->mode = 0; break;
    case ACT_SNAP_OPEN:
        if (v && v->mode == 1) { // a changed file: where it is in the newer snapshot (or now), or the older one when it's gone
            if (v->chcur < 0 || v->chcur >= v->nch) break;
            SnapChange *c = &v->ch[v->chcur];
            int num = c->st == '-' ? v->ca : v->cb;
            char base[PATH_MAX], full[PATH_MAX], par[PATH_MAX];
            if (num) snprintf(base, sizeof base, "%s%s.snapshots/%d/snapshot", v->l->mnt, v->l->mnt[1] ? "/" : "", num);
            else snprintf(base, sizeof base, "%s", v->l->mnt[1] ? v->l->mnt : "");
            size_t ml = strlen(v->l->mnt);
            const char *rel = ml > 1 && !strncmp(c->path, v->l->mnt, ml) ? c->path + ml : c->path;
            snprintf(full, sizeof full, "%s%s", base, rel);
            parent_of(full, par, sizeof par);
            snap_open(p, par[0] ? par : "/", path_basename(full));
        } else if (s) snap_open(p, s->root, NULL);
        break;
    case ACT_SNAP_COMPARE: if (s) snap_compare(p, s->root, v->l->mnt); break;
    case ACT_SNAP_CHANGES: snap_changes(p); break;
    case ACT_SNAP_DELETE: {
        if (!s || !v->l->manage) break;
        char msg[400];
        if (v->sel2 >= 0) snprintf(msg, sizeof msg, "Delete snapshots %s and %s? Their files are gone for good.", s->name, v->l->s[v->sel2].name);
        else snprintf(msg, sizeof msg, "Delete snapshot %s (%s)? Its files are gone for good.", s->name, s->desc[0] ? s->desc : s->type);
        static const char *b[] = {"Delete", "Cancel"};
        dialog_open("Delete snapshot", msg, b, 2, 0, 1, NULL, snap_delete_cb, p);
        A.dlg.icon = DI_DANGER;
        A.dlg.danger = true;
        break;
    }
    case ACT_SNAP_CREATE: {
        if (!v || !v->l || !v->l->manage) break;
        char msg[300];
        snprintf(msg, sizeof msg, "Take a snapshot of %s now? Snapper removes old ones like this by itself (number cleanup).", v->l->mnt);
        static const char *b[] = {"Create", "Cancel"};
        dialog_open("Create snapshot", msg, b, 2, 0, 1, NULL, snap_create_cb, p);
        A.dlg.icon = DI_QUESTION;
        break;
    }
    case ACT_SNAP_ALLOW:
        if (v && v->l && v->l->config[0] && admin_allowed() && !v->cmd) {
            v->cmd = snap_allow_start(v->l->config);
            v->cmd_kind = SC_ALLOW;
        }
        break;
    case ACT_SNAP_RESTORE: snap_restore(p); break;
    default: break;
    }
    A.redraw = true;
}

static f32 snap_row_h(void) { return active_pane()->ly.row_h > 0 ? active_pane()->ly.row_h : roundf(S(26)); } // like the file list's rows

static int snap_row_at(SnapView *v, f32 x, f32 y) {
    if (!v || !in_rect(v->list, x, y)) return -1;
    int n = v->mode ? v->nch : v->l && atomic_load(&v->l->done) ? v->l->n : 0;
    int r = (int)((y - v->list.y + v->scroll) / snap_row_h());
    return r >= 0 && r < n ? r : -1;
}

static void snap_visible(SnapView *v) {
    f32 rh = snap_row_h();
    int c = v->mode ? v->chcur : v->cur;
    f64 top = (f64)c * rh;
    if (top < v->scroll) v->scroll = top;
    else if (top + rh > v->scroll + v->list.h) v->scroll = top + rh - v->list.h;
}

static void snap_mouse_down(Pane *p, SDL_MouseButtonEvent *b, f32 x, f32 y) {
    SnapView *v = p->sv;
    if (!v) return;
    bool ctrl = SDL_GetModState() & SDL_KMOD_CTRL;
    if (b->button == SDL_BUTTON_LEFT) {
        if (v->ballow.w > 0 && in_rect(v->ballow, x, y)) { snap_action(p, ACT_SNAP_ALLOW); return; }
        if (v->bback.w > 0 && in_rect(v->bback, x, y)) { snap_action(p, v->mode ? ACT_SNAP_BACK : ACT_SNAP_TOGGLE); return; }
        for (int i = 0; i < v->nact; i++)
            if (in_rect(v->bact[i], x, y)) { snap_action(p, v->act[i]); return; }
    }
    int r = snap_row_at(v, x, y);
    if (v->mode == 1) {
        if (r >= 0) v->chcur = r;
        if (b->button == SDL_BUTTON_RIGHT) menu_open_snap(p, x, y);
        else if (r >= 0 && b->clicks >= 2) snap_action(p, ACT_SNAP_OPEN);
        return;
    }
    if (r >= 0) {
        if (ctrl && b->button == SDL_BUTTON_LEFT) v->sel2 = r == v->cur ? -1 : r == v->sel2 ? -1 : r; // a second one, for "between"
        else if (b->button != SDL_BUTTON_RIGHT || (r != v->cur && r != v->sel2)) { v->cur = r; v->sel2 = -1; }
    }
    if (b->button == SDL_BUTTON_RIGHT) menu_open_snap(p, x, y);
    else if (r >= 0 && b->clicks >= 2 && !ctrl) snap_action(p, ACT_SNAP_OPEN);
    A.redraw = true;
}

static void snap_wheel(Pane *p, f32 dy) {
    SnapView *v = p->sv;
    if (!v) return;
    f32 rh = snap_row_h();
    int n = v->mode ? v->nch : v->l ? v->l->n : 0;
    v->scroll = CLAMP(v->scroll - (f64)dy * rh * 3, 0.0, MAX(0.0, (f64)n * rh - v->list.h));
    A.redraw = true;
}

static bool snap_key(Pane *p, SDL_Keycode k) {
    SnapView *v = p->sv;
    if (!v) return false;
    int n = v->mode ? v->nch : v->l && atomic_load(&v->l->done) ? v->l->n : 0;
    int *c = v->mode ? &v->chcur : &v->cur;
    int page = MAX(1, (int)(v->list.h / snap_row_h()) - 1);
    switch (k) {
    case SDLK_DOWN: *c = MIN(*c + 1, n - 1); break;
    case SDLK_UP: *c = MAX(*c - 1, 0); break;
    case SDLK_PAGEDOWN: *c = MIN(*c + page, n - 1); break;
    case SDLK_PAGEUP: *c = MAX(*c - page, 0); break;
    case SDLK_HOME: *c = 0; break;
    case SDLK_END: *c = n - 1; break;
    case SDLK_RETURN: case SDLK_KP_ENTER: snap_action(p, ACT_SNAP_OPEN); return true;
    case SDLK_ESCAPE: case SDLK_BACKSPACE:
        if (v->mode) v->mode = 0;
        else if (v->sel2 >= 0) v->sel2 = -1;
        else snapview_toggle(p);
        return true;
    case SDLK_DELETE: if (!v->mode) snap_action(p, ACT_SNAP_DELETE); return true;
    case SDLK_F5: snapview_reload(p); return true;
    case SDLK_APPLICATION: menu_open_snap(p, v->list.x + S(40), v->list.y + S(20)); return true;
    default: return false;
    }
    *c = MAX(*c, 0);
    if (!v->mode) v->sel2 = -1;
    snap_visible(v);
    A.redraw = true;
    return true;
}

// --- commands --------------------------------------------------------------


typedef struct { const char *label, *shortcut; int id; } CmdDef;
static const CmdDef g_cmds[] = {
    {"Go to file or folder", "Ctrl+P", CMD_GOTO},
    {"Show all commands", "Ctrl+Shift+P", CMD_PALETTE},
    {"Search in this folder", "Ctrl+F", CMD_SEARCH},
    {"Toggle search in subfolders", "Alt+R", CMD_SEARCH_SUBFOLDERS},
    {"Search in file contents", "", CMD_SEARCH_CONTENTS},
    {"Toggle search in file contents", "Alt+T", CMD_CS_TOGGLE},
    {"New tab", "Ctrl+T", CMD_NEW_TAB},
    {"Close tab", "Ctrl+W", CMD_CLOSE_TAB},
    {"Next tab", "Ctrl+Tab", CMD_NEXT_TAB},
    {"Previous tab", "Ctrl+Shift+Tab", CMD_PREV_TAB},
    {"Split panel right", "Ctrl+\\", CMD_SPLIT},
    {"Split panel down", "Ctrl+Shift+\\", CMD_SPLIT_DOWN},
    {"Next panel", "Tab", CMD_SWITCH_PANE},
    {"Previous panel", "Shift+Tab", CMD_PREV_PANE},
    {"Close panel", "", CMD_CLOSE_PANEL},
    {"Copy selection to other panel", "Shift+F5", CMD_COPY_OTHER},
    {"Move selection to other panel", "Shift+F6", CMD_MOVE_OTHER},
    {"Reopen closed tab", "Ctrl+Shift+T", ACT_TAB_REOPEN},
    {"Search tabs", "Ctrl+Shift+A", CMD_TABS_POPUP},
    {"Connect to server\xE2\x80\xA6 (SFTP, SMB, FTP, WebDAV, NFS)", "", CMD_CONNECT},
    {"Duplicate tab", "", ACT_TAB_DUP},
    {"Close other tabs", "", ACT_TAB_CLOSE_OTHERS},
    {"New window", "Ctrl+N", CMD_NEW_WINDOW},
    {"New folder", "Ctrl+Shift+N", ACT_NEW_FOLDER},
    {"New file", "Ctrl+Alt+N", ACT_NEW_FILE},
    {"Rename", "F2", ACT_RENAME},
    {"Batch rename", "F2 (multiple)", ACT_BATCH_RENAME},
    {"Open with", "", ACT_OPEN_WITH},
    {"Properties", "Alt+Enter", ACT_PROPERTIES},
    {"Copy", "Ctrl+C", ACT_COPY},
    {"Cut", "Ctrl+X", ACT_CUT},
    {"Paste", "Ctrl+V", ACT_PASTE},
    {"Move to Trash", "Del", ACT_TRASH},
    {"Delete permanently", "Shift+Del", ACT_DELETE},
    {"Undo", "Ctrl+Z", ACT_UNDO},
    {"Select all", "Ctrl+A", ACT_SELECT_ALL},
    {"Copy path", "Ctrl+Shift+C", ACT_COPY_PATH},
    {"Open terminal here", "F4", ACT_TERMINAL},
    {"Toggle hidden files", "Ctrl+H", ACT_HIDDEN},
    {"Tree view", "Ctrl+Shift+E", CMD_TREE_VIEW},
    {"Empty Trash", "", ACT_TRASH_EMPTY},
    {"Show all file types", "", ACT_TF_RESET},
    {"Toggle folder sizes", "", CMD_FOLDER_SIZES},
    {"Toggle Git status", "", CMD_GIT_STATUS},
    {"Disk usage of this folder (treemap / sunburst)", "Ctrl+Shift+U", CMD_DISK_USAGE},
    {"Compare folders (two panels) / end compare", "Ctrl+Shift+K", CMD_COMPARE},
    {"Snapshots of this drive (Snapper, Timeshift, ZFS)", "", CMD_SNAPSHOTS},
    {"Refresh", "F5", ACT_REFRESH},
    {"Go back", "Alt+Left", CMD_BACK},
    {"Go forward", "Alt+Right", CMD_FORWARD},
    {"Go up", "Alt+Up", CMD_UP},
    {"Go home", "", CMD_HOME},
    {"Edit path", "Ctrl+L", CMD_EDIT_PATH},
    {"Sort by name", "", CMD_SORT_NAME},
    {"Sort by date modified", "", CMD_SORT_DATE},
    {"Sort by type", "", CMD_SORT_TYPE},
    {"Sort by size", "", CMD_SORT_SIZE},
    {"Re-index folders for Go to", "", CMD_REINDEX},
    {"Toggle preview panel", "Space / Ctrl+I", ACT_PREVIEW},
    {"Pin / unpin folder in sidebar", "Ctrl+D", ACT_PIN},
    {"Zoom in", "Ctrl+=", CMD_ZOOM_IN},
    {"Zoom out", "Ctrl+-", CMD_ZOOM_OUT},
    {"Reset zoom", "Ctrl+0", CMD_ZOOM_RESET},
    {"Toggle restore tabs on startup", "", CMD_RESTORE_SESSION},
    {"Options", "Ctrl+,", CMD_OPTIONS},
    {"Transfers: queue, pause, speed", "Ctrl+J", CMD_TRANSFERS},
    {"Save layout", "", CMD_SAVE_LAYOUT},
    {"Open settings file", "", CMD_OPEN_SETTINGS},
    {"Icon theme: Material", "", CMD_ICON_THEME_BASE + 0},
    {"Icon theme: Catppuccin", "", CMD_ICON_THEME_BASE + 1},
    {"Icon theme: Symbols", "", CMD_ICON_THEME_BASE + 2},
    {"Icon theme: Monokai", "", CMD_ICON_THEME_BASE + 3},
    {"Toggle frame-time overlay", "F12", CMD_OVERLAY},
    {"Quit", "Ctrl+Q", CMD_QUIT},
};

static void search_focus(Pane *p);
static void pal_open(bool commands);
static void pal_open_tabs(void);
static bool is_dir_path(const char *p);
static void search_toggle_recursive(Pane *p);
static void set_zoom(f32 z);
static void settings_save(void);
static void settings_file(char *out, size_t cap, bool dir_only);

static void run_command(int id) {
    Pane *p = active_pane();
    if (id < 100) { A.menu_tab = cur_tab(); do_action(p, id); return; }
    switch (id) {
    case CMD_NEW_TAB: tab_new(p->path, A.cur + 1, true); break;
    case CMD_CLOSE_TAB: tab_close(A.cur); break;
    case CMD_SPLIT: panel_split(A.ap, ZONE_RIGHT, NULL, p->path); break;
    case CMD_SPLIT_DOWN: panel_split(A.ap, ZONE_BOTTOM, NULL, p->path); break;
    case CMD_SWITCH_PANE: panel_cycle(1); break;
    case CMD_PREV_PANE: panel_cycle(-1); break;
    case CMD_NEXT_TAB: tab_cycle(1); break;
    case CMD_PREV_TAB: tab_cycle(-1); break;
    case CMD_GOTO: pal_open(false); break;
    case CMD_PALETTE: pal_open(true); break;
    case CMD_NEW_WINDOW: new_window(p->path); break;
    case CMD_CLOSE_PANEL: {
        if (A.npanels < 2) { toast("This is the only panel"); break; }
        Panel *pn = A.ap;
        for (int i = A.ntabs - 1; i >= 0 && panel_alive(pn); i--)
            if (A.tabs[i]->panel == pn) tab_close(i);
        break;
    }
    case CMD_COPY_OTHER:
    case CMD_MOVE_OTHER: {
        Panel *dst = A.prev_panel && panel_alive(A.prev_panel) && A.prev_panel != A.ap ? A.prev_panel : NULL;
        if (!dst && A.npanels == 2) dst = A.panels[0] == A.ap ? A.panels[1] : A.panels[0];
        if (!dst || !dst->cur) { toast("Open a second panel first (Ctrl+\\)"); break; }
        int n;
        char **paths = pane_selection_paths(p, &n);
        if (n) start_transfer(paths, n, dst->cur->pane[0].path, id == CMD_MOVE_OTHER, false);
        strv_free(paths, n);
        break;
    }
    case CMD_BACK: pane_history(p, -1); break;
    case CMD_FORWARD: pane_history(p, +1); break;
    case CMD_UP: pane_up(p); break;
    case CMD_HOME: pane_navigate(p, getenv("HOME") ? getenv("HOME") : "/", true); break;
    case CMD_SEARCH: search_focus(p); break;
    case CMD_SEARCH_SUBFOLDERS: search_toggle_recursive(p); break;
    case CMD_SEARCH_CONTENTS: search_focus_contents(p); break;
    case CMD_CS_TOGGLE: search_set_contents(p, !A.search_contents); if (!A.search_pane) search_focus(p); break;
    case CMD_ZOOM_IN: set_zoom(A.zoom + 0.1f); break;
    case CMD_ZOOM_OUT: set_zoom(A.zoom - 0.1f); break;
    case CMD_ZOOM_RESET: set_zoom(1.f); break;
    case CMD_RESTORE_SESSION:
        A.restore_session = !A.restore_session;
        A.settings_dirty = true;
        toast(A.restore_session ? "Tabs will be restored on startup" : "Tabs won't be restored on startup");
        break;
    case CMD_OPEN_SETTINGS: {
        settings_save();
        char path[PATH_MAX];
        settings_file(path, sizeof path, false);
        open_path(path);
        break;
    }
    case CMD_SORT_NAME: pane_set_sort(p, SORT_NAME); break;
    case CMD_SORT_DATE: pane_set_sort(p, SORT_DATE); break;
    case CMD_SORT_TYPE: pane_set_sort(p, SORT_TYPE); break;
    case CMD_SORT_SIZE: pane_set_sort(p, SORT_SIZE); break;
    case CMD_FOLDER_SIZES:
        A.folder_sizes = !A.folder_sizes;
        A.settings_dirty = true;
        toast(A.folder_sizes ? "Folder sizes on" : "Folder sizes off");
        break;
    case CMD_DISK_USAGE: du_toggle(p); break;
    case CMD_COMPARE: cmp_action(p, ACT_CMP_START); break;
    case CMD_SNAPSHOTS: snapview_toggle(p); break;
    case CMD_GIT_STATUS:
        do_action(p, ACT_GIT_STATUS);
        toast(A.git_status ? "Git status on" : "Git status off");
        break;
    case CMD_EDIT_PATH: begin_edit(p, NULL); break;
    case CMD_OVERLAY: A.show_overlay = !A.show_overlay; break;
    case CMD_OPTIONS: options_open(); break;
    case CMD_TRANSFERS: queue_toggle(); break;
    case CMD_SAVE_LAYOUT: options_open_page(OC_LAYOUTS); break;
    case CMD_TREE_VIEW: tree_toggle_mode(p); break;
    case CMD_TABS_POPUP: pal_open_tabs(); break;
    case CMD_CONNECT: connect_open(-1); break;
    case CMD_REINDEX: index_rebuild_default(); toast("Re-indexing folders\xE2\x80\xA6"); break;
    case CMD_QUIT: A.quit = true; break;
    default:
        if (id >= CMD_LAYOUT_BASE && id < CMD_LAYOUT_BASE + MAX_LAYOUTS) {
            if (layout_open(id - CMD_LAYOUT_BASE)) toast("Layout \xE2\x80\x9C%s\xE2\x80\x9D", g_layouts[id - CMD_LAYOUT_BASE].name);
        } else if (id >= CMD_ICON_THEME_BASE && id < CMD_ICON_THEME_BASE + ICON_THEMES) {
            icon_set_theme(id - CMD_ICON_THEME_BASE);
            toast("Icon theme: %s", g_icon_theme_names[g_icon_theme]);
            A.settings_dirty = true;
        }
        break;
    }
    A.redraw = true;
}

#include "keys.c"

// --- command palette (Ctrl+P: go to anything, Ctrl+Shift+P / ">": commands) ----

enum { PI_CMD, PI_RECENT, PI_PLACE, PI_DIR, PI_FILE, PI_ENTRY, PI_TAB, PI_CLOSED };

typedef struct {
    int kind, score, cmd, tab;
    char *text;  // path, or command label
    char *label; // place label (optional)
    u16 pos[FZ_MAXPOS];
    int npos;
} PalItem;

#define PAL_K 96
static PalItem g_pal_items[PAL_K];

typedef struct {
    u32 gen;
    char q[256];
    bool cmd_mode, tab_mode; // '>' commands, '@' tabs
    char **closed;
    int nclosed;
    int *tab_panel; // panel number of each tab (1-based)
    int cur_tab;
    Listing *L;
    char cur[PATH_MAX];
    char **recent, **places, **place_labels, **tabs;
    int nrecent, nplaces, ntabs;
    PalItem out[PAL_K];
    int nout;
    char cmd_sc[128][48]; // shortcut labels (keymap is main-thread state)
    char layouts[MAX_LAYOUTS][64];
    int nlayouts;
} PalJob;

static void pal_item_free(PalItem *it) {
    free(it->text);
    free(it->label);
    it->text = it->label = NULL;
}

static void pal_add(PalJob *j, PalItem *it) {
    if (j->nout < PAL_K) { j->out[j->nout++] = *it; return; }
    int m = 0;
    for (int i = 1; i < PAL_K; i++)
        if (j->out[i].score < j->out[m].score) m = i;
    if (it->score <= j->out[m].score) { pal_item_free(it); return; }
    pal_item_free(&j->out[m]);
    j->out[m] = *it;
}

static bool pal_would_add(PalJob *j, int score) {
    if (j->nout < PAL_K) return true;
    for (int i = 0; i < PAL_K; i++)
        if (j->out[i].score < score) return true;
    return false;
}

static void pal_try_path(PalJob *j, int kind, const char *path, const char *label, const char *q, int qn, int bonus, int tab) {
    PalItem it = {0};
    it.score = fuzzy_path(path, q, qn, it.pos, &it.npos);
    if (it.score < 0 && label) {
        int s = fuzzy_match(label, (int)strlen(label), q, qn, NULL);
        if (s >= 0) { it.score = s + 40; it.npos = 0; }
    }
    if (it.score < 0) return;
    it.score += bonus;
    if (!pal_would_add(j, it.score)) return;
    it.kind = kind;
    it.tab = tab;
    it.text = strdup(path);
    it.label = label ? strdup(label) : NULL;
    pal_add(j, &it);
}

static int pal_cmp(const void *a, const void *b) {
    const PalItem *x = a, *y = b;
    return (y->score > x->score) - (y->score < x->score);
}

static void pal_done(void *arg);

static void pal_job(void *arg) {
    PalJob *j = arg;
    const char *q = j->q;
    while (*q == ' ') q++;
    int qn = (int)strlen(q);
    if (j->tab_mode) { // searchable tabs: open (all panels) + recently closed
        for (int i = 0; i < j->ntabs; i++) {
            if (!qn) {
                PalItem it = {.kind = PI_TAB, .score = 2000 - i + (i == j->cur_tab ? 500 : 0), .text = strdup(j->tabs[i]), .tab = i};
                pal_add(j, &it);
            } else pal_try_path(j, PI_TAB, j->tabs[i], NULL, q, qn, 15, i);
        }
        for (int k = j->nclosed - 1; k >= 0; k--) {
            int age = j->nclosed - 1 - k;
            bool newer = false; // the same folder closed twice: list it once (newest)
            for (int k2 = k + 1; k2 < j->nclosed && !newer; k2++) newer = !strcmp(j->closed[k2], j->closed[k]);
            if (newer) continue;
            if (!qn) {
                PalItem it = {.kind = PI_CLOSED, .score = 500 - age, .text = strdup(j->closed[k]), .tab = k};
                pal_add(j, &it);
            } else pal_try_path(j, PI_CLOSED, j->closed[k], NULL, q, qn, -age, k);
        }
    }
    if (!j->tab_mode && (j->cmd_mode || qn >= 2)) {
        for (size_t i = 0; i < ARRAY_COUNT(g_cmds); i++) {
            PalItem it = {0};
            const char *l = g_cmds[i].label;
            it.score = qn ? fuzzy_match(l, (int)strlen(l), q, qn, it.pos) : 1000 - (int)i;
            if (it.score < 0) continue;
            it.npos = qn ? MIN(qn, FZ_MAXPOS) : 0;
            if (!j->cmd_mode) it.score -= 20;
            it.kind = PI_CMD;
            it.cmd = g_cmds[i].id;
            it.text = strdup(l);
            it.label = strdup(i < 128 ? j->cmd_sc[i] : g_cmds[i].shortcut);
            pal_add(j, &it);
        }
        for (int i = 0; i < j->nlayouts; i++) { // saved layouts
            char l[96];
            snprintf(l, sizeof l, "Layout: %s", j->layouts[i]);
            PalItem it = {0};
            it.score = qn ? fuzzy_match(l, (int)strlen(l), q, qn, it.pos) : 900 - i;
            if (it.score < 0) continue;
            it.npos = qn ? MIN(qn, FZ_MAXPOS) : 0;
            if (!j->cmd_mode) it.score -= 10;
            it.kind = PI_CMD;
            it.cmd = CMD_LAYOUT_BASE + i;
            it.text = strdup(l);
            it.label = strdup("");
            pal_add(j, &it);
        }
    }
    if (!j->cmd_mode && !j->tab_mode) {
        if (!qn) {
            for (int i = 0; i < j->nrecent && i < 30; i++) {
                if (!strcmp(j->recent[i], j->cur)) continue;
                PalItem it = {.kind = PI_RECENT, .score = 1000 - i, .text = strdup(j->recent[i])};
                pal_add(j, &it);
            }
            for (int i = 0; i < j->nplaces; i++) {
                PalItem it = {.kind = PI_PLACE, .score = 500 - i, .text = strdup(j->places[i]), .label = strdup(j->place_labels[i])};
                pal_add(j, &it);
            }
        } else {
            for (int i = 0; i < j->nrecent; i++)
                if (strcmp(j->recent[i], j->cur)) pal_try_path(j, PI_RECENT, j->recent[i], NULL, q, qn, 25, 0);
            for (int i = 0; i < j->nplaces; i++) pal_try_path(j, PI_PLACE, j->places[i], j->place_labels[i], q, qn, 20, 0);
            for (int i = 0; i < j->ntabs; i++) pal_try_path(j, PI_TAB, j->tabs[i], NULL, q, qn, 15, i);
            if (j->L) { // entries of the current folder
                Listing *L = j->L;
                size_t cl = strlen(j->cur);
                for (u32 i = 0; i < L->count && i < 200000; i++) {
                    Entry *e = &L->entries[i];
                    PalItem it = {0};
                    it.score = fuzzy_match(e->name, (int)e->name_len, q, qn, it.pos);
                    if (it.score < 0) continue;
                    it.score += 30 + (e->flags & ENT_DIR ? 5 : 0);
                    if (!pal_would_add(j, it.score)) continue;
                    it.npos = MIN(qn, FZ_MAXPOS);
                    int off = (int)cl + (cl > 1 ? 1 : 0);
                    for (int k = 0; k < it.npos; k++) it.pos[k] = (u16)(it.pos[k] + off);
                    char full[PATH_MAX];
                    path_join(full, sizeof full, j->cur, e->name);
                    it.kind = PI_ENTRY;
                    it.text = strdup(full);
                    pal_add(j, &it);
                }
            }
            DirIndex *d = index_acquire();
            if (d) {
                // Query chars each folder's path uses up (greedy subsequence): a file can only match
                // if the rest fits in its name, so most files are rejected without building their path.
                u8 *used = xmalloc(MAX(d->n, 1u));
                char full[PATH_MAX];
                for (u32 i = 0; i < d->n; i++) {
                    bool file = index_is_file(d, i);
                    if (file) {
                        int k = used[d->parent[i]];
                        if (k < qn && fz_consume(d->buf + d->offs[i], q + k, qn - k) < qn - k) continue;
                    } else {
                        used[i] = (u8)fz_consume(d->buf + d->offs[i], q, qn);
                    }
                    // the current folder and its files are already listed (as "in this folder")
                    if (!strcmp(d->buf + d->offs[file ? d->parent[i] : i], j->cur)) continue;
                    const char *path = index_path(d, i, full, sizeof full);
                    PalItem it = {0};
                    it.score = fuzzy_path(path, q, qn, it.pos, &it.npos);
                    if (it.score < 0 || !pal_would_add(j, it.score)) continue;
                    it.kind = file ? PI_FILE : PI_DIR;
                    it.text = strdup(path);
                    pal_add(j, &it);
                }
                free(used);
                index_release(d);
            }
        }
    }
    qsort(j->out, (size_t)j->nout, sizeof(PalItem), pal_cmp);
    // drop duplicate paths (keep the best-scored one)
    int w = 0;
    for (int i = 0; i < j->nout; i++) {
        bool dup = false;
        for (int k = 0; k < w && !dup; k++)
            dup = !j->tab_mode && j->out[i].kind != PI_CMD && j->out[k].kind != PI_CMD && !strcmp(j->out[i].text, j->out[k].text);
        if (dup) pal_item_free(&j->out[i]);
        else j->out[w++] = j->out[i];
    }
    j->nout = w;
    main_post(pal_done, j);
}

static void pal_clear_items(void) {
    for (int i = 0; i < A.pal_n; i++) pal_item_free(&g_pal_items[i]);
    A.pal_n = 0;
}

static void pal_done(void *arg) {
    PalJob *j = arg;
    if (A.pal_open && j->gen == A.pal_gen) {
        pal_clear_items();
        memcpy(g_pal_items, j->out, (size_t)j->nout * sizeof(PalItem));
        A.pal_n = j->nout;
        A.pal_sel = 0;
        A.pal_top = 0;
        A.pal_pending = false;
        A.redraw = true;
    } else {
        for (int i = 0; i < j->nout; i++) pal_item_free(&j->out[i]);
    }
    if (j->L) listing_release(j->L);
    strv_free(j->recent, j->nrecent);
    strv_free(j->places, j->nplaces);
    strv_free(j->closed, j->nclosed);
    free(j->tab_panel);
    strv_free(j->place_labels, j->nplaces);
    strv_free(j->tabs, j->ntabs);
    free(j);
}

static void pal_query(void) {
    snprintf(A.pal_last, sizeof A.pal_last, "%s", A.pal_te.buf);
    PalJob *j = xcalloc(1, sizeof *j);
    j->gen = ++A.pal_gen;
    const char *q = A.pal_te.buf;
    j->cmd_mode = q[0] == '>';
    j->tab_mode = q[0] == '@';
    snprintf(j->q, sizeof j->q, "%s", j->cmd_mode || j->tab_mode ? q + 1 : q);
    j->closed = strv_dup(A.closed, A.nclosed);
    j->nclosed = A.nclosed;
    j->tab_panel = xmalloc((size_t)MAX(A.ntabs, 1) * sizeof(int));
    for (int i = 0; i < A.ntabs; i++) j->tab_panel[i] = A.npanels > 1 ? panel_index(A.tabs[i]->panel) + 1 : 0;
    j->cur_tab = A.cur;
    Pane *p = active_pane();
    snprintf(j->cur, sizeof j->cur, "%s", p->path);
    if (p->L && !p->L->is_search && atomic_load(&p->L->state) >= LST_READY && atomic_load(&p->L->state) != LST_ERROR) {
        j->L = p->L;
        listing_retain(p->L);
    }
    j->nlayouts = g_nlayouts;
    for (int i = 0; i < g_nlayouts; i++) snprintf(j->layouts[i], sizeof j->layouts[i], "%s", g_layouts[i].name);
    for (size_t i = 0; i < ARRAY_COUNT(g_cmds) && i < 128; i++) {
        const char *k = keymap_label(g_cmds[i].id);
        snprintf(j->cmd_sc[i], sizeof j->cmd_sc[i], "%s", k ? k : g_cmds[i].shortcut);
    }
    j->recent = strv_dup(g_recent, g_nrecent);
    j->nrecent = g_nrecent;
    j->nplaces = A.nplaces;
    j->places = xmalloc((size_t)MAX(A.nplaces, 1) * sizeof(char *));
    j->place_labels = xmalloc((size_t)MAX(A.nplaces, 1) * sizeof(char *));
    j->nplaces = 0;
    for (int i = 0; i < A.nplaces; i++) {
        if (!A.places[i].path[0]) continue;
        j->places[j->nplaces] = strdup(A.places[i].path);
        j->place_labels[j->nplaces++] = strdup(A.places[i].label);
    }
    j->ntabs = A.ntabs;
    j->tabs = xmalloc((size_t)MAX(A.ntabs, 1) * sizeof(char *));
    for (int i = 0; i < A.ntabs; i++) j->tabs[i] = strdup(A.tabs[i]->pane[A.tabs[i]->active].path);
    A.pal_pending = true;
    pool_submit(pal_job, j);
}

static void pal_open(bool commands);
static void pal_open_tabs(void);
static bool is_dir_path(const char *p);
static void pal_open_tabs(void) {
    pal_open(false);
    te_set(&A.pal_te, "@", 1, 1);
    pal_query();
}

static void pal_open(bool commands) {
    A.pal_open = true;
    A.menu.open = false;
    A.edit_pane = NULL;
    A.ren_pane = NULL;
    te_set(&A.pal_te, commands ? ">" : "", commands ? 1 : 0, commands ? 1 : 0);
    pal_clear_items();
    pal_query();
    DirIndex *d = index_acquire();
    if (!d || now_ns() - d->t_built > 600000000000ull) index_rebuild_default(); // refresh every 10 min
    index_release(d);
    A.redraw = true;
}

static void pal_close(void) {
    A.pal_open = false;
    pal_clear_items();
    A.redraw = true;
}

static void pal_changed(void) {
    if (strcmp(A.pal_last, A.pal_te.buf)) pal_query();
}

static void pal_run(int i, bool new_tab) {
    if (i < 0 || i >= A.pal_n) return;
    PalItem it = g_pal_items[i];
    it.text = strdup(it.text);
    it.label = it.label ? strdup(it.label) : NULL;
    pal_close();
    Pane *p = active_pane();
    switch (it.kind) {
    case PI_CMD: run_command(it.cmd); break;
    case PI_TAB: tab_select(it.tab); break;
    case PI_CLOSED: // reopen it (and forget it from the closed list)
        if (it.tab >= 0 && it.tab < A.nclosed && !strcmp(A.closed[it.tab], it.text)) {
            free(A.closed[it.tab]);
            memmove(&A.closed[it.tab], &A.closed[it.tab + 1], (size_t)(A.nclosed - it.tab - 1) * sizeof(char *));
            A.nclosed--;
        }
        if (is_dir_path(it.text)) tab_new(it.text, A.cur + 1, true);
        else toast("That folder no longer exists");
        break;
    case PI_ENTRY: {
        struct stat st;
        bool dir = stat(it.text, &st) == 0 && S_ISDIR(st.st_mode);
        char par[PATH_MAX];
        parent_of(it.text, par, sizeof par);
        if (dir) {
            if (new_tab || (A.ap && A.ap->cur && A.ap->cur->term)) tab_new(it.text, A.cur + 1, true);
            else pane_navigate(p, it.text, true);
        } else if (new_tab) {
            open_path(it.text);
        } else if (!strcmp(par, p->path) && p->order) {
            const char *nm = path_basename(it.text);
            for (u32 k = 0; k < p->count; k++)
                if (!strcmp(p->L->entries[p->order[k]].name, nm)) { move_cursor(p, k, false, false); break; }
        }
        break;
    }
    case PI_FILE: { // Enter: show it in its folder; Ctrl+Enter: open it
        char par[PATH_MAX];
        parent_of(it.text, par, sizeof par);
        if (new_tab) open_path(it.text);
        else if (!strcmp(par, p->path) && p->order) {
            const char *nm = path_basename(it.text);
            for (u32 k = 0; k < p->count; k++)
                if (!strcmp(p->L->entries[p->order[k]].name, nm)) { move_cursor(p, k, false, false); break; }
        } else {
            if (A.ap && A.ap->cur && A.ap->cur->term) { tab_new(par, A.cur + 1, true); p = active_pane(); }
            else pane_navigate(p, par, true);
            snprintf(p->select_name, sizeof p->select_name, "%s", path_basename(it.text));
        }
        break;
    }
    default:
        if (new_tab) tab_new(it.text, A.cur + 1, true);
        else pane_navigate(p, it.text, true);
        break;
    }
    pal_item_free(&it);
}

static void pal_move(int d) {
    if (!A.pal_n) return;
    A.pal_sel = CLAMP(A.pal_sel + d, 0, A.pal_n - 1);
    A.redraw = true;
}

static void pal_key(SDL_Keycode k, SDL_Keymod mod) {
    switch (k) {
    case SDLK_ESCAPE: pal_close(); return;
    case SDLK_RETURN: case SDLK_KP_ENTER: pal_run(A.pal_sel, mod & SDL_KMOD_CTRL); return;
    case SDLK_DOWN: pal_move(1); return;
    case SDLK_UP: pal_move(-1); return;
    case SDLK_PAGEDOWN: pal_move(8); return;
    case SDLK_PAGEUP: pal_move(-8); return;
    case SDLK_TAB: pal_move(mod & SDL_KMOD_SHIFT ? -1 : 1); return;
    default: break;
    }
    te_key(&A.pal_te, k, mod);
    pal_changed();
    A.redraw = true;
}

// --- recursive search UI ---------------------------------------------------

static void search_focus(Pane *p) {
    set_active_pane(p);
    A.search_pane = p;
    A.edit_pane = NULL;
    A.ren_pane = NULL;
    te_set(&A.search_te, p->search_q, 0, (int)strlen(p->search_q));
    A.redraw = true;
}

static bool q_is_glob(const char *q) { return strpbrk(q, "*?[") != NULL; }

// small is a (case-insensitive) subsequence of big
static bool subseq_ci(const char *small, const char *big) {
    for (; *small && *big; big++)
        if (ascii_lower((u8)*small) == ascii_lower((u8)*big)) small++;
    return !*small;
}

// In subfolder mode, a query that only adds letters can narrow the results in place.
static bool search_refinable(Pane *p, const char *q) {
    return p->L && p->L->is_search && !p->L->is_content && !A.search_contents && !p->L_next && p->search_q[0] &&
           p->L->search_recursive && A.search_recursive &&
           !q_is_glob(q) && !q_is_glob(p->search_q) && subseq_ci(p->search_q, q);
}

// Folder filter (subfolders off): narrow the current folder in memory, instantly.
static void search_set_filter(Pane *p, const char *q) {
    char qq[256];
    snprintf(qq, sizeof qq, "%s", q);
    bool staged_search = p->L_next && p->L_next->is_search;
    if ((p->L && p->L->is_search) || staged_search) { // coming from subfolder results: rescan the folder
        snprintf(p->search_q, sizeof p->search_q, "%s", qq);
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s", p->path);
        pane_load_ex(p, path, true);
        return;
    }
    if (!strcmp(p->search_q, qq)) return;
    snprintf(p->search_q, sizeof p->search_q, "%s", qq);
    if (p->order) pane_resort(p);
    A.redraw = true;
}

// Called on every edit of the search box.
static void search_typed(Pane *p) {
    const char *q = A.search_te.buf;
    while (*q == ' ') q++;
    if (A.search_contents) {
        A.search_due = now_ns() + 300000000ull; // contents: search once typing pauses
    } else if (!A.search_recursive) {
        search_set_filter(p, q);
        A.search_due = 0;
    } else if (*q && search_refinable(p, q)) {
        search_refine(p, q);
        A.search_due = 0;
    } else {
        A.search_due = now_ns() + 300000000ull; // rescan once typing pauses
    }
    A.redraw = true;
}

// Contents mode: search inside files (two characters at least unless forced, and a valid regex).
static void search_apply_contents(Pane *p, const char *q, bool force) {
    bool showing = (p->L && p->L->is_search) || (p->L_next && p->L_next->is_search);
    A.cs_error[0] = 0;
    if (!*q) {
        if (showing) {
            p->search_q[0] = 0;
            char path[PATH_MAX];
            snprintf(path, sizeof path, "%s", p->path);
            pane_load(p, path);
        }
        return;
    }
    if (p->L && p->L->is_content && !p->L_next && !strcmp(p->search_q, q) && !A.cs_dirty) return;
    A.cs_dirty = false;
    char text[256], glob[128];
    content_split_query(q, text, sizeof text, glob, sizeof glob);
    if (strlen(text) < 2 && !force) return; // one letter matches nearly every line
    if (!content_query_ok(q)) { snprintf(A.cs_error, sizeof A.cs_error, "%s", text[0] ? "Invalid regular expression" : "Type what to look for"); A.redraw = true; return; }
    pane_load_content(p, q);
}

// force: run even for a 1-character query (Enter)
static void search_apply(Pane *p, bool force) {
    A.search_due = 0;
    if (!pane_exists(p)) return;
    const char *q = A.search_te.buf;
    while (*q == ' ') q++;
    if (A.search_contents) { search_apply_contents(p, q, force); return; }
    if (!A.search_recursive) { search_set_filter(p, q); return; }
    if (!*q) {
        if ((p->L && p->L->is_search) || (p->L_next && p->L_next->is_search)) {
            p->search_q[0] = 0;
            char path[PATH_MAX];
            snprintf(path, sizeof path, "%s", p->path);
            pane_load(p, path);
        }
        return;
    }
    if (p->L && p->L->is_search && !p->L_next && !strcmp(p->search_q, q) && p->L->search_recursive) return;
    if (search_refinable(p, q)) { search_refine(p, q); return; }
    if (strlen(q) < 2 && !force) return; // one letter matches nearly everything
    pane_load_search(p, q);
}

static void search_toggle_recursive(Pane *p) {
    A.search_recursive = !A.search_recursive;
    A.settings_dirty = true;
    const char *q = A.search_pane == p ? A.search_te.buf : p->search_q;
    while (*q == ' ') q++;
    char qq[256];
    snprintf(qq, sizeof qq, "%s", q);
    A.search_due = 0;
    if (A.search_contents) {
        A.cs_dirty = true;
        if (qq[0]) search_apply_contents(p, qq, true);
    } else if (A.search_recursive) {
        if (qq[0]) pane_load_search(p, qq);
    } else {
        search_set_filter(p, qq);
    }
    A.redraw = true;
}

// Contents switch (Alt+T) and its options (Alt+C match case, Alt+W whole word, Alt+X regex): search again.
static void search_set_contents(Pane *p, bool on) {
    const char *q = A.search_pane == p ? A.search_te.buf : p->search_q;
    while (*q == ' ') q++;
    char qq[256];
    snprintf(qq, sizeof qq, "%s", q);
    A.search_contents = on;
    A.settings_dirty = true;
    A.search_due = 0;
    A.cs_error[0] = 0;
    A.cs_dirty = true;
    if (on) {
        if (qq[0]) search_apply_contents(p, qq, true);
    } else {
        if (p->cs_view) { p->view = p->cs_view - 1; p->cs_view = 0; }
        if (A.search_recursive && qq[0]) pane_load_search(p, qq);
        else search_set_filter(p, qq);
    }
    A.redraw = true;
}

static void search_cs_option(Pane *p, int which) { // 0 case, 1 word, 2 regex
    bool *o = which == 0 ? &A.cs_case : which == 1 ? &A.cs_word : &A.cs_regex;
    *o = !*o;
    A.settings_dirty = true;
    if (!A.search_contents) return;
    const char *q = A.search_pane == p ? A.search_te.buf : p->search_q;
    while (*q == ' ') q++;
    char qq[256];
    snprintf(qq, sizeof qq, "%s", q);
    A.cs_dirty = true;
    if (qq[0]) search_apply_contents(p, qq, true);
    A.redraw = true;
}

// Ctrl+Shift+F: the search bar, searching inside files.
static void search_focus_contents(Pane *p) {
    if (!A.search_contents) search_set_contents(p, true);
    search_focus(p);
}

// Bottom bar layout: [ magnifier  query ......  count ][ Aa ab .* ][ doc-icon Contents (switch) ][ tree-icon
// Subfolders (switch) ][ x ]  (the three option buttons only while Contents is on)
static void search_bar_layout(Pane *p, Rect *field, Rect *toggle, Rect *close, Rect *cont, Rect opt[3]) {
    Rect sb = p->ly.sbar;
    f32 pad = roundf(S(8)), h = sb.h - roundf(S(14)), cs = roundf(S(26));
    *close = (Rect){sb.x + sb.w - cs - pad, sb.y + roundf((sb.h - cs) * 0.5f), cs, cs};
    f32 tw = roundf(S(18) + text_width(&A.font_small, "Subfolders", 10) + S(10) + S(30) + S(14));
    *toggle = (Rect){close->x - S(6) - tw, sb.y + roundf(S(7)), tw, h};
    f32 cw = roundf(S(18) + text_width(&A.font_small, "Contents", 8) + S(10) + S(30) + S(14));
    *cont = (Rect){toggle->x - S(4) - cw, toggle->y, cw, h};
    f32 right = cont->x - S(8), ow = roundf(S(28));
    for (int i = 2; i >= 0; i--) {
        if (A.search_contents) { right -= ow; opt[i] = (Rect){right, toggle->y, ow - S(2), h}; }
        else opt[i] = (Rect){0, 0, 0, 0};
    }
    if (A.search_contents) right -= S(6);
    *field = (Rect){sb.x + pad, sb.y + roundf(S(7)), right - (sb.x + pad), h};
}

static void search_bar_rects(Pane *p, Rect *field, Rect *toggle, Rect *close) {
    Rect cont, opt[3];
    search_bar_layout(p, field, toggle, close, &cont, opt);
}

static void search_exit(Pane *p) {
    A.search_pane = NULL;
    A.search_due = 0;
    A.cs_error[0] = 0;
    if (p->cs_view) { p->view = p->cs_view - 1; p->cs_view = 0; }
    bool from_search = (p->L && p->L->is_search) || (p->L_next && p->L_next->is_search);
    p->search_q[0] = 0;
    if (from_search) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s", p->path);
        pane_load(p, path);
    } else if (p->order) {
        pane_resort(p); // drop the filter
    }
    A.redraw = true;
}


#include "settings.c"

// --- zoom / pins -----------------------------------------------------------

static void reload_fonts(void);
static void set_zoom_level(Pane *p, int lvl);
static void zoom_step(Pane *p, int dir);

static void set_zoom(f32 z) {
    z = CLAMP(roundf(z * 10.f) / 10.f, 0.6f, 2.5f);
    if (z == A.zoom) return;
    A.zoom = z;
    A.scale = A.dpi_scale * A.zoom;
    reload_fonts();
    toast("Zoom %d%%", (int)roundf(z * 100));
    A.settings_dirty = true;
}

static int zoom_level_of(const Pane *p) {
    if (p->view != VIEW_GRID) return p->view == VIEW_LIST ? 1 : 0;
    int best = 2;
    for (int i = 2; i < ZOOM_COUNT; i++)
        if (abs(g_zoom[i].px - p->icon_px) < abs(g_zoom[best].px - p->icon_px)) best = i;
    return best;
}

static void set_zoom_level(Pane *p, int lvl) {
    lvl = CLAMP(lvl, 0, ZOOM_COUNT - 1);
    p->du = p->snapv = false; // picking a view leaves the disk usage map / the snapshots
    p->view = g_zoom[lvl].view;
    if (g_zoom[lvl].view == VIEW_GRID) p->icon_px = g_zoom[lvl].px;
    folder_view_remember(p);
    layout();
    pane_resort(p); // grouping only applies to the details view
    p->scroll = p->scroll_target = CLAMP(p->scroll, 0.0, max_scroll(p));
    if (p->cursor >= 0) {
        ensure_visible(p, p->cursor);
        p->scroll = p->scroll_target;
    }
    A.redraw = true;
}

static void zoom_step(Pane *p, int dir) { set_zoom_level(p, zoom_level_of(p) + dir); }

static int pin_index(const char *path) { return qa_find(path, NULL, NULL) ? 0 : -1; }

static void pin_toggle(const char *path) {
    int g, i;
    if (qa_find(path, &g, &i)) {
        qa_remove(g, i);
        toast("Removed \xE2\x80\x9C%s\xE2\x80\x9D from Quick Access", path[1] ? path_basename(path) : "/");
    } else {
        if (!A.nqa) qa_add_group("Quick access");
        int at = A.qa[0].n; // before Trash when it's last
        char tf[PATH_MAX], ti[PATH_MAX];
        trash_dirs(tf, ti, sizeof tf);
        if (at && !strcmp(A.qa[0].items[at - 1].path, tf)) at--;
        qa_insert(0, at, "", path);
        A.qa[0].collapsed = false;
        toast("Pinned \xE2\x80\x9C%s\xE2\x80\x9D to Quick Access", path[1] ? path_basename(path) : "/");
    }
    init_places();
    A.settings_dirty = true;
}


// Folder an action like "Pin" applies to: the folder under the cursor, else the current one.
static void pane_target_folder(Pane *p, char *out, size_t cap) {
    snprintf(out, cap, "%s", p->path);
    if (p->order && p->cursor >= 0 && p->cursor < (i64)p->count) {
        Entry *e = &p->L->entries[p->order[p->cursor]];
        if (e->flags & ENT_DIR) path_join(out, cap, p->path, e->name);
    }
}

// --- preview panel (main thread side) --------------------------------------

static void preview_toggle(void) {
    A.preview_on = !A.preview_on;
    A.pv_want[0] = 0;
    A.pv_path[0] = 0;
    A.settings_dirty = true;
    A.redraw = true;
}

static void preview_done_cb(void *arg) {
    PreviewResult *r = arg;
    if (r->gen != A.pv_gen) { preview_result_free(r); return; }
    preview_result_free(A.pv);
    if (A.pv_tex) { SDL_DestroyTexture(A.pv_tex); A.pv_tex = NULL; }
    if (r->kind == PV_IMAGE && r->rgba) {
        A.pv_tex = SDL_CreateTexture(A.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, r->w, r->h);
        if (A.pv_tex) {
            SDL_UpdateTexture(A.pv_tex, NULL, r->rgba, r->w * 4);
            SDL_SetTextureBlendMode(A.pv_tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(A.pv_tex, SDL_SCALEMODE_LINEAR);
        }
        stbi_image_free(r->rgba);
        r->rgba = NULL;
    }
    A.pv = r;
    A.redraw = true;
}

// Track the active pane's cursor; request a preview once it rests for a moment.
static void preview_update(void) {
    if (!A.preview_on || !A.ntabs) return;
    Pane *p = active_pane();
    if (pane_is_archive(p)) return; // members aren't real files
    char path[PATH_MAX];
    i64 mtime = 0;
    int line = 0; // contents search: the matching line (a file's first one)
    if (pane_du_on(p) && du_pick_path(p, path, sizeof path)) { // disk usage: the picked item, at any depth
    } else if (p->order && p->cursor >= 0 && p->cursor < (i64)p->count) {
        u32 ei = p->order[p->cursor];
        Entry *e = &p->L->entries[ei];
        path_join(path, sizeof path, p->path, e->name);
        if (atomic_load_explicit(&e->meta, memory_order_acquire) == META_READY) mtime = e->mtime;
        if (p->L->is_content) {
            if (e->flags & ENT_MATCH) line = (int)e->size;
            else if ((p->L->cinfo[ei] & ~CI_COLLAPSED) && ei + 1 < p->L->count) line = (int)p->L->entries[ei + 1].size;
        }
    } else {
        snprintf(path, sizeof path, "%s", p->path);
    }
    u64 now = now_ns();
    if (strcmp(path, A.pv_want) || mtime != A.pv_mtime || line != A.pv_line) {
        bool first = !A.pv_want[0];
        snprintf(A.pv_want, sizeof A.pv_want, "%s", path);
        A.pv_mtime = mtime;
        A.pv_line = line;
        A.pv_due = now + (first ? 0 : 90000000ull); // don't decode every row while holding an arrow key
        A.pv_path[0] = 0;
    }
    if (A.pv_due && now >= A.pv_due) {
        A.pv_due = 0;
        snprintf(A.pv_path, sizeof A.pv_path, "%s", A.pv_want);
        int max_px = (int)CLAMP(A.r_preview.w * 1.25f, 256.f, 2048.f);
        preview_request(++A.pv_gen, A.pv_path, max_px, A.pv_line);
    }
}

#include "input.c"

#include "render.c"

// --- start at login (XDG autostart entry) and the tray icon --------------------------------------------------

static bool g_bg_start; // --background: started at login, window hidden

static void autostart_file(char *out, size_t cap) {
    const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
    if (x && x[0] == '/') snprintf(out, cap, "%s/autostart/file-explorer.desktop", x);
    else snprintf(out, cap, "%s/.config/autostart/file-explorer.desktop", h ? h : "/tmp");
}

static bool autostart_enabled(void) {
    char p[PATH_MAX];
    autostart_file(p, sizeof p);
    return access(p, F_OK) == 0;
}

static void autostart_set(bool on) {
    char p[PATH_MAX], dir[PATH_MAX], exe[PATH_MAX];
    autostart_file(p, sizeof p);
    if (!on) { unlink(p); return; }
    parent_of(p, dir, sizeof dir);
    mkdir_p(dir, 0755);
    exe_path(exe, sizeof exe);
    FILE *f = fopen(p, "w");
    if (!f) { toast("Couldn't write %s", p); return; }
    fprintf(f, "[Desktop Entry]\nType=Application\nName=" APP_NAME "\nComment=File manager (starts in the background)\n"
               "Exec=\"%s\" --background\nIcon=dev.fileexplorer\nTerminal=false\nNoDisplay=true\nX-GNOME-Autostart-enabled=true\n", exe);
    fclose(f);
}

static SDL_Tray *g_tray;
static char g_tray_sig[4096];
static int g_tray_host = -1; // a StatusNotifier host (tray) on the session bus: -1 unknown

static bool tray_host_available(void) {
    if (g_tray_host < 0) {
        // asked with whichever D-Bus tool is there: busctl (systemd), gdbus (GLib), dbus-send (dbus)
        char out[128];
        char *busctl[] = {"busctl", "--user", "call", "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                          "NameHasOwner", "s", "org.kde.StatusNotifierWatcher", NULL};
        char *gdbus[] = {"gdbus", "call", "--session", "--dest", "org.freedesktop.DBus", "--object-path", "/org/freedesktop/DBus",
                         "--method", "org.freedesktop.DBus.NameHasOwner", "org.kde.StatusNotifierWatcher", NULL};
        char *dsend[] = {"dbus-send", "--session", "--print-reply", "--dest=org.freedesktop.DBus", "/org/freedesktop/DBus",
                         "org.freedesktop.DBus.NameHasOwner", "string:org.kde.StatusNotifierWatcher", NULL};
        char **tries[] = {busctl, gdbus, dsend};
        g_tray_host = 0;
        for (size_t i = 0; i < ARRAY_COUNT(tries) && !A.selftest && !A.screenshot; i++) {
            if (!in_path(tries[i][0])) continue;
            g_tray_host = run_capture(tries[i], out, sizeof out) == 0 && strstr(out, "true");
            break;
        }
    }
    return g_tray_host > 0;
}

static void window_show(void) {
    SDL_ShowWindow(A.win);
    if (SDL_GetWindowFlags(A.win) & SDL_WINDOW_MINIMIZED) SDL_RestoreWindow(A.win);
    SDL_RaiseWindow(A.win);
    A.win_hidden = false;
    A.redraw = true;
}

// Tray callbacks run the action on the main loop (main_post is safe from any thread).
static void tray_do_open(void *ud) { (void)ud; window_show(); }
static void tray_do_quit(void *ud) { (void)ud; A.quit = true; }
static void SDLCALL tray_cb_open(void *ud, SDL_TrayEntry *e) { (void)e; main_post(tray_do_open, ud); }
static void SDLCALL tray_cb_quit(void *ud, SDL_TrayEntry *e) { (void)e; main_post(tray_do_quit, ud); }

// Menu: Open, add-on entries, Quit. Rebuilt when those change.
static void tray_sync(void) {
    if (!g_tray) return;
    char sig[sizeof g_tray_sig] = "";
    addon_tray_sig(sig, sizeof sig);
    const char *want = sig[0] ? sig : "-"; // never empty: "" in g_tray_sig means "not built yet"
    if (!strcmp(want, g_tray_sig)) return;
    snprintf(g_tray_sig, sizeof g_tray_sig, "%s", want);
    SDL_TrayMenu *m = SDL_GetTrayMenu(g_tray);
    if (!m) m = SDL_CreateTrayMenu(g_tray);
    if (!m) return;
    int n = 0;
    const SDL_TrayEntry **es = SDL_GetTrayEntries(m, &n);
    for (int i = n - 1; es && i >= 0; i--) SDL_RemoveTrayEntry((SDL_TrayEntry *)es[i]);
    int pos = 0;
    SDL_TrayEntry *e = SDL_InsertTrayEntryAt(m, pos++, "Open " APP_NAME, SDL_TRAYENTRY_BUTTON);
    if (e) SDL_SetTrayEntryCallback(e, tray_cb_open, NULL);
    addon_tray_entries(m, &pos);
    SDL_InsertTrayEntryAt(m, pos++, NULL, 0);
    e = SDL_InsertTrayEntryAt(m, pos++, "Quit " APP_NAME, SDL_TRAYENTRY_BUTTON);
    if (e) SDL_SetTrayEntryCallback(e, tray_cb_quit, NULL);
}

// The tray icon exists while "keep running in the tray" is on (or the app was started in the background).
static void tray_update(void) {
    bool want = (A.close_to_tray || g_bg_start) && tray_host_available();
    if (want && !g_tray) {
        u8 *px = icon_raster(ICON_LOGO_SMALL, 64, 64);
        SDL_Surface *ic = px ? SDL_CreateSurfaceFrom(64, 64, SDL_PIXELFORMAT_RGBA32, px, 64 * 4) : NULL;
        g_tray = SDL_CreateTray(ic, APP_NAME);
        if (ic) SDL_DestroySurface(ic);
        free(px);
        g_tray_sig[0] = 0;
        tray_sync();
    } else if (!want && g_tray) {
        SDL_DestroyTray(g_tray);
        g_tray = NULL;
    }
}

// The window's close button: hide to the tray when that's on (and there is a tray), else quit.
static void window_close_requested(void) {
    if (picker_active()) { picker_cancel(); return; }
    if (A.close_to_tray && g_tray) {
        settings_save();
        SDL_HideWindow(A.win);
        A.win_hidden = true;
        return;
    }
    A.quit = true;
}

#include "term.c"
#include "integration.c"
#include "ui_tools.c"
#include "picker.c"

static void draw_overlay(void) {
    char buf[128];
    snprintf(buf, sizeof buf, "frame %.2f ms  scale %.2f  %dx%d  tabs %d", A.frame_ms, A.scale, A.W, A.H, A.ntabs);
    Font *f = &A.font_small;
    int len = (int)strlen(buf);
    f32 w = text_width(f, buf, len) + S(16), h = S(24);
    f32 x = A.W - w - S(10), y = A.r_content.y + S(90);
    draw_rrect(x, y, w, h, S(6), CA(0x000000, 0.75f));
    draw_text(f, x + S(8), text_y(f, y, h), buf, len, C(0x8FD45A));
}

static bool band_autoscrolling(void) {
    if (A.drag != DRAG_BAND || !A.drag_pane) return false;
    Rect lr = A.drag_pane->ly.list;
    return A.my < lr.y || A.my > lr.y + lr.h;
}

static bool menu_autoscrolling(void) {
    return A.menu.open && A.mouse_in && in_rect(A.menu.r, A.mx, A.my) && menu_arrow_at(A.my) != 0;
}

static bool animating(void) {
    if (band_autoscrolling() || menu_autoscrolling()) return true;
    if (fabsf(A.side_scroll_target - A.side_scroll) > 0.5f) return true;
    for (int i = 0; i < A.npanels; i++) {
        Pane *p = A.panels[i]->cur ? &A.panels[i]->cur->pane[0] : NULL;
        if (p && fabs(p->scroll_target - p->scroll) > 0.5) return true;
    }
    return false;
}

static void frame_draw(void) {
    draw_clip(NULL);
    g_alpha = 1.f;
    SDL_SetRenderDrawColor(A.ren, 0x1B, 0x1B, 0x20, 255);
    SDL_RenderClear(A.ren);
    A.ptr_hand = A.ptr_text = false;
    // While a menu, the palette, a sheet or a dialog is open, nothing underneath reacts to the mouse.
    bool mouse_in = A.mouse_in;
    if (A.menu.open || A.pal_open || sheet_open() || A.dlg.open) A.mouse_in = false;
    bool multi = A.npanels > 1;
    for (int i = 0; i < A.npanels; i++) {
        Panel *pn = A.panels[i];
        if (!pn->cur) continue;
        Pane *p = &pn->cur->pane[0];
        if (pn->cur->term) term_draw(pn->cur->term, p->ly.area, pn == A.ap);
        else draw_pane(p, pn == A.ap, multi);
        draw_panel_bar(pn);
        if (multi && pn != A.ap && A.dim_inactive) {
            Rect a = p->ly.area;
            draw_rect(a.x, a.y, a.w, a.h, CA(0x000000, 0.10f)); // dim inactive panels
        }
    }
    draw_node_dividers(A.root);
    draw_preview();
    draw_sidebar();
    draw_brand();
    draw_window_controls();
    draw_status();
    draw_ops();
    draw_queue();
    draw_toast();
    draw_drag_badge();
    draw_tab_drop();
    A.mouse_in = mouse_in; // overlays get the mouse
    draw_menu();
    draw_palette();
    A.mouse_in = mouse_in && !A.dlg.open;
    sheet_draw();
    A.mouse_in = mouse_in;
    draw_dialog();
    draw_preedit();
    if (A.show_overlay) draw_overlay();
    draw_flush();
    cursor_apply();
}

static void window_icon_update(void) { // the logo in the accent colour (X11 / taskbars without the .desktop icon)
    u8 *logo = icon_raster(ICON_LOGO, 128, 128);
    if (!logo) return;
    SDL_Surface *ic = SDL_CreateSurfaceFrom(128, 128, SDL_PIXELFORMAT_RGBA32, logo, 128 * 4);
    if (ic) { SDL_SetWindowIcon(A.win, ic); SDL_DestroySurface(ic); }
    free(logo);
}

// The system icon (launchers, docks, the window list - anything that looks up Icon=dev.fileexplorer in the icon
// theme) in the accent colour too: a recoloured copy in the user's icon folder, which wins over the packaged one.
// Blue is the artwork as packaged, so then the copy goes away. dh: the data folder ($XDG_DATA_HOME).
static bool app_icon_write(const char *dh, u32 accent) {
    char dir[PATH_MAX], path[PATH_MAX + 32];
    snprintf(dir, sizeof dir, "%s/icons/hicolor", dh);
    snprintf(path, sizeof path, "%s/scalable/apps/dev.fileexplorer.svg", dir);
    bool changed;
    if ((accent & 0xFFFFFF) == LOGO_BASE_ACCENT) changed = unlink(path) == 0;
    else {
        static const char from[] = "width=\"624\" height=\"456\" viewBox=\"0 0 624 456\"";
        const char *at = strstr(g_logo_svg, from);
        if (!at) return false;
        char *svg = xmalloc(sizeof g_logo_svg + 64);
        snprintf(svg, sizeof g_logo_svg + 64, "%.*swidth=\"256\" height=\"256\" viewBox=\"0 -84 624 624\"%s\n",
                 (int)(at - g_logo_svg), g_logo_svg, at + strlen(from)); // square, like packaging/dev.fileexplorer.svg
        logo_recolor(svg, accent);
        changed = !file_equals(path, svg);
        if (changed) {
            char apps[PATH_MAX + 32];
            snprintf(apps, sizeof apps, "%s/scalable/apps", dir);
            mkdir_p(apps, 0755);
            changed = write_file_atomic(path, svg);
        }
        free(svg);
    }
    if (changed) utimensat(AT_FDCWD, dir, NULL, 0); // icon caches watch the theme folder's time
    return changed;
}

// Once the accent has settled (not on every step of trying colours in Settings), outside tests.
static void app_icon_sync(void) {
    static u32 done = 0xFFFFFFFF, seen;
    static u64 since;
    if (A.selftest || A.screenshot || COL_ACCENT == done) return;
    if (COL_ACCENT != seen) { seen = COL_ACCENT; since = now_ns(); return; }
    if (now_ns() - since < 1500000000ull) return;
    done = COL_ACCENT;
    char dh[PATH_MAX];
    data_home(dh, sizeof dh);
    app_icon_write(dh, done);
}

static void frame(void) {
    trash_size_poll();
    icons_sync_theme();
    if (g_logo_changed) { g_logo_changed = false; window_icon_update(); }
    app_icon_sync();
    check_conflicts();
    ops_gc();
    ops_schedule();
    preview_update();
    u64 t0 = now_ns();
    f32 dt = A.last_frame ? (f32)((t0 - A.last_frame) / 1e9) : 0.016f;
    dt = MIN(dt, 0.05f);
    A.last_frame = t0;

    if (band_autoscrolling()) {
        Pane *p = A.drag_pane;
        Rect lr = p->ly.list;
        f32 over = A.my < lr.y ? A.my - lr.y : A.my - (lr.y + lr.h);
        p->scroll += (f64)over * dt * 12.0;
        p->scroll = p->scroll_target = CLAMP(p->scroll, 0.0, max_scroll(p));
        layout();
        band_update(p);
    }
    if (menu_autoscrolling()) menu_scroll_by((f32)menu_arrow_at(A.my) * S(360) * (f32)dt); // hover the arrows
    for (int i = 0; i < A.npanels; i++) {
        if (!A.panels[i]->cur) continue;
        Pane *p = &A.panels[i]->cur->pane[0];
        if (fabs(p->scroll_target - p->scroll) > 0.5) {
            p->scroll += A.smooth_scroll ? (p->scroll_target - p->scroll) * (1.0 - exp(-dt * 22.0)) : p->scroll_target - p->scroll;
            if (fabs(p->scroll_target - p->scroll) <= 0.5) p->scroll = p->scroll_target;
        }
    }
    if (fabsf(A.side_scroll_target - A.side_scroll) > 0.5f) {
        A.side_scroll += A.smooth_scroll ? (A.side_scroll_target - A.side_scroll) * (1.f - expf(-dt * 22.f)) : A.side_scroll_target - A.side_scroll;
        if (fabsf(A.side_scroll_target - A.side_scroll) <= 0.5f) A.side_scroll = A.side_scroll_target;
    }
    layout();

    // If the glyph atlas fills up mid-frame, that frame would show missing text (a flicker):
    // clear it and draw again (growing the atlas if one frame alone doesn't fit) before presenting.
    ime_update_area();
    for (int attempt = 0; attempt < 3; attempt++) {
        frame_draw();
        if (!g_atlas_full) break;
        if (attempt == 0 || !atlas_grow()) atlas_reset();
    }
    A.frame_ms = (f32)((now_ns() - t0) / 1e6);

    if (A.screenshot) {
        bool ready = !g_atlas_full;
        for (int i = 0; i < A.npanels; i++) {
            if (!A.panels[i]->cur) continue;
            Pane *p = &A.panels[i]->cur->pane[0];
            int st = p->L ? atomic_load(&p->L->state) : LST_ERROR;
            if ((st != LST_COMPLETE && st != LST_ERROR) || p->sort_pending) ready = false;
        }
        for (int i = 0; i < g_nthumbs; i++)
            if (g_thumbs[i].state == 1) ready = false; // wait for visible thumbnails
        for (int i = 0; i < A.ntabs; i++)
            if (A.tabs[i]->term && !term_settled(A.tabs[i]->term)) ready = false; // a shell prompt takes a moment
        if (git_busy() || du_busy() || snapview_busy() || (g_cmp.on && !cmp_ready())) ready = false;
        if (ready && ++A.screenshot_frames >= 3) {
            SDL_Surface *s = SDL_RenderReadPixels(A.ren, NULL);
            if (s) {
                SDL_SaveBMP(s, A.screenshot);
                SDL_DestroySurface(s);
            }
            A.quit = true;
        }
    }
    if (A.shot_once[0] && !g_atlas_full) {
        SDL_Surface *s = SDL_RenderReadPixels(A.ren, NULL);
        if (s) {
            SDL_SaveBMP(s, A.shot_once);
            SDL_DestroySurface(s);
        }
        A.shot_once[0] = 0;
    }
    rec_frame();
    SDL_RenderPresent(A.ren);
    if (g_atlas_full) {
        atlas_reset();
        A.redraw = true;
    }
}

// --- benchmark (headless) --------------------------------------------------

// Is this a Vulkan renderer on Mesa's software rasterizer (lavapipe)? It draws on the CPU and presents with plain
// image copies on X11 (blinking, tearing). Asked without Vulkan headers: deviceType is the 5th u32 of
// VkPhysicalDeviceProperties, 4 = VK_PHYSICAL_DEVICE_TYPE_CPU.
static bool vulkan_on_cpu(SDL_Renderer *ren) {
    SDL_PropertiesID pr = SDL_GetRendererProperties(ren);
    void *inst = SDL_GetPointerProperty(pr, SDL_PROP_RENDERER_VULKAN_INSTANCE_POINTER, NULL);
    void *dev = SDL_GetPointerProperty(pr, SDL_PROP_RENDERER_VULKAN_PHYSICAL_DEVICE_POINTER, NULL);
    void *(*gipa)(void *, const char *) = (void *(*)(void *, const char *))SDL_Vulkan_GetVkGetInstanceProcAddr();
    if (!inst || !dev || !gipa) return false;
    void (*props)(void *, void *) = (void (*)(void *, void *))gipa(inst, "vkGetPhysicalDeviceProperties");
    if (!props) return false;
    static u32 buf[1024]; // the struct is ~830 bytes
    props(dev, buf);
    return buf[4] == 4;
}

static int run_bench(const char *path, const char *query) {
#ifdef __GLIBC__
    mallopt(M_ARENA_MAX, 2); // as in main()
#endif
    SDL_Init(SDL_INIT_EVENTS);
    g_wake_event = SDL_RegisterEvents(1);
    pool_init();
    char p[PATH_MAX];
    if (!realpath(path, p)) { perror(path); return 1; }
    Listing *L = listing_new(p, SORT_NAME, false, true);
    listing_start(L);
    while (atomic_load(&L->state) != LST_COMPLETE && atomic_load(&L->state) != LST_ERROR) {
        SDL_Event e;
        SDL_WaitEventTimeout(&e, 10);
    }
    if (atomic_load(&L->state) == LST_ERROR) { fprintf(stderr, "error: %s\n", strerror(L->err)); return 1; }
    char n[32];
    fmt_count(n, L->count);
    printf("%s: %s entries\n", p, n);
    printf("  names + type + name sort : %8.2f ms\n", (L->t_names - L->t_start) / 1e6);
    printf("  + all metadata (statx)   : %8.2f ms  (%d workers)\n", (L->t_meta - L->t_start) / 1e6, g_nworkers);
    int keys[] = {SORT_DATE, SORT_SIZE, SORT_TYPE};
    const char *names[] = {"date", "size", "type"};
    for (int i = 0; i < 3; i++) {
        u32 *o, c;
        u64 t = now_ns();
        listing_build_order(L, keys[i], true, true, true, NULL, GROUP_NONE, &o, &c);
        printf("  re-sort by %-4s          : %8.2f ms\n", names[i], (now_ns() - t) / 1e6);
        free(o);
    }
    // recursive folder sizes (single thread): the first pass reads every folder, the second finds them unchanged
    char *buf = xmalloc(64 * 1024);
    for (int pass = 0; pass < 2; pass++) {
        u64 t = now_ns(), total = 0;
        for (u32 i = 0; i < L->count; i++) {
            Entry *e = &L->entries[i];
            struct stat st;
            if (!(e->flags & ENT_DIR) || (e->flags & ENT_LINK) || fstatat(L->dirfd, e->name, &st, AT_SYMLINK_NOFOLLOW)) continue;
            total += du_tree(L->dirfd, e->name, st.st_dev, &L->cancel, buf);
        }
        char sz[32];
        fmt_size(sz, sizeof sz, total);
        printf("  folder sizes, %s     : %8.2f ms  (%s, %llu bytes)\n", pass ? "cached" : "cold  ", (now_ns() - t) / 1e6, sz, (unsigned long long)total);
    }
    free(buf);
    listing_release(L);
    if (query) { // recursive search, as typed in the search bar with Subfolders on
        struct rusage r0, r1;
        getrusage(RUSAGE_SELF, &r0);
        u64 t = now_ns();
        Listing *S = listing_new(p, SORT_NAME, false, true);
        S->is_search = true;
        search_start(S, p, query, true, true);
        while (!g_search_bench.done) {
            SDL_Event e;
            if (SDL_WaitEventTimeout(&e, 10)) main_drain();
        }
        u64 dt = now_ns() - t;
        getrusage(RUSAGE_SELF, &r1);
        f64 cpu = (r1.ru_utime.tv_sec - r0.ru_utime.tv_sec + r1.ru_stime.tv_sec - r0.ru_stime.tv_sec) * 1e3 +
                  (r1.ru_utime.tv_usec - r0.ru_utime.tv_usec + r1.ru_stime.tv_usec - r0.ru_stime.tv_usec) / 1e3;
        fmt_count(n, (u64)atomic_load(&S->search_dirs));
        printf("search \"%s\": %llu hits in %s folders, %llu batches\n", query, (unsigned long long)g_search_bench.hits, n,
               (unsigned long long)g_search_bench.batches);
        printf("  wall                     : %8.2f ms\n", dt / 1e6);
        printf("  cpu (all threads)        : %8.2f ms\n", cpu);
        listing_release(S);
    }
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    printf("peak RSS                   : %8.1f MB\n", ru.ru_maxrss / 1024.0);
    return 0;
}


#include "selftest.c"

#ifdef FE_ADDON
#include "addon.c" // compiled-in add-ons (addons.c)
#endif

// --- command line + single instance ------------------------------------------

// Command-line argument (path or file:// URI) -> absolute path. False if it doesn't exist.
static bool cli_abs(const char *in, char *out) {
    char tmp[PATH_MAX];
    if (!strncmp(in, "file://", 7)) {
        if (!uri_to_path(in, tmp, sizeof tmp)) return false;
        in = tmp;
    }
    return realpath(in, out) != NULL;
}

// Folder to show for an argument, plus the file to select when the argument is a file.
static bool cli_target(const char *in, char *dir, char *sel) {
    char abs[PATH_MAX];
    sel[0] = 0;
    if (!cli_abs(in, abs)) return false;
    struct stat st;
    if (stat(abs, &st) != 0) return false;
    if (S_ISDIR(st.st_mode)) { snprintf(dir, PATH_MAX, "%s", abs); return true; }
    parent_of(abs, dir, PATH_MAX);
    snprintf(sel, PATH_MAX, "%s", path_basename(abs));
    return true;
}

// Open an argument as a tab in the active panel (or a new split). Returns the tab.
static Tab *open_target(const char *in, bool split, bool activate) {
    char dir[PATH_MAX], sel[PATH_MAX];
    if (!cli_target(in, dir, sel)) { toast("Not found: %s", in); return NULL; }
    Tab *t = NULL;
    if (split) {
        Panel *pn = panel_split(A.ap, ZONE_RIGHT, NULL, dir);
        t = pn ? pn->cur : NULL;
    } else {
        t = tab_new(dir, A.ntabs, activate);
    }
    if (t && sel[0]) snprintf(t->pane[0].select_name, sizeof t->pane[0].select_name, "%s", sel);
    return t;
}

// Another launch handed us its arguments (see ipc.c).
// An update replaced our binary while we ran (the package manager unlinks the old file): /proc/self/exe then reads
// "<path> (deleted)". link: that text; on true it's cut back to the path of the new binary.
static bool exe_link_replaced(char *link) {
    static const char suf[] = " (deleted)";
    size_t n = strlen(link), sl = sizeof suf - 1;
    if (n <= sl || strcmp(link + n - sl, suf)) return false;
    link[n - sl] = 0;
    return true;
}

// A later launch handed us its folders but we're the old version: save, then become the new binary with those
// folders (tabs come back through the session, or as arguments without it). Not while it would lose something:
// transfers running, terminal sessions, the picker. Returns only if it didn't restart.
static void restart_if_updated(const char *msg) {
    if (A.selftest || picker_active() || A.nops > 0) return;
    for (int i = 0; i < A.ntabs; i++) if (A.tabs[i]->term) return;
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return;
    exe[n] = 0;
    if (!exe_link_replaced(exe) || access(exe, X_OK) != 0) return;
    char *argv[2 * MAX_TABS + 8];
    int argc = 0;
    argv[argc++] = exe;
    if (!A.restore_session) // the current tabs as arguments, the session won't bring them back
        for (int i = 0; i < A.ntabs && argc < MAX_TABS; i++) argv[argc++] = strdup(A.tabs[i]->pane[0].path);
    char *copy = strdup(msg);
    for (char *p = copy, *line; (line = strsep(&p, "\n"));) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        if (!strcmp(line, "open") && argc < 2 * MAX_TABS) argv[argc++] = tab + 1;
        else if (!strcmp(line, "split") && argc < 2 * MAX_TABS) { argv[argc++] = "--split"; argv[argc++] = tab + 1; }
        else if (!strcmp(line, "token")) setenv("XDG_ACTIVATION_TOKEN", tab + 1, 1);
    }
    argv[argc] = NULL;
    settings_save();
    ipc_shutdown();
    SDL_DestroyRenderer(A.ren);
    SDL_DestroyWindow(A.win);
    SDL_Quit();
    execv(exe, argv);
    _exit(127); // (the window is gone: nothing sensible to go back to)
}

static void ipc_handle(char *msg) {
    if (getenv("FE_IPC_DEBUG")) fprintf(stderr, "IPC[%s]\n", msg);
    restart_if_updated(msg);
    bool first = true;
    for (char *p = msg, *line; (line = strsep(&p, "\n"));) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        const char *arg = tab + 1;
        if (!strcmp(line, "open")) { if (open_target(arg, false, first)) first = false; }
        else if (!strcmp(line, "split")) open_target(arg, true, true);
        else if (!strcmp(line, "token")) setenv("XDG_ACTIVATION_TOKEN", arg, 1); // lets Wayland focus us
    }
    free(msg);
    if (A.selftest) { A.ipc_count++; A.redraw = true; return; }
    window_show(); // (also out of the tray)
}

static void usage(void) {
    printf("Usage: file-explorer [PATH|FILE|URI]... [--tab PATH]... [--split PATH] [--new-window] [--version]\n"
           "  Folders open as tabs, files open their folder with the file selected. If File Explorer is already\n"
           "  running they open in that window (Options > Behaviour), unless --new-window / -n is given.\n"
           "       file-explorer --bench PATH            time listing a directory (headless)\n"
           "       file-explorer --screenshot OUT.bmp [PATH...]\n");
}

static bool resolve_dir(const char *in, char *out) {
    struct stat st;
    return realpath(in, out) && stat(out, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool app_selftest(void) { return A.selftest; }

int main(int argc, char **argv) {
    if (argc == 2 && getenv("FE_ASKPASS_FILE")) return askpass_main(argv[1]); // ssh wants a password (devices.c)
    if (argc == 4 && !strcmp(argv[1], "--admin-op") && !strcmp(argv[2], "versions")) return snap_admin_main(argv[3]); // (snapshots.c)
    if (argc == 5 && !strcmp(argv[1], "--admin-op") && !strcmp(argv[2], "snapper-allow")) return snap_allow_main(argv[3], argv[4]);
    if (argc > 1 && !strcmp(argv[1], "--admin-op")) return admin_main(argc, argv); // as root through pkexec (ops.c)
    if (argc > 1 && !strcmp(argv[1], "--device-mount")) return devfs_main(argc, argv); // a phone as a folder (devfs.c)
    if (argc > 1 && !strcmp(argv[1], "--selftest-devices")) return run_selftest_devices(); // headless (selftest.c)
    int addon_rc = addon_main(argc, argv); // add-ons' helper modes
    if (addon_rc >= 0) return addon_rc;
    const char *start = NULL;
    const char *extra_tabs[MAX_TABS];
    int nextra = 0;
    const char *split_path = NULL;
    int select_tab = -1;
    const char *paths[MAX_TABS];
    int npaths = 0;
    bool force_new_window = false;
    const char *pick_mode = NULL;
    static char pick_start[PATH_MAX];
    bool selftest = false, selftest_ops = false, selftest_m4 = false, selftest_m5 = false, selftest_m6 = false, selftest_m7 = false, selftest_m8 = false, selftest_m9 = false, selftest_m10 = false, selftest_m11 = false, selftest_git = false, selftest_grep = false, selftest_du = false, selftest_cmp = false, selftest_admin = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bench") && i + 1 < argc) return run_bench(argv[i + 1], i + 2 < argc ? argv[i + 2] : NULL);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) A.screenshot = argv[++i];
        else if (!strcmp(argv[i], "--tab") && i + 1 < argc) { if (nextra < MAX_TABS - 1) extra_tabs[nextra++] = argv[++i]; }
        else if (!strcmp(argv[i], "--split") && i + 1 < argc) split_path = argv[++i];
        else if (!strcmp(argv[i], "--selftest")) selftest = true;
        else if (!strcmp(argv[i], "--selftest-ops")) { selftest_ops = true; st_setup_ops(); start = ST_BASE; }
        else if (!strcmp(argv[i], "--selftest-m4")) { selftest_m4 = true; st_setup_m4(); start = M4_BASE; }
        else if (!strcmp(argv[i], "--selftest-m5")) { selftest_m5 = true; st_setup_m5(); start = M5_BASE; }
        else if (!strcmp(argv[i], "--selftest-m6")) { selftest_m6 = true; st_setup_m6(); start = M6_BASE "/a"; }
        else if (!strcmp(argv[i], "--selftest-m7")) { selftest_m7 = true; st_setup_m7(); start = M7_BASE; }
        else if (!strcmp(argv[i], "--selftest-m8")) { selftest_m8 = true; st_setup_m8(); start = M8_BASE; }
        else if (!strcmp(argv[i], "--selftest-m9")) { selftest_m9 = true; st_setup_m9(); start = M9_BASE; }
        else if (!strcmp(argv[i], "--selftest-m10")) { selftest_m10 = true; st_setup_m10(); start = M10_BASE; }
        else if (!strcmp(argv[i], "--selftest-m11")) { selftest_m11 = true; st_setup_m11(); start = M11_BASE; }
        else if (!strcmp(argv[i], "--selftest-git")) { selftest_git = true; st_setup_git(); start = GIT_BASE "/repo"; }
        else if (!strcmp(argv[i], "--selftest-grep")) { selftest_grep = true; st_setup_grep(); start = GREP_BASE "/tree"; }
        else if (!strcmp(argv[i], "--selftest-du")) { selftest_du = true; st_setup_du(); start = DU_BASE "/tree"; }
        else if (!strcmp(argv[i], "--selftest-cmp")) { selftest_cmp = true; st_setup_cmp(); start = CMP_BASE "/a"; }
        else if (!strcmp(argv[i], "--selftest-admin")) { selftest_admin = true; st_setup_admin(); start = ADM_BASE; }
        else if (!strcmp(argv[i], "--select-tab") && i + 1 < argc) select_tab = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
        else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
            int v = SDL_GetVersion();
            printf("file-explorer %s (SDL %d.%d.%d)\n", APP_VERSION, SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
            return 0;
        }
        else if (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--new-window")) force_new_window = true;
        else if (!strcmp(argv[i], "--background")) g_bg_start = true; // started at login: no window until opened
        else if (!strcmp(argv[i], "--pick") && i + 1 < argc) pick_mode = argv[++i];  // file chooser (picker.c)
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) snprintf(g_pick_out, sizeof g_pick_out, "%s", argv[++i]);
        else if (npaths < MAX_TABS - 1) paths[npaths++] = argv[i];
    }
    if (npaths && !start) { // first path is the initial tab, the rest open as extra tabs
        start = paths[0];
        for (int i = 1; i < npaths && nextra < MAX_TABS - 1; i++) extra_tabs[nextra++] = paths[i];
    }
    if (pick_mode) { // file chooser: its own window, one folder, nothing remembered
        picker_setup(pick_mode, &start, pick_start, sizeof pick_start);
        force_new_window = true;
        nextra = 0;
        split_path = NULL;
    }
    signal(SIGCHLD, SIG_IGN); // reap xdg-open children automatically
    A.rec_dir = getenv("FE_RECORD");
    if (A.rec_dir && !A.rec_dir[0]) A.rec_dir = NULL;
    bool any_selftest = selftest || selftest_ops || selftest_m4 || selftest_m5 || selftest_m6 || selftest_m7 || selftest_m8 || selftest_m9 || selftest_m10 || selftest_m11 || selftest_git || selftest_grep || selftest_du || selftest_cmp || selftest_admin;
    A.selftest = any_selftest;
    if (any_selftest || A.screenshot) setenv("XDG_STATE_HOME", "/tmp/fe-selftest-state", 1); // don't touch the user's recent list
    // Self-tests and screenshots run headless: no window on the user's screen (a window the compositor isn't
    // showing can also stall presenting), rendering into memory. SDL_VIDEODRIVER overrides.
    bool headless = (any_selftest || A.screenshot) && !getenv("SDL_VIDEODRIVER");
    if (headless) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");

    if (any_selftest) {
        setenv("XDG_CONFIG_HOME", "/tmp/fe-selftest-config", 1); // never touch the user's settings
        unlink("/tmp/fe-selftest-config/file-explorer/settings.conf"); // each run starts clean (saves merge with the file)
        unlink("/tmp/fe-selftest-config/file-explorer/settings.conf.bak");
        setenv("XDG_CACHE_HOME", "/tmp/fe-selftest-cache", 1);
    }
#ifdef __GLIBC__
    mallopt(M_ARENA_MAX, 2); // many short-lived worker threads: keep glibc from growing per-thread heaps
#endif
    if (!getenv("MESA_VK_IGNORE_CONFORMANCE_WARNING")) setenv("MESA_VK_IGNORE_CONFORMANCE_WARNING", "1", 1);
    settings_load();
    // Single instance: hand the arguments to a running window and quit (before SDL starts: instant).
    if (!any_selftest && !A.screenshot && !force_new_window && A.single_instance && !A.rec_dir) {
        size_t cap = 256, o = 0;
        for (int i = 0; i < npaths; i++) cap += strlen(paths[i]) + PATH_MAX;
        cap += (size_t)nextra * PATH_MAX + PATH_MAX;
        char *msg = xmalloc(cap), abs[PATH_MAX];
        msg[0] = 0;
        if (start && cli_abs(start, abs)) o += (size_t)snprintf(msg + o, cap - o, "open\t%s\n", abs);
        for (int i = 0; i < nextra; i++)
            if (cli_abs(extra_tabs[i], abs)) o += (size_t)snprintf(msg + o, cap - o, "open\t%s\n", abs);
        if (split_path && cli_abs(split_path, abs)) o += (size_t)snprintf(msg + o, cap - o, "split\t%s\n", abs);
        const char *tok = getenv("XDG_ACTIVATION_TOKEN");
        if (tok && tok[0] && !strchr(tok, '\n')) o += (size_t)snprintf(msg + o, cap - o, "token\t%s\n", tok);
        o += (size_t)snprintf(msg + o, cap - o, "raise\t1\n");
        bool handed = ipc_send(msg);
        free(msg);
        if (handed) return 0;
    }
    if (!any_selftest) folders_load(); // self-tests start without remembered folder views
    if (!any_selftest) layouts_load();
    if (any_selftest) { settings_defaults(); A.restore_session = false; }
    if (picker_active()) { A.restore_session = false; A.maximized = false; A.win_w = MIN(A.win_w, 1100); A.win_h = MIN(A.win_h, 720); }
    themes_scan();
    g_theme_cur = MAX(0, theme_find(A.theme_name));
    theme_apply();

    SDL_SetAppMetadata(APP_NAME, APP_VERSION, picker_active() ? "dev.fileexplorer.picker" : "dev.fileexplorer"); // (picker: floated by the compositor)
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0"); // the close button is ours (window_close_requested)
    // SDL asks X11 compositors (picom, KWin, xfwm4) to stop compositing its windows, which suits full-screen games:
    // for a desktop app it makes the screen blink whenever the compositor switches between the two.
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    g_wake_event = SDL_RegisterEvents(1);
    pool_init();

    A.csd = titlebar_wanted(); // (borderless from the start: no system title bar flashing up first)
    A.win = SDL_CreateWindow(APP_NAME, A.win_w, A.win_h,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (A.maximized ? SDL_WINDOW_MAXIMIZED : 0) |
                             (A.csd ? SDL_WINDOW_BORDERLESS : 0) |
                             (g_bg_start && !any_selftest && !A.screenshot ? SDL_WINDOW_HIDDEN : 0));
    if (g_bg_start) A.win_hidden = true;
    if (!A.win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    window_icon_update();
    titlebar_apply();
    SDL_SetWindowMinimumSize(A.win, 760, 420);
    // Renderer: Vulkan uses noticeably less memory than GL with Mesa; fall back to SDL's default.
    A.ren = NULL;
    // headless: self-tests on the software renderer (deterministic), screenshots on the GPU (what users see)
    if (headless && any_selftest && !getenv("SDL_RENDER_DRIVER")) A.ren = SDL_CreateRenderer(A.win, "software");
    else if (!getenv("SDL_RENDER_DRIVER")) {
        if (!strcmp(A.renderer, "auto")) A.ren = SDL_CreateRenderer(A.win, "vulkan");
        else A.ren = SDL_CreateRenderer(A.win, A.renderer);
    }
    if (A.ren && !strcmp(A.renderer, "auto") && !getenv("SDL_RENDER_DRIVER") && vulkan_on_cpu(A.ren)) {
        SDL_DestroyRenderer(A.ren); // no Vulkan driver for the GPU (older Intel, nouveau, VMs): OpenGL usually has one
        A.ren = SDL_CreateRenderer(A.win, "opengl");
    }
    if (!A.ren) A.ren = SDL_CreateRenderer(A.win, NULL);
    if (!A.ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }
    // Self-tests and screenshots don't need vsync, and with it a window the compositor isn't showing
    // (another workspace on Wayland) blocks in present forever.
    SDL_SetRenderVSync(A.ren, any_selftest || A.screenshot ? 0 : 1);
    g_ren = A.ren;
    A.cur_default = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    A.cur_ew = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    A.cur_ns = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    A.cur_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    A.cur_text = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    SDL_GetWindowSizeInPixels(A.win, &A.W, &A.H);
    A.dpi_scale = SDL_GetWindowDisplayScale(A.win);
    if (A.dpi_scale <= 0) A.dpi_scale = 1;
    A.scale = A.dpi_scale * A.zoom;

    if (!atlas_init(A.ren)) { fprintf(stderr, "font atlas init failed\n"); return 1; }
    A.font_path = A.ui_font[0] ? font_family_file(A.ui_font) : NULL;
    if (!A.font_path) A.font_path = find_font_file(g_ui_families, (int)ARRAY_COUNT(g_ui_families));
    if (!A.font_path || !font_init(&A.font, A.font_path, (int)roundf((f32)A.font_size * A.scale)) ||
        !font_init(&A.font_small, A.font_path, (int)roundf((f32)(A.font_size - 1) * A.scale))) {
        fprintf(stderr, "could not load a UI font\n");
        return 1;
    }
    icon_themes_reload();
    icon_set_theme(MAX(0, icon_theme_find(A.icon_theme_name)));
    const char *mono[] = {"JetBrains Mono", "Cascadia Code", "Fira Code", "DejaVu Sans Mono", "Noto Sans Mono", "monospace"};
    A.mono_path = find_font_file(mono, (int)ARRAY_COUNT(mono));
    char *bold = font_weight_file(A.font_path, FC_WEIGHT_SEMIBOLD);
    if (!font_init(&A.font_brand, bold ? bold : A.font_path, (int)roundf((f32)(A.font_size + 3) * A.scale))) {
        fprintf(stderr, "could not load a UI font\n");
        return 1;
    }
    free(bold);
    if (A.mono_path && !font_init(&A.font_mono, A.mono_path, (int)roundf((f32)(A.font_size - 1) * A.scale))) A.mono_path = NULL;

    init_places();
    trash_size_refresh();
    watch_init();
    addon_start((!any_selftest || getenv("FE_SYSTEMCTL")) && !A.screenshot && !picker_active());
    A.devs_enabled = !any_selftest || getenv("FE_LSBLK");
    if (A.devs_enabled) {
        if (!any_selftest) devices_watch_start();
        devices_refresh();
    }
    recent_load();
    if (!any_selftest) index_rebuild_default();
    A.drop_row = -1;
    A.drop_place = -1;
    A.press_collapse = -1;
    A.click_open = -1;
    A.qa_edit_g = -1;
    A.qa_drag_row = -1;
    A.drop_qa = -1;
    layout();
    char path[PATH_MAX];
    bool restored = false;
    char selname[PATH_MAX];
    if (start && cli_target(start, path, selname)) {
        Tab *t0 = tab_new(path, 0, true);
        if (t0 && selname[0]) snprintf(t0->pane[0].select_name, sizeof t0->pane[0].select_name, "%s", selname);
    } else {
        restored = A.restore_session && !A.screenshot && session_restore();
        if (!restored) {
            if (!getcwd(path, sizeof path) || !strcmp(path, "/")) snprintf(path, sizeof path, "%s", getenv("HOME") ? getenv("HOME") : "/");
            tab_new(path, 0, true);
        }
    }
    A.settings_dirty = false;
    for (int i = 0; i < nextra; i++) open_target(extra_tabs[i], false, false);
    if (A.screenshot && getenv("FE_SHOT_TERMINAL")) open_terminal(active_pane()->path); // visual checks of terminal tabs
    if (A.screenshot && getenv("FE_SHOT_CONNECT")) { // visual checks: Edit server (first saved one), "pick" opens the icon picker
        connect_open(A.nservers ? 0 : -1);
        if (!strcmp(getenv("FE_SHOT_CONNECT"), "pick")) g_sheet.cn_pick = true;
    }
    if (A.screenshot && getenv("FE_SHOT_DU")) { // visual checks of the disk usage map ("sunburst" for that style)
        active_pane()->du = true;
        A.du_style = !strcmp(getenv("FE_SHOT_DU"), "sunburst");
    }
    if (split_path) open_target(split_path, true, true);
    if (A.screenshot && getenv("FE_SHOT_CMP") && A.npanels > 1) { // visual checks of folder compare ("diff": differences only)
        g_cmp.diff_only = !strcmp(getenv("FE_SHOT_CMP"), "diff");
        cmp_start(A.panels[0]->cur, A.panels[1]->cur);
    }
    if (A.single_instance && !A.screenshot && !picker_active() && (!any_selftest || A.ipc_test)) ipc_listen(ipc_handle);
    if (!any_selftest && !A.screenshot && !picker_active()) tray_update();
    if (select_tab >= 0) tab_select(select_tab);
    settings_snapshot(); // started: from here on, saves write only what this process changes (settings_merge)
    if (A.ig_fm1 && !any_selftest && !A.screenshot) ig_fm1_start(); // "Show in folder" (integration.c)
    if (!any_selftest && !A.screenshot && !g_bg_start) ig_first_run();
    ig_update_check(); // (AppImage only)
    SDL_StartTextInput(A.win);
    if (selftest) return run_selftest();
    if (selftest_ops) return run_selftest_ops();
    if (selftest_m4) return run_selftest_m4();
    if (selftest_m5) return run_selftest_m5();
    if (selftest_m6) return run_selftest_m6();
    if (selftest_m7) return run_selftest_m7();
    if (selftest_m8) return run_selftest_m8();
    if (selftest_m9) return run_selftest_m9();
    if (selftest_m10) return run_selftest_m10();
    if (selftest_m11) return run_selftest_m11();
    if (selftest_git) return run_selftest_git();
    if (selftest_grep) return run_selftest_grep();
    if (selftest_du) return run_selftest_du();
    if (selftest_cmp) return run_selftest_cmp();
    if (selftest_admin) return run_selftest_admin();

    // lsblk takes a few ms: wait for it (bounded) so the drives are in the first frame instead of popping in after.
    for (u64 t0 = now_ns(); A.devs_enabled && !A.devs_known && now_ns() - t0 < 150000000ull;) { SDL_Delay(2); main_drain(); }
    A.redraw = true;
    while (!A.quit) {
        bool loading = false;
        for (int i = 0; i < A.ntabs; i++)
            for (int j = 0; j < A.tabs[i]->npanes; j++) {
                Pane *p = &A.tabs[i]->pane[j];
                if ((p->L && atomic_load(&p->L->state) < LST_COMPLETE) || p->sort_pending) loading = true;
            }
        SDL_Event e;
        bool got;
        if ((A.redraw || animating()) && !A.win_hidden) got = SDL_PollEvent(&e);
        else if (loading || A.screenshot || ops_busy() || watch_pending() || A.search_due || A.pal_pending || sheet_busy())
            got = SDL_WaitEventTimeout(&e, (loading || watch_pending() || A.search_due) ? 30 : ops_moving() ? 16 : 100);
        else if (addon_wait_ms() > 0 || git_wait_ms() > 0 || cloud_wait_ms() > 0) { // add-on / cloud status, .git polls
            int t = INT_MAX;
            int w[] = {addon_wait_ms(), git_wait_ms(), cloud_wait_ms()};
            for (int k = 0; k < 3; k++) if (w[k] > 0) t = MIN(t, w[k]);
            got = SDL_WaitEventTimeout(&e, t);
        }
        else got = SDL_WaitEvent(&e);
        if (got) {
            handle_event(&e);
            while (SDL_PollEvent(&e)) handle_event(&e);
        }
        if (A.quit) break;
        main_drain();
        watch_sync();
        watch_process();
        if (A.search_due && now_ns() >= A.search_due && A.search_pane) search_apply(A.search_pane, false);
        if (atomic_exchange(&g_dev_changed, 0)) devices_refresh(); // hotplug / mount table changed
        if (g_recent_dirty && now_ns() - g_recent_saved > 5000000000ull) recent_save();
        if (A.settings_dirty && !A.screenshot && now_ns() - A.settings_saved > 3000000000ull) settings_save();
        term_tick();
        addon_tick();
        cloud_tick();
        git_tick();
        du_tick();
        cmp_tick();
        snap_tick();
        tray_sync();
        layout();
        for (int i = 0; i < A.ntabs; i++)
            for (int j = 0; j < A.tabs[i]->npanes; j++) pane_poll(&A.tabs[i]->pane[j]);
        if (A.win_hidden) continue; // keeps A.redraw: the next visible frame catches up
        frame();
        A.redraw = false;
    }
    ipc_shutdown();
    if (g_tray) { SDL_DestroyTray(g_tray); g_tray = NULL; }
    for (int i = 0; i < A.ntabs; i++) if (A.tabs[i]->term) { term_free(A.tabs[i]->term); A.tabs[i]->term = NULL; }
    if (A.trash_job) { atomic_store(&A.trash_job->cancel, 1); stats_release(A.trash_job); A.trash_job = NULL; }
    recent_save();
    if (!A.screenshot) settings_save();
    SDL_DestroyRenderer(A.ren);
    SDL_DestroyWindow(A.win);
    SDL_Quit();
    return 0;
}
