// picker.c - the app as the system file chooser.
//
// `file-explorer --pick file|files|dir|save --out FILE [PATH]` opens a separate window (its own app id,
// dev.fileexplorer.picker, so the compositor can float it) with a bar at the bottom: what's being asked, the file
// name for Save, Cancel and the main button. The chosen path(s) are written to FILE, one per line; cancelling
// leaves it empty. xdg-desktop-portal-termfilechooser runs this for every app's Open / Save / Choose folder
// dialog (set up by Options > Integration, integration.c). The picker never changes the user's settings or session.

enum { PICK_NONE, PICK_FILE, PICK_FILES, PICK_DIR, PICK_SAVE };
static int g_pick;              // PICK_*
static char g_pick_out[PATH_MAX];
static TextEdit g_pick_name;    // Save: the file name
static bool g_pick_name_focus;  // Save: typing goes to the name field
static Rect g_pick_rname, g_pick_rok, g_pick_rcancel;
static char g_pick_save_path[PATH_MAX]; // waiting for "Replace?"

static bool picker_active(void) { return g_pick != PICK_NONE; }

static const char *picker_title(void) {
    switch (g_pick) {
    case PICK_FILE: return "Open file";
    case PICK_FILES: return "Open files";
    case PICK_DIR: return "Choose a folder";
    case PICK_SAVE: return "Save as";
    }
    return "";
}

static void picker_finish(char **paths, int n) {
    FILE *f = g_pick_out[0] ? fopen(g_pick_out, "w") : NULL;
    for (int i = 0; f && i < n; i++) fprintf(f, "%s\n", paths[i]);
    if (f) fclose(f);
    if (A.selftest) { A.pick_done = n ? 1 : -1; return; } // (tests keep running)
    A.quit = true;
}

static void picker_cancel(void) { picker_finish(NULL, 0); }

static bool picker_folder_ok(Pane *p) {
    if (pane_is_archive(p) || pane_is_trash(p)) { toast("Choose a regular folder"); return false; }
    return true;
}

static bool path_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// The folder Choose would return: the one selected (a single folder), else the folder being shown.
static void picker_dir_target(Pane *p, char *out, size_t cap) {
    snprintf(out, cap, "%s", p->path);
    if (p->sel_count != 1 || !p->order) return;
    for (u32 i = 0; i < p->count; i++)
        if (p->sel[p->order[i]] && (p->L->entries[p->order[i]].flags & ENT_DIR)) {
            path_join(out, cap, p->path, p->L->entries[p->order[i]].name);
            return;
        }
}

static void picker_replace_cb(int choice, bool check, void *ud) {
    (void)check; (void)ud;
    if (choice != 0) return;
    char *v[1] = {g_pick_save_path};
    picker_finish(v, 1);
}

static void picker_commit(void) {
    Pane *p = active_pane();
    if (g_pick == PICK_DIR) {
        if (!picker_folder_ok(p)) return;
        char dir[PATH_MAX];
        picker_dir_target(p, dir, sizeof dir);
        char *v[1] = {dir};
        picker_finish(v, 1);
        return;
    }
    if (g_pick == PICK_SAVE) {
        const char *nm = g_pick_name.buf;
        while (*nm == ' ') nm++;
        if (!nm[0]) { toast("Type a file name"); g_pick_name_focus = true; return; }
        if (strchr(nm, '/')) { toast("A file name can't contain /"); return; }
        if (!picker_folder_ok(p)) return;
        path_join(g_pick_save_path, sizeof g_pick_save_path, p->path, nm);
        if (path_is_dir(g_pick_save_path)) { pane_navigate(p, g_pick_save_path, true); te_set(&g_pick_name, "", 0, 0); return; }
        if (access(g_pick_save_path, F_OK) == 0) {
            static const char *btn[] = {"Replace", "Cancel"};
            char title[NAME_MAX + 40];
            snprintf(title, sizeof title, "Replace \xE2\x80\x9C%s\xE2\x80\x9D?", nm);
            dialog_open(title, "A file with this name already exists in this folder. Replacing it overwrites its contents.",
                        btn, 2, 1, 1, NULL, picker_replace_cb, NULL);
            A.dlg.icon = DI_WARN;
            return;
        }
        char *v[1] = {g_pick_save_path};
        picker_finish(v, 1);
        return;
    }
    // Open: the selected files (a single selected folder is opened instead)
    int n = 0;
    char **paths = pane_selection_paths(p, &n);
    if (n == 1 && path_is_dir(paths[0])) {
        pane_navigate(p, paths[0], true);
        strv_free(paths, n);
        return;
    }
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (path_is_dir(paths[i])) { free(paths[i]); continue; }
        paths[k++] = paths[i];
    }
    if (g_pick == PICK_FILE && k > 1) { for (int i = 1; i < k; i++) free(paths[i]); k = 1; }
    if (!k) toast(g_pick == PICK_FILES ? "Select one or more files" : "Select a file");
    else if (pane_is_archive(p)) toast("Files inside archives can't be chosen: extract them first");
    else picker_finish(paths, k);
    strv_free(paths, k);
}

