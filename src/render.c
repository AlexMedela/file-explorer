// render.c - drawing every frame part: icons, panel/tab bars, toolbar, sidebar, list and grid (with the
// thumbnail texture cache), status bar, progress cards, dialogs, menus, palette, preview panel (included from app.c)

// --- rendering -------------------------------------------------------------

static u32 file_color(const Entry *e) {
    if (e->mflags & MF_EXEC) return g_dcat[DC_EXEC].col;
    if (!e->ext) return g_dcat[DC_OTHER].col;
    return g_dcat[file_category(e->name, false)].col;
}

static void draw_folder_icon(f32 x, f32 y, f32 s, u32 col, u32 tab) {
    draw_rrect(x, y + s * 0.14f, s * 0.46f, s * 0.3f, s * 0.09f, C(tab));
    draw_rrect(x, y + s * 0.26f, s, s * 0.62f, s * 0.1f, C(col));
}

// Icon id from the current set at (x, y), s px; hidden = faded. False if it couldn't be drawn.
static bool draw_icon_id(f32 x, f32 y, f32 s, int id, bool hidden) {
    if (id < 0) return false;
    if (s <= 48) {
        Glyph *g = icon_glyph(id, (int)roundf(s));
        if (g) {
            f32 a = g_alpha;
            if (hidden) g_alpha *= 0.55f;
            draw_glyph(g, roundf(x), roundf(y), (Color){1, 1, 1, 1});
            g_alpha = a;
            return true;
        }
    }
    SDL_Texture *t = big_icon_tex(id, (int)roundf(s));
    if (!t) return false;
    draw_flush();
    SDL_FRect dst = {roundf(x), roundf(y), roundf(s), roundf(s)};
    SDL_SetTextureAlphaMod(t, (u8)(255.f * g_alpha * (hidden ? 0.55f : 1.f)));
    SDL_RenderTexture(A.ren, t, NULL, &dst);
    SDL_SetTextureAlphaMod(t, 255);
    return true;
}

// A folder: the icon set's folder icon for this name (falls back to the built-in folder).
static void draw_dir_icon(f32 x, f32 y, f32 s, const char *name, bool hidden) {
    if (draw_icon_id(x, y, s, folder_icon_for_name(name), hidden)) return;
    draw_folder_icon(x, y + (s > 48 ? s * 0.04f : 0), s, hidden ? rgb_mix(COL_FOLDER, COL_BG, 0.35f) : COL_FOLDER, hidden ? rgb_mix(COL_FOLDER_TAB, COL_BG, 0.35f) : COL_FOLDER_TAB);
}

// A folder given by its full path: the trash gets the bin, home its folder, anything else goes by its name.
static void draw_path_icon(f32 x, f32 y, f32 s, const char *path) {
    if (is_trash_dir(path) && draw_icon_id(x, y, s, icon_find("trash"), false)) return;
    const char *home = getenv("HOME");
    draw_dir_icon(x, y, s, home && home[0] && !strcmp(path, home) ? "home" : path_basename(path), false);
}

static void draw_file_icon(f32 x, f32 y, f32 s, u32 col) {
    f32 w = roundf(s * 0.74f), ox = roundf(x + (s - w) * 0.5f);
    draw_rrect(ox, y + s * 0.04f, w, s * 0.92f, s * 0.12f, C(0xD5D8DF));
    draw_rect(ox, roundf(y + s * 0.58f), w, roundf(s * 0.2f), C(col));
}

// Drive: body, slot and status light, pixel-snapped so they stay centred and crisp at any scale.
// dim (not mounted): solid colours faded towards the sidebar, so the slot keeps its contrast.
static void draw_drive_icon_ex(f32 x, f32 y, f32 s, bool dim) {
    u32 body = 0x9AA0AC, slot = 0x5C616B, light = 0x4FD18B;
    if (dim) {
        body = rgb_mix(body, COL_SIDEBAR, 0.45f);
        slot = rgb_mix(slot, COL_SIDEBAR, 0.35f);
        light = rgb_mix(light, COL_SIDEBAR, 0.6f);
    }
    f32 bx = roundf(x), bh = roundf(s * 0.5f), by = roundf(y + (s - bh) * 0.5f), bw = roundf(s);
    draw_rrect(bx, by, bw, bh, roundf(s * 0.12f), C(body));
    f32 d = MAX(2.f, roundf(s * 0.14f)), dy = by + roundf((bh - d) * 0.5f);
    draw_rrect(bx + bw - roundf(s * 0.16f) - d, dy, d, d, d * 0.5f, C(light)); // status light
    f32 lh = MAX(1.f, roundf(s * 0.1f)); // slot
    draw_rrect(bx + roundf(s * 0.16f), by + roundf((bh - lh) * 0.5f), roundf(s * 0.36f), lh, lh * 0.5f, C(slot));
}

static void draw_drive_icon(f32 x, f32 y, f32 s) { draw_drive_icon_ex(x, y, s, false); }

// Phone (upright body, screen, speaker slot) or camera (body, bump, lens), the drive icon's colours; dim: not connected.
static void draw_phone_icon(f32 x, f32 y, f32 s, bool camera, bool dim) {
    u32 body = 0x9AA0AC, screen = 0x5C616B, accent = 0x7FB2FF;
    if (dim) {
        body = rgb_mix(body, COL_SIDEBAR, 0.45f);
        screen = rgb_mix(screen, COL_SIDEBAR, 0.35f);
        accent = rgb_mix(accent, COL_SIDEBAR, 0.6f);
    }
    if (camera) {
        f32 bw = roundf(s), bh = roundf(s * 0.62f), bx = roundf(x), by = roundf(y + s * 0.26f);
        draw_rrect(bx + roundf(s * 0.18f), by - roundf(s * 0.12f), roundf(s * 0.3f), roundf(s * 0.2f), roundf(s * 0.05f), C(body));
        draw_rrect(bx, by, bw, bh, roundf(s * 0.12f), C(body));
        f32 d = roundf(s * 0.42f), lx = roundf(bx + (bw - d) * 0.5f), ly = roundf(by + (bh - d) * 0.5f);
        draw_rrect(lx, ly, d, d, d * 0.5f, C(screen));
        f32 d2 = roundf(s * 0.2f);
        draw_rrect(roundf(lx + (d - d2) * 0.5f), roundf(ly + (d - d2) * 0.5f), d2, d2, d2 * 0.5f, C(accent));
        return;
    }
    f32 bw = roundf(s * 0.58f), bh = roundf(s), bx = roundf(x + (s - bw) * 0.5f), by = roundf(y);
    draw_rrect(bx, by, bw, bh, roundf(s * 0.12f), C(body));
    f32 m = MAX(1.f, roundf(s * 0.08f)), top = roundf(s * 0.16f);
    draw_rrect(bx + m, by + top, bw - 2 * m, bh - top - roundf(s * 0.12f), roundf(s * 0.04f), C(screen));
    f32 sl = MAX(1.f, roundf(s * 0.06f)), sw = roundf(bw * 0.36f);
    draw_rrect(roundf(bx + (bw - sw) * 0.5f), by + roundf((top - sl) * 0.5f), sw, sl, sl * 0.5f, C(accent));
}

static void draw_chevron(f32 cx, f32 cy, f32 s, int dir, f32 t, Color c) {
    // dir: 0 left, 1 right, 2 up, 3 down
    f32 h = s * 0.5f, q = s * 0.25f;
    switch (dir) {
    case 0: draw_line(cx + q, cy - h, cx - q, cy, t, c); draw_line(cx - q, cy, cx + q, cy + h, t, c); break;
    case 1: draw_line(cx - q, cy - h, cx + q, cy, t, c); draw_line(cx + q, cy, cx - q, cy + h, t, c); break;
    case 2: draw_line(cx - h, cy + q, cx, cy - q, t, c); draw_line(cx, cy - q, cx + h, cy + q, t, c); break;
    case 3: draw_line(cx - h, cy - q, cx, cy + q, t, c); draw_line(cx, cy + q, cx + h, cy - q, t, c); break;
    }
}

static void draw_cross(f32 cx, f32 cy, f32 s, f32 t, Color c) {
    f32 h = s * 0.5f;
    draw_line(cx - h, cy - h, cx + h, cy + h, t, c);
    draw_line(cx - h, cy + h, cx + h, cy - h, t, c);
}

// Two bars (pause), or a play triangle when it's paused (resume), centred in r.
static void draw_pause_icon(Rect r, bool paused, Color c) {
    f32 cx = roundf(r.x + r.w * 0.5f), cy = roundf(r.y + r.h * 0.5f), h = roundf(S(9));
    if (paused) {
        f32 w = roundf(h * 0.85f);
        draw_tri(cx - w * 0.4f, cy - h * 0.5f, cx - w * 0.4f, cy + h * 0.5f, cx + w * 0.6f, cy, c);
    } else {
        f32 bw = MAX(2.f, roundf(S(2.6f))), gap = roundf(S(2.4f));
        draw_rect(cx - gap * 0.5f - bw, cy - h * 0.5f, bw, h, c);
        draw_rect(cx + gap * 0.5f, cy - h * 0.5f, bw, h, c);
    }
}

// Transfers, after Material's swap_vert on its 24-unit grid: the up arrow high on the left, the down arrow low on the
// right (their heads at opposite ends never meet), a little further apart than Material's so each arrow keeps its own
// column at small sizes; solid heads that stay legible at status-bar size. s = grid size.
static void draw_xfer_icon(f32 cx, f32 cy, f32 s, Color c) {
    f32 u = s / 24.f, t = MAX(1.f, roundf(S(1.4f)));
#define GX(v) (cx + ((v) - 12.f) * u)
#define GY(v) (cy + ((v) - 12.f) * u)
    f32 ux = roundf(GX(8.5f) - t * 0.5f), dxs = roundf(GX(15.5f) - t * 0.5f); // shafts on whole pixels, 7 units apart:
    f32 uc = ux + t * 0.5f, dc = dxs + t * 0.5f, hw = 3.5f * u;                  // the heads keep their own columns
    draw_rect(ux, roundf(GY(6.5f)), t, roundf(GY(19.5f)) - roundf(GY(6.5f)), c); // long shafts: most of the height
    draw_tri(uc - hw, GY(7), uc, GY(3), uc + hw, GY(7), c);
    draw_rect(dxs, roundf(GY(4.5f)), t, roundf(GY(17.5f)) - roundf(GY(4.5f)), c);
    draw_tri(dc - hw, GY(17), dc + hw, GY(17), dc, GY(21), c);
#undef GX
#undef GY
}

static void te_draw(TextEdit *t, Font *f, Rect r, f32 pad, Color tc) {
    ClipSave cs = draw_clip_push((SDL_Rect){(int)r.x + 1, (int)r.y, (int)r.w - 2, (int)r.h});
    int n = (int)strlen(t->buf);
    f32 cx = text_width(f, t->buf, t->caret);
    f32 avail = r.w - pad * 2 - S(2);
    if (cx - t->scroll > avail) t->scroll = cx - avail;
    if (cx - t->scroll < 0) t->scroll = cx;
    f32 tw = text_width(f, t->buf, n);
    if (tw - t->scroll < avail) t->scroll = MAX(0.f, tw - avail);
    f32 x = r.x + pad - t->scroll, ty = text_y(f, r.y, r.h);
    int lo = MIN(t->anchor, t->caret), hi = MAX(t->anchor, t->caret);
    if (lo != hi) {
        f32 a = text_width(f, t->buf, lo), b = text_width(f, t->buf, hi);
        draw_rect(roundf(x + a), r.y + roundf(S(4)), roundf(b - a), r.h - roundf(S(8)), C(COL_SEL_FOCUS));
    }
    draw_text(f, x, ty, t->buf, n, tc);
    draw_rect(roundf(x + cx), r.y + roundf(S(5)), MAX(1.f, roundf(S(1.2f))), r.h - roundf(S(10)), C(COL_ACCENT));
    draw_clip_pop(cs);
}

static void draw_split_icon(Rect sr, bool down, Color ic) {
    f32 iw = roundf(S(15)), ih = roundf(S(12)), ix = roundf(sr.x + (sr.w - iw) * 0.5f), iy = roundf(sr.y + (sr.h - ih) * 0.5f);
    f32 t = MAX(1.f, roundf(S(1.3f)));
    draw_rrect_outline(ix, iy, iw, ih, S(2.5f), MAX(1.f, S(1.3f)), ic);
    if (down) draw_rect(ix + 1, roundf(iy + ih * 0.5f), iw - 2, t, ic);
    else draw_rect(roundf(ix + iw * 0.5f), iy + 1, t, ih - 2, ic);
}

static void draw_panel_bar(Panel *pn) {
    Rect tb = pn->bar;
    bool active_panel = pn == A.ap, multi = A.npanels > 1;
    draw_rect(tb.x, tb.y, tb.w, tb.h, C(COL_TABBAR));
    Font *f = &A.font;
    ClipSave cs = draw_clip_push((SDL_Rect){(int)tb.x, (int)tb.y, (int)tb.w, (int)tb.h});
    Rect pr = pn->plus;
    // Backgrounds first, so the active card (and its flares) sits on top of a hovered neighbour:
    // active tab = card merging into the toolbar with outward-curving bottom corners (Chrome / Brave style);
    // hovered inactive tab = the same shape in a fainter colour.
    f32 rad = roundf(S(8));
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < A.ntabs; i++) {
            Tab *t = A.tabs[i];
            if (t->panel != pn) continue;
            Rect r = t->r;
            bool cur = pn->cur == t;
            bool hover = A.mouse_in && A.drag == DRAG_NONE && in_rect(r, A.mx, A.my);
            if (pass == 0 && !cur && hover) { // the active card's shape, faded halfway towards the tab bar
                f32 bot = tb.y + tb.h;
                Color hc = C(rgb_mix(COL_TABBAR, COL_TOOLBAR, 0.5f));
                draw_rrect(r.x, r.y, r.w, r.h + rad * 2, rad, hc);
                draw_flare(r.x - rad, bot - rad, rad, 0.f, r.x, bot, hc);
                draw_flare(r.x + r.w + rad, bot - rad, rad, (f32)M_PI * 0.5f, r.x + r.w, bot, hc);
            } else if (pass == 1 && cur) {
                f32 bot = tb.y + tb.h;
                // inactive panel of a split: the same dimming as its toolbar below (that overlay stops at the bar)
                u32 card = t->term ? term_bg_color() : COL_TOOLBAR; // a terminal tab merges into the terminal below
                if (multi && !active_panel && A.dim_inactive && !t->term) card = rgb_mix(card, 0x000000, 0.10f);
                if (t->term && fabsf(rgb_luma(card) - rgb_luma(COL_TABBAR)) < 0.09f) {
                    // a terminal colour close to the tab bar's (black on a dark theme...): outline the card and the
                    // bar's bottom edge so the tab still reads as a tab
                    Color bc = C(rgb_mix(COL_TABBAR, term_fg_color(), theme_is_light(&T) ? 0.22f : 0.16f));
                    draw_rect(tb.x, bot - 1, tb.w, 1, bc);
                    draw_rrect(r.x - 1, r.y - 1, r.w + 2, r.h + rad * 2 + 1, rad + 1, bc);
                    draw_flare(r.x - rad, bot - rad, rad - 1, 0.f, r.x, bot, bc);
                    draw_flare(r.x + r.w + rad, bot - rad, rad - 1, (f32)M_PI * 0.5f, r.x + r.w, bot, bc);
                }
                draw_rrect(r.x, r.y, r.w, r.h + rad * 2, rad, C(card)); // card merging into the toolbar
                // flares: concave corners curving out into the toolbar (drawn over a hovered neighbour)
                draw_flare(r.x - rad, bot - rad, rad, 0.f, r.x, bot, C(card));
                draw_flare(r.x + r.w + rad, bot - rad, rad, (f32)M_PI * 0.5f, r.x + r.w, bot, C(card));
            }
        }
    Tab *prev = NULL;
    for (int i = 0; i < A.ntabs; i++) {
        Tab *t = A.tabs[i];
        if (t->panel != pn) continue;
        Rect r = t->r;
        Pane *p = &t->pane[0];
        bool cur = pn->cur == t;
        bool hover = A.mouse_in && A.drag == DRAG_NONE && in_rect(r, A.mx, A.my);
        bool dragging = A.drag == DRAG_TAB && A.drag_tab == t;
        if (prev && !cur && pn->cur != prev && !hover) draw_rect(r.x - 1, r.y + S(9), 1, r.h - S(18), C(COL_FIELD));
        prev = t;
        if (A.drop_tab == t) draw_rrect_outline(r.x + S(1), r.y + S(1), r.w - S(2), r.h - S(4), S(6), MAX(1.f, S(1.3f)), C(COL_ACCENT));
        if (dragging) g_alpha = 0.45f;
        f32 is = roundf(S(15)), ix = roundf(r.x + S(12)), iy = roundf(r.y + (r.h - is) * 0.5f);
        char dnb[NAME_MAX + 1];
        const char *label = t->term ? term_label(t->term) : disp_name(p->path, dnb, sizeof dnb);
        // the active terminal tab sits on the terminal's own colour: use its foreground so the text stays readable
        bool on_term = t->term && cur;
        u32 tfg = on_term ? term_fg_color() : 0, tbg = on_term ? term_bg_color() : 0;
        if (t->term) { // a small terminal: rounded box with ">_"
            Color tcc = on_term ? C(rgb_mix(tfg, tbg, 0.35f)) : C(COL_TEXT_DIM);
            draw_rrect_outline(ix, iy + is * 0.1f, is, is * 0.8f, S(2.5f), MAX(1.f, S(1.2f)), tcc);
            f32 lt = MAX(1.f, roundf(S(1.3f)));
            draw_line(ix + is * 0.25f, iy + is * 0.35f, ix + is * 0.42f, iy + is * 0.5f, lt, tcc);
            draw_line(ix + is * 0.42f, iy + is * 0.5f, ix + is * 0.25f, iy + is * 0.65f, lt, tcc);
            draw_rect(roundf(ix + is * 0.5f), roundf(iy + is * 0.62f), roundf(is * 0.26f), lt, tcc);
        } else draw_path_icon(ix, iy, is, p->path);
        f32 csz = roundf(S(18));
        bool show_close = A.ntabs > 1 && (cur || hover) && r.w > S(90);
        f32 tx = ix + is + roundf(S(8));
        f32 maxw = r.x + r.w - tx - (show_close ? csz + S(12) : S(10));
        Color tc = cur ? (active_panel || !multi ? C(COL_TEXT) : C(COL_TEXT2)) : C(COL_TEXT_DIM);
        if (on_term) tc = C(active_panel || !multi ? tfg : rgb_mix(tfg, tbg, 0.25f));
        draw_text_fit(f, tx, text_y(f, r.y, r.h), maxw, label, (int)strlen(label), tc, 0);
        if (show_close) {
            Rect c = {r.x + r.w - csz - roundf(S(8)), r.y + (r.h - csz) * 0.5f, csz, csz};
            bool ch = A.mouse_in && in_rect(c, A.mx, A.my);
            if (ch) draw_rrect(c.x, c.y, c.w, c.h, S(4), on_term ? C(rgb_mix(tbg, tfg, 0.14f)) : C(COL_CHIP));
            Color xc = on_term ? C(ch ? tfg : rgb_mix(tfg, tbg, 0.35f)) : C(ch ? COL_TEXT : COL_TEXT_DIM);
            draw_cross(c.x + c.w * 0.5f, c.y + c.h * 0.5f, S(7), MAX(1.f, S(1.3f)), xc);
        }
        g_alpha = 1.f;
    }
    // + and split buttons (+'s highlight after the tab backgrounds: the active tab's flare can reach under it)
    if (A.mouse_in && A.drag == DRAG_NONE && in_rect(pr, A.mx, A.my)) draw_rrect(pr.x, pr.y, pr.w, pr.h, pr.w * 0.5f, C(COL_TAB_HOVER));
    draw_plus(pr.x + pr.w * 0.5f, pr.y + pr.h * 0.5f, S(12), MAX(2.f, S(1.8f)), C(COL_TEXT_DIM));
    Rect sr = pn->splitbtn;
    bool sh = A.mouse_in && in_rect(sr, A.mx, A.my);
    if (sh) draw_rrect(sr.x, sr.y, sr.w, sr.h, S(6), C(COL_HOVER));
    draw_split_icon(sr, sh && (SDL_GetModState() & SDL_KMOD_SHIFT), sh ? C(COL_TEXT) : C(COL_TEXT_DIM));
    // insertion caret while dragging a tab over this bar
    if (A.drag == DRAG_TAB && A.tdrop_kind == TD_BAR && A.tdrop_panel == pn) {
        Tab *lt2 = panel_tab(pn, -1);
        f32 x = A.tdrop_before ? A.tdrop_before->r.x : (lt2 ? lt2->r.x + lt2->r.w : tb.x + S(8));
        draw_rrect(x - S(1.5f), tb.y + S(7), S(3), tb.h - S(12), S(1.5f), C(COL_ACCENT));
    }
    draw_clip_pop(cs);
}

static void draw_node_dividers(Node *n) {
    if (!n || n->kind == NODE_PANEL) return;
    bool h = A.drag_node == n || (A.drag == DRAG_NONE && A.mouse_in && in_rect(n->div, A.mx, A.my) && node_div_at(A.root, A.mx, A.my) == n);
    Color c = h ? C(COL_ACCENT) : C(COL_DIVIDER);
    if (n->kind == NODE_HSPLIT) draw_rect(n->b->r.x - 1, n->r.y, 1, n->r.h, c);
    else draw_rect(n->r.x, n->b->r.y - 1, n->r.w, 1, c);
    draw_node_dividers(n->a);
    draw_node_dividers(n->b);
}

// Snap-assist overlay while dragging a tab.
static void draw_tab_drop(void) {
    if (A.drag != DRAG_TAB || !A.drag_tab) return;
    Panel *pn = A.tdrop_panel;
    if (pn && (A.tdrop_kind == TD_CENTER || A.tdrop_kind == TD_EDGE)) {
        Rect a = {pn->area.x, pn->bar.y + pn->bar.h, pn->area.w, pn->area.h - pn->bar.h};
        Rect z = a;
        if (A.tdrop_kind == TD_EDGE) {
            if (A.tdrop_zone == ZONE_LEFT) z.w = roundf(a.w * 0.5f);
            else if (A.tdrop_zone == ZONE_RIGHT) { z.w = roundf(a.w * 0.5f); z.x = a.x + a.w - z.w; }
            else if (A.tdrop_zone == ZONE_TOP) z.h = roundf(a.h * 0.5f);
            else { z.h = roundf(a.h * 0.5f); z.y = a.y + a.h - z.h; }
        }
        f32 in = roundf(S(6));
        draw_rrect(z.x + in, z.y + in, z.w - in * 2, z.h - in * 2, S(10), CA(COL_ACCENT, 0.18f));
        draw_rrect_outline(z.x + in, z.y + in, z.w - in * 2, z.h - in * 2, S(10), MAX(1.f, S(2)), CA(COL_ACCENT, 0.9f));
    }
    // ghost of the dragged tab under the cursor
    Pane *p = &A.drag_tab->pane[0];
    const char *label = p->path[1] ? path_basename(p->path) : "/";
    Font *f = &A.font;
    f32 w = MIN(S(220), text_width(f, label, (int)strlen(label)) + S(48)), h = roundf(S(30));
    f32 x = roundf(A.mx - S(20)), y = roundf(A.my - h * 0.5f);
    draw_rrect(x + S(1), y + S(4), w, h, S(7), CA(0x000000, 0.4f));
    draw_rrect(x, y, w, h, S(7), C(COL_TOOLBAR));
    draw_rrect_outline(x, y, w, h, S(7), 1, C(COL_ACCENT));
    f32 is = roundf(S(15));
    draw_path_icon(roundf(x + S(10)), roundf(y + (h - is) * 0.5f), is, p->path);
    draw_text_fit(f, x + S(32), text_y(f, y, h), w - S(40), label, (int)strlen(label), C(COL_TEXT), 0);
}

// Git branch glyph (ICON_BRANCH, an SVG tinted with ic): left edge x, centred on cy, about S(12) wide.
static void draw_branch_icon(f32 x, f32 cy, Color ic) {
    int px = (int)roundf(S(14));
    Glyph *g = icon_glyph(ICON_BRANCH, px);
    if (!g) return;
    f32 margin = floorf((f32)px * (1.f - 577.f / 677.f) * 0.5f); // the square raster is wider than the glyph
    draw_glyph(g, roundf(x - margin), roundf(cy - (f32)px * 0.5f), ic);
}

static void draw_toolbar(Pane *p, bool active, bool split) {
    PaneLayout *ly = &p->ly;
    Rect tb = ly->toolbar;
    draw_rect(tb.x, tb.y, tb.w, tb.h, C(COL_TOOLBAR));
    Rect btn[3] = {ly->back, ly->fwd, ly->up};
    bool enabled[3] = {p->hist_pos > 0, p->hist_pos < p->hist_n - 1, p->path[1] != 0};
    for (int i = 0; i < 3; i++) {
        Rect r = btn[i];
        bool hover = A.mouse_in && in_rect(r, A.mx, A.my) && enabled[i];
        if (hover) draw_rrect(r.x, r.y, r.w, r.h, S(6), C(COL_FIELD_HOVER));
        Color c = enabled[i] ? C(COL_TEXT) : C(COL_TEXT_FAINT);
        draw_chevron(r.x + r.w * 0.5f, r.y + r.h * 0.5f, S(11), i, MAX(1.5f, S(1.6f)), c);
    }

    {   // type filter (funnel)
        Rect fb = ly->filterbtn;
        bool on = p->tf_mode || p->tf_exts[0], hv = A.mouse_in && in_rect(fb, A.mx, A.my);
        if (on || hv) draw_rrect(fb.x, fb.y, fb.w, fb.h, S(6), on ? CA(COL_ACCENT, 0.22f) : C(COL_FIELD_HOVER));
        Color fc = on ? C(COL_ACCENT) : C(COL_TEXT_DIM);
        f32 cx = fb.x + fb.w * 0.5f, cy = fb.y + fb.h * 0.5f, t = MAX(1.4f, S(1.5f));
        draw_line(cx - S(7), cy - S(5), cx + S(7), cy - S(5), t, fc);
        draw_line(cx - S(7), cy - S(5), cx - S(1.5f), cy + S(1), t, fc);
        draw_line(cx + S(7), cy - S(5), cx + S(1.5f), cy + S(1), t, fc);
        draw_line(cx - S(1.5f), cy + S(1), cx - S(1.5f), cy + S(6), t, fc);
        draw_line(cx + S(1.5f), cy + S(1), cx + S(1.5f), cy + S(4.5f), t, fc);
    }
    Rect pb = ly->pathbar;
    bool editing = A.edit_pane == p;
    bool hover = A.mouse_in && in_rect(pb, A.mx, A.my);
    draw_rrect(pb.x, pb.y, pb.w, pb.h, S(7), C(editing ? COL_FIELD_HOVER : COL_FIELD));
    if (editing) draw_rrect_outline(pb.x, pb.y, pb.w, pb.h, S(7), MAX(1.f, S(1.2f)), C(COL_ACCENT));

    Font *f = &A.font;
    f32 ty = text_y(f, pb.y, pb.h);
    if (editing) {
        te_draw(&A.edit, f, (Rect){pb.x + 2, pb.y + S(2), pb.w - 4, pb.h - S(4)}, roundf(S(8)), C(COL_TEXT));
        return;
    }
    SDL_Rect clip = {(int)pb.x, (int)pb.y, (int)pb.w, (int)pb.h};
    draw_clip(&clip);
    layout_crumbs(p);
    f32 sep_w = roundf(S(16));
    for (int i = 0; i < ly->ncrumbs; i++) {
        Crumb *c = &ly->crumbs[i];
        bool last = i == ly->ncrumbs - 1;
        bool h = hover && in_rect(c->r, A.mx, A.my);
        if (h) draw_rrect(c->r.x, c->r.y, c->r.w, c->r.h, S(5), C(COL_CHIP));
        if (i == 0 && ly->crumbs_trash) {
            f32 s = roundf(S(16)), ix = roundf(c->r.x + S(8));
            draw_icon_id(ix, roundf(c->r.y + (c->r.h - s) * 0.5f), s, icon_find("trash"), false);
            draw_text(f, ix + s + roundf(S(8)), ty, "Trash", 5, C(last ? COL_TEXT : COL_TEXT_DIM));
        } else if (i == 0) {
            f32 s = roundf(S(16));
            draw_drive_icon(roundf(c->r.x + (c->r.w - s) * 0.5f), roundf(c->r.y + (c->r.h - s) * 0.5f), s);
        } else {
            int start = c->end;
            while (start > 0 && p->path[start - 1] != '/') start--;
            int len = c->end - start;
            if (len > 2 && !strncmp(p->path + c->end - 2, "::", 2)) len -= 2; // archive root
            draw_text_fit(f, c->r.x + roundf(S(8)), ty, pb.x + pb.w - c->r.x - S(12), p->path + start,
                          len, C(last ? COL_TEXT : COL_TEXT_DIM), 0);
        }
        if (!last) {
            f32 sx = c->r.x + c->r.w + sep_w * 0.5f;
            if (i == 0 && ly->crumbs_elided) {
                draw_chevron(sx, pb.y + pb.h * 0.5f, S(7), 1, MAX(1.f, S(1.2f)), C(COL_TEXT_FAINT));
                f32 ex = c->r.x + c->r.w + sep_w + roundf(S(8));
                draw_text(f, ex, ty, ELLIPSIS, 3, C(COL_TEXT_DIM));
                sx = ly->crumbs[i + 1].r.x - sep_w * 0.5f;
            }
            draw_chevron(sx, pb.y + pb.h * 0.5f, S(7), 1, MAX(1.f, S(1.2f)), C(COL_TEXT_FAINT));
        }
    }
    char gt[200];
    bool dirty;
    ly->has_gitchip = false;
    f32 cw = git_chip_width(p);
    if (cw > 0 && cw < pb.w * 0.5f && git_chip_text(p, gt, sizeof gt, &dirty)) { // branch chip at the right end
        Font *fs = &A.font_small;
        Rect c = {pb.x + pb.w - cw - roundf(S(3)), pb.y + roundf(S(3)), cw, pb.h - roundf(S(6))};
        ly->gitchip = c;
        ly->has_gitchip = true;
        bool hv = hover && in_rect(c, A.mx, A.my), on = A.menu.open && A.menu.pane == p && A.menu.n && A.menu.items[0].info;
        draw_rrect(c.x, c.y, c.w, c.h, S(5), on ? CA(COL_ACCENT, 0.25f) : C(hv ? COL_CONTROL_HOVER : COL_CHIP));
        draw_branch_icon(c.x + S(6), c.y + c.h * 0.5f, C(COL_TEXT_DIM));
        f32 tx = c.x + S(22);
        draw_text_fit(fs, tx, text_y(fs, c.y, c.h), c.x + c.w - S(20) - tx, gt, (int)strlen(gt), C(COL_TEXT2), 0);
        f32 dr = roundf(S(2.5f)); // dot: changes in the repository (orange) or clean (green)
        draw_rrect(c.x + c.w - S(8) - dr, c.y + c.h * 0.5f - dr, dr * 2, dr * 2, dr, C(dirty ? COL_WARN : COL_OK));
    }
    draw_clip(NULL);
}

