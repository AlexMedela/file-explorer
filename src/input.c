// input.c - keyboard, text and mouse handlers, and SDL event dispatch (handle_event) (included from app.c)

// --- input -----------------------------------------------------------------

static void dialog_key(SDL_Keycode k, SDL_Keymod mod) {
    Dialog *d = &A.dlg;
    switch (k) {
    case SDLK_ESCAPE: dialog_choose(d->cancel); break;
    case SDLK_RETURN: case SDLK_KP_ENTER: dialog_choose(d->focus); break;
    case SDLK_LEFT: d->focus = (d->focus + d->nbuttons - 1) % d->nbuttons; break;
    case SDLK_RIGHT: d->focus = (d->focus + 1) % d->nbuttons; break;
    case SDLK_TAB: d->focus = (d->focus + (mod & SDL_KMOD_SHIFT ? d->nbuttons - 1 : 1)) % d->nbuttons; break;
    case SDLK_SPACE: if (d->has_check) d->check = !d->check; break;
    case SDLK_UP: if (d->nopts) d->opt = (d->opt + d->nopts - 1) % d->nopts; break;
    case SDLK_DOWN: if (d->nopts) d->opt = (d->opt + 1) % d->nopts; break;
    default:
        if (d->nopts && k >= SDLK_1 && k < SDLK_1 + (SDL_Keycode)d->nopts) d->opt = (int)(k - SDLK_1);
        break;
    }
    A.redraw = true;
}

// Cancel a running op, or dismiss a finished one (its card / queue row).
static void op_close_or_cancel(Op *op) {
    if (atomic_load(&op->phase) == OPS_DONE) op->dismissed = true;
    else op_cancel(op);
    A.redraw = true;
}

// The transfer queue has the keyboard while it's open: Up/Down pick a row, Ctrl+Up/Down (or Alt) move it in the
// queue, Space pauses / resumes, Delete cancels (or dismisses), Esc closes. Other keys go on as usual.
static bool queue_key(SDL_Keycode k, SDL_Keymod mod) {
    bool move = mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT);
    Op *op = A.q_sel >= 0 && A.q_sel < A.nops ? A.ops[A.q_sel] : NULL;
    switch (k) {
    case SDLK_ESCAPE: A.q_open = false; break;
    case SDLK_UP:
        if (move) op_move_in_queue(A.q_sel, -1);
        else if (A.q_sel > 0) A.q_sel--;
        break;
    case SDLK_DOWN:
        if (move) op_move_in_queue(A.q_sel, 1);
        else if (A.q_sel < A.nops - 1) A.q_sel++;
        break;
    case SDLK_SPACE: if (op) op_set_paused(op, !atomic_load(&op->paused)); break;
    case SDLK_DELETE: case SDLK_BACKSPACE: if (op) op_close_or_cancel(op); break;
    default: return false;
    }
    A.redraw = true;
    return true;
}

static bool queue_mouse(f32 x, f32 y) {
    if (!in_rect(A.q_r, x, y)) return false;
    if (in_rect(A.q_verify, x, y)) { // also for what hasn't started yet
        A.verify_copies = !A.verify_copies;
        A.settings_dirty = true;
        for (int i = 0; i < A.nops; i++)
            if (op_is_transfer(A.ops[i]) && atomic_load(&A.ops[i]->phase) < OPS_RUNNING) atomic_store(&A.ops[i]->verify, A.verify_copies);
        return true;
    }
    for (int i = 0; i < A.nops; i++) {
        Op *op = A.ops[i];
        if (in_rect(A.q_close[i], x, y)) { op_close_or_cancel(op); return true; }
        if (in_rect(A.q_pause[i], x, y)) { op_set_paused(op, !atomic_load(&op->paused)); return true; }
        if (in_rect(A.q_up[i], x, y)) { op_move_in_queue(i, -1); return true; }
        if (in_rect(A.q_down[i], x, y)) { op_move_in_queue(i, 1); return true; }
        if (in_rect(A.q_row[i], x, y)) { A.q_sel = i; return true; }
    }
    return true;
}

