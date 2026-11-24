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
    Rect r[RIBBON_N];          // where each shown button is (w 0: in the "..." menu, scrolled out, or not shown here)
    bool shown[RIBBON_N];
    bool sep_before[RIBBON_N]; // a thin line before it (a new group on the same line)
    Rect more, grip;           // the "..." button (w 0: everything fits / not that mode), the grip that moves it
    Rect prev, next, win;      // Scroll: the arrows (w 0: everything fits) and the window between them
    f32 total, avail;          // Scroll: the icons' length and the window's
} g_rb;           // (of the pane last laid out: drawing and clicks lay it out first)

static bool ribbon_vertical(void) { return A.ribbon_pos == RP_LEFT || A.ribbon_pos == RP_RIGHT; }
static f32 ribbon_line(void) { return roundf(S(ribbon_vertical() ? 40 : 36)); } // a row's height / a column's width
static f32 ribbon_item_len(const RibbonItem *it) { return roundf(S(30)) + (it->sub ? roundf(S(14)) : 0); } // (room for a chevron)

// Lines (rows, or columns at the side) the band needs for a length: Wrap fits every icon (also the ones that only
// show for an archive or an image, so the band doesn't jump as the cursor moves); the other modes one.
static int ribbon_lines(f32 len) {
    if (!A.ribbon) return 0;
    if (A.ribbon_overflow != RO_WRAP) return 1;
    f32 start = roundf(S(6)), end = len - roundf(S(24)), u = start;
    int lines = 1, last = -1;
    for (int i = 0; i < RIBBON_N; i++) {
        const RibbonItem *it = &g_ribbon[i];
        f32 l = ribbon_item_len(it), sx = last >= 0 && it->group != last ? roundf(S(13)) : 0;
        if (u + sx + l > end && u > start) { lines++; u = start; sx = 0; }
        u += sx + l + roundf(S(2));
        last = it->group;
    }
    return MIN(lines, 6);
}

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

