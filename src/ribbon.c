// ribbon.c - the ribbon (Options > Appearance > Ribbon, off by default): a row of icons for the common file actions
// right under each panel's navigation bar, acting on that panel. Icons that don't apply are greyed; their names show
// in a tooltip; what doesn't fit goes into its "..." menu. Included from app.c after render.c.

typedef struct {
    const char *label;
    int icon, act;
    u8 group; // a thin line between groups
    u8 need;  // RN_*: when it can be pressed
} RibbonItem;

enum { RN_ALWAYS, RN_TARGET, RN_ONE, RN_PASTE, RN_ARCHIVE, RN_IMAGE };

static const RibbonItem g_ribbon[] = {
    {"Cut", UI_SCISSORS, ACT_CUT, 0, RN_TARGET},
    {"Copy", UI_COPY, ACT_COPY, 0, RN_TARGET},
    {"Paste", UI_CLIPBOARD_PASTE, ACT_PASTE, 0, RN_PASTE},
    {"Copy path", UI_LINK, ACT_COPY_PATH, 0, RN_ALWAYS},
    {"Rename", UI_PENCIL, ACT_RENAME, 1, RN_TARGET},
    {"Delete", UI_TRASH, ACT_TRASH, 1, RN_TARGET},
    {"New folder", UI_FOLDER_PLUS, ACT_NEW_FOLDER, 2, RN_ALWAYS},
    {"New file", UI_FILE_PLUS, ACT_NEW_FILE, 2, RN_ALWAYS},
    {"Open", UI_FOLDER_OPEN, ACT_OPEN, 3, RN_ONE},
    {"Open with", UI_EXTERNAL_LINK, ACT_OPEN_WITH, 3, RN_ONE},
    {"Terminal", UI_TERMINAL, ACT_TERMINAL, 3, RN_ALWAYS},
    {"Properties", UI_INFO, ACT_PROPERTIES, 3, RN_ALWAYS},
    {"Extract here", UI_ARCHIVE, ACT_EXTRACT_HERE, 3, RN_ARCHIVE},
    {"Mount", UI_EJECT, ACT_IMG_MOUNT, 3, RN_IMAGE},
    {"Select all", UI_SQUARE_CHECK_BIG, ACT_SELECT_ALL, 4, RN_ALWAYS},
    {"Hidden files", UI_EYE, ACT_HIDDEN, 5, RN_ALWAYS},
    {"Tree view", UI_LIST_TREE, ACT_TREE_VIEW, 5, RN_ALWAYS},
    {"Details", UI_PANEL_RIGHT, ACT_PREVIEW, 5, RN_ALWAYS},
};
#define RIBBON_N ((int)ARRAY_COUNT(g_ribbon))

static struct {
    Rect r[RIBBON_N]; // where each shown button is (w 0: in the "..." menu, or not shown here)
    bool shown[RIBBON_N];
    Rect more;        // the "..." button (w 0: everything fits)
} g_rb;           // (of the pane last laid out: drawing and clicks lay it out first)

// The entry under the cursor of the pane (NULL: none, or a line of a contents search).
static Entry *ribbon_cursor_entry(Pane *p) {
    if (!p || !p->L || !p->order || p->cursor < 0 || p->cursor >= (i64)p->count) return NULL;
    Entry *e = &p->L->entries[p->order[p->cursor]];
    return e->flags & ENT_MATCH ? NULL : e;
}

// Shown at all: the contextual ones only for what they apply to (an archive, a disc image).
static bool ribbon_item_shown(const RibbonItem *it, Pane *p) {
    Entry *e = ribbon_cursor_entry(p);
    bool file = e && !(e->flags & ENT_DIR);
    if (it->need == RN_ARCHIVE) return file && !pane_is_archive(p) && is_archive_name(e->name);
    if (it->need == RN_IMAGE) return file && !pane_is_archive(p) && is_disc_image(e->name) && !path_is_remote(p->path);
    return true;
}

static bool ribbon_item_enabled(const RibbonItem *it, Pane *p) {
    bool target = p->sel_count > 0 || ribbon_cursor_entry(p);
    switch (it->need) {
    case RN_TARGET: return target;
    case RN_ONE: return p->sel_count <= 1 && ribbon_cursor_entry(p);
    case RN_PASTE: return clip_has_files() || clip_image_type() >= 0;
    }
    return true;
}

// On (drawn with the accent): the toggles.
static bool ribbon_item_on(const RibbonItem *it, Pane *p) {
    return (it->act == ACT_HIDDEN && A.show_hidden) || (it->act == ACT_PREVIEW && A.preview_on) || (it->act == ACT_TREE_VIEW && p->tree);
}