static void on_key(SDL_KeyboardEvent *k) {
    Pane *p = active_pane();
    bool ctrl = k->mod & SDL_KMOD_CTRL, shift = k->mod & SDL_KMOD_SHIFT;
    A.redraw = true;

    if (A.dlg.open) { dialog_key(k->key, k->mod); return; }
    if (sheet_open()) { sheet_key(k->key, k->mod); return; }
    if (A.pal_open) { pal_key(k->key, k->mod); return; }
    if (A.menu.open) { menu_key(k->key); return; }
    if (A.q_open && !A.ren_pane && !A.search_pane && !A.edit_pane && A.qa_edit_g < 0 && !active_term() && queue_key(k->key, k->mod)) return;
    if (A.drag != DRAG_NONE && k->key == SDLK_ESCAPE) { drag_cancel(); return; }
    if (!A.ren_pane && A.qa_edit_g < 0 && picker_key(k->key, k->mod)) return;
    if (active_term()) { // the terminal has the keyboard; the app keeps tab switching, closing and its menus
        Term *tt = active_term();
        bool tabsw = ctrl && (k->key == SDLK_TAB || k->key == SDLK_PAGEUP || k->key == SDLK_PAGEDOWN);
        if (ctrl && shift && k->key == SDLK_W) { tab_close(A.cur); return; }
        if (ctrl && shift && k->key == SDLK_T) { term_tab_open(A.ap->cur->pane[0].path, NULL); return; }
        if (ctrl && shift && k->key == SDLK_P) { run_command(CMD_PALETTE); return; }
        if (ctrl && !shift && k->key == SDLK_COMMA) { run_command(CMD_OPTIONS); return; }
        if (!tabsw && term_key(tt, k->key, k->mod)) return;
    }

    if (A.ren_pane) {
        if (k->key == SDLK_ESCAPE) A.ren_pane = NULL;
        else if (k->key == SDLK_RETURN || k->key == SDLK_KP_ENTER || k->key == SDLK_TAB) commit_rename();
        else te_key(&A.ren_te, k->key, k->mod);
        return;
    }
    if (A.qa_edit_g >= 0) { // renaming in the sidebar
        if (k->key == SDLK_ESCAPE) A.qa_edit_g = -1;
        else if (k->key == SDLK_RETURN || k->key == SDLK_KP_ENTER || k->key == SDLK_TAB) qa_edit_commit();
        else te_key(&A.qa_te, k->key, k->mod);
        return;
    }

    // rebindable shortcuts (keys.c); in a text field only the global ones
    bool text_mode = A.search_pane || A.edit_pane;
    if (keys_dispatch(k, text_mode)) return;

    // fixed shortcuts that work while typing too
    if (ctrl) {
        if (shift && k->key >= SDLK_1 && k->key <= SDLK_6) { // views, like Explorer's Ctrl+Shift+1..6
            static const int map[6] = {7, 6, 4, 2, 1, 0};    // XL, L, M, S, List, Details
            set_zoom_level(p, map[k->key - SDLK_1]);
            return;
        }
        if (!shift && k->key >= SDLK_1 && k->key <= SDLK_9) { // tabs of the active panel
            Tab *t2 = panel_tab(A.ap, k->key == SDLK_9 ? -1 : (int)(k->key - SDLK_1));
            if (t2) tab_select(tab_index(t2));
            return;
        }
    }

    if (A.search_pane) {
        Pane *sp = A.search_pane;
        if (k->key == SDLK_ESCAPE) search_exit(sp);
        else if (k->key == SDLK_R && (k->mod & SDL_KMOD_ALT)) search_toggle_recursive(sp);
        else if (k->key == SDLK_T && (k->mod & SDL_KMOD_ALT)) search_set_contents(sp, !A.search_contents);
        else if (A.search_contents && (k->mod & SDL_KMOD_ALT) && (k->key == SDLK_C || k->key == SDLK_W || k->key == SDLK_X))
            search_cs_option(sp, k->key == SDLK_C ? 0 : k->key == SDLK_W ? 1 : 2);
        else if (k->key == SDLK_RETURN || k->key == SDLK_KP_ENTER || k->key == SDLK_DOWN) {
            search_apply(sp, true);
            A.search_pane = NULL; // keyboard goes to the results
        } else if (te_key(&A.search_te, k->key, k->mod)) {
            search_typed(sp);
        }
        return;
    }

    if (A.edit_pane) {
        if (k->key == SDLK_ESCAPE) A.edit_pane = NULL;
        else if (k->key == SDLK_RETURN || k->key == SDLK_KP_ENTER) commit_edit();
        else te_key(&A.edit, k->key, k->mod);
        return;
    }

    if (pane_du_on(p) && du_key(p, k->key, k->mod)) return; // disk usage map: its own list of the biggest items
    if (pane_snap_on(p) && snap_key(p, k->key)) return;    // snapshots view: its list

    // navigation and selection (not rebindable)
    int pr = MAX(1, p->ly.per_row);
    i64 page = MAX(1, (i64)(p->ly.list.h / p->ly.cell_h) - 1) * pr;
    switch (k->key) {
    case SDLK_DOWN: move_cursor(p, p->cursor < 0 ? 0 : MIN(p->cursor + pr, (i64)p->count - 1), shift, ctrl); break;
    case SDLK_UP: move_cursor(p, p->cursor < 0 ? 0 : (p->cursor >= pr ? p->cursor - pr : p->cursor), shift, ctrl); break;
    case SDLK_PAGEDOWN: move_cursor(p, p->cursor + page, shift, ctrl); break;
    case SDLK_PAGEUP: move_cursor(p, p->cursor - page, shift, ctrl); break;
    case SDLK_HOME: move_cursor(p, 0, shift, ctrl); break;
    case SDLK_END: move_cursor(p, (i64)p->count - 1, shift, ctrl); break;
    case SDLK_RETURN: case SDLK_KP_ENTER: open_entry(p, p->cursor, ctrl); break;
    case SDLK_BACKSPACE:
        if (p->search_q[0]) search_exit(p);
        else pane_up(p);
        break;
    case SDLK_LEFT:
        if (p->L && p->L->is_content && p->cursor >= 0 && p->order) { // fold the file's lines / go to the file
            Entry *e = &p->L->entries[p->order[p->cursor]];
            if (e->flags & ENT_MATCH) {
                u32 fe = content_file_of(p->L, p->order[p->cursor]);
                for (i64 k2 = p->cursor - 1; k2 >= 0; k2--)
                    if (p->order[k2] == fe) { move_cursor(p, k2, shift, ctrl); break; }
            } else content_fold(p, p->cursor, 0);
        } else if (p->L && p->L->tree && p->cursor >= 0 && p->order) { // collapse, or jump to the parent folder
            Entry *e = &p->L->entries[p->order[p->cursor]];
            if ((e->flags & ENT_DIR) && tree_has(p->L->tree, e->name, e->name_len)) tree_set(p, p->cursor, 0);
            else if (strchr(e->name, '/')) {
                size_t pl = (size_t)(strrchr(e->name, '/') - e->name);
                for (i64 k = p->cursor - 1; k >= 0; k--) {
                    Entry *pe = &p->L->entries[p->order[k]];
                    if (pe->name_len == pl && !strncmp(pe->name, e->name, pl)) { move_cursor(p, k, shift, ctrl); break; }
                }
            }
        } else if (pr > 1 || p->view != VIEW_DETAILS) move_cursor(p, MAX(p->cursor - 1, 0), shift, ctrl);
        break;
    case SDLK_RIGHT:
        if (p->L && p->L->is_content && p->cursor >= 0 && p->order) { // unfold, or step to the first line
            Entry *e = &p->L->entries[p->order[p->cursor]];
            if (!(e->flags & ENT_MATCH) && (p->L->cinfo[p->order[p->cursor]] & CI_COLLAPSED)) content_fold(p, p->cursor, 1);
            else if (p->cursor + 1 < (i64)p->count) move_cursor(p, p->cursor + 1, shift, ctrl);
        } else if (p->L && p->L->tree && p->cursor >= 0 && p->order) { // expand, or step into it
            Entry *e = &p->L->entries[p->order[p->cursor]];
            if (!(e->flags & ENT_DIR)) break;
            if (!tree_has(p->L->tree, e->name, e->name_len)) tree_set(p, p->cursor, 1);
            else if (p->cursor + 1 < (i64)p->count) move_cursor(p, p->cursor + 1, shift, ctrl);
        } else if (pr > 1 || p->view != VIEW_DETAILS) move_cursor(p, p->cursor + 1, shift, ctrl);
        break;
    case SDLK_ESCAPE:
        if (p->search_q[0] && !p->sel_count) { search_exit(p); break; }
        sel_clear(p);
        A.jump[0] = 0;
        if (g_clip.cut) clip_clear();
        break;
    case SDLK_APPLICATION: case SDLK_F10:
        if (k->key == SDLK_F10 && !shift) break;
        {
            f32 y = p->cursor >= 0 ? item_rect(p, p->cursor).y + p->ly.cell_h : p->ly.list.y;
            menu_open(p, p->ly.col_name + S(40), CLAMP(y, p->ly.list.y, p->ly.list.y + p->ly.list.h), p->sel_count > 0);
        }
        break;
    case SDLK_SPACE:
        if (ctrl && p->cursor >= 0) sel_set(p, p->cursor, !p->sel[p->order[p->cursor]]);
        break;
    default: break;
    }
}

static void on_text(const char *text) {
    if (A.dlg.open || A.menu.open) return;
    if (sheet_open()) { sheet_text(text); return; }
    if (!A.pal_open && picker_text(text)) { A.redraw = true; return; }
    if (A.pal_open) {
        te_insert(&A.pal_te, text);
        pal_changed();
        A.redraw = true;
        return;
    }
    if (active_term()) { term_text(active_term(), text); return; }
    if (A.search_pane) {
        te_insert(&A.search_te, text);
        search_typed(A.search_pane);
        return;
    }
    if (A.ren_pane) {
        te_insert(&A.ren_te, text);
        A.redraw = true;
        return;
    }
    if (A.qa_edit_g >= 0) {
        te_insert(&A.qa_te, text);
        A.redraw = true;
        return;
    }
    if (A.edit_pane) {
        te_insert(&A.edit, text);
        A.redraw = true;
        return;
    }
    if (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) return;
    Pane *p = active_pane();
    if ((text[0] == '/' || text[0] == '~') && !A.jump[0]) { begin_edit(p, text); return; }
    if (text[0] == ' ' && A.q_open) return; // (pauses the highlighted transfer instead, see queue_key)
    if (text[0] == ' ' && !A.jump[0]) { preview_toggle(); return; } // Space: inspector, like File Pilot
    type_to_jump(p, text);
    A.redraw = true;
}

static i64 item_at(Pane *p, f32 x, f32 y) {
    PaneLayout *ly = &p->ly;
    if (y < ly->list.y || y >= ly->list.y + ly->list.h) return -1;
    f64 yy = (f64)y - ly->list.y - ly->pad + p->scroll;
    if (yy < 0) return -1;
    i64 r = (i64)(yy / ly->cell_h), c = 0;
    if (ly->per_row > 1) {
        f32 xx = x - ly->list.x - ly->pad;
        if (xx < 0 || x >= ly->track.x) return -1;
        c = (i64)(xx / ly->cell_w);
        if (c >= ly->per_row) return -1;
    }
    if (p->nhdr) { // grouped: rows map to items through row_of (header rows hit nothing)
        i64 k = grp_first_pos(p, r);
        return (k < (i64)p->count && (i64)p->row_of[k] == r) ? k : -1;
    }
    i64 i = r * MAX(1, ly->per_row) + c;
    return (i >= 0 && i < (i64)p->count) ? i : -1;
}