static bool sidebar_edge_hot(void) { return A.drag == DRAG_SIDEBAR || (A.drag == DRAG_NONE && A.mouse_in && in_rect(A.r_sdiv, A.mx, A.my)); }

// Brand header: logo + app name, pinned above the (scrolling) sidebar.
static void draw_brand(void) {
    Rect r = A.r_brand;
    draw_rect(r.x, r.y, r.w, r.h, C(COL_SIDEBAR));
    draw_rect(r.x + r.w - 1, r.y, 1, r.h, C(sidebar_edge_hot() ? COL_ACCENT : COL_BORDER));
    f32 lh = roundf(S(17)), lw = roundf(lh * 624.f / 456.f), lx = roundf(r.x + S(18)), ly = roundf(r.y + (r.h - lh) * 0.5f);
    Glyph *g = icon_glyph(lh < 48 ? ICON_LOGO_SMALL : ICON_LOGO, (int)lw); // square raster, logo centred in it
    if (g) draw_glyph(g, lx, roundf(ly - (lw - lh) * 0.5f), (Color){1, 1, 1, 1});
    Font *f = &A.font_brand;
    f32 tx = lx + lw + roundf(S(8));
    draw_text_fit(f, tx, text_y(f, r.y, r.h), r.x + r.w - tx - S(8), APP_LABEL, (int)strlen(APP_LABEL), C(COL_TEXT), 0);
}

// Input method: the text being composed, in a box under the focused field (underlined, like other apps show it).
static void draw_preedit(void) {
    if (!g_preedit[0]) return;
    Rect f = ime_field();
    Font *fn = &A.font;
    int n = (int)strlen(g_preedit);
    f32 pad = roundf(S(8)), w = text_width(fn, g_preedit, n) + pad * 2, h = roundf(fn->line_h + S(10));
    f32 x = MIN(f.x, (f32)A.W - w - S(4)), y = f.y + f.h + S(2);
    if (y + h > A.H) y = f.y - h - S(2);
    draw_rrect(x, y, w, h, S(5), C(COL_FIELD));
    draw_rrect_outline(x, y, w, h, S(5), MAX(1.f, S(1)), C(COL_BORDER));
    draw_text(fn, x + pad, text_y(fn, y, h), g_preedit, n, C(COL_TEXT));
    draw_rect(x + pad, y + h - roundf(S(5)), w - pad * 2, MAX(1.f, roundf(S(1))), C(COL_ACCENT));
}

// Own title bar: minimise / maximise / close at the top right (Windows-sized, red close on hover), the strip above
// the preview, and a hairline round the window (borderless windows get no shadow from some compositors).
static void draw_window_controls(void) {
    if (!A.csd) return;
    Rect s = A.r_wstrip;
    if (s.w > 0) {
        draw_rect(s.x, s.y, s.w, s.h, C(COL_TABBAR));
        draw_rect(s.x, s.y, 1, s.h, C(COL_BORDER));
    }
    bool max = window_maximized();
    f32 lt = MAX(1.f, roundf(S(1))), is = roundf(S(10));
    for (int i = 0; i < 3; i++) {
        Rect b = wctl_button(i);
        bool hover = A.mouse_in && A.drag == DRAG_NONE && in_rect(b, A.mx, A.my);
        if (hover) draw_rect(b.x, b.y, b.w, b.h, i == 2 ? (Color){0xC4 / 255.f, 0x2B / 255.f, 0x1C / 255.f, 1} : C(COL_TAB_HOVER));
        Color ic = hover ? (i == 2 ? (Color){1, 1, 1, 1} : C(COL_TEXT)) : C(COL_TEXT2);
        f32 cx = roundf(b.x + b.w * 0.5f), cy = roundf(b.y + b.h * 0.5f), x0 = cx - is * 0.5f, y0 = cy - is * 0.5f;
        if (i == 0) draw_rect(x0, cy, is, lt, ic);
        else if (i == 1 && !max) draw_rrect_outline(x0, y0, is, is, S(1.5f), lt, ic);
        else if (i == 1) { // restore: two overlapping windows
            f32 o = roundf(S(2)), w2 = is - o;
            draw_rect(x0 + o, y0, w2, lt, ic);
            draw_rect(x0 + is - lt, y0, lt, w2, ic);
            draw_rrect_outline(x0, y0 + o, w2, w2, S(1.5f), lt, ic);
        } else draw_cross(cx, cy, is, lt, ic);
    }
    if (!max) {
        Color bc = C(COL_BORDER);
        f32 W = (f32)A.W, H = (f32)A.H;
        draw_rect(0, 0, W, 1, bc);
        draw_rect(0, H - 1, W, 1, bc);
        draw_rect(0, 0, 1, H, bc);
        draw_rect(W - 1, 0, 1, H, bc);
    }
}

// Status at an icon's corner (cloud folders, add-on rows): green up, amber connecting, red retrying / failed; 0 = none.
static void draw_status_dot(f32 ix, f32 iy, f32 is, u32 dot, bool hover) {
    if (!dot) return;
    f32 dr = roundf(S(3.5f)), dx = ix + is - dr * 1.3f, dy = iy + is - dr * 1.3f, ring = MAX(1.f, roundf(S(1.5f)));
    draw_rrect(dx - ring, dy - ring, (dr + ring) * 2, (dr + ring) * 2, dr + ring, C(hover ? COL_HOVER : COL_SIDEBAR));
    draw_rrect(dx, dy, dr * 2, dr * 2, dr, C(dot));
}

static void draw_sidebar(void) {
    Pane *p = active_pane();
    Rect sb = A.r_sidebar;
    draw_rect(sb.x, sb.y, sb.w, sb.h, C(COL_SIDEBAR));
    draw_rect(sb.x + sb.w - 1, sb.y, 1, sb.h, C(sidebar_edge_hot() ? COL_ACCENT : COL_BORDER));
    Font *fs = &A.font_small, *f = &A.font;
    ClipSave cs = draw_clip_push((SDL_Rect){(int)sb.x, (int)sb.y, (int)sb.w - 1, (int)sb.h});
    bool dragging_place = A.drag == DRAG_PLACE;
    for (int i = 0; i < A.nplaces; i++) {
        Place *pl = &A.places[i];
        Rect r = place_rect(i);
        if (r.y + r.h < sb.y || r.y > sb.y + sb.h) continue;
        bool hover = A.mouse_in && in_rect(r, A.mx, A.my) && (A.drag == DRAG_NONE || A.drag == DRAG_ITEMS);
        bool editing = place_editing(pl);
        if (place_is_header(pl)) {
            if (A.drop_place == i) {
                draw_rrect(r.x, r.y + 1, r.w, r.h - 2, S(6), CA(COL_ACCENT, 0.25f));
                draw_rrect_outline(r.x, r.y + 1, r.w, r.h - 2, S(6), MAX(1.f, S(1.2f)), C(COL_ACCENT));
            } else if (hover) {
                draw_rrect(r.x, r.y + 1, r.w, r.h - 2, S(6), C(COL_HOVER));
            }
            f32 tx = r.x + S(10), tmax = r.x + r.w - S(6);
            pl->eject = (Rect){0, 0, 0, 0};
            if (pl->kind == PK_NETHDR || pl->kind == PK_CLOUDHDR || pl->kind == PK_ADDONHDR) { // + : connect to a server / rclone config / add-on
                f32 bs = roundf(S(20));
                Rect pr = {roundf(r.x + r.w - bs - S(4)), roundf(r.y + (r.h - bs) * 0.5f), bs, bs};
                pl->eject = pr;
                bool ph = A.mouse_in && in_rect(pr, A.mx, A.my);
                if (ph) draw_rrect(pr.x, pr.y, pr.w, pr.h, S(5), C(COL_CHIP));
                draw_plus(pr.x + bs * 0.5f, pr.y + bs * 0.5f, S(10), MAX(2.f, S(1.6f)), C(ph ? COL_TEXT : hover ? COL_TEXT_DIM : COL_TEXT_FAINT));
                tmax = pr.x - S(4);
            }
            { // collapse chevron
                bool col = pl->kind == PK_GROUP ? A.qa[pl->group].collapsed : pl->kind == PK_DEVICES ? A.devs_collapsed
                         : pl->kind == PK_ADDONHDR ? addon_section_collapsed(pl) : pl->kind == PK_CLOUDHDR ? g_cloud_collapsed : A.nets_collapsed;
                draw_chevron(r.x + S(9), r.y + r.h * 0.5f, S(4), col ? 1 : 3, MAX(1.f, S(1.3f)), C(hover ? COL_TEXT_DIM : COL_TEXT_FAINT));
                tx = r.x + S(18);
            }
            if (editing) {
                Rect er = {tx - S(4), r.y + S(2), r.x + r.w - tx, r.h - S(4)};
                draw_rrect(er.x, er.y, er.w, er.h, S(5), C(COL_FIELD_HOVER));
                draw_rrect_outline(er.x, er.y, er.w, er.h, S(5), MAX(1.f, S(1.2f)), C(COL_ACCENT));
                te_draw(&A.qa_te, fs, er, roundf(S(4)), C(COL_TEXT));
            } else {
                draw_text_fit(fs, tx, text_y(fs, r.y, r.h), tmax - tx, pl->label, (int)strlen(pl->label),
                              C(hover ? COL_TEXT_DIM : COL_TEXT_FAINT), 0);
            }
            continue;
        }
        if (pl->kind == PK_ADDON) { addon_place_draw(pl, r, hover); continue; }
        if (pl->kind == PK_NETEMPTY || pl->kind == PK_ADDONEMPTY || pl->kind == PK_CLOUDEMPTY) { // placeholder text, not a row you can use
            draw_text_fit(fs, r.x + S(12), text_y(fs, r.y, r.h), r.w - S(18), pl->label, (int)strlen(pl->label), C(COL_TEXT_FAINT), 0);
            continue;
        }
        bool active = !strcmp(pl->path, p->path);
        bool lifted = dragging_place && A.qa_drag_row == i;
        if (A.drop_place == i) {
            draw_rrect(r.x, r.y + 1, r.w, r.h - 2, S(6), CA(COL_ACCENT, 0.25f));
            draw_rrect_outline(r.x, r.y + 1, r.w, r.h - 2, S(6), MAX(1.f, S(1.2f)), C(COL_ACCENT));
        } else if (active) draw_rrect(r.x, r.y + 1, r.w, r.h - 2, S(6), C(rgb_mix(COL_SEL_IDLE, COL_ACCENT, 0.16f))); // accent-tinted
        else if (hover && A.drag == DRAG_NONE) draw_rrect(r.x, r.y + 1, r.w, r.h - 2, S(6), C(COL_HOVER));
        if (active) { // accent edge: the left strip of the row's own rounded shape, full height
            ClipSave ac = draw_clip_push((SDL_Rect){(int)r.x, (int)r.y, (int)roundf(S(3)), (int)r.h});
            draw_rrect(r.x, r.y + 1, r.w, r.h - 2, S(6), C(COL_ACCENT));
            draw_clip_pop(ac);
        }
        if (lifted) g_alpha = 0.45f;
        f32 is = roundf(S(16)), ix = roundf(r.x + S(12)), iy = roundf(r.y + (r.h - is) * 0.5f);
        pl->eject = (Rect){0, 0, 0, 0};
        pl->usage = (Rect){0, 0, 0, 0};
        bool show_eject = pl->removable && hover && A.drag == DRAG_NONE && pl->kind != PK_UNMOUNTED;
        bool phone_off = pl->kind == PK_PHONE && !pl->path[0]; // not connected yet: dim, "Connect" on hover
        Cloud *cl = pl->kind == PK_CLOUD && pl->item < g_ncloud ? &g_cloud[pl->item] : NULL;
        bool cloud_off = cl && !pl->path[0];
        if (pl->kind == PK_HOME) draw_dir_icon(ix, iy, is, "home", false);
        else if (pl->kind == PK_DRIVE) draw_drive_icon(ix, iy, is);
        else if (pl->kind == PK_UNMOUNTED) draw_drive_icon_ex(ix, iy, is, true);
        else if (pl->kind == PK_PHONE)
            draw_phone_icon(ix, iy, is, pl->phone >= 0 && pl->phone < A.devs.nph && A.devs.ph[pl->phone].kind == 'c', phone_off);
        else if (pl->kind == PK_NETWORK || pl->kind == PK_NETSAVED) // the server's chosen icon (saved, not connected: faded)
            draw_net_icon(ix, iy, is, pl->server >= 0 && pl->server < A.nservers ? A.servers[pl->server].icon : "",
                          pl->kind == PK_NETSAVED ? 0.5f : 1.f);
        else if (cl) { // the service's icon (not connected: faded) + a status dot while connecting / failing
            draw_net_icon(ix, iy, is, cloud_icon(cl), cloud_off && cl->state != CS_FAILED && cl->state != CS_RETRY ? 0.5f : 1.f);
            draw_status_dot(ix, iy, is, cloud_busy(cl) && cl->state != CS_RETRY ? 0xE8B04B : cl->state == CS_RETRY || cl->state == CS_FAILED ? 0xE5534B : 0, hover);
        } else if (pl->kind == PK_TRASH) draw_path_icon(ix, iy, is, pl->path);
        else draw_dir_icon(ix, iy, is, path_basename(pl->path), false);
        f32 lx = ix + is + S(10), lmax = r.x + r.w - lx - S(6);
        f32 bar_end = r.x + r.w - S(8); // usage bar's right end (shortened when the eject button shows)
        if (show_eject) { // eject / disconnect button: takes the free-space text's place, nothing else moves
            f32 bs = roundf(S(22));
            Rect er = {r.x + r.w - bs - S(4), roundf(r.y + (r.h - bs) * 0.5f), bs, bs};
            bar_end = er.x - S(6);
            pl->eject = er;
            bool eh = in_rect(er, A.mx, A.my);
            if (eh) draw_rrect(er.x, er.y, er.w, er.h, S(5), C(COL_CHIP));
            f32 cx = er.x + bs * 0.5f, cy = er.y + bs * 0.5f, w2 = S(5);
            Color ec = C(eh ? COL_TEXT : COL_TEXT_DIM);
            for (f32 k = 0; k < S(5); k += 1.f) // triangle
                draw_rect(roundf(cx - w2 * (k / S(5))), roundf(cy - S(4) + k), roundf(w2 * 2 * (k / S(5))), 1, ec);
            draw_rect(roundf(cx - w2), roundf(cy + S(3)), roundf(w2 * 2), MAX(1.f, roundf(S(1.6f))), ec);
        }
        if (pl->kind == PK_TRASH && A.trash_known && A.trash_size > 0) { // how much is in it
            char sz[32];
            fmt_size(sz, sizeof sz, A.trash_size);
            f32 tw = text_width(fs, sz, (int)strlen(sz));
            draw_text(fs, r.x + r.w - S(8) - tw, text_y(fs, r.y, r.h), sz, (int)strlen(sz), C(COL_TEXT_FAINT));
            lmax -= tw + S(10);
        }
        if (cloud_off && (hover || cloud_busy(cl))) { // "Connect" on hover, "Connecting..." while it does
            const char *t = cloud_busy(cl) ? "Connecting\xE2\x80\xA6" : "Connect";
            f32 tw = text_width(fs, t, (int)strlen(t));
            draw_text(fs, r.x + r.w - S(8) - tw, text_y(fs, r.y, r.h), t, (int)strlen(t), C(cloud_busy(cl) ? COL_TEXT_FAINT : COL_ACCENT));
            lmax -= tw + S(8);
        }
        if ((pl->kind == PK_NETSAVED || phone_off) && hover) { // saved server, phone: "Connect" on hover
            draw_text(fs, r.x + r.w - S(8) - text_width(fs, "Connect", 7), text_y(fs, r.y, r.h), "Connect", 7, C(COL_ACCENT));
            lmax -= text_width(fs, "Connect", 7) + S(8);
        }
        if (pl->kind == PK_UNMOUNTED) { // not mounted: dim, size on the right
            char sz[32]; // unlabelled volumes already show their size: show the filesystem instead
            BlockDev *bd = pl->dev >= 0 ? &A.devs.d[pl->dev] : NULL;
            if (bd && !bd->label[0]) {
                size_t k = 0;
                for (const char *c = bd->fstype; *c && k + 1 < sizeof sz; c++) sz[k++] = (char)toupper((u8)*c);
                sz[k] = 0;
            } else fmt_size(sz, sizeof sz, bd ? bd->size : 0);
            f32 fw = text_width(fs, sz, (int)strlen(sz));
            if (!hover) draw_text(fs, r.x + r.w - S(8) - fw, text_y(fs, r.y, r.h), sz, (int)strlen(sz), C(COL_TEXT_FAINT));
            else draw_text(fs, r.x + r.w - S(8) - text_width(fs, "Mount", 5), text_y(fs, r.y, r.h), "Mount", 5, C(COL_ACCENT));
            lmax -= fw + S(8);
        }
        bool has_bar = false;
        if (pl->kind == PK_DRIVE) { // drive: usage bar + free space
            static struct { char path[PATH_MAX]; f64 used; u64 avail; u64 t; } du[16];
            int k = i % 16;
            u64 now = now_ns();
            if ((strcmp(du[k].path, pl->path) || now - du[k].t > 5000000000ull) && path_is_remote(pl->path)) {
                snprintf(du[k].path, sizeof du[k].path, "%s", pl->path);
                du[k].t = now;
                du[k].used = -1; // network drive: no usage bar (statvfs could hang)
            } else if (strcmp(du[k].path, pl->path) || now - du[k].t > 5000000000ull) {
                struct statvfs vs;
                snprintf(du[k].path, sizeof du[k].path, "%s", pl->path);
                du[k].t = now;
                if (statvfs(pl->path, &vs) == 0 && vs.f_blocks) {
                    du[k].used = 1.0 - (f64)vs.f_bavail / (f64)vs.f_blocks;
                    du[k].avail = (u64)vs.f_bavail * vs.f_frsize;
                } else du[k].used = -1;
            }
            if (du[k].used >= 0) {
                has_bar = true;
                char fr[32];
                fmt_size(fr, sizeof fr, du[k].avail);
                f32 fw = text_width(fs, fr, (int)strlen(fr));
                if (!show_eject) draw_text(fs, r.x + r.w - S(8) - fw, text_y(fs, r.y - S(3), r.h), fr, (int)strlen(fr), C(COL_TEXT_FAINT));
                lmax -= fw + S(8); // same room either way: the name doesn't jump when the button appears
                f32 bw = MAX(S(10), bar_end - lx), by2 = r.y + r.h - roundf(S(7));
                pl->usage = (Rect){lx - S(2), by2 - S(5), bw + S(4), S(12)};
                bool uh = A.drag == DRAG_NONE && in_rect(pl->usage, A.mx, A.my); // click: disk usage
                f32 bt = roundf(S(uh ? 5 : 3));
                by2 -= uh ? roundf(S(1)) : 0;
                draw_rrect(lx, by2, bw, bt, S(1.5f), C(COL_TRACK));
                draw_rrect(lx, by2, MAX(S(3), (f32)(bw * du[k].used)), bt, S(1.5f),
                           C(du[k].used > 0.9 ? COL_DANGER : du[k].used > 0.75 ? COL_WARN : COL_ACCENT));
            }
        }
        if (editing) {
            Rect er = {lx - S(4), r.y + S(3), r.x + r.w - lx, r.h - S(6)};
            draw_rrect(er.x, er.y, er.w, er.h, S(5), C(COL_FIELD_HOVER));
            draw_rrect_outline(er.x, er.y, er.w, er.h, S(5), MAX(1.f, S(1.2f)), C(COL_ACCENT));
            te_draw(&A.qa_te, f, er, roundf(S(4)), C(COL_TEXT));
        } else {
            if (show_eject && !has_bar) lmax -= roundf(S(22)) + S(4); // button next to a plain name
            draw_text_fit(f, lx, text_y(f, r.y - (has_bar ? S(3) : 0), r.h), lmax, pl->label,
                          (int)strlen(pl->label), C(active ? COL_TEXT : pl->kind == PK_UNMOUNTED || phone_off ? COL_TEXT_DIM : COL_TEXT2), 0);
        }
        g_alpha = 1.f;
    }
    // insertion line while reordering
    if (dragging_place && A.qa_ins_g >= 0) {
        f32 ly = -1;
        int last = -1;
        for (int i = 0; i < A.nplaces_user; i++) {
            Place *pl = &A.places[i];
            if (pl->group != A.qa_ins_g) continue;
            if (pl->kind != PK_GROUP && pl->item >= A.qa_ins_i) { ly = place_rect(i).y; break; }
            last = i;
        }
        if (ly < 0 && last >= 0) { Rect lr = place_rect(last); ly = lr.y + lr.h; }
        if (ly >= 0) {
            Rect r0 = place_rect(0);
            draw_rrect(r0.x + S(6), roundf(ly - S(1)), r0.w - S(12), roundf(S(2)), S(1), C(COL_ACCENT));
            draw_rrect(r0.x + S(3), roundf(ly - S(3)), roundf(S(6)), roundf(S(6)), S(3), C(COL_ACCENT));
        }
    }
    draw_clip_pop(cs);
}

static const char *owner_name(u32 uid) {
    static struct { u32 uid; char name[32]; } cache[64];
    static int n;
    for (int i = 0; i < n; i++)
        if (cache[i].uid == uid) return cache[i].name;
    int k = n < 64 ? n++ : (int)(uid % 64);
    cache[k].uid = uid;
    struct passwd pw, *res = NULL;
    char buf[1024];
    if (getpwuid_r(uid, &pw, buf, sizeof buf, &res) == 0 && res) snprintf(cache[k].name, sizeof cache[k].name, "%s", pw.pw_name);
    else snprintf(cache[k].name, sizeof cache[k].name, "%u", uid);
    return cache[k].name;
}

static void fmt_perms(const Entry *e, char *out) {
    u16 m = e->mode;
    out[0] = (e->flags & ENT_LINK) ? 'l' : (e->flags & ENT_DIR) ? 'd' : '-';
    const char *rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; i++) out[1 + i] = (m & (0400 >> i)) ? rwx[i] : '-';
    out[10] = 0;
}

static void fmt_date(i64 t0, char *buf, size_t cap, const struct tm *now) {
    if (!t0) { buf[0] = 0; return; }
    time_t t = (time_t)t0;
    struct tm tm;
    localtime_r(&t, &tm);
    if (tm.tm_year == now->tm_year && tm.tm_yday == now->tm_yday) strftime(buf, cap, "Today %H:%M", &tm);
    else strftime(buf, cap, "%Y-%m-%d %H:%M", &tm);
}

static void fmt_type(const Entry *e, char *buf, size_t cap) {
    if (e->flags & ENT_DIR) { snprintf(buf, cap, "Folder"); return; }
    if (e->ext && e->name_len - e->ext <= 8) {
        size_t n = e->name_len - e->ext, k;
        for (k = 0; k < n && k + 1 < cap; k++) buf[k] = (char)toupper((u8)e->name[e->ext + k]);
        snprintf(buf + k, cap - k, " File");
    } else snprintf(buf, cap, "File");
}

static void draw_header(Pane *p) {
    PaneLayout *ly = &p->ly;
    Rect h = ly->header;
    if (h.h <= 0) return;
    draw_rect(h.x, h.y, h.w, h.h, C(COL_BG));
    draw_rect(h.x, h.y + h.h - 1, h.w, 1, C(COL_BORDER));
    Font *f = &A.font_small;
    f32 ty = text_y(f, h.y, h.h);
    bool dragging_here = A.drag == DRAG_COL && A.drag_pane == p;
    for (int i = 0; i < ly->ncols; i++) {
        int c = ly->cid[i];
        f32 x0 = ly->cx0[i], x1 = ly->cx1[i];
        Rect r = {x0, h.y + S(4), x1 - x0, h.h - S(8)};
        bool moving = dragging_here && A.drag_col == c;
        if (moving) draw_rrect(r.x + S(2), r.y, r.w - S(4), r.h, S(5), CA(COL_ACCENT, 0.2f));
        else if (A.mouse_in && A.drag == DRAG_NONE && in_rect(r, A.mx, A.my) && header_edge_at(ly, A.mx) < 0)
            draw_rrect(r.x + S(2), r.y, r.w - S(4), r.h, S(5), C(COL_HOVER));
        bool active = p->sort_key == g_coldef[c].sort;
        Color tc = active ? C(COL_TEXT) : C(COL_TEXT_DIM);
        const char *label = g_coldef[c].label;
        int len = (int)strlen(label);
        f32 tw = text_width(f, label, len);
        bool right = g_coldef[c].right;
        f32 tx = c == COL_NAME ? ly->col_name : right ? x1 - tw - S(6) : x0 + S(10);
        draw_text_fit(f, tx, ty, x1 - tx - S(4), label, len, tc, 0);
        if (active) {
            f32 ax = right ? tx - S(10) : MIN(tx + tw + S(9), x1 - S(8));
            draw_chevron(ax, h.y + h.h * 0.5f, S(7), p->sort_desc ? 3 : 2, MAX(1.f, S(1.3f)), C(COL_ACCENT));
        }
        if (i > 0) {
            bool eh = (A.drag == DRAG_COL_RESIZE && A.drag_pane == p && ly->cid[i - 1] == A.drag_col) ||
                      (A.mouse_in && A.drag == DRAG_NONE && in_rect(h, A.mx, A.my) && header_edge_at(ly, A.mx) == i - 1);
            draw_rect(x0, h.y + S(eh ? 5 : 9), 1, h.h - S(eh ? 10 : 18), eh ? C(COL_ACCENT) : C(COL_BORDER));
        }
    }
    if (dragging_here && A.col_insert >= 0) { // reorder caret
        f32 cx = ly->cx1[ly->ncols - 1];
        for (int i = 0; i < ly->ncols; i++) {
            int vi = -1;
            for (int k = 0; k < A.ncols; k++)
                if (A.cols[k] == ly->cid[i]) vi = k;
            if (vi >= A.col_insert) { cx = ly->cx0[i]; break; }
        }
        draw_rrect(cx - S(1.5f), h.y + S(4), S(3), h.h - S(8), S(1.5f), C(COL_ACCENT));
    }
}