// Place the icons in the pane's ribbon band, from the left under the back button; the ones that don't fit (from the
// end) go into the "..." menu at its right end.
static void ribbon_layout(Pane *p) {
    Rect r = p->ly.ribbon;
    f32 bs = roundf(S(30)), gap = roundf(S(2)), sep = roundf(S(13)), by = r.y + roundf((r.h - bs) * 0.5f) - roundf(S(2));
    f32 x0 = r.x + roundf(S(8)), right = r.x + r.w - roundf(S(8));
    for (int pass = 0; pass < 2; pass++) { // pass 1: with room for "..."
        f32 x = x0, lim = pass ? right - bs - gap : right;
        bool all = true;
        int last_group = -1;
        for (int i = 0; i < RIBBON_N; i++) {
            const RibbonItem *it = &g_ribbon[i];
            g_rb.shown[i] = ribbon_item_shown(it, p);
            g_rb.r[i] = (Rect){0};
            if (!g_rb.shown[i]) continue;
            f32 sx = last_group >= 0 && it->group != last_group ? sep : 0;
            if (!all || x + sx + bs > lim) { all = false; continue; }
            x += sx;
            g_rb.r[i] = (Rect){x, by, bs, bs};
            x += bs + gap;
            last_group = it->group;
        }
        g_rb.more = all ? (Rect){0} : (Rect){right - bs, by, bs, bs};
        if (all || pass) break;
    }
}

// The pane's ribbon: icons on the navigation bar's colour (it reads as a second row of it); the name of each in a
// tooltip.
static void draw_ribbon(Pane *p) {
    Rect r = p->ly.ribbon;
    if (r.h <= 0 || r.w <= 0) return;
    draw_rect(r.x, r.y, r.w, r.h, C(COL_TOOLBAR));
    ribbon_layout(p);
    int last_group = -1;
    for (int i = 0; i < RIBBON_N; i++) {
        Rect b = g_rb.r[i];
        if (b.w <= 0) continue;
        const RibbonItem *it = &g_ribbon[i];
        if (last_group >= 0 && it->group != last_group) // the line between two groups
            draw_rect(b.x - roundf(S(7)), b.y + roundf(S(8)), 1, b.h - roundf(S(16)), C(COL_BORDER));
        last_group = it->group;
        bool en = ribbon_item_enabled(it, p), on = ribbon_item_on(it, p), hv = en && hot(b);
        if (on || hv) draw_rrect(b.x, b.y, b.w, b.h, S(6), on ? CA(COL_ACCENT, hv ? 0.30f : 0.22f) : C(COL_FIELD_HOVER));
        f32 is = roundf(S(18));
        int icon = it->act == ACT_HIDDEN && !A.show_hidden ? UI_EYE_OFF : it->icon;
        draw_ui_icon(icon, roundf(b.x + (b.w - is) * 0.5f), roundf(b.y + (b.h - is) * 0.5f), is,
                     !en ? C(COL_TEXT_FAINT) : on ? C(COL_ACCENT) : C(COL_TEXT_DIM));
        fit_tip_offer_box(b.x, b.y, b.w, b.h, it->label, (int)strlen(it->label)); // (its name, after a moment)
    }
    if (g_rb.more.w > 0) {
        Rect b = g_rb.more;
        bool open = A.menu.open && A.menu.ox == b.x && A.menu.oy == b.y + b.h + S(4);
        if (open || hot(b)) draw_rrect(b.x, b.y, b.w, b.h, S(6), C(COL_FIELD_HOVER));
        f32 is = roundf(S(18));
        draw_ui_icon(UI_ELLIPSIS, roundf(b.x + (b.w - is) * 0.5f), roundf(b.y + (b.h - is) * 0.5f), is, C(COL_TEXT_DIM));
        fit_tip_offer_box(b.x, b.y, b.w, b.h, "More", 4);
    }
}

// "...": the buttons that didn't fit, as a menu (same order, same greying).
static void ribbon_more_menu(Pane *p) {
    Menu *m = &A.menu;
    m->n = m->ninfo = 0;
    m->hover = -1;
    m->pane = p;
    m->keep_open = false;
    m->reopen = NULL;
    int last_group = -1;
    for (int i = 0; i < RIBBON_N; i++) {
        const RibbonItem *it = &g_ribbon[i];
        if (!g_rb.shown[i] || g_rb.r[i].w > 0) continue;
        if (last_group >= 0 && it->group != last_group) menu_sep();
        last_group = it->group;
        if (it->act == ACT_HIDDEN || it->act == ACT_PREVIEW || it->act == ACT_TREE_VIEW)
            menu_add_check(it->label, it->act, ribbon_item_on(it, p), true);
        else menu_add(it->label, "", it->act, ribbon_item_enabled(it, p));
    }
    menu_finish(g_rb.more.x, g_rb.more.y + g_rb.more.h + S(4));
}

// A click in the pane's ribbon: the button there (greyed ones do nothing), or the "..." menu.
static void ribbon_click(Pane *p, f32 x, f32 y) {
    ribbon_layout(p);
    if (g_rb.more.w > 0 && in_rect(g_rb.more, x, y)) { ribbon_more_menu(p); return; }
    for (int i = 0; i < RIBBON_N; i++) {
        if (g_rb.r[i].w <= 0 || !in_rect(g_rb.r[i], x, y)) continue;
        if (ribbon_item_enabled(&g_ribbon[i], p)) do_action(p, g_ribbon[i].act);
        A.redraw = true;
        return;
    }
}