static int grp_hdr_at(Pane *p, f32 y) {
    if (!p->nhdr) return -1;
    PaneLayout *ly = &p->ly;
    if (y < ly->list.y || y >= ly->list.y + ly->list.h) return -1;
    i64 r = (i64)(((f64)y - ly->list.y + p->scroll) / ly->cell_h);
    int k = grp_first_hdr(p, r);
    return k < p->nhdr && (i64)p->hdr[k].row == r ? k : -1;
}

// x-extent of the clickable name (icon + text) for a row
static f32 row_name_end(Pane *p, i64 r) {
    Entry *e = &p->L->entries[p->order[r]];
    f32 nx = p->ly.col_name + tree_indent(p->L, e) + roundf(S(16)) + roundf(S(9));
    const char *b = p->L->tree ? rel_base(e->name) : e->name;
    f32 w = text_width(&A.font, b, (int)strlen(b));
    return MIN(nx + w + S(8), p->ly.name_end - S(6));
}

static void menu_open_tab(Tab *t, f32 x, f32 y);

static void on_mouse_down_tabbar(Panel *pn, SDL_MouseButtonEvent *b, f32 x, f32 y) {
    bool shift = SDL_GetModState() & SDL_KMOD_SHIFT;
    if (b->button == SDL_BUTTON_LEFT && in_rect(pn->plus, x, y)) {
        panel_activate(pn);
        tab_new_in(pn, pn->cur->pane[0].path, panel_end_index(pn), true);
        return;
    }
    if (b->button == SDL_BUTTON_LEFT && in_rect(pn->splitbtn, x, y)) {
        panel_activate(pn);
        panel_split(pn, shift ? ZONE_BOTTOM : ZONE_RIGHT, NULL, pn->cur->pane[0].path);
        return;
    }
    for (int i = 0; i < A.ntabs; i++) {
        Tab *t = A.tabs[i];
        if (t->panel != pn || !in_rect(t->r, x, y)) continue;
        if (b->button == SDL_BUTTON_MIDDLE) { tab_close(i); return; }
        if (b->button == SDL_BUTTON_RIGHT) { tab_select(i); menu_open_tab(t, x, y); return; }
        if (b->button != SDL_BUTTON_LEFT) return;
        f32 cs = roundf(S(18));
        Rect close = {t->r.x + t->r.w - cs - roundf(S(8)), t->r.y + (t->r.h - cs) * 0.5f, cs, cs};
        if (A.ntabs > 1 && t->r.w > S(90) && in_rect(close, x, y)) { tab_close(i); return; }
        if (b->clicks >= 2 && pn->cur == t) { tab_new_in(pn, t->pane[0].path, i + 1, true); return; } // duplicate
        tab_select(i);
        A.drag = DRAG_TAB_PENDING; // may become a tab drag (reorder / move / split)
        A.drag_tab = t;
        A.band_mx0 = x;
        A.band_my0 = y;
        return;
    }
    if (b->button == SDL_BUTTON_LEFT && b->clicks >= 2) {
        panel_activate(pn);
        tab_new_in(pn, pn->cur->pane[0].path, panel_end_index(pn), true);
    }
}

// Where would the dragged tab land? Bar = reorder/move, panel centre = move, edge = new split.
static void tab_drop_update(f32 x, f32 y) {
    A.tdrop_kind = TD_NONE;
    A.tdrop_panel = NULL;
    A.tdrop_before = NULL;
    Tab *t = A.drag_tab;
    if (!t) return;
    bool dup = SDL_GetModState() & SDL_KMOD_CTRL;
    for (int i = 0; i < A.npanels; i++) {
        Panel *pn = A.panels[i];
        if (in_rect(pn->bar, x, y)) {
            A.tdrop_kind = TD_BAR;
            A.tdrop_panel = pn;
            for (int k = 0; k < A.ntabs; k++) {
                Tab *o = A.tabs[k];
                if (o->panel == pn && x < o->r.x + o->r.w * 0.5f) { A.tdrop_before = o; break; }
            }
            return;
        }
        if (!in_rect(pn->area, x, y)) continue;
        Rect a = {pn->area.x, pn->bar.y + pn->bar.h, pn->area.w, pn->area.h - pn->bar.h};
        f32 fx = (x - a.x) / MAX(a.w, 1.f), fy = (y - a.y) / MAX(a.h, 1.f);
        f32 d[4] = {fx, 1 - fx, fy, 1 - fy};
        int zone = ZONE_CENTER;
        f32 best = 0.25f;
        for (int k = 0; k < 4; k++)
            if (d[k] < best) { best = d[k]; zone = k; }
        bool alone = t->panel == pn && panel_ntabs(pn) == 1 && !dup;
        if (zone == ZONE_CENTER) {
            if (t->panel == pn && !dup) return; // already here
            A.tdrop_kind = TD_CENTER;
        } else {
            if (alone) return; // can't split a panel with its only tab
            A.tdrop_kind = TD_EDGE;
        }
        A.tdrop_panel = pn;
        A.tdrop_zone = zone;
        return;
    }
}

static void tab_drop_apply(void) {
    Tab *t = A.drag_tab;
    Panel *pn = A.tdrop_panel;
    int kind = A.tdrop_kind, zone = A.tdrop_zone;
    Tab *before = A.tdrop_before;
    bool dup = SDL_GetModState() & SDL_KMOD_CTRL;
    drag_cancel();
    if (!t || !pn || !panel_alive(pn) || tab_index(t) < 0) return;
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s", t->pane[0].path);
    if (kind == TD_BAR) {
        if (dup) tab_new_in(pn, path, before ? tab_index(before) : panel_end_index(pn), true);
        else tab_move(t, pn, before);
    } else if (kind == TD_CENTER) {
        if (dup) tab_new_in(pn, path, panel_end_index(pn), true);
        else tab_move(t, pn, NULL);
    } else if (kind == TD_EDGE) {
        panel_split(pn, zone, dup ? NULL : t, path);
    }
}