// Place the icons in the pane's ribbon band, along its row (or down its column at the side); the grip at the far end.
// What doesn't fit: the "..." menu before the grip, more lines (Wrap), or arrows that slide them along (Scroll).
static void ribbon_layout(Pane *p) {
    Rect r = p->ly.ribbon;
    bool v = ribbon_vertical();
    f32 bs = roundf(S(30)), gap = roundf(S(2)), sep = roundf(S(13)), lt = ribbon_line(), off = roundf((lt - bs) * 0.5f);
    f32 a0 = v ? r.y : r.x, a1 = v ? r.y + r.h : r.x + r.w, c0 = v ? r.x : r.y;
    f32 start = a0 + roundf(S(6)), end = a1 - roundf(S(24));
#define RB_RECT(u, l, k) (v ? (Rect){c0 + off + (f32)(k) * lt, (u), bs, (l)} : (Rect){(u), c0 + off + (f32)(k) * lt, (l), bs})
    g_rb.grip = v ? (Rect){c0 + off, a1 - roundf(S(20)), bs, roundf(S(16))} : (Rect){a1 - roundf(S(20)), c0 + off, roundf(S(16)), bs};
    g_rb.more = g_rb.prev = g_rb.next = g_rb.win = (Rect){0};
    g_rb.total = 0;
    for (int i = 0; i < RIBBON_N; i++) { g_rb.shown[i] = ribbon_item_shown(&g_ribbon[i], p); g_rb.r[i] = (Rect){0}; g_rb.sep_before[i] = false; }
    if (A.ribbon_overflow == RO_SCROLL) { // one line; arrows at both ends when it's longer than the band
        int last = -1;
        for (int i = 0; i < RIBBON_N; i++) {
            if (!g_rb.shown[i]) continue;
            g_rb.total += (last >= 0 && g_ribbon[i].group != last ? sep : 0) + ribbon_item_len(&g_ribbon[i]) + gap;
            last = g_ribbon[i].group;
        }
        f32 u = start, w0 = start, w1 = end, aw = roundf(S(20));
        if (start + g_rb.total > end) {
            g_rb.prev = RB_RECT(start, aw, 0);
            g_rb.next = RB_RECT(end - aw, aw, 0);
            w0 = start + aw + gap;
            w1 = end - aw - gap;
            g_rb.avail = w1 - w0;
            p->rb_scroll = CLAMP(p->rb_scroll, 0.f, MAX(0.f, g_rb.total - g_rb.avail));
            u = w0 - p->rb_scroll;
        } else p->rb_scroll = 0;
        g_rb.win = v ? (Rect){r.x, w0, r.w, w1 - w0} : (Rect){w0, r.y, w1 - w0, r.h};
        last = -1;
        for (int i = 0; i < RIBBON_N; i++) {
            if (!g_rb.shown[i]) continue;
            const RibbonItem *it = &g_ribbon[i];
            f32 l = ribbon_item_len(it);
            if (last >= 0 && it->group != last) { u += sep; g_rb.sep_before[i] = true; }
            if (u + l > w0 && u < w1) g_rb.r[i] = RB_RECT(u, l, 0); // (partly out: clipped)
            u += l + gap;
            last = it->group;
        }
        return;
    }
    bool wrap = A.ribbon_overflow == RO_WRAP;
    int lines = wrap ? MAX(1, (int)roundf((v ? r.w : r.h) / lt)) : 1;
    for (int pass = 0; pass < 2; pass++) { // More, pass 1: with room for "..."
        f32 u = start, lim = pass ? end - bs - gap : end;
        bool all = true;
        int last = -1, k = 0;
        for (int i = 0; i < RIBBON_N; i++) {
            if (!g_rb.shown[i]) continue;
            const RibbonItem *it = &g_ribbon[i];
            f32 l = ribbon_item_len(it), sx = last >= 0 && it->group != last ? sep : 0;
            g_rb.r[i] = (Rect){0};
            g_rb.sep_before[i] = false;
            if (wrap && u + sx + l > lim && u > start && k + 1 < lines) { k++; u = start; sx = 0; }
            if (!all || u + sx + l > lim) { all = false; continue; }
            u += sx;
            g_rb.sep_before[i] = sx > 0;
            g_rb.r[i] = RB_RECT(u, l, k);
            u += l + gap;
            last = it->group;
        }
        g_rb.more = all ? (Rect){0} : RB_RECT(end - bs, bs, lines - 1);
        if (all || pass) break;
    }
#undef RB_RECT
}

// Six dots: the grip that drags the ribbon to another place.
static void ribbon_draw_grip(Rect g, bool hv) {
    if (hv) draw_rrect(g.x, g.y, g.w, g.h, S(4), C(COL_FIELD_HOVER));
    f32 d = roundf(MAX(S(2.4f), 2.f)), st = roundf(S(5)), cx = g.x + g.w * 0.5f, cy = g.y + g.h * 0.5f;
    bool v = g.w > g.h; // (lying down at the side)
    for (int a = -1; a <= 1; a++)
        for (int b = 0; b < 2; b++) {
            f32 x = v ? cx + (f32)a * st : cx + ((f32)b - 0.5f) * st, y = v ? cy + ((f32)b - 0.5f) * st : cy + (f32)a * st;
            draw_rrect(roundf(x - d * 0.5f), roundf(y - d * 0.5f), d, d, d * 0.5f, C(hv ? COL_TEXT_DIM : COL_TEXT_FAINT));
        }
}

