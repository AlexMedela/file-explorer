// selftest.c - headless self-tests (--selftest, --selftest-ops, --selftest-m4 ... m11): drive the real input
// handlers and frames in sandboxed /tmp folders with private config dirs (included from app.c)

// --- self-test: drives real input handlers + frames (run under ASan) ------

static int st_fail;
#define ST_CHECK(c) do { if (!(c)) { fprintf(stderr, "SELFTEST FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); st_fail++; } } while (0)

static void st_frames(int n) {
    for (int k = 0; k < n; k++) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) // keep real window state (size/scale), ignore real input
            if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST) handle_event(&e);
        main_drain();
        watch_sync();
        watch_process();
        term_tick();
        addon_tick();
        cloud_tick();
        git_tick();
        du_tick();
        cmp_tick();
        snap_tick();
        borg_tick();
        layout();
        for (int i = 0; i < A.ntabs; i++)
            for (int j = 0; j < A.tabs[i]->npanes; j++) pane_poll(&A.tabs[i]->pane[j]);
        frame();
    }
}

static void st_wait(void) {
    for (int k = 0; k < 500; k++) {
        bool busy = false;
        for (int i = 0; i < A.ntabs; i++)
            for (int j = 0; j < A.tabs[i]->npanes; j++) {
                Pane *p = &A.tabs[i]->pane[j];
                if ((p->L && atomic_load(&p->L->state) < LST_COMPLETE) || p->sort_pending || !p->order || p->L_next) busy = true;
                if (p->L && atomic_load(&p->L->state) == LST_ERROR) busy = false;
            }
        st_frames(1);
        if (!busy) { st_frames(1); return; }
        SDL_Delay(5);
    }
    fprintf(stderr, "SELFTEST: timeout waiting for load\n");
    st_fail++;
}

static void st_settle(void) {
    for (int k = 0; k < 300 && animating(); k++) st_frames(1);
}

static void st_key(SDL_Keycode k, SDL_Keymod mod) {
    SDL_KeyboardEvent e;
    SDL_zero(e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key = k;
    e.mod = mod;
    SDL_SetModState(mod);
    layout();
    on_key(&e);
    SDL_SetModState(SDL_KMOD_NONE);
    st_frames(1);
    st_settle();
}

static void st_mouse(int type, f32 x, f32 y, int button, int clicks, SDL_Keymod mod) {
    SDL_SetModState(mod);
    layout();
    if (type == SDL_EVENT_MOUSE_MOTION) {
        SDL_MouseMotionEvent m;
        SDL_zero(m);
        m.x = x; m.y = y;
        on_mouse_motion(&m);
    } else if (type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        SDL_MouseButtonEvent b;
        SDL_zero(b);
        b.button = (Uint8)button; b.clicks = (Uint8)clicks; b.x = x; b.y = y;
        on_mouse_down(&b);
    } else {
        on_mouse_up();
    }
    SDL_SetModState(SDL_KMOD_NONE);
    st_frames(1);
}

static int run_selftest(void) {
    st_wait();
    Pane *p = active_pane();
    ST_CHECK(p->order && p->count > 1000);
    u32 total = p->count;

    // tabs
    st_key(SDLK_T, SDL_KMOD_CTRL); st_wait();
    ST_CHECK(A.ntabs == 2 && A.cur == 1);
    // split
    st_key(SDLK_BACKSLASH, SDL_KMOD_CTRL); st_wait();
    ST_CHECK(A.npanels == 2 && A.ap == A.panels[1]);
    Panel *right = A.ap;
    st_key(SDLK_TAB, SDL_KMOD_NONE);
    ST_CHECK(A.ap == A.panels[0]);
    p = active_pane();
    ST_CHECK(p->count == total);

    { // the wheel: a quick run of notches goes further; a touchpad moves the view at once and carries on when it lets go
        SDL_MouseWheelEvent wv;
        SDL_zero(wv);
        wv.mouse_x = p->ly.list.x + S(40), wv.mouse_y = p->ly.list.y + S(40), wv.y = -1;
        on_wheel(&wv);
        f64 step = p->scroll_target;
        ST_CHECK(step > 0 && p->scroll < step); // (eases there)
        for (int k = 0; k < 9; k++) on_wheel(&wv);
        ST_CHECK(p->scroll_target > step * 12);
        st_settle();
        ST_CHECK(p->scroll == p->scroll_target);
        f64 at = p->scroll;
        wv.y = -0.37f; // (a touchpad: any fraction)
        on_wheel(&wv);
        ST_CHECK(p->scroll == p->scroll_target && p->scroll > at);
        for (int k = 0; k < 8; k++) { SDL_Delay(5); on_wheel(&wv); }
        f64 lifted = p->scroll;
        SDL_Delay(60);
        st_frames(3);
        ST_CHECK(g_wf.coast && p->scroll > lifted && p->scroll == p->scroll_target);
        wv.y = 1; // the wheel (or a click, a key) stops it
        on_wheel(&wv);
        ST_CHECK(!g_wf.coast);
        st_settle();
        p->scroll = p->scroll_target = 0;
    }

    // rubber band from the Size column, 10 rows, then autoscroll below the list
    PaneLayout *ly = &p->ly;
    f32 sx0 = 0;
    col_span(ly, COL_SIZE, &sx0, NULL);
    f32 bx = sx0 + S(10), by = ly->list.y + ly->row_h * 0.5f;
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, bx, by, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_MOTION, bx - S(40), by + ly->row_h * 9, 0, 0, 0);
    ST_CHECK(A.drag == DRAG_BAND);
    ST_CHECK(p->sel_count == 10);
    st_mouse(SDL_EVENT_MOUSE_MOTION, bx - S(40), ly->list.y + ly->list.h + S(80), 0, 0, 0);
    st_frames(30);
    u32 after_auto = p->sel_count;
    ST_CHECK(after_auto > 15);
    ST_CHECK(p->scroll > 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(A.drag == DRAG_NONE && p->sel_count == after_auto);

    // Ctrl+band adds to existing selection
    st_key(SDLK_HOME, SDL_KMOD_NONE);
    ST_CHECK(p->sel_count == 1);
    by = ly->list.y + ly->row_h * 3.5f;
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, bx, by, SDL_BUTTON_LEFT, 1, SDL_KMOD_CTRL);
    st_mouse(SDL_EVENT_MOUSE_MOTION, bx, by + ly->row_h * 2, 0, 0, SDL_KMOD_CTRL);
    ST_CHECK(p->sel_count == 4); // row 0 + rows 3..5
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);

    // click on name selects one row; shift-click extends
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, ly->col_name + S(30), ly->list.y + ly->row_h * 1.5f, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(p->sel_count == 1 && p->cursor == 1);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, ly->col_name + S(30), ly->list.y + ly->row_h * 6.5f, SDL_BUTTON_LEFT, 1, SDL_KMOD_SHIFT);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(p->sel_count == 6);

    // sort by size via header, then hidden toggle
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, sx0 + S(20), ly->header.y + S(10), SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    st_wait();
    ST_CHECK(p->sort_key == SORT_SIZE && p->sort_desc && p->count == total);
    st_key(SDLK_H, SDL_KMOD_CTRL); st_wait();
    st_key(SDLK_H, SDL_KMOD_CTRL); st_wait();
    ST_CHECK(p->count == total);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, ly->col_name + S(10), ly->header.y + S(10), SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    st_wait();
    ST_CHECK(p->sort_key == SORT_NAME);

    // enter a folder, go up (child reselected), history back/forward
    char parent[PATH_MAX];
    snprintf(parent, sizeof parent, "%s", p->path);
    st_key(SDLK_HOME, SDL_KMOD_NONE);
    ST_CHECK(p->L->entries[p->order[0]].flags & ENT_DIR);
    char child[NAME_MAX + 1];
    snprintf(child, sizeof child, "%s", p->L->entries[p->order[0]].name);
    st_key(SDLK_RETURN, SDL_KMOD_NONE); st_wait();
    ST_CHECK(strcmp(p->path, parent) != 0);
    st_key(SDLK_BACKSPACE, SDL_KMOD_NONE); st_wait();
    ST_CHECK(!strcmp(p->path, parent));
    ST_CHECK(p->cursor >= 0 && !strcmp(p->L->entries[p->order[p->cursor]].name, child));
    st_key(SDLK_LEFT, SDL_KMOD_ALT); st_wait();
    ST_CHECK(strcmp(p->path, parent) != 0);
    st_key(SDLK_RIGHT, SDL_KMOD_ALT); st_wait();
    ST_CHECK(!strcmp(p->path, parent));

    // drag the split divider
    Node *dn = A.root;
    ST_CHECK(dn->kind == NODE_HSPLIT);
    f32 before = dn->ratio;
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, dn->div.x + dn->div.w * 0.5f, A.r_content.y + S(200), SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_MOTION, A.r_content.x + A.r_content.w * 0.3f, A.r_content.y + S(200), 0, 0, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(fabsf(dn->ratio - 0.3f) < 0.02f && dn->ratio != before);

    // close the right panel (its only tab) while a sort is in flight
    st_key(SDLK_TAB, SDL_KMOD_NONE);
    ST_CHECK(A.ap == right);
    pane_set_sort(active_pane(), SORT_DATE);
    st_key(SDLK_W, SDL_KMOD_CTRL); st_wait();
    ST_CHECK(A.npanels == 1 && A.root->kind == NODE_PANEL && A.ap == A.panels[0]);

    // middle-click a folder -> background tab; Ctrl+9 -> last tab; close it
    p = active_pane();
    ly = &p->ly;
    pane_set_sort(p, SORT_NAME); st_wait();
    int ntabs = A.ntabs;
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, ly->col_name + S(30), ly->list.y + ly->row_h * 0.5f, SDL_BUTTON_MIDDLE, 1, 0);
    st_wait();
    ST_CHECK(A.ntabs == ntabs + 1 && A.cur == 1);
    st_key(SDLK_9, SDL_KMOD_CTRL);
    ST_CHECK(A.cur == A.ntabs - 1);
    st_key(SDLK_W, SDL_KMOD_CTRL);
    ST_CHECK(A.ntabs == ntabs);

    // path editing
    st_key(SDLK_L, SDL_KMOD_CTRL);
    ST_CHECK(A.edit_pane == active_pane() && A.edit.anchor == 0 && A.edit.caret == (int)strlen(A.edit.buf));
    on_text("/usr/share");
    st_key(SDLK_RETURN, SDL_KMOD_NONE); st_wait();
    ST_CHECK(!strcmp(active_pane()->path, "/usr/share"));
    on_text("/");
    ST_CHECK(A.edit_pane && !strcmp(A.edit.buf, "/"));
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    ST_CHECK(!A.edit_pane);

    // tab bar clicks: + button, close button, select
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, A.ap->plus.x + 5, A.ap->plus.y + 5, SDL_BUTTON_LEFT, 1, 0); st_wait();
    ST_CHECK(A.ntabs == ntabs + 1 && A.cur == A.ntabs - 1);
    Rect r = A.tabs[0]->r;
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, r.x + S(30), r.y + r.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(A.cur == 0);
    r = A.tabs[0]->r;
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, r.x + r.w - S(17), r.y + r.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(A.ntabs == ntabs);

    // stress: rapid navigation + tab churn while 100k-entry scans are in flight
    char start[PATH_MAX];
    snprintf(start, sizeof start, "%s", parent);
    for (int k = 0; k < 20; k++) {
        tab_new(start, A.ntabs, true);
        pane_navigate(active_pane(), "/usr/bin", true);
        pane_set_sort(active_pane(), SORT_SIZE);
        if (k % 3 == 0) panel_split(A.ap, k % 2 ? ZONE_RIGHT : ZONE_BOTTOM, NULL, "/usr/bin");
        st_frames(1);
        tab_close(A.cur);
    }
    st_wait();
    while (A.ntabs > 1) tab_close(A.ntabs - 1);
    st_frames(2);
    ST_CHECK(!A.quit);
    st_key(SDLK_W, SDL_KMOD_CTRL);
    ST_CHECK(A.quit); // closing the last tab quits

    if (st_fail) { fprintf(stderr, "SELFTEST: %d failure(s)\n", st_fail); return 1; }
    printf("SELFTEST OK\n");
    fflush(stdout);
    return 0;
}

// --- self-test for file operations (sandboxed in /tmp, own XDG trash) ------

#define ST_BASE "/tmp/fe-ops-test"
#define ST_XDEV "/dev/shm/fe-ops-xdev"

static void st_write(const char *path, size_t size) {
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return;
    char buf[65536];
    for (size_t i = 0; i < sizeof buf; i++) buf[i] = (char)(i * 31);
    for (size_t done = 0; done < size;) {
        size_t k = MIN(sizeof buf, size - done);
        if (write(fd, buf, k) != (ssize_t)k) break;
        done += k;
    }
    close(fd);
}

static bool st_exists(const char *path) { struct stat st; return lstat(path, &st) == 0; }
static i64 st_size(const char *path) { struct stat st; return lstat(path, &st) == 0 ? (i64)st.st_size : -1; }

static void st_rmrf(const char *path) {
    Op tmp;
    memset(&tmp, 0, sizeof tmp);
    pthread_mutex_init(&tmp.mtx, NULL);
    delete_tree(&tmp, AT_FDCWD, path);
    pthread_mutex_destroy(&tmp.mtx);
}

static void st_setup_ops(void) {
    st_rmrf(ST_BASE);
    st_rmrf(ST_XDEV);
    mkdir(ST_BASE, 0755);
    mkdir(ST_BASE "/sub", 0755);
    mkdir(ST_BASE "/dst", 0755);
    mkdir(ST_BASE "/xdg", 0755);
    st_write(ST_BASE "/a.txt", 1 << 20);
    st_write(ST_BASE "/b.txt", 10);
    st_write(ST_BASE "/sub/c.txt", 1234);
    setenv("XDG_DATA_HOME", ST_BASE "/xdg", 1);
}

static void st_wait_ops(void) {
    for (int k = 0; k < 2000; k++) {
        bool busy = false;
        for (int i = 0; i < A.nops; i++)
            if (!A.ops[i]->finished) busy = true;
        if (!busy) break;
        st_frames(1);
        SDL_Delay(2);
    }
    st_wait();
}

static bool st_select(Pane *p, const char *name) {
    // let pending folder-change refreshes land first (a late one would restore the old cursor)
    for (int k = 0; k < 200 && watch_pending(); k++) { st_frames(1); SDL_Delay(2); }
    st_wait();
    for (u32 i = 0; i < p->count; i++)
        if (!strcmp(p->L->entries[p->order[i]].name, name)) {
            move_cursor(p, i, false, false);
            st_settle();
            st_frames(1);
            return true;
        }
    fprintf(stderr, "SELFTEST: no entry %s in %s\n", name, p->path);
    st_fail++;
    return false;
}

static const char *st_cursor_name(Pane *p) {
    return (p->order && p->cursor >= 0) ? p->L->entries[p->order[p->cursor]].name : "";
}

static const void *st_img_clip_cb(void *ud, const char *mime, size_t *size) {
    (void)ud;
    *size = mime && !strcmp(mime, "image/png") ? 8 : 0;
    return *size ? "\x89PNG\r\n\x1A\n" : NULL;
}

static f32 st_row_y(Pane *p, const char *name) {
    for (u32 i = 0; i < p->count; i++)
        if (!strcmp(p->L->entries[p->order[i]].name, name))
            return (f32)(p->ly.list.y + ((f64)i + 0.5) * p->ly.row_h - p->scroll);
    return -1;
}

// Centre pixel of the brand icon slot: true when the pure-green test icon is being drawn there.
static bool st_brand_icon_green(void) {
    Rect r = A.r_brand;
    if (r.h <= 0) return false;
    f32 lh = roundf(S(17)), lw = roundf(lh * 624.f / 456.f);
    f32 x = r.x + S(18) + lw * 0.5f;
    f32 y = roundf(r.y + (r.h - lh) * 0.5f - (lw - lh) * 0.5f) + lw * 0.5f;
    int sx = (int)roundf(x) + A.shadow_px, sy = (int)roundf(y) + A.shadow_px;
    SDL_Texture *target = SDL_GetRenderTarget(A.ren);
    SDL_SetRenderTarget(A.ren, NULL);
    SDL_Surface *s = SDL_RenderReadPixels(A.ren, NULL);
    SDL_SetRenderTarget(A.ren, target);
    if (!s) return false;
    SDL_Surface *c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA8888);
    SDL_DestroySurface(s);
    if (!c || sx < 0 || sy < 0 || sx >= c->w || sy >= c->h) { if (c) SDL_DestroySurface(c); return false; }
    const u8 *px = (const u8 *)c->pixels + (size_t)sy * c->pitch + (size_t)sx * 4;
    u8 rr, gg, bb, aa;
    SDL_GetRGBA(*(const u32 *)px, SDL_GetPixelFormatDetails(c->format), SDL_GetSurfacePalette(c), &rr, &gg, &bb, &aa);
    bool green = rr < 80 && gg > 180 && bb < 80 && aa > 0;
    SDL_DestroySurface(c);
    return green;
}

static void st_shot(const char *name) {
    const char *dir = getenv("FE_SHOTS");
    if (!dir) return;
    st_frames(2);
    snprintf(A.shot_once, sizeof A.shot_once, "%s/%s.bmp", dir, name);
    st_frames(1);
}

static Op *st_last_op(void) { return A.nops ? A.ops[A.nops - 1] : NULL; }

// Until op has finished, without frames (they'd free a finished op whose card isn't shown): results land and the
// queue gives turns.
static void st_wait_op_done(Op *op) {
    for (int k = 0; k < 10000 && !op->finished; k++) { main_drain(); ops_schedule(); SDL_Delay(1); }
    ST_CHECK(op->finished);
}

static void st_wait_dlg(void) {
    for (int k = 0; k < 1000 && !A.dlg.open; k++) { st_frames(1); SDL_Delay(2); }
}

static void st_set_mtime(const char *path, time_t t) {
    struct timespec ts[2] = {{t, 0}, {t, 0}};
    utimensat(AT_FDCWD, path, ts, AT_SYMLINK_NOFOLLOW);
}

// Pick a conflict answer (its row) and press a button.
static void st_conflict_answer(int cf, int button) {
    for (int k = 0; k < A.dlg.nopts; k++)
        if (g_cf_choice[k] == cf) A.dlg.opt = k;
    if (A.dlg.open) dialog_choose(button);
}

// Several conflicts asked about together: newer only, rename existing, skip, one by one; merging folders newer-only
// in a move keeps the source when it still has files.
static void st_conflict_batches(void) {
    time_t t = time(NULL) - 1000;
    mkdir(ST_BASE "/bs", 0755);
    mkdir(ST_BASE "/bd", 0755);
    st_write(ST_BASE "/bs/n1.txt", 100); st_set_mtime(ST_BASE "/bs/n1.txt", t + 100); // newer than what's there
    st_write(ST_BASE "/bs/n2.txt", 200); st_set_mtime(ST_BASE "/bs/n2.txt", t);       // older
    st_write(ST_BASE "/bs/n3.txt", 300); st_set_mtime(ST_BASE "/bs/n3.txt", t + 100); // newer
    st_write(ST_BASE "/bs/fresh.txt", 5);
    st_write(ST_BASE "/bd/n1.txt", 1); st_set_mtime(ST_BASE "/bd/n1.txt", t);
    st_write(ST_BASE "/bd/n2.txt", 2); st_set_mtime(ST_BASE "/bd/n2.txt", t + 100);
    st_write(ST_BASE "/bd/n3.txt", 3); st_set_mtime(ST_BASE "/bd/n3.txt", t);
    char *four[4] = {ST_BASE "/bs/n1.txt", ST_BASE "/bs/n2.txt", ST_BASE "/bs/n3.txt", ST_BASE "/bs/fresh.txt"};
    start_transfer(four, 4, ST_BASE "/bd", false, false);
    st_wait_dlg();
    ST_CHECK(A.dlg.open && A.dlg.nbuttons == 3 && A.dlg.nitems == 3 && strstr(A.dlg.title, "3 items"));
    ST_CHECK(A.dlg.nitems == 3 && A.dlg.item_sub[0] && strstr(A.dlg.item_sub[0], "newer") && strstr(A.dlg.item_sub[1], "older"));
    ST_CHECK(strstr(A.dlg.opt_desc[3], "(2 of 3)") != NULL);
    st_shot("conflicts");
    st_conflict_answer(CF_NEWER, 0); // apply to all
    st_wait_ops();
    ST_CHECK(st_size(ST_BASE "/bd/n1.txt") == 100 && st_size(ST_BASE "/bd/n2.txt") == 2 && st_size(ST_BASE "/bd/n3.txt") == 300);
    ST_CHECK(st_size(ST_BASE "/bd/fresh.txt") == 5);
    // rename existing: what's there becomes "n2 (2).txt"
    char *two[2] = {ST_BASE "/bs/n2.txt", ST_BASE "/bs/fresh.txt"};
    start_transfer(two, 2, ST_BASE "/bd", false, false);
    st_wait_dlg();
    st_conflict_answer(CF_RENAME, 0);
    st_wait_ops();
    ST_CHECK(st_size(ST_BASE "/bd/n2.txt") == 200 && st_size(ST_BASE "/bd/n2 (2).txt") == 2);
    ST_CHECK(st_size(ST_BASE "/bd/fresh.txt") == 5 && st_size(ST_BASE "/bd/fresh (2).txt") == 5);
    // skip: nothing changes, and the skipped bytes leave the totals
    st_write(ST_BASE "/bs/n1.txt", 111);
    start_transfer(two, 2, ST_BASE "/bd", false, false);
    st_wait_dlg();
    st_conflict_answer(CF_SKIP, 0);
    st_wait_ops();
    ST_CHECK(st_size(ST_BASE "/bd/n2.txt") == 200 && !st_exists(ST_BASE "/bd/n2 (3).txt") && !st_exists(ST_BASE "/bd/fresh (3).txt"));
    // one by one: each conflict asked about alone; "do this for the rest" answers the remaining one
    char *nn[2] = {ST_BASE "/bs/n1.txt", ST_BASE "/bs/n3.txt"};
    start_transfer(nn, 2, ST_BASE "/bd", false, false);
    st_wait_dlg();
    ST_CHECK(A.dlg.nbuttons == 3);
    if (A.dlg.open) dialog_choose(1); // one by one
    st_frames(1);
    st_wait_dlg();
    ST_CHECK(A.dlg.open && A.dlg.nbuttons == 2 && A.dlg.has_check && strstr(A.dlg.check_label, "1 remaining") && strstr(A.dlg.title, "File"));
    A.dlg.check = true;
    st_conflict_answer(CF_REPLACE, 0);
    st_wait_ops();
    ST_CHECK(!A.dlg.open && st_size(ST_BASE "/bd/n1.txt") == 111 && st_size(ST_BASE "/bd/n3.txt") == 300);
    // move + merge newer only: older files stay in the source, so the source folder stays (with a note)
    mkdir(ST_BASE "/bs/m", 0755);
    mkdir(ST_BASE "/bd/m", 0755);
    st_write(ST_BASE "/bs/m/old.txt", 10); st_set_mtime(ST_BASE "/bs/m/old.txt", t);
    st_write(ST_BASE "/bs/m/new.txt", 20); st_set_mtime(ST_BASE "/bs/m/new.txt", t + 100);
    st_write(ST_BASE "/bd/m/old.txt", 1); st_set_mtime(ST_BASE "/bd/m/old.txt", t + 100);
    st_write(ST_BASE "/bd/m/new.txt", 2); st_set_mtime(ST_BASE "/bd/m/new.txt", t);
    char *m[1] = {ST_BASE "/bs/m"};
    start_transfer(m, 1, ST_BASE "/bd", true, false);
    Op *op = st_last_op();
    st_wait_dlg();
    ST_CHECK(A.dlg.open && !strcmp(A.dlg.opts[1], "Merge"));
    st_conflict_answer(CF_NEWER, 0);
    st_wait_ops();
    ST_CHECK(st_size(ST_BASE "/bd/m/new.txt") == 20 && st_size(ST_BASE "/bd/m/old.txt") == 1 && st_exists(ST_BASE "/bs/m/old.txt"));
    ST_CHECK(A.nops && st_last_op() == op && op->nerr == 1 && strstr(op->err, "Left"));
    if (A.nops && st_last_op() == op) op->dismissed = true;
    st_frames(1);
    st_rmrf(ST_BASE "/bs");
    st_rmrf(ST_BASE "/bd");
}

// The transfer queue: big transfers take turns in queue order (paused ones don't hold a turn), the panel and its
// keys; pausing a running copy stops it; verify after copy; the checksum itself.
static void st_queue(void) {
    // XXH64 against xxhsum
    Xxh64 h;
    xxh64_init(&h);
    ST_CHECK(xxh64_final(&h) == 0xEF46DB3751D8E999ull);
    xxh64_update(&h, (const u8 *)"abc", 3);
    ST_CHECK(xxh64_final(&h) == 0x44BC2CF5AD770999ull);
    char az[1000];
    for (int i = 0; i < 1000; i++) az[i] = (char)('a' + i % 26);
    xxh64_init(&h);
    for (int i = 0; i < 1000; i += 7) xxh64_update(&h, (const u8 *)az + i, (size_t)MIN(7, 1000 - i)); // in odd pieces
    ST_CHECK(xxh64_final(&h) == 0x94B86DB9A16D86A9ull);

    int was = A.transfers_at_once;
    A.transfers_at_once = 1;
    st_write(ST_BASE "/q1.bin", 48u << 20);
    st_write(ST_BASE "/q2.bin", 48u << 20);
    char *q1[1] = {ST_BASE "/q1.bin"}, *q2[1] = {ST_BASE "/q2.bin"};
    start_transfer(q1, 1, ST_XDEV, false, false);
    Op *o1 = st_last_op();
    op_set_paused(o1, true);
    start_transfer(q2, 1, ST_XDEV, false, false);
    Op *o2 = st_last_op();
    op_set_paused(o2, true);
    for (int k = 0; k < 1000 && (atomic_load(&o1->phase) != OPS_QUEUED || atomic_load(&o2->phase) != OPS_QUEUED); k++) { st_frames(1); SDL_Delay(2); }
    ST_CHECK(atomic_load(&o1->phase) == OPS_QUEUED && atomic_load(&o2->phase) == OPS_QUEUED && !o1->go && !o2->go);
    ST_CHECK(op_queue_pos(o1) == 1 && op_queue_pos(o2) == 2);
    // the panel: Ctrl+J; the highlighted row moves down with Ctrl+Down (q2 is next now)
    st_key(SDLK_J, SDL_KMOD_CTRL);
    st_frames(1);
    ST_CHECK(A.q_open && A.q_r.w > 0 && A.ops[A.q_sel] == o1 && A.q_pause[A.q_sel].w > 0 && A.op_card[0].w == 0);
    st_key(SDLK_DOWN, SDL_KMOD_CTRL);
    ST_CHECK(A.ops[A.q_sel] == o1 && A.q_sel == A.nops - 1 && A.ops[A.nops - 2] == o2 && op_queue_pos(o2) == 1);
    st_frames(1);
    st_shot("queue");
    // a click on q2's row highlights it; Space resumes it; Down + Space resumes q1: q2 gets the only turn. (Keys and
    // the click run frames: both are paused until then, so nothing has started yet)
    Rect rr = A.q_row[A.nops - 2];
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, rr.x + S(20), rr.y + rr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
    ST_CHECK(A.ops[A.q_sel] == o2 && !o2->go && !o1->go);
    bool pv = A.preview_on;
    queue_key(SDLK_SPACE, SDL_KMOD_NONE);
    on_text(" "); // (the text event that comes with Space doesn't open Quick Look)
    queue_key(SDLK_DOWN, SDL_KMOD_NONE);
    queue_key(SDLK_SPACE, SDL_KMOD_NONE);
    ST_CHECK(!atomic_load(&o1->paused) && !atomic_load(&o2->paused) && A.preview_on == pv && !A.ql_on && A.ops[A.q_sel] == o1);
    main_drain();
    ops_schedule();
    ST_CHECK(o2->go && !o1->go);
    u64 t2 = 0;
    for (int k = 0; k < 10000 && !o1->finished; k++) {
        if (!t2 && o2->finished) t2 = o2->t_end;
        main_drain();
        ops_schedule();
        SDL_Delay(1);
    }
    ST_CHECK(o1->finished && t2 && o1->t_run >= t2); // q1 started after q2 was done
    st_wait_ops();
    ST_CHECK(st_size(ST_XDEV "/q1.bin") == 48 << 20 && st_size(ST_XDEV "/q2.bin") == 48 << 20);
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    ST_CHECK(!A.q_open);
    unlink(ST_XDEV "/q1.bin");
    unlink(ST_XDEV "/q2.bin");

    // pausing a running copy stops it where it is; resuming finishes it
    st_write(ST_BASE "/big.bin", 512u << 20);
    char *big[1] = {ST_BASE "/big.bin"};
    start_transfer(big, 1, ST_XDEV, false, false);
    Op *op = st_last_op();
    for (int k = 0; k < 2000 && atomic_load(&op->bytes_done) == 0 && atomic_load(&op->phase) != OPS_DONE; k++) { st_frames(1); SDL_Delay(1); }
    op_set_paused(op, true);
    if (atomic_load(&op->phase) != OPS_DONE) {
        SDL_Delay(100); // (it finishes the chunk it's in)
        u64 b0 = atomic_load(&op->bytes_done);
        SDL_Delay(150);
        st_frames(1);
        ST_CHECK(atomic_load(&op->bytes_done) == b0 && b0 < (512u << 20) && atomic_load(&op->phase) == OPS_RUNNING);
        char det[600];
        op_detail(op, now_ns(), det, sizeof det);
        ST_CHECK(!strncmp(det, "Paused", 6));
        op_set_paused(op, false);
    } else printf("copy finished before it could be paused\n");
    st_wait_op_done(op);
    ST_CHECK(!op->nerr);
    st_wait_ops();
    ST_CHECK(st_size(ST_XDEV "/big.bin") == 512 << 20);
    unlink(ST_BASE "/big.bin");
    unlink(ST_XDEV "/big.bin");
    A.transfers_at_once = was;

    // verify after copy: every copied file read back and compared
    A.verify_copies = true;
    mkdir(ST_BASE "/vf", 0755);
    st_write(ST_BASE "/vf/one.bin", 3u << 20);
    st_write(ST_BASE "/vf/two.bin", 12345);
    char *vf[1] = {ST_BASE "/vf"};
    start_transfer(vf, 1, ST_XDEV, false, false);
    op = st_last_op();
    ST_CHECK(atomic_load(&op->verify));
    st_wait_op_done(op);
    ST_CHECK(op->verify_on && atomic_load(&op->files_verified) == 2 && atomic_load(&op->bytes_verified) == (3u << 20) + 12345 && !op->nerr);
    ST_CHECK(st_size(ST_XDEV "/vf/one.bin") == 3 << 20);
    {
        char det[600];
        op_detail(op, now_ns(), det, sizeof det);
        ST_CHECK(strstr(det, "2 files verified") != NULL);
    }
    st_wait_ops();
    A.verify_copies = false;
    // a copy that differs from its original fails and is removed
    st_write(ST_XDEV "/vf/bad.bin", 12344);
    Op *vo = op_new(OP_COPY, NULL, 0, NULL);
    int in = open(ST_BASE "/vf/two.bin", O_RDONLY | O_CLOEXEC), dd = open(ST_XDEV "/vf", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    ST_CHECK(verify_copy(vo, in, dd, "two.bin", "two.bin") && !vo->nerr);
    ST_CHECK(!verify_copy(vo, in, dd, "bad.bin", "bad.bin") && vo->nerr == 1 && strstr(vo->err, "failed verification"));
    close(in);
    close(dd);
    op_free(vo);
    st_rmrf(ST_BASE "/vf");
    st_rmrf(ST_XDEV "/vf");
}

static int run_selftest_ops(void) {
    st_wait();
    Pane *p = active_pane();
    ST_CHECK(!strcmp(p->path, ST_BASE));

    // copy + paste in the same folder -> "a (copy).txt", selected after paste
    st_select(p, "a.txt");
    st_key(SDLK_C, SDL_KMOD_CTRL);
    st_key(SDLK_V, SDL_KMOD_CTRL);
    st_wait_ops();
    ST_CHECK(st_size(ST_BASE "/a (copy).txt") == 1 << 20);
    ST_CHECK(!strcmp(st_cursor_name(p), "a (copy).txt"));

    // F2 rename selects the stem; typing replaces it
    st_select(p, "a.txt");
    st_key(SDLK_F2, SDL_KMOD_NONE);
    ST_CHECK(A.ren_pane == p && A.ren_te.anchor == 0 && A.ren_te.caret == 1);
    st_shot("rename");
    on_text("renamed");
    ST_CHECK(!strcmp(A.ren_te.buf, "renamed.txt"));
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(st_exists(ST_BASE "/renamed.txt") && !st_exists(ST_BASE "/a.txt"));
    ST_CHECK(!strcmp(st_cursor_name(p), "renamed.txt"));
    // rename onto an existing name is refused
    st_key(SDLK_F2, SDL_KMOD_NONE);
    st_key(SDLK_A, SDL_KMOD_CTRL);
    on_text("b.txt");
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(st_exists(ST_BASE "/renamed.txt") && st_size(ST_BASE "/b.txt") == 10);
    // undo rename
    st_key(SDLK_Z, SDL_KMOD_CTRL);
    st_wait();
    ST_CHECK(st_exists(ST_BASE "/a.txt") && !st_exists(ST_BASE "/renamed.txt"));

    // cut folder, paste into dst
    st_select(p, "sub");
    st_key(SDLK_X, SDL_KMOD_CTRL);
    ST_CHECK(clip_is_cut(ST_BASE, "sub"));
    pane_navigate(p, ST_BASE "/dst", true);
    st_wait();
    st_key(SDLK_V, SDL_KMOD_CTRL);
    st_wait_ops();
    ST_CHECK(st_size(ST_BASE "/dst/sub/c.txt") == 1234 && !st_exists(ST_BASE "/sub"));
    ST_CHECK(g_clip.n == 0); // cut+paste empties the clipboard
    ST_CHECK(!strcmp(st_cursor_name(p), "sub"));
    // undo move
    st_key(SDLK_Z, SDL_KMOD_CTRL);
    st_wait_ops();
    ST_CHECK(st_exists(ST_BASE "/sub/c.txt") && !st_exists(ST_BASE "/dst/sub"));

    // move to trash + restore
    pane_navigate(p, ST_BASE, true);
    st_wait();
    st_select(p, "b.txt");
    st_key(SDLK_DELETE, SDL_KMOD_NONE);
    st_wait_ops();
    ST_CHECK(!st_exists(ST_BASE "/b.txt"));
    ST_CHECK(st_exists(ST_BASE "/xdg/Trash/files/b.txt"));
    {
        char buf[512] = {0};
        int fd = open(ST_BASE "/xdg/Trash/info/b.txt.trashinfo", O_RDONLY);
        if (fd >= 0) { ssize_t r = read(fd, buf, sizeof buf - 1); (void)r; close(fd); }
        ST_CHECK(strstr(buf, "[Trash Info]") && strstr(buf, "Path=" ST_BASE "/b.txt") && strstr(buf, "DeletionDate="));
    }
    st_key(SDLK_Z, SDL_KMOD_CTRL);
    st_wait_ops();
    ST_CHECK(st_size(ST_BASE "/b.txt") == 10 && !st_exists(ST_BASE "/xdg/Trash/files/b.txt") &&
             !st_exists(ST_BASE "/xdg/Trash/info/b.txt.trashinfo"));

    // permanent delete asks first
    st_select(p, "b.txt");
    st_key(SDLK_DELETE, SDL_KMOD_SHIFT);
    ST_CHECK(A.dlg.open);
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    ST_CHECK(!A.dlg.open && st_exists(ST_BASE "/b.txt"));
    st_key(SDLK_DELETE, SDL_KMOD_SHIFT);
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait_ops();
    ST_CHECK(!st_exists(ST_BASE "/b.txt"));

    // new folder starts an inline rename
    st_key(SDLK_N, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
    st_wait();
    ST_CHECK(st_exists(ST_BASE "/New folder"));
    ST_CHECK(A.ren_pane == p && !strcmp(A.ren_te.buf, "New folder"));
    on_text("made");
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(st_exists(ST_BASE "/made") && !st_exists(ST_BASE "/New folder"));

    // conflicts: one asks with the choices (Keep both first), Enter applies; the answer is offered first next time
    char *one[1] = {ST_BASE "/a.txt"};
    start_transfer(one, 1, ST_BASE "/dst", false, false);
    st_wait_ops();
    ST_CHECK(st_exists(ST_BASE "/dst/a.txt"));
    start_transfer(one, 1, ST_BASE "/dst", false, false);
    st_wait_dlg();
    ST_CHECK(A.dlg.open && A.dlg.nbuttons == 2 && A.dlg.nopts == 5 && g_cf_choice[A.dlg.opt] == CF_KEEP_BOTH && !A.dlg.has_check);
    st_shot("conflict");
    st_key(SDLK_RETURN, SDL_KMOD_NONE); // keep both
    st_wait_ops();
    ST_CHECK(st_exists(ST_BASE "/dst/a (2).txt"));
    st_write(ST_BASE "/a.txt", 777);
    start_transfer(one, 1, ST_BASE "/dst", false, false);
    st_wait_dlg();
    ST_CHECK(A.dlg.open && g_cf_choice[A.dlg.opt] == CF_KEEP_BOTH);
    st_key(SDLK_2, SDL_KMOD_NONE); // overwrite
    ST_CHECK(g_cf_choice[A.dlg.opt] == CF_REPLACE);
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait_ops();
    ST_CHECK(st_size(ST_BASE "/dst/a.txt") == 777 && !st_exists(ST_BASE "/dst/a (3).txt") && A.conflict_last == CF_REPLACE);
    st_conflict_batches();

    // cross-filesystem move (tmpfs /tmp -> /dev/shm) and undo back
    mkdir(ST_XDEV, 0755);
    st_write(ST_BASE "/made/x.bin", 3 << 20);
    char *made[1] = {ST_BASE "/made"};
    start_transfer(made, 1, ST_XDEV, true, false);
    st_wait_ops();
    ST_CHECK(st_size(ST_XDEV "/made/x.bin") == 3 << 20 && !st_exists(ST_BASE "/made"));
    st_key(SDLK_Z, SDL_KMOD_CTRL);
    st_wait_ops();
    ST_CHECK(st_size(ST_BASE "/made/x.bin") == 3 << 20 && !st_exists(ST_XDEV "/made"));

    // drag "a (copy).txt" by its name onto the "dst" folder row -> move
    pane_refresh(p);
    st_wait();
    f32 sy = st_row_y(p, "a (copy).txt"), dy = st_row_y(p, "dst");
    f32 nx = p->ly.col_name + S(40);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, nx, sy, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(A.drag == DRAG_ITEMS_PENDING);
    st_mouse(SDL_EVENT_MOUSE_MOTION, nx + S(20), (sy + dy) * 0.5f, 0, 0, 0);
    st_mouse(SDL_EVENT_MOUSE_MOTION, nx, dy, 0, 0, 0);
    ST_CHECK(A.drag == DRAG_ITEMS && A.drop_row >= 0 && !strcmp(A.drop_dir, ST_BASE "/dst") && !A.drop_copy);
    st_shot("drag");
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    st_wait_ops();
    ST_CHECK(st_exists(ST_BASE "/dst/a (copy).txt") && !st_exists(ST_BASE "/a (copy).txt"));

    // "Ask what to do when dropping": the drop opens Move / Copy / Link here (move preselected); Esc keeps everything,
    // Link here makes a symlink, undo takes it away
    A.drop_ask = true;
    mkdir(ST_BASE "/lnk", 0755);
    for (int k = 0; k < 3; k++) { st_frames(10); SDL_Delay(50); st_wait(); } // (let the refreshes from the move above land)
    pane_refresh(p);
    st_wait();
    for (int round = 0; round < 2; round++) {
        sy = st_row_y(p, "a.txt"), dy = st_row_y(p, "lnk");
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, nx, sy, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, nx + S(20), (sy + dy) * 0.5f, 0, 0, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, nx, dy, 0, 0, 0);
        ST_CHECK(A.drag == DRAG_ITEMS && !strcmp(A.drop_dir, ST_BASE "/lnk"));
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(A.menu.open && A.menu.n == 5 && A.menu.items[A.menu.hover].act == ACT_DROP_MOVE && A.dmenu_n == 1);
        if (!round) {
            st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
            st_wait_ops();
            ST_CHECK(!A.menu.open && st_exists(ST_BASE "/a.txt") && !st_exists(ST_BASE "/lnk/a.txt"));
        }
    }
    st_shot("dropmenu");
    st_key(SDLK_DOWN, SDL_KMOD_NONE);
    st_key(SDLK_DOWN, SDL_KMOD_NONE);
    ST_CHECK(A.menu.items[A.menu.hover].act == ACT_DROP_LINK);
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    char lt[PATH_MAX] = "";
    ssize_t ll = readlink(ST_BASE "/lnk/a.txt", lt, sizeof lt - 1);
    if (ll > 0) lt[ll] = 0;
    ST_CHECK(!A.menu.open && !A.dmenu_n && !strcmp(lt, ST_BASE "/a.txt") && st_exists(ST_BASE "/a.txt"));
    st_key(SDLK_Z, SDL_KMOD_CTRL);
    st_wait_ops();
    struct stat lst;
    ST_CHECK(lstat(ST_BASE "/lnk/a.txt", &lst) != 0 && st_exists(ST_BASE "/a.txt"));
    rmdir(ST_BASE "/lnk");
    A.drop_ask = false;

    // dragging out to other apps (dragout.c; no window system here): what's offered, which action - from the same
    // keys and setting as a drop inside -, recognising our own drag dropped back onto this window
    {
        char *dp[2] = {ST_BASE "/a b.txt", ST_BASE "/c%d"};
        dragout_data(dp, 2);
        ST_CHECK(!strcmp(g_do.uri, "file://" ST_BASE "/a%20b.txt\r\nfile://" ST_BASE "/c%25d\r\n"));
        ST_CHECK(!strcmp(g_do.plain, ST_BASE "/a b.txt\n" ST_BASE "/c%d"));
        int offer, pref;
        static const struct { SDL_Keymod mod; bool ask; int offer, pref; } want[] = {
            {SDL_KMOD_NONE, false, DO_COPY | DO_MOVE, DO_MOVE}, {SDL_KMOD_NONE, true, DO_COPY | DO_MOVE | DO_LINK | DO_ASK, DO_ASK},
            {SDL_KMOD_LCTRL, true, DO_COPY, DO_COPY}, {SDL_KMOD_LSHIFT, false, DO_MOVE, DO_MOVE},
            {SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT, false, DO_LINK | DO_COPY, DO_LINK},
        };
        for (size_t k = 0; k < ARRAY_COUNT(want); k++) {
            SDL_SetModState(want[k].mod);
            A.drop_ask = want[k].ask;
            dragout_pick(&offer, &pref);
            ST_CHECK(offer == want[k].offer && pref == want[k].pref);
        }
        SDL_SetModState(SDL_KMOD_NONE);
        A.drop_ask = false;
        g_do.sys = DOS_WAYLAND; // (pretend: only the bookkeeping is used)
        g_do.active = true;
        char *other[2] = {ST_BASE "/a b.txt", ST_BASE "/x"};
        ST_CHECK(dragout_is_self(dp, 2) && !dragout_is_self(other, 2) && !dragout_is_self(dp, 1));
        g_do.active = false;
        g_do.t_end = now_ns() - 4000000000ull; // long over: a drop of the same files now comes from elsewhere
        ST_CHECK(!dragout_is_self(dp, 2));
        g_do.sys = DOS_NONE;
        // no window system: an item dragged past the window's edge stays a drag inside
        // (let the watcher's refreshes from the undo and rmdir above land first: a listing mid-drag cancels the drag)
        st_wait_ops();
        for (int k = 0; k < 3; k++) { st_frames(10); SDL_Delay(50); st_wait(); }
        pane_refresh(p);
        st_wait();
        f32 ay2 = st_row_y(p, "a.txt");
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, nx, ay2, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, nx + S(30), ay2, 0, 0, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, -5, ay2, 0, 0, 0);
        ST_CHECK(A.drag == DRAG_ITEMS && !g_do.active);
        st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
        ST_CHECK(A.drag == DRAG_NONE && st_exists(ST_BASE "/a.txt"));
    }

    { // the card while dragging: the first 3 items as stacked in the list (icon + name), "+ N items" for the rest
        st_wait_ops(); // (an undo above may still be trashing copies; its refresh would drop the selection mid-drag)
        for (int k = 0; k < 3; k++) { st_frames(10); SDL_Delay(50); st_wait(); }
        do_action(p, ACT_SELECT_ALL);
        u32 total = p->sel_count;
        f32 ay3 = st_row_y(p, "a.txt");
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, nx, ay3, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, nx + S(40), ay3 + S(40), 0, 0, 0);
        DragCard dc;
        drag_card_build(&dc);
        ST_CHECK(A.drag == DRAG_ITEMS && total > 3 && dc.n == 3 && dc.more == total - 3);
        ST_CHECK(dc.e[0] == &p->L->entries[p->order[0]]); // (in list order)
        st_shot("drag-many");
        st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
        sel_clear(p);
    }

    // cancel a big copy: the partial file must be removed
    st_write(ST_BASE "/big.bin", 768u << 20);
    char *big[1] = {ST_BASE "/big.bin"};
    start_transfer(big, 1, ST_XDEV, false, false);
    if (getenv("FE_SHOTS")) {
        A.ops[A.nops - 1]->t_start -= 400000000ull; // make the card visible right away
        st_frames(3);
        st_shot("progress");
    }
    op_cancel(A.ops[A.nops - 1]);
    st_wait_ops();
    if (st_exists(ST_XDEV "/big.bin")) ST_CHECK(st_size(ST_XDEV "/big.bin") == 768 << 20); // finished before cancel
    else printf("cancelled copy left no partial file\n");
    unlink(ST_BASE "/big.bin");
    unlink(ST_XDEV "/big.bin");

    // a canceled op keeps the progress it reached (by bytes, or by items for a delete), in red, marked Canceled
    static const int ckinds[] = {OP_COPY, OP_MOVE, OP_DELETE, OP_EXTRACT, OP_COMPRESS};
    for (int k = 0; k < (int)(sizeof ckinds / sizeof *ckinds); k++) {
        Op *cop = xcalloc(1, sizeof *cop);
        pthread_mutex_init(&cop->mtx, NULL);
        cop->kind = ckinds[k];
        cop->nsrc = 1;
        atomic_store(&cop->bytes_total, 1000);
        atomic_store(&cop->bytes_done, 420);
        atomic_store(&cop->files_total, 10);
        atomic_store(&cop->files_done, 3);
        snprintf(cop->cur, sizeof cop->cur, "half.bin");
        atomic_store(&cop->phase, OPS_DONE);
        char det[600], title[256], want[64];
        op_detail(cop, now_ns(), det, sizeof det);
        ST_CHECK(op_frac(cop) == 1.0 && !strncmp(det, "Done", 4)); // not canceled: full
        atomic_store(&cop->cancel, 1);
        int pct = cop->kind == OP_DELETE ? 30 : 42;
        ST_CHECK(fabs(op_frac(cop) - pct / 100.0) < 1e-9);
        op_detail(cop, now_ns(), det, sizeof det);
        snprintf(want, sizeof want, "Canceled  \xC2\xB7  %d%%  \xC2\xB7  half.bin", pct);
        ST_CHECK(!strcmp(det, want));
        op_title(cop, title, sizeof title);
        ST_CHECK(!strncmp(title, "Canceled: ", 10));
        cop->cur[0] = 0; // nothing started yet: just the percentage
        op_detail(cop, now_ns(), det, sizeof det);
        snprintf(want, sizeof want, "Canceled  \xC2\xB7  %d%%", pct);
        ST_CHECK(!strcmp(det, want));
        cop->nerr = 2;
        op_title(cop, title, sizeof title);
        ST_CHECK(!strcmp(title, "Canceled after 2 errors"));
        pthread_mutex_destroy(&cop->mtx);
        free(cop);
    }
    st_queue();

    // context menus (first let the refreshes from the deletes above land: slower machines see them later)
    for (int k = 0; k < 3; k++) { st_frames(10); SDL_Delay(50); st_wait(); }
    pane_refresh(p);
    st_wait();
    f32 ay = st_row_y(p, "a.txt");
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, nx, ay, SDL_BUTTON_RIGHT, 1, 0);
    if (!(A.menu.open && A.menu.items[0].act == ACT_OPEN && !strcmp(st_cursor_name(p), "a.txt")))
        fprintf(stderr, "menu check: open=%d act0=%d cursor=%s dialog=%d sheet=%d ops=%d row_y=%.0f\n", A.menu.open,
                A.menu.n ? A.menu.items[0].act : -1, st_cursor_name(p), A.dlg.open, sheet_open(), A.nops, ay);
    if (!(A.menu.open && !strcmp(st_cursor_name(p), "a.txt")) && p->L)
        for (u32 k = 0; k < p->count && k < 12; k++) fprintf(stderr, "  row %u: %s\n", k, p->L->entries[p->order[k]].name);
    ST_CHECK(A.menu.open && A.menu.items[0].act == ACT_OPEN && !strcmp(st_cursor_name(p), "a.txt"));
    st_shot("menu");
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    ST_CHECK(!A.menu.open);
    f32 ey = p->ly.list.y + p->ly.list.h - S(20); // empty space under the rows
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, nx, ey, SDL_BUTTON_RIGHT, 1, 0);
    ST_CHECK(A.menu.open && A.menu.items[0].act == ACT_NEW_FOLDER);
    // click "New folder"
    Rect mr = A.menu.r;
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, mr.x + S(30), mr.y + S(5) + S(14), SDL_BUTTON_LEFT, 1, 0);
    st_wait();
    ST_CHECK(!A.menu.open && st_exists(ST_BASE "/New folder") && A.ren_pane == p);
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    ST_CHECK(!A.ren_pane);
    st_key(SDLK_Z, SDL_KMOD_CTRL); // undo create
    st_wait();
    ST_CHECK(!st_exists(ST_BASE "/New folder"));

    // clipboard formats we publish
    char *two[2] = {ST_BASE "/a.txt", ST_BASE "/dst/a (2).txt"};
    clip_set(two, 2, false);
    size_t sz;
    const char *g = clip_cb(NULL, "x-special/gnome-copied-files", &sz);
    ST_CHECK(g && !strcmp(g, "copy\nfile://" ST_BASE "/a.txt\nfile://" ST_BASE "/dst/a%20%282%29.txt"));
    int n;
    bool cut;
    char **back = clip_get(&n, &cut);
    ST_CHECK(n == 2 && !cut && back && !strcmp(back[1], ST_BASE "/dst/a (2).txt"));
    strv_free(back, n);
    clip_clear();

    // an image on the clipboard pastes as a new file
    static const char *img_mimes[] = {"text/html", "image/jpeg", "image/png"};
    SDL_SetClipboardData(st_img_clip_cb, NULL, NULL, img_mimes, ARRAY_COUNT(img_mimes));
    ST_CHECK(!clip_has_files() && clip_image_type() == 0); // png preferred over jpeg
    pane_navigate(p, ST_BASE "/dst", true);
    st_wait();
    st_key(SDLK_V, SDL_KMOD_CTRL);
    st_wait_ops();
    st_wait();
    const char *pasted = st_cursor_name(p);
    ST_CHECK(pasted && !strncmp(pasted, "Pasted image ", 13) && strstr(pasted, ".png"));
    if (pasted) {
        char pp[PATH_MAX];
        path_join(pp, sizeof pp, ST_BASE "/dst", pasted);
        ST_CHECK(st_size(pp) == 8);
    }
    SDL_ClearClipboardData();
    char tidy[PATH_MAX];
    clip_paste_tidy(tidy, sizeof tidy);

    st_rmrf(ST_BASE);
    st_rmrf(ST_XDEV);
    if (st_fail) { fprintf(stderr, "SELFTEST-OPS: %d failure(s)\n", st_fail); return 1; }
    printf("SELFTEST-OPS OK\n");
    fflush(stdout);
    return 0;
}

// --- self-test for M4: palette, search, live refresh, folder sizes ---------

#define M4_BASE "/tmp/fe-m4-test"

static void st_setup_m4(void) {
    st_rmrf(M4_BASE);
    mkdir(M4_BASE, 0755);
    mkdir(M4_BASE "/alpha", 0755);
    mkdir(M4_BASE "/alpha/beta", 0755);
    mkdir(M4_BASE "/alpha/beta/gamma", 0755);
    mkdir(M4_BASE "/alpha/beta/gamma/deepfolder", 0755);
    mkdir(M4_BASE "/big", 0755);
    mkdir(M4_BASE "/small", 0755);
    st_write(M4_BASE "/alpha/beta/gamma/deepfolder/needle.txt", 1000);
    st_write(M4_BASE "/alpha/one.log", 500);
    st_write(M4_BASE "/alpha/beta/two.log", 300);
    st_write(M4_BASE "/big/blob.bin", 2 << 20);
    st_write(M4_BASE "/small/tiny.bin", 1024);
    st_write(M4_BASE "/readme.md", 42);
}

static void st_wait_pal(void) {
    for (int k = 0; k < 400 && A.pal_pending; k++) { st_frames(1); SDL_Delay(2); }
}

static void st_type(const char *s) {
    on_text(s);
    st_frames(1);
}

static bool st_wait_cond(bool (*cond)(void), int ms) {
    for (int k = 0; k < ms / 5; k++) {
        if (cond()) return true;
        st_frames(1);
        SDL_Delay(5);
    }
    return cond();
}

static bool c_index_ready(void) {
    DirIndex *d = index_acquire();
    bool ok = d && d->n >= 7;
    index_release(d);
    return ok;
}

static bool c_search_done(void) {
    Pane *p = active_pane();
    return p->L && p->L->is_search && !p->L_next && atomic_load(&p->L->state) == LST_COMPLETE;
}

static bool c_has_new_ext(void) {
    Pane *p = active_pane();
    if (!p->order || p->L_next) return false;
    for (u32 i = 0; i < p->count; i++)
        if (!strcmp(p->L->entries[p->order[i]].name, "new_ext.txt")) return true;
    return false;
}

static bool c_went_up(void) { return !strcmp(active_pane()->path, M4_BASE "/alpha"); }

static bool c_sizes_done(void) {
    Pane *p = active_pane();
    if (!p->order) return false;
    for (u32 i = 0; i < p->count; i++) {
        Entry *e = &p->L->entries[p->order[i]];
        bool known;
        if (e->flags & ENT_DIR) { entry_dir_size(e, &known); if (!known) return false; }
    }
    return true;
}

static u64 st_dir_size(Pane *p, const char *name) {
    for (u32 i = 0; i < p->count; i++)
        if (!strcmp(p->L->entries[p->order[i]].name, name)) return entry_dir_size(&p->L->entries[p->order[i]], NULL);
    return (u64)-1;
}

// Size of any entry of the listing, shown or not; *known = false while not measured.
static u64 st_entry_dir_size(Pane *p, const char *name, bool *known) {
    *known = false;
    for (u32 i = 0; i < p->L->count; i++)
        if (!strcmp(p->L->entries[i].name, name)) return entry_dir_size(&p->L->entries[i], known);
    return (u64)-1;
}

// Set a folder's mtime `secs` back, so the size cache trusts it (a recent one could still change unseen).
static void st_age(const char *path, int secs) {
    struct timespec ts[2] = {{time(NULL) - secs, 0}, {time(NULL) - secs, 0}};
    utimensat(AT_FDCWD, path, ts, 0);
}

static u64 st_du(const char *path) {
    static _Atomic int no_cancel;
    struct stat st;
    if (stat(path, &st)) return (u64)-1;
    char *buf = xmalloc(64 * 1024);
    u64 n = du_tree(AT_FDCWD, path, st.st_dev, &no_cancel, buf);
    free(buf);
    return n;
}

static bool c_hidden_size_known(void) {
    bool known;
    st_entry_dir_size(active_pane(), ".hid", &known);
    return known;
}

static ssize_t st_read_text(const char *path, char *out, size_t cap); // M10
static void st_write_text(const char *path, const char *text); // M11

static int run_selftest_m4(void) {
    st_wait();
    Pane *p = active_pane();
    ST_CHECK(!strcmp(p->path, M4_BASE));
    char *roots[1] = {M4_BASE};
    index_rebuild(roots, 1);
    ST_CHECK(st_wait_cond(c_index_ready, 3000));

    // palette: fuzzy go-to a deep folder from anywhere
    st_key(SDLK_P, SDL_KMOD_CTRL);
    ST_CHECK(A.pal_open);
    st_wait_pal();
    st_type("dpfldr");
    st_wait_pal();
    ST_CHECK(A.pal_n > 0 && g_pal_items[0].kind == PI_DIR && !strcmp(path_basename(g_pal_items[0].text), "deepfolder"));
    st_shot("palette");
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(!A.pal_open && !strcmp(p->path, M4_BASE "/alpha/beta/gamma/deepfolder"));

    pane_navigate(p, M4_BASE, true);
    st_wait();
    // palette: files anywhere under the roots are found; Enter shows the file in its folder
    st_key(SDLK_P, SDL_KMOD_CTRL);
    st_wait_pal();
    st_type("needle");
    st_wait_pal();
    ST_CHECK(A.pal_n > 0 && g_pal_items[0].kind == PI_FILE && !strcmp(g_pal_items[0].text, M4_BASE "/alpha/beta/gamma/deepfolder/needle.txt"));
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(!A.pal_open && !strcmp(p->path, M4_BASE "/alpha/beta/gamma/deepfolder") && !strcmp(st_cursor_name(p), "needle.txt"));
    pane_navigate(p, M4_BASE, true);
    st_wait();
    // a path query reaches a file whose name alone doesn't match
    st_key(SDLK_P, SDL_KMOD_CTRL);
    st_wait_pal();
    st_type("gamma/ndl");
    st_wait_pal();
    ST_CHECK(A.pal_n > 0 && g_pal_items[0].kind == PI_FILE && !strcmp(path_basename(g_pal_items[0].text), "needle.txt"));
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);

    // palette command mode
    st_key(SDLK_P, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
    st_wait_pal();
    ST_CHECK(A.pal_open && A.pal_n == (int)ARRAY_COUNT(g_cmds) && g_pal_items[0].kind == PI_CMD);
    st_type("splt");
    st_wait_pal();
    ST_CHECK(A.pal_n > 0 && (g_pal_items[0].cmd == CMD_SPLIT || g_pal_items[0].cmd == CMD_SPLIT_DOWN));
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(A.npanels == 2);
    run_command(CMD_CLOSE_PANEL);
    st_wait();
    ST_CHECK(A.npanels == 1);
    p = active_pane();

    // palette: empty query lists recent folders; entries of the current folder match
    pane_navigate(p, M4_BASE, true);
    st_wait();
    ST_CHECK(g_nrecent > 0 && !strcmp(g_recent[0], M4_BASE));
    st_key(SDLK_P, SDL_KMOD_CTRL);
    st_wait_pal();
    ST_CHECK(A.pal_n > 0 && g_pal_items[0].kind == PI_RECENT);
    st_type("readme");
    st_wait_pal();
    ST_CHECK(A.pal_n > 0 && g_pal_items[0].kind == PI_ENTRY);
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    ST_CHECK(!strcmp(st_cursor_name(p), "readme.md"));
    // Esc closes, click outside closes
    st_key(SDLK_P, SDL_KMOD_CTRL);
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    ST_CHECK(!A.pal_open);

    // Ctrl+F opens the bottom bar; default = instant fuzzy filter of this folder (no rescan)
    ST_CHECK(!A.search_recursive);
    st_key(SDLK_F, SDL_KMOD_CTRL);
    ST_CHECK(A.search_pane == p && p->ly.has_sbar);
    st_shot("placeholder");
    Listing *folder = p->L;
    st_type("rdm");
    ST_CHECK(p->L == folder && !p->L->is_search && p->count == 1 && !strcmp(p->L->entries[p->order[0]].name, "readme.md"));
    st_shot("filter");
    st_key(SDLK_A, SDL_KMOD_CTRL);
    st_type("a"); // alpha, small, readme.md
    ST_CHECK(p->L == folder && p->count == 3);
    // the filter survives live refreshes
    st_write(M4_BASE "/another.txt", 1);
    for (int k = 0; k < 600 && p->count != 4; k++) { st_frames(1); SDL_Delay(5); }
    ST_CHECK(p->count == 4 && !strcmp(p->search_q, "a"));
    unlink(M4_BASE "/another.txt");
    for (int k = 0; k < 600 && p->count != 3; k++) { st_frames(1); SDL_Delay(5); }
    ST_CHECK(p->count == 3);
    // Enter focuses the list; Esc (nothing selected) clears the filter and closes the bar
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    ST_CHECK(!A.search_pane && p->ly.has_sbar);
    sel_clear(p);
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(!p->search_q[0] && p->count == 4 && !p->ly.has_sbar);

    // Subfolders switch on: recursive fuzzy search streaming into the list
    st_key(SDLK_F, SDL_KMOD_CTRL);
    {
        Rect field, toggle, close;
        search_bar_rects(p, &field, &toggle, &close);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, toggle.x + toggle.w - S(14), toggle.y + toggle.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
    }
    ST_CHECK(A.search_recursive);
    search_focus(p);
    st_type("needle");
    search_apply(p, true);
    ST_CHECK(st_wait_cond(c_search_done, 3000));
    ST_CHECK(p->count == 1 && !strcmp(p->L->entries[p->order[0]].name, "alpha/beta/gamma/deepfolder/needle.txt"));
    {
        int n;
        char **paths = pane_selection_paths(p, &n);
        ST_CHECK(n == 1 && !strcmp(paths[0], M4_BASE "/alpha/beta/gamma/deepfolder/needle.txt"));
        strv_free(paths, n);
    }
    st_shot("search");
    // typing more letters narrows in place (same listing, no rescan)
    st_key(SDLK_A, SDL_KMOD_CTRL);
    st_type("ne");
    search_apply(p, true);
    ST_CHECK(st_wait_cond(c_search_done, 3000));
    ST_CHECK(p->count == 2); // needle.txt, one.log
    Listing *before = p->L;
    st_type("e");
    ST_CHECK(p->L == before && p->count == 1 && !strcmp(p->search_q, "nee"));
    // a broader query re-searches, but the old results stay until new ones arrive
    st_key(SDLK_BACKSPACE, SDL_KMOD_NONE);
    ST_CHECK(A.search_due != 0 && p->L == before && p->count == 1);
    search_apply(p, false);
    ST_CHECK(p->L == before && p->count == 1 && p->L_next);
    ST_CHECK(st_wait_cond(c_search_done, 3000));
    ST_CHECK(p->L != before && p->count == 2);
    // one letter doesn't auto-search
    st_key(SDLK_A, SDL_KMOD_CTRL);
    st_type("x");
    search_apply(p, false);
    ST_CHECK(!strcmp(p->search_q, "ne"));
    // wildcards
    st_key(SDLK_A, SDL_KMOD_CTRL);
    st_type("*.log");
    search_apply(p, true);
    ST_CHECK(st_wait_cond(c_search_done, 3000));
    ST_CHECK(p->count == 2);
    // Alt+R: back to filtering this folder only; the .log files are in subfolders
    st_key(SDLK_R, SDL_KMOD_ALT);
    st_wait();
    ST_CHECK(!A.search_recursive && !p->L->is_search && p->count == 0);
    st_key(SDLK_A, SDL_KMOD_CTRL);
    st_type("*.md");
    ST_CHECK(p->count == 1 && !strcmp(p->L->entries[p->order[0]].name, "readme.md"));
    st_key(SDLK_R, SDL_KMOD_ALT);
    ST_CHECK(st_wait_cond(c_search_done, 3000));
    ST_CHECK(p->L->is_search && p->count == 1); // still just readme.md
    st_key(SDLK_A, SDL_KMOD_CTRL);
    st_type("*.log");
    search_apply(p, true);
    ST_CHECK(st_wait_cond(c_search_done, 3000));
    ST_CHECK(p->count == 2);
    // Enter moves focus to results; Esc (no selection) leaves search
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    ST_CHECK(!A.search_pane && p->L->is_search);
    sel_clear(p);
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(!p->L->is_search && !p->search_q[0] && p->count == 4);
    A.search_recursive = false;

    // live refresh: a file created by someone else shows up, without the list ever going blank
    st_write(M4_BASE "/new_ext.txt", 5);
    bool blank = false;
    for (int k = 0; k < 600 && !c_has_new_ext(); k++) {
        st_frames(1);
        if (!p->order) blank = true;
        SDL_Delay(5);
    }
    ST_CHECK(c_has_new_ext());
    ST_CHECK(!blank);

    // the folder we're in is deleted -> jump to the nearest existing parent
    pane_navigate(p, M4_BASE "/alpha/beta/gamma/deepfolder", true);
    st_wait();
    st_rmrf(M4_BASE "/alpha/beta");
    ST_CHECK(st_wait_cond(c_went_up, 3000));

    // folder size cache: unchanged folders are reused, changed ones (new mtime) read again
    mkdir(M4_BASE "/du", 0755);
    mkdir(M4_BASE "/du/a", 0755);
    mkdir(M4_BASE "/du/a/b", 0755);
    st_write(M4_BASE "/du/top.bin", 10);
    st_write(M4_BASE "/du/a/mid.bin", 100);
    st_write(M4_BASE "/du/a/b/deep.bin", 1000);
    st_age(M4_BASE "/du/a/b", 3600), st_age(M4_BASE "/du/a", 3600), st_age(M4_BASE "/du", 3600);
    ST_CHECK(st_du(M4_BASE "/du") == 1110);
    ST_CHECK(st_du(M4_BASE "/du") == 1110); // from the cache
    st_write(M4_BASE "/du/a/b/new.bin", 5); // b's mtime changes: b is read again, du and a come from the cache
    ST_CHECK(st_du(M4_BASE "/du") == 1115);
    ST_CHECK(st_du(M4_BASE "/du/a") == 1105);
    st_age(M4_BASE "/du/a/b", 1800);
    ST_CHECK(st_du(M4_BASE "/du") == 1115);
    st_write(M4_BASE "/du/a/b/deep.bin", 2000); // rewritten in place: b's mtime stays, the cache can't tell...
    ST_CHECK(st_du(M4_BASE "/du") == 1115);
    dirsize_cache_expire(); // ...until F5
    ST_CHECK(st_du(M4_BASE "/du") == 2115);
    st_write(M4_BASE "/du/a/b/new2.bin", 7); // b's mtime is now: a recent mtime is never trusted...
    ST_CHECK(st_du(M4_BASE "/du") == 2122);
    st_write(M4_BASE "/du/a/b/new2.bin", 9); // ...so an in-place change right after it is still seen
    ST_CHECK(st_du(M4_BASE "/du") == 2124);
    st_rmrf(M4_BASE "/du");

    // folder sizes, and sorting folders by size; hidden folders are only measured once they are shown
    mkdir(M4_BASE "/.hid", 0755);
    st_write(M4_BASE "/.hid/x.bin", 700);
    bool hid0 = A.show_hidden;
    A.show_hidden = false;
    pane_navigate(p, M4_BASE, true);
    st_wait();
    ST_CHECK(st_wait_cond(c_sizes_done, 5000));
    ST_CHECK(st_dir_size(p, "big") == 2u << 20 && st_dir_size(p, "small") == 1024 && st_dir_size(p, "alpha") == 500);
    st_frames(10);
    ST_CHECK(!c_hidden_size_known());
    A.show_hidden = true;
    pane_resort(p);
    ST_CHECK(st_wait_cond(c_hidden_size_known, 5000));
    bool known;
    ST_CHECK(st_entry_dir_size(p, ".hid", &known) == 700);
    A.show_hidden = hid0;
    pane_resort(p);
    st_wait();
    st_shot("sizes");
    pane_set_sort(p, SORT_SIZE);
    st_wait();
    ST_CHECK(!strcmp(p->L->entries[p->order[0]].name, "big"));
    // a reload (live refresh) keeps showing the folder sizes while they're measured again: no blank Size column
    {
        dirsize_cache_expire(); // measure for real, so only the carried-over sizes can fill the first frame
        Listing *was = p->L;
        pane_refresh(p);
        bool seen = false, kept = true;
        for (u64 t0 = now_ns(); !seen && now_ns() - t0 < 5000000000ull;) {
            main_drain();
            pane_poll(p);
            if (p->L != was && atomic_load(&p->L->state) == LST_COMPLETE) {
                seen = true;
                kept = st_dir_size(p, "big") == 2u << 20 && st_dir_size(p, "small") == 1024;
            } else SDL_Delay(1);
        }
        ST_CHECK(seen && kept && !p->L_prev);
        st_wait();
        ST_CHECK(st_wait_cond(c_sizes_done, 5000) && !strcmp(p->L->entries[p->order[0]].name, "big"));
    }
    // going into a folder and back up: the old frame is held until the new folder is listed and sorted (no blank
    // list in between), and the folder sizes just measured are in its first frame
    {
        pane_navigate(p, M4_BASE "/big", true);
        ST_CHECK(p->nav_t && nav_hold_ns() > 0);
        st_wait();
        ST_CHECK(nav_hold_ns() == 0 && !p->nav_t);
        pane_navigate(p, M4_BASE, true);
        bool seen = false, kept = false;
        for (u64 t0 = now_ns(); !seen && now_ns() - t0 < 5000000000ull;) {
            main_drain();
            pane_poll(p);
            if (atomic_load(&p->L->state) == LST_COMPLETE && p->order) {
                seen = true;
                kept = st_dir_size(p, "big") == 2u << 20 && st_dir_size(p, "small") == 1024;
            } else SDL_Delay(1);
        }
        ST_CHECK(seen && kept);
        st_wait();
        ST_CHECK(nav_hold_ns() == 0 && !strcmp(p->L->entries[p->order[0]].name, "big"));
    }
    pane_set_sort(p, SORT_NAME);
    st_wait();
    // network folders (pretend GVfs): an SFTP one asks du on the server (a stand-in ssh running it here), one whose
    // server can't be reached and an SMB one are walked through the mount; none stays at "..." forever
    {
#define M4_SFTP M4_BASE "/gvfs/sftp:host=vm,user=me,port=2222"
        mkdir_p(M4_SFTP "/srv/da'ta/a b", 0755);
        mkdir_p(M4_SFTP "/srv/da'ta/empty", 0755);
        st_write(M4_SFTP "/srv/da'ta/a b/x.bin", 3000);
        mkdir_p(M4_BASE "/gvfs/sftp:host=down/srv/c", 0755);
        st_write(M4_BASE "/gvfs/sftp:host=down/srv/c/y.bin", 400);
        mkdir_p(M4_BASE "/gvfs/smb-share:server=nas,share=x/d", 0755);
        st_write(M4_BASE "/gvfs/smb-share:server=nas,share=x/d/z.bin", 500);
        FILE *f = fopen(M4_BASE "/ssh", "w");
        fputs("#!/bin/sh\necho \"ssh $*\" >> " M4_BASE "/ssh.log\nfor a; do last=$a; case $a in down) exit 255;; esac; done\n"
              "exec sh -c \"$(printf '%s' \"$last\" | sed \"s#sh '/srv/#sh '" M4_SFTP "/srv/#\")\"\n", f);
        fclose(f);
        chmod(M4_BASE "/ssh", 0755);
        setenv("FE_SSH", M4_BASE "/ssh", 1);
        setenv("FE_GVFS_DIR", M4_BASE "/gvfs", 1);
        pane_navigate(p, M4_SFTP "/srv/da'ta", true);
        st_wait();
        ST_CHECK(st_wait_cond(c_sizes_done, 5000));
        u64 ab = st_dir_size(p, "a b"), em = st_dir_size(p, "empty");
        ST_CHECK(ab >= 3000 && ab < 3000 + 65536 && em < 65536); // (du counts the folders themselves too)
        char log[4096];
        st_read_text(M4_BASE "/ssh.log", log, sizeof log);
        ST_CHECK(strstr(log, "ssh -o BatchMode=yes ") && strstr(log, " -p 2222 -- me@vm sh -c ") && strstr(log, " sh '/srv/da'\\''ta'"));
        pane_navigate(p, M4_BASE "/gvfs/sftp:host=down/srv", true);
        st_wait();
        ST_CHECK(st_wait_cond(c_sizes_done, 5000) && st_dir_size(p, "c") == 400);
        st_read_text(M4_BASE "/ssh.log", log, sizeof log);
        ST_CHECK(strstr(log, "-- down sh -c ") != NULL);
        pane_navigate(p, M4_BASE "/gvfs/smb-share:server=nas,share=x", true);
        st_wait();
        ST_CHECK(st_wait_cond(c_sizes_done, 5000) && st_dir_size(p, "d") == 500);
        unsetenv("FE_GVFS_DIR");
        unsetenv("FE_SSH");
        pane_navigate(p, M4_BASE, true);
        st_wait();
#undef M4_SFTP
    }

    st_rmrf(M4_BASE);
    if (st_fail) { fprintf(stderr, "SELFTEST-M4: %d failure(s)\n", st_fail); return 1; }
    printf("SELFTEST-M4 OK\n");
    fflush(stdout);
    return 0;
}

// --- self-test for M5: preview, thumbnails, zoom, pins, settings, session --

#define M5_BASE "/tmp/fe-m5-test"

static void st_setup_m5(void) {
    st_rmrf(M5_BASE);
    st_rmrf("/tmp/fe-selftest-config");
    st_rmrf("/tmp/fe-selftest-cache");
    mkdir(M5_BASE, 0755);
    mkdir(M5_BASE "/sub", 0755);
    mkdir(M5_BASE "/sub/inner", 0755);
    st_write(M5_BASE "/sub/a.bin", 10);
    st_write(M5_BASE "/sub/b.bin", 20);
    FILE *f = fopen(M5_BASE "/notes.txt", "w");
    for (int i = 1; i <= 50; i++) fprintf(f, "line %d\tof the notes\n", i);
    fclose(f);
    f = fopen(M5_BASE "/logo.svg", "w");
    fprintf(f, "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64'><rect width='64' height='64' fill='#4c9bff'/></svg>\n");
    fclose(f);
}

static void st_make_bmp(const char *path, int w, int h) {
    SDL_Surface *s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    SDL_FillSurfaceRect(s, NULL, SDL_MapSurfaceRGBA(s, 230, 120, 40, 255));
    SDL_Rect r = {w / 4, h / 4, w / 2, h / 2};
    SDL_FillSurfaceRect(s, &r, SDL_MapSurfaceRGBA(s, 40, 120, 230, 255));
    SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
}

static bool c_pv_ready(void) {
    Pane *p = active_pane();
    char cur[PATH_MAX];
    path_join(cur, sizeof cur, p->path, st_cursor_name(p));
    return A.pv && !strcmp(A.pv_want, cur) && !strcmp(A.pv->path, A.pv_want) && !A.pv_due;
}

static int run_selftest_m5(void) {
    st_make_bmp(M5_BASE "/photo.bmp", 640, 480);
    pane_refresh(active_pane());
    st_wait();
    Pane *p = active_pane();

    // preview panel: Ctrl+I, image with original dimensions, downscaled texture
    st_key(SDLK_I, SDL_KMOD_CTRL);
    ST_CHECK(A.preview_on && A.r_preview.w > 0 && A.r_content.w < A.W - A.r_sidebar.w);
    st_select(p, "photo.bmp");
    ST_CHECK(st_wait_cond(c_pv_ready, 3000));
    ST_CHECK(A.pv->kind == PV_IMAGE && A.pv->src_w == 640 && A.pv->src_h == 480 && A.pv_tex);
    st_shot("preview-image");
    // ...with what Properties shows: dates, owner, group, permissions, and the media details read on a thread
    for (int k = 0; k < 300 && !(A.pv_media && atomic_load(&A.pv_media->done)); k++) { st_frames(1); SDL_Delay(5); }
    ST_CHECK(A.pv_media && !strcmp(A.pv_media->path, M5_BASE "/photo.bmp") && atomic_load(&A.pv_media->done) && A.pv_media->w == 640);
    st_frames(1);
    { // a short window: the rows don't fit, the wheel scrolls the panel (not the list); another item starts at the top
        int h0 = A.H;
        A.H = (int)roundf(S(380));
        st_frames(2);
        ST_CHECK(A.pv_content_h > A.r_preview.h && A.pv_scroll == 0);
        i64 top0 = (i64)p->scroll;
        SDL_MouseWheelEvent wv;
        SDL_zero(wv);
        wv.y = -3;
        wv.mouse_x = A.r_preview.x + A.r_preview.w * 0.5f;
        wv.mouse_y = A.r_preview.y + A.r_preview.h * 0.5f;
        on_wheel(&wv);
        st_settle();
        ST_CHECK(A.pv_scroll > 0 && A.pv_scroll <= A.pv_content_h - A.r_preview.h + 1 && (i64)p->scroll == top0);
        st_shot("preview-scrolled");
        st_select(p, "notes.txt");
        st_frames(2);
        ST_CHECK(A.pv_scroll == 0);
        st_select(p, "photo.bmp");
        A.H = h0;
        st_frames(2);
    }
    // text
    st_select(p, "notes.txt");
    ST_CHECK(st_wait_cond(c_pv_ready, 3000));
    ST_CHECK(A.pv->kind == PV_TEXT && A.pv->text && !strncmp(A.pv->text, "line 1\t", 7));
    st_shot("preview-text");
    ST_CHECK(!A.pv_media); // (no media details for text)
    // folder
    st_select(p, "sub");
    ST_CHECK(st_wait_cond(c_pv_ready, 3000));
    ST_CHECK(A.pv->kind == PV_DIR && A.pv->n_dirs == 1 && A.pv->n_files == 2);
    // SVG through an installed thumbnailer (if any), cached for next time
    st_select(p, "logo.svg");
    ST_CHECK(st_wait_cond(c_pv_ready, 10000));
    if (A.pv->kind == PV_IMAGE && A.pv->from_thumb) {
        char dir[PATH_MAX];
        cache_dir(dir, sizeof dir);
        DIR *d = opendir(dir);
        int n = 0;
        struct dirent *de;
        while (d && (de = readdir(d))) n += strstr(de->d_name, ".png") != NULL;
        if (d) closedir(d);
        ST_CHECK(n >= 1);
        printf("svg thumbnail via installed thumbnailer: ok\n");
    } else { // none installed: drawn by nanosvg
        ST_CHECK(A.pv->kind == PV_IMAGE && !A.pv->from_thumb && A.pv->src_w == 64 && A.pv->src_h == 64 && A.pv_tex); // (pixels went to the texture)
        printf("svg preview via nanosvg (no thumbnailer output)\n");
    }
    { // nanosvg previews directly: fitted size, colours, transparency, viewBox only, what's refused
        const char *sp = "/tmp/fe-m5-svg-test.svg"; // (outside the listed folder: no refresh mid-test)
        int w, h, sw, sh;
        st_write_text(sp, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\"><rect width=\"100\" height=\"100\" fill=\"#ff0000\"/></svg>");
        u8 *px = svg_preview_pixels(sp, 64, &w, &h, &sw, &sh);
        ST_CHECK(px && w == 64 && h == 32 && sw == 200 && sh == 100);
        if (px) {
            size_t red = ((size_t)16 * w + 16) * 4, clear = ((size_t)16 * w + 48) * 4;
            ST_CHECK(px[red] > 240 && px[red + 3] > 240 && !px[clear + 3]);
        }
        free(px);
        st_write_text(sp, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 20\"><rect width=\"10\" height=\"20\" fill=\"blue\"/></svg>");
        px = svg_preview_pixels(sp, 64, &w, &h, &sw, &sh);
        ST_CHECK(px && w == 32 && h == 64);
        free(px);
        // currentColor (local nanosvg patch): the color property, inherited from the root; black by default
        st_write_text(sp, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\" color=\"#00ff00\"><rect width=\"4\" height=\"8\" fill=\"currentColor\"/><rect x=\"4\" width=\"4\" height=\"8\" style=\"color:#0000ff\" fill=\"currentColor\"/></svg>");
        px = svg_preview_pixels(sp, 8, &w, &h, &sw, &sh);
        ST_CHECK(px && px[(4 * 8 + 1) * 4 + 1] > 240 && px[(4 * 8 + 1) * 4] < 16 && px[(4 * 8 + 6) * 4 + 2] > 240 && px[(4 * 8 + 6) * 4 + 1] < 16);
        free(px);
        st_write_text(sp, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\"><rect width=\"8\" height=\"8\" fill=\"currentColor\"/></svg>");
        px = svg_preview_pixels(sp, 8, &w, &h, &sw, &sh);
        ST_CHECK(px && px[(4 * 8 + 4) * 4] < 16 && px[(4 * 8 + 4) * 4 + 3] > 240);
        free(px);
        st_write_text(sp, "not an svg");
        ST_CHECK(!svg_preview_pixels(sp, 64, &w, &h, &sw, &sh));
        st_write_text(sp, "<svg width=\"1e999\" height=\"10\"></svg>");
        ST_CHECK(!svg_preview_pixels(sp, 64, &w, &h, &sw, &sh));
        st_write_text(sp, "<svg width=\"0\" height=\"0\"></svg>");
        ST_CHECK(!svg_preview_pixels(sp, 64, &w, &h, &sw, &sh));
        ST_CHECK(!svg_preview_pixels(sp, 50000, &w, &h, &sw, &sh));
        unlink(sp);
    }
    // several selected -> summary
    st_key(SDLK_A, SDL_KMOD_CTRL);
    st_frames(2);
    ST_CHECK(p->sel_count > 1);
    st_shot("preview-multi");
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    // drag the divider wider
    f32 before_w = A.r_preview.w;
    f32 dx = A.r_pdiv.x + A.r_pdiv.w * 0.5f;
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, dx, A.r_pdiv.y + S(100), SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_MOTION, dx + S(60), A.r_pdiv.y + S(100), 0, 0, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(fabsf(A.r_preview.w - MAX(before_w - S(60), roundf(S(220)))) < S(4)); // min width 220
    // Quick Look: Space shows the file large over the window (asked for at the window's size), the arrows go to the
    // previous / next file, other keys do nothing, Space or Esc closes it; the panel stays. Ctrl+I hides the panel.
    st_select(p, "photo.bmp");
    st_key(SDLK_SPACE, SDL_KMOD_NONE);
    on_text(" ");
    ST_CHECK(A.ql_on && A.preview_on);
    ST_CHECK(st_wait_cond(c_pv_ready, 3000) && A.pv->kind == PV_IMAGE && A.pv->w == 640 && A.pv->h == 480 && A.pv_tex);
    st_frames(2);
    st_shot("quicklook-image");
    i64 c0 = p->cursor;
    st_key(SDLK_LEFT, SDL_KMOD_NONE);
    ST_CHECK(A.ql_on && p->cursor == c0 - 1);
    st_key(SDLK_RIGHT, SDL_KMOD_NONE);
    ST_CHECK(A.ql_on && p->cursor == c0);
    st_key(SDLK_DELETE, SDL_KMOD_NONE); // (nothing happens to the file while it's up)
    st_wait_ops();
    ST_CHECK(A.ql_on && st_exists(M5_BASE "/photo.bmp"));
    st_key(SDLK_SPACE, SDL_KMOD_NONE);
    on_text(" ");
    ST_CHECK(!A.ql_on && A.preview_on);
    st_select(p, "notes.txt");
    on_text(" ");
    ST_CHECK(A.ql_on && st_wait_cond(c_pv_ready, 3000) && A.pv->kind == PV_TEXT);
    st_frames(2);
    st_shot("quicklook-text");
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    ST_CHECK(!A.ql_on);
    st_key(SDLK_I, SDL_KMOD_CTRL);
    ST_CHECK(!A.preview_on && !A.pv_media);

    // zoom
    int px0 = A.font.px;
    st_key(SDLK_EQUALS, SDL_KMOD_CTRL);
    ST_CHECK(fabsf(A.zoom - 1.1f) < 0.01f && A.font.px > px0);
    st_key(SDLK_0, SDL_KMOD_CTRL);
    ST_CHECK(A.zoom == 1.f && A.font.px == px0);

    // pin / unpin the folder under the cursor
    int places0 = A.nplaces;
    st_select(p, "sub");
    st_key(SDLK_D, SDL_KMOD_CTRL);
    ST_CHECK(pin_index(M5_BASE "/sub") >= 0 && A.nplaces == places0 + 1);
    int idx = -1;
    for (int i = 0; i < A.nplaces; i++)
        if (A.places[i].kind == PK_PINNED) idx = i;
    ST_CHECK(idx >= 0 && idx < A.nplaces_user);
    st_shot("pinned");
    menu_open_place(idx, A.r_sidebar.x + S(40), S(200));
    int rm = -1;
    for (int i = 0; i < A.menu.n; i++)
        if (A.menu.items[i].act == ACT_QA_REMOVE) rm = i;
    ST_CHECK(A.menu.open && rm >= 0);
    menu_activate(rm);
    ST_CHECK(pin_index(M5_BASE "/sub") < 0 && A.nplaces == places0);

    // settings round trip + session
    A.show_hidden = true;
    A.folder_sizes = false;
    A.search_recursive = true;
    A.preview_on = true;
    A.zoom = 1.3f;
    pane_set_sort(p, SORT_SIZE);
    pin_toggle(M5_BASE "/sub");
    tab_new(M5_BASE "/sub", A.ntabs, true);
    st_wait();
    Panel *np = panel_split(A.ap, ZONE_RIGHT, NULL, M5_BASE "/sub/inner");
    st_wait();
    ST_CHECK(np && A.npanels == 2);
    A.root->ratio = 0.4f;
    A.window_buttons = A.brand_logo = false;
    A.brand_name = BRAND_CUSTOM;
    snprintf(A.brand_text, sizeof A.brand_text, "My files");
    A.tab_style = TABS_PILLS;
    A.compress_fmt = ARC_TXZ, A.compress_level = ARC_LVL_CUSTOM, A.compress_num = 4;
    settings_save();
    int ntabs0 = A.ntabs;
    settings_load(); // resets to defaults first, then reads the file
    ST_CHECK(A.show_hidden && !A.folder_sizes && A.search_recursive && A.preview_on);
    ST_CHECK(!A.window_buttons && !A.brand_logo && A.brand_name == BRAND_CUSTOM && !strcmp(A.brand_text, "My files"));
    A.window_buttons = A.brand_logo = true;
    ST_CHECK(A.tab_style == TABS_PILLS);
    ST_CHECK(A.compress_fmt == ARC_TXZ && A.compress_level == ARC_LVL_CUSTOM && A.compress_num == 4);
    A.brand_name = BRAND_APP;
    A.brand_text[0] = 0;
    A.tab_style = TABS_CARDS;
    FolderView *fv5 = folder_view_find(M5_BASE, true); // sort order is remembered per folder
    ST_CHECK(fabsf(A.zoom - 1.3f) < 0.01f && fv5 && fv5->sort == SORT_SIZE && fv5->desc);
    ST_CHECK(pin_index(M5_BASE "/sub") >= 0 && A.nsess_tabs == ntabs0 && !strncmp(A.sess_layout, "H0.400(P0,P1)", 13));
    layout_reset();
    ST_CHECK(session_restore());
    ST_CHECK(A.ntabs == ntabs0 && A.npanels == 2 && A.root->kind == NODE_HSPLIT && fabsf(A.root->ratio - 0.4f) < 0.01f);
    Panel *rp = A.root->b->panel;
    ST_CHECK(rp->cur && !strcmp(rp->cur->pane[0].path, M5_BASE "/sub/inner") && A.ap == rp);
    st_wait();
    A.zoom = 1.f;
    A.scale = A.dpi_scale;
    reload_fonts();
    st_frames(2);

    st_rmrf(M5_BASE);
    if (st_fail) { fprintf(stderr, "SELFTEST-M5: %d failure(s)\n", st_fail); return 1; }
    printf("SELFTEST-M5 OK\n");
    fflush(stdout);
    return 0;
}


// --- self-test for M6: File Pilot-style panels -----------------------------

#define M6_BASE "/tmp/fe-m6-test"

static void st_setup_m6(void) {
    st_rmrf(M6_BASE);
    mkdir(M6_BASE, 0755);
    const char *dirs[] = {"/a", "/b", "/c", "/d"};
    for (size_t i = 0; i < ARRAY_COUNT(dirs); i++) {
        char p[PATH_MAX];
        snprintf(p, sizeof p, M6_BASE "%s", dirs[i]);
        mkdir(p, 0755);
    }
    st_write(M6_BASE "/a/file1.txt", 100);
    st_write(M6_BASE "/a/file2.txt", 200);
}

static void st_drag_tab(Tab *t, f32 x, f32 y, SDL_Keymod mod) {
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, t->r.x + S(30), t->r.y + t->r.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_MOTION, t->r.x + S(60), t->r.y + t->r.h * 0.5f + S(4), 0, 0, mod);
    st_mouse(SDL_EVENT_MOUSE_MOTION, x, y, 0, 0, mod);
    SDL_SetModState(mod);
    on_mouse_up();
    SDL_SetModState(SDL_KMOD_NONE);
    st_wait();
}

static int run_selftest_m6(void) {
    st_wait();
    Panel *p0 = A.ap;
    ST_CHECK(A.npanels == 1 && A.root->kind == NODE_PANEL);
    { // tab bar hover states (FE_SHOTS): inactive tab, + button
        int nt = A.ntabs;
        st_key(SDLK_T, SDL_KMOD_CTRL); st_wait();
        Tab *t0 = panel_tab(A.ap, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, t0->r.x + t0->r.w * 0.5f, t0->r.y + t0->r.h * 0.5f, 0, 0, 0);
        st_frames(2);
        st_shot("tab-hover");
        Rect pr = A.ap->plus;
        st_mouse(SDL_EVENT_MOUSE_MOTION, pr.x + pr.w * 0.5f, pr.y + pr.h * 0.5f, 0, 0, 0);
        st_frames(2);
        st_shot("tab-plus-hover");
        st_mouse(SDL_EVENT_MOUSE_MOTION, A.r_content.x + S(300), A.r_content.y + S(300), 0, 0, 0);
        st_key(SDLK_W, SDL_KMOD_CTRL); st_wait();
        ST_CHECK(A.ntabs == nt);
    }
    { // Tab style Pills: apart, centred on the strip, + and split still after them; a click still switches tabs
        int nt = A.ntabs;
        opt_choice_set(OPT_TAB_STYLE, TABS_PILLS);
        st_key(SDLK_T, SDL_KMOD_CTRL); st_wait();
        st_key(SDLK_T, SDL_KMOD_CTRL); st_wait();
        Panel *pn = A.ap;
        Tab *a = panel_tab(pn, 0), *b = panel_tab(pn, 1), *c = panel_tab(pn, -1);
        ST_CHECK(a && b && c && a != c && b->r.x >= a->r.x + a->r.w + S(2) && c->r.x + c->r.w <= pn->plus.x);
        ST_CHECK(pn->plus.x + pn->plus.w <= pn->splitbtn.x && a->r.y > pn->bar.y && a->r.y + a->r.h < pn->bar.y + pn->bar.h);
        f32 left = a->r.x - pn->bar.x, right = pn->bar.x + pn->bar.w - (c->r.x + c->r.w);
        ST_CHECK(fabsf(left - right) < S(2) || c->r.x + c->r.w > pn->plus.x - S(20)); // centred unless + or split stop it
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, a->r.x + a->r.w * 0.5f, a->r.y + a->r.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, a->r.x + a->r.w * 0.5f, a->r.y + a->r.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        st_wait();
        ST_CHECK(pn->cur == a);
        st_mouse(SDL_EVENT_MOUSE_MOTION, b->r.x + b->r.w * 0.5f, b->r.y + b->r.h * 0.5f, 0, 0, 0);
        st_frames(2);
        st_shot("tab-pills");
        int th = g_theme_cur;
        g_theme_cur = theme_find("Light");
        theme_apply();
        st_frames(2);
        st_shot("tab-pills-light");
        g_theme_cur = th;
        theme_apply();
        st_mouse(SDL_EVENT_MOUSE_MOTION, A.r_content.x + S(300), A.r_content.y + S(300), 0, 0, 0);
        while (A.ntabs > nt) { st_key(SDLK_W, SDL_KMOD_CTRL); st_wait(); }
        opt_choice_set(OPT_TAB_STYLE, TABS_CARDS);
        ST_CHECK(A.ntabs == nt && panel_tab(A.ap, 0)->r.y == A.ap->bar.y + roundf(S(6)));
    }

    // split right (Ctrl+\) then split that one down (Ctrl+Shift+\): nested tree
    st_key(SDLK_BACKSLASH, SDL_KMOD_CTRL); st_wait();
    Panel *p1 = A.ap;
    ST_CHECK(A.npanels == 2 && p1 != p0 && A.root->kind == NODE_HSPLIT);
    pane_navigate(active_pane(), M6_BASE "/b", true); st_wait();
    st_key(SDLK_BACKSLASH, SDL_KMOD_CTRL | SDL_KMOD_SHIFT); st_wait();
    Panel *p2 = A.ap;
    ST_CHECK(A.npanels == 3 && A.root->b->kind == NODE_VSPLIT && A.root->b->b->panel == p2);
    st_shot("split-active");
    pane_navigate(active_pane(), M6_BASE "/c", true); st_wait();
    st_shot("panels");
    // Tab cycles panels in reading order; each panel shows its own tab bar
    st_key(SDLK_TAB, SDL_KMOD_NONE);
    ST_CHECK(A.ap == p0);
    st_key(SDLK_TAB, SDL_KMOD_NONE);
    ST_CHECK(A.ap == p1);
    st_key(SDLK_TAB, SDL_KMOD_SHIFT);
    ST_CHECK(A.ap == p0);
    ST_CHECK(p0->bar.y == p1->bar.y && p2->bar.y > p1->bar.y);

    { // a click on another panel's toolbar (not on a button) makes that panel the active one
        Rect tb = p1->cur->pane[0].ly.toolbar;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, tb.x + tb.w * 0.5f, tb.y + S(2), SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, tb.x + tb.w * 0.5f, tb.y + S(2), SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(A.ap == p1);
        panel_activate(p0);
    }
    { // narrow panes: back / forward / up go first, then sort, then filter + menu; the path keeps the rest
        f32 ratio = A.root->ratio;
        A.root->ratio = S(220) / A.root->r.w;
        st_frames(1);
        PaneLayout *ly = &p0->cur->pane[0].ly;
        ST_CHECK(ly->area.w < S(240) && ly->back.w == 0 && ly->fwd.w == 0 && ly->up.w == 0 && ly->sortbtn.w > 0);
        ST_CHECK(ly->pathbar.w > 0 && ly->pathbar.x + ly->pathbar.w <= ly->sortbtn.x && ly->sortbtn.x + ly->sortbtn.w <= ly->filterbtn.x);
        st_shot("split-narrow");
        Pane *np = &p0->cur->pane[0];
        pane_navigate(np, M6_BASE, true); st_wait();
        pane_navigate(np, M6_BASE "/a", true); st_wait();
        st_key(SDLK_LEFT, SDL_KMOD_ALT); st_wait(); // the keys still work without the buttons
        ST_CHECK(!strcmp(np->path, M6_BASE));
        st_key(SDLK_RIGHT, SDL_KMOD_ALT); st_wait();
        ST_CHECK(!strcmp(np->path, M6_BASE "/a"));
        st_key(SDLK_UP, SDL_KMOD_ALT); st_wait();
        ST_CHECK(!strcmp(np->path, M6_BASE));
        Rect area = ly->area;
        layout_pane(np, (Rect){area.x, area.y, S(110), area.h});
        ST_CHECK(ly->back.w == 0 && ly->sortbtn.w == 0 && ly->filterbtn.w > 0 && ly->morebtn.w > 0);
        ST_CHECK(ly->pathbar.w > 0 && ly->pathbar.x + ly->pathbar.w <= ly->filterbtn.x);
        layout_pane(np, (Rect){area.x, area.y, S(70), area.h});
        ST_CHECK(ly->back.w == 0 && ly->sortbtn.w == 0 && ly->filterbtn.w == 0 && ly->morebtn.w == 0);
        ST_CHECK(ly->pathbar.w > 0 && ly->pathbar.x + ly->pathbar.w <= area.x + S(70));
        // hidden buttons can't be clicked: where back was, the path bar starts
        f32 bx = area.x + S(20), by = ly->pathbar.y + ly->pathbar.h * 0.5f;
        ST_CHECK(!in_rect(ly->back, bx, by) && !in_rect(ly->filterbtn, area.x + S(50), by));
        A.root->ratio = ratio;
        st_frames(1);
        ST_CHECK(p0->cur->pane[0].ly.back.w > 0 && p0->cur->pane[0].ly.sortbtn.w > 0);
    }

    // tabs within a panel: Ctrl+T adds to the active panel only; Ctrl+1/Ctrl+9
    st_key(SDLK_T, SDL_KMOD_CTRL); st_wait();
    pane_navigate(active_pane(), M6_BASE "/d", true); st_wait();
    ST_CHECK(panel_ntabs(p0) == 2 && panel_ntabs(p1) == 1 && panel_ntabs(p2) == 1);
    st_key(SDLK_1, SDL_KMOD_CTRL);
    ST_CHECK(A.ap == p0 && p0->cur == panel_tab(p0, 0));
    st_key(SDLK_9, SDL_KMOD_CTRL);
    ST_CHECK(p0->cur == panel_tab(p0, 1) && !strcmp(active_pane()->path, M6_BASE "/d"));

    // drag p0's "d" tab onto p2's tab bar -> moved there
    Tab *td = p0->cur;
    st_drag_tab(td, p2->bar.x + p2->bar.w - S(80), p2->bar.y + p2->bar.h * 0.5f, 0);
    ST_CHECK(td->panel == p2 && panel_ntabs(p2) == 2 && panel_ntabs(p0) == 1 && A.ap == p2 && p2->cur == td);
    // drag it to the left edge of p1 -> new split inside p1's slot
    st_drag_tab(td, p1->area.x + S(20), p1->area.y + p1->area.h * 0.5f, 0);
    ST_CHECK(A.npanels == 4 && td->panel != p2 && td->panel != p1 && panel_ntabs(td->panel) == 1);
    Panel *p3 = td->panel;
    Node *n3 = node_find(A.root, p3);
    ST_CHECK(n3 && n3->parent && n3->parent->kind == NODE_HSPLIT && n3->parent->a == n3);
    // Ctrl+drag duplicates into another panel's centre
    int nt = A.ntabs;
    st_drag_tab(td, p0->area.x + p0->area.w * 0.5f, p0->area.y + p0->area.h * 0.5f, SDL_KMOD_CTRL);
    ST_CHECK(A.ntabs == nt + 1 && panel_ntabs(p3) == 1 && panel_ntabs(p0) == 2 && !strcmp(p0->cur->pane[0].path, M6_BASE "/d"));
    // moving a panel's only tab into another panel closes the empty panel
    st_drag_tab(td, p0->area.x + p0->area.w * 0.5f, p0->area.y + p0->area.h * 0.5f, 0);
    ST_CHECK(A.npanels == 3 && !panel_alive(p3) && td->panel == p0 && panel_ntabs(p0) == 3);

    // nested divider drag (the vertical split on the right)
    Node *vs = node_find(A.root, p2)->parent;
    ST_CHECK(vs->kind == NODE_VSPLIT);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, vs->div.x + vs->div.w * 0.5f, vs->div.y + vs->div.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_MOTION, vs->div.x + vs->div.w * 0.5f, vs->r.y + vs->r.h * 0.3f, 0, 0, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(fabsf(vs->ratio - 0.3f) < 0.03f);

    // tab context menu: close others; reopen closed tab
    panel_activate(p0);
    Tab *keep = p0->cur;
    menu_open_tab(keep, keep->r.x + S(20), keep->r.y + S(20));
    ST_CHECK(A.menu.open);
    int ci = -1;
    for (int i = 0; i < A.menu.n; i++)
        if (A.menu.items[i].act == ACT_TAB_CLOSE_OTHERS) ci = i;
    menu_activate(ci);
    ST_CHECK(panel_ntabs(p0) == 1 && p0->cur == keep && A.nclosed >= 2);
    st_key(SDLK_T, SDL_KMOD_CTRL | SDL_KMOD_SHIFT); st_wait();
    ST_CHECK(panel_ntabs(p0) == 2);

    // copy the selection to the previously active panel (Shift+F5)
    panel_activate(p1); // p1 shows /b
    panel_activate(p0);
    pane_navigate(active_pane(), M6_BASE "/a", true); st_wait();
    st_select(active_pane(), "file1.txt");
    st_key(SDLK_F5, SDL_KMOD_SHIFT);
    st_wait_ops();
    ST_CHECK(st_size(M6_BASE "/b/file1.txt") == 100);

    // drop files onto a tab -> into that tab's folder
    panel_activate(p0);
    st_select(active_pane(), "file2.txt");
    f32 rowy = st_row_y(active_pane(), "file2.txt");
    Tab *tc = p2->cur; // shows /c
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, active_pane()->ly.col_name + S(40), rowy, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_MOTION, tc->r.x + S(40), tc->r.y + tc->r.h * 0.5f + S(20), 0, 0, 0);
    st_mouse(SDL_EVENT_MOUSE_MOTION, tc->r.x + S(40), tc->r.y + tc->r.h * 0.5f, 0, 0, 0);
    ST_CHECK(A.drag == DRAG_ITEMS && A.drop_tab == tc && !strcmp(A.drop_dir, tc->pane[0].path));
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    st_wait_ops();
    ST_CHECK(st_exists(M6_BASE "/c/file2.txt") && !st_exists(M6_BASE "/a/file2.txt"));

    { // drag folders onto a panel edge -> opened in a new split there; onto the empty part of a tab bar -> new tabs
        panel_activate(p0);
        pane_navigate(active_pane(), M6_BASE, true); st_wait();
        Pane *src = active_pane();
        int np = A.npanels, nt = A.ntabs;
        st_select(src, "b");
        f32 ry = st_row_y(src, "b"), ex = p0->area.x + p0->area.w - S(10), ey = p0->area.y + p0->area.h * 0.6f;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, src->ly.col_name + S(20), ry, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, ex - S(40), ey, 0, 0, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, ex, ey, 0, 0, 0);
        ST_CHECK(A.drag == DRAG_ITEMS && A.drop_split_panel == p0 && A.drop_split == ZONE_RIGHT && !A.drop_dir[0]);
        st_frames(1);
        st_shot("folder-edge-drop");
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
        st_wait();
        Panel *pb = A.ap;
        Node *nb = node_find(A.root, pb);
        ST_CHECK(A.npanels == np + 1 && pb != p0 && nb && nb->parent && nb->parent->kind == NODE_HSPLIT && nb->parent->b == nb);
        ST_CHECK(!strcmp(active_pane()->path, M6_BASE "/b") && st_exists(M6_BASE "/b"));
        // two folders onto p0's empty bar space: two new tabs, the first one shown
        panel_activate(p0);
        st_select(src, "c");
        f32 dy = st_row_y(src, "d");
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, src->ly.col_name + S(20), dy, SDL_BUTTON_LEFT, 1, SDL_KMOD_CTRL); // Ctrl+click adds
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, src->ly.col_name + S(20), dy, SDL_BUTTON_LEFT, 1, SDL_KMOD_CTRL);
        ST_CHECK(src->sel_count == 2);
        nt = A.ntabs;
        int n0 = panel_ntabs(p0);
        f32 bx = (p0->plus.x + p0->plus.w + p0->splitbtn.x) * 0.5f, by = p0->bar.y + p0->bar.h * 0.5f;
        ry = st_row_y(src, "c");
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, src->ly.col_name + S(20), ry, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, bx, by + S(30), 0, 0, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, bx, by, 0, 0, 0);
        ST_CHECK(A.drag == DRAG_ITEMS && A.drop_newtab == p0 && !A.drop_split_panel);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
        st_wait();
        ST_CHECK(A.ntabs == nt + 2 && panel_ntabs(p0) == n0 + 2 && A.ap == p0 && !strcmp(active_pane()->path, M6_BASE "/c"));
        ST_CHECK(!strcmp(panel_tab(p0, -1)->pane[0].path, M6_BASE "/d"));
        // a file is no folder: the edge stays a normal drop (into the pane under it)
        panel_activate(p1);
        pane_navigate(active_pane(), M6_BASE "/b", true); st_wait();
        src = active_pane();
        st_select(src, "file1.txt");
        ry = st_row_y(src, "file1.txt");
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, src->ly.col_name + S(20), ry, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, pb->area.x + S(50), pb->area.y + pb->area.h * 0.6f, 0, 0, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, pb->area.x + S(8), pb->area.y + pb->area.h * 0.6f, 0, 0, 0);
        ST_CHECK(A.drag == DRAG_ITEMS && !A.drop_split_panel && !A.drop_newtab);
        drag_cancel();
        panel_activate(pb); // close the new panel again (the session check below numbers panels by creation order)
        while (panel_alive(pb)) run_command(CMD_CLOSE_PANEL);
        panel_activate(p0);
    }

    // session round trip of the whole tree
    int ntabs0 = A.ntabs, npan0 = A.npanels;
    char lay0[1024];
    size_t o = 0;
    lay0[0] = 0;
    layout_serialize(A.root, lay0, sizeof lay0, &o);
    Panel *act0 = A.ap;
    int act_idx = panel_index(act0);
    settings_save();
    settings_load();
    layout_reset();
    ST_CHECK(session_restore());
    char lay1[1024];
    o = 0;
    lay1[0] = 0;
    layout_serialize(A.root, lay1, sizeof lay1, &o);
    ST_CHECK(A.ntabs == ntabs0 && A.npanels == npan0 && !strcmp(lay0, lay1) && panel_index(A.ap) == act_idx);
    st_wait();
    // closing every tab of a panel collapses the tree back
    while (A.npanels > 1) run_command(CMD_CLOSE_PANEL);
    ST_CHECK(A.npanels == 1 && A.root->kind == NODE_PANEL && !A.quit);
    st_frames(2);
    {   // splitting right again and again: a panel too narrow to halve hands the split to a wider one, then it stops
        int n = 0;
        for (int i = 0; i < MAX_PANELS && A.npanels > n; i++) { n = A.npanels; run_command(CMD_SPLIT); st_wait(); }
        ST_CHECK(A.npanels > 2 && A.npanels < MAX_PANELS && !panel_split_target(A.ap, ZONE_RIGHT));
        Panel *ord[MAX_PANELS];
        int k = panels_ordered(A.root, ord, 0);
        for (int i = 0; i < k; i++) ST_CHECK(node_find(A.root, ord[i])->r.w >= PANEL_MIN_W);
        ST_CHECK(panel_split_target(A.ap, ZONE_BOTTOM) != NULL); // down still has room
        st_shot("split-full");
        while (A.npanels > 1) run_command(CMD_CLOSE_PANEL);
        ST_CHECK(A.npanels == 1);
        st_frames(2);
    }
    {   // new tabs again and again: they stop once the tabs would get too narrow, then go to a panel with room
        Panel *pn = A.ap;
        int nt0 = A.ntabs, n = 0;
        for (int i = 0; i < 64 && A.ntabs > n; i++) { n = A.ntabs; st_key(SDLK_T, SDL_KMOD_CTRL); st_wait(); }
        int full = panel_ntabs(pn);
        ST_CHECK(full > 3 && !tab_target_panel(pn) && panel_tab(pn, -1)->r.w >= TAB_MIN_W);
        st_shot("tabs-full");
        ST_CHECK(!panel_split_target(pn, ZONE_RIGHT) && panel_split_target(pn, ZONE_BOTTOM) == pn); // halving would squeeze its tabs
        int np0 = A.npanels;
        run_command(CMD_SPLIT); st_wait();
        ST_CHECK(A.npanels == np0);
        Panel *np = panel_split(pn, ZONE_RIGHT, NULL, M6_BASE "/a");
        panel_activate(pn); layout();
        ST_CHECK(np && A.ap == pn && tab_target_panel(pn) == np);
        st_key(SDLK_T, SDL_KMOD_CTRL); st_wait();
        ST_CHECK(panel_ntabs(pn) == full && panel_ntabs(np) == 2 && A.ap == np);
        while (A.npanels > 1) run_command(CMD_CLOSE_PANEL);
        while (A.ntabs > nt0) { st_key(SDLK_W, SDL_KMOD_CTRL); st_wait(); }
        ST_CHECK(A.npanels == 1 && A.ntabs == nt0);
        st_frames(2);
    }
    {   // pointer shape: hand over buttons (the new-tab +), I-beam over the path bar, arrow over the file list
        Panel *pn = A.ap;
        PaneLayout *ly = &pn->cur->pane[0].ly;
        st_mouse(SDL_EVENT_MOUSE_MOTION, pn->plus.x + pn->plus.w * 0.5f, pn->plus.y + pn->plus.h * 0.5f, 0, 0, 0);
        ST_CHECK(A.cursor_shown == 3);
        st_mouse(SDL_EVENT_MOUSE_MOTION, pathbar_right(ly) - S(8), ly->pathbar.y + ly->pathbar.h * 0.5f, 0, 0, 0); // (not its copy button: a hand)
        ST_CHECK(A.cursor_shown == 4);
        st_mouse(SDL_EVENT_MOUSE_MOTION, ly->list.x + ly->list.w * 0.5f, ly->list.y + ly->list.h - S(4), 0, 0, 0);
        ST_CHECK(A.cursor_shown == 0);
        Pane *cp = &pn->cur->pane[0]; // file and folder rows keep the arrow, even when one click opens them
        pane_navigate(cp, M6_BASE, true); st_wait();
        A.single_click = true;
        st_mouse(SDL_EVENT_MOUSE_MOTION, ly->list.x + S(60), st_row_y(cp, "b"), 0, 0, 0);
        ST_CHECK(A.cursor_shown == 0);
        A.single_click = false;
    }

    st_rmrf(M6_BASE);
    if (st_fail) { fprintf(stderr, "SELFTEST-M6: %d failure(s)\n", st_fail); return 1; }
    printf("SELFTEST-M6 OK\n");
    fflush(stdout);
    return 0;
}

// --- self-test for M7: icons, columns, views, grouping, folder views --------

#define M7_BASE "/tmp/fe-m7-test"

static void st_touch_days_ago(const char *path, int days) {
    struct timespec ts[2];
    clock_gettime(CLOCK_REALTIME, &ts[0]);
    ts[0].tv_sec -= (time_t)days * 86400;
    ts[1] = ts[0];
    utimensat(AT_FDCWD, path, ts, 0);
}

static void st_setup_m7(void) {
    st_rmrf(M7_BASE);
    mkdir(M7_BASE, 0755);
    mkdir(M7_BASE "/sub", 0755);
    mkdir(M7_BASE "/sub/deeper", 0755);
    st_write(M7_BASE "/package.json", 10);
    st_write(M7_BASE "/main.rs", 2000);
    st_write(M7_BASE "/notes.txt", 0);
    st_write(M7_BASE "/big.bin", 3 << 20);
    st_write(M7_BASE "/old.log", 100);
    st_write(M7_BASE "/yesterday.md", 50);
    for (int i = 0; i < 14; i++) {
        char pth[PATH_MAX];
        snprintf(pth, sizeof pth, M7_BASE "/file%02d.c", i);
        st_write(pth, 100 + i);
    }
    st_touch_days_ago(M7_BASE "/old.log", 900);
    st_touch_days_ago(M7_BASE "/yesterday.md", 1);
}

static bool c_thumb_ready(void) {
    for (int i = 0; i < g_nthumbs; i++)
        if (g_thumbs[i].state == 2 && strstr(g_thumbs[i].path, "photo.bmp")) return true;
    return false;
}

static bool c_sysi_settled(void) {
    for (int i = 0; i < g_nsysicons; i++) if (g_sysicons[i].state == SYSI_PENDING) return false;
    return true;
}

static int run_selftest_m7(void) {
    st_make_bmp(M7_BASE "/photo.bmp", 300, 200);
    pane_refresh(active_pane());
    st_wait();
    Pane *p = active_pane();

    // icons
    ST_CHECK(icon_for_name("package.json", false) == icon_find("nodejs"));
    ST_CHECK(icon_for_name("main.rs", false) == icon_find("rust"));
    ST_CHECK(icon_for_name("Dockerfile", false) == icon_find("docker"));
    ST_CHECK(icon_for_name(".env.local", false) == icon_find("tune"));
    ST_CHECK(icon_for_name("api.d.ts", false) == icon_find("typescript"));
    ST_CHECK(icon_for_name("weird.zzz", false) == icon_find("document"));
    ST_CHECK(icon_for_name("runme", true) == icon_find("console"));
    ST_CHECK(icon_glyph(icon_find("rust"), 16) != NULL && big_icon_tex(icon_find("rust"), 128) != NULL);
    // folder icons: special names (case-insensitive, leading dot ignored), generic fallback, every set
    ST_CHECK(folder_icon_for_name(".git") == icon_find("folder-git") && icon_find("folder-git") >= 0);
    ST_CHECK(folder_icon_for_name("Documents") == icon_find("folder-docs"));
    ST_CHECK(folder_icon_for_name("node_modules") == icon_find("folder-node"));
    ST_CHECK(folder_icon_for_name("random-folder") == icon_find("folder") && icon_find("folder") >= 0);
    ST_CHECK(folder_icon_for_name("") == icon_find("folder"));
    for (int t = 0; t < ICON_THEMES; t++) {
        icon_set_theme(t);
        ST_CHECK(icon_glyph(folder_icon_for_name("src"), 16) != NULL && big_icon_tex(icon_find("folder"), 96) != NULL);
    }
    { // custom icon themes: <config>/icons/<Name>/ - overrides, ext-*.svg, base fallback, Options list, persistence
        char dir[PATH_MAX], path[PATH_MAX + 64];
        icon_themes_dir(dir, sizeof dir);
        snprintf(path, sizeof path, "%s/TestIcons", dir);
        mkdir_p(path, 0700);
        const char *svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\"><rect width=\"16\" height=\"16\" fill=\"#ff00ff\"/></svg>";
        const char *files[5] = {"python.svg", "ext-xyz.svg", "theme.conf", "trash.svg", "Emblem-Git.svg"};
        for (int k = 0; k < 5; k++) {
            snprintf(path, sizeof path, "%s/TestIcons/%s", dir, files[k]);
            FILE *f = fopen(path, "w");
            if (f) { fputs(k == 2 ? "base=Symbols\n" : svg, f); fclose(f); }
        }
        icon_themes_reload();
        int t = icon_theme_find("TestIcons");
        ST_CHECK(t >= ICON_THEMES && opt_choice_count(OPT_ICON_THEME) == icon_theme_count());
        opt_choice_set(OPT_ICON_THEME, t);
        ST_CHECK(g_icon_theme == t);
        int py = icon_find("python"), fo = icon_find("folder");
        ST_CHECK(icon_src(py) && strstr(icon_src(py), "#ff00ff"));                    // override
        ST_CHECK(icon_src(fo) == g_icon_svg[2][fo]);                                   // missing: base (Symbols)
        ST_CHECK(trash_icon(true) == icon_find("trash"));                              // own bin, no full one: keeps it
        int xi = icon_for_name("scene.XYZ", false);
        ST_CHECK(xi >= ICON_EXT_BASE && icon_glyph(xi, 16) && big_icon_tex(xi, 96)); // any extension
        ST_CHECK(icon_glyph(py, 16) != NULL);
        // badges: the theme's emblem-git.svg replaces the app's Git mark; no emblem-symbolic-link.svg: the app's own
        ST_CHECK(badge_icon(BADGE_GIT) == ICON_BADGE_BASE + BADGE_GIT && badge_icon(BADGE_LINK) < 0);
        ST_CHECK(icon_glyph(badge_icon(BADGE_GIT), 16) && big_icon_tex(badge_icon(BADGE_GIT), 82));
        ST_CHECK(icon_find("emblem-git") < 0 && xi != ICON_BADGE_BASE + BADGE_GIT); // (not a file icon, ids apart)
        settings_save();
        settings_load();
        ST_CHECK(!strcmp(A.icon_theme_name, "TestIcons"));
        icon_themes_readme(dir);
        snprintf(path, sizeof path, "%s/README.txt", dir);
        ST_CHECK(st_exists(path));
        opt_choice_set(OPT_ICON_THEME, 0);
        snprintf(path, sizeof path, "rm -rf '%s'", dir);
        if (system(path)) {}
        icon_themes_reload();
        ST_CHECK(icon_theme_find("TestIcons") < 0 && g_icon_theme == 0);
        ST_CHECK(badge_icon(BADGE_GIT) < 0 && badge_icon(BADGE_LINK) < 0 && !icon_src(ICON_BADGE_BASE)); // built in: the app's marks
        ST_CHECK(trash_icon(true) == icon_find("trash-full") && trash_icon(false) == icon_find("trash")); // built in: both bins
    }
    { // File icons "System": the desktop's icon theme, looked up and drawn on a worker, over the bundled icon meanwhile
        const char *base = "/tmp/fe-m7-sysicons";
        char path[PATH_MAX], dirs[16384] = "", *old_home = getenv("XDG_DATA_HOME"), *old_dirs = getenv("XDG_DATA_DIRS");
        char save_home[PATH_MAX] = "", save_dirs[4096] = "";
        if (old_home) snprintf(save_home, sizeof save_home, "%s", old_home);
        if (old_dirs) snprintf(save_dirs, sizeof save_dirs, "%s", old_dirs);
        st_rmrf(base);
        // FETheme: Directories= over 8 KB and more than 512 folders (only the last fixed one exists), then a
        // scalable one; inherits FEParent (a PNG at 48 px)
        snprintf(path, sizeof path, "%s/data/icons/FETheme", base);
        mkdir_p(path, 0755);
        FILE *f;
        snprintf(path, sizeof path, "%s/data/icons/FETheme/index.theme", base);
        if ((f = fopen(path, "w"))) {
            for (int i = 0; i < 700; i++) snprintf(dirs + strlen(dirs), sizeof dirs - strlen(dirs), "16x16/places-%03d,", i);
            fprintf(f, "[Icon Theme]\nName=FE\nInherits=FEParent\nDirectories=%sscalable/places\n\n", dirs);
            for (int i = 0; i < 700; i++) fprintf(f, "[16x16/places-%03d]\nSize=16\nType=Fixed\n\n", i);
            fprintf(f, "[scalable/places]\nSize=64\nMinSize=8\nMaxSize=512\nType=Scalable\n");
            fclose(f);
        }
        ST_CHECK(strlen(dirs) > 8192);
        const char *svg16 = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\"><rect width=\"16\" height=\"16\" fill=\"#123456\"/></svg>";
        snprintf(path, sizeof path, "%s/data/icons/FETheme/16x16/places-699", base);
        mkdir_p(path, 0755);
        snprintf(path, sizeof path, "%s/data/icons/FETheme/16x16/places-699/folder.svg", base);
        st_write_text(path, svg16);
        snprintf(path, sizeof path, "%s/data/icons/FETheme/scalable/places", base);
        mkdir_p(path, 0755);
        snprintf(path, sizeof path, "%s/data/icons/FETheme/scalable/places/user-home.svg", base);
        st_write_text(path, svg16);
        snprintf(path, sizeof path, "%s/data/icons/FEParent/48", base);
        mkdir_p(path, 0755);
        snprintf(path, sizeof path, "%s/data/icons/FEParent/index.theme", base);
        st_write_text(path, "[Icon Theme]\nName=Parent\nDirectories=48\n\n[48]\nSize=48\n");
        snprintf(path, sizeof path, "%s/data/icons/FEParent/48/text-x-generic.png", base);
        st_make_bmp(path, 48, 48); // (stb_image goes by the contents)
        // FEBad: a folder icon of infinite size (refused, the bundled one stays)
        snprintf(path, sizeof path, "%s/data/icons/FEBad/16", base);
        mkdir_p(path, 0755);
        snprintf(path, sizeof path, "%s/data/icons/FEBad/index.theme", base);
        st_write_text(path, "[Icon Theme]\nName=Bad\nDirectories=16\n[16]\nSize=16\n");
        snprintf(path, sizeof path, "%s/data/icons/FEBad/16/folder.svg", base);
        st_write_text(path, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1e999\" height=\"16\"><rect width=\"16\" height=\"16\"/></svg>");
        snprintf(path, sizeof path, "%s/data", base);
        setenv("XDG_DATA_HOME", path, 1);
        setenv("XDG_DATA_DIRS", "/tmp/fe-m7-sysicons/none", 1);
        sysi_reset();
        char found[PATH_MAX];
        ST_CHECK(sysi_lookup("FETheme", "folder", 16, found, sizeof found) && strstr(found, "places-699/folder.svg")); // (past 8 KB, 512 folders)
        ST_CHECK(sysi_lookup("FETheme", "user-home", 48, found, sizeof found) && strstr(found, "scalable/places"));
        ST_CHECK(sysi_lookup("FETheme", "text-x-generic", 16, found, sizeof found) && strstr(found, "FEParent/48")); // inherited, closest size
        ST_CHECK(!sysi_lookup("FETheme", "no-such-icon", 16, found, sizeof found) && !sysi_lookup("FETheme", "../x", 16, found, sizeof found));
        snprintf(path, sizeof path, "%s/data/icons/FEBad/16/folder.svg", base);
        ST_CHECK(!sysi_raster(path, 16, 0xFFFFFF));
        // through the UI
        int sys = icon_theme_find("System");
        ST_CHECK(sys == icon_theme_count() - 1 && !strcmp(icon_theme_name(sys), "System"));
        opt_choice_set(OPT_ICON_THEME, sys);
        ST_CHECK(icon_system_selected() && !icon_custom() && !g_dlook.running); // (no watcher in tests)
        DesktopLook *d = xcalloc(1, sizeof *d);
        *d = (DesktopLook){.icons = "FETheme"};
        dlook_apply(d);
        int fo = icon_find("folder"), doc = icon_find("python"), bin = icon_find("trash");
        Glyph *g = icon_glyph(fo, 16); // the bundled folder while the desktop's is looked up
        ST_CHECK(g && g_nsysicons == 1 && g_sysicons[0].state == SYSI_PENDING);
        ST_CHECK(icon_glyph(doc, 16) && big_icon_tex(doc, 96) && icon_glyph(bin, 16));
        ST_CHECK(st_wait_cond(c_sysi_settled, 5000));
        g = icon_glyph(fo, 16); // ... then the desktop's, copied over it in the atlas
        ST_CHECK(g && g_atlas[((size_t)(g->y + 8) * ATLAS_SIZE + g->x + 8) * 4] == 0x12 && g_atlas[((size_t)(g->y + 8) * ATLAS_SIZE + g->x + 8) * 4 + 2] == 0x56);
        for (int i = 0; i < g_nsysicons; i++) { // python: text-x-generic from FEParent; the bin: the theme has none
            SysIcon *si = &g_sysicons[i];
            if (si->id == doc) ST_CHECK(si->state == SYSI_READY);
            if (si->id == bin) ST_CHECK(si->state == SYSI_NONE);
        }
        ST_CHECK(big_icon_tex(doc, 96) != NULL);
        d = xcalloc(1, sizeof *d);
        *d = (DesktopLook){.icons = "FEBad"}; // another desktop theme: asked again
        dlook_apply(d);
        ST_CHECK(g_nsysicons == 0 && icon_glyph(fo, 16));
        ST_CHECK(st_wait_cond(c_sysi_settled, 5000));
        for (int i = 0; i < g_nsysicons; i++) if (g_sysicons[i].id == fo) ST_CHECK(g_sysicons[i].state == SYSI_NONE && !g_sysicons[i].rgba);
        settings_save();
        settings_load();
        ST_CHECK(!strcmp(A.icon_theme_name, "System"));
        opt_choice_set(OPT_ICON_THEME, 0);
        ST_CHECK(!icon_system_selected() && g_nsysicons == 0);
        icon_glyph(fo, 16); // (built-in sets never ask the desktop)
        ST_CHECK(g_nsysicons == 0);
        if (save_home[0]) setenv("XDG_DATA_HOME", save_home, 1); else unsetenv("XDG_DATA_HOME");
        if (save_dirs[0]) setenv("XDG_DATA_DIRS", save_dirs, 1); else unsetenv("XDG_DATA_DIRS");
        sysi_reset();
        memset(&g_desktop, 0, sizeof g_desktop);
        st_rmrf(base);
    }
    { // the logo follows the accent colour (Blue = the artwork as drawn)
        u32 saved = g_accent_custom;
        ST_CHECK(icon_glyph(ICON_LOGO_SMALL, 24) != NULL);
        icons_sync_theme();
        g_logo_changed = false;
        g_accent_custom = 0x01000000u | 0x9D7CF4; // purple
        theme_apply();
        icons_sync_theme();
        ST_CHECK(g_logo_changed && !g_logo_small_img && icon_glyph(ICON_LOGO_SMALL, 24) && g_logo_accent == COL_ACCENT);
        char buf[64] = "fill=\"#2566DE\" x #FFFFFF";
        logo_recolor(buf, LOGO_BASE_ACCENT);
        ST_CHECK(!strcmp(buf, "fill=\"#2566DE\" x #FFFFFF")); // base accent: unchanged
        // the system icon: a recoloured copy in the user's icon theme, none for Blue
        const char *dh = ST_BASE "/appicon", *ip = ST_BASE "/appicon/icons/hicolor/scalable/apps/dev.fileexplorer.svg";
        ST_CHECK(app_icon_write(dh, 0x9D7CF4) && access(ip, F_OK) == 0);
        FILE *f = fopen(ip, "r");
        char svg[4096] = "";
        if (f) { svg[fread(svg, 1, sizeof svg - 1, f)] = 0; fclose(f); }
        ST_CHECK(strstr(svg, "viewBox=\"0 -84 624 624\"") && !strstr(svg, "#2566DE") && strstr(svg, "#FFFFFF"));
        ST_CHECK(!app_icon_write(dh, 0x9D7CF4));                       // same colour: left alone
        ST_CHECK(app_icon_write(dh, LOGO_BASE_ACCENT) && access(ip, F_OK) == 0); // Blue: the copy stays, in the packaged colours
        f = fopen(ip, "r");
        svg[0] = 0;
        if (f) { svg[fread(svg, 1, sizeof svg - 1, f)] = 0; fclose(f); }
        ST_CHECK(strstr(svg, "#2566DE") != NULL);
        unlink(ip);
        ST_CHECK(!app_icon_write(dh, LOGO_BASE_ACCENT) && access(ip, F_OK) != 0); // no copy yet: none made for Blue
        g_accent_custom = saved;
        theme_apply();
        icons_sync_theme();
        g_logo_changed = false;
    }
    { // icon contrast: pale artwork is darkened only on light navigation surfaces, keeping its hue
        ST_CHECK(icon_contrast_scale(0.8f, 0.64f) == 1.f);       // dark surface: no change
        ST_CHECK(icon_contrast_scale(0.f, 0.8f) == 1.f);          // transparent ink: no change
        ST_CHECK(fabsf(icon_contrast_scale(0.8f, 0.8f) - 0.3125f) < 0.001f); // (0.8 - 0.55) / 0.8
        ST_CHECK(fabsf(icon_contrast_scale(0.5f, 0.7f) - 0.30f) < 0.001f);
        ST_CHECK(fabsf(icon_contrast_scale(4.f, 0.8f) - 0.28f) < 0.001f); // clamped floor
        u8 white[16], clear[16] = {0};
        for (int i = 0; i < 4; i++) { white[i * 4] = white[i * 4 + 1] = white[i * 4 + 2] = 255; white[i * 4 + 3] = 255; }
        ST_CHECK(fabsf(icon_pixel_lightness(white, 4) - 1.f) < 0.001f);
        ST_CHECK(icon_pixel_lightness(clear, 4) == 0.f);
        u8 yellow[16]; // Monokai's folder yellow: bright but coloured, so it is not pale and keeps its colour
        for (int i = 0; i < 4; i++) { yellow[i * 4] = 0xE6; yellow[i * 4 + 1] = 0xDB; yellow[i * 4 + 2] = 0x74; yellow[i * 4 + 3] = 255; }
        ST_CHECK(icon_pixel_lightness(yellow, 4) < 0.01f && icon_contrast_scale(icon_pixel_lightness(yellow, 4), 1.f) == 1.f);
        int fo = icon_find("folder"), th = g_theme_cur;
        ST_CHECK(icon_glyph(fo, 16) != NULL && big_icon_tex(fo, 96) != NULL);
        f32 gl = -1.f, bl = -1.f;
        for (int i = 0; i < g_icon_cache_n; i++) if (g_icon_cache[i].id == fo && g_icon_cache[i].px == 16) gl = g_icon_cache[i].lightness;
        for (int i = 0; i < g_nbig; i++) if (g_big_icons[i].id == fo && g_big_icons[i].px == 96) bl = g_big_icons[i].lightness;
        ST_CHECK(gl > 0.f && gl <= 1.f && fabsf(bl - gl) < 0.001f);
        g_theme_cur = theme_find("Light");
        theme_apply();
        icons_sync_theme();
        ST_CHECK(icon_glyph(fo, 16) != NULL);
        f32 gl2 = -1.f;
        for (int i = 0; i < g_icon_cache_n; i++) if (g_icon_cache[i].id == fo && g_icon_cache[i].px == 16) gl2 = g_icon_cache[i].lightness;
        ST_CHECK(fabsf(gl2 - gl) < 0.0001f); // cached lightness is theme-independent
        g_theme_cur = th;
        theme_apply();
        icons_sync_theme();
        // draw-time tint: only light navigation surfaces darken pale artwork; the pixel caches never change
        u32 saved_bg = g_icon_surface_bg;
        g_icon_surface_bg = 0;
        ST_CHECK(icon_glyph_tint(fo, 16) == 1.f);
        g_icon_surface_bg = 0x202124; // dark surface
        ST_CHECK(icon_glyph_tint(fo, 16) == 1.f);
        g_icon_surface_bg = 0xFFFFFF; // light navigation surface: white artwork darkens, the coloured folder doesn't
        int ui = ICON_UI_BASE + UI_TRASH;
        ST_CHECK(icon_glyph(ui, 16) != NULL);
        f32 tint = icon_glyph_tint(ui, 16);
        ST_CHECK(tint > 0.f && tint < 1.f);
        ST_CHECK(icon_glyph_tint(fo, 16) == 1.f);
        SDL_Texture *bt = big_icon_tex(fo, 96);
        ST_CHECK(bt != NULL && icon_texture_tint(bt) == 1.f);
        g_icon_surface_bg = saved_bg;
        ST_CHECK(icon_glyph_tint(ui, 16) == 1.f && (!bt || icon_texture_tint(bt) == 1.f));
    }
    { // the trash bin's main colour follows the UI theme (re-rasterized on a theme change)
        int tr = icon_find("trash"), saved = g_theme_cur;
        ST_CHECK(tr >= 0 && strstr(g_icon_svg[0][tr], ICON_THEME_MARK) && icon_glyph(tr, 16));
        icons_sync_theme();
        g_theme_cur = theme_find("Light");
        theme_apply();
        icons_sync_theme();
        ST_CHECK(g_icon_main == COL_TEXT2 && !g_icon_img[tr] && icon_glyph(tr, 16));
        g_theme_cur = saved;
        theme_apply();
        icons_sync_theme();
    }
    icon_set_theme(0);

    // columns: show Extension, reorder Size before Date, resize Date, sort by Extension
    ST_CHECK(!col_visible(COL_EXT));
    col_toggle(COL_TYPE); // fit the test into narrow (tiled) windows: Name + Size + Date + Ext
    A.colw[COL_DATE] = 90;
    A.colw[COL_SIZE] = 70;
    A.colw[COL_EXT] = 50;
    col_toggle(COL_EXT);
    layout();
    PaneLayout *ly = &p->ly;
    f32 x0 = 0, x1 = 0;
    ST_CHECK(col_visible(COL_EXT) && col_span(ly, COL_EXT, &x0, &x1));
    f32 dx0 = 0, dx1 = 0, sx0 = 0, sx1 = 0;
    col_span(ly, COL_DATE, &dx0, &dx1);
    col_span(ly, COL_SIZE, &sx0, &sx1);
    f32 hy = ly->header.y + ly->header.h * 0.5f;
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, (sx0 + sx1) * 0.5f, hy, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_MOTION, (sx0 + sx1) * 0.5f - S(30), hy, 0, 0, 0);
    st_mouse(SDL_EVENT_MOUSE_MOTION, dx0 + S(10), hy, 0, 0, 0);
    ST_CHECK(A.drag == DRAG_COL);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    int ip = -1, id = -1;
    for (int i = 0; i < A.ncols; i++) {
        if (A.cols[i] == COL_SIZE) ip = i;
        if (A.cols[i] == COL_DATE) id = i;
    }
    ST_CHECK(ip >= 0 && id >= 0 && ip < id);
    layout();
    col_span(ly, COL_DATE, &dx0, &dx1);
    f32 w0 = A.colw[COL_DATE];
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, dx1, hy, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(A.drag == DRAG_COL_RESIZE);
    st_mouse(SDL_EVENT_MOUSE_MOTION, dx1 - S(10), hy, 0, 0, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(fabsf(A.colw[COL_DATE] - (w0 - 10)) < 2);
    layout();
    { // the grabbed edge follows the cursor; the column's other edge stays put
        f32 nx0 = 0, nx1 = 0;
        col_span(ly, COL_DATE, &nx0, &nx1);
        ST_CHECK(fabsf(nx1 - (dx1 - S(10))) < 2 && fabsf(nx0 - dx0) < 2);
    }
    if (!col_span(ly, COL_EXT, &x0, &x1)) { fprintf(stderr, "SELFTEST: Extension column not visible\n"); st_fail++; goto m7_done; }
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, (x0 + x1) * 0.5f, hy, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    st_wait();
    ST_CHECK(p->sort_key == SORT_EXT);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, (x0 + x1) * 0.5f, hy, SDL_BUTTON_RIGHT, 1, 0);
    ST_CHECK(A.menu.open && A.menu.items[0].checkable && A.menu.items[COL_EXT].checked);
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    A.size_bars = true;
    st_shot("columns");
    pane_set_sort(p, SORT_NAME);
    st_wait();

    // the ... button: the folder's menu, the selection stays; its actions are about the folder, not the subfolder
    // under the cursor
    ST_CHECK(st_select(p, "sub") && p->sel_count == 1);
    Rect mb = p->ly.morebtn;
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, mb.x + mb.w * 0.5f, mb.y + mb.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, mb.x + mb.w * 0.5f, mb.y + mb.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(A.menu.open && A.menu.folder && A.menu.items[0].act == ACT_NEW_FOLDER && p->sel_count == 1);
    int pin_i = -1;
    for (int k = 0; k < A.menu.n; k++)
        if (A.menu.items[k].act == ACT_PIN) pin_i = k;
    ST_CHECK(pin_i >= 0 && pin_index(M7_BASE) < 0);
    menu_activate(pin_i);
    ST_CHECK(!A.menu.open && pin_index(M7_BASE) >= 0 && pin_index(M7_BASE "/sub") < 0);
    pin_toggle(M7_BASE);
    ST_CHECK(pin_index(M7_BASE) < 0);

    // grid view: geometry, 2D navigation, rubber band, thumbnails
    set_zoom_level(p, 4); // medium icons
    st_wait();
    ly = &p->ly;
    ST_CHECK(p->view == VIEW_GRID && ly->per_row > 1);
    int pr = ly->per_row;
    Rect r0 = item_rect(p, 0);
    ST_CHECK(item_at(p, r0.x + r0.w * 0.5f, r0.y + r0.h * 0.5f) == 0);
    st_key(SDLK_HOME, SDL_KMOD_NONE);
    st_key(SDLK_RIGHT, SDL_KMOD_NONE);
    ST_CHECK(p->cursor == 1);
    st_key(SDLK_DOWN, SDL_KMOD_NONE);
    ST_CHECK(p->cursor == 1 + pr);
    Rect rb = item_rect(p, pr + 1);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, ly->list.x + 2, ly->list.y + 2, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_MOTION, rb.x + rb.w * 0.5f, rb.y + rb.h * 0.5f, 0, 0, 0);
    ST_CHECK(A.drag == DRAG_BAND && p->sel_count == 4);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(st_wait_cond(c_thumb_ready, 5000));
    st_shot("grid");
    // Ctrl+wheel over the pane makes the content smaller / bigger, the view stays
    SDL_MouseWheelEvent wev;
    SDL_zero(wev);
    wev.y = -1;
    wev.mouse_x = ly->list.x + S(50);
    wev.mouse_y = ly->list.y + S(50);
    SDL_SetModState(SDL_KMOD_CTRL);
    on_wheel(&wev);
    SDL_SetModState(SDL_KMOD_NONE);
    ST_CHECK(p->view == VIEW_GRID && p->icon_px == 80);
    set_zoom_level(p, 1); // list
    ST_CHECK(p->view == VIEW_LIST && p->ly.cell_h == p->ly.row_h + roundf(S(2)));
    set_zoom_level(p, 0); // details
    st_wait();
    { // details: bigger icons, taller rows; the status bar slider (log scale: the far end is the biggest)
        f32 rh0 = p->ly.row_h;
        wev.y = 1;
        SDL_SetModState(SDL_KMOD_CTRL);
        for (int k = 0; k < 3; k++) on_wheel(&wev);
        SDL_SetModState(SDL_KMOD_NONE);
        ST_CHECK(p->view == VIEW_DETAILS && p->list_px == 32 && p->ly.row_h > rh0);
        st_shot("details-big");
        st_frames(1);
        Rect sr = A.r_size;
        ST_CHECK(sr.w > 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, sr.x + sr.w - 1, sr.y + sr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(A.drag == DRAG_SIZE && p->list_px == 64);
        st_mouse(SDL_EVENT_MOUSE_MOTION, sr.x, sr.y + sr.h * 0.5f, 0, 0, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, sr.x, sr.y + sr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(A.drag == DRAG_NONE && p->list_px == 16 && p->ly.row_h == rh0);
        // each view keeps its own size through the status bar's view buttons, and new tabs get it too
        Rect vb0 = A.r_viewbtn[0], vb2 = A.r_viewbtn[2];
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, vb2.x + vb2.w * 0.5f, vb2.y + vb2.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, vb2.x + vb2.w * 0.5f, vb2.y + vb2.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(p->view == VIEW_GRID && p->icon_px == 80 && A.def_icon == 80);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, vb0.x + vb0.w * 0.5f, vb0.y + vb0.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, vb0.x + vb0.w * 0.5f, vb0.y + vb0.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(p->view == VIEW_DETAILS && p->icon_px == 80);
        pane_set_view(p, VIEW_LIST);
        wev.y = 1;
        SDL_SetModState(SDL_KMOD_CTRL);
        on_wheel(&wev);
        SDL_SetModState(SDL_KMOD_NONE);
        ST_CHECK(p->listv_px == 20 && p->list_px == 16 && A.def_listv_icon == 20);
        ST_CHECK(A.def_icon == 80 && A.def_list_icon == 16); // (what pane_init gives new tabs)
        pane_set_view(p, VIEW_DETAILS);
        st_wait();
    }

    // grouping (details): size groups, collapse/expand by clicking the header
    pane_set_group(p, GROUP_SIZE);
    st_wait();
    ST_CHECK(p->nhdr >= 3 && !strcmp(p->hdr[0].label, "Folders"));
    u32 before = p->count;
    u32 folders = p->hdr[0].count;
    Rect hr = {0, 0, 0, 0};
    f32 hy0 = (f32)(p->ly.list.y + (f64)p->hdr[0].row * p->ly.row_h - p->scroll) + p->ly.row_h * 0.5f;
    (void)hr;
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, p->ly.col_name + S(40), hy0, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(p->hdr[0].collapsed && p->count == before - folders);
    st_shot("groups");
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, p->ly.col_name + S(40), hy0, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(!p->hdr[0].collapsed && p->count == before);
    pane_set_group(p, GROUP_DATE);
    st_wait();
    bool today = false, yday = false, old = false;
    for (int i = 0; i < p->nhdr; i++) {
        today |= !strcmp(p->hdr[i].label, "Today");
        yday |= !strcmp(p->hdr[i].label, "Yesterday");
        old |= !strcmp(p->hdr[i].label, "A long time ago");
    }
    ST_CHECK(today && yday && old);
    // keyboard navigation skips header rows
    st_key(SDLK_HOME, SDL_KMOD_NONE);
    st_key(SDLK_DOWN, SDL_KMOD_NONE);
    ST_CHECK(p->cursor == 1);
    pane_set_group(p, GROUP_NONE);
    st_wait();
    ST_CHECK(p->nhdr == 0);

    // by default the tab keeps its view while browsing (new tabs start in the default view)
    set_zoom_level(p, 4);
    pane_navigate(p, M7_BASE "/sub", true); st_wait();
    ST_CHECK(p->view == VIEW_GRID && p->icon_px == g_zoom[4].px);
    pane_history(p, -1); st_wait();
    ST_CHECK(p->view == VIEW_GRID);
    set_zoom_level(p, 0);
    // per-folder view (when folders keep their own): grid here, details elsewhere; subfolders inherit when asked
    A.view_per_folder = true;
    set_zoom_level(p, 4);
    pane_navigate(p, M7_BASE "/sub", true); st_wait();
    ST_CHECK(p->view == VIEW_DETAILS);
    pane_history(p, -1); st_wait();
    ST_CHECK(p->view == VIEW_GRID);
    folder_view_action(p, ACT_FV_SUBFOLDERS);
    pane_navigate(p, M7_BASE "/sub/deeper", true); st_wait();
    ST_CHECK(p->view == VIEW_GRID);
    settings_save();
    folders_load();
    ST_CHECK(folder_view_find(M7_BASE, true) && folder_view_find(M7_BASE, true)->recursive);
    ST_CHECK(folder_view_find(M7_BASE "/sub/deeper", false) != NULL);
    pane_navigate(p, M7_BASE, true); st_wait();
    folder_view_action(p, ACT_FV_RESET);
    st_wait();
    ST_CHECK(p->view == VIEW_DETAILS && !folder_view_find(M7_BASE, true));
    A.view_per_folder = false;

    // a cut-off name shows its full text after the mouse rests on it; a name that fits doesn't; then no more waking
    {
        char longname[200];
        snprintf(longname, sizeof longname, "%s", "a rather long file name with many words that cannot fit in the name column of a window, "
                                                  "nor in two lines of a grid label, so it ends in an ellipsis.txt");
        char path[PATH_MAX];
        mkdir(M7_BASE "/tips", 0755);
        snprintf(path, sizeof path, M7_BASE "/tips/%s", longname);
        st_write(path, 10);
        st_write(M7_BASE "/tips/short.txt", 10);
        pane_navigate(p, M7_BASE "/tips", true); st_wait();
        ly = &p->ly;
        ST_CHECK(p->view == VIEW_DETAILS);
        A.mouse_in = true;
        A.mx = ly->col_name + S(80);
        A.my = st_row_y(p, "short.txt");
        st_frames(1);
        ST_CHECK(g_fit_tip_len == 0);
        A.my = st_row_y(p, longname);
        st_frames(1);
        ST_CHECK(g_fit_tip_len > 0 && !strcmp(g_fit_tip, longname) && !fit_tip_ready() && fit_tip_wait_ms() > 0);
        SDL_Delay(600);
        st_frames(1);
        ST_CHECK(fit_tip_ready() && fit_tip_wait_ms() == 0);
        // grid: hovering the first line of the label shows the whole name (not the second line's end)
        set_zoom_level(p, 4);
        st_wait();
        ST_CHECK(p->view == VIEW_GRID);
        i64 li = -1;
        for (u32 i = 0; i < p->count; i++)
            if (!strcmp(p->L->entries[p->order[i]].name, longname)) li = i;
        ST_CHECK(li >= 0);
        if (li >= 0) {
            Rect r = item_rect(p, li);
            A.mx = r.x + r.w * 0.5f;
            A.my = r.y + roundf(S(8)) + roundf(S(p->icon_px)) + roundf(S(6)) + S(3);
            st_frames(1);
            ST_CHECK(g_fit_tip_len > 0 && !strcmp(g_fit_tip, longname));
        }
        A.mouse_in = false;
        set_zoom_level(p, 0);
        st_frames(1);
        ST_CHECK(g_fit_tip_len == 0);
    }

m7_done:
    st_rmrf(M7_BASE);
    if (st_fail) { fprintf(stderr, "SELFTEST-M7: %d failure(s)\n", st_fail); return 1; }
    printf("SELFTEST-M7 OK\n");
    fflush(stdout);
    return 0;
}

// --- self-test for M8: open with, properties, batch rename -------------------

#define M8_BASE "/tmp/fe-m8-test"

// Where the caret for byte offset off of a text field drawn last frame sits (x), and the field's middle (y).
static bool st_text_at(TextEdit *t, int off, f32 *x, f32 *y) {
    for (int i = 0; i < g_text_field_count; i++) {
        TextFieldHit *h = &g_text_fields[i];
        if (h->edit != t) continue;
        f32 cx = h->masked ? utf8_index(t->buf, off) * (roundf(S(3)) * 2 + S(4)) - S(2) : text_caret_x(h->font, t->buf, (int)strlen(t->buf), off);
        *x = h->rect.x + h->pad - t->scroll + cx;
        *y = h->hit.y + h->hit.h * 0.5f;
        return true;
    }
    return false;
}

static void st_text_click(TextEdit *t, int off, int clicks, SDL_Keymod mod) {
    f32 x = 0, y = 0;
    ST_CHECK(st_text_at(t, off, &x, &y));
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y, SDL_BUTTON_LEFT, clicks, mod);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, x, y, SDL_BUTTON_LEFT, clicks, mod);
}

static void st_setup_m8(void) {
    st_rmrf(M8_BASE);
    mkdir(M8_BASE, 0755);
    mkdir(M8_BASE "/dir", 0755);
    st_write(M8_BASE "/dir/a.bin", 1000);
    st_write(M8_BASE "/dir/b.bin", 2000);
    FILE *f = fopen(M8_BASE "/abc.txt", "w");
    fputs("abc", f);
    fclose(f);
    st_write(M8_BASE "/photo1.jpg", 10);
    st_write(M8_BASE "/photo2.jpg", 10);
    st_write(M8_BASE "/photo3.jpg", 10);
    mkdir(M8_BASE "/xdg", 0755);
    setenv("XDG_DATA_HOME", M8_BASE "/xdg", 1); // private trash
}

static bool c_arc_done(void) { return g_sheet.arc && atomic_load(&g_sheet.arc->done); }
static bool c_media_done(void) { return g_sheet.media && atomic_load(&g_sheet.media->done); }
static bool c_stats_done(void) { return g_sheet.stats && atomic_load(&g_sheet.stats->done); }
static bool c_sum_done(void) { return g_sheet.sum && atomic_load(&g_sheet.sum->done); }

// The menu's item with this label (-1: none; also the git suite's).
static int st_menu_find(const char *label) {
    for (int i = 0; i < A.menu.n; i++)
        if (A.menu.items[i].label && !strcmp(A.menu.items[i].label, label)) return i;
    return -1;
}

static int run_selftest_m8(void) {
    st_wait();
    Pane *p = active_pane();

    { // feedback is drawn above an open sheet, not hidden behind it
        options_open();
        ST_CHECK(sheet_open());
        toast("Saved");
        st_frames(1);
        ST_CHECK(A.toast[0] && g_toast_seq > g_sheet_seq && g_sheet_seq > 0);
        SDL_Surface *surf = SDL_RenderReadPixels(A.ren, NULL);
        ST_CHECK(surf != NULL && g_toast_probe.w > 0);
        if (surf && g_toast_probe.w > 0) {
            int px = (int)(g_toast_probe.x + g_toast_probe.w * 0.5f), py = (int)(g_toast_probe.y + g_toast_probe.h * 0.5f);
            ST_CHECK(px >= 0 && py >= 0 && px < surf->w && py < surf->h);
            if (px >= 0 && py >= 0 && px < surf->w && py < surf->h) {
                Uint32 v = *(Uint32 *)((Uint8 *)surf->pixels + py * surf->pitch + px * SDL_BYTESPERPIXEL(surf->format));
                Uint8 r, g, b, a;
                SDL_GetRGBA(v, SDL_GetPixelFormatDetails(surf->format), SDL_GetSurfacePalette(surf), &r, &g, &b, &a);
                ST_CHECK(r == 0x00 && g == 0xFF && b == 0x00); // the toast's selftest marker survived the sheet
            }
            SDL_DestroySurface(surf);
        }
        st_shot("toast-above-sheet");
        A.toast[0] = 0;
        sheet_close();
        st_frames(1);
    }

    // MIME + apps
    ST_CHECK(!strcmp(mime_for_file("Photo.PNG", false), "image/png"));
    ST_CHECK(!strcmp(mime_for_file("a.tar.gz", false), "application/x-compressed-tar"));
    ST_CHECK(!strcmp(mime_for_file("x", true), "inode/directory"));
    apps_load();
    ST_CHECK(g_napps > 0);
    DesktopApp *list[64];
    ST_CHECK(apps_for_mime("text/plain", list, 64) >= 1);
    // launching expands field codes (a fake app that writes its argument)
    // Exec quoting per the Desktop Entry spec: double quotes, backslash escapes, %% for a literal %
    DesktopApp fake = {"fake.desktop", "/tmp/fake.desktop", "Fake",
                       "sh -c \"printf '%%s' \\\"$1\\\" > " M8_BASE "/launched\" sh %f", "", ";", false, false};
    char *files[1] = {M8_BASE "/abc.txt"};
    ST_CHECK(app_launch(&fake, files, 1));
    for (int k = 0; k < 200 && st_size(M8_BASE "/launched") <= 0; k++) SDL_Delay(5);
    {
        char buf[256] = {0};
        int fd = open(M8_BASE "/launched", O_RDONLY);
        if (fd >= 0) { ssize_t r = read(fd, buf, sizeof buf - 1); (void)r; close(fd); }
        ST_CHECK(!strcmp(buf, M8_BASE "/abc.txt"));
    }
    unlink(M8_BASE "/launched");

    // Open with sheet
    st_select(p, "abc.txt");
    do_action(p, ACT_OPEN_WITH);
    ST_CHECK(g_sheet.kind == SHEET_OPENWITH && !strcmp(g_sheet.mime, "text/plain") && g_sheet.napps >= 1);
    { // the "default" label keeps contrast on the dark inset well; selected rows keep their own colour
        int saved = g_theme_cur;
        u32 dark_well = rgb_mix(COL_POPUP, 0x000000, 0.22f);
        g_theme_cur = theme_find("Dark");
        theme_apply();
        ST_CHECK(rgb_luma(dark_well) < 0.5f);
        ST_CHECK(ow_default_label_color(dark_well, false) == rgb_mix(COL_ACCENT, 0xFFFFFF, 0.15f));
        ST_CHECK(ow_default_label_color(dark_well, true) == COL_SEL_DIM);
        g_theme_cur = theme_find("Light");
        theme_apply();
        ST_CHECK(ow_default_label_color(rgb_mix(COL_POPUP, 0x000000, 0.04f), false) == rgb_mix(COL_ACCENT, 0x000000, 0.35f));
        ST_CHECK(ow_default_label_color(rgb_mix(COL_POPUP, 0x000000, 0.04f), true) == COL_SEL_DIM);
        g_theme_cur = saved;
        theme_apply();
    }
    st_frames(2);
    st_shot("openwith");
    { // all apps: the list scrolls inside its container; wheel scrolling sticks; clicks hit the right row
        g_sheet.show_all = true;
        openwith_refresh();
        st_frames(2);
        f32 vis = g_sheet.rlist.h / g_sheet.row_h;
        if (g_sheet.napps > vis + 2) {
            sheet_wheel(-3);
            st_settle();
            f32 sc = g_sheet.ow_scroll;
            ST_CHECK(sc > 0);
            st_frames(2);
            ST_CHECK(g_sheet.ow_scroll == sc); // not pulled back to the selection
            SDL_MouseButtonEvent mb = {.type = SDL_EVENT_MOUSE_BUTTON_DOWN, .button = SDL_BUTTON_LEFT, .clicks = 1,
                                       .x = g_sheet.rlist.x + S(40), .y = g_sheet.rlist.y + g_sheet.row_h * 1.5f};
            sheet_mouse(&mb);
            ST_CHECK(g_sheet.sel == (int)((g_sheet.row_h * 1.5f + sc) / g_sheet.row_h));
            st_shot("openwith-scrolled");
            for (int k = 0; k < g_sheet.napps; k++) sheet_key(SDLK_DOWN, SDL_KMOD_NONE); // keyboard: follows the selection
            st_frames(2);
            ST_CHECK(g_sheet.sel == g_sheet.napps - 1 && g_sheet.ow_scroll > sc);
        } else {
            fprintf(stderr, "selftest-m8: only %d apps installed, scroll check skipped\n", g_sheet.napps);
        }
        g_sheet.show_all = false;
        openwith_refresh();
    }
    on_text("zzzzzz-no-such-app");
    ST_CHECK(g_sheet.napps == 0);
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    ST_CHECK(!sheet_open());

    { // a folder opened with an app: it starts inside it; an Exec without file codes gets no argument
        DesktopApp term = {"faketerm.desktop", "/tmp/faketerm.desktop", "Fake terminal",
                           "sh -c \"pwd > " M8_BASE "/launched; printf '%%s' \\\"$1\\\" >> " M8_BASE "/launched\" sh", "", ";", false, false};
        char *dirs[1] = {M8_BASE "/dir"};
        ST_CHECK(app_launch(&term, dirs, 1));
        for (int k = 0; k < 200 && st_size(M8_BASE "/launched") <= 0; k++) SDL_Delay(5);
        SDL_Delay(20);
        char buf[256] = {0};
        int fd = open(M8_BASE "/launched", O_RDONLY);
        if (fd >= 0) { ssize_t r = read(fd, buf, sizeof buf - 1); (void)r; close(fd); }
        ST_CHECK(!strcmp(buf, M8_BASE "/dir\n"));
        unlink(M8_BASE "/launched");
    }
    { // apps picked for a type through the sheet (newest first, kept across runs), the "Open with" submenu
        DesktopApp *ap[2]; // apps that don't list folders themselves
        int na = 0;
        for (int i = 0; i < g_napps && na < 2; i++)
            if (!g_apps[i].nodisplay && !ow_type_app("inode/directory", &g_apps[i])) ap[na++] = &g_apps[i];
        if (na == 2) {
            recent_apps_push("inode/directory", ap[0]->id);
            recent_apps_push("inode/directory", ap[1]->id);
            recent_apps_push("inode/directory", ap[0]->id);
            recent_apps_push("text/plain", ap[1]->id);
            free(g_rapps); g_rapps = NULL; g_nrapps = 0; g_rapps_loaded = false; // read back from the file
            DesktopApp *ra[RECENT_APPS];
            ST_CHECK(recent_apps_for("inode/directory", ra, RECENT_APPS) == 2 && ra[0] == ap[0] && ra[1] == ap[1]);
            ST_CHECK(recent_apps_for("text/plain", ra, RECENT_APPS) == 1 && ra[0] == ap[1]);
            ST_CHECK(recent_apps_for("image/png", ra, RECENT_APPS) == 0);

            st_select(p, "dir");
            menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), true);
            int oi = -1;
            for (int i = 0; i < A.menu.n; i++)
                if (A.menu.items[i].label && !strcmp(A.menu.items[i].label, "Open with")) oi = i;
            ST_CHECK(oi >= 0 && A.menu.items[oi].sub);
            A.menu.hover = oi;
            menu_key(SDLK_RIGHT); // the keyboard opens it, on its first app
            { // "Recent": the picked ones (newest first), "Suggested": the type's own apps (as the sheet lists them), then "Other app..."
                DesktopApp *ta[OW_MENU_TYPE_APPS];
                int nt = apps_for_mime("inode/directory", ta, OW_MENU_TYPE_APPS);
                Menu *sm = &g_menu_sub;
                ST_CHECK(g_ow_menu.npick == 2 && g_ow_menu.n == nt + 2);
                if (nt) {
                    ST_CHECK(menu_sub_shown() && g_menu_sub_of == oi && sm->n == 1 + 2 + 2 + nt + 2 && sm->hover == 1);
                    ST_CHECK(sm->items[0].head && !strcmp(sm->items[0].label, "Recent") && !sm->items[0].enabled);
                    ST_CHECK(!strcmp(sm->items[1].label, ap[0]->name) && sm->items[1].act == ACT_OW_APP_BASE);
                    ST_CHECK(!strcmp(sm->items[2].label, ap[1]->name));
                    ST_CHECK(sm->items[3].sep && sm->items[4].head && !strcmp(sm->items[4].label, "Suggested"));
                    ST_CHECK(!strcmp(sm->items[5].label, ta[0]->name) && sm->items[5].act == ACT_OW_APP_BASE + 2);
                    ST_CHECK(g_ow_menu.def == (ta[0] == app_default_for("inode/directory") ? 2 : -1));
                    menu_key(SDLK_UP); // titles are skipped: from the first app up to "Other app..."
                    ST_CHECK(sm->hover == sm->n - 1);
                    menu_key(SDLK_DOWN);
                    ST_CHECK(sm->hover == 1);
                } else { // no section titles with one section
                    ST_CHECK(menu_sub_shown() && sm->n == 2 + 2 && !sm->items[0].head && sm->hover == 0);
                }
                ST_CHECK(!strcmp(sm->items[sm->n - 1].label, "Other app\xE2\x80\xA6"));
            }
            ST_CHECK(g_menu_sub.r.x >= A.menu.r.x + A.menu.r.w - S(8) || g_menu_sub.r.x + g_menu_sub.r.w <= A.menu.r.x + S(8));
            st_frames(2);
            st_shot("openwith-submenu");
            menu_key(SDLK_LEFT); // back to the menu, which stays open
            ST_CHECK(A.menu.open && !menu_sub_shown() && A.menu.hover == oi);
            { // hovering the item opens it too; another item closes it
                f32 iy = menu_item_y(&A.menu, oi) + S(8);
                menu_motion(A.menu.r.x + S(20), iy);
                ST_CHECK(menu_sub_shown() && g_menu_sub.hover < 0);
                menu_motion(g_menu_sub.r.x + S(20), menu_item_y(&g_menu_sub, 0) + S(8)); // a section title: nothing
                ST_CHECK(menu_sub_shown() && g_menu_sub.hover == (g_menu_sub.items[0].head ? -1 : 0) && A.menu.hover == oi);
                menu_motion(g_menu_sub.r.x + S(20), menu_item_y(&g_menu_sub, 1) + S(8));
                ST_CHECK(menu_sub_shown() && g_menu_sub.hover == 1 && A.menu.hover == oi);
                int other = oi > 0 ? oi - 1 : oi + 1;
                while (other > 0 && (A.menu.items[other].sep || !A.menu.items[other].enabled)) other--;
                menu_motion(A.menu.r.x + S(20), menu_item_y(&A.menu, other) + S(8));
                ST_CHECK(!menu_sub_shown() && A.menu.open);
            }
            menu_sub_open(oi, true);
            menu_key(SDLK_UP); // wraps round to "Other app..."
            menu_key(SDLK_RETURN);
            ST_CHECK(!A.menu.open && g_sheet.kind == SHEET_OPENWITH && !strcmp(g_sheet.mime, "inode/directory"));
            ST_CHECK(g_sheet.npaths == 1 && !strcmp(g_sheet.paths[0], M8_BASE "/dir"));
            st_frames(2);
            st_shot("openwith-folder");
            ST_CHECK(g_sheet.rcheck_def.w == 0); // no "Always use this app" for folders (that's the file manager)
            st_key(SDLK_ESCAPE, SDL_KMOD_NONE);

            // the folder's own menu: "Open this folder with" is about the folder, not the selection
            menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), false);
            oi = -1;
            for (int i = 0; i < A.menu.n; i++)
                if (A.menu.items[i].label && !strcmp(A.menu.items[i].label, "Open this folder with")) oi = i;
            ST_CHECK(oi >= 0 && A.menu.items[oi].sub);
            menu_reveal_in(&A.menu, oi); // (a long menu: scrolled to it, as hovering or the arrow keys would)
            menu_activate(oi); // a click opens the submenu, the menu stays
            ST_CHECK(A.menu.open && menu_sub_shown());
            ST_CHECK(g_menu_sub.r.x == A.menu.r.x + A.menu.r.w); // grows out of the menu's side
            st_frames(2);
            st_shot("openwith-submenu-folder");
            if (menu_scrollable()) { // scrolling the menu closes its submenu (it would float off its item)
                menu_scroll_by(-S(28));
                ST_CHECK(A.menu.open && !menu_sub_shown());
                menu_sub_open(oi, false);
            }
            menu_sub_activate(g_menu_sub.n - 1);
            ST_CHECK(!A.menu.open && !menu_sub_shown() && g_sheet.kind == SHEET_OPENWITH && !strcmp(g_sheet.paths[0], p->path));
            st_key(SDLK_ESCAPE, SDL_KMOD_NONE);

            for (int zi = 0; zi < 2; zi++) { // other zoom levels: whole pixels, still one shape (or apart when there's no room)
                set_zoom(zi ? 2.0f : 1.3f);
                layout();
                st_frames(2);
                st_select(p, "dir");
                menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), true);
                for (int i = 0; i < A.menu.n; i++)
                    if (A.menu.items[i].sub) { menu_sub_open(i, true); break; }
                Rect mr = A.menu.r, sr = g_menu_sub.r;
                ST_CHECK(menu_sub_shown() && mr.h == roundf(mr.h) && sr.h == roundf(sr.h) && mr.y == roundf(mr.y) && sr.y == roundf(sr.y));
                bool beside = sr.x == mr.x + mr.w || sr.x + sr.w == mr.x;
                ST_CHECK(beside || (A.W - S(4) - (mr.x + mr.w) < sr.w && mr.x - S(4) < sr.w)); // apart only with no room on either side
                st_frames(2);
                st_shot(zi ? "openwith-submenu-zoom200" : "openwith-submenu-zoom130");
                A.menu.open = false;
            }
            set_zoom(1.f);
            layout();
            st_frames(2);

            { // no room on the right: it grows out of the menu's left side
                st_select(p, "dir");
                menu_open(p, (f32)A.W, p->ly.list.y + S(40), true);
                for (int i = 0; i < A.menu.n; i++)
                    if (A.menu.items[i].sub) { menu_sub_open(i, false); break; }
                ST_CHECK(menu_sub_shown() && g_menu_sub.r.x + g_menu_sub.r.w == A.menu.r.x);
                st_frames(2);
                st_shot("openwith-submenu-left");
                A.menu.open = false;
            }

            // nothing picked for a type: just its own apps (the sheet's list) + "Other app..."
            st_select(p, "photo1.jpg");
            menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), true);
            oi = -1;
            for (int i = 0; i < A.menu.n; i++)
                if (A.menu.items[i].label && !strcmp(A.menu.items[i].label, "Open with")) oi = i;
            ST_CHECK(oi >= 0 && A.menu.items[oi].sub);
            {
                DesktopApp *ta[OW_MENU_TYPE_APPS];
                int nt = apps_for_mime("image/jpeg", ta, OW_MENU_TYPE_APPS);
                menu_sub_open(oi, false);
                ST_CHECK(menu_sub_shown() && g_ow_menu.n == nt && g_ow_menu.npick == 0 && g_menu_sub.n == nt + 1 + (nt > 0));
                for (int k = 0; k < g_menu_sub.n; k++) ST_CHECK(!g_menu_sub.items[k].head); // one section: no titles
            }
            A.menu.open = false;
        } else {
            fprintf(stderr, "selftest-m8: fewer than 2 apps installed, recent apps checks skipped\n");
        }
    }
    ST_CHECK(!sheet_open());

    { // Delete permanently: danger badge + red default button + the items listed
        sel_clear(p);
        for (u32 i = 0; i < p->count && i < 6; i++) sel_set(p, i, true);
        do_action(p, ACT_DELETE);
        ST_CHECK(A.dlg.open && A.dlg.icon == DI_DANGER && A.dlg.danger && A.dlg.nitems == 4 && A.dlg.more == (int)MIN(p->count, 6) - 4);
        st_frames(2);
        st_shot("delete-dialog");
        if (A.dlg.open) dialog_choose(A.dlg.cancel);
        ST_CHECK(!A.dlg.open && A.dlg.nitems == 0);
        sel_clear(p);
    }
    // Properties: folder stats, file permissions, checksums
    st_select(p, "dir");
    st_key(SDLK_RETURN, SDL_KMOD_ALT);
    ST_CHECK(g_sheet.kind == SHEET_PROPS);
    ST_CHECK(st_wait_cond(c_stats_done, 3000));
    ST_CHECK(atomic_load(&g_sheet.stats->size) == 3000 && atomic_load(&g_sheet.stats->files) == 2);
    st_frames(2);
    st_shot("properties-folder");
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    { // Properties > Media: an image's size from the file; a video's / a song's details through ffprobe, a PDF's pages
      // through pdfinfo (stand-ins here); no card for other files
        const char *lab[16];
        char val[16][160];
        st_make_bmp(M8_BASE "/pic.bmp", 640, 480);
        char *pv[1] = {M8_BASE "/pic.bmp"};
        props_open(pv, 1);
        ST_CHECK(g_sheet.media && st_wait_cond(c_media_done, 3000));
        ST_CHECK(props_media_rows(g_sheet.media, lab, val, 160) == 1 && !strcmp(lab[0], "Dimensions") && !strcmp(val[0], "640 \xC3\x97 480 pixels"));
        st_frames(2);
        st_shot("properties-image");
        sheet_close();
        char fp[PATH_MAX], pp2[PATH_MAX];
        snprintf(fp, sizeof fp, "%s/ffprobe", M8_BASE);
        snprintf(pp2, sizeof pp2, "%s/pdfinfo", M8_BASE);
        FILE *f = fopen(fp, "w");
        if (f) {
            fputs("#!/bin/sh\ncase \"$*\" in *clip.mp4*)\n"
                  "printf '[STREAM]\\ncodec_name=h264\\ncodec_type=video\\nwidth=1920\\nheight=1080\\navg_frame_rate=30000/1001\\nDISPOSITION:attached_pic=0\\n[/STREAM]\\n"
                  "[STREAM]\\ncodec_name=aac\\ncodec_type=audio\\nsample_rate=48000\\nchannels=2\\navg_frame_rate=0/0\\nDISPOSITION:attached_pic=0\\n[/STREAM]\\n"
                  "[FORMAT]\\nduration=3725.4\\nbit_rate=5000000\\n[/FORMAT]\\n';;\n"
                  "*) printf '[STREAM]\\ncodec_name=mp3\\ncodec_type=audio\\nsample_rate=44100\\nchannels=2\\navg_frame_rate=0/0\\nDISPOSITION:attached_pic=0\\n[/STREAM]\\n"
                  "[STREAM]\\ncodec_name=mjpeg\\ncodec_type=video\\nwidth=500\\nheight=500\\navg_frame_rate=0/0\\nDISPOSITION:attached_pic=1\\n[/STREAM]\\n"
                  "[FORMAT]\\nduration=185.2\\nbit_rate=320000\\nTAG:title=Song\\nTAG:artist=Band\\nTAG:album=Record\\n[/FORMAT]\\n';;\nesac\n", f);
            fclose(f);
            chmod(fp, 0755);
        }
        f = fopen(pp2, "w");
        if (f) { fputs("#!/bin/sh\nprintf 'Title:           A Report\\nAuthor:          Someone\\nPages:           12\\n'\n", f); fclose(f); chmod(pp2, 0755); }
        setenv("FE_FFPROBE", fp, 1);
        setenv("FE_PDFINFO", pp2, 1);
        static const struct { const char *file; int rows; const char *want[10]; } cases[] = {
            {"clip.mp4", 4, {"Dimensions", "1920 \xC3\x97 1080 pixels", "Duration", "1:02:05", "Video", "H.264 \xC2\xB7 29.97 fps", "Audio", "AAC \xC2\xB7 stereo \xC2\xB7 48 kHz"}},
            {"song.mp3", 6, {"Duration", "3:05", "Audio", "MP3 \xC2\xB7 stereo \xC2\xB7 44.1 kHz", "Bit rate", "320 kbps", "Title", "Song", "Artist", "Band"}},
            {"doc.pdf", 3, {"Pages", "12", "Title", "A Report", "Author", "Someone"}},
        };
        for (size_t c = 0; c < ARRAY_COUNT(cases); c++) {
            char fpath[PATH_MAX];
            snprintf(fpath, sizeof fpath, "%s/%s", M8_BASE, cases[c].file);
            st_write(fpath, 100);
            char *v1[1] = {fpath};
            props_open(v1, 1);
            ST_CHECK(g_sheet.media && st_wait_cond(c_media_done, 3000));
            int nr = props_media_rows(g_sheet.media, lab, val, 160);
            ST_CHECK(nr == cases[c].rows);
            for (int i = 0; i < nr && i < 5; i++)
                if (cases[c].want[i * 2] && (strcmp(lab[i], cases[c].want[i * 2]) || strcmp(val[i], cases[c].want[i * 2 + 1]))) {
                    fprintf(stderr, "SELFTEST: %s row %d: %s = %s\n", cases[c].file, i, lab[i], val[i]);
                    st_fail++;
                }
            if (c == 0) { st_frames(2); st_shot("properties-video"); }
            sheet_close();
            unlink(fpath);
        }
        unsetenv("FE_FFPROBE");
        unsetenv("FE_PDFINFO");
        char *tv[1] = {M8_BASE "/abc.txt"};
        props_open(tv, 1);
        ST_CHECK(!g_sheet.media);
        sheet_close();
        unlink(fp);
        unlink(pp2);
        unlink(M8_BASE "/pic.bmp");
    }
    { // Properties > Media for a photo: its EXIF (when, camera, lens, exposure, where), next to its size
        static const char jpg[] =
            "\xff\xd8\xff\xe0\x00\x10\x4a\x46\x49\x46\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00\xff\xe1\x01\x4a\x45\x78\x69\x66\x00\x00\x4d\x4d\x00\x2a\x00\x00\x00\x08\x00\x04"
            "\x01\x0f\x00\x02\x00\x00\x00\x07\x00\x00\x00\x3e\x01\x10\x00\x02\x00\x00\x00\x08\x00\x00\x00\x46\x87\x69\x00\x04\x00\x00\x00\x01\x00\x00\x00\x4e\x88\x25\x00\x04"
            "\x00\x00\x00\x01\x00\x00\x00\xdc\x00\x00\x00\x00\x47\x6f\x6f\x67\x6c\x65\x00\x00\x50\x69\x78\x65\x6c\x20\x38\x00\x00\x06\x82\x9a\x00\x05\x00\x00\x00\x01\x00\x00"
            "\x00\x9c\x82\x9d\x00\x05\x00\x00\x00\x01\x00\x00\x00\xa4\x88\x27\x00\x03\x00\x00\x00\x01\x00\x64\x00\x00\x90\x03\x00\x02\x00\x00\x00\x14\x00\x00\x00\xac\x92\x0a"
            "\x00\x05\x00\x00\x00\x01\x00\x00\x00\xc0\xa4\x34\x00\x02\x00\x00\x00\x14\x00\x00\x00\xc8\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x78\x00\x00\x00\x09\x00\x00"
            "\x00\x05\x32\x30\x32\x34\x3a\x30\x35\x3a\x30\x31\x20\x31\x34\x3a\x32\x33\x3a\x31\x31\x00\x00\x00\x00\x45\x00\x00\x00\x0a\x50\x69\x78\x65\x6c\x20\x38\x20\x62\x61"
            "\x63\x6b\x20\x63\x61\x6d\x65\x72\x61\x00\x00\x04\x00\x01\x00\x02\x00\x00\x00\x02\x4e\x00\x00\x00\x00\x02\x00\x05\x00\x00\x00\x03\x00\x00\x01\x12\x00\x03\x00\x02"
            "\x00\x00\x00\x02\x45\x00\x00\x00\x00\x04\x00\x05\x00\x00\x00\x03\x00\x00\x01\x2a\x00\x00\x00\x00\x00\x00\x00\x34\x00\x00\x00\x01\x00\x00\x00\x16\x00\x00\x00\x01"
            "\x00\x00\x06\x3f\x00\x00\x00\x7d\x00\x00\x00\x04\x00\x00\x00\x01\x00\x00\x00\x35\x00\x00\x00\x01\x00\x00\x29\x9d\x00\x00\x00\xfa\xff\xdb\x00\x43\x00\x10\x0b\x0c"
            "\x0e\x0c\x0a\x10\x0e\x0d\x0e\x12\x11\x10\x13\x18\x28\x1a\x18\x16\x16\x18\x31\x23\x25\x1d\x28\x3a\x33\x3d\x3c\x39\x33\x38\x37\x40\x48\x5c\x4e\x40\x44\x57\x45\x37"
            "\x38\x50\x6d\x51\x57\x5f\x62\x67\x68\x67\x3e\x4d\x71\x79\x70\x64\x78\x5c\x65\x67\x63\xff\xdb\x00\x43\x01\x11\x12\x12\x18\x15\x18\x2f\x1a\x1a\x2f\x63\x42\x38\x42"
            "\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63"
            "\x63\x63\x63\x63\x63\x63\x63\x63\x63\x63\xff\xc0\x00\x11\x08\x00\x0c\x00\x10\x03\x01\x22\x00\x02\x11\x01\x03\x11\x01\xff\xc4\x00\x1f\x00\x00\x01\x05\x01\x01\x01"
            "\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\xff\xc4\x00\xb5\x10\x00\x02\x01\x03\x03\x02\x04\x03\x05\x05\x04\x04\x00"
            "\x00\x01\x7d\x01\x02\x03\x00\x04\x11\x05\x12\x21\x31\x41\x06\x13\x51\x61\x07\x22\x71\x14\x32\x81\x91\xa1\x08\x23\x42\xb1\xc1\x15\x52\xd1\xf0\x24\x33\x62\x72\x82"
            "\x09\x0a\x16\x17\x18\x19\x1a\x25\x26\x27\x28\x29\x2a\x34\x35\x36\x37\x38\x39\x3a\x43\x44\x45\x46\x47\x48\x49\x4a\x53\x54\x55\x56\x57\x58\x59\x5a\x63\x64\x65\x66"
            "\x67\x68\x69\x6a\x73\x74\x75\x76\x77\x78\x79\x7a\x83\x84\x85\x86\x87\x88\x89\x8a\x92\x93\x94\x95\x96\x97\x98\x99\x9a\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xb2\xb3"
            "\xb4\xb5\xb6\xb7\xb8\xb9\xba\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xf1\xf2\xf3\xf4\xf5"
            "\xf6\xf7\xf8\xf9\xfa\xff\xc4\x00\x1f\x01\x00\x03\x01\x01\x01\x01\x01\x01\x01\x01\x01\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\xff\xc4"
            "\x00\xb5\x11\x00\x02\x01\x02\x04\x04\x03\x04\x07\x05\x04\x04\x00\x01\x02\x77\x00\x01\x02\x03\x11\x04\x05\x21\x31\x06\x12\x41\x51\x07\x61\x71\x13\x22\x32\x81\x08"
            "\x14\x42\x91\xa1\xb1\xc1\x09\x23\x33\x52\xf0\x15\x62\x72\xd1\x0a\x16\x24\x34\xe1\x25\xf1\x17\x18\x19\x1a\x26\x27\x28\x29\x2a\x35\x36\x37\x38\x39\x3a\x43\x44\x45"
            "\x46\x47\x48\x49\x4a\x53\x54\x55\x56\x57\x58\x59\x5a\x63\x64\x65\x66\x67\x68\x69\x6a\x73\x74\x75\x76\x77\x78\x79\x7a\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x92\x93"
            "\x94\x95\x96\x97\x98\x99\x9a\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xd2\xd3\xd4\xd5\xd6\xd7"
            "\xd8\xd9\xda\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xff\xda\x00\x0c\x03\x01\x00\x02\x11\x03\x11\x00\x3f\x00\xad\x45\x14\x57\x98"
            "\x7b\xc7\xff\xd9";
        FILE *f = fopen(M8_BASE "/photo-exif.jpg", "wb");
        if (f) { fwrite(jpg, 1, sizeof jpg - 1, f); fclose(f); }
        char *jv[1] = {M8_BASE "/photo-exif.jpg"};
        props_open(jv, 1);
        ST_CHECK(g_sheet.media && st_wait_cond(c_media_done, 3000));
        const char *lab[16];
        char val[16][160], when[160];
        struct tm tm = {.tm_year = 124, .tm_mon = 4, .tm_mday = 1, .tm_hour = 14, .tm_min = 23, .tm_sec = 11, .tm_isdst = -1};
        props_date((i64)mktime(&tm), when, sizeof when);
        static const char *want[][2] = {{"Dimensions", "16 \xC3\x97 12 pixels"}, {"Taken", NULL}, {"Camera", "Google Pixel 8"},
                                        {"Lens", "Pixel 8 back camera"}, {"Exposure", "f/1.8 \xC2\xB7 1/120 s \xC2\xB7 ISO 100 \xC2\xB7 6.9 mm"},
                                        {"Location", "52.37022\xC2\xB0 N, 4.89517\xC2\xB0 E"}};
        int nr = props_media_rows(g_sheet.media, lab, val, 160);
        ST_CHECK(nr == (int)ARRAY_COUNT(want));
        for (int i = 0; i < nr && i < (int)ARRAY_COUNT(want); i++)
            if (strcmp(lab[i], want[i][0]) || strcmp(val[i], want[i][1] ? want[i][1] : when)) {
                fprintf(stderr, "SELFTEST: photo row %d: %s = %s\n", i, lab[i], val[i]);
                st_fail++;
            }
        st_frames(2);
        st_shot("properties-photo");
        sheet_close();
        f = fopen(M8_BASE "/photo-exif.jpg", "wb"); // a cut-off EXIF block: no crash, no rows from it
        if (f) { fwrite(jpg, 1, 60, f); fclose(f); }
        props_open(jv, 1);
        ST_CHECK(g_sheet.media && st_wait_cond(c_media_done, 3000) && !g_sheet.media->camera[0] && !g_sheet.media->taken);
        sheet_close();
        unlink(M8_BASE "/photo-exif.jpg");
    }
    { // Properties > Archive: files, folders and the unpacked size, counted on a worker; a broken one says so
        struct archive *w = archive_write_new();
        archive_write_set_format_zip(w);
        archive_write_open_filename(w, M8_BASE "/box.zip");
        static const struct { const char *name; int dir; size_t size; } ents[] = {{"d/", 1, 0}, {"d/a.txt", 0, 1000}, {"b.txt", 0, 500}};
        char data[1000];
        memset(data, 'x', sizeof data);
        for (size_t i = 0; i < ARRAY_COUNT(ents); i++) {
            struct archive_entry *e = archive_entry_new();
            archive_entry_set_pathname(e, ents[i].name);
            archive_entry_set_filetype(e, ents[i].dir ? AE_IFDIR : AE_IFREG);
            archive_entry_set_perm(e, ents[i].dir ? 0755 : 0644);
            archive_entry_set_size(e, (la_int64_t)ents[i].size);
            archive_write_header(w, e);
            if (ents[i].size) archive_write_data(w, data, ents[i].size);
            archive_entry_free(e);
        }
        archive_write_close(w);
        archive_write_free(w);
        char *av[1] = {M8_BASE "/box.zip"};
        props_open(av, 1);
        ST_CHECK(g_sheet.arc && st_wait_cond(c_arc_done, 3000));
        ST_CHECK(!atomic_load(&g_sheet.arc->failed) && atomic_load(&g_sheet.arc->files) == 2 && atomic_load(&g_sheet.arc->dirs) == 1 &&
                 atomic_load(&g_sheet.arc->size) == 1500);
        st_frames(2);
        st_shot("properties-archive");
        sheet_close();
        st_write(M8_BASE "/broken.zip", 300);
        char *bv[1] = {M8_BASE "/broken.zip"};
        props_open(bv, 1);
        ST_CHECK(g_sheet.arc && st_wait_cond(c_arc_done, 3000) && atomic_load(&g_sheet.arc->failed));
        sheet_close();
        unlink(M8_BASE "/box.zip");
        unlink(M8_BASE "/broken.zip");
    }
    st_select(p, "abc.txt");
    do_action(p, ACT_PROPERTIES);
    st_frames(2);
    mode_t m0 = g_sheet.st.st_mode & 07777;
    props_toggle_perm(2); // owner execute
    struct stat st2;
    stat(M8_BASE "/abc.txt", &st2);
    ST_CHECK((st2.st_mode & 07777) == (m0 ^ 0100));
    props_toggle_perm(2);
    g_sheet.sum = sum_start(g_sheet.paths[0]);
    ST_CHECK(st_wait_cond(c_sum_done, 3000));
    ST_CHECK(!strcmp(g_sheet.sum->sha256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    ST_CHECK(!strcmp(g_sheet.sum->md5, "900150983cd24fb0d6963f7d28e17f72"));
    st_frames(2);
    st_shot("properties");
    { // per-row Copy buttons; the body scrolls when it doesn't fit; Show opens the folder with the file selected
        SDL_MouseButtonEvent mb = {.type = SDL_EVENT_MOUSE_BUTTON_DOWN, .button = SDL_BUTTON_LEFT, .clicks = 1};
        if (g_sheet.rcopy_md5.w > 0 && in_rect(g_sheet.rbody, g_sheet.rcopy_md5.x + 2, g_sheet.rcopy_md5.y + 2)) {
            mb.x = g_sheet.rcopy_md5.x + 4; mb.y = g_sheet.rcopy_md5.y + 4;
            sheet_mouse(&mb);
            char *cb = SDL_GetClipboardText();
            ST_CHECK(cb && !strcmp(cb, "900150983cd24fb0d6963f7d28e17f72"));
            SDL_free(cb);
        }
        int h0 = A.H;
        A.H = (int)roundf(S(420)); // small window: content overflows
        st_frames(2);
        ST_CHECK(g_sheet.pv_content > g_sheet.rbody.h);
        sheet_wheel(-20);
        st_settle();
        ST_CHECK(g_sheet.pv_scroll > 0 && g_sheet.pv_scroll <= g_sheet.pv_content - g_sheet.rbody.h + 0.5f);
        st_shot("properties-scrolled");
        A.H = h0;
        g_sheet.pv_scroll = 0;
        st_frames(2);
        ST_CHECK(g_sheet.rreveal.w > 0);
        mb.x = g_sheet.rreveal.x + 4; mb.y = g_sheet.rreveal.y + 4;
        sheet_mouse(&mb);
        st_wait();
        for (int k = 0; k < 100 && strcmp(st_cursor_name(active_pane()), "abc.txt"); k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(!sheet_open() && !strcmp(active_pane()->path, M8_BASE) && !strcmp(st_cursor_name(active_pane()), "abc.txt"));
    }
    sheet_close();
    { // several items: header + General totals only
        sel_clear(p);
        for (u32 i = 0; i < p->count; i++) sel_set(p, i, true);
        do_action(p, ACT_PROPERTIES);
        st_wait_cond(c_stats_done, 3000);
        st_frames(2);
        ST_CHECK(g_sheet.kind == SHEET_PROPS && g_sheet.npaths > 1);
        st_shot("properties-multi");
        sheet_close();
        sel_clear(p);
    }

    // Mouse in a text field (inline rename): click places the caret, drag selects, Shift-click extends,
    // double-click picks a word, triple-click everything; none of it ends the rename
    sel_clear(p);
    ST_CHECK(st_select(p, "photo1.jpg"));
    st_key(SDLK_F2, SDL_KMOD_NONE);
    ST_CHECK(A.ren_pane == p && !strcmp(A.ren_te.buf, "photo1.jpg"));
    st_text_click(&A.ren_te, 2, 1, 0);
    ST_CHECK(A.ren_pane == p && A.ren_te.anchor == 2 && A.ren_te.caret == 2);
    {
        f32 x0 = 0, x1 = 0, y = 0;
        ST_CHECK(st_text_at(&A.ren_te, 1, &x0, &y) && st_text_at(&A.ren_te, 5, &x1, &y));
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, x0, y, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, x1, y, 0, 0, 0);
        ST_CHECK(A.ren_te.anchor == 1 && A.ren_te.caret == 5);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, x1, y, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, x0, y, 0, 0, 0); // released: moving no longer selects
        ST_CHECK(A.ren_te.anchor == 1 && A.ren_te.caret == 5);
    }
    st_text_click(&A.ren_te, 8, 1, SDL_KMOD_SHIFT);
    ST_CHECK(A.ren_te.anchor == 1 && A.ren_te.caret == 8);
    st_text_click(&A.ren_te, 3, 2, 0);
    ST_CHECK(A.ren_te.anchor == 0 && A.ren_te.caret == 6); // "photo1" (the dot ends the word)
    st_text_click(&A.ren_te, 3, 3, 0);
    ST_CHECK(A.ren_te.anchor == 0 && A.ren_te.caret == 10);
    ST_CHECK(A.ren_pane == p);
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    ST_CHECK(!A.ren_pane && st_exists(M8_BASE "/photo1.jpg"));

    // Batch rename: F2 with several selected
    sel_clear(p);
    for (u32 i = 0; i < p->count; i++)
        if (!strncmp(p->L->entries[p->order[i]].name, "photo", 5)) sel_set(p, i, true);
    ST_CHECK(p->sel_count == 3);
    st_key(SDLK_F2, SDL_KMOD_NONE);
    ST_CHECK(g_sheet.kind == SHEET_RENAME && g_sheet.npaths == 3);
    on_text("holiday_{n:2}"); // replaces the pre-selected "{name}"
    ST_CHECK(!strcmp(g_sheet.tmpl.buf, "holiday_{n:2}{ext}") && !strcmp(g_sheet.newnames[0], "holiday_01.jpg"));
    st_frames(2);
    st_shot("batchrename");
    // a click in another field moves the keyboard there, the caret where it was clicked
    st_text_click(&g_sheet.find, 0, 1, 0);
    ST_CHECK(g_sheet.kind == SHEET_RENAME && g_sheet.focus == 1);
    st_text_click(&g_sheet.tmpl, 4, 1, 0);
    ST_CHECK(g_sheet.focus == 0 && g_sheet.tmpl.anchor == 4 && g_sheet.tmpl.caret == 4);
    // a pattern that makes every name equal is refused
    te_set(&g_sheet.tmpl, "same.jpg", 8, 8);
    rename_recompute();
    ST_CHECK(g_sheet.nconflicts == 3);
    te_set(&g_sheet.tmpl, "holiday_{n:2}{ext}", 0, 0);
    g_sheet.focus = 1;
    on_text("holiday");
    g_sheet.focus = 2;
    on_text("trip");
    rename_recompute();
    ST_CHECK(!strcmp(g_sheet.newnames[2], "trip_03.jpg") && !g_sheet.nconflicts);
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(st_exists(M8_BASE "/trip_01.jpg") && st_exists(M8_BASE "/trip_03.jpg") && !st_exists(M8_BASE "/photo1.jpg"));
    ST_CHECK(p->sel_count == 3);
    st_key(SDLK_Z, SDL_KMOD_CTRL);
    st_wait();
    ST_CHECK(st_exists(M8_BASE "/photo1.jpg") && st_exists(M8_BASE "/photo3.jpg") && !st_exists(M8_BASE "/trip_01.jpg"));
    // swapping names works thanks to the two-phase rename
    char *from[2] = {M8_BASE "/photo1.jpg", M8_BASE "/photo2.jpg"};
    char *to[2] = {M8_BASE "/photo2.jpg", M8_BASE "/photo1.jpg"};
    st_write(M8_BASE "/photo1.jpg", 111);
    ST_CHECK(rename_many(from, to, 2) == 2 && st_size(M8_BASE "/photo2.jpg") == 111);

    // type filter: files only, then hide .jpg (popup stays open while toggling)
    pane_navigate(p, M8_BASE, true); st_wait();
    u32 all = p->count;
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, p->ly.filterbtn.x + 5, p->ly.filterbtn.y + 5, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(A.menu.open && A.menu.keep_open && A.menu.items[0].checked);
    menu_activate(1); // Files only
    st_wait();
    ST_CHECK(A.menu.open && p->tf_mode == 1 && p->count == all - 2); // dir + xdg hidden
    int jpg = -1;
    for (int i = 0; i < g_ntf; i++)
        if (!strcmp(g_tf_keys[i], "jpg")) jpg = i;
    ST_CHECK(jpg >= 0);
    for (int i = 0; i < A.menu.n; i++)
        if (A.menu.items[i].act == ACT_TF_EXT_BASE + jpg) { menu_activate(i); break; }
    st_wait();
    ST_CHECK(p->count == all - 2 - 3 && strstr(p->tf_exts, "|jpg|"));
    st_shot("typefilter");
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    do_action(p, ACT_TF_RESET);
    st_wait();
    ST_CHECK(p->count == all && !p->tf_exts[0]);

    // trash view: trash a file, see where it came from, restore it, delete permanently, empty
    st_select(p, "abc.txt");
    st_key(SDLK_DELETE, SDL_KMOD_NONE);
    st_wait_ops();
    ST_CHECK(!st_exists(M8_BASE "/abc.txt"));
    int ti = -1;
    for (int i = 0; i < A.nplaces; i++)
        if (A.places[i].kind == 4) ti = i;
    ST_CHECK(ti >= 0 && !strcmp(A.places[ti].path, M8_BASE "/xdg/Trash/files"));
    pane_navigate(p, A.places[ti].path, true); st_wait();
    ST_CHECK(pane_is_trash(p) && p->ly.has_trashbar && p->count == 1);
    { // shown as "Trash": tab / title name, a single crumb in the path bar
        char dnb[NAME_MAX + 1];
        ST_CHECK(!strcmp(disp_name(p->path, dnb, sizeof dnb), "Trash"));
        layout_crumbs(p);
        ST_CHECK(p->ly.crumbs_trash && p->ly.ncrumbs == 1 && p->ly.crumbs[0].end == (int)strlen(p->path));
    }
    {
        char orig[PATH_MAX], date[32];
        ST_CHECK(trash_info("abc.txt", orig, sizeof orig, date, sizeof date) && !strcmp(orig, M8_BASE "/abc.txt"));
    }
    for (int k = 0; k < 300 && (A.trash_job || !A.trash_known); k++) { st_frames(1); SDL_Delay(5); }
    ST_CHECK(A.trash_known && A.trash_size == 3 && !A.trash_empty); // abc.txt ("abc") is in it: the sidebar shows its size
    st_shot("trash");
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, p->ly.rrestore.x + 5, p->ly.rrestore.y + 5, SDL_BUTTON_LEFT, 1, 0);
    st_wait_ops();
    for (int k = 0; k < 300 && A.trash_job; k++) { st_frames(1); SDL_Delay(5); }
    ST_CHECK(A.trash_known && A.trash_size == 0 && A.trash_empty); // restored: empty again (the empty bin)
    ST_CHECK(st_exists(M8_BASE "/abc.txt") && !st_exists(M8_BASE "/xdg/Trash/info/abc.txt.trashinfo") && p->count == 0);
    pane_navigate(p, M8_BASE, true); st_wait();
    st_select(p, "abc.txt");
    st_key(SDLK_DELETE, SDL_KMOD_NONE);
    st_wait_ops();
    st_select(p, "photo3.jpg");
    st_key(SDLK_DELETE, SDL_KMOD_NONE);
    st_wait_ops();
    pane_navigate(p, A.places[ti].path, true); st_wait();
    ST_CHECK(p->count == 2);
    st_select(p, "abc.txt");
    st_key(SDLK_DELETE, SDL_KMOD_NONE); // in the trash: permanent, asks first
    ST_CHECK(A.dlg.open);
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait_ops();
    ST_CHECK(p->count == 1 && !st_exists(M8_BASE "/xdg/Trash/info/abc.txt.trashinfo"));
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, p->ly.rempty.x + 5, p->ly.rempty.y + 5, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(A.dlg.open);
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait_ops();
    ST_CHECK(p->count == 0 && !st_exists(M8_BASE "/xdg/Trash/files/photo3.jpg") && !st_exists(M8_BASE "/xdg/Trash/info/photo3.jpg.trashinfo"));
    // Empty Trash from Quick Access / the palette while another folder is open: the trash's own contents (a hidden
    // empty file counts), never the open folder's names; asked first, read off the UI thread
    pane_navigate(p, M8_BASE, true); st_wait();
    for (int k = 0; k < 300 && (A.trash_job || !A.trash_known); k++) { st_frames(1); SDL_Delay(5); }
    int te = -1;
    menu_open_place(ti, A.r_sidebar.x + S(40), S(200));
    for (int i = 0; i < A.menu.n; i++)
        if (A.menu.items[i].act == ACT_TRASH_EMPTY) te = i;
    ST_CHECK(A.menu.open && te >= 0 && !A.menu.items[te].enabled && A.trash_empty); // nothing in it
    A.menu.open = false;
    st_write(M8_BASE "/xdg/Trash/files/.hidden-trash", 0);
    st_write(M8_BASE "/xdg/Trash/info/.hidden-trash.trashinfo", 1);
    st_write(M8_BASE "/xdg/Trash/files/photo1.jpg", 5); // the same name as a file in the open folder
    st_write(M8_BASE "/xdg/Trash/info/photo1.jpg.trashinfo", 1);
    for (int k = 0; k < 400 && (A.trash_job || A.trash_empty); k++) { st_frames(1); SDL_Delay(5); } // seen by the trash's watch
    ST_CHECK(A.trash_known && !A.trash_empty); // (another program filled it while we were elsewhere)
    menu_open_place(ti, A.r_sidebar.x + S(40), S(200));
    te = -1;
    for (int i = 0; i < A.menu.n; i++)
        if (A.menu.items[i].act == ACT_TRASH_EMPTY) te = i;
    ST_CHECK(te >= 0 && A.menu.items[te].enabled && !pane_is_trash(p));
    if (te >= 0) menu_activate(te);
    for (int k = 0; k < 300 && !A.dlg.open; k++) { st_frames(1); SDL_Delay(5); }
    ST_CHECK(A.dlg.open && !strcmp(A.dlg.title, "Empty Trash") && strstr(A.dlg.msg, "all 2 items"));
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait_ops();
    ST_CHECK(!st_exists(M8_BASE "/xdg/Trash/files/.hidden-trash") && !st_exists(M8_BASE "/xdg/Trash/info/.hidden-trash.trashinfo"));
    ST_CHECK(!st_exists(M8_BASE "/xdg/Trash/files/photo1.jpg") && st_exists(M8_BASE "/photo1.jpg"));
    for (int k = 0; k < 300 && A.trash_job; k++) { st_frames(1); SDL_Delay(5); }
    ST_CHECK(A.trash_known && A.trash_empty);
    A.toast[0] = 0;
    do_action(p, ACT_TRASH_EMPTY); // the palette's Empty Trash on an empty trash
    for (int k = 0; k < 300 && !A.toast[0]; k++) { st_frames(1); SDL_Delay(5); }
    ST_CHECK(!A.dlg.open && !strcmp(A.toast, "The trash is already empty"));

    // other drives' trashes (a made-up mount table): listed in the Trash beside the home one's items (the same name
    // twice is fine), Path= relative to the drive, restored onto the drive, counted in the size, emptied with the rest
    {
        char dt[256], df[300], di[300], mi[300], ip[400];
        snprintf(dt, sizeof dt, M8_BASE "/drive/.Trash-%u", (unsigned)getuid());
        snprintf(df, sizeof df, "%s/files", dt);
        snprintf(di, sizeof di, "%s/info", dt);
        mkdir(M8_BASE "/drive", 0755);
        mkdir(M8_BASE "/drive/docs", 0755);
        mkdir(dt, 0700);
        mkdir(df, 0700);
        mkdir(di, 0700);
        mkdir(M8_BASE "/drive/.Trash", 0777); // a shared .Trash without the sticky bit: not a trash
        char fp[400];
        snprintf(fp, sizeof fp, "%s/abc.txt", df);
        st_write(fp, 7);
        snprintf(ip, sizeof ip, "%s/abc.txt.trashinfo", di);
        FILE *f = fopen(ip, "w");
        if (f) { fputs("[Trash Info]\nPath=docs/abc%20x.txt\nDeletionDate=2026-11-20T10:00:00\n", f); fclose(f); }
        st_write(M8_BASE "/xdg/Trash/files/abc.txt", 3);
        f = fopen(M8_BASE "/xdg/Trash/info/abc.txt.trashinfo", "w");
        if (f) { fputs("[Trash Info]\nPath=" M8_BASE "/abc-home.txt\nDeletionDate=2026-11-20T11:00:00\n", f); fclose(f); }
        snprintf(mi, sizeof mi, M8_BASE "/mountinfo");
        f = fopen(mi, "w");
        if (f) {
            fputs("22 1 0:21 / /proc rw - proc proc rw\n", f);
            fputs("40 1 8:1 / " M8_BASE "/drive rw,relatime - ext4 /dev/sdz1 rw\n", f);
            fputs("41 1 8:1 / " M8_BASE "/drive rw,relatime - ext4 /dev/sdz1 rw\n", f); // (mounted twice: one trash)
            fputs("42 1 0:50 / " M8_BASE " rw - fuse.sshfs me@host:/ rw\n", f);      // (network: never looked at)
            fclose(f);
        }
        g_trash_mountinfo = M8_BASE "/mountinfo";
        trash_roots_refresh();
        for (int k = 0; k < 300 && !g_ntroots; k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(g_ntroots == 1 && !strcmp(g_troots[0].dir, dt) && !strcmp(g_troots[0].top, M8_BASE "/drive"));
        pane_navigate(p, A.places[ti].path, true); st_wait();
        ST_CHECK(pane_is_trash(p) && p->count == 2 && p->L->nextra == 1);
        char orig[PATH_MAX], date[32];
        ST_CHECK(trash_info(fp, orig, sizeof orig, date, sizeof date) && !strcmp(orig, M8_BASE "/drive/docs/abc x.txt") &&
                 !strcmp(date, "2026-11-20 10:00"));
        ST_CHECK(trash_info("abc.txt", orig, sizeof orig, date, sizeof date) && !strcmp(orig, M8_BASE "/abc-home.txt"));
        char a[PATH_MAX + 16], b[PATH_MAX + 16], nested[500];
        snprintf(nested, sizeof nested, "%s/abc.txt/inner", df);
        ST_CHECK(!trash_item_paths(nested, a, b, sizeof a, NULL, 0) && !trash_item_paths("abc.txt/inner", a, b, sizeof a, NULL, 0));
        trash_size_refresh();
        for (int k = 0; k < 300 && A.trash_job; k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(A.trash_known && A.trash_size == 10 && !A.trash_empty);
        { // a folder inside the drive's trash: "Trash" crumb, which leads back to the one Trash
            char sub[400];
            snprintf(sub, sizeof sub, "%s/folder", df);
            mkdir(sub, 0755);
            pane_navigate(p, sub, true); st_wait();
            layout_crumbs(p);
            ST_CHECK(p->ly.crumbs_trash && p->ly.ncrumbs == 2 && p->ly.crumbs[0].end == (int)strlen(df));
            pane_navigate(p, df, true); st_wait();
            ST_CHECK(pane_is_trash(p));
            rmdir(sub);
            for (int k = 0; k < 400 && p->count != 2; k++) { st_frames(1); SDL_Delay(5); }
        }
        ST_CHECK(p->count == 2 && st_select(p, fp));
        do_action(p, ACT_TRASH_RESTORE);
        st_wait_ops();
        ST_CHECK(st_size(M8_BASE "/drive/docs/abc x.txt") == 7 && !st_exists(fp) && !st_exists(ip));
        for (int k = 0; k < 400 && p->count != 1; k++) { st_frames(1); SDL_Delay(5); } // (seen by the drive trash's watch)
        ST_CHECK(p->count == 1 && !strcmp(p->L->entries[p->order[0]].name, "abc.txt"));
        snprintf(fp, sizeof fp, "%s/z.bin", df);
        st_write(fp, 4);
        snprintf(ip, sizeof ip, "%s/z.bin.trashinfo", di);
        st_write(ip, 1);
        for (int k = 0; k < 400 && p->count != 2; k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(p->count == 2);
        do_action(p, ACT_TRASH_EMPTY);
        for (int k = 0; k < 300 && !A.dlg.open; k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(A.dlg.open && strstr(A.dlg.msg, "all 2 items"));
        st_key(SDLK_RETURN, SDL_KMOD_NONE);
        st_wait_ops();
        ST_CHECK(!st_exists(fp) && !st_exists(ip) && !st_exists(M8_BASE "/xdg/Trash/files/abc.txt"));
        for (int k = 0; k < 300 && A.trash_job; k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(A.trash_known && A.trash_empty);
        g_trash_mountinfo = "";
        trash_roots_refresh();
        for (int k = 0; k < 300 && g_ntroots; k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(!g_ntroots);
        pane_navigate(p, M8_BASE, true); st_wait();
    }

    // archives: compress a folder, browse into the zip, extract, read-only, zip-slip
    pane_navigate(p, M8_BASE, true); st_wait();
    mkdir(M8_BASE "/proj", 0755);
    mkdir(M8_BASE "/proj/src", 0755);
    st_write(M8_BASE "/proj/src/main.c", 4321);
    st_write(M8_BASE "/proj/readme.md", 77);
    pane_refresh(p); st_wait();
    { // the menu: one "Compress to" item, its submenu lists the formats then Custom...
        st_select(p, "proj");
        menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), true);
        int ci = -1, nold = 0;
        for (int i = 0; i < A.menu.n; i++) {
            if (A.menu.items[i].act == ACT_COMPRESS_MENU) ci = i;
            nold += A.menu.items[i].label && !strncmp(A.menu.items[i].label, "Compress to ", 12); // (the old one-format items)
        }
        ST_CHECK(ci >= 0 && !nold && A.menu.items[ci].sub);
        if (ci >= 0 && A.menu.items[ci].sub) {
            menu_sub_open(ci, true);
            Menu *sm = &g_menu_sub;
            ST_CHECK(menu_sub_shown() && sm->n == ARC_NFMT - 1 + 2 && sm->items[ARC_NFMT - 1].sep);
            for (int f = ARC_ZIP; f < ARC_NFMT; f++)
                ST_CHECK(sm->items[f - ARC_ZIP].act == ACT_COMPRESS_BASE + f && !strcmp(sm->items[f - ARC_ZIP].label, g_arc_fmts[f].label));
            ST_CHECK(sm->items[sm->n - 1].act == ACT_COMPRESS_CUSTOM);
            st_shot("compress-menu");
        }
        A.menu.open = false;
        menu_sub_close();
    }
    for (int f = ARC_ZIP; f < ARC_NFMT; f++) { // Compress to > each format
        st_select(p, "proj");
        do_action(p, ACT_COMPRESS_BASE + f);
        st_wait_ops();
        char out[PATH_MAX];
        snprintf(out, sizeof out, M8_BASE "/proj%s", g_arc_fmts[f].ext);
        ST_CHECK(st_size(out) > 0);
    }
    { // Compress to > Custom...: the sheet picks name, format and level (all by keyboard), and remembers them
        A.compress_fmt = ARC_ZIP, A.compress_level = ARC_LVL_NORMAL;
        st_select(p, "proj");
        do_action(p, ACT_COMPRESS_CUSTOM);
        ST_CHECK(g_sheet.kind == SHEET_COMPRESS && !strcmp(g_sheet.cn[CN_NAME].buf, "proj") && g_sheet.cz_fmt == ARC_ZIP);
        sheet_key(SDLK_TAB, SDL_KMOD_NONE); // -> format
        for (int k = 0; k < 9; k++) sheet_key(SDLK_RIGHT, SDL_KMOD_NONE); // (stops at the last)
        sheet_key(SDLK_LEFT, SDL_KMOD_NONE);
        ST_CHECK(g_sheet.cz_fmt == ARC_TZST);
        sheet_key(SDLK_DOWN, SDL_KMOD_NONE); // -> level
        sheet_key(SDLK_RIGHT, SDL_KMOD_NONE);
        ST_CHECK(g_sheet.cz_lvl == ARC_LVL_BEST && compress_number() == 19 && g_sheet.cz_kb);
        sheet_key(SDLK_PLUS, SDL_KMOD_NONE); // a number of one's own...
        ST_CHECK(g_sheet.cz_lvl == ARC_LVL_CUSTOM && compress_number() == 20);
        sheet_key(SDLK_MINUS, SDL_KMOD_NONE); // ...back on Best's
        ST_CHECK(g_sheet.cz_lvl == ARC_LVL_BEST);
        for (int k = 0; k < 5; k++) sheet_key(SDLK_PLUS, SDL_KMOD_NONE);
        ST_CHECK(g_sheet.cz_lvl == ARC_LVL_CUSTOM && compress_number() == 22); // (zstd's highest)
        sheet_key(SDLK_MINUS, SDL_KMOD_NONE);
        st_frames(1); // (where the buttons are)
        compress_click(g_sheet.cz_rminus.x + 2, g_sheet.cz_rminus.y + 2); // the stepper's button; a click: no outline
        ST_CHECK(compress_number() == 20 && !g_sheet.cz_kb && g_sheet.cz_focus == 2);
        st_shot("compress-sheet");
        sheet_key(SDLK_TAB, SDL_KMOD_NONE); // -> name (wraps)
        ST_CHECK(g_sheet.cz_focus == 0);
        te_set(&g_sheet.cn[CN_NAME], "", 0, 0);
        sheet_text("packed/");
        sheet_key(SDLK_RETURN, SDL_KMOD_NONE); // a '/': not a name, the sheet stays
        ST_CHECK(g_sheet.kind == SHEET_COMPRESS);
        sheet_key(SDLK_BACKSPACE, SDL_KMOD_NONE);
        sheet_text(".tar.zst"); // typed with its extension: not doubled
        sheet_key(SDLK_RETURN, SDL_KMOD_NONE);
        st_wait_ops();
        ST_CHECK(g_sheet.kind == SHEET_NONE && st_size(M8_BASE "/packed.tar.zst") > 0 && !st_exists(M8_BASE "/packed.tar.zst.tar.zst"));
        ST_CHECK(A.compress_fmt == ARC_TZST && A.compress_level == ARC_LVL_CUSTOM && A.compress_num == 20);
        st_select(p, "proj");
        do_action(p, ACT_COMPRESS_CUSTOM);
        ST_CHECK(g_sheet.cz_fmt == ARC_TZST && compress_number() == 20); // offered again
        st_frames(1); // (where the buttons are)
        compress_click(g_sheet.cz_rfmt[ARC_ZIP].x + 2, g_sheet.cz_rfmt[ARC_ZIP].y + 2);
        ST_CHECK(g_sheet.cz_fmt == ARC_ZIP && compress_number() == 9); // (within ZIP's levels)
        compress_click(g_sheet.cz_rlvl[0].x + 2, g_sheet.cz_rlvl[0].y + 2);
        ST_CHECK(g_sheet.cz_lvl == ARC_LVL_FAST && compress_number() == 1);
        sheet_key(SDLK_ESCAPE, SDL_KMOD_NONE);
        ST_CHECK(g_sheet.kind == SHEET_NONE);
    }
    { // the levels do something: Best is smaller than Fast on text (zstd 19 vs 3)
        char text[1 << 16];
        u32 seed = 7;
        size_t o = 0;
        static const char *words[] = {"alpha ", "beta ", "gamma ", "delta ", "epsilon ", "zeta ", "eta ", "theta\n"};
        while (o + 16 < sizeof text) { seed = seed * 1103515245u + 12345u; o += (size_t)snprintf(text + o, 16, "%s", words[(seed >> 16) & 7]); }
        int fd = open(M8_BASE "/words.txt", O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);
        for (int k = 0; k < 16 && fd >= 0; k++) if (write(fd, text, o) != (ssize_t)o) break;
        if (fd >= 0) close(fd);
        char *wp[] = {M8_BASE "/words.txt"};
        i64 sz[2];
        for (int b = 0; b < 2; b++) {
            const char *arc = b ? M8_BASE "/words-best.tar.zst" : M8_BASE "/words-fast.tar.zst";
            Op *op = op_new(OP_COMPRESS, wp, 1, M8_BASE);
            op->arc = strdup(arc);
            op->arc_fmt = ARC_TZST, op->arc_level = b ? 19 : 3;
            run_compress(op);
            ST_CHECK(!op->nerr);
            op_free(op);
            sz[b] = st_size(arc);
        }
        ST_CHECK(sz[0] > 0 && sz[1] > 0 && sz[1] < sz[0]);
    }
    { // accented names and spaces keep their bytes; an unreadable file is left out and named, the rest is there;
      // a "<name>.part" that was already there is left alone
        mkdir(M8_BASE "/uni", 0755);
        mkdir(M8_BASE "/uni/sub dir", 0755);
        st_write(M8_BASE "/uni/caf\xC3\xA9 cr\xC3\xA8me.txt", 10);
        st_write(M8_BASE "/uni/sub dir/\xE6\x97\xA5\xE6\x9C\xAC.txt", 20);
        st_write(M8_BASE "/uni/locked.txt", 30);
        chmod(M8_BASE "/uni/locked.txt", 0);
        bool locked = geteuid() != 0; // (root reads it anyway)
        char *uni[] = {M8_BASE "/uni"};
        for (int fmt = ARC_ZIP; fmt < ARC_NFMT; fmt++) { // every format, picked by the op (not the name)
            char arc[PATH_MAX];
            snprintf(arc, sizeof arc, M8_BASE "/uni%s", g_arc_fmts[fmt].ext);
            char part[PATH_MAX];
            snprintf(part, sizeof part, "%s.part", arc);
            st_write_text(part, "mine");
            Op *op = op_new(OP_COMPRESS, uni, 1, M8_BASE);
            op->arc = strdup(arc);
            op->arc_fmt = fmt;
            run_compress(op);
            ST_CHECK(!op->nerr && st_size(arc) > 0);
            if (op->nerr) fprintf(stderr, "SELFTEST compress: %s\n", op->err);
            if (locked) ST_CHECK(strstr(op->warn, "1 item couldn't be read") && strstr(op->warn, "uni/locked.txt"));
            else ST_CHECK(!op->warn[0]);
            char mine[16] = "";
            st_read_text(part, mine, sizeof mine);
            ST_CHECK(!strcmp(mine, "mine")); // untouched
            char other[PATH_MAX + 8];
            snprintf(other, sizeof other, "%s.1.part", arc);
            ST_CHECK(!st_exists(other)); // ours was renamed into place
            int seen = 0;
            locale_t oloc = arc_utf8_begin(); // (as the app reads them)
            struct archive *a = arc_open(arc);
            struct archive_entry *ae;
            while (a && arc_next(a, &ae) == ARCHIVE_OK) {
                if (!seen) { // written as asked: the format, and the tar's filter
                    static const int want[ARC_NFMT] = {[ARC_ZIP] = ARCHIVE_FILTER_NONE, [ARC_TGZ] = ARCHIVE_FILTER_GZIP,
                                                       [ARC_TXZ] = ARCHIVE_FILTER_XZ, [ARC_TZST] = ARCHIVE_FILTER_ZSTD, [ARC_7Z] = ARCHIVE_FILTER_NONE};
                    int af = archive_format(a) & ARCHIVE_FORMAT_BASE_MASK;
                    ST_CHECK(fmt == ARC_ZIP ? af == ARCHIVE_FORMAT_ZIP : fmt == ARC_7Z ? af == ARCHIVE_FORMAT_7ZIP : af == ARCHIVE_FORMAT_TAR);
                    ST_CHECK(archive_filter_code(a, 0) == want[fmt]);
                }
                const char *nm = archive_entry_pathname_utf8(ae);
                if (!nm) nm = archive_entry_pathname(ae);
                if (!nm) continue;
                seen |= !strcmp(nm, "uni/caf\xC3\xA9 cr\xC3\xA8me.txt") ? 1 : 0;
                seen |= !strcmp(nm, "uni/sub dir/\xE6\x97\xA5\xE6\x9C\xAC.txt") ? 2 : 0;
                seen |= !strcmp(nm, "uni/locked.txt") ? 4 : 0;
            }
            if (a) archive_read_free(a);
            arc_utf8_end(oloc);
            ST_CHECK((seen & 3) == 3 && (locked ? !(seen & 4) : (seen & 4)));
            unlink(part);
            op_free(op);
        }
        chmod(M8_BASE "/uni/locked.txt", 0644);
        // everything vanished since it was picked: no empty archive, and the reason
        char *gone[] = {M8_BASE "/missing-source"};
        Op *op = op_new(OP_COMPRESS, gone, 1, M8_BASE);
        op->arc = strdup(M8_BASE "/missing.zip");
        run_compress(op);
        ST_CHECK(op->nerr && strstr(op->err, "missing-source") && !st_exists(M8_BASE "/missing.zip") && !st_exists(M8_BASE "/missing.zip.part"));
        op_free(op);
        // a ".part" that is a symlink isn't written through (here to a full disk): ours gets a name of its own
        Op *fo = op_new(OP_COMPRESS, uni, 1, M8_BASE);
        fo->arc = strdup(M8_BASE "/full.zip");
        if (symlink("/dev/full", M8_BASE "/full.zip.part")) {}
        run_compress(fo);
        ST_CHECK(!fo->nerr && st_size(M8_BASE "/full.zip") > 0 && !st_exists(M8_BASE "/full.zip.1.part"));
        op_free(fo);
        unlink(M8_BASE "/full.zip.part");
        unlink(M8_BASE "/full.zip");
        // browsing it lists every name (the C locale used to stop at the first accented one)
        pane_navigate(p, M8_BASE "/uni.zip" ARC_SEP "uni", true);
        st_wait();
        ST_CHECK(p->count == (locked ? 2u : 3u) && st_row_y(p, "caf\xC3\xA9 cr\xC3\xA8me.txt") > 0 && st_row_y(p, "sub dir") > 0);
        pane_navigate(p, M8_BASE, true);
        st_wait();
        unlink(M8_BASE "/uni.zip");
        unlink(M8_BASE "/uni.tar.gz");
        st_rmrf(M8_BASE "/uni");
    }
    st_select(p, "proj.zip");
    st_key(SDLK_RETURN, SDL_KMOD_NONE); // Enter browses the archive
    st_wait();
    ST_CHECK(pane_is_archive(p) && p->L->is_archive && p->count == 1 && !strcmp(p->L->entries[p->order[0]].name, "proj"));
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(p->count == 2); // src/, readme.md
    st_select(p, "src");
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(p->count == 1 && p->L->entries[p->order[0]].size == 4321);
    st_shot("archive");
    // read-only
    st_key(SDLK_DELETE, SDL_KMOD_NONE);
    st_wait_ops();
    ST_CHECK(p->count == 1 && strstr(A.toast, "read-only"));
    // drag main.c out of the archive into another panel -> extracted there
    mkdir(M8_BASE "/out", 0755);
    Panel *right = panel_split(A.ap, ZONE_RIGHT, NULL, M8_BASE "/out");
    st_wait();
    tab_select(tab_index(panel_tab(A.panels[0], 0)));
    st_wait();
    p = active_pane();
    f32 ry = st_row_y(p, "main.c");
    Pane *rp = &right->cur->pane[0];
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, p->ly.col_name + S(40), ry, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_MOTION, rp->ly.list.x + S(80), rp->ly.list.y + S(40), 0, 0, 0);
    st_mouse(SDL_EVENT_MOUSE_MOTION, rp->ly.list.x + S(100), rp->ly.list.y + S(60), 0, 0, 0);
    ST_CHECK(A.drag == DRAG_ITEMS && !strcmp(A.drop_dir, M8_BASE "/out"));
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
    st_wait_ops();
    ST_CHECK(st_size(M8_BASE "/out/main.c") == 4321);
    panel_activate(right);
    run_command(CMD_CLOSE_PANEL);
    st_wait();
    ST_CHECK(A.npanels == 1);
    // up out of the archive: back in the folder with the archive selected
    p = active_pane();
    if (pane_is_archive(p)) {
        pane_up(p); st_wait();
        pane_up(p); st_wait();
        pane_up(p); st_wait();
    }
    ST_CHECK(!strcmp(p->path, M8_BASE) && !strcmp(st_cursor_name(p), "proj.zip"));
    // opening a file inside an archive extracts it to the cache, then opens it
    st_select(p, "proj.zip");
    st_key(SDLK_RETURN, SDL_KMOD_NONE); st_wait();
    st_key(SDLK_RETURN, SDL_KMOD_NONE); st_wait();
    st_select(p, "readme.md");
    A.last_opened[0] = 0;
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait_ops();
    ST_CHECK(strstr(A.last_opened, "/extract/") && st_size(A.last_opened) == 77);
    pane_up(p); st_wait();
    pane_up(p); st_wait();
    // tar.gz browses too
    st_select(p, "proj.tar.gz");
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(p->L->is_archive && p->count == 1);
    pane_up(p); st_wait();
    // extract here -> "proj (2)" (since "proj" exists)
    st_select(p, "proj.zip");
    do_action(p, ACT_EXTRACT_HERE);
    st_wait_ops();
    ST_CHECK(st_size(M8_BASE "/proj (2)/proj/src/main.c") == 4321);
    st_key(SDLK_Z, SDL_KMOD_CTRL); // undo extraction -> trashed
    st_wait_ops();
    ST_CHECK(!st_exists(M8_BASE "/proj (2)"));
    // several archives selected: each one into its own folder, even when their names give the same one
    st_select(p, "proj.zip");
    for (u32 i = 0; i < p->count; i++)
        if (!strcmp(p->L->entries[p->order[i]].name, "proj.tar.gz")) sel_set(p, i, true);
    ST_CHECK(p->sel_count == 2 && extract_here_count(p, false) == 2);
    do_action(p, ACT_EXTRACT_HERE);
    st_wait_ops();
    ST_CHECK(st_size(M8_BASE "/proj (2)/proj/src/main.c") == 4321 && st_exists(M8_BASE "/proj (3)"));
    st_key(SDLK_Z, SDL_KMOD_CTRL);
    st_wait_ops();
    st_key(SDLK_Z, SDL_KMOD_CTRL);
    st_wait_ops();
    ST_CHECK(!st_exists(M8_BASE "/proj (2)") && !st_exists(M8_BASE "/proj (3)"));
    // zip-slip: a member "../escaped.txt" must not be written outside the target
    {
        struct archive *w = archive_write_new();
        archive_write_set_format_zip(w);
        archive_write_open_filename(w, M8_BASE "/evil.zip");
        struct archive_entry *e = archive_entry_new();
        archive_entry_set_pathname(e, "../escaped.txt");
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_entry_set_size(e, 4);
        archive_write_header(w, e);
        archive_write_data(w, "evil", 4);
        archive_entry_free(e);
        archive_write_close(w);
        archive_write_free(w);
        mkdir(M8_BASE "/safe", 0755);
        Op *op = op_new(OP_EXTRACT, NULL, 0, M8_BASE "/safe/inner");
        op->arc = strdup(M8_BASE "/evil.zip");
        ops_start(op);
        st_wait_ops();
        ST_CHECK(!st_exists(M8_BASE "/safe/escaped.txt") && !st_exists(M8_BASE "/escaped.txt"));
    }

    // executable files: opening asks (Run / Run in terminal / [Open] / Cancel), "Don't ask again" keeps the answer,
    // one without the executable bit runs only after "Make executable and run"; right-click > Run
    {
        mkdir(M8_BASE "/run", 0755);
        static const struct { const char *name, *data; mode_t mode; } rf[] = {
            {"prog.AppImage", "\x7f" "ELF\x02\x01", 0755}, {"dl.AppImage", "\x7f" "ELF\x02\x01", 0644},
            {"tool.sh", "#!/bin/sh\necho hi\n", 0755}, {"notes.sh", "#!/bin/sh\necho hi\n", 0644}, {"plain.txt", "just text\n", 0755}, {"bare.sh", "echo hi\n", 0755}};
        for (size_t i = 0; i < ARRAY_COUNT(rf); i++) {
            char fp[PATH_MAX];
            path_join(fp, sizeof fp, M8_BASE "/run", rf[i].name);
            FILE *rfh = fopen(fp, "w");
            fputs(rf[i].data, rfh);
            fclose(rfh);
            chmod(fp, rf[i].mode);
        }
        pane_navigate(p, M8_BASE "/run", true); st_wait();
        int saved_exec = A.exec_open;
        A.exec_open = EXEC_ASK;
        // a program: asked, then run in its folder
        st_select(p, "prog.AppImage");
        A.last_opened[0] = 0;
        do_action(p, ACT_OPEN);
        ST_CHECK(A.dlg.open && A.dlg.nbuttons == 3 && A.dlg.has_check && strstr(A.dlg.msg, "AppImage") && !A.last_opened[0]);
        st_frames(2);
        st_shot("run-ask");
        dialog_choose(0);
        ST_CHECK(!strcmp(A.last_opened, "run:" M8_BASE "/run/prog.AppImage") && A.exec_open == EXEC_ASK);
        // Cancel: nothing
        A.last_opened[0] = 0;
        do_action(p, ACT_OPEN);
        dialog_choose(A.dlg.cancel);
        ST_CHECK(!A.dlg.open && !A.last_opened[0]);
        // a script: Open shows it as a document
        st_select(p, "tool.sh");
        do_action(p, ACT_OPEN);
        ST_CHECK(A.dlg.open && A.dlg.nbuttons == 4);
        dialog_choose(2);
        ST_CHECK(!strcmp(A.last_opened, M8_BASE "/run/tool.sh"));
        // "Don't ask again" + Run in terminal: kept, and the next program runs that way without a question
        do_action(p, ACT_OPEN);
        A.dlg.check = true;
        dialog_choose(1);
        ST_CHECK(!strcmp(A.last_opened, "run-terminal:" M8_BASE "/run/tool.sh") && A.exec_open == EXEC_TERMINAL);
        st_select(p, "prog.AppImage");
        do_action(p, ACT_OPEN);
        ST_CHECK(!A.dlg.open && !strcmp(A.last_opened, "run-terminal:" M8_BASE "/run/prog.AppImage"));
        // "Open as a document": scripts open, programs still ask (nothing else opens them)
        A.exec_open = EXEC_OPEN;
        do_action(p, ACT_OPEN);
        ST_CHECK(A.dlg.open);
        dialog_choose(A.dlg.cancel);
        st_select(p, "tool.sh");
        do_action(p, ACT_OPEN);
        ST_CHECK(!A.dlg.open && !strcmp(A.last_opened, M8_BASE "/run/tool.sh"));
        A.exec_open = EXEC_ASK;
        // a .sh without #!: a script all the same, run by the shell
        st_select(p, "bare.sh");
        do_action(p, ACT_OPEN);
        ST_CHECK(A.dlg.open && A.dlg.nbuttons == 4);
        dialog_choose(0);
        ST_CHECK(!strcmp(A.last_opened, "run:/bin/sh " M8_BASE "/run/bare.sh"));
        // a script without the executable bit, an executable text file without #!: documents, no question
        st_select(p, "notes.sh");
        do_action(p, ACT_OPEN);
        ST_CHECK(!A.dlg.open && !strcmp(A.last_opened, M8_BASE "/run/notes.sh"));
        st_select(p, "plain.txt");
        do_action(p, ACT_OPEN);
        ST_CHECK(!A.dlg.open && !strcmp(A.last_opened, M8_BASE "/run/plain.txt"));
        // a downloaded AppImage (no executable bit): Cancel leaves it, "Make executable and run" runs it
        st_select(p, "dl.AppImage");
        A.last_opened[0] = 0;
        do_action(p, ACT_OPEN);
        ST_CHECK(A.dlg.open && A.dlg.icon == DI_WARN && A.dlg.nbuttons == 2 && !A.dlg.has_check);
        st_frames(2);
        st_shot("run-chmod");
        dialog_choose(A.dlg.cancel);
        struct stat rst;
        ST_CHECK(!stat(M8_BASE "/run/dl.AppImage", &rst) && (rst.st_mode & 0777) == 0644 && !A.last_opened[0]);
        do_action(p, ACT_OPEN);
        dialog_choose(0);
        ST_CHECK(!stat(M8_BASE "/run/dl.AppImage", &rst) && (rst.st_mode & 0777) == 0755);
        ST_CHECK(!strcmp(A.last_opened, "run:" M8_BASE "/run/dl.AppImage"));
        // right-click: Run / Run in terminal for programs and scripts only; no question for an executable one
        st_select(p, "tool.sh");
        menu_open(p, 50, 50, true);
        bool has_run = false;
        for (int k = 0; k < A.menu.n; k++) has_run |= A.menu.items[k].act == ACT_RUN_TERM;
        ST_CHECK(has_run);
        A.menu.open = false;
        st_select(p, "plain.txt");
        menu_open(p, 50, 50, true);
        has_run = false;
        for (int k = 0; k < A.menu.n; k++) has_run |= A.menu.items[k].act == ACT_RUN;
        ST_CHECK(!has_run);
        A.menu.open = false;
        st_select(p, "tool.sh");
        do_action(p, ACT_RUN);
        ST_CHECK(!A.dlg.open && !strcmp(A.last_opened, "run:" M8_BASE "/run/tool.sh"));
        st_select(p, "notes.sh");
        do_action(p, ACT_RUN_TERM);
        ST_CHECK(A.dlg.open && A.dlg.icon == DI_WARN);
        dialog_choose(0);
        ST_CHECK(!strcmp(A.last_opened, "run-terminal:" M8_BASE "/run/notes.sh"));
        A.exec_open = saved_exec;
        pane_navigate(p, M8_BASE, true); st_wait();
    }

    { // custom actions (*.action files): which menus list them, and what runs - file names only ever as arguments
        mkdir(M8_BASE "/xdg/file-explorer", 0755);
        mkdir(M8_BASE "/xdg/file-explorer/actions", 0755);
        mkdir(M8_BASE "/ua", 0755);
        mkdir(M8_BASE "/ua/sub", 0755);
        st_write(M8_BASE "/ua/x$(touch PWNED).png", 1);
        st_write(M8_BASE "/ua/it's.png", 1);
        st_write(M8_BASE "/ua/notes.txt", 1);
#define UA_FILE(name, text) st_write_text(M8_BASE "/xdg/file-explorer/actions/" name, text)
        UA_FILE("a-copy.action", "[Nemo Action]\nName=Copy %f\nExec=sh -c \"cat %F | wl-copy\"\nSelection=any\nExtensions=png;jpg;\n");
        UA_FILE("b-arq.nemo_action", "[Nemo Action]\nName=Compress\nExec=arqiver --aa %P/%e.tar.zst %F\nSelection=m\nExtensions=any;\n");
        UA_FILE("c-here.action", "[Action]\nName=Here\nExec=<run.sh> %P\nSelection=none\nExtensions=any;\n");
        UA_FILE("d-off.action", "[Action]\nActive=false\nName=Off\nExec=true\n");
        UA_FILE("e-dep.action", "[Action]\nName=Missing\nExec=true\nDependencies=fe-no-such-program;\n");
        UA_FILE("f-dir.action", "[Action]\nName=Dir only\nExec=echo %F\nSelection=s\nExtensions=dir;\n");
        UA_FILE("g-mime.action", "[Action]\nName=Text\nExec=xed %F\nSelection=single\nMimetypes=text/*;\nTerminal=true\n");
        UA_FILE("h-quotes.action", "[Action]\nName=Echo\nExec=sh -c \"echo \\\"%f\\\" '%f'\"\nSelection=s\nExtensions=png;\n");
        UA_FILE("j-subst.action", "[Action]\nName=Words\nExec=sh -c \"echo \\\"$(wc -w < %F) in %f\\\"\"\nSelection=s\nExtensions=png;\n");
        UA_FILE("i-pct.action", "[Action]\nName=Pct %%\nExec=printf \"%%s 100%%\" %f\nSelection=s\nExtensions=txt;\n");
        UA_FILE("notes.txt", "[Action]\nName=Not an action file\nExec=true\n");
        UA_FILE("k-tool1.action", "[Action]\nName=Tool one\nExec=t1 %F\nSelection=s\nExtensions=txt;\nGroup=Tools\nIcon=terminal\n");
        UA_FILE("k-tool2.action", "[Action]\nName=Tool two\nExec=t2 %F\nSelection=s\nExtensions=txt;\nGroup=Tools\n");
        UA_FILE("groups.conf", "[Tools]\nIcon=wand\n");
#undef UA_FILE
        pane_navigate(p, M8_BASE "/ua", true);
        st_wait();
        sel_clear(p);
        for (u32 i = 0; i < p->count; i++)
            if (strstr(p->L->entries[p->order[i]].name, ".png")) { sel_set(p, i, true); p->cursor = i; } // (right-clicked one)
        ST_CHECK(p->sel_count == 2);
        menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), true);
        int copy = st_menu_find("Copy it's.png"); // (Name's codes filled in: the first selected)
        ST_CHECK(copy >= 0 && st_menu_find("Compress") > copy && st_menu_find("Properties") > st_menu_find("Compress"));
        ST_CHECK(st_menu_find("Here") < 0 && st_menu_find("Off") < 0 && st_menu_find("Missing") < 0 && st_menu_find("Dir only") < 0);
        ST_CHECK(st_menu_find("Text") < 0 && st_menu_find("Echo") < 0 && st_menu_find("Not an action file") < 0);
        st_frames(2);
        st_shot("custom-actions");
        A.last_opened[0] = 0;
        menu_activate(copy); // a script for sh: each name a single-quoted word, so $(...) stays a name
        ST_CHECK(!strcmp(A.last_opened, "action:[sh][-c][cat '" M8_BASE "/ua/it'\\''s.png' '" M8_BASE "/ua/x$(touch PWNED).png' | wl-copy]"));
        menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), true);
        menu_activate(st_menu_find("Compress")); // no shell: the names go in as they are, %F one argument each
        ST_CHECK(!strcmp(A.last_opened, "action:[arqiver][--aa][" M8_BASE "/ua/it's.tar.zst][" M8_BASE "/ua/it's.png][" M8_BASE "/ua/x$(touch PWNED).png]"));

        sel_clear(p);
        st_select(p, "it's.png");
        menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), true);
        ST_CHECK(st_menu_find("Copy it's.png") >= 0 && st_menu_find("Compress") < 0);
        menu_activate(st_menu_find("Echo")); // inside the script's "..." and '...': quoted for each
        ST_CHECK(!strcmp(A.last_opened, "action:[sh][-c][echo \"\"'it'\\''s.png'\"\" 'it'\\''s.png']"));
        menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), true);
        menu_activate(st_menu_find("Words")); // inside "$( )" quotes start afresh: a plain quoted word there
        ST_CHECK(!strcmp(A.last_opened, "action:[sh][-c][echo \"$(wc -w < '" M8_BASE "/ua/it'\\''s.png') in \"'it'\\''s.png'\"\"]"));

        st_select(p, "sub");
        menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), true);
        ST_CHECK(st_menu_find("Copy sub") < 0 && st_menu_find("Text") < 0);
        menu_activate(st_menu_find("Dir only"));
        ST_CHECK(!strcmp(A.last_opened, "action:[echo][" M8_BASE "/ua/sub]"));

        st_select(p, "notes.txt");
        menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), true);
        ST_CHECK(st_menu_find("Dir only") < 0 && st_menu_find("Pct %") >= 0);
        menu_activate(st_menu_find("Text")); // a type pattern; in the terminal
        ST_CHECK(!strcmp(A.last_opened, "action-terminal:[xed][" M8_BASE "/ua/notes.txt]"));
        menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), true);
        menu_activate(st_menu_find("Pct %"));
        ST_CHECK(!strcmp(A.last_opened, "action:[printf][%s 100%][notes.txt]"));
        { // a group: one submenu item (with the group's icon) where its first action would be; actions' own icons; a separator
          // before Properties
            menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), true);
            int gi = st_menu_find("Tools"), pi = st_menu_find("Properties"), ti = st_menu_find("Text");
            ST_CHECK(gi >= 0 && A.menu.items[gi].sub && st_menu_find("Tool one") < 0 && st_menu_find("Tool two") < 0);
            ST_CHECK(menu_action_icon(&A.menu.items[gi]) == UI_WAND_SPARKLES && ti >= 0 && menu_action_icon(&A.menu.items[ti]) == UI_ZAP);
            ST_CHECK(pi > gi && A.menu.items[pi - 1].sep && A.menu.items[ti - 1].sep);
            menu_sub_open(gi, true);
            ST_CHECK(menu_sub_shown() && g_menu_sub.n == 2 && !strcmp(g_menu_sub.items[0].label, "Tool one") && !strcmp(g_menu_sub.items[1].label, "Tool two"));
            ST_CHECK(menu_action_icon(&g_menu_sub.items[0]) == UI_TERMINAL && menu_action_icon(&g_menu_sub.items[1]) == UI_ZAP);
            st_frames(2);
            st_shot("custom-actions-group");
            menu_sub_activate(1);
            ST_CHECK(!strcmp(A.last_opened, "action:[t2][" M8_BASE "/ua/notes.txt]"));
        }
        { // a failing action says why: the start of its stderr and its exit code (read from the pipe the sh around it writes)
            char msg[400];
            ST_CHECK(!ua_failure_text("X", "noise\n" UA_RC_MARK "0\n", 0, msg, sizeof msg));
            ST_CHECK(ua_failure_text("Word count", "sh: 1: notify-send: not found\n" UA_RC_MARK "127\n", 127, msg, sizeof msg));
            ST_CHECK(!strcmp(msg, "\xE2\x80\x9CWord count\xE2\x80\x9D failed (command not found): sh: 1: notify-send: not found"));
            ST_CHECK(ua_failure_text("X", UA_RC_MARK "2\n", 2, msg, sizeof msg) && !strcmp(msg, "\xE2\x80\x9CX\xE2\x80\x9D failed (exit 2)"));
            int pfd[2];
            ST_CHECK(pipe(pfd) == 0);
            static const char out[] = "convert: no such file\nmore\n" UA_RC_MARK "1\n";
            ST_CHECK(write(pfd[1], out, sizeof out - 1) == (ssize_t)(sizeof out - 1));
            close(pfd[1]);
            UaWatch *w = xcalloc(1, sizeof *w);
            w->fd = pfd[0];
            snprintf(w->name, sizeof w->name, "Convert");
            A.toast[0] = 0;
            ua_watch_thread(w); // (here, not on its thread: it posts the toast to the main loop)
            st_frames(2);
            ST_CHECK(!strcmp(A.toast, "\xE2\x80\x9C" "Convert\xE2\x80\x9D failed (exit 1): convert: no such file"));
        }

        menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), false); // the folder's own menu: Selection=none ones
        ST_CHECK(st_menu_find("Here") >= 0 && st_menu_find("Copy it's.png") < 0 && st_menu_find("Text") < 0);
        menu_activate(st_menu_find("Here")); // <prog>: in the actions folder
        ST_CHECK(!strcmp(A.last_opened, "action:[" M8_BASE "/xdg/file-explorer/actions/run.sh][" M8_BASE "/ua]"));
        ST_CHECK(!st_exists(M8_BASE "/ua/PWNED"));
        A.menu.open = false;
        pane_navigate(p, M8_BASE, true);
        st_wait();
    }

    st_rmrf(M8_BASE);
    if (st_fail) { fprintf(stderr, "SELFTEST-M8: %d failure(s)\n", st_fail); return 1; }
    printf("SELFTEST-M8 OK\n");
    fflush(stdout);
    return 0;
}

// --- self-test for M9: themes, Options, shortcuts, mouse options, layouts, Quick Access, tree view ---

#define M9_BASE "/tmp/fe-m9-test"

static void st_setup_m9(void) {
    st_rmrf(M9_BASE);
    mkdir(M9_BASE, 0755);
    mkdir(M9_BASE "/sub", 0755);
    mkdir(M9_BASE "/tree", 0755);
    mkdir(M9_BASE "/tree/deep", 0755);
    st_write(M9_BASE "/tree/inner.txt", 10);
    st_write(M9_BASE "/tree/deep/x.md", 10);
    for (int i = 0; i < 12; i++) {
        char p[PATH_MAX];
        snprintf(p, sizeof p, M9_BASE "/file%02d.%s", i, i % 3 == 0 ? "c" : i % 3 == 1 ? "png" : "md");
        st_write(p, (size_t)(i + 1) * 100);
    }
    st_rmrf("/tmp/fe-selftest-config/file-explorer/themes");
    unlink("/tmp/fe-selftest-config/file-explorer/layouts.conf");
}

static OptHit *st_opt_hit(int part, int opt, int arg) {
    for (int k = 0; k < g_opt.nhits; k++)
        if (g_opt.hits[k].part == part && g_opt.hits[k].opt == opt && g_opt.hits[k].arg == arg && g_opt.hits[k].r.w > 0)
            return &g_opt.hits[k];
    return NULL;
}

static void st_opt_click(OptHit *h) {
    ST_CHECK(h != NULL);
    if (!h) return;
    f32 x = h->r.x + h->r.w * 0.5f, y = h->r.y + h->r.h * 0.5f;
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, x, y, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
    st_frames(1);
}

static int run_selftest_m9(void) {
    st_wait();
    Pane *p = active_pane();

    { // caret animation: a short ease-in to the new position, then a slow breathing pulse at rest
        TextCaret s;
        memset(&s, 0, sizeof s);
        f32 a = 0;
        u64 t = 1000000000ull;
        ST_CHECK(text_caret_animate(&s, 20.f, 1, 1, t, false, &a) == 20.f && a == 1.f && s.valid); // first target is immediate
        ST_CHECK(text_caret_animate(&s, 40.f, 1, 1, t + 45000000ull, false, &a) == 20.f); // next one starts where it was
        f32 mid = text_caret_animate(&s, 40.f, 1, 1, t + 90000000ull, false, &a);
        ST_CHECK(mid > 20.f && mid < 40.f);
        f32 pos = mid;
        for (u64 dt = 200000000ull; dt <= 1000000000ull; dt += 200000000ull) // frames, not one long gap (that resets)
            pos = text_caret_animate(&s, 40.f, 1, 1, t + dt, false, &a);
        ST_CHECK(pos == 40.f && a < 1.f && a > 0.f); // breathing once settled
        f32 from = text_caret_position(&s, t + 1000000000ull);
        text_caret_animate(&s, 60.f, 2, 2, t + 1000000000ull, false, &a);
        ST_CHECK(fabsf(s.from - from) < 0.0001f && s.target == 60.f); // a new caret eases from where it was
        ST_CHECK(text_caret_animate(&s, 60.f, 2, 2, t + 1000000000ull, true, &a) == 60.f && a == 1.f); // snap
        TextEdit e;
        te_set(&e, "hello", 3, 3);
        ST_CHECK(!e.animation.valid && !e.animation.moved && !e.animation.seen && !e.animation.input);
        e.animation.input = 1;
        te_insert(&e, "XY");
        ST_CHECK(e.animation.input > 1 && e.caret == 5 && !strcmp(e.buf, "helXYlo"));
        e.animation.input = 1;
        ST_CHECK(te_key(&e, SDLK_LEFT, SDL_KMOD_NONE) && e.animation.input > 1);
    }

    // --- Date accessed, Group, Link target columns: filled with the metadata, sorted by, drawn ---
    {
        char back[PATH_MAX];
        snprintf(back, sizeof back, "%s", p->path);
        mkdir(M9_BASE "/cols", 0755);
        st_write(M9_BASE "/cols/plain", 3);
        ST_CHECK(symlink("zz-target", M9_BASE "/cols/l1") == 0 && symlink("aa-target", M9_BASE "/cols/l2") == 0);
        pane_navigate(p, M9_BASE "/cols", true);
        st_wait();
        Listing *L = p->L;
        ST_CHECK(L && L->links && L->count == 3);
        for (u32 i = 0; L && i < L->count; i++) {
            Entry *e = &L->entries[i];
            const char *want = !strcmp(e->name, "l1") ? "zz-target" : !strcmp(e->name, "l2") ? "aa-target" : NULL;
            ST_CHECK(e->gid == getgid() && e->atime > 0 && (want ? L->links[i] && !strcmp(L->links[i], want) : !L->links[i]));
        }
        for (int c = COL_ACCESSED; c <= COL_LINK; c++) do_action(p, ACT_COL_BASE + c);
        ST_CHECK(col_visible(COL_ACCESSED) && col_visible(COL_GROUP) && col_visible(COL_LINK));
        do_action(p, ACT_SORT_BASE + SORT_LINK);
        st_wait();
        st_frames(1);
        L = p->L; // (the watcher may have reloaded the folder meanwhile)
        ST_CHECK(L && p->sort_key == SORT_LINK && p->count == 3 && !strcmp(L->entries[p->order[0]].name, "l2") &&
                 !strcmp(L->entries[p->order[1]].name, "l1") && !strcmp(L->entries[p->order[2]].name, "plain"));
        st_shot("columns-link");
        for (int c = COL_ACCESSED; c <= COL_LINK; c++) do_action(p, ACT_COL_BASE + c);
        do_action(p, ACT_SORT_BASE + SORT_NAME);
        // CRC32 / SHA-256: read on their thread for the rows on screen, kept by path + size + mtime; a header click doesn't sort
        FILE *hf = fopen(M9_BASE "/cols/plain", "w");
        if (hf) { fputs("abc", hf); fclose(hf); }
        mkfifo(M9_BASE "/cols/pipe", 0644); // (never opened: not a regular file)
        pane_refresh(p);
        st_wait();
        do_action(p, ACT_COL_BASE + COL_CRC32);
        do_action(p, ACT_COL_BASE + COL_SHA256);
        const HashEnt *he = NULL;
        struct stat hst; // (not the list's entry: the watcher may reload the folder meanwhile)
        bool hok = stat(M9_BASE "/cols/plain", &hst) == 0;
        for (int k = 0; k < 400 && hok; k++) {
            st_frames(1);
            he = hash_get(M9_BASE "/cols/plain", (i64)hst.st_mtime, (u64)hst.st_size);
            if (he->st != HC_PENDING) break;
            SDL_Delay(5);
        }
        ST_CHECK(hok && hst.st_size == 3 && he && he->st == HC_DONE && he->crc == 0x352441C2u &&
                 !strcmp(he->sha, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        int sk = p->sort_key;
        st_frames(1);
        st_shot("columns-hash");
        for (int k = 0; k < p->ly.ncols; k++)
            if (p->ly.cid[k] == COL_SHA256) {
                f32 hx = (p->ly.cx0[k] + p->ly.cx1[k]) * 0.5f, hy = p->ly.header.y + p->ly.header.h * 0.5f;
                st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, hx, hy, SDL_BUTTON_LEFT, 1, 0);
                st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, hx, hy, SDL_BUTTON_LEFT, 1, 0);
            }
        ST_CHECK(p->sort_key == sk);
        do_action(p, ACT_COL_BASE + COL_CRC32);
        do_action(p, ACT_COL_BASE + COL_SHA256);
        pane_navigate(p, back, true);
        st_wait();
        st_rmrf(M9_BASE "/cols");
    }

    // --- the path bar's copy button (always): the folder's path to the clipboard; the crumbs end before it ---
    {
        st_frames(1);
        PaneLayout *cl = &p->ly;
        layout_crumbs(p);
        Rect cpb = cl->copypath, lc = cl->ncrumbs ? cl->crumbs[cl->ncrumbs - 1].r : (Rect){0};
        ST_CHECK(cpb.w > 0 && cpb.x + cpb.w <= cl->pathbar.x + cl->pathbar.w && lc.x + lc.w <= cpb.x);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, cpb.x + cpb.w * 0.5f, cpb.y + cpb.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, cpb.x + cpb.w * 0.5f, cpb.y + cpb.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(!A.edit_pane && strstr(A.toast, "Copied: ") && strstr(A.toast, p->path));
        st_mouse(SDL_EVENT_MOUSE_MOTION, cpb.x + cpb.w * 0.5f, cpb.y + cpb.h * 0.5f, 0, 0, 0);
        st_frames(1);
        ST_CHECK(A.ptr_hand && !A.ptr_text); // a button: the hand, not the text pointer of the field around it
    }

    // --- the ribbon (off by default): icons under the navigation bar, for that panel; the rest in "..." ---
    {
        ST_CHECK(!A.ribbon && p->ly.ribbon.h == 0);
        f32 hy0 = p->ly.header.y;
        A.ribbon = true;
        st_frames(2);
        PaneLayout *rl = &p->ly;
        ST_CHECK(rl->ribbon.h == roundf(S(36)) && rl->ribbon.y == rl->toolbar.y + rl->toolbar.h && rl->header.y == rl->ribbon.y + rl->ribbon.h &&
                 rl->header.y == hy0 + rl->ribbon.h);
        ST_CHECK(rl->morebtn.w == 0 && rl->filterbtn.w > 0); // (the ribbon has the folder's actions: no "..." in the toolbar)
        st_shot("ribbon");
        int hi = -1, di = -1;
        for (int i = 0; i < RIBBON_N; i++) {
            if (g_ribbon[i].act == ACT_HIDDEN) hi = i;
            if (g_ribbon[i].act == ACT_PREVIEW) di = i;
        }
        // everything fits in a wide panel; a click on an icon runs it (here: hidden files, drawn as on)
        ribbon_layout(p);
        bool h0 = A.show_hidden;
        ST_CHECK(g_rb.more.w == 0 && hi >= 0 && g_rb.r[hi].w > 0);
        Rect hb = g_rb.r[hi];
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, hb.x + hb.w * 0.5f, hb.y + hb.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, hb.x + hb.w * 0.5f, hb.y + hb.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(A.show_hidden == !h0 && ribbon_item_on(&g_ribbon[hi], p) == A.show_hidden);
        do_action(p, ACT_HIDDEN);
        // a dropdown: the right-click menu's submenu of the same name under the icon (Views: view modes, Tree view)
        int vi = -1;
        for (int i = 0; i < RIBBON_N; i++)
            if (g_ribbon[i].sub == RS_VIEWS) vi = i;
        Rect vb = vi >= 0 ? g_rb.r[vi] : (Rect){0};
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, vb.x + vb.w * 0.5f, vb.y + vb.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, vb.x + vb.w * 0.5f, vb.y + vb.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        bool tree_item = false;
        for (int i = 0; A.menu.open && i < A.menu.n; i++) tree_item |= A.menu.items[i].act == ACT_TREE_VIEW;
        ST_CHECK(vb.w > 0 && A.menu.open && tree_item && A.menu.r.y >= vb.y + vb.h);
        st_shot("ribbon-views");
        st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
        ST_CHECK(!A.menu.open);
        // icons: the right-click menu's for the same action
        ST_CHECK(ribbon_icon(&g_ribbon[hi]) == UI_EYE && ribbon_icon(&g_ribbon[0]) == UI_SCISSORS);
        // a narrow window: the last ones go into "...", which runs them like the icons
        int w0 = A.W;
        A.W = (int)roundf(S(640));
        st_frames(1);
        ribbon_layout(p);
        ST_CHECK(g_rb.more.w > 0 && di >= 0 && g_rb.shown[di] && g_rb.r[di].w == 0 && g_rb.more.x + g_rb.more.w <= rl->ribbon.x + rl->ribbon.w);
        bool p0 = A.preview_on;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, g_rb.more.x + 4, g_rb.more.y + 4, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, g_rb.more.x + 4, g_rb.more.y + 4, SDL_BUTTON_LEFT, 1, 0);
        int mi = -1;
        for (int i = 0; A.menu.open && i < A.menu.n; i++)
            if (A.menu.items[i].act == ACT_PREVIEW) mi = i;
        ST_CHECK(A.menu.open && mi >= 0);
        st_shot("ribbon-more");
        if (mi >= 0) menu_activate(mi);
        ST_CHECK(A.preview_on == !p0 && !A.menu.open);
        if (A.preview_on != p0) preview_toggle();
        A.W = w0;
        st_frames(1);
        // positions: above the path bar, the list's bottom / left / right (the list gives up the room)
        A.ribbon_pos = RP_ABOVE;
        st_frames(1);
        ST_CHECK(rl->ribbon.y + rl->ribbon.h == rl->toolbar.y && rl->header.y == rl->toolbar.y + rl->toolbar.h);
        A.ribbon_pos = RP_BOTTOM;
        st_frames(1);
        ST_CHECK(rl->header.y == hy0 && rl->ribbon.y + rl->ribbon.h == rl->area.y + rl->area.h && rl->list.y + rl->list.h <= rl->ribbon.y);
        A.ribbon_pos = RP_RIGHT;
        st_frames(1);
        ST_CHECK(rl->ribbon.w == roundf(S(40)) && rl->ribbon.x + rl->ribbon.w == rl->area.x + rl->area.w && rl->list.x + rl->list.w == rl->ribbon.x);
        st_shot("ribbon-right");
        // dragged by its grip to the pane's left edge: it goes there (shown while dragging)
        ribbon_layout(p);
        Rect gr = g_rb.grip;
        ST_CHECK(gr.w > 0 && in_rect(rl->ribbon, gr.x + 2, gr.y + 2));
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, gr.x + gr.w * 0.5f, gr.y + gr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(A.drag == DRAG_RIBBON);
        st_mouse(SDL_EVENT_MOUSE_MOTION, rl->area.x + S(10), rl->list.y + S(100), 0, 0, 0);
        ST_CHECK(A.ribbon_drop == RP_LEFT && A.ribbon_drop_pane == p);
        st_frames(1);
        st_shot("ribbon-drag");
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, rl->area.x + S(10), rl->list.y + S(100), SDL_BUTTON_LEFT, 1, 0);
        st_frames(1);
        ST_CHECK(A.drag == DRAG_NONE && A.ribbon_pos == RP_LEFT && rl->ribbon.x == rl->area.x && rl->list.x == rl->ribbon.x + rl->ribbon.w);
        // Wrap: a narrow window gets a second row instead of "..."
        A.ribbon_pos = RP_BELOW;
        A.ribbon_overflow = RO_WRAP;
        A.W = (int)roundf(S(640));
        st_frames(1);
        ribbon_layout(p);
        ST_CHECK(rl->ribbon.h == roundf(S(36)) * 2 && g_rb.more.w == 0 && di >= 0 && g_rb.r[di].w > 0 && g_rb.r[di].y > rl->ribbon.y + S(30));
        st_shot("ribbon-wrap");
        // Scroll: arrows at both ends; the next one (and the wheel) slide it along
        A.ribbon_overflow = RO_SCROLL;
        st_frames(1);
        ribbon_layout(p);
        ST_CHECK(rl->ribbon.h == roundf(S(36)) && g_rb.prev.w > 0 && g_rb.next.w > 0 && p->rb_scroll == 0 && g_rb.r[di].w == 0);
        Rect nb = g_rb.next;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, nb.x + nb.w * 0.5f, nb.y + nb.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, nb.x + nb.w * 0.5f, nb.y + nb.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        f32 sc1 = p->rb_scroll;
        ST_CHECK(sc1 > 0);
        f32 rcx = rl->ribbon.x + rl->ribbon.w * 0.5f, rcy = rl->ribbon.y + rl->ribbon.h * 0.5f;
        ST_CHECK(ribbon_wheel(rcx, rcy, 2)); // back a little
        st_frames(1);
        ST_CHECK(p->rb_scroll < sc1);
        ribbon_wheel(rcx, rcy, -60); // forward past the end: kept at the end, the last icon shows
        st_frames(1);
        ribbon_layout(p);
        ST_CHECK(fabsf(p->rb_scroll - (g_rb.total - g_rb.avail)) < 0.5f && g_rb.r[di].w > 0);
        st_shot("ribbon-scroll");
        A.ribbon_overflow = RO_MORE;
        A.W = w0;
        A.ribbon = false;
        st_frames(2);
        ST_CHECK(p->ly.ribbon.h == 0 && p->ly.header.y == hy0);
    }

    // --- the toolbar's sort button: the same keys as the folder menu's Sort by, the direction, Folders first ---
    st_frames(1);
    Rect sb = p->ly.sortbtn;
    ST_CHECK(sb.w > 0 && sb.x + sb.w <= p->ly.filterbtn.x && p->ly.pathbar.x + p->ly.pathbar.w <= sb.x);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, sb.x + 5, sb.y + 5, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, sb.x + 5, sb.y + 5, SDL_BUTTON_LEFT, 1, 0);
    int nkeys = SORT_CREATED + 1 + col_visible(COL_PERMS) + col_visible(COL_OWNER) + col_visible(COL_ACCESSED) + col_visible(COL_GROUP) +
                col_visible(COL_LINK);
    ST_CHECK(A.menu.open && A.menu.pane == p && A.menu.n == nkeys + 5);
    if (A.menu.open && A.menu.n == nkeys + 5) {
        bool has_ext = false;
        for (int i = 0; i < A.menu.n; i++) has_ext |= A.menu.items[i].act == ACT_SORT_BASE + SORT_EXT;
        ST_CHECK(!has_ext && A.menu.items[A.menu.n - 3].act == ACT_SORT_DESC && A.menu.items[A.menu.n - 1].act == ACT_FOLDERS_FIRST);
        ST_CHECK(A.menu.items[A.menu.n - 1].checked == atomic_load(&g_folders_first));
        st_frames(1);
        st_shot("toolbar-sort");
        st_key(SDLK_DOWN, 0); // keyboard: down to Size, Enter picks it
        st_key(SDLK_DOWN, 0);
        st_key(SDLK_DOWN, 0);
        st_key(SDLK_DOWN, 0);
        st_key(SDLK_RETURN, 0);
        ST_CHECK(!A.menu.open && p->sort_key == SORT_SIZE);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, sb.x + 5, sb.y + 5, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, sb.x + 5, sb.y + 5, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(A.menu.open && A.menu.items[SORT_SIZE].checked);
        bool was_desc = p->sort_desc;
        int dir = A.menu.n - (was_desc ? 4 : 3); // the other direction
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, A.menu.r.x + S(20), menu_item_y(&A.menu, dir) + S(12), SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(!A.menu.open && p->sort_key == SORT_SIZE && p->sort_desc == !was_desc);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, sb.x + 5, sb.y + 5, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, sb.x + 5, sb.y + 5, SDL_BUTTON_LEFT, 1, 0);
        st_key(SDLK_ESCAPE, 0);
        ST_CHECK(!A.menu.open);
    }

    // --- Folders first (on by default): folders above files whatever the key and direction; off: mixed ---
    pane_set_sort(p, SORT_NAME);
    for (int pass = 0; pass < 4; pass++) {
        bool first = pass < 2, desc = pass & 1;
        if (atomic_load(&g_folders_first) != first) do_action(p, ACT_FOLDERS_FIRST);
        do_action(p, desc ? ACT_SORT_DESC : ACT_SORT_ASC);
        st_frames(2);
        u32 last_dir = 0, first_file = p->count, ndirs = 0;
        for (u32 i = 0; i < p->count; i++) {
            if (p->L->entries[p->order[i]].flags & ENT_DIR) { last_dir = i; ndirs++; }
            else if (first_file == p->count) first_file = i;
        }
        ST_CHECK(ndirs == 2 && first_file < p->count);
        // by name file* < sub < tree: ascending puts files above the folders, descending below
        if (first) ST_CHECK(last_dir < first_file);
        else ST_CHECK(desc ? last_dir < first_file : last_dir > first_file);
    }
    ST_CHECK(!atomic_load(&g_folders_first));
    do_action(p, ACT_FOLDERS_FIRST);
    do_action(p, ACT_SORT_ASC);
    ST_CHECK(atomic_load(&g_folders_first));
    Rect area = p->ly.area; // a narrow split: no sort button, nothing overlaps
    layout_pane(p, (Rect){area.x, area.y, S(110), area.h});
    ST_CHECK(p->ly.sortbtn.w == 0 && p->ly.pathbar.w >= 0 && p->ly.pathbar.x + p->ly.pathbar.w <= p->ly.filterbtn.x);
    layout_pane(p, area);
    pane_set_sort(p, SORT_NAME);
    do_action(p, ACT_SORT_ASC);

    // --- the folder menu's Views / Sort by / Group by submenus ---
    st_frames(1);
    do_action(p, ACT_SORT_DESC);
    ST_CHECK(p->sort_desc);
    do_action(p, ACT_SORT_ASC);
    ST_CHECK(!p->sort_desc);
    // Views, Sort and Group share the connected Open With submenu behavior.
    menu_open(p, S(100), S(100), false);
    ST_CHECK(menu_icons_in(&A.menu)); // built-in icons: shown whatever fonts are installed
    for (int i = 0; i < UI_ICON_COUNT; i++) {
        NSVGimage *img = icon_image(ICON_UI_BASE + i);
        ST_CHECK(img && img->shapes);
    }
    int views = -1, sorts = -1, groups = -1;
    for (int i = 0; i < A.menu.n; i++) {
        int act = A.menu.items[i].act;
        if (act == ACT_MENU_VIEWS) views = i;
        if (act == ACT_MENU_SORT) sorts = i;
        if (act == ACT_MENU_GROUP) groups = i;
        ST_CHECK(!(act >= ACT_VIEW_BASE && act < ACT_VIEW_BASE + ZOOM_COUNT));
    }
    ST_CHECK(views >= 0 && sorts >= 0 && groups >= 0);
    if (views >= 0 && sorts >= 0 && groups >= 0) {
        ST_CHECK(A.menu.items[views].sub && A.menu.items[sorts].sub && A.menu.items[groups].sub);
        st_mouse(SDL_EVENT_MOUSE_MOTION, A.menu.r.x + S(20), menu_item_y(&A.menu, views) + S(12), 0, 0, 0);
        ST_CHECK(menu_sub_shown() && g_menu_sub.items[0].act == ACT_VIEW_BASE);
        st_frames(1);
        st_shot("menu-views");
        st_mouse(SDL_EVENT_MOUSE_MOTION, g_menu_sub.r.x + S(20), menu_item_y(&g_menu_sub, 0) + S(12), 0, 0, 0);
        ST_CHECK(g_menu_sub.hover == 0 && A.menu.open);
        st_mouse(SDL_EVENT_MOUSE_MOTION, A.menu.r.x + S(20), menu_item_y(&A.menu, sorts) + S(12), 0, 0, 0);
        ST_CHECK(menu_sub_shown() && g_menu_sub_of == sorts);
        bool has_ext = false, has_perms = false;
        for (int i = 0; i < g_menu_sub.n; i++) {
            has_ext |= g_menu_sub.items[i].act == ACT_SORT_BASE + SORT_EXT;
            has_perms |= g_menu_sub.items[i].act == ACT_SORT_BASE + SORT_PERMS;
        }
        // Extension sorts like Type; Permissions only with its column shown
        ST_CHECK(!has_ext && has_perms == col_visible(COL_PERMS) && g_menu_sub.items[0].act == ACT_SORT_BASE + SORT_NAME);
        int descending = g_menu_sub.n - 3;
        ST_CHECK(g_menu_sub.items[descending].act == ACT_SORT_DESC);
        st_shot("menu-sort");
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, g_menu_sub.r.x + S(20), menu_item_y(&g_menu_sub, descending) + S(12), SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(!A.menu.open && p->sort_desc);
        do_action(p, ACT_SORT_BASE + SORT_NAME); // picking the current key keeps its direction
        ST_CHECK(p->sort_key == SORT_NAME && p->sort_desc);
        do_action(p, ACT_SORT_ASC);

        menu_open(p, S(100), S(100), false);
        st_mouse(SDL_EVENT_MOUSE_MOTION, A.menu.r.x + S(20), menu_item_y(&A.menu, groups) + S(12), 0, 0, 0);
        ST_CHECK(menu_sub_shown() && g_menu_sub_of == groups && g_menu_sub.n == GROUP_COUNT);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, g_menu_sub.r.x + S(20), menu_item_y(&g_menu_sub, GROUP_NONE) + S(12), SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(!A.menu.open && p->group == GROUP_NONE);

        // the keyboard: Right opens the submenu, Left / Esc goes back, Enter picks
        menu_open(p, S(100), S(100), false);
        A.menu.hover = views;
        menu_key(SDLK_RIGHT);
        ST_CHECK(menu_sub_shown() && g_menu_sub.hover == 0);
        menu_key(SDLK_ESCAPE);
        ST_CHECK(A.menu.open && !menu_sub_shown());
        A.menu.hover = sorts;
        menu_key(SDLK_RIGHT);
        ST_CHECK(menu_sub_shown() && g_menu_sub_of == sorts && g_menu_sub.hover == 0);
        menu_key(SDLK_LEFT);
        ST_CHECK(A.menu.open && !menu_sub_shown() && A.menu.hover == sorts);
        menu_key(SDLK_RETURN); // Enter on a submenu's item opens it too
        ST_CHECK(menu_sub_shown() && g_menu_sub.hover == 0);
        menu_key(SDLK_DOWN);
        ST_CHECK(g_menu_sub.hover == 1 && g_menu_sub.items[1].act == ACT_SORT_BASE + SORT_DATE);
        menu_key(SDLK_RETURN);
        ST_CHECK(!A.menu.open && p->sort_key == SORT_DATE);
        pane_set_sort(p, SORT_NAME);
        menu_open(p, S(100), S(100), false);
        menu_key(SDLK_ESCAPE);
        ST_CHECK(!A.menu.open);
    }

    // --- themes ---
    ST_CHECK(g_nthemes >= 5 && T.bg == g_theme_dark.bg);
    st_shot("m9-dark");
    st_key(SDLK_COMMA, SDL_KMOD_CTRL);
    ST_CHECK(g_sheet.kind == SHEET_OPTIONS);
    st_frames(2);
    st_shot("options");
    // theme dropdown: open, move down, pick
    st_opt_click(st_opt_hit(HP_DROP, OPT_THEME, 0));
    ST_CHECK(g_opt.drop_opt == OPT_THEME);
    st_frames(1);
    st_shot("options-dropdown");
    { // hovering an item highlights it (the selection follows the mouse)
        int sel0 = g_opt.drop_sel;
        OptHit *it = st_opt_hit(HP_DROPITEM, OPT_THEME, 2);
        ST_CHECK(it != NULL);
        if (it) {
            st_mouse(SDL_EVENT_MOUSE_MOTION, it->r.x + it->r.w * 0.5f, it->r.y + it->r.h * 0.5f, 0, 0, 0);
            st_frames(2);
            ST_CHECK(g_opt.drop_sel == 2);
            st_shot("options-dropdown-hover");
        }
        st_mouse(SDL_EVENT_MOUSE_MOTION, g_sheet.r.x + S(20), g_sheet.r.y + S(10), 0, 0, 0); // off the list
        st_frames(1);
        g_opt.drop_sel = sel0;
    }
    st_key(SDLK_DOWN, SDL_KMOD_NONE);
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    ST_CHECK(g_opt.drop_opt < 0 && g_theme_cur == theme_find("Light") && T.bg == g_theme_light.bg);
    st_frames(2);
    st_shot("options-light");
    // accent swatch
    st_opt_click(st_opt_hit(HP_SWATCH, OPT_ACCENT, 3)); // Pink
    ST_CHECK(g_accent_custom == (0x01000000u | g_accents[2].rgb) && T.accent == g_accents[2].rgb);
    ST_CHECK(T.sel != g_theme_light.sel);
    { // picking a palette swatch also leaves desktop-accent mode
        bool da = g_accent_desktop; u32 de = g_desktop.accent;
        g_accent_desktop = true; g_desktop.accent = 0x01000000u | 0x3584E4; theme_apply();
        st_opt_click(st_opt_hit(HP_SWATCH, OPT_ACCENT, 0)); // theme default
        ST_CHECK(!g_accent_desktop && !g_accent_custom && T.accent == g_theme_light.accent);
        g_desktop.accent = de; g_accent_desktop = da; theme_apply();
    }
    st_opt_click(st_opt_hit(HP_SWATCH, OPT_ACCENT, 0)); // theme default (idempotent)
    ST_CHECK(!g_accent_custom && T.accent == g_theme_light.accent);
    { // the Blue preset applies Blue: under the System theme it isn't the theme's own accent (the desktop's is)
        int saved = g_theme_cur; u32 de = g_desktop.accent; bool da = g_accent_desktop;
        g_theme_cur = theme_find("System");
        g_desktop.accent = 0x01000000u | 0x3584E4;
        g_accent_desktop = true;
        theme_apply();
        st_frames(1);
        st_opt_click(st_opt_hit(HP_SWATCH, OPT_ACCENT, 1)); // Blue
        ST_CHECK(g_accent_custom == (0x01000000u | g_accents[0].rgb) && T.accent == g_accents[0].rgb);
        g_theme_cur = saved; g_accent_custom = 0; g_desktop.accent = de; g_accent_desktop = da; theme_apply();
        st_frames(1);
    }
    { // the theme's own accent only gets its own swatch when it isn't a preset (Dark's is Blue: no duplicate)
        int saved = g_theme_cur, counts[2] = {0, 0};
        const char *names[2] = {"Dark", "Monokai"};
        for (int k = 0; k < 2; k++) {
            g_theme_cur = theme_find(names[k]);
            theme_apply();
            st_frames(1);
            for (int h = 0; h < g_opt.nhits; h++) counts[k] += g_opt.hits[h].part == HP_SWATCH;
        }
        ST_CHECK(counts[0] == ACCENT_COUNT && counts[1] == ACCENT_COUNT + 1);
        g_theme_cur = saved;
        theme_apply();
        st_frames(1);
    }
    // search filters rows (also by choice values)
    st_type("vulkan");
    st_frames(1);
    ST_CHECK(st_opt_hit(HP_DROP, OPT_RENDERER, 0) && !st_opt_hit(HP_TOGGLE, OPT_HIDDEN, 0));
    // Automatic names the backend it runs on (self-tests: software)
    ST_CHECK(!strcmp(opt_choice_label(OPT_RENDERER, 0), "Automatic (Software)") && !strcmp(opt_choice_label(OPT_RENDERER, 1), "Vulkan"));
    st_shot("options-search");
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE); // clears the search
    ST_CHECK(g_sheet.kind == SHEET_OPTIONS && !g_opt.search.buf[0]);
    // toggles from the Behaviour page
    st_opt_click(st_opt_hit(HP_NAV, -1, OC_BEHAVIOUR));
    bool hid = A.show_hidden;
    st_opt_click(st_opt_hit(HP_TOGGLE, OPT_HIDDEN, 0));
    ST_CHECK(A.show_hidden == !hid);
    st_opt_click(st_opt_hit(HP_TOGGLE, OPT_HIDDEN, 0));
    ST_CHECK(A.show_hidden == hid);
    // appearance: font size, row spacing (the icon rows pushed these below the fold)
    st_opt_click(st_opt_hit(HP_NAV, -1, OC_APPEARANCE));
    st_frames(1);
    g_opt.scroll = S(140);
    st_frames(1);
    int px0 = A.font.px;
    st_opt_click(st_opt_hit(HP_PLUS, OPT_FONT_SIZE, 0));
    ST_CHECK(A.font_size == 14 && A.font.px == (int)roundf(14.f * A.scale) && A.font.px > px0);
    st_opt_click(st_opt_hit(HP_MINUS, OPT_FONT_SIZE, 0));
    ST_CHECK(A.font_size == 13);
    st_opt_click(st_opt_hit(HP_SEG, OPT_ROW_SPACING, 0));
    ST_CHECK(A.row_spacing == 0);
    g_opt.scroll = 0;
    st_frames(1);
    { // the scrollbar's thumb can be dragged (not only the wheel); a press on its track pages
        st_frames(1);
        SheetBar *sb = NULL;
        for (int k = 0; k < g_sbar.n; k++)
            if (g_sbar.bar[k].scroll == &g_opt.scroll) sb = &g_sbar.bar[k];
        ST_CHECK(sb && g_opt.max_scroll > 0 && g_opt.scroll == 0);
        if (sb) {
            f32 bx = sb->hit.x + sb->hit.w * 0.5f, by = sb->y0 + sb->th * 0.5f, end = sb->y0 + sb->travel;
            st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, bx, by, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
            ST_CHECK(g_sbar.drag == &g_opt.scroll);
            st_mouse(SDL_EVENT_MOUSE_MOTION, bx, by + sb->travel * 0.5f, 0, 0, SDL_KMOD_NONE);
            ST_CHECK(fabsf(g_opt.scroll - g_opt.max_scroll * 0.5f) < 1.f);
            st_mouse(SDL_EVENT_MOUSE_MOTION, bx, end + S(200), 0, 0, SDL_KMOD_NONE); // past the end: clamped
            ST_CHECK(g_opt.scroll == g_opt.max_scroll);
            st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, bx, end + S(200), SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
            ST_CHECK(!g_sbar.drag && g_sheet.kind == SHEET_OPTIONS);
            st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, bx, sb->y0 + S(2), SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE); // above the thumb
            st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, bx, sb->y0 + S(2), SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
            ST_CHECK(!g_sbar.drag && g_opt.scroll < g_opt.max_scroll);
            g_opt.scroll = 0;
        }
    }
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    ST_CHECK(!sheet_open());
    layout();
    ST_CHECK(p->ly.row_h == roundf(S(22)));
    st_frames(2);
    st_shot("m9-light-compact");
    A.row_spacing = 1;

    // JSON colour scheme: inherits from its base, overrides given keys
    char tdir[PATH_MAX], tpath[PATH_MAX + 32];
    themes_dir(tdir, sizeof tdir);
    mkdir_p(tdir, 0700);
    snprintf(tpath, sizeof tpath, "%s/test.json", tdir);
    FILE *f = fopen(tpath, "w");
    fputs("{\n  \"name\": \"Test Scheme\",\n  \"base\": \"light\",\n  \"colors\": { \"bg\": \"#112233\", \"accent\": \"#f80\" }\n}\n", f);
    fclose(f);
    options_open();
    int ti = theme_find("test scheme");
    ST_CHECK(ti >= 5);
    if (ti >= 0) {
        opt_choice_set(OPT_THEME, ti);
        ST_CHECK(T.bg == 0x112233 && T.accent == 0xFF8800 && T.text == g_theme_light.text);
    }
    // export -> reload round trip
    snprintf(tpath, sizeof tpath, "%s/roundtrip.json", tdir);
    ST_CHECK(theme_export(tpath, "Roundtrip"));
    ThemeEntry te;
    ST_CHECK(theme_load_json(tpath, &te) && !memcmp(&te.t, &T, sizeof T) && !strcmp(te.name, "Roundtrip"));
    // "Create theme" button writes a new scheme and selects it
    opt_button(OPT_THEME_NEW);
    ST_CHECK(theme_find("My theme") >= 0 && g_theme_cur == theme_find("My theme"));
    sheet_close();
    { // Theme "System": Light or Dark as the desktop says (Dark until it does), its accent unless one is picked
        u32 saved_accent = g_accent_custom;
        g_accent_custom = 0;
        memset(&g_desktop, 0, sizeof g_desktop);
        opt_choice_set(OPT_THEME, theme_find("System"));
        ST_CHECK(theme_is_system() && T.bg == g_theme_dark.bg && T.accent == g_theme_dark.accent && !g_dlook.running); // (no watcher in tests)
        DesktopLook *d = xcalloc(1, sizeof *d);
        *d = (DesktopLook){.light = true, .accent = 0x01000000u | 0x3584E4, .icons = "hicolor"};
        dlook_apply(d);
        ST_CHECK(g_desktop.known && T.bg == g_theme_light.bg && T.accent == 0x3584E4 && T.sel != g_theme_light.sel);
        g_accent_custom = 0x01000000u | g_accents[1].rgb; // a picked accent wins
        theme_apply();
        ST_CHECK(T.accent == g_accents[1].rgb && T.bg == g_theme_light.bg);
        g_accent_custom = 0;
        d = xcalloc(1, sizeof *d);
        *d = (DesktopLook){.light = false, .icons = "hicolor"}; // dark again, no accent: the theme's own
        dlook_apply(d);
        ST_CHECK(T.bg == g_theme_dark.bg && T.accent == g_theme_dark.accent);
        settings_save();
        settings_load();
        ST_CHECK(!strcmp(A.theme_name, "System"));
        // Light theme / Dark theme: which ones System switches between (only shown with System; every theme but System)
        ST_CHECK(!opt_hidden(OPT_THEME_LIGHT) && !opt_hidden(OPT_THEME_DARK) && opt_choice_count(OPT_THEME_DARK) == g_nthemes - 1);
        ST_CHECK(!strcmp(opt_choice_label(OPT_THEME_LIGHT, opt_choice_get(OPT_THEME_LIGHT)), "Light") &&
                 !strcmp(opt_choice_label(OPT_THEME_DARK, opt_choice_get(OPT_THEME_DARK)), "Dark"));
        for (int i = 0; i < opt_choice_count(OPT_THEME_DARK); i++) {
            ST_CHECK(strcmp(opt_choice_label(OPT_THEME_DARK, i), "System"));
            if (!strcmp(opt_choice_label(OPT_THEME_DARK, i), "Nord")) opt_choice_set(OPT_THEME_DARK, i);
        }
        ST_CHECK(T.bg == g_themes[theme_find("Nord")].t.bg && !strcmp(g_theme_pick[1], "Nord")); // the desktop is dark
        d = xcalloc(1, sizeof *d);
        *d = (DesktopLook){.light = true, .icons = "hicolor"};
        dlook_apply(d);
        ST_CHECK(T.bg == g_theme_light.bg); // light: still the built-in Light
        settings_save();
        snprintf(g_theme_pick[1], sizeof g_theme_pick[1], "Dark");
        settings_load();
        ST_CHECK(!strcmp(g_theme_pick[1], "Nord") && !strcmp(g_theme_pick[0], "Light"));
        snprintf(g_theme_pick[1], sizeof g_theme_pick[1], "Dark");
        opt_choice_set(OPT_THEME, theme_find("Nord")); // other themes ignore the desktop
        ST_CHECK(opt_hidden(OPT_THEME_LIGHT) && opt_hidden(OPT_THEME_DARK));
        d = xcalloc(1, sizeof *d);
        *d = (DesktopLook){.light = true, .accent = 0x01000000u | 0x3584E4, .icons = "hicolor"};
        dlook_apply(d);
        ST_CHECK(!theme_is_system() && T.bg == g_themes[theme_find("Nord")].t.bg && T.accent == g_themes[theme_find("Nord")].t.accent);
        // on KDE: its colour scheme, read only where KDE is (its portal, or kdeglobals when the session is KDE)
        opt_choice_set(OPT_THEME, theme_find("System"));
        d = xcalloc(1, sizeof *d);
        *d = (DesktopLook){.light = true, .accent = 0x01000000u | 0x3DAEE9, .icons = "breeze",
                           .kde = {0x01000000u | 0xFCFCFC, 0x01000000u | 0xEFF0F1, 0x01000000u | 0x232627, 0x01000000u | 0x3DAEE9}};
        dlook_apply(d);
        ST_CHECK(T.accent == 0x3DAEE9);
        ST_CHECK(fabsf(rgb_luma(T.bg) - rgb_luma(g_theme_light.bg)) < 0.02f);
        ST_CHECK(fabsf(rgb_luma(T.sidebar) - rgb_luma(g_theme_light.sidebar)) < 0.02f);
        ST_CHECK(fabsf(rgb_luma(T.toolbar) - rgb_luma(g_theme_light.toolbar)) < 0.02f);
        ST_CHECK(T.text == 0x232627);
        ST_CHECK(T.tabbar != T.toolbar && COL_STATUS == T.toolbar);
        st_frames(2);
        st_shot("m9-system-kde");
        char kg[PATH_MAX], *de0 = getenv("XDG_CURRENT_DESKTOP") ? strdup(getenv("XDG_CURRENT_DESKTOP")) : NULL;
        snprintf(kg, sizeof kg, "%s/kdeglobals", getenv("XDG_CONFIG_HOME"));
        FILE *kf = fopen(kg, "w");
        if (kf) {
            fputs("[Colors:Selection]\nBackgroundNormal=61,174,233\n[Colors:View]\nBackgroundNormal=20,22,24\nForegroundNormal=252,252,252\n"
                  "[Colors:Window]\nBackgroundNormal=32,35,38\n", kf);
            fclose(kf);
        }
        DesktopLook look;
        const char *des[3] = {"KDE", "GNOME", ""};
        for (int k = 0; k < 3; k++) {
            setenv("XDG_CURRENT_DESKTOP", des[k], 1);
            dlook_get(NULL, &look);
            if (k == 0) ST_CHECK(look.kde[KDE_VIEW] == (0x01000000u | 0x141618) && look.kde[KDE_WINDOW] == (0x01000000u | 0x202326) && !look.light &&
                                 look.accent == (0x01000000u | 0x3DAEE9));
            else ST_CHECK(!look.kde[KDE_VIEW] && !look.accent); // (not KDE, or not saying: the file is ignored)
        }
        if (de0) setenv("XDG_CURRENT_DESKTOP", de0, 1);
        else unsetenv("XDG_CURRENT_DESKTOP");
        free(de0);
        unlink(kg);
        memset(&g_desktop, 0, sizeof g_desktop);
        theme_apply();
        ST_CHECK(T.bg == g_theme_dark.bg && T.text == g_theme_dark.text);
        // Desktop's accent colour (off by default): with every theme, over a picked one; none from the desktop: as before
        opt_choice_set(OPT_THEME, theme_find("Nord"));
        g_accent_custom = 0x01000000u | g_accents[1].rgb;
        ST_CHECK(!g_accent_desktop);
        opt_bool_toggle(OPT_ACCENT_DESKTOP);
        ST_CHECK(g_accent_desktop && T.accent == g_accents[1].rgb && !g_dlook.running); // (no desktop answer yet; no watcher in tests)
        d = xcalloc(1, sizeof *d);
        *d = (DesktopLook){.light = true, .accent = 0x01000000u | 0x3584E4, .icons = "hicolor"};
        dlook_apply(d);
        ST_CHECK(T.accent == 0x3584E4 && T.bg == g_themes[theme_find("Nord")].t.bg);
        settings_save();
        settings_load();
        ST_CHECK(g_accent_desktop);
        opt_bool_toggle(OPT_ACCENT_DESKTOP);
        ST_CHECK(!g_accent_desktop && T.accent == g_accents[1].rgb);
        memset(&g_desktop, 0, sizeof g_desktop);
        g_accent_custom = saved_accent;
    }

    // settings round trip
    g_theme_cur = theme_find("Nord");
    g_accent_custom = 0x01000000u | 0x123456;
    theme_apply();
    A.font_size = 15;
    A.row_spacing = 2;
    A.smooth_scroll = false;
    settings_save();
    settings_load();
    ST_CHECK(!strcmp(A.theme_name, "Nord") && g_accent_custom == (0x01000000u | 0x123456));
    ST_CHECK(A.font_size == 15 && A.row_spacing == 2 && !A.smooth_scroll);
    // no smooth scrolling: scroll jumps to the target in one frame
    p->scroll_target = MIN(max_scroll(p), 50.0);
    st_frames(1);
    ST_CHECK(p->scroll == p->scroll_target);
    A.smooth_scroll = true;
    A.row_spacing = 1;
    A.font_size = 13;
    reload_fonts();
    g_accent_custom = 0;
    { // Unified surfaces (off by default): sidebar, header, preview and status bar take the file area's background
        int saved = g_theme_cur;
        g_theme_cur = theme_find("Dark");
        theme_apply();
        ST_CHECK(!g_unified_surfaces && !memcmp(&T, &g_theme_dark, sizeof T));
        opt_bool_toggle(OPT_UNIFIED);
        ST_CHECK(T.sidebar == T.bg && T.tabbar == T.bg && T.toolbar == T.bg && T.preview_bg == T.bg && T.border == g_theme_dark.border);
        st_frames(2);
        st_shot("m9-unified");
        settings_save();
        settings_load();
        ST_CHECK(g_unified_surfaces);
        opt_bool_toggle(OPT_UNIFIED);
        ST_CHECK(!g_unified_surfaces && !memcmp(&T, &g_theme_dark, sizeof T));
        // separator lines (on by default)
        ST_CHECK(g_separators);
        opt_bool_toggle(OPT_SEPARATORS);
        st_frames(2);
        st_shot("m9-no-separators");
        settings_save();
        settings_load();
        ST_CHECK(!g_separators);
        opt_bool_toggle(OPT_SEPARATORS);
        ST_CHECK(g_separators);
        // brightness per part, always from the theme's own colour; the status bar is the toolbar's until moved
        ST_CHECK(COL_STATUS == g_theme_dark.toolbar);
        opt_int_step(OPT_BRIGHT_SIDEBAR, +1);
        opt_int_step(OPT_BRIGHT_SIDEBAR, +1);
        for (int k = 0; k < 8; k++) opt_int_step(OPT_BRIGHT_STATUS, -1);
        ST_CHECK(g_surface_bright[SURF_SIDEBAR] == 10 && T.sidebar == rgb_mix(g_theme_dark.sidebar, 0xFFFFFF, 0.10f));
        ST_CHECK(g_surface_bright[SURF_STATUS] == -30 && COL_STATUS == rgb_mix(g_theme_dark.toolbar, 0x000000, 0.30f));
        ST_CHECK(T.bg == g_theme_dark.bg && T.toolbar == g_theme_dark.toolbar && T.preview_bg == g_theme_dark.preview_bg);
        char buf[16];
        opt_int_fmt(OPT_BRIGHT_SIDEBAR, buf, sizeof buf);
        ST_CHECK(!strcmp(buf, "+10%"));
        st_frames(2);
        st_shot("m9-brightness");
        settings_save();
        settings_load();
        theme_apply();
        ST_CHECK(g_surface_bright[SURF_SIDEBAR] == 10 && g_surface_bright[SURF_STATUS] == -30 && !g_surface_bright[SURF_FILES]);
        ST_CHECK(T.sidebar == rgb_mix(g_theme_dark.sidebar, 0xFFFFFF, 0.10f));
        u32 sidebar = T.sidebar, status = COL_STATUS;
        opt_button(OPT_THEME_NEW); // a scheme from the current colours keeps them, without applying the brightness twice
        ST_CHECK(!g_surface_bright[SURF_SIDEBAR] && T.sidebar == sidebar && g_surface_bright[SURF_STATUS] == -30 && COL_STATUS == status);
        memset(g_surface_bright, 0, sizeof g_surface_bright);
        g_theme_cur = saved;
        theme_apply();
    }
    { // path field / pill enclosure and folder search brightness are independent of the header
        ST_CHECK(!g_surface_bright[SURF_PATHBAR] && !g_surface_bright[SURF_SEARCHBAR] && !g_transparent_pathbar);
        opt_int_step(OPT_BRIGHT_PATHBAR, +1);
        opt_int_step(OPT_BRIGHT_PATHBAR, +1);
        opt_int_step(OPT_BRIGHT_SEARCHBAR, -1);
        opt_int_step(OPT_BRIGHT_SEARCHBAR, -1);
        ST_CHECK(g_surface_bright[SURF_PATHBAR] == 10 && g_surface_bright[SURF_SEARCHBAR] == -10);
        char fbuf[16];
        opt_int_fmt(OPT_BRIGHT_PATHBAR, fbuf, sizeof fbuf);
        ST_CHECK(!strcmp(fbuf, "+10%"));
        g_theme_cur = theme_find("Dark");
        theme_apply();
        ST_CHECK(pathbar_field_color() == rgb_mix(g_theme_dark.field, 0xFFFFFF, 0.10f));
        ST_CHECK(searchbar_field_color() == rgb_mix(g_theme_dark.field, 0x000000, 0.10f));
        opt_bool_toggle(OPT_TRANSPARENT_PATHBAR);
        ST_CHECK(g_transparent_pathbar);
        st_frames(2);
        st_shot("m9-transparent-pathbar");
        settings_save();
        settings_load();
        init_places();
        theme_apply();
        ST_CHECK(g_surface_bright[SURF_PATHBAR] == 10 && g_surface_bright[SURF_SEARCHBAR] == -10 && g_transparent_pathbar);
        char cfgp[PATH_MAX];
        settings_file(cfgp, sizeof cfgp, false);
        FILE *cf = fopen(cfgp, "w");
        ST_CHECK(cf != NULL);
        if (cf) {
            fputs("pathbar_brightness=99\nsearchbar_brightness=-99\ntransparent_pathbar=1\n", cf);
            fclose(cf);
        }
        settings_load();
        init_places();
        ST_CHECK(g_surface_bright[SURF_PATHBAR] == 30 && g_surface_bright[SURF_SEARCHBAR] == -30 && g_transparent_pathbar);
        // No settings_version bump: an older config must leave the new keys at their defaults.
        cf = fopen(cfgp, "w");
        ST_CHECK(cf != NULL);
        if (cf) {
            fputs("header_brightness=-30\n", cf);
            fclose(cf);
        }
        settings_load();
        init_places();
        ST_CHECK(!g_surface_bright[SURF_PATHBAR] && !g_surface_bright[SURF_SEARCHBAR] && !g_transparent_pathbar);
        memset(g_surface_bright, 0, sizeof g_surface_bright);
        g_transparent_pathbar = false;
        theme_apply();
    }
    { // two tab modes: Classic above the navigation bar, Pill below it; one tab can give its row back (Behavior setting)
        ST_CHECK(opt_choice_count(OPT_TAB_STYLE) == 2);
        ST_CHECK(!strcmp(opt_choice_label(OPT_TAB_STYLE, 0), "Classic") &&
                 !strcmp(opt_choice_label(OPT_TAB_STYLE, 1), "Pills"));
        opt_choice_set(OPT_TAB_STYLE, TABS_CARDS);
        TabBands b = tab_bands(100.f, 1.f, 2, 0);
        ST_CHECK(b.tabs_y == 100.f && b.tabs_h == 38.f);
        ST_CHECK(b.navigation_y == 138.f && b.nav_h == 46.f);
        ST_CHECK(b.content_y == b.navigation_y + b.nav_h);
        opt_choice_set(OPT_TAB_STYLE, TABS_PILLS);
        b = tab_bands(100.f, 1.f, 2, 0);
        ST_CHECK(b.navigation_y == 100.f && b.nav_h == 46.f);
        ST_CHECK(b.tabs_y == 146.f && b.tabs_h == 44.f);
        ST_CHECK(b.content_y == b.tabs_y + b.tabs_h);
        ST_CHECK(tab_bands(100.f, 1.f, 1, 0).tabs_h == 44.f); // default: the strip stays
        ST_CHECK(!g_hide_single_tab);
        opt_bool_toggle(OPT_HIDE_SINGLE_TAB);
        ST_CHECK(g_hide_single_tab);
        b = tab_bands(100.f, 1.f, 1, 0);
        ST_CHECK(b.tabs_h == 0.f && b.content_y == b.navigation_y + b.nav_h);
        ST_CHECK(tab_bands(100.f, 1.f, 2, 0).tabs_h == 44.f);
        opt_bool_toggle(OPT_HIDE_SINGLE_TAB);
        ST_CHECK(!g_hide_single_tab);
        Panel *pn = A.ap;
        opt_choice_set(OPT_TAB_STYLE, TABS_PILLS);
        layout();
        ST_CHECK(pn->bar.y == pn->area.y + 46.f);
        ST_CHECK(panel_content_rect(pn).y == tab_bands(pn->area.y, 1.f, panel_ntabs(pn), 0).content_y);
        {
            TabBands pnb = tab_bands(pn->area.y, 1.f, panel_ntabs(pn), 0);
            PaneLayout *pnly = &pn->cur->pane[0].ly;
            ST_CHECK(pnly->toolbar.y == pnb.navigation_y);
            ST_CHECK(pnly->header.y == pnb.content_y && pnly->list.y == pnb.content_y + pnly->header.h);
        }
        opt_choice_set(OPT_TAB_STYLE, TABS_CARDS);
        layout();
        ST_CHECK(pn->bar.y == pn->area.y);
        ST_CHECK(panel_content_rect(pn).y == tab_bands(pn->area.y, 1.f, panel_ntabs(pn), 0).content_y);
        {
            TabBands pnb = tab_bands(pn->area.y, 1.f, panel_ntabs(pn), 0);
            PaneLayout *pnly = &pn->cur->pane[0].ly;
            ST_CHECK(pnly->toolbar.y == pnb.navigation_y);
            ST_CHECK(pnly->header.y == pnb.content_y && pnly->list.y == pnb.content_y + pnly->header.h);
        }
        opt_choice_set(OPT_TAB_STYLE, TABS_PILLS);
        g_hide_single_tab = true;
        layout();
        ST_CHECK(pn->bar.h == 0.f && pn->splitbtn.w == 0.f);
        { // a hidden band must not make the panel look full: a new tab still fits
            ST_CHECK(pn->tabs_w >= TAB_MIN_W && tab_target_panel(pn) == pn);
            int nt0 = A.ntabs;
            Tab *nt = tab_new_fit(pn, M9_BASE, panel_end_index(pn), false);
            ST_CHECK(nt && A.ntabs == nt0 + 1);
            if (nt) tab_close(tab_index(nt));
            ST_CHECK(A.ntabs == nt0);
        }
        settings_save();
        opt_choice_set(OPT_TAB_STYLE, TABS_CARDS);
        g_hide_single_tab = false;
        settings_load();
        init_places();
        ST_CHECK(A.tab_style == TABS_PILLS && g_hide_single_tab);
        st_frames(2);
        st_shot("m9-tab-pill-below-nav");
        g_hide_single_tab = false;
        opt_choice_set(OPT_TAB_STYLE, TABS_CARDS);
        layout();
    }
    { // application icon: an absolute SVG/PNG overrides the built-in logo; clearing it restores the default
        int ii = -1, ia = -1, it = -1, il = -1;
        for (int k = 0; k < OPT_COUNT; k++) {
            if (g_opts[k].id == OPT_ICON_THEME) ii = k;
            if (g_opts[k].id == OPT_APP_ICON) ia = k;
            if (g_opts[k].id == OPT_TITLE_ICON) it = k;
            if (g_opts[k].id == OPT_BRAND_LOGO) il = k;
        }
        ST_CHECK(ii >= 0 && ia == ii + 1 && it == ia + 1 && il >= 0);
        options_open();
        g_opt.cat = OC_APPEARANCE;
        g_opt.scroll = 0;
        st_frames(2);
        st_shot("options-app-icon");
        { // both icon rows: every button shares the field height and stays inside a narrow control strip
            OptHit *af = st_opt_hit(HP_TEXT, OPT_APP_ICON, 2), *tf = st_opt_hit(HP_TEXT, OPT_TITLE_ICON, 3);
            OptHit *as = st_opt_hit(HP_BUTTON, OPT_APP_ICON, 1), *ad = st_opt_hit(HP_BUTTON, OPT_APP_ICON, 2);
            OptHit *td = st_opt_hit(HP_BUTTON, OPT_TITLE_ICON, 1);
            ST_CHECK(af && tf && as && ad && td); // Apply has no hit when its field is empty
            ST_CHECK(af && as && fabsf(as->r.y - af->r.y) < 0.5f && fabsf(ad->r.y - af->r.y) < 0.5f);
            ST_CHECK(tf && td && fabsf(td->r.y - tf->r.y) < 0.5f);
            ST_CHECK(!strcmp(g_opts[ia].label, "App icon"));
            ST_CHECK(strstr(g_opts[ia].desc, "Apply") && strstr(g_opts[ia].desc, "File-Icon") && strstr(g_opts[ia].desc, "Default"));
            ST_CHECK(!strstr(g_opts[ia].desc, "System"));
            ST_CHECK(!strcmp(opt_icon_button_label(OPT_APP_ICON, 1), "File-Icon"));
            ST_CHECK(strstr(g_opts[it].desc, "Apply") && strstr(g_opts[it].desc, "Default"));
            Rect pv, fld, bb[3];
            opt_icon_row_layout(S(186), S(30), 3, &pv, &fld, bb);
            ST_CHECK(fld.w > 0 && pv.x <= fld.x && fld.x + fld.w <= bb[0].x - 0.5f);
            for (int k = 0; k < 3; k++) {
                ST_CHECK(bb[k].x >= fld.x + fld.w - 0.5f && bb[k].x + bb[k].w <= S(186) + 0.5f);
                if (k) ST_CHECK(bb[k].x >= bb[k - 1].x + bb[k - 1].w - 0.5f);
            }
            Rect pv2, fld2, bb2[2];
            opt_icon_row_layout(S(186), S(30), 2, &pv2, &fld2, bb2);
            ST_CHECK(fld2.w > 0 && bb2[0].x + bb2[0].w <= bb2[1].x + 0.5f && bb2[1].x + bb2[1].w <= S(186) + 0.5f);
            ST_CHECK(opt_icon_row_ctlw(S(760), 3) > 0 && opt_icon_row_ctlw(S(760), 2) > 0);
            st_shot("options-icon-modes");
        }
        char svg[PATH_MAX], dh[PATH_MAX], dest[PATH_MAX + 64];
        snprintf(svg, sizeof svg, M9_BASE "/app-icon.svg");
        st_write_text(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"32\" height=\"32\"><rect width=\"32\" height=\"32\" fill=\"#ff0000\"/></svg>");
        ST_CHECK(g_app_icon_mode == APP_ICON_DEFAULT);
        ST_CHECK(app_icon_mode_parse("custom") == APP_ICON_CUSTOM);
        ST_CHECK(app_icon_mode_parse("file_icon") == APP_ICON_FILE_ICON);
        ST_CHECK(app_icon_mode_parse("system") == APP_ICON_FILE_ICON); // legacy local config
        ST_CHECK(app_icon_mode_parse("default") == APP_ICON_DEFAULT);
        ST_CHECK(app_icon_mode_parse("nonsense") == APP_ICON_DEFAULT);
        ST_CHECK(!strcmp(app_icon_mode_name(APP_ICON_CUSTOM), "custom"));
        ST_CHECK(!strcmp(app_icon_mode_name(APP_ICON_FILE_ICON), "file_icon"));
        ST_CHECK(!strcmp(app_icon_mode_name(APP_ICON_DEFAULT), "default"));
        ST_CHECK(app_icon_path_ok(NULL) && app_icon_path_ok(""));
        ST_CHECK(app_icon_path_ok(svg) && !app_icon_path_ok("relative.svg"));
        ST_CHECK(!app_icon_path_ok(M9_BASE "/app-icon.txt"));
        ST_CHECK(!app_icon_path_ok(M9_BASE "/app\n-icon.svg"));
        ST_CHECK(!app_icon_raster(16)); // no custom artwork selected yet
        u8 *def_prev = app_icon_preview_raster(16); // the row previews the icon in effect: the built-in default
        ST_CHECK(def_prev != NULL);
        u8 *def_win = app_icon_window_raster(16);
        ST_CHECK(def_win && !memcmp(def_win, def_prev, 16 * 16 * 4));
        free(def_win);
        opt_app_icon_chosen(svg);
        ST_CHECK(!strcmp(g_app_icon_path, svg));
        u8 *px = app_icon_raster(16);
        ST_CHECK(px != NULL);
        u8 *custom_prev = app_icon_preview_raster(16);
        ST_CHECK(custom_prev != NULL && memcmp(def_prev, custom_prev, 16 * 16 * 4) != 0);
        u8 *custom_win = app_icon_window_raster(16);
        ST_CHECK(custom_win && !memcmp(custom_win, custom_prev, 16 * 16 * 4));
        free(custom_win);
        free(px);
        { // File-Icon mode previews the folder icon of the current File icons set, never the app logo
            u8 *folder = icon_raster(icon_find("folder"), 16, 16), *direct = file_icon_app_raster(16);
            AppIconMode saved_mode = g_app_icon_mode;
            g_app_icon_mode = APP_ICON_FILE_ICON;
            u8 *fi_prev = app_icon_preview_raster(16), *fi_win = app_icon_window_raster(16);
            g_app_icon_mode = saved_mode;
            ST_CHECK(icon_find("folder") >= 0 && folder && direct && fi_prev && fi_win);
            ST_CHECK(direct && folder && !memcmp(direct, folder, 16 * 16 * 4));
            ST_CHECK(fi_prev && folder && !memcmp(fi_prev, folder, 16 * 16 * 4));
            ST_CHECK(fi_win && folder && !memcmp(fi_win, folder, 16 * 16 * 4));
            ST_CHECK(fi_prev && def_prev && memcmp(fi_prev, def_prev, 16 * 16 * 4));
            ST_CHECK(fi_prev && custom_prev && memcmp(fi_prev, custom_prev, 16 * 16 * 4));
            free(folder);
            free(direct);
            free(fi_prev);
            free(fi_win);
        }
        { // the real buttons switch modes; Default erases the path and clears the field to its placeholder
            st_opt_click(st_opt_hit(HP_BUTTON, OPT_APP_ICON, 1));
            ST_CHECK(g_app_icon_mode == APP_ICON_FILE_ICON && !strcmp(g_app_icon_path, svg));
            st_opt_click(st_opt_hit(HP_BUTTON, OPT_APP_ICON, 2));
            ST_CHECK(g_app_icon_mode == APP_ICON_DEFAULT && !g_app_icon_path[0] && !g_opt.app_icon.buf[0]);
            u8 *after = app_icon_preview_raster(16), *logo = icon_raster(ICON_LOGO, 16, 16);
            ST_CHECK(after && logo && !memcmp(after, logo, 16 * 16 * 4));
            free(after);
            free(logo);
            te_set(&g_opt.app_icon, "", 0, 0);
            ST_CHECK(!st_opt_hit(HP_BUTTON, OPT_APP_ICON, 0)); // Apply is disabled until a path is typed
            te_set(&g_opt.app_icon, svg, (int)strlen(svg), (int)strlen(svg));
            st_frames(1);
            st_opt_click(st_opt_hit(HP_BUTTON, OPT_APP_ICON, 0));
            ST_CHECK(g_app_icon_mode == APP_ICON_CUSTOM && !strcmp(g_app_icon_path, svg));
        }
        { // File-Icon mode follows the active File icons theme; the app logo is never a fallback
            char themes[PATH_MAX], p[PATH_MAX];
            icon_themes_dir(themes, sizeof themes);
            snprintf(p, sizeof p, "%s/M9FileIcons", themes);
            mkdir_p(p, 0755);
            snprintf(p, sizeof p, "%s/M9FileIcons/theme.conf", themes);
            st_write_text(p, "base=Symbols\n");
            snprintf(p, sizeof p, "%s/M9FileIcons/folder.svg", themes);
            st_write_text(p, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\"><rect width=\"16\" height=\"16\" fill=\"#ff00ff\"/></svg>");
            icon_themes_reload();
            int t = icon_theme_find("M9FileIcons");
            ST_CHECK(t >= ICON_THEMES);
            int saved_theme = g_icon_theme;
            AppIconMode saved_mode = g_app_icon_mode;
            g_app_icon_mode = APP_ICON_FILE_ICON;
            u8 *before = app_icon_preview_raster(16);
            opt_choice_set(OPT_ICON_THEME, t);
            ST_CHECK(g_icon_theme == t);
            u8 *folder = icon_raster(icon_find("folder"), 16, 16);
            u8 *after = app_icon_preview_raster(16), *after_win = app_icon_window_raster(16), *direct = file_icon_app_raster(16);
            ST_CHECK(folder && after && after_win && direct);
            ST_CHECK(before && memcmp(before, after, 16 * 16 * 4));
            ST_CHECK(folder && after && !memcmp(after, folder, 16 * 16 * 4));
            ST_CHECK(folder && after_win && !memcmp(after_win, folder, 16 * 16 * 4));
            ST_CHECK(folder && direct && !memcmp(direct, folder, 16 * 16 * 4));
            opt_choice_set(OPT_ICON_THEME, saved_theme);
            g_app_icon_mode = saved_mode;
            { // a late folder arrival (the desktop's icon, a reload) marks the launcher override stale
                g_app_icon_theme_dirty = false;
                g_app_icon_mode = APP_ICON_FILE_ICON;
                icons_replace(icon_find("folder"), 16, after);
                ST_CHECK(g_app_icon_theme_dirty);
                g_app_icon_theme_dirty = false; // app_icon_sync() consumes it outside tests
            }
            free(before);
            free(folder);
            free(after);
            free(after_win);
            free(direct);
        }
        free(custom_prev);
        free(def_prev);
        { // Apply from the path field: invalid paths are rejected; an empty field leaves the current choice alone
            te_set(&g_opt.app_icon, svg, (int)strlen(svg), (int)strlen(svg));
            opt_app_icon_apply();
            ST_CHECK(!strcmp(g_app_icon_path, svg));
            te_set(&g_opt.app_icon, "relative.svg", 12, 12);
            opt_app_icon_apply();
            ST_CHECK(!strcmp(g_app_icon_path, svg));
            te_set(&g_opt.app_icon, "", 0, 0);
            opt_app_icon_apply();
            ST_CHECK(!strcmp(g_app_icon_path, svg) && g_app_icon_mode == APP_ICON_CUSTOM);
        }
        { // the mode persists, the typed path survives it, and a legacy path-only config means custom
            opt_app_icon_chosen(svg);
            g_app_icon_mode = APP_ICON_FILE_ICON;
            settings_save();
            g_app_icon_mode = APP_ICON_DEFAULT;
            settings_load();
            ST_CHECK(g_app_icon_mode == APP_ICON_FILE_ICON && !strcmp(g_app_icon_path, svg));
            char cfg[PATH_MAX], backup[262144] = "", legacy[PATH_MAX + 32];
            settings_file(cfg, sizeof cfg, false);
            FILE *f = fopen(cfg, "rb");
            ST_CHECK(f != NULL);
            if (f) {
                size_t n = fread(backup, 1, sizeof backup - 1, f);
                backup[n] = 0;
                fclose(f);
            }
            snprintf(legacy, sizeof legacy, "app_icon=%s\n", svg);
            st_write_text(cfg, legacy);
            g_app_icon_path[0] = 0;
            g_app_icon_mode = APP_ICON_DEFAULT;
            settings_load();
            ST_CHECK(g_app_icon_mode == APP_ICON_CUSTOM && !strcmp(g_app_icon_path, svg));
            snprintf(legacy, sizeof legacy, "app_icon_mode=system\napp_icon=%s\n", svg);
            st_write_text(cfg, legacy);
            g_app_icon_path[0] = 0;
            g_app_icon_mode = APP_ICON_DEFAULT;
            settings_load();
            ST_CHECK(g_app_icon_mode == APP_ICON_FILE_ICON && !strcmp(g_app_icon_path, svg));
            st_write_text(cfg, backup);
            settings_load();
            g_app_icon_mode = APP_ICON_DEFAULT;
        }
        { // title bar icon: its own path setting, preview and Apply, independent of the app icon
            char ti[PATH_MAX];
            snprintf(ti, sizeof ti, M9_BASE "/title-icon.svg");
            st_write_text(ti, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\"><rect width=\"16\" height=\"16\" fill=\"#00ff00\"/></svg>");
            u8 *def_title = title_icon_preview_raster(16), *small_title = icon_raster(ICON_LOGO_SMALL, 16, 16);
            ST_CHECK(def_title && small_title && !g_title_icon_path[0]);
            ST_CHECK(!memcmp(def_title, small_title, 16 * 16 * 4));
            free(small_title);
            te_set(&g_opt.title_icon, ti, (int)strlen(ti), (int)strlen(ti));
            opt_title_icon_apply();
            ST_CHECK(!strcmp(g_title_icon_path, ti));
            Glyph *h1 = title_icon_header_glyph(23), *p1 = title_icon_preview_glyph(30);
            Glyph *h2 = title_icon_header_glyph(23), *p2 = title_icon_preview_glyph(30);
            ST_CHECK(h1 && p1 && h1 == h2 && p1 == p2 && h1 != p1);
            u8 *custom_title = title_icon_preview_raster(16);
            ST_CHECK(custom_title && memcmp(def_title, custom_title, 16 * 16 * 4) != 0);
            free(custom_title);
            free(def_title);
            te_set(&g_opt.title_icon, "relative.svg", 12, 12);
            opt_title_icon_apply();
            ST_CHECK(!strcmp(g_title_icon_path, ti));
            st_opt_click(st_opt_hit(HP_BUTTON, OPT_TITLE_ICON, 1));
            ST_CHECK(!g_title_icon_path[0] && !g_opt.title_icon.buf[0]);
            te_set(&g_opt.title_icon, ti, (int)strlen(ti), (int)strlen(ti));
            opt_title_icon_apply();
            ST_CHECK(!strcmp(g_title_icon_path, ti));
            settings_save();
            settings_load();
            ST_CHECK(!strcmp(g_title_icon_path, ti));
            options_open();
            g_opt.cat = OC_APPEARANCE;
            g_opt.scroll = 0;
            st_frames(2);
            ST_CHECK(st_opt_hit(HP_TEXT, OPT_TITLE_ICON, 3) != NULL);
            ST_CHECK(title_icon_header_glyph(24) && title_icon_preview_glyph(30));
            sheet_close();
            st_frames(5);
            ST_CHECK(st_brand_icon_green()); // the custom artwork is drawn in the header, not just previewed
            bool logo = A.brand_logo;
            if (logo) opt_bool_toggle(OPT_BRAND_LOGO);
            st_frames(5);
            ST_CHECK(!A.brand_logo && !st_brand_icon_green()); // Logo off hides the custom icon too
            st_shot("options-title-icon-off");
            if (logo) opt_bool_toggle(OPT_BRAND_LOGO);
            st_frames(5);
            ST_CHECK(A.brand_logo && st_brand_icon_green());
            { // title Default clears the path, restores the small logo and removes the custom header pixels
                options_open();
                g_opt.cat = OC_APPEARANCE;
                g_opt.scroll = 0;
                st_frames(2);
                st_opt_click(st_opt_hit(HP_BUTTON, OPT_TITLE_ICON, 1));
                ST_CHECK(!g_title_icon_path[0] && !g_opt.title_icon.buf[0]);
                u8 *after = title_icon_preview_raster(16), *small = icon_raster(ICON_LOGO_SMALL, 16, 16);
                ST_CHECK(after && small && !memcmp(after, small, 16 * 16 * 4));
                free(after);
                free(small);
                sheet_close();
                st_frames(5);
                ST_CHECK(A.brand_logo && !st_brand_icon_green());
            }
        }
        data_home(dh, sizeof dh);
        snprintf(dest, sizeof dest, "%s/icons/hicolor/scalable/apps/dev.fileexplorer.svg", dh);
        ST_CHECK(app_icon_write_ex(dh, 0x123456, svg, false));
        FILE *df = fopen(dest, "rb");
        ST_CHECK(df != NULL);
        if (df) {
            char body[8192] = {0};
            size_t n = fread(body, 1, sizeof body - 1, df);
            fclose(df);
            ST_CHECK(n && strstr(body, "#ff0000"));
        }
        ST_CHECK(!app_icon_write_ex(dh, 0x123456, svg, false)); // already installed: nothing changed
        g_app_icon_mode = APP_ICON_FILE_ICON;
        ST_CHECK(app_icon_write_ex(dh, 0x123456, NULL, false)); // File-Icon installs the active folder icon
        ST_CHECK(access(dest, F_OK) != 0);
        char png_dest[PATH_MAX];
        snprintf(png_dest, sizeof png_dest, "%s/icons/hicolor/256x256/apps/dev.fileexplorer.png", dh);
        ST_CHECK(access(png_dest, F_OK) == 0);
        u8 *file_icon = file_icon_app_raster(256);
        ST_CHECK(file_icon != NULL);
        free(file_icon);
        g_app_icon_mode = APP_ICON_DEFAULT;
        opt_app_icon_chosen("");
        ST_CHECK(!g_app_icon_path[0] && !app_icon_raster(16));
        ST_CHECK(app_icon_write(dh, 0x123456)); // Default restores the recoloured built-in artwork
        df = fopen(dest, "rb");
        ST_CHECK(df != NULL);
        if (df) {
            char body[8192] = {0};
            size_t n = fread(body, 1, sizeof body - 1, df);
            fclose(df);
            ST_CHECK(n && strstr(body, "<svg"));
        }
        settings_save();
        opt_app_icon_chosen(svg);
        settings_save();
        opt_app_icon_chosen("");
        settings_load();
        init_places();
        ST_CHECK(!strcmp(g_app_icon_path, svg));
        opt_app_icon_chosen("");
        settings_save();
    }
    { // File Icon -> Default: the launcher keeps an icon (the base accent makes no copy of its own)
        char dh[PATH_MAX], dh2[PATH_MAX], svg2[PATH_MAX + 48], png2[PATH_MAX + 48];
        data_home(dh, sizeof dh);
        snprintf(dh2, sizeof dh2, "%s/appicon2", dh);
        snprintf(svg2, sizeof svg2, "%s/icons/hicolor/scalable/apps/dev.fileexplorer.svg", dh2);
        snprintf(png2, sizeof png2, "%s/icons/hicolor/256x256/apps/dev.fileexplorer.png", dh2);
        g_app_icon_mode = APP_ICON_FILE_ICON;
        ST_CHECK(app_icon_write_ex(dh2, LOGO_BASE_ACCENT, NULL, false)); // the folder icon as the launcher icon
        ST_CHECK(access(png2, F_OK) == 0 && access(svg2, F_OK) != 0);
        g_app_icon_mode = APP_ICON_DEFAULT;
        ST_CHECK(app_icon_write(dh2, LOGO_BASE_ACCENT)); // back to the app's own icon
        ST_CHECK(access(png2, F_OK) != 0 && access(svg2, F_OK) == 0); // the override is there: not gone from the launcher
    }
    { // scrollbar colour: Auto (the theme's own; System: the desktop's accent), or #RRGGBB / #RGB typed and applied
        int saved = g_theme_cur;
        g_theme_cur = theme_find("Dark");
        theme_apply();
        ST_CHECK(!g_scroll_custom && T.scroll == g_theme_dark.scroll);
        options_open();
        g_opt.cat = OC_APPEARANCE;
        st_frames(1);
        st_opt_click(st_opt_hit(HP_TEXT, OPT_SCROLL_COLOR, 1));
        ST_CHECK(g_opt.scroll_focus);
        st_type("#12345"); // malformed: nothing changes
        st_key(SDLK_RETURN, SDL_KMOD_NONE);
        ST_CHECK(!g_scroll_custom && g_opt.scroll_focus && options_is_open());
        te_set(&g_opt.scroll_color, "", 0, 0);
        st_type("#f80");
        st_frames(1);
        st_shot("m9-scrollbar-field");
        ST_CHECK(st_opt_hit(HP_EDIT_APPLY, OPT_SCROLL_COLOR, 0) && st_opt_hit(HP_EDIT_RESET, OPT_SCROLL_COLOR, 0));
        st_opt_click(st_opt_hit(HP_EDIT_APPLY, OPT_SCROLL_COLOR, 0)); // Apply
        ST_CHECK(g_scroll_custom == (0x01000000u | 0xFF8800) && T.scroll == 0xFF8800 && !strcmp(g_opt.scroll_color.buf, "#FF8800"));
        st_opt_click(st_opt_hit(HP_EDIT_RESET, OPT_SCROLL_COLOR, 0)); // Default: back to the theme's own colour
        ST_CHECK(!g_scroll_custom && !g_opt.scroll_color.buf[0] && T.scroll == g_theme_dark.scroll);
        te_set(&g_opt.scroll_color, "#f80", 3, 3);
        st_frames(1);
        st_opt_click(st_opt_hit(HP_EDIT_APPLY, OPT_SCROLL_COLOR, 0));
        ST_CHECK(g_scroll_custom == (0x01000000u | 0xFF8800));
        settings_save();
        settings_load();
        theme_apply();
        ST_CHECK(g_scroll_custom == (0x01000000u | 0xFF8800) && T.scroll == 0xFF8800);
        u32 c;
        ST_CHECK(scroll_color_parse("auto", &c) && !c && scroll_color_parse("#000000", &c) && c == 0x01000000u);
        ST_CHECK(!scroll_color_parse("#12", &c) && !scroll_color_parse("red", &c) && !scroll_color_parse("#1234567", &c));
        g_desktop.accent = 0x01000000u | 0x3584E4; // Auto: only System follows the desktop's accent
        g_scroll_custom = 0;
        theme_apply();
        ST_CHECK(T.scroll == g_theme_dark.scroll);
        g_theme_cur = theme_find("System");
        theme_apply();
        ST_CHECK(T.scroll == rgb_mix(T.bg, 0x3584E4, 0.55f));
        g_desktop.accent = 0x01000000u | 0x000000; // a black accent on Dark: kept visible
        theme_apply();
        ST_CHECK(fabsf(rgb_luma(T.scroll) - rgb_luma(T.bg)) >= 0.10f);
        memset(&g_desktop, 0, sizeof g_desktop);
        te_set(&g_opt.scroll_color, "", 0, 0);
        opt_button(OPT_SCROLL_COLOR); // empty: Auto again
        ST_CHECK(!g_scroll_custom);
        sheet_close();
        g_theme_cur = saved;
        theme_apply();
    }

    // font family switch (if a second family is installed)
    char *dv = font_family_file("DejaVu Sans");
    if (dv) {
        ST_CHECK(apply_ui_font("DejaVu Sans") && !strcmp(A.ui_font, "DejaVu Sans"));
        st_frames(2);
        ST_CHECK(apply_ui_font("") && !A.ui_font[0]);
        free(dv);
    }
    ST_CHECK(!font_family_file("No Such Font Family 123"));

    // --- shortcuts ---
    {
        Chord c;
        KeySeq q;
        char buf[96];
        ST_CHECK(chord_parse("Ctrl+Shift+T", &c) && c.key == SDLK_T && c.mod == (KM_CTRL | KM_SHIFT));
        chord_format(c, buf, sizeof buf);
        ST_CHECK(!strcmp(buf, "Ctrl+Shift+T"));
        ST_CHECK(chord_parse("Ctrl++", &c) && c.key == SDLK_PLUS && c.mod == KM_CTRL);
        ST_CHECK(chord_parse("Ctrl+Keypad +", &c) && c.key == SDLK_KP_PLUS);
        ST_CHECK(chord_parse("alt+enter", &c) && c.key == SDLK_RETURN && c.mod == KM_ALT);
        chord_format(c, buf, sizeof buf);
        ST_CHECK(!strcmp(buf, "Alt+Enter"));
        ST_CHECK(seq_parse("Ctrl+K Ctrl+T", &q) && q.n == 2 && q.c[1].key == SDLK_T);
        ST_CHECK(seq_parse("Ctrl+Keypad +", &q) && q.n == 1);
        ST_CHECK(!chord_parse("Hyper+X", &c));
        ST_CHECK(chord_parse("Ctrl+\\", &c) && c.key == SDLK_BACKSLASH);
    }
    int nt = A.ntabs;
    st_key(SDLK_T, SDL_KMOD_CTRL);
    ST_CHECK(A.ntabs == nt + 1);
    st_key(SDLK_W, SDL_KMOD_CTRL);
    ST_CHECK(A.ntabs == nt);
    p = active_pane();
    // rebind New tab to Ctrl+Y: Ctrl+T stops working
    KeyDef *kd = keydef_find(CMD_NEW_TAB);
    KeySeq sq;
    seq_parse("Ctrl+Y", &sq);
    ST_CHECK(keys_assign(kd, 0, &sq) == -1);
    st_key(SDLK_T, SDL_KMOD_CTRL);
    ST_CHECK(A.ntabs == nt);
    st_key(SDLK_Y, SDL_KMOD_CTRL);
    ST_CHECK(A.ntabs == nt + 1);
    st_key(SDLK_W, SDL_KMOD_CTRL);
    p = active_pane();
    ST_CHECK(!strcmp(keymap_label(CMD_NEW_TAB), "Ctrl+Y"));
    // taking a key that another command uses removes it there
    seq_parse("Ctrl+C", &sq);
    ST_CHECK(keys_assign(kd, -1, &sq) == ACT_COPY);
    ST_CHECK(keydef_find(ACT_COPY)->nseq == 0 && !strcmp(keymap_label(ACT_COPY), ""));
    // menus show the live binding
    menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), false);
    bool found_label = false;
    for (int i = 0; i < A.menu.n; i++)
        if (A.menu.items[i].act == ACT_SELECT_ALL) found_label = !strcmp(A.menu.items[i].shortcut, "Ctrl+A");
    ST_CHECK(found_label);
    A.menu.open = false;
    // two-step sequence
    seq_parse("Ctrl+K H", &sq);
    keys_assign(keydef_find(ACT_HIDDEN), -1, &sq);
    bool hid0 = A.show_hidden;
    st_key(SDLK_K, SDL_KMOD_CTRL);
    ST_CHECK(A.show_hidden == hid0 && g_key_prefix.key == SDLK_K);
    st_key(SDLK_H, SDL_KMOD_NONE);
    ST_CHECK(A.show_hidden == !hid0 && !g_key_prefix.key);
    do_action(p, ACT_HIDDEN);
    // persistence: only changed commands are written
    settings_save();
    keys_defaults();
    ST_CHECK(!strcmp(keymap_label(CMD_NEW_TAB), "Ctrl+T"));
    settings_load();
    ST_CHECK(keydef_find(CMD_NEW_TAB)->nseq == 2 && !strcmp(keymap_label(CMD_NEW_TAB), "Ctrl+Y"));
    ST_CHECK(keydef_find(ACT_COPY)->nseq == 0);
    ST_CHECK(keydef_find(ACT_HIDDEN)->nseq == 2 && keydef_find(ACT_HIDDEN)->seq[1].n == 2);
    ST_CHECK(keydef_is_default(keydef_find(ACT_PASTE)));
    keys_defaults();
    // in the path bar only global shortcuts fire: Ctrl+A selects text, Ctrl+T still opens a tab
    begin_edit(p, NULL);
    u32 sel0 = p->sel_count;
    st_key(SDLK_A, SDL_KMOD_CTRL);
    ST_CHECK(A.edit_pane == p && p->sel_count == sel0);
    st_key(SDLK_T, SDL_KMOD_CTRL);
    ST_CHECK(A.ntabs == nt + 1);
    st_key(SDLK_W, SDL_KMOD_CTRL);
    A.edit_pane = NULL;
    p = active_pane();
    // typing a path: a click places the caret (the path bar stays open), double-click picks a folder name,
    // triple-click the whole path
    begin_edit(p, NULL);
    st_frames(1);
    {
        int n = (int)strlen(A.edit.buf), last = (int)(strrchr(A.edit.buf, '/') - A.edit.buf);
        st_text_click(&A.edit, last + 2, 1, 0);
        ST_CHECK(A.edit_pane == p && A.edit.anchor == last + 2 && A.edit.caret == last + 2);
        st_text_click(&A.edit, last + 2, 2, 0);
        ST_CHECK(A.edit_pane == p && A.edit.anchor > last && A.edit.anchor < A.edit.caret && A.edit.caret <= n);
        st_text_click(&A.edit, 1, 3, 0);
        ST_CHECK(A.edit.anchor == 0 && A.edit.caret == n);
    }
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    ST_CHECK(!A.edit_pane);

    // Options > Keyboard: record a new shortcut with the recorder
    options_open();
    st_frames(1);
    st_opt_click(st_opt_hit(HP_NAV, -1, OC_KEYBOARD));
    int di = (int)(keydef_find(CMD_NEW_TAB) - g_keys);
    st_opt_click(st_opt_hit(HP_KEYADD, di, 0));
    ST_CHECK(g_opt.rec_def == di && g_opt.rec_slot == -1);
    st_key(SDLK_J, SDL_KMOD_CTRL | SDL_KMOD_ALT);
    st_frames(1);
    st_shot("options-keys-recording");
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    ST_CHECK(g_opt.rec_def < 0 && keydef_find(CMD_NEW_TAB)->nseq == 2);
    ST_CHECK(chord_eq(keydef_find(CMD_NEW_TAB)->seq[1].c[0], (Chord){SDLK_J, KM_CTRL | KM_ALT}));
    st_frames(2);
    st_shot("options-keys");
    // reset that command
    st_opt_click(st_opt_hit(HP_KEYRESET, di, 0));
    ST_CHECK(keydef_is_default(keydef_find(CMD_NEW_TAB)));
    // Esc while recording cancels, it doesn't close the window
    st_opt_click(st_opt_hit(HP_KEYCHIP, di, 0));
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    ST_CHECK(g_sheet.kind == SHEET_OPTIONS && g_opt.rec_def < 0 && keydef_is_default(keydef_find(CMD_NEW_TAB)));
    // search finds shortcuts by key
    st_type("ctrl+shift+n");
    st_frames(1);
    ST_CHECK(st_opt_hit(HP_KEYCHIP, (int)(keydef_find(ACT_NEW_FOLDER) - g_keys), 0) != NULL);
    sheet_close();

    // --- mouse options ---
    st_wait();
    p = active_pane();
    A.single_click = true;
    f32 ry = st_row_y(p, "sub");
    ST_CHECK(ry > 0);
    f32 rx = p->ly.col_name + S(30);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, rx, ry, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, rx, ry, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(!strcmp(active_pane()->path, M9_BASE "/sub"));
    A.single_click = false;
    // double-click on empty space goes up
    p = active_pane();
    f32 ey = p->ly.list.y + p->ly.list.h - S(20);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, rx, ey, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, rx, ey, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, rx, ey, SDL_BUTTON_LEFT, 2, SDL_KMOD_NONE);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, rx, ey, SDL_BUTTON_LEFT, 2, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(!strcmp(active_pane()->path, M9_BASE));
    // single click (default mode) only selects
    p = active_pane();
    ry = st_row_y(p, "sub");
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, rx, ry, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, rx, ry, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(!strcmp(active_pane()->path, M9_BASE));

    // --- the external terminal (Options > Integration): one executable field with Apply/Default ---
    ST_CHECK(!g_external_terminal[0]);
    A.menu.n = 0;
    menu_terminal_items(p);
    ST_CHECK(A.menu.n > 0 && !strcmp(A.menu.items[A.menu.n - 1].label, "Open in external terminal"));
    do_action(p, ACT_TERMINAL_EXTERNAL); // (recorded, not started)
    ST_CHECK(!strncmp(A.last_opened, "terminal:", 9) && !options_is_open());
    st_write_text(M9_BASE "-bin/foot", "#!/bin/sh\n");
    chmod(M9_BASE "-bin/foot", 0755);
    char *tpath0 = getenv("PATH") ? strdup(getenv("PATH")) : NULL;
    setenv("PATH", M9_BASE "-bin", 1); // (only the stand-ins)
    options_open_page(OC_INTEGRATION);
    st_frames(1);
    g_opt.scroll = g_opt.max_scroll;
    st_frames(1);
    ST_CHECK(st_opt_hit(HP_TEXT, OPT_TERMINAL_PATH, 0));
    ST_CHECK(st_opt_hit(HP_EDIT_APPLY, OPT_TERMINAL_PATH, 0) && st_opt_hit(HP_EDIT_RESET, OPT_TERMINAL_PATH, 0));
    te_set(&g_opt.terminal_path, "foot", 4, 4);
    st_opt_click(st_opt_hit(HP_EDIT_APPLY, OPT_TERMINAL_PATH, 0));
    ST_CHECK(!strcmp(g_external_terminal, "foot"));
    te_set(&g_opt.terminal_path, "/usr/bin/foot", 13, 13);
    st_opt_click(st_opt_hit(HP_EDIT_RESET, OPT_TERMINAL_PATH, 0));
    ST_CHECK(!g_external_terminal[0]);
    A.menu.n = 0;
    menu_terminal_items(p);
    ST_CHECK(A.menu.n > 0 && !strcmp(A.menu.items[A.menu.n - 1].label, "Open in external terminal"));
    sheet_close();
    if (tpath0) setenv("PATH", tpath0, 1);
    free(tpath0);

    // --- terminal executable validation ---
    {
        char too_long[sizeof g_external_terminal + 1];
        memset(too_long, 'a', sizeof too_long - 1);
        too_long[sizeof too_long - 1] = 0;
        ST_CHECK(opt_terminal_path_valid("foot") && !opt_terminal_path_valid("foot\n") &&
                 !opt_terminal_path_valid("foot\r") && !opt_terminal_path_valid(too_long));
        char *tpath1 = getenv("PATH") ? strdup(getenv("PATH")) : NULL;
        setenv("PATH", M9_BASE "-bin", 1);
        options_open_page(OC_INTEGRATION);
        st_frames(1);
        g_opt.scroll = g_opt.max_scroll;
        st_frames(1);
        te_set(&g_opt.terminal_path, "no-such-terminal-xyz", 20, 20);
        A.toast[0] = 0;
        st_opt_click(st_opt_hit(HP_EDIT_APPLY, OPT_TERMINAL_PATH, 0));
        ST_CHECK(!g_external_terminal[0] && strstr(A.toast, "Didn't apply"));
        sheet_close();
        if (tpath1) setenv("PATH", tpath1, 1);
        else unsetenv("PATH");
        free(tpath1);
    }

    // --- named layouts ---
    st_wait();
    st_key(SDLK_BACKSLASH, SDL_KMOD_CTRL);
    st_wait();
    ST_CHECK(A.npanels == 2);
    ST_CHECK(layout_save_named("  Two [panels]  ") && layout_find("Two  panels") >= 0);
    int li = layout_find("Two  panels");
    ST_CHECK(li >= 0 && g_layouts[li].npanels == 2 && g_layouts[li].ntabs == 2);
    run_command(CMD_CLOSE_PANEL);
    ST_CHECK(A.npanels == 1);
    ST_CHECK(layout_open(li));
    st_wait();
    ST_CHECK(A.npanels == 2 && A.ntabs == 2 && A.root && A.root->kind == NODE_HSPLIT);
    layouts_load(); // persisted to layouts.conf
    ST_CHECK(g_nlayouts == 1 && layout_find("Two  panels") == 0);
    // Options > Layouts: type a name, Enter saves
    run_command(CMD_SAVE_LAYOUT);
    ST_CHECK(g_sheet.kind == SHEET_OPTIONS && g_opt.cat == OC_LAYOUTS && g_opt.lname_focus);
    st_type("Solo");
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    ST_CHECK(g_nlayouts == 2 && layout_find("Solo") >= 0 && !g_opt.lname.buf[0]);
    // a click moves the keyboard between the search field and the layout name
    st_text_click(&g_opt.search, 0, 1, 0);
    ST_CHECK(g_sheet.kind == SHEET_OPTIONS && !g_opt.lname_focus);
    st_text_click(&g_opt.lname, 0, 1, 0);
    ST_CHECK(g_sheet.kind == SHEET_OPTIONS && g_opt.lname_focus);
    st_frames(2);
    st_shot("options-layouts");
    // delete asks first
    st_opt_click(st_opt_hit(HP_LDELETE, -1, layout_find("Solo")));
    ST_CHECK(A.dlg.open);
    dialog_choose(1);
    ST_CHECK(g_nlayouts == 1 && layout_find("Solo") < 0);
    sheet_close();
    // the palette offers saved layouts
    pal_open(true);
    st_type("two pan");
    st_wait_pal();
    ST_CHECK(A.pal_n > 0 && g_pal_items[0].kind == PI_CMD && g_pal_items[0].cmd == CMD_LAYOUT_BASE + 0);
    st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    run_command(CMD_CLOSE_PANEL);
    p = active_pane();

    // --- Quick Access groups ---
    ST_CHECK(A.nqa == 1 && !strcmp(A.qa[0].name, "Quick access"));
    int trash_row = -1;
    for (int i = 0; i < A.nplaces; i++)
        if (A.places[i].kind == PK_TRASH) trash_row = i;
    ST_CHECK(trash_row > 0 && A.places[0].kind == PK_GROUP);
    int g1 = qa_add_group("Work");
    char *pp[2] = {M9_BASE, M9_BASE "/file00.c"};
    qa_pin_paths(pp, 2, g1); // the file is skipped
    ST_CHECK(A.qa[g1].n == 1 && !strcmp(A.qa[g1].items[0].path, M9_BASE));
    qa_pin_paths((char *[]){M9_BASE "/sub"}, 1, g1);
    ST_CHECK(A.qa[g1].n == 2);
    // rename an item inline
    qa_edit_begin(g1, 0);
    st_key(SDLK_A, SDL_KMOD_CTRL);
    st_type("Test root");
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    ST_CHECK(A.qa_edit_g < 0 && !strcmp(A.qa[g1].items[0].label, "Test root"));
    // drag "sub" above "Test root"
    int r_root = -1, r_sub = -1, r_hdr = -1;
    for (int i = 0; i < A.nplaces; i++) {
        if (A.places[i].group != g1) continue;
        if (A.places[i].kind == PK_GROUP) r_hdr = i;
        else if (A.places[i].item == 0) r_root = i;
        else if (A.places[i].item == 1) r_sub = i;
    }
    ST_CHECK(r_root > 0 && r_sub > r_root && r_hdr >= 0);
    {
        Rect a0 = place_rect(r_sub), b0 = place_rect(r_root);
        f32 x0 = a0.x + S(40);
        A.side_scroll = A.side_scroll_target = 0;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, x0, a0.y + a0.h * 0.5f, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        st_mouse(SDL_EVENT_MOUSE_MOTION, x0, b0.y + S(4), 0, 0, SDL_KMOD_NONE);
        ST_CHECK(A.drag == DRAG_PLACE && A.qa_ins_g == g1 && A.qa_ins_i == 0);
        st_shot("sidebar-drag");
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, x0, b0.y + S(4), SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        ST_CHECK(!strcmp(A.qa[g1].items[0].path, M9_BASE "/sub") && !strcmp(A.qa[g1].items[1].label, "Test root"));
    }
    st_frames(2);
    st_shot("sidebar-groups");
    // click a group header to collapse it
    {
        int n0 = A.nplaces;
        Rect hr = place_rect(r_hdr);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, hr.x + S(40), hr.y + hr.h * 0.5f, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, hr.x + S(40), hr.y + hr.h * 0.5f, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        ST_CHECK(A.qa[g1].collapsed && A.nplaces == n0 - 2);
        A.qa[g1].collapsed = false;
        init_places();
    }
    // move to another group from the menu
    for (int i = 0; i < A.nplaces; i++)
        if (A.places[i].group == g1 && A.places[i].item == 0) { A.menu_place = i; break; }
    qa_action(ACT_QA_MOVE_BASE + 0);
    ST_CHECK(A.qa[g1].n == 1 && qa_find(M9_BASE "/sub", &g1, NULL) && g1 == 0);
    g1 = 1;
    // persistence
    settings_save();
    qa_free();
    settings_load();
    init_places();
    ST_CHECK(A.nqa == 2 && !strcmp(A.qa[1].name, "Work") && A.qa[1].n == 1 && !strcmp(A.qa[1].items[0].label, "Test root"));
    ST_CHECK(qa_find(M9_BASE "/sub", NULL, NULL));
    // deleting a group keeps its folders
    for (int i = 0; i < A.nplaces; i++)
        if (A.places[i].kind == PK_GROUP && A.places[i].group == 1) { A.menu_place = i; break; }
    qa_action(ACT_QA_GROUP_DELETE);
    ST_CHECK(A.nqa == 1 && qa_find(M9_BASE, NULL, NULL));
    // dropping files on a group header pins folders
    {
        int hr0 = 0;
        Rect hr = place_rect(hr0);
        drop_target(NULL, hr.x + S(30), hr.y + hr.h * 0.5f);
        ST_CHECK(A.drop_qa == 0 && !A.drop_dir[0]);
        drag_cancel();
    }

    // --- tree view ---
    p = active_pane();
    pane_navigate(p, M9_BASE, true);
    st_wait();
    run_command(CMD_TREE_VIEW);
    st_wait();
    ST_CHECK(p->tree && A.def_tree && p->L->tree);
    ST_CHECK(st_select(p, "tree"));
    i64 t0 = p->cursor;
    st_key(SDLK_RIGHT, SDL_KMOD_NONE); // expand
    st_wait();
    ST_CHECK(p->nexpanded == 1 && !strcmp(p->expanded[0], "tree"));
    ST_CHECK(p->cursor == t0 && !strcmp(st_cursor_name(p), "tree"));
    ST_CHECK(t0 + 2 < (i64)p->count && !strcmp(p->L->entries[p->order[t0 + 1]].name, "tree/deep") &&
             !strcmp(p->L->entries[p->order[t0 + 2]].name, "tree/inner.txt"));
    st_key(SDLK_RIGHT, SDL_KMOD_NONE); // step in
    ST_CHECK(!strcmp(st_cursor_name(p), "tree/deep"));
    st_key(SDLK_RIGHT, SDL_KMOD_NONE); // expand the nested folder
    st_wait();
    ST_CHECK(p->nexpanded == 2 && !strcmp(p->L->entries[p->order[t0 + 2]].name, "tree/deep/x.md"));
    st_frames(2);
    st_shot("tree");
    move_cursor(p, t0 + 2, false, false);
    st_key(SDLK_LEFT, SDL_KMOD_NONE); // to the parent
    ST_CHECK(!strcmp(st_cursor_name(p), "tree/deep"));
    st_key(SDLK_LEFT, SDL_KMOD_NONE); // collapse
    st_wait();
    ST_CHECK(p->nexpanded == 1 && !strcmp(p->L->entries[p->order[t0 + 2]].name, "tree/inner.txt"));
    // live refresh inside an expanded folder
    st_frames(2);
    st_write(M9_BASE "/tree/new.txt", 5);
    bool seen = false;
    for (int k = 0; k < 300 && !seen; k++) {
        st_frames(1);
        SDL_Delay(5);
        main_drain();
        watch_sync();
        watch_process();
        for (u32 i = 0; p->order && i < p->count && !seen; i++) seen = !strcmp(p->L->entries[p->order[i]].name, "tree/new.txt");
    }
    ST_CHECK(seen);
    // drag a file out of the expanded folder onto the pane's own folder (empty space below the rows) -> moved up
    st_wait();
    {
        f32 fy = st_row_y(p, "tree/new.txt"), tx = p->ly.col_name + S(60), ty = st_row_y(p, "tree");
        f32 ey = (f32)(p->ly.list.y + ((f64)p->count + 0.5) * p->ly.row_h - p->scroll);
        ST_CHECK(fy > 0 && ey < p->ly.list.y + p->ly.list.h);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, tx, fy, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, tx + S(20), fy + S(20), 0, 0, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, tx, ty, 0, 0, 0); // onto the folder it's already in: no drop
        ST_CHECK(A.drag == DRAG_ITEMS && !A.drop_dir[0]);
        st_mouse(SDL_EVENT_MOUSE_MOTION, tx, ey, 0, 0, 0);
        ST_CHECK(A.drag == DRAG_ITEMS && A.drop_row < 0 && !strcmp(A.drop_dir, M9_BASE));
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, tx, ey, SDL_BUTTON_LEFT, 1, 0);
        st_wait_ops();
        ST_CHECK(st_exists(M9_BASE "/new.txt") && !st_exists(M9_BASE "/tree/new.txt"));
        pane_refresh(p);
        st_wait();
    }
    // and back: dropped onto a file row inside the expanded folder -> into that folder (its whole section is the target)
    {
        f32 fy = st_row_y(p, "new.txt"), tx = p->ly.col_name + S(60), ty = st_row_y(p, "tree/inner.txt");
        ST_CHECK(fy > 0 && ty > 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, tx, fy, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, tx + S(20), fy + S(20), 0, 0, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, tx, ty, 0, 0, 0);
        ST_CHECK(A.drag == DRAG_ITEMS && A.drop_row >= 0 && !strcmp(A.drop_dir, M9_BASE "/tree") &&
                 !strcmp(p->L->entries[p->order[A.drop_row]].name, "tree") && A.drop_end > A.drop_row);
        st_shot("tree-drop");
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, tx, ty, SDL_BUTTON_LEFT, 1, 0);
        st_wait_ops();
        ST_CHECK(st_exists(M9_BASE "/tree/new.txt") && !st_exists(M9_BASE "/new.txt"));
        pane_refresh(p);
        st_wait();
    }
    // rename inside the tree renames within the subfolder
    st_wait();
    ST_CHECK(st_select(p, "tree/inner.txt"));
    begin_rename(p, p->cursor);
    ST_CHECK(A.ren_pane == p && !strcmp(A.ren_te.buf, "inner.txt"));
    te_set(&A.ren_te, "renamed.txt", 0, 0);
    commit_rename();
    ST_CHECK(st_exists(M9_BASE "/tree/renamed.txt") && !st_exists(M9_BASE "/tree/inner.txt"));
    st_wait();
    // several selected items drag together: Ctrl+click two files, then drag one of them onto a folder
    {
        f32 tx = p->ly.col_name + S(60);
        ST_CHECK(st_select(p, "file00.c"));
        f32 y1 = st_row_y(p, "file01.png");
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, tx, y1, SDL_BUTTON_LEFT, 1, SDL_KMOD_CTRL);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, tx, y1, SDL_BUTTON_LEFT, 1, SDL_KMOD_CTRL);
        ST_CHECK(p->sel_count == 2);
        f32 y0 = st_row_y(p, "file00.c"), ty = st_row_y(p, "sub");
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, tx, y0, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, tx + S(20), y0 + S(20), 0, 0, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, tx, ty, 0, 0, 0);
        ST_CHECK(A.drag == DRAG_ITEMS && p->sel_count == 2 && !strcmp(A.drop_dir, M9_BASE "/sub"));
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, tx, ty, SDL_BUTTON_LEFT, 1, 0);
        st_wait_ops();
        ST_CHECK(st_exists(M9_BASE "/sub/file00.c") && st_exists(M9_BASE "/sub/file01.png"));
        pane_refresh(p);
        st_wait();
    }
    // Ctrl+V pastes into the selected folder, or the subfolder of a selected file
    {
        char *cp[1] = {M9_BASE "/file02.md"};
        clip_set(cp, 1, false);
        ST_CHECK(st_select(p, "tree/deep"));
        st_key(SDLK_V, SDL_KMOD_CTRL);
        st_wait_ops();
        ST_CHECK(st_exists(M9_BASE "/tree/deep/file02.md"));
        ST_CHECK(st_select(p, "tree/renamed.txt"));
        st_key(SDLK_V, SDL_KMOD_CTRL);
        st_wait_ops();
        ST_CHECK(st_exists(M9_BASE "/tree/file02.md"));
        clip_clear();
        pane_refresh(p);
        st_wait();
    }
    // right-click a collapsed subfolder: "New folder inside" creates it there, expands the folder, starts the rename
    {
        ST_CHECK(tree_find(p, "tree/deep") < 0);
        f32 ry = st_row_y(p, "tree/deep"), rx = p->ly.col_name + S(60);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, rx, ry, SDL_BUTTON_RIGHT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, rx, ry, SDL_BUTTON_RIGHT, 1, 0);
        bool has_in = false;
        for (int i = 0; i < A.menu.n; i++) has_in |= A.menu.items[i].act == ACT_NEW_FOLDER_IN;
        ST_CHECK(A.menu.open && has_in && !strcmp(st_cursor_name(p), "tree/deep"));
        A.menu.open = false;
        do_action(p, ACT_NEW_FOLDER_IN);
        st_wait();
        ST_CHECK(st_exists(M9_BASE "/tree/deep/New folder") && tree_find(p, "tree/deep") >= 0);
        ST_CHECK(A.ren_pane == p && !strcmp(A.ren_te.buf, "New folder") && !strcmp(st_cursor_name(p), "tree/deep/New folder"));
        te_set(&A.ren_te, "made", 0, 0);
        commit_rename();
        ST_CHECK(st_exists(M9_BASE "/tree/deep/made"));
        st_wait();
        ST_CHECK(st_select(p, "tree/deep"));
        do_action(p, ACT_NEW_FILE_IN);
        st_wait();
        ST_CHECK(st_exists(M9_BASE "/tree/deep/New file") && A.ren_pane == p);
        A.ren_pane = NULL;
        st_wait();
    }
    // chevron click collapses
    {
        f32 ry2 = st_row_y(p, "tree");
        Entry *te2 = NULL;
        for (u32 i = 0; i < p->count; i++)
            if (!strcmp(p->L->entries[p->order[i]].name, "tree")) te2 = &p->L->entries[p->order[i]];
        ST_CHECK(ry2 > 0 && te2);
        if (te2) {
            f32 cx = p->ly.col_name + tree_indent(p->L, te2) - S(9);
            st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, cx, ry2, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
            st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, cx, ry2, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
            st_wait();
            ST_CHECK(p->nexpanded == 0);
        }
    }
    // the menu shows it as a checked view option; turning it off goes back to plain names
    run_command(CMD_TREE_VIEW);
    st_wait();
    ST_CHECK(!p->tree && !p->L->tree);
    for (u32 i = 0; i < p->count; i++) ST_CHECK(!strchr(p->L->entries[p->order[i]].name, '/'));

    // gear button in the status bar opens Options
    layout();
    st_frames(1);
    ST_CHECK(A.r_gear.w > 0 && A.r_gear.x > A.r_viewbtn[2].x);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, A.r_gear.x + A.r_gear.w * 0.5f, A.r_gear.y + A.r_gear.h * 0.5f, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, A.r_gear.x + A.r_gear.w * 0.5f, A.r_gear.y + A.r_gear.h * 0.5f, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
    ST_CHECK(g_sheet.kind == SHEET_OPTIONS);
    sheet_close();
    st_mouse(SDL_EVENT_MOUSE_MOTION, A.r_gear.x + A.r_gear.w * 0.5f, A.r_gear.y + A.r_gear.h * 0.5f, 0, 0, SDL_KMOD_NONE);
    st_shot("gear");

    // features button (left of the gear): a menu of toggles that stays open; each item flips its setting
    {
        ST_CHECK(A.r_feat.w > 0 && A.r_feat.x < A.r_gear.x && A.r_feat.x > A.r_viewbtn[2].x + A.r_viewbtn[2].w);
        f32 fx = A.r_feat.x + A.r_feat.w * 0.5f, fy = A.r_feat.y + A.r_feat.h * 0.5f;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, fx, fy, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, fx, fy, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        ST_CHECK(features_menu_open() && A.menu.r.y + A.menu.r.h <= A.r_feat.y && A.menu.r.y >= 0);
        st_shot("features-menu");
        int hi = -1, pi = -1;
        for (int i = 0; i < A.menu.n; i++) {
            if (A.menu.items[i].act == ACT_HIDDEN) hi = i;
            if (A.menu.items[i].act == ACT_PREVIEW) pi = i;
        }
        ST_CHECK(hi >= 0 && pi >= 0 && A.menu.items[hi].checked == A.show_hidden && A.menu.items[hi].shortcut[0]);
        bool h0 = A.show_hidden, p0 = A.preview_on;
        menu_activate(hi);
        ST_CHECK(A.show_hidden == !h0 && features_menu_open() && A.menu.items[hi].checked == A.show_hidden);
        menu_activate(pi);
        ST_CHECK(A.preview_on == !p0 && features_menu_open());
        menu_activate(hi);
        menu_activate(pi);
        ST_CHECK(A.show_hidden == h0 && A.preview_on == p0);
        int li = -1;
        for (int i = 0; i < A.menu.n; i++)
            if (A.menu.items[i].act == ACT_LOAD_TIMES) li = i;
        ST_CHECK(li >= 0 && A.menu.items[li].checked == A.load_times);
        bool l0 = A.load_times;
        menu_activate(li);
        ST_CHECK(A.load_times == !l0 && features_menu_open() && A.menu.items[li].checked == A.load_times);
        menu_activate(li);
        ST_CHECK(A.load_times == l0);
        A.menu.open = false;
        st_frames(1);
    }

    // nothing under a menu / sheet reacts to the mouse
    {
        p = active_pane();
        st_wait();
        f32 hx = p->ly.list.x + S(80), hy = p->ly.list.y + p->ly.row_h * 1.5f;
        st_mouse(SDL_EVENT_MOUSE_MOTION, hx, hy, 0, 0, SDL_KMOD_NONE);
        st_frames(1);
        ST_CHECK(p->ly.hover >= 0);
        menu_open(p, p->ly.list.x + p->ly.list.w - S(260), p->ly.list.y + S(10), false);
        st_mouse(SDL_EVENT_MOUSE_MOTION, hx, hy, 0, 0, SDL_KMOD_NONE);
        st_frames(1);
        ST_CHECK(p->ly.hover < 0 && A.mouse_in);
        A.menu.open = false;
        options_open();
        st_mouse(SDL_EVENT_MOUSE_MOTION, hx, hy, 0, 0, SDL_KMOD_NONE);
        st_frames(1);
        ST_CHECK(p->ly.hover < 0);
        sheet_close();
        st_frames(1);
        ST_CHECK(p->ly.hover >= 0);
    }
    // a full glyph atlas never reaches the screen: the frame is redrawn (or the atlas grows) first
    {
        int ox, oy;
        while (atlas_pack(64, 64, &ox, &oy)) {}
        ST_CHECK(g_atlas_full);
        options_open();
        st_frames(1);
        ST_CHECK(!g_atlas_full);
        ST_CHECK(atlas_grow() && g_atlas_size == 2048);
        st_frames(2);
        ST_CHECK(!g_atlas_full);
        st_shot("atlas-2048");
        sheet_close();
    }

    // context menu in a short window: fits on screen and scrolls
    {
        int h0 = A.H;
        A.H = 300;
        p = active_pane();
        menu_open(p, p->ly.list.x + S(60), S(120), false);
        Menu *m = &A.menu;
        ST_CHECK(m->open && m->r.y >= S(4) - 1 && m->r.y + m->r.h <= A.H - S(4) + 1 && m->content_h > m->r.h);
        ST_CHECK(m->scroll == 0);
        SDL_MouseWheelEvent wv;
        SDL_zero(wv);
        wv.y = -3;
        wv.mouse_x = m->r.x + m->r.w * 0.5f;
        wv.mouse_y = m->r.y + m->r.h * 0.5f;
        on_wheel(&wv);
        st_settle(); // (it eases there)
        ST_CHECK(m->scroll > 0);
        st_key(SDLK_END, SDL_KMOD_NONE);
        for (int k = 0; k < m->n; k++) st_key(SDLK_UP, SDL_KMOD_NONE); // wrap around: every item gets revealed
        int last = m->n - 1;
        while (last > 0 && (m->items[last].sep || !m->items[last].enabled)) last--;
        m->hover = 0;
        st_key(SDLK_UP, SDL_KMOD_NONE); // wraps to the last item
        ST_CHECK(m->hover == last && m->scroll >= m->content_h - m->r.h - 1);
        f32 iy = m->r.y + m->r.h - S(30);
        int hit = menu_item_at(m->r.x + S(30), iy);
        ST_CHECK(hit >= 0 && hit <= last);
        st_shot("menu-scroll");
        m->open = false;
        A.H = h0;
    }

    { // input method: the text being composed shows under the focused field, and goes when it's committed
        on_text_editing("\xE6\x97\xA5\xE6\x9C\xAC"); // (two CJK characters)
        ST_CHECK(g_preedit[0]);
        st_frames(1);
        st_shot("m9-preedit");
        on_text_editing("");
        ST_CHECK(!g_preedit[0]);
    }

    { // own title bar: buttons at the top right, drag areas, resize edges, the strip above the preview
        ST_CHECK(!A.csd); // headless + automatic: left to the system
        A.title_bar = TITLEBAR_APP;
        titlebar_apply();
        st_frames(1);
        ST_CHECK(A.csd && A.r_wctl.w > 0 && fabsf(A.r_wctl.x + A.r_wctl.w - (f32)A.W) < 1);
        Panel *tr = NULL; // the panel at the top right
        for (int i = 0; i < A.npanels; i++)
            if (A.panels[i]->area.y < 1 && A.panels[i]->area.x + A.panels[i]->area.w >= A.r_content.x + A.r_content.w - 1) tr = A.panels[i];
        ST_CHECK(tr && tr->splitbtn.x + tr->splitbtn.w <= A.r_wctl.x);
        f32 d = SDL_GetWindowPixelDensity(A.win);
        #define ST_HIT(px, py) window_hit_test(A.win, &(SDL_Point){(int)((px) / d), (int)((py) / d)}, NULL)
        Rect b1 = wctl_button(1);
        ST_CHECK(ST_HIT(b1.x + b1.w * 0.5f, b1.y + b1.h * 0.5f) == SDL_HITTEST_NORMAL);
        ST_CHECK(ST_HIT(A.r_brand.x + A.r_brand.w * 0.5f, A.r_brand.y + A.r_brand.h * 0.5f) == SDL_HITTEST_DRAGGABLE);
        if (tr) {
            f32 ex = tr->plus.x + tr->plus.w + S(16); // between + and the split button: empty strip
            if (ex < tr->splitbtn.x) ST_CHECK(ST_HIT(ex, tr->bar.y + tr->bar.h * 0.5f) == SDL_HITTEST_DRAGGABLE);
            Rect tb = tr->cur->r;
            ST_CHECK(ST_HIT(tb.x + tb.w * 0.5f, tb.y + tb.h * 0.5f) == SDL_HITTEST_NORMAL); // a tab stays a tab
        }
        ST_CHECK(ST_HIT(1, A.H * 0.5f) == SDL_HITTEST_RESIZE_LEFT);
        ST_CHECK(ST_HIT(A.W - 1, A.H - 1) == SDL_HITTEST_RESIZE_BOTTOMRIGHT);
        ST_CHECK(ST_HIT(A.W * 0.5f, A.H * 0.5f) == SDL_HITTEST_NORMAL);
        #undef ST_HIT
        // an overlay owns the pointer: the header row is not a drag area any more (a press there would move the
        // window instead of reaching the overlay)
        options_open();
        st_frames(1);
        ST_CHECK(overlay_open());
        ST_CHECK(window_hit_test(A.win, &(SDL_Point){(int)((A.r_brand.x + A.r_brand.w * 0.5f) / d), (int)((A.r_brand.y + A.r_brand.h * 0.5f) / d)}, NULL) == SDL_HITTEST_NORMAL);
        if (tr) {
            f32 ex = tr->plus.x + tr->plus.w + S(16);
            if (ex < tr->splitbtn.x)
                ST_CHECK(window_hit_test(A.win, &(SDL_Point){(int)(ex / d), (int)((tr->bar.y + tr->bar.h * 0.5f) / d)}, NULL) == SDL_HITTEST_NORMAL);
        }
        ST_CHECK(window_hit_test(A.win, &(SDL_Point){(int)(1 / d), (int)(A.H * 0.5f / d)}, NULL) == SDL_HITTEST_RESIZE_LEFT); // the edges still resize
        sheet_close();
        ST_CHECK(!overlay_open());
        ST_CHECK(window_hit_test(A.win, &(SDL_Point){(int)((A.r_brand.x + A.r_brand.w * 0.5f) / d), (int)((A.r_brand.y + A.r_brand.h * 0.5f) / d)}, NULL) == SDL_HITTEST_DRAGGABLE);
        A.last_opened[0] = 0;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, wctl_button(1).x + S(20), S(15), SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
        ST_CHECK(!strcmp(A.last_opened, "window:maximise"));
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, wctl_button(2).x + S(20), S(15), SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
        ST_CHECK(!strcmp(A.last_opened, "window:close"));
        opt_bool_toggle(OPT_WINDOW_BUTTONS); // without the buttons: the tab strip runs to the window's edge, nothing to press
        ST_CHECK(!A.window_buttons && A.r_wctl.w == 0);
        if (tr) ST_CHECK(tr->splitbtn.x + tr->splitbtn.w > A.r_content.x + A.r_content.w - S(46));
        A.last_opened[0] = 0;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, A.W - S(20), S(15), SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
        ST_CHECK(strncmp(A.last_opened, "window:", 7) != 0);
        A.menu.open = false; // the top row is the navigation band now: that click can open the folder menu
        st_frames(1);
        st_shot("m9-titlebar-nobuttons");
        opt_bool_toggle(OPT_WINDOW_BUTTONS);
        ST_CHECK(A.window_buttons && A.r_wctl.w > 0);
        opt_bool_toggle(OPT_BRAND_LOGO); // the name alone above the sidebar, the brand still moves the window
        st_frames(1);
        st_shot("m9-brand-nologo");
        ST_CHECK(!A.brand_logo && A.r_brand.h > 0);
        opt_bool_toggle(OPT_BRAND_LOGO);
        ST_CHECK(A.brand_logo);
        // the name above the sidebar: the computer's, typed in Options, or none (the sidebar takes the top)
        ST_CHECK(!strcmp(brand_label(), APP_LABEL));
        opt_choice_set(OPT_BRAND_NAME, BRAND_HOST);
        ST_CHECK(brand_label()[0] && A.r_brand.h > 0);
        options_open();
        ST_CHECK(opt_hidden(OPT_BRAND_TEXT));
        opt_choice_set(OPT_BRAND_NAME, BRAND_CUSTOM);
        ST_CHECK(!opt_hidden(OPT_BRAND_TEXT) && g_opt.brand_focus && !strcmp(brand_label(), APP_LABEL)); // (nothing typed yet)
        options_text("Work\nstation \xC3\xA9");
        ST_CHECK(!strcmp(A.brand_text, "Work station \xC3\xA9") && !strcmp(brand_label(), A.brand_text));
        te_set(&g_opt.search, "sidebar", 7, 7);
        st_frames(1);
        st_shot("m9-brand-custom");
        { // a drawn, focused field drives the caret animation; pointer hit-testing still selects [lo, hi)
            ST_CHECK(g_opt.brand_focus && g_opt.brand.animation.seen > 0 && g_opt.brand.animation.valid);
            A.menu.open = false; // a menu left by an earlier click would swallow the first click here
            st_text_click(&g_opt.search, 0, 1, 0);
            ST_CHECK(g_opt.search.anchor == 0 && g_opt.search.caret == 0 && g_opt.search.animation.input > 0);
            st_text_click(&g_opt.search, 7, 1, SDL_KMOD_SHIFT);
            ST_CHECK(g_opt.search.anchor == 0 && g_opt.search.caret == 7);
        }
        options_key(SDLK_RETURN, 0);
        ST_CHECK(!g_opt.brand_focus && options_is_open());
        sheet_close();
        st_frames(1);
        st_shot("m9-brand-own");
        opt_choice_set(OPT_BRAND_NAME, BRAND_HIDDEN);
        ST_CHECK(A.r_brand.h == 0 && A.r_sidebar.y == 0);
        st_frames(1);
        st_shot("m9-brand-hidden");
        opt_choice_set(OPT_BRAND_NAME, BRAND_APP);
        ST_CHECK(A.r_brand.h > 0 && A.r_sidebar.y == A.r_brand.h);
        A.brand_text[0] = 0;
        bool pv = A.preview_on;
        if (!pv) preview_toggle();
        layout();
        ST_CHECK(A.r_wstrip.w > 0 && A.r_preview.y >= A.r_wctl.h);
        st_frames(1);
        st_shot("m9-titlebar");
        if (!pv) preview_toggle();
        A.title_bar = TITLEBAR_AUTO;
        titlebar_apply();
        ST_CHECK(!A.csd && A.r_wctl.w == 0 && A.r_wstrip.w == 0);
    }

    { // Options > Actions: the list, the editor (only what changed is written, the rest of the file stays), a new one,
      // templates, on / off, order (the files get numbered), Edit as text, delete, search
        char *old_dh = getenv("XDG_DATA_HOME") ? strdup(getenv("XDG_DATA_HOME")) : NULL;
        setenv("XDG_DATA_HOME", M9_BASE "/xdg", 1); // (never the user's actions)
        mkdir(M9_BASE "/xdg", 0755);
        mkdir(M9_BASE "/xdg/file-explorer", 0755);
        mkdir(M9_BASE "/xdg/file-explorer/actions", 0755);
#define M9_ACT M9_BASE "/xdg/file-explorer/actions/"
        static const char nemo_before[] = "# from Nemo\n[Nemo Action]\nActive=true\nName=Shrink\nName[de]=Verkleinern\nExec=mogrify -resize 50%% %F\n"
                                          "Selection=m\nMimetypes=image/*;\nExtensions=png;\n\n[Other]\nName=Kept\n";
        st_write_text(M9_ACT "nemo.nemo_action", nemo_before);
        st_write_text(M9_ACT "off.action", "[Action]\nActive=false\nName=Off one\nExec=true\n");
        options_open_page(OC_ACTIONS);
        st_frames(2);
        ST_CHECK(g_opt.cat == OC_ACTIONS && g_nua == 2 && !strcmp(g_ua[0].file, "nemo.nemo_action") && !g_ua[1].active);
        st_shot("options-actions");
        char buf[1024];

        st_opt_click(st_opt_hit(HP_ACT_EDIT, -1, 0)); // a new name: only Name= changes
        ST_CHECK(g_opt.act_edit == 0 && g_opt.act_focus == 0 && !strcmp(g_opt.act_te[0].buf, "Shrink") && g_opt.act_sel == 2);
        st_frames(2);
        st_shot("options-actions-edit");
        te_set(&g_opt.act_te[0], "Shrink", 0, 6);
        st_type("Half size");
        st_key(SDLK_RETURN, SDL_KMOD_NONE); // Enter saves
        st_read_text(M9_ACT "nemo.nemo_action", buf, sizeof buf);
        ST_CHECK(!strcmp(buf, "# from Nemo\n[Nemo Action]\nActive=true\nName=Half size\nName[de]=Verkleinern\nExec=mogrify -resize 50%% %F\n"
                              "Selection=m\nMimetypes=image/*;\nExtensions=png;\n\n[Other]\nName=Kept\n"));
        ST_CHECK(g_opt.act_edit < 0 && !strcmp(g_ua[0].name, "Half size"));

        st_frames(1);
        st_opt_click(st_opt_hit(HP_ACT_TOGGLE, -1, 1)); // on / off: Active=
        st_read_text(M9_ACT "off.action", buf, sizeof buf);
        ST_CHECK(g_ua[1].active && !strcmp(buf, "[Action]\nActive=true\nName=Off one\nExec=true\n"));

        st_opt_click(st_opt_hit(HP_ACT_NEW, -1, 0)); // a new one: Tab between the fields, Shows for
        ST_CHECK(g_opt.act_new && g_opt.act_focus == 0);
        st_type("Open in Gimp");
        st_key(SDLK_TAB, SDL_KMOD_NONE);
        ST_CHECK(g_opt.act_focus == 1);
        st_type("gimp %F");
        ST_CHECK(!strcmp(g_opt.act_te[1].buf, "gimp %F"));
        st_opt_click(st_opt_hit(HP_DROP, OPT_ACT_SEL, 0));
        ST_CHECK(g_opt.drop_opt == OPT_ACT_SEL);
        st_frames(1);
        st_opt_click(st_opt_hit(HP_DROPITEM, OPT_ACT_SEL, 1)); // One item
        ST_CHECK(g_opt.act_sel == 1 && g_opt.drop_opt < 0);
        g_opt.scroll = 1e6f; // (the editor is taller than the window: its buttons are at the bottom)
        st_frames(2);
        st_opt_click(st_opt_hit(HP_ACT_SAVE, -1, 0)); // last in the menu: the files get numbered
        ST_CHECK(g_nua == 3 && !strcmp(g_ua[0].file, "010-nemo.nemo_action") && !strcmp(g_ua[1].file, "020-off.action"));
        ST_CHECK(!strcmp(g_ua[2].file, "030-open-in-gimp.action") && g_opt.act_edit < 0);
        st_read_text(M9_ACT "030-open-in-gimp.action", buf, sizeof buf);
        ST_CHECK(!strcmp(buf, "[Action]\nName=Open in Gimp\nExec=gimp %F\nExtensions=any;\nSelection=s\n"));
        st_read_text(M9_ACT "010-nemo.nemo_action", buf, sizeof buf);
        ST_CHECK(strstr(buf, "Name=Half size\n") && strstr(buf, "[Other]\n"));

        st_frames(1);
        st_opt_click(st_opt_hit(HP_ACT_UP, -1, 2)); // order
        ST_CHECK(g_nua == 3 && !strcmp(g_ua[1].file, "020-open-in-gimp.action") && !strcmp(g_ua[2].file, "030-off.action"));
        ST_CHECK(st_exists(M9_ACT "020-open-in-gimp.action") && !st_exists(M9_ACT "030-open-in-gimp.action"));

        st_frames(1);
        st_opt_click(st_opt_hit(HP_DROP, OPT_ACT_TEMPLATE, 0)); // a template fills the editor
        st_frames(1);
        st_opt_click(st_opt_hit(HP_DROPITEM, OPT_ACT_TEMPLATE, 1));
        ST_CHECK(g_opt.act_new && !strcmp(g_opt.act_te[0].buf, "Copy image to clipboard") && g_opt.act_sel == 1);
        ST_CHECK(!strcmp(g_opt.act_te[3].buf, "wl-copy"));
        st_frames(1);
        A.last_opened[0] = 0;
        g_opt.scroll = 1e6f; // (the editor is taller than the window: its buttons are at the bottom)
        st_frames(2);
        st_opt_click(st_opt_hit(HP_ACT_TEXT, -1, 0)); // Edit as text: saved first, then the file in the editor
        ST_CHECK(g_nua == 4 && !strcmp(A.last_opened, "edit:" M9_ACT "040-copy-image-to-clipboard.action"));
        st_read_text(M9_ACT "040-copy-image-to-clipboard.action", buf, sizeof buf);
        ST_CHECK(!strcmp(buf, "[Action]\nName=Copy image to clipboard\nExec=sh -c \"wl-copy < %F\"\nExtensions=png; jpg; jpeg; webp; gif; svg\n"
                              "Dependencies=wl-copy\nSelection=s\nIcon=copy\n"));

        st_frames(1);
        st_opt_click(st_opt_hit(HP_ACT_EDIT, -1, 1)); // a group and an icon: Group= and Icon=; the group gets its row
        st_opt_click(st_opt_hit(HP_DROP, OPT_ACT_ICON, 0));
        st_frames(1);
        st_opt_click(st_opt_hit(HP_DROPITEM, OPT_ACT_ICON, 1 + ua_icon_find("image", false)));
        ST_CHECK(g_opt.act_icon == ua_icon_find("image", false));
        g_opt.scroll = 0;
        st_frames(2);
        st_text_click(&g_opt.act_te[0], 0, 1, 0);
        for (int k = 0; k < 4; k++) st_key(SDLK_TAB, SDL_KMOD_NONE); // Tab down to Group
        ST_CHECK(g_opt.act_focus == 4);
        g_opt.scroll = 1e6f;
        st_frames(2);
        f32 bottom = g_opt.scroll;
        st_key(SDLK_TAB, SDL_KMOD_NONE); // round to Name (out of view at the top when the editor scrolls): shown
        st_frames(3);
        OptHit *nh = st_opt_hit(HP_ACT_FIELD, -1, 0);
        ST_CHECK(g_opt.act_focus == 0 && g_opt.scroll <= bottom && nh && nh->r.h >= roundf(S(30)) - 1); // (all of it)
        st_key(SDLK_TAB, SDL_KMOD_SHIFT); // back to Group
        st_frames(2);
        ST_CHECK(g_opt.act_focus == 4 && st_opt_hit(HP_ACT_FIELD, -1, 4));
        st_type("Images");
        st_frames(1);
        st_shot("options-actions-edit-group");
        st_key(SDLK_RETURN, SDL_KMOD_NONE);
        st_read_text(M9_ACT "020-open-in-gimp.action", buf, sizeof buf);
        ST_CHECK(!strcmp(buf, "[Action]\nName=Open in Gimp\nExec=gimp %F\nExtensions=any;\nSelection=s\nGroup=Images\nIcon=image\n"));
        ST_CHECK(g_nuag == 1 && !strcmp(g_uag[0].name, "Images") && g_uag[0].icon < 0 && g_ua[1].icon == ua_icon_find("image", false));
        g_opt.scroll = 1e6f; // (the groups are at the bottom)
        st_frames(2);
        st_opt_click(st_opt_hit(HP_DROP, OPT_ACT_GICON, 0)); // the group's icon: groups.conf; typing filters the list
        st_type("camera");
        st_frames(1);
        ST_CHECK(g_opt.drop_n == 1);
        st_opt_click(st_opt_hit(HP_DROPITEM, OPT_ACT_GICON, 0));
        st_read_text(M9_ACT "groups.conf", buf, sizeof buf);
        ST_CHECK(!strcmp(buf, "[Images]\nIcon=camera\n") && g_uag[0].icon == ua_icon_find("camera", false));
        ua_load(); // (read back)
        ST_CHECK(g_nuag == 1 && g_uag[0].icon == ua_icon_find("camera", false));
        g_opt.scroll = 0;
        st_frames(2);
        st_shot("options-actions-groups");

        { // the editor checks the command as it's typed: a preview, or what's wrong (a missing program goes into Needs
          // programs with a click); Common types add to File types; the Group button lists the groups
            st_opt_click(st_opt_hit(HP_ACT_NEW, -1, 0));
            char pv[1200], prog[256];
            bool fsel;
            te_set(&g_opt.act_te[1], "fe-no-such-program %F", 0, 0);
            ST_CHECK(opt_act_preview(pv, sizeof pv, prog, sizeof prog, &fsel) && !strcmp(prog, "fe-no-such-program"));
            ST_CHECK(!strcmp(pv, "\xE2\x80\x9C" "fe-no-such-program\xE2\x80\x9D isn't installed"));
            st_frames(2);
            st_shot("options-actions-warning");
            st_opt_click(st_opt_hit(HP_ACT_NEEDS, -1, 0));
            ST_CHECK(!strcmp(g_opt.act_te[3].buf, "fe-no-such-program"));
            st_frames(1);
            ST_CHECK(!st_opt_hit(HP_ACT_NEEDS, -1, 0)); // (listed now: hidden until it's installed)
            te_set(&g_opt.act_te[1], "echo \"open", 0, 0);
            ST_CHECK(opt_act_preview(pv, sizeof pv, prog, sizeof prog, &fsel) && !strcmp(pv, "A double quote isn't closed"));
            te_set(&g_opt.act_te[1], "echo %x", 0, 0);
            ST_CHECK(opt_act_preview(pv, sizeof pv, prog, sizeof prog, &fsel) && !strncmp(pv, "%x isn't a code", 15));
            te_set(&g_opt.act_te[1], "echo 100%% %f", 0, 0);
            ST_CHECK(!opt_act_preview(pv, sizeof pv, prog, sizeof prog, &fsel) && !prog[0] && !strncmp(pv, "echo 100% ", 10));
            Pane *ap = active_pane(); // nothing selected, no cursor: an example of the file types
            i64 cur = ap->cursor;
            sel_clear(ap);
            ap->cursor = -1;
            te_set(&g_opt.act_te[2], "png; jpg", 0, 0);
            te_set(&g_opt.act_te[1], "sh -c \"cat %F | wl-copy\"", 0, 0);
            ST_CHECK(!opt_act_preview(pv, sizeof pv, prog, sizeof prog, &fsel) && !fsel);
            char want[PATH_MAX + 64];
            snprintf(want, sizeof want, "sh -c 'cat '\\''%s/example.png'\\'' | wl-copy'", getenv("HOME"));
            ST_CHECK(!strcmp(pv, want));
            ap->cursor = cur;
            te_set(&g_opt.act_te[2], "", 0, 0);
            st_frames(1);
            st_opt_click(st_opt_hit(HP_ACT_TYPE, -1, 0)); // Images
            ST_CHECK(!strncmp(g_opt.act_te[2].buf, "png; jpg; jpeg; gif", 19));
            size_t l1 = strlen(g_opt.act_te[2].buf);
            st_opt_click(st_opt_hit(HP_ACT_TYPE, -1, 0)); // (again: nothing twice)
            st_opt_click(st_opt_hit(HP_ACT_TYPE, -1, 5)); // Folders
            ST_CHECK(strlen(g_opt.act_te[2].buf) == l1 + 5 && !strcmp(g_opt.act_te[2].buf + l1, "; dir"));
            g_opt.scroll = 1e6f;
            st_frames(2);
            st_opt_click(st_opt_hit(HP_ACT_GROUPS, -1, 0));
            ST_CHECK(g_opt.drop_opt == OPT_ACT_GROUP && g_opt.drop_n == 2);
            st_frames(1);
            st_opt_click(st_opt_hit(HP_DROPITEM, OPT_ACT_GROUP, 1));
            ST_CHECK(!strcmp(g_opt.act_te[4].buf, "Images"));
            st_key(SDLK_ESCAPE, SDL_KMOD_NONE); // (unsaved)
            ST_CHECK(g_opt.act_edit < 0 && g_nua == 4);
            g_opt.scroll = 0;
        }

        st_frames(1);
        st_opt_click(st_opt_hit(HP_ACT_EDIT, -1, 0)); // Esc: the field lets go, then the editor closes unsaved
        st_type("x");
        st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
        st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
        ST_CHECK(g_sheet.kind == SHEET_OPTIONS && g_opt.act_edit < 0 && !strcmp(g_ua[0].name, "Half size"));

        st_frames(1);
        st_opt_click(st_opt_hit(HP_ACT_DELETE, -1, 2)); // delete asks, then the file goes to the trash
        ST_CHECK(A.dlg.open);
        dialog_choose(1);
        st_wait_ops();
        ST_CHECK(g_nua == 3 && !st_exists(M9_ACT "030-off.action") && st_exists(M9_BASE "/xdg/Trash/files/030-off.action"));

        st_type("gimp"); // search finds actions by name
        st_frames(2);
        ST_CHECK(st_opt_hit(HP_ACT_EDIT, -1, 1) && !st_opt_hit(HP_ACT_EDIT, -1, 0) && !st_opt_hit(HP_ACT_NEW, -1, 0));
        st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
        sheet_close();
#undef M9_ACT
        if (old_dh) setenv("XDG_DATA_HOME", old_dh, 1);
        else unsetenv("XDG_DATA_HOME");
        free(old_dh);
    }

    g_theme_cur = 0;
    theme_apply();
    st_frames(2);
    fprintf(stderr, "selftest-m9: %s (%d failures)\n", st_fail ? "FAIL" : "ok", st_fail);
    return st_fail ? 1 : 0;
}

// --- self-test for M11: terminal tabs -------------------------------------------------------------------
#define M11_BASE "/tmp/fe-m11-test"

static int st_place_row(int kind, const char *label);

static void st_setup_m11(void) {
    if (system("rm -rf " M11_BASE " /tmp/fe-selftest-config/foot /tmp/fe-selftest-config/kitty /tmp/fe-selftest-config/xdg-terminals.list")) {}
    mkdir_p(M11_BASE "/cfg", 0755);
    setenv("FE_TERM_SHELL", "/bin/sh", 1); // a predictable shell
}

static void st_write_text(const char *path, const char *text) {
    char dir[PATH_MAX];
    parent_of(path, dir, sizeof dir);
    mkdir_p(dir, 0755);
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

// The text of a terminal row (absolute row: scrollback < 0).
static void st_term_row(Term *t, int r, char *out, size_t cap) {
    size_t n = 0;
    for (int c = 0; c < t->cols && n + 5 < cap; c++) {
        TCell cell;
        if (!term_cell(t, r, c, &cell)) break;
        if (cell.ch == (u32)-1) continue;
        n += (size_t)utf8_encode(cell.ch ? cell.ch : ' ', out + n);
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
}

static int st_term_find(Term *t, const char *needle) { // the screen row containing needle, or -1
    char row[1024];
    for (int r = 0; r < t->rows; r++) {
        st_term_row(t, r, row, sizeof row);
        if (strstr(row, needle)) return r;
    }
    return -1;
}

static bool st_term_wait(Term *t, const char *needle) {
    for (int k = 0; k < 400; k++) {
        st_frames(1);
        if (st_term_find(t, needle) >= 0) return true;
        SDL_Delay(5);
    }
    return false;
}

static int run_selftest_m11(void) {
    st_wait();
    // Exercise the same calt shaping used by the renderer with patched JetBrains
    // families when installed; Fira Code is a useful independent ligature fixture.
    {
        const char *families[] = {"JetBrainsMono Nerd Font", "JetBrainsMono Nerd Font Mono", "JetBrainsMono NFM", "JetBrainsMono NF", "Fira Code"};
        for (size_t family = 0; family < ARRAY_COUNT(families); family++) {
            char *path = font_family_file(families[family]);
            if (!path) continue;
            FT_Face face;
            if (!FT_New_Face(g_ft, path, 0, &face)) {
                FT_Set_Pixel_Sizes(face, 0, 16);
                hb_font_t *font = hb_ft_font_create_referenced(face);
                const char *sequences[] = {"->", "!=", "==="};
                for (size_t seq = 0; seq < ARRAY_COUNT(sequences); seq++) {
                    hb_buffer_t *buf = hb_buffer_create();
                    const char *text = sequences[seq];
                    int len = (int)strlen(text);
                    hb_buffer_add_utf8(buf, text, len, 0, len);
                    term_shape_buffer(font, buf);
                    unsigned count;
                    hb_glyph_info_t *glyphs = hb_buffer_get_glyph_infos(buf, &count);
                    bool changed = count != (unsigned)len;
                    for (unsigned i = 0; i < count; i++) {
                        ST_CHECK(glyphs[i].codepoint != 0 && glyphs[i].cluster < (unsigned)len);
                        if (glyphs[i].cluster < (unsigned)len)
                            changed |= glyphs[i].codepoint != FT_Get_Char_Index(face, (unsigned char)text[glyphs[i].cluster]);
                    }
                    ST_CHECK(changed);
                    hb_buffer_destroy(buf);
                }
                hb_font_destroy(font);
                FT_Done_Face(face);
            }
            free(path);
        }
    }
    { // Fish can change colour between operator characters; that must not break shaping.
        Term sample = {0};
        sample.rows = 3; sample.cols = 8; sample.cw = 10; sample.ch = 20;
        sample.vt = vterm_new(sample.rows, sample.cols);
        sample.scr = vterm_obtain_screen(sample.vt);
        vterm_screen_reset(sample.scr, 1);
        const char *ansi = "\033[31m=\033[32m=\033[0m\r\nM\r\n\033[31mM\033[32mM";
        vterm_input_write(sample.vt, ansi, strlen(ansi));
        vterm_screen_flush_damage(sample.scr);
        TCell first, second;
        ST_CHECK(term_cell(&sample, 0, 0, &first) && term_cell(&sample, 0, 1, &second));
        ST_CHECK(first.fg != second.fg);
        term_fonts();
        ST_CHECK(term_draw_run(&sample, 0, 0, 0, 14, &g_term_font) == 1);
        ST_CHECK(term_cell_foreground(&sample, 0, 0, &first) == first.fg);
        ST_CHECK(term_cell_foreground(&sample, 0, 1, &second) == second.fg);
        // a glyph alone in its cell is drawn as it is: not fitted to the cell, nor cut at the run's ends
        sample.cw = 2; // cells narrower than the glyph
        int nv0 = g_nv;
        ST_CHECK(term_draw_run(&sample, 1, 0, 20, 14, &g_term_font) == 0);
        Glyph *mg = font_glyph(&g_term_font, 'M');
        ST_CHECK(g_nv == nv0 + 4 && mg->w > 2 && fabsf((g_v[nv0 + 1].position.x - g_v[nv0].position.x) - mg->w) < 0.01f);
        // a slight overhang into a cell of another colour stays the glyph's own colour (no seam by powerline caps)
        sample.cw = MAX(2.f, (f32)mg->w - 1);
        nv0 = g_nv;
        ST_CHECK(term_draw_run(&sample, 2, 0, 40, 14, &g_term_font) == 1);
        ST_CHECK(g_nv == nv0 + 8 && fabsf((g_v[nv0 + 1].position.x - g_v[nv0].position.x) - mg->w) < 0.01f &&
                 g_v[nv0].color.r != g_v[nv0 + 4].color.r);
        vterm_free(sample.vt);
    }
    const char *cfg = "/tmp/fe-selftest-config";
    // --- the default terminal's config: foot (include + [colors-dark]), kitty, Alacritty (import), Ghostty (theme)
    st_write_text("/tmp/fe-selftest-config/xdg-terminals.list", "# preference\nfoot.desktop\n");
    st_write_text("/tmp/fe-selftest-config/foot/foot.ini",
             "[main]\ninclude=~/../../tmp/fe-m11-test/cfg/theme.ini\nfont=DejaVu Sans Mono:size=11, Noto Color Emoji\nline-height=15\npad=10x12\n"
             "[cursor]\nstyle=beam\n[scrollback]\nlines=5000\nmultiplier=6.0\n");
    st_write_text(M11_BASE "/cfg/theme.ini", "[colors-dark]\nforeground=d0d3d9\nbackground=0e0f13\nregular1=ff5f6d\nbright4=8cc2ff\n"
                                       "selection-background=26304a\ncursor=0e0f13 f4f5f7\n");
    char which[32];
    tc_default_terminal(which, sizeof which);
    ST_CHECK(!strcmp(which, "foot"));
    tc_reload_all();
    ST_CHECK(!strcmp(g_tc.source, "foot") && !strcmp(g_tc.family, "DejaVu Sans Mono") && g_tc.size_pt == 11 && g_tc.line_pt == 15);
    ST_CHECK(g_tc.padx == 10 && g_tc.pady == 12 && g_tc.cursor_shape == VTERM_PROP_CURSORSHAPE_BAR_LEFT && g_tc.scrollback == 5000);
    ST_CHECK(g_tc.has_bg && g_tc.bg == 0x0E0F13 && g_tc.fg == 0xD0D3D9 && tc_pal(1) == 0xFF5F6D && tc_pal(12) == 0x8CC2FF);
    ST_CHECK(g_tc.has_cur && g_tc.cur == 0xF4F5F7 && g_tc.cur_text == 0x0E0F13 && g_tc.sel_bg == 0x26304A && g_tc.nfiles == 2);
    { // kitty
        st_write_text(M11_BASE "/cfg/kitty.conf", "include theme-k.conf\nfont_family JetBrains Mono\nfont_size 9.5\ncursor_shape underline\n"
                                            "window_padding_width 6\nscrollback_lines 2000\n");
        st_write_text(M11_BASE "/cfg/theme-k.conf", "background #101010\nforeground #eeeeee\ncolor2 #00ff00\n");
        TermConfig save = g_tc;
        memset(&g_tc, 0, sizeof g_tc);
        tc_kitty(M11_BASE "/cfg/kitty.conf", 0);
        ST_CHECK(!strcmp(g_tc.family, "JetBrains Mono") && g_tc.size_pt == 9.5f && g_tc.bg == 0x101010 && tc_pal(2) == 0x00FF00 &&
                 g_tc.cursor_shape == VTERM_PROP_CURSORSHAPE_UNDERLINE && g_tc.scrollback == 2000 && g_tc.padx == 8);
        // Alacritty
        st_write_text(M11_BASE "/cfg/ala.toml", "[general]\nimport = [\"ala-colors.toml\"]\n[font]\nsize = 12\nnormal = { family = \"Hack\", style = \"Regular\" }\n"
                                          "[window.padding]\nx = 4\ny = 5\n");
        st_write_text(M11_BASE "/cfg/ala-colors.toml", "[colors.primary]\nbackground = \"#1e1e2e\"\nforeground = \"#cdd6f4\"\n[colors.normal]\nred = \"#f38ba8\"\n"
                                                 "[colors.bright]\nblue = \"0x89b4fa\"\n");
        memset(&g_tc, 0, sizeof g_tc);
        tc_alacritty(M11_BASE "/cfg/ala.toml", 0);
        ST_CHECK(!strcmp(g_tc.family, "Hack") && g_tc.size_pt == 12 && g_tc.bg == 0x1E1E2E && tc_pal(1) == 0xF38BA8 && tc_pal(12) == 0x89B4FA &&
                 g_tc.padx == 4 && g_tc.pady == 5);
        // Ghostty
        st_write_text(M11_BASE "/cfg/ghostty", "font-family = \"Fira Code\"\nfont-size = 13\ntheme = mytheme\npalette = 3=#abcdef\ncursor-style = bar\n");
        st_write_text(M11_BASE "/cfg/themes/mytheme", "background = 202020\nforeground = e0e0e0\n");
        memset(&g_tc, 0, sizeof g_tc);
        tc_ghostty(M11_BASE "/cfg/ghostty", 0);
        ST_CHECK(!strcmp(g_tc.family, "Fira Code") && g_tc.size_pt == 13 && g_tc.bg == 0x202020 && tc_pal(3) == 0xABCDEF &&
                 g_tc.cursor_shape == VTERM_PROP_CURSORSHAPE_BAR_LEFT);
        g_tc = save;
        // the list names the default: kitty now
        st_write_text("/tmp/fe-selftest-config/xdg-terminals.list", "kitty.desktop\n");
        tc_default_terminal(which, sizeof which);
        ST_CHECK(!strcmp(which, "kitty"));
        st_write_text("/tmp/fe-selftest-config/xdg-terminals.list", "foot.desktop\n");
    }
    (void)cfg;
    // --- a terminal tab: output, colours, title, typing, selection
    int nt0 = A.ntabs;
    char *av[] = {"/bin/sh", "-c", "printf 'hello \\033[31mred\\033[0m done\\n\\033]0;MyTitle\\007'; exec cat", NULL};
    Tab *tt = term_tab_open(M11_BASE, av);
    ST_CHECK(tt && tt->term && A.ntabs == nt0 + 1 && active_term() == tt->term);
    if (!tt || !tt->term) return st_fail ? 1 : 0;
    Term *t = tt->term;
    ST_CHECK(st_term_wait(t, "hello red done"));
    int hr = st_term_find(t, "hello red");
    TCell rc;
    ST_CHECK(hr >= 0 && term_cell(t, hr, 6, &rc) && rc.ch == 'r' && rc.fg == 0xFF5F6D && !(rc.attr & 32)); // red = the config's colour 1
    ST_CHECK(term_cell(t, hr, 0, &rc) && (rc.attr & 32) && rc.fg == 0xD0D3D9);                                // default = its foreground
    for (int k = 0; k < 100 && strcmp(t->title, "MyTitle"); k++) { st_frames(1); SDL_Delay(5); }
    ST_CHECK(!strcmp(term_label(t), "MyTitle"));
    ST_CHECK(t->cols > 20 && t->rows > 5 && t->cw > 0); // sized to the panel
    st_frames(2);
    st_shot("terminal");
    on_text("typed words");
    term_key(t, SDLK_RETURN, SDL_KMOD_NONE);
    ST_CHECK(st_term_wait(t, "typed words"));
    for (int k = 0; k < 200; k++) { // the tty echoes the line, then cat prints it again
        int a = st_term_find(t, "typed words");
        char row[512];
        if (a >= 0 && a + 1 < t->rows) { st_term_row(t, a + 1, row, sizeof row); if (strstr(row, "typed words")) break; }
        st_frames(1);
        SDL_Delay(5);
    }
    t->sel_on = true;
    t->sel_r0 = t->sel_r1 = hr;
    t->sel_c0 = 0;
    t->sel_c1 = 8;
    char *sel = term_selection_text(t);
    ST_CHECK(sel && !strcmp(sel, "hello red"));
    free(sel);
    t->sel_on = false;
    // keys stay with the terminal: a plain key never reaches the file list behind it
    st_key(SDLK_DELETE, SDL_KMOD_NONE);
    ST_CHECK(A.ntabs == nt0 + 1 && active_term() == t && !A.dlg.open);
    // Ctrl+C ends cat -> the tab closes by itself
    st_key(SDLK_C, SDL_KMOD_CTRL);
    for (int k = 0; k < 400 && A.ntabs != nt0; k++) { st_frames(1); SDL_Delay(5); }
    ST_CHECK(A.ntabs == nt0 && !active_term());
    // --- scrollback: 200 lines, the wheel scrolls back, typing returns to the bottom
    char *av2[] = {"/bin/sh", "-c", "i=1; while [ $i -le 200 ]; do echo line$i; i=$((i+1)); done; exec cat", NULL};
    tt = term_tab_open(M11_BASE, av2);
    t = tt ? tt->term : NULL;
    ST_CHECK(t && st_term_wait(t, "line200"));
    if (t) {
        ST_CHECK(t->sb_n >= 200 - t->rows);
        char row[256];
        st_term_row(t, -t->sb_n, row, sizeof row);
        ST_CHECK(!strcmp(row, "line1"));
        term_wheel(t, 2);
        ST_CHECK(t->sb_off > 0);
        term_text(t, "x");
        ST_CHECK(t->sb_off == 0);
        st_key(SDLK_W, SDL_KMOD_CTRL | SDL_KMOD_SHIFT); // Ctrl+Shift+W closes the terminal tab
        ST_CHECK(A.ntabs == nt0);
    }
    // --- F4: the shell in the folder; the tab is named after it; not saved with the session
    open_terminal(M11_BASE);
    t = active_term();
    ST_CHECK(t != NULL);
    if (t) {
        char lp[64], target[PATH_MAX] = "";
        snprintf(lp, sizeof lp, "/proc/%d/cwd", (int)t->pid);
        ssize_t k = readlink(lp, target, sizeof target - 1);
        if (k > 0) target[k] = 0;
        ST_CHECK(!strcmp(target, M11_BASE));
        t->label_t = 0;
        ST_CHECK(!strcmp(term_label(t), "fe-m11-test"));
        settings_save();
        char cfgf[PATH_MAX], line[2048];
        settings_file(cfgf, sizeof cfgf, false);
        int ptabs = 0;
        FILE *f = fopen(cfgf, "r");
        while (f && fgets(line, sizeof line, f)) ptabs += !strncmp(line, "ptab=", 5);
        if (f) fclose(f);
        ST_CHECK(ptabs == A.ntabs - 1); // the terminal tab isn't restored
        st_frames(2);
        st_shot("terminal-shell");
        // a sidebar place clicked while a terminal is in front opens in a folder tab (not the hidden one)
        int nt = A.ntabs, home = st_place_row(PK_HOME, NULL);
        Tab *tterm = A.ap->cur;
        ST_CHECK(home >= 0);
        if (home >= 0) {
            Rect hr = place_rect(home);
            st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, hr.x + S(40), hr.y + hr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
            st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, hr.x + S(40), hr.y + hr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
            st_wait();
            ST_CHECK(A.ntabs == nt + 1 && !active_term() && !strcmp(active_pane()->path, getenv("HOME")) && tterm->term);
            tab_close(A.cur);
            tab_select(tab_index(tterm));
        }
        tab_close(A.cur);
    }
    { // --- F4: the terminal panel under the folder (like Dolphin's), following the folder both ways; Shift+F4: a tab
        mkdir_p(M11_BASE "/a/b", 0755);
        Tab *ft = cur_tab();
        Pane *fp = &ft->pane[0];
        int nt = A.ntabs;
        pane_navigate(fp, M11_BASE, true);
        st_wait();
        f32 h0 = fp->ly.area.h;
        st_key(SDLK_F4, SDL_KMOD_NONE);
        layout();
        ST_CHECK(!ft->term && ft->dock && ft->dock_on && active_term() == ft->dock && A.ntabs == nt);
        ST_CHECK(fp->ly.area.h < h0 - S(100) && ft->dock_r.y >= fp->ly.area.y + fp->ly.area.h && ft->dock_r.h > S(100));
        char cwd[PATH_MAX] = "";
        for (int k = 0; k < 400 && !(ft->dock && term_cwd(ft->dock, cwd, sizeof cwd) && !strcmp(cwd, M11_BASE)); k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(!strcmp(cwd, M11_BASE));
        // the pane goes somewhere: the shell follows (cd typed at its prompt)
        pane_navigate(fp, M11_BASE "/a/b", true);
        for (int k = 0; k < 400 && !(ft->dock && term_cwd(ft->dock, cwd, sizeof cwd) && !strcmp(cwd, M11_BASE "/a/b")); k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(!strcmp(cwd, M11_BASE "/a/b"));
        st_frames(2);
        st_shot("terminal-panel");
        // a cd typed in the shell: the pane follows
        if (ft->dock) term_send(ft->dock, "cd ..\r");
        for (int k = 0; k < 400 && strcmp(fp->path, M11_BASE "/a"); k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(!strcmp(fp->path, M11_BASE "/a"));
        st_frames(20); // and nothing bounces back
        ST_CHECK(!strcmp(fp->path, M11_BASE "/a") && ft->dock && term_cwd(ft->dock, cwd, sizeof cwd) && !strcmp(cwd, M11_BASE "/a"));
        // going back where the shell was before its own cd: typed again; a folder it can't enter: typed only once
        pane_navigate(fp, M11_BASE "/a/b", true);
        for (int k = 0; k < 400 && !(ft->dock && term_cwd(ft->dock, cwd, sizeof cwd) && !strcmp(cwd, M11_BASE "/a/b")); k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(!strcmp(cwd, M11_BASE "/a/b"));
        if (getuid() != 0) {
            mkdir(M11_BASE "/locked", 0);
            pane_navigate(fp, M11_BASE "/locked", true);
            ft->dock_want_t -= 3000000000ull; // (as if the 2 s had passed)
            for (int k = 0; k < 60; k++) { st_frames(1); SDL_Delay(5); if (ft->dock_want[0]) ft->dock_want_t -= 3000000000ull; }
            int tries = 0;
            for (int r = -(ft->dock ? ft->dock->sb_n : 0); ft->dock && r < ft->dock->rows; r++) {
                char row[512];
                st_term_row(ft->dock, r, row, sizeof row);
                tries += strstr(row, "cd '" M11_BASE "/locked'") != NULL;
            }
            ST_CHECK(tries == 1 && !strcmp(fp->path, M11_BASE "/locked"));
            rmdir(M11_BASE "/locked");
            pane_navigate(fp, M11_BASE "/a", true);
            for (int k = 0; k < 400 && !(ft->dock && term_cwd(ft->dock, cwd, sizeof cwd) && !strcmp(cwd, M11_BASE "/a")); k++) { st_frames(1); SDL_Delay(5); }
        }
        // F4 from inside the terminal hides it (the shell keeps running); the folder has the keyboard again
        Term *keep = ft->dock;
        st_key(SDLK_F4, SDL_KMOD_NONE);
        layout();
        ST_CHECK(!ft->dock_on && ft->dock == keep && !active_term() && fabsf(fp->ly.area.h - h0) < 1);
        // Ctrl+Shift+F4 shows it with the keyboard; again: back to the folder, the panel stays
        st_key(SDLK_F4, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
        ST_CHECK(ft->dock_on && ft->dock == keep && active_term() == keep);
        st_key(SDLK_F4, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
        ST_CHECK(ft->dock_on && !active_term());
        // a click in it gives it the keyboard, a click in the folder takes it back
        layout();
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, ft->dock_r.x + S(40), ft->dock_r.y + S(20), SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, ft->dock_r.x + S(40), ft->dock_r.y + S(20), SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(active_term() == keep);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, fp->ly.list.x + S(300), fp->ly.list.y + fp->ly.list.h - S(10), SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, fp->ly.list.x + S(300), fp->ly.list.y + fp->ly.list.h - S(10), SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(!active_term() && ft->dock_on);
        // its close button hides it (the shell keeps running); F4 brings it back
        Rect xb = ft->dock_x;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, xb.x + xb.w * 0.5f, xb.y + xb.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, xb.x + xb.w * 0.5f, xb.y + xb.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        layout();
        ST_CHECK(xb.w > 0 && !ft->dock_on && ft->dock == keep && !active_term() && fabsf(fp->ly.area.h - h0) < 1);
        st_key(SDLK_F4, SDL_KMOD_NONE);
        st_key(SDLK_F4, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
        layout();
        ST_CHECK(ft->dock_on && ft->dock == keep && !active_term());
        // drag the line above it: taller, remembered
        f32 dh0 = ft->dock_r.h, ey = ft->dock_div.y + ft->dock_div.h * 0.5f;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, ft->dock_r.x + S(200), ey, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, ft->dock_r.x + S(200), ey - S(50), 0, 0, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, ft->dock_r.x + S(200), ey - S(50), SDL_BUTTON_LEFT, 1, 0);
        layout();
        ST_CHECK(A.drag == DRAG_NONE && fabsf(ft->dock_r.h - (dh0 + S(50))) <= 2);
        // the session remembers it (a new shell next time)
        settings_save();
        char cfgf[PATH_MAX], line[2048];
        settings_file(cfgf, sizeof cfgf, false);
        bool saved = false, height = false;
        FILE *f = fopen(cfgf, "r");
        while (f && fgets(line, sizeof line, f)) {
            const char *v = strstr(line, M11_BASE "/a\t");
            int view, px, dock = 0; // view:icon_px:dock_on:...
            if (!strncmp(line, "ptab=", 5) && v && sscanf(v + strlen(M11_BASE "/a\t"), "%d:%d:%d", &view, &px, &dock) == 3 && dock == 1) saved = true;
            if (!strncmp(line, "terminal_panel_height=", 22) && fabsf((f32)atof(line + 22) - A.dock_h) < 1) height = true;
        }
        if (f) fclose(f);
        ST_CHECK(saved && height);
        // exit in the shell closes the panel
        if (ft->dock) term_send(ft->dock, "exit\r");
        for (int k = 0; k < 400 && ft->dock; k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(!ft->dock && !ft->dock_on && !active_term());
        // bash, zsh and fish (those installed) get the shell integration: the pane's folder changes without a cd typed
        // at the prompt, what's typed there stays, a cd typed there still moves the pane. Their startup files (and
        // fish's greeting) run fastfetch: skipped in the panel, while one typed at the prompt runs (a stand-in on $PATH)
        static const char *shells[] = {"bash", "zsh", "fish"};
        char *home0 = getenv("HOME") ? strdup(getenv("HOME")) : NULL, *path0 = getenv("PATH") ? strdup(getenv("PATH")) : NULL;
        mkdir_p(M11_BASE "/home", 0755);
        st_write_text(M11_BASE "/bin/fastfetch", "#!/bin/sh\necho FF''RAN\n");
        chmod(M11_BASE "/bin/fastfetch", 0755);
        st_write_text(M11_BASE "/home/.bashrc", "fastfetch\n");
        st_write_text(M11_BASE "/home/.zshrc", "command -v fastfetch >/dev/null && fastfetch\n");
        st_write_text("/tmp/fe-selftest-config/fish/config.fish", "fastfetch\nfunction fish_greeting\n    fastfetch\nend\n");
        char spath[PATH_MAX * 2];
        snprintf(spath, sizeof spath, "%s/bin:%s", M11_BASE, path0 ? path0 : "/usr/bin:/bin");
        for (int si = 0; si < 3; si++) {
            char sp[PATH_MAX];
            if (!tool_abs(shells[si], sp, sizeof sp)) { printf("(%s not installed: its shell integration not tested)\n", shells[si]); continue; }
            setenv("FE_TERM_SHELL", sp, 1);
            setenv("HOME", M11_BASE "/home", 1);
            setenv("PATH", spath, 1);
            pane_navigate(fp, M11_BASE, true);
            st_wait();
            dock_toggle();
            setenv("HOME", home0 ? home0 : "/", 1);
            if (path0) setenv("PATH", path0, 1);
            Term *d = ft->dock;
            ST_CHECK(d && d->shint == si + 1);
            if (!d) continue;
            char rp[PATH_MAX + 8];
            snprintf(rp, sizeof rp, "%s.ready", d->cd_file);
            struct stat rst;
            for (int k = 0; k < 600 && stat(rp, &rst) != 0; k++) { st_frames(1); SDL_Delay(5); }
            ST_CHECK(stat(rp, &rst) == 0);
            for (int k = 0; k < 40; k++) { st_frames(1); SDL_Delay(5); } // (its first prompt)
            ST_CHECK(st_term_find(d, "FFRAN") < 0);
            if (st_term_find(d, "FFRAN") >= 0) printf("%s: fastfetch ran at startup\n", shells[si]);
            term_send(d, "typed");
            pane_navigate(fp, M11_BASE "/a/b", true);
            for (int k = 0; k < 600 && !(term_cwd(d, cwd, sizeof cwd) && !strcmp(cwd, M11_BASE "/a/b")); k++) { st_frames(1); SDL_Delay(5); }
            ST_CHECK(!strcmp(cwd, M11_BASE "/a/b"));
            ST_CHECK(st_term_wait(d, "typed")); // still at the prompt
            for (int k = 0; k < 20; k++) { st_frames(1); SDL_Delay(5); }
            ST_CHECK(st_term_find(d, " cd '") < 0); // nothing typed for it
            if (st_term_find(d, " cd '") >= 0) printf("%s: a cd was typed\n", shells[si]);
            term_send(d, "\x15" "cd ..\r");
            for (int k = 0; k < 600 && strcmp(fp->path, M11_BASE "/a"); k++) { st_frames(1); SDL_Delay(5); }
            ST_CHECK(!strcmp(fp->path, M11_BASE "/a"));
            // typed now, it's the program again (not our function); XDG_DATA_DIRS is the user's again
            term_send(d, "type fastfetch; env | grep -q fish-data || echo NO_FISH_DATA\r");
            ST_CHECK(st_term_wait(d, "fastfetch is /") && st_term_wait(d, "NO_FISH_DATA"));
            char cdf[PATH_MAX];
            snprintf(cdf, sizeof cdf, "%s", d->cd_file);
            dock_free(ft);
            ft->dock_on = ft->dock_focus = false;
            ST_CHECK(stat(rp, &rst) != 0 && stat(cdf, &rst) != 0); // its files are removed with it
        }
        free(home0);
        free(path0);
        unlink("/tmp/fe-selftest-config/fish/config.fish");
        setenv("FE_TERM_SHELL", "/bin/sh", 1);
        // Shift+F4: a terminal tab, as F4 used to open
        st_key(SDLK_F4, SDL_KMOD_SHIFT);
        ST_CHECK(A.ntabs == nt + 1 && A.ap->cur->term);
        if (A.ap->cur->term) tab_close(A.cur);
        A.dock_h = 240;
    }
    { // the sidebar edge is one full-height line drawn once, not a brand piece plus a list piece
        g_sidebar_edge_draws = 0;
        st_frames(1);
        ST_CHECK(g_sidebar_edge_draws == 1);
        ST_CHECK(g_sidebar_edge_probe.x == A.r_content.x - 1 && g_sidebar_edge_probe.y == 0 &&
                 fabsf(g_sidebar_edge_probe.h - (A.r_sidebar.y + A.r_sidebar.h)) < 0.5f);
        ST_CHECK(g_sidebar_edge_probe.color == COL_BORDER);
        int saved_drag = A.drag;
        A.drag = DRAG_SIDEBAR;
        g_sidebar_edge_draws = 0;
        st_frames(1);
        ST_CHECK(sidebar_edge_hot() && g_sidebar_edge_draws == 1 && g_sidebar_edge_probe.color == COL_ACCENT);
        A.drag = saved_drag;
        st_shot("sidebar-edge");
    }
    { // the sidebar is resizable: drag its edge (kept within limits), double-click resets, remembered
        f32 w0 = A.r_sidebar.w;
        Rect e = A.r_sdiv;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, e.x + e.w * 0.5f, e.y + S(200), SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, e.x + e.w * 0.5f + S(80), e.y + S(200), 0, 0, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, e.x + e.w * 0.5f + S(80), e.y + S(200), SDL_BUTTON_LEFT, 1, 0);
        st_frames(1);
        ST_CHECK(A.drag == DRAG_NONE && fabsf(A.r_sidebar.w - (w0 + S(80))) <= 2 && A.r_content.x == A.r_sidebar.w);
        st_shot("sidebar-wide");
        settings_save();
        f32 sw = A.side_w;
        settings_load();
        ST_CHECK(fabsf(A.side_w - sw) < 1);
        st_mouse(SDL_EVENT_MOUSE_MOTION, A.W - 10, 10, 0, 0, 0);
        A.side_w = 2000; // too wide: limited
        layout();
        ST_CHECK(A.r_sidebar.w <= S(480) + 1);
        e = A.r_sdiv;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, e.x + e.w * 0.5f, e.y + S(200), SDL_BUTTON_LEFT, 2, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, e.x + e.w * 0.5f, e.y + S(200), SDL_BUTTON_LEFT, 2, 0);
        st_frames(1);
        ST_CHECK(A.side_w == 216 && fabsf(A.r_sidebar.w - roundf(S(216))) < 1);
    }
    fprintf(stderr, "selftest-m11: %s (%d failures)\n", st_fail ? "FAIL" : "ok", st_fail);
    return st_fail ? 1 : 0;
}

// --- self-test for M10: single instance, tabs popup, drives/network, odd names, deep paths, scaling -------

#define M10_BASE "/tmp/fe-m10-test"
#define ST_GIO_VOLUMES "Drive(0): CT1000T705SSD3\n  Type: GProxyDrive (GProxyVolumeMonitorUDisks2)\n" \
    "  Volume(0): 161 GB Volume\n    Type: GProxyVolume (GProxyVolumeMonitorUDisks2)\n    can_mount=1\n" \
    "Volume(0): Pixel 7\n  Type: GProxyVolume (GProxyVolumeMonitorMTP)\n  activation_root=mtp://Google_Pixel_7_2A111FDH200ABC/\n" \
    "  can_mount=1\n" \
    "Volume(1): Al's iPhone\n  Type: GProxyVolume (GProxyVolumeMonitorAfc)\n  activation_root=afc://00008110-001A/\n" \
    "  Mount(0): Al's iPhone -> afc://00008110-001A/\n    Type: GProxyMount (GProxyVolumeMonitorAfc)\n" \
    "Volume(2): Documents on Al's iPhone\n  activation_root=afc://00008110-001A:3/\n" \
    "Volume(3): Locked iPhone\n  activation_root=afc://locked/\n" \
    "Volume(4): iPhone\n  activation_root=gphoto2://Apple_Inc._iPhone_00008110001A/\n" \
    "Mount(0): me on box -> sftp://box/\n  Type: GDaemonMount\n"
static bool cli_target(const char *in, char *dir, char *sel);

static void st_setup_m10(void) {
    st_rmrf(M10_BASE);
    mkdir(M10_BASE, 0755);
    mkdir(M10_BASE "/a", 0755);
    mkdir(M10_BASE "/b", 0755);
    st_write(M10_BASE "/c.txt", 10);
    st_write(M10_BASE "/my file.txt", 10);
    st_rmrf("/tmp/fe-m10-run");
    mkdir("/tmp/fe-m10-run", 0700);
    setenv("FE_IPC_SOCKET", "/tmp/fe-m10-run/file-explorer.sock", 1); // private instance (keep XDG_RUNTIME_DIR: Wayland lives there)
    // fake drive tools: a USB stick (vfat) + hidden EFI/swap partitions; mount/unmount/power-off log what they did
    mkdir(M10_BASE "/tools", 0755);
    mkdir(M10_BASE "/gvfs", 0755);
    mkdir(M10_BASE "/gvfs/smb-share:server=nas,share=media", 0755);
    mkdir(M10_BASE "/gvfs/gphoto2:host=%5Busb%3A001%2C007%5D", 0755); // a camera another program connected
    FILE *f = fopen(M10_BASE "/tools/volumes", "w"); // what `gio mount -li` lists: a drive, a phone, an iPhone (2 volumes), a locked one
    fputs(ST_GIO_VOLUMES, f);
    fclose(f);
    f = fopen(M10_BASE "/tools/lsblk", "w");
    fputs("#!/bin/sh\n"
          "echo 'PATH=\"/dev/sdz\" PKNAME=\"\" TYPE=\"disk\" RM=\"1\" HOTPLUG=\"1\" MAJ:MIN=\"8:240\" LABEL=\"\" FSTYPE=\"\" SIZE=\"16000000000\" PARTTYPE=\"\"'\n"
          "echo 'PATH=\"/dev/sdz1\" PKNAME=\"sdz\" TYPE=\"part\" RM=\"1\" HOTPLUG=\"1\" MAJ:MIN=\"8:241\" LABEL=\"MY\\x20STICK\" FSTYPE=\"vfat\" SIZE=\"15999000000\" PARTTYPE=\"0xc\"'\n"
          "echo 'PATH=\"/dev/sdy1\" PKNAME=\"sdy\" TYPE=\"part\" RM=\"0\" HOTPLUG=\"0\" MAJ:MIN=\"8:225\" LABEL=\"\" FSTYPE=\"vfat\" SIZE=\"100\" PARTTYPE=\"c12a7328-f81f-11d2-ba4b-00a0c93ec93b\"'\n"
          "echo 'PATH=\"/dev/sdy2\" PKNAME=\"sdy\" TYPE=\"part\" RM=\"0\" HOTPLUG=\"0\" MAJ:MIN=\"8:226\" LABEL=\"\" FSTYPE=\"swap\" SIZE=\"100\" PARTTYPE=\"\"'\n", f);
    fclose(f);
    f = fopen(M10_BASE "/tools/udisksctl", "w");
    fputs("#!/bin/sh\n"
          "echo \"$@\" >> " M10_BASE "/tools/log\n"
          "case \"$1\" in\n"
          "mount) [ \"$3\" = /dev/fail ] && { echo 'Error mounting /dev/fail: wrong fs type' >&2; exit 1; }\n"
          "  [ \"$3\" = /dev/loop8 ] && { echo 'Error mounting /dev/loop8: no filesystem' >&2; exit 1; }\n"
          "  [ \"$3\" = /dev/loop7 ] && { mkdir -p '" M10_BASE "/media/IMG'; echo \"Error mounting /dev/loop7: GDBus.Error:org.freedesktop.UDisks2.Error."
          "AlreadyMounted: Device /dev/loop7 is already mounted at \\`" M10_BASE "/media/IMG'.\" >&2; exit 1; }\n"
          "  [ \"$3\" = /dev/dirty ] && [ \"$5\" != ro ] && { echo 'Error mounting /dev/dirty at /run/media/me/WIN: wrong fs type, bad option, "
          "bad superblock on /dev/dirty, missing codepage or helper program, or other error (udisks-error-quark, 0)' >&2; exit 1; }\n"
          "  mkdir -p '" M10_BASE "/media/MY STICK'; echo \"Mounted $3 at " M10_BASE "/media/MY STICK.\" ;;\n"
          "unmount) echo \"Unmounted $3.\" ;;\n"
          "power-off|loop-delete) ;;\n"
          "dump) cat " M10_BASE "/tools/udisks-dump ;;\n"
          "loop-setup) case \"$4\" in *empty*) echo \"Mapped file $4 as /dev/loop8.\";; *) echo \"Mapped file $4 as /dev/loop7.\";; esac ;;\n"
          "esac\n", f);
    fclose(f);
    f = fopen(M10_BASE "/tools/gio", "w");
    fputs("#!/bin/sh\n" // like gio mount: unknown-host question for *newhost*, password prompts for *secure*/*newhost*
          "[ \"$2\" = -li ] && { cat " M10_BASE "/tools/volumes 2>/dev/null; exit 0; }\n" // volume monitors (not logged: every scan)
          "echo \"gio $@\" >> " M10_BASE "/tools/log\n"
          "[ \"$1\" = mount ] && [ \"$2\" != -u ] || exit 0\n"
          "case \"$2\" in mtp://*|afc://*|gphoto2://*)\n" // phones: no questions; a locked iPhone waits for Trust
          "  case \"$2\" in *locked*) echo \"gio: $2: Failed to mount: Unhandled Lockdown error: pairing dialog response pending\"; exit 2;; esac\n"
          "  n=$(echo \"$2\" | sed -E 's|^([a-z0-9]+)://([^:/]*):([0-9]+)/.*$|\\1:host=\\2,port=\\3|;s|^([a-z0-9]+)://([^:/]*)/.*$|\\1:host=\\2|')\n"
          "  mkdir -p \"" M10_BASE "/gvfs/$n\"; exit 0;;\n"
          "esac\n"
          "case \"$2\" in *dupe*) echo \"gio: $2: Location is already mounted\"; exit 2;; esac\n"
          "case \"$2\" in *newhost*) printf 'The identity of the remote computer is unknown.\\nAre you really sure?\\n"
          "[1] Log In Anyway\\n[2] Cancel Login\\nChoice: '; read c; [ \"$c\" = 1 ] || { echo \"gio: $2: Login dialog cancelled\"; exit 2; };; esac\n"
          "case \"$2\" in *secure*|*newhost*) printf 'Authentication Required\\nEnter password for \\\"x\\\" on box:\\nPassword: '; read pw;\n"
          "  if [ \"$pw\" != s3cret ]; then printf 'Password: '; read pw; [ \"$pw\" = s3cret ] || { echo \"gio: $2: Failed to mount: Permission denied\"; exit 2; }; fi;; esac\n"
          "h=$(echo \"$2\" | sed -E 's|^[a-z]+://([^@/]*@)?([^:/]*).*|\\2|')\n"
          "s=$(echo \"$2\" | sed -E 's|^([a-z]+)://.*|\\1|')\n"
          "mkdir -p \"" M10_BASE "/gvfs/$s:host=$h,user=me\"\n"
          "exit 0\n", f);
    fclose(f);
    chmod(M10_BASE "/tools/lsblk", 0755);
    chmod(M10_BASE "/tools/udisksctl", 0755);
    chmod(M10_BASE "/tools/gio", 0755);
    setenv("FE_LSBLK", M10_BASE "/tools/lsblk", 1);
    setenv("FE_UDISKSCTL", M10_BASE "/tools/udisksctl", 1);
    setenv("FE_GIO", M10_BASE "/tools/gio", 1);
    f = fopen(M10_BASE "/tools/secret-tool", "w"); // a keyring in files: secrets/<uri with / -> _>
    fputs("#!/bin/sh\n"
          "echo \"secret $1\" >> " M10_BASE "/tools/log\n"
          "eval k=\\${$#}; k=$(echo \"$k\" | tr '/:' '__'); d=" M10_BASE "/secrets; mkdir -p $d\n"
          "case \"$1\" in store) cat > \"$d/$k\" ;; lookup) [ -f \"$d/$k\" ] && cat \"$d/$k\" ;; clear) rm -f \"$d/$k\" ;; esac\n", f);
    fclose(f);
    chmod(M10_BASE "/tools/secret-tool", 0755);
    setenv("FE_SECRET_TOOL", M10_BASE "/tools/secret-tool", 1);
    f = fopen(M10_BASE "/tools/ssh", "w");
    fputs("#!/bin/sh\n"
          "case \" $* \" in\n" // the shared connection (devices.c): checked, opened with askpass, closed
          "*\" -O \"*) echo \"$*\" >> " M10_BASE "/tools/ssh-mux; case \" $* \" in *\" check \"*) exit 1;; esac; exit 0;;\n"
          "*ControlMaster=yes*) echo \"$*\" >> " M10_BASE "/tools/ssh-mux\n"
          "  [ -n \"$SSH_ASKPASS\" ] && \"$SSH_ASKPASS\" \"me@host's password: \" > " M10_BASE "/tools/ssh-pw.tmp\n"
          "  mv " M10_BASE "/tools/ssh-pw.tmp " M10_BASE "/tools/ssh-pw; exit 0;;\n"
          "esac\n"
          "echo \"ssh-args: $*\"\nexec cat\n", f);
    fclose(f);
    chmod(M10_BASE "/tools/ssh", 0755);
    setenv("FE_SSH", M10_BASE "/tools/ssh", 1);
    setenv("FE_GVFS_DIR", M10_BASE "/gvfs", 1);
    // systemd units: systemctl / journalctl stand-ins keeping unit state in files (never the real user manager)
    f = fopen(M10_BASE "/tools/systemctl", "w");
    fputs("#!/bin/sh\n"
          "D=" M10_BASE "/sd; mkdir -p \"$D\"; echo \"$*\" >> \"$D/log\"\n"
          "while [ \"${1#--}\" != \"$1\" ]; do shift; done\n"
          "cmd=$1; shift\n"
          "case $cmd in\n"
          " show) shift 2; for u in \"$@\"; do st=inactive; sub=dead; [ -f \"$D/$u.active\" ] && st=$(cat \"$D/$u.active\")\n"
          "   case $st in active) sub=running;; activating) sub=auto-restart;; failed) sub=failed;; esac\n"
          "   ufs=disabled; [ -f \"$D/$u.enabled\" ] && ufs=enabled\n"
          "   printf 'Id=%s\\nActiveState=%s\\nSubState=%s\\nUnitFileState=%s\\n\\n' \"$u\" $st $sub $ufs; done;;\n"
          " start) for u; do if [ -f \"$D/next-start\" ]; then cat \"$D/next-start\" > \"$D/$u.active\"; else echo active > \"$D/$u.active\"; fi; done;;\n"
          " stop) for u; do rm -f \"${D:?}/${u:?}.active\"; done;;\n"
          " enable) touch \"$D/$1.enabled\";;\n"
          " disable) if [ \"$1\" = --now ]; then shift; rm -f \"${D:?}/${1:?}.active\"; fi; rm -f \"${D:?}/${1:?}.enabled\";;\n"
          "esac\nexit 0\n", f);
    fclose(f);
    chmod(M10_BASE "/tools/systemctl", 0755);
    f = fopen(M10_BASE "/tools/journalctl", "w");
    fputs("#!/bin/sh\n[ -f " M10_BASE "/sd/journal ] && cat " M10_BASE "/sd/journal\nexit 0\n", f);
    fclose(f);
    chmod(M10_BASE "/tools/journalctl", 0755);
    f = fopen(M10_BASE "/tools/tcp", "w"); // /proc/net/tcp: port 15432 listening
    fputs("  sl  local_address rem_address   st\n   0: 0100007F:3C48 00000000:0000 0A 00000000:00000000\n", f);
    fclose(f);
    setenv("FE_SYSTEMCTL", M10_BASE "/tools/systemctl", 1);
    setenv("FE_JOURNALCTL", M10_BASE "/tools/journalctl", 1);
    setenv("FE_PROC_NET_TCP", M10_BASE "/tools/tcp", 1);
    A.ipc_test = true;
}

static bool st_file_has(const char *path, const char *needle) {
    char buf[8192] = {0};
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    ssize_t r = read(fd, buf, sizeof buf - 1);
    close(fd);
    return r > 0 && strstr(buf, needle);
}

static ssize_t st_read_text(const char *path, char *out, size_t cap) {
    out[0] = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t r = read(fd, out, cap - 1);
    close(fd);
    out[r > 0 ? r : 0] = 0;
    return r > 0 ? r : 0;
}

static void st_cloud_settle(void) { // commands, then a status poll
    for (int k = 0; k < 400; k++) {
        st_frames(1);
        if (atomic_load(&g_cloud_job) == 0 && k > 3) {
            g_cloud_polled = 0;
            st_frames(1);
            while (atomic_load(&g_cloud_job)) { st_frames(1); SDL_Delay(2); }
            break;
        }
        SDL_Delay(2);
    }
}

static bool c_ipc_one(void) { return A.ipc_count >= 1; }
static bool c_devs(void) { return A.devs.n > 0 && !A.dev_scanning; }
static bool c_dev_idle(void) { return A.dev_busy == 0 && !A.dev_scanning; }
static bool st_log_has(const char *needle) {
    char buf[4096] = {0};
    int fd = open(M10_BASE "/tools/log", O_RDONLY);
    if (fd < 0) return false;
    ssize_t r = read(fd, buf, sizeof buf - 1);
    close(fd);
    return r > 0 && strstr(buf, needle);
}
static int st_place_row(int kind, const char *label) {
    for (int i = 0; i < A.nplaces; i++)
        if (A.places[i].kind == kind && (!label || !strcmp(A.places[i].label, label))) return i;
    return -1;
}
static bool c_ipc_two(void) { return A.ipc_count >= 2; }
static bool c_ipc_three(void) { return A.ipc_count >= 3; }
static bool c_ipc_four(void) { return A.ipc_count >= 4; }

static Tab *st_tab_at(const char *path) {
    for (int i = 0; i < A.ntabs; i++)
        if (!strcmp(A.tabs[i]->pane[0].path, path)) return A.tabs[i];
    return NULL;
}

static int run_selftest_m10(void) {
    st_wait();
    // --- command line + single instance ---
    ST_CHECK(g_ipc_fd >= 0 && st_exists("/tmp/fe-m10-run/file-explorer.sock"));
    ST_CHECK(!ipc_listen(ipc_handle)); // already serving
    char dir[PATH_MAX], sel[PATH_MAX];
    ST_CHECK(cli_target("file://" M10_BASE "/my%20file.txt", dir, sel) && !strcmp(dir, M10_BASE) && !strcmp(sel, "my file.txt"));
    ST_CHECK(cli_target(M10_BASE "/a/../b", dir, sel) && !strcmp(dir, M10_BASE "/b") && !sel[0]);
    ST_CHECK(!cli_target(M10_BASE "/missing", dir, sel));
    // a second launch hands its arguments over and exits
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = 0;
    int nt0 = A.ntabs;
    char *argv2[] = {exe, M10_BASE "/b", M10_BASE "/c.txt", NULL};
    pid_t pid;
    ST_CHECK(posix_spawn(&pid, exe, NULL, NULL, argv2, environ) == 0);
    ST_CHECK(st_wait_cond(c_ipc_one, 15000));
    st_wait();
    ST_CHECK(A.ntabs == nt0 + 2);
    Tab *tb = st_tab_at(M10_BASE "/b");
    ST_CHECK(tb && cur_tab() == tb); // the first one opened is active
    Tab *tc = NULL;
    for (int i = 0; i < A.ntabs; i++)
        if (A.tabs[i] != A.tabs[0] && !strcmp(A.tabs[i]->pane[0].path, M10_BASE)) tc = A.tabs[i];
    ST_CHECK(tc != NULL);
    if (tc) {
        tab_select(tab_index(tc));
        st_wait();
        ST_CHECK(!strcmp(st_cursor_name(active_pane()), "c.txt"));
    }
    // talking to it directly: a split
    ST_CHECK(ipc_send("split\t" M10_BASE "/a\nraise\t1\n"));
    ST_CHECK(st_wait_cond(c_ipc_two, 5000));
    st_wait();
    ST_CHECK(A.npanels == 2 && !strcmp(active_pane()->path, M10_BASE "/a"));
    st_shot("m10-ipc");
    // launched again without folders: a new window (in home), unless ours is hidden in the tray
    A.last_opened[0] = 0;
    ST_CHECK(ipc_send("raise\t1\n"));
    ST_CHECK(st_wait_cond(c_ipc_three, 5000));
    ST_CHECK(!strncmp(A.last_opened, "window:", 7));
    A.last_opened[0] = 0;
    SDL_HideWindow(A.win);
    ST_CHECK(ipc_send("raise\t1\n"));
    ST_CHECK(st_wait_cond(c_ipc_four, 5000));
    ST_CHECK(!A.last_opened[0]);
    SDL_ShowWindow(A.win);
    ipc_shutdown();
    ST_CHECK(!st_exists("/tmp/fe-m10-run/file-explorer.sock"));
    ST_CHECK(!ipc_send("raise\t1\n")); // nobody listening: a new launch would start its own window

    // --- searchable tabs popup (Ctrl+Shift+A) ---
    run_command(CMD_CLOSE_PANEL); // back to one panel
    st_wait();
    Tab *ta = tab_new(M10_BASE "/a", A.ntabs, false);
    ST_CHECK(ta != NULL);
    tab_new(M10_BASE "/b/..", A.ntabs, false); // another tab on the base folder
    st_wait();
    tab_close(tab_index(ta)); // "a" becomes recently closed
    st_key(SDLK_A, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
    ST_CHECK(A.pal_open && A.pal_te.buf[0] == '@');
    st_wait_pal();
    int ntab_items = 0, nclosed_items = 0;
    for (int i = 0; i < A.pal_n; i++) {
        ntab_items += g_pal_items[i].kind == PI_TAB;
        nclosed_items += g_pal_items[i].kind == PI_CLOSED;
    }
    ST_CHECK(ntab_items == A.ntabs && nclosed_items >= 1 && g_pal_items[0].kind == PI_TAB && g_pal_items[0].tab == A.cur);
    st_frames(1);
    st_shot("tabs-popup");
    // filter to the closed "a" tab and reopen it
    st_type("fe-m10-test/a");
    st_wait_pal();
    ST_CHECK(A.pal_n >= 1 && g_pal_items[0].kind == PI_CLOSED && !strcmp(g_pal_items[0].text, M10_BASE "/a"));
    int nclosed0 = A.nclosed, nt1 = A.ntabs;
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    st_wait();
    ST_CHECK(!A.pal_open && A.ntabs == nt1 + 1 && A.nclosed == nclosed0 - 1 && !strcmp(active_pane()->path, M10_BASE "/a"));
    // jump to an open tab by name
    pal_open_tabs();
    st_type("/b");
    st_wait_pal();
    ST_CHECK(A.pal_n >= 1 && g_pal_items[0].kind == PI_TAB && !strcmp(g_pal_items[0].text, M10_BASE "/b"));
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    ST_CHECK(!strcmp(active_pane()->path, M10_BASE "/b"));

    // --- drives and network places ---
    {
        char out[256];
        char *a1[] = {"sh", "-c", "echo hi; exit 3", NULL};
        ST_CHECK(run_capture(a1, out, sizeof out) == 3 && !strcmp(out, "hi"));
        char *a2[] = {"/nonexistent/tool", NULL};
        ST_CHECK(run_capture(a2, out, sizeof out) == -1);
        char lbl[64];
        gvfs_label("smb-share:server=nas,share=my%20media", lbl, sizeof lbl);
        ST_CHECK(!strcmp(lbl, "my media on nas"));
        gvfs_label("sftp:host=box,user=me", lbl, sizeof lbl);
        ST_CHECK(!strcmp(lbl, "me@box"));
    }
    ST_CHECK(st_wait_cond(c_devs, 5000));
    ST_CHECK(A.devs.n == 1 && !strcmp(A.devs.d[0].dev, "/dev/sdz1") && !strcmp(A.devs.d[0].label, "MY STICK") &&
             A.devs.d[0].removable && !A.devs.d[0].mount[0] && !strcmp(A.devs.d[0].disk, "sdz")); // EFI + swap hidden
    int ur = st_place_row(PK_UNMOUNTED, "MY STICK");
    int nr = st_place_row(PK_NETWORK, "media on nas");
    ST_CHECK(ur > A.nplaces_user && nr > ur && st_place_row(PK_NETHDR, NULL) > 0);
    st_frames(2);
    st_shot("devices");
    // click the unmounted stick: mounts it and opens it
    {
        Rect rr = place_rect(ur);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, rr.x + S(40), rr.y + rr.h * 0.5f, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, rr.x + S(40), rr.y + rr.h * 0.5f, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        ST_CHECK(st_wait_cond(c_dev_idle, 5000));
        st_wait();
        ST_CHECK(st_log_has("mount -b /dev/sdz1") && !strcmp(active_pane()->path, M10_BASE "/media/MY STICK"));
    }
    // a mounted removable drive: eject leaves the folder first, unmounts and powers off the disk
    {
        snprintf(A.devs.d[0].mount, sizeof A.devs.d[0].mount, "%s", M10_BASE "/media/MY STICK");
        init_places();
        int dr = -1;
        for (int i = A.nplaces_user; i < A.nplaces; i++)
            if (A.places[i].kind == PK_DRIVE && A.places[i].dev == 0) dr = i;
        ST_CHECK(dr > 0 && A.places[dr].removable);
        Rect rr = place_rect(dr);
        st_mouse(SDL_EVENT_MOUSE_MOTION, rr.x + S(40), rr.y + rr.h * 0.5f, 0, 0, SDL_KMOD_NONE);
        st_frames(1);
        ST_CHECK(A.places[dr].eject.w > 0);
        st_shot("eject-hover");
        Rect er = A.places[dr].eject;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, er.x + er.w * 0.5f, er.y + er.h * 0.5f, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, er.x + er.w * 0.5f, er.y + er.h * 0.5f, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        ST_CHECK(strcmp(active_pane()->path, M10_BASE "/media/MY STICK") != 0); // left before unmounting
        ST_CHECK(st_wait_cond(c_dev_idle, 5000));
        ST_CHECK(st_log_has("unmount -b /dev/sdz1") && st_log_has("power-off -b /dev/sdz"));
        ST_CHECK(!strcmp(A.toast, "Safe to remove"));
    }
    // discs and disk images: lsblk's "rom" (a disc in the drive) and loop devices with an image behind them (sysfs);
    // free loop devices and snaps stay hidden
    {
        DevList dl;
        mkdir(M10_BASE "/sys", 0755);
        mkdir(M10_BASE "/sys/loop3", 0755);
        mkdir(M10_BASE "/sys/loop3/loop", 0755);
        st_write_text(M10_BASE "/sys/loop3/loop/backing_file", "/home/me/Linux Mint.iso\n");
        setenv("FE_SYS_BLOCK", M10_BASE "/sys", 1);
        devices_parse("PATH=\"/dev/sr0\" PKNAME=\"\" TYPE=\"rom\" RM=\"1\" HOTPLUG=\"0\" MAJ:MIN=\"11:0\" LABEL=\"MOVIE\" FSTYPE=\"udf\" SIZE=\"4\" PARTTYPE=\"\" UUID=\"\"\n"
                      "PATH=\"/dev/sr1\" PKNAME=\"\" TYPE=\"rom\" RM=\"1\" HOTPLUG=\"0\" MAJ:MIN=\"11:1\" LABEL=\"\" FSTYPE=\"\" SIZE=\"0\" PARTTYPE=\"\" UUID=\"\"\n"
                      "PATH=\"/dev/loop3\" PKNAME=\"\" TYPE=\"loop\" RM=\"0\" HOTPLUG=\"0\" MAJ:MIN=\"7:3\" LABEL=\"Mint\" FSTYPE=\"iso9660\" SIZE=\"4\" PARTTYPE=\"\" UUID=\"\"\n"
                      "PATH=\"/dev/loop4\" PKNAME=\"\" TYPE=\"loop\" RM=\"0\" HOTPLUG=\"0\" MAJ:MIN=\"7:4\" LABEL=\"\" FSTYPE=\"ext4\" SIZE=\"4\" PARTTYPE=\"\" UUID=\"\"\n"
                      "PATH=\"/dev/loop5\" PKNAME=\"\" TYPE=\"loop\" RM=\"0\" HOTPLUG=\"0\" MAJ:MIN=\"7:5\" LABEL=\"\" FSTYPE=\"squashfs\" SIZE=\"4\" PARTTYPE=\"\" UUID=\"\"\n", &dl);
        unsetenv("FE_SYS_BLOCK");
        ST_CHECK(dl.n == 2 && !strcmp(dl.d[0].dev, "/dev/sr0") && dl.d[0].optical && dl.d[0].removable && !strcmp(dl.d[0].disk, "sr0") &&
                 !strcmp(dl.d[1].dev, "/dev/loop3") && !strcmp(dl.d[1].image, "/home/me/Linux Mint.iso") && dl.d[1].removable &&
                 !strcmp(dl.d[1].disk, "loop3"));
        // "Mount" on an image: a read-only loop device, its filesystem mounted and opened (the desktop's automounter got there
        // first: its mount); nothing to mount: the loop goes
        st_write(M10_BASE "/disc.iso", 10);
        pane_navigate(active_pane(), M10_BASE, true);
        st_wait();
        st_select(active_pane(), "disc.iso");
        open_entry(active_pane(), active_pane()->cursor, false); // (a double-click: mounted, not browsed or handed to an app)
        ST_CHECK(st_wait_cond(c_dev_idle, 5000));
        st_wait();
        ST_CHECK(st_log_has("loop-setup -r -f " M10_BASE "/disc.iso") && st_log_has("mount -b /dev/loop7") &&
                 !strcmp(active_pane()->path, M10_BASE "/media/IMG"));
        dev_start(DEV_IMAGE_MOUNT, M10_BASE "/empty.img", NULL, false);
        ST_CHECK(st_wait_cond(c_dev_idle, 5000));
        ST_CHECK(st_log_has("mount -b /dev/loop8") && st_log_has("loop-delete -b /dev/loop8") && strstr(A.toast, "Couldn't mount"));
        // eject: an image's loop device is let go, a disc's tray opens
        st_write_text(M10_BASE "/tools/eject", "#!/bin/sh\necho \"eject $*\" >> " M10_BASE "/tools/log\n");
        chmod(M10_BASE "/tools/eject", 0755);
        setenv("FE_EJECT", M10_BASE "/tools/eject", 1);
        dev_start(DEV_EJECT, "/dev/loop3", "loop3", false);
        ST_CHECK(st_wait_cond(c_dev_idle, 5000));
        dev_start(DEV_EJECT, "/dev/sr0", "sr0", false);
        ST_CHECK(st_wait_cond(c_dev_idle, 5000));
        ST_CHECK(st_log_has("loop-delete -b /dev/loop3") && st_log_has("eject /dev/sr0") && !st_log_has("power-off -b /dev/sr0"));
        unsetenv("FE_EJECT");
    }
    // disk health: a partition's / an encrypted volume's disk (sysfs), its temperature (hwmon), SMART from udisks;
    // a dot only when something's wrong, the details on hover and in the details panel
    {
        const char *sb = M10_BASE "/sysblock";
        char pth[PATH_MAX];
        mkdir(sb, 0755);
        snprintf(pth, sizeof pth, "%s/sdq", sb); mkdir(pth, 0755);
        snprintf(pth, sizeof pth, "%s/sdq/sdq1", sb); mkdir(pth, 0755);
        snprintf(pth, sizeof pth, "%s/sdq/sdq1/partition", sb); st_write_text(pth, "1\n");
        snprintf(pth, sizeof pth, "%s/sdq1", sb); ST_CHECK(symlink("sdq/sdq1", pth) == 0);
        snprintf(pth, sizeof pth, "%s/sdq/device", sb); mkdir(pth, 0755);
        snprintf(pth, sizeof pth, "%s/sdq/device/model", sb); st_write_text(pth, "Spinning Disk   \n");
        snprintf(pth, sizeof pth, "%s/sdq/device/hwmon", sb); mkdir(pth, 0755);
        snprintf(pth, sizeof pth, "%s/sdq/device/hwmon/hwmon5", sb); mkdir(pth, 0755);
        snprintf(pth, sizeof pth, "%s/sdq/device/hwmon/hwmon5/temp1_input", sb); st_write_text(pth, "41000\n"); // SATA (drivetemp)
        snprintf(pth, sizeof pth, "%s/dm-0", sb); mkdir(pth, 0755);
        snprintf(pth, sizeof pth, "%s/dm-0/slaves", sb); mkdir(pth, 0755);
        snprintf(pth, sizeof pth, "%s/dm-0/slaves/nvme9n1p2", sb); mkdir(pth, 0755);
        snprintf(pth, sizeof pth, "%s/nvme9n1", sb); mkdir(pth, 0755);
        snprintf(pth, sizeof pth, "%s/nvme9n1/nvme9n1p2", sb); mkdir(pth, 0755);
        snprintf(pth, sizeof pth, "%s/nvme9n1/nvme9n1p2/partition", sb); st_write_text(pth, "2\n");
        snprintf(pth, sizeof pth, "%s/nvme9n1p2", sb); ST_CHECK(symlink("nvme9n1/nvme9n1p2", pth) == 0);
        snprintf(pth, sizeof pth, "%s/nvme9n1/device", sb); mkdir(pth, 0755);
        snprintf(pth, sizeof pth, "%s/nvme9n1/device/hwmon3", sb); mkdir(pth, 0755);
        snprintf(pth, sizeof pth, "%s/nvme9n1/device/hwmon3/temp1_input", sb); st_write_text(pth, "74500\n"); // NVMe: hot
        st_write_text(M10_BASE "/tools/udisks-dump",
                      "/org/freedesktop/UDisks2/block_devices/sdq:\n  org.freedesktop.UDisks2.Block:\n    Drive:   '/org/freedesktop/UDisks2/drives/Spin_1'\n"
                      "/org/freedesktop/UDisks2/block_devices/nvme9n1:\n  org.freedesktop.UDisks2.Block:\n    Drive:   '/org/freedesktop/UDisks2/drives/Fast_2'\n"
                      "/org/freedesktop/UDisks2/drives/Spin_1:\n  org.freedesktop.UDisks2.Drive:\n    Model:   Spinner 4000\n"
                      "  org.freedesktop.UDisks2.Drive.Ata:\n    SmartFailing:   true\n    SmartPowerOnSeconds:   36000\n    SmartTemperature:   0.000000\n"
                      "/org/freedesktop/UDisks2/drives/Fast_2:\n  org.freedesktop.UDisks2.Drive:\n    Model:   Fast NVMe\n"
                      "  org.freedesktop.UDisks2.NVMe.Controller:\n    SmartCriticalWarning:   \n    SmartPowerOnHours:   1234\n    SmartTemperature:   330\n");
        setenv("FE_SYS_CLASS_BLOCK", sb, 1);
        char dk[32] = "";
        ST_CHECK(disk_of("sdq1", dk, sizeof dk) && !strcmp(dk, "sdq") && disk_of("dm-0", dk, sizeof dk) && !strcmp(dk, "nvme9n1") &&
                 !disk_of("loop3", dk, sizeof dk) && !disk_of("zram0", dk, sizeof dk));
        ST_CHECK(hwmon_temp("sdq") == 410 && hwmon_temp("nvme9n1") == 745 && hwmon_temp("nope") == DH_NO_TEMP);
        DiskHealthList *L = xcalloc(1, sizeof *L);
        snprintf(L->map[0].dev, sizeof L->map[0].dev, "sdq1");
        snprintf(L->map[1].dev, sizeof L->map[1].dev, "dm-0");
        L->nmap = 2;
        g_dh.n = 0;
        disk_health_job(L); // (posts the result to the main thread)
        for (int k = 0; k < 200 && g_dh.n < 2; k++) { st_frames(1); SDL_Delay(5); }
        unsetenv("FE_SYS_CLASS_BLOCK");
        ST_CHECK(g_dh.n == 2 && g_dh.nmap == 2 && g_dh.map[0].di == 0 && g_dh.map[1].di == 1);
        char what[128];
        const DiskHealth *s0 = &g_dh.d[0], *s1 = &g_dh.d[1];
        ST_CHECK(!strcmp(s0->disk, "sdq") && !strcmp(s0->model, "Spinner 4000") && s0->temp == 410 && s0->failing && s0->hours == 10 &&
                 disk_health_level(s0, what, sizeof what) == 2 && strstr(what, "Failing"));
        ST_CHECK(!strcmp(s1->disk, "nvme9n1") && s1->temp == 745 && !s1->failing && s1->hours == 1234 && s1->smart &&
                 disk_health_level(s1, what, sizeof what) == 1 && !strcmp(what, "Running hot")); // (74.5 C: over 70 for NVMe)
        char line[256];
        disk_health_line(s1, line, sizeof line);
        ST_CHECK(!strncmp(line, "Fast NVMe\nTemperature: 75 \xC2\xB0" "C\nHealth: Running hot\nPowered on: 1,234 hours", 200));
        // a sidebar drive on that disk: found by its mount, drawn with the dot
        int nd = A.devs.n;
        if (nd < MAX_BLOCKDEVS) {
            mkdir(M10_BASE "/media/Q", 0755);
            A.devs.d[nd] = (BlockDev){.dev = "/dev/sdq1", .disk = "sdq", .fstype = "ext4", .label = "Q", .size = 1000, .removable = true};
            snprintf(A.devs.d[nd].mount, sizeof A.devs.d[nd].mount, "%s", M10_BASE "/media/Q");
            A.devs.n++;
            init_places();
            ST_CHECK(disk_health_for_mount(M10_BASE "/media/Q") == s0 && !disk_health_for_mount(M10_BASE "/nowhere"));
            st_frames(2);
            st_shot("disk-health");
            A.devs.n = nd;
            init_places();
        }
        g_dh.n = g_dh.nmap = 0;
    }
    // Android over ADB (only with adb + adbfs): phones from `adb devices -l`, connecting runs adbfs on our folder
    // (ANDROID_SERIAL picks the phone), a mount whose phone went away is let go
    {
        const char *mf = M10_BASE "/tools/adb-mounts";
        st_write_text(mf, "");
        st_write_text(M10_BASE "/tools/adb", "#!/bin/sh\necho \"adb $*\" >> " M10_BASE "/tools/log\n"
                      "printf 'List of devices attached\\nR58M123  device usb:1-2 product:p model:Pixel_7 device:panther transport_id:1\\n"
                      "ZX9  unauthorized usb:1-3 transport_id:2\\nemulator-5554  offline\\n'\n");
        st_write_text(M10_BASE "/tools/adbfs", "#!/bin/sh\necho \"adbfs $ANDROID_SERIAL $*\" >> " M10_BASE "/tools/log\n"
                      "echo \"adbfs $1 fuse.adbfs rw 0 0\" >> " M10_BASE "/tools/adb-mounts\n");
        st_write_text(M10_BASE "/tools/fusermount3", "#!/bin/sh\necho \"fusermount3 $*\" >> " M10_BASE "/tools/log\n");
        chmod(M10_BASE "/tools/adb", 0755);
        chmod(M10_BASE "/tools/adbfs", 0755);
        chmod(M10_BASE "/tools/fusermount3", 0755);
        setenv("FE_ADB", M10_BASE "/tools/adb", 1);
        setenv("FE_ADBFS", M10_BASE "/tools/adbfs", 1);
        setenv("FE_ADB_DIR", M10_BASE "/adb", 1);
        setenv("FE_ADB_MOUNTS", mf, 1);
        setenv("FE_FUSERMOUNT", M10_BASE "/tools/fusermount3", 1);
        mkdir(M10_BASE "/adb", 0755);
        mkdir(M10_BASE "/adb/GONE1", 0755); // a phone that was unplugged while connected
        st_write_text(mf, "adbfs " M10_BASE "/adb/GONE1 fuse.adbfs rw 0 0\n");
        DevList *dl = xcalloc(1, sizeof *dl);
        phones_scan_adb(dl);
        ST_CHECK(dl->nph == 2 && dl->ph[0].kind == 'd' && !strcmp(dl->ph[0].serial, "R58M123") && !strcmp(dl->ph[0].name, "Pixel 7 (ADB)") &&
                 !dl->ph[0].need[0] && !dl->ph[0].mount[0] && !strcmp(dl->ph[1].need, "allow") && !strcmp(dl->ph[1].name, "Android (ADB)"));
        ST_CHECK(st_log_has("adb devices -l") && st_log_has("fusermount3 -u -z " M10_BASE "/adb/GONE1"));
        // connecting: adbfs for that phone on its folder, which then opens
        dev_start_phone(&dl->ph[0]);
        ST_CHECK(st_wait_cond(c_dev_idle, 5000));
        st_wait();
        ST_CHECK(st_log_has("adbfs R58M123 " M10_BASE "/adb/R58M123") && !strcmp(active_pane()->path, M10_BASE "/adb/R58M123"));
        dl->nph = 0;
        phones_scan_adb(dl);
        ST_CHECK(dl->nph == 2 && !strcmp(dl->ph[0].mount, M10_BASE "/adb/R58M123")); // now connected
        free(dl);
        pane_navigate(active_pane(), M10_BASE, true);
        st_wait();
        unsetenv("FE_ADB"); unsetenv("FE_ADBFS"); unsetenv("FE_ADB_DIR"); unsetenv("FE_ADB_MOUNTS"); unsetenv("FE_FUSERMOUNT");
    }
    // errors are shown
    dev_start(DEV_MOUNT, "/dev/fail", NULL, true);
    ST_CHECK(st_wait_cond(c_dev_idle, 5000));
    ST_CHECK(strstr(A.toast, "Couldn't mount") && strstr(A.toast, "wrong fs type"));
    { // a Windows volume that wasn't shut down cleanly: explained, offered read-only (udisksctl ... -o ro)
        int nd = A.devs.n;
        if (nd < MAX_BLOCKDEVS) {
            A.devs.d[nd] = (BlockDev){.dev = "/dev/dirty", .disk = "nvme9", .fstype = "ntfs", .label = "Windows", .size = 1000};
            A.devs.n++;
            dev_start(DEV_MOUNT, "/dev/dirty", NULL, true);
            for (int k = 0; k < 400 && !A.dlg.open; k++) { st_frames(1); SDL_Delay(5); }
            ST_CHECK(A.dlg.open && strstr(A.dlg.title, "Windows") && strstr(A.dlg.msg, "Fast Startup") && A.dlg.icon == DI_WARN);
            st_shot("ntfs-dirty");
            if (A.dlg.open) dialog_choose(0);
            ST_CHECK(st_wait_cond(c_dev_idle, 5000));
            ST_CHECK(st_log_has("mount -b /dev/dirty -o ro"));
            A.devs.n = nd;
        }
        // long messages wrap (and the udisks noise is gone)
        char out[600] = "Error mounting /dev/x at /run/media/me/X: wrong fs type, bad option, bad superblock on /dev/x, missing codepage "
                        "or helper program, or other error (udisks-error-quark, 0)";
        udisks_tidy(out);
        ST_CHECK(!strncmp(out, "wrong fs type", 13) && !strstr(out, "quark"));
        toast("Couldn't mount: %s", out);
        st_frames(2);
        st_shot("toast-long");
    }
    // network: a URI in the path bar opens Connect to server prefilled; Enter mounts it with GVfs and opens it
    Pane *p = active_pane();
    begin_edit(p, "sftp://me@box/");
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    ST_CHECK(g_sheet.kind == SHEET_CONNECT && !strcmp(g_sheet.cn[CN_HOST].buf, "box") && !strcmp(g_sheet.cn[CN_USER].buf, "me") &&
             !g_sheet.cn_remember && !A.edit_pane);
    st_key(SDLK_RETURN, SDL_KMOD_NONE);
    ST_CHECK(st_wait_cond(c_dev_idle, 5000));
    st_wait();
    ST_CHECK(st_log_has("gio mount sftp://me@box/") && !strcmp(active_pane()->path, M10_BASE "/gvfs/sftp:host=box,user=me"));
    ST_CHECK(st_place_row(PK_NETWORK, "me@box") > 0);
    // disconnect from the menu
    {
        int r2 = st_place_row(PK_NETWORK, "me@box");
        menu_open_place(r2, A.r_sidebar.x + S(40), S(200));
        int dc = -1;
        for (int i = 0; i < A.menu.n; i++)
            if (A.menu.items[i].act == ACT_NET_DISCONNECT) dc = i;
        ST_CHECK(dc >= 0);
        menu_activate(dc);
        ST_CHECK(st_wait_cond(c_dev_idle, 5000));
        ST_CHECK(st_log_has("gio mount -u " M10_BASE "/gvfs/sftp:host=box,user=me") && !strcmp(A.toast, "Disconnected"));
        char mux[4096] = "";
        st_read_text(M10_BASE "/tools/ssh-mux", mux, sizeof mux);
        ST_CHECK(strstr(mux, "-O check -- me@box") && strstr(mux, "-O exit -- me@box")); // its shared ssh connection too
    }

    { // mount tables write spaces, tabs, newlines and backslashes as octal escapes: decoded once, malformed ones kept
        static const char *cases[][2] = {
            {"/mnt/my\\040share", "/mnt/my share"}, {"a\\011b\\012c\\134d", "a\tb\nc\\d"}, {"/literal\\134040", "/literal\\040"},
            {"/x\\043y", "/x#y"}, {"\\000\\777\\04\\", "\\000\\777\\04\\"}, {"end\\0", "end\\0"}, {"end\\04", "end\\04"},
            {"\\", "\\"}, {"\\8\\400\\09a", "\\8\\400\\09a"}, {"", ""},
        };
        for (size_t i = 0; i < ARRAY_COUNT(cases); i++) {
            char out[64];
            mount_unescape(cases[i][0], out, sizeof out);
            ST_CHECK(!strcmp(out, cases[i][1]));
        }
        char tiny[6];
        mount_unescape("ab\\040cdefgh", tiny, sizeof tiny); // cut to the buffer, still terminated
        ST_CHECK(!strcmp(tiny, "ab cd"));
        char inplace[] = "/mnt/a\\040b";
        mount_unescape(inplace, inplace, sizeof inplace);
        ST_CHECK(!strcmp(inplace, "/mnt/a b"));
        // network mounts with escaped fields: the folder as it is on disk, sshfs's source readable
        st_write_text(M10_BASE "/mounts", "//nas/my\\040share " M10_BASE "/smb\\040share cifs rw 0 0\n"
                                          "me@box:/home/me/my\\040docs " M10_BASE "/ssh\\011tab fuse.sshfs rw 0 0\n"
                                          "nas:/x " M10_BASE "/literal\\134040 nfs rw 0 0\n"
                                          "/dev/sda1 " M10_BASE "/not\\040network ext4 rw 0 0\n");
        setenv("FE_NET_MOUNTS", M10_BASE "/mounts", 1);
        NetMount nm[16];
        int nn = network_mounts(nm, 16), found = 0;
        unsetenv("FE_NET_MOUNTS");
        for (int i = 0; i < nn; i++) {
            found += !strcmp(nm[i].path, M10_BASE "/smb share") && !strcmp(nm[i].label, "//nas/my share");
            found += !strcmp(nm[i].path, M10_BASE "/ssh\ttab") && !strcmp(nm[i].label, "me@box:/home/me/my docs") && nm[i].sshfs;
            ST_CHECK(nm[i].sshfs == !strcmp(nm[i].path, M10_BASE "/ssh\ttab"));
            found += !strcmp(nm[i].path, M10_BASE "/literal\\040");
            ST_CHECK(!strstr(nm[i].path, "not network") && !strstr(nm[i].path, "\\011"));
        }
        ST_CHECK(found == 3);
        unlink(M10_BASE "/mounts");
    }

    // phones and cameras on a cable: what the volume monitors list, matched with their folders in the FUSE bridge
    {
        DevList *L = xcalloc(1, sizeof *L);
        phones_parse(ST_GIO_VOLUMES, L);
        ST_CHECK(L->nph == 5 && !strcmp(L->ph[0].name, "Pixel 7") && L->ph[0].kind == 'm' &&
                 !strcmp(L->ph[0].uri, "mtp://Google_Pixel_7_2A111FDH200ABC/") && !strcmp(L->ph[1].name, "Al's iPhone") &&
                 L->ph[1].kind == 'a' && !strcmp(L->ph[2].uri, "afc://00008110-001A:3/")); // the drive and sftp mount aren't phones
        char k1[400], k2[400], uri[512], nm[96];
        ST_CHECK(phone_key("afc:host=00008110-001A,port=3", k1, sizeof k1, uri, sizeof uri) &&
                 phone_key("afc://00008110-001A:3/", k2, sizeof k2, NULL, 0) && !strcmp(k1, k2) && !strcmp(uri, "afc://00008110-001A:3/"));
        ST_CHECK(phone_key("gphoto2:host=%5Busb%3A001%2C007%5D", k1, sizeof k1, NULL, 0) && !strcmp(k1, "c|[usb:001,007]|") &&
                 phone_key("gphoto2://%5Busb%3A001%2C007%5D/", k2, sizeof k2, NULL, 0) && !strcmp(k1, k2));
        ST_CHECK(!phone_key("sftp:host=box,user=me", k1, sizeof k1, NULL, 0) && !phone_kind("smb-share:server=nas"));
        phone_default_name("m|Google_Pixel_7_2A111FDH200ABC|", nm, sizeof nm);
        ST_CHECK(!strcmp(nm, "Google Pixel 7"));
        phone_default_name("a|00008110-001A|3", nm, sizeof nm);
        ST_CHECK(!strcmp(nm, "Documents on iPhone"));
        // an iPhone on the bus without GVfs's AFC backend: listed, says what to install
        mkdir(M10_BASE "/usb", 0755);
        mkdir(M10_BASE "/usb/1-2", 0755);
        mkdir(M10_BASE "/libexec", 0755);
        FILE *f = fopen(M10_BASE "/usb/1-2/idVendor", "w"); fputs("05ac\n", f); fclose(f);
        f = fopen(M10_BASE "/usb/1-2/idProduct", "w"); fputs("12a8\n", f); fclose(f);
        setenv("FE_SYS_USB", M10_BASE "/usb", 1);
        setenv("FE_NO_OWN_PHONES", "1", 1); // GVfs on its own here (ours is below)
        setenv("FE_GVFS_LIBEXEC", M10_BASE "/libexec", 1);
        ST_CHECK(!gvfs_backend_installed("afc"));
        f = fopen(M10_BASE "/tools/volumes2", "w"); fputs("Volume(0): Pixel 7\n  activation_root=mtp://Pixel/\n", f); fclose(f);
        rename(M10_BASE "/tools/volumes", M10_BASE "/tools/volumes.off");
        rename(M10_BASE "/tools/volumes2", M10_BASE "/tools/volumes");
        phones_scan(L);
        // the phone (not connected), the camera another program connected (named by its kind), the iPhone
        ST_CHECK(L->nph == 3 && L->ph[0].kind == 'm' && !L->ph[0].mount[0] && L->ph[1].kind == 'c' && !strcmp(L->ph[1].name, "Camera") &&
                 L->ph[1].mount[0] && !strcmp(L->ph[1].uri, "gphoto2://%5Busb%3A001%2C007%5D/") && L->ph[2].kind == 'a' &&
                 !strcmp(L->ph[2].need, "gvfs-afc") && !L->ph[2].uri[0]);
        // only the apps' Documents listed (no AFC media entry): the PTP side stays, for the photos
        f = fopen(M10_BASE "/tools/volumes", "w");
        fputs("Volume(0): Documents on Al's iPhone\n  activation_root=afc://00008110-001A:3/\n"
              "Volume(1): iPhone\n  activation_root=gphoto2://Apple_Inc._iPhone_00008110001A/\n", f);
        fclose(f);
        phones_scan(L);
        ST_CHECK(L->nph >= 2 && L->ph[0].kind == 'a' && L->ph[1].kind == 'c' && !strcmp(L->ph[1].name, "Photos on iPhone"));
        rename(M10_BASE "/tools/volumes.off", M10_BASE "/tools/volumes");
        free(L);
        unsetenv("FE_SYS_USB");
        unsetenv("FE_NO_OWN_PHONES");
        unsetenv("FE_GVFS_LIBEXEC");
        char hint[300];
        phone_error_hint('a', "Unhandled Lockdown error: pairing dialog response pending", hint, sizeof hint);
        ST_CHECK(strstr(hint, "tap Trust") != NULL);
        phone_error_hint('a', "Unhandled Lockdown error (-8)", hint, sizeof hint);
        ST_CHECK(strstr(hint, "usbmuxd") != NULL);
        phone_error_hint('m', "Unable to open MTP device \"001,009\"", hint, sizeof hint);
        ST_CHECK(strstr(hint, "File transfer") != NULL);
    }
    devices_refresh();
    ST_CHECK(st_wait_cond(c_dev_idle, 5000));
    {
        int pr = st_place_row(PK_PHONE, "Pixel 7"), cr = st_place_row(PK_PHONE, "Camera"), dr = st_place_row(PK_PHONE, "Documents on Al's iPhone");
        ST_CHECK(st_place_row(PK_PHONE, "iPhone") < 0 && st_place_row(PK_PHONE, "Photos on iPhone") < 0); // its PTP side: covered by AFC
        ST_CHECK(pr > st_place_row(PK_DEVICES, NULL) && pr < st_place_row(PK_NETHDR, NULL) && !A.places[pr].path[0] && dr > 0);
        ST_CHECK(cr > 0 && A.places[cr].removable && strstr(A.places[cr].path, "gphoto2:host=")); // connected elsewhere: shown as such
        for (int i = 0; i < A.nplaces; i++) ST_CHECK(A.places[i].kind != PK_NETWORK || !strstr(A.places[i].path, "gphoto2")); // not a network place
        st_frames(2);
        st_shot("phones");
        // click the phone: connects (gio mount URI) and opens its folder
        Rect rr = place_rect(pr);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, rr.x + S(40), rr.y + rr.h * 0.5f, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, rr.x + S(40), rr.y + rr.h * 0.5f, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        ST_CHECK(st_wait_cond(c_dev_idle, 5000));
        st_wait();
        const char *pdir = M10_BASE "/gvfs/mtp:host=Google_Pixel_7_2A111FDH200ABC";
        ST_CHECK(st_log_has("gio mount mtp://Google_Pixel_7_2A111FDH200ABC/") && !strcmp(active_pane()->path, pdir));
        devices_refresh();
        ST_CHECK(st_wait_cond(c_dev_idle, 5000));
        pr = st_place_row(PK_PHONE, "Pixel 7");
        ST_CHECK(pr > 0 && !strcmp(A.places[pr].path, pdir) && A.places[pr].removable && path_on_phone(pdir));
        // no trash on a phone: Delete asks to delete for good
        char fp[PATH_MAX];
        snprintf(fp, sizeof fp, "%s/IMG_0001.jpg", pdir);
        st_write(fp, 10);
        Pane *pp = active_pane();
        pane_navigate(pp, pdir, false);
        st_wait();
        for (u32 i = 0; i < pp->count; i++) sel_set(pp, i, true);
        do_action(pp, ACT_TRASH);
        ST_CHECK(A.dlg.open && A.dlg.danger && strstr(A.dlg.msg, "no trash"));
        if (A.dlg.open) dialog_choose(A.dlg.cancel);
        ST_CHECK(st_exists(fp));
        // a locked iPhone: what to do about it
        PhoneDev lk = {.name = "Locked iPhone", .uri = "afc://locked/", .kind = 'a'};
        dev_start_phone(&lk);
        ST_CHECK(st_wait_cond(c_dev_idle, 5000));
        ST_CHECK(strstr(A.toast, "Couldn't open the iPhone") && strstr(A.toast, "tap Trust"));
        // disconnect from the menu: leaves the folder, "Safe to unplug"
        menu_open_place(pr, A.r_sidebar.x + S(40), S(200));
        int dc = -1;
        for (int i = 0; i < A.menu.n; i++)
            if (A.menu.items[i].act == ACT_NET_DISCONNECT) dc = i;
        ST_CHECK(dc >= 0);
        if (dc >= 0) menu_activate(dc);
        ST_CHECK(strcmp(active_pane()->path, pdir) != 0);
        ST_CHECK(st_wait_cond(c_dev_idle, 5000));
        ST_CHECK(st_log_has("gio mount -u /tmp/fe-m10-test/gvfs/mtp:host=Google_Pixel_7_2A111FDH200ABC") && !strcmp(A.toast, "Safe to unplug"));
        st_rmrf(pdir); // unplugged: the rest of the suite sees no phones
        st_rmrf(M10_BASE "/gvfs/gphoto2:host=%5Busb%3A001%2C007%5D");
        unlink(M10_BASE "/tools/volumes");
        devices_refresh();
        ST_CHECK(st_wait_cond(c_dev_idle, 5000) && st_place_row(PK_PHONE, NULL) < 0);
    }

    // our own connector: devices on a made-up bus; the helper (a stand-in here) says what the user has to do, then
    // where the folder is; when it can't have the device (busy), GVfs opens it instead
    {
        const char *u = M10_BASE "/usb2";
        mkdir(u, 0755);
        const char *devs[][5] = {{"1-7", "05ac", "12a8", "00008140000123C6E0C13001C", "iPhone"}, {"1-8", "18d1", "4ee1", "PIXSER123456", "Pixel 8"}};
        for (int i = 0; i < 2; i++) {
            char p[PATH_MAX];
            snprintf(p, sizeof p, "%s/%s", u, devs[i][0]);
            mkdir(p, 0755);
            const char *at[][2] = {{"idVendor", devs[i][1]}, {"idProduct", devs[i][2]}, {"serial", devs[i][3]}, {"product", devs[i][4]}, {"busnum", "1"}, {"devnum", "5"}};
            for (int k = 0; k < 6; k++) { snprintf(p, sizeof p, "%s/%s/%s", u, devs[i][0], at[k][0]); st_write_text(p, at[k][1]); }
        }
        mkdir(M10_BASE "/usb2/1-8:1.0", 0755);
        st_write_text(M10_BASE "/usb2/1-8:1.0/bInterfaceClass", "06");
        st_write_text(M10_BASE "/usb2/1-8:1.0/bInterfaceSubClass", "01");
        st_write_text(M10_BASE "/usb2/1-8:1.0/interface", "MTP");
        st_write_text(M10_BASE "/tools/volumes", "Volume(0): Pixel 8\n  activation_root=mtp://Google_Pixel_8_PIXSER123456/\n");
        st_write_text(M10_BASE "/tools/devhelper", "#!/bin/sh\necho \"helper $*\" >> " M10_BASE "/tools/log\n"
                                                   "case \"$2\" in 1-7) echo 'wait trust'; sleep 0.4; mkdir -p \"$3/Al's iPhone\"; echo \"ok $3/Al's iPhone\";;\n"
                                                   "*) echo 'error busy: another program is using the device';; esac\n");
        chmod(M10_BASE "/tools/devhelper", 0755);
        setenv("FE_SYS_USB", u, 1);
        setenv("FE_DEVICE_HELPER", M10_BASE "/tools/devhelper", 1);
        setenv("FE_DEVICES_DIR", M10_BASE "/devices", 1);
        devices_refresh();
        ST_CHECK(st_wait_cond(c_dev_idle, 5000));
        int ir = st_place_row(PK_PHONE, "iPhone"), pr = st_place_row(PK_PHONE, "Pixel 8");
        ST_CHECK(ir > 0 && pr > 0 && !A.places[ir].path[0] && A.devs.ph[A.places[ir].phone].own && A.devs.ph[A.places[pr].phone].own);
        int rows = 0;
        for (int i = 0; i < A.nplaces; i++) rows += A.places[i].kind == PK_PHONE;
        ST_CHECK(rows == 2); // GVfs's entry for the Pixel folded into ours
        Rect rr = place_rect(ir);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, rr.x + S(40), rr.y + rr.h * 0.5f, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, rr.x + S(40), rr.y + rr.h * 0.5f, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        bool asked = false;
        for (int k = 0; k < 400 && !c_dev_idle(); k++) { st_frames(1); SDL_Delay(5); asked |= strstr(A.toast, "tap Trust") != NULL; }
        st_wait();
        ST_CHECK(asked && st_log_has("helper --device-mount 1-7 " M10_BASE "/devices") && !strcmp(active_pane()->path, M10_BASE "/devices/Al's iPhone"));
        { // it drops off USB: the tab leaves its folder; it's back a moment later: connected again, the tab returns
            mkdir(M10_BASE "/devices/Al's iPhone/DCIM", 0755);
            pane_navigate(active_pane(), M10_BASE "/devices/Al's iPhone/DCIM", true);
            st_wait();
            DevList *was = xmalloc(sizeof *was), *now = xmalloc(sizeof *now);
            *was = A.devs;
            *now = A.devs;
            int pi = A.places[ir].phone;
            snprintf(was->ph[pi].mount, sizeof was->ph[pi].mount, M10_BASE "/devices/Al's iPhone");
            dev_phones_follow(was, now);
            ST_CHECK(strncmp(active_pane()->path, M10_BASE "/devices/", strlen(M10_BASE "/devices/")) != 0 && strstr(A.toast, "disconnected"));
            ST_CHECK(!strcmp(A.dev_connecting, "1-7")); // the same device is still listed: reconnecting
            ST_CHECK(st_wait_cond(c_dev_idle, 5000));
            st_wait();
            ST_CHECK(!strcmp(active_pane()->path, M10_BASE "/devices/Al's iPhone/DCIM") && !strcmp(A.toast, "Reconnected"));
            free(was);
            free(now);
        }
        pr = st_place_row(PK_PHONE, "Pixel 8");
        place_dev_action(pr, ACT_DEV_MOUNT);
        for (int k = 0; k < 400 && !st_log_has("gio mount mtp://Google_Pixel_8"); k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(st_wait_cond(c_dev_idle, 5000));
        st_wait();
        ST_CHECK(st_log_has("helper --device-mount 1-8") && st_log_has("gio mount mtp://Google_Pixel_8_PIXSER123456/") &&
                 !strcmp(active_pane()->path, M10_BASE "/gvfs/mtp:host=Google_Pixel_8_PIXSER123456"));
        unsetenv("FE_SYS_USB");
        unsetenv("FE_DEVICE_HELPER");
        unsetenv("FE_DEVICES_DIR");
        unlink(M10_BASE "/tools/volumes");
        st_rmrf(M10_BASE "/gvfs/mtp:host=Google_Pixel_8_PIXSER123456");
        pane_navigate(active_pane(), M10_BASE, true);
        st_wait();
        devices_refresh();
        ST_CHECK(st_wait_cond(c_dev_idle, 5000) && st_place_row(PK_PHONE, NULL) < 0);
    }

    // first frames after idling (with glyph uploads mid-frame) still draw every row; the Vulkan bug this guards
    // against (fixed in atlas_upload) depends on GPU timing, so this is a sanity check rather than a reliable repro
    {
        Pane *gp = active_pane();
        pane_navigate(gp, M10_BASE, true);
        st_wait();
        int bad = 0;
        for (int k = 0; k < 8; k++) {
            SDL_Delay(300);
            if (k % 2 == 0) atlas_reset();
            layout();
            f32 cy = gp->ly.list.y + (2 + k % 4 + 0.5f) * gp->ly.row_h, cx = gp->ly.col_name + S(30);
            SDL_MouseButtonEvent mb;
            SDL_zero(mb);
            mb.button = SDL_BUTTON_LEFT;
            mb.clicks = 1;
            mb.x = cx;
            mb.y = cy;
            on_mouse_down(&mb);
            char fn[64];
            snprintf(fn, sizeof fn, "/tmp/fe-m10-run/glyph%d.bmp", k);
            snprintf(A.shot_once, sizeof A.shot_once, "%s", fn);
            frame();
            on_mouse_up();
            frame();
            SDL_Surface *im = SDL_LoadBMP(fn);
            if (!im) { bad++; continue; }
            int ry = (int)(gp->ly.list.y + gp->ly.row_h * 0.5f), bright = 0;
            for (int x = (int)gp->ly.col_name; x < (int)gp->ly.name_end && x < im->w; x++) {
                Uint8 r, g, b2;
                SDL_ReadSurfacePixel(im, x, ry, &r, &g, &b2, NULL);
                bright += r + g + b2 > 400;
            }
            SDL_DestroySurface(im);
            if (!bright) bad++;
        }
        if (bad) fprintf(stderr, "glyph upload test: %d bad frames\n", bad);
        ST_CHECK(bad == 0);
    }

    // a click with a little hand movement selects, it doesn't start a drag (the drag badge used to flash)
    {
        Pane *cp = active_pane();
        pane_navigate(cp, M10_BASE, true);
        st_wait();
        f32 cy = st_row_y(cp, "c.txt"), cx = cp->ly.col_name + S(30);
        ST_CHECK(cy > 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, cx, cy, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        st_mouse(SDL_EVENT_MOUSE_MOTION, cx + S(8), cy + S(2), 0, 0, SDL_KMOD_NONE);
        ST_CHECK(A.drag != DRAG_ITEMS);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, cx + S(8), cy + S(2), SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        ST_CHECK(cp->sel_count == 1 && !strcmp(st_cursor_name(cp), "c.txt"));
        // a real drag still starts
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, cx, cy, SDL_BUTTON_LEFT, 1, SDL_KMOD_NONE);
        st_mouse(SDL_EVENT_MOUSE_MOTION, cx + S(30), cy + S(30), 0, 0, SDL_KMOD_NONE);
        ST_CHECK(A.drag == DRAG_ITEMS);
        drag_cancel();
    }

    // --- robustness ---
    p = active_pane();
    // unreadable folder: an error message, nothing breaks, Back works
    mkdir(M10_BASE "/locked", 0755);
    chmod(M10_BASE "/locked", 0);
    pane_navigate(p, M10_BASE "/locked", true);
    st_wait();
    if (geteuid() != 0) ST_CHECK(p->L && atomic_load(&p->L->state) == LST_ERROR && p->L->err == EACCES);
    st_frames(2);
    st_shot("locked");
    pane_history(p, -1);
    st_wait();
    ST_CHECK(strcmp(p->path, M10_BASE "/locked") != 0);
    chmod(M10_BASE "/locked", 0755);
    // odd names: invalid UTF-8, newline, tab, spaces, 255 bytes, emoji, right to left
    mkdir(M10_BASE "/odd", 0755);
    {
        char longname[256];
        memset(longname, 'x', 250);
        snprintf(longname + 250, 6, ".text");
        const char *names[] = {"bad\xff\xfe" "name.txt", "new\nline.txt", "tab\there", " lead and trail ", longname,
                               "\xF0\x9F\x93\x81 folder \xE6\x96\x87\xE5\xAD\x97",
                               "\xD9\x81\xD8\xA7\xDB\x8C\xD9\x84 \xDB\xB1\xDB\xB2.txt"};
        for (size_t i = 0; i < ARRAY_COUNT(names); i++) {
            char full[PATH_MAX];
            path_join(full, sizeof full, M10_BASE "/odd", names[i]);
            st_write(full, 3);
        }
        mkdir(M10_BASE "/odd/dir\nwith newline", 0755);
        pane_navigate(p, M10_BASE "/odd", true);
        st_wait();
        ST_CHECK(p->count == ARRAY_COUNT(names) + 1);
        st_frames(2);
        st_shot("odd-names");
        // rename the invalid UTF-8 file
        ST_CHECK(st_select(p, "bad\xff\xfe" "name.txt"));
        begin_rename(p, p->cursor);
        te_set(&A.ren_te, "fixed.txt", 0, 0);
        commit_rename();
        ST_CHECK(st_exists(M10_BASE "/odd/fixed.txt"));
        // right-to-left names: Persian letters join and read right to left, digits and Latin parts left to right
        {
            const char *fa = "\xD9\x81\xD9\x88\xD9\x84\xD8\xAF\xD8\xB1"; // "folder" in Persian, 5 letters
            ST_CHECK(!text_complex("plain caf\xC3\xA9 \xD0\x96 \xE6\x96\x87", 13) && text_complex(fa, 10));
            f32 w;
            int ng = text_shape(&A.font, fa, 10, &w);
            bool rtl = ng >= 5 && w > 0;
            for (int i = 1; i < ng; i++) rtl &= g_shaped[i].rtl && g_shaped[i].cl <= g_shaped[i - 1].cl; // visual = reversed
            ST_CHECK(rtl && g_shaped[0].cl == 4 && g_shaped[ng - 1].cl == 0);
            // the first letter is drawn differently in the word (initial form) than alone; compare all its glyphs,
            // as fonts may build it from a shared base + a joining tail / dot
            u32 joined = 0, alone = 0;
            for (int i = 0; i < ng; i++)
                if (!g_shaped[i].cl) joined = joined * 65599 + g_shaped[i].gid;
            ng = text_shape(&A.font, fa, 2, &w);
            for (int i = 0; i < ng; i++) alone = alone * 65599 + g_shaped[i].gid;
            ST_CHECK(ng >= 1 && (!g_shaped[0].gid || alone != joined)); // (no Arabic font installed: notdef either way)
            u8 cls[32], lv[32];
            u32 cps[] = {'a', ' ', 0x0641, 0x0648, ' ', 0x06F1, 0x06F2, '.', 't'};
            ST_CHECK(bidi_levels(cps, 9, cls, lv) == 0 && lv[0] == 0 && lv[2] == 1 && lv[3] == 1 && lv[5] == 2 && lv[8] == 0);
            ST_CHECK(bidi_levels(cps + 2, 7, cls, lv) == 1 && lv[0] == 1 && lv[3] == 2 && lv[6] == 2);
            const char *mixed = "Report (\xD9\x81\xD9\x88\xD9\x84\xD8\xAF\xD8\xB1).pdf"; // the Persian part reversed in place
            int ml = (int)strlen(mixed);
            ST_CHECK(text_caret_x(&A.font, mixed, ml, 8) > text_caret_x(&A.font, mixed, ml, 16) &&
                     text_caret_x(&A.font, mixed, ml, 8) > text_caret_x(&A.font, mixed, ml, 7) &&
                     text_caret_x(&A.font, mixed, ml, ml) >= text_width(&A.font, mixed, ml) - 0.5f);
        }
        // a folder with a newline is browsable, but never written into the line-based session file
        pane_navigate(p, M10_BASE "/odd/dir\nwith newline", true);
        st_wait();
        ST_CHECK(p->L && atomic_load(&p->L->state) != LST_ERROR);
        settings_save();
        settings_load();
        bool nl_saved = false;
        for (int i = 0; i < A.nsess_tabs; i++) nl_saved |= strstr(A.sess_tabs[i], "with newline") != NULL;
        ST_CHECK(!nl_saved && A.nsess_tabs == A.ntabs - 1);
    }
    // deep and long paths: the path bar still ends with the current folder
    {
        char deep[PATH_MAX];
        snprintf(deep, sizeof deep, "%s", M10_BASE "/deep");
        mkdir(deep, 0755);
        for (int i = 0; i < 200; i++) {
            size_t l = strlen(deep);
            snprintf(deep + l, sizeof deep - l, "/d%d", i);
            mkdir(deep, 0755);
        }
        pane_navigate(p, deep, true);
        st_wait();
        layout_crumbs(p);
        ST_CHECK(!strcmp(p->path, deep) && p->ly.ncrumbs >= 2 && p->ly.crumbs[p->ly.ncrumbs - 1].end == (int)strlen(deep) &&
                 p->ly.crumbs_elided);
        // Path as text: no crumbs; a click on what was a crumb edits the path instead of going there
        A.path_text = true;
        st_frames(1);
        Rect pb = p->ly.pathbar;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, pb.x + S(20), pb.y + pb.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, pb.x + S(20), pb.y + pb.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(A.edit_pane == p && !strcmp(p->path, deep) && !strcmp(A.edit.buf, deep));
        st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
        ST_CHECK(!A.edit_pane && !strcmp(p->path, deep));
        settings_save();
        A.path_text = false;
        settings_load();
        ST_CHECK(A.path_text);
        char lp[PATH_MAX];
        snprintf(lp, sizeof lp, "%s", M10_BASE "/long");
        mkdir(lp, 0755);
        char comp[201];
        memset(comp, 'L', 200);
        comp[200] = 0;
        while (strlen(lp) + 202 < PATH_MAX - 64) {
            size_t l = strlen(lp);
            snprintf(lp + l, sizeof lp - l, "/%s", comp);
            if (mkdir(lp, 0755) != 0) break;
        }
        st_write(M10_BASE "/long/end-marker", 1);
        pane_navigate(p, lp, true);
        st_wait();
        ST_CHECK(!strcmp(p->path, lp) && strlen(p->path) > 3500 && p->L && atomic_load(&p->L->state) != LST_ERROR);
        st_frames(2);
        pane_up(p);
        st_wait();
        ST_CHECK(strlen(p->path) < strlen(lp));
    }
    // fractional scaling: 125% / 150% render and lay out on whole pixels
    {
        f32 d0 = A.dpi_scale;
        pane_navigate(p, M10_BASE, true);
        st_wait();
        for (int k = 0; k < 2; k++) {
            A.dpi_scale = k ? 1.5f : 1.25f;
            A.scale = A.dpi_scale * A.zoom;
            reload_fonts();
            layout();
            st_frames(2);
            ST_CHECK(p->ly.row_h == roundf(p->ly.row_h) && p->ly.list.y == roundf(p->ly.list.y) && !g_atlas_full);
            st_shot(k ? "scale-150" : "scale-125");
        }
        A.dpi_scale = d0;
        A.scale = A.dpi_scale * A.zoom;
        reload_fonts();
    }
    // network paths are never stat()ed / watched on the main thread
    ST_CHECK(path_is_remote(M10_BASE "/gvfs/smb-share:server=nas,share=media/x") && !path_is_remote(M10_BASE "/odd"));
    pane_navigate(p, M10_BASE "/gvfs/smb-share:server=nas,share=media", true);
    st_wait();
    ST_CHECK(!pane_watchable(p)); // (folder sizes are skipped there too)

    { // Connect to server: addresses, password + question prompts answered through gio, saved servers
        NetAddr na = {.proto = 1}, nb;
        snprintf(na.host, sizeof na.host, "nas.lan");
        snprintf(na.path, sizeof na.path, "My Files/sub");
        snprintf(na.user, sizeof na.user, "al ex");
        snprintf(na.domain, sizeof na.domain, "WORK");
        char uri[1024];
        net_uri(&na, uri, sizeof uri);
        ST_CHECK(!strcmp(uri, "smb://WORK;al%20ex@nas.lan/My%20Files/sub"));
        ST_CHECK(net_parse(uri, &nb) && nb.proto == 1 && !strcmp(nb.user, "al ex") && !strcmp(nb.domain, "WORK") &&
                 !strcmp(nb.path, "My Files/sub") && !strcmp(nb.host, "nas.lan"));
        ST_CHECK(net_parse("sftp://me@[fe80::1]:2222/home", &nb) && !strcmp(nb.host, "fe80::1") && !strcmp(nb.port, "2222"));
        int ns0 = A.nservers;
        { // the Network section is always there; empty: a note; its + opens Connect to server
            int nn = A.nnets, nsv = A.nservers;
            A.nnets = 0;
            A.nservers = 0;
            init_places();
            int hdr = st_place_row(PK_NETHDR, NULL);
            ST_CHECK(hdr >= 0 && st_place_row(PK_NETEMPTY, NULL) == hdr + 1);
            st_frames(2);
            if (hdr >= 0 && A.places[hdr].eject.w > 0) {
                Rect pr = A.places[hdr].eject;
                st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, pr.x + pr.w * 0.5f, pr.y + pr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
                st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, pr.x + pr.w * 0.5f, pr.y + pr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
                ST_CHECK(g_sheet.kind == SHEET_CONNECT && g_sheet.cn_edit < 0 && !A.nets_collapsed);
                sheet_close();
            } else ST_CHECK(!"network + button");
            st_shot("network-empty");
            A.nnets = nn;
            A.nservers = nsv;
            init_places();
        }
        // right password: mounted, opened, remembered (without the password)
        run_command(CMD_CONNECT);
        ST_CHECK(g_sheet.kind == SHEET_CONNECT);
        g_sheet.cn_proto = 0;
        te_set(&g_sheet.cn[CN_HOST], "secure.lan", 10, 10);
        te_set(&g_sheet.cn[CN_USER], "me", 2, 2);
        te_set(&g_sheet.cn[CN_PASS], "s3cret", 6, 6);
        st_frames(2);
        st_shot("connect");
        // clicks move the keyboard between fields; in the password the caret lands between the dots and a
        // double-click selects all of it (no words to give away)
        st_text_click(&g_sheet.cn[CN_PASS], 2, 1, 0);
        ST_CHECK(g_sheet.cn_focus == CN_PASS && g_sheet.cn[CN_PASS].anchor == 2 && g_sheet.cn[CN_PASS].caret == 2);
        st_text_click(&g_sheet.cn[CN_HOST], 3, 1, 0);
        ST_CHECK(g_sheet.cn_focus == CN_HOST && g_sheet.cn[CN_HOST].caret == 3);
        st_text_click(&g_sheet.cn[CN_PASS], 3, 2, 0);
        ST_CHECK(g_sheet.cn_focus == CN_PASS && g_sheet.cn[CN_PASS].anchor == 0 && g_sheet.cn[CN_PASS].caret == 6);
        ST_CHECK(!strcmp(g_sheet.cn[CN_PASS].buf, "s3cret") && !strcmp(g_sheet.cn[CN_HOST].buf, "secure.lan"));
        st_key(SDLK_RETURN, SDL_KMOD_NONE);
        ST_CHECK(!sheet_open() && !g_sheet.cn[CN_PASS].buf[0]);
        for (int k = 0; k < 600 && A.dev_busy; k++) { st_frames(1); SDL_Delay(5); }
        st_wait();
        ST_CHECK(st_exists(M10_BASE "/gvfs/sftp:host=secure.lan,user=me") && strstr(active_pane()->path, "host=secure.lan"));
        ST_CHECK(A.nservers == ns0 + 1 && !strcmp(A.servers[ns0].uri, "sftp://me@secure.lan/") && !strstr(A.servers[ns0].uri, "s3cret"));
        // the shared ssh connection for git status: opened in the background with the same password through our
        // askpass (never on the command line), the password file gone afterwards
        {
            char pw[64] = "", mux[4096] = "";
            for (int k = 0; k < 600 && !st_exists(M10_BASE "/tools/ssh-pw"); k++) SDL_Delay(5);
            SDL_Delay(50);
            st_read_text(M10_BASE "/tools/ssh-pw", pw, sizeof pw);
            st_read_text(M10_BASE "/tools/ssh-mux", mux, sizeof mux);
            char *sl = strstr(mux, "-O check -- me@secure.lan"); // (earlier servers were connected without a password)
            ST_CHECK(!strcmp(pw, "s3cret\n") && sl && strstr(sl, "-f -N -- me@secure.lan") && !strstr(mux, "s3cret"));
            if (sl && (sl = strchr(sl, '\n'))) {
                char *e = strchr(sl + 1, '\n');
                if (e) *e = 0;
                ST_CHECK(strstr(sl, "ControlMaster=yes") && strstr(sl, "ControlPersist=yes") && !strstr(sl, "BatchMode"));
            }
            char pat[PATH_MAX];
            snprintf(pat, sizeof pat, "%s/fe-askpass-%d-", getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "/tmp", (int)getpid());
            DIR *dd = opendir(getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "/tmp");
            bool left = false;
            for (struct dirent *de; dd && (de = readdir(dd));) {
                char full[PATH_MAX + 256];
                snprintf(full, sizeof full, "%s/%s", getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "/tmp", de->d_name);
                if (!strncmp(full, pat, strlen(pat))) left = true;
            }
            if (dd) closedir(dd);
            ST_CHECK(!left);
            // disconnected by another app: the next scan closes the shared connection
            ST_CHECK(!strstr(mux, "-O exit -- me@secure.lan"));
            rmdir(M10_BASE "/gvfs/sftp:host=secure.lan,user=me");
            devices_refresh();
            for (int k = 0; k < 200 && A.dev_scanning; k++) { st_frames(1); SDL_Delay(5); }
            for (int k = 0; k < 200 && !strstr(mux, "-O exit -- me@secure.lan"); k++) {
                SDL_Delay(5);
                st_read_text(M10_BASE "/tools/ssh-mux", mux, sizeof mux);
            }
            ST_CHECK(strstr(mux, "-O exit -- me@secure.lan") != NULL);
            mkdir(M10_BASE "/gvfs/sftp:host=secure.lan,user=me", 0755); // connected again for what follows
            devices_refresh();
            for (int k = 0; k < 200 && A.dev_scanning; k++) { st_frames(1); SDL_Delay(5); }
        }
        // wrong password: gio is told nothing more after asking again, the error is shown, nothing mounted
        connect_open(-1);
        g_sheet.cn_remember = true;
        te_set(&g_sheet.cn[CN_HOST], "secure2.lan", 11, 11);
        te_set(&g_sheet.cn[CN_USER], "me", 2, 2);
        te_set(&g_sheet.cn[CN_PASS], "nope", 4, 4);
        connect_go();
        for (int k = 0; k < 600 && g_sheet.kind != SHEET_SECRET; k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(g_sheet.kind == SHEET_SECRET && g_sheet.sec_job && g_sheet.sec_job->qretry); // not accepted: asked once more
        st_shot("connect-retry");
        st_key(SDLK_ESCAPE, SDL_KMOD_NONE); // cancel: gio gets nothing more
        for (int k = 0; k < 600 && A.dev_busy; k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(!st_exists(M10_BASE "/gvfs/sftp:host=secure2.lan,user=me") && strstr(A.toast, "Permission denied"));
        // the saved-but-unconnected server shows in the sidebar; clicking it prefills the dialog
        devices_refresh();
        for (int k = 0; k < 200 && A.dev_scanning; k++) { st_frames(1); SDL_Delay(5); }
        init_places();
        int sr = st_place_row(PK_NETSAVED, "secure2.lan");
        ST_CHECK(sr >= 0 && st_place_row(PK_NETSAVED, "secure.lan") < 0); // the connected one isn't listed twice
        if (sr >= 0) {
            A.menu_place = sr;
            do_action(active_pane(), ACT_NET_EDIT_SAVED);
            ST_CHECK(g_sheet.kind == SHEET_CONNECT && g_sheet.cn_edit >= 0 && !strcmp(g_sheet.cn[CN_HOST].buf, "secure2.lan"));
            // edit in place: Save writes the form back into the same entry (no duplicate)
            int ei = g_sheet.cn_edit, n0 = A.nservers;
            te_set(&g_sheet.cn[CN_HOST], "secure2b.lan", 12, 12);
            st_frames(2);
            st_shot("connect-edit");
            ST_CHECK(g_sheet.cn_rsave.w > 0);
            SDL_MouseButtonEvent mb = {.type = SDL_EVENT_MOUSE_BUTTON_DOWN, .button = SDL_BUTTON_LEFT, .clicks = 1,
                                       .x = g_sheet.cn_rsave.x + 4, .y = g_sheet.cn_rsave.y + 4};
            sheet_mouse(&mb);
            ST_CHECK(!sheet_open() && A.nservers == n0 && !strcmp(A.servers[ei].uri, "sftp://me@secure2b.lan/") &&
                     !strcmp(A.servers[ei].name, "secure2b.lan"));
            init_places();
            A.menu_place = st_place_row(PK_NETSAVED, "secure2b.lan");
            do_action(active_pane(), ACT_NET_FORGET);
            ST_CHECK(st_place_row(PK_NETSAVED, "secure2b.lan") < 0 && A.nservers == ns0 + 1);
        }
        // a question (unknown host key) goes to the user; their answer goes back to gio
        connect_open(-1);
        g_sheet.cn_remember = false;
        te_set(&g_sheet.cn[CN_HOST], "newhost.lan", 11, 11);
        te_set(&g_sheet.cn[CN_USER], "me", 2, 2);
        te_set(&g_sheet.cn[CN_PASS], "s3cret", 6, 6);
        connect_go();
        for (int k = 0; k < 600 && !A.dlg.open; k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(A.dlg.open && A.dlg.nbuttons == 2 && !strcmp(A.dlg.buttons[0], "Log In Anyway") && A.dlg.cancel == 1 &&
                 strstr(A.dlg.msg, "identity"));
        st_shot("connect-question");
        if (A.dlg.open) dialog_choose(0);
        for (int k = 0; k < 600 && A.dev_busy; k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(st_exists(M10_BASE "/gvfs/sftp:host=newhost.lan,user=me") && A.nservers == ns0 + 1);
        settings_save();
        settings_load();
        ST_CHECK(A.nservers == ns0 + 1 && !strcmp(A.servers[ns0].name, "secure.lan"));
        { // a server's address with a folder opens that folder in its mount, not the server's root
            char d[PATH_MAX];
            net_server_folder("sftp://al@vm1.lan:2222/srv/my%20app/", "/g/sftp:host=vm1.lan,user=al,port=2222", d, sizeof d);
            ST_CHECK(!strcmp(d, "/g/sftp:host=vm1.lan,user=al,port=2222/srv/my app"));
            net_server_folder("sftp://al@vm1.lan/", "/g/sftp:host=vm1.lan,user=al", d, sizeof d);
            ST_CHECK(!strcmp(d, "/g/sftp:host=vm1.lan,user=al"));
            net_server_folder("smb://nas/media/films", "/g/smb-share:server=nas,share=media", d, sizeof d);
            ST_CHECK(!strcmp(d, "/g/smb-share:server=nas,share=media/films"));
            net_server_folder("davs://cloud/dav/files", "/g/dav:host=cloud,ssl=true,prefix=%2Fdav%2Ffiles", d, sizeof d);
            ST_CHECK(!strcmp(d, "/g/dav:host=cloud,ssl=true,prefix=%2Fdav%2Ffiles"));
            Place pl = {.kind = PK_NETWORK, .server = -1, .path = "/g/sftp:host=other"}; // not a saved server: its mount
            ST_CHECK(place_open_path(&pl, d, sizeof d) == pl.path);
        }
        { // SSH terminals for SFTP: F4 inside a connection opens ssh in the same remote folder (quoted safely)
            SshTarget t;
            ST_CHECK(ssh_target_for(M10_BASE "/gvfs/sftp:host=vm1.lan,user=al,port=2222/srv/my app's dir", &t) && !strcmp(t.host, "vm1.lan") &&
                     !strcmp(t.user, "al") && !strcmp(t.port, "2222") && !strcmp(t.path, "/srv/my app's dir"));
            ST_CHECK(ssh_target_for(M10_BASE "/gvfs/sftp:host=vm1.lan,user=al", &t) && !strcmp(t.path, "/"));
            ST_CHECK(!ssh_target_for(M10_BASE "/gvfs/smb-share:server=nas,share=media/x", &t) && !ssh_target_for("/home", &t));
            { // sshfs mounts: the server from the mount's source; NFS and rclone sources look alike but aren't ssh
                int nn = A.nnets;
                NetMount saved[3];
                memcpy(saved, A.nets, sizeof saved);
                A.nets[0] = (NetMount){M10_BASE "/nfs", "nas:/exports/team", false};
                A.nets[1] = (NetMount){M10_BASE "/drive", "gdrive:", false};
                A.nets[2] = (NetMount){M10_BASE "/box", "me@box:/srv", true};
                A.nnets = 3;
                ST_CHECK(!ssh_target_for(M10_BASE "/nfs/a", &t) && !ssh_target_for(M10_BASE "/drive", &t));
                ST_CHECK(ssh_target_for(M10_BASE "/box/www", &t) && !strcmp(t.host, "box") && !strcmp(t.user, "me") && !strcmp(t.path, "/srv/www"));
                memcpy(A.nets, saved, sizeof saved);
                A.nnets = nn;
            }
            char d[PATH_MAX];
            snprintf(d, sizeof d, "%s", M10_BASE "/gvfs/sftp:host=vm1.lan,user=al,port=2222/srv/my app's dir");
            ext_terminal(d);
            ST_CHECK(!strcmp(A.last_opened, "ssh:-t -p 2222 al@vm1.lan cd '/srv/my app'\\''s dir' 2>/dev/null || cd; exec \"${SHELL:-/bin/sh}\" -l"));
            ext_terminal("/tmp");
            ST_CHECK(!strcmp(A.last_opened, "terminal:/tmp"));
            // sidebar: an SFTP saved server offers "Open SSH terminal"
            net_server_save("vm1", "sftp://al@vm1.lan:2222/srv", "");
            init_places();
            int r = st_place_row(PK_NETSAVED, "vm1");
            ST_CHECK(r >= 0);
            if (r >= 0) {
                A.menu_place = r;
                int nt = A.ntabs;
                do_action(active_pane(), ACT_SSH_TERMINAL); // a terminal tab running ssh (a stand-in here)
                ST_CHECK(A.ntabs == nt + 1 && active_term() && !strcmp(term_label(active_term()), "ssh al@vm1.lan"));
                if (active_term()) tab_close(A.cur);
                net_server_forget(A.places[r].item);
                init_places();
            }
        }
        { // "already mounted" (connected earlier, maybe from another app): not an error - its folder opens
            mkdir_p(M10_BASE "/gvfs/sftp:host=dupe.lan,user=me", 0755);
            devices_refresh();
            for (int k = 0; k < 200 && A.dev_scanning; k++) { st_frames(1); SDL_Delay(5); }
            A.toast[0] = 0;
            dev_start_net("sftp://me@dupe.lan/", "me", "", "", false, false);
            for (int k = 0; k < 600 && A.dev_busy; k++) { st_frames(1); SDL_Delay(5); }
            st_wait();
            ST_CHECK(!strstr(A.toast, "Couldn't") && strstr(active_pane()->path, "host=dupe.lan"));
        }
        { // display name: shown in the sidebar for the saved server and its connection; empty = from the address
            connect_open(-1);
            te_set(&g_sheet.cn[CN_NAME], "Office NAS", 10, 10);
            te_set(&g_sheet.cn[CN_HOST], "office.lan", 10, 10);
            te_set(&g_sheet.cn[CN_USER], "me", 2, 2);
            st_frames(2);
            st_shot("connect-name");
            connect_go();
            for (int k = 0; k < 600 && A.dev_busy; k++) { st_frames(1); SDL_Delay(5); }
            devices_refresh();
            for (int k = 0; k < 200 && A.dev_scanning; k++) { st_frames(1); SDL_Delay(5); }
            init_places();
            int oi = A.nservers - 1;
            ST_CHECK(!strcmp(A.servers[oi].name, "Office NAS") && st_place_row(PK_NETWORK, "Office NAS") >= 0);
            connect_open(oi); // edit: the name is there and can change
            ST_CHECK(!strcmp(g_sheet.cn[CN_NAME].buf, "Office NAS"));
            te_set(&g_sheet.cn[CN_NAME], "", 0, 0);
            connect_save_edit();
            sheet_close();
            ST_CHECK(!strcmp(A.servers[oi].name, "office.lan"));
            { // display icon: picked from the popover, saved with the server, shown in the sidebar, kept in settings
                ST_CHECK(!A.servers[oi].icon[0] && net_icon_find(A.servers[oi].icon) == 0);
                connect_open(oi);
                st_frames(2);
                Rect ib = g_sheet.cn_ricon;
                st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, ib.x + ib.w * 0.5f, ib.y + ib.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
                st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, ib.x + ib.w * 0.5f, ib.y + ib.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
                st_frames(2);
                ST_CHECK(g_sheet.cn_pick && g_sheet.cn_ncell > 10);
                st_shot("connect-icon-picker");
                int aws = -1;
                for (int c = 0; c < g_sheet.cn_ncell; c++) if (!strcmp(net_icon_key(g_sheet.cn_cell_idx[c]), "aws")) aws = c;
                ST_CHECK(aws >= 0);
                if (aws >= 0) {
                    Rect cr = g_sheet.cn_rcell[aws];
                    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, cr.x + cr.w * 0.5f, cr.y + cr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
                    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, cr.x + cr.w * 0.5f, cr.y + cr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
                }
                ST_CHECK(!g_sheet.cn_pick && !strcmp(g_sheet.cn_icon, "aws") && g_sheet.kind == SHEET_CONNECT);
                st_key(SDLK_ESCAPE, 0); // (closes the dialog: the picker is already closed)
                ST_CHECK(!sheet_open() && !A.servers[oi].icon[0]); // cancelled: nothing saved
                connect_open(oi);
                snprintf(g_sheet.cn_icon, sizeof g_sheet.cn_icon, "aws");
                g_sheet.cn_pick = true;
                st_key(SDLK_ESCAPE, 0); // Escape closes the picker first
                ST_CHECK(sheet_open() && !g_sheet.cn_pick);
                connect_save_edit();
                sheet_close();
                init_places();
                ST_CHECK(!strcmp(A.servers[oi].icon, "aws"));
                int pr = st_place_row(PK_NETWORK, "office.lan");
                ST_CHECK(pr >= 0 && A.places[pr].server == oi);
                st_shot("sidebar-net-icon");
                settings_save();
                char sp[PATH_MAX], line[2048];
                settings_file(sp, sizeof sp, false);
                FILE *sf = fopen(sp, "r");
                bool found = false;
                while (sf && fgets(line, sizeof line, sf)) found |= strstr(line, "server=office.lan\t") && strstr(line, "\taws\n");
                if (sf) fclose(sf);
                ST_CHECK(found);
                // custom icons: net-<name>.svg in any icon themes folder, offered without making it a file icon theme
                char idir[PATH_MAX], f1[PATH_MAX + 64];
                icon_themes_dir(idir, sizeof idir);
                snprintf(f1, sizeof f1, "%s/Homelab", idir);
                mkdir_p(f1, 0755);
                snprintf(f1, sizeof f1, "%s/Homelab/net-my-rack.svg", idir);
                st_write_text(f1, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><rect width=\"24\" height=\"24\" rx=\"6\" fill=\"#E91E63\"/></svg>");
                int nt0 = icon_theme_count();
                icon_themes_reload();
                int ci = net_icon_find("custom/Homelab/my-rack");
                ST_CHECK(ci >= NET_ICON_BUILTIN && !strcmp(net_icon_label(ci), "My rack") && icon_theme_count() == nt0);
                ST_CHECK(icon_glyph(ICON_NET_BASE + ci, 16) != NULL);
                net_server_update(oi, A.servers[oi].name, A.servers[oi].uri, "custom/Homelab/my-rack");
                unlink(f1);
                icon_themes_reload(); // removed: falls back to the globe, the key is kept
                ST_CHECK(net_icon_find(A.servers[oi].icon) == 0 && !strcmp(A.servers[oi].icon, "custom/Homelab/my-rack"));
                st_frames(2);
            }
            net_server_forget(oi);
        }
        // saved server: connects directly; the password is asked only when the server wants it, kept only if ticked
        net_server_save("vault.lan", "sftp://me@secure-vault.lan/", "");
        int vi = A.nservers - 1;
        net_connect_saved(vi);
        ST_CHECK(g_sheet.kind != SHEET_CONNECT);
        for (int k = 0; k < 600 && g_sheet.kind != SHEET_SECRET; k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(g_sheet.kind == SHEET_SECRET && !g_sheet.sec_job->qretry && strstr(g_sheet.sec_job->question, "Enter password"));
        on_text("s3cret");
        g_sheet.cn_remember_pw = true;
        st_frames(2);
        st_shot("connect-password");
        st_key(SDLK_RETURN, SDL_KMOD_NONE);
        ST_CHECK(!sheet_open() && !g_sheet.cn[CN_PASS].buf[0]);
        for (int k = 0; k < 600 && A.dev_busy; k++) { st_frames(1); SDL_Delay(5); }
        const char *vs = M10_BASE "/secrets/sftp___me@secure-vault.lan_";
        ST_CHECK(st_exists(M10_BASE "/gvfs/sftp:host=secure-vault.lan,user=me") && st_exists(vs));
        char sec[64] = "";
        FILE *sf = fopen(vs, "r");
        if (sf) { if (!fgets(sec, sizeof sec, sf)) sec[0] = 0; fclose(sf); }
        ST_CHECK(!strcmp(sec, "s3cret")); // in the keyring...
        settings_save();
        char cfg[PATH_MAX], line[2048];
        settings_file(cfg, sizeof cfg, false);
        bool leaked = false;
        FILE *cf = fopen(cfg, "r");
        while (cf && fgets(line, sizeof line, cf)) leaked |= strstr(line, "s3cret") != NULL;
        if (cf) fclose(cf);
        ST_CHECK(!leaked); // ...never in the settings
        // next time: the keyring password is used, nothing asked
        if (system("rm -rf '" M10_BASE "/gvfs/sftp:host=secure-vault.lan,user=me'")) {}
        net_connect_saved(vi);
        for (int k = 0; k < 600 && A.dev_busy; k++) { st_frames(1); SDL_Delay(5); ST_CHECK(g_sheet.kind != SHEET_SECRET); if (g_sheet.kind == SHEET_SECRET) break; }
        ST_CHECK(st_exists(M10_BASE "/gvfs/sftp:host=secure-vault.lan,user=me"));
        // a stale keyring password: asked once ("not accepted"), cancel = no login
        sf = fopen(vs, "w");
        if (sf) { fputs("old", sf); fclose(sf); }
        if (system("rm -rf '" M10_BASE "/gvfs/sftp:host=secure-vault.lan,user=me'")) {}
        net_connect_saved(vi);
        for (int k = 0; k < 600 && g_sheet.kind != SHEET_SECRET; k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(g_sheet.kind == SHEET_SECRET && g_sheet.sec_job->qretry);
        if (g_sheet.kind == SHEET_SECRET) secret_prompt_answer(false);
        for (int k = 0; k < 600 && A.dev_busy; k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(!st_exists(M10_BASE "/gvfs/sftp:host=secure-vault.lan,user=me"));
        // forgetting the server removes its password too
        net_server_forget(vi);
        for (int k = 0; k < 200 && st_exists(vs); k++) { st_frames(1); SDL_Delay(5); }
        ST_CHECK(!st_exists(vs));
    }

    // devices: multi-device btrfs members hidden, custom names, collapsible sections, stable hover layout
    for (int k = 0; k < 400 && (A.dev_scanning || A.dev_busy); k++) { st_frames(1); SDL_Delay(5); }
    {
        DevList saved = A.devs;
        memset(&A.devs, 0, sizeof A.devs);
        BlockDev *d = A.devs.d;
        d[0] = (BlockDev){.dev = "/dev/fake1", .disk = "fake", .fstype = "btrfs", .uuid = "U-1", .mount = "/", .size = 100};
        d[1] = (BlockDev){.dev = "/dev/fake2", .disk = "fake", .fstype = "btrfs", .uuid = "U-1", .size = 500}; // same fs
        d[2] = (BlockDev){.dev = "/dev/fake3", .disk = "fake", .fstype = "ntfs", .uuid = "U-2", .size = 150};
        d[3] = (BlockDev){.dev = "/dev/fake4", .disk = "usb", .fstype = "vfat", .uuid = "U-3", .label = "STICK", .size = 8, .removable = true};
        snprintf(d[3].mount, sizeof d[3].mount, "%s", M10_BASE);
        A.devs.n = 4;
        init_places();
        int unm = 0, member = 0;
        for (int i = 0; i < A.nplaces; i++)
            if (A.places[i].kind == PK_UNMOUNTED) { unm++; member += A.places[i].dev == 1; }
        ST_CHECK(unm == 1 && !member);
        // rename the stick (app-side only), survives a settings round trip, resets
        int r = st_place_row(PK_DRIVE, "STICK");
        ST_CHECK(r >= 0);
        if (r >= 0) {
            A.menu_place = r;
            do_action(active_pane(), ACT_DEV_RENAME);
            ST_CHECK(A.qa_edit_g == QA_EDIT_DEV && !strcmp(A.qa_te.buf, "STICK"));
            te_set(&A.qa_te, "Photos USB", 0, 10);
            st_key(SDLK_RETURN, SDL_KMOD_NONE);
            ST_CHECK(A.qa_edit_g < 0 && st_place_row(PK_DRIVE, "Photos USB") >= 0 && st_place_row(PK_DRIVE, "STICK") < 0);
            settings_save();
            settings_load();
            init_places();
            r = st_place_row(PK_DRIVE, "Photos USB");
            ST_CHECK(r >= 0 && !strcmp(dev_name_get("U-3"), "Photos USB"));
            // hover: the eject button takes the free-space text's place; the bar stays, shortened
            if (r >= 0) {
                Rect pr = place_rect(r);
                st_mouse(SDL_EVENT_MOUSE_MOTION, pr.x + S(40), pr.y + pr.h * 0.5f, 0, 0, 0);
                st_frames(2);
                ST_CHECK(A.places[r].eject.w > 0 && A.places[r].eject.x + A.places[r].eject.w <= pr.x + pr.w);
                st_shot("dev-hover");
                st_mouse(SDL_EVENT_MOUSE_MOTION, A.r_content.x + S(200), A.r_content.y + S(200), 0, 0, 0);
                st_frames(2);
                st_shot("dev-idle");
                A.menu_place = r;
                do_action(active_pane(), ACT_DEV_RESET_NAME);
                ST_CHECK(st_place_row(PK_DRIVE, "STICK") >= 0 && !dev_name_get("U-3"));
            }
        }
        // collapse / expand the Devices section
        int hdr = st_place_row(PK_DEVICES, NULL);
        ST_CHECK(hdr >= 0);
        if (hdr >= 0) {
            Rect hr = place_rect(hdr);
            st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, hr.x + S(30), hr.y + hr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
            st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, hr.x + S(30), hr.y + hr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
            ST_CHECK(A.devs_collapsed && st_place_row(PK_DRIVE, NULL) < 0 && st_place_row(PK_DEVICES, NULL) >= 0);
            settings_save();
            settings_load();
            ST_CHECK(A.devs_collapsed);
            init_places();
            hdr = st_place_row(PK_DEVICES, NULL);
            hr = place_rect(hdr);
            st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, hr.x + S(30), hr.y + hr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
            st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, hr.x + S(30), hr.y + hr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
            ST_CHECK(!A.devs_collapsed && st_place_row(PK_DRIVE, "File System") >= 0);
        }
        A.devs = saved;
        init_places();
    }

    addon_selftest_m10(); // add-ons' parts of this suite (addons.c)

    { // --- settings this build doesn't know (add-ons', a newer version's) survive a load + save ---
        char sp[PATH_MAX], text[65536];
        g_settings_other_len = 0; // (none left over from an earlier run's config)
        g_settings_other[0] = 0;
        settings_save();
        settings_file(sp, sizeof sp, false);
        FILE *f = fopen(sp, "a");
        ST_CHECK(f != NULL);
        if (f) { fputs("st_unknown_key=a\tb=c\n", f); fclose(f); }
        settings_load();
        settings_save();
        st_read_text(sp, text, sizeof text);
        char *first = strstr(text, "\nst_unknown_key=a\tb=c\n");
        ST_CHECK(first && !strstr(first + 1, "\nst_unknown_key="));
        settings_load();
        settings_save(); // (kept once, not doubled)
        st_read_text(sp, text, sizeof text);
        first = strstr(text, "\nst_unknown_key=a\tb=c\n");
        ST_CHECK(first && !strstr(first + 1, "\nst_unknown_key="));
    }

    { // --- start at login, tray ---
        // start at login: the XDG autostart entry (in the test config dir), started in the background
        char ap[PATH_MAX];
        autostart_file(ap, sizeof ap);
        ST_CHECK(!autostart_enabled() && strstr(ap, "/tmp/fe-selftest-config/"));
        autostart_set(true);
        ST_CHECK(autostart_enabled() && st_file_has(ap, "--background") && st_file_has(ap, "Exec="));
        autostart_set(false);
        ST_CHECK(!autostart_enabled());
        // no tray in tests: "keep running in the tray" can't hide the window, so closing still quits
        ST_CHECK(!tray_host_available() && !g_tray);
    }

    { // --- cloud: rclone remotes listed, connected as systemd units, opened, at login, errors, other mounts, removed ---
        const char *cdir = M10_BASE "/cloud", *rcd = M10_BASE "/rc";
        char udir[PATH_MAX], up[PATH_MAX + 64], mline[PATH_MAX * 2], gdir[PATH_MAX];
        units_dir(udir, sizeof udir);
        st_rmrf(cdir);
        st_rmrf(rcd);
        mkdir(rcd, 0755);
        mkdir_p("/tmp/fe-selftest-config/rclone", 0755);
        st_write_text("/tmp/fe-selftest-config/rclone/rclone.conf", "[gdrive]\ntype = drive\n");
        st_write_text(M10_BASE "/rc/remotes", "gdrive:      drive\nwork s3:     s3\n");
        st_write_text(M10_BASE "/rc/mounts", "");
        FILE *f = fopen(M10_BASE "/tools/rclone", "w");
        fputs("#!/bin/sh\necho \"rclone $*\" >> " M10_BASE "/rc/log\n[ \"$1\" = listremotes ] && cat " M10_BASE "/rc/remotes\nexit 0\n", f);
        fclose(f);
        chmod(M10_BASE "/tools/rclone", 0755);
        f = fopen(M10_BASE "/tools/fusermount", "w");
        fputs("#!/bin/sh\necho \"fusermount $*\" >> " M10_BASE "/rc/log\n: > " M10_BASE "/rc/mounts\n", f);
        fclose(f);
        chmod(M10_BASE "/tools/fusermount", 0755);
        setenv("FE_RCLONE", M10_BASE "/tools/rclone", 1);
        setenv("FE_FUSERMOUNT", M10_BASE "/tools/fusermount", 1);
        setenv("FE_CLOUD_DIR", cdir, 1);
        setenv("FE_PROC_MOUNTS", M10_BASE "/rc/mounts", 1);
        unlink(M10_BASE "/sd/journal");
        g_ncloud = 0;
        g_rclone = -1;
        g_cloud_relist = true;
        g_cloud_collapsed = false;
        st_cloud_settle();
        int gr = st_place_row(PK_CLOUD, "gdrive");
        ST_CHECK(g_rclone == 1 && g_ncloud == 2 && st_place_row(PK_CLOUDHDR, "Cloud") >= 0 && gr >= 0 && st_place_row(PK_CLOUD, "work s3") >= 0);
        ST_CHECK(!strcmp(cloud_icon(&g_cloud[0]), "gdrive") && !strcmp(cloud_icon(&g_cloud[1]), "aws") && !A.places[gr].path[0]);
        char un[160];
        cloud_unit_name("work s3", un, sizeof un);
        ST_CHECK(!strcmp(un, "file-explorer-cloud-work_20s3.service"));
        // click: the unit written and started; connected once the mount shows up, then opened
        Rect rr = place_rect(gr);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, rr.x + S(40), rr.y + rr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, rr.x + S(40), rr.y + rr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(g_cloud[0].state == CS_STARTING && g_cloud[0].open_after == 1);
        st_cloud_settle();
        snprintf(up, sizeof up, "%s/file-explorer-cloud-gdrive.service", udir);
        snprintf(gdir, sizeof gdir, "%s/gdrive", cdir);
        ST_CHECK(st_file_has(up, "# Written by file-explorer for the cloud remote \"gdrive\"") && st_file_has(up, "Type=notify") &&
                 st_file_has(up, "mount \"gdrive:\" \"" M10_BASE "/cloud/gdrive\" --vfs-cache-mode full") && st_file_has(up, "Restart=on-failure") &&
                 st_file_has(up, "Description=Files cloud: gdrive (Google Drive)"));
        ST_CHECK(access(M10_BASE "/sd/file-explorer-cloud-gdrive.service.active", F_OK) == 0 && access(gdir, F_OK) == 0);
        ST_CHECK(g_cloud[0].state == CS_STARTING && g_cloud[0].unit && path_is_remote(gdir)); // active, not mounted yet
        st_frames(2);
        st_shot("cloud-connecting");
        snprintf(mline, sizeof mline, "gdrive: %s fuse.rclone rw,nosuid,nodev 0 0\n", gdir);
        st_write_text(M10_BASE "/rc/mounts", mline);
        st_cloud_settle();
        gr = st_place_row(PK_CLOUD, "gdrive");
        ST_CHECK(g_cloud[0].state == CS_UP && g_cloud[0].ours && !strcmp(g_cloud[0].mount, gdir) && !g_cloud[0].open_after);
        ST_CHECK(gr >= 0 && !strcmp(A.places[gr].path, gdir) && A.places[gr].removable && !strcmp(active_pane()->path, gdir));
        ST_CHECK(cloud_for_mount(gdir) == &g_cloud[0]);
        st_frames(2);
        st_shot("cloud-sidebar");
        // connect at login: enabled, kept in the settings
        A.menu_place = gr;
        do_action(active_pane(), ACT_CLOUD_LOGIN);
        st_cloud_settle();
        ST_CHECK(g_cloud[0].login && access(M10_BASE "/sd/file-explorer-cloud-gdrive.service.enabled", F_OK) == 0);
        settings_save();
        g_cloud[0].login = false;
        settings_load();
        ST_CHECK(cloud_find("gdrive") && cloud_find("gdrive")->login && !cloud_find("work s3")->login);
        // disconnect: panes leave it, the unit stops
        cloud_disconnect(0);
        ST_CHECK(strcmp(active_pane()->path, gdir));
        st_write_text(M10_BASE "/rc/mounts", "");
        st_cloud_settle();
        gr = st_place_row(PK_CLOUD, "gdrive");
        ST_CHECK(g_cloud[0].state == CS_OFF && !g_cloud[0].mount[0] && gr >= 0 && !A.places[gr].path[0] &&
                 access(M10_BASE "/sd/file-explorer-cloud-gdrive.service.active", F_OK) != 0);
        // a connect that fails: the reason from the log; an expired sign-in offers rclone's reconnect
        st_write_text(M10_BASE "/sd/next-start", "activating\n");
        st_write_text(M10_BASE "/sd/journal", "2026/11/03 17:00:00 NOTICE: starting\n2026/11/03 17:00:01 CRITICAL: Failed to create file system: "
                      "couldn't fetch token: invalid_grant: maybe token expired?\n");
        cloud_connect(0, 2);
        st_cloud_settle();
        ST_CHECK(g_cloud[0].state == CS_RETRY && strstr(g_cloud[0].err, "sign-in expired") && !g_cloud[0].open_after);
        ST_CHECK(A.dlg.open && strstr(A.dlg.msg, "Reconnecting opens rclone"));
        A.dlg.open = false;
        cloud_reconnect_cb(0, false, "gdrive");
        ST_CHECK(!strcmp(g_cloud_term_cmd, M10_BASE "/tools/rclone config reconnect gdrive:"));
        st_write_text(M10_BASE "/sd/journal", "2026/11/03 17:00:01 ERROR : Something new went wrong\n");
        st_cloud_settle();
        ST_CHECK(!strcmp(g_cloud[0].err, "Something new went wrong"));
        cloud_disconnect(0);
        st_cloud_settle();
        unlink(M10_BASE "/sd/next-start");
        unlink(M10_BASE "/sd/journal");
        ST_CHECK(g_cloud[0].state == CS_OFF && !g_cloud[0].err[0]);
        // mounted some other way (a space in the name, a sub-folder): connected at its own folder, fusermount to disconnect
        st_write_text(M10_BASE "/rc/mounts", "work\\040s3:bucket/x " M10_BASE "/a fuse.rclone rw 0 0\n");
        st_cloud_settle();
        int wr = st_place_row(PK_CLOUD, "work s3");
        ST_CHECK(wr >= 0 && g_cloud[1].state == CS_UP && !g_cloud[1].ours && !strcmp(A.places[wr].path, M10_BASE "/a"));
        cloud_disconnect(1);
        st_cloud_settle();
        ST_CHECK(st_file_has(M10_BASE "/rc/log", "fusermount -u " M10_BASE "/a") && !g_cloud[1].mount[0]);
        // the header's +: rclone config in a terminal
        int ch = st_place_row(PK_CLOUDHDR, NULL);
        st_frames(2);
        Rect plus = A.places[ch].eject;
        ST_CHECK(plus.w > 0);
        g_cloud_term_cmd[0] = 0;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, plus.x + plus.w * 0.5f, plus.y + plus.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, plus.x + plus.w * 0.5f, plus.y + plus.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(!strcmp(g_cloud_term_cmd, M10_BASE "/tools/rclone config"));
        // a remote deleted from rclone's config (seen by its time stamp): gone from the sidebar, its unit removed
        st_write_text(M10_BASE "/rc/remotes", "work s3:     s3\n");
        struct timespec ts[2] = {{0, UTIME_NOW}, {time(NULL) + 5, 0}};
        utimensat(AT_FDCWD, "/tmp/fe-selftest-config/rclone/rclone.conf", ts, 0);
        st_cloud_settle();
        ST_CHECK(g_ncloud == 1 && !cloud_find("gdrive") && st_place_row(PK_CLOUD, "gdrive") < 0 && access(up, F_OK) != 0);
        // collapse
        ch = st_place_row(PK_CLOUDHDR, NULL);
        Rect hr = place_rect(ch);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, hr.x + S(30), hr.y + hr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, hr.x + S(30), hr.y + hr.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(g_cloud_collapsed && st_place_row(PK_CLOUD, NULL) < 0);
        g_cloud_collapsed = false;
        // no rclone: no Cloud section
        st_write_text(M10_BASE "/rc/remotes", "");
        unlink(M10_BASE "/tools/rclone");
        g_cloud_relist = true;
        st_cloud_settle();
        ST_CHECK(g_rclone == 0 && st_place_row(PK_CLOUDHDR, NULL) < 0);
        unsetenv("FE_RCLONE");
        unsetenv("FE_PROC_MOUNTS");
        g_ncloud = 0;
        init_places();
    }

    { // --- picker (the app as the system file chooser): Open / Open files / Choose folder / Save, output file ---
        Pane *pp = active_pane();
        const char *pdir = M10_BASE "/pick";
        st_rmrf(pdir);
        mkdir_p(M10_BASE "/pick/sub", 0755);
        st_write_text(M10_BASE "/pick/a.txt", "a");
        st_write_text(M10_BASE "/pick/b.txt", "b");
        const char *outf = M10_BASE "/pick-out";
        char outbuf[2048];
        #define ST_PICK_OUT() (outbuf[0] = 0, (void)st_read_text(outf, outbuf, sizeof outbuf), outbuf)
        snprintf(g_pick_out, sizeof g_pick_out, "%s", outf);
        pane_navigate(pp, pdir, true);
        st_wait();
        // Choose folder: nothing selected = the folder shown; one selected folder = that one
        g_pick = PICK_DIR;
        layout();
        st_frames(2);
        ST_CHECK(A.r_status.h == picker_bar_h() && g_pick_rok.w > 0);
        st_shot("picker-dir");
        sel_clear(pp);
        A.pick_done = 0;
        unlink(outf);
        st_key(SDLK_RETURN, 0);
        ST_CHECK(A.pick_done == 1 && !strcmp(ST_PICK_OUT(), M10_BASE "/pick\n"));
        st_select(pp, "sub");
        A.pick_done = 0;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, g_pick_rok.x + 5, g_pick_rok.y + 5, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, g_pick_rok.x + 5, g_pick_rok.y + 5, SDL_BUTTON_LEFT, 1, 0);
        ST_CHECK(A.pick_done == 1 && !strcmp(ST_PICK_OUT(), M10_BASE "/pick/sub\n"));
        // Open file: Enter on a file picks it; on a folder it opens the folder
        g_pick = PICK_FILE;
        st_select(pp, "b.txt");
        A.pick_done = 0;
        st_key(SDLK_RETURN, 0);
        ST_CHECK(A.pick_done == 1 && !strcmp(ST_PICK_OUT(), M10_BASE "/pick/b.txt\n"));
        // Open files: several at once (folders in the selection are left out)
        g_pick = PICK_FILES;
        do_action(pp, ACT_SELECT_ALL);
        A.pick_done = 0;
        picker_commit();
        ST_CHECK(A.pick_done == 1 && !strcmp(ST_PICK_OUT(), M10_BASE "/pick/a.txt\n" M10_BASE "/pick/b.txt\n"));
        // Save: typed name; an existing file asks first; Escape cancels with an empty answer
        g_pick = PICK_SAVE;
        sel_clear(pp);
        te_set(&g_pick_name, "", 0, 0);
        g_pick_name_focus = true;
        st_type("new.txt");
        A.pick_done = 0;
        st_key(SDLK_RETURN, 0);
        ST_CHECK(A.pick_done == 1 && !strcmp(ST_PICK_OUT(), M10_BASE "/pick/new.txt\n"));
        te_set(&g_pick_name, "a.txt", 5, 5);
        A.pick_done = 0;
        picker_commit();
        ST_CHECK(A.pick_done == 0 && A.dlg.open && strstr(A.dlg.title, "Replace")); // asked, nothing chosen yet
        st_frames(1);
        st_shot("picker-replace");
        dialog_key(SDLK_ESCAPE, 0); // keep it: back to the picker
        ST_CHECK(!A.dlg.open && A.pick_done == 0);
        picker_commit();
        ST_CHECK(A.dlg.open);
        A.dlg.focus = 0;
        dialog_key(SDLK_RETURN, 0); // Replace
        ST_CHECK(A.pick_done == 1 && !strcmp(ST_PICK_OUT(), M10_BASE "/pick/a.txt\n"));
        te_set(&g_pick_name, "sub", 3, 3); // a folder's name: goes into it
        picker_commit();
        st_wait();
        ST_CHECK(!strcmp(pp->path, M10_BASE "/pick/sub") && !g_pick_name.buf[0]);
        // while the name has the keyboard: Alt+Up goes up and keeps the name, Ctrl+L gives the path bar the keys
        g_pick_name_focus = true;
        A.pick_done = 0;
        st_type("draft");
        st_key(SDLK_UP, SDL_KMOD_ALT);
        st_wait();
        ST_CHECK(!strcmp(pp->path, M10_BASE "/pick") && !strcmp(g_pick_name.buf, "draft") && g_pick_name_focus);
        st_key(SDLK_L, SDL_KMOD_CTRL);
        ST_CHECK(A.edit_pane == pp && !g_pick_name_focus);
        st_type("x");
        ST_CHECK(!strcmp(g_pick_name.buf, "draft") && !strcmp(A.edit.buf, "x")); // typed into the path bar, not the name
        st_key(SDLK_ESCAPE, 0);
        ST_CHECK(!A.edit_pane && A.pick_done == 0);
        // selecting a file puts its name in the field, stem selected; typed over, it stays; a folder leaves it
        g_pick_name_focus = false;
        st_select(pp, "a.txt");
        picker_tick();
        ST_CHECK(!strcmp(g_pick_name.buf, "a.txt") && g_pick_name.anchor == 0 && g_pick_name.caret == 1);
        g_pick_name_focus = true;
        st_type("c");
        picker_tick();
        ST_CHECK(!strcmp(g_pick_name.buf, "c.txt"));
        g_pick_name_focus = false;
        st_select(pp, "sub");
        picker_tick();
        ST_CHECK(!strcmp(g_pick_name.buf, "c.txt") && !strcmp(pp->path, M10_BASE "/pick"));
        // a path as the name (a file selected in the tree view): into that folder, if it exists
        A.pick_done = 0;
        te_set(&g_pick_name, "nope/x.txt", 0, 0);
        picker_commit();
        ST_CHECK(A.pick_done == 0 && !A.dlg.open);
        te_set(&g_pick_name, "sub/new.txt", 0, 0);
        picker_commit();
        ST_CHECK(A.pick_done == 1 && !strcmp(ST_PICK_OUT(), M10_BASE "/pick/sub/new.txt\n"));
        A.pick_done = 0;
        te_set(&g_pick_name, M10_BASE "/pick/sub/abs.txt", 0, 0);
        picker_commit();
        ST_CHECK(A.pick_done == 1 && !strcmp(ST_PICK_OUT(), M10_BASE "/pick/sub/abs.txt\n"));
        A.pick_done = 0;
        te_set(&g_pick_name, "", 0, 0);
        unlink(outf);
        A.pick_done = 0;
        g_pick_name_focus = true;
        st_key(SDLK_ESCAPE, 0);
        ST_CHECK(A.pick_done == -1 && st_read_text(outf, outbuf, sizeof outbuf) == 0);
        // the suggested path splits into folder + name with the stem selected; settings stay untouched
        const char *st0 = M10_BASE "/pick/report.final.pdf";
        char sb[PATH_MAX];
        picker_setup("save", &st0, sb, sizeof sb);
        ST_CHECK(g_pick == PICK_SAVE && !strcmp(st0, M10_BASE "/pick") && !strcmp(g_pick_name.buf, "report.final.pdf") &&
                 g_pick_name.caret == 12 && g_pick_name.anchor == 0 && g_pick_name_focus);
        char sp[PATH_MAX];
        settings_file(sp, sizeof sp, false);
        struct stat s1, s2;
        stat(sp, &s1);
        A.settings_dirty = true;
        settings_save();
        ST_CHECK(stat(sp, &s2) == 0 && s1.st_mtim.tv_sec == s2.st_mtim.tv_sec && s1.st_mtim.tv_nsec == s2.st_mtim.tv_nsec);
        // the folder last saved into, per type: the next .txt starts where the last one went (sub/abs.txt above);
        // other types keep the app's folder; a remembered folder that's gone doesn't count
        const char *st1 = M10_BASE "/pick/notes.TXT";
        picker_setup("save", &st1, sb, sizeof sb);
        ST_CHECK(!strcmp(st1, M10_BASE "/pick/sub") && !strcmp(g_pick_name.buf, "notes.TXT"));
        const char *st2 = M10_BASE "/pick/report.final.pdf";
        picker_setup("save", &st2, sb, sizeof sb);
        ST_CHECK(!strcmp(st2, M10_BASE "/pick"));
        mkdir(M10_BASE "/pick/gone", 0755);
        picker_type_remember(M10_BASE "/pick/gone/x.md");
        rmdir(M10_BASE "/pick/gone");
        const char *st3 = M10_BASE "/pick/y.md";
        picker_setup("save", &st3, sb, sizeof sb);
        ST_CHECK(!strcmp(st3, M10_BASE "/pick"));
        g_pick = PICK_NONE;
        g_pick_out[0] = 0;
        A.settings_dirty = false;
        layout();
        #undef ST_PICK_OUT
    }

    { // --- several processes share settings.conf: a save writes only what this one changed ---
        // mine, base (mine at the last sync), disk (the file now), disk_base (the file at the last sync)
        char *r = settings_merge("# h\na=2\nb=1\npin=x\n", "# h\na=1\nb=1\npin=x\npin=y\n", "a=1\nb=3\nc=9\npin=x\npin=y\n",
                                 "a=1\nb=1\npin=x\npin=y\n");
        ST_CHECK(!strncmp(r, "# h\n", 4) && strstr(r, "\na=2\n") && strstr(r, "\nb=3\n") && strstr(r, "\nc=9\n")); // ours, theirs, new there
        ST_CHECK(strstr(r, "pin=x\n") && !strstr(r, "pin=y")); // a group (all its lines) changed here: ours whole
        free(r);
        r = settings_merge("a=1\n", "a=1\nold=1\n", "a=1\nold=1\n", "a=1\nold=1\n"); // removed here, untouched there: gone
        ST_CHECK(!strstr(r, "old="));
        free(r);
        r = settings_merge("a=1\nsrv=1\n", "a=1\nsrv=1\n", "a=1\n", "a=1\nsrv=1\n"); // removed there, untouched here: gone
        ST_CHECK(!strstr(r, "srv="));
        free(r);
        r = settings_merge("a=1\nb=2\n", "a=1\nb=2\n", "a=1\n", "a=1\n"); // a short (hand-written, old) file: ours fill it in
        ST_CHECK(strstr(r, "b=2\n") != NULL);
        free(r);
        // the session and Quick Access are one group each: never one window's layout with another's tabs
        const char *s1 = "layout=P0\nptab=0\t1\t/a\t0:96\nactive_panel=0\n";
        r = settings_merge(s1, s1, "layout=H0.500(P0,P1)\nptab=0\t1\t/b\t0:96\nptab=1\t1\t/c\t0:96\nactive_panel=1\n", s1);
        ST_CHECK(strstr(r, "layout=H0.500(P0,P1)") && strstr(r, "\t/b\t") && strstr(r, "\t/c\t") && !strstr(r, "\t/a\t"));
        free(r);
        r = settings_merge("qa_group=G\t0\nqa=A\t/a\nqa_group=H\t0\nqa=B\t/b\n", "qa_group=G\t0\nqa=A\t/a\n", "qa_group=G\t0\nqa=Z\t/z\n",
                           "qa_group=G\t0\nqa=A\t/a\n");
        ST_CHECK(strstr(r, "qa_group=G\t0\nqa=A\t/a\nqa_group=H\t0\nqa=B\t/b\n") && !strstr(r, "/z"));
        free(r);
        r = settings_merge("a=1\n", "a=0\n", NULL, NULL); // no file (yet): ours
        ST_CHECK(!strcmp(r, "a=1\n"));
        free(r);
        // for real: another process changes title_bar while this one changes dim_inactive; both survive
        char sp[PATH_MAX], text[65536];
        settings_file(sp, sizeof sp, false);
        settings_save();
        settings_snapshot();
        st_read_text(sp, text, sizeof text);
        char *tb = strstr(text, "title_bar=auto");
        ST_CHECK(tb != NULL);
        if (tb) memcpy(tb, "title_bar=app ", 14); // "another process" ("app " + the line's own newline)
        st_write_text(sp, text);
        bool dim = A.dim_inactive;
        A.dim_inactive = !dim;
        settings_save();
        st_read_text(sp, text, sizeof text);
        ST_CHECK(strstr(text, "title_bar=app") && strstr(text, A.dim_inactive ? "dim_inactive=1" : "dim_inactive=0"));
        ST_CHECK(strstr(text, "settings_version=") != NULL);
        A.dim_inactive = dim;
        // a file from before settings_version: backed up on load (settings_migrate runs on the original)
        char bak[PATH_MAX + 8];
        snprintf(bak, sizeof bak, "%s.bak", sp);
        unlink(bak);
        char *v = strstr(text, "settings_version=");
        if (v) memcpy(v, "#", 1);
        st_write_text(sp, text);
        settings_load();
        ST_CHECK(access(bak, F_OK) == 0);
        settings_save(); // (writes the version again)
        st_read_text(sp, text, sizeof text);
        ST_CHECK(strstr(text, "\nsettings_version=") != NULL);
        A.title_bar = TITLEBAR_AUTO;
        settings_save();
        // an update replaced the binary while running: the old process notices
        char l1[] = "/usr/bin/file-explorer (deleted)", l2[] = "/usr/bin/file-explorer";
        ST_CHECK(exe_link_replaced(l1) && !strcmp(l1, "/usr/bin/file-explorer") && !exe_link_replaced(l2));
    }

    fprintf(stderr, "selftest-m10: %s (%d failures)\n", st_fail ? "FAIL" : "ok", st_fail);
    return st_fail ? 1 : 0;
}

// --- self-test for git status: parser, badges, path bar chip + details, refresh, the features toggle, branch pills (stand-in git) ---

#define GIT_BASE "/tmp/fe-git-test"
static const char g_st_git_out[] =
    "# branch.oid abcdef1234567890\0# branch.head main\0# branch.upstream origin/main\0# branch.ab +2 -1\0"
    "1 .M N... 100644 100644 100644 h1 h2 a.txt\0"
    "1 M. N... 100644 100644 100644 h1 h2 b.txt\0"
    "1 A. N... 000000 100644 100644 h0 h2 sub/c.txt\0"
    "2 R. N... 100644 100644 100644 h1 h2 R100 sub/e.txt\0sub/old.txt\0"
    "u UU N... 100644 100644 100644 100644 h1 h2 h3 conf.txt\0"
    "1 .D N... 100644 100644 000000 h1 h1 gone.txt\0"
    "? new.txt\0? nt/\0! ign.log\0";

static void st_git_output(const char *data, size_t len) { // what the stand-in `git status` prints next
    FILE *f = fopen(GIT_BASE "/tools/out", "wb");
    fwrite(data, 1, len, f);
    fclose(f);
}

static void st_setup_git(void) {
    st_rmrf(GIT_BASE);
    mkdir_p(GIT_BASE "/repo/.git/logs/refs", 0755);
    mkdir_p(GIT_BASE "/repo/sub/deeper", 0755);
    mkdir_p(GIT_BASE "/repo/nt", 0755);
    mkdir_p(GIT_BASE "/repo/inner/.git", 0755);
    mkdir_p(GIT_BASE "/plain", 0755);
    mkdir_p(GIT_BASE "/tools", 0755);
    const char *files[] = {"a.txt", "b.txt", "conf.txt", "new.txt", "ign.log", "clean.txt", "sub/c.txt", "sub/d.txt",
                           "sub/e.txt", "sub/deeper/f.txt", "nt/x.txt", "inner/i.txt", ".git/index"};
    for (size_t i = 0; i < ARRAY_COUNT(files); i++) {
        char p[PATH_MAX];
        snprintf(p, sizeof p, GIT_BASE "/repo/%s", files[i]);
        st_write(p, 10);
    }
    st_write_text(GIT_BASE "/repo/.git/HEAD", "ref: refs/heads/main\n");
    st_write_text(GIT_BASE "/repo/.git/logs/refs/stash", "0 1 me 1 +0000\tWIP one\n1 2 me 2 +0000\tWIP two\n");
    st_write_text(GIT_BASE "/repo/inner/.git/HEAD", "0123456789abcdef0123456789abcdef01234567\n");
    st_write_text(GIT_BASE "/repo/.git/config", "[core]\n\tbare = false\n[remote \"backup\"]\n\turl = https://gitlab.com/me/proj.git\n"
                                                "[remote \"origin\"]\n\turl = git@github.com:me/proj.git\n\tfetch = +refs/heads/*\n");
    mkdir_p(GIT_BASE "/wt", 0755); // a worktree: ".git" is a file pointing at the git folder
    st_write_text(GIT_BASE "/wt/.git", "gitdir: ../repo/.git\n");
    st_write(GIT_BASE "/plain/f.txt", 10);
    st_git_output(g_st_git_out, sizeof g_st_git_out - 1);
    FILE *f = fopen(GIT_BASE "/tools/git", "w");
    fputs("#!/bin/sh\necho \"$@\" >> " GIT_BASE "/tools/log\n"
          "case \" $* \" in\n"
          "*\" log \"*) printf 'abc1234\\037%s\\037Ada Lovelace\\037Fix the parser\\n' $(( $(date +%s) - 7300 )) ;;\n"
          "*\" config \"*) printf 'remote.origin.url git@github.com:me/proj.git\\nremote.backup.url https://u@gitlab.com/me/proj.git\\n' ;;\n"
          "*) cat " GIT_BASE "/tools/out ;;\nesac\n", f);
    fclose(f);
    chmod(GIT_BASE "/tools/git", 0755);
    setenv("FE_GIT", GIT_BASE "/tools/git", 1);
    // repositories in (pretend) GVfs network folders: SMB and SFTP; a stand-in ssh that runs the command here
    static const char *const net[] = {GIT_BASE "/gvfs/smb-share:server=nas,share=x/proj", GIT_BASE "/gvfs/sftp:host=vm,user=me,port=2222/srv/it's/proj"};
    for (int i = 0; i < 2; i++) {
        char d[PATH_MAX];
        snprintf(d, sizeof d, "%s/.git", net[i]);
        mkdir_p(d, 0755);
        snprintf(d, sizeof d, "%s/.git/HEAD", net[i]);
        st_write_text(d, "ref: refs/heads/main\n");
        snprintf(d, sizeof d, "%s/sub", net[i]);
        mkdir_p(d, 0755);
        snprintf(d, sizeof d, "%s/sub/c.txt", net[i]);
        st_write(d, 10);
    }
    f = fopen(GIT_BASE "/tools/ssh", "w");
    fputs("#!/bin/sh\necho \"ssh $*\" >> " GIT_BASE "/tools/log\nfor a; do last=$a; done\nexec sh -c \"$last\"\n", f);
    fclose(f);
    chmod(GIT_BASE "/tools/ssh", 0755);
    setenv("FE_SSH", GIT_BASE "/tools/ssh", 1);
}

static int st_git_count(const char *what) { // invocations of the stand-in git containing what
    static char buf[1 << 16];
    st_read_text(GIT_BASE "/tools/log", buf, sizeof buf);
    int n = 0;
    for (char *c = buf; (c = strstr(c, what)); c++) n++;
    return n;
}

static int st_git_status_runs(void) { return st_git_count(" status --porcelain"); }

static bool st_git_wait(Pane *p, GitView *v) {
    for (int k = 0; k < 400; k++) {
        st_frames(1);
        if (!git_busy() && git_view(p, v) && v->g) return true;
        SDL_Delay(2);
    }
    return false;
}

static void st_git_settle(void) {
    for (int k = 0; k < 400 && git_busy(); k++) { st_frames(1); SDL_Delay(2); }
    st_frames(1);
}

static u8 st_gs(const GitResult *g, const char *prefix, const char *name) { return git_state(g, prefix, name, strlen(name)); }

static const char *st_menu_value(const char *key) {
    int i = st_menu_find(key);
    return i >= 0 ? A.menu.items[i].shortcut : "";
}

static const GitSub *st_git_sub(Pane *p, const char *name) { // the branch pill of a folder in the listing
    for (u32 i = 0; p->L && i < p->L->count; i++)
        if (!strcmp(p->L->entries[i].name, name)) return git_entry_sub(p, &p->L->entries[i]);
    return NULL;
}

static int run_selftest_git(void) {
    st_wait();
    // --- parser: states, folders get the strongest state inside, ignored folders don't spread, counts
    GitResult *g = git_parse(g_st_git_out, sizeof g_st_git_out - 1);
    ST_CHECK(g->repo && !strcmp(g->branch, "main") && !g->detached && !strcmp(g->upstream, "origin/main") && g->ahead == 2 && g->behind == 1);
    ST_CHECK(st_gs(g, "", "a.txt") == GS_MODIFIED && st_gs(g, "", "b.txt") == GS_STAGED && st_gs(g, "", "conf.txt") == GS_CONFLICT);
    ST_CHECK(st_gs(g, "", "new.txt") == GS_UNTRACKED && st_gs(g, "", "ign.log") == GS_IGNORED && st_gs(g, "", "clean.txt") == GS_NONE);
    ST_CHECK(st_gs(g, "", "gone.txt") == GS_DELETED && st_gs(g, "", "sub") == GS_RENAMED && st_gs(g, "", "sub/c.txt") == GS_ADDED);
    ST_CHECK(st_gs(g, "", "sub/old.txt") == GS_NONE && st_gs(g, "", "sub/d.txt") == GS_NONE && st_gs(g, "", "sub/e.txt") == GS_RENAMED);
    ST_CHECK(st_gs(g, "", "nt") == GS_UNTRACKED && st_gs(g, "", "nt/x.txt") == GS_UNTRACKED && st_gs(g, "", "su") == GS_NONE);
    ST_CHECK(st_gs(g, "sub/", "c.txt") == GS_ADDED && st_gs(g, "sub/", "e.txt") == GS_RENAMED && st_gs(g, "sub/", "a.txt") == GS_NONE);
    ST_CHECK(st_gs(g, "nt/deep/", "anything") == GS_UNTRACKED && st_gs(g, "sub/", "deeper") == GS_NONE);
    ST_CHECK(g->nmodified == 2 && g->nstaged == 3 && g->nuntracked == 2 && g->nconflicts == 1);
    ST_CHECK(git_count_under(g, "") == 8 && git_count_under(g, "sub/") == 2 && git_count_under(g, "nt/") == 0);
    git_result_free(g);
    static const char det[] = "# branch.oid 0123456789abcdef\0# branch.head (detached)\0";
    g = git_parse(det, sizeof det - 1);
    ST_CHECK(g->repo && g->detached && !strcmp(g->branch, "0123456") && !g->upstream[0] && !g->n);
    git_result_free(g);
    static const char init[] = "# branch.oid (initial)\0# branch.head main\0? x\0";
    g = git_parse(init, sizeof init - 1);
    ST_CHECK(g->repo && !strcmp(g->branch, "main") && st_gs(g, "", "x") == GS_UNTRACKED);
    git_result_free(g);
    static const char bad[] = "fatal: not a git repository";
    g = git_parse(bad, sizeof bad - 1);
    ST_CHECK(!g->repo && !g->n && st_gs(g, "", "x") == GS_NONE);
    git_result_free(g);
    // remotes as host/path + a web page; the root as a folder's path spells it
    char disp[320], web[340], vr[PATH_MAX];
    git_remote_web("git@github.com:me/proj.git", disp, sizeof disp, web, sizeof web);
    ST_CHECK(!strcmp(disp, "github.com/me/proj") && !strcmp(web, "https://github.com/me/proj"));
    git_remote_web("ssh://git@host.example:2222/me/x/", disp, sizeof disp, web, sizeof web);
    ST_CHECK(!strcmp(disp, "host.example/me/x") && !strcmp(web, "https://host.example/me/x"));
    git_remote_web("https://u@gitlab.com/me/proj.git", disp, sizeof disp, web, sizeof web);
    ST_CHECK(!strcmp(disp, "gitlab.com/me/proj") && !strcmp(web, "https://gitlab.com/me/proj"));
    git_remote_web("/srv/repos/x.git", disp, sizeof disp, web, sizeof web);
    ST_CHECK(!strcmp(disp, "/srv/repos/x.git") && !web[0]);
    git_vroot("/home/me/link/src/lib", "src/lib/", vr, sizeof vr);
    ST_CHECK(!strcmp(vr, "/home/me/link"));
    git_vroot("/home/me/r", "", vr, sizeof vr);
    ST_CHECK(!strcmp(vr, "/home/me/r"));
    git_vroot("/a", "a/", vr, sizeof vr);
    ST_CHECK(!strcmp(vr, "/"));
    git_vroot("/home/me/other", "src/", vr, sizeof vr);
    ST_CHECK(!vr[0]);

    // --- in the app: shows up within a few frames, whole-repository status with safe flags, details
    Pane *p = active_pane();
    GitView v;
    int frames = 0;
    while (frames < 400 && !(git_view(p, &v) && v.g)) { st_frames(1); frames++; SDL_Delay(1); }
    ST_CHECK(frames < 100 && v.g && v.repo && !v.prefix[0]);
    ST_CHECK(st_git_wait(p, &v) && st_gs(v.g, v.prefix, "a.txt") == GS_MODIFIED && st_gs(v.g, v.prefix, "sub") == GS_RENAMED);
    char log[16384];
    st_read_text(GIT_BASE "/tools/log", log, sizeof log);
    ST_CHECK(strstr(log, "-c core.fsmonitor=false --no-optional-locks -C " GIT_BASE "/repo status --porcelain=v2 -z --branch "
                         "--untracked-files=normal --ignored=matching\n") != NULL);
    ST_CHECK(!strcmp(v.g->last_hash, "abc1234") && !strcmp(v.g->last_author, "Ada Lovelace") && !strcmp(v.g->last_subject, "Fix the parser"));
    ST_CHECK(v.g->nremotes == 2 && !strcmp(v.g->remotes[0].name, "origin") && !strcmp(v.g->remotes[1].url, "https://u@gitlab.com/me/proj.git"));
    ST_CHECK(v.g->nstash == 2 && !strcmp(v.repo->head, "main"));
    char br[160], rest[96];
    ST_CHECK(git_status_label(p, br, sizeof br, rest, sizeof rest) && !strcmp(br, "main"));
    ST_CHECK(!strcmp(rest, "   \xC2\xB7   8 changed"));
    st_shot("git-details");

    // --- moving inside the repository: the badges are there in the very first frame (no wait for git)
    pane_navigate(p, GIT_BASE "/repo/sub", true);
    st_frames(1);
    ST_CHECK(git_view(p, &v) && v.g && !strcmp(v.prefix, "sub/") && st_gs(v.g, v.prefix, "c.txt") == GS_ADDED);
    ST_CHECK(git_status_label(p, br, sizeof br, rest, sizeof rest) && strstr(rest, "2 changed"));
    pane_navigate(p, GIT_BASE "/repo/sub/deeper", true);
    st_frames(1);
    ST_CHECK(git_view(p, &v) && v.g && !strcmp(v.prefix, "sub/deeper/"));
    st_git_settle();
    // a nested repository takes over once it's resolved
    pane_navigate(p, GIT_BASE "/repo/inner", true);
    st_wait();
    st_git_settle();
    ST_CHECK(git_view(p, &v) && v.repo && !strcmp(path_basename(v.repo->root), "inner") && !v.prefix[0]);
    pane_navigate(p, GIT_BASE "/repo/sub", true);
    st_wait();
    st_git_settle();

    // --- the path bar chip: branch + ahead/behind; a click shows the repository details and actions
    layout();
    st_frames(2);
    ST_CHECK(p->ly.has_gitchip);
    Rect gc = p->ly.gitchip;
    ST_CHECK(gc.x + gc.w <= p->ly.pathbar.x + p->ly.pathbar.w && gc.x > p->ly.crumbs[p->ly.ncrumbs - 1].r.x);
    char ct[200];
    bool dirty;
    ST_CHECK(git_chip_text(p, ct, sizeof ct, &dirty) && dirty && !strcmp(ct, "main"));
    int ah, bh;
    git_sync_counts(p, &ah, &bh);
    ST_CHECK(ah == 2 && bh == 1);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, gc.x + gc.w * 0.5f, gc.y + gc.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, gc.x + gc.w * 0.5f, gc.y + gc.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(A.menu.open && A.menu.n && A.menu.items[0].info && !A.edit_pane);
    ST_CHECK(!strcmp(st_menu_value("Repository"), "repo") && !strcmp(st_menu_value("Folder"), "sub"));
    ST_CHECK(!strcmp(st_menu_value("Branch"), "main \xE2\x86\x92 origin/main"));
    ST_CHECK(!strcmp(st_menu_value("Sync"), "2 to push \xC2\xB7 1 to pull"));
    ST_CHECK(!strcmp(st_menu_value("Changes"), "2 modified \xC2\xB7 3 staged \xC2\xB7 2 untracked \xC2\xB7 1 conflict"));
    ST_CHECK(!strcmp(st_menu_value("Stashes"), "2") && !strcmp(st_menu_value("Remote"), "origin  github.com/me/proj"));
    ST_CHECK(strstr(st_menu_value("Last commit"), "abc1234 \xC2\xB7 2 hours ago \xC2\xB7 Ada Lovelace") != NULL);
    st_shot("git-chip-menu");
    int oi = st_menu_find("Open remote in browser");
    ST_CHECK(oi >= 0);
    if (oi >= 0) menu_activate(oi);
    ST_CHECK(!strcmp(A.last_opened, "https://github.com/me/proj"));
    menu_open_git(p, 0, 0);
    int ri = st_menu_find("Go to repository root");
    ST_CHECK(ri >= 0);
    if (ri >= 0) menu_activate(ri);
    st_wait();
    ST_CHECK(!strcmp(p->path, GIT_BASE "/repo"));
    st_git_settle();
    menu_open_git(p, 0, 0);
    ST_CHECK(st_menu_find("Go to repository root") < 0 && st_menu_find("Folder") < 0); // already there
    A.menu.open = false;

    // --- refresh: the 2 s .git check runs git only when .git changed; a reload or regained focus always does
    int runs = st_git_status_runs();
    for (int i = 0; i < GIT_REPOS; i++) { g_repos[i].checked = 0; g_repos[i].full = now_ns(); }
    st_frames(1);
    st_git_settle();
    ST_CHECK(st_git_status_runs() == runs);
    // the periodic re-check (files edited in folders nobody watches): status only, the details are kept
    int logs = st_git_count(" log -1");
    for (int i = 0; i < GIT_REPOS; i++) g_repos[i].full = 0;
    st_frames(1);
    ST_CHECK(st_git_wait(p, &v) && st_git_status_runs() == runs + 1 && st_git_count(" log -1") == logs);
    ST_CHECK(!strcmp(v.g->last_hash, "abc1234") && v.g->nremotes == 2 && v.g->nstash == 2 && v.g->has_details);
    ST_CHECK(v.repo->full > 0 && now_ns() - v.repo->full < 2000000000ull);
    for (int i = 0; i < GIT_REPOS; i++) g_repos[i].full = now_ns() - 1900000000ull; // due in 100 ms
    SDL_Delay(150);
    st_frames(1);
    st_git_settle();
    ST_CHECK(st_git_status_runs() == runs + 2);
    // big repositories are re-checked less often: 50x as long as git takes there
    ST_CHECK(git_view(p, &v) && v.g);
    if (v.g) ((GitResult *)v.g)->dur_ns = 100000000ull; // 100 ms -> every 5 s
    for (int i = 0; i < GIT_REPOS; i++) { g_repos[i].full = now_ns() - 3000000000ull; g_repos[i].checked = now_ns(); }
    st_frames(1);
    st_git_settle();
    ST_CHECK(st_git_status_runs() == runs + 2);
    // hidden / minimised window: no periodic checks, no wake-ups
    A.win_hidden = true;
    for (int i = 0; i < GIT_REPOS; i++) g_repos[i].full = g_repos[i].checked = 0;
    git_tick();
    st_git_settle();
    ST_CHECK(st_git_status_runs() == runs + 2 && git_wait_ms() < 0);
    A.win_hidden = false;
    git_tick();
    st_git_settle();
    ST_CHECK(st_git_status_runs() == runs + 3);
    runs = st_git_status_runs();
    for (int i = 0; i < GIT_REPOS; i++) g_repos[i].full = now_ns();
    static const char out2[] = "# branch.oid abcdef1234567890\0# branch.head main\0? new.txt\0";
    st_git_output(out2, sizeof out2 - 1);
    SDL_Delay(15);
    st_write(GIT_BASE "/repo/.git/index", 20);
    for (int i = 0; i < GIT_REPOS; i++) g_repos[i].checked = 0;
    st_frames(1);
    ST_CHECK(st_git_wait(p, &v) && st_gs(v.g, "", "a.txt") == GS_NONE && st_gs(v.g, "", "new.txt") == GS_UNTRACKED);
    ST_CHECK(st_git_status_runs() == runs + 1);
    ST_CHECK(git_status_label(p, br, sizeof br, rest, sizeof rest) && !strcmp(rest, "   \xC2\xB7   1 changed"));
    ST_CHECK(git_chip_text(p, ct, sizeof ct, &dirty) && dirty && !strcmp(ct, "main"));
    st_git_output(g_st_git_out, sizeof g_st_git_out - 1);
    st_key(SDLK_F5, 0);
    st_wait();
    ST_CHECK(st_git_wait(p, &v) && st_gs(v.g, "", "a.txt") == GS_MODIFIED && st_git_status_runs() == runs + 2);
    git_refresh_all();
    st_frames(1);
    ST_CHECK(st_git_wait(p, &v) && st_git_status_runs() == runs + 3);
    // list and grid views draw the badges too
    p->view = VIEW_LIST;
    st_frames(2);
    st_shot("git-list");
    p->view = VIEW_GRID;
    p->icon_px = 96;
    st_frames(2);
    st_shot("git-grid");
    p->view = VIEW_DETAILS;
    st_frames(1);

    // --- the features menu (sliders, next to the gear) switches it; the setting is saved
    menu_open_features(p, 0, 0);
    int gi = st_menu_find("Git status");
    ST_CHECK(gi >= 0 && A.menu.items[gi].checkable && A.menu.items[gi].checked && A.menu.items[gi].act == ACT_GIT_STATUS);
    st_shot("git-features-menu");
    if (gi >= 0) menu_activate(gi);
    ST_CHECK(!A.git_status && !git_view(p, &v) && !git_status_label(p, br, sizeof br, rest, sizeof rest));
    st_frames(2);
    ST_CHECK(!p->ly.has_gitchip);
    menu_open_features(p, 0, 0);
    ST_CHECK(gi >= 0 && !A.menu.items[gi].checked);
    A.menu.open = false;
    settings_save();
    char sp[PATH_MAX], sbuf[8192];
    settings_file(sp, sizeof sp, false);
    st_read_text(sp, sbuf, sizeof sbuf);
    ST_CHECK(strstr(sbuf, "\ngit_status=0\n") != NULL);
    int jobs = g_git_runs;
    st_key(SDLK_F5, 0); // off: reloads don't run git
    st_wait();
    st_frames(3);
    ST_CHECK(g_git_runs == jobs && git_wait_ms() < 0 && !git_busy());
    do_action(p, ACT_GIT_STATUS);
    ST_CHECK(st_git_wait(p, &v) && A.git_status && g_git_runs > jobs);

    // --- outside a repository: no git at all, no chip
    runs = st_git_status_runs();
    pane_navigate(p, GIT_BASE "/plain", true);
    st_wait();
    st_git_settle();
    st_frames(2);
    ST_CHECK(!git_view(p, &v) && st_git_status_runs() == runs && !git_status_label(p, br, sizeof br, rest, sizeof rest));
    ST_CHECK(!p->ly.has_gitchip);

    // --- folders that are repositories: a branch pill + the remote (HEAD and config read directly, no git)
    runs = st_git_status_runs();
    pane_navigate(p, GIT_BASE, true);
    st_wait();
    st_git_settle();
    st_frames(2);
    const GitSub *gsub = st_git_sub(p, "repo");
    ST_CHECK(gsub && !strcmp(gsub->branch, "main") && !gsub->detached && !strcmp(gsub->remote, "git@github.com:me/proj.git"));
    ST_CHECK(gsub && !strcmp(gsub->disp, "github.com/me/proj"));
    gsub = st_git_sub(p, "wt");
    ST_CHECK(gsub && !strcmp(gsub->branch, "main") && !strcmp(gsub->disp, "github.com/me/proj"));
    ST_CHECK(!st_git_sub(p, "plain") && !st_git_sub(p, "tools") && st_git_status_runs() == runs);
    st_shot("git-pills");
    int pview = p->view;
    p->view = VIEW_LIST;
    st_shot("git-pills-list");
    p->view = VIEW_GRID;
    st_shot("git-pills-grid");
    p->view = pview;
    pane_navigate(p, GIT_BASE "/repo", true);
    st_wait();
    st_git_settle();
    st_frames(2);
    gsub = st_git_sub(p, "inner");
    ST_CHECK(gsub && gsub->detached && !strcmp(gsub->branch, "0123456") && !gsub->remote[0]);
    ST_CHECK(!st_git_sub(p, "sub") && !st_git_sub(p, ".git"));
    A.git_status = false; // the feature off: no pills
    ST_CHECK(!st_git_sub(p, "inner"));
    A.git_status = true;
    // opening: text files without a known type go to the text editor
    ST_CHECK(file_looks_text(GIT_BASE "/repo/.git/HEAD") && !file_looks_text(GIT_BASE "/repo/a.txt") && !file_looks_text(GIT_BASE "/repo"));

    // --- SFTP folders: git runs on the server over ssh; other network folders are skipped
    setenv("FE_GVFS_DIR", GIT_BASE "/gvfs", 1);
    st_git_settle();
    runs = st_git_status_runs();
    pane_navigate(p, GIT_BASE "/gvfs/smb-share:server=nas,share=x/proj/sub", true);
    st_wait();
    st_git_settle();
    ST_CHECK(!git_view(p, &v) && st_git_status_runs() == runs && st_git_count("ssh ") == 0);
    pane_navigate(p, GIT_BASE "/gvfs/sftp:host=vm,user=me,port=2222/srv/it's/proj/sub", true);
    st_wait();
    ST_CHECK(st_git_wait(p, &v) && !strcmp(v.prefix, "sub/") && st_gs(v.g, v.prefix, "c.txt") == GS_ADDED);
    ST_CHECK(st_git_status_runs() == runs + 1 && !strcmp(v.g->last_hash, "abc1234") && v.g->nremotes == 2);
    st_read_text(GIT_BASE "/tools/log", log, sizeof log);
    ST_CHECK(strstr(log, "ssh -o BatchMode=yes ") && strstr(log, " -p 2222 -- me@vm '" GIT_BASE "/tools/git' '-c' 'core.fsmonitor=false'"));
    ST_CHECK(strstr(log, "-c core.fsmonitor=false --no-optional-locks -C /srv/it's/proj status --porcelain=v2") != NULL);
    unsetenv("FE_GVFS_DIR");

    fprintf(stderr, "selftest-git: %s (%d failures)\n", st_fail ? "FAIL" : "ok", st_fail);
    return st_fail ? 1 : 0;
}

// --- self-test for contents search: matcher, results + lines, options, folding, open at line, preview, ripgrep ----

#define GREP_BASE "/tmp/fe-grep-test"

static void st_setup_grep(void) {
    st_rmrf(GREP_BASE);
    mkdir_p(GREP_BASE "/tree/sub/deep", 0755);
    mkdir_p(GREP_BASE "/tree/.hidden", 0755);
    mkdir_p(GREP_BASE "/tree/.git", 0755);
    mkdir_p(GREP_BASE "/many", 0755);
    mkdir_p(GREP_BASE "/data/applications", 0755);
    mkdir_p(GREP_BASE "/data2", 0755);
    mkdir_p(GREP_BASE "/tools", 0755);
    st_write_text(GREP_BASE "/tree/a.c", "int x;\n\nchar *needle = 0;\nint y;\n\nvoid f(void) {\n    find(needle);\n}\n// Needle too\n");
    st_write_text(GREP_BASE "/tree/plain.txt", "nothing to see here\n");
    char longl[400];
    memset(longl, 'x', 200);
    snprintf(longl + 200, sizeof longl - 200, " needle at the end\n");
    char b[1024];
    snprintf(b, sizeof b, "the needle_x is not a word match\n\t\ttabbed needle\n%s", longl);
    st_write_text(GREP_BASE "/tree/sub/b.txt", b);
    FILE *f = fopen(GREP_BASE "/tree/sub/deep/big.md", "w"); // > 64 KB, the match far down
    for (int i = 1; i < 3000; i++) fprintf(f, "line %d of filler text, nothing special in it\n", i);
    fprintf(f, "here is the needle on line 3000\n");
    for (int i = 0; i < 50; i++) fprintf(f, "after\n");
    fclose(f);
    f = fopen(GREP_BASE "/tree/bin.dat", "wb");
    fwrite("needle\0\x01\x02 needle", 1, 16, f);
    fclose(f);
    st_write_text(GREP_BASE "/tree/.hidden/h.txt", "hidden needle\n");
    st_write_text(GREP_BASE "/tree/.git/config", "needle in git\n");
    for (int i = 0; i < 205; i++) { // 205 files x 100 lines: more than the 20,000-line limit
        char p[PATH_MAX];
        snprintf(p, sizeof p, GREP_BASE "/many/f%03d.txt", i);
        f = fopen(p, "w");
        for (int k = 0; k < 120; k++) fputs("hay needle hay\n", f);
        fclose(f);
    }
    // a known editor as the default for text (never started: tests record what would run)
    st_write_text(GREP_BASE "/data/applications/code.desktop",
                  "[Desktop Entry]\nType=Application\nName=Code\nExec=/usr/bin/code --unity-launch %F\nMimeType=text/plain;\n");
    st_write_text("/tmp/fe-selftest-config/mimeapps.list", "[Default Applications]\ntext/plain=code.desktop\ntext/x-csrc=code.desktop\n");
    // freedesktop lookups beyond /usr: types and apps from $XDG_DATA_DIRS, desktop-specific defaults, removed
    // associations, entries for other desktops or missing programs, console apps
    mkdir_p(GREP_BASE "/data2/mime", 0755);
    mkdir_p(GREP_BASE "/data2/applications", 0755);
    st_write_text(GREP_BASE "/data2/mime/globs2", "50:application/x-fe-test:*.fetest\n");
    static const char *fe_apps[][2] = {
        {"fe-a", ""}, {"fe-b", ""}, {"fe-removed", ""}, {"fe-other", "OnlyShowIn=SomeOtherDesktop;\n"},
        {"fe-missing", "TryExec=/nonexistent/fe-prog\n"}, {"fe-console", "Terminal=true\n"}};
    for (size_t i = 0; i < ARRAY_COUNT(fe_apps); i++) {
        char p[PATH_MAX], t[512];
        snprintf(p, sizeof p, GREP_BASE "/data2/applications/%s.desktop", fe_apps[i][0]);
        snprintf(t, sizeof t, "[Desktop Entry]\nType=Application\nName=%s\nExec=/bin/true %%f\nMimeType=application/x-fe-test;\n%s",
                 fe_apps[i][0], fe_apps[i][1]);
        st_write_text(p, t);
    }
    st_write_text(GREP_BASE "/data2/applications/fetestdesk-mimeapps.list", "[Default Applications]\napplication/x-fe-test=fe-b.desktop\n");
    // integration: us and another file manager for folders (the other one is the system's default)
    st_write_text(GREP_BASE "/data2/applications/dev.fileexplorer.desktop",
                  "[Desktop Entry]\nType=Application\nName=File Explorer\nExec=/bin/true %F\nMimeType=inode/directory;\n");
    st_write_text(GREP_BASE "/data2/applications/fake-files.desktop",
                  "[Desktop Entry]\nType=Application\nName=Fake Files\nExec=/bin/true %U\nMimeType=inode/directory;\n");
    st_write_text(GREP_BASE "/data2/applications/mimeapps.list",
                  "[Default Applications]\napplication/x-fe-test=fe-a.desktop\ninode/directory=fake-files.desktop\n"
                  "[Removed Associations]\napplication/x-fe-test=fe-removed.desktop;\n");
    mkdir_p("/tmp/fe-selftest-config/xdg-desktop-portal", 0755);
    st_write_text("/tmp/fe-selftest-config/xdg-desktop-portal/fetestdesk-portals.conf",
                  "[preferred]\ndefault=gtk\norg.freedesktop.impl.portal.FileChooser=gtk\n");
    unlink("/tmp/fe-selftest-config/xdg-desktop-portal/fetestdesk-portals.conf.fe-backup");
    unlink("/tmp/fe-selftest-config/xdg-desktop-portal-termfilechooser/config");
    unlink("/tmp/fe-selftest-config/xdg-desktop-portal-termfilechooser/config.fe-backup");
    setenv("FE_TEST_TERMFILECHOOSER", "1", 1);
    setenv("XDG_CURRENT_DESKTOP", "FETestDesk", 1);
    setenv("XDG_DATA_HOME", GREP_BASE "/data", 1);
    setenv("XDG_DATA_DIRS", GREP_BASE "/data2", 1);
    // stand-in ripgrep: logs its arguments, prints two matches the way rg --null does
    f = fopen(GREP_BASE "/tools/rg", "w");
    fputs("#!/bin/sh\necho \"$PWD|$*\" >> " GREP_BASE "/tools/rg.log\n"
          "printf '%s\\0%s\\n' ./sub/b.txt '2:9:\t\ttabbed needle' ./a.c '3:7:char *needle = 0;' ./a.c '7:10:    find(needle);'\n", f);
    fclose(f);
    chmod(GREP_BASE "/tools/rg", 0755);
    unsetenv("FE_RG");
    unsetenv("VISUAL");
    unsetenv("EDITOR");
}

static bool st_cs_done(void) {
    Pane *p = active_pane();
    return p->L && p->L->is_content && !p->L_next && atomic_load(&p->L->state) == LST_COMPLETE && !p->sort_pending;
}

static void st_cs_search(const char *q) { // type a query in the (contents) search bar and wait for the results
    Pane *p = active_pane();
    search_focus(p);
    te_set(&A.search_te, "", 0, 0);
    st_type(q);
    search_apply(p, true);
    st_wait_cond(st_cs_done, 5000);
    st_frames(2);
}

static i64 st_cs_row(Pane *p, const char *file, u32 line) { // row of a file (line 0) or of one of its lines
    for (u32 i = 0; i < p->count; i++) {
        Entry *e = &p->L->entries[p->order[i]];
        if (strcmp(e->name, file)) continue;
        if (!line && !(e->flags & ENT_MATCH)) return i;
        if (line && (e->flags & ENT_MATCH) && e->size == line) return i;
    }
    return -1;
}

static u32 st_cs_lines(Pane *p, const char *file) {
    i64 r = st_cs_row(p, file, 0);
    return r < 0 ? 0 : p->L->cinfo[p->order[r]] & ~CI_COLLAPSED;
}

static bool st_cq(const char *q, bool mc, bool w, bool re, const char *s, size_t want_at, size_t want_len) {
    CQuery c;
    bool ok = cq_init(&c, q, mc, w, re);
    size_t ms = 0, ml = 0;
    bool hit = ok && cq_find(&c, s, strlen(s), 0, &ms, &ml);
    cq_free(&c);
    if (want_at == (size_t)-1) return !hit;
    return hit && ms == want_at && ml == want_len;
}

static int run_selftest_grep(void) {
    st_wait();
    Pane *p = active_pane();
    // --- matcher: smart case, match case, whole word, regex; the query's in: filter
    ST_CHECK(st_cq("needle", false, false, false, "a NEEDLE here", 2, 6));
    ST_CHECK(st_cq("Needle", false, false, false, "a needle, a Needle", 12, 6)); // a capital: case matters
    ST_CHECK(st_cq("needle", true, false, false, "NEEDLE", (size_t)-1, 0));
    ST_CHECK(st_cq("needle", false, true, false, "needle_x needles needle!", 17, 6));
    ST_CHECK(st_cq("ne+dle$", false, false, true, "a neeedle", 2, 7));
    ST_CHECK(st_cq("^int", false, false, true, "x int\nint y", 6, 3)); // REG_NEWLINE: ^ at each line
    ST_CHECK(st_cq("lo", false, true, true, "hello lo", 6, 2));
    CQuery bad;
    ST_CHECK(!cq_init(&bad, "(", false, false, true));
    cq_free(&bad);
    char text[256], glob[128];
    content_split_query("needle in:*.c", text, sizeof text, glob, sizeof glob);
    ST_CHECK(!strcmp(text, "needle") && !strcmp(glob, "*.c"));
    content_split_query("in:src/*.h two words ", text, sizeof text, glob, sizeof glob);
    ST_CHECK(!strcmp(text, "two words") && !strcmp(glob, "src/*.h"));
    content_split_query("main(", text, sizeof text, glob, sizeof glob);
    ST_CHECK(!strcmp(text, "main(") && !glob[0]);
    char *av[8], buf[2][PATH_MAX + 32];
    ST_CHECK(editor_style("code.desktop") == ED_GOTO && editor_style("org.kde.kate.desktop") == ED_KATE &&
             editor_style("org.gnome.TextEditor.desktop") == ED_PLUS_COL && editor_style("dev.zed.Zed.desktop") == ED_COLON &&
             editor_style("org.gnome.gedit.desktop") == ED_PLUS && editor_style("libreoffice-writer.desktop") < 0);
    editor_argv(ED_KATE, "kate", "/x/a.c", 12, 5, av, buf);
    ST_CHECK(!strcmp(av[1], "-l") && !strcmp(av[2], "12") && !strcmp(av[3], "-c") && !strcmp(av[4], "4") && !strcmp(av[5], "/x/a.c") && !av[6]);
    editor_argv(ED_GOTO, "code", "/x/a.c", 12, 5, av, buf);
    ST_CHECK(!strcmp(av[1], "-g") && !strcmp(av[2], "/x/a.c:12:5") && !av[3]);

    // --- Ctrl+Shift+F: the bar searches inside files (built-in search in tests); files followed by their lines
    p->view = VIEW_GRID; // contents results need rows: back to the grid afterwards
    st_key(SDLK_F, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
    ST_CHECK(A.search_contents && A.search_pane == p && p->ly.has_sbar);
    A.search_recursive = true;
    st_cs_search("needle");
    ST_CHECK(st_cs_done() && p->view == VIEW_DETAILS && p->cs_view == VIEW_GRID + 1);
    Listing *L = p->L;
    ST_CHECK(L->nfiles == 3 && L->nmatches == 7); // a.c 3, sub/b.txt 3, sub/deep/big.md 1; not bin.dat, .hidden, .git
    ST_CHECK(st_cs_lines(p, "a.c") == 3 && st_cs_lines(p, "sub/b.txt") == 3 && st_cs_lines(p, "sub/deep/big.md") == 1);
    ST_CHECK(st_cs_row(p, "bin.dat", 0) < 0 && st_cs_row(p, ".hidden/h.txt", 0) < 0 && st_cs_row(p, "plain.txt", 0) < 0);
    i64 r3 = st_cs_row(p, "a.c", 3), r0 = st_cs_row(p, "a.c", 0);
    ST_CHECK(r0 >= 0 && r3 == r0 + 1 && st_cs_row(p, "a.c", 7) == r0 + 2 && st_cs_row(p, "a.c", 9) == r0 + 3);
    ST_CHECK(r0 == 0 && st_cs_row(p, "sub/b.txt", 0) == 4 && st_cs_row(p, "sub/deep/big.md", 0) == 8); // sorted by path
    if (r3 >= 0) {
        u32 ei = p->order[r3];
        Entry *e = &L->entries[ei];
        ST_CHECK(!strcmp(L->ctext[ei], "char *needle = 0;") && CM_COL(e) == 6 && CM_LEN(e) == 6 && CM_RCOL(e) == 6 && !CM_ELIDED(e));
    }
    i64 rt = st_cs_row(p, "sub/b.txt", 2); // indentation trimmed, the column follows
    ST_CHECK(rt >= 0 && !strcmp(L->ctext[p->order[rt]], "tabbed needle") && CM_COL(&L->entries[p->order[rt]]) == 7 &&
             CM_RCOL(&L->entries[p->order[rt]]) == 9);
    i64 rl = st_cs_row(p, "sub/b.txt", 3); // far right on a long line: what's around it, start elided
    ST_CHECK(rl >= 0 && CM_ELIDED(&L->entries[p->order[rl]]) && CM_RCOL(&L->entries[p->order[rl]]) == 201 &&
             !strncmp(L->ctext[p->order[rl]] + CM_COL(&L->entries[p->order[rl]]), "needle", 6));
    st_shot("grep-results");
    // the folders' sizes and the status bar don't count line rows as files
    p->cursor = r0;
    sel_clear(p);
    p->sel[p->order[r0]] = p->sel[p->order[r0 + 1]] = p->sel[p->order[r0 + 2]] = 1;
    p->sel_count = 3;
    int n;
    char **paths = pane_selection_paths(p, &n);
    ST_CHECK(n == 1 && !strcmp(paths[0], GREP_BASE "/tree/a.c"));
    for (int i = 0; i < n; i++) free(paths[i]);
    free(paths);
    sel_clear(p);

    // --- folding: Left on a file folds its lines, Right unfolds, Left on a line goes to its file
    A.search_pane = NULL;
    p->cursor = p->anchor = r0;
    u32 rows = p->count;
    st_key(SDLK_LEFT, 0);
    ST_CHECK(p->count == rows - 3 && (L->cinfo[p->order[r0]] & CI_COLLAPSED) && p->cursor == r0);
    st_key(SDLK_RIGHT, 0);
    ST_CHECK(p->count == rows && p->cursor == r0);
    st_key(SDLK_RIGHT, 0); // unfolded: steps onto its first line
    ST_CHECK(p->cursor == r0 + 1);
    st_key(SDLK_DOWN, 0);
    st_key(SDLK_LEFT, 0);
    ST_CHECK(p->cursor == r0);
    do_action(p, ACT_CS_FOLD_ALL);
    ST_CHECK(p->count == 3);
    do_action(p, ACT_CS_UNFOLD_ALL);
    ST_CHECK(p->count == rows);
    // a click on the chevron folds too
    layout();
    st_frames(1);
    f32 cy = st_row_y(p, "a.c");
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, p->ly.col_name + S(5), cy, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, p->ly.col_name + S(5), cy, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(p->count == rows - 3);
    content_fold(p, st_cs_row(p, "a.c", 0), 1);
    ST_CHECK(p->count == rows);
    // sorting keeps every file's lines under it
    pane_set_sort(p, SORT_SIZE);
    st_wait_cond(st_cs_done, 2000);
    st_frames(1);
    r0 = st_cs_row(p, "a.c", 0);
    ST_CHECK(r0 >= 0 && st_cs_row(p, "a.c", 3) == r0 + 1 && st_cs_row(p, "sub/b.txt", 2) == st_cs_row(p, "sub/b.txt", 0) + 2);
    pane_set_sort(p, SORT_NAME);
    st_wait_cond(st_cs_done, 2000);
    st_frames(1);

    // --- Enter on a line: the default editor at that line (code -g file:line:col); $VISUAL when none knows lines
    r3 = st_cs_row(p, "a.c", 7);
    p->cursor = r3;
    A.last_opened[0] = 0;
    st_key(SDLK_RETURN, 0);
    ST_CHECK(!strcmp(A.last_opened, "/usr/bin/code -g " GREP_BASE "/tree/a.c:7:10"));
    DesktopApp *code = app_by_id("code.desktop");
    ST_CHECK(code != NULL);
    if (code) code->terminal = true; // the default app can't take a line now
    setenv("VISUAL", "nvim", 1);
    A.last_opened[0] = 0;
    st_key(SDLK_RETURN, 0);
    ST_CHECK(!strcmp(A.last_opened, "term: nvim +7 " GREP_BASE "/tree/a.c"));
    unsetenv("VISUAL");
    A.last_opened[0] = 0;
    st_key(SDLK_RETURN, 0); // nothing that takes a line: opens the file normally
    ST_CHECK(!strcmp(A.last_opened, GREP_BASE "/tree/a.c"));
    if (code) code->terminal = false;
    r0 = st_cs_row(p, "a.c", 0);
    p->cursor = r0;
    A.last_opened[0] = 0;
    st_key(SDLK_RETURN, 0); // the file row itself: opened as usual
    ST_CHECK(!strcmp(A.last_opened, GREP_BASE "/tree/a.c"));

    // --- preview: the file around its match (a window far down a big file), the line highlighted
    if (!A.preview_on) preview_toggle();
    p->cursor = st_cs_row(p, "sub/deep/big.md", 3000);
    for (int k = 0; k < 200 && !(A.pv && A.pv->want_line == 3000); k++) { st_frames(1); SDL_Delay(3); }
    ST_CHECK(A.pv && A.pv->kind == PV_TEXT && A.pv->want_line == 3000 && A.pv->first_line == 2970 &&
             !strncmp(A.pv->text, "line 2970 ", 10));
    st_shot("grep-preview");
    p->cursor = st_cs_row(p, "sub/deep/big.md", 0); // a file row previews its first match
    for (int k = 0; k < 100 && A.pv_line != 3000; k++) st_frames(1);
    ST_CHECK(A.pv_line == 3000);
    p->cursor = st_cs_row(p, "a.c", 0);
    for (int k = 0; k < 200 && !(A.pv && A.pv->want_line == 3 && A.pv->first_line == 1); k++) { st_frames(1); SDL_Delay(3); }
    ST_CHECK(A.pv && A.pv->want_line == 3 && A.pv->first_line == 1);
    preview_toggle();
    // Quick Look on a match line far down: the text opens at the match (in the upper third), not at the window's top
    p->cursor = st_cs_row(p, "sub/deep/big.md", 3000);
    on_text(" ");
    for (int k = 0; k < 200 && !(A.ql_on && A.pv && A.pv->want_line == 3000 && A.ql_placed == A.pv); k++) { st_frames(1); SDL_Delay(3); }
    ST_CHECK(A.ql_on && A.ql_placed == A.pv && A.ql_scroll > 10 && A.ql_scroll < 30); // (line 2970 + scroll: 3000 a third down)
    st_shot("grep-quicklook");
    on_text(" ");
    ST_CHECK(!A.ql_on);

    // --- options: smart case, match case, whole word, regex, file filter, bad regex, hidden files
    st_cs_search("Needle");
    ST_CHECK(p->L->nfiles == 1 && p->L->nmatches == 1 && st_cs_row(p, "a.c", 9) >= 0);
    search_cs_option(p, 1); // whole word
    st_cs_search("needle");
    ST_CHECK(A.cs_word && st_cs_lines(p, "sub/b.txt") == 2 && st_cs_lines(p, "a.c") == 3);
    search_cs_option(p, 1);
    search_cs_option(p, 0); // match case
    st_cs_search("needle");
    ST_CHECK(A.cs_case && st_cs_lines(p, "a.c") == 2);
    search_cs_option(p, 0);
    search_cs_option(p, 2); // regex
    st_cs_search("need(le)? on");
    ST_CHECK(A.cs_regex && p->L->nfiles == 1 && st_cs_row(p, "sub/deep/big.md", 3000) >= 0);
    search_focus(p);
    te_set(&A.search_te, "(", 1, 1);
    search_apply(p, true);
    st_frames(1);
    ST_CHECK(!strcmp(A.cs_error, "Invalid regular expression"));
    st_shot("grep-bad-regex");
    search_cs_option(p, 2);
    st_cs_search("needle in:*.md");
    ST_CHECK(p->L->nfiles == 1 && st_cs_lines(p, "sub/deep/big.md") == 1 && !A.cs_error[0]);
    A.show_hidden = true;
    st_cs_search("needle");
    ST_CHECK(st_cs_row(p, ".hidden/h.txt", 0) >= 0 && st_cs_row(p, ".git/config", 0) < 0);
    A.show_hidden = false;
    search_toggle_recursive(p); // this folder only: searches again
    st_wait_cond(st_cs_done, 5000);
    st_frames(2);
    ST_CHECK(!A.search_recursive && p->L->nfiles == 1 && st_cs_row(p, "a.c", 0) >= 0);
    search_toggle_recursive(p);
    st_wait_cond(st_cs_done, 5000);

    // --- the result limit: 20,000 lines, then it stops and says so
    pane_navigate(p, GREP_BASE "/many", true);
    st_wait();
    st_cs_search("needle");
    ST_CHECK(p->L->ctrunc && p->L->nmatches == CS_MAX_LINES && p->L->nfiles >= 200 && p->L->nfiles <= 208);
    bool empty = false; // every file listed has lines (none added after the limit)
    for (u32 i = 0; i < p->L->count; i++)
        if (!(p->L->entries[i].flags & ENT_MATCH) && !(p->L->cinfo[i] & ~CI_COLLAPSED)) empty = true;
    ST_CHECK(!empty && (st_cs_lines(p, "f000.txt") == 0 || st_cs_lines(p, "f000.txt") == CS_FILE_LINES));
    pane_navigate(p, GREP_BASE "/tree", true);
    st_wait();
    ST_CHECK(!p->cs_view); // navigating away: the folder's own view

    // --- ripgrep, when installed: its output parsed, our flags passed
    setenv("FE_RG", GREP_BASE "/tools/rg", 1);
    A.search_pane = p;
    search_set_contents(p, true);
    st_cs_search("needle");
    ST_CHECK(p->L->nfiles == 2 && p->L->nmatches == 3 && st_cs_row(p, "a.c", 7) >= 0);
    rt = st_cs_row(p, "sub/b.txt", 2);
    ST_CHECK(rt >= 0 && !strcmp(p->L->ctext[p->order[rt]], "tabbed needle") && CM_RCOL(&p->L->entries[p->order[rt]]) == 9);
    char rlog[2048];
    st_read_text(GREP_BASE "/tools/rg.log", rlog, sizeof rlog);
    ST_CHECK(strstr(rlog, GREP_BASE "/tree|--null --line-number --column --no-heading --color never --no-messages") &&
             strstr(rlog, "--max-count 100 --ignore-case --fixed-strings --regexp needle -- .\n"));
    unsetenv("FE_RG");

    // --- Escape: back to the folder in the view it had (results switched the grid to rows)
    pane_navigate(p, GREP_BASE "/tree", true); // (from a plain folder in the grid)
    st_wait();
    p->view = VIEW_GRID;
    st_cs_search("needl");
    ST_CHECK(p->view == VIEW_DETAILS && p->cs_view == VIEW_GRID + 1);
    search_focus(p);
    st_key(SDLK_ESCAPE, 0);
    st_wait();
    ST_CHECK(!p->L->is_search && !p->search_q[0] && !A.search_pane && p->view == VIEW_GRID && !p->cs_view);
    p->view = VIEW_DETAILS;
    search_set_contents(p, false);
    ST_CHECK(!A.search_contents);
    settings_save();
    char spath[PATH_MAX], sbuf[8192];
    settings_file(spath, sizeof spath, false);
    st_read_text(spath, sbuf, sizeof sbuf);
    ST_CHECK(strstr(sbuf, "\nsearch_contents=0\nsearch_options=0,0,0\n") != NULL);
    unlink("/tmp/fe-selftest-config/mimeapps.list");

    { // --- freedesktop lookups beyond /usr (see st_setup_grep)
        ST_CHECK(!strcmp(mime_for_file("x.fetest", false), "application/x-fe-test")); // globs2 from $XDG_DATA_DIRS
        DesktopApp *d = app_default_for("application/x-fe-test");
        ST_CHECK(d && !strcmp(d->id, "fe-b.desktop")); // fetestdesk-mimeapps.list wins over mimeapps.list
        DesktopApp *list[64];
        int n = apps_for_mime("application/x-fe-test", list, 64);
        bool has_a = false, has_removed = false, has_other = false, has_missing = false, console = false;
        for (int i = 0; i < n; i++) {
            has_a |= !strcmp(list[i]->id, "fe-a.desktop");
            has_removed |= !strcmp(list[i]->id, "fe-removed.desktop");
            has_other |= !strcmp(list[i]->id, "fe-other.desktop");
            has_missing |= !strcmp(list[i]->id, "fe-missing.desktop");
        }
        DesktopApp *c = app_by_id("fe-console.desktop");
        console = c && c->terminal;
        ST_CHECK(has_a && !has_removed && !has_other && !has_missing && console);
        char dirs[8][PATH_MAX];
        setenv("FE_TEST_DIRS", "/a/:relative:/b::/a", 1);
        int nd = xdg_dirs("FE_TEST_DIRS", "/def", dirs, 8);
        ST_CHECK(nd == 2 && !strcmp(dirs[0], "/a") && !strcmp(dirs[1], "/b")); // trimmed, deduplicated, relative dropped
        nd = xdg_dirs("FE_TEST_UNSET", "/usr/local/share:/usr/share", dirs, 8);
        ST_CHECK(nd == 2 && !strcmp(dirs[1], "/usr/share"));
        // rename without replacing: an existing target stays, a free name works
        st_write_text(GREP_BASE "/rn-a", "a");
        st_write_text(GREP_BASE "/rn-b", "b");
        ST_CHECK(rename_noreplace(AT_FDCWD, GREP_BASE "/rn-a", AT_FDCWD, GREP_BASE "/rn-b") != 0 && errno == EEXIST);
        ST_CHECK(rename_noreplace(AT_FDCWD, GREP_BASE "/rn-a", AT_FDCWD, GREP_BASE "/rn-c") == 0);
    }
    { // --- integration (Options > Integration): every role is opt-in and turning it off puts back what was there
        char mp[PATH_MAX], text[8192];
        snprintf(mp, sizeof mp, "/tmp/fe-selftest-config/mimeapps.list");
        ST_CHECK(!ig_default_fm_on() && !strcmp(ig_folder_app(), "fake-files.desktop"));
        const OptDef *od = NULL;
        for (int i = 0; i < OPT_COUNT; i++) if (g_opts[i].id == OPT_IG_DEFAULT_FM) od = &g_opts[i];
        ST_CHECK(od && strstr(opt_desc(od), "Fake Files")); // the page says who has it now
        st_key(SDLK_COMMA, SDL_KMOD_CTRL); // the Options page itself
        g_opt.cat = OC_INTEGRATION;
        st_frames(2);
        st_shot("options-integration");
        st_key(SDLK_ESCAPE, SDL_KMOD_NONE);
        st_write_text(mp, "[Added Associations]\nx-test/keep=keep.desktop;\n"); // someone's file already there
        ig_default_fm_set(true);
        st_read_text(mp, text, sizeof text);
        ST_CHECK(ig_default_fm_on() && strstr(text, "inode/directory=dev.fileexplorer.desktop;") && !strcmp(A.ig_prev_fm, "fake-files.desktop"));
        ST_CHECK(strstr(text, "[Added Associations]\nx-test/keep=keep.desktop;\n") && strstr(text, "\n[Default Applications]\n")); // the rest kept
        ig_default_fm_set(false);
        st_read_text(mp, text, sizeof text);
        ST_CHECK(!ig_default_fm_on() && strstr(text, "inode/directory=fake-files.desktop;") && !strstr(text, "dev.fileexplorer"));
        // the file chooser: wrapper + termfilechooser config + the desktop's portals.conf; off puts the old one back
        char w[PATH_MAX + 64], t[PATH_MAX + 64], pc[PATH_MAX + 64];
        ig_picker_paths(w, t, pc, PATH_MAX + 64);
        ST_CHECK(strstr(pc, "/xdg-desktop-portal/fetestdesk-portals.conf") && !ig_picker_on());
        A.last_opened[0] = 0;
        ig_picker_set(true);
        st_read_text(pc, text, sizeof text);
        ST_CHECK(ig_picker_on() && strstr(text, "default=gtk\n") && strstr(text, "FileChooser=termfilechooser") && !strstr(text, "FileChooser=gtk"));
        ST_CHECK(access(w, X_OK) == 0 && st_file_has(w, "--pick") && st_file_has(t, w));
        ST_CHECK(!strncmp(A.last_opened, "run:systemctl", 13)); // the portal restarted to read it (recorded in tests)
        ig_picker_set(false);
        st_read_text(pc, text, sizeof text);
        ST_CHECK(!ig_picker_on() && strstr(text, "FileChooser=gtk") && access(w, F_OK) != 0 && access(t, F_OK) != 0);
        // new versions (AppImage): version compare, GitHub's answer, never looked for outside an AppImage
        ST_CHECK(ig_vercmp("v0.9.1", "0.9.0") > 0 && ig_vercmp("0.9.0", "0.9.0") == 0 && ig_vercmp("0.10.0", "0.9.9") > 0 &&
                 ig_vercmp("1.0", "1.0.1") < 0 && ig_vercmp("0.9.0", "0.9.0~git2026") == 0);
        char tag[64];
        ig_tag_from_json("{\"url\": \"x\", \"tag_name\": \"v1.2.3\", \"name\": \"1.2.3\"}", tag, sizeof tag);
        ST_CHECK(!strcmp(tag, "v1.2.3"));
        ig_tag_from_json("{\"message\": \"Not Found\"}", tag, sizeof tag);
        ST_CHECK(!tag[0]);
        i64 chk0 = A.ig_update_checked;
        ig_update_check(); // (self-test, not an AppImage: nothing happens)
        upd_check_now(true);
        ST_CHECK(A.ig_update_checked == chk0 && g_upd.state == UPD_IDLE);
        // the release's AppImage: its size in GitHub's answer, the checksum file
        char an[160];
        upd_asset_name("v1.2.3", an, sizeof an);
        ST_CHECK(!strcmp(an, "file-explorer-1.2.3-" UPD_ARCH ".AppImage"));
        char rel[512];
        snprintf(rel, sizeof rel, "{\"tag_name\":\"v1.2.3\",\"assets\":[{\"name\":\"%s.zsync\",\"size\":11},"
                 "{\"name\":\"%s\",\"label\":\"\",\"uploader\":{\"login\":\"x\"},\"size\": 4567,\"browser_download_url\":\"u/%s\"}]}", an, an, an);
        ST_CHECK(upd_asset_size(rel, an) == 4567 && upd_asset_size(rel, "file-explorer-9-x.AppImage") == 0);
        char sha[65];
        upd_sha_from_line("2FA9706CB1DF08B00019D9B90D2D4688EB70063B505212ABBD629B6113C03B48  file-explorer.AppImage\n", sha);
        ST_CHECK(!strcmp(sha, "2fa9706cb1df08b00019d9b90d2d4688eb70063b505212abbd629b6113c03b48"));
        upd_sha_from_line("<html>Not Found</html>", sha);
        ST_CHECK(!sha[0]);
        // how this copy updates: only the AppImage asks GitHub and installs; makepkg / source builds get a command to
        // copy; packages (any package manager) only show the version: no button, nothing to press, no request
        bool pr, en;
        setenv("FE_UPDATE_KIND", "appimage", 1);
        ST_CHECK(upd_can_install() && !upd_how_cmd() && upd_button(&pr, &en) && opt_hidden(OPT_UPD_HOW) && !opt_hidden(OPT_IG_UPDATES));
        setenv("FE_UPDATE_KIND", "arch", 1);
        ST_CHECK(!upd_can_install() && upd_how_cmd() && strstr(upd_how_cmd(), "makepkg") && !opt_hidden(OPT_UPD_HOW));
        setenv("FE_UPDATE_KIND", "source", 1);
        ST_CHECK(!upd_can_install() && upd_how_cmd() && strstr(upd_how_cmd(), "build.sh"));
        setenv("FE_UPDATE_KIND", "package", 1);
        ST_CHECK(!upd_can_install() && !upd_how_cmd() && !upd_button(&pr, &en) && opt_hidden(OPT_UPD_HOW) && opt_hidden(OPT_IG_UPDATES) &&
                 opt_hidden(OPT_UPD_AUTO) && !strcmp(upd_status(), "Updated by your package manager"));
        upd_button_press();
        ST_CHECK(g_upd.state == UPD_IDLE);
        unsetenv("FE_UPDATE_KIND");
        ST_CHECK(upd_kind() == UK_SOURCE); // (this test binary: build/ in the checkout)
        // "Show in folder": the activation file only while it's on; requests open folders / select items
        char svc[PATH_MAX + 64];
        ig_fm1_service_path(svc, sizeof svc);
        ig_fm1_set(true);
        ST_CHECK(A.ig_fm1 && st_file_has(svc, "Name=org.freedesktop.FileManager1") && st_file_has(svc, "--background"));
        ig_fm1_set(false);
        ST_CHECK(!A.ig_fm1 && access(svc, F_OK) != 0);
        int nt = A.ntabs;
        IgShow *sh = xcalloc(1, sizeof *sh);
        sh->paths = xmalloc(sizeof(char *));
        sh->paths[sh->n++] = strdup(GREP_BASE "/tree/sub/b.txt");
        ig_show_apply(sh);
        st_wait();
        Pane *ap = active_pane();
        ST_CHECK(A.ntabs == nt + 1 && !strcmp(ap->path, GREP_BASE "/tree/sub"));
    }
    fprintf(stderr, "selftest-grep: %s (%d failures)\n", st_fail ? "FAIL" : "ok", st_fail);
    return st_fail ? 1 : 0;
}

// --- self-test for the disk usage view: scan, mount rules, treemap + sunburst layout, picking, keys, ops, cache ---

#define DU_BASE "/tmp/fe-du-test"

static void st_setup_du(void) {
    st_rmrf(DU_BASE);
    st_rmrf("/tmp/fe-du-xdg");
    mkdir_p(DU_BASE "/tree/sub/deep", 0755);
    mkdir_p(DU_BASE "/tree/many", 0755);
    mkdir_p(DU_BASE "/tree/locked", 0755);
    mkdir_p("/tmp/fe-du-xdg", 0755);
    st_write(DU_BASE "/tree/big.bin", 4 << 20);
    st_write(DU_BASE "/tree/mid.mkv", 1 << 20);
    st_write(DU_BASE "/tree/tiny.txt", 100); // < 1% of the folder: summed with the small files
    st_write(DU_BASE "/tree/sub/deep/x.png", 512 << 10);
    st_write(DU_BASE "/tree/sub/y.txt", 8 << 10);
    if (link(DU_BASE "/tree/sub/y.txt", DU_BASE "/tree/sub/deep/y_link")) {} // counted once
    for (int i = 0; i < 40; i++) { // more files of one size than are kept by name
        char p[PATH_MAX];
        snprintf(p, sizeof p, DU_BASE "/tree/many/f%02d.dat", i);
        st_write(p, 4096 * (size_t)(1 + i % 3));
    }
    st_write(DU_BASE "/tree/locked/secret", 4096);
    chmod(DU_BASE "/tree/locked", 0);
    mkdir_p(DU_BASE "/in", 0755);
    st_write(DU_BASE "/in/new.bin", 64 << 10);
    setenv("XDG_DATA_HOME", "/tmp/fe-du-xdg", 1); // the trash
}

static u64 st_du_expect(const char *path, u64 *files, u64 *seen, int *nseen) { // what the scan should find
    struct stat st;
    if (lstat(path, &st)) return 0;
    u64 sz = (u64)st.st_blocks * 512;
    if (!S_ISDIR(st.st_mode)) {
        ++*files;
        if (st.st_nlink > 1) {
            for (int i = 0; i < *nseen; i++)
                if (seen[i] == (u64)st.st_ino) return 0;
            seen[(*nseen)++] = (u64)st.st_ino;
        }
        return sz;
    }
    DIR *d = opendir(path);
    if (!d) return sz;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (is_dot_or_dotdot(de->d_name)) continue;
        char c[PATH_MAX];
        path_join(c, sizeof c, path, de->d_name);
        sz += st_du_expect(c, files, seen, nseen);
    }
    closedir(d);
    return sz;
}

static bool st_du_ready(void) {
    Pane *p = active_pane();
    return p->dv && p->dv->s && !du_scan_running(p->dv->s) && p->dv->cur && !p->L_next && p->order;
}

static DuItem *st_du_kid(DuItem *it, const char *name) {
    DuItem *k = atomic_load(&it->kids);
    for (u32 i = 0; k && i < it->nkids; i++)
        if (k[i].name && !strcmp(k[i].name, name) && !k[i].removed) return &k[i];
    return NULL;
}

static DuCell *st_du_cell(DuView *v, const DuItem *it) {
    for (int i = 0; i < v->ncells; i++)
        if (v->cells[i].it == it) return &v->cells[i];
    return NULL;
}

static bool st_cmp_sug(void) { return g_sheet.cp_nsug > 0; }

static bool st_menu_has(int act) {
    for (int i = 0; i < A.menu.n; i++)
        if (A.menu.items[i].act == act && !A.menu.items[i].sep) return true;
    return false;
}

static int run_selftest_du(void) {
    st_wait();
    Pane *p = active_pane();

    // --- mount rules: btrfs subvolumes of the same disk are measured, snapshots / other drives / /proc aren't
    {
        DuScan t;
        memset(&t, 0, sizeof t);
        du_mounts_parse(&t, "21 1 0:31 /@ / rw,relatime shared:1 - btrfs /dev/nvme0n1p2 rw,ssd\n"
                            "22 21 0:33 /@home /home rw,relatime shared:2 - btrfs /dev/nvme0n1p2 rw,ssd,compress=zstd:1\n"
                            "23 21 0:34 /@snapshots /.snapshots rw shared:3 - btrfs /dev/nvme0n1p2 rw\n"
                            "24 21 259:1 / /boot rw shared:4 - vfat /dev/nvme0n1p1 rw\n"
                            "25 21 0:22 / /proc rw,nosuid - proc proc rw\n"
                            "26 22 0:50 / /home/me/remote rw - fuse.sshfs me@host:/ rw\n"
                            "27 21 8:17 /my\\040dir /mnt/usb\\040disk rw - ext4 /dev/sdb1 rw\n");
        ST_CHECK(t.nmounts == 7 && !strcmp(t.mounts[1].mnt, "/home") && !strcmp(t.mounts[3].fstype, "vfat"));
        ST_CHECK(!strcmp(t.mounts[6].mnt, "/mnt/usb disk") && !strcmp(t.mounts[6].root, "/my dir")); // (decoded)
        ST_CHECK(t.mounts[1].compressed && !t.mounts[0].compressed && !t.mounts[5].compressed);
        t.dev = makedev(0, 31);
        t.fstype = "btrfs";
        t.src = "/dev/nvme0n1p2";
        ST_CHECK(du_crosses(&t, makedev(0, 33), 0, "home"));
        ST_CHECK(!du_crosses(&t, makedev(0, 34), 0, ".snapshots") && !du_crosses(&t, makedev(0, 77), 0, ".snapshots"));
        ST_CHECK(!du_crosses(&t, makedev(259, 1), 0, "boot") && !du_crosses(&t, makedev(0, 22), 0, "proc"));
        ST_CHECK(!du_crosses(&t, makedev(0, 50), 0, "remote"));
        ST_CHECK(du_crosses(&t, makedev(0, 77), 0, "docker-subvol")); // a btrfs subvolume that isn't mounted
        // by mount id (what statx gives): btrfs devices are per subvolume and needn't match mountinfo's
        t.mnt_id = 21;
        ST_CHECK(du_crosses(&t, makedev(0, 90), 22, "home") && !du_crosses(&t, makedev(0, 90), 23, "snaps"));
        ST_CHECK(!du_crosses(&t, makedev(0, 33), 24, "boot") && !du_crosses(&t, makedev(0, 33), 25, "proc"));
        ST_CHECK(!du_crosses(&t, makedev(0, 33), 26, "remote") && !du_crosses(&t, makedev(0, 33), 99, "gone"));
        ST_CHECK(du_crosses(&t, makedev(0, 91), 21, "docker-subvol") && !du_crosses(&t, makedev(0, 91), 21, ".snapshots"));
        t.mnt_id = 0;
        t.fstype = "ext4";
        ST_CHECK(!du_crosses(&t, makedev(0, 77), 0, "other"));
        for (int i = 0; i < t.nmounts; i++) { free(t.mounts[i].root); free(t.mounts[i].mnt); free(t.mounts[i].fstype); free(t.mounts[i].src); }
        free(t.mounts);
        ST_CHECK(file_category("a.MKV", false) == DC_VIDEO && file_category("x.tar.gz", false) == DC_ARCHIVE &&
                 file_category(".bashrc", false) == DC_OTHER && file_category("run", true) == DC_EXEC);
    }

    // --- the view: Ctrl+Shift+U measures the folder; sizes are on-disk blocks, hard links once
    ST_CHECK(!p->du && p->ly.header.h > 0);
    st_key(SDLK_U, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
    ST_CHECK(p->du && pane_du_on(p) && p->dv && p->ly.header.h == 0);
    ST_CHECK(st_wait_cond(st_du_ready, 5000));
    st_frames(2);
    DuView *v = p->dv;
    DuScan *s = v->s;
    ST_CHECK(s && !strcmp(s->rootpath, DU_BASE "/tree") && v->cur == &s->root);
    u64 ef = 0, seen[64];
    int nseen = 0;
    chmod(DU_BASE "/tree/locked", 0755); // (what the scan couldn't read doesn't count)
    u64 locked = (u64)0;
    { struct stat st; if (!lstat(DU_BASE "/tree/locked/secret", &st)) locked = (u64)st.st_blocks * 512; }
    u64 expect = st_du_expect(DU_BASE "/tree", &ef, seen, &nseen);
    bool root_user = geteuid() == 0;
    if (!root_user) { expect -= locked; ef -= 1; }
    chmod(DU_BASE "/tree/locked", 0);
    ST_CHECK(atomic_load(&s->root.size) == expect && atomic_load(&s->root.files) == ef);
    { // bytes per type add up the tree; a folder full of one type takes its colour
        f32 share;
        DuItem *deep = du_find(s, DU_BASE "/tree/sub/deep");
        ST_CHECK(deep && atomic_load(&s->root.cats[DC_VIDEO]) == atomic_load(&st_du_kid(&s->root, "mid.mkv")->size) &&
                 du_main_category(deep, &share) == DC_IMAGE && share > 0.9f && du_main_category(&s->root, &share) == DC_OTHER);
    }
    ST_CHECK(atomic_load(&s->root.done) && (root_user || atomic_load(&s->nerr) == 1));
    DuItem *locked_it = st_du_kid(&s->root, "locked");
    ST_CHECK(locked_it && (root_user || locked_it->err));
    DuItem *many = st_du_kid(&s->root, "many"), *sub = st_du_kid(&s->root, "sub"), *big = st_du_kid(&s->root, "big.bin");
    DuItem *mid = st_du_kid(&s->root, "mid.mkv");
    ST_CHECK(many && sub && big && mid && big->kind == DK_FILE && mid->cat == DC_VIDEO && !st_du_kid(&s->root, "tiny.txt"));
    { // "many": the 24 biggest by name, the other 16 summed up
        DuItem *k = atomic_load(&many->kids);
        u32 nfile = 0, nsmall = 0;
        for (u32 i = 0; i < many->nkids; i++) {
            if (k[i].kind == DK_FILE) nfile++;
            if (k[i].kind == DK_SMALL) { nsmall++; ST_CHECK(atomic_load(&k[i].files) == 16); }
        }
        ST_CHECK(nfile == DU_KEEP && nsmall == 1 && atomic_load(&many->files) == 40);
    }
    ST_CHECK(du_find(s, DU_BASE "/tree/sub/deep") && du_find(s, DU_BASE "/tree/sub/deep/x.png") && !du_find(s, DU_BASE "/tree/nope") &&
             !du_find(s, DU_BASE "/treex") && du_find(s, DU_BASE "/tree") == &s->root);
    { char ip[PATH_MAX]; du_item_path(s, du_find(s, DU_BASE "/tree/sub/deep/x.png"), ip, sizeof ip); ST_CHECK(!strcmp(ip, DU_BASE "/tree/sub/deep/x.png")); }

    // --- the list: biggest first, other drives last
    ST_CHECK(v->nrows >= 5 && v->rows[0] == big && v->rows[1] == mid);
    for (int i = 1; i < v->nrows; i++) ST_CHECK(atomic_load(&v->rows[i - 1]->size) >= atomic_load(&v->rows[i]->size));

    // --- treemap: inside the map, siblings don't overlap, areas follow sizes; small files grouped
    A.du_style = 0;
    st_frames(1);
    ST_CHECK(v->ncells > 5);
    f64 ratio = -1;
    bool small_cell = false;
    for (int i = 0; i < v->ncells; i++) {
        DuCell *c = &v->cells[i];
        ST_CHECK(c->r.x >= v->map.x - 0.5f && c->r.y >= v->map.y - 0.5f && c->r.x + c->r.w <= v->map.x + v->map.w + 0.5f &&
                 c->r.y + c->r.h <= v->map.y + v->map.h + 0.5f);
        if (c->parent != &s->root) continue;
        if (c->kind == DUC_SMALL) small_cell = true;
        for (int j = i + 1; j < v->ncells; j++) {
            DuCell *d = &v->cells[j];
            if (d->parent != &s->root) continue;
            f32 ox = MIN(c->r.x + c->r.w, d->r.x + d->r.w) - MAX(c->r.x, d->r.x), oy = MIN(c->r.y + c->r.h, d->r.y + d->r.h) - MAX(c->r.y, d->r.y);
            ST_CHECK(ox <= 0 || oy <= 0);
        }
        if (c->r.w * c->r.h > 2000) {
            f64 r = (f64)(c->r.w + 1) * (c->r.h + 1) / (f64)c->size;
            if (ratio < 0) ratio = r;
            else ST_CHECK(fabs(r / ratio - 1) < 0.2);
        }
    }
    ST_CHECK(small_cell && ratio > 0);
    DuCell *bc = st_du_cell(v, big), *sc = st_du_cell(v, sub);
    ST_CHECK(bc && bc->kind == DUC_FILE && sc && sc->kind == DUC_FRAME && st_du_cell(v, du_find(s, DU_BASE "/tree/sub/deep")));

    // --- picking in the map: the list follows, the file list's cursor too (preview / Properties), and the selection
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, bc->r.x + bc->r.w * 0.5f, bc->r.y + bc->r.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
    ST_CHECK(v->pick == big && v->lcur == 0 && !strcmp(st_cursor_name(p), "big.bin"));
    {
        int n;
        char **paths = pane_selection_paths(p, &n);
        ST_CHECK(n == 1 && !strcmp(paths[0], DU_BASE "/tree/big.bin"));
        strv_free(paths, n);
    }
    DuItem *x = du_find(s, DU_BASE "/tree/sub/deep/x.png"); // deeper than the folder: still the selection
    DuCell *xc = st_du_cell(v, x);
    ST_CHECK(xc != NULL);
    if (xc) {
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, xc->r.x + xc->r.w * 0.5f, xc->r.y + xc->r.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
        int n;
        char **paths = pane_selection_paths(p, &n);
        ST_CHECK(v->pick == x && v->lcur == -1 && n == 1 && !strcmp(paths[0], DU_BASE "/tree/sub/deep/x.png"));
        strv_free(paths, n);
        preview_update();
        ST_CHECK(!strcmp(A.pv_want, DU_BASE "/tree/sub/deep/x.png") || !A.preview_on);
    }
    // a list row
    f32 ly0 = v->list.y + roundf(S(30)) + du_row_h() * 1.5f;
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, v->list.x + v->list.w * 0.5f, ly0, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
    ST_CHECK(v->pick == mid && v->lcur == 1);

    // --- keys: the list moves the pick, Enter opens a file / goes into a folder (same scan), Backspace goes back
    st_key(SDLK_HOME, 0);
    ST_CHECK(v->pick == big);
    st_key(SDLK_DOWN, 0);
    ST_CHECK(v->pick == mid);
    st_key(SDLK_END, 0);
    ST_CHECK(v->pick == v->rows[v->nrows - 1]);
    st_key(SDLK_ESCAPE, 0);
    ST_CHECK(!v->pick);
    st_key(SDLK_HOME, 0);
    A.last_opened[0] = 0;
    st_key(SDLK_RETURN, 0);
    ST_CHECK(!strcmp(A.last_opened, DU_BASE "/tree/big.bin"));
    sc = st_du_cell(v, sub);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, sc->r.x + S(20), sc->r.y + S(6), SDL_BUTTON_LEFT, 1, 0); // its title strip
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
    ST_CHECK(v->pick == sub);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, sc->r.x + S(20), sc->r.y + S(6), SDL_BUTTON_LEFT, 2, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
    st_wait();
    st_frames(1);
    ST_CHECK(!strcmp(p->path, DU_BASE "/tree/sub") && p->dv == v && v->s == s && v->cur == sub && !v->pick);
    ST_CHECK(v->nrows == 2 && v->rows[0] == du_find(s, DU_BASE "/tree/sub/deep")); // deep (with the hard link), y.txt
    st_key(SDLK_BACKSPACE, 0);
    st_wait();
    st_frames(1);
    ST_CHECK(!strcmp(p->path, DU_BASE "/tree") && v->s == s && v->cur == &s->root);

    // --- sunburst: the first ring goes all the way round, segments are hit by angle; the middle goes up
    A.du_style = 1;
    st_frames(1);
    {
        f64 sum = 0;
        DuCell *bigc = NULL;
        for (int i = 0; i < v->ncells; i++) {
            DuCell *c = &v->cells[i];
            ST_CHECK(c->ring && c->r0 >= v->hole - 0.5f);
            if (c->depth == 1) sum += c->a1 - c->a0;
            if (c->it == big) bigc = c;
        }
        ST_CHECK(fabs(sum - 2 * M_PI) < 0.05 && bigc);
        if (bigc) {
            f32 am = (bigc->a0 + bigc->a1) * 0.5f, rm = (bigc->r0 + bigc->r1) * 0.5f;
            ST_CHECK(du_cell_at(v, v->cx + cosf(am) * rm, v->cy + sinf(am) * rm) == bigc);
            ST_CHECK(!du_cell_at(v, v->cx, v->cy));
        }
        pane_navigate(p, DU_BASE "/tree/sub", true);
        st_wait();
        st_frames(1);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, v->cx, v->cy, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, v->cx, v->cy, SDL_BUTTON_LEFT, 2, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
        st_wait();
        st_frames(1);
        ST_CHECK(!strcmp(p->path, DU_BASE "/tree") && v->s == s);
    }
    A.du_style = 0;
    st_frames(1);

    // --- the header buttons
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, v->b_sun.x + 4, v->b_sun.y + 4, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
    ST_CHECK(A.du_style == 1);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, v->b_tree.x + 4, v->b_tree.y + 4, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
    ST_CHECK(A.du_style == 0);
    bool had_list = A.du_list;
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, v->b_list.x + 4, v->b_list.y + 4, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
    ST_CHECK(A.du_list == !had_list && (A.du_list ? v->list.w > 0 : v->list.w == 0));
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, v->b_list.x + 4, v->b_list.y + 4, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
    ST_CHECK(A.du_list == had_list);

    // --- trash from the map: out of it and out of the folder sizes above, without measuring again
    bc = st_du_cell(v, mid);
    u64 before = atomic_load(&s->root.size), msz = atomic_load(&mid->size), mfiles = atomic_load(&s->root.files);
    du_pick(p, mid);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, bc->r.x + 5, bc->r.y + 5, SDL_BUTTON_RIGHT, 1, 0);
    ST_CHECK(A.menu.open && st_menu_has(ACT_DU_SHOW) && st_menu_has(ACT_TRASH) && st_menu_has(ACT_DU_RESCAN) && v->pick == mid);
    A.menu.open = false;
    do_action(p, ACT_TRASH);
    st_wait_ops();
    st_frames(2);
    ST_CHECK(!st_exists(DU_BASE "/tree/mid.mkv") && mid->removed && v->s == s && !s->stale);
    ST_CHECK(atomic_load(&s->root.size) == before - msz && atomic_load(&s->root.files) == mfiles - 1 && !st_du_cell(v, mid) && !v->pick);
    ST_CHECK(atomic_load(&s->root.cats[DC_VIDEO]) == 0 && atomic_load(&s->root.cats[DC_IMAGE]) == atomic_load(&x->size));
    for (int i = 0; i < v->nrows; i++) ST_CHECK(v->rows[i] != mid);
    // something copied in: the map says so; F5 measures again
    {
        char *src[1] = {(char *)DU_BASE "/in/new.bin"};
        start_transfer(src, 1, DU_BASE "/tree/many", false, false);
        st_wait_ops();
        st_frames(1);
        ST_CHECK(s->stale);
        st_key(SDLK_F5, 0);
        ST_CHECK(st_wait_cond(st_du_ready, 5000));
        ST_CHECK(v->s != s && !v->s->stale && !strcmp(v->s->rootpath, DU_BASE "/tree"));
        s = v->s;
        st_frames(1);
        ST_CHECK(st_du_kid(st_du_kid(&s->root, "many"), "new.bin") != NULL);
    }

    // --- a folder made after the scan: measured on its own; going back reuses the scan above it
    mkdir_p(DU_BASE "/tree/later", 0755);
    st_write(DU_BASE "/tree/later/l.bin", 8192);
    pane_navigate(p, DU_BASE "/tree/later", true);
    ST_CHECK(st_wait_cond(st_du_ready, 5000));
    ST_CHECK(v->s != s && !strcmp(v->s->rootpath, DU_BASE "/tree/later") && atomic_load(&v->cur->files) == 1);
    st_key(SDLK_BACKSPACE, 0);
    ST_CHECK(st_wait_cond(st_du_ready, 5000));
    ST_CHECK(v->s == s && v->cur == &s->root);

    // --- another pane in a folder of the same scan shares it; leaving keeps it cached for a while
    Tab *t2 = tab_new(DU_BASE "/tree/many", A.cur + 1, true);
    t2->pane[0].du = true;
    st_wait();
    st_frames(2);
    ST_CHECK(t2->pane[0].dv && t2->pane[0].dv->s == s && s->refs == 3 && t2->pane[0].dv->cur == st_du_kid(&s->root, "many"));
    tab_close(tab_index(t2));
    st_frames(1);
    ST_CHECK(s->refs == 2 && active_pane() == p);

    // --- context menus: a folder offers its disk usage in a new tab; the background menu toggles the view
    du_toggle(p);
    st_frames(1);
    ST_CHECK(!p->du && !p->dv && p->ly.header.h > 0 && s->refs == 1);
    ST_CHECK(st_select(p, "sub"));
    menu_open(p, 100, 100, true);
    ST_CHECK(st_menu_has(ACT_DU_NEW_TAB));
    A.menu.open = false;
    int ntabs = A.ntabs;
    do_action(p, ACT_DU_NEW_TAB);
    st_wait();
    st_frames(2);
    Pane *np = active_pane();
    ST_CHECK(A.ntabs == ntabs + 1 && np != p && np->du && !strcmp(np->path, DU_BASE "/tree/sub") && np->dv && np->dv->s == s);
    tab_close(A.cur);
    st_frames(1);
    p = active_pane();
    menu_open(p, 100, 100, false);
    int views = -1, toggle = -1;
    for (int i = 0; i < A.menu.n; i++)
        if (A.menu.items[i].act == ACT_MENU_VIEWS && A.menu.items[i].enabled && A.menu.items[i].sub) views = i;
    ST_CHECK(views >= 0);
    if (views >= 0) {
        menu_sub_open(views, false);
        for (int i = 0; i < g_menu_sub.n; i++)
            if (g_menu_sub.items[i].act == ACT_DU_TOGGLE && g_menu_sub.items[i].enabled) toggle = i;
        ST_CHECK(menu_sub_shown() && toggle >= 0);
        if (toggle >= 0) menu_sub_activate(toggle);
    }
    A.menu.open = false;
    st_frames(1);
    ST_CHECK(p->du && p->dv);
    for (int i = 0; i < A.nplaces; i++)
        if (A.places[i].kind == PK_HOME) {
            menu_open_place(i, 10, 10);
            ST_CHECK(st_menu_has(ACT_DU_PLACE)); // (not run: that would measure the real home folder)
            A.menu.open = false;
        }
    // "Show in the file list": back to the list in the item's folder, the item selected
    ST_CHECK(st_wait_cond(st_du_ready, 5000));
    v = p->dv;
    du_pick(p, du_find(v->s, DU_BASE "/tree/sub/deep/x.png"));
    do_action(p, ACT_DU_SHOW);
    st_wait();
    st_frames(1);
    ST_CHECK(!p->du && !strcmp(p->path, DU_BASE "/tree/sub/deep") && !strcmp(st_cursor_name(p), "x.png"));
    // a view from the menu or Ctrl+Shift+1..6 leaves the map too
    du_toggle(p);
    ST_CHECK(p->du);
    st_key(SDLK_5, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
    ST_CHECK(!p->du && p->view == VIEW_LIST);
    set_zoom_level(p, 0);
    // a search shows its results, the map comes back after it
    du_toggle(p);
    st_frames(1);
    search_focus(p);
    st_type("x");
    search_apply(p, true);
    st_wait();
    ST_CHECK(p->du && !pane_du_on(p));
    st_key(SDLK_ESCAPE, 0);
    st_wait();
    ST_CHECK(pane_du_on(p));
    // the trash doesn't have a map
    {
        static Pane tp;
        char ti[PATH_MAX];
        memset(&tp, 0, sizeof tp);
        trash_dirs(tp.path, sizeof tp.path, ti, sizeof ti);
        tp.du = true;
        ST_CHECK(!pane_du_on(&tp));
        snprintf(tp.path, sizeof tp.path, "%s", DU_BASE "/a.zip::/x");
        ST_CHECK(!pane_du_on(&tp));
    }

    settings_save();
    char spath[PATH_MAX], sbuf[8192];
    settings_file(spath, sizeof spath, false);
    st_read_text(spath, sbuf, sizeof sbuf);
    ST_CHECK(strstr(sbuf, "\ndisk_usage_style=treemap\ndisk_usage_list=1\n") != NULL);
    A.du_style = 1;
    settings_save();
    st_read_text(spath, sbuf, sizeof sbuf);
    ST_CHECK(strstr(sbuf, "\ndisk_usage_style=sunburst\n") != NULL);
    A.du_style = 0;
    du_toggle(p);
    chmod(DU_BASE "/tree/locked", 0755);
    st_rmrf("/tmp/fe-du-xdg");

    fprintf(stderr, "selftest-du: %s (%d failures)\n", st_fail ? "FAIL" : "ok", st_fail);
    return st_fail ? 1 : 0;
}

// --- self-test for folder compare: the comparison, marks per side, following navigation, differences only, sync ---

#define CMP_BASE "/tmp/fe-cmp-test"

static void st_touch_at(const char *path, i64 t) {
    struct timespec ts[2] = {{t, 0}, {t, 0}};
    utimensat(AT_FDCWD, path, ts, AT_SYMLINK_NOFOLLOW);
}

static void st_setup_cmp(void) {
    st_rmrf(CMP_BASE);
    st_rmrf("/tmp/fe-cmp-xdg");
    mkdir_p("/tmp/fe-cmp-xdg", 0755);
    const char *both[] = {"same.txt", "photos/1.jpg", "photos/2.jpg", "docs/x.txt", "sub/deep/same.bin", "sub/deep/changed.txt", "slack.txt", "samesize.txt"};
    for (int s = 0; s < 2; s++)
        for (size_t i = 0; i < ARRAY_COUNT(both); i++) {
            char p[PATH_MAX], dir[PATH_MAX];
            snprintf(p, sizeof p, CMP_BASE "/%c/%s", s ? 'b' : 'a', both[i]);
            parent_of(p, dir, sizeof dir);
            mkdir_p(dir, 0755);
            st_write_text(p, !strcmp(both[i], "samesize.txt") ? (s ? "bbbb\n" : "aaaa\n") : "hello\n");
            st_touch_at(p, 1600000000);
        }
    st_write_text(CMP_BASE "/b/docs/x.txt", "hello, longer\n"); // same date, other size
    st_touch_at(CMP_BASE "/b/docs/x.txt", 1600000000);
    st_touch_at(CMP_BASE "/b/photos/2.jpg", 1700000000);         // newer on the right
    st_touch_at(CMP_BASE "/a/sub/deep/changed.txt", 1700000000); // newer on the left, two levels down
    st_touch_at(CMP_BASE "/b/slack.txt", 1600000001);            // 1 s apart: the same (FAT keeps 2 s)
    mkdir_p(CMP_BASE "/a/only_a_dir/inner", 0755);
    st_write_text(CMP_BASE "/a/only_a_dir/inner/z.txt", "z\n");
    st_write_text(CMP_BASE "/a/photos/new.jpg", "new\n");
    st_write_text(CMP_BASE "/b/only_b.txt", "b\n");
    st_write_text(CMP_BASE "/a/thing", "a file\n");
    mkdir_p(CMP_BASE "/b/thing", 0755);
    setenv("XDG_DATA_HOME", "/tmp/fe-cmp-xdg", 1); // the trash
}

static bool st_cmp_ready(void) {
    if (!g_cmp.on || !cmp_ready() || g_cmp.old) return false;
    for (int s = 0; s < 2; s++) {
        Pane *p = &g_cmp.t[s]->pane[0];
        if (p->sort_pending || !p->order || p->L_next || atomic_load(&p->L->state) != LST_COMPLETE) return false;
    }
    return true;
}

static u8 st_cmp_rec(const char *rel) { return cmp_state_of(g_cmp.job, rel); }

static bool st_has_entry(Pane *p, const char *name) {
    for (u32 i = 0; i < p->count; i++)
        if (!strcmp(p->L->entries[p->order[i]].name, name)) return true;
    return false;
}

static u8 st_cmp_state(Pane *p, const char *name) {
    for (u32 i = 0; i < p->L->count; i++)
        if (!strcmp(p->L->entries[i].name, name)) return cmp_entry_state(p, &p->L->entries[i]);
    return 200;
}

static void st_cmp_sync(int act) { // run a sync from the bar's menu and accept its dialog
    Pane *p = &g_cmp.t[act == ACT_CMP_SYNC_B || act == ACT_CMP_MIRROR_B ? 1 : 0]->pane[0];
    do_action(p, act);
    ST_CHECK(A.dlg.open && A.dlg.fn == cmp_sync_cb && A.dlg.rich && A.dlg.nitems > 0);
    if (act == ACT_CMP_MIRROR_B) ST_CHECK(A.dlg.item_kind[0] == DK_TRASH && !strcmp(A.dlg.items[0], "a/extra.txt") && !strcmp(A.dlg.item_sub[0], "Trash"));
    if (!A.dlg.open) return;
    char shot[32];
    snprintf(shot, sizeof shot, "cmp-dialog-%d", act);
    st_shot(shot);
    A.dlg.open = false;
    cmp_sync_cb(0, false, NULL);
    st_wait_ops();
    st_wait_cond(st_cmp_ready, 5000);
    st_frames(2);
}

static Tab *g_cmp_tab_of(Pane *p) {
    for (int i = 0; i < A.ntabs; i++)
        if (&A.tabs[i]->pane[0] == p) return A.tabs[i];
    return NULL;
}

static int run_selftest_cmp(void) {
    st_wait();
    Pane *pa = active_pane();
    pa->tree = false;
    // --- two panels: Ctrl+Shift+K compares them (the left one is side A)
    Panel *np = panel_split(A.ap, ZONE_RIGHT, NULL, CMP_BASE "/b");
    st_wait();
    Pane *pb = &np->cur->pane[0];
    pb->tree = false;
    ST_CHECK(A.ap == np);
    st_key(SDLK_K, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
    ST_CHECK(g_cmp.on && &g_cmp.t[0]->pane[0] == pa && &g_cmp.t[1]->pane[0] == pb && cmp_side(pa) == 0 && cmp_side(pb) == 1);
    ST_CHECK(st_wait_cond(st_cmp_ready, 5000));
    st_frames(1);
    ST_CHECK(pa->ly.has_cmpbar && pb->ly.has_cmpbar && pa->ly.cmpbar.h > 0);
    st_shot("cmp-marks");

    // --- what differs: recorded once (side A's view), each side sees it its way
    ST_CHECK(st_cmp_rec("only_a_dir") == CMP_ONLY_A && !cmp_lookup(g_cmp.job, "only_a_dir/inner") && // not walked into,
             st_cmp_rec("only_a_dir/inner/z.txt") == CMP_ONLY_A && st_cmp_rec("photos/1.jpg") == CMP_SAME);  // but only there
    ST_CHECK(st_cmp_rec("only_b.txt") == CMP_ONLY_B && st_cmp_rec("photos/new.jpg") == CMP_ONLY_A);
    ST_CHECK(st_cmp_rec("photos/2.jpg") == CMP_B_NEWER && st_cmp_rec("sub/deep/changed.txt") == CMP_A_NEWER);
    ST_CHECK(st_cmp_rec("docs/x.txt") == CMP_DIFF && st_cmp_rec("thing") == CMP_TYPE);
    ST_CHECK(st_cmp_rec("photos") == CMP_INSIDE && st_cmp_rec("sub") == CMP_INSIDE && st_cmp_rec("sub/deep") == CMP_INSIDE);
    ST_CHECK(st_cmp_rec("same.txt") == CMP_SAME && st_cmp_rec("slack.txt") == CMP_SAME && st_cmp_rec("samesize.txt") == CMP_SAME);
    ST_CHECK(st_cmp_state(pa, "only_a_dir") == CMP_ONLY_A && st_cmp_state(pb, "only_b.txt") == CMP_ONLY_A); // "only here"
    ST_CHECK(st_cmp_state(pb, "photos") == CMP_INSIDE && st_cmp_state(pa, "same.txt") == CMP_SAME);
    ST_CHECK(g_cmp.cnt[0][0] == 2 && g_cmp.cnt[0][1] == 1 && g_cmp.cnt[1][0] == 1 && g_cmp.cnt[1][1] == 1 && g_cmp.cnt[0][2] == 1 && g_cmp.cnt[0][3] == 1);
    char bt[256];
    cmp_bar_text(pa, bt, sizeof bt);
    ST_CHECK(!strcmp(bt, "2 only here  \xC2\xB7  1 newer here  \xC2\xB7  1 differ  \xC2\xB7  1 file vs folder"));
    // contents: same size and date but other bytes
    do_action(pa, ACT_CMP_CONTENTS);
    ST_CHECK(st_wait_cond(st_cmp_ready, 5000));
    ST_CHECK(g_cmp.contents && st_cmp_rec("samesize.txt") == CMP_DIFF && st_cmp_rec("same.txt") == CMP_SAME);
    do_action(pa, ACT_CMP_CONTENTS);
    ST_CHECK(st_wait_cond(st_cmp_ready, 5000));

    // --- differences only: what's the same leaves both lists, and comes back
    ST_CHECK(st_has_entry(pa, "same.txt") && st_has_entry(pb, "same.txt"));
    Rect sw, sy, cl;
    cmp_bar_layout(pa, &sw, &sy, &cl);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, sw.x + sw.w * 0.5f, sw.y + sw.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
    st_wait();
    ST_CHECK(g_cmp.diff_only && !st_has_entry(pa, "same.txt") && !st_has_entry(pb, "same.txt") && !st_has_entry(pa, "slack.txt"));
    ST_CHECK(st_has_entry(pa, "photos") && st_has_entry(pa, "only_a_dir") && st_has_entry(pb, "only_b.txt") && pa->count == 5);
    do_action(pa, ACT_CMP_DIFF_ONLY);
    st_wait();
    ST_CHECK(!g_cmp.diff_only && st_has_entry(pa, "same.txt"));

    // --- going into a folder takes the other side along; back up too; a folder only on one side: the other stays
    pane_navigate(pa, CMP_BASE "/a/sub", true);
    st_wait();
    st_frames(1);
    ST_CHECK(!strcmp(pb->path, CMP_BASE "/b/sub"));
    pane_navigate(pb, CMP_BASE "/b/sub/deep", true);
    st_wait();
    st_frames(1);
    ST_CHECK(!strcmp(pa->path, CMP_BASE "/a/sub/deep") && st_cmp_state(pa, "changed.txt") == CMP_A_NEWER && st_cmp_state(pb, "changed.txt") == CMP_B_NEWER);
    set_active_pane(pa);
    st_key(SDLK_BACKSPACE, 0);
    st_key(SDLK_BACKSPACE, 0);
    st_wait();
    st_frames(1);
    ST_CHECK(!strcmp(pa->path, CMP_BASE "/a") && !strcmp(pb->path, CMP_BASE "/b") && g_cmp.on);
    pane_navigate(pa, CMP_BASE "/a/only_a_dir", true);
    st_wait();
    st_frames(1);
    ST_CHECK(g_cmp.on && !strcmp(pb->path, CMP_BASE "/b") && st_cmp_state(pa, "inner") == CMP_ONLY_A);
    pane_navigate(pa, CMP_BASE "/a", true);
    st_wait();
    st_frames(1);

    // --- sync left -> right: new and newer (and same-date differences) copied over; newer on the right left alone
    ST_CHECK(st_wait_cond(st_cmp_ready, 5000));
    {
        CmpPlan pl;
        cmp_plan(&pl, 0, false);
        ST_CHECK(pl.n == 4 && pl.ndel == 0 && pl.skipped == 2); // only_a_dir, new.jpg, changed.txt, x.txt; 2.jpg + thing skipped
        cmp_plan_free(&pl);
        cmp_plan(&pl, -1, false);
        ST_CHECK(pl.n == 5 && pl.skipped == 2); // both ways: + 2.jpg and only_b.txt the other way, x.txt and thing skipped
        cmp_plan_free(&pl);
    }
    do_action(pa, ACT_CMP_SYNC_A); // the dialog: what happens, in rows
    ST_CHECK(A.dlg.open && A.dlg.nitems == 4 && A.dlg.more == 0 && A.dlg.item_kind[0] == DK_REPLACE && !strcmp(A.dlg.items[0], "a/docs/x.txt") &&
             !strcmp(A.dlg.item_sub[0], "b/docs/x.txt") && A.dlg.item_kind[1] == DK_COPY && A.dlg.item_dir[1] &&
             !strcmp(A.dlg.items[1], "a/only_a_dir") && A.dlg.item_kind[3] == DK_REPLACE && !strcmp(A.dlg.item_sub[3], "b/sub/deep/changed.txt"));
    ST_CHECK(strstr(A.dlg.msg, "2 new, 1 newer, 1 with the same date: older copies are replaced") == A.dlg.msg && strstr(A.dlg.msg, "2 left alone"));
    ST_CHECK(!strcmp(A.dlg.title, "Copy new and newer from \xE2\x80\x9C" "a\xE2\x80\x9D to \xE2\x80\x9C" "b\xE2\x80\x9D"));
    A.dlg.open = false;
    cmp_plan_free(&g_cmp_plan);
    st_cmp_sync(ACT_CMP_SYNC_A);
    ST_CHECK(st_exists(CMP_BASE "/b/only_a_dir/inner/z.txt") && st_exists(CMP_BASE "/b/photos/new.jpg"));
    char t[64];
    st_read_text(CMP_BASE "/b/docs/x.txt", t, sizeof t);
    ST_CHECK(!strcmp(t, "hello\n"));
    { struct stat st; ST_CHECK(stat(CMP_BASE "/b/photos/2.jpg", &st) == 0 && st.st_mtime == 1700000000); }        // newer one kept
    { struct stat st; ST_CHECK(stat(CMP_BASE "/b/sub/deep/changed.txt", &st) == 0 && st.st_mtime == 1700000000); } // date copied
    ST_CHECK(g_cmp.cnt[0][0] == 0 && g_cmp.cnt[0][1] == 0 && g_cmp.cnt[1][0] == 1 && g_cmp.cnt[1][1] == 1); // compared again by itself
    cmp_bar_text(pa, bt, sizeof bt);
    ST_CHECK(!strncmp(bt, "1 file vs folder", 16));

    // --- mirror right -> left: the left side's extras go to the Trash
    st_write_text(CMP_BASE "/a/extra.txt", "x\n");
    do_action(pa, ACT_CMP_AGAIN);
    ST_CHECK(st_wait_cond(st_cmp_ready, 5000));
    st_cmp_sync(ACT_CMP_MIRROR_B);
    ST_CHECK(!st_exists(CMP_BASE "/a/extra.txt") && st_exists("/tmp/fe-cmp-xdg/Trash/files/extra.txt") && st_exists(CMP_BASE "/a/only_b.txt"));
    { struct stat st; ST_CHECK(stat(CMP_BASE "/a/photos/2.jpg", &st) == 0 && st.st_mtime == 1700000000); }
    ST_CHECK(st_exists(CMP_BASE "/a/only_a_dir")); // (on both sides now: not an extra)

    // --- both ways: each side gets the other's new files
    st_write_text(CMP_BASE "/a/left_new.txt", "l\n");
    st_write_text(CMP_BASE "/b/right_new.txt", "r\n");
    do_action(pb, ACT_CMP_AGAIN);
    ST_CHECK(st_wait_cond(st_cmp_ready, 5000));
    st_cmp_sync(ACT_CMP_SYNC_BOTH);
    ST_CHECK(st_exists(CMP_BASE "/b/left_new.txt") && st_exists(CMP_BASE "/a/right_new.txt"));
    ST_CHECK(g_cmp.cnt[0][0] == 0 && g_cmp.cnt[1][0] == 0 && g_cmp.cnt[0][1] == 0 && g_cmp.cnt[1][1] == 0);
    do_action(pa, ACT_CMP_SYNC_A); // nothing left to copy: no dialog
    ST_CHECK(!A.dlg.open);

    // --- the bar's Sync... menu, the context menus
    cmp_bar_layout(pb, &sw, &sy, &cl);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, sy.x + 5, sy.y + 5, SDL_BUTTON_LEFT, 1, 0);
    ST_CHECK(A.menu.open && st_menu_has(ACT_CMP_SYNC_B) && st_menu_has(ACT_CMP_MIRROR_B) && st_menu_has(ACT_CMP_SYNC_BOTH) && !st_menu_has(ACT_CMP_SYNC_A));
    A.menu.open = false;
    menu_open(pa, 100, 100, false);
    ST_CHECK(st_menu_has(ACT_CMP_END) && !st_menu_has(ACT_CMP_START));
    A.menu.open = false;

    // --- leaving the compared folders ends it; the close button too
    pane_navigate(pa, CMP_BASE, true);
    st_wait();
    st_frames(1);
    ST_CHECK(!g_cmp.on && !pa->ly.has_cmpbar && !pb->ly.has_cmpbar);
    pane_navigate(pa, CMP_BASE "/a", true);
    st_wait();
    menu_open(pa, 100, 100, false);
    ST_CHECK(st_menu_has(ACT_CMP_DIALOG));
    A.menu.open = false;
    do_action(pa, ACT_CMP_DIALOG); // two panels: the other one's folder is already on the right
    ST_CHECK(g_sheet.kind == SHEET_COMPARE && !strcmp(g_sheet.cp[1].buf, CMP_BASE "/b"));
    st_key(SDLK_RETURN, 0);
    ST_CHECK(st_wait_cond(st_cmp_ready, 5000));
    cmp_bar_layout(pa, &sw, &sy, &cl);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, cl.x + 4, cl.y + 4, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
    ST_CHECK(!g_cmp.on);
    // not with itself, nor a folder inside the other
    pane_navigate(pb, CMP_BASE "/a/sub", true);
    st_wait();
    cmp_start(g_cmp_tab_of(pa), g_cmp_tab_of(pb));
    ST_CHECK(!g_cmp.on);

    // --- one panel: "Compare with..." offers the other tabs; picking one opens it in a split, compared
    set_active_pane(pb);
    run_command(CMD_CLOSE_PANEL);
    st_frames(1);
    ST_CHECK(A.npanels == 1);
    Tab *tb = tab_new(CMP_BASE "/b", A.ntabs, false);
    st_wait();
    Tab *ta = A.ap->cur;
    { // the folder's menu: "Compare with..." opens the dialog
        Pane *p = active_pane();
        menu_open(p, p->ly.list.x + S(40), p->ly.list.y + S(40), false);
        ST_CHECK(st_menu_has(ACT_CMP_DIALOG));
        A.menu.open = false;
    }
    st_key(SDLK_K, SDL_KMOD_CTRL | SDL_KMOD_SHIFT); // one panel: the dialog, this folder on the left, the right to type
    ST_CHECK(g_sheet.kind == SHEET_COMPARE && !strcmp(g_sheet.cp[0].buf, CMP_BASE "/a") && !g_sheet.cp[1].buf[0] && g_sheet.cp_focus == 1);
    st_type(CMP_BASE "/");
    ST_CHECK(st_wait_cond(st_cmp_sug, 5000)); // the folders in it, a and b
    ST_CHECK(g_sheet.cp_nsug == 2 && !strcmp(g_sheet.cp_sug[1], CMP_BASE "/b"));
    st_key(SDLK_DOWN, 0);
    st_key(SDLK_DOWN, 0);
    st_key(SDLK_RETURN, 0); // b picked: into the field, a slash after it
    ST_CHECK(g_sheet.kind == SHEET_COMPARE && !strcmp(g_sheet.cp[1].buf, CMP_BASE "/b/"));
    ST_CHECK(st_wait_cond(st_cmp_sug, 5000)); // and its folders come up
    st_key(SDLK_ESCAPE, 0); // closes the suggestions (if any), not the dialog
    ST_CHECK(g_sheet.kind == SHEET_COMPARE && !g_sheet.cp_nsug);
    st_type("nope");
    st_key(SDLK_RETURN, 0);
    ST_CHECK(g_sheet.kind == SHEET_COMPARE && g_sheet.cp_err[0]); // not a folder: says so, stays open
    { // the left field's suggestions cover the right field: a click there picks one (not the field)
        TextEdit *l = &g_sheet.cp[0];
        st_text_click(l, (int)strlen(l->buf), 1, 0);
        ST_CHECK(g_sheet.cp_focus == 0);
        te_set(l, "", 0, 0);
        st_type(CMP_BASE "/");
        ST_CHECK(st_wait_cond(st_cmp_sug, 5000));
        st_frames(1);
        Rect r = g_sheet.cp_rsug[1];
        ST_CHECK(g_sheet.cp_nsug == 2 && r.y < g_sheet.cp_rf[1].y + g_sheet.cp_rf[1].h && r.y + r.h > g_sheet.cp_rf[1].y);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, r.x + r.w * 0.5f, r.y + r.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
        ST_CHECK(g_sheet.cp_focus == 0 && !strcmp(l->buf, CMP_BASE "/b/"));
        te_set(l, CMP_BASE "/a", 0, 0);
        g_sheet.cp_focus = 1;
    }
    te_set(&g_sheet.cp[1], CMP_BASE "/b", 0, 0);
    cmp_sug_clear();
    st_key(SDLK_RETURN, 0);
    ST_CHECK(g_sheet.kind == SHEET_NONE);
    st_wait();
    ST_CHECK(g_cmp.on && A.npanels == 2 && g_cmp.t[0] == ta && g_cmp.t[1] == tb && tb->panel != ta->panel); // that tab moved over
    ST_CHECK(st_wait_cond(st_cmp_ready, 5000));
    // closing a compared tab ends it
    tab_close(tab_index(tb));
    st_frames(1);
    ST_CHECK(!g_cmp.on && A.npanels == 1);
    // a tab's menu: "Compare with current tab" (same panel: it moves next to the current one)
    tb = tab_new(CMP_BASE "/b", A.ntabs, false);
    st_wait();
    menu_open_tab(tb, 10, 10);
    ST_CHECK(st_menu_has(ACT_CMP_TAB));
    do_action(active_pane(), ACT_CMP_TAB);
    A.menu.open = false;
    st_wait();
    ST_CHECK(g_cmp.on && A.npanels == 2 && g_cmp.t[1] == tb);
    ST_CHECK(st_wait_cond(st_cmp_ready, 5000));
    st_frames(2);
    run_command(CMD_COMPARE); // again: ends it
    ST_CHECK(!g_cmp.on);
    st_rmrf("/tmp/fe-cmp-xdg");

    fprintf(stderr, "selftest-cmp: %s (%d failures)\n", st_fail ? "FAIL" : "ok", st_fail);
    return st_fail ? 1 : 0;
}

// --- self-test for administrator retries (stand-in pkexec) and previous versions from snapshots (made-up layouts) ---

#define ADM_BASE "/tmp/fe-adm-test"

static void st_setup_admin(void) {
    chmod(ADM_BASE "/locked", 0755);
    chmod(ADM_BASE "/vol2/.snapshots", 0755);
    st_rmrf(ADM_BASE);
    mkdir_p(ADM_BASE "/src", 0755);
    mkdir_p(ADM_BASE "/locked", 0755);
    mkdir_p(ADM_BASE "/tools", 0755);
    mkdir_p(ADM_BASE "/xdg", 0755);
    st_write_text(ADM_BASE "/src/a.txt", "a\n");
    st_write_text(ADM_BASE "/locked/sys.conf", "x\n");
    st_write_text(ADM_BASE "/locked/old.conf", "y\n");
    chmod(ADM_BASE "/locked", 0555); // what the user may not change
    // stand-in pkexec: logs, then "has the rights" (makes the locked places writable / readable) and runs it
    FILE *f = fopen(ADM_BASE "/tools/pkexec", "w");
    fputs("#!/bin/sh\necho \"$*\" >> " ADM_BASE "/tools/pkexec.log\nchmod 755 " ADM_BASE "/locked " ADM_BASE "/vol2/.snapshots 2>/dev/null\n"
          "exec \"$@\"\n", f);
    fclose(f);
    chmod(ADM_BASE "/tools/pkexec", 0755);
    // snapshots: Snapper on a btrfs volume (#1 and #2 the same version, #3 another), Timeshift, ZFS, and one only root reads
    const struct { const char *n, *date, *desc, *text; i64 mt; } sn[] = {
        {"1", "2026-01-01 10:00:00", "first", "v1\n", 1767261600}, {"2", "2026-02-01 10:00:00", "second", "v1\n", 1767261600},
        {"3", "2026-03-01 10:00:00", "before update", "version three\n", 1772359200}};
    for (int i = 0; i < 3; i++) {
        char p[PATH_MAX], d[PATH_MAX];
        snprintf(d, sizeof d, ADM_BASE "/vol/.snapshots/%s/snapshot/docs", sn[i].n);
        mkdir_p(d, 0755);
        snprintf(p, sizeof p, "%s/a.txt", d);
        st_write_text(p, sn[i].text);
        st_touch_at(p, sn[i].mt);
        snprintf(p, sizeof p, ADM_BASE "/vol/.snapshots/%s/info.xml", sn[i].n);
        char xml[400];
        snprintf(xml, sizeof xml, "<?xml version=\"1.0\"?>\n<snapshot>\n  <type>single</type>\n  <num>%s</num>\n  <date>%s</date>\n"
                 "  <description>%s</description>\n</snapshot>\n", sn[i].n, sn[i].date, sn[i].desc);
        st_write_text(p, xml);
    }
    mkdir_p(ADM_BASE "/vol/docs", 0755);
    st_write_text(ADM_BASE "/vol/docs/a.txt", "the current one\n");
    st_touch_at(ADM_BASE "/vol/docs/a.txt", 1775000000); // unchanged since the Timeshift snapshot
    mkdir_p(ADM_BASE "/run/7/backup/timeshift-btrfs/snapshots/2026-04-01_12-00-00/@vol/docs", 0755);
    st_write_text(ADM_BASE "/run/7/backup/timeshift-btrfs/snapshots/2026-04-01_12-00-00/@vol/docs/a.txt", "the current one\n");
    st_touch_at(ADM_BASE "/run/7/backup/timeshift-btrfs/snapshots/2026-04-01_12-00-00/@vol/docs/a.txt", 1775000000);
    mkdir_p(ADM_BASE "/zvol/.zfs/snapshot/daily/sub", 0755);
    st_write_text(ADM_BASE "/zvol/.zfs/snapshot/daily/sub/z.txt", "zfs\n");
    mkdir_p(ADM_BASE "/zvol/sub", 0755);
    st_write_text(ADM_BASE "/zvol/sub/z.txt", "zfs now\n");
    mkdir_p(ADM_BASE "/vol2/.snapshots/5/snapshot", 0755);
    st_write_text(ADM_BASE "/vol2/.snapshots/5/snapshot/r.txt", "root only\n");
    st_write_text(ADM_BASE "/vol2/.snapshots/5/info.xml", "<snapshot><date>2026-05-01 08:00:00</date><description>root</description></snapshot>");
    st_write_text(ADM_BASE "/vol2/r.txt", "now\n");
    chmod(ADM_BASE "/vol2/.snapshots", 0);
    // Timeshift in rsync mode: one snapshot on "/" (also seen through /run/timeshift/8/backup: listed once), one on the
    // backup drive its settings name (by UUID); the drive is ext4
    mkdir_p(ADM_BASE "/tsroot/timeshift/snapshots/2026-06-01_09-30-00/localhost" ADM_BASE "/vol3/notes", 0755);
    st_write_text(ADM_BASE "/tsroot/timeshift/snapshots/2026-06-01_09-30-00/localhost" ADM_BASE "/vol3/notes/n.txt", "old notes\n");
    st_write_text(ADM_BASE "/tsroot/timeshift/snapshots/2026-06-01_09-30-00/info.json",
                  "{\n  \"created\" : \"1780300000\",\n  \"tags\" : \"daily weekly\",\n  \"comments\" : \"before upgrade\"\n}\n");
    mkdir_p(ADM_BASE "/run/8", 0755);
    if (symlink(ADM_BASE "/tsroot", ADM_BASE "/run/8/backup")) {}
    mkdir_p(ADM_BASE "/backupdisk/timeshift/snapshots/2026-07-01_08-00-00/localhost" ADM_BASE "/vol3", 0755);
    st_write_text(ADM_BASE "/backupdisk/timeshift/snapshots/2026-07-01_08-00-00/info.json", "{\"tags\" : \"O B\"}\n"); // (older Timeshift: letters)
    mkdir_p(ADM_BASE "/byuuid", 0755);
    st_write_text(ADM_BASE "/sdz", "");
    if (symlink(ADM_BASE "/sdz", ADM_BASE "/byuuid/abcd-1234")) {}
    st_write_text(ADM_BASE "/timeshift.json", "{\n  \"backup_device_uuid\" : \"abcd-1234\",\n  \"btrfs_mode\" : \"false\"\n}\n");
    mkdir_p(ADM_BASE "/vol3/notes", 0755);
    st_write_text(ADM_BASE "/vol3/notes/n.txt", "new notes\n");
    f = fopen(ADM_BASE "/tools/timeshift", "w");
    fputs("#!/bin/sh\necho \"$*\" >> " ADM_BASE "/tools/timeshift.log\nexit 0\n", f);
    fclose(f);
    chmod(ADM_BASE "/tools/timeshift", 0755);
    st_write_text(ADM_BASE "/mountinfo", "21 1 0:31 /@ / rw - btrfs /dev/x rw\n"
                                          "60 21 0:61 /@vol " ADM_BASE "/vol rw - btrfs /dev/y rw\n"
                                          "61 21 0:62 / " ADM_BASE "/zvol rw - zfs pool/z rw\n"
                                          "62 21 0:63 /@vol2 " ADM_BASE "/vol2 rw - btrfs /dev/y rw\n"
                                          "63 21 0:64 / " ADM_BASE "/vol3 rw - ext4 /dev/w rw\n"
                                          "64 21 0:65 / " ADM_BASE "/backupdisk rw - ext4 " ADM_BASE "/sdz rw\n");
    // stand-in snapper: two configs; "list" only works once allowed (a flag file per config), status / create / delete logged
    mkdir_p(ADM_BASE "/configs", 0755);
    st_write_text(ADM_BASE "/configs/home2", "SUBVOLUME=\"" ADM_BASE "/vol2\"\nALLOW_USERS=\"bob\"\nSYNC_ACL=\"no\"\n");
    st_write_text(ADM_BASE "/allowed-root", "");
    f = fopen(ADM_BASE "/tools/snapper", "w");
    fputs("#!/bin/sh\nB=" ADM_BASE "\necho \"$*\" >> $B/tools/snapper.log\n"
          "case \"$*\" in\n"
          "*list-configs*) printf '{\"configs\": [{\"config\": \"root\", \"subvolume\": \"%s/vol\"}, {\"config\": \"home2\", \"subvolume\": \"%s/vol2\"}]}\\n' $B $B ;;\n"
          "*'-c root list'*) [ -e $B/allowed-root ] || { echo 'No permissions.'; exit 1; }\n"
          "  printf '{\"root\": [{\"number\": 0, \"type\": \"single\", \"description\": \"current\"},\\n"
          "{\"number\": 1, \"type\": \"pre\", \"pre-number\": null, \"date\": \"2026-01-01 10:00:00\", \"user\": \"root\", \"cleanup\": \"number\", \"description\": \"pacman -Syu\", \"userdata\": {\"important\": \"yes\"}},\\n"
          "{\"number\": 2, \"type\": \"post\", \"pre-number\": 1, \"date\": \"2026-02-01 10:00:00\", \"user\": \"root\", \"cleanup\": \"number\", \"description\": \"linux\", \"userdata\": null},\\n"
          "{\"number\": 3, \"type\": \"single\", \"pre-number\": null, \"date\": \"2026-03-01 10:00:00\", \"user\": \"root\", \"cleanup\": \"timeline\", \"description\": \"timeline\", \"userdata\": null}]}\\n' ;;\n"
          "*'-c home2 list'*) [ -e $B/allowed-home2 ] || { echo 'No permissions.'; exit 1; }\n"
          "  printf '{\"home2\": [{\"number\": 5, \"type\": \"single\", \"date\": \"2026-05-01 08:00:00\", \"description\": \"root\"}]}\\n' ;;\n"
          "*'status 1..2'*) printf 'c..... %s/vol/docs/a.txt\\n+..... %s/vol/docs/new.txt\\n-..... %s/vol/gone.txt\\n' $B $B $B ;;\n"
          "*set-config*) touch $B/allowed-home2 ;;\n"
          "esac\nexit 0\n", f);
    fclose(f);
    chmod(ADM_BASE "/tools/snapper", 0755);
    setenv("FE_SNAPPER", ADM_BASE "/tools/snapper", 1);
    setenv("FE_SNAPPER_CONFIGS", ADM_BASE "/configs", 1);
    setenv("FE_MOUNTINFO", ADM_BASE "/mountinfo", 1);
    setenv("FE_TIMESHIFT", ADM_BASE "/run", 1);
    setenv("FE_TIMESHIFT_ROOT", ADM_BASE "/tsroot", 1);
    setenv("FE_TIMESHIFT_CONF", ADM_BASE "/timeshift.json", 1);
    setenv("FE_DISK_BY_UUID", ADM_BASE "/byuuid", 1);
    setenv("FE_TIMESHIFT_BIN", ADM_BASE "/tools/timeshift", 1);
    setenv("XDG_DATA_HOME", ADM_BASE "/xdg", 1);
    unsetenv("FE_PKEXEC");
}

static bool st_snap_done(void) { return g_sheet.snap && atomic_load(&g_sheet.snap->done); }
static bool st_snapl_done(void) { Pane *p = active_pane(); return p->sv && p->sv->l && atomic_load(&p->sv->l->done) && !p->sv->cmd; }

static void st_click_rect(Rect r) {
    st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, r.x + r.w * 0.5f, r.y + r.h * 0.5f, SDL_BUTTON_LEFT, 1, 0);
    st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
}

static int run_selftest_admin(void) {
    st_wait();
    Pane *p = active_pane();
    char log[4096];

    // --- a copy into a folder you may not write to: the card offers to retry as administrator
    {
        char *src[1] = {(char *)ADM_BASE "/src/a.txt"};
        start_transfer(src, 1, ADM_BASE "/locked", false, false);
        st_wait_ops();
        st_frames(2);
        Op *op = st_last_op();
        ST_CHECK(op && op->nerr == 1 && op->perm_errs == 1 && !st_exists(ADM_BASE "/locked/a.txt"));
        int ci = A.nops - 1;
        ST_CHECK(A.op_retry[ci].w > 0);
        st_shot("admin-card");
        st_click_rect(A.op_retry[ci]); // tests never run the real pkexec
        ST_CHECK(A.nops == ci + 1 && !op->dismissed);
        setenv("FE_PKEXEC", ADM_BASE "/tools/pkexec", 1);
        st_click_rect(A.op_retry[ci]); // (the old card is dismissed and freed)
        // the retry is an administrator copy (on a fast machine it may already be done: a card that worked goes away)
        ST_CHECK((st_last_op() && st_last_op()->admin && st_last_op()->kind == OP_COPY) || st_exists(ADM_BASE "/locked/a.txt"));
        st_wait_ops();
        st_frames(2);
        ST_CHECK(st_exists(ADM_BASE "/locked/a.txt") && (!A.nops || !A.ops[A.nops - 1]->nerr)); // (a card that worked goes away)
        st_read_text(ADM_BASE "/tools/pkexec.log", log, sizeof log);
        ST_CHECK(strstr(log, "--admin-op copy --dst " ADM_BASE "/locked -- " ADM_BASE "/src/a.txt") != NULL);
        chmod(ADM_BASE "/locked", 0555);
    }
    // --- the helper itself: refuses what it doesn't do, reports errors and results
    {
        char *av[] = {"x", "--admin-op", "trash", "--", "/x", NULL};
        ST_CHECK(admin_main(5, av) == 2);
    }

    // --- rename and new folder where it isn't allowed: a dialog offers it as administrator
    pane_navigate(p, ADM_BASE "/locked", true);
    st_wait();
    ST_CHECK(st_select(p, "old.conf"));
    begin_rename(p, p->cursor);
    te_set(&A.ren_te, "new.conf", 0, 8);
    commit_rename();
    ST_CHECK(A.dlg.open && A.dlg.fn == admin_simple_cb && g_admin_pending && g_admin_pending->kind == OP_RENAME);
    st_shot("admin-rename");
    A.dlg.open = false;
    admin_simple_cb(0, false, NULL);
    st_wait_ops();
    ST_CHECK(st_exists(ADM_BASE "/locked/new.conf") && !st_exists(ADM_BASE "/locked/old.conf"));
    chmod(ADM_BASE "/locked", 0555);
    do_action(p, ACT_NEW_FOLDER);
    ST_CHECK(A.dlg.open && g_admin_pending && g_admin_pending->kind == OP_MKDIR);
    A.dlg.open = false;
    admin_simple_cb(0, false, NULL);
    st_wait_ops();
    ST_CHECK(st_exists(ADM_BASE "/locked/New folder"));
    chmod(ADM_BASE "/locked", 0555);
    // --- trash that isn't allowed: "Delete as administrator..." asks first
    st_wait();
    ST_CHECK(st_select(p, "sys.conf"));
    do_action(p, ACT_TRASH);
    st_wait_ops();
    st_frames(2);
    int ti = A.nops - 1;
    ST_CHECK(A.nops && st_last_op()->kind == OP_TRASH && st_last_op()->perm_errs && A.op_retry[ti].w > 0);
    st_click_rect(A.op_retry[ti]);
    ST_CHECK(A.dlg.open && A.dlg.fn == admin_delete_cb && A.dlg.danger);
    A.dlg.open = false;
    admin_delete_cb(0, false, A.dlg.ud);
    st_wait_ops();
    ST_CHECK(!st_exists(ADM_BASE "/locked/sys.conf"));
    st_read_text(ADM_BASE "/tools/pkexec.log", log, sizeof log);
    ST_CHECK(strstr(log, "--admin-op delete -- " ADM_BASE "/locked/sys.conf") && strstr(log, "--admin-op rename -- " ADM_BASE "/locked/old.conf " ADM_BASE "/locked/new.conf") &&
             strstr(log, "--admin-op mkdir -- " ADM_BASE "/locked/New folder"));
    chmod(ADM_BASE "/locked", 0755);

    // --- previous versions: Snapper (#1 = #2), Timeshift (same as now), newest first
    {
        SnapJob *j = snap_start(ADM_BASE "/vol/docs/a.txt");
        for (int k = 0; k < 400 && !atomic_load(&j->done); k++) SDL_Delay(5);
        ST_CHECK(atomic_load(&j->done) && j->nsnap == 4 && j->n == 3);
        if (j->n == 3) {
            ST_CHECK(!strcmp(j->v[0].label, "Timeshift") && j->v[0].same_as_now && !j->v[0].also);
            ST_CHECK(!strcmp(j->v[1].label, "Snapper #3: before update") && j->v[1].size == 14 && !j->v[1].same_as_now &&
                     j->v[1].when == 1772359200);
            ST_CHECK(strstr(j->v[2].label, "Snapper #2") && j->v[2].also == 1 && !strcmp(j->v[2].path, ADM_BASE "/vol/.snapshots/2/snapshot/docs/a.txt"));
        }
        snap_release(j);
        j = snap_start(ADM_BASE "/zvol/sub/z.txt"); // ZFS
        for (int k = 0; k < 400 && !atomic_load(&j->done); k++) SDL_Delay(5);
        ST_CHECK(j->n == 1 && !strcmp(j->v[0].label, "ZFS daily") && !strcmp(j->v[0].path, ADM_BASE "/zvol/.zfs/snapshot/daily/sub/z.txt"));
        snap_release(j);
        j = snap_start(ADM_BASE "/src/a.txt"); // no snapshots
        for (int k = 0; k < 400 && !atomic_load(&j->done); k++) SDL_Delay(5);
        ST_CHECK(j->n == 0); // (on "/": this computer's own /.snapshots may be there, readable or not)
        snap_release(j);
    }
    // in Properties: Open, Restore (keep both / replace -> the current one to the Trash)
    {
        char *pp[1] = {(char *)ADM_BASE "/vol/docs/a.txt"};
        props_open(pp, 1);
        ST_CHECK(st_wait_cond(st_snap_done, 3000));
        st_frames(2);
        g_sheet.pv_scroll = 100000; // (the section is at the bottom)
        st_frames(2);
        ST_CHECK(g_sheet.rsv_open[1].w > 0 && g_sheet.rsv_act[1].w > 0);
        st_shot("props-versions");
        A.last_opened[0] = 0;
        st_click_rect(g_sheet.rsv_open[1]);
        ST_CHECK(!strcmp(A.last_opened, ADM_BASE "/vol/.snapshots/3/snapshot/docs/a.txt"));
        st_click_rect(g_sheet.rsv_act[0]); // same as now: nothing to restore
        ST_CHECK(!A.dlg.open);
        st_click_rect(g_sheet.rsv_act[1]);
        ST_CHECK(A.dlg.open && A.dlg.fn == version_restore_cb && A.dlg.nbuttons == 3);
        A.dlg.open = false;
        version_restore_cb(1, false, NULL); // keep both
        st_wait_ops();
        char kept[64];
        ST_CHECK(st_read_text(ADM_BASE "/vol/docs/a (2026-03-01 11.00).txt", kept, sizeof kept) > 0 || true);
        DIR *d = opendir(ADM_BASE "/vol/docs");
        int found = 0;
        for (struct dirent *e; d && (e = readdir(d));)
            if (strstr(e->d_name, "a (2026-03-01 ")) found++;
        if (d) closedir(d);
        ST_CHECK(found == 1 && !sheet_open());
        props_open(pp, 1);
        ST_CHECK(st_wait_cond(st_snap_done, 3000));
        g_sheet.pv_scroll = 100000;
        st_frames(3);
        st_click_rect(g_sheet.rsv_act[1]);
        A.dlg.open = false;
        version_restore_cb(0, false, NULL); // replace
        st_wait_ops();
        char now[64];
        st_read_text(ADM_BASE "/vol/docs/a.txt", now, sizeof now);
        ST_CHECK(!strcmp(now, "version three\n") && st_exists(ADM_BASE "/xdg/Trash/files/a.txt"));
    }
    // a folder's version: Compare opens it next to the folder, compared
    {
        char *pp[1] = {(char *)ADM_BASE "/vol/docs"};
        props_open(pp, 1);
        ST_CHECK(st_wait_cond(st_snap_done, 3000));
        g_sheet.pv_scroll = 100000;
        st_frames(3);
        ST_CHECK(g_sheet.snap->n >= 1 && g_sheet.snap->v[0].dir && g_sheet.rsv_act[0].w > 0);
        st_click_rect(g_sheet.rsv_act[0]);
        st_wait();
        ST_CHECK(g_cmp.on && !strcmp(g_cmp.root[0], ADM_BASE "/vol/docs") && strstr(g_cmp.root[1], "/docs") && A.npanels == 2);
        cmp_end();
        run_command(CMD_CLOSE_PANEL);
        st_frames(1);
    }
    // snapshots only root can read: the note, then "Show as administrator" lists them through the helper
    if (geteuid() != 0) {
        chmod(ADM_BASE "/vol2/.snapshots", 0);
        char *pp[1] = {(char *)ADM_BASE "/vol2/r.txt"};
        props_open(pp, 1);
        ST_CHECK(st_wait_cond(st_snap_done, 3000));
        ST_CHECK(g_sheet.snap->noaccess && !g_sheet.snap->n && strstr(g_sheet.snap->note, "only the administrator"));
        g_sheet.pv_scroll = 100000;
        st_frames(3);
        ST_CHECK(g_sheet.rsv_admin.w > 0);
        st_shot("props-noaccess");
        st_click_rect(g_sheet.rsv_admin);
        ST_CHECK(g_sheet.snap->admin);
        ST_CHECK(st_wait_cond(st_snap_done, 5000));
        ST_CHECK(g_sheet.snap->n == 1 && !strcmp(g_sheet.snap->v[0].label, "Snapper #5: root"));
        st_read_text(ADM_BASE "/tools/pkexec.log", log, sizeof log);
        ST_CHECK(strstr(log, "--admin-op versions " ADM_BASE "/vol2/r.txt") != NULL);
        g_sheet.pv_scroll = 100000;
        st_frames(3);
        st_click_rect(g_sheet.rsv_open[0]); // copied to the cache as root, opened from there
        st_wait_ops();
        ST_CHECK(strstr(A.last_opened, "/versions/") && strstr(A.last_opened, "r.txt") && st_exists(A.last_opened));
        st_read_text(ADM_BASE "/tools/pkexec.log", log, sizeof log);
        ST_CHECK(strstr(log, "--admin-op sync --replace -- " ADM_BASE "/vol2/.snapshots/5/snapshot/r.txt") != NULL);
        sheet_close();
    }
    // --- the snapshots of a drive
    {
        char r[PATH_MAX], lv[PATH_MAX], nm[96];
        ST_CHECK(snap_where("/.snapshots/41/snapshot/etc/x", r, sizeof r, lv, sizeof lv, nm, sizeof nm) && !strcmp(r, "/.snapshots/41/snapshot") &&
                 !strcmp(lv, "/etc/x") && !strcmp(nm, "#41"));
        ST_CHECK(snap_where("/.snapshots/41/snapshot", r, sizeof r, lv, sizeof lv, nm, sizeof nm) && !strcmp(lv, "/"));
        ST_CHECK(snap_where("/home/.snapshots/3/snapshot/a", r, sizeof r, lv, sizeof lv, nm, sizeof nm) && !strcmp(lv, "/home/a"));
        ST_CHECK(snap_where("/tank/.zfs/snapshot/daily/f", r, sizeof r, lv, sizeof lv, nm, sizeof nm) && !strcmp(lv, "/tank/f") && !strcmp(nm, "daily"));
        ST_CHECK(!snap_where("/x/.snapshots/abc", r, sizeof r, lv, sizeof lv, nm, sizeof nm) && !snap_where("/home/me", r, sizeof r, lv, sizeof lv, nm, sizeof nm));
        // Timeshift: rsync mode (the whole system under localhost), btrfs mode (@ is /, @home is /home)
        ST_CHECK(snap_where("/timeshift/snapshots/2026-06-01_09-30-00/localhost/etc/x", r, sizeof r, lv, sizeof lv, nm, sizeof nm) &&
                 !strcmp(r, "/timeshift/snapshots/2026-06-01_09-30-00/localhost") && !strcmp(lv, "/etc/x") && !strcmp(nm, "2026-06-01 09:30 (Timeshift)"));
        ST_CHECK(snap_where("/run/timeshift/9/backup/timeshift/snapshots/2026-06-01_09-30-00/localhost", r, sizeof r, lv, sizeof lv, nm, sizeof nm) && !strcmp(lv, "/"));
        ST_CHECK(snap_where("/run/timeshift/9/backup/timeshift-btrfs/snapshots/2026-06-01_09-30-00/@home/me/a", r, sizeof r, lv, sizeof lv, nm, sizeof nm) &&
                 !strcmp(r, "/run/timeshift/9/backup/timeshift-btrfs/snapshots/2026-06-01_09-30-00/@home") && !strcmp(lv, "/home/me/a"));
        ST_CHECK(snap_where("/x/timeshift-btrfs/snapshots/2026-06-01_09-30-00/@/etc", r, sizeof r, lv, sizeof lv, nm, sizeof nm) && !strcmp(lv, "/etc"));
        ST_CHECK(!snap_where("/timeshift/snapshots/2026-06-01_09-30-00", r, sizeof r, lv, sizeof lv, nm, sizeof nm) &&
                 !snap_where("/timeshift/snapshots/2026-06-01_09-30-00/other/x", r, sizeof r, lv, sizeof lv, nm, sizeof nm));
    }
    p = active_pane();
    pane_navigate(p, ADM_BASE "/vol", true);
    st_wait();
    snapview_toggle(p);
    st_frames(1);
    ST_CHECK(p->snapv && p->sv);
    SnapView *sv = p->sv;
    ST_CHECK(st_wait_cond(st_snapl_done, 3000));
    st_frames(2);
    SnapList *sl = sv->l;
    // (Timeshift's btrfs snapshot of the same subvolume next to Snapper's)
    ST_CHECK(sl && sl->manage && sl->ts_manage && !strcmp(sl->config, "root") && !strcmp(sl->tool, "Snapper, Timeshift") && sl->n == 4 &&
             !strcmp(sl->mnt, ADM_BASE "/vol"));
    if (sl && sl->n == 4) {
        ST_CHECK(!strcmp(sl->s[0].type, "timeshift") && !strcmp(sl->s[0].root, ADM_BASE "/run/7/backup/timeshift-btrfs/snapshots/2026-04-01_12-00-00/@vol"));
        ST_CHECK(sl->s[1].num == 3 && !strcmp(sl->s[1].cleanup, "timeline") && sl->s[2].num == 2 && !strcmp(sl->s[2].type, "post") && sl->s[2].pre == 1 &&
                 sl->s[3].num == 1 && !strcmp(sl->s[3].desc, "pacman -Syu") && !strcmp(sl->s[2].root, ADM_BASE "/vol/.snapshots/2/snapshot"));
    }
    st_shot("snapshots-list");
    // what a change did (a post and its pre): snapper status, then a changed file opens in the snapshot
    st_key(SDLK_DOWN, 0);
    st_key(SDLK_DOWN, 0);
    ST_CHECK(sv->cur == 2);
    snap_action(p, ACT_SNAP_CHANGES);
    for (int k = 0; k < 400 && sv->cmd; k++) { st_frames(1); SDL_Delay(5); }
    ST_CHECK(sv->mode == 1 && sv->nch == 3 && sv->ca == 1 && sv->cb == 2 && sv->ch[1].st == '+' && !strcmp(sv->ch[2].path, ADM_BASE "/vol/gone.txt"));
    st_shot("snapshots-changes");
    st_key(SDLK_RETURN, 0);
    st_wait();
    st_frames(1);
    ST_CHECK(!p->snapv && !strcmp(p->path, ADM_BASE "/vol/.snapshots/2/snapshot/docs") && !strcmp(st_cursor_name(p), "a.txt") && p->ly.has_snapbar);
    st_shot("snapshot-bar");
    // restore from the bar: the selected file back where it lives now (the current one to the Trash)
    {
        Rect bc, br, ba;
        snapbar_layout(p, &bc, &br, &ba);
        sel_set(p, p->cursor, true);
        st_click_rect(br);
        ST_CHECK(A.dlg.open && A.dlg.fn == snap_restore_cb && A.dlg.nitems == 1 && strstr(A.dlg.item_sub[0], "/vol/docs/a.txt"));
        A.dlg.open = false;
        snap_restore_cb(0, false, NULL);
        st_wait_ops();
        char now[64];
        st_read_text(ADM_BASE "/vol/docs/a.txt", now, sizeof now);
        ST_CHECK(!strcmp(now, "v1\n"));
        // Compare with now: this folder in the snapshot next to the live one
        snapbar_layout(p, &bc, &br, &ba);
        st_click_rect(bc);
        st_wait();
        ST_CHECK(g_cmp.on && !strcmp(g_cmp.root[0], ADM_BASE "/vol/.snapshots/2/snapshot/docs") && !strcmp(g_cmp.root[1], ADM_BASE "/vol/docs"));
        cmp_end();
        run_command(CMD_CLOSE_PANEL);
        st_frames(1);
        p = active_pane();
        snapbar_layout(p, &bc, &br, &ba);
        st_click_rect(ba); // All snapshots
        ST_CHECK(st_wait_cond(st_snapl_done, 3000));
        ST_CHECK(p->snapv && !strcmp(p->path, ADM_BASE "/vol/docs"));
        sv = p->sv;
    }
    // create, and delete two (Ctrl+click picks a second one)
    snap_action(p, ACT_SNAP_CREATE);
    ST_CHECK(A.dlg.open && A.dlg.fn == snap_create_cb);
    A.dlg.open = false;
    snap_create_cb(0, false, p);
    for (int k = 0; k < 400 && (sv->cmd || !atomic_load(&sv->l->done)); k++) { st_frames(1); SDL_Delay(5); }
    st_frames(2);
    sv->cur = 1;
    {
        f32 y2 = sv->list.y + snap_row_h() * 2.5f;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, sv->list.x + 100, y2, SDL_BUTTON_LEFT, 1, SDL_KMOD_CTRL);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, 0, 0, 0, 0, 0);
    }
    ST_CHECK(sv->cur == 1 && sv->sel2 == 2);
    st_key(SDLK_DELETE, 0);
    ST_CHECK(A.dlg.open && A.dlg.fn == snap_delete_cb && A.dlg.danger);
    A.dlg.open = false;
    snap_delete_cb(0, false, p);
    for (int k = 0; k < 400 && (sv->cmd || !atomic_load(&sv->l->done)); k++) { st_frames(1); SDL_Delay(5); }
    st_read_text(ADM_BASE "/tools/snapper.log", log, sizeof log);
    ST_CHECK(strstr(log, "-c root create --description File Explorer --cleanup-algorithm number") && strstr(log, "-c root delete 3 2") &&
             strstr(log, "-c root status 1..2"));
    st_key(SDLK_ESCAPE, 0);
    ST_CHECK(!p->snapv && !p->sv);
    // Timeshift in rsync mode on an ext4 drive: listed (with info.json's time, comment and tags), browsed with the bar,
    // a file restored, created and deleted as administrator (the stand-in pkexec)
    pane_navigate(p, ADM_BASE "/vol3/notes", true);
    st_wait();
    snapview_toggle(p);
    st_frames(1);
    ST_CHECK(st_wait_cond(st_snapl_done, 3000));
    st_frames(2);
    sv = p->sv;
    sl = sv->l;
    ST_CHECK(!strcmp(sl->tool, "Timeshift") && sl->n == 2 && sl->ts_manage && !sl->manage && !strcmp(sl->mnt, ADM_BASE "/vol3"));
    if (sl->n == 2) {
        ST_CHECK(!strcmp(sl->s[0].name, "2026-07-01_08-00-00") && sl->s[0].when == snap_parse_time("2026-07-01_08-00-00", false) &&
                 !strcmp(sl->s[0].cleanup, "on demand, boot") &&
                 !strcmp(sl->s[0].root, ADM_BASE "/backupdisk/timeshift/snapshots/2026-07-01_08-00-00/localhost" ADM_BASE "/vol3"));
        ST_CHECK(sl->s[1].when == 1780300000 && !strcmp(sl->s[1].desc, "before upgrade") && !strcmp(sl->s[1].cleanup, "daily, weekly") &&
                 !strcmp(sl->s[1].root, ADM_BASE "/tsroot/timeshift/snapshots/2026-06-01_09-30-00/localhost" ADM_BASE "/vol3"));
    }
    st_shot("snapshots-timeshift");
    st_key(SDLK_DOWN, 0);
    st_key(SDLK_RETURN, 0);
    st_wait();
    st_frames(1);
    ST_CHECK(!p->snapv && !strcmp(p->path, ADM_BASE "/tsroot/timeshift/snapshots/2026-06-01_09-30-00/localhost" ADM_BASE "/vol3") && p->ly.has_snapbar);
    pane_navigate(p, ADM_BASE "/tsroot/timeshift/snapshots/2026-06-01_09-30-00/localhost" ADM_BASE "/vol3/notes", true);
    st_wait();
    st_frames(1);
    st_shot("snapshot-bar-timeshift");
    {
        Rect bc, br, ba;
        snapbar_layout(p, &bc, &br, &ba);
        sel_set(p, p->cursor, true);
        st_click_rect(br);
        ST_CHECK(A.dlg.open && A.dlg.fn == snap_restore_cb && A.dlg.nitems == 1 && strstr(A.dlg.item_sub[0], "/vol3/notes/n.txt") &&
                 strstr(A.dlg.msg, "2026-06-01 09:30 (Timeshift)"));
        A.dlg.open = false;
        snap_restore_cb(0, false, NULL);
        st_wait_ops();
        char now[64];
        st_read_text(ADM_BASE "/vol3/notes/n.txt", now, sizeof now);
        ST_CHECK(!strcmp(now, "old notes\n"));
        st_click_rect(ba); // All snapshots
        ST_CHECK(st_wait_cond(st_snapl_done, 3000));
        ST_CHECK(p->snapv && !strcmp(p->path, ADM_BASE "/vol3/notes"));
        sv = p->sv;
    }
    snap_action(p, ACT_SNAP_CREATE);
    ST_CHECK(A.dlg.open && A.dlg.fn == snap_create_cb && strstr(A.dlg.msg, "Timeshift"));
    A.dlg.open = false;
    snap_create_cb(0, false, p);
    ST_CHECK(sv->cmd && sv->cmd_kind == SC_CREATE);
    for (int k = 0; k < 400 && (sv->cmd || !atomic_load(&sv->l->done)); k++) { st_frames(1); SDL_Delay(5); }
    st_frames(2);
    sv->cur = 1;
    st_key(SDLK_DELETE, 0);
    ST_CHECK(A.dlg.open && A.dlg.fn == snap_delete_cb && A.dlg.danger);
    A.dlg.open = false;
    snap_delete_cb(0, false, p);
    for (int k = 0; k < 400 && (sv->cmd || !atomic_load(&sv->l->done)); k++) { st_frames(1); SDL_Delay(5); }
    st_read_text(ADM_BASE "/tools/timeshift.log", log, sizeof log);
    ST_CHECK(strstr(log, "--create --comments File Explorer --scripted") && strstr(log, "--delete --snapshot 2026-06-01_09-30-00 --scripted"));
    st_read_text(ADM_BASE "/tools/pkexec.log", log, sizeof log);
    ST_CHECK(strstr(log, ADM_BASE "/tools/timeshift --delete") != NULL);
    st_key(SDLK_ESCAPE, 0);
    // snapshots only root may see: "Allow me to browse snapshots" (pkexec once), then they're listed
    if (geteuid() != 0) {
        chmod(ADM_BASE "/vol2/.snapshots", 0);
        pane_navigate(p, ADM_BASE "/vol2", true);
        st_wait();
        snapview_toggle(p);
        st_frames(1);
        ST_CHECK(st_wait_cond(st_snapl_done, 3000));
        st_frames(2);
        sv = p->sv;
        ST_CHECK(sv->l->noaccess && !sv->l->manage && !sv->l->n && !strcmp(sv->l->config, "home2") && sv->ballow.w > 0);
        st_shot("snapshots-noaccess");
        st_click_rect(sv->ballow);
        ST_CHECK(sv->cmd && sv->cmd_kind == SC_ALLOW);
        for (int k = 0; k < 600 && (sv->cmd || !atomic_load(&sv->l->done)); k++) { st_frames(1); SDL_Delay(5); }
        st_frames(1);
        ST_CHECK(sv->l->manage && sv->l->n == 1 && sv->l->s[0].num == 5);
        st_read_text(ADM_BASE "/tools/pkexec.log", log, sizeof log);
        struct passwd *pw = getpwuid(getuid());
        char want[256];
        snprintf(want, sizeof want, "--admin-op snapper-allow home2 %s", pw ? pw->pw_name : "");
        ST_CHECK(strstr(log, want) != NULL);
        st_read_text(ADM_BASE "/tools/snapper.log", log, sizeof log);
        snprintf(want, sizeof want, "-c home2 set-config ALLOW_USERS=bob %s SYNC_ACL=yes", pw ? pw->pw_name : "");
        ST_CHECK(strstr(log, want) != NULL);
        snapview_toggle(p);
    }
    for (int i = 0; i < A.nplaces; i++)
        if (A.places[i].kind == PK_DRIVE) {
            menu_open_place(i, 10, 10);
            ST_CHECK(st_menu_has(ACT_SNAP_PLACE));
            A.menu.open = false;
            break;
        }
    unsetenv("FE_SNAPPER");
    unsetenv("FE_SNAPPER_CONFIGS");
    unsetenv("FE_MOUNTINFO");
    unsetenv("FE_TIMESHIFT");
    unsetenv("FE_TIMESHIFT_ROOT");
    unsetenv("FE_TIMESHIFT_CONF");
    unsetenv("FE_DISK_BY_UUID");
    unsetenv("FE_TIMESHIFT_BIN");
    unsetenv("FE_PKEXEC");
    chmod(ADM_BASE "/locked", 0755);
    chmod(ADM_BASE "/vol2/.snapshots", 0755);

    fprintf(stderr, "selftest-admin: %s (%d failures)\n", st_fail ? "FAIL" : "ok", st_fail);
    return st_fail ? 1 : 0;
}

// --- self-test for Borg backups: recognising repositories (1 and 2), opening (stand-in borg), passphrase, restore, unmount ---

#define BORG_BASE "/tmp/fe-borg-test"

static void st_borg_repo(const char *dir, int kind) { // the files borg keeps at a repository's top
    char p[PATH_MAX];
    if (kind == 1) {
        snprintf(p, sizeof p, "%s/data/0", dir);
        mkdir_p(p, 0755);
        snprintf(p, sizeof p, "%s/README", dir);
        st_write_text(p, "This is a Borg Backup repository.\nSee https://borgbackup.readthedocs.io/\n");
        snprintf(p, sizeof p, "%s/config", dir);
        st_write_text(p, "[repository]\nversion = 1\n");
        snprintf(p, sizeof p, "%s/nonce", dir);
        st_write(p, 16);
    } else {
        snprintf(p, sizeof p, "%s/archives", dir);
        mkdir_p(p, 0755);
        snprintf(p, sizeof p, "%s/packs", dir);
        mkdir_p(p, 0755);
        snprintf(p, sizeof p, "%s/config", dir);
        mkdir_p(p, 0755);
        snprintf(p, sizeof p, "%s/config/readme", dir);
        st_write_text(p, "This is a Borg Backup repository.\nSee https://borgbackup.readthedocs.io/\n");
    }
}

static void st_setup_borg(void) {
    st_rmrf(BORG_BASE);
    mkdir_p(BORG_BASE "/tools", 0755);
    mkdir_p(BORG_BASE "/mnt", 0755);
    mkdir_p(BORG_BASE "/live/docs", 0755);
    mkdir_p(BORG_BASE "/home/plain", 0755);
    st_write_text(BORG_BASE "/live/docs/a.txt", "now\n");
    st_borg_repo(BORG_BASE "/home/repo1", 1);
    st_borg_repo(BORG_BASE "/home/enc1", 1);
    st_borg_repo(BORG_BASE "/home/locked1", 1);
    st_borg_repo(BORG_BASE "/home/repo2", 2);
    st_borg_repo(BORG_BASE "/gvfs/sftp:host=vm,user=me,port=2222/srv/remote1", 1);
    mkdir_p(BORG_BASE "/home/fake/data", 0755); // looks like one, but its README says otherwise
    st_write_text(BORG_BASE "/home/fake/README", "my notes\n");
    st_write_text(BORG_BASE "/home/fake/config", "x\n");
    st_write_text(BORG_BASE "/tools/mounts", "");
    // the stand-in borg: logs, answers --version, "mounts" by making an archive folder (and a mount table line);
    // ssh:// finds no borg on the server, *locked* is busy, *enc* wants the passphrase "secret" (asked via the pass command)
    static const char script[] =
        "#!/bin/sh\n"
        "echo \"borg $* | PASSCMD=${BORG_PASSCOMMAND-unset} PASS=${BORG_PASSPHRASE-unset} RSH=${BORG_RSH-}\" >> " BORG_BASE "/tools/log\n"
        "[ \"$1\" = --version ] && { echo \"borg VERSION\"; exit 0; }\n"
        "prev=; repo=; n=0; for a; do n=$((n+1)); [ \"$prev\" = -r ] && repo=$a; prev=$a; last2=$last; last=$a; done\n"
        "[ -n \"$repo\" ] || repo=$last2\n"
        "case \"$repo\" in ssh://*) echo 'Remote: sh: 1: borg: not found'; echo '{\"type\": \"log_message\", \"levelname\": \"ERROR\", \"message\": \"Connection closed by remote host\", \"msgid\": \"ConnectionClosedWithHint\"}'; exit 2;; esac\n"
        "case \"$repo\" in *locked*) echo '{\"type\": \"log_message\", \"levelname\": \"ERROR\", \"message\": \"Failed to create/acquire the lock\", \"msgid\": \"LockTimeout\"}'; exit 2;; esac\n"
        "case \"$repo\" in *enc*) if [ -n \"${BORG_PASSCOMMAND-}\" ]; then p=$(eval \"$BORG_PASSCOMMAND\"); else p=${BORG_PASSPHRASE-}; fi\n"
        "  [ \"$p\" = secret ] || { echo '{\"type\": \"log_message\", \"levelname\": \"ERROR\", \"message\": \"Passphrase supplied in BORG_PASSPHRASE, by BORG_PASSCOMMAND, or via BORG_PASSPHRASE_FD is incorrect.\", \"msgid\": \"PassphraseWrong\"}'; exit 2; };; esac\n"
        "mkdir -p \"$last/2026-11-01/${BASE#/}/live/docs\" \"$last/2026-11-01/nowhere/else\" && echo then > \"$last/2026-11-01/${BASE#/}/live/docs/a.txt\"\n"
        "echo \"borgfs $last fuse ro 0 0\" >> " BORG_BASE "/tools/mounts\n";
    for (int v = 1; v <= 2; v++) {
        char body[sizeof script + 64], *o = body;
        for (const char *s = script; *s;) {
            if (!strncmp(s, "VERSION", 7)) { o += sprintf(o, "%s", v == 1 ? "1.4.5" : "2.0.0b24"); s += 7; }
            else if (!strncmp(s, "${BASE#/}", 9)) { o += sprintf(o, "%s", BORG_BASE + 1); s += 9; }
            else *o++ = *s++;
        }
        *o = 0;
        const char *path = v == 1 ? BORG_BASE "/tools/borg" : BORG_BASE "/tools/borg2";
        st_write_text(path, body);
        chmod(path, 0755);
    }
    st_write_text(BORG_BASE "/tools/fusermount", "#!/bin/sh\necho \"fusermount $*\" >> " BORG_BASE "/tools/log\nfor a; do last=$a; done\n"
                  "rm -rf \"$last\"/*\ngrep -v \" $last \" " BORG_BASE "/tools/mounts > " BORG_BASE "/tools/m.tmp; mv " BORG_BASE "/tools/m.tmp " BORG_BASE "/tools/mounts\n");
    chmod(BORG_BASE "/tools/fusermount", 0755);
    st_write_text(BORG_BASE "/tools/secret-tool", "#!/bin/sh\necho \"secret-tool $*\" >> " BORG_BASE "/tools/log\n");
    chmod(BORG_BASE "/tools/secret-tool", 0755);
    setenv("FE_BORG", BORG_BASE "/tools/borg", 1);
    setenv("FE_BORG2", BORG_BASE "/tools/borg2", 1);
    setenv("FE_BORG1", BORG_BASE "/tools/none", 1);
    setenv("FE_BORG_MNT", BORG_BASE "/mnt", 1);
    setenv("FE_BORG_MOUNTS", BORG_BASE "/tools/mounts", 1);
    setenv("FE_FUSERMOUNT", BORG_BASE "/tools/fusermount", 1);
    setenv("FE_SECRET_TOOL", BORG_BASE "/tools/secret-tool", 1);
    setenv("FE_GVFS_DIR", BORG_BASE "/gvfs", 1);
    unsetenv("BORG_PASSPHRASE");
    unsetenv("BORG_PASSCOMMAND");
    unsetenv("BORG_PASSPHRASE_FD");
}

static void st_borg_settle(void) {
    for (int k = 0; k < 1500 && (g_borg_busy || git_busy()); k++) { st_frames(1); SDL_Delay(2); }
    st_wait();
    st_frames(2);
}

static const GitSub *st_borg_sub(Pane *p, const char *name) {
    for (u32 i = 0; p->L && i < p->L->count; i++)
        if (!strcmp(p->L->entries[i].name, name)) return git_entry_sub(p, &p->L->entries[i]);
    return NULL;
}

static bool st_borg_log(const char *needle) {
    static char buf[1 << 16];
    st_read_text(BORG_BASE "/tools/log", buf, sizeof buf);
    return strstr(buf, needle) != NULL;
}

static void st_borg_open(Pane *p, const char *name) { // Enter on a repository in the folder shown
    if (!st_select(p, name)) return;
    open_entry(p, p->cursor, false);
    st_borg_settle();
}

static int run_selftest_borg(void) {
    st_wait();
    Pane *p = active_pane();
    char r[PATH_MAX], lv[PATH_MAX], nm[96], mp[PATH_MAX], want[PATH_MAX * 2];

    // --- paths: where our mounts are, which archive a path is in and where it lives now, borg's words
    borg_mount_point(BORG_BASE "/home/repo1", mp, sizeof mp);
    ST_CHECK(!strncmp(mp, BORG_BASE "/mnt/repo1-", strlen(BORG_BASE "/mnt/repo1-")) && strlen(mp) == strlen(BORG_BASE "/mnt/repo1-") + 8);
    snprintf(want, sizeof want, "%s/2026-11-01/home/me/x.txt", mp);
    ST_CHECK(borg_where(want, r, sizeof r, lv, sizeof lv, nm, sizeof nm) && !strcmp(lv, "/home/me/x.txt") && !strcmp(nm, "2026-11-01"));
    snprintf(want, sizeof want, "%s/2026-11-01", mp);
    ST_CHECK(borg_where(want, r, sizeof r, lv, sizeof lv, nm, sizeof nm) && !strcmp(lv, "/") && !strcmp(r, want));
    ST_CHECK(!borg_where(mp, r, sizeof r, lv, sizeof lv, nm, sizeof nm) && !borg_where("/home/me", r, sizeof r, lv, sizeof lv, nm, sizeof nm));
    snprintf(want, sizeof want, "%s/2026-11-01/etc", mp);
    ST_CHECK(snap_where(want, r, sizeof r, lv, sizeof lv, nm, sizeof nm) && !strcmp(lv, "/etc")); // the snapshot bar knows them too
    char err[400];
    ST_CHECK(borg_classify("{\"type\": \"log_message\", \"levelname\": \"ERROR\", \"message\": \"Passphrase \\\"x\\\" wrong\", \"msgid\": \"PassphraseWrong\"}\n",
                           err, sizeof err) == BORG_E_PASS && !strcmp(err, "Passphrase \"x\" wrong"));
    ST_CHECK(borg_classify("borg mount not available: no FUSE support, BORG_FUSE_IMPL=pyfuse3,llfuse.\n", err, sizeof err) == BORG_E_FUSE);
    ST_CHECK(borg_classify("something odd\n\n", err, sizeof err) == BORG_E_OTHER && !strcmp(err, "something odd"));
    ST_CHECK(borg_repo_kind(BORG_BASE "/home/repo1") == BORG_V1 && borg_repo_kind(BORG_BASE "/home/repo2") == BORG_V2);
    ST_CHECK(!borg_repo_kind(BORG_BASE "/home/fake") && !borg_repo_kind(BORG_BASE "/home/plain") && !borg_repo_kind(BORG_BASE "/nothing"));

    // --- marked in their folder like git repositories (git status off: still marked)
    A.git_status = false;
    pane_navigate(p, BORG_BASE "/home", true);
    st_borg_settle();
    const GitSub *g = st_borg_sub(p, "repo1");
    ST_CHECK(g && g->borg == BORG_V1 && (g = st_borg_sub(p, "repo2")) && g->borg == BORG_V2);
    ST_CHECK(!st_borg_sub(p, "fake") && !st_borg_sub(p, "plain") && !pane_borg_repo(p) && !p->ly.has_snapbar);
    st_shot("borg-pills");
    int pview = p->view;
    p->view = VIEW_GRID;
    st_shot("borg-pills-grid");
    p->view = pview;

    // --- open from the folder it's in: mounted, the pane shows its backups (dated folders)
    st_borg_open(p, "repo1");
    ST_CHECK(!strcmp(p->path, mp) && g_borg_n == 1 && st_exists(BORG_BASE "/tools/mounts"));
    snprintf(want, sizeof want, "--log-json mount -o ignore_permissions " BORG_BASE "/home/repo1 %s | PASSCMD=unset PASS= RSH=", mp);
    ST_CHECK(st_borg_log(want));
    ST_CHECK(p->ly.has_snapbar && st_cursor_name(p)[0] && !strcmp(st_cursor_name(p), "2026-11-01"));
    st_shot("borg-archives");
    // read-only: no new folders, no renames
    A.toast[0] = 0;
    do_action(p, ACT_NEW_FOLDER);
    ST_CHECK(strstr(A.toast, "read-only") != NULL);
    // inside an archive: the bar compares with now and restores
    snprintf(want, sizeof want, "%s/2026-11-01" BORG_BASE "/live/docs", mp);
    pane_navigate(p, want, true);
    st_wait();
    ST_CHECK(p->ly.has_snapbar && st_select(p, "a.txt"));
    st_shot("borg-inside");
    snap_restore(p);
    ST_CHECK(A.dlg.open && g_snap_rn == 1 && !strcmp(g_snap_rdst[0], BORG_BASE "/live/docs/a.txt") && strstr(A.dlg.msg, "backup 2026-11-01"));
    if (A.dlg.open) dialog_choose(1);
    ST_CHECK(!A.dlg.open && !g_snap_rn);
    snprintf(want, sizeof want, "%s/2026-11-01/nowhere", mp); // not from this computer: copy it out instead
    pane_navigate(p, want, true);
    st_wait();
    st_select(p, "else");
    snap_restore(p);
    ST_CHECK(!A.dlg.open && strstr(A.toast, "isn't on this computer"));
    snprintf(want, sizeof want, "%s/2026-11-01", mp); // a whole backup: not at once
    pane_navigate(p, want, true);
    st_wait();
    sel_clear(p);
    p->cursor = -1; // nothing picked: the whole backup isn't restored at once
    snap_restore(p);
    ST_CHECK(!A.dlg.open && strstr(A.toast, "Pick what to restore"));
    // the bar's last button: all backups, then closing them (back to the repository's folder, unmounted)
    ST_CHECK(p->ly.has_snapbar);
    Rect bc, br, ba;
    snapbar_layout(p, &bc, &br, &ba);
    snapbar_click(p, ba.x + 2, ba.y + 2);
    ST_CHECK(!strcmp(p->path, mp));
    snapbar_layout(p, &bc, &br, &ba);
    snapbar_click(p, ba.x + 2, ba.y + 2);
    st_borg_settle();
    for (int k = 0; k < 500 && st_exists(mp); k++) { st_frames(1); SDL_Delay(2); }
    ST_CHECK(!strcmp(p->path, BORG_BASE "/home") && g_borg_n == 0 && !st_exists(mp) && !borg_is_mounted(mp));
    snprintf(want, sizeof want, "fusermount -u %s", mp);
    ST_CHECK(st_borg_log(want));

    // --- a repository's own files (new tab / "Open as folder"): a bar offers its backups
    st_select(p, "repo1");
    do_action(p, ACT_BORG_RAW);
    st_borg_settle();
    ST_CHECK(!strcmp(p->path, BORG_BASE "/home/repo1") && pane_borg_repo(p) == BORG_V1 && p->ly.has_snapbar);
    st_shot("borg-raw");
    snapbar_layout(p, &bc, &br, &ba);
    snapbar_click(p, ba.x + 2, ba.y + 2);
    st_borg_settle();
    ST_CHECK(!strcmp(p->path, mp) && g_borg_n == 1);
    // unused for a while: unmounted
    pane_navigate(p, BORG_BASE "/home", true);
    st_wait();
    u64 idle = g_borg_idle_ns, idle1 = g_borg1_idle_ns;
    g_borg1_idle_ns = 0;
    st_frames(2);
    for (int k = 0; k < 500 && st_exists(mp); k++) { st_frames(1); SDL_Delay(2); }
    g_borg1_idle_ns = idle1;
    ST_CHECK(g_borg_n == 0 && !borg_is_mounted(mp));

    // --- encrypted: asked for the passphrase (a wrong one: asked again), it goes through our askpass, never argv
    st_borg_open(p, "enc1");
    ST_CHECK(g_sheet.kind == SHEET_SECRET && g_sheet.sec_borg && !g_sheet.sec_borg->retry && !strcmp(p->path, BORG_BASE "/home"));
    st_shot("borg-passphrase");
    te_set(&g_sheet.cn[CN_PASS], "nope", 4, 4);
    secret_prompt_answer(true);
    st_borg_settle();
    ST_CHECK(g_sheet.kind == SHEET_SECRET && g_sheet.sec_borg && g_sheet.sec_borg->retry);
    te_set(&g_sheet.cn[CN_PASS], "secret", 6, 6);
    g_sheet.cn_remember_pw = true;
    secret_prompt_answer(true);
    st_borg_settle();
    borg_mount_point(BORG_BASE "/home/enc1", mp, sizeof mp);
    ST_CHECK(g_sheet.kind != SHEET_SECRET && !strcmp(p->path, mp) && borg_is_mounted(mp));
    ST_CHECK(st_borg_log("PASSCMD='") && st_borg_log("' passphrase PASS=unset") && !st_borg_log("secret ") && !st_borg_log("nope"));
    ST_CHECK(st_borg_log("secret-tool lookup application file-explorer uri borg:" BORG_BASE "/home/enc1"));
    ST_CHECK(st_borg_log("secret-tool store --label Files: borg:" BORG_BASE "/home/enc1"));
    { // the passphrase files are gone
        char pat[64];
        snprintf(pat, sizeof pat, "fe-askpass-%d-b", (int)getpid());
        const char *rt = getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "/tmp";
        DIR *dd = opendir(rt);
        struct dirent *de;
        bool left = false;
        while (dd && (de = readdir(dd))) left |= !strncmp(de->d_name, pat, strlen(pat));
        if (dd) closedir(dd);
        ST_CHECK(!left);
    }
    // cancelled: nothing happens
    pane_navigate(p, BORG_BASE "/home", true);
    st_wait();
    borg_close(p); // (not inside: nothing)
    for (int k = 0; k < g_borg_n; k++) pool_submit(borg_umount_job, strdup(g_borg[k].mp));
    g_borg_n = 0;
    st_borg_settle();
    for (int k = 0; k < 500 && borg_is_mounted(mp); k++) { st_frames(1); SDL_Delay(2); }
    st_write_text(BORG_BASE "/tools/log", "");
    st_borg_open(p, "enc1");
    ST_CHECK(g_sheet.kind == SHEET_SECRET);
    secret_prompt_answer(false);
    st_borg_settle();
    ST_CHECK(g_sheet.kind != SHEET_SECRET && !strcmp(p->path, BORG_BASE "/home") && !borg_is_mounted(mp));

    // --- busy (a backup running): said so; Borg 2: its own program and argument order
    st_borg_open(p, "locked1");
    ST_CHECK(strstr(A.toast, "busy") && !strcmp(p->path, BORG_BASE "/home"));
    st_borg_open(p, "repo2");
    borg_mount_point(BORG_BASE "/home/repo2", mp, sizeof mp);
    snprintf(want, sizeof want, "--log-json -r " BORG_BASE "/home/repo2 mount -o ignore_permissions %s |", mp);
    ST_CHECK(!strcmp(p->path, mp) && st_borg_log(want) && st_borg_log("borg2 ") == false);
    pane_navigate(p, BORG_BASE "/home", true);
    st_wait();
    setenv("FE_BORG2", BORG_BASE "/tools/missing", 1); // no Borg 2 installed (a Borg 1 doesn't do)
    borg_open(p, BORG_BASE "/home/repo2", BORG_V2);
    st_borg_settle();
    ST_CHECK(strstr(A.toast, "Borg 2") && !strcmp(p->path, BORG_BASE "/home"));
    setenv("FE_BORG2", BORG_BASE "/tools/borg2", 1);

    // --- saved in the sidebar: name and icon kept in the settings, opened with a click; Borg 2 can stay mounted
    pane_navigate(p, BORG_BASE "/home", true);
    st_borg_settle();
    for (int k = 0; k < g_borg_n; k++) pool_submit(borg_umount_job, strdup(g_borg[k].mp));
    g_borg_n = 0;
    st_borg_settle();
    st_select(p, "repo2");
    do_action(p, ACT_BORG_SAVE);
    st_select(p, "repo1");
    do_action(p, ACT_BORG_SAVE);
    do_action(p, ACT_BORG_SAVE); // (once)
    ST_CHECK(A.nborgs == 2 && !strcmp(A.borgs[0].name, "repo2") && A.borgs[0].kind == BORG_V2 && !strcmp(A.borgs[1].repo, BORG_BASE "/home/repo1"));
    int brow = -1;
    for (int i = 0; i < A.nplaces; i++) if (A.places[i].kind == PK_BORG && A.places[i].item == 0) brow = i;
    ST_CHECK(brow >= 0 && !A.places[brow].path[0] && !strcmp(A.places[brow].label, "repo2"));
    ST_CHECK(net_icon_find(BORG_ICON_DEFAULT) >= 0);
    st_shot("borg-sidebar-default");
    borg_edit_open(0);
    ST_CHECK(g_sheet.kind == SHEET_BORG && !strcmp(g_sheet.cn[CN_NAME].buf, "repo2"));
    te_set(&g_sheet.cn[CN_NAME], "Server nightly", 14, 14);
    snprintf(g_sheet.cn_icon, sizeof g_sheet.cn_icon, "nas");
    g_sheet.cn_remember = true;
    st_shot("borg-edit");
    borg_edit_save();
    ST_CHECK(g_sheet.kind != SHEET_BORG && !strcmp(A.borgs[0].name, "Server nightly") && !strcmp(A.borgs[0].icon, "nas") && A.borgs[0].keep);
    borg_saved_update(1, "Laptop", "", true); // Borg 1: never kept mounted (it would be locked)
    ST_CHECK(!strcmp(A.borgs[1].name, "Laptop") && !A.borgs[1].keep);
    settings_save();
    {
        char sp[PATH_MAX], sbuf[1 << 16];
        snprintf(sp, sizeof sp, "%s/file-explorer/settings.conf", getenv("XDG_CONFIG_HOME"));
        st_read_text(sp, sbuf, sizeof sbuf);
        ST_CHECK(strstr(sbuf, "\nborg=Server nightly\t" BORG_BASE "/home/repo2\tnas\t2\t1\n") && strstr(sbuf, "\nborg=Laptop\t" BORG_BASE "/home/repo1\t\t1\t0\n"));
    }
    // kept mounted: mounted in the background (no tab moves), never unmounted for being unused
    for (int k = 0; k < 1000 && !g_borg_n; k++) { st_frames(1); SDL_Delay(2); }
    st_borg_settle();
    borg_mount_point(BORG_BASE "/home/repo2", mp, sizeof mp);
    ST_CHECK(g_borg_n == 1 && borg_is_mounted(mp) && !strcmp(p->path, BORG_BASE "/home"));
    g_borg_idle_ns = 0;
    st_frames(3);
    ST_CHECK(g_borg_n == 1 && borg_is_mounted(mp));
    g_borg_idle_ns = idle;
    for (int i = 0; i < A.nplaces; i++) if (A.places[i].kind == PK_BORG && A.places[i].item == 0) brow = i;
    ST_CHECK(!strcmp(A.places[brow].path, mp) && A.places[brow].removable && !strcmp(A.places[brow].label, "Server nightly"));
    st_shot("borg-sidebar");
    // a click on the saved Borg 1 one: opened in the pane
    for (int i = 0; i < A.nplaces; i++) if (A.places[i].kind == PK_BORG && A.places[i].item == 1) brow = i;
    st_click_rect(place_rect(brow));
    st_borg_settle();
    char mp1[PATH_MAX];
    borg_mount_point(BORG_BASE "/home/repo1", mp1, sizeof mp1);
    ST_CHECK(!strcmp(p->path, mp1) && g_borg_n == 2 && p->ly.has_snapbar);
    { // tabs (and other path icons) show the backup's icon at the mount and its archives, a folder deeper in
        char sub[PATH_MAX + 16];
        snprintf(sub, sizeof sub, "%s/archive", mp);
        ST_CHECK(!strcmp(borg_path_icon(mp), "nas") && !strcmp(borg_path_icon(mp1), BORG_ICON_DEFAULT) && !strcmp(borg_path_icon(sub), "nas"));
        snprintf(sub, sizeof sub, "%s/archive/home", mp);
        ST_CHECK(!borg_path_icon(sub) && !borg_path_icon(BORG_BASE "/home"));
    }
    // closed by hand: the kept one stays closed this run; removed from the sidebar
    borg_close_mount(mp);
    st_borg_settle();
    for (int k = 0; k < 500 && borg_is_mounted(mp); k++) { st_frames(1); SDL_Delay(2); }
    st_frames(3);
    ST_CHECK(!borg_is_mounted(mp) && g_borg_n == 1);
    borg_forget(0);
    ST_CHECK(A.nborgs == 1 && !strcmp(A.borgs[0].name, "Laptop"));
    borg_forget(0);
    borg_close(p);
    st_borg_settle();

    // --- in an SFTP folder: borg on the server first (ssh://, the shared connection), no borg there: through the folder
    st_write_text(BORG_BASE "/tools/log", "");
    pane_navigate(p, BORG_BASE "/gvfs/sftp:host=vm,user=me,port=2222/srv", true);
    st_borg_settle();
    g = st_borg_sub(p, "remote1");
    ST_CHECK(g && g->borg == BORG_V1);
    st_borg_open(p, "remote1");
    ST_CHECK(st_borg_log("mount -o ignore_permissions ssh://me@vm:2222/srv/remote1 ") && st_borg_log("-o BatchMode=yes") && st_borg_log("ControlPath="));
    borg_mount_point(BORG_BASE "/gvfs/sftp:host=vm,user=me,port=2222/srv/remote1", mp, sizeof mp);
    ST_CHECK(!strcmp(p->path, mp) && borg_is_mounted(mp) && strstr(A.toast, "Couldn't use borg on the server"));
    borg_close(p);
    st_borg_settle();

    unsetenv("FE_GVFS_DIR");
    fprintf(stderr, "selftest-borg: %s (%d failures)\n", st_fail ? "FAIL" : "ok", st_fail);
    return st_fail ? 1 : 0;
}

// --- self-test for phones and cameras: plists, usbmux + lockdown + pairing + AFC (made-up iPhone), MTP (made-up phone), the FUSE folders ---

#define DEV_BASE "/tmp/fe-dev-test"

// A byte queue a made-up device answers into (the host's bulk IN endpoint).
typedef struct { pthread_mutex_t m; pthread_cond_t c; u8 *b; size_t n, cap; } StQueue;

static void stq_push(StQueue *q, const void *d, size_t n) {
    pthread_mutex_lock(&q->m);
    if (q->n + n > q->cap) { q->cap = (q->n + n) * 2; q->b = xrealloc(q->b, q->cap); }
    memcpy(q->b + q->n, d, n);
    q->n += n;
    pthread_cond_broadcast(&q->c);
    pthread_mutex_unlock(&q->m);
}

static int stq_pop(StQueue *q, u8 *buf, int cap, int timeout_ms) {
    struct timespec dl = deadline_in(timeout_ms);
    pthread_mutex_lock(&q->m);
    while (!q->n) if (pthread_cond_timedwait(&q->c, &q->m, &dl) == ETIMEDOUT) break;
    size_t k = MIN(q->n, (size_t)cap);
    memcpy(buf, q->b, k);
    memmove(q->b, q->b + k, q->n - k);
    q->n -= k;
    pthread_mutex_unlock(&q->m);
    return (int)k;
}

// --- a made-up iPhone: usbmux, TCP, lockdown, installation_proxy, house_arrest and AFC over a folder -----------

typedef struct {
    bool used;
    u16 hport, dport;  // host's port, device's port
    u32 seq, ack;      // the device's sequence, what it acknowledges
    u8 *buf;
    size_t len;
    int kind;          // 0 lockdown, 1 AFC (media), 2 house_arrest (then AFC in the app), 3 installation_proxy
    bool vended;       // house_arrest: now AFC
} StIosConn;

typedef struct {
    StQueue q;
    int version;
    u16 tx;
    StIosConn conns[24];
    char root[PATH_MAX], approot[PATH_MAX];
    EVP_PKEY *key;
    char *pub_pem;
    char host_id[64];
    int pair_calls, sessions;
    int files[64];
    bool cert_ok;
} StIos;

static void st_ios_packet(StIos *d, StIosConn *c, u8 flags, const void *payload, size_t len) {
    size_t total = 16 + 20 + len;
    u8 *b = xcalloc(1, total);
    put_be32(b, MUX_TCP);
    put_be32(b + 4, (u32)total);
    put_be32(b + 8, 0xfeedface);
    put_be16(b + 12, d->tx++);
    put_be16(b + 14, 0);
    u8 *th = b + 16;
    put_be16(th, c->dport);
    put_be16(th + 2, c->hport);
    put_be32(th + 4, c->seq);
    put_be32(th + 8, c->ack);
    th[12] = 5 << 4;
    th[13] = flags;
    put_be16(th + 14, 0xffff);
    if (len) memcpy(b + 36, payload, len);
    c->seq += (u32)len;
    stq_push(&d->q, b, total);
    free(b);
}

static void st_ios_plist(StIos *d, StIosConn *c, Plist *p) {
    size_t n;
    char *x = pl_to_xml(p, &n);
    u8 *b = xmalloc(n + 4);
    put_be32(b, (u32)n);
    memcpy(b + 4, x, n);
    st_ios_packet(d, c, TH_ACK, b, n + 4);
    free(b);
    free(x);
    pl_free(p);
}

static void st_ios_afc_reply(StIos *d, StIosConn *c, u64 num, u64 op, const void *data, size_t n) {
    u8 *b = xmalloc(40 + n);
    memcpy(b, "CFA6LPAA", 8);
    put_le64(b + 8, 40 + n);
    put_le64(b + 16, 40 + n);
    put_le64(b + 24, num);
    put_le64(b + 32, op);
    if (n) memcpy(b + 40, data, n);
    st_ios_packet(d, c, TH_ACK, b, 40 + n);
    free(b);
}

static void st_ios_afc_status(StIos *d, StIosConn *c, u64 num, u64 code) { u8 v[8]; put_le64(v, code); st_ios_afc_reply(d, c, num, AFC_STATUS, v, 8); }

static u64 st_errno_afc(int e) { return e == ENOENT ? 8 : e == EEXIST ? 16 : e == ENOTEMPTY ? 33 : e == EISDIR ? 9 : 1; }

// One AFC request on a folder of this computer (the app's container for house_arrest).
static void st_ios_afc(StIos *d, StIosConn *c, const u8 *pkt, size_t n) {
    u64 this_len = get_le64(pkt + 16), num = get_le64(pkt + 24), op = get_le64(pkt + 32);
    const u8 *hd = pkt + 40, *pay = pkt + this_len;
    size_t pl = n - this_len;
    const char *root = c->kind == 2 ? d->approot : d->root;
    char p[PATH_MAX], p2[PATH_MAX];
    switch (op) {
    case AFC_READ_DIR: {
        snprintf(p, sizeof p, "%s%s", root, (const char *)hd);
        DIR *dir = opendir(p);
        if (!dir) { st_ios_afc_status(d, c, num, st_errno_afc(errno)); return; }
        PlBuf b = {0};
        struct dirent *de;
        while ((de = readdir(dir))) pb_add(&b, de->d_name, strlen(de->d_name) + 1);
        closedir(dir);
        st_ios_afc_reply(d, c, num, AFC_DATA, b.b, b.n);
        free(b.b);
        return;
    }
    case AFC_FILE_INFO: {
        snprintf(p, sizeof p, "%s%s", root, (const char *)hd);
        struct stat st;
        if (lstat(p, &st)) { st_ios_afc_status(d, c, num, st_errno_afc(errno)); return; }
        char info[512];
        int k = snprintf(info, sizeof info, "st_size%c%lld%cst_blocks%c%lld%cst_nlink%c1%cst_ifmt%c%s%cst_mtime%c%llu%c", 0, (long long)st.st_size, 0, 0,
                         (long long)st.st_blocks, 0, 0, 0, 0, S_ISDIR(st.st_mode) ? "S_IFDIR" : "S_IFREG", 0, 0,
                         (unsigned long long)st.st_mtim.tv_sec * 1000000000ull + (unsigned long long)st.st_mtim.tv_nsec, 0);
        st_ios_afc_reply(d, c, num, AFC_DATA, info, (size_t)k);
        return;
    }
    case AFC_DEV_INFO: {
        char info[256];
        int k = snprintf(info, sizeof info, "Model%ciPhone17,3%cFSTotalBytes%c128000000000%cFSFreeBytes%c64000000000%cFSBlockSize%c4096%c", 0, 0, 0, 0, 0, 0, 0, 0);
        st_ios_afc_reply(d, c, num, AFC_DATA, info, (size_t)k);
        return;
    }
    case AFC_OPEN: {
        u64 mode = get_le64(hd);
        snprintf(p, sizeof p, "%s%s", root, (const char *)hd + 8);
        int fl = mode == AFC_RDONLY ? O_RDONLY : mode == AFC_RW ? O_RDWR : mode == AFC_WRONLY_TRUNC ? O_WRONLY | O_CREAT | O_TRUNC
               : mode == AFC_RW_TRUNC ? O_RDWR | O_CREAT | O_TRUNC : O_WRONLY | O_CREAT | O_APPEND;
        int fd = open(p, fl | O_CLOEXEC, 0644);
        if (fd < 0) { st_ios_afc_status(d, c, num, st_errno_afc(errno)); return; }
        int h = 1;
        while (h < 64 && d->files[h] > 0) h++;
        d->files[h] = fd;
        u8 v[8];
        put_le64(v, (u64)h);
        st_ios_afc_reply(d, c, num, AFC_OPEN_RES, v, 8);
        return;
    }
    case AFC_READ: {
        int fd = d->files[get_le64(hd) & 63];
        u64 want = get_le64(hd + 8);
        u8 *b = xmalloc(want + 1);
        ssize_t r = read(fd, b, want);
        st_ios_afc_reply(d, c, num, AFC_DATA, b, r > 0 ? (size_t)r : 0);
        free(b);
        return;
    }
    case AFC_WRITE: {
        int fd = d->files[get_le64(hd) & 63];
        st_ios_afc_status(d, c, num, write(fd, pay, pl) == (ssize_t)pl ? 0 : 5);
        return;
    }
    case AFC_SEEK: lseek(d->files[get_le64(hd) & 63], (off_t)get_le64(hd + 16), SEEK_SET); st_ios_afc_status(d, c, num, 0); return;
    case AFC_SET_SIZE: st_ios_afc_status(d, c, num, ftruncate(d->files[get_le64(hd) & 63], (off_t)get_le64(hd + 8)) ? 1 : 0); return;
    case AFC_CLOSE: { u64 h = get_le64(hd) & 63; if (d->files[h] > 0) close(d->files[h]); d->files[h] = 0; st_ios_afc_status(d, c, num, 0); return; }
    case AFC_MKDIR: snprintf(p, sizeof p, "%s%s", root, (const char *)hd); st_ios_afc_status(d, c, num, mkdir(p, 0755) ? st_errno_afc(errno) : 0); return;
    case AFC_REMOVE: {
        snprintf(p, sizeof p, "%s%s", root, (const char *)hd);
        struct stat st;
        int r = lstat(p, &st) ? -1 : S_ISDIR(st.st_mode) ? rmdir(p) : unlink(p);
        st_ios_afc_status(d, c, num, r ? st_errno_afc(errno) : 0);
        return;
    }
    case AFC_RENAME:
        snprintf(p, sizeof p, "%s%s", root, (const char *)hd);
        snprintf(p2, sizeof p2, "%s%s", root, (const char *)hd + strlen((const char *)hd) + 1);
        st_ios_afc_status(d, c, num, rename(p, p2) ? st_errno_afc(errno) : 0);
        return;
    case AFC_SET_MTIME: {
        snprintf(p, sizeof p, "%s%s", root, (const char *)hd + 8);
        u64 ns = get_le64(hd);
        struct timespec ts[2] = {{(time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull)}, {(time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull)}};
        st_ios_afc_status(d, c, num, utimensat(AT_FDCWD, p, ts, 0) ? st_errno_afc(errno) : 0);
        return;
    }
    case AFC_TRUNCATE: snprintf(p, sizeof p, "%s%s", root, (const char *)hd + 8); st_ios_afc_status(d, c, num, truncate(p, (off_t)get_le64(hd)) ? 1 : 0); return;
    default: st_ios_afc_status(d, c, num, 15);
    }
}

static void st_ios_lockdown(StIos *d, StIosConn *c, Plist *q) {
    const char *req = pl_get_str(q, "Request"), *key = pl_get_str(q, "Key");
    Plist *r = pl_dict();
    if (req) pl_set(r, "Request", pl_str(req));
    if (!req) pl_set(r, "Error", pl_str("MissingRequest"));
    else if (!strcmp(req, "QueryType")) pl_set(r, "Type", pl_str("com.apple.mobile.lockdown"));
    else if (!strcmp(req, "GetValue") && key) {
        if (!strcmp(key, "UniqueDeviceID")) pl_set(r, "Value", pl_str("FAKE-UDID-0001"));
        else if (!strcmp(key, "DevicePublicKey")) pl_set(r, "Value", pl_data(d->pub_pem, strlen(d->pub_pem)));
        else if (!strcmp(key, "DeviceName") && d->sessions) pl_set(r, "Value", pl_str("Test iPhone"));
        else if (!strcmp(key, "ProductType")) pl_set(r, "Value", pl_str("iPhone17,3"));
        else if (!strcmp(key, "ProductVersion")) pl_set(r, "Value", pl_str("18.1"));
        else pl_set(r, "Error", pl_str("MissingValue"));
    } else if (!strcmp(req, "Pair")) {
        Plist *rec = pl_get(q, "PairRecord");
        Plist *dc = pl_get(rec, "DeviceCertificate"), *rc = pl_get(rec, "RootCertificate"), *hc = pl_get(rec, "HostCertificate");
        if (++d->pair_calls == 1) pl_set(r, "Error", pl_str("PairingDialogResponsePending")); // the user hasn't tapped Trust yet
        else if (!dc || !rc || !hc || !pl_get_str(rec, "HostID") || !pl_get_str(rec, "SystemBUID")) pl_set(r, "Error", pl_str("InvalidPairRecord"));
        else { // our certificates: the device's carries its key, root signed it and the host's
            char *dp = xmalloc(dc->len + 1), *rp = xmalloc(rc->len + 1), *hp = xmalloc(hc->len + 1);
            memcpy(dp, dc->data, dc->len); dp[dc->len] = 0;
            memcpy(rp, rc->data, rc->len); rp[rc->len] = 0;
            memcpy(hp, hc->data, hc->len); hp[hc->len] = 0;
            X509 *dx = cert_from_pem(dp), *rx = cert_from_pem(rp), *hx = cert_from_pem(hp);
            EVP_PKEY *rk = rx ? X509_get0_pubkey(rx) : NULL, *dk = dx ? X509_get0_pubkey(dx) : NULL;
            d->cert_ok = rk && dk && X509_verify(dx, rk) == 1 && X509_verify(hx, rk) == 1 && EVP_PKEY_eq(dk, d->key) == 1 && X509_check_ca(rx) >= 1;
            X509_free(dx); X509_free(rx); X509_free(hx);
            free(dp); free(rp); free(hp);
            snprintf(d->host_id, sizeof d->host_id, "%s", pl_get_str(rec, "HostID"));
            pl_set(r, "EscrowBag", pl_data("escrow", 6));
        }
    } else if (!strcmp(req, "StartSession")) {
        const char *hid = pl_get_str(q, "HostID");
        if (!hid || strcmp(hid, d->host_id)) pl_set(r, "Error", pl_str("InvalidHostID"));
        else { d->sessions++; pl_set(r, "SessionID", pl_str("S1")); pl_set(r, "EnableSessionSSL", pl_bool(false)); }
    } else if (!strcmp(req, "StartService")) {
        const char *svc = pl_get_str(q, "Service");
        int port = !svc ? 0 : !strcmp(svc, "com.apple.afc") ? 1000 : !strcmp(svc, "com.apple.mobile.house_arrest") ? 1001
                 : !strcmp(svc, "com.apple.mobile.installation_proxy") ? 1002 : 0;
        if (!d->sessions) pl_set(r, "Error", pl_str("NoRunningSession"));
        else if (!port) pl_set(r, "Error", pl_str("InvalidService"));
        else { pl_set(r, "Port", pl_int(port)); pl_set(r, "EnableServiceSSL", pl_bool(false)); }
    } else pl_set(r, "Error", pl_str("UnknownRequest"));
    st_ios_plist(d, c, r);
}

// A whole message on a connection: by service.
static size_t st_ios_message(StIos *d, StIosConn *c) {
    if (c->kind == 1 || (c->kind == 2 && c->vended)) {
        if (c->len < 40) return 0;
        u64 total = get_le64(c->buf + 8);
        if (c->len < total) return 0;
        st_ios_afc(d, c, c->buf, (size_t)total);
        return (size_t)total;
    }
    if (c->len < 4) return 0;
    u32 n = get_be32(c->buf);
    if (c->len < 4 + n) return 0;
    Plist *q = pl_parse(c->buf + 4, n);
    if (c->kind == 0) st_ios_lockdown(d, c, q);
    else if (c->kind == 2) { // VendDocuments
        Plist *r = pl_dict();
        const char *id = pl_get_str(q, "Identifier");
        if (id && !strcmp(id, "org.videolan.vlc")) { pl_set(r, "Status", pl_str("Complete")); c->vended = true; }
        else pl_set(r, "Error", pl_str("InstallationLookupFailed"));
        st_ios_plist(d, c, r);
    } else if (c->kind == 3) { // Browse: two apps, one shares files
        Plist *r = pl_dict(), *list = pl_array(), *a1 = pl_dict(), *a2 = pl_dict();
        pl_set(a1, "CFBundleIdentifier", pl_str("org.videolan.vlc"));
        pl_set(a1, "CFBundleDisplayName", pl_str("VLC"));
        pl_set(a1, "UIFileSharingEnabled", pl_bool(true));
        pl_set(a2, "CFBundleIdentifier", pl_str("com.example.notes"));
        pl_set(a2, "CFBundleDisplayName", pl_str("Notes"));
        pl_push(list, a1);
        pl_push(list, a2);
        pl_set(r, "Status", pl_str("BrowsingApplications"));
        pl_set(r, "CurrentList", list);
        st_ios_plist(d, c, r);
        Plist *done = pl_dict();
        pl_set(done, "Status", pl_str("Complete"));
        st_ios_plist(d, c, done);
    }
    pl_free(q);
    return 4 + n;
}

static int st_ios_out(Usb *u, const u8 *p, int len) {
    StIos *d = u->fake;
    if (len < 8) return len;
    u32 proto = get_be32(p);
    if (!d->version) { // the version request (8-byte header): we speak 2
        if (proto == MUX_VERSION) {
            u8 r[20] = {0};
            put_be32(r, MUX_VERSION);
            put_be32(r + 4, 20);
            put_be32(r + 8, 2);
            stq_push(&d->q, r, sizeof r);
            d->version = 2;
        }
        return len;
    }
    if (proto != MUX_TCP || len < 36) return len; // setup
    const u8 *th = p + 16, *pay = p + 36;
    size_t pl = (size_t)len - 36;
    u16 sport = get_be16(th), dport = get_be16(th + 2);
    u8 flags = th[13];
    StIosConn *c = NULL;
    for (int i = 0; i < 24 && !c; i++) if (d->conns[i].used && d->conns[i].hport == sport) c = &d->conns[i];
    if (flags & TH_SYN) {
        for (int i = 0; i < 24 && !c; i++) if (!d->conns[i].used) c = &d->conns[i];
        if (!c) return len;
        memset(c, 0, sizeof *c);
        c->used = true;
        c->hport = sport;
        c->dport = dport;
        c->seq = 100;
        c->ack = get_be32(th + 4) + 1;
        c->kind = dport == 62078 ? 0 : dport == 1000 ? 1 : dport == 1001 ? 2 : dport == 1002 ? 3 : -1;
        if (c->kind < 0) { st_ios_packet(d, c, TH_RST, NULL, 0); c->used = false; return len; }
        st_ios_packet(d, c, TH_SYN | TH_ACK, NULL, 0);
        c->seq++;
        return len;
    }
    if (!c) return len;
    if (flags & TH_RST) { free(c->buf); c->used = false; return len; }
    if (!pl) return len;
    c->buf = xrealloc(c->buf, c->len + pl);
    memcpy(c->buf + c->len, pay, pl);
    c->len += pl;
    c->ack += (u32)pl;
    for (size_t used; (used = st_ios_message(d, c)) > 0;) { memmove(c->buf, c->buf + used, c->len - used); c->len -= used; }
    return len;
}

static int st_ios_in(Usb *u, u8 *buf, int cap, int timeout_ms) { return stq_pop(&((StIos *)u->fake)->q, buf, cap, MIN(timeout_ms, 200)); }

// --- a made-up MTP phone: objects in memory --------------------------------------------------------------------

typedef struct { bool used; u32 handle, parent, storage; u16 format; char name[256]; u8 *data; size_t len; } StObj;

typedef struct {
    StQueue q;
    StObj objs[128];
    u32 next;
    u8 *pend;           // a command waiting for its data phase
    size_t pend_len;
    u16 pend_code;
    u32 pend_tid, pend_params[5];
    u8 *data;           // the data phase being received
    size_t data_len, data_want;
    u32 last_created;   // SendObjectInfo -> SendObject
    int proplists, infos;
    bool short_mid;     // a write inside a data phase wasn't whole packets (a device would take it as the end)
} StMtp;

static void st_mtp_container(StMtp *d, u16 type, u16 code, u32 tid, const u32 *params, int np, const void *payload, size_t pl) {
    size_t total = 12 + 4 * (size_t)np + pl;
    u8 *b = xmalloc(total);
    put_le32(b, (u32)total);
    put_le16(b + 4, type);
    put_le16(b + 6, code);
    put_le32(b + 8, tid);
    for (int i = 0; i < np; i++) put_le32(b + 12 + 4 * i, params[i]);
    if (pl) memcpy(b + 12 + 4 * np, payload, pl);
    stq_push(&d->q, b, total);
    free(b);
}

static void st_mtp_resp(StMtp *d, u16 code, u32 tid, const u32 *params, int np) { st_mtp_container(d, PTP_RESP, code, tid, params, np, NULL, 0); }
static void st_mtp_data(StMtp *d, u16 code, u32 tid, const PtpBuf *b) { st_mtp_container(d, PTP_DATA, code, tid, NULL, 0, b->b, b->n); }

static StObj *st_mtp_obj(StMtp *d, u32 h) { for (int i = 0; i < 128; i++) if (d->objs[i].used && d->objs[i].handle == h) return &d->objs[i]; return NULL; }

static StObj *st_mtp_add(StMtp *d, u32 parent, const char *name, bool folder, const char *text) {
    for (int i = 0; i < 128; i++)
        if (!d->objs[i].used) {
            StObj *o = &d->objs[i];
            memset(o, 0, sizeof *o);
            o->used = true;
            o->handle = d->next++;
            o->parent = parent;
            o->storage = 0x00010001;
            o->format = folder ? PTP_FMT_FOLDER : 0x3000;
            snprintf(o->name, sizeof o->name, "%s", name);
            if (text) { o->len = strlen(text); o->data = (u8 *)strdup(text); }
            return o;
        }
    return NULL;
}

static void st_mtp_del(StMtp *d, u32 h) {
    for (int i = 0; i < 128; i++) if (d->objs[i].used && d->objs[i].parent == h) st_mtp_del(d, d->objs[i].handle);
    StObj *o = st_mtp_obj(d, h);
    if (o) { free(o->data); o->data = NULL; o->used = false; }
}

static void st_mtp_command(StMtp *d, u16 code, u32 tid, const u32 *prm, int np, const u8 *data, size_t dl) {
    PtpBuf b = {0};
    switch (code) {
    case PTP_GET_DEVICE_INFO: {
        static const u16 ops[] = {0x1001, 0x1002, 0x1003, 0x1004, 0x1005, 0x1007, 0x1008, 0x1009, 0x100B, 0x100C, 0x100D, 0x1019, 0x95C1, 0x9804, 0x9805};
        pbuf_u16(&b, 100); pbuf_u32(&b, 6); pbuf_u16(&b, 100); pbuf_str(&b, "microsoft.com: 1.0; android.com: 1.0;"); pbuf_u16(&b, 0);
        pbuf_u32(&b, ARRAY_COUNT(ops));
        for (size_t i = 0; i < ARRAY_COUNT(ops); i++) pbuf_u16(&b, ops[i]);
        for (int k = 0; k < 4; k++) pbuf_u32(&b, 0);
        pbuf_str(&b, "Google"); pbuf_str(&b, "Pixel Test"); pbuf_str(&b, "1.0"); pbuf_str(&b, "PIXSERIAL123");
        st_mtp_data(d, code, tid, &b);
        st_mtp_resp(d, PTP_OK, tid, NULL, 0);
        break;
    }
    case PTP_OPEN_SESSION: case PTP_CLOSE_SESSION: st_mtp_resp(d, PTP_OK, tid, NULL, 0); break;
    case PTP_GET_STORAGE_IDS: pbuf_u32(&b, 1); pbuf_u32(&b, 0x00010001); st_mtp_data(d, code, tid, &b); st_mtp_resp(d, PTP_OK, tid, NULL, 0); break;
    case PTP_GET_STORAGE_INFO:
        pbuf_u16(&b, 3); pbuf_u16(&b, 2); pbuf_u16(&b, 0);
        pbuf_u32(&b, 0); pbuf_u32(&b, 8); pbuf_u32(&b, 0); pbuf_u32(&b, 4); pbuf_u32(&b, 0); // 32 GB, 16 GB free
        pbuf_str(&b, "Internal shared storage"); pbuf_str(&b, "");
        st_mtp_data(d, code, tid, &b);
        st_mtp_resp(d, PTP_OK, tid, NULL, 0);
        break;
    case PTP_GET_OBJECT_HANDLES: {
        u32 parent = np >= 3 ? prm[2] : 0, n = 0;
        if (parent == PTP_ROOT) parent = 0;
        for (int i = 0; i < 128; i++) if (d->objs[i].used && d->objs[i].parent == parent) n++;
        pbuf_u32(&b, n);
        for (int i = 0; i < 128; i++) if (d->objs[i].used && d->objs[i].parent == parent) pbuf_u32(&b, d->objs[i].handle);
        st_mtp_data(d, code, tid, &b);
        st_mtp_resp(d, PTP_OK, tid, NULL, 0);
        break;
    }
    case PTP_GET_OBJECT_INFO: {
        StObj *o = st_mtp_obj(d, prm[0]);
        if (!o) { st_mtp_resp(d, PTP_INVALID_HANDLE, tid, NULL, 0); break; }
        d->infos++;
        pbuf_u32(&b, o->storage); pbuf_u16(&b, o->format); pbuf_u16(&b, 0); pbuf_u32(&b, (u32)o->len);
        pbuf_u16(&b, 0); for (int k = 0; k < 6; k++) pbuf_u32(&b, 0);
        pbuf_u32(&b, o->parent); pbuf_u16(&b, 0); pbuf_u32(&b, 0); pbuf_u32(&b, 0);
        pbuf_str(&b, o->name); pbuf_str(&b, "20240102T030405"); pbuf_str(&b, "20240102T030405"); pbuf_str(&b, "");
        st_mtp_data(d, code, tid, &b);
        st_mtp_resp(d, PTP_OK, tid, NULL, 0);
        break;
    }
    case MTP_GET_OBJECT_PROP_LIST: { // depth 1: the folder's children, six properties each
        d->proplists++;
        u32 parent = prm[0], n = 0;
        for (int i = 0; i < 128; i++) if (d->objs[i].used && d->objs[i].parent == parent) n++;
        pbuf_u32(&b, n * 6);
        for (int i = 0; i < 128; i++) {
            StObj *o = &d->objs[i];
            if (!o->used || o->parent != parent) continue;
            pbuf_u32(&b, o->handle); pbuf_u16(&b, MTP_PROP_STORAGE); pbuf_u16(&b, 6); pbuf_u32(&b, o->storage);
            pbuf_u32(&b, o->handle); pbuf_u16(&b, MTP_PROP_FORMAT); pbuf_u16(&b, 4); pbuf_u16(&b, o->format);
            pbuf_u32(&b, o->handle); pbuf_u16(&b, MTP_PROP_SIZE); pbuf_u16(&b, 8); pbuf_u32(&b, (u32)o->len); pbuf_u32(&b, 0);
            pbuf_u32(&b, o->handle); pbuf_u16(&b, MTP_PROP_NAME); pbuf_u16(&b, 0xFFFF); pbuf_str(&b, o->name);
            pbuf_u32(&b, o->handle); pbuf_u16(&b, MTP_PROP_MODIFIED); pbuf_u16(&b, 0xFFFF); pbuf_str(&b, "20240102T030405");
            pbuf_u32(&b, o->handle); pbuf_u16(&b, MTP_PROP_PARENT); pbuf_u16(&b, 6); pbuf_u32(&b, o->parent);
        }
        st_mtp_data(d, code, tid, &b);
        st_mtp_resp(d, PTP_OK, tid, NULL, 0);
        break;
    }
    case MTP_GET_PARTIAL_OBJECT_64: case PTP_GET_OBJECT: {
        StObj *o = st_mtp_obj(d, prm[0]);
        if (!o) { st_mtp_resp(d, PTP_INVALID_HANDLE, tid, NULL, 0); break; }
        u64 off = code == PTP_GET_OBJECT ? 0 : prm[1] | (u64)prm[2] << 32, max = code == PTP_GET_OBJECT ? o->len : prm[3];
        size_t k = off < o->len ? (size_t)MIN((u64)(o->len - off), max) : 0;
        pbuf_need(&b, k + 1);
        if (k) memcpy(b.b, o->data + off, k);
        b.n = k;
        st_mtp_data(d, code, tid, &b);
        u32 got = (u32)k;
        st_mtp_resp(d, PTP_OK, tid, &got, 1);
        break;
    }
    case PTP_SEND_OBJECT_INFO: {
        PtpObj info;
        ptp_parse_info(data, dl, 0, &info);
        StObj *o = st_mtp_add(d, prm[1] == PTP_ROOT ? 0 : prm[1], info.name, info.format == PTP_FMT_FOLDER, NULL);
        d->last_created = o ? o->handle : 0;
        u32 rp[3] = {0x00010001, prm[1], d->last_created};
        st_mtp_resp(d, o ? PTP_OK : PTP_STORE_FULL, tid, rp, 3);
        break;
    }
    case PTP_SEND_OBJECT: {
        StObj *o = st_mtp_obj(d, d->last_created);
        if (o) { free(o->data); o->data = xmalloc(dl + 1); memcpy(o->data, data, dl); o->len = dl; }
        st_mtp_resp(d, o ? PTP_OK : PTP_GENERAL_ERROR, tid, NULL, 0);
        break;
    }
    case PTP_DELETE_OBJECT: {
        bool ok = st_mtp_obj(d, prm[0]) != NULL;
        st_mtp_del(d, prm[0]);
        st_mtp_resp(d, ok ? PTP_OK : PTP_INVALID_HANDLE, tid, NULL, 0);
        break;
    }
    case MTP_SET_OBJECT_PROP_VALUE: {
        StObj *o = st_mtp_obj(d, prm[0]);
        PtpCur c = {data, dl, 0, false};
        if (o && prm[1] == MTP_PROP_NAME) pc_str(&c, o->name, sizeof o->name);
        st_mtp_resp(d, o ? PTP_OK : PTP_INVALID_HANDLE, tid, NULL, 0);
        break;
    }
    case PTP_MOVE_OBJECT: {
        StObj *o = st_mtp_obj(d, prm[0]);
        if (o) o->parent = prm[2] == PTP_ROOT ? 0 : prm[2];
        st_mtp_resp(d, o ? PTP_OK : PTP_INVALID_HANDLE, tid, NULL, 0);
        break;
    }
    default: st_mtp_resp(d, PTP_NOT_SUPPORTED, tid, NULL, 0);
    }
    free(b.b);
}

static int st_mtp_out(Usb *u, const u8 *p, int len) {
    StMtp *d = u->fake;
    if (d->data_want) { // more of a data phase
        size_t k = MIN((size_t)len, d->data_want - d->data_len);
        if (d->data_len + k < d->data_want && len % 512) d->short_mid = true;
        memcpy(d->data + d->data_len, p, k);
        d->data_len += k;
        if (d->data_len == d->data_want) {
            st_mtp_command(d, d->pend_code, d->pend_tid, d->pend_params, 5, d->data + 12, d->data_len - 12);
            free(d->data);
            d->data = NULL;
            d->data_want = d->data_len = 0;
        }
        return len;
    }
    if (len < 12) return len;
    u32 cl = get_le32(p);
    u16 type = get_le16(p + 4), code = get_le16(p + 6);
    u32 tid = get_le32(p + 8), prm[5] = {0};
    int np = (int)MIN((cl - 12) / 4, 5u);
    if (type == PTP_CMD) {
        for (int i = 0; i < np; i++) prm[i] = get_le32(p + 12 + 4 * i);
        if (code == PTP_SEND_OBJECT_INFO || code == PTP_SEND_OBJECT || code == MTP_SET_OBJECT_PROP_VALUE) { // data follows
            d->pend_code = code;
            d->pend_tid = tid;
            memcpy(d->pend_params, prm, sizeof prm);
            return len;
        }
        st_mtp_command(d, code, tid, prm, np, NULL, 0);
    } else if (type == PTP_DATA) {
        if ((size_t)len < cl && len % 512) d->short_mid = true;
        d->data_want = cl;
        d->data = xmalloc(cl);
        d->data_len = 0;
        return st_mtp_out(u, p, len);
    }
    return len;
}

static int st_mtp_in(Usb *u, u8 *buf, int cap, int timeout_ms) { return stq_pop(&((StMtp *)u->fake)->q, buf, cap, MIN(timeout_ms, 200)); }

// --- FUSE: serve g_dfs_ops at a folder from a thread of this process -------------------------------------------

typedef struct { struct fuse *f; pthread_t t; bool up; } StFuse;

static void *st_fuse_loop(void *arg) { fuse_loop_mt(((StFuse *)arg)->f, 0); return NULL; }

static bool st_fuse_start(StFuse *sf, const char *mp) {
    mkdir(mp, 0700);
    char *fav[] = {"fe-test", "-o", "fsname=fe-device:TEST,subtype=fe-device", NULL};
    struct fuse_args args = FUSE_ARGS_INIT(3, fav);
    sf->f = fuse_new(&args, &g_dfs_ops, sizeof g_dfs_ops, NULL);
    fuse_opt_free_args(&args);
    if (!sf->f) return false;
    if (fuse_mount(sf->f, mp)) { fuse_destroy(sf->f); sf->f = NULL; return false; }
    sf->up = pthread_create(&sf->t, NULL, st_fuse_loop, sf) == 0;
    return sf->up;
}

static void st_fuse_stop(StFuse *sf, const char *mp) {
    if (!sf->f) return;
    pid_t pid;
    char *av[] = {"fusermount3", "-u", "-z", (char *)mp, NULL};
    if (posix_spawnp(&pid, "fusermount3", NULL, NULL, av, environ) == 0) waitpid(pid, NULL, 0);
    if (sf->up) pthread_join(sf->t, NULL);
    fuse_unmount(sf->f);
    fuse_destroy(sf->f);
    sf->f = NULL;
}

static bool st_file_is(const char *path, const char *text) {
    char buf[4096];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    return n >= 0 && (buf[n] = 0, !strcmp(buf, text));
}

static int st_count_dir(const char *path, const char *must_have) {
    DIR *d = opendir(path);
    if (!d) return -1;
    int n = 0;
    bool has = !must_have;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        n++;
        if (must_have && !strcmp(de->d_name, must_have)) has = true;
    }
    closedir(d);
    return has ? n : -2;
}

static int run_selftest_devices(void) {
    signal(SIGPIPE, SIG_IGN);
    st_rmrf(DEV_BASE);
    mkdir(DEV_BASE, 0755);
    setenv("FE_LOCKDOWN_DIR", DEV_BASE "/lockdown", 1); // never the real pairing records
    setenv("XDG_CACHE_HOME", DEV_BASE "/cache", 1);

    { // plists: XML out and back, binary in
        Plist *d = pl_dict(), *a = pl_array();
        pl_set(d, "Name", pl_str("Al's <iPhone> & co"));
        pl_set(d, "Count", pl_int(-42));
        pl_set(d, "On", pl_bool(true));
        pl_set(d, "Blob", pl_data("\x00\x01\x02\xff", 4));
        pl_push(a, pl_str("x"));
        pl_push(a, pl_int(7));
        pl_set(d, "List", a);
        pl_set(d, "Count", pl_int(43)); // replaced
        size_t n;
        char *x = pl_to_xml(d, &n);
        Plist *b = pl_parse(x, n);
        ST_CHECK(b && !strcmp(pl_get_str(b, "Name"), "Al's <iPhone> & co") && pl_get_int(b, "Count", 0) == 43 && pl_get_bool(b, "On"));
        Plist *blob = pl_get(b, "Blob"), *list = pl_get(b, "List");
        ST_CHECK(blob && blob->len == 4 && !memcmp(blob->data, "\x00\x01\x02\xff", 4) && list && list->n == 2 && list->kids[1]->i == 7 && d->n == 5);
        free(x);
        pl_free(b);
        pl_free(d);
        // bplist00: {"Port": 49152, "Ok": true, "S": "hi"}
        static const u8 bp[] = {'b','p','l','i','s','t','0','0', 0xD3, 1, 2, 3, 4, 5, 6, 0x54,'P','o','r','t', 0x52,'O','k',
                                0x51,'S', 0x11, 0xC0, 0x00, 0x09, 0x52,'h','i',
                                8, 15, 20, 23, 25, 28, 29, // offsets
                                0,0,0,0,0,0, 1, 1, 0,0,0,0,0,0,0,7, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,32};
        Plist *pb = pl_parse(bp, sizeof bp);
        ST_CHECK(pb && pl_get_int(pb, "Port", 0) == 49152 && pl_get_bool(pb, "Ok") && pl_get_str(pb, "S") && !strcmp(pl_get_str(pb, "S"), "hi"));
        pl_free(pb);
        size_t bn;
        u8 *dec = b64_decode("aGVs\nbG8=", 9, &bn);
        ST_CHECK(bn == 5 && !memcmp(dec, "hello", 5));
        free(dec);
    }
    { // descriptors: the usbmux interface of an Apple configuration, the PTP one of an Android phone
        static const u8 desc[] = {18, 1, 0, 2, 0, 0, 0, 64, 0xac, 0x05, 0xa8, 0x12, 0, 0, 1, 2, 3, 2,
                                  9, 2, 39, 0, 2, 4, 0, 0xc0, 250,
                                  9, 4, 0, 0, 3, 6, 1, 1, 0, 7, 5, 0x81, 2, 0, 2, 0, 7, 5, 0x02, 2, 0, 2, 0, 7, 5, 0x83, 3, 8, 0, 9,
                                  9, 4, 1, 0, 2, 0xff, 0xfe, 2, 0, 7, 5, 0x84, 2, 0, 2, 0, 7, 5, 0x05, 2, 0, 2, 0};
        UsbTarget t = usb_find_target(desc, sizeof desc, 'a', 4);
        ST_CHECK(t.found && t.cfg == 4 && t.iface == 1 && t.ep_in == 0x84 && t.ep_out == 0x05 && t.mps == 512);
        t = usb_find_target(desc, sizeof desc, 'm', 4);
        ST_CHECK(t.found && t.iface == 0 && t.ep_in == 0x81 && t.ep_out == 0x02);
        // a made-up bus: an iPhone, an Android phone with an MTP interface, a camera, a keyboard
        const char *u = DEV_BASE "/usb";
        mkdir(u, 0755);
        struct { const char *dev, *v, *p, *serial, *product; } devs[] = {{"1-2", "05ac", "12a8", "00008140000123C6E0C13001C", "iPhone"},
                                                                         {"1-3", "18d1", "4ee1", "2A111FDH200ABC", "Pixel 7"},
                                                                         {"1-4", "04a9", "3218", "CAM1", "Canon EOS"},
                                                                         {"1-5", "1a2c", "1514", "", "USB Keyboard"}};
        for (size_t i = 0; i < ARRAY_COUNT(devs); i++) {
            char p[PATH_MAX];
            snprintf(p, sizeof p, "%s/%s", u, devs[i].dev);
            mkdir(p, 0755);
            const char *attrs[][2] = {{"idVendor", devs[i].v}, {"idProduct", devs[i].p}, {"serial", devs[i].serial}, {"product", devs[i].product},
                                      {"busnum", "1"}, {"devnum", "9"}, {"bConfigurationValue", "1"}};
            for (size_t k = 0; k < ARRAY_COUNT(attrs); k++) {
                snprintf(p, sizeof p, "%s/%s/%s", u, devs[i].dev, attrs[k][0]);
                st_write_text(p, attrs[k][1]);
            }
        }
        const char *ifs[][4] = {{"1-3:1.0", "06", "01", "MTP"}, {"1-4:1.0", "06", "01", ""}, {"1-5:1.0", "03", "01", ""}};
        for (size_t i = 0; i < ARRAY_COUNT(ifs); i++) {
            char p[PATH_MAX];
            snprintf(p, sizeof p, "%s/%s", u, ifs[i][0]);
            mkdir(p, 0755);
            snprintf(p, sizeof p, "%s/%s/bInterfaceClass", u, ifs[i][0]); st_write_text(p, ifs[i][1]);
            snprintf(p, sizeof p, "%s/%s/bInterfaceSubClass", u, ifs[i][0]); st_write_text(p, ifs[i][2]);
            snprintf(p, sizeof p, "%s/%s/interface", u, ifs[i][0]); st_write_text(p, ifs[i][3]);
        }
        setenv("FE_SYS_USB", u, 1);
        UsbDevInfo info[8];
        int n = usb_list_phones(info, 8);
        char kinds[8] = "";
        for (int i = 0; i < n; i++) {
            if (!strcmp(info[i].sys, "1-2")) kinds[0] = info[i].kind;
            if (!strcmp(info[i].sys, "1-3")) kinds[1] = info[i].kind;
            if (!strcmp(info[i].sys, "1-4")) kinds[2] = info[i].kind;
        }
        ST_CHECK(n == 3 && kinds[0] == 'a' && kinds[1] == 'm' && kinds[2] == 'c'); // the keyboard isn't one
        // ours list them, GVfs's entries for the same devices fold in (the iPhone's PTP and Documents, the phone)
        setenv("FE_GIO", DEV_BASE "/gio", 1);
        st_write_text(DEV_BASE "/gio", "#!/bin/sh\n[ \"$2\" = -li ] && printf 'Volume(0): Documents on Al\\n  activation_root=afc://00008140-000123C6E0C13001C:3/\\n"
                                       "Volume(1): iPhone\\n  activation_root=gphoto2://Apple_Inc._iPhone_00008140000123C6E0C13001C/\\n"
                                       "Volume(2): Pixel 7\\n  activation_root=mtp://Google_Pixel_7_2A111FDH200ABC/\\n"
                                       "Volume(3): Other\\n  activation_root=mtp://Samsung_SM_X_R58M/\\n'\nexit 0\n");
        chmod(DEV_BASE "/gio", 0755);
        setenv("FE_GVFS_DIR", DEV_BASE "/gvfs", 1);
        mkdir(DEV_BASE "/gvfs", 0755);
        DevList *L = xcalloc(1, sizeof *L);
        phones_scan(L);
        int own = 0, other = -1, pix = -1;
        for (int i = 0; i < L->nph; i++) {
            own += L->ph[i].own;
            if (!L->ph[i].own && strstr(L->ph[i].uri, "Samsung")) other = i;
            if (L->ph[i].own && !strcmp(L->ph[i].sys, "1-3")) pix = i;
        }
        ST_CHECK(L->nph == 4 && own == 3 && other >= 0); // the Samsung (no USB device here) stays GVfs's
        ST_CHECK(pix >= 0 && !strcmp(L->ph[pix].name, "Pixel 7") && !strcmp(L->ph[pix].gvfs_uri, "mtp://Google_Pixel_7_2A111FDH200ABC/"));
        setenv("FE_NO_OWN_PHONES", "1", 1);
        phones_scan(L);
        ST_CHECK(L->nph == 4 && !L->ph[0].own); // GVfs only: its four
        unsetenv("FE_NO_OWN_PHONES");
        free(L);
        unsetenv("FE_GIO");
        unsetenv("FE_GVFS_DIR");
        unsetenv("FE_SYS_USB");
    }

    // --- the made-up iPhone: version, pairing (Trust pending once), session, services, AFC ---
    StIos *ios = xcalloc(1, sizeof *ios);
    pthread_mutex_init(&ios->q.m, NULL);
    pthread_cond_init(&ios->q.c, NULL);
    snprintf(ios->root, sizeof ios->root, DEV_BASE "/iphone");
    snprintf(ios->approot, sizeof ios->approot, DEV_BASE "/vlc");
    mkdir(ios->root, 0755);
    mkdir(DEV_BASE "/iphone/DCIM", 0755);
    mkdir(DEV_BASE "/iphone/DCIM/100APPLE", 0755);
    for (int i = 0; i < 40; i++) { char p[PATH_MAX]; snprintf(p, sizeof p, DEV_BASE "/iphone/DCIM/100APPLE/IMG_%04d.HEIC", i); st_write(p, 100 + (size_t)i); }
    mkdir(ios->approot, 0755);
    mkdir(DEV_BASE "/vlc/Documents", 0755);
    st_write_text(DEV_BASE "/vlc/Documents/movie.txt", "a film");
    ios->key = EVP_RSA_gen(2048);
    { // the device's public key as lockdown gives it: PEM PKCS#1
        u8 *der = NULL;
        int dl = i2d_PublicKey(ios->key, &der);
        char *b = b64_encode(der, (size_t)dl);
        size_t bl = strlen(b);
        ios->pub_pem = xmalloc(bl + bl / 64 + 80);
        size_t o = (size_t)sprintf(ios->pub_pem, "-----BEGIN RSA PUBLIC KEY-----\n");
        for (size_t i = 0; i < bl; i += 64) o += (size_t)sprintf(ios->pub_pem + o, "%.64s\n", b + i);
        sprintf(ios->pub_pem + o, "-----END RSA PUBLIC KEY-----\n");
        OPENSSL_free(der);
        free(b);
    }
    memset(&g_ios, 0, sizeof g_ios);
    g_ios.usb.fd = -1;
    g_ios.usb.iface = -1;
    g_ios.usb.fake_out = st_ios_out;
    g_ios.usb.fake_in = st_ios_in;
    g_ios.usb.fake = ios;
    g_ios.usb.mps = 512;
    char name[160], err[512] = "";
    bool ok = ios_start_on(name, sizeof name, err, sizeof err, NULL);
    ST_CHECK(ok && !strcmp(name, "Test iPhone") && g_ios.npool == AFC_POOL && g_ios.mux->version == 2);
    if (!ok) fprintf(stderr, "  ios: %s\n", err);
    ST_CHECK(ios->pair_calls == 2 && ios->cert_ok && ios->sessions == 1 && !strcmp(g_ios.ld.udid, "FAKE-UDID-0001"));
    { // the pairing record is kept (ours, 0600) and used next time without asking again
        struct stat st;
        ST_CHECK(stat(DEV_BASE "/lockdown/FAKE-UDID-0001.plist", &st) == 0 && (st.st_mode & 077) == 0);
        PairRec pr;
        ST_CHECK(pair_load("FAKE-UDID-0001", &pr) && pr.ours && !strcmp(pr.host_id, ios->host_id) && pr.escrow_len == 6);
        pair_free(&pr);
    }
    if (ok) {
        Afc *a = g_ios.pool[1];
        char *names;
        size_t nl;
        ST_CHECK(afc_list(a, "/DCIM/100APPLE", &names, &nl) == 40);
        if (nl) free(names);
        struct stat st;
        ST_CHECK(afc_info(a, "/DCIM/100APPLE/IMG_0003.HEIC", &st) == 0 && st.st_size == 103 && S_ISREG(st.st_mode));
        ST_CHECK(afc_info(a, "/DCIM", &st) == 0 && S_ISDIR(st.st_mode) && afc_info(a, "/nope", &st) == -ENOENT);
        u64 h;
        ST_CHECK(afc_open(a, "/note.txt", AFC_WRONLY_TRUNC, &h) == 0 && afc_write(a, h, "hello iphone", 12) == 12 && afc_close(a, h) == 0);
        ST_CHECK(st_file_is(DEV_BASE "/iphone/note.txt", "hello iphone"));
        char buf[64] = "";
        ST_CHECK(afc_open(a, "/note.txt", AFC_RDONLY, &h) == 0 && afc_seek(a, h, 6) == 0 && afc_read(a, h, buf, sizeof buf) == 6 && !memcmp(buf, "iphone", 6));
        afc_close(a, h);
        // a big file goes in several requests and comes back whole (mux windows, packets across USB transfers)
        size_t big = 3u << 20;
        u8 *bd = xmalloc(big), *back = xmalloc(big);
        for (size_t i = 0; i < big; i++) bd[i] = (u8)(i * 7 + (i >> 13));
        ST_CHECK(afc_open(a, "/big.bin", AFC_WRONLY_TRUNC, &h) == 0 && afc_write(a, h, bd, big) == (ssize_t)big && afc_close(a, h) == 0);
        ST_CHECK(afc_open(a, "/big.bin", AFC_RDONLY, &h) == 0 && afc_read(a, h, back, big) == (ssize_t)big && !memcmp(bd, back, big));
        afc_close(a, h);
        free(bd);
        free(back);
        ST_CHECK(afc_rename(a, "/note.txt", "/note2.txt") == 0 && afc_path_op(a, AFC_MKDIR, "/Made") == 0 && afc_path_op(a, AFC_REMOVE, "/note2.txt") == 0);
        u64 tot, av, bs;
        ST_CHECK(afc_devinfo(a, &tot, &av, &bs) == 0 && tot == 128000000000ull && av == 64000000000ull);
        IosApp *apps;
        int na = ios_list_apps(&g_ios.ld, &apps, err, sizeof err);
        ST_CHECK(na == 1 && !strcmp(apps[0].name, "VLC") && !strcmp(apps[0].id, "org.videolan.vlc"));
        if (na > 0) free(apps);
        Afc *va = ios_app_documents(&g_ios.ld, "org.videolan.vlc", err, sizeof err);
        ST_CHECK(va && afc_info(va, "/Documents/movie.txt", &st) == 0 && st.st_size == 6);
        afc_free(va);
        ST_CHECK(!ios_app_documents(&g_ios.ld, "com.example.notes", err, sizeof err));
    }

    // --- the iPhone as a folder (FUSE), with plain file calls ---
    StFuse sf = {0};
    const char *mp = DEV_BASE "/mnt-iphone";
    g_dfs_kind = 'a';
    g_dfs_uid = getuid();
    g_dfs_gid = getgid();
    bool fuse_ok = ok && st_fuse_start(&sf, mp);
    if (!fuse_ok) fprintf(stderr, "  (FUSE isn't available here: the folder tests are skipped)\n");
    if (fuse_ok) {
        ST_CHECK(st_count_dir(mp, "DCIM") >= 3 && st_count_dir(mp, DFS_APPS) >= 3);
        ST_CHECK(st_count_dir(DEV_BASE "/mnt-iphone/DCIM/100APPLE", "IMG_0039.HEIC") == 40);
        struct stat st;
        ST_CHECK(stat(DEV_BASE "/mnt-iphone/DCIM/100APPLE/IMG_0010.HEIC", &st) == 0 && st.st_size == 110 && st.st_uid == getuid());
        st_write_text(DEV_BASE "/mnt-iphone/from-pc.txt", "copied in");
        ST_CHECK(st_file_is(DEV_BASE "/iphone/from-pc.txt", "copied in") && st_file_is(DEV_BASE "/mnt-iphone/from-pc.txt", "copied in"));
        ST_CHECK(rename(DEV_BASE "/mnt-iphone/from-pc.txt", DEV_BASE "/mnt-iphone/DCIM/moved.txt") == 0 &&
                 st_file_is(DEV_BASE "/iphone/DCIM/moved.txt", "copied in"));
        ST_CHECK(mkdir(DEV_BASE "/mnt-iphone/NewFolder", 0755) == 0 && stat(DEV_BASE "/iphone/NewFolder", &st) == 0);
        ST_CHECK(rmdir(DEV_BASE "/mnt-iphone/DCIM") != 0 && errno == ENOTEMPTY);
        ST_CHECK(unlink(DEV_BASE "/mnt-iphone/DCIM/moved.txt") == 0 && stat(DEV_BASE "/iphone/DCIM/moved.txt", &st) != 0);
        ST_CHECK(st_count_dir(DEV_BASE "/mnt-iphone/" DFS_APPS, "VLC") == 1);
        ST_CHECK(st_file_is(DEV_BASE "/mnt-iphone/" DFS_APPS "/VLC/movie.txt", "a film"));
        st_write_text(DEV_BASE "/mnt-iphone/" DFS_APPS "/VLC/sub.srt", "1\n00:00");
        ST_CHECK(st_file_is(DEV_BASE "/vlc/Documents/sub.srt", "1\n00:00"));
        ST_CHECK(rename(DEV_BASE "/mnt-iphone/" DFS_APPS "/VLC/sub.srt", DEV_BASE "/mnt-iphone/sub.srt") != 0 && errno == EXDEV); // between areas: copied
        struct timespec ts[2] = {{1700000000, 0}, {1700000000, 0}};
        ST_CHECK(utimensat(AT_FDCWD, DEV_BASE "/mnt-iphone/NewFolder", ts, 0) == 0 && stat(DEV_BASE "/iphone/NewFolder", &st) == 0 && st.st_mtim.tv_sec == 1700000000);
        struct statvfs sv;
        ST_CHECK(statvfs(mp, &sv) == 0 && (u64)sv.f_blocks * sv.f_frsize == 128000000000ull);
        // a connection that broke (an answer went missing) is replaced, the folder keeps working
        g_ios.pool[2]->broken = true;
        Afc *was = g_ios.pool[2];
        for (int i = 0; i < AFC_POOL; i++) ios_pick();
        ST_CHECK(g_ios.pool[2] != was && !g_ios.pool[2]->broken && g_ios.nold == 1);
        for (int i = 0; i < 8; i++) ST_CHECK(st_count_dir(DEV_BASE "/mnt-iphone/DCIM/100APPLE", "IMG_0001.HEIC") == 40);
        st_fuse_stop(&sf, mp);
    }
    // the device goes away: everything waiting gives up (no hang)
    if (ok) {
        atomic_store(&g_ios.mux->dead, true);
        pthread_mutex_lock(&g_ios.mux->mtx);
        pthread_cond_broadcast(&g_ios.mux->cond);
        pthread_mutex_unlock(&g_ios.mux->mtx);
        struct stat st;
        ST_CHECK(afc_info(g_ios.pool[0], "/DCIM", &st) == -EIO && !mux_connect(g_ios.mux, 62078, 1000));
        for (int i = 0; i < g_ios.npool; i++) afc_free(g_ios.pool[i]);
        for (int i = 0; i < g_ios.nappafc; i++) afc_free(g_ios.appafc[i].afc);
        for (int i = 0; i < g_ios.nold; i++) afc_free(g_ios.old[i]);
        free(g_ios.apps);
        ld_close(&g_ios.ld);
        mux_free(g_ios.mux);
    }
    for (int i = 0; i < 24; i++) free(ios->conns[i].buf);
    for (int i = 0; i < 64; i++) if (ios->files[i] > 0) close(ios->files[i]);
    EVP_PKEY_free(ios->key);
    free(ios->pub_pem);
    free(ios->q.b);
    free(ios);

    // --- the made-up MTP phone ---
    StMtp *mtp = xcalloc(1, sizeof *mtp);
    pthread_mutex_init(&mtp->q.m, NULL);
    pthread_cond_init(&mtp->q.c, NULL);
    mtp->next = 1;
    StObj *dcim = st_mtp_add(mtp, 0, "DCIM", true, NULL);
    StObj *cam = st_mtp_add(mtp, dcim->handle, "Camera", true, NULL);
    for (int i = 0; i < 25; i++) { char nm[32]; snprintf(nm, sizeof nm, "PXL_%02d.jpg", i); st_mtp_add(mtp, cam->handle, nm, false, "jpegdata"); }
    st_mtp_add(mtp, 0, "readme.txt", false, "hello android");
    memset(&g_mtp, 0, sizeof g_mtp);
    g_mtp.usb.fd = -1;
    g_mtp.usb.iface = -1;
    g_mtp.usb.fake_out = st_mtp_out;
    g_mtp.usb.fake_in = st_mtp_in;
    g_mtp.usb.fake = mtp;
    g_mtp.usb.mps = 512;
    ok = mtp_start_on("Phone", name, sizeof name, err, sizeof err, NULL);
    ST_CHECK(ok && !strcmp(name, "Pixel Test") && g_mtp.ptp.mtp && g_mtp.nst == 1 && !strcmp(g_mtp.st[0].name, "Internal shared storage") &&
             g_mtp.st[0].total == 32ull << 30 && g_mtp.st[0].avail == 16ull << 30);
    if (!ok) fprintf(stderr, "  mtp: %s\n", err);
    if (ok) {
        PtpObj *objs;
        int n = ptp_list(&g_mtp.ptp, 0x00010001, PTP_ROOT, &objs);
        ST_CHECK(n == 2);
        free(objs);
        int infos = mtp->infos;
        n = ptp_list(&g_mtp.ptp, 0x00010001, cam->handle, &objs);
        ST_CHECK(n == 25 && mtp->proplists == 1 && mtp->infos == infos && objs[3].size == 8 && !strcmp(objs[3].name, "PXL_03.jpg")); // one request for all 25
        free(objs);
        char buf[32] = "";
        ST_CHECK(ptp_read(&g_mtp.ptp, st_mtp_obj(mtp, mtp->next - 1)->handle, 6, buf, 100) == 7 && !memcmp(buf, "android", 7));
    }
    const char *mp2 = DEV_BASE "/mnt-phone";
    g_dfs_kind = 'm';
    dfs_cache_drop("/"); // (the iPhone's paths: one device per helper normally)
    StFuse sf2 = {0};
    fuse_ok = ok && st_fuse_start(&sf2, mp2);
    if (fuse_ok) {
        struct stat st;
        ST_CHECK(st_count_dir(mp2, "readme.txt") == 2 && st_count_dir(DEV_BASE "/mnt-phone/DCIM/Camera", "PXL_24.jpg") == 25);
        ST_CHECK(st_file_is(DEV_BASE "/mnt-phone/readme.txt", "hello android"));
        ST_CHECK(stat(DEV_BASE "/mnt-phone/DCIM/Camera/PXL_07.jpg", &st) == 0 && st.st_size == 8 && S_ISREG(st.st_mode));
        st_write_text(DEV_BASE "/mnt-phone/DCIM/new.txt", "sent over MTP");
        StObj *nw = NULL;
        for (int i = 0; i < 128; i++) if (mtp->objs[i].used && !strcmp(mtp->objs[i].name, "new.txt")) nw = &mtp->objs[i];
        ST_CHECK(nw && nw->parent == dcim->handle && nw->len == 13 && !memcmp(nw->data, "sent over MTP", 13));
        ST_CHECK(st_file_is(DEV_BASE "/mnt-phone/DCIM/new.txt", "sent over MTP"));
        { // a big file: whole packets until the end of the upload, and it comes back the same
            size_t big = (3u << 20) + 777;
            u8 *bd = xmalloc(big), *back = xmalloc(big);
            for (size_t i = 0; i < big; i++) bd[i] = (u8)(i * 13 + (i >> 11));
            int fd = open(DEV_BASE "/mnt-phone/big.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
            ssize_t wr = fd >= 0 ? write(fd, bd, big) : -1;
            int cr = fd >= 0 ? close(fd) : -1;
            if (wr != (ssize_t)big || cr) fprintf(stderr, "  big upload: write %zd close %d (%s)\n", wr, cr, strerror(errno));
            ST_CHECK(fd >= 0 && wr == (ssize_t)big && cr == 0);
            fd = open(DEV_BASE "/mnt-phone/big.bin", O_RDONLY);
            size_t got = 0;
            for (ssize_t r; fd >= 0 && (r = read(fd, back + got, big - got)) > 0;) got += (size_t)r;
            if (fd >= 0) close(fd);
            ST_CHECK(got == big && !memcmp(bd, back, big) && !mtp->short_mid);
            free(bd);
            free(back);
        }
        st_write_text(DEV_BASE "/mnt-phone/DCIM/new.txt", "replaced"); // MTP can't change a file: it's sent again
        ST_CHECK(st_file_is(DEV_BASE "/mnt-phone/DCIM/new.txt", "replaced"));
        ST_CHECK(rename(DEV_BASE "/mnt-phone/DCIM/new.txt", DEV_BASE "/mnt-phone/DCIM/renamed.txt") == 0 &&
                 st_file_is(DEV_BASE "/mnt-phone/DCIM/renamed.txt", "replaced") && stat(DEV_BASE "/mnt-phone/DCIM/new.txt", &st) != 0);
        ST_CHECK(rename(DEV_BASE "/mnt-phone/DCIM/renamed.txt", DEV_BASE "/mnt-phone/moved.txt") == 0 && st_file_is(DEV_BASE "/mnt-phone/moved.txt", "replaced"));
        ST_CHECK(mkdir(DEV_BASE "/mnt-phone/Music", 0755) == 0 && stat(DEV_BASE "/mnt-phone/Music", &st) == 0 && S_ISDIR(st.st_mode));
        ST_CHECK(rmdir(DEV_BASE "/mnt-phone/DCIM") != 0 && errno == ENOTEMPTY);
        ST_CHECK(unlink(DEV_BASE "/mnt-phone/moved.txt") == 0 && stat(DEV_BASE "/mnt-phone/moved.txt", &st) != 0);
        ST_CHECK(rmdir(DEV_BASE "/mnt-phone/Music") == 0);
        struct statvfs sv;
        ST_CHECK(statvfs(mp2, &sv) == 0 && (u64)sv.f_bavail * sv.f_frsize == 16ull << 30);
        st_fuse_stop(&sf2, mp2);
    }
    if (ok) ptp_close(&g_mtp.ptp);
    for (int i = 0; i < 128; i++) free(mtp->objs[i].data);
    free(mtp->q.b);
    free(mtp);
    unsetenv("FE_LOCKDOWN_DIR");
    fprintf(stderr, "selftest-devices: %s (%d failures)\n", st_fail ? "FAIL" : "ok", st_fail);
    return st_fail ? 1 : 0;
}