static void on_mouse_down_pane(Pane *p, SDL_MouseButtonEvent *b, f32 x, f32 y) {
    PaneLayout *ly = &p->ly;
    SDL_Keymod mod = SDL_GetModState();
    bool ctrl = mod & SDL_KMOD_CTRL, shift = mod & SDL_KMOD_SHIFT;
    set_active_pane(p);
    A.click_open = -1;

    if (b->button == SDL_BUTTON_X1) { pane_history(p, -1); return; }
    if (b->button == SDL_BUTTON_X2) { pane_history(p, +1); return; }
    if (pane_du_on(p) && in_rect(ly->list, x, y)) { du_mouse_down(p, b, x, y); return; }
    if (pane_snap_on(p) && in_rect(ly->list, x, y)) { snap_mouse_down(p, b, x, y); return; }
    if (ly->has_snapbar && in_rect(ly->snapbar, x, y)) { snapbar_click(p, x, y); return; }

    if (b->button == SDL_BUTTON_RIGHT && in_rect(ly->header, x, y)) { menu_open_columns(p, x, y); return; }
    if (b->button == SDL_BUTTON_RIGHT) {
        if (in_rect(ly->list, x, y) && p->order && !(ly->has_scroll && in_rect(ly->track, x, y))) {
            i64 r = item_at(p, x, y);
            if (r >= 0) {
                if (!p->sel[p->order[r]]) move_cursor(p, r, false, false);
                else p->cursor = r;
                menu_open(p, x, y, true);
            } else {
                sel_clear(p);
                menu_open(p, x, y, false);
            }
        }
        return;
    }
    if (b->button == SDL_BUTTON_MIDDLE) {
        i64 r = item_at(p, x, y);
        if (r >= 0 && in_rect(ly->list, x, y)) open_entry(p, r, true);
        return;
    }
    if (b->button != SDL_BUTTON_LEFT) return;

    if (in_rect(ly->back, x, y)) { pane_history(p, -1); return; }
    if (in_rect(ly->fwd, x, y)) { pane_history(p, +1); return; }
    if (in_rect(ly->up, x, y)) { pane_up(p); return; }
    if (in_rect(ly->filterbtn, x, y)) { menu_open_types(p, ly->filterbtn.x, ly->filterbtn.y + ly->filterbtn.h + S(4)); return; }
    if (ly->has_cmpbar && in_rect(ly->cmpbar, x, y)) { cmp_bar_click(p, x, y); return; }
    if (ly->has_trashbar && in_rect(ly->trashbar, x, y)) {
        if (in_rect(ly->rrestore, x, y)) trash_restore(p, p->sel_count == 0);
        else if (in_rect(ly->rempty, x, y)) trash_delete(p, true);
        return;
    }
    if (in_rect(ly->pathbar, x, y)) {
        if (A.edit_pane == p) return;
        if (ly->has_gitchip && in_rect(ly->gitchip, x, y)) { // branch chip: repository details
            menu_open_git(p, ly->gitchip.x, ly->gitchip.y + ly->gitchip.h + S(4));
            return;
        }
        layout_crumbs(p);
        for (int i = 0; i < ly->ncrumbs; i++) {
            if (in_rect(ly->crumbs[i].r, x, y)) {
                char path[PATH_MAX];
                int end = ly->crumbs[i].end;
                memcpy(path, p->path, (size_t)end);
                path[end] = 0;
                pane_navigate(p, path, true);
                return;
            }
        }
        begin_edit(p, NULL);
        return;
    }
    if (ly->has_sbar && in_rect(ly->sbar, x, y)) {
        Rect field, toggle, close, cont, opt[3];
        search_bar_layout(p, &field, &toggle, &close, &cont, opt);
        if (in_rect(close, x, y)) search_exit(p);
        else if (in_rect(toggle, x, y)) search_toggle_recursive(p);
        else if (in_rect(cont, x, y)) search_set_contents(p, !A.search_contents);
        else if (A.search_contents && in_rect(opt[0], x, y)) search_cs_option(p, 0);
        else if (A.search_contents && in_rect(opt[1], x, y)) search_cs_option(p, 1);
        else if (A.search_contents && in_rect(opt[2], x, y)) search_cs_option(p, 2);
        else search_focus(p);
        return;
    }
    if (in_rect(ly->header, x, y)) {
        int edge = header_edge_at(ly, x);
        if (edge >= 0) { // resize: the column left of the edge (Name's edge resizes the next column)
            int c = ly->cid[edge];
            if (c == COL_NAME) c = ly->cid[edge + 1];
            A.drag = DRAG_COL_RESIZE;
            A.drag_pane = p;
            A.drag_col = c;
            A.drag_col_w0 = A.colw[c];
            A.band_mx0 = x;
            A.drag_off = ly->cid[edge] == COL_NAME ? -1.f : 1.f; // direction
            return;
        }
        int ci = header_col_at(ly, x);
        if (ci >= 0) { // click = sort (on release), drag = reorder
            A.drag = DRAG_COL_PENDING;
            A.drag_pane = p;
            A.drag_col = ly->cid[ci];
            A.band_mx0 = x;
            A.band_my0 = y;
            A.col_insert = -1;
        }
        return;
    }
    if (ly->has_scroll && in_rect(ly->track, x, y)) {
        if (in_rect(ly->thumb, x, y)) {
            A.drag = DRAG_SCROLL;
            A.drag_pane = p;
            A.drag_off = y - ly->thumb.y;
        } else {
            f64 page = ly->list.h - ly->row_h;
            p->scroll_target += y < ly->thumb.y ? -page : page;
            p->scroll_target = CLAMP(p->scroll_target, 0.0, max_scroll(p));
        }
        return;
    }
    if (in_rect(ly->list, x, y)) {
        if (!p->order) return;
        int gh = grp_hdr_at(p, y);
        if (gh >= 0) { group_toggle(p, gh); return; }
        i64 r = item_at(p, x, y);
        if (r >= 0 && p->L->is_content && !(p->L->entries[p->order[r]].flags & ENT_MATCH) && x < p->ly.col_name + S(14)) {
            content_fold(p, r, -1); // the chevron in front of a file
            return;
        }
        if (r >= 0 && p->L->tree && (p->L->entries[p->order[r]].flags & ENT_DIR)) { // tree chevron
            f32 cx = ly->col_name + tree_indent(p->L, &p->L->entries[p->order[r]]);
            if (x >= cx - S(18) && x < cx - S(1)) { tree_set(p, r, -1); return; }
        }
        bool on_name = r >= 0 && (p->view != VIEW_DETAILS || x < row_name_end(p, r));
        if (r >= 0 && b->clicks >= 2 && r == p->cursor && !ctrl && !shift && !A.single_click) {
            open_entry(p, r, false);
            return;
        }
        // Possible rubber band: starts on empty space or outside the name.
        if (!on_name) {
            A.drag = DRAG_BAND_PENDING;
            A.drag_pane = p;
            A.band_x0 = x;
            A.band_mx0 = x;
            A.band_my0 = y;
            A.band_y0 = (f64)(y - ly->list.y) + p->scroll;
            A.band_r0 = 0;
            A.band_r1 = -1;
            free(A.band_base);
            A.band_base = NULL;
            if (ctrl && p->L) {
                A.band_base = xmalloc(MAX(p->L->count, 1u));
                memcpy(A.band_base, p->sel, MAX(p->L->count, 1u));
            }
        }
        if (r < 0) {
            if (!ctrl && !shift) sel_clear(p);
            if (b->clicks == 2 && A.dblclick_up && !ctrl && !shift) { // double-click on empty space: parent folder
                drag_cancel();
                if (p->search_q[0]) search_exit(p);
                else pane_up(p);
            }
            return;
        }
        if (A.single_click && b->clicks >= 2) return; // the first click already opened it
        bool was_selected = p->sel[p->order[r]];
        if (ctrl) {
            sel_set(p, r, !p->sel[p->order[r]]);
            p->cursor = p->anchor = r;
        } else if (on_name && was_selected && !shift) {
            // keep a multi-selection for dragging; collapse to this row on mouse-up
            p->cursor = p->anchor = r;
            A.press_collapse = r;
        } else {
            move_cursor(p, r, shift, false);
        }
        if (on_name && p->sel[p->order[r]]) {
            A.drag = DRAG_ITEMS_PENDING;
            A.drag_pane = p;
            A.band_mx0 = x;
            A.band_my0 = y;
            if (A.single_click && !ctrl && !shift) A.click_open = r; // opens on release unless it becomes a drag
        }
    }
}

static void dialog_mouse(f32 x, f32 y) {
    Dialog *d = &A.dlg;
    for (int i = 0; i < d->nbuttons; i++)
        if (in_rect(d->rb[i], x, y)) { dialog_choose(i); return; }
    if (d->has_check && in_rect(d->rcheck, x, y)) d->check = !d->check;
    for (int i = 0; i < d->nopts; i++)
        if (in_rect(d->ropt[i], x, y)) d->opt = i;
    A.redraw = true;
}