static void draw_message(Pane *p, const char *msg) {
    Font *f = &A.font;
    int len = (int)strlen(msg);
    f32 w = text_width(f, msg, len);
    Rect lr = p->ly.list;
    draw_text_fit(f, lr.x + MAX(S(10), (lr.w - w) * 0.5f), lr.y + S(40), lr.w - S(20), msg, len, C(COL_TEXT_DIM), 0);
}

static f32 draw_text_hl(Font *f, f32 x, f32 y, f32 maxw, const char *s, int len, int base, const u16 *pos, int npos,
                        Color c, Color hc);
static int wrap_text(Font *f, const char *s, f32 maxw, int *starts, int *lens, int max);

// --- thumbnail textures for the grid (LRU) ------------------------------------

typedef struct {
    u32 hash;
    char path[PATH_MAX];
    i64 mtime;
    int px;
    SDL_Texture *tex;
    int w, h;
    u64 used;
    u8 state; // 1 pending, 2 ready, 3 failed
} Thumb;
#define MAX_THUMBS 200
static Thumb *g_thumbs;
static int g_nthumbs;
static u64 g_thumb_clock;

static bool thumb_get(const char *path, i64 mtime, int px, SDL_Texture **tex, int *w, int *h) {
    u32 hsh = str_hash(path);
    g_thumb_clock++;
    for (int i = 0; i < g_nthumbs; i++) {
        Thumb *t = &g_thumbs[i];
        if (t->hash == hsh && t->px == px && t->mtime == mtime && !strcmp(t->path, path)) {
            t->used = g_thumb_clock;
            if (t->state != 2) return false;
            *tex = t->tex;
            *w = t->w;
            *h = t->h;
            return true;
        }
    }
    if (!g_thumbs) g_thumbs = xcalloc(MAX_THUMBS, sizeof(Thumb));
    int k = -1;
    if (g_nthumbs < MAX_THUMBS) k = g_nthumbs++;
    else
        for (int i = 0; i < g_nthumbs; i++)
            if (g_thumbs[i].state != 1 && (k < 0 || g_thumbs[i].used < g_thumbs[k].used)) k = i;
    if (k < 0) return false;
    Thumb *t = &g_thumbs[k];
    if (t->tex) SDL_DestroyTexture(t->tex);
    memset(t, 0, sizeof *t);
    t->hash = hsh;
    snprintf(t->path, sizeof t->path, "%s", path);
    t->mtime = mtime;
    t->px = px;
    t->used = g_thumb_clock;
    t->state = 1;
    thumb_request(path, mtime, px);
    return false;
}