// Enter / double-click on a file (open_entry): Open picks it; Save takes its name.
static bool picker_open_file(Pane *p, const char *full) {
    if (!picker_active()) return false;
    if (g_pick == PICK_SAVE) {
        const char *b = path_basename(full);
        te_set(&g_pick_name, b, (int)strlen(b), (int)strlen(b));
        picker_commit(); // asks before replacing
        return true;
    }
    if (g_pick == PICK_DIR) return true; // files aren't the answer here
    (void)p;
    picker_commit();
    return true;
}

static f32 picker_bar_h(void) { return roundf(S(54)); }
static f32 picker_tab_h(void) { return roundf(S(24)); }

static void quad_pt(f32 *px, f32 *py, f32 x0, f32 y0, f32 cx, f32 cy, f32 x1, f32 y1, f32 t) {
    f32 u = 1 - t;
    *px = u * u * x0 + 2 * u * t * cx + t * t * x1;
    *py = u * u * y0 + 2 * u * t * cy + t * t * y1;
}

// The title tab, shaped like the top of a folder: it grows out of the bar's top edge at the window's left edge
// and its right side drops back to the bar in a straight diagonal, both bends slightly rounded. It lives in the
// strip layout() keeps free under the sidebar, so it never covers files.
static void draw_picker_tab(Rect s, const char *cap) {
    Font *fs = &A.font_small;
    f32 th = picker_tab_h(), k = th / 24.f, y0 = s.y - th, sw = A.r_sidebar.w; // y0: the sidebar's end
    f32 dw = roundf(22 * k), lt = MAX(1.f, roundf(S(1)));
    f32 tx = roundf(S(16)), tw = MIN(text_width(fs, cap, (int)strlen(cap)), sw - dw - tx - S(4));
    f32 bw = roundf(tx + tw + S(4)); // body; the diagonal piece follows it
    // under the tab: the sidebar's colour and right edge, carried down to the bar
    draw_rect(0, y0, sw, th, C(COL_SIDEBAR));
    draw_rect(sw - 1, y0, 1, th, C(COL_BORDER));
    // the outline's centre line, in the 22x24 design box: flat, soft bend, diagonal, soft bend, flat
    f32 ax = bw, ay = y0 + 0.5f, by = s.y + 0.5f, ky = (by - ay) / 23.f; // top / bottom border centre rows
    static const f32 seg[][6] = {{1, 0, 3.2f, 0, 4.2f, 2}, {14, 21.5f, 15.2f, 23, 18, 23}};
    f32 px[16], py[16];
    int n = 0;
    px[n] = ax; py[n++] = ay;
    for (int i = 0; i < 2; i++) {
        f32 x0 = ax + seg[i][0] * k, y0s = ay + seg[i][1] * ky, cx = ax + seg[i][2] * k, cy = ay + seg[i][3] * ky;
        f32 x1 = ax + seg[i][4] * k, y1 = ay + seg[i][5] * ky;
        for (int j = 0; j <= 5; j++) quad_pt(&px[n], &py[n], x0, y0s, cx, cy, x1, y1, j / 5.f), n++;
    }
    px[n] = ax + dw; py[n++] = by;
    // fill: the body, then vertical strips under the outline down through the bar's border row
    Color tb = C(COL_TOOLBAR);
    draw_rect(0, y0, bw, th + 1, tb);
    draw_reserve(n * 2, (n - 1) * 6);
    int base = g_nv;
    for (int i = 0; i < n; i++) dv(px[i], py[i], tb, WU, WU), dv(px[i], s.y + 1, tb, WU, WU);
    for (int i = 0; i + 1 < n; i++) di4(base + 2 * i, base + 2 * i + 2, base + 2 * i + 3, base + 2 * i + 1);
    // outline: the top edge from the window's left edge, then the curve
    Color bc = C(COL_BORDER);
    draw_rect(0, y0, bw, lt, bc);
    for (int i = 0; i + 1 < n; i++) draw_line(px[i], py[i], px[i + 1], py[i + 1], lt, bc);
    draw_text_fit(fs, tx, text_y(fs, y0 + 1, th), tw + 1, cap, (int)strlen(cap), C(COL_TEXT_DIM), 0);
}