static void on_mouse_down(SDL_MouseButtonEvent *b) {
    f32 x = b->x, y = b->y;
    A.redraw = true;
    if (A.dlg.open) {
        if (b->button == SDL_BUTTON_LEFT) dialog_mouse(x, y);
        return;
    }
    if (sheet_open()) { sheet_mouse(b); return; }
    if (A.qa_edit_g >= 0 && !in_rect(A.r_sidebar, x, y)) qa_edit_commit();
    if (A.pal_open) {
        if (in_rect(A.pal_list, x, y)) {
            int i = A.pal_top + (int)((y - A.pal_list.y) / roundf(S(40)));
            if (i < A.pal_n && b->button == SDL_BUTTON_LEFT) pal_run(i, SDL_GetModState() & SDL_KMOD_CTRL);
        } else if (!in_rect(A.pal_r, x, y)) {
            pal_close();
        }
        return;
    }
    if (A.menu.open) {
        if (in_rect(A.menu.r, x, y)) {
            int arrow = menu_arrow_at(y);
            if (arrow) menu_scroll_by((f32)arrow * A.menu.r.h * 0.5f);
            else if (b->button == SDL_BUTTON_LEFT) menu_activate(menu_item_at(x, y));
            return;
        }
        A.menu.open = false;
        if (b->button != SDL_BUTTON_RIGHT) return; // a right-click elsewhere opens a new menu
    }
    if (b->button == SDL_BUTTON_LEFT) {
        if (A.q_open && queue_mouse(x, y)) return;
        for (int i = 0; i < A.nops; i++) {
            if (A.op_retry[i].w > 0 && in_rect(A.op_retry[i], x, y)) { op_retry_admin(A.ops[i]); return; }
            if (A.op_close[i].w > 0 && in_rect(A.op_close[i], x, y)) { op_close_or_cancel(A.ops[i]); return; }
            if (A.op_pause[i].w > 0 && in_rect(A.op_pause[i], x, y)) { op_set_paused(A.ops[i], !atomic_load(&A.ops[i]->paused)); return; }
        }
        for (int i = 0; i < A.nops; i++)
            if (A.op_card[i].w > 0 && in_rect(A.op_card[i], x, y)) { queue_toggle(); A.q_sel = i; return; } // the card: the queue
    }
    if (A.ren_pane) {
        if (in_rect(A.ren_rect, x, y)) return;
        commit_rename();
    }
    if (A.edit_pane && !in_rect(A.edit_pane->ly.pathbar, x, y)) A.edit_pane = NULL;

    if (titlebar_mouse_down(b, x, y)) return;
    Node *dn = node_div_at(A.root, x, y);
    if (dn && b->button == SDL_BUTTON_LEFT) {
        A.drag = DRAG_SPLIT;
        A.drag_node = dn;
        return;
    }
    Panel *bpn = panel_at(x, y);
    if (bpn && in_rect(bpn->bar, x, y)) { on_mouse_down_tabbar(bpn, b, x, y); return; }
    if (bpn && bpn->cur && bpn->cur->term && in_rect(bpn->cur->pane[0].ly.area, x, y)) { // a terminal
        if (bpn != A.ap) panel_activate(bpn);
        Term *tt = bpn->cur->term;
        if (b->button == SDL_BUTTON_RIGHT && !term_wants_mouse(tt, SDL_GetModState())) {
            Menu *m = &A.menu;
            m->keep_open = false;
            m->reopen = NULL;
            m->n = 0;
            m->hover = -1;
            m->pane = &bpn->cur->pane[0];
            menu_add("Copy", "Ctrl+Shift+C", ACT_TERM_COPY, term_has_sel(tt));
            menu_add("Paste", "Ctrl+Shift+V", ACT_TERM_PASTE, true);
            menu_sep();
            menu_add("Clear scrollback", "", ACT_TERM_CLEAR, true);
            menu_finish(x, y);
            return;
        }
        term_mouse_down(tt, b->button, b->clicks, x, y, SDL_GetModState());
        A.term_mouse = tt;
        A.term_mouse_btn = b->button;
        return;
    }
    if (b->button == SDL_BUTTON_LEFT && picker_mouse(x, y)) return;
    if (in_rect(A.r_status, x, y) && b->button == SDL_BUTTON_LEFT) {
        if (in_rect(A.r_gear, x, y)) { options_open(); return; }
        if (in_rect(A.r_feat, x, y)) { menu_open_features(active_pane(), 0, 0); return; }
        if (A.r_xfer.w > 0 && in_rect(A.r_xfer, x, y)) { queue_toggle(); return; }
        static const int lvl_for[3] = {0, 1, 4};
        for (int i = 0; i < 3; i++)
            if (in_rect(A.r_viewbtn[i], x, y)) {
                Pane *ap2 = active_pane();
                set_zoom_level(ap2, i == 2 && ap2->view == VIEW_GRID ? zoom_level_of(ap2) : lvl_for[i]);
            }
        return;
    }
    if (in_rect(A.r_sdiv, x, y) && b->button == SDL_BUTTON_LEFT) { // the sidebar's edge: drag to resize, double-click resets
        if (b->clicks >= 2) { A.side_w = 216; A.settings_dirty = true; layout(); }
        else A.drag = DRAG_SIDEBAR;
        return;
    }
    if (A.r_pdiv.w > 0 && in_rect(A.r_pdiv, x, y) && b->button == SDL_BUTTON_LEFT) {
        A.drag = DRAG_PREVIEW;
        return;
    }
    if (A.preview_on && in_rect(A.r_preview, x, y)) {
        if (b->button == SDL_BUTTON_LEFT && b->clicks >= 2 && A.pv_want[0]) open_path(A.pv_want);
        return;
    }

    if (in_rect(A.r_sidebar, x, y)) {
        int idx = place_at(y);
        if (A.qa_edit_g >= 0) { // clicking outside the name being edited saves it
            bool on_edit = idx >= 0 && place_editing(&A.places[idx]);
            if (on_edit) return;
            qa_edit_commit();
            idx = place_at(y);
        }
        if (idx < 0) return;
        Place *pl = &A.places[idx];
        if (b->button == SDL_BUTTON_RIGHT) { menu_open_place(idx, x, y); return; }
        if (pl->kind == PK_NETEMPTY || pl->kind == PK_ADDONEMPTY || pl->kind == PK_CLOUDEMPTY) return;
        if (pl->kind == PK_CLOUDHDR && b->button == SDL_BUTTON_LEFT) {
            if (pl->eject.w > 0 && in_rect(pl->eject, x, y)) cloud_config_terminal();
            else { g_cloud_collapsed = !g_cloud_collapsed; init_places(); A.settings_dirty = true; }
            return;
        }
        if (pl->kind == PK_CLOUD && pl->item < g_ncloud) { // not connected: connect, then open; connected: a folder row
            if (b->button == SDL_BUTTON_LEFT && pl->removable && pl->eject.w > 0 && in_rect(pl->eject, x, y)) { cloud_disconnect(pl->item); return; }
            if (!pl->path[0]) {
                if (b->button == SDL_BUTTON_LEFT || b->button == SDL_BUTTON_MIDDLE) cloud_connect(pl->item, b->button == SDL_BUTTON_MIDDLE ? 2 : 1);
                return;
            }
        }
        if (pl->kind == PK_NETHDR && b->button == SDL_BUTTON_LEFT && pl->eject.w > 0 && in_rect(pl->eject, x, y)) {
            connect_open(-1);
            return;
        }
        if (pl->kind == PK_ADDONHDR) {
            if (b->button == SDL_BUTTON_LEFT) addon_header_click(pl, pl->eject.w > 0 && in_rect(pl->eject, x, y));
            return;
        }
        if (pl->kind == PK_ADDON) { addon_place_click(pl, b->button); return; }
        if (pl->kind == PK_DEVICES || pl->kind == PK_NETHDR) {
            if (b->button != SDL_BUTTON_LEFT) return;
            if (pl->kind == PK_DEVICES) A.devs_collapsed = !A.devs_collapsed;
            else A.nets_collapsed = !A.nets_collapsed;
            init_places();
            A.settings_dirty = true;
            return;
        }
        if (pl->kind == PK_GROUP) {
            if (b->button != SDL_BUTTON_LEFT) return;
            if (b->clicks >= 2) { qa_edit_begin(pl->group, -1); return; }
            A.qa[pl->group].collapsed = !A.qa[pl->group].collapsed;
            init_places();
            A.settings_dirty = true;
            return;
        }
        if (pl->kind == PK_NETSAVED) {
            if (b->button == SDL_BUTTON_LEFT) net_connect_saved(pl->item);
            return;
        }
        if (pl->kind == PK_UNMOUNTED || (pl->kind == PK_PHONE && !pl->path[0])) {
            if (b->button == SDL_BUTTON_LEFT || b->button == SDL_BUTTON_MIDDLE)
                place_dev_action(idx, b->button == SDL_BUTTON_MIDDLE ? ACT_DEV_OPEN_TAB : ACT_DEV_MOUNT);
            return;
        }
        if (b->button == SDL_BUTTON_LEFT && pl->removable && pl->eject.w > 0 && in_rect(pl->eject, x, y)) {
            place_dev_action(idx, pl->kind == PK_NETWORK || pl->kind == PK_PHONE ? ACT_NET_DISCONNECT : ACT_DEV_EJECT);
            return;
        }
        if (b->button == SDL_BUTTON_LEFT && pl->kind == PK_DRIVE && pl->usage.w > 0 && in_rect(pl->usage, x, y)) {
            du_open_tab(pl->path); // the usage bar / free space: what takes the space
            return;
        }
        if (b->button == SDL_BUTTON_MIDDLE) tab_new(pl->path, A.cur + 1, false);
        else if (b->button == SDL_BUTTON_LEFT) {
            open_folder(pl->path);
            if (pl->group >= 0) { // may become a drag to reorder
                A.drag = DRAG_PLACE_PENDING;
                A.qa_drag_row = idx;
                A.band_mx0 = x;
                A.band_my0 = y;
            }
        }
        return;
    }
    Pane *p = pane_at(x, y);
    if (p) on_mouse_down_pane(p, b, x, y);
}

