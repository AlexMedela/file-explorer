// ribbon.c - the ribbon (Options > Appearance > Ribbon, off by default): a row of icons for the common file actions
// right under each panel's navigation bar, acting on that panel. Icons that don't apply are greyed; their names show
// in a tooltip; what doesn't fit goes into its "..." menu. Included from app.c after render.c.

typedef struct {
    const char *label;
    int act;
    u8 group; // a thin line between groups
    u8 need;  // RN_*: when it can be pressed
    u8 sub;   // RS_*: opens a dropdown (the right-click menu's submenu of the same name)
} RibbonItem;

enum { RN_ALWAYS, RN_TARGET, RN_ONE, RN_PASTE, RN_ARCHIVE, RN_IMAGE };
enum { RS_NONE, RS_OPEN_WITH, RS_COMPRESS, RS_VIEWS, RS_GROUP };

// (icons: the right-click menu's for the same action, menu_action_icon)
static const RibbonItem g_ribbon[] = {
    {"Cut", ACT_CUT, 0, RN_TARGET, 0},
    {"Copy", ACT_COPY, 0, RN_TARGET, 0},
    {"Paste", ACT_PASTE, 0, RN_PASTE, 0},
    {"Copy path", ACT_COPY_PATH, 0, RN_ALWAYS, 0},
    {"Rename", ACT_RENAME, 1, RN_TARGET, 0},
    {"Move to Trash", ACT_TRASH, 1, RN_TARGET, 0},
    {"Compress to", ACT_COMPRESS_MENU, 1, RN_TARGET, RS_COMPRESS},
    {"New folder", ACT_NEW_FOLDER, 2, RN_ALWAYS, 0},
    {"New file", ACT_NEW_FILE, 2, RN_ALWAYS, 0},
    {"Open", ACT_OPEN, 3, RN_ONE, 0},
    {"Open with", ACT_OPEN_WITH, 3, RN_ONE, RS_OPEN_WITH},
    {"Extract here", ACT_EXTRACT_HERE, 3, RN_ARCHIVE, 0},
    {"Mount", ACT_IMG_MOUNT, 3, RN_IMAGE, 0},
    {"Terminal", ACT_TERMINAL, 3, RN_ALWAYS, 0},
    {"Properties", ACT_PROPERTIES, 3, RN_ALWAYS, 0},
    {"Select all", ACT_SELECT_ALL, 4, RN_ALWAYS, 0},
    {"Views", ACT_MENU_VIEWS, 5, RN_ALWAYS, RS_VIEWS},
    {"Group by", ACT_MENU_GROUP, 5, RN_ALWAYS, RS_GROUP},
    {"Hidden files", ACT_HIDDEN, 5, RN_ALWAYS, 0},
    {"Details panel", ACT_PREVIEW, 5, RN_ALWAYS, 0},
};

static void ribbon_dummy_sub(Pane *p) { (void)p; }

static int ribbon_icon(const RibbonItem *it) {
    MenuItem mi = {.act = it->act, .checkable = true, .sub = it->sub ? ribbon_dummy_sub : NULL};
    return menu_action_icon(&mi);
}
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
    (void)p;
    return (it->act == ACT_HIDDEN && A.show_hidden) || (it->act == ACT_PREVIEW && A.preview_on);
}

static f32 ribbon_item_w(const RibbonItem *it) { return roundf(S(30)) + (it->sub ? roundf(S(10)) : 0); } // (room for the dropdown's chevron)

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
            f32 w = ribbon_item_w(it);
            if (!all || x + sx + w > lim) { all = false; continue; }
            x += sx;
            g_rb.r[i] = (Rect){x, by, w, bs};
            x += w + gap;
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
        f32 is = roundf(S(18)), iw = it->sub ? b.w - roundf(S(10)) : b.w;
        Color ic = !en ? C(COL_TEXT_FAINT) : on ? C(COL_ACCENT) : C(COL_TEXT_DIM);
        draw_ui_icon(ribbon_icon(it), roundf(b.x + (iw - is) * 0.5f + (it->sub ? roundf(S(2)) : 0)), roundf(b.y + (b.h - is) * 0.5f), is, ic);
        if (it->sub) draw_chevron(b.x + b.w - roundf(S(9)), b.y + b.h * 0.5f, S(6), 3, MAX(1.f, S(1.2f)), ic); // a dropdown
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
        if (it->act == ACT_HIDDEN || it->act == ACT_PREVIEW) menu_add_check(it->label, it->act, ribbon_item_on(it, p), true);
        else if (it->sub == RS_OPEN_WITH) {
            Entry *e = ribbon_cursor_entry(p);
            menu_add_open_with(it->label, e ? mime_for_file(path_basename(e->name), e->flags & ENT_DIR) : "", ribbon_item_enabled(it, p));
        } else {
            menu_add(it->label, "", it->act, ribbon_item_enabled(it, p));
            m->items[m->n - 1].sub = it->sub == RS_COMPRESS ? menu_sub_compress : it->sub == RS_VIEWS ? menu_sub_views
                                   : it->sub == RS_GROUP ? menu_sub_group : NULL;
        }
    }
    menu_finish(g_rb.more.x, g_rb.more.y + g_rb.more.h + S(4));
}

// A dropdown: the right-click menu's submenu of the same name, as a menu under the button (its items act like there:
// on the selection, or the cursor's item).
static void ribbon_dropdown(Pane *p, const RibbonItem *it, Rect b) {
    Menu *m = &A.menu;
    m->n = m->ninfo = 0;
    m->hover = -1;
    m->pane = p;
    m->keep_open = false;
    m->reopen = NULL;
    switch (it->sub) {
    case RS_OPEN_WITH: {
        Entry *e = ribbon_cursor_entry(p);
        if (!e) return;
        menu_add_open_with("Open with", mime_for_file(path_basename(e->name), e->flags & ENT_DIR), true); // (the apps)
        m->n = 0;
        menu_sub_open_with(p);
        break;
    }
    case RS_COMPRESS: menu_sub_compress(p); break;
    case RS_VIEWS: menu_sub_views(p); break;
    case RS_GROUP: menu_sub_group(p); break;
    }
    menu_finish(b.x, b.y + b.h + S(4));
}

// A click in the pane's ribbon: the button there (greyed ones do nothing), its dropdown, or the "..." menu.
static void ribbon_click(Pane *p, f32 x, f32 y) {
    ribbon_layout(p);
    if (g_rb.more.w > 0 && in_rect(g_rb.more, x, y)) { ribbon_more_menu(p); return; }
    for (int i = 0; i < RIBBON_N; i++) {
        if (g_rb.r[i].w <= 0 || !in_rect(g_rb.r[i], x, y)) continue;
        const RibbonItem *it = &g_ribbon[i];
        if (ribbon_item_enabled(it, p)) {
            if (it->sub) ribbon_dropdown(p, it, g_rb.r[i]);
            else do_action(p, it->act);
        }
        A.redraw = true;
        return;
    }
}