// The pane's ribbon: icons on the navigation bar's colour (it reads as a part of it); the name of each in a tooltip.
static void draw_ribbon(Pane *p) {
    Rect r = p->ly.ribbon;
    if (r.h <= 0 || r.w <= 0) return;
    bool v = ribbon_vertical();
    draw_rect(r.x, r.y, r.w, r.h, C(COL_TOOLBAR));
    if (g_separators && A.ribbon_pos != RP_BELOW && A.ribbon_pos != RP_ABOVE) { // (at the side / bottom: a line to the list)
        Rect l = A.ribbon_pos == RP_LEFT ? (Rect){r.x + r.w - 1, r.y, 1, r.h} : A.ribbon_pos == RP_RIGHT ? (Rect){r.x, r.y, 1, r.h}
                                                                                                          : (Rect){r.x, r.y, r.w, 1};
        draw_rect(l.x, l.y, l.w, l.h, C(COL_BORDER));
    }
    ribbon_layout(p);
    ribbon_draw_grip(g_rb.grip, A.drag == DRAG_RIBBON || hot(g_rb.grip));
    fit_tip_offer_box(g_rb.grip.x, g_rb.grip.y, g_rb.grip.w, g_rb.grip.h, "Drag to move the ribbon", 23);
    ClipSave cs = {0};
    bool clip = g_rb.win.w > 0 && g_rb.prev.w > 0;
    if (clip) cs = draw_clip_push((SDL_Rect){(int)g_rb.win.x, (int)g_rb.win.y, (int)g_rb.win.w, (int)g_rb.win.h});
    for (int i = 0; i < RIBBON_N; i++) {
        Rect b = g_rb.r[i];
        if (b.w <= 0) continue;
        const RibbonItem *it = &g_ribbon[i];
        if (g_rb.sep_before[i]) { // the line between two groups
            f32 o = roundf(S(7)), in = roundf(S(8));
            if (v) draw_rect(b.x + in, b.y - o, b.w - 2 * in, 1, C(COL_BORDER));
            else draw_rect(b.x - o, b.y + in, 1, b.h - 2 * in, C(COL_BORDER));
        }
        bool en = ribbon_item_enabled(it, p), on = ribbon_item_on(it, p), hv = en && hot(b) && (!clip || hot(g_rb.win));
        if (on || hv) draw_rrect(b.x, b.y, b.w, b.h, S(6), on ? CA(COL_ACCENT, hv ? 0.30f : 0.22f) : C(COL_FIELD_HOVER));
        f32 is = roundf(S(18)), bs = roundf(S(30)), ix = roundf(b.x + (bs - is) * 0.5f), iy = roundf(b.y + (bs - is) * 0.5f);
        if (!v) iy = roundf(b.y + (b.h - is) * 0.5f);
        else ix = roundf(b.x + (b.w - is) * 0.5f);
        Color ic = !en ? C(COL_TEXT_FAINT) : on ? C(COL_ACCENT) : C(COL_TEXT_DIM);
        draw_ui_icon(ribbon_icon(it), ix, iy, is, ic);
        if (it->sub) { // a dropdown: its chevron, clear of the icon (after it, or under it in a column)
            if (v) draw_chevron(b.x + b.w * 0.5f, b.y + b.h - roundf(S(8)), S(6), 3, MAX(1.f, S(1.2f)), ic);
            else draw_chevron(b.x + b.w - roundf(S(9)), b.y + b.h * 0.5f, S(6), 3, MAX(1.f, S(1.2f)), ic);
        }
        if (!clip || hot(g_rb.win)) fit_tip_offer_box(b.x, b.y, b.w, b.h, it->label, (int)strlen(it->label)); // (its name, after a moment)
    }
    if (clip) draw_clip_pop(cs);
    for (int k = 0; k < 2 && g_rb.prev.w > 0; k++) { // Scroll: the arrows (faint at the end they can't go past)
        Rect b = k ? g_rb.next : g_rb.prev;
        bool can = k ? p->rb_scroll < g_rb.total - g_rb.avail - 0.5f : p->rb_scroll > 0.5f, hv = can && hot(b);
        if (hv) draw_rrect(b.x, b.y, b.w, b.h, S(5), C(COL_FIELD_HOVER));
        draw_chevron(b.x + b.w * 0.5f, b.y + b.h * 0.5f, S(9), v ? (k ? 3 : 2) : (k ? 1 : 0), MAX(1.f, S(1.4f)), C(can ? COL_TEXT_DIM : COL_TEXT_FAINT));
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
    if (in_rect(g_rb.grip, x, y)) { // drag it somewhere else (the drop shows where; ribbon_drop on release)
        A.drag = DRAG_RIBBON;
        A.drag_pane = p;
        A.ribbon_drop = -1;
        return;
    }
    if (g_rb.prev.w > 0 && (in_rect(g_rb.prev, x, y) || in_rect(g_rb.next, x, y))) { // Scroll: most of a window along
        p->rb_scroll += (in_rect(g_rb.next, x, y) ? 1.f : -1.f) * MAX(roundf(S(32)), g_rb.avail * 0.6f);
        A.redraw = true;
        return;
    }
    if (g_rb.more.w > 0 && in_rect(g_rb.more, x, y)) { ribbon_more_menu(p); return; }
    if (g_rb.prev.w > 0 && !in_rect(g_rb.win, x, y)) return;
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

// The wheel over a Scroll ribbon slides it (true: it was one).
static bool ribbon_wheel(f32 x, f32 y, f32 wy) {
    Pane *p = A.ribbon && A.ribbon_overflow == RO_SCROLL ? pane_at(x, y) : NULL;
    if (!p || p->ly.ribbon.h <= 0 || !in_rect(p->ly.ribbon, x, y)) return false;
    p->rb_scroll -= wy * roundf(S(36));
    A.redraw = true;
    return true;
}

// Where a dragged ribbon would go for the pointer at x, y (-1: not over a folder): near a pane's side -> that side,
// its lower part -> the bottom, above the middle of its navigation bar -> above the path, else below it.
static int ribbon_drop_at(f32 x, f32 y, Pane **pp) {
    Pane *p = pane_at(x, y);
    if (pp) *pp = p;
    if (!p) return -1;
    PaneLayout *ly = &p->ly;
    Rect a = ly->area;
    f32 edge = MIN(roundf(S(90)), a.w * 0.2f);
    if (y > ly->toolbar.y + ly->toolbar.h && x < a.x + edge) return RP_LEFT;
    if (y > ly->toolbar.y + ly->toolbar.h && x > a.x + a.w - edge) return RP_RIGHT;
    if (y > a.y + a.h * 0.65f) return RP_BOTTOM;
    return y < ly->toolbar.y + ly->toolbar.h * 0.5f ? RP_ABOVE : RP_BELOW;
}

// While dragging: a bar of the accent where it would go (in the pane under the pointer).
static void draw_ribbon_drop(void) {
    if (A.drag != DRAG_RIBBON || A.ribbon_drop < 0 || !A.ribbon_drop_pane || !pane_exists(A.ribbon_drop_pane)) return;
    PaneLayout *ly = &A.ribbon_drop_pane->ly;
    Rect a = ly->area, tb = ly->toolbar;
    f32 t = roundf(S(4)), top = tb.y + tb.h, bottom = a.y + a.h;
    Rect r = A.ribbon_drop == RP_ABOVE ? (Rect){a.x, tb.y - t * 0.5f, a.w, t}
           : A.ribbon_drop == RP_BELOW ? (Rect){a.x, top - t * 0.5f, a.w, t}
           : A.ribbon_drop == RP_BOTTOM ? (Rect){a.x, bottom - t, a.w, t}
           : A.ribbon_drop == RP_LEFT ? (Rect){a.x, top, t, bottom - top}
                                      : (Rect){a.x + a.w - t, top, t, bottom - top};
    draw_rrect(r.x, r.y, r.w, r.h, t * 0.5f, CA(COL_ACCENT, 0.85f));
}

// Released: the ribbon goes there (every panel's).
static void ribbon_drop(void) {
    if (A.ribbon_drop >= 0 && A.ribbon_drop != A.ribbon_pos) {
        A.ribbon_pos = A.ribbon_drop;
        A.settings_dirty = true;
        layout();
    }
    A.ribbon_drop = -1;
    A.ribbon_drop_pane = NULL;
    drag_cancel();
    A.redraw = true;
}