static void on_mouse_up(void) {
    if (A.term_mouse) { term_mouse_up(A.term_mouse, A.term_mouse_btn); A.term_mouse = NULL; return; }
    if (A.drag == DRAG_TAB) { tab_drop_apply(); return; }
    if (A.drag == DRAG_PLACE || A.drag == DRAG_PLACE_PENDING) {
        int r = A.qa_drag_row;
        bool moved = A.drag == DRAG_PLACE;
        A.drag = DRAG_NONE;
        if (moved && r >= 0 && r < A.nplaces && A.places[r].group >= 0 && A.qa_ins_g >= 0)
            qa_move(A.places[r].group, A.places[r].item, A.qa_ins_g, A.qa_ins_i);
        A.qa_drag_row = -1;
        return;
    }
    if (A.drag == DRAG_COL_PENDING && A.drag_pane && pane_exists(A.drag_pane)) { // plain click on a header: sort
        Pane *sp = A.drag_pane;
        int key = g_coldef[A.drag_col].sort;
        drag_cancel();
        pane_set_sort(sp, key);
        return;
    }
    if (A.drag == DRAG_COL && A.col_insert >= 0) { // move the column
        int from = -1;
        for (int k = 0; k < A.ncols; k++)
            if (A.cols[k] == A.drag_col) from = k;
        int to = A.col_insert;
        if (from >= 0 && to != from && to != from + 1) {
            int c = A.cols[from];
            memmove(&A.cols[from], &A.cols[from + 1], (size_t)(A.ncols - from - 1) * sizeof(int));
            if (to > from) to--;
            memmove(&A.cols[to + 1], &A.cols[to], (size_t)(A.ncols - 1 - to) * sizeof(int));
            A.cols[to] = c;
            A.settings_dirty = true;
        }
        drag_cancel();
        return;
    }
    Pane *p = A.drag_pane;
    if (A.drag == DRAG_ITEMS && p && pane_exists(p) && (A.drop_split_panel || A.drop_newtab)) { // folders: open them
        Panel *pn = A.drop_split_panel ? A.drop_split_panel : A.drop_newtab;
        int n, zone = A.drop_split_panel ? A.drop_split : -1;
        char **paths = pane_selection_paths(p, &n);
        drag_cancel();
        open_dirs_at(pn, zone, paths, n);
        strv_free(paths, n);
        return;
    }
    if (A.drag == DRAG_ITEMS && p && pane_exists(p) && A.drop_qa >= 0 && A.drop_qa < A.nqa) { // onto a group header
        int n, g = A.drop_qa;
        char **paths = pane_selection_paths(p, &n);
        drag_cancel();
        qa_pin_paths(paths, n, g);
        strv_free(paths, n);
        return;
    }
    if (A.drag == DRAG_ITEMS && p && pane_exists(p) && A.drop_dir[0] && pane_is_archive(p)) {
        int n;
        char **members = arc_selection(p, &n);
        char dst[PATH_MAX];
        snprintf(dst, sizeof dst, "%s", A.drop_dir);
        drag_cancel();
        arc_extract_to(p, members, n, dst);
        strv_free(members, n);
        return;
    }
    if (A.drag == DRAG_ITEMS && p && pane_exists(p) && A.drop_dir[0]) {
        int n;
        char **paths = pane_selection_paths(p, &n);
        char dst[PATH_MAX];
        snprintf(dst, sizeof dst, "%s", A.drop_dir);
        bool copy = A.drop_copy;
        drag_cancel();
        start_transfer(paths, n, dst, !copy, false);
        strv_free(paths, n);
        return;
    }
    if (A.drag == DRAG_ITEMS_PENDING && p && A.press_collapse >= 0 && pane_exists(p) && p->order &&
        A.press_collapse < (i64)p->count)
        move_cursor(p, A.press_collapse, false, false);
    i64 open_row = A.drag == DRAG_ITEMS_PENDING ? A.click_open : -1;
    A.click_open = -1;
    drag_cancel();
    if (open_row >= 0 && p && pane_exists(p) && p->order && open_row < (i64)p->count) open_entry(p, open_row, false);
}