// The bar: a box with the answer - icon, name and where it is - then Cancel and the main button, under a
// folder-top tab naming what's asked ("Selected folder", "Save as"...).
static void draw_picker_bar(Rect s) {
    Font *f = &A.font, *fs = &A.font_small;
    Pane *p = active_pane();
    draw_rect(s.x, s.y, s.w, s.h, C(COL_TOOLBAR));
    draw_rect(s.x, s.y, s.w, 1, C(COL_BORDER));
    f32 pad = roundf(S(16)), bh = roundf(S(32));
    f32 by = s.y + roundf((s.h - bh) * 0.5f);
    const char *ok = g_pick == PICK_SAVE ? "Save" : g_pick == PICK_DIR ? "Choose folder" : "Open";
    f32 bw = MAX(roundf(S(110)), roundf(text_width(f, ok, (int)strlen(ok)) + S(36))), cw = roundf(S(96));
    g_pick_rok = (Rect){s.x + s.w - pad - bw, by, bw, bh};
    g_pick_rcancel = (Rect){g_pick_rok.x - S(8) - cw, by, cw, bh};
    Rect box = {s.x + pad, by, g_pick_rcancel.x - S(16) - s.x - pad, bh};
    g_pick_rname = (Rect){0, 0, 0, 0};
    bool enabled = true;
    char cap[64], name[PATH_MAX + 32] = "", where[PATH_MAX] = "", icon_path[PATH_MAX] = "";
    if (g_pick == PICK_SAVE) {
        snprintf(cap, sizeof cap, "Save as");
        g_pick_rname = box;
        bool foc = g_pick_name_focus;
        char dnb[NAME_MAX + 1], in[NAME_MAX + 8];
        snprintf(in, sizeof in, "in %s", disp_name(p->path, dnb, sizeof dnb));
        f32 iw = MIN(text_width(fs, in, (int)strlen(in)), box.w * 0.4f);
        draw_rrect(box.x, box.y, box.w, box.h, S(7), C(foc ? COL_FIELD_HOVER : COL_FIELD));
        Rect tr = {box.x, box.y, box.w - iw - S(14), box.h};
        if (A.mouse_in && in_rect(box, A.mx, A.my)) A.ptr_text = true;
        if (foc) {
            draw_rrect_outline(box.x, box.y, box.w, box.h, S(7), MAX(1.f, S(1.2f)), C(COL_ACCENT));
            te_draw(&g_pick_name, f, tr, roundf(S(10)), C(COL_TEXT));
        } else draw_text_fit(f, tr.x + S(10), text_y(f, tr.y, tr.h), tr.w - S(14), g_pick_name.buf, (int)strlen(g_pick_name.buf), C(COL_TEXT), 0);
        if (!g_pick_name.buf[0] && !foc) draw_text(f, box.x + S(10), text_y(f, box.y, box.h), "File name", 9, C(COL_TEXT_FAINT));
        draw_text_fit(fs, box.x + box.w - S(10) - iw, text_y(fs, box.y, box.h), iw, in, (int)strlen(in), C(COL_TEXT_FAINT), 0);
        enabled = g_pick_name.buf[0] != 0;
    } else {
        if (g_pick == PICK_DIR) {
            snprintf(cap, sizeof cap, "Selected folder");
            picker_dir_target(p, icon_path, sizeof icon_path);
            char dnb[NAME_MAX + 1];
            snprintf(name, sizeof name, "%s", disp_name(icon_path, dnb, sizeof dnb));
        } else {
            snprintf(cap, sizeof cap, g_pick == PICK_FILES ? "Selected files" : "Selected file");
            int nf = 0;
            for (u32 i = 0; p->order && i < p->count; i++)
                if (p->sel[p->order[i]] && !(p->L->entries[p->order[i]].flags & ENT_DIR)) {
                    if (!nf) path_join(icon_path, sizeof icon_path, p->path, p->L->entries[p->order[i]].name);
                    nf++;
                }
            if (!nf && p->order && p->cursor >= 0 && p->cursor < p->count && !(p->L->entries[p->order[p->cursor]].flags & ENT_DIR)) {
                path_join(icon_path, sizeof icon_path, p->path, p->L->entries[p->order[p->cursor]].name);
                nf = 1;
            }
            if (g_pick == PICK_FILE && nf > 1) nf = 1;
            if (nf) {
                if (nf > 1) snprintf(name, sizeof name, "%s + %d more", path_basename(icon_path), nf - 1);
                else snprintf(name, sizeof name, "%s", path_basename(icon_path));
            }
            enabled = nf > 0;
        }
        if (icon_path[0]) { // where it is: the parent folder, ~ for home
            char par[PATH_MAX];
            parent_of(icon_path, par, sizeof par);
            tilde_path(par, where, sizeof where);
        }
        // the answer, in a read-only box
        draw_rrect(box.x, box.y, box.w, box.h, S(7), C(COL_FIELD));
        f32 x = box.x + S(10);
        if (enabled && icon_path[0]) {
            f32 is = roundf(S(18));
            if (g_pick == PICK_DIR || !draw_icon_for_name(roundf(x), roundf(box.y + (box.h - is) * 0.5f), is, path_basename(icon_path), false))
                draw_path_icon(roundf(x), roundf(box.y + (box.h - is) * 0.5f), is, icon_path);
            x += is + S(8);
            f32 nw = MIN(text_width(f, name, (int)strlen(name)), box.x + box.w - S(10) - x);
            draw_text_fit(f, x, text_y(f, box.y, box.h), nw, name, (int)strlen(name), C(COL_TEXT_STRONG), 0);
            x += nw + S(10);
            if (where[0] && x < box.x + box.w - S(40))
                draw_text_fit(fs, x, text_y(fs, box.y, box.h), box.x + box.w - S(10) - x, where, (int)strlen(where), C(COL_TEXT_FAINT), 0);
        } else {
            const char *h = g_pick == PICK_FILES ? "No files selected: select one or more, then Open" : "No file selected: double-click a file, or select one and Open";
            draw_text_fit(f, x, text_y(f, box.y, box.h), box.w - S(20), h, (int)strlen(h), C(COL_TEXT_FAINT), 0);
        }
    }
    draw_picker_tab(s, cap);
    sheet_button(g_pick_rcancel, "Cancel", false, true);
    sheet_button(g_pick_rok, ok, true, enabled);
}