static void thumb_done_cb(void *arg) {
    ThumbReq *r = arg;
    u32 hsh = str_hash(r->path);
    for (int i = 0; i < g_nthumbs; i++) {
        Thumb *t = &g_thumbs[i];
        if (t->state != 1 || t->hash != hsh || t->px != r->px || t->mtime != r->mtime || strcmp(t->path, r->path)) continue;
        t->state = 3;
        if (r->rgba) {
            t->tex = SDL_CreateTexture(A.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, r->w, r->h);
            if (t->tex) {
                SDL_UpdateTexture(t->tex, NULL, r->rgba, r->w * 4);
                SDL_SetTextureBlendMode(t->tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(t->tex, SDL_SCALEMODE_LINEAR);
                t->w = r->w;
                t->h = r->h;
                t->state = 2;
            }
        }
        break;
    }
    if (r->rgba) stbi_image_free(r->rgba);
    free(r);
    A.redraw = true;
}

static void render_tex_fit(SDL_Texture *t, int tw, int th, f32 x, f32 y, f32 box, bool frame, bool dim) {
    f32 sc = MIN(box / (f32)tw, box / (f32)th);
    SDL_FRect dst = {roundf(x + (box - tw * sc) * 0.5f), roundf(y + (box - th * sc) * 0.5f), roundf(tw * sc), roundf(th * sc)};
    if (frame) draw_rect(dst.x - 1, dst.y - 1, dst.w + 2, dst.h + 2, C(COL_TRACK));
    draw_flush();
    SDL_SetTextureAlphaMod(t, dim ? 115 : 255);
    SDL_RenderTexture(A.ren, t, NULL, &dst);
    SDL_SetTextureAlphaMod(t, 255);
}

// Big icon for a grid cell: folder, thumbnail, or file-type icon.
static void draw_item_icon_big(Pane *p, Entry *e, f32 ix, f32 iy, f32 isz, bool cut) {
    bool hidden = e->flags & ENT_HIDDEN;
    if (e->flags & ENT_DIR) {
        draw_dir_icon(ix, iy, isz, rel_base(e->name), hidden);
        return;
    }
    const char *name = rel_base(e->name);
    if (!pane_is_archive(p) && thumb_capable(name) && atomic_load_explicit(&e->meta, memory_order_acquire) == META_READY) {
        char full[PATH_MAX];
        path_join(full, sizeof full, p->path, e->name);
        SDL_Texture *t;
        int tw, th;
        if (thumb_get(full, e->mtime, (int)isz, &t, &tw, &th)) {
            render_tex_fit(t, tw, th, ix, iy, isz, true, cut);
            return;
        }
    }
    int id = icon_for_name(name, e->mflags & MF_EXEC);
    if (isz <= 48) {
        Glyph *g = icon_glyph(id, (int)isz);
        if (g) { draw_glyph(g, roundf(ix), roundf(iy), (Color){1, 1, 1, 1}); return; }
    }
    SDL_Texture *bt = big_icon_tex(id, (int)isz);
    if (bt) render_tex_fit(bt, (int)isz, (int)isz, ix, iy, isz, false, cut);
    else draw_file_icon(ix, iy, isz, file_color(e));
}

// List and grid views.
// Git state next to a name: a letter for files, a dot for folders (changes inside). Returns the width it takes.
static f32 git_badge_w(u8 gs) { return gs > GS_IGNORED ? roundf(S(16)) : 0; }

static void draw_git_badge(u8 gs, bool dir, f32 right, f32 y, f32 h, bool bright) {
    if (gs <= GS_IGNORED) return;
    Color c = C(bright ? rgb_mix(git_color(gs), COL_SEL_TEXT, 0.35f) : git_color(gs));
    f32 w = git_badge_w(gs), cx = right - w * 0.5f;
    if (dir) {
        f32 r = roundf(S(3));
        draw_rrect(roundf(cx - r), roundf(y + h * 0.5f - r), r * 2, r * 2, r, c);
    } else {
        Font *f = &A.font_small;
        const char *l = git_letter(gs);
        draw_text(f, roundf(cx - text_width(f, l, (int)strlen(l)) * 0.5f), text_y(f, y, h), l, (int)strlen(l), c);
    }
}

// A folder that is a repository: a pill with its branch, then its remote (faint) where there's room.
static const char *repo_pill_text(const GitSub *g) { return g->branch[0] ? g->branch : "no commits"; }

static f32 repo_pill_w(const GitSub *g) {
    const char *b = repo_pill_text(g);
    return roundf(MIN(text_width(&A.font_small, b, (int)strlen(b)), S(140)) + S(31));
}

static void draw_repo_pill(const GitSub *g, f32 x, f32 y, f32 h, f32 maxw, bool bright) {
    Font *fs = &A.font_small;
    const char *b = repo_pill_text(g);
    f32 pw = MIN(repo_pill_w(g), maxw), ph = roundf(MAX(S(14), MIN(h - S(8), fs->line_h + S(6))));
    if (pw < S(44)) return;
    x = roundf(x);
    f32 py = roundf(y + (h - ph) * 0.5f);
    draw_rrect(x, py, pw, ph, ph * 0.5f, bright ? CA(COL_SEL_TEXT, 0.18f) : C(COL_CHIP));
    draw_branch_icon(x + S(6), py + ph * 0.5f, bright ? C(COL_SEL_DIM) : C(COL_TEXT_DIM));
    f32 tx = x + S(23);
    draw_text_fit(fs, tx, text_y(fs, py, ph), x + pw - S(8) - tx, b, (int)strlen(b), bright ? C(COL_SEL_TEXT) : C(COL_TEXT2), 0);
    f32 rx = x + pw + S(8);
    if (g->disp[0] && x + maxw - rx > S(40))
        draw_text_fit(fs, rx, text_y(fs, y, h), x + maxw - rx, g->disp, (int)strlen(g->disp), bright ? C(COL_SEL_DIM) : C(COL_TEXT_FAINT), 0);
}

// Name (up to maxw), then the repository pill when the folder is one (the name gives way so the pill stays visible);
// gap: room kept after the name for a git badge. Returns where the name ends.
static f32 draw_name_pill(Pane *p, const Entry *e, Font *f, f32 x, f32 y, f32 h, f32 maxw, f32 gap, const char *name,
                          int nlen, Color c, bool bright) {
    const GitSub *g = git_entry_sub(p, e);
    f32 pw = g ? repo_pill_w(g) + S(10) : 0, nw = MIN(text_width(f, name, nlen), maxw);
    if (!pw || maxw < pw + S(70)) { draw_text_fit(f, x, text_y(f, y, h), maxw, name, nlen, c, 0); return x + nw; }
    nw = MIN(nw, maxw - pw);
    draw_text_fit(f, x, text_y(f, y, h), nw, name, nlen, c, 0);
    draw_repo_pill(g, x + nw + gap + S(10), y, h, maxw - nw - S(10), bright);
    return x + nw;
}

// Folder compare marks: + only here, up newer here, down older here, != differs, ! file vs folder, a dot: inside.
static bool cmp_marked(u8 st) { return st != CMP_SAME && st != CMP_UNKNOWN; }
static u32 cmp_color(u8 st) {
    switch (st) {
    case CMP_ONLY_A: return COL_OK;
    case CMP_A_NEWER: return COL_ACCENT;
    case CMP_B_NEWER: return COL_TEXT_DIM;
    case CMP_DIFF: return COL_WARN;
    case CMP_TYPE: return COL_DANGER;
    default: return COL_ACCENT;
    }
}
static f32 cmp_badge_w(u8 st) { return cmp_marked(st) ? roundf(S(18)) : 0; }

// The mark itself, drawn (not a font glyph: same size and centre in every font), centred on (cx, cy).
static void draw_cmp_mark(u8 st, f32 cx, f32 cy, f32 s, Color c) {
    f32 t = MAX(1.f, roundf(s * 0.17f)), hw = s * 0.5f;
    switch (st) {
    case CMP_ONLY_A: draw_plus(cx, cy, s * 0.95f, t, c); break;
    case CMP_A_NEWER: draw_tri(cx - hw, cy + s * 0.36f, cx + hw, cy + s * 0.36f, cx, cy - s * 0.42f, c); break;
    case CMP_B_NEWER: draw_tri(cx - hw, cy - s * 0.36f, cx + hw, cy - s * 0.36f, cx, cy + s * 0.42f, c); break;
    case CMP_DIFF:
        draw_rect(roundf(cx - hw), roundf(cy - s * 0.22f - t * 0.5f), roundf(s), t, c);
        draw_rect(roundf(cx - hw), roundf(cy + s * 0.22f - t * 0.5f), roundf(s), t, c);
        draw_line(cx + s * 0.22f, cy - hw, cx - s * 0.22f, cy + hw, t, c);
        break;
    case CMP_TYPE:
        draw_rect(roundf(cx - t * 0.5f), roundf(cy - hw), t, roundf(s * 0.62f), c);
        draw_rect(roundf(cx - t * 0.5f), roundf(cy + hw - t), t, t, c);
        break;
    default: {
        f32 r = roundf(s * 0.3f);
        draw_rrect(roundf(cx - r), roundf(cy - r), r * 2, r * 2, r, c);
    }
    }
}

static void draw_cmp_badge(u8 st, f32 right, f32 y, f32 h, bool bright) {
    if (!cmp_marked(st)) return;
    Color c = C(bright ? rgb_mix(cmp_color(st), COL_SEL_TEXT, 0.35f) : cmp_color(st));
    draw_cmp_mark(st, roundf(right - cmp_badge_w(st) * 0.5f), roundf(y + h * 0.5f), roundf(S(10)), c);
}

static Color git_name_color(u8 gs, Color c, bool bright) {
    if (bright || gs == GS_NONE) return c;
    if (gs == GS_IGNORED) return C(COL_TEXT_FAINT);
    return C(rgb_mix(COL_TEXT, git_color(gs), 0.7f));
}

static void draw_cells(Pane *p, bool active, i64 hover, u32 sel_col, u32 sel_cur) {
    Listing *L = p->L;
    GitView gv;
    git_view(p, &gv);
    PaneLayout *ly = &p->ly;
    Rect lr = ly->list;
    Font *f = &A.font;
    int pr = MAX(1, ly->per_row);
    i64 first = (i64)MAX(0.0, floor((p->scroll - ly->pad) / ly->cell_h)) * pr;
    for (i64 i = first; i < (i64)p->count; i++) {
        Rect r = item_rect(p, i);
        if (r.y >= lr.y + lr.h) break;
        r.x = roundf(r.x);
        r.y = roundf(r.y);
        u32 ei = p->order[i];
        Entry *e = &L->entries[ei];
        bool sel = p->sel[ei], cur = i == p->cursor, hidden = e->flags & ENT_HIDDEN;
        Rect bg = {r.x + S(3), r.y + S(2), r.w - S(6), r.h - S(4)};
        f32 rad = p->view == VIEW_GRID ? S(8) : S(5);
        if (sel) draw_rrect(bg.x, bg.y, bg.w, bg.h, rad, C(cur ? sel_cur : sel_col));
        else if (i == hover) draw_rrect(bg.x, bg.y, bg.w, bg.h, rad, C(COL_HOVER));
        if (cur && !sel && active) draw_rrect_outline(bg.x, bg.y, bg.w, bg.h, rad, MAX(1.f, S(1.f)), CA(COL_ACCENT, 0.7f));
        if (A.drop_pane == p && A.drop_row == i) {
            draw_rrect(bg.x, bg.y, bg.w, bg.h, rad, CA(COL_ACCENT, 0.22f));
            draw_rrect_outline(bg.x, bg.y, bg.w, bg.h, rad, MAX(1.f, S(1.3f)), C(COL_ACCENT));
        }
        bool cut = clip_is_cut(p->path, e->name);
        if (cut) g_alpha = 0.45f;
        const char *name = rel_base(e->name);
        int nlen = (int)strlen(name);
        bool bright = sel && active;
        Color name_c = bright ? C(COL_SEL_TEXT) : hidden ? C(COL_TEXT_HIDDEN) : C(COL_TEXT);
        bool renaming = A.ren_pane == p && A.ren_L == L && A.ren_entry == ei;
        u8 gs = git_entry_state(&gv, e), cs = cmp_entry_state(p, e);
        if (cs != CMP_UNKNOWN) gs = GS_NONE; // comparing: the compare marks take the place of git's
        if (cs == CMP_SAME && !cut) g_alpha = 0.5f; // the same on both sides: in the background
        name_c = git_name_color(gs, name_c, bright);
        if (p->view == VIEW_LIST) {
            f32 is = roundf(S(16)), ix = r.x + S(10), iy = roundf(r.y + (r.h - is) * 0.5f);
            if (e->flags & ENT_DIR) draw_dir_icon(ix, iy, is, name, hidden);
            else if (!draw_icon_for_name(ix, iy, is, name, e->mflags & MF_EXEC)) draw_file_icon(ix, iy, is, file_color(e));
            f32 tx = ix + is + S(8);
            if (renaming) {
                Rect fr = {tx - S(4), r.y + S(3), r.x + r.w - S(6) - (tx - S(4)), r.h - S(6)};
                A.ren_rect = fr;
                draw_rrect(fr.x, fr.y, fr.w, fr.h, S(4), C(COL_FIELD_HOVER));
                draw_rrect_outline(fr.x, fr.y, fr.w, fr.h, S(4), MAX(1.f, S(1.2f)), C(COL_ACCENT));
                te_draw(&A.ren_te, f, fr, roundf(S(4)), C(COL_TEXT));
            } else {
                f32 gw = git_badge_w(gs) + cmp_badge_w(cs), maxw = r.x + r.w - tx - S(10) - gw;
                f32 ex = draw_name_pill(p, e, f, tx, r.y, r.h, maxw, gw + S(4), name, nlen, name_c, bright);
                draw_git_badge(gs, e->flags & ENT_DIR, ex + S(4) + gw, r.y, r.h, bright);
                draw_cmp_badge(cs, ex + S(4) + gw, r.y, r.h, bright);
            }
        } else {
            f32 isz = roundf(S(p->icon_px)), ix = roundf(r.x + (r.w - isz) * 0.5f), iy = r.y + roundf(S(8));
            draw_item_icon_big(p, e, ix, iy, isz, cut);
            if (gs > GS_IGNORED || cmp_marked(cs)) { // git / compare: a dot on the icon's top-right corner
                f32 gr2 = roundf(MAX(S(4), isz * 0.07f));
                f32 gx = ix + isz - gr2 * 2, gy = iy;
                draw_rrect(gx - S(2), gy - S(2), gr2 * 2 + S(4), gr2 * 2 + S(4), gr2 + S(2), C(COL_BG));
                draw_rrect(gx, gy, gr2 * 2, gr2 * 2, gr2, C(cmp_marked(cs) ? cmp_color(cs) : git_color(gs)));
            }
            if (git_entry_sub(p, e)) { // a repository: a branch mark on the icon's bottom-left corner
                f32 bs = roundf(MAX(S(18), isz * 0.3f)), bx = ix - S(2), by = iy + isz - bs + S(2);
                draw_rrect(bx - S(2), by - S(2), bs + S(4), bs + S(4), (bs + S(4)) * 0.5f, C(COL_BG));
                draw_rrect(bx, by, bs, bs, bs * 0.5f, C(COL_CHIP));
                draw_branch_icon(bx + (bs - S(13)) * 0.5f, by + bs * 0.5f, C(COL_TEXT2));
            }
            if (e->flags & ENT_LINK) {
                f32 bs = roundf(MAX(S(10), isz * 0.18f));
                draw_rrect(ix + isz - bs, iy + isz - bs, bs, bs, bs * 0.5f, C(COL_BG));
                draw_rrect(ix + isz - bs + S(2), iy + isz - bs + S(2), bs - S(4), bs - S(4), bs * 0.4f, C(0x7FB2FF));
            }
            f32 lyy = iy + isz + roundf(S(6)), lw = r.w - S(12), lh = roundf(f->line_h * 1.1f);
            if (renaming) {
                Rect fr = {r.x + S(4), lyy - S(2), r.w - S(8), lh + S(6)};
                A.ren_rect = fr;
                draw_rrect(fr.x, fr.y, fr.w, fr.h, S(4), C(COL_FIELD_HOVER));
                draw_rrect_outline(fr.x, fr.y, fr.w, fr.h, S(4), MAX(1.f, S(1.2f)), C(COL_ACCENT));
                te_draw(&A.ren_te, f, fr, roundf(S(4)), C(COL_TEXT));
            } else {
                int st[3], ln[3];
                int nl = wrap_text(f, name, lw, st, ln, 3);
                for (int k = 0; k < MIN(nl, 2); k++) {
                    int len = (k == 1 && nl > 2) ? nlen - st[1] : ln[k]; // second line takes the rest, elided
                    f32 tw = MIN(text_width(f, name + st[k], len), lw);
                    draw_text_fit(f, roundf(r.x + (r.w - tw) * 0.5f), lyy + k * lh, lw, name + st[k], len, name_c, 0);
                }
            }
        }
        g_alpha = 1.f;
    }
}

// Contents search: line number + the line, the match highlighted, across the whole row.
static void draw_match_row(Pane *p, const Entry *e, u32 ei, f32 y, f32 rh, bool bright) {
    PaneLayout *ly = &p->ly;
    Listing *L = p->L;
    Font *f = &A.font, *fs = &A.font_small;
    f32 gx = ly->col_name + roundf(S(14)), gw = roundf(text_width(fs, "00000", 5));
    char num[16];
    snprintf(num, sizeof num, "%u", (u32)e->size);
    int nl = (int)strlen(num);
    draw_text(fs, gx + gw - text_width(fs, num, nl), text_y(fs, y, rh), num, nl, bright ? C(COL_SEL_DIM) : C(COL_TEXT_FAINT));
    const char *t = L->ctext[ei] ? L->ctext[ei] : "";
    int tl = (int)strlen(t), col = (int)MIN(CM_COL(e), (u32)tl), ml = (int)MIN(CM_LEN(e), (u32)(tl - col));
    f32 x = gx + gw + roundf(S(12)), xe = ly->track.x - S(12), ty = text_y(f, y, rh);
    Color tc = bright ? C(COL_SEL_TEXT) : C(COL_TEXT2), hc = bright ? C(COL_SEL_TEXT) : C(COL_TEXT_STRONG);
    if (CM_ELIDED(e)) x = draw_text(f, x, ty, ELLIPSIS, 3, C(COL_TEXT_FAINT));
    f32 w1 = text_width(f, t, col);
    if (x + w1 > xe) { draw_text_fit(f, x, ty, xe - x, t, col, tc, 0); return; }
    x = draw_text(f, x, ty, t, col, tc);
    if (ml) {
        f32 w2 = MIN(text_width(f, t + col, ml), xe - x);
        draw_rrect(x - S(1), y + S(4), w2 + S(2), rh - S(8), S(3), bright ? CA(COL_SEL_TEXT, 0.22f) : CA(COL_ACCENT, 0.32f));
        draw_text_fit(f, x, ty, xe - x, t + col, ml, hc, 0);
        x += w2;
    }
    if (x < xe) draw_text_fit(f, x, ty, xe - x, t + col + ml, tl - col - ml, tc, 0);
}

// --- disk usage map ---------------------------------------------------------------------------------------------

static Color du_mix(u32 a, u32 b, f32 t) { // a blended towards b
    Color x = C(a), y = C(b);
    return (Color){x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, 1.f};
}
static f32 du_lum(Color c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
static bool du_dark(void) { return du_lum(C(COL_BG)) < 0.5f; }

static Color du_file_col(int cat) { return du_mix(g_dcat[cat].col, COL_BG, du_dark() ? 0.22f : 0.12f); }
static Color du_folder_col(void) { return du_mix(0x7C8DB0, COL_BG, du_dark() ? 0.4f : 0.2f); } // (not the folder icon colour: that is close to Archives)
static Color du_small_col(void) { return du_mix(0x8C8F99, COL_BG, du_dark() ? 0.55f : 0.45f); }

// A folder: slate, tinted towards the type of file that fills most of it (the more, the stronger).
static Color du_dir_col(const DuItem *it) {
    Color fc = du_folder_col();
    f32 share;
    int c = du_main_category(it, &share);
    if (c == DC_OTHER) return fc;
    Color tc = du_file_col(c);
    f32 t = 0.3f + 0.5f * (share - 0.5f) * 2.f;
    return (Color){fc.r + (tc.r - fc.r) * t, fc.g + (tc.g - fc.g) * t, fc.b + (tc.b - fc.b) * t, 1.f};
}

static DuCell *du_cell_add(DuView *v) {
    if (v->ncells == v->capcells) {
        v->capcells = v->capcells ? v->capcells * 2 : 512;
        v->cells = xrealloc(v->cells, (size_t)v->capcells * sizeof(DuCell));
    }
    DuCell *c = &v->cells[v->ncells++];
    memset(c, 0, sizeof *c);
    return c;
}

typedef struct { DuItem *it; u64 size, files; u8 kind; } DuEnt;
static int du_ent_cmp(const void *a, const void *b) {
    u64 x = ((const DuEnt *)a)->size, y = ((const DuEnt *)b)->size;
    return x < y ? 1 : x > y ? -1 : 0;
}

// What a folder shows: subfolders and files biggest first, files under 1% of the folder (and the scan's own sum of
// small files) as one "small files" entry at the end. Returns a malloc'd array.
static DuEnt *du_ents(DuItem *it, int *n) {
    *n = 0;
    DuItem *k = atomic_load_explicit(&it->kids, memory_order_acquire);
    if (!k || !it->nkids) return NULL;
    DuEnt *e = xmalloc((it->nkids + 1) * sizeof(DuEnt));
    u64 total = atomic_load_explicit(&it->size, memory_order_relaxed), ssum = 0, sfiles = 0;
    DuItem *small = NULL;
    for (u32 i = 0; i < it->nkids; i++) {
        DuItem *c = &k[i];
        if (c->removed || c->kind == DK_MOUNT) continue;
        u64 sz = atomic_load_explicit(&c->size, memory_order_relaxed);
        if (!sz) continue;
        if (c->kind == DK_SMALL || (c->kind == DK_FILE && sz * 100 < total)) {
            ssum += sz;
            sfiles += atomic_load_explicit(&c->files, memory_order_relaxed);
            if (c->kind == DK_SMALL) small = c;
            continue;
        }
        e[(*n)++] = (DuEnt){c, sz, atomic_load_explicit(&c->files, memory_order_relaxed), c->kind == DK_DIR ? DUC_FOLDER : DUC_FILE};
    }
    qsort(e, (size_t)*n, sizeof(DuEnt), du_ent_cmp);
    if (ssum) e[(*n)++] = (DuEnt){small, ssum, sfiles, DUC_SMALL};
    if (!*n) { free(e); e = NULL; }
    return e;
}

// Squarified treemap (Bruls et al.): rows along the shorter side, each as close to square cells as it gets.
static void du_squarify(const f64 *a, int n, Rect r, Rect *out) {
    f64 x = r.x, y = r.y, w = r.w, h = r.h;
    for (int i = 0; i < n;) {
        f64 side = MIN(w, h);
        if (side <= 0.5) { for (; i < n; i++) out[i] = (Rect){(f32)x, (f32)y, 0, 0}; break; }
        int j = i;
        f64 sum = 0, best = INFINITY;
        while (j < n) {
            f64 ns = sum + a[j], s2 = side * side;
            f64 worst = MAX(s2 * a[i] / (ns * ns), ns * ns / (s2 * a[j]));
            if (worst > best) break;
            best = worst;
            sum = ns;
            j++;
        }
        f64 thick = sum / side, off = 0;
        if (j == n) thick = w >= h ? w : h; // the last row takes what's left (no rounding sliver)
        for (int k = i; k < j; k++) {
            f64 len = k == j - 1 ? side - off : a[k] / (sum / side) ;
            out[k] = w >= h ? (Rect){(f32)x, (f32)(y + off), (f32)thick, (f32)len} : (Rect){(f32)(x + off), (f32)y, (f32)len, (f32)thick};
            off += len;
        }
        if (w >= h) { x += thick; w -= thick; } else { y += thick; h -= thick; }
        i = j;
    }
}

static void du_leaf_text(Rect r, const char *name, u64 size, Color bg) {
    Font *fs = &A.font_small;
    f32 pad = roundf(S(5));
    if (r.w < S(34) || r.h < fs->line_h + S(4)) return;
    Color tc = du_lum(bg) > 0.55f ? C(0x15171C) : C(0xF4F5F7);
    Color dc = tc;
    dc.a = 0.72f;
    int nl = (int)strlen(name);
    bool two = r.h >= fs->line_h * 2 + S(8);
    f32 y0 = two ? r.y + S(3) : text_y(fs, r.y, r.h);
    draw_text_fit(fs, r.x + pad, two ? y0 : y0, r.w - pad * 2, name, nl, tc, 0);
    if (two) {
        char sz[32];
        fmt_size(sz, sizeof sz, size);
        draw_text_fit(fs, r.x + pad, y0 + fs->line_h, r.w - pad * 2, sz, (int)strlen(sz), dc, 0);
    }
}

static void du_small_label(char *out, size_t cap, u64 files) {
    char n[32];
    fmt_count(n, files);
    snprintf(out, cap, "%s small file%s", n, files == 1 ? "" : "s");
}

// Lay out and draw a folder's contents inside r; nested folders get a title strip and their own contents.
static void du_tm_folder(DuView *v, DuItem *it, Rect r, int depth) {
    int n;
    DuEnt *e = du_ents(it, &n);
    if (!e) return;
    f64 total = 0;
    for (int i = 0; i < n; i++) total += (f64)e[i].size;
    f64 *a = xmalloc((size_t)n * sizeof(f64));
    Rect *rr = xmalloc((size_t)n * sizeof(Rect));
    for (int i = 0; i < n; i++) a[i] = (f64)e[i].size / total * (f64)r.w * (f64)r.h;
    du_squarify(a, n, r, rr);
    Font *fs = &A.font_small;
    f32 gap = MAX(1.f, roundf(S(1)));
    for (int i = 0; i < n; i++) {
        f32 x0 = roundf(rr[i].x), y0 = roundf(rr[i].y), x1 = roundf(rr[i].x + rr[i].w), y1 = roundf(rr[i].y + rr[i].h);
        Rect cr = {x0, y0, x1 - x0 - gap, y1 - y0 - gap};
        if (cr.w < 1 || cr.h < 1) continue;
        DuCell *c = du_cell_add(v);
        c->r = cr;
        c->it = e[i].it;
        c->parent = it;
        c->size = e[i].size;
        c->files = e[i].files;
        c->kind = e[i].kind;
        c->depth = (u8)MIN(depth, 255);
        if (e[i].kind == DUC_SMALL) {
            Color bg = du_small_col();
            draw_rect(cr.x, cr.y, cr.w, cr.h, bg);
            char lb[64];
            du_small_label(lb, sizeof lb, e[i].files);
            du_leaf_text(cr, lb, e[i].size, bg);
        } else if (e[i].kind == DUC_FILE) {
            Color bg = du_file_col(e[i].it->cat);
            draw_rect(cr.x, cr.y, cr.w, cr.h, bg);
            du_leaf_text(cr, e[i].it->name, e[i].size, bg);
        } else {
            f32 th = roundf(fs->line_h + S(5));
            bool nest = cr.w >= S(44) && cr.h >= th + S(18) && depth < 12 && v->ncells < 40000 &&
                        atomic_load_explicit(&e[i].it->kids, memory_order_acquire);
            if (!nest) {
                Color bg = du_dir_col(e[i].it);
                draw_rect(cr.x, cr.y, cr.w, cr.h, bg);
                du_leaf_text(cr, e[i].it->name, e[i].size, bg);
                continue;
            }
            c->kind = DUC_FRAME;
            Color fb = du_mix(COL_BG, COL_TEXT, MIN(0.07f + 0.035f * (f32)depth, 0.24f));
            draw_rect(cr.x, cr.y, cr.w, cr.h, fb);
            char sz[32];
            fmt_size(sz, sizeof sz, e[i].size);
            f32 sw = text_width(fs, sz, (int)strlen(sz)), pad = roundf(S(5));
            f32 tyy = text_y(fs, cr.y, th);
            bool fits = cr.w > sw + S(60);
            draw_text_fit(fs, cr.x + pad, tyy, cr.w - pad * 2 - (fits ? sw + S(8) : 0), e[i].it->name, (int)strlen(e[i].it->name), C(COL_TEXT), 0);
            if (fits) draw_text(fs, cr.x + cr.w - pad - sw, tyy, sz, (int)strlen(sz), C(COL_TEXT_DIM));
            Rect in = {cr.x + roundf(S(2)), cr.y + th, cr.w - roundf(S(4)), cr.h - th - roundf(S(2))};
            du_tm_folder(v, e[i].it, in, depth + 1);
        }
    }
    free(a);
    free(rr);
    free(e);
}

// Sunburst: rings outwards from the folder in the middle, each child an arc sized by its share. Colours as in the
// treemap (folders slate, files by type); neighbouring folders alternate shades.
static void du_sb_folder(DuView *v, DuItem *it, f64 a0, f64 a1, int depth, int maxd, f32 ring) {
    int n;
    DuEnt *e = du_ents(it, &n);
    if (!e) return;
    f64 total = 0;
    for (int i = 0; i < n; i++) total += (f64)e[i].size;
    f32 r0 = v->hole + (f32)(depth - 1) * ring, r1 = r0 + ring - MAX(1.f, roundf(S(1.5f)));
    f64 a = a0;
    Font *fs = &A.font_small;
    for (int i = 0; i < n; i++) {
        f64 span = (a1 - a0) * (f64)e[i].size / total;
        f64 b0 = a, b1 = a + span;
        a = b1;
        if (span * r1 < S(1.2f)) continue; // too thin to see
        Color col;
        if (e[i].kind == DUC_SMALL) col = du_small_col();
        else if (e[i].kind == DUC_FILE) col = du_file_col(e[i].it->cat);
        else {
            Color fc = du_dir_col(e[i].it), tc = C(i & 1 ? COL_BG : COL_TEXT), bg = C(COL_BG);
            f32 t = (i & 1) ? 0.12f : 0.08f, d = MIN(0.06f * (f32)(depth - 1), 0.3f);
            col = (Color){fc.r + (tc.r - fc.r) * t, fc.g + (tc.g - fc.g) * t, fc.b + (tc.b - fc.b) * t, 1.f};
            col = (Color){col.r + (bg.r - col.r) * d, col.g + (bg.g - col.g) * d, col.b + (bg.b - col.b) * d, 1.f};
        }
        f64 g = MIN(span * 0.25, (f64)S(0.75f) / r1); // a hairline between segments
        draw_arc_band(v->cx, v->cy, r0, r1, (f32)(b0 + g), (f32)(b1 - g), col);
        DuCell *c = du_cell_add(v);
        c->ring = true;
        c->r0 = r0;
        c->r1 = r1;
        c->a0 = (f32)b0;
        c->a1 = (f32)b1;
        c->it = e[i].it;
        c->parent = it;
        c->size = e[i].size;
        c->files = e[i].files;
        c->kind = e[i].kind;
        c->depth = (u8)depth;
        if (depth == 1 && e[i].it && e[i].it->name && ring > fs->line_h + S(4)) { // names on the inner ring
            f32 mid = (r0 + r1) * 0.5f, am = (f32)((b0 + b1) * 0.5);
            const char *nm = e[i].it->name;
            f32 room = MIN((f32)(span * mid) * 0.8f, ring * 1.6f), tw = text_width(fs, nm, (int)strlen(nm));
            if (room > S(36)) {
                f32 w = MIN(tw, room);
                Color tc = du_lum(col) > 0.55f ? C(0x15171C) : C(0xF4F5F7);
                draw_text_fit(fs, v->cx + cosf(am) * mid - w * 0.5f, v->cy + sinf(am) * mid - fs->line_h * 0.5f, room, nm, (int)strlen(nm), tc, 0);
            }
        }
        if (e[i].kind == DUC_FOLDER && depth < maxd && v->ncells < 40000) du_sb_folder(v, e[i].it, b0, b1, depth + 1, maxd, ring);
    }
    free(e);
}

static void du_outline(Rect r, f32 t, Color c) {
    draw_rect(r.x, r.y, r.w, t, c);
    draw_rect(r.x, r.y + r.h - t, r.w, t, c);
    draw_rect(r.x, r.y + t, t, r.h - 2 * t, c);
    draw_rect(r.x + r.w - t, r.y + t, t, r.h - 2 * t, c);
}

static void du_cell_name(const DuCell *c, char *out, size_t cap) {
    if (c->kind == DUC_SMALL) du_small_label(out, cap, c->files);
    else snprintf(out, cap, "%s", c->it && c->it->name ? c->it->name : "");
}

static void draw_du_tooltip(DuView *v, const DuCell *c, Rect bounds) {
    Font *f = &A.font, *fs = &A.font_small;
    char name[NAME_MAX + 64], l2[160], sz[32], pct[32] = "";
    du_cell_name(c, name, sizeof name);
    fmt_size(sz, sizeof sz, c->size);
    u64 tot = v->cur ? atomic_load(&v->cur->size) : 0;
    if (tot) snprintf(pct, sizeof pct, "  \xC2\xB7  %.1f%%", (f64)c->size * 100.0 / (f64)tot);
    if (c->kind == DUC_FRAME || c->kind == DUC_FOLDER) {
        char nf[32];
        fmt_count(nf, c->files);
        snprintf(l2, sizeof l2, "%s%s  \xC2\xB7  %s file%s%s", sz, pct, nf, c->files == 1 ? "" : "s",
                 c->it && !atomic_load(&c->it->done) ? "  \xC2\xB7  measuring\xE2\x80\xA6" : "");
    } else snprintf(l2, sizeof l2, "%s%s", sz, pct);
    f32 pad = roundf(S(9));
    f32 w = MAX(text_width(f, name, (int)strlen(name)), text_width(fs, l2, (int)strlen(l2))) + pad * 2;
    w = MIN(w, S(420));
    f32 h = f->line_h + fs->line_h + pad * 2 - S(2);
    f32 x = A.mx + S(14), y = A.my + S(18);
    if (x + w > bounds.x + bounds.w) x = A.mx - w - S(8);
    if (y + h > bounds.y + bounds.h) y = A.my - h - S(8);
    x = MAX(x, bounds.x);
    y = MAX(y, bounds.y);
    draw_rrect(x - 1, y - 1, w + 2, h + 2, S(7), C(COL_POPUP_BORDER));
    draw_rrect(x, y, w, h, S(6), C(COL_POPUP));
    draw_text_fit(f, x + pad, y + pad - S(1), w - pad * 2, name, (int)strlen(name), C(COL_TEXT), 0);
    draw_text_fit(fs, x + pad, y + pad + f->line_h - S(1), w - pad * 2, l2, (int)strlen(l2), C(COL_TEXT_DIM), 0);
}

static void draw_du_header(Pane *p, DuView *v) {
    Font *f = &A.font, *fs = &A.font_small;
    Rect h = v->hdr;
    draw_rect(h.x, h.y + h.h - 1, h.w, 1, C(COL_DIVIDER));
    // right: [Treemap|Sunburst]  list  close
    f32 bh = roundf(S(26)), by = h.y + roundf((h.h - bh) * 0.5f), bw = roundf(S(78)), x = h.x + h.w - roundf(S(10));
    v->b_close = (Rect){x - bh, by, bh, bh};
    x -= bh + roundf(S(6));
    v->b_list = (Rect){x - bh, by, bh, bh};
    x -= bh + roundf(S(10));
    v->b_sun = (Rect){x - bw, by, bw, bh};
    v->b_tree = (Rect){x - bw * 2, by, bw, bh};
    draw_rrect(v->b_tree.x, by, bw * 2, bh, S(6), C(COL_CHIP));
    for (int i = 0; i < 2; i++) {
        Rect b = i ? v->b_sun : v->b_tree;
        bool on = A.du_style == i, hov = in_rect(b, A.mx, A.my);
        if (on) draw_rrect(b.x + 2, b.y + 2, b.w - 4, b.h - 4, S(5), C(COL_ACCENT));
        else if (hov) draw_rrect(b.x + 2, b.y + 2, b.w - 4, b.h - 4, S(5), C(COL_CONTROL_HOVER));
        const char *lb = i ? "Sunburst" : "Treemap";
        f32 tw = text_width(fs, lb, (int)strlen(lb));
        draw_text(fs, roundf(b.x + (b.w - tw) * 0.5f), text_y(fs, b.y, b.h), lb, (int)strlen(lb), C(on ? COL_ON_ACCENT : COL_TEXT2));
    }
    { // list toggle: three lines
        Rect b = v->b_list;
        bool hov = in_rect(b, A.mx, A.my);
        if (A.du_list) draw_rrect(b.x, b.y, b.w, b.h, S(6), C(COL_CHIP));
        else if (hov) draw_rrect(b.x, b.y, b.w, b.h, S(6), C(COL_CONTROL_HOVER));
        Color ic = C(A.du_list ? COL_TEXT : COL_TEXT_DIM);
        f32 lx = b.x + roundf(S(7)), lw = b.w - roundf(S(14)), t = MAX(1.f, roundf(S(1.5f)));
        for (int k = 0; k < 3; k++) draw_rect(lx, roundf(b.y + b.h * 0.5f + (k - 1) * S(5) - t * 0.5f), k == 2 ? lw * 0.6f : lw, t, ic);
    }
    {
        Rect b = v->b_close;
        bool hov = in_rect(b, A.mx, A.my);
        if (hov) draw_rrect(b.x, b.y, b.w, b.h, S(6), C(COL_CONTROL_HOVER));
        draw_cross(b.x + b.w * 0.5f, b.y + b.h * 0.5f, S(9), MAX(1.f, S(1.5f)), C(hov ? COL_TEXT : COL_TEXT_DIM));
    }
    // left: size, files, progress
    DuScan *s = v->s;
    u64 tot = v->cur ? atomic_load(&v->cur->size) : 0, nf = v->cur ? atomic_load(&v->cur->files) : 0;
    char sz[32], n[32], line[400];
    fmt_size(sz, sizeof sz, tot);
    fmt_count(n, nf);
    f32 lx = h.x + roundf(S(14)), maxw = v->b_tree.x - lx - S(12);
    f32 sw = text_width(f, sz, (int)strlen(sz));
    draw_text(f, lx, text_y(f, h.y, h.h), sz, (int)strlen(sz), C(COL_TEXT_STRONG));
    size_t o = (size_t)snprintf(line, sizeof line, "in %s file%s", n, nf == 1 ? "" : "s");
    bool running = du_scan_running(s);
    if (running) {
        u64 rs = atomic_load(&s->root.size);
        char nd[32];
        fmt_count(nd, atomic_load(&s->ndirs));
        if (s->used) o += (size_t)snprintf(line + o, sizeof line - o, "  \xC2\xB7  measuring\xE2\x80\xA6 %d%%", (int)MIN(99, rs * 100 / MAX(s->used, 1)));
        else o += (size_t)snprintf(line + o, sizeof line - o, "  \xC2\xB7  measuring\xE2\x80\xA6 %s folders", nd);
    } else if (s->t_done > s->t_start) {
        f64 sec = (f64)(s->t_done - s->t_start) / 1e9;
        if (sec < 1) o += (size_t)snprintf(line + o, sizeof line - o, "  \xC2\xB7  measured in %d ms", (int)(sec * 1000));
        else o += (size_t)snprintf(line + o, sizeof line - o, "  \xC2\xB7  measured in %.1f s", sec);
    }
    u32 ne = atomic_load(&s->nerr);
    if (ne) o += (size_t)snprintf(line + o, sizeof line - o, "  \xC2\xB7  %u folder%s couldn't be read", ne, ne == 1 ? "" : "s");
    if (s->stale) snprintf(line + o, sizeof line - o, "  \xC2\xB7  changed since: F5 measures again");
    draw_text_fit(fs, lx + sw + S(8), text_y(fs, h.y, h.h) + S(1), maxw - sw - S(8), line, (int)strlen(line), C(s->stale ? COL_WARN : COL_TEXT_DIM), 0);
    (void)p;
}

static void draw_du_legend(DuView *v) {
    Font *fs = &A.font_small;
    Rect r = v->legend;
    f32 x = r.x, d = roundf(S(9)), cy = r.y + r.h * 0.5f;
    for (int k = -1; k <= DC_COUNT; k++) { // folders, the categories, small files
        const char *lb = k < 0 ? "Folders" : k == DC_COUNT ? "Small files" : g_dcat[k].label;
        Color c = k < 0 ? du_folder_col() : k == DC_COUNT ? du_small_col() : du_file_col(k);
        if (k == DC_OTHER) continue; // listed last
        f32 tw = text_width(fs, lb, (int)strlen(lb));
        if (x + d + S(5) + tw > r.x + r.w) break;
        draw_rrect(x, roundf(cy - d * 0.5f), d, d, S(2), c);
        draw_text(fs, x + d + S(5), text_y(fs, r.y, r.h), lb, (int)strlen(lb), C(COL_TEXT_DIM));
        x += d + S(5) + tw + S(14);
        if (k == DC_COUNT - 1) { // "Other" after the named ones
            const char *ol = g_dcat[DC_OTHER].label;
            f32 ow = text_width(fs, ol, (int)strlen(ol));
            if (x + d + S(5) + ow > r.x + r.w) break;
            draw_rrect(x, roundf(cy - d * 0.5f), d, d, S(2), du_file_col(DC_OTHER));
            draw_text(fs, x + d + S(5), text_y(fs, r.y, r.h), ol, (int)strlen(ol), C(COL_TEXT_DIM));
            x += d + S(5) + ow + S(14);
        }
    }
}

static void draw_du_list(Pane *p, DuView *v, bool active) {
    Font *f = &A.font, *fs = &A.font_small;
    Rect r = v->list;
    draw_rect(r.x, r.y, r.w, r.h, C(COL_SIDEBAR));
    draw_rect(r.x, r.y, 1, r.h, C(COL_DIVIDER));
    f32 th = roundf(S(30)), pad = roundf(S(12));
    char t[NAME_MAX + 32];
    snprintf(t, sizeof t, "Biggest in %s", p->path[1] ? path_basename(p->path) : "/");
    draw_text_fit(fs, r.x + pad, text_y(fs, r.y, th), r.w - pad * 2, t, (int)strlen(t), C(COL_GROUP_TEXT), 0);
    ClipSave cs = draw_clip_push((SDL_Rect){(int)r.x + 1, (int)(r.y + th), (int)r.w - 1, (int)(r.h - th)});
    f32 rh = du_row_h();
    u64 tot = v->cur ? MAX(atomic_load(&v->cur->size), 1) : 1;
    int hov = A.drag == DRAG_NONE && !A.menu.open ? du_list_row_at(v, A.mx, A.my) : -1;
    int first = (int)(v->lscroll / rh);
    for (int i = MAX(first, 0); i < v->nrows; i++) {
        f32 y = r.y + th + (f32)((f64)i * rh - v->lscroll);
        if (y > r.y + r.h) break;
        DuItem *it = v->rows[i];
        Rect rr = {r.x + roundf(S(6)), y, r.w - roundf(S(12)), rh - roundf(S(2))};
        if (i == v->lcur) draw_rrect(rr.x, rr.y, rr.w, rr.h, S(6), C(active ? COL_SEL_FOCUS : COL_SEL_IDLE));
        else if (i == hov) draw_rrect(rr.x, rr.y, rr.w, rr.h, S(6), C(COL_HOVER));
        u64 sz = atomic_load(&it->size);
        f32 is = roundf(S(16)), ix = rr.x + roundf(S(6)), iy = rr.y + roundf(S(5));
        Color bar;
        if (it->kind == DK_DIR || it->kind == DK_MOUNT) { draw_dir_icon(ix, iy, is, it->name, it->name[0] == '.'); bar = du_folder_col(); }
        else if (it->kind == DK_FILE) { draw_file_icon(ix, iy, is, g_dcat[it->cat].col); bar = du_file_col(it->cat); }
        else { draw_file_icon(ix, iy, is, 0x8C8F99); bar = du_small_col(); }
        char nm[NAME_MAX + 64], right[48];
        if (it->kind == DK_SMALL) du_small_label(nm, sizeof nm, atomic_load(&it->files));
        else snprintf(nm, sizeof nm, "%s", it->name);
        bool dim = it->kind == DK_MOUNT || it->err;
        if (it->kind == DK_MOUNT) snprintf(right, sizeof right, "other drive");
        else if (it->err) snprintf(right, sizeof right, "no access");
        else fmt_size(right, sizeof right, sz);
        bool growing = it->kind == DK_DIR && !atomic_load(&it->done);
        f32 rw = text_width(fs, right, (int)strlen(right));
        f32 nx = ix + is + roundf(S(8)), ty = rr.y + roundf(S(3));
        draw_text_fit(f, nx, ty, rr.x + rr.w - nx - rw - S(14), nm, (int)strlen(nm), C(dim ? COL_TEXT_DIM : COL_TEXT), 0);
        draw_text(fs, rr.x + rr.w - rw - S(6), ty + roundf((f->line_h - fs->line_h) * 0.5f), right, (int)strlen(right),
                  C(growing || dim ? COL_TEXT_FAINT : COL_TEXT2));
        if (it->kind != DK_MOUNT && !it->err) { // share of the folder
            f32 by = rr.y + rr.h - roundf(S(9)), bw = rr.x + rr.w - nx - S(46), bt = roundf(S(4));
            f64 fr = (f64)sz / (f64)tot;
            draw_rrect(nx, by, bw, bt, S(2), C(COL_TRACK));
            draw_rrect(nx, by, MAX(S(2), (f32)(bw * MIN(fr, 1.0))), bt, S(2), bar);
            char pc[16];
            snprintf(pc, sizeof pc, fr >= 0.0995 ? "%.0f%%" : "%.1f%%", fr * 100.0);
            f32 pw = text_width(fs, pc, (int)strlen(pc));
            draw_text(fs, rr.x + rr.w - pw - S(6), by + bt * 0.5f - fs->line_h * 0.5f, pc, (int)strlen(pc), C(COL_TEXT_FAINT));
        }
    }
    if (!v->nrows) {
        const char *m = v->cur ? "Nothing in here" : "Measuring\xE2\x80\xA6";
        draw_text(fs, r.x + pad, r.y + th + S(6), m, (int)strlen(m), C(COL_TEXT_FAINT));
    }
    draw_clip_pop(cs);
}

static void draw_diskuse(Pane *p, bool active) {
    DuView *v = p->dv;
    Rect lr = p->ly.list;
    draw_rect(lr.x, lr.y, lr.w, lr.h, C(COL_BG));
    if (!v || !v->s) { draw_message(p, "Measuring\xE2\x80\xA6"); return; }
    ClipSave cs0 = draw_clip_push((SDL_Rect){(int)lr.x, (int)lr.y, (int)lr.w, (int)lr.h});
    Font *f = &A.font, *fs = &A.font_small;
    f32 hh = roundf(S(42)), lgh = roundf(S(28)), m = roundf(S(10));
    v->hdr = (Rect){lr.x, lr.y, lr.w, hh};
    bool list = A.du_list && lr.w >= S(560);
    f32 lw = list ? CLAMP(roundf(lr.w * 0.32f), roundf(S(250)), roundf(S(420))) : 0;
    v->list = list ? (Rect){lr.x + lr.w - lw, lr.y + hh, lw, lr.h - hh} : (Rect){0, 0, 0, 0};
    v->map = (Rect){lr.x + m, lr.y + hh + m, lr.w - lw - m * 2, lr.h - hh - lgh - m};
    v->legend = (Rect){v->map.x, v->map.y + v->map.h, v->map.w, lgh};
    du_rows(v);
    draw_du_header(p, v);
    v->ncells = 0;
    Rect mr = v->map;
    if (v->cur && mr.w > S(40) && mr.h > S(40)) {
        ClipSave cs = draw_clip_push((SDL_Rect){(int)mr.x, (int)mr.y, (int)mr.w, (int)mr.h});
        if (A.du_style == 0) {
            draw_rect(mr.x, mr.y, mr.w, mr.h, du_mix(COL_BG, COL_TEXT, 0.05f));
            du_tm_folder(v, v->cur, mr, 0);
        } else {
            v->cx = roundf(mr.x + mr.w * 0.5f);
            v->cy = roundf(mr.y + mr.h * 0.5f);
            f32 R = MIN(mr.w, mr.h) * 0.5f - S(4);
            v->hole = MAX(S(36), R * 0.2f);
            int maxd = CLAMP((int)((R - v->hole) / S(34)), 2, 6);
            f32 ring = (R - v->hole) / (f32)maxd;
            du_sb_folder(v, v->cur, -M_PI * 0.5, M_PI * 1.5, 1, maxd, ring);
            bool ch = hypotf(A.mx - v->cx, A.my - v->cy) < v->hole && A.drag == DRAG_NONE && !A.menu.open;
            draw_arc_band(v->cx, v->cy, 0, v->hole - S(2), 0, 2 * (f32)M_PI, C(ch ? COL_CONTROL_HOVER : COL_CHIP));
            char sz[32];
            fmt_size(sz, sizeof sz, atomic_load(&v->cur->size));
            const char *nm = p->path[1] ? path_basename(p->path) : "/";
            f32 hw = v->hole * 1.6f, sw = text_width(f, sz, (int)strlen(sz)), nw = MIN(text_width(fs, nm, (int)strlen(nm)), hw);
            draw_text_fit(fs, v->cx - nw * 0.5f, v->cy - fs->line_h - S(1), hw, nm, (int)strlen(nm), C(COL_TEXT_DIM), 0);
            draw_text(f, roundf(v->cx - sw * 0.5f), v->cy, sz, (int)strlen(sz), C(COL_TEXT_STRONG));
        }
        // the picked item and the one under the mouse
        DuCell *hc = A.drag == DRAG_NONE && !A.menu.open && in_rect(mr, A.mx, A.my) ? du_cell_at(v, A.mx, A.my) : NULL;
        for (int i = 0; i < v->ncells; i++) {
            DuCell *c = &v->cells[i];
            bool pk = v->pick && c->it == v->pick, hv = c == hc;
            if (!pk && !hv) continue;
            if (c->ring) {
                Color hl = pk ? C(COL_ACCENT) : CA(0xFFFFFF, du_dark() ? 0.18f : 0.35f);
                if (pk) draw_arc_band(v->cx, v->cy, c->r1 - S(3), c->r1, c->a0, c->a1, hl);
                else draw_arc_band(v->cx, v->cy, c->r0, c->r1, c->a0, c->a1, hl);
            } else {
                if (hv && !pk) draw_rect(c->r.x, c->r.y, c->r.w, c->kind == DUC_FRAME ? roundf(fs->line_h + S(5)) : c->r.h, CA(0xFFFFFF, du_dark() ? 0.12f : 0.3f));
                du_outline(c->r, MAX(1.f, roundf(S(pk ? 2 : 1))), pk ? C(COL_ACCENT) : CA(0xFFFFFF, 0.7f));
            }
        }
        draw_clip_pop(cs);
        if (hc) draw_du_tooltip(v, hc, lr);
    } else if (!v->cur) {
        const char *msg = "Measuring\xE2\x80\xA6";
        f32 w = text_width(f, msg, (int)strlen(msg));
        draw_text(f, roundf(mr.x + (mr.w - w) * 0.5f), roundf(mr.y + mr.h * 0.4f), msg, (int)strlen(msg), C(COL_TEXT_DIM));
    }
    if (v->cur && !v->ncells && atomic_load(&v->cur->done)) {
        const char *msg = v->cur->err ? "This folder couldn't be read" : "Nothing here takes space";
        f32 w = text_width(f, msg, (int)strlen(msg));
        draw_text(f, roundf(mr.x + (mr.w - w) * 0.5f), roundf(mr.y + mr.h * 0.4f), msg, (int)strlen(msg), C(COL_TEXT_DIM));
    }
    draw_du_legend(v);
    if (list) draw_du_list(p, v, active);
    draw_clip_pop(cs0);
}

// --- snapshots view ---------------------------------------------------------------------------------------------

static void draw_path_fit(Font *f, f32 x, f32 y, f32 maxw, const char *path, bool plain);
static void props_date(i64 t0, char *buf, size_t cap);

static void snap_type_label(const SnapInfo *s, char *out, size_t cap) {
    if (!strcmp(s->type, "pre")) snprintf(out, cap, "before");
    else if (!strcmp(s->type, "post")) snprintf(out, cap, "after #%d", s->pre);
    else if (!strcmp(s->cleanup, "timeline")) snprintf(out, cap, "timeline");
    else if (!strcmp(s->type, "zfs") || !strcmp(s->type, "timeshift")) out[0] = 0;
    else snprintf(out, cap, "%s", s->type[0] ? s->type : "single");
}

static void draw_snapview(Pane *p, bool active) {
    SnapView *v = p->sv;
    PaneLayout *ly = &p->ly;
    Rect lr = ly->list;
    draw_rect(lr.x, lr.y, lr.w, lr.h, C(COL_BG));
    if (!v) return;
    Font *f = &A.font, *fs = &A.font_small;
    ClipSave cs0 = draw_clip_push((SDL_Rect){(int)lr.x, (int)lr.y, (int)lr.w, (int)lr.h});
    // like the details view: a column header on top, rows, and a bar at the bottom with what this is + the actions
    f32 hh = roundf(S(32)), fh = roundf(S(44)), pad = roundf(S(14));
    v->hdr = (Rect){lr.x, lr.y + lr.h - fh, lr.w, fh};
    v->list = (Rect){lr.x, lr.y + hh, lr.w, lr.h - hh - fh};
    v->ballow = v->bback = (Rect){0, 0, 0, 0};
    v->nact = 0;
    SnapList *l = v->l;
    bool ready = l && atomic_load(&l->done);
    f32 rx = lr.x + roundf(S(6)), rw = lr.w - roundf(S(12));
    // columns (x where each starts)
    f32 c0 = rx + roundf(S(10)), c1 = c0 + roundf(S(96)), c2 = c1 + roundf(S(176)), c3 = c2 + roundf(S(112));
    {
        Rect h = {lr.x, lr.y, lr.w, hh};
        draw_rect(h.x, h.y + h.h - 1, h.w, 1, C(COL_BORDER));
        f32 ty = text_y(fs, h.y, h.h);
        const char *lab[4] = {"Snapshot", "Date", "Kind", "Description"};
        f32 cx[4] = {c0, c1, c2, c3};
        int nc = 4;
        if (v->mode == 1) { lab[0] = "Path"; lab[1] = "Change"; cx[1] = rx + rw - roundf(S(120)); nc = 2; }
        for (int i = 0; i < nc; i++) {
            draw_text(fs, cx[i] + (i ? S(0) : S(16) + S(8)), ty, lab[i], (int)strlen(lab[i]), C(COL_TEXT_DIM));
            if (i) draw_rect(cx[i] - S(10), h.y + S(9), 1, h.h - S(18), C(COL_BORDER));
        }
    }
    // the bottom bar
    {
        Rect b = v->hdr;
        draw_rect(b.x, b.y, b.w, b.h, C(COL_TOOLBAR));
        draw_rect(b.x, b.y, b.w, 1, C(COL_BORDER));
        f32 bh = roundf(S(28)), by = b.y + roundf((b.h - bh) * 0.5f), bx = b.x + b.w - roundf(S(8));
        bx -= bh;
        v->bback = (Rect){bx, by, bh, bh};
        bool h = in_rect(v->bback, A.mx, A.my);
        if (h) draw_rrect(bx, by, bh, bh, S(6), C(COL_CONTROL_HOVER));
        if (v->mode) draw_chevron(bx + bh * 0.5f, by + bh * 0.5f, S(10), 0, MAX(1.f, S(1.5f)), C(h ? COL_TEXT : COL_TEXT_DIM));
        else draw_cross(bx + bh * 0.5f, by + bh * 0.5f, S(9), MAX(1.f, S(1.5f)), C(h ? COL_TEXT : COL_TEXT_DIM));
        bx -= roundf(S(6));
        SnapInfo *s = snap_cur(v);
        if (v->mode == 0 && ready && l->n) {
            struct { int act; const char *lb; bool on; } bt[4] = {
                {ACT_SNAP_OPEN, "Open", s != NULL},
                {ACT_SNAP_COMPARE, "Compare with now", s != NULL},
                {ACT_SNAP_CHANGES, v->sel2 >= 0 ? "Changes between" : "What changed", s && l->manage},
                {ACT_SNAP_CREATE, "Create\xE2\x80\xA6", l->manage},
            };
            for (int i = 3; i >= 0; i--) {
                f32 w = roundf(text_width(f, bt[i].lb, (int)strlen(bt[i].lb)) + S(24));
                if (bx - w < b.x + b.w * 0.42f) continue; // narrow: the rest is in the right-click menu
                bx -= w;
                v->bact[v->nact] = (Rect){bx, by, w, bh};
                v->act[v->nact++] = bt[i].act;
                sheet_button((Rect){bx, by, w, bh}, bt[i].lb, false, bt[i].on);
                bx -= roundf(S(6));
            }
        }
        // icon + what this is
        f32 is = roundf(S(14)), ix = b.x + pad, iy = roundf(b.y + (b.h - is) * 0.5f), t = MAX(1.f, roundf(S(1.4f)));
        draw_rrect_outline(ix, iy, is, is, is * 0.5f, t, C(COL_ACCENT)); // a clock
        draw_line(ix + is * 0.5f, iy + is * 0.5f, ix + is * 0.5f, iy + is * 0.22f, t, C(COL_ACCENT));
        draw_line(ix + is * 0.5f, iy + is * 0.5f, ix + is * 0.72f, iy + is * 0.5f, t, C(COL_ACCENT));
        char msg[PATH_MAX + 200];
        size_t o;
        if (v->mode == 1) {
            o = (size_t)snprintf(msg, sizeof msg, "What changed from #%d to ", v->ca);
            o += (size_t)(v->cb ? snprintf(msg + o, sizeof msg - o, "#%d", v->cb) : snprintf(msg + o, sizeof msg - o, "now"));
            o += (size_t)snprintf(msg + o, sizeof msg - o, "  \xC2\xB7  %d item%s", v->nch, v->nch == 1 ? "" : "s");
        } else {
            o = (size_t)snprintf(msg, sizeof msg, "Snapshots of %s", ready && l->mnt[0] ? l->mnt : p->path);
            if (!ready) o += (size_t)snprintf(msg + o, sizeof msg - o, "  \xC2\xB7  looking\xE2\x80\xA6");
            else if (l->tool && l->noaccess && !l->n) o += (size_t)snprintf(msg + o, sizeof msg - o, "  \xC2\xB7  %s  \xC2\xB7  only the administrator can see them", l->tool);
            else if (l->tool) o += (size_t)snprintf(msg + o, sizeof msg - o, "  \xC2\xB7  %s  \xC2\xB7  %d snapshot%s%s", l->tool, l->n, l->n == 1 ? "" : "s",
                                                    v->sel2 >= 0 ? "  \xC2\xB7  two picked" : "");
        }
        if (v->cmd) snprintf(msg + o, sizeof msg - o, "  \xC2\xB7  %s",
                             v->cmd_kind == SC_STATUS ? "comparing\xE2\x80\xA6" : v->cmd_kind == SC_ALLOW ? "waiting for the password\xE2\x80\xA6" : "working\xE2\x80\xA6");
        f32 tx = ix + is + S(10);
        draw_text_fit(fs, tx, text_y(fs, b.y, b.h), bx - tx - S(8), msg, (int)strlen(msg), C(COL_TEXT2), 0);
    }

    Rect L = v->list;
    if (!ready) { draw_clip_pop(cs0); return; }
    if (v->mode == 0 && !l->n) { // nothing to list: why, and the way in when it's only root that may look
        const char *m = l->note;
        int st[6], ln[6];
        f32 mw = MIN(L.w - S(40), S(520));
        int nl = wrap_text(f, m, mw, st, ln, 6);
        f32 y = L.y + roundf(S(40));
        for (int i = 0; i < nl; i++) {
            f32 w = text_width(f, m + st[i], ln[i]);
            draw_text(f, roundf(L.x + (L.w - w) * 0.5f), y, m + st[i], ln[i], C(COL_TEXT2));
            y += roundf(f->line_h * 1.3f);
        }
        if (l->noaccess && l->config[0]) {
            const char *bl = "Allow me to browse snapshots";
            f32 w = roundf(text_width(f, bl, (int)strlen(bl)) + S(32)), h2 = roundf(S(32));
            v->ballow = (Rect){roundf(L.x + (L.w - w) * 0.5f), y + roundf(S(14)), w, h2};
            sheet_button(v->ballow, bl, true, !v->cmd);
            const char *h = "Asks for the administrator's password once: Snapper then lets you see, compare, create and delete its snapshots. "
                            "They stay read-only; files that are private stay private.";
            int nh = wrap_text(fs, h, mw, st, ln, 6);
            f32 hy = v->ballow.y + h2 + roundf(S(14));
            for (int i = 0; i < nh; i++) {
                f32 w2 = text_width(fs, h + st[i], ln[i]);
                draw_text(fs, roundf(L.x + (L.w - w2) * 0.5f), hy, h + st[i], ln[i], C(COL_TEXT_DIM));
                hy += fs->line_h;
            }
        }
        draw_clip_pop(cs0);
        return;
    }
    ClipSave cs = draw_clip_push((SDL_Rect){(int)L.x, (int)L.y, (int)L.w, (int)L.h});
    int hov = A.drag == DRAG_NONE && !A.menu.open && !A.dlg.open ? snap_row_at(v, A.mx, A.my) : -1;
    f32 rh = snap_row_h(), is = roundf(S(16));
    u32 sel_cur = active ? COL_SEL_FOCUS : COL_SEL_IDLE;
    int n = v->mode == 1 ? v->nch : l->n;
    for (int i = MAX(0, (int)(v->scroll / rh)); i < n; i++) {
        f32 y = roundf(L.y + (f32)((f64)i * rh - v->scroll));
        if (y > L.y + L.h) break;
        bool cur = i == (v->mode ? v->chcur : v->cur), two = !v->mode && i == v->sel2;
        if (A.alt_rows && (i & 1) && !cur) draw_rect(L.x, y, L.w, rh, C(COL_ALT_ROW));
        if (cur) draw_rrect(rx, y + 1, rw, rh - 2, S(5), C(sel_cur));
        else if (two) draw_rrect(rx, y + 1, rw, rh - 2, S(5), C(active ? COL_SEL : COL_SEL_IDLE));
        else if (i == hov) draw_rrect(rx, y + 1, rw, rh - 2, S(5), C(COL_HOVER));
        Color tc = C(cur && active ? COL_SEL_TEXT : COL_TEXT), dc = C(cur && active ? COL_SEL_TEXT : COL_TEXT_DIM);
        f32 iy = roundf(y + (rh - is) * 0.5f), ty = text_y(f, y, rh), tys = text_y(fs, y, rh);
        if (v->mode == 1) { // what changed: + added, - deleted, c changed (t/p/u/g: type, permissions, owner)
            SnapChange *c = &v->ch[i];
            u32 col = c->st == '+' ? COL_OK : c->st == '-' ? COL_DANGER : c->st == 'c' ? COL_WARN : COL_TEXT_DIM;
            const char *what = c->st == '+' ? "added" : c->st == '-' ? "deleted" : c->st == 'c' ? "changed" : "permissions / owner";
            f32 chx = rx + rw - roundf(S(120));
            draw_text(fs, chx, tys, what, (int)strlen(what), C(col));
            const char *nm = path_basename(c->path);
            if (!draw_icon_for_name(c0, iy, is, nm, false)) draw_file_icon(c0, iy, is, 0x8C8F99);
            draw_path_fit(f, c0 + is + S(8), ty, chx - S(16) - (c0 + is + S(8)), c->path, false);
            continue;
        }
        SnapInfo *si = &l->s[i];
        // the snapshot: a dot (filled for the "after" half of an update), its number
        bool post = !strcmp(si->type, "post"), pre = !strcmp(si->type, "pre");
        f32 dr = roundf(S(4)), dx = c0 + is * 0.5f - dr, dy = roundf(y + rh * 0.5f - dr);
        if (post || pre) draw_rrect(dx, dy, dr * 2, dr * 2, dr, C(post ? COL_ACCENT : COL_TEXT_FAINT));
        else draw_rrect_outline(dx, dy, dr * 2, dr * 2, dr, MAX(1.f, S(1.2f)), C(COL_TEXT_FAINT));
        draw_text_fit(f, c0 + is + S(8), ty, c1 - S(12) - (c0 + is + S(8)), si->num ? si->name : "", si->num ? (int)strlen(si->name) : 0, dc, 0);
        char d[64], tl[48];
        if (si->when) props_date(si->when, d, sizeof d);
        else snprintf(d, sizeof d, "%s", si->name);
        draw_text_fit(f, c1, ty, c2 - c1 - S(14), d, (int)strlen(d), tc, 0);
        snap_type_label(si, tl, sizeof tl);
        if (tl[0]) draw_text_fit(f, c2, ty, c3 - c2 - S(14), tl, (int)strlen(tl), dc, 0);
        const char *desc = si->desc[0] ? si->desc : !strcmp(si->type, "zfs") || !strcmp(si->type, "timeshift") ? si->name : "";
        draw_text_fit(f, c3, ty, rx + rw - c3 - S(10), desc, (int)strlen(desc), tc, 0);
    }
    draw_clip_pop(cs);
    draw_clip_pop(cs0);
}

// Inside a snapshot: which one, that it's read-only, and the way back to now.
static void draw_snapbar(Pane *p) {
    PaneLayout *ly = &p->ly;
    if (!ly->has_snapbar) return;
    Rect b = ly->snapbar;
    Font *fs = &A.font_small;
    draw_rect(b.x, b.y, b.w, b.h, C(rgb_mix(COL_TOOLBAR, COL_ACCENT, 0.1f)));
    draw_rect(b.x, b.y, b.w, 1, C(COL_BORDER));
    char root[PATH_MAX], live[PATH_MAX], nm[96], msg[PATH_MAX + 160], tl[PATH_MAX];
    if (!snap_where(p->path, root, sizeof root, live, sizeof live, nm, sizeof nm)) return;
    Rect bc, br, ba;
    snapbar_layout(p, &bc, &br, &ba);
    f32 is = roundf(S(14)), ix = b.x + S(14), iy = roundf(b.y + (b.h - is) * 0.5f), t = MAX(1.f, roundf(S(1.4f)));
    draw_rrect_outline(ix, iy, is, is, is * 0.5f, t, C(COL_ACCENT)); // a clock
    draw_line(ix + is * 0.5f, iy + is * 0.5f, ix + is * 0.5f, iy + is * 0.22f, t, C(COL_ACCENT));
    draw_line(ix + is * 0.5f, iy + is * 0.5f, ix + is * 0.72f, iy + is * 0.5f, t, C(COL_ACCENT));
    tilde_path(live, tl, sizeof tl);
    snprintf(msg, sizeof msg, "Snapshot %s  \xC2\xB7  read-only  \xC2\xB7  now at %s", nm, tl);
    f32 tx = ix + is + S(10);
    draw_text_fit(fs, tx, text_y(fs, b.y, b.h), bc.x - tx - S(8), msg, (int)strlen(msg), C(COL_TEXT2), 0);
    sheet_button(bc, "Compare with now", false, true);
    sheet_button(br, "Restore\xE2\x80\xA6", false, true);
    sheet_button(ba, "All snapshots", false, true);
}

static void draw_list(Pane *p, bool active) {
    Listing *L = p->L;
    PaneLayout *ly = &p->ly;
    Rect lr = ly->list;
    if (pane_du_on(p)) { draw_diskuse(p, active); return; }
    if (pane_snap_on(p)) { draw_snapview(p, active); return; }
    draw_rect(lr.x, lr.y, lr.w, lr.h, C(COL_BG));
    if (!L) return;
    int st = atomic_load(&L->state);
    if (st == LST_ERROR) {
        char msg[256];
        snprintf(msg, sizeof msg, "Can't open this folder: %s", strerror(L->err));
        draw_message(p, msg);
        return;
    }
    if (!p->order) {
        if (now_ns() - L->t_start > 120000000ull) {
            char n[32], msg[64];
            fmt_count(n, atomic_load(&L->scanned));
            snprintf(msg, sizeof msg, "Loading\xE2\x80\xA6 %s items", n);
            draw_message(p, msg);
        }
        return;
    }
    if (!p->count) {
        if (L->is_search) {
            char msg[400];
            if (st == LST_COMPLETE) snprintf(msg, sizeof msg, "No results for \xE2\x80\x9C%s\xE2\x80\x9D", p->search_q);
            else snprintf(msg, sizeof msg, "Searching\xE2\x80\xA6");
            draw_message(p, msg);
        } else {
            if (p->search_q[0]) {
                char msg[400];
                snprintf(msg, sizeof msg, "Nothing here matches \xE2\x80\x9C%s\xE2\x80\x9D  \xC2\xB7  turn on Subfolders to look deeper", p->search_q);
                draw_message(p, msg);
            } else {
                draw_message(p, L->count ? "Only hidden items here (Ctrl+H to show)" : "This folder is empty");
            }
        }
        return;
    }

    SDL_Rect clip = {(int)lr.x, (int)lr.y, (int)lr.w, (int)lr.h};
    draw_clip(&clip);
    Font *f = &A.font;
    f32 rh = ly->row_h, is = roundf(S(16));
    f32 rx = lr.x + roundf(S(6)), rw = ly->track.x - rx - roundf(S(2));
    i64 first = (i64)(p->scroll / rh);
    bool hovering = A.mouse_in && A.drag == DRAG_NONE && in_rect(lr, A.mx, A.my) && !in_rect(ly->track, A.mx, A.my);
    i64 hover = hovering ? item_at(p, A.mx, A.my) : -1;
    ly->hover = hover;
    ly->drawn = 0;
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    u32 sel_col = active ? COL_SEL : COL_SEL_IDLE, sel_cur = active ? COL_SEL_FOCUS : COL_SEL_IDLE;
    GitView gv;
    git_view(p, &gv);
    u64 size_max = 0; // for size bars: largest item in the folder (visible rows only for huge folders)
    if (A.size_bars && col_visible(COL_SIZE)) {
        i64 from = p->count <= 200000 ? 0 : first, to = p->count <= 200000 ? (i64)p->count : MIN((i64)p->count, first + 200);
        for (i64 i = from; i < to; i++) {
            Entry *e = &L->entries[p->order[i]];
            if (atomic_load_explicit(&e->meta, memory_order_acquire) != META_READY || (e->flags & ENT_MATCH)) continue;
            u64 sz = (e->flags & ENT_DIR) ? entry_dir_size(e, NULL) : e->size;
            if (sz > size_max) size_max = sz;
        }
    }

    if (p->view != VIEW_DETAILS) draw_cells(p, active, hover, sel_col, sel_cur);
    i64 i = p->nhdr ? grp_first_pos(p, first) : first;
    int hi = p->nhdr ? grp_first_hdr(p, first) : 0;
    for (i64 row = first; p->view == VIEW_DETAILS; row++, i++) {
        f32 y = roundf((f32)((f64)lr.y + (f64)row * rh - p->scroll));
        if (y >= lr.y + lr.h) break;
        if (p->nhdr && hi < p->nhdr && (i64)p->hdr[hi].row == row) { // group header row
            GroupHdr *gh = &p->hdr[hi++];
            i--;
            bool hh = A.mouse_in && A.drag == DRAG_NONE && in_rect((Rect){lr.x, y, ly->track.x - lr.x, rh}, A.mx, A.my);
            if (hh) draw_rrect(rx, y + 1, rw, rh - 2, S(5), C(COL_HOVER));
            f32 cxh = ly->col_name + S(6), cyh = y + rh * 0.5f;
            draw_chevron(cxh, cyh, S(7), gh->collapsed ? 1 : 3, MAX(1.f, S(1.4f)), C(COL_ACCENT));
            char lab[96];
            snprintf(lab, sizeof lab, "%s", gh->label);
            f32 tx = ly->col_name + S(20);
            f32 ex = draw_text(f, tx, text_y(f, y, rh), lab, (int)strlen(lab), C(COL_GROUP_TEXT));
            char cnt[32];
            fmt_count(cnt, gh->count);
            f32 ex2 = draw_text(&A.font_small, ex + S(8), text_y(&A.font_small, y, rh), cnt, (int)strlen(cnt), C(COL_TEXT_FAINT));
            draw_rect(ex2 + S(10), roundf(cyh), MAX(0.f, ly->track.x - S(12) - ex2 - S(10)), 1, C(COL_BORDER));
            continue;
        }
        if (i >= (i64)p->count) break;
        u32 ei = p->order[i];
        Entry *e = &L->entries[ei];
        bool sel = p->sel[ei], cur = i == p->cursor;
        ly->drawn++;
        if (A.alt_rows && (i & 1) && !sel) draw_rect(lr.x, y, ly->track.x - lr.x, rh, C(COL_ALT_ROW));
        if (sel) draw_rrect(rx, y + 1, rw, rh - 2, S(5), C(cur ? sel_cur : sel_col));
        else if (i == hover) draw_rrect(rx, y + 1, rw, rh - 2, S(5), C(COL_HOVER));
        if (cur && !sel && active) draw_rrect_outline(rx, y + 1, rw, rh - 2, S(5), MAX(1.f, S(1.f)), CA(COL_ACCENT, 0.7f));
        if (A.drop_pane == p && A.drop_row == i) {
            draw_rrect(rx, y + 1, rw, rh - 2, S(5), CA(COL_ACCENT, 0.22f));
            draw_rrect_outline(rx, y + 1, rw, rh - 2, S(5), MAX(1.f, S(1.3f)), C(COL_ACCENT));
        }
        bool cut = clip_is_cut(p->path, e->name);
        if (cut) g_alpha = 0.45f;
        else if (cmp_entry_state(p, e) == CMP_SAME) g_alpha = 0.5f; // compared: the same on both sides, in the background
        if (e->flags & ENT_MATCH) { // contents search: a matching line under its file
            draw_match_row(p, e, ei, y, rh, sel && active);
            g_alpha = 1.f;
            continue;
        }

        f32 iy = roundf(y + (rh - is) * 0.5f);
        bool hidden = e->flags & ENT_HIDDEN;
        f32 ind = tree_indent(L, e) + (L->is_content ? roundf(S(14)) : 0), icx = ly->col_name + ind; // tree view: chevron + indent
        if (L->is_content && (L->cinfo[ei] & ~CI_COLLAPSED)) // fold chevron of a file's lines
            draw_chevron(ly->col_name + roundf(S(5)), y + rh * 0.5f, S(4), (L->cinfo[ei] & CI_COLLAPSED) ? 1 : 3, MAX(1.f, S(1.4f)),
                         sel && active ? C(COL_SEL_DIM) : C(COL_TEXT_DIM));
        if (ind > 0 && (e->flags & ENT_DIR) && L->tree) {
            bool open = tree_has(L->tree, e->name, e->name_len);
            draw_chevron(icx - roundf(S(9)), y + rh * 0.5f, S(4), open ? 3 : 1, MAX(1.f, S(1.4f)),
                         sel && active ? C(COL_SEL_DIM) : C(COL_TEXT_DIM));
        }
        if (ind > 0 && L->tree) { // guide lines for nested rows
            int d = rel_depth(e->name);
            for (int k = 0; k < d; k++)
                draw_rect(roundf(ly->col_name + roundf(S(16)) * (f32)k + roundf(S(7))), y, 1, rh, CA(COL_TEXT_FAINT, 0.35f));
        }
        if (e->flags & ENT_DIR) draw_dir_icon(icx, iy, is, rel_base(e->name), hidden);
        else if (!draw_icon_for_name(icx, iy, is, rel_base(e->name), e->mflags & MF_EXEC)) draw_file_icon(icx, iy, is, file_color(e));
        if (e->flags & ENT_LINK) { // small link badge
            f32 bs = roundf(S(7));
            draw_rrect(icx + is - bs + S(2), iy + is - bs + S(1), bs, bs, bs * 0.5f, C(COL_BG));
            draw_rrect(icx + is - bs + S(3), iy + is - bs + S(2), bs - S(2), bs - S(2), bs * 0.4f, C(0x7FB2FF));
        }

        f32 ty = text_y(f, y, rh);
        f32 nx = icx + is + roundf(S(9));
        bool bright = sel && active;
        Color name_c = bright ? C(COL_SEL_TEXT) : hidden ? C(COL_TEXT_HIDDEN) : C(COL_TEXT);
        u8 gs = git_entry_state(&gv, e), cs = cmp_entry_state(p, e);
        if (cs != CMP_UNKNOWN) gs = GS_NONE; // comparing: the compare marks take the place of git's
        name_c = git_name_color(gs, name_c, bright);
        f32 gw = git_badge_w(gs) + cmp_badge_w(cs), name_end = ly->name_end - gw; // the badge sits at the end of the Name column
        if (gs > GS_IGNORED && !(A.ren_pane == p && A.ren_L == L && A.ren_entry == ei))
            draw_git_badge(gs, e->flags & ENT_DIR, ly->name_end - S(8), y, rh, bright);
        if (cmp_marked(cs) && !(A.ren_pane == p && A.ren_L == L && A.ren_entry == ei))
            draw_cmp_badge(cs, ly->name_end - S(6), y, rh, bright);
        if (A.ren_pane == p && A.ren_L == L && A.ren_entry == ei) {
            Rect fr = {nx - roundf(S(5)), y + roundf(S(2)), ly->name_end - S(6) - (nx - roundf(S(5))), rh - roundf(S(4))};
            A.ren_rect = fr;
            draw_rrect(fr.x, fr.y, fr.w, fr.h, S(4), C(COL_FIELD_HOVER));
            draw_rrect_outline(fr.x, fr.y, fr.w, fr.h, S(4), MAX(1.f, S(1.2f)), C(COL_ACCENT));
            te_draw(&A.ren_te, f, fr, roundf(S(5)), C(COL_TEXT));
        } else if (pane_is_trash(p)) { // name + where it came from + when
            char orig[PATH_MAX], date[32], par[PATH_MAX], tp[PATH_MAX], extra[PATH_MAX + 48];
            f32 maxw = ly->name_end - nx - S(10);
            f32 ex = nx + MIN(text_width(f, e->name, (int)e->name_len), maxw);
            draw_text_fit(f, nx, ty, maxw, e->name, (int)e->name_len, name_c, 0);
            if (trash_info(e->name, orig, sizeof orig, date, sizeof date)) {
                parent_of(orig, par, sizeof par);
                tilde_path(par, tp, sizeof tp);
                snprintf(extra, sizeof extra, "from %s  \xC2\xB7  deleted %s", tp, date);
                if (ex + S(40) < nx + maxw)
                    draw_text_fit(&A.font_small, ex + S(12), text_y(&A.font_small, y, rh), nx + maxw - ex - S(12), extra,
                                  (int)strlen(extra), bright ? C(COL_SEL_DIM) : C(COL_TEXT_FAINT), 0);
            }
        } else if (L->is_content) { // file name, its folder (dim), how many lines match
            const char *slash = strrchr(e->name, '/');
            int boff = slash ? (int)(slash - e->name) + 1 : 0, blen = (int)e->name_len - boff;
            u32 nm = L->cinfo[ei] & ~CI_COLLAPSED;
            char cnt[24];
            snprintf(cnt, sizeof cnt, nm >= CS_FILE_LINES ? "%u+" : "%u", nm);
            Font *fs = &A.font_small;
            f32 maxw = name_end - nx - S(10);
            f32 bw = MIN(text_width(f, e->name + boff, blen), maxw);
            draw_text_fit(f, nx, ty, maxw, e->name + boff, blen, name_c, 0);
            f32 cwid = text_width(fs, cnt, (int)strlen(cnt)) + S(12), cx0 = nx + bw + S(10);
            if (cx0 + cwid < nx + maxw) {
                draw_rrect(cx0, y + S(5), cwid, rh - S(10), (rh - S(10)) * 0.5f, bright ? CA(COL_SEL_TEXT, 0.18f) : C(COL_CHIP));
                draw_text(fs, cx0 + S(6), text_y(fs, y, rh), cnt, (int)strlen(cnt), bright ? C(COL_SEL_TEXT) : C(COL_TEXT_DIM));
                f32 fx = cx0 + cwid + S(10);
                if (boff && fx + S(30) < nx + maxw)
                    draw_text_fit(f, fx, ty, nx + maxw - fx, e->name, boff - 1, bright ? C(COL_SEL_DIM) : C(COL_TEXT_FAINT), 0);
            }
        } else if (p->search_q[0]) {
            // basename with fuzzy-matched letters highlighted (+ dim subfolder for recursive results)
            const char *slash = L->is_search ? strrchr(e->name, '/') : NULL;
            int boff = slash ? (int)(slash - e->name) + 1 : 0, blen = (int)e->name_len - boff;
            f32 maxw = name_end - nx - S(10);
            u16 pos[FZ_MAXPOS];
            int npos = 0;
            int qn = (int)strlen(p->search_q);
            if (!q_is_glob(p->search_q) && fuzzy_match(e->name + boff, blen, p->search_q, qn, pos) >= 0) npos = MIN(qn, FZ_MAXPOS);
            f32 ex = draw_text_hl(f, nx, ty, maxw, e->name + boff, blen, 0, pos, npos, name_c,
                                  bright ? C(COL_SEL_HL) : C(COL_ACCENT));
            f32 bw = ex - nx;
            if (boff && bw + S(30) < maxw)
                draw_text_fit(f, nx + bw + S(12), ty, maxw - bw - S(12), e->name, boff - 1, bright ? C(COL_SEL_DIM) : C(COL_TEXT_FAINT), 0);
        } else {
            const char *b = L->tree ? rel_base(e->name) : e->name;
            draw_name_pill(p, e, f, nx, y, rh, name_end - nx - S(10), 0, b, (int)strlen(b), name_c, bright);
        }

        Color dim = bright ? C(COL_SEL_DIM) : C(COL_TEXT_DIM);
        char buf[64];
        bool meta = atomic_load_explicit(&e->meta, memory_order_acquire) == META_READY;
        for (int ci = 0; ci < ly->ncols; ci++) {
            int c = ly->cid[ci];
            if (c == COL_NAME) continue;
            f32 x0 = ly->cx0[ci] + S(10), cw = ly->cx1[ci] - ly->cx0[ci] - S(16);
            buf[0] = 0;
            Color tc = dim;
            switch (c) {
            case COL_DATE: if (meta) fmt_date(e->mtime, buf, sizeof buf, &tm_now); break;
            case COL_CREATED: if (meta) fmt_date(e->btime, buf, sizeof buf, &tm_now); break;
            case COL_TYPE: fmt_type(e, buf, sizeof buf); break;
            case COL_EXT:
                if (!(e->flags & ENT_DIR) && e->ext) {
                    size_t k = 0;
                    for (const char *q = e->name + e->ext; *q && k + 1 < sizeof buf; q++) buf[k++] = (char)ascii_lower((u8)*q);
                    buf[k] = 0;
                }
                break;
            case COL_PERMS: if (meta) fmt_perms(e, buf); break;
            case COL_OWNER: if (meta) snprintf(buf, sizeof buf, "%s", owner_name(e->uid)); break;
            case COL_SIZE: {
                u64 sz = 0;
                bool have = false;
                if (meta && !(e->flags & ENT_DIR)) { sz = e->size; have = true; }
                else if ((e->flags & ENT_DIR) && A.folder_sizes && !L->is_search) {
                    sz = entry_dir_size(e, &have);
                    if (!have && L->dirsize_started && !(e->flags & ENT_LINK)) snprintf(buf, sizeof buf, "\xE2\x80\xA6");
                    tc = bright ? C(COL_SEL_DIM) : C(COL_TEXT_DIM);
                }
                if (have) {
                    fmt_size(buf, sizeof buf, sz);
                    if (A.size_bars && size_max > 0 && sz > 0) { // TreeSize-style bar
                        f32 bw = (f32)((f64)(cw + S(6)) * ((f64)sz / (f64)size_max));
                        draw_rrect(ly->cx0[ci] + S(4), y + S(5), MAX(S(2), bw), rh - S(10), S(3),
                                   CA(bright ? COL_SEL_TEXT : COL_ACCENT, bright ? 0.18f : 0.22f));
                    }
                }
                break;
            }
            }
            if (buf[0]) draw_text_fit(f, x0, ty, cw, buf, (int)strlen(buf), tc, g_coldef[c].right);
        }
        g_alpha = 1.f;
    }

    // rubber band
    if (A.drag == DRAG_BAND && A.drag_pane == p) {
        f32 y0 = (f32)(A.band_y0 - p->scroll) + lr.y;
        f32 x1 = CLAMP(A.mx, lr.x, lr.x + lr.w), y1 = CLAMP(A.my, lr.y, lr.y + lr.h);
        f32 bx = MIN(A.band_x0, x1), by = MIN(y0, y1), bw = fabsf(x1 - A.band_x0), bh = fabsf(y1 - y0);
        draw_rect(bx, by, bw, bh, CA(COL_ACCENT, 0.16f));
        draw_rect(bx, by, bw, 1, CA(COL_ACCENT, 0.8f));
        draw_rect(bx, by + bh - 1, bw, 1, CA(COL_ACCENT, 0.8f));
        draw_rect(bx, by, 1, bh, CA(COL_ACCENT, 0.8f));
        draw_rect(bx + bw - 1, by, 1, bh, CA(COL_ACCENT, 0.8f));
    }

    if (A.drop_pane == p && A.drop_row < 0 && A.drop_dir[0])
        draw_rrect_outline(lr.x + S(3), lr.y + S(3), lr.w - S(6), lr.h - S(6), S(6), MAX(1.f, S(1.5f)), C(COL_ACCENT));

    if (ly->has_scroll) {
        bool h = (A.drag == DRAG_SCROLL && A.drag_pane == p) || (A.mouse_in && in_rect(ly->track, A.mx, A.my));
        f32 w = h ? roundf(S(8)) : roundf(S(5));
        Rect t = ly->thumb;
        draw_rrect(t.x + (t.w - w) * 0.5f, t.y + S(2), w, t.h - S(4), w * 0.5f, C(h ? COL_SCROLL_HOVER : COL_SCROLL));
    }
    draw_clip(NULL);
}

static void draw_tree_icon(f32 x, f32 y, f32 s, Color c) {
    // folder with two nested children: "includes subfolders"
    f32 t = MAX(1.f, roundf(S(1.3f)));
    draw_rrect(x, y, s * 0.5f, s * 0.36f, s * 0.07f, c);
    draw_rect(roundf(x + s * 0.18f), y + s * 0.36f, t, s * 0.5f, c);
    draw_rect(roundf(x + s * 0.18f), roundf(y + s * 0.58f), s * 0.22f, t, c);
    draw_rect(roundf(x + s * 0.18f), roundf(y + s * 0.86f) - t, s * 0.22f, t, c);
    draw_rrect(x + s * 0.44f, y + s * 0.47f, s * 0.42f, s * 0.24f, s * 0.05f, c);
    draw_rrect(x + s * 0.44f, y + s * 0.74f, s * 0.42f, s * 0.24f, s * 0.05f, c);
}

// A labelled on/off switch in the search bar: icon 0 = subfolders (tree), 1 = contents (text lines).
static void draw_bar_switch(Rect t, const char *label, bool on, int icon) {
    Font *fs = &A.font_small;
    bool th = A.mouse_in && in_rect(t, A.mx, A.my);
    if (th) draw_rrect(t.x, t.y, t.w, t.h, S(7), C(COL_FIELD));
    f32 is = roundf(S(14)), ix = t.x + S(8), iy = roundf(t.y + (t.h - is) * 0.5f);
    Color ic = on ? C(COL_ACCENT) : C(COL_TEXT_DIM);
    if (icon == 0) draw_tree_icon(ix, iy, is, ic);
    else if (icon == 2) { // differences: not-equal sign
        f32 lw = MAX(1.f, roundf(S(1.4f)));
        draw_rect(ix + S(2), roundf(iy + is * 0.36f), is - S(4), lw, ic);
        draw_rect(ix + S(2), roundf(iy + is * 0.62f), is - S(4), lw, ic);
        draw_line(ix + is * 0.68f, iy + S(2), ix + is * 0.32f, iy + is - S(2), lw, ic);
    } else { // a page with lines of text
        f32 lw = MAX(1.f, roundf(S(1.3f)));
        draw_rrect_outline(ix + S(1), iy, is - S(2), is, S(2), lw, ic);
        for (int k = 0; k < 3; k++) draw_rect(ix + S(4), roundf(iy + S(4) + k * S(3)), is - S(8) - (k == 2 ? S(2) : 0), lw, ic);
    }
    f32 lx = ix + is + S(8);
    draw_text(fs, lx, text_y(fs, t.y, t.h), label, (int)strlen(label), on ? C(COL_TEXT) : C(COL_TEXT_DIM));
    f32 sw = roundf(S(30)), sh = roundf(S(16));
    f32 sx = t.x + t.w - sw - S(8), sy = roundf(t.y + (t.h - sh) * 0.5f);
    draw_rrect(sx, sy, sw, sh, sh * 0.5f, on ? C(th ? COL_ACCENT_HOVER : COL_ACCENT) : C(th ? COL_CONTROL_HOVER : COL_CHIP));
    f32 kr = sh * 0.5f - S(2);
    f32 kx = on ? sx + sw - sh * 0.5f : sx + sh * 0.5f;
    draw_rrect(kx - kr, sy + sh * 0.5f - kr, kr * 2, kr * 2, kr, C(on ? COL_ON_ACCENT : COL_KNOB_OFF));
}

static void draw_searchbar(Pane *p) {
    PaneLayout *ly = &p->ly;
    if (!ly->has_sbar) return;
    Rect sb = ly->sbar;
    Font *f = &A.font, *fs = &A.font_small;
    draw_rect(sb.x, sb.y, sb.w, sb.h, C(COL_TOOLBAR));
    draw_rect(sb.x, sb.y, sb.w, 1, C(COL_BORDER));
    Rect field, toggle, close, cont, opt[3];
    search_bar_layout(p, &field, &toggle, &close, &cont, opt);
    bool focused = A.search_pane == p;
    draw_rrect(field.x, field.y, field.w, field.h, S(7), C(focused ? COL_FIELD_HOVER : COL_FIELD));
    if (focused) draw_rrect_outline(field.x, field.y, field.w, field.h, S(7), MAX(1.f, S(1.2f)), C(COL_ACCENT));
    // magnifier
    f32 r = roundf(S(5)), cx = field.x + S(15), cy = field.y + field.h * 0.5f - S(1);
    Color mc = focused ? C(COL_ACCENT) : C(COL_TEXT_DIM);
    draw_rrect_outline(cx - r, cy - r, r * 2, r * 2, r, MAX(1.f, S(1.5f)), mc);
    draw_line(cx + r * 0.7f, cy + r * 0.7f, cx + r * 1.6f, cy + r * 1.6f, MAX(1.5f, S(1.6f)), mc);
    // result count on the right of the field
    char cnt[64] = "";
    Listing *L = p->L;
    if (L && p->order && p->search_q[0]) {
        char a[32], b[32];
        fmt_count(a, p->count);
        if (L->is_content) {
            int st = atomic_load(&L->state);
            fmt_count(a, L->nfiles);
            fmt_count(b, L->nmatches);
            snprintf(cnt, sizeof cnt, "%s file%s \xC2\xB7 %s match%s%s", a, L->nfiles == 1 ? "" : "s", b, L->nmatches == 1 ? "" : "es",
                     st == LST_COMPLETE ? "" : "\xE2\x80\xA6");
        } else if (L->is_search) {
            int st = atomic_load(&L->state);
            snprintf(cnt, sizeof cnt, "%s%s", a, st == LST_COMPLETE ? (p->count == 1 ? " result" : " results") : "\xE2\x80\xA6");
        } else {
            u32 total = 0;
            for (u32 i = 0; i < L->count; i++) total += A.show_hidden || !(L->entries[i].flags & ENT_HIDDEN);
            fmt_count(b, total);
            snprintf(cnt, sizeof cnt, "%s of %s", a, b);
        }
    }
    bool err = A.search_contents && A.cs_error[0];
    if (err) snprintf(cnt, sizeof cnt, "%s", A.cs_error);
    f32 cw = cnt[0] ? text_width(fs, cnt, (int)strlen(cnt)) + S(12) : 0;
    if (cnt[0]) draw_text(fs, field.x + field.w - cw, text_y(fs, field.y, field.h), cnt, (int)strlen(cnt), C(err ? COL_DANGER : COL_TEXT_FAINT));
    Rect tf = {field.x + S(28), field.y, field.w - S(28) - cw - S(4), field.h};
    const char *txt = focused ? A.search_te.buf : p->search_q;
    if (focused) te_draw(&A.search_te, f, tf, roundf(S(4)), C(COL_TEXT));
    else draw_text_fit(f, tf.x + S(4), text_y(f, tf.y, tf.h), tf.w - S(8), txt, (int)strlen(txt), C(COL_TEXT), 0);
    if (!txt[0]) {
        char ph[NAME_MAX + 96];
        const char *dn = p->path[1] ? path_basename(p->path) : "/";
        if (A.search_contents)
            snprintf(ph, sizeof ph, "Search inside files in \xE2\x80\x9C%s\xE2\x80\x9D%s   (in:*.c limits the files)", dn,
                     A.search_recursive ? " and subfolders" : "");
        else if (A.search_recursive) snprintf(ph, sizeof ph, "Search \xE2\x80\x9C%s\xE2\x80\x9D and subfolders", dn);
        else snprintf(ph, sizeof ph, "Filter \xE2\x80\x9C%s\xE2\x80\x9D", dn);
        draw_text_fit(f, tf.x + S(6), text_y(f, tf.y, tf.h), tf.w - S(10), ph, (int)strlen(ph), C(COL_TEXT_FAINT), 0);
    }
    draw_bar_switch(toggle, "Subfolders", A.search_recursive, 0);
    draw_bar_switch(cont, "Contents", A.search_contents, 1);
    if (A.search_contents) { // match case, whole word, regular expression
        static const char *const lab[3] = {"Aa", "ab", ".*"}; // Alt+C, Alt+W, Alt+X
        bool on3[3] = {A.cs_case, A.cs_word, A.cs_regex};
        for (int i = 0; i < 3; i++) {
            Rect o = opt[i];
            bool hv = A.mouse_in && in_rect(o, A.mx, A.my);
            if (on3[i] || hv) draw_rrect(o.x, o.y, o.w, o.h, S(6), on3[i] ? CA(COL_ACCENT, 0.25f) : C(COL_FIELD));
            Color tc = on3[i] ? C(COL_ACCENT) : C(hv ? COL_TEXT : COL_TEXT_DIM);
            f32 lw = text_width(fs, lab[i], 2), lx2 = roundf(o.x + (o.w - lw) * 0.5f);
            draw_text(fs, lx2, text_y(fs, o.y, o.h), lab[i], 2, tc);
            if (i == 1) { // word boundaries: little bars on both sides of "ab"
                f32 by = roundf(o.y + o.h * 0.5f - S(5));
                draw_rect(lx2 - S(3), by, MAX(1.f, S(1)), S(10), tc);
                draw_rect(lx2 + lw + S(2), by, MAX(1.f, S(1)), S(10), tc);
            }
        }
    }
    // close
    bool ch = A.mouse_in && in_rect(close, A.mx, A.my);
    if (ch) draw_rrect(close.x, close.y, close.w, close.h, S(6), C(COL_CHIP));
    draw_cross(close.x + close.w * 0.5f, close.y + close.h * 0.5f, S(7), MAX(1.f, S(1.3f)), C(ch ? COL_TEXT : COL_TEXT_DIM));
}

static void draw_trashbar(Pane *p) {
    PaneLayout *ly = &p->ly;
    if (!ly->has_trashbar) return;
    Rect tb = ly->trashbar;
    Font *fs = &A.font_small;
    draw_rect(tb.x, tb.y, tb.w, tb.h, C(COL_TOOLBAR));
    draw_rect(tb.x, tb.y, tb.w, 1, C(COL_BORDER));
    char msg[96], n[32];
    int st = p->L ? atomic_load(&p->L->state) : LST_ERROR;
    u32 total = (st == LST_READY || st == LST_COMPLETE) ? p->L->count : 0; // count is published with the state
    fmt_count(n, total);
    snprintf(msg, sizeof msg, "Trash  \xC2\xB7  %s item%s", n, total == 1 ? "" : "s");
    draw_text_fit(fs, tb.x + S(14), text_y(fs, tb.y, tb.h), ly->rrestore.x - tb.x - S(24), msg, (int)strlen(msg), C(COL_TEXT_DIM), 0);
    sheet_button(ly->rrestore, p->sel_count ? "Restore" : "Restore all", false, p->count > 0);
    sheet_button(ly->rempty, "Empty Trash", false, total > 0);
}

static void draw_cmpbar(Pane *p) {
    PaneLayout *ly = &p->ly;
    if (!ly->has_cmpbar) return;
    Rect b = ly->cmpbar;
    Font *fs = &A.font_small;
    draw_rect(b.x, b.y, b.w, b.h, C(COL_TOOLBAR));
    draw_rect(b.x, b.y, b.w, 1, C(COL_BORDER));
    Rect sw, sync, close;
    cmp_bar_layout(p, &sw, &sync, &close);
    f32 is = roundf(S(14)), ix = b.x + S(14), iy = roundf(b.y + (b.h - is) * 0.5f), t = MAX(1.f, roundf(S(1.3f)));
    draw_rrect_outline(ix, iy, is * 0.42f, is, S(2), t, C(COL_ACCENT)); // two columns
    draw_rrect_outline(ix + is * 0.58f, iy, is * 0.42f, is, S(2), t, C(COL_ACCENT));
    char msg[256];
    cmp_bar_text(p, msg, sizeof msg);
    f32 tx = ix + is + S(10);
    draw_text_fit(fs, tx, text_y(fs, b.y, b.h), sw.x - tx - S(8), msg, (int)strlen(msg), C(COL_TEXT2), 0);
    draw_bar_switch(sw, b.w < S(620) ? "Differences" : "Differences only", g_cmp.diff_only, 2);
    sheet_button(sync, "Sync\xE2\x80\xA6", false, cmp_ready());
    bool ch = A.mouse_in && in_rect(close, A.mx, A.my);
    if (ch) draw_rrect(close.x, close.y, close.w, close.h, S(6), C(COL_CONTROL_HOVER));
    draw_cross(close.x + close.w * 0.5f, close.y + close.h * 0.5f, S(9), MAX(1.f, S(1.5f)), C(ch ? COL_TEXT : COL_TEXT_DIM));
}

static void draw_pane(Pane *p, bool active, bool split) {
    draw_cmpbar(p);
    draw_snapbar(p);
    draw_trashbar(p);
    draw_searchbar(p);
    draw_list(p, active || !split);
    draw_header(p);
    draw_toolbar(p, active, split);
}

// Gear: ring with 8 teeth and a hole (bg = colour behind it, used for the hole).
static void draw_gear_icon(f32 cx, f32 cy, f32 r, Color c, Color bg) {
    f32 t = MAX(1.5f, roundf(S(2.4f)));
    for (int k = 0; k < 8; k++) {
        f32 a = (f32)k * 3.14159265f / 4.f, ca = cosf(a), sa = sinf(a);
        draw_line(cx + ca * r * 0.55f, cy + sa * r * 0.55f, cx + ca * r * 1.02f, cy + sa * r * 1.02f, t, c);
    }
    f32 ro = r * 0.78f, ri = r * 0.34f;
    draw_rrect(cx - ro, cy - ro, ro * 2, ro * 2, ro, c);
    draw_rrect(cx - ri, cy - ri, ri * 2, ri * 2, ri, bg);
}

// Eye: two lid arcs meeting at pointed corners and a round pupil.
static void draw_eye_icon(f32 cx, f32 cy, f32 hw, Color c) {
    f32 t = MAX(1.f, roundf(S(1.4f))), h = hw * 0.62f;      // half width, lid height above/below the centre
    f32 R = (hw * hw + h * h) / (2.f * h), d = R - h, a = atan2f(hw, d); // lid circle through both corners
    for (int lid = 0; lid < 2; lid++) {
        f32 ocy = lid ? cy - d : cy + d, sg = lid ? 1.f : -1.f; // upper lid: circle centred below, and v.v.
        f32 px = cx - hw, py = cy;
        for (int k = 1; k <= 12; k++) {
            f32 ang = -a + 2.f * a * (f32)k / 12.f, x = cx + sinf(ang) * R, y = ocy + sg * cosf(ang) * R;
            draw_line(px, py, x, y, t, c);
            px = x; py = y;
        }
    }
    f32 rp = h * 0.6f;
    draw_rrect(cx - rp, cy - rp, rp * 2, rp * 2, rp, c);
}

static void draw_status_term(Term *t);


static void draw_status(void) {
    Pane *p = active_pane();
    Listing *L = p->L;
    Rect s = A.r_status;
    A.r_xfer = (Rect){0, 0, 0, 0};
    if (picker_active()) { draw_picker_bar(s); return; } // Open / Save / Choose folder bar
    if (active_term()) { draw_status_term(active_term()); return; }
    draw_rect(s.x, s.y, s.w, s.h, C(COL_TOOLBAR));
    draw_rect(s.x, s.y, s.w, 1, C(COL_BORDER));
    Font *f = &A.font_small;
    f32 ty = text_y(f, s.y, s.h);
    char left[256], right[256], n1[32], n2[32];
    left[0] = right[0] = 0;
    if (L && p->order) {
        fmt_count(n1, p->count);
        int k;
        if (L->is_content) {
            fmt_count(n1, L->nfiles);
            fmt_count(n2, L->nmatches);
            k = snprintf(left, sizeof left, "%s matching line%s in %s file%s%s", n2, L->nmatches == 1 ? "" : "s", n1,
                         L->nfiles == 1 ? "" : "s", L->ctrunc ? " (stopped at 20,000 lines: narrow the search)" : "");
        } else {
            k = L->is_search ? snprintf(left, sizeof left, "%s result%s", n1, p->count == 1 ? "" : "s")
                             : snprintf(left, sizeof left, "%s item%s", n1, p->count == 1 ? "" : "s");
        }
        if (p->sel_count) {
            fmt_count(n2, p->sel_count);
            k += snprintf(left + k, sizeof left - k, "   \xC2\xB7   %s selected", n2);
            if (p->sel_count <= 100000) {
                u64 total = 0;
                bool all = true;
                for (u32 i = 0; i < p->count; i++) {
                    Entry *e = &L->entries[p->order[i]];
                    if (!p->sel[p->order[i]] || (e->flags & ENT_MATCH)) continue; // (a matching line's size is its number)
                    if (atomic_load_explicit(&e->meta, memory_order_acquire) != META_READY) { all = false; break; }
                    if (e->flags & ENT_DIR) {
                        bool known;
                        u64 ds = entry_dir_size(e, &known);
                        if (!known && A.folder_sizes && !L->is_search) { all = false; break; }
                        total += ds;
                    } else {
                        total += e->size;
                    }
                }
                if (all && total) {
                    char sz[32];
                    fmt_size(sz, sizeof sz, total);
                    snprintf(left + k, sizeof left - k, "   \xC2\xB7   %s", sz);
                }
            }
        }
        int st = atomic_load(&L->state);
        f64 tn = (L->t_names - L->t_start) / 1e6;
        if (st == LST_COMPLETE) {
            if (A.load_times) snprintf(right, sizeof right, "listed in %.1f ms  \xC2\xB7  details in %.1f ms", tn, (L->t_meta - L->t_start) / 1e6);
        } else if (!A.load_times) { // (progress still shows while details are read)
            u32 done = atomic_load(&L->meta_done);
            snprintf(right, sizeof right, "reading details %u%%", L->count ? (u32)((u64)done * 100 / L->count) : 100);
        } else {
            u32 done = atomic_load(&L->meta_done);
            snprintf(right, sizeof right, "listed in %.1f ms  \xC2\xB7  reading details %u%%", tn,
                     L->count ? (u32)((u64)done * 100 / L->count) : 100);
        }
        if (L->is_search) {
            char nd[32];
            fmt_count(nd, atomic_load(&L->search_dirs));
            if (st == LST_COMPLETE) snprintf(right, sizeof right, "searched %s folders in %.2f s", nd, (L->t_meta - L->t_start) / 1e9);
            else snprintf(right, sizeof right, "searching\xE2\x80\xA6 %s folders", nd);
        }
        if (p->sort_pending) snprintf(right, sizeof right, "sorting\xE2\x80\xA6");
    } else if (L && atomic_load(&L->state) == LST_SCANNING) {
        fmt_count(n1, atomic_load(&L->scanned));
        snprintf(left, sizeof left, "Scanning\xE2\x80\xA6 %s items", n1);
    }
    if (A.show_hidden) {
        size_t k = strlen(left);
        snprintf(left + k, sizeof left - k, "   \xC2\xB7   showing hidden");
    }
    f32 left_end = draw_text(f, s.x + S(14), ty, left, (int)strlen(left), C(COL_TEXT_DIM));
    char gbranch[160], grest[96];
    bool has_git = git_status_label(p, gbranch, sizeof gbranch, grest, sizeof grest);
    // Options (gear), far right
    f32 bw = roundf(S(26)), bh = s.h - roundf(S(6));
    {
        Rect r = {s.x + s.w - S(4) - bw + S(2), s.y + roundf(S(3)), bw - S(2), bh};
        A.r_gear = r;
        bool hv = A.mouse_in && in_rect(r, A.mx, A.my), on = options_is_open();
        if (hv || on) draw_rrect(r.x, r.y, r.w, r.h, S(5), on ? CA(COL_ACCENT, 0.25f) : C(COL_FIELD));
        draw_gear_icon(r.x + r.w * 0.5f, r.y + r.h * 0.5f, S(7), on ? C(COL_ACCENT) : hv ? C(COL_TEXT) : C(COL_TEXT_DIM),
                       C(hv || on ? (on ? rgb_mix(COL_TOOLBAR, COL_ACCENT, 0.25f) : COL_FIELD) : COL_TOOLBAR));
    }
    { // features (eye): view toggles that otherwise only have a key
        Rect r = {A.r_gear.x - bw, s.y + roundf(S(3)), bw - S(2), bh};
        A.r_feat = r;
        bool hv = A.mouse_in && in_rect(r, A.mx, A.my), on = features_menu_open();
        if (hv || on) draw_rrect(r.x, r.y, r.w, r.h, S(5), on ? CA(COL_ACCENT, 0.25f) : C(COL_FIELD));
        Color ic = on ? C(COL_ACCENT) : hv ? C(COL_TEXT) : C(COL_TEXT_DIM);
        draw_eye_icon(r.x + r.w * 0.5f, r.y + r.h * 0.5f, S(7.5f), ic);
        draw_rect(roundf(r.x - S(6)), r.y + S(4), 1, r.h - S(8), C(COL_BORDER));
    }
    // view switcher: details / list / grid
    f32 bx = s.x + s.w - S(4) - bw * 5 - S(10);
    int curv = p->view;
    for (int i = 0; i < 3; i++) {
        Rect r = {bx + i * bw, s.y + roundf(S(3)), bw - S(2), bh};
        A.r_viewbtn[i] = r;
        bool on = curv == i, hv = A.mouse_in && in_rect(r, A.mx, A.my);
        if (on || hv) draw_rrect(r.x, r.y, r.w, r.h, S(5), on ? CA(COL_ACCENT, 0.25f) : C(COL_FIELD));
        Color ic = on ? C(COL_ACCENT) : C(COL_TEXT_DIM);
        f32 cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f, t = MAX(1.f, roundf(S(1.4f)));
        if (i == 0) {
            for (int k = -1; k <= 1; k++) draw_rect(roundf(cx - S(6)), roundf(cy + k * S(4)), roundf(S(12)), t, ic);
        } else if (i == 1) {
            for (int k = -1; k <= 1; k++) {
                draw_rect(roundf(cx - S(7)), roundf(cy + k * S(4)), roundf(S(5)), t, ic);
                draw_rect(roundf(cx + S(1)), roundf(cy + k * S(4)), roundf(S(5)), t, ic);
            }
        } else {
            f32 q = roundf(S(5));
            draw_rrect(cx - q - S(1), cy - q - S(1), q, q, S(1.5f), ic);
            draw_rrect(cx + S(1), cy - q - S(1), q, q, S(1.5f), ic);
            draw_rrect(cx - q - S(1), cy + S(1), q, q, S(1.5f), ic);
            draw_rrect(cx + S(1), cy + S(1), q, q, S(1.5f), ic);
        }
    }
    // transfers (the queue, Ctrl+J): only while there are some, or the queue is open; the bar under the arrows is
    // how far the running ones are
    if (A.nops || A.q_open) {
        Rect r = {bx - bw - S(8), s.y + roundf(S(3)), bw - S(2), bh};
        A.r_xfer = r;
        bool hv = A.mouse_in && in_rect(r, A.mx, A.my), on = A.q_open, busy = false;
        u64 wd = 0, wt = 0;
        for (int i = 0; i < A.nops; i++) {
            if (atomic_load(&A.ops[i]->phase) == OPS_DONE) continue;
            u64 d, t;
            op_work(A.ops[i], &d, &t);
            wd += d;
            wt += t;
            busy = true;
        }
        if (hv || on) draw_rrect(r.x, r.y, r.w, r.h, S(5), on ? CA(COL_ACCENT, 0.25f) : C(COL_FIELD));
        draw_xfer_icon(r.x + r.w * 0.5f, r.y + r.h * 0.5f - S(1), S(15), C(on || busy ? COL_ACCENT : hv ? COL_TEXT : COL_TEXT_DIM));
        if (busy && wt) {
            f32 lw = r.w - S(10), lx = r.x + S(5), ly = r.y + r.h - S(3);
            draw_rect(lx, ly, lw, MAX(1.f, S(1.5f)), C(COL_CONTROL));
            draw_rect(lx, ly, roundf(lw * (f32)CLAMP((f64)wd / (f64)wt, 0.0, 1.0)), MAX(1.f, S(1.5f)), C(COL_ACCENT));
        }
        bx = r.x - S(4);
    }
    int rl = (int)strlen(right);
    f32 right_x = bx - S(12) - text_width(f, right, rl);
    draw_text(f, right_x, ty, right, rl, C(COL_TEXT_FAINT));
    if (has_git) { // branch icon + name, ahead/behind, changes - between the counts and the timings
        f32 x = left_end + S(22), lim = right_x - S(16);
        f32 cy = roundf(s.y + s.h * 0.5f), t = MAX(1.f, roundf(S(1.2f))), r = roundf(S(2.2f));
        (void)t; (void)r;
        if (x + S(60) < lim) {
            draw_branch_icon(x, cy, C(COL_TEXT_DIM));
            x += S(17);
            int bl = (int)strlen(gbranch);
            f32 ex = x + MIN(text_width(f, gbranch, bl), lim - x);
            draw_text_fit(f, x, ty, lim - x, gbranch, bl, C(COL_TEXT2), 0);
            if (ex < lim - S(20)) draw_text_fit(f, ex, ty, lim - ex, grest, (int)strlen(grest), C(COL_TEXT_DIM), 0);
        }
    }
}

// "Copying 3 items to "Docs"", "Moved 1 item", "Finished with 2 errors"...
static void op_title(const Op *op, char *title, size_t cap) {
    static const char *verbs[] = {"Copying", "Moving", "Moving to Trash", "Deleting", "Restoring", "Extracting", "Compressing", "Syncing",
                                  "Renaming", "Creating", "Creating"};
    static const char *dverbs[] = {"Copied", "Moved", "Moved to Trash", "Deleted", "Restored", "Extracted", "Compressed", "Synced",
                                   "Renamed", "Created", "Created"};
    bool done = atomic_load(&op->phase) == OPS_DONE;
    int n = op->nsrc;
    const char *as = op->admin ? "As administrator: " : !done && atomic_load(&op->paused) ? "Paused: " : "";
    if (done && op->nerr) snprintf(title, cap, "%sFinished with %d error%s", as, op->nerr, op->nerr == 1 ? "" : "s");
    else if (done) snprintf(title, cap, "%s%s %d item%s", as, dverbs[op->kind], n, n == 1 ? "" : "s");
    else if (op->dst_dir) snprintf(title, cap, "%s%s %d item%s to \xE2\x80\x9C%s\xE2\x80\x9D", as, verbs[op->kind], n,
                                   n == 1 ? "" : "s", op->dst_dir[1] ? path_basename(op->dst_dir) : "/");
    else snprintf(title, cap, "%s%s %d item%s", as, verbs[op->kind], n, n == 1 ? "" : "s");
}

// The progress bar of a card or queue row: eases toward the real fraction between progress updates; a sliding
// block while counting; grey while paused or waiting.
static void op_bar_draw(Op *op, u64 now, f32 bx, f32 by, f32 bw) {
    int ph = atomic_load(&op->phase);
    bool done = ph == OPS_DONE;
    f64 frac = op_frac(op);
    f64 dt = op->bar_t ? (now - op->bar_t) / 1e9 : 0;
    op->bar_t = now;
    if (done || frac < op->bar) op->bar = frac; // totals can grow (a move that had to copy): don't ease backwards
    else op->bar += (frac - op->bar) * (1.0 - exp(-dt * 12.0));
    f32 bh = roundf(S(5));
    draw_rrect(bx, by, bw, bh, bh * 0.5f, C(COL_CONTROL));
    bool idle = atomic_load(&op->paused) || (ph == OPS_QUEUED && !op->go);
    u32 bar = done ? (op->nerr ? COL_DANGER : COL_OK) : idle ? COL_TEXT_FAINT : COL_ACCENT;
    if (ph == OPS_COUNTING) {
        f32 t = (f32)fmod((f64)(now - op->t_start) / 1e9, 1.2) / 1.2f;
        draw_rrect(bx + (bw * 0.75f) * t, by, bw * 0.25f, bh, bh * 0.5f, C(bar));
    } else if (op->bar > 0.001) {
        draw_rrect(bx, by, MAX(bh, (f32)(bw * op->bar)), bh, bh * 0.5f, C(bar));
    }
}

// A small square icon button (hover chip); returns its rect. kind: 0 close, 1 pause/resume, 2 up, 3 down.
static Rect op_icon_button(f32 x, f32 y, f32 cs, int kind, bool paused, bool enabled) {
    Rect r = {x, y, cs, cs};
    bool hv = enabled && A.mouse_in && in_rect(r, A.mx, A.my);
    if (hv) draw_rrect(r.x, r.y, r.w, r.h, S(5), C(COL_CHIP));
    Color c = C(!enabled ? COL_TEXT_FAINT : hv ? COL_TEXT : COL_TEXT_DIM);
    f32 cx = r.x + cs * 0.5f, cy = r.y + cs * 0.5f, t = MAX(1.f, S(1.3f));
    if (kind == 0) draw_cross(cx, cy, S(7), t, c);
    else if (kind == 1) draw_pause_icon(r, paused, c);
    else draw_chevron(cx, cy, S(10), kind == 2 ? 2 : 3, t, c);
    return enabled ? r : (Rect){0, 0, 0, 0};
}

static void draw_ops(void) {
    u64 now = now_ns();
    Font *f = &A.font, *fs = &A.font_small;
    f32 w = roundf(S(380)), x = A.W - w - roundf(S(16)), y = A.r_status.y - roundf(S(10));
    for (int i = 0; i < A.nops; i++) A.op_close[i] = A.op_retry[i] = A.op_pause[i] = A.op_card[i] = (Rect){0, 0, 0, 0};
    if (A.q_open) return; // the transfer queue lists them instead
    for (int i = A.nops - 1; i >= 0; i--) {
        Op *op = A.ops[i];
        if (!op_card_visible(op, now) || op->dismissed) continue;
        bool done = atomic_load(&op->phase) == OPS_DONE;
        bool retry = done && op->nerr && op->perm_errs && !op->admin && op_admin_ok(op->kind); // permission denied
        f32 h = roundf(S(retry ? 110 : 74));
        y -= h;
        A.op_card[i] = (Rect){x, y, w, h};
        draw_rrect(x + S(2), y + S(4), w, h, S(10), CA(0x000000, 0.35f));
        draw_rrect(x, y, w, h, S(10), C(COL_POPUP));
        draw_rrect_outline(x, y, w, h, S(10), 1, C(COL_CONTROL));
        char title[400];
        op_title(op, title, sizeof title);
        f32 pad = roundf(S(14)), cs = roundf(S(20));
        bool can_pause = op_pausable(op);
        draw_text_fit(f, x + pad, y + S(10), w - pad * 2 - S(24) - (can_pause ? cs + S(4) : 0), title, (int)strlen(title), C(COL_TEXT), 0);
        // close / cancel, and pause / resume beside it
        A.op_close[i] = op_icon_button(x + w - cs - S(8), y + S(8), cs, 0, false, true);
        if (can_pause) A.op_pause[i] = op_icon_button(x + w - cs * 2 - S(12), y + S(8), cs, 1, atomic_load(&op->paused), true);
        op_bar_draw(op, now, x + pad, y + S(36), w - pad * 2);
        char det[600];
        op_detail(op, now, det, sizeof det);
        draw_text_fit(fs, x + pad, y + S(48), w - pad * 2, det, (int)strlen(det), C(done && op->nerr ? COL_DANGER_TEXT : COL_TEXT_DIM), 0);
        if (retry) { // the same again with the administrator's rights (pkexec asks for the password)
            const char *lb = op->kind == OP_TRASH ? "Delete as administrator\xE2\x80\xA6" : "Retry as administrator";
            f32 rbw = roundf(text_width(f, lb, (int)strlen(lb)) + S(28)), rbh = roundf(S(28));
            A.op_retry[i] = (Rect){x + w - pad - rbw, y + h - pad - rbh + S(4), rbw, rbh};
            sheet_button(A.op_retry[i], lb, true, true);
            f32 ls = roundf(S(13)), lx = x + pad, ly2 = roundf(A.op_retry[i].y + (rbh - ls) * 0.5f); // a padlock
            Color lc = C(COL_TEXT_DIM);
            draw_rrect_outline(lx + ls * 0.2f, ly2, ls * 0.6f, ls * 0.7f, ls * 0.3f, MAX(1.f, S(1.4f)), lc);
            draw_rrect(lx, ly2 + ls * 0.42f, ls, ls * 0.58f, S(2), lc);
            draw_text(fs, lx + ls + S(8), text_y(fs, A.op_retry[i].y, rbh), "Not allowed", 11, lc);
        }
        y -= roundf(S(8));
    }
}

// The transfer queue (Ctrl+J): every operation in queue order, above the status bar. Per row: what it is, the bar,
// progress / speed / time left (or its place in line), and buttons to move it up / down, pause / resume, cancel.
static int g_q_top; // first row shown when they don't all fit
static void draw_queue(void) {
    for (int i = 0; i < MAX_OPS; i++) A.q_row[i] = A.q_pause[i] = A.q_up[i] = A.q_down[i] = A.q_close[i] = (Rect){0, 0, 0, 0};
    A.q_r = A.q_verify = (Rect){0, 0, 0, 0};
    if (!A.q_open) return;
    u64 now = now_ns();
    Font *f = &A.font, *fs = &A.font_small, *tf = &A.font_brand;
    f32 w = roundf(MIN(S(520), A.W - S(32))), pad = roundf(S(16)), headh = roundf(S(52)), rowh = roundf(S(66)), cs = roundf(S(22));
    f32 avail = A.r_status.y - S(10) - S(60) - headh - pad;
    int fit = MAX(1, (int)(avail / rowh)), nshow = MIN(MAX(A.nops, 1), fit);
    A.q_sel = A.nops ? CLAMP(A.q_sel, 0, A.nops - 1) : 0;
    if (A.q_sel < g_q_top) g_q_top = A.q_sel;
    if (A.q_sel >= g_q_top + fit) g_q_top = A.q_sel - fit + 1;
    g_q_top = CLAMP(g_q_top, 0, MAX(0, A.nops - fit));
    f32 h = headh + nshow * rowh + pad * 0.5f;
    f32 x = A.W - w - roundf(S(16)), y = roundf(A.r_status.y - S(10) - h);
    A.q_r = (Rect){x, y, w, h};
    draw_rrect(x + S(2), y + S(4), w, h, S(12), CA(0x000000, 0.35f));
    draw_rrect(x, y, w, h, S(12), C(COL_POPUP));
    draw_rrect_outline(x, y, w, h, S(12), 1, C(COL_POPUP_BORDER));
    // header: title, how many run / wait, the verify switch, how many at once
    int nrun = 0, nwait = 0;
    for (int i = 0; i < A.nops; i++) {
        int ph = atomic_load(&A.ops[i]->phase);
        if (ph == OPS_QUEUED && !A.ops[i]->go) nwait++;
        else if (ph != OPS_DONE) nrun++;
    }
    draw_text(tf, x + pad, y + S(10), "Transfers", 9, C(COL_TEXT_STRONG));
    char sub[96];
    if (!A.nops) snprintf(sub, sizeof sub, "Nothing running");
    else snprintf(sub, sizeof sub, "%d running  \xC2\xB7  %d waiting  \xC2\xB7  %s", nrun, nwait,
                  A.transfers_at_once == 1 ? "one at a time" : A.transfers_at_once ? "a few at a time" : "all at once");
    draw_text(fs, x + pad, y + S(31), sub, (int)strlen(sub), C(COL_TEXT_DIM));
    const char *vl = "Verify after copy";
    f32 vw = roundf(S(16) + S(8) + text_width(fs, vl, (int)strlen(vl)));
    A.q_verify = (Rect){x + w - pad - vw, y + S(15), vw, roundf(S(22))};
    sheet_check(A.q_verify, vl, A.verify_copies);
    draw_rect(x + S(1), y + headh - S(1), w - S(2), 1, C(COL_BORDER));
    if (!A.nops) {
        const char *e = "Copies, moves and deletes show up here while they run.";
        draw_text_fit(fs, x + pad, text_y(fs, y + headh, rowh), w - pad * 2, e, (int)strlen(e), C(COL_TEXT_FAINT), 0);
        return;
    }
    for (int k = 0; k < nshow; k++) {
        int i = g_q_top + k;
        if (i >= A.nops) break;
        Op *op = A.ops[i];
        f32 ry = y + headh + k * rowh;
        Rect rr = {x + S(6), ry + S(3), w - S(12), rowh - S(6)};
        A.q_row[i] = rr;
        if (i == A.q_sel) draw_rrect(rr.x, rr.y, rr.w, rr.h, S(8), C(COL_SEL_FOCUS));
        bool done = atomic_load(&op->phase) == OPS_DONE, paused = atomic_load(&op->paused);
        // buttons, right to left: cancel / dismiss, pause / resume, down, up
        f32 bxr = rr.x + rr.w - S(6) - cs, byr = rr.y + S(6);
        A.q_close[i] = op_icon_button(bxr, byr, cs, 0, false, true);
        bxr -= cs + S(2);
        if (op_pausable(op)) A.q_pause[i] = op_icon_button(bxr, byr, cs, 1, paused, true);
        bxr -= cs + S(8);
        A.q_down[i] = op_icon_button(bxr, byr, cs, 3, false, i < A.nops - 1);
        bxr -= cs + S(2);
        A.q_up[i] = op_icon_button(bxr, byr, cs, 2, false, i > 0);
        char title[400];
        op_title(op, title, sizeof title);
        f32 tx = rr.x + S(10);
        draw_text_fit(f, tx, rr.y + S(7), bxr - tx - S(8), title, (int)strlen(title), C(COL_TEXT), 0);
        op_bar_draw(op, now, tx, rr.y + S(33), rr.x + rr.w - S(10) - tx);
        char det[600];
        op_detail(op, now, det, sizeof det);
        draw_text_fit(fs, tx, rr.y + S(43), rr.x + rr.w - S(10) - tx, det, (int)strlen(det), C(done && op->nerr ? COL_DANGER_TEXT : COL_TEXT_DIM), 0);
    }
    if (A.nops > nshow) { // more than fit: say where we are
        char m[48];
        snprintf(m, sizeof m, "%d\xE2\x80\x93%d of %d", g_q_top + 1, g_q_top + nshow, A.nops);
        f32 mw = text_width(fs, m, (int)strlen(m));
        draw_text(fs, A.q_verify.x - mw - S(16), y + S(31), m, (int)strlen(m), C(COL_TEXT_FAINT));
    }
}

// Greedy word wrap; returns number of lines.
static int wrap_text(Font *f, const char *s, f32 maxw, int *starts, int *lens, int max) {
    int n = 0, len = (int)strlen(s), i = 0;
    while (i < len && n < max) {
        int best = -1, j = i;
        while (j <= len) {
            if (j == len || s[j] == ' ') {
                if (text_width(f, s + i, j - i) <= maxw || best < 0) best = j;
                else break;
                if (j == len) break;
            }
            j++;
        }
        if (best < 0) best = len;
        starts[n] = i;
        lens[n] = best - i;
        n++;
        i = best + 1;
    }
    return n;
}

static u32 props_well(void);
static void props_card(f32 x, f32 y, f32 w, int n, f32 rh, f32 extra);

// Confirmation dialogs: a tinted badge, the title in semibold, the message, optionally the items it's about in a
// card, and the buttons (the default one red when it destroys something).
// "folder/sub/name": the folders dimmed, shortened from the left so the name stays readable.
static void draw_path_fit(Font *f, f32 x, f32 y, f32 maxw, const char *path, bool plain) {
    if (maxw <= 0) return;
    const char *slash = plain ? NULL : strrchr(path, '/');
    if (!slash) { draw_text_fit(f, x, y, maxw, path, (int)strlen(path), C(COL_TEXT), 0); return; }
    int fl = (int)(slash - path + 1);
    const char *name = slash + 1;
    f32 nw = text_width(f, name, (int)strlen(name)), fw = text_width(f, path, fl);
    if (nw >= maxw) { draw_text_fit(f, x, y, maxw, name, (int)strlen(name), C(COL_TEXT), 0); return; }
    const char *fs = path;
    f32 ell = 0;
    if (fw + nw > maxw) { // drop leading characters of the folders
        ell = text_width(f, ELLIPSIS, 3);
        while (*fs && fs < slash && ell + text_width(f, fs, (int)(slash + 1 - fs)) + nw > maxw) fs++;
    }
    f32 cx = x;
    if (ell > 0) cx = draw_text(f, cx, y, ELLIPSIS, 3, C(COL_TEXT_FAINT));
    cx = draw_text(f, cx, y, fs, (int)(slash + 1 - fs), C(COL_TEXT_DIM));
    draw_text(f, cx, y, name, (int)strlen(name), C(COL_TEXT));
}

// Copy: two overlapping pages; replace: the front one filled (what's there gets overwritten).
static void draw_copy_icon(f32 x, f32 y, f32 s, Color c, bool filled) {
    f32 t = MAX(1.f, roundf(S(1.4f))), w = roundf(s * 0.62f), h = roundf(s * 0.72f), r = S(2);
    draw_rrect_outline(x, y, w, h, r, t, c);
    f32 fx = x + s - w, fy = y + s - h;
    draw_rrect(fx - t, fy - t, w + 2 * t, h + 2 * t, r + t, C(COL_POPUP)); // (gap around the front page)
    if (filled) draw_rrect(fx, fy, w, h, r, c);
    else draw_rrect_outline(fx, fy, w, h, r, t, c);
}

static void draw_dialog(void) {
    Dialog *d = &A.dlg;
    if (!d->open) return;
    draw_rect(0, 0, (f32)A.W, (f32)A.H, CA(0x000000, 0.45f));
    Font *f = &A.font, *fs = &A.font_small, *tf = &A.font_brand;
    f32 w = roundf(MIN(S(d->rich ? 640 : d->nopts ? 580 : 480), A.W - S(40))), pad = roundf(S(22)), bs = d->icon ? roundf(S(40)) : 0, tx0 = pad + (bs ? bs + roundf(S(14)) : 0);
    int st[10], ln[10];
    int nl = wrap_text(f, d->msg, w - tx0 - pad, st, ln, 10);
    f32 lh = roundf(f->line_h * 1.3f), irh = roundf(S(32)), bh = roundf(S(32)), orh = roundf(S(30));
    int nrows = d->nitems + (d->more > 0);
    f32 head = MAX(bs, roundf(tf->line_h + S(6)) + nl * lh);
    f32 h = pad + head + (nrows ? roundf(S(14)) + nrows * irh : 0) + (d->nopts ? roundf(S(14)) + d->nopts * orh + S(8) : 0) +
            (d->has_check ? roundf(S(36)) : 0) + roundf(S(22)) + bh + pad;
    f32 x = roundf((A.W - w) * 0.5f), y = roundf((A.H - h) * 0.42f);
    d->r = (Rect){x, y, w, h};
    draw_rrect(x, y, w, h, S(12), C(COL_POPUP));
    draw_rrect_outline(x, y, w, h, S(12), 1, C(COL_POPUP_BORDER));
    if (d->icon) { // badge: tinted circle with a symbol; deletions get the bin alone, no circle
        u32 col = d->icon == DI_DANGER ? COL_DANGER : d->icon == DI_WARN ? COL_WARN : COL_ACCENT;
        f32 bx = x + pad, by = y + pad;
        if (d->icon == DI_DANGER && draw_icon_id(roundf(bx + bs * 0.1f), roundf(by + bs * 0.1f), roundf(bs * 0.8f), icon_find("trash"), false)) {
        } else {
            draw_rrect(bx, by, bs, bs, bs * 0.5f, C(rgb_mix(COL_POPUP, col, 0.18f)));
            const char *sym = d->icon == DI_QUESTION ? "?" : "!";
            f32 sw = text_width(tf, sym, 1);
            draw_text(tf, roundf(bx + (bs - sw) * 0.5f), text_y(tf, by, bs), sym, 1, C(col));
        }
    }
    f32 tx = x + tx0, cy = y + pad;
    draw_text_fit(tf, tx, cy - S(1), x + w - pad - tx, d->title, (int)strlen(d->title), C(COL_TEXT_STRONG), 0);
    cy += roundf(tf->line_h + S(6));
    for (int i = 0; i < nl; i++) {
        draw_text(f, tx, cy, d->msg + st[i], ln[i], C(COL_TEXT2));
        cy += lh;
    }
    cy = y + pad + head;
    if (nrows && d->rich) { // what happens: icon folder/name -> icon folder/name, the operation at the end
        cy += roundf(S(14));
        f32 cx = x + pad, cw = w - pad * 2;
        props_card(cx, cy, cw, nrows, irh, 0);
        for (int i = 0; i < d->nitems; i++) {
            f32 ry = cy + i * irh, mid = roundf(ry + irh * 0.5f), is = roundf(S(16)), iy = roundf(mid - is * 0.5f);
            f32 opw = roundf(S(30)), aw = roundf(S(26)), x0 = cx + S(12), x1 = cx + cw - opw - S(6);
            f32 half = floorf((x1 - x0 - aw) * 0.5f);
            int k = d->item_kind[i];
            for (int side = 0; side < 2; side++) { // source, then destination
                const char *pth = side ? d->item_sub[i] : d->items[i];
                if (!pth) continue;
                f32 sx = side ? x0 + half + aw : x0;
                bool trash = side && k == DK_TRASH;
                if (trash) { if (!draw_icon_id(sx, iy, is, icon_find("trash"), false)) draw_file_icon(sx, iy, is, 0x8C8F99); }
                else if (d->item_dir[i]) draw_dir_icon(sx, iy, is, path_basename(pth), false);
                else if (!draw_icon_for_name(sx, iy, is, path_basename(pth), false)) draw_file_icon(sx, iy, is, 0x8C8F99);
                draw_path_fit(fs, sx + is + S(7), text_y(fs, ry, irh), half - is - S(10), pth, trash);
            }
            // the arrow, on the row's centre line: a hairline shaft and a small open head (the shaft stops short of the
            // tip so the strokes don't pile up there)
            f32 lt = MAX(1.f, S(1.f)), ax = x0 + half + roundf(S(5)), tip = x0 + half + aw - roundf(S(6)), hd = S(3.5f);
            f32 my = mid + 0.5f; // pixel centre: a 1 px line stays crisp
            Color ac = C(COL_TEXT_FAINT);
            draw_line(ax, my, tip - lt, my, lt, ac);
            draw_line(tip, my, tip - hd, my - hd, lt, ac);
            draw_line(tip, my, tip - hd, my + hd, lt, ac);
            f32 ox = x1 + (opw - is) * 0.5f + S(3); // the operation
            if (k == DK_TRASH) {
                if (!draw_icon_id(ox, iy, is, icon_find("trash"), false)) draw_cross(ox + is * 0.5f, mid, S(8), lt, C(COL_DANGER));
            } else draw_copy_icon(ox, iy, is, C(k == DK_REPLACE ? COL_WARN : COL_OK), k == DK_REPLACE);
        }
        if (d->more > 0) {
            char m[48];
            snprintf(m, sizeof m, "and %d more", d->more);
            draw_text(fs, cx + S(12), text_y(fs, cy + d->nitems * irh, irh), m, (int)strlen(m), C(COL_TEXT_DIM));
        }
        cy += nrows * irh;
    } else if (nrows) { // the items it's about
        cy += roundf(S(14));
        f32 cx = x + pad, cw = w - pad * 2;
        props_card(cx, cy, cw, nrows, irh, 0);
        for (int i = 0; i < d->nitems; i++) {
            f32 ry = cy + i * irh, is = roundf(S(16)), ix = cx + S(12), iy = roundf(ry + (irh - is) * 0.5f);
            if (d->item_dir[i]) draw_dir_icon(ix, iy, is, d->items[i], false);
            else if (!draw_icon_for_name(ix, iy, is, d->items[i], false)) draw_file_icon(ix, iy, is, 0x8C8F99);
            f32 tx1 = ix + is + S(10), tw = cw - is - S(34), ty = text_y(fs, ry, irh);
            const char *nm = d->items[i], *fp = d->item_sub[i];
            f32 nw = text_width(fs, nm, (int)strlen(nm));
            if (fp) nw = MIN(nw, roundf(tw * 0.5f)); // the name keeps up to half; the full path, dimmed, gets the rest
            draw_text_fit(fs, tx1, ty, nw, nm, (int)strlen(nm), C(COL_TEXT), 0);
            f32 px = tx1 + nw + roundf(S(12)), pw = tx1 + tw - px;
            if (fp && pw > S(20)) { // shortened from the left so the end of the path stays readable
                const char *ps = fp;
                f32 ell = 0;
                if (text_width(fs, ps, (int)strlen(ps)) > pw) {
                    ell = text_width(fs, ELLIPSIS, 3);
                    while (*ps && ell + text_width(fs, ps, (int)strlen(ps)) > pw) ps++;
                    while ((*ps & 0xC0) == 0x80) ps++; // not mid-character
                }
                f32 cx2 = px;
                if (ell > 0) cx2 = draw_text(fs, cx2, ty, ELLIPSIS, 3, C(COL_TEXT_FAINT));
                draw_text(fs, cx2, ty, ps, (int)strlen(ps), C(COL_TEXT_FAINT));
            }
        }
        if (d->more > 0) {
            char m[48];
            snprintf(m, sizeof m, "and %d more", d->more);
            draw_text(fs, cx + S(12), text_y(fs, cy + d->nitems * irh, irh), m, (int)strlen(m), C(COL_TEXT_DIM));
        }
        cy += nrows * irh;
    }
    if (d->nopts) { // the choices: a radio dot, the name, what it does (dim)
        cy += roundf(S(14));
        f32 cx = x + pad, cw = w - pad * 2, lw = 0;
        for (int i = 0; i < d->nopts; i++) lw = MAX(lw, text_width(f, d->opts[i], (int)strlen(d->opts[i])));
        draw_rrect(cx, cy, cw, d->nopts * orh + S(8), S(8), C(rgb_mix(COL_POPUP, COL_CONTROL, 0.45f)));
        cy += roundf(S(4));
        for (int i = 0; i < d->nopts; i++) {
            Rect r = {cx + S(4), cy + i * orh, cw - S(8), orh};
            d->ropt[i] = r;
            bool on = i == d->opt, hv = A.mouse_in && in_rect(r, A.mx, A.my);
            if (on || hv) draw_rrect(r.x, r.y, r.w, r.h, S(6), C(on ? COL_SEL_FOCUS : COL_CONTROL_HOVER));
            f32 rs = roundf(S(14)), rx = r.x + S(10), ry = roundf(r.y + (r.h - rs) * 0.5f);
            draw_rrect(rx, ry, rs, rs, rs * 0.5f, C(on ? COL_ACCENT : COL_CHIP));
            if (on) { f32 q = roundf(rs * 0.36f); draw_rrect(rx + (rs - q) * 0.5f, ry + (rs - q) * 0.5f, q, q, q * 0.5f, C(COL_ON_ACCENT)); }
            f32 lx = rx + rs + S(10);
            draw_text(f, lx, text_y(f, r.y, r.h), d->opts[i], (int)strlen(d->opts[i]), C(COL_TEXT));
            f32 dx = lx + lw + S(16);
            if (d->opt_desc[i][0] && dx < r.x + r.w - S(30))
                draw_text_fit(fs, dx, text_y(fs, r.y, r.h), r.x + r.w - S(10) - dx, d->opt_desc[i], (int)strlen(d->opt_desc[i]), C(COL_TEXT_DIM), 0);
        }
        cy += d->nopts * orh + roundf(S(4));
    }
    if (d->has_check) {
        cy += roundf(S(14));
        d->rcheck = (Rect){x + pad, cy, w - pad * 2, roundf(S(22))};
        sheet_check(d->rcheck, d->check_label, d->check);
        cy += roundf(S(22));
    }
    // buttons, right-aligned; the default one is the accent (red when destructive)
    f32 bx = x + w - pad;
    for (int i = d->nbuttons - 1; i >= 0; i--) {
        f32 bw = MAX(roundf(S(88)), roundf(text_width(f, d->buttons[i], (int)strlen(d->buttons[i])) + S(28)));
        bx -= bw;
        Rect r = {bx, y + h - pad - bh, bw, bh};
        d->rb[i] = r;
        bool hover = A.mouse_in && in_rect(r, A.mx, A.my);
        bool primary = i == d->focus;
        u32 pc = d->danger && i == d->def ? (hover ? rgb_mix(COL_DANGER_FILL, 0xFFFFFF, 0.12f) : COL_DANGER_FILL) : (hover ? COL_ACCENT_HOVER : COL_ACCENT);
        draw_rrect(r.x, r.y, r.w, r.h, S(7), C(primary ? pc : (hover ? COL_CONTROL_HOVER : COL_CONTROL)));
        f32 tw = text_width(f, d->buttons[i], (int)strlen(d->buttons[i]));
        draw_text(f, r.x + (r.w - tw) * 0.5f, text_y(f, r.y, r.h), d->buttons[i], (int)strlen(d->buttons[i]),
                  C(primary ? COL_ON_ACCENT : COL_TEXT));
        bx -= roundf(S(8));
    }
}

static void draw_menu(void) {
    Menu *m = &A.menu;
    if (!m->open) return;
    Font *f = &A.font, *fs = &A.font_small;
    Rect r = m->r;
    draw_rrect(r.x, r.y, r.w, r.h, S(9), C(COL_POPUP));
    draw_rrect_outline(r.x, r.y, r.w, r.h, S(9), 1, C(COL_POPUP_BORDER));
    ClipSave cs = draw_clip_push((SDL_Rect){(int)r.x, (int)r.y + 1, (int)r.w, (int)r.h - 2});
    bool anycheck = false; // one check mark column for the whole menu keeps labels aligned
    for (int i = 0; i < m->n; i++) anycheck |= m->items[i].checkable;
    f32 yy = r.y + roundf(S(5)) - m->scroll;
    for (int i = 0; i < m->n; i++) {
        MenuItem *it = &m->items[i];
        f32 h = menu_item_h(it);
        if (yy + h < r.y || yy > r.y + r.h) { yy += h; continue; }
        if (it->sep) {
            draw_rect(r.x + S(10), roundf(yy + h * 0.5f), r.w - S(20), 1, C(COL_POPUP_BORDER));
        } else {
            if (i == m->hover && it->enabled) draw_rrect(r.x + S(5), yy, r.w - S(10), h, S(6), C(COL_SEL_FOCUS));
            Color tc = it->enabled ? C(COL_TEXT) : C(COL_TEXT_FAINT);
            f32 lx = anycheck ? r.x + S(30) : r.x + S(16);
            if (it->checkable) {
                if (it->checked) {
                    f32 cx = r.x + S(14), cy = yy + h * 0.5f, t = MAX(1.4f, S(1.6f));
                    draw_line(cx - S(4), cy, cx - S(1), cy + S(3), t, it->enabled ? C(COL_ACCENT) : C(COL_TEXT_FAINT));
                    draw_line(cx - S(1), cy + S(3), cx + S(5), cy - S(4), t, it->enabled ? C(COL_ACCENT) : C(COL_TEXT_FAINT));
                }
            }
            if (it->info) { // key (dim) + value
                draw_text(fs, lx, text_y(fs, yy, h), it->label, (int)strlen(it->label), C(COL_TEXT_FAINT));
                int vl = (int)strlen(it->shortcut);
                draw_text(fs, r.x + r.w - S(16) - text_width(fs, it->shortcut, vl), text_y(fs, yy, h), it->shortcut, vl, C(COL_TEXT2));
                yy += h;
                continue;
            }
            draw_text(f, lx, text_y(f, yy, h), it->label, (int)strlen(it->label), tc);
            int sl = (int)strlen(it->shortcut);
            f32 sw = text_width(fs, it->shortcut, sl);
            draw_text(fs, r.x + r.w - S(16) - sw, text_y(fs, yy, h), it->shortcut, sl,
                      it->enabled ? (i == m->hover ? C(COL_SEL_DIM) : C(COL_TEXT_FAINT)) : C(COL_TEXT_FAINT));
        }
        yy += h;
    }
    if (menu_scrollable()) { // scroll arrows (click, hover or wheel)
        f32 ah = roundf(S(18));
        for (int d = -1; d <= 1; d += 2) {
            bool show = d < 0 ? m->scroll > 0 : m->scroll < m->content_h - r.h;
            if (!show) continue;
            f32 ay = d < 0 ? r.y : r.y + r.h - ah;
            bool hv = A.mouse_in && in_rect(r, A.mx, A.my) && menu_arrow_at(A.my) == d;
            draw_rrect(r.x + 1, ay, r.w - 2, ah, S(8), C(hv ? COL_CONTROL_HOVER : COL_POPUP));
            draw_chevron(r.x + r.w * 0.5f, ay + ah * 0.5f, S(5), d < 0 ? 2 : 3, MAX(1.f, S(1.5f)), C(COL_TEXT_DIM));
        }
    }
    draw_clip_pop(cs);
}

static void draw_drag_badge(void) {
    if (A.drag != DRAG_ITEMS || !A.drag_pane) return;
    Pane *p = A.drag_pane;
    Font *f = &A.font_small;
    char buf[NAME_MAX + 96];
    u32 n = p->sel_count;
    if (A.drop_dir[0]) {
        const char *to = A.drop_dir[1] ? path_basename(A.drop_dir) : "/";
        snprintf(buf, sizeof buf, "%s %u item%s to %s", A.drop_copy ? "Copy" : "Move", n, n == 1 ? "" : "s", to);
    } else {
        snprintf(buf, sizeof buf, "%u item%s", n, n == 1 ? "" : "s");
    }
    int len = (int)strlen(buf);
    f32 w = text_width(f, buf, len) + S(42), h = roundf(S(28));
    f32 x = roundf(A.mx + S(18)), y = roundf(A.my + S(20)); // clear of the row under the cursor
    draw_rrect(x + S(1), y + S(3), w, h, S(7), CA(0x000000, 0.4f));
    draw_rrect(x, y, w, h, S(7), C(A.drop_dir[0] ? COL_SEL_FOCUS : COL_CONTROL));
    f32 is = roundf(S(14));
    draw_file_icon(x + S(8), y + (h - is) * 0.5f, is, COL_ACCENT);
    draw_text(f, x + S(28), text_y(f, y, h), buf, len, C(COL_SEL_TEXT));
}

static void draw_toast(void) {
    if (!A.toast[0]) return;
    int len = (int)strlen(A.toast);
    u64 life = 3000000000ull + (u64)MIN(len, 200) * 30000000ull; // long messages stay longer (up to ~9 s)
    u64 age = now_ns() - A.toast_t;
    if (age > life) { A.toast[0] = 0; return; }
    f32 a = age > life - 400000000ull ? (f32)(life - age) / 400000000.f : 1.f;
    Font *f = &A.font;
    f32 maxw = MIN(S(560), A.r_content.w - S(40)), padx = roundf(S(16)), lh = roundf(f->line_h * 1.25f);
    int st[4], ln[4];
    int nl = wrap_text(f, A.toast, maxw - padx * 2, st, ln, 4); // long errors wrap instead of running off
    f32 tw = 0;
    for (int i = 0; i < nl; i++) tw = MAX(tw, text_width(f, A.toast + st[i], ln[i]));
    f32 w = roundf(MIN(maxw, tw + padx * 2)), h = roundf(MAX(S(34), nl * lh + S(16)));
    f32 x = roundf(A.r_content.x + (A.r_content.w - w) * 0.5f), y = roundf(A.r_status.y - h - S(16));
    g_alpha = a;
    draw_rrect(x, y, w, h, S(9), C(COL_POPUP));
    draw_rrect_outline(x, y, w, h, S(9), 1, C(COL_POPUP_BORDER));
    f32 ty = roundf(y + (h - nl * lh) * 0.5f + (lh - f->line_h) * 0.5f);
    for (int i = 0; i < nl; i++)
        draw_text_fit(f, x + padx, ty + i * lh, w - padx * 2, A.toast + st[i], ln[i], C(COL_TEXT), 0);
    g_alpha = 1.f;
}

// Text with matched characters highlighted; pos are byte offsets into the full string,
// s starts at byte `base` of it. Elides with "..." past maxw. Returns the end x.
static f32 draw_text_hl(Font *f, f32 x, f32 y, f32 maxw, const char *s, int len, int base, const u16 *pos, int npos,
                        Color c, Color hc) {
    f32 bl = roundf(y + f->ascent), pen = roundf(x), lim = x + maxw - text_width(f, ELLIPSIS, 3);
    bool over = text_width(f, s, len) > maxw;
    const char *p = s, *end = s + len;
    while (p < end) {
        int bi = (int)(p - s) + base;
        Glyph *g = font_glyph(f, utf8_decode(&p, end));
        if (over && pen + g->adv > lim) {
            draw_text(f, pen, y, ELLIPSIS, 3, c);
            return pen;
        }
        bool hl = false;
        for (int k = 0; k < npos && !hl; k++) hl = pos[k] == bi;
        if (g->w) draw_glyph(g, roundf(pen) + g->bx, bl - g->by, hl ? hc : c);
        pen += g->adv;
    }
    return pen;
}

static void tilde_path(const char *path, char *out, size_t cap) {
    const char *home = getenv("HOME");
    size_t hl = home ? strlen(home) : 0;
    if (hl > 1 && !strncmp(path, home, hl) && (path[hl] == '/' || !path[hl])) snprintf(out, cap, "~%s", path + hl);
    else snprintf(out, cap, "%s", path);
}

static void draw_palette(void) {
    if (!A.pal_open) return;
    draw_rect(0, 0, (f32)A.W, (f32)A.H, CA(0x000000, 0.35f));
    Font *f = &A.font, *fs = &A.font_small;
    f32 w = roundf(MIN(S(680), A.W - S(60))), ih = roundf(S(44)), rh = roundf(S(40));
    int vis = MIN(A.pal_n, 10);
    f32 h = ih + (A.pal_n ? vis * rh + S(12) : roundf(S(44)));
    f32 x = roundf((A.W - w) * 0.5f), y = roundf(A.r_content.y + S(24));
    A.pal_r = (Rect){x, y, w, h};
    A.pal_list = (Rect){x, y + ih + S(6), w, vis * rh};
    draw_rrect(x, y, w, h, S(12), C(COL_POPUP));
    draw_rrect_outline(x, y, w, h, S(12), 1, C(COL_CHIP));
    // input
    Rect in = {x + S(10), y + S(8), w - S(20), ih - S(10)};
    bool cmd = A.pal_te.buf[0] == '>';
    f32 r = roundf(S(5)), cx = in.x + S(12), cy = in.y + in.h * 0.5f - S(1);
    draw_rrect_outline(cx - r, cy - r, r * 2, r * 2, r, MAX(1.f, S(1.5f)), C(COL_ACCENT));
    draw_line(cx + r * 0.7f, cy + r * 0.7f, cx + r * 1.6f, cy + r * 1.6f, MAX(1.5f, S(1.6f)), C(COL_ACCENT));
    Rect field = {in.x + S(26), in.y, in.w - S(26), in.h};
    te_draw(&A.pal_te, f, field, roundf(S(4)), C(COL_TEXT));
    if (!A.pal_te.buf[0]) {
        const char *ph = "Go to folder or file\xE2\x80\xA6   (> commands, @ tabs)";
        draw_text(f, field.x + S(6), text_y(f, field.y, field.h), ph, (int)strlen(ph), C(COL_TEXT_FAINT));
    }
    draw_rect(x + 1, y + ih + S(1), w - 2, 1, C(COL_CONTROL));
    if (!A.pal_n) {
        const char *m = A.pal_pending ? "Searching\xE2\x80\xA6" : cmd ? "No matching commands" : A.pal_te.buf[0] == '@' ? "No matching tabs" : "No matches";
        draw_text(fs, x + S(20), text_y(fs, y + ih, S(44)), m, (int)strlen(m), C(COL_TEXT_DIM));
        return;
    }
    if (A.pal_sel < A.pal_top) A.pal_top = A.pal_sel;
    if (A.pal_sel >= A.pal_top + vis) A.pal_top = A.pal_sel - vis + 1;
    for (int vi = 0; vi < vis; vi++) {
        int i = A.pal_top + vi;
        if (i >= A.pal_n) break;
        PalItem *it = &g_pal_items[i];
        f32 ry = A.pal_list.y + vi * rh;
        bool sel = i == A.pal_sel;
        if (sel) draw_rrect(x + S(6), ry, w - S(12), rh, S(7), C(COL_SEL_FOCUS));
        f32 is = roundf(S(18)), ix = x + S(18), iy = roundf(ry + (rh - is) * 0.5f);
        if (it->kind == PI_CMD) {
            draw_rrect(ix, iy, is, is, S(4), C(sel ? COL_SEL_FOCUS : COL_CHIP));
            draw_chevron(ix + is * 0.45f, iy + is * 0.5f, S(7), 1, MAX(1.f, S(1.4f)), C(COL_TEXT));
        } else if (it->kind == PI_ENTRY) {
            struct stat st;
            bool dir = stat(it->text, &st) == 0 && S_ISDIR(st.st_mode);
            if (dir) draw_path_icon(ix, iy, is, it->text);
            else if (!draw_icon_for_name(ix, iy, is, path_basename(it->text), false)) draw_file_icon(ix, iy, is, 0x8C8F99);
        } else if (it->kind == PI_FILE) {
            if (!draw_icon_for_name(ix, iy, is, path_basename(it->text), false)) draw_file_icon(ix, iy, is, 0x8C8F99);
        } else if (it->kind == PI_PLACE && !strcmp(it->text, "/")) {
            draw_drive_icon(ix, iy, is);
        } else {
            draw_path_icon(ix, iy, is, it->text);
        }
        f32 tx = ix + is + S(12), avail = w - (tx - x) - S(20);
        Color tc = C(sel ? COL_SEL_TEXT : COL_TEXT), hc = C(sel ? COL_SEL_HL : COL_ACCENT), dc = C(sel ? COL_SEL_DIM : COL_TEXT_FAINT);
        // right-hand hint
        const char *hint = "";
        char hbuf[32];
        switch (it->kind) {
        case PI_CMD: hint = it->label ? it->label : ""; break;
        case PI_RECENT: hint = "recent"; break;
        case PI_PLACE: hint = "place"; break;
        case PI_ENTRY: hint = "in this folder"; break;
        case PI_TAB:
            if (it->tab == A.cur) snprintf(hbuf, sizeof hbuf, "current tab");
            else if (A.npanels > 1 && it->tab < A.ntabs) snprintf(hbuf, sizeof hbuf, "panel %d", panel_index(A.tabs[it->tab]->panel) + 1);
            else snprintf(hbuf, sizeof hbuf, "open tab");
            hint = hbuf;
            break;
        case PI_CLOSED: hint = "recently closed"; break;
        default: break;
        }
        f32 hw = text_width(fs, hint, (int)strlen(hint));
        draw_text(fs, x + w - S(20) - hw, text_y(fs, ry, rh), hint, (int)strlen(hint), dc);
        avail -= hw + S(16);
        f32 ty = text_y(f, ry, rh);
        if (it->kind == PI_CMD) {
            draw_text_hl(f, tx, ty, avail, it->text, (int)strlen(it->text), 0, it->pos, it->npos, tc, hc);
            continue;
        }
        const char *base = it->text[1] ? path_basename(it->text) : "/";
        int boff = (int)(base - it->text);
        const char *primary = (it->kind == PI_PLACE && it->label) ? it->label : is_trash_dir(it->text) ? "Trash" : base;
        f32 ex;
        if (primary == base) ex = draw_text_hl(f, tx, ty, avail, base, (int)strlen(base), boff, it->pos, it->npos, tc, hc);
        else ex = draw_text(f, tx, ty, primary, (int)strlen(primary), tc);
        char par[PATH_MAX], tp[PATH_MAX];
        parent_of(it->text, par, sizeof par);
        tilde_path(it->kind == PI_PLACE ? it->text : par, tp, sizeof tp);
        f32 rem = avail - (ex - tx) - S(12);
        if (rem > S(40)) draw_text_fit(fs, ex + S(12), text_y(fs, ry, rh), rem, tp, (int)strlen(tp), dc, 0);
    }
}

static void type_label(const Entry *e, char *buf, size_t cap) {
    if (e->flags & ENT_DIR) { snprintf(buf, cap, "Folder"); return; }
    if (e->ext && e->name_len - e->ext <= 10) {
        size_t n = e->name_len - e->ext, k;
        for (k = 0; k < n && k + 1 < cap; k++) buf[k] = (char)toupper((u8)e->name[e->ext + k]);
        snprintf(buf + k, cap - k, " file");
    } else snprintf(buf, cap, "File");
}

static f32 pv_row(Font *f, f32 x, f32 y, f32 w, const char *label, const char *value) {
    f32 lw = roundf(S(84)), h = roundf(f->line_h * 1.5f);
    draw_text(f, x, y, label, (int)strlen(label), C(COL_TEXT_FAINT));
    draw_text_fit(f, x + lw, y, w - lw, value, (int)strlen(value), C(COL_TEXT2), 0);
    return y + h;
}

static void draw_preview(void) {
    if (!A.preview_on || !A.ntabs) return;
    Rect r = A.r_preview;
    Pane *p = active_pane();
    Font *f = &A.font, *fs = &A.font_small;
    draw_rect(r.x, r.y, r.w, r.h, C(COL_PREVIEW_BG));
    bool dh = A.drag == DRAG_PREVIEW || (A.mouse_in && in_rect(A.r_pdiv, A.mx, A.my));
    draw_rect(r.x, r.y, 1, r.h, C(dh ? COL_ACCENT : COL_BORDER));
    f32 pad = roundf(S(16)), x = r.x + pad, w = r.w - pad * 2, y = r.y + pad;
    ClipSave cs = draw_clip_push((SDL_Rect){(int)r.x + 1, (int)r.y, (int)r.w - 1, (int)r.h});
    Listing *L = p->L;

    // several items selected: summary only
    if (L && p->order && p->sel_count > 1) {
        f32 is = roundf(S(64)), ix = roundf(x + (w - is) * 0.5f);
        draw_file_icon(ix + S(10), y + S(10), is, 0x5A5D66);
        draw_file_icon(ix, y, is, COL_ACCENT);
        y += is + S(20);
        char a[32], b[48], line[96];
        fmt_count(a, p->sel_count);
        snprintf(line, sizeof line, "%s items selected", a);
        draw_text(f, x + (w - text_width(f, line, (int)strlen(line))) * 0.5f, y, line, (int)strlen(line), C(COL_TEXT));
        y += roundf(f->line_h * 2);
        u64 total = 0;
        u32 nd = 0;
        for (u32 i = 0; i < p->count; i++) {
            Entry *e = &L->entries[p->order[i]];
            if (!p->sel[p->order[i]] || (e->flags & ENT_MATCH)) continue;
            if (e->flags & ENT_DIR) { nd++; total += entry_dir_size(e, NULL); }
            else if (atomic_load_explicit(&e->meta, memory_order_acquire) == META_READY) total += e->size;
        }
        fmt_size(b, sizeof b, total);
        y = pv_row(fs, x, y, w, "Total size", b);
        snprintf(line, sizeof line, "%u folder%s, %u file%s", nd, nd == 1 ? "" : "s", p->sel_count - nd, p->sel_count - nd == 1 ? "" : "s");
        pv_row(fs, x, y, w, "Contains", line);
        draw_clip_pop(cs);
        return;
    }

    Entry *e = (L && p->order && p->cursor >= 0 && p->cursor < (i64)p->count) ? &L->entries[p->order[p->cursor]] : NULL;
    if (e && (e->flags & ENT_MATCH)) e = &L->entries[content_file_of(L, p->order[p->cursor])]; // a line: its file's details
    bool ready = A.pv && !strcmp(A.pv->path, A.pv_want);
    PreviewResult *pv = ready ? A.pv : NULL;
    const char *name = e ? (strrchr(e->name, '/') ? strrchr(e->name, '/') + 1 : e->name)
                         : (p->path[1] ? path_basename(p->path) : "/");
    bool is_dir = e ? (e->flags & ENT_DIR) : true;

    // visual: image/thumbnail, else a large icon
    f32 vh = roundf(MIN(w * 0.8f, r.h * 0.42f));
    if (pv && pv->kind == PV_IMAGE && A.pv_tex) {
        f32 tw = (f32)pv->w, th = (f32)pv->h, sc = MIN(w / tw, vh / th);
        sc = MIN(sc, 2.f * A.scale); // don't blow up tiny icons too much
        SDL_FRect dst = {roundf(x + (w - tw * sc) * 0.5f), roundf(y + (vh - th * sc) * 0.5f), roundf(tw * sc), roundf(th * sc)};
        draw_rect(dst.x - 1, dst.y - 1, dst.w + 2, dst.h + 2, C(COL_FIELD));
        draw_flush();
        SDL_RenderTexture(A.ren, A.pv_tex, NULL, &dst);
        y += vh + S(16);
    } else if (!(pv && pv->kind == PV_TEXT)) {
        f32 is = roundf(MIN(S(96), vh * 0.7f)), ix = roundf(x + (w - is) * 0.5f);
        if (is_dir) draw_dir_icon(ix, y + S(8), is, name, false);
        else if (!e || !draw_icon_for_name(ix, y + S(8), is, name, e->mflags & MF_EXEC))
            draw_file_icon(ix, y + S(8), is, e ? file_color(e) : 0x8C8F99);
        y += is + S(28);
    }

    // name (wrapped)
    int st[4], ln[4];
    int nl = wrap_text(f, name, w, st, ln, 4);
    for (int i = 0; i < nl; i++) {
        draw_text_fit(f, x, y, w, name + st[i], ln[i], C(COL_TEXT_STRONG), 0);
        y += roundf(f->line_h * 1.2f);
    }
    y += roundf(S(10));
    draw_rect(x, y, w, 1, C(COL_FIELD));
    y += roundf(S(12));

    // info rows
    char buf[256], b2[64];
    if (e) {
        type_label(e, buf, sizeof buf);
        if (pv && pv->kind == PV_IMAGE && pv->src_w) snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "  \xC2\xB7  %d \xC3\x97 %d", pv->src_w, pv->src_h);
        y = pv_row(fs, x, y, w, "Type", buf);
        bool meta = atomic_load_explicit(&e->meta, memory_order_acquire) == META_READY;
        if (is_dir) {
            bool known;
            u64 ds = entry_dir_size(e, &known);
            if (known) { fmt_size(b2, sizeof b2, ds); y = pv_row(fs, x, y, w, "Size", b2); }
        } else if (meta) {
            char cnt[32];
            fmt_size(b2, sizeof b2, e->size);
            fmt_count(cnt, e->size);
            snprintf(buf, sizeof buf, "%s  (%s bytes)", b2, cnt);
            y = pv_row(fs, x, y, w, "Size", buf);
        }
        if (meta) {
            time_t t = (time_t)e->mtime;
            struct tm tm;
            localtime_r(&t, &tm);
            strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
            y = pv_row(fs, x, y, w, "Modified", buf);
        }
    }
    if (pv && pv->kind == PV_DIR) {
        char a[32], b[32];
        fmt_count(a, pv->n_dirs);
        fmt_count(b, pv->n_files);
        snprintf(buf, sizeof buf, "%s folder%s, %s file%s%s", a, pv->n_dirs == 1 ? "" : "s", b, pv->n_files == 1 ? "" : "s",
                 pv->truncated ? "+" : "");
        y = pv_row(fs, x, y, w, "Contains", buf);
    }
    char par[PATH_MAX], tp[PATH_MAX];
    if (e) {
        char full[PATH_MAX];
        path_join(full, sizeof full, p->path, e->name);
        parent_of(full, par, sizeof par);
    } else {
        parent_of(p->path, par, sizeof par);
    }
    tilde_path(par, tp, sizeof tp);
    y = pv_row(fs, x, y, w, "Location", tp);
    if (pv && pv->note[0]) {
        y += roundf(S(8));
        draw_text_fit(fs, x, y, w, pv->note, (int)strlen(pv->note), C(COL_TEXT_FAINT), 0);
        y += roundf(fs->line_h * 1.5f);
    }

    // text preview with line numbers
    if (pv && pv->kind == PV_TEXT && pv->text && A.mono_path) {
        y += roundf(S(10));
        Rect box = {x - S(6), y, w + S(12), r.y + r.h - pad - y};
        if (box.h > S(40)) {
            draw_rrect(box.x, box.y, box.w, box.h, S(8), C(COL_SUNKEN));
            ClipSave c2 = draw_clip_push((SDL_Rect){(int)box.x + 2, (int)box.y + 2, (int)box.w - 4, (int)box.h - 4});
            Font *fm = &A.font_mono;
            f32 lh = roundf(fm->line_h * 1.15f), ty = box.y + S(8);
            f32 gw = text_width(fm, "00000", 5);
            const char *s0 = pv->text;
            int lineno = pv->first_line > 0 ? pv->first_line : 1;
            // contents search: the line in the upper third, it and the matches highlighted
            Pane *ap = active_pane();
            const CQuery *cq = ap->L && ap->L->is_content && pv->want_line ? ap->L->cq : NULL;
            if (pv->want_line) {
                int vis = (int)((box.h - S(8)) / lh), skip_to = pv->want_line - vis / 3;
                while (*s0 && lineno < skip_to) {
                    const char *eol = strchr(s0, '\n');
                    if (!eol) break;
                    s0 = eol + 1;
                    lineno++;
                }
            }
            while (*s0 && ty < box.y + box.h - S(4)) {
                const char *eol = strchr(s0, '\n');
                int len = eol ? (int)(eol - s0) : (int)strlen(s0);
                char num[16];
                snprintf(num, sizeof num, "%d", lineno++);
                draw_text(fm, box.x + S(8) + gw - text_width(fm, num, (int)strlen(num)), ty, num, (int)strlen(num), C(COL_TEXT_FAINT));
                char exp[1024];
                int o = 0;
                for (int i = 0; i < len && o < (int)sizeof exp - 5; i++) {
                    if (s0[i] == '\t') { for (int k = 0; k < 4; k++) exp[o++] = ' '; }
                    else if (s0[i] != '\r') exp[o++] = s0[i];
                }
                f32 tx0 = box.x + S(16) + gw;
                if (lineno - 1 == pv->want_line) draw_rect(box.x + S(2), ty - roundf((lh - fm->line_h) * 0.5f), box.w - S(4), lh, CA(COL_ACCENT, 0.16f));
                if (cq) { // every match on the line
                    size_t ms, ml, from = 0;
                    for (int guard = 0; guard < 64 && cq_find(cq, exp, (size_t)o, from, &ms, &ml) && ml; guard++) {
                        f32 mx = tx0 + text_width(fm, exp, (int)ms), mw = text_width(fm, exp + ms, (int)ml);
                        if (mx > box.x + box.w) break;
                        draw_rrect(mx - S(1), ty - S(1), mw + S(2), fm->line_h + S(2), S(2), CA(COL_ACCENT, 0.38f));
                        from = ms + ml;
                    }
                }
                draw_text_fit(fm, tx0, ty, box.w - gw - S(24), exp, o, C(COL_TEXT2), 0);
                ty += lh;
                if (!eol) break;
                s0 = eol + 1;
            }
            draw_clip_pop(c2);
        }
    }
    draw_clip_pop(cs);
}