static void on_mouse_motion(SDL_MouseMotionEvent *m) {
    A.mx = m->x;
    A.my = m->y;
    A.mouse_in = true;
    A.redraw = true;
    if (A.term_mouse) { term_mouse_move(A.term_mouse, m->x, m->y, true); return; }
    if (A.menu.open) {
        int i = menu_item_at(m->x, m->y);
        if (i >= 0 || in_rect(A.menu.r, m->x, m->y)) A.menu.hover = i;
    }
    if (A.pal_open && in_rect(A.pal_list, m->x, m->y)) {
        int i = A.pal_top + (int)((m->y - A.pal_list.y) / roundf(S(40)));
        if (i < A.pal_n) A.pal_sel = i;
    }
    Pane *p = A.drag_pane;
    if (A.drag == DRAG_SCROLL && p && p->ly.has_scroll) {
        f32 range = p->ly.list.h - p->ly.thumb.h;
        f64 t = range > 0 ? (m->y - A.drag_off - p->ly.list.y) / range : 0;
        p->scroll = p->scroll_target = CLAMP(t, 0.0, 1.0) * max_scroll(p);
    } else if (A.drag == DRAG_BAND_PENDING && p) {
        if (fabsf(m->x - A.band_mx0) + fabsf(m->y - A.band_my0) > S(8)) {
            A.drag = DRAG_BAND;
            if (!A.band_base) sel_clear(p);
            else { // restore pre-click selection, the click's toggle shouldn't stick
                memcpy(p->sel, A.band_base, MAX(p->L->count, 1u));
                u32 n = 0;
                for (u32 i = 0; i < p->L->count; i++) n += p->sel[i];
                p->sel_count = n;
            }
            band_update(p);
        }
    } else if (A.drag == DRAG_BAND && p) {
        band_update(p);
    } else if (A.drag == DRAG_ITEMS_PENDING && p) {
        if (fabsf(m->x - A.band_mx0) + fabsf(m->y - A.band_my0) > S(12) && p->sel_count) {
            A.drag = DRAG_ITEMS;
            A.press_collapse = -1;
            drop_target(p, m->x, m->y);
        }
    } else if (A.drag == DRAG_ITEMS && p) {
        drop_target(p, m->x, m->y);
    } else if (A.drag == DRAG_PLACE_PENDING || A.drag == DRAG_PLACE) {
        if (A.drag == DRAG_PLACE_PENDING && fabsf(m->y - A.band_my0) > S(6)) A.drag = DRAG_PLACE;
        if (A.drag == DRAG_PLACE) qa_drop_point(m->y, &A.qa_ins_g, &A.qa_ins_i);
    } else if (A.drag == DRAG_SIDEBAR) {
        A.side_w = CLAMP(m->x / A.scale, 150.f, MAX(150.f, MIN(480.f, ((f32)A.W / A.scale) - 520.f)));
        A.settings_dirty = true;
        layout();
    } else if (A.drag == DRAG_PREVIEW) {
        f32 max_w = MAX(220.f, ((f32)A.W - A.r_sidebar.w - S(380)) / A.scale);
        A.preview_w = CLAMP(((f32)A.W - m->x) / A.scale, 220.f, MIN(1200.f, max_w));
        A.settings_dirty = true;
    } else if (A.drag == DRAG_COL_RESIZE) {
        A.colw[A.drag_col] = CLAMP(A.drag_col_w0 + A.drag_off * (m->x - A.band_mx0) / A.scale, 40.f, 600.f);
        A.settings_dirty = true;
    } else if (A.drag == DRAG_COL_PENDING || A.drag == DRAG_COL) {
        if (A.drag == DRAG_COL_PENDING && fabsf(m->x - A.band_mx0) > S(8)) A.drag = DRAG_COL;
        if (A.drag == DRAG_COL && p) { // insertion index in A.cols from the visible column under the cursor
            PaneLayout *ly = &p->ly;
            A.col_insert = A.ncols;
            for (int i = 0; i < ly->ncols; i++) {
                if (m->x < (ly->cx0[i] + ly->cx1[i]) * 0.5f) {
                    for (int k = 0; k < A.ncols; k++)
                        if (A.cols[k] == ly->cid[i]) A.col_insert = k;
                    break;
                }
            }
        }
    } else if (A.drag == DRAG_TAB_PENDING && A.drag_tab) {
        if (fabsf(m->x - A.band_mx0) + fabsf(m->y - A.band_my0) > S(8)) {
            A.drag = DRAG_TAB;
            tab_drop_update(m->x, m->y);
        }
    } else if (A.drag == DRAG_TAB) {
        tab_drop_update(m->x, m->y);
    } else if (A.drag == DRAG_SPLIT && A.drag_node) {
        Node *n = A.drag_node;
        if (n->kind == NODE_HSPLIT) n->ratio = CLAMP((m->x - n->r.x) / MAX(n->r.w, 1.f), 0.12f, 0.88f);
        else n->ratio = CLAMP((m->y - n->r.y) / MAX(n->r.h, 1.f), 0.15f, 0.85f);
        A.settings_dirty = true;
    }
    // resize cursors over dividers
    Node *hn = A.drag == DRAG_SPLIT ? A.drag_node : (A.drag == DRAG_NONE ? node_div_at(A.root, m->x, m->y) : NULL);
    Pane *hp = A.drag == DRAG_NONE ? pane_at(m->x, m->y) : NULL;
    bool col_edge = A.drag == DRAG_COL_RESIZE || (hp && in_rect(hp->ly.header, m->x, m->y) && header_edge_at(&hp->ly, m->x) >= 0);
    int want = col_edge ? 1 : hn ? (hn->kind == NODE_HSPLIT ? 1 : 2)
                  : (A.drag == DRAG_PREVIEW || A.drag == DRAG_SIDEBAR || (A.drag == DRAG_NONE && A.r_pdiv.w > 0 && in_rect(A.r_pdiv, m->x, m->y)) ||
                     (A.drag == DRAG_NONE && in_rect(A.r_sdiv, m->x, m->y))) ? 1 : 0;
    if (want != A.cursor_ew) {
        A.cursor_ew = want;
        SDL_SetCursor(want == 1 ? A.cur_ew : want == 2 ? A.cur_ns : A.cur_default);
    }
}

static void on_wheel(SDL_MouseWheelEvent *w) {
    if (sheet_open()) { sheet_wheel(w->y); return; }
    if (A.menu.open) { // scroll a tall menu; don't scroll what's underneath
        if (in_rect(A.menu.r, w->mouse_x, w->mouse_y)) menu_scroll_by(-w->y * S(28) * 2);
        return;
    }
    {
        Panel *wp = panel_at(w->mouse_x, w->mouse_y);
        if (wp && wp->cur && wp->cur->term && in_rect(wp->cur->pane[0].ly.area, w->mouse_x, w->mouse_y)) {
            term_wheel(wp->cur->term, w->y);
            return;
        }
    }
    {
        Pane *dp = pane_at(w->mouse_x, w->mouse_y);
        if (dp && pane_snap_on(dp) && in_rect(dp->ly.list, w->mouse_x, w->mouse_y)) { snap_wheel(dp, w->y); return; }
        if (dp && pane_du_on(dp) && in_rect(dp->ly.list, w->mouse_x, w->mouse_y)) { // the map doesn't scroll or zoom
            if (!(SDL_GetModState() & SDL_KMOD_CTRL)) du_wheel(dp, w->mouse_x, w->mouse_y, w->y);
            return;
        }
    }
    if (SDL_GetModState() & SDL_KMOD_CTRL) { // like File Pilot: Ctrl+wheel changes the view / icon size
        Pane *zp = pane_at(w->mouse_x, w->mouse_y);
        if (zp) zoom_step(zp, w->y > 0 ? 1 : -1);
        else set_zoom(A.zoom + (w->y > 0 ? 0.1f : -0.1f));
        return;
    }
    if (A.pal_open) {
        pal_move(w->y < 0 ? 3 : -3);
        return;
    }
    if (in_rect(A.r_sidebar, w->mouse_x, w->mouse_y)) {
        A.side_scroll_target = CLAMP(A.side_scroll_target - w->y * S(60), 0.f, MAX(0.f, A.side_content_h - A.r_sidebar.h));
        A.redraw = true;
        return;
    }
    Pane *p = pane_at(w->mouse_x, w->mouse_y);
    if (!p) return;
    p->scroll_target -= (f64)w->y * (p->ly.per_row > 1 ? p->ly.cell_h * 0.75 : p->ly.row_h * 3.0);
    p->scroll_target = CLAMP(p->scroll_target, 0.0, max_scroll(p));
    A.redraw = true;
}

static void reload_fonts(void) {
    font_set_px(&A.font, (int)roundf((f32)A.font_size * A.scale));
    font_set_px(&A.font_small, (int)roundf((f32)(A.font_size - 1) * A.scale));
    if (A.mono_path) font_set_px(&A.font_mono, (int)roundf((f32)(A.font_size - 1) * A.scale));
    font_set_px(&A.font_brand, (int)roundf((f32)(A.font_size + 3) * A.scale));
    atlas_reset();
    A.redraw = true;
}