static bool picker_mouse(f32 x, f32 y) {
    if (!picker_active() || !in_rect(A.r_status, x, y)) {
        if (picker_active()) g_pick_name_focus = false; // clicking elsewhere: keys go to the list
        return false;
    }
    if (in_rect(g_pick_rok, x, y)) picker_commit();
    else if (in_rect(g_pick_rcancel, x, y)) picker_cancel();
    else if (g_pick_rname.w > 0 && in_rect(g_pick_rname, x, y)) g_pick_name_focus = true;
    A.redraw = true;
    return true;
}

// Keys before the list gets them. Escape cancels unless something else would use it.
static bool picker_key(SDL_Keycode k, SDL_Keymod mod) {
    if (!picker_active()) return false;
    Pane *p = active_pane();
    if (g_pick_name_focus) {
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) picker_commit();
        else if (k == SDLK_ESCAPE) picker_cancel();
        else if (k == SDLK_TAB) g_pick_name_focus = false;
        else te_key(&g_pick_name, k, mod);
        return true;
    }
    if ((mod & SDL_KMOD_CTRL) && (k == SDLK_RETURN || k == SDLK_KP_ENTER)) { picker_commit(); return true; }
    if (k == SDLK_ESCAPE && !A.search_pane && !A.edit_pane && !p->sel_count) { picker_cancel(); return true; }
    if (k == SDLK_TAB && g_pick == PICK_SAVE && !(mod & SDL_KMOD_SHIFT)) { g_pick_name_focus = true; return true; }
    if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && g_pick == PICK_DIR && !A.search_pane && !A.edit_pane && !p->sel_count) {
        picker_commit(); // nothing selected: choose the folder being shown
        return true;
    }
    return false;
}

static bool picker_text(const char *text) {
    if (!picker_active() || !g_pick_name_focus) return false;
    te_insert(&g_pick_name, text);
    return true;
}

// --pick MODE: the start folder (and for Save the suggested name) from PATH.
static void picker_setup(const char *mode, const char **start, char *buf, size_t cap) {
    g_pick = !strcmp(mode, "files") ? PICK_FILES : !strcmp(mode, "dir") ? PICK_DIR : !strcmp(mode, "save") ? PICK_SAVE : PICK_FILE;
    te_set(&g_pick_name, "", 0, 0);
    const char *path = *start;
    if (!path || !path[0]) path = getenv("HOME") ? getenv("HOME") : "/";
    if (g_pick == PICK_SAVE && !path_is_dir(path)) { // ".../Downloads/report.pdf": folder + name
        const char *b = path_basename(path);
        te_set(&g_pick_name, b, (int)strlen(b), (int)strlen(b));
        // the stem is selected, like F2 (typing replaces the name, the extension stays)
        const char *dot = strrchr(b, '.');
        if (dot && dot != b) { g_pick_name.anchor = 0; g_pick_name.caret = (int)(dot - b); }
        else { g_pick_name.anchor = 0; g_pick_name.caret = (int)strlen(b); }
        parent_of(path, buf, cap);
        if (!path_is_dir(buf)) snprintf(buf, cap, "%s", getenv("HOME") ? getenv("HOME") : "/");
        g_pick_name_focus = true;
    } else if (!path_is_dir(path) && g_pick != PICK_SAVE) {
        // a suggested file: open its folder (the file is selected by the normal FILE argument handling)
        snprintf(buf, cap, "%s", path);
    } else snprintf(buf, cap, "%s", path);
    if (g_pick == PICK_SAVE && !g_pick_name.buf[0]) g_pick_name_focus = true;
    *start = buf;
}