static void rec_event(const char *what, f32 x, f32 y, int clicks) {
    if (!A.rec_log) {
        char path[PATH_MAX];
        mkdir_p(A.rec_dir, 0755);
        snprintf(path, sizeof path, "%s/log.txt", A.rec_dir);
        A.rec_log = fopen(path, "w");
        if (!A.rec_log) { A.rec_dir = NULL; return; }
    }
    fprintf(A.rec_log, "%.3f EVENT %s x=%.0f y=%.0f clicks=%d drag=%d\n", now_ns() / 1e9, what, x, y, clicks, A.drag);
    fflush(A.rec_log);
    A.rec_until = now_ns() + 700000000ull;
}

// Log + half-size snapshot of the frame about to be presented.
static void rec_frame(void) {
    if (!A.rec_dir || !A.rec_log || now_ns() > A.rec_until || A.rec_n >= 600) return;
    Pane *p = active_pane();
    fprintf(A.rec_log, "%.3f FRAME %04d drawn=%d count=%u L=%p Lnext=%p sortpend=%d drag=%d cursor=%lld sel=%u scroll=%.1f "
            "atlas=%d full=%d menu=%d sheet=%d toast=%d\n",
            now_ns() / 1e9, A.rec_n, p->ly.drawn, p->count, (void *)p->L, (void *)p->L_next, p->sort_pending, A.drag,
            (long long)p->cursor, p->sel_count, p->scroll, g_atlas_size, g_atlas_full, A.menu.open, sheet_open(),
            A.toast[0] && now_ns() - A.toast_t < 2500000000ull);
    fflush(A.rec_log);
    SDL_Surface *s = SDL_RenderReadPixels(A.ren, NULL);
    if (!s) return;
    SDL_Surface *h = SDL_CreateSurface(s->w / 2, s->h / 2, s->format);
    if (h) {
        SDL_BlitSurfaceScaled(s, NULL, h, NULL, SDL_SCALEMODE_LINEAR);
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/f%04d.bmp", A.rec_dir, A.rec_n);
        SDL_SaveBMP(h, path);
        SDL_DestroySurface(h);
    }
    SDL_DestroySurface(s);
    A.rec_n++;
}

// --- input methods (fcitx, ibus: Chinese, Japanese, Korean...) ----------------------------------------------------
// Text input stays on (typing in the list jumps to a file), so the input method is told where the focused field is
// (its candidate window opens there) and the text being composed is shown under it until it's committed.
static char g_preedit[256];
static SDL_Rect g_ime_area = {-1, -1, -1, -1};

static Rect ime_field(void) {
    if (A.pal_open) return (Rect){A.pal_r.x, A.pal_r.y, A.pal_r.w, roundf(S(44))};
    if (A.search_pane) { Rect f, t, c; search_bar_rects(A.search_pane, &f, &t, &c); return f; }
    if (A.ren_pane) return A.ren_rect;
    if (A.edit_pane) return A.edit_pane->ly.pathbar;
    Pane *p = active_pane();
    if (p) return (Rect){p->ly.list.x + S(8), p->ly.list.y, p->ly.list.w * 0.5f, roundf(S(28))}; // type-to-jump
    return (Rect){0, 0, (f32)A.W, roundf(S(38))};
}

static void ime_update_area(void) { // (every frame; tells the input method only when it moved)
    if (!A.win || A.selftest) return;
    Rect r = ime_field();
    f32 d = SDL_GetWindowPixelDensity(A.win);
    SDL_Rect a = {(int)(r.x / d), (int)(r.y / d), (int)(r.w / d), (int)(r.h / d)};
    if (!memcmp(&a, &g_ime_area, sizeof a)) return;
    g_ime_area = a;
    SDL_SetTextInputArea(A.win, &a, 0);
}

static void on_text_editing(const char *text) {
    snprintf(g_preedit, sizeof g_preedit, "%s", text ? text : "");
    A.redraw = true;
}

static void window_close_requested(void);
static void handle_event(SDL_Event *e) {
    SDL_ConvertEventToRenderCoordinates(A.ren, e);
    layout();
    switch (e->type) {
    case SDL_EVENT_QUIT: A.quit = true; break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: window_close_requested(); break;
    case SDL_EVENT_KEY_DOWN: on_key(&e->key); break;
    case SDL_EVENT_TEXT_INPUT: g_preedit[0] = 0; on_text(e->text.text); break;
    case SDL_EVENT_TEXT_EDITING: on_text_editing(e->edit.text); break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (A.rec_dir) rec_event("down", e->button.x, e->button.y, e->button.clicks);
        on_mouse_down(&e->button);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (A.rec_dir) rec_event("up", e->button.x, e->button.y, e->button.clicks);
        if (e->button.button == SDL_BUTTON_LEFT) on_mouse_up();
        A.redraw = true;
        break;
    case SDL_EVENT_DROP_BEGIN:
        strv_free(A.ext_drop, A.next_drop);
        A.ext_drop = NULL;
        A.next_drop = 0;
        break;
    case SDL_EVENT_DROP_FILE:
        if (e->drop.data) {
            A.ext_drop = xrealloc(A.ext_drop, (size_t)(A.next_drop + 1) * sizeof(char *));
            A.ext_drop[A.next_drop++] = strdup(e->drop.data);
        }
        A.ext_x = e->drop.x;
        A.ext_y = e->drop.y;
        break;
    case SDL_EVENT_DROP_POSITION:
        A.ext_x = e->drop.x;
        A.ext_y = e->drop.y;
        break;
    case SDL_EVENT_DROP_COMPLETE:
        if (A.next_drop) drop_target(NULL, A.ext_x, A.ext_y);
        if (A.next_drop && A.drop_qa >= 0 && A.drop_qa < A.nqa) {
            qa_pin_paths(A.ext_drop, A.next_drop, A.drop_qa);
        } else if (A.next_drop) {
            char dst[PATH_MAX];
            snprintf(dst, sizeof dst, "%s", A.drop_dir[0] ? A.drop_dir : active_pane()->path);
            start_transfer(A.ext_drop, A.next_drop, dst, SDL_GetModState() & SDL_KMOD_SHIFT, false);
        }
        strv_free(A.ext_drop, A.next_drop);
        A.ext_drop = NULL;
        A.next_drop = 0;
        A.drop_dir[0] = 0;
        A.drop_pane = NULL;
        A.drop_row = -1;
        A.drop_place = -1;
        A.redraw = true;
        break;
    case SDL_EVENT_CLIPBOARD_UPDATE:
        if (!e->clipboard.owner && g_clip.n) clip_free_data(); // another app took the clipboard
        A.redraw = true;
        break;
    case SDL_EVENT_MOUSE_MOTION: on_mouse_motion(&e->motion); break;
    case SDL_EVENT_MOUSE_WHEEL: on_wheel(&e->wheel); break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: A.mouse_in = false; A.redraw = true; break;
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_OCCLUDED:
        A.win_hidden = !A.screenshot; // screenshots render regardless (vsync is off for them)
        break;
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        trash_size_refresh(); // other apps may have used the trash meanwhile
        if (e->type == SDL_EVENT_WINDOW_FOCUS_GAINED) git_refresh_all(); // files may have been edited / committed elsewhere
        A.win_hidden = false;
        A.redraw = true;
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        SDL_GetWindowSizeInPixels(A.win, &A.W, &A.H);
        A.redraw = true;
        break;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
        f32 s = SDL_GetWindowDisplayScale(A.win);
        if (s > 0 && s != A.dpi_scale) {
            A.dpi_scale = s;
            A.scale = s * A.zoom;
            reload_fonts();
        }
        A.redraw = true;
        break;
    }
    default: A.redraw = true; break;
    }
}
