// ui_tools.c - modal "sheets": Open with, Properties, Batch rename, Options, Connect to server (included from app.c)

enum { SHEET_NONE, SHEET_OPENWITH, SHEET_PROPS, SHEET_RENAME, SHEET_OPTIONS, SHEET_CONNECT, SHEET_SECRET, SHEET_ADDON }; // SHEET_ADDON: an add-on's (addons.c)

typedef struct {
    int kind;
    Rect r;
    char **paths;
    int npaths;
    Pane *pane;
    int hover;
    // open with
    char mime[160];
    DesktopApp *apps[512];
    int napps, sel, top;
    f32 ow_scroll;   // app list scroll offset (pixels)
    bool ow_reveal;  // keep the selection in view on the next draw (keyboard / filter changes)
    bool show_all, set_default;
    TextEdit filter;
    Rect rlist, rsearch, ropen, rcancel, rcheck_def, rcheck_all;
    f32 row_h;
    // properties
    StatsJob *stats;
    SumJob *sum;
    struct stat st;
    i64 btime;
    bool have_st;
    char link_target[PATH_MAX];
    Rect rperm[9], rsum, rclose, rchange, rcopy_sha, rcopy_md5, rreveal, rbody;
    SnapJob *snap;                  // previous versions (snapshots)
    Rect rsv_open[8], rsv_act[8];   // their Open / Restore... (Compare for folders) buttons
    Rect rsv_admin;                 // "Show as administrator" (snapshots only root can read)
    f32 pv_scroll, pv_content; // properties body scroll offset / content height
    // batch rename
    TextEdit tmpl, find, repl;
    int focus; // 0 template, 1 find, 2 replace
    int case_mode;
    int num_start;
    bool match_case;
    char **newnames;
    u8 *status; // 0 unchanged, 1 change, 2 conflict
    int nconflicts, nchanges;
    Rect rfield[3], rcase[4], rmatch, rapply, rnum_minus, rnum_plus;
    // connect to server
    TextEdit cn[7]; // CN_* fields (+ CN_NAME)
    int cn_focus, cn_proto;
    bool cn_remember, cn_remember_pw;
    int cn_edit; // editing saved server i (-1: a new connection)
    Rect cn_rsave;
    Rect cn_rf[7], cn_rproto[8], cn_rremember, cn_rremember_pw, cn_rconnect, cn_rsaved[8], cn_rforget[8];
    DevJob *sec_job; // password prompt: the connection waiting for it
    // connect: the location's icon (network icon key, "" = default) and its picker popover
    char cn_icon[160];
    bool cn_pick;
    Rect cn_ricon, cn_rpick, cn_rpick_body;
    f32 cn_pick_scroll, cn_pick_content;
    Rect cn_rcell[256];
    int cn_cell_idx[256], cn_ncell;
} Sheet;
enum { CN_HOST, CN_PORT, CN_PATH, CN_USER, CN_PASS, CN_DOMAIN, CN_COUNT, CN_NAME = CN_COUNT, CN_ALL }; // CN_COUNT: the server card

static Sheet g_sheet;

static void sheet_close(void) {
    Sheet *s = &g_sheet;
    if (s->kind == SHEET_SECRET && s->sec_job) { dev_net_answer(s->sec_job, -1); s->sec_job = NULL; } // closed: cancel the login
    explicit_bzero(s->cn[CN_PASS].buf, sizeof s->cn[CN_PASS].buf); // the typed password doesn't linger
    if (s->stats) { atomic_store(&s->stats->cancel, 1); stats_release(s->stats); }
    if (s->sum) { atomic_store(&s->sum->cancel, 1); sum_release(s->sum); }
    if (s->snap) snap_release(s->snap);
    strv_free(s->newnames, s->npaths);
    free(s->status);
    strv_free(s->paths, s->npaths);
    memset(s, 0, sizeof *s);
    A.redraw = true;
}

static bool sheet_open(void) { return g_sheet.kind != SHEET_NONE; }

// --- app icons (hicolor theme / pixmaps / absolute paths) --------------------

typedef struct { char name[128]; int px; SDL_Texture *tex; bool missing; } AppIcon;
static AppIcon g_app_icons[96];
static int g_napp_icons;

static SDL_Texture *app_icon_tex(const char *icon, int px) {
    if (!icon || !icon[0]) return NULL;
    for (int i = 0; i < g_napp_icons; i++)
        if (g_app_icons[i].px == px && !strcmp(g_app_icons[i].name, icon)) return g_app_icons[i].tex;
    if (g_napp_icons == (int)ARRAY_COUNT(g_app_icons)) { // simple flush
        for (int i = 0; i < g_napp_icons; i++)
            if (g_app_icons[i].tex) SDL_DestroyTexture(g_app_icons[i].tex);
        g_napp_icons = 0;
    }
    AppIcon *ai = &g_app_icons[g_napp_icons++];
    memset(ai, 0, sizeof *ai);
    snprintf(ai->name, sizeof ai->name, "%s", icon);
    ai->px = px;
    char found[PATH_MAX] = "";
    if (icon[0] == '/') {
        if (access(icon, R_OK) == 0) snprintf(found, sizeof found, "%s", icon);
    } else {
        // hicolor (where apps install their icons) in the data home, Flatpak's exports and every $XDG_DATA_DIRS entry
        const char *home = getenv("HOME") ? getenv("HOME") : "/";
        char data[24][PATH_MAX], bases[28][PATH_MAX];
        int nd = xdg_dirs("XDG_DATA_DIRS", XDG_DATA_DEF, data, 24), nb = 0;
        const char *xd = getenv("XDG_DATA_HOME");
        if (xd && xd[0] == '/') snprintf(bases[nb++], PATH_MAX, "%s/icons/hicolor", xd);
        else snprintf(bases[nb++], PATH_MAX, "%s/.local/share/icons/hicolor", home);
        snprintf(bases[nb++], PATH_MAX, "%s/.local/share/flatpak/exports/share/icons/hicolor", home);
        snprintf(bases[nb++], PATH_MAX, "/var/lib/flatpak/exports/share/icons/hicolor");
        for (int i = 0; i < nd; i++) snprintf(bases[nb++], PATH_MAX, "%.*s/icons/hicolor", PATH_MAX - 16, data[i]);
        static const char *sizes[] = {"scalable", "256x256", "128x128", "96x96", "64x64", "48x48", "32x32", "24x24"};
        for (int b = 0; b < nb && !found[0]; b++)
            for (size_t z = 0; z < ARRAY_COUNT(sizes) && !found[0]; z++)
                for (int e = 0; e < 2 && !found[0]; e++) {
                    char p[PATH_MAX * 2];
                    snprintf(p, sizeof p, "%s/%s/apps/%s.%s", bases[b], sizes[z], icon, e ? "png" : "svg");
                    if (access(p, R_OK) == 0) snprintf(found, sizeof found, "%s", p);
                }
        for (int i = 0; i < nd && !found[0]; i++)
            for (int e = 0; e < 2 && !found[0]; e++) {
                char p[PATH_MAX * 2];
                snprintf(p, sizeof p, "%s/pixmaps/%s.%s", data[i], icon, e ? "png" : "svg");
                if (access(p, R_OK) == 0) snprintf(found, sizeof found, "%s", p);
            }
    }
    if (!found[0]) { ai->missing = true; return NULL; }
    u8 *rgba = NULL;
    int w = 0, h = 0;
    size_t fl = strlen(found);
    if (fl > 4 && !strcmp(found + fl - 4, ".svg")) {
        NSVGimage *img = nsvgParseFromFile(found, "px", 96.0f);
        if (img && img->width > 0 && img->height > 0) {
            if (!g_rast) g_rast = nsvgCreateRasterizer();
            w = h = px;
            rgba = xcalloc((size_t)px * px, 4);
            f32 sc = (f32)px / MAX(img->width, img->height);
            nsvgRasterize(g_rast, img, (px - img->width * sc) * 0.5f, (px - img->height * sc) * 0.5f, sc, rgba, px, px, px * 4);
        }
        if (img) nsvgDelete(img);
    } else {
        int n;
        u8 *px0 = stbi_load(found, &w, &h, &n, 4);
        if (px0) rgba = downscale(px0, w, h, px, &w, &h);
    }
    if (!rgba) { ai->missing = true; return NULL; }
    ai->tex = SDL_CreateTexture(A.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (ai->tex) {
        SDL_UpdateTexture(ai->tex, NULL, rgba, w * 4);
        SDL_SetTextureBlendMode(ai->tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(ai->tex, SDL_SCALEMODE_LINEAR);
    }
    if (fl > 4 && !strcmp(found + fl - 4, ".svg")) free(rgba);
    else stbi_image_free(rgba);
    return ai->tex;
}

// --- small widgets -------------------------------------------------------------

static void sheet_frame(Rect r, const char *title, const char *subtitle) {
    draw_rect(0, 0, (f32)A.W, (f32)A.H, CA(0x000000, 0.45f));
    draw_rrect(r.x, r.y, r.w, r.h, S(12), C(COL_POPUP));
    draw_rrect_outline(r.x, r.y, r.w, r.h, S(12), 1, C(COL_POPUP_BORDER));
    Font *tf = &A.font_brand, *fs = &A.font_small; // title: semibold, a size up
    f32 pad = roundf(S(20));
    draw_text_fit(tf, r.x + pad, r.y + pad - roundf(S(2)), r.w - pad * 2, title, (int)strlen(title), C(COL_TEXT_STRONG), 0);
    if (subtitle && subtitle[0])
        draw_text_fit(fs, r.x + pad, r.y + pad + roundf(S(24)), r.w - pad * 2, subtitle, (int)strlen(subtitle), C(COL_TEXT_DIM), 0);
}

static bool sheet_button(Rect r, const char *label, bool primary, bool enabled) {
    bool hv = hand_if(enabled && A.mouse_in && in_rect(r, A.mx, A.my));
    Font *f = &A.font;
    u32 col = !enabled ? COL_CONTROL_OFF : primary ? (hv ? COL_ACCENT_HOVER : COL_ACCENT) : (hv ? COL_CONTROL_HOVER : COL_CONTROL);
    draw_rrect(r.x, r.y, r.w, r.h, S(7), C(col));
    f32 tw = text_width(f, label, (int)strlen(label));
    draw_text(f, r.x + (r.w - tw) * 0.5f, text_y(f, r.y, r.h), label, (int)strlen(label),
              C(!enabled ? COL_TEXT_FAINT : primary ? COL_ON_ACCENT : COL_TEXT));
    return hv;
}

static void sheet_check(Rect r, const char *label, bool on) {
    hot(r);
    Font *fs = &A.font_small;
    f32 cb = roundf(S(16)), cy = roundf(r.y + (r.h - cb) * 0.5f);
    draw_rrect(r.x, cy, cb, cb, S(4), on ? C(COL_ACCENT) : C(COL_CHIP));
    if (on) {
        f32 t = MAX(1.5f, S(1.8f));
        draw_line(r.x + cb * 0.22f, cy + cb * 0.52f, r.x + cb * 0.42f, cy + cb * 0.72f, t, C(COL_ON_ACCENT));
        draw_line(r.x + cb * 0.42f, cy + cb * 0.72f, r.x + cb * 0.8f, cy + cb * 0.3f, t, C(COL_ON_ACCENT));
    }
    draw_text_fit(fs, r.x + cb + S(8), text_y(fs, r.y, r.h), r.w - cb - S(8), label, (int)strlen(label), C(COL_TEXT2), 0);
}

static void sheet_field(Rect r, TextEdit *te, bool focused, const char *placeholder) {
    if (A.mouse_in && in_rect(r, A.mx, A.my)) A.ptr_text = true;
    draw_rrect(r.x, r.y, r.w, r.h, S(6), C(focused ? COL_FIELD_HOVER : COL_FIELD));
    if (focused) draw_rrect_outline(r.x, r.y, r.w, r.h, S(6), MAX(1.f, S(1.2f)), C(COL_ACCENT));
    if (focused) te_draw(te, &A.font, r, roundf(S(8)), C(COL_TEXT));
    else draw_text_fit(&A.font, r.x + S(8), text_y(&A.font, r.y, r.h), r.w - S(16), te->buf, (int)strlen(te->buf), C(COL_TEXT), 0);
    if (!te->buf[0] && placeholder)
        draw_text_fit(&A.font, r.x + S(10), text_y(&A.font, r.y, r.h), r.w - S(20), placeholder, (int)strlen(placeholder),
                      C(COL_TEXT_FAINT), 0);
}

// --- Open with ---------------------------------------------------------------

static int cmp_app_name(const void *a, const void *b) {
    return natcmp((*(DesktopApp *const *)a)->name, (*(DesktopApp *const *)b)->name);
}

static void openwith_refresh(void) {
    Sheet *s = &g_sheet;
    DesktopApp *tmp[512];
    int n = 0;
    if (s->show_all) {
        apps_load();
        for (int i = 0; i < g_napps && n < 512; i++)
            if (!g_apps[i].nodisplay) tmp[n++] = &g_apps[i];
        qsort(tmp, (size_t)n, sizeof(DesktopApp *), cmp_app_name);
    } else {
        n = apps_for_mime(s->mime, tmp, 512);
    }
    s->napps = 0;
    for (int i = 0; i < n; i++) {
        if (s->filter.buf[0] && !strcasestr(tmp[i]->name, s->filter.buf) && !strcasestr(tmp[i]->id, s->filter.buf)) continue;
        s->apps[s->napps++] = tmp[i];
    }
    s->sel = CLAMP(s->sel, 0, MAX(s->napps - 1, 0));
    s->top = 0;
    s->ow_scroll = 0;
    s->ow_reveal = true;
}

static void openwith_open(char **paths, int n) {
    if (!n) return;
    sheet_close();
    Sheet *s = &g_sheet;
    s->kind = SHEET_OPENWITH;
    s->paths = strv_dup(paths, n);
    s->npaths = n;
    struct stat st;
    bool dir = stat(paths[0], &st) == 0 && S_ISDIR(st.st_mode);
    snprintf(s->mime, sizeof s->mime, "%s", mime_for_file(path_basename(paths[0]), dir));
    te_set(&s->filter, "", 0, 0);
    openwith_refresh();
    if (!s->napps) { s->show_all = true; openwith_refresh(); }
    A.redraw = true;
}

// Folders: the default app for them is the file manager - Options > Integration owns that, not this sheet.
static bool ow_is_folder(const Sheet *s) { return !strcmp(s->mime, "inode/directory"); }

// One of the apps the type lists anyway (the Open with submenu shows those already)?
static bool ow_type_app(const char *mime, const DesktopApp *a) {
    DesktopApp *ta[512];
    int n = apps_for_mime(mime, ta, 512);
    for (int k = 0; k < n; k++)
        if (ta[k] == a) return true;
    return false;
}

static void openwith_go(void) {
    Sheet *s = &g_sheet;
    if (s->sel < 0 || s->sel >= s->napps) return;
    DesktopApp *a = s->apps[s->sel];
    if (s->set_default && !ow_is_folder(s)) app_set_default(a, s->mime);
    if (!app_launch(a, s->paths, s->npaths)) toast("Couldn't start %s", a->name);
    else if (!ow_type_app(s->mime, a)) recent_apps_push(s->mime, a->id); // joins the context menu's Open with submenu
    sheet_close();
}

static void openwith_draw(void) {
    Sheet *s = &g_sheet;
    Font *f = &A.font, *fs = &A.font_small;
    f32 w = roundf(MIN(S(520), A.W - S(40))), h = roundf(MIN(S(560), A.H - S(40)));
    Rect r = {roundf((A.W - w) * 0.5f), roundf((A.H - h) * 0.42f), w, h};
    s->r = r;
    char title[NAME_MAX + 32], sub[256];
    if (s->npaths == 1) snprintf(title, sizeof title, "Open \xE2\x80\x9C%s\xE2\x80\x9D with", path_basename(s->paths[0]));
    else snprintf(title, sizeof title, "Open %d items with", s->npaths);
    snprintf(sub, sizeof sub, "%s", s->mime);
    sheet_frame(r, title, sub);
    f32 pad = roundf(S(20)), y = r.y + pad + roundf(S(48));
    s->rsearch = (Rect){r.x + pad, y, w - pad * 2, roundf(S(32))};
    sheet_field(s->rsearch, &s->filter, true, "Search apps");
    y += roundf(S(42));
    s->row_h = roundf(S(40));
    f32 bh = roundf(S(32)), foot = bh + roundf(S(64));
    // the apps live in a rounded container that scrolls when they don't fit
    // inset well: darker than the dialog, with a border that stands out from it (any theme)
    Rect box = {r.x + pad, y, w - pad * 2, r.y + h - foot - y};
    f32 brad = roundf(S(8));
    u32 well = rgb_mix(COL_POPUP, 0x000000, theme_is_light(&T) ? 0.04f : 0.22f);
    draw_rrect(box.x, box.y, box.w, box.h, brad, C(well));
    f32 in = roundf(S(4)), sw = roundf(S(12)); // sw: scrollbar strip along the right edge
    s->rlist = (Rect){box.x + in, box.y + in, box.w - in * 2, box.h - in * 2};
    f32 content = s->napps * s->row_h, maxs = MAX(0.f, content - s->rlist.h);
    bool overflow = maxs > 0;
    if (overflow) s->rlist.w = box.w - sw - in * 2; // room for the scrollbar
    if (s->ow_reveal && s->napps) { // keyboard moved the selection: scroll just enough to show it
        f32 sy = s->sel * s->row_h;
        if (sy < s->ow_scroll) s->ow_scroll = sy;
        if (sy + s->row_h > s->ow_scroll + s->rlist.h) s->ow_scroll = sy + s->row_h - s->rlist.h;
        s->ow_reveal = false;
    }
    s->ow_scroll = CLAMP(s->ow_scroll, 0.f, maxs);
    ClipSave cs = draw_clip_push((SDL_Rect){(int)s->rlist.x, (int)s->rlist.y, (int)s->rlist.w, (int)s->rlist.h});
    DesktopApp *def = app_default_for(s->mime);
    int first = (int)(s->ow_scroll / s->row_h);
    for (int i = MAX(0, first); i < s->napps; i++) {
        DesktopApp *a = s->apps[i];
        Rect rr = {s->rlist.x, roundf(s->rlist.y + i * s->row_h - s->ow_scroll), s->rlist.w, s->row_h};
        if (rr.y > s->rlist.y + s->rlist.h) break;
        bool sel = i == s->sel, hv = hand_if(A.mouse_in && in_rect(rr, A.mx, A.my) && in_rect(s->rlist, A.mx, A.my));
        if (sel) draw_rrect(rr.x, rr.y + 1, rr.w, rr.h - 2, S(6), C(COL_SEL_FOCUS));
        else if (hv) draw_rrect(rr.x, rr.y + 1, rr.w, rr.h - 2, S(6), C(COL_HOVER));
        f32 is = roundf(S(28)), ix = rr.x + S(10), iy = roundf(rr.y + (rr.h - is) * 0.5f);
        SDL_Texture *t = app_icon_tex(a->icon, (int)is);
        if (t) {
            draw_flush();
            SDL_RenderTexture(A.ren, t, NULL, &(SDL_FRect){ix, iy, is, is});
        } else {
            draw_rrect(ix, iy, is, is, S(6), C(COL_CHIP));
            char ch[2] = {(char)toupper((u8)a->name[0]), 0};
            draw_text(f, ix + (is - text_width(f, ch, 1)) * 0.5f, text_y(f, iy, is), ch, 1, C(COL_TEXT));
        }
        f32 tx = ix + is + S(12);
        f32 ex = draw_text(f, tx, text_y(f, rr.y, rr.h), a->name, (int)strlen(a->name), C(sel ? COL_SEL_TEXT : COL_TEXT));
        if (a == def) draw_text(fs, ex + S(10), text_y(fs, rr.y, rr.h), "default", 7, C(sel ? COL_SEL_DIM : COL_ACCENT));
    }
    if (!s->napps) {
        const char *m = "No apps found";
        draw_text(f, s->rlist.x + S(14), s->rlist.y + S(12), m, (int)strlen(m), C(COL_TEXT_DIM));
    }
    draw_clip_pop(cs);
    if (overflow) { // scrollbar: a strip fused with the container's right edge (its rounded corners included)
        f32 sx = box.x + box.w - sw;
        ClipSave sc = draw_clip_push((SDL_Rect){(int)sx, (int)box.y, (int)sw + 1, (int)box.h});
        draw_rrect(box.x, box.y, box.w, box.h, brad, C(rgb_mix(well, COL_TEXT, 0.06f)));
        draw_clip_pop(sc);
        draw_rect(sx, box.y, 1, box.h, C(rgb_mix(well, COL_TEXT, 0.12f))); // divider between list and strip
        f32 tm = roundf(S(3)), tw = sw - tm * 2, track = box.h - tm * 2;
        f32 th = MAX(S(24), track * s->rlist.h / content), ty = box.y + tm + (track - th) * (s->ow_scroll / maxs);
        bool sh = A.mouse_in && A.mx >= sx && in_rect(box, A.mx, A.my);
        draw_rrect(sx + tm, roundf(ty), tw, roundf(th), tw * 0.5f, C(sh ? COL_SCROLL_HOVER : COL_SCROLL));
    }
    draw_rrect_outline(box.x, box.y, box.w, box.h, brad, MAX(1.f, S(1)), C(rgb_mix(COL_POPUP, COL_TEXT, 0.16f)));
    f32 fy = r.y + h - foot + S(10);
    char lab[220];
    snprintf(lab, sizeof lab, "Always use this app for %s", s->mime);
    s->rcheck_def = (Rect){0};
    if (!ow_is_folder(s)) {
        s->rcheck_def = (Rect){r.x + pad, fy, w - pad * 2, roundf(S(22))};
        sheet_check(s->rcheck_def, lab, s->set_default);
        fy += roundf(S(24));
    }
    s->rcheck_all = (Rect){r.x + pad, fy, roundf(S(200)), roundf(S(22))};
    sheet_check(s->rcheck_all, "Show all apps", s->show_all);
    f32 bw = roundf(S(96));
    s->ropen = (Rect){r.x + w - pad - bw, r.y + h - pad - bh, bw, bh};
    s->rcancel = (Rect){s->ropen.x - S(8) - bw, s->ropen.y, bw, bh};
    sheet_button(s->rcancel, "Cancel", false, true);
    sheet_button(s->ropen, "Open", true, s->napps > 0);
}

// --- Properties -------------------------------------------------------------

static void props_open(char **paths, int n) {
    if (!n) return;
    sheet_close();
    Sheet *s = &g_sheet;
    s->kind = SHEET_PROPS;
    s->paths = strv_dup(paths, n);
    s->npaths = n;
    if (n == 1) {
        s->have_st = lstat(paths[0], &s->st) == 0;
        struct statx sx;
        if (statx(AT_FDCWD, paths[0], AT_SYMLINK_NOFOLLOW, STATX_BTIME, &sx) == 0 && (sx.stx_mask & STATX_BTIME))
            s->btime = sx.stx_btime.tv_sec;
        if (s->have_st && S_ISLNK(s->st.st_mode)) {
            ssize_t k = readlink(paths[0], s->link_target, sizeof s->link_target - 1);
            if (k > 0) s->link_target[k] = 0;
        }
        struct stat tst;
        bool dir = stat(paths[0], &tst) == 0 && S_ISDIR(tst.st_mode);
        snprintf(s->mime, sizeof s->mime, "%s", mime_for_file(path_basename(paths[0]), dir));
        if (dir) s->stats = stats_start(paths, n);
        if (!strstr(paths[0], "::") && !path_is_remote(paths[0])) s->snap = snap_start(paths[0]); // previous versions
    } else {
        s->stats = stats_start(paths, n);
    }
    A.redraw = true;
}

static void props_toggle_perm(int k) {
    Sheet *s = &g_sheet;
    if (!s->have_st || s->npaths != 1) return;
    mode_t bit = 0400 >> k;
    mode_t m = (s->st.st_mode & 07777) ^ bit;
    if (chmod(s->paths[0], m)) { toast("Can't change permissions: %s", strerror(errno)); return; }
    lstat(s->paths[0], &s->st);
}

static const char *group_name(gid_t gid) {
    static char buf[64];
    struct group gr, *res = NULL;
    char tmp[1024];
    if (getgrgid_r(gid, &gr, tmp, sizeof tmp, &res) == 0 && res) snprintf(buf, sizeof buf, "%s", gr.gr_name);
    else snprintf(buf, sizeof buf, "%u", (unsigned)gid);
    return buf;
}

static void props_date(i64 t0, char *buf, size_t cap);

// Previous versions: restore one (the current one goes to the Trash first, or keep both), compare a folder's.
static struct { char cur[PATH_MAX], ver[PATH_MAX]; i64 when; bool admin; } g_restore;

static void version_restore_cb(int choice, bool check, void *ud) {
    (void)check;
    (void)ud;
    if (choice != 0 && choice != 1) return;
    char dst[PATH_MAX];
    if (choice == 0) snprintf(dst, sizeof dst, "%s", g_restore.cur);
    else { // keep both: "name (2026-11-01 10.00).ext" next to it
        char par[PATH_MAX], base[NAME_MAX + 1], stamp[32];
        path_split(g_restore.cur, par, sizeof par, base, sizeof base);
        time_t t = (time_t)g_restore.when;
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(stamp, sizeof stamp, "%Y-%m-%d %H.%M", &tm);
        const char *dot = strrchr(base, '.');
        int stem = dot && dot != base ? (int)(dot - base) : (int)strlen(base);
        for (int k = 1; k < 100; k++) {
            char nm[NAME_MAX + 64];
            if (k == 1) snprintf(nm, sizeof nm, "%.*s (%s)%s", stem, base, stamp, base + stem);
            else snprintf(nm, sizeof nm, "%.*s (%s %d)%s", stem, base, stamp, k, base + stem);
            path_join(dst, sizeof dst, par, nm);
            struct stat st;
            if (lstat(dst, &st)) break;
        }
    }
    char *src[1] = {g_restore.ver}, *dv[1] = {dst};
    Op *op = op_new(OP_SYNC, src, 1, NULL);
    op->dst = strv_dup(dv, 1);
    op->policy = CF_REPLACE;
    op->trash_first = choice == 0 && !g_restore.admin;
    op->admin = g_restore.admin; // (the snapshot is only readable as root)
    if (op->admin && !admin_allowed()) { op_free(op); return; }
    ops_start(op);
    sheet_close();
}

// A version only root can read: copied (as root) to the cache, then opened from there.
static void version_open_admin(const char *cur, const SnapVer *v) {
    if (!admin_allowed()) return;
    char dir[PATH_MAX], dst[PATH_MAX], stamp[32], nm[NAME_MAX + 40];
    cache_subdir("versions", dir, sizeof dir);
    mkdir_p(dir, 0700);
    time_t t = (time_t)v->when;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(stamp, sizeof stamp, "%Y-%m-%d_%H.%M", &tm);
    snprintf(nm, sizeof nm, "%s %s", stamp, path_basename(cur));
    path_join(dst, sizeof dst, dir, nm);
    char *src[1] = {(char *)v->path}, *dv[1] = {dst};
    Op *op = op_new(OP_SYNC, src, 1, NULL);
    op->dst = strv_dup(dv, 1);
    op->policy = CF_REPLACE;
    op->admin = true;
    op->open_after = true;
    ops_start(op);
}

static void version_restore_ask(const char *cur, const SnapVer *v, bool admin) {
    g_restore.admin = admin;
    snprintf(g_restore.cur, sizeof g_restore.cur, "%s", cur);
    snprintf(g_restore.ver, sizeof g_restore.ver, "%s", v->path);
    g_restore.when = v->when;
    char d[64], msg[700];
    props_date(v->when, d, sizeof d);
    snprintf(msg, sizeof msg, "Put back \xE2\x80\x9C%s\xE2\x80\x9D as it was on %s (%s)? %s Keep both puts the old version next to it.",
             path_basename(cur), d, v->label, admin ? "Replace overwrites the current one (as the administrator)." : "Replace moves the current one to the Trash.");
    static const char *b[] = {"Replace", "Keep both", "Cancel"};
    dialog_open("Restore previous version", msg, b, 3, 0, 2, NULL, version_restore_cb, NULL);
    A.dlg.icon = DI_QUESTION;
}

static void version_compare(const char *cur, const char *ver) { // the folder now (left) and as it was (right)
    char a[PATH_MAX], b[PATH_MAX];
    snprintf(a, sizeof a, "%s", cur);
    snprintf(b, sizeof b, "%s", ver);
    sheet_close();
    if (!A.ap || !A.ap->cur) return;
    Tab *t = A.ap->cur;
    if (strcmp(t->pane[0].path, a)) pane_navigate(&t->pane[0], a, true);
    Panel *np = panel_split(A.ap, ZONE_RIGHT, NULL, b);
    if (np) cmp_start(t, np->cur);
}

// --- Properties layout: header (icon, name, summary) + sections of rounded cards with label/value rows ---

static void props_date(i64 t0, char *buf, size_t cap) {
    time_t t = (time_t)t0, now = time(NULL);
    struct tm tm, tn, ty;
    localtime_r(&t, &tm);
    localtime_r(&now, &tn);
    time_t yest = now - 86400;
    localtime_r(&yest, &ty);
    if (tm.tm_year == tn.tm_year && tm.tm_yday == tn.tm_yday) strftime(buf, cap, "Today, %H:%M", &tm);
    else if (tm.tm_year == ty.tm_year && tm.tm_yday == ty.tm_yday) strftime(buf, cap, "Yesterday, %H:%M", &tm);
    else strftime(buf, cap, "%-d %b %Y, %H:%M", &tm);
}

static u32 props_well(void) { return rgb_mix(COL_POPUP, 0x000000, theme_is_light(&T) ? 0.04f : 0.22f); }

// Section heading; returns the y where its card starts.
static f32 props_heading(f32 x, f32 y, const char *t) {
    Font *fs = &A.font_small;
    draw_text(fs, x + S(2), y, t, (int)strlen(t), C(COL_TEXT_FAINT));
    return y + roundf(fs->line_h + S(6));
}

// Card background for n rows of height rh (plus extra pixels); dividers between rows.
static void props_card(f32 x, f32 y, f32 w, int n, f32 rh, f32 extra) {
    f32 h = n * rh + extra;
    draw_rrect(x, y, w, h, S(8), C(props_well()));
    draw_rrect_outline(x, y, w, h, S(8), MAX(1.f, S(1)), C(rgb_mix(COL_POPUP, COL_TEXT, 0.12f)));
    for (int i = 1; i < n; i++) draw_rect(x + S(12), roundf(y + i * rh), w - S(24), 1, C(rgb_mix(props_well(), COL_TEXT, 0.08f)));
}

// One label/value row inside a card; btn (optional) is placed at the row's right end and returned in *rb.
static void props_line(f32 x, f32 y, f32 w, f32 rh, const char *label, const char *value, Font *vf, const char *btn, Rect *rb) {
    Font *fs = &A.font_small;
    f32 lw = roundf(S(118)), px = roundf(S(14));
    draw_text_fit(fs, x + px, text_y(fs, y, rh), lw - px, label, (int)strlen(label), C(COL_TEXT_DIM), 0);
    f32 vx = x + lw, vmax = x + w - px - vx;
    if (btn) {
        Font *f = &A.font;
        f32 bw = roundf(text_width(f, btn, (int)strlen(btn)) + S(24)), bh = roundf(S(26));
        Rect b = {roundf(x + w - S(8) - bw), roundf(y + (rh - bh) * 0.5f), bw, bh};
        sheet_button(b, btn, false, true);
        if (rb) *rb = b;
        vmax = b.x - S(10) - vx;
    }
    draw_text_fit(vf, vx, text_y(vf, y, rh), vmax, value, (int)strlen(value), C(COL_TEXT), 0);
}

// Scrolling area b cut at its edges: fade the content out (dialog colour) and draw a divider, only on the
// edges that have more content beyond them.
static void sheet_fade_edges(Rect b, f32 scroll, f32 maxs, f32 dpad) {
    f32 fh = roundf(S(22));
    Color solid = C(COL_POPUP), clear = solid;
    clear.a = 0;
    u32 line = rgb_mix(COL_POPUP, COL_TEXT, 0.12f);
    if (scroll < maxs - 0.5f) {
        draw_reserve(4, 6);
        int a0 = dv(b.x + 1, b.y + b.h - fh, clear, WU, WU), a1 = dv(b.x + b.w - 1, b.y + b.h - fh, clear, WU, WU);
        int a2 = dv(b.x + b.w - 1, b.y + b.h, solid, WU, WU), a3 = dv(b.x + 1, b.y + b.h, solid, WU, WU);
        di4(a0, a1, a2, a3);
        draw_rect(b.x + dpad, b.y + b.h, b.w - dpad * 2, 1, C(line));
    }
    if (scroll > 0.5f) {
        draw_reserve(4, 6);
        int a0 = dv(b.x + 1, b.y, solid, WU, WU), a1 = dv(b.x + b.w - 1, b.y, solid, WU, WU);
        int a2 = dv(b.x + b.w - 1, b.y + fh, clear, WU, WU), a3 = dv(b.x + 1, b.y + fh, clear, WU, WU);
        di4(a0, a1, a2, a3);
        draw_rect(b.x + dpad, b.y - 1, b.w - dpad * 2, 1, C(line));
    }
}

static void props_draw(void) {
    Sheet *s = &g_sheet;
    Font *f = &A.font, *fs = &A.font_small, *fm = A.mono_path ? &A.font_mono : fs;
    // height fits the content (measured last frame), up to the window
    f32 hdr = roundf(S(20)) + roundf(S(48)) + roundf(S(16)), ftr = roundf(S(32)) + roundf(S(20)) * 2;
    f32 want = s->pv_content > 0 ? hdr + s->pv_content + ftr : S(840);
    f32 w = roundf(MIN(S(560), A.W - S(40))), h = roundf(MIN(MIN(S(840), want), A.H - S(40)));
    if (fabsf(want - (hdr + s->pv_content + ftr)) > 0.5f || s->pv_content <= 0) A.redraw = true;
    Rect r = {roundf((A.W - w) * 0.5f), roundf((A.H - h) * 0.42f), w, h};
    s->r = r;
    draw_rect(0, 0, (f32)A.W, (f32)A.H, CA(0x000000, 0.45f));
    draw_rrect(r.x, r.y, r.w, r.h, S(12), C(COL_POPUP));
    draw_rrect_outline(r.x, r.y, r.w, r.h, S(12), 1, C(COL_POPUP_BORDER));
    memset(s->rperm, 0, sizeof s->rperm);
    memset(s->rsv_open, 0, sizeof s->rsv_open);
    memset(s->rsv_act, 0, sizeof s->rsv_act);
    s->rsv_admin = (Rect){0, 0, 0, 0};
    s->rchange = s->rsum = s->rcopy_sha = s->rcopy_md5 = s->rreveal = (Rect){0, 0, 0, 0};
    f32 pad = roundf(S(20)), cw = w - pad * 2;
    char buf[PATH_MAX + 64], b2[64], b3[64], b4[64];
    bool single = s->npaths == 1, isdir = single && s->have_st && S_ISDIR(s->st.st_mode);
    const char *base = single ? (s->paths[0][1] ? path_basename(s->paths[0]) : "/") : NULL;

    // header: icon, name (semibold), one-line summary
    f32 is = roundf(S(48)), ix = r.x + pad, iy = r.y + pad;
    if (single && (isdir || (s->stats && !s->have_st))) draw_path_icon(ix, iy, is, s->paths[0]);
    else if (single) { if (!draw_icon_for_name(ix, iy, is, base, s->have_st && (s->st.st_mode & 0111))) draw_file_icon(ix, iy, is, 0x8C8F99); }
    else draw_dir_icon(ix, iy, is, "", false);
    char title[NAME_MAX + 64], sum[256] = "";
    if (single) snprintf(title, sizeof title, "%s", base);
    else snprintf(title, sizeof title, "%d items", s->npaths);
    if (s->stats) {
        fmt_size(b2, sizeof b2, atomic_load(&s->stats->size));
        snprintf(sum, sizeof sum, "%s \xC2\xB7 %s%s", single ? "Folder" : "Selection", b2, atomic_load(&s->stats->done) ? "" : "\xE2\x80\xA6");
    } else if (single && s->have_st) {
        char tl[64] = "File";
        const char *dot = strrchr(base, '.');
        if (S_ISLNK(s->st.st_mode)) snprintf(tl, sizeof tl, "Link");
        else if (dot && dot != base && strlen(dot + 1) <= 8) {
            size_t k = 0;
            for (const char *c = dot + 1; *c && k + 6 < sizeof tl; c++) tl[k++] = (char)toupper((u8)*c);
            snprintf(tl + k, sizeof tl - k, " file");
        }
        fmt_size(b2, sizeof b2, (u64)s->st.st_size);
        snprintf(sum, sizeof sum, "%s \xC2\xB7 %s", tl, b2);
    }
    f32 tx = ix + is + roundf(S(14));
    Font *tf = &A.font_brand;
    draw_text_fit(tf, tx, iy + roundf(S(2)), r.x + w - pad - tx, title, (int)strlen(title), C(COL_TEXT_STRONG), 0);
    draw_text_fit(fs, tx, iy + roundf(tf->line_h + S(6)), r.x + w - pad - tx, sum, (int)strlen(sum), C(COL_TEXT_DIM), 0);

    // scrolling body between the header and the footer
    f32 bh = roundf(S(32)), foot = bh + pad * 2;
    s->rbody = (Rect){r.x, iy + is + roundf(S(16)), w, r.y + h - foot - (iy + is + roundf(S(16)))};
    s->pv_scroll = CLAMP(s->pv_scroll, 0.f, MAX(0.f, s->pv_content - s->rbody.h));
    ClipSave cs = draw_clip_push((SDL_Rect){(int)s->rbody.x, (int)s->rbody.y, (int)s->rbody.w, (int)s->rbody.h});
    f32 x = r.x + pad, y0 = s->rbody.y - s->pv_scroll, y = y0, rh = roundf(S(36)), gap = roundf(S(18));

    // General
    {
        const char *lab[4];
        char val[4][PATH_MAX + 64];
        int n = 0, reveal_row = -1;
        if (single) {
            lab[n] = "Type";
            snprintf(val[n++], sizeof val[0], "%s", isdir ? "Folder" : s->mime);
        }
        if (s->stats) {
            fmt_size(b2, sizeof b2, atomic_load(&s->stats->size));
            u64 nf = atomic_load(&s->stats->files), nd = atomic_load(&s->stats->dirs);
            fmt_count(b3, nf);
            fmt_count(b4, nd);
            lab[n] = "Size";
            snprintf(val[n++], sizeof val[0], "%s \xC2\xB7 %s file%s, %s folder%s%s", b2, b3, nf == 1 ? "" : "s", b4, nd == 1 ? "" : "s",
                     atomic_load(&s->stats->done) ? "" : " \xE2\x80\xA6");
        } else if (single && s->have_st) {
            char cnt[32];
            fmt_size(b2, sizeof b2, (u64)s->st.st_size);
            fmt_count(cnt, (u64)s->st.st_size);
            lab[n] = "Size";
            snprintf(val[n++], sizeof val[0], "%s (%s bytes)", b2, cnt);
        }
        if (single) {
            char par[PATH_MAX];
            parent_of(s->paths[0], par, sizeof par);
            lab[n] = "Location";
            reveal_row = n;
            tilde_path(par, val[n++], sizeof val[0]);
            if (s->link_target[0]) { lab[n] = "Link to"; snprintf(val[n++], sizeof val[0], "%s", s->link_target); }
        }
        y = props_heading(x, y, "General");
        props_card(x, y, cw, n, rh, 0);
        for (int i = 0; i < n; i++)
            props_line(x, y + i * rh, cw, rh, lab[i], val[i], fs, i == reveal_row ? "Show" : NULL, i == reveal_row ? &s->rreveal : NULL);
        y += n * rh + gap;
    }
    if (single && s->have_st) {
        // Dates
        const char *lab[3];
        char val[3][64];
        int n = 0;
        lab[n] = "Modified"; props_date(s->st.st_mtime, val[n++], 64);
        if (s->btime) { lab[n] = "Created"; props_date(s->btime, val[n++], 64); }
        lab[n] = "Accessed"; props_date(s->st.st_atime, val[n++], 64);
        y = props_heading(x, y, "Dates");
        props_card(x, y, cw, n, rh, 0);
        for (int i = 0; i < n; i++) props_line(x, y + i * rh, cw, rh, lab[i], val[i], fs, NULL, NULL);
        y += n * rh + gap;

        // Permissions: owner / group, then the read-write-execute grid with the octal mode as a chip
        y = props_heading(x, y, "Permissions");
        f32 gh = roundf(S(30)), grid = gh * 4 + roundf(S(8));
        props_card(x, y, cw, 2, rh, grid);
        props_line(x, y, cw, rh, "Owner", owner_name(s->st.st_uid), fs, NULL, NULL);
        props_line(x, y + rh, cw, rh, "Group", group_name(s->st.st_gid), fs, NULL, NULL);
        f32 gy = y + rh * 2 + roundf(S(4)), lw = roundf(S(118)), colw = roundf(S(82)), px = roundf(S(14));
        draw_rect(x + S(12), roundf(y + rh * 2), cw - S(24), 1, C(rgb_mix(props_well(), COL_TEXT, 0.08f)));
        static const char *who[3] = {"Owner", "Group", "Others"}, *what[3] = {"Read", "Write", "Execute"};
        for (int c = 0; c < 3; c++) draw_text(fs, x + lw + c * colw, text_y(fs, gy, gh), what[c], (int)strlen(what[c]), C(COL_TEXT_FAINT));
        snprintf(buf, sizeof buf, "%04o", (unsigned)(s->st.st_mode & 07777));
        f32 chw = roundf(text_width(fm, buf, 4) + S(16)), chh = roundf(S(22));
        Rect chip = {roundf(x + cw - S(12) - chw), roundf(gy + (gh - chh) * 0.5f), chw, chh};
        draw_rrect(chip.x, chip.y, chip.w, chip.h, chh * 0.5f, C(COL_CHIP));
        draw_text(fm, chip.x + S(8), text_y(fm, chip.y, chip.h), buf, 4, C(COL_TEXT2));
        for (int rr = 0; rr < 3; rr++) {
            f32 yy = gy + (rr + 1) * gh;
            draw_text(fs, x + px, text_y(fs, yy, gh), who[rr], (int)strlen(who[rr]), C(COL_TEXT_DIM));
            for (int c = 0; c < 3; c++) {
                int k = rr * 3 + c;
                s->rperm[k] = (Rect){x + lw + c * colw, roundf(yy + (gh - S(20)) * 0.5f), roundf(S(20)), roundf(S(20))};
                sheet_check(s->rperm[k], "", s->st.st_mode & (0400 >> k));
            }
        }
        y += rh * 2 + grid + gap;

        if (!isdir) {
            // Opens with: app icon + name, Change... on the right
            y = props_heading(x, y, "Opens with");
            props_card(x, y, cw, 1, rh + S(8), 0);
            DesktopApp *a = app_default_for(s->mime);
            f32 rr_h = rh + S(8), ais = roundf(S(24)), aix = x + roundf(S(14)), aiy = roundf(y + (rr_h - ais) * 0.5f);
            SDL_Texture *t = a ? app_icon_tex(a->icon, (int)ais) : NULL;
            if (t) { draw_flush(); SDL_RenderTexture(A.ren, t, NULL, &(SDL_FRect){aix, aiy, ais, ais}); }
            else draw_rrect(aix, aiy, ais, ais, S(6), C(COL_CHIP));
            const char *an = a ? a->name : "No default app";
            Font *bf = &A.font;
            f32 bw = roundf(text_width(bf, "Change\xE2\x80\xA6", 9) + S(24)), bh2 = roundf(S(26));
            s->rchange = (Rect){roundf(x + cw - S(8) - bw), roundf(y + (rr_h - bh2) * 0.5f), bw, bh2};
            sheet_button(s->rchange, "Change\xE2\x80\xA6", false, true);
            draw_text_fit(f, aix + ais + S(10), text_y(f, y, rr_h), s->rchange.x - S(10) - (aix + ais + S(10)), an, (int)strlen(an),
                          C(a ? COL_TEXT : COL_TEXT_DIM), 0);
            y += rr_h + gap;

            // Checksums
            y = props_heading(x, y, "Checksums");
            if (!s->sum) {
                props_card(x, y, cw, 1, rh, 0);
                draw_text(fs, x + S(14), text_y(fs, y, rh), "MD5 and SHA-256", 15, C(COL_TEXT_DIM));
                Font *bf2 = &A.font;
                f32 bw2 = roundf(text_width(bf2, "Calculate", 9) + S(24));
                s->rsum = (Rect){roundf(x + cw - S(8) - bw2), roundf(y + (rh - S(26)) * 0.5f), bw2, roundf(S(26))};
                sheet_button(s->rsum, "Calculate", false, true);
                y += rh;
            } else if (!atomic_load(&s->sum->done)) {
                u64 tot = atomic_load(&s->sum->total), pr = atomic_load(&s->sum->progress);
                props_card(x, y, cw, 1, rh, 0);
                snprintf(buf, sizeof buf, "Calculating\xE2\x80\xA6 %d%%", tot ? (int)(pr * 100 / tot) : 0);
                props_line(x, y, cw, rh, "Progress", buf, fs, NULL, NULL);
                y += rh;
            } else {
                props_card(x, y, cw, 2, rh, 0);
                props_line(x, y, cw, rh, "MD5", s->sum->md5, fm, "Copy", &s->rcopy_md5);
                props_line(x, y + rh, cw, rh, "SHA-256", s->sum->sha256, fm, "Copy", &s->rcopy_sha);
                y += rh * 2;
            }
            y += gap;
        }
    }
    if (s->snap) { // Previous versions: one row per version kept in a snapshot, newest first
        y = props_heading(x, y, "Previous versions");
        SnapJob *sj = s->snap;
        if (!atomic_load(&sj->done) || !sj->n) {
            const char *m = !atomic_load(&sj->done) ? "Looking for snapshots\xE2\x80\xA6"
                          : sj->note[0] ? sj->note : "No snapshots of it (Snapper, Timeshift or ZFS)";
            bool adm = atomic_load(&sj->done) && sj->noaccess && !sj->admin; // only root can read them: offer to look as root
            const char *al = "Show as administrator";
            f32 aw = adm ? roundf(text_width(f, al, (int)strlen(al)) + S(24)) : 0;
            int st2[4], ln2[4];
            int nl2 = wrap_text(fs, m, cw - S(28) - (adm ? aw + S(10) : 0), st2, ln2, 4);
            f32 ch = MAX(rh + (adm ? S(8) : 0), nl2 * fs->line_h + S(18));
            props_card(x, y, cw, 1, ch, 0);
            f32 ty0 = y + roundf((ch - nl2 * fs->line_h) * 0.5f);
            for (int i = 0; i < nl2; i++) draw_text(fs, x + S(14), ty0 + i * fs->line_h, m + st2[i], ln2[i], C(COL_TEXT_DIM));
            if (adm) {
                s->rsv_admin = (Rect){roundf(x + cw - S(8) - aw), roundf(y + (ch - S(26)) * 0.5f), aw, roundf(S(26))};
                sheet_button(s->rsv_admin, al, false, true);
            }
            y += ch + gap;
        } else {
            int nr = MIN(sj->n, 8);
            f32 vr = roundf(f->line_h + fs->line_h + S(18));
            props_card(x, y, cw, nr + (sj->n > nr), vr, 0);
            for (int i = 0; i < nr; i++) {
                SnapVer *v = &sj->v[i];
                f32 ry = y + i * vr, bh2 = roundf(S(26)), by2 = roundf(ry + (vr - bh2) * 0.5f);
                const char *a2 = v->dir ? "Compare" : "Restore\xE2\x80\xA6";
                f32 w2 = roundf(text_width(f, a2, (int)strlen(a2)) + S(24)), w1 = roundf(text_width(f, "Open", 4) + S(24));
                s->rsv_act[i] = (Rect){roundf(x + cw - S(8) - w2), by2, w2, bh2};
                s->rsv_open[i] = (Rect){s->rsv_act[i].x - S(6) - w1, by2, w1, bh2};
                sheet_button(s->rsv_open[i], "Open", false, !(v->dir && sj->admin));
                sheet_button(s->rsv_act[i], a2, false, v->dir ? !sj->admin : !v->same_as_now);
                char d1[64], l1[160], l2[300], sz[32] = "";
                if (v->when) props_date(v->when, d1, sizeof d1);
                else snprintf(d1, sizeof d1, "Unknown date");
                snprintf(l1, sizeof l1, "%s%s", d1, v->same_as_now ? "  \xC2\xB7  same as now" : "");
                if (!v->dir) fmt_size(sz, sizeof sz, v->size);
                char mt[64];
                props_date(v->mtime, mt, sizeof mt);
                size_t o = (size_t)snprintf(l2, sizeof l2, "%s", v->label);
                if (sz[0]) o += (size_t)snprintf(l2 + o, sizeof l2 - o, "  \xC2\xB7  %s", sz);
                o += (size_t)snprintf(l2 + o, sizeof l2 - o, "  \xC2\xB7  modified %s", mt);
                if (v->also) snprintf(l2 + o, sizeof l2 - o, "  \xC2\xB7  also in %d older snapshot%s", v->also, v->also == 1 ? "" : "s");
                f32 tw = s->rsv_open[i].x - S(10) - (x + S(14));
                draw_text_fit(f, x + S(14), ry + S(8), tw, l1, (int)strlen(l1), C(v->same_as_now ? COL_TEXT_DIM : COL_TEXT), 0);
                draw_text_fit(fs, x + S(14), ry + S(9) + f->line_h, tw, l2, (int)strlen(l2), C(COL_TEXT_DIM), 0);
            }
            if (sj->n > nr) {
                snprintf(buf, sizeof buf, "and %d older version%s", sj->n - nr, sj->n - nr == 1 ? "" : "s");
                draw_text(fs, x + S(14), text_y(fs, y + nr * vr, vr), buf, (int)strlen(buf), C(COL_TEXT_DIM));
            }
            y += (nr + (sj->n > nr)) * vr + gap;
        }
    }
    s->pv_content = y - gap + roundf(S(4)) - y0;
    draw_clip_pop(cs);
    sheet_fade_edges(s->rbody, s->pv_scroll, MAX(0.f, s->pv_content - s->rbody.h), pad);
    if (s->pv_content > s->rbody.h) { // scrollbar along the dialog's right edge
        f32 maxs = s->pv_content - s->rbody.h, tw = roundf(S(4)), th = MAX(S(24), s->rbody.h * s->rbody.h / s->pv_content);
        f32 ty = s->rbody.y + (s->rbody.h - th) * (s->pv_scroll / maxs);
        draw_rrect(r.x + w - S(8) - tw * 0.5f, ty, tw, th, tw * 0.5f, C(COL_SCROLL));
    }
    f32 bw = roundf(S(96));
    s->rclose = (Rect){r.x + w - pad - bw, r.y + h - pad - bh, bw, bh};
    sheet_button(s->rclose, "Close", true, true);
}

// --- Batch rename -----------------------------------------------------------

static void str_replace_all(const char *src, const char *find, const char *rep, bool match_case, char *out, size_t cap) {
    size_t fl = strlen(find), o = 0;
    if (!fl) { snprintf(out, cap, "%s", src); return; }
    for (const char *p = src; *p && o + 1 < cap;) {
        bool hit = match_case ? !strncmp(p, find, fl) : !strncasecmp(p, find, fl);
        if (hit) {
            for (const char *q = rep; *q && o + 1 < cap; q++) out[o++] = *q;
            p += fl;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = 0;
}

// Expand the template for item i: {name} {ext} {n} {n:3} {date} {parent} {orig}
static void rename_expand(int i, char *out, size_t cap) {
    Sheet *s = &g_sheet;
    const char *orig = path_basename(s->paths[i]);
    struct stat st;
    bool ok = lstat(s->paths[i], &st) == 0;
    bool dir = ok && S_ISDIR(st.st_mode);
    const char *dot = dir ? NULL : strrchr(orig, '.');
    if (dot == orig) dot = NULL;
    int stem = dot ? (int)(dot - orig) : (int)strlen(orig);
    char par[PATH_MAX];
    parent_of(s->paths[i], par, sizeof par);
    size_t o = 0;
    for (const char *p = s->tmpl.buf; *p && o + 1 < cap;) {
        char tok[32] = "";
        if (*p == '{') {
            const char *e = strchr(p, '}');
            if (e && e - p < 30) {
                snprintf(tok, sizeof tok, "%.*s", (int)(e - p - 1), p + 1);
                p = e + 1;
            }
        }
        if (!tok[0] && *p) { out[o++] = *p++; continue; }
        char rep[NAME_MAX + 1] = "";
        if (!strcmp(tok, "name")) snprintf(rep, sizeof rep, "%.*s", stem, orig);
        else if (!strcmp(tok, "ext")) snprintf(rep, sizeof rep, "%s", dot ? dot : "");
        else if (!strcmp(tok, "orig")) snprintf(rep, sizeof rep, "%s", orig);
        else if (!strcmp(tok, "parent")) snprintf(rep, sizeof rep, "%s", par[1] ? path_basename(par) : "");
        else if (!strcmp(tok, "date") || !strcmp(tok, "time")) {
            struct tm tm;
            time_t t = ok ? st.st_mtime : 0;
            localtime_r(&t, &tm);
            strftime(rep, sizeof rep, !strcmp(tok, "date") ? "%Y-%m-%d" : "%H%M%S", &tm);
        } else if (!strncmp(tok, "n", 1) && (!tok[1] || tok[1] == ':')) {
            int pad = tok[1] == ':' ? atoi(tok + 2) : 0;
            snprintf(rep, sizeof rep, "%0*d", CLAMP(pad, 0, 12), s->num_start + i);
        } else {
            snprintf(rep, sizeof rep, "{%s}", tok); // unknown token: keep it
        }
        for (const char *q = rep; *q && o + 1 < cap; q++) out[o++] = *q;
    }
    out[o] = 0;
}

static void rename_recompute(void) {
    Sheet *s = &g_sheet;
    s->nconflicts = s->nchanges = 0;
    for (int i = 0; i < s->npaths; i++) {
        char a[NAME_MAX * 2 + 1], b[NAME_MAX * 2 + 1];
        rename_expand(i, a, sizeof a);
        str_replace_all(a, s->find.buf, s->repl.buf, s->match_case, b, sizeof b);
        if (s->case_mode) {
            bool start = true;
            for (char *q = b; *q; q++) {
                u8 c = (u8)*q;
                if (s->case_mode == 1) *q = (char)ascii_lower(c);
                else if (s->case_mode == 2) *q = (char)toupper(c);
                else { *q = (char)(start ? toupper(c) : ascii_lower(c)); start = c == ' ' || c == '_' || c == '-' || c == '.'; }
            }
        }
        free(s->newnames[i]);
        s->newnames[i] = strdup(b);
        s->status[i] = strcmp(b, path_basename(s->paths[i])) ? 1 : 0;
        s->nchanges += s->status[i] == 1;
    }
    // conflicts: invalid names, duplicates within the set, or an existing file outside the set
    for (int i = 0; i < s->npaths; i++) {
        const char *nn = s->newnames[i];
        bool bad = !nn[0] || strchr(nn, '/') || !strcmp(nn, ".") || !strcmp(nn, "..") || strlen(nn) > NAME_MAX;
        for (int k = 0; k < s->npaths && !bad; k++) {
            if (k == i) continue;
            char pi[PATH_MAX], pk[PATH_MAX];
            parent_of(s->paths[i], pi, sizeof pi);
            parent_of(s->paths[k], pk, sizeof pk);
            if (!strcmp(pi, pk) && !strcmp(nn, s->newnames[k])) bad = true;
        }
        if (!bad && s->status[i]) {
            char par[PATH_MAX], np[PATH_MAX];
            parent_of(s->paths[i], par, sizeof par);
            path_join(np, sizeof np, par, nn);
            struct stat st;
            if (lstat(np, &st) == 0) { // exists: fine only if it's one of the items being renamed away
                bool ours = false;
                for (int k = 0; k < s->npaths; k++) ours |= !strcmp(s->paths[k], np) && s->status[k];
                bad = !ours;
            }
        }
        if (bad) { s->status[i] = 2; s->nconflicts++; }
    }
}

static void rename_open(char **paths, int n, Pane *p) {
    if (n < 1) return;
    sheet_close();
    Sheet *s = &g_sheet;
    s->kind = SHEET_RENAME;
    s->paths = strv_dup(paths, n);
    s->npaths = n;
    s->pane = p;
    s->newnames = xcalloc((size_t)n, sizeof(char *));
    s->status = xcalloc((size_t)n, 1);
    te_set(&s->tmpl, "{name}{ext}", 0, 6); // "{name}" pre-selected
    te_set(&s->find, "", 0, 0);
    te_set(&s->repl, "", 0, 0);
    s->num_start = 1;
    rename_recompute();
    A.redraw = true;
}

// Rename a set of paths in two phases (via temporary names) so swaps and chains work.
static int rename_many(char **from, char **to, int n) {
    char **tmp = xcalloc((size_t)n, sizeof(char *));
    int done = 0, i;
    for (i = 0; i < n; i++) {
        char par[PATH_MAX], t[PATH_MAX + 64];
        parent_of(from[i], par, sizeof par);
        snprintf(t, sizeof t, "%s/.fe-rename-%d-%d", par, (int)getpid(), i);
        if (rename(from[i], t)) break;
        tmp[i] = strdup(t);
    }
    if (i < n) { // undo phase 1
        int e = errno;
        for (int k = i - 1; k >= 0; k--) rename(tmp[k], from[k]);
        strv_free(tmp, n);
        toast("Rename failed: %s", strerror(e));
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (rename_noreplace(AT_FDCWD, tmp[i], AT_FDCWD, to[i]) == 0) done++;
        else rename(tmp[i], from[i]); // put it back under its old name
    }
    strv_free(tmp, n);
    return done;
}

static void rename_apply(void) {
    Sheet *s = &g_sheet;
    if (s->nconflicts || !s->nchanges) return;
    char **from = xmalloc((size_t)s->nchanges * sizeof(char *)), **to = xmalloc((size_t)s->nchanges * sizeof(char *));
    int n = 0;
    for (int i = 0; i < s->npaths; i++) {
        if (s->status[i] != 1) continue;
        char par[PATH_MAX], np[PATH_MAX];
        parent_of(s->paths[i], par, sizeof par);
        path_join(np, sizeof np, par, s->newnames[i]);
        from[n] = strdup(s->paths[i]);
        to[n] = strdup(np);
        n++;
    }
    int done = rename_many(from, to, n);
    if (done == n) {
        undo_push(U_RENAME, from, to, NULL, n);
        toast("Renamed %d item%s", n, n == 1 ? "" : "s");
    } else if (done) {
        toast("Renamed %d of %d items", done, n);
    }
    refresh_after(to, n, NULL, 0);
    strv_free(from, n);
    strv_free(to, n);
    sheet_close();
}

static void rename_draw(void) {
    Sheet *s = &g_sheet;
    Font *f = &A.font, *fs = &A.font_small;
    f32 w = roundf(MIN(S(720), A.W - S(40))), h = roundf(MIN(S(620), A.H - S(40)));
    Rect r = {roundf((A.W - w) * 0.5f), roundf((A.H - h) * 0.42f), w, h};
    s->r = r;
    char title[64];
    snprintf(title, sizeof title, "Rename %d items", s->npaths);
    sheet_frame(r, title, "Tokens: {name} {ext} {n} {n:3} {date} {time} {parent} {orig}");
    f32 pad = roundf(S(20)), x = r.x + pad, y = r.y + pad + roundf(S(52)), lw = roundf(S(104)), fh = roundf(S(30));
    static const char *labels[3] = {"Pattern", "Find / Replace", NULL};
    TextEdit *tes[3] = {&s->tmpl, &s->find, &s->repl};
    f32 fw = (w - pad * 2 - lw);
    for (int i = 0; i < 3; i++) {
        f32 yy = y + i * (fh + S(8));
        if (labels[i]) draw_text_fit(fs, x, text_y(fs, yy, fh), lw - S(6), labels[i], (int)strlen(labels[i]), C(COL_TEXT_DIM), 0);
        f32 ww = i == 0 ? fw : (fw - S(8)) * 0.5f;
        f32 xx = x + lw + (i == 2 ? (fw + S(8)) * 0.5f : 0);
        f32 yy2 = i == 2 ? y + (fh + S(8)) : yy;
        s->rfield[i] = (Rect){xx, yy2, ww, fh};
        sheet_field(s->rfield[i], tes[i], s->focus == i, i == 1 ? "text to find" : i == 2 ? "replace with" : NULL);
    }
    y += 2 * (fh + S(8));
    // options row: case, match case, number start
    static const char *cases[4] = {"Keep case", "lower", "UPPER", "Title"};
    f32 cx = x + lw, bw = roundf(S(80)), bh = roundf(S(26));
    draw_text(fs, x, text_y(fs, y, bh), "Options", 7, C(COL_TEXT_DIM));
    for (int i = 0; i < 4; i++) {
        s->rcase[i] = (Rect){cx + i * (bw + S(4)), y, bw, bh};
        bool on = s->case_mode == i, hv = hot(s->rcase[i]);
        draw_rrect(s->rcase[i].x, y, bw, bh, S(6), on ? CA(COL_ACCENT, 0.3f) : C(hv ? COL_CONTROL_HOVER : COL_CONTROL));
        f32 tw = text_width(fs, cases[i], (int)strlen(cases[i]));
        draw_text(fs, s->rcase[i].x + (bw - tw) * 0.5f, text_y(fs, y, bh), cases[i], (int)strlen(cases[i]), on ? C(COL_ACCENT) : C(COL_TEXT));
    }
    f32 ox = cx + 4 * (bw + S(4)) + S(12);
    s->rmatch = (Rect){ox, y, roundf(S(110)), bh};
    sheet_check(s->rmatch, "Match case", s->match_case);
    f32 nx = ox + roundf(S(120));
    char nb[48];
    snprintf(nb, sizeof nb, "{n} from %d", s->num_start);
    draw_text(fs, nx, text_y(fs, y, bh), nb, (int)strlen(nb), C(COL_TEXT_DIM));
    f32 nbx = nx + text_width(fs, nb, (int)strlen(nb)) + S(8);
    s->rnum_minus = (Rect){nbx, y, bh, bh};
    s->rnum_plus = (Rect){nbx + bh + S(4), y, bh, bh};
    sheet_button(s->rnum_minus, "\xE2\x88\x92", false, s->num_start > 0);
    sheet_button(s->rnum_plus, "+", false, true);
    y += bh + roundf(S(14));
    // preview list
    f32 foot = roundf(S(32)) + pad * 2;
    Rect lr = {r.x + pad * 0.5f, y, w - pad, r.y + h - foot - y};
    draw_rrect(lr.x, lr.y, lr.w, lr.h, S(8), C(COL_SUNKEN));
    ClipSave cs = draw_clip_push((SDL_Rect){(int)lr.x + 2, (int)lr.y + 2, (int)lr.w - 4, (int)lr.h - 4});
    f32 rh = roundf(S(24)), half = (lr.w - S(40)) * 0.5f;
    int vis = (int)(lr.h / rh);
    s->top = CLAMP(s->top, 0, MAX(0, s->npaths - vis));
    for (int i = s->top; i < s->npaths && i < s->top + vis + 1; i++) {
        f32 yy = lr.y + S(4) + (i - s->top) * rh;
        const char *old = path_basename(s->paths[i]);
        draw_text_fit(f, lr.x + S(12), text_y(f, yy, rh), half - S(12), old, (int)strlen(old), C(COL_TEXT_DIM), 0);
        draw_chevron(lr.x + S(12) + half + S(8), yy + rh * 0.5f, S(6), 1, MAX(1.f, S(1.3f)), C(COL_TEXT_FAINT));
        Color nc = s->status[i] == 2 ? C(COL_DANGER) : s->status[i] == 1 ? C(COL_TEXT_STRONG) : C(COL_TEXT_FAINT);
        draw_text_fit(f, lr.x + S(28) + half, text_y(f, yy, rh), half - S(8), s->newnames[i], (int)strlen(s->newnames[i]), nc, 0);
    }
    draw_clip_pop(cs);
    // footer
    char st[96];
    if (s->nconflicts) snprintf(st, sizeof st, "%d conflict%s (duplicate, invalid, or already exists)", s->nconflicts, s->nconflicts == 1 ? "" : "s");
    else snprintf(st, sizeof st, "%d of %d will be renamed", s->nchanges, s->npaths);
    f32 by = r.y + h - pad - roundf(S(32));
    draw_text(fs, x, text_y(fs, by, S(32)), st, (int)strlen(st), C(s->nconflicts ? COL_DANGER_TEXT : COL_TEXT_DIM));
    f32 bw2 = roundf(S(96));
    s->rapply = (Rect){r.x + w - pad - bw2, by, bw2, roundf(S(32))};
    s->rcancel = (Rect){s->rapply.x - S(8) - bw2, by, bw2, roundf(S(32))};
    sheet_button(s->rcancel, "Cancel", false, true);
    sheet_button(s->rapply, "Rename", true, !s->nconflicts && s->nchanges > 0);
}

// --- Connect to server ---------------------------------------------------------------------------------

static bool cn_field_visible(int f) {
    if (f == CN_NAME) return true;
    const char *sch = g_net_protos[g_sheet.cn_proto].scheme;
    if (f == CN_DOMAIN) return !strcmp(sch, "smb");
    if (f == CN_USER || f == CN_PASS) return strcmp(sch, "nfs") != 0; // NFS: access by host
    return true;
}

static void cn_addr(NetAddr *a) {
    Sheet *s = &g_sheet;
    memset(a, 0, sizeof *a);
    a->proto = s->cn_proto;
    snprintf(a->host, sizeof a->host, "%s", s->cn[CN_HOST].buf);
    snprintf(a->port, sizeof a->port, "%s", s->cn[CN_PORT].buf);
    snprintf(a->path, sizeof a->path, "%s", s->cn[CN_PATH].buf);
    if (cn_field_visible(CN_USER)) snprintf(a->user, sizeof a->user, "%s", s->cn[CN_USER].buf);
    if (cn_field_visible(CN_DOMAIN)) snprintf(a->domain, sizeof a->domain, "%s", s->cn[CN_DOMAIN].buf);
    // "host:port" or "host/path" typed into the server field
    char *sl = strchr(a->host, '/');
    if (sl) { if (!a->path[0]) snprintf(a->path, sizeof a->path, "%s", sl + 1); *sl = 0; }
    char *co = strchr(a->host, ':');
    if (co && co == strrchr(a->host, ':') && a->host[0] != '[') { // exactly one ':' (not an IPv6 address)
        if (!a->port[0]) snprintf(a->port, sizeof a->port, "%.6s", co + 1);
        *co = 0;
    }
}

static void cn_fill(const NetAddr *a) {
    Sheet *s = &g_sheet;
    s->cn_proto = a->proto;
    const char *v[CN_COUNT] = {a->host, a->port, a->path, a->user, "", a->domain};
    for (int f = 0; f < CN_COUNT; f++) te_set(&s->cn[f], v[f], (int)strlen(v[f]), (int)strlen(v[f]));
}

// saved: index into A.servers to prefill (password focused), -1 = empty form
static void cn_name(const NetAddr *a, char *name, size_t cap);

static void connect_open(int saved) {
    sheet_close();
    Sheet *s = &g_sheet;
    s->kind = SHEET_CONNECT;
    s->cn_remember = true;
    s->cn_edit = -1;
    s->cn_icon[0] = 0;
    s->cn_pick = false;
    for (int f = 0; f < CN_ALL; f++) te_set(&s->cn[f], "", 0, 0);
    NetAddr a;
    if (saved >= 0 && saved < A.nservers && net_parse(A.servers[saved].uri, &a)) { // edit a saved server
        cn_fill(&a);
        // a name made from the address stays automatic (empty field: follows address edits); a chosen one is shown
        char auto_name[64];
        cn_name(&a, auto_name, sizeof auto_name); // (Name is still empty here)
        const char *nm = A.servers[saved].name;
        if (strcmp(nm, auto_name)) te_set(&s->cn[CN_NAME], nm, (int)strlen(nm), (int)strlen(nm));
        snprintf(s->cn_icon, sizeof s->cn_icon, "%s", A.servers[saved].icon);
        s->cn_edit = saved;
        s->cn_focus = CN_HOST;
    }
    A.redraw = true;
}

// An address typed in the path bar: the dialog prefilled (not remembered unless ticked), password focused.
static void connect_open_addr(const NetAddr *a) {
    connect_open(-1);
    cn_fill(a);
    g_sheet.cn_remember = false;
    g_sheet.cn_focus = cn_field_visible(CN_PASS) ? CN_PASS : CN_HOST;
}

// The display name: what was typed in Name, else made from the address.
static void cn_name(const NetAddr *a, char *name, size_t cap) {
    const char *typed = g_sheet.cn[CN_NAME].buf;
    while (*typed == ' ') typed++;
    if (typed[0]) { snprintf(name, cap, "%s", typed); for (char *c = name; *c; c++) if (*c == '\t' || *c == '\n') *c = ' '; return; }
    if (a->path[0]) snprintf(name, cap, "%.40s/%.20s", a->host, a->path);
    else snprintf(name, cap, "%.63s", a->host);
}

// Edit mode: write the form back into the saved server (no connection). A typed password is kept only if ticked.
static bool connect_save_edit(void) {
    Sheet *s = &g_sheet;
    NetAddr a;
    cn_addr(&a);
    if (s->cn_edit < 0 || s->cn_edit >= A.nservers || !a.host[0]) return false;
    char uri[1024], name[64];
    net_uri(&a, uri, sizeof uri);
    cn_name(&a, name, sizeof name);
    net_server_update(s->cn_edit, name, uri, s->cn_icon);
    if (s->cn_remember_pw && s->cn[CN_PASS].buf[0] && secret_available() && cn_field_visible(CN_PASS))
        secret_put_async(uri, s->cn[CN_PASS].buf);
    return true;
}

static void connect_go(void) {
    Sheet *s = &g_sheet;
    NetAddr a;
    cn_addr(&a);
    if (!a.host[0] || !net_proto_available(s->cn_proto)) return;
    char uri[1024];
    net_uri(&a, uri, sizeof uri);
    if (s->cn_edit >= 0) { // a saved server: update it, connect with its keyring password (or the one typed now)
        int i = s->cn_edit;
        bool typed = cn_field_visible(CN_PASS) && s->cn[CN_PASS].buf[0];
        bool keep = s->cn_remember_pw && typed && secret_available();
        char pw[256];
        snprintf(pw, sizeof pw, "%s", typed ? s->cn[CN_PASS].buf : "");
        s->cn_remember_pw = false; // stored after a successful login instead
        connect_save_edit();
        toast("Connecting to %s\xE2\x80\xA6", A.servers[i].name);
        dev_start_net(A.servers[i].uri, a.user, a.domain, pw, !typed, keep);
        explicit_bzero(pw, sizeof pw);
        sheet_close();
        init_places();
        return;
    }
    if (s->cn_remember || s->cn_remember_pw) {
        char name[64];
        cn_name(&a, name, sizeof name);
        net_server_save(name, uri, s->cn_icon);
    }
    toast("Connecting to %s\xE2\x80\xA6", a.host);
    bool keep = s->cn_remember_pw && cn_field_visible(CN_PASS) && secret_available();
    dev_start_net(uri, a.user, a.domain, cn_field_visible(CN_PASS) ? s->cn[CN_PASS].buf : "", false, keep);
    sheet_close();
    init_places();
}

// The icon currently chosen in the dialog ("" = the sheet's default: globe for servers, an add-on sheet's own).
static const char *cn_icon_key(void) { return g_sheet.cn_icon[0] ? g_sheet.cn_icon : g_sheet.kind == SHEET_ADDON ? addon_sheet_icon() : ""; }
static const char *cn_icon_default(void) { return g_sheet.kind == SHEET_ADDON ? addon_sheet_icon() : NET_ICON_DEFAULT; }

// The icon picker: a popover under the Display icon button with the built-in icons by group, then the custom
// ones (net-*.svg in the icon themes folder); the hovered icon's name in its footer.
static void connect_icon_picker_draw(void) {
    Sheet *s = &g_sheet;
    Font *fs = &A.font_small;
    Rect ib = s->cn_ricon;
    f32 pad = roundf(S(10)), cs = roundf(S(38)), hh = roundf(fs->line_h + S(10)), foot = roundf(S(34));
    f32 w = ib.w, gw = w - pad * 2;
    int cols = MAX(1, (int)(gw / cs));
    cs = floorf(gw / cols);
    int ngroups = (int)ARRAY_COUNT(g_net_icon_groups);
    // content height: each group's header + rows (custom: its icons or a hint line)
    f32 ch = 0;
    for (int g = 0; g <= ngroups; g++) {
        int n = 0;
        for (int i = 0; i < net_icon_count(); i++) n += (i < NET_ICON_BUILTIN ? g_net_icons[i].group : ngroups) == g;
        ch += hh + (g == ngroups && !n ? roundf(fs->line_h * 2 + S(8)) : ((n + cols - 1) / cols) * cs) + roundf(S(4));
    }
    s->cn_pick_content = ch;
    f32 below = A.H - (ib.y + ib.h + S(6)) - S(12), above = ib.y - S(6) - S(12);
    f32 want = MIN(ch + foot + pad, roundf(S(420)));
    f32 h = MIN(want, MAX(below, above));
    Rect r = {ib.x, below >= want || below >= above ? ib.y + ib.h + S(6) : ib.y - S(6) - h, w, h};
    s->cn_rpick = r;
    draw_rrect(r.x, r.y, r.w, r.h, S(10), C(COL_POPUP));
    draw_rrect_outline(r.x, r.y, r.w, r.h, S(10), 1, C(rgb_mix(COL_POPUP, COL_TEXT, 0.18f)));
    Rect body = {r.x, r.y + S(4), r.w, r.h - foot - S(4)};
    s->cn_rpick_body = body;
    s->cn_pick_scroll = CLAMP(s->cn_pick_scroll, 0, MAX(0.f, ch - body.h + S(4)));
    ClipSave cl = draw_clip_push((SDL_Rect){(int)body.x, (int)body.y, (int)body.w, (int)body.h});
    int sel = net_icon_find(cn_icon_key()), hov = -1;
    s->cn_ncell = 0;
    f32 y = body.y - s->cn_pick_scroll;
    f32 is = roundf(S(22));
    for (int g = 0; g <= ngroups; g++) {
        const char *gt = g < ngroups ? g_net_icon_groups[g] : "Custom";
        draw_text(fs, r.x + pad + S(2), y + S(4), gt, (int)strlen(gt), C(COL_TEXT_FAINT));
        y += hh;
        int k = 0;
        for (int i = 0; i < net_icon_count(); i++) {
            if ((i < NET_ICON_BUILTIN ? g_net_icons[i].group : ngroups) != g) continue;
            Rect c = {r.x + pad + (k % cols) * cs, y + (k / cols) * cs, cs, cs};
            k++;
            bool vis = c.y + c.h > body.y && c.y < body.y + body.h;
            if (vis && s->cn_ncell < (int)ARRAY_COUNT(s->cn_rcell)) { s->cn_rcell[s->cn_ncell] = c; s->cn_cell_idx[s->cn_ncell++] = i; }
            if (!vis) continue;
            bool h = hand_if(A.mouse_in && in_rect(body, A.mx, A.my) && in_rect(c, A.mx, A.my));
            if (h) hov = i;
            f32 in = roundf(S(2));
            if (i == sel) {
                draw_rrect(c.x + in, c.y + in, c.w - in * 2, c.h - in * 2, S(7), CA(COL_ACCENT, 0.22f));
                draw_rrect_outline(c.x + in, c.y + in, c.w - in * 2, c.h - in * 2, S(7), MAX(1.f, S(1.3f)), C(COL_ACCENT));
            } else if (h) draw_rrect(c.x + in, c.y + in, c.w - in * 2, c.h - in * 2, S(7), C(COL_HOVER));
            Glyph *gl = icon_glyph(ICON_NET_BASE + i, (int)is);
            if (gl) draw_glyph(gl, roundf(c.x + (c.w - is) * 0.5f), roundf(c.y + (c.h - is) * 0.5f), (Color){1, 1, 1, 1});
        }
        if (g == ngroups && !k) { // no custom icons yet: how to add them
            const char *h1 = "Add your own: net-<name>.svg files in a folder of", *h2 = "the icon themes folder (Options > Appearance).";
            draw_text_fit(fs, r.x + pad + S(2), y, gw, h1, (int)strlen(h1), C(COL_TEXT_DIM), 0);
            draw_text_fit(fs, r.x + pad + S(2), y + fs->line_h, gw, h2, (int)strlen(h2), C(COL_TEXT_DIM), 0);
            y += roundf(fs->line_h * 2 + S(8));
        } else y += ((k + cols - 1) / cols) * cs;
        y += roundf(S(4));
    }
    draw_clip_pop(cl);
    // footer: the hovered icon's name (else the selected one's)
    f32 fy = r.y + r.h - foot;
    draw_rect(r.x + S(10), fy, r.w - S(20), 1, C(rgb_mix(COL_POPUP, COL_TEXT, 0.10f)));
    int show = hov >= 0 ? hov : sel;
    const char *lbl = net_icon_label(show);
    char line[160];
    if (show >= NET_ICON_BUILTIN) snprintf(line, sizeof line, "%s  \xC2\xB7  %s", lbl, g_net_custom[show - NET_ICON_BUILTIN].theme);
    else snprintf(line, sizeof line, "%s", lbl);
    draw_text_fit(fs, r.x + pad + S(2), text_y(fs, fy, foot), gw, line, (int)strlen(line), C(hov >= 0 ? COL_TEXT : COL_TEXT_DIM), 0);
}

// "Display icon" row (Connect dialog, add-on sheets): a button showing the current icon; opens the picker.
static void sheet_icon_row(f32 x, f32 y, f32 cw, f32 rh, f32 lw0) {
    Sheet *s = &g_sheet;
    Font *f = &A.font, *fs = &A.font_small;
    f32 fh0 = roundf(S(30));
    draw_text(fs, x + S(14), text_y(fs, y, rh), "Display icon", 12, C(COL_TEXT_DIM));
    Rect ib = {x + lw0, roundf(y + (rh - fh0) * 0.5f), cw - lw0 - S(10), fh0};
    s->cn_ricon = ib;
    bool ih = hot(ib) || s->cn_pick;
    draw_rrect(ib.x, ib.y, ib.w, ib.h, S(6), C(ih ? COL_FIELD_HOVER : COL_FIELD));
    if (s->cn_pick) draw_rrect_outline(ib.x, ib.y, ib.w, ib.h, S(6), MAX(1.f, S(1.2f)), C(COL_ACCENT));
    f32 is0 = roundf(S(18));
    draw_net_icon(ib.x + S(8), ib.y + (ib.h - is0) * 0.5f, is0, cn_icon_key(), 1.f);
    const char *il = net_icon_label(net_icon_find(cn_icon_key()));
    draw_text_fit(f, ib.x + S(8) + is0 + S(8), text_y(f, ib.y, ib.h), ib.w - is0 - S(48), il, (int)strlen(il), C(COL_TEXT), 0);
    draw_chevron(ib.x + ib.w - S(16), ib.y + ib.h * 0.5f, S(9), s->cn_pick ? 2 : 3, MAX(1.f, S(1.3f)), C(COL_TEXT_DIM));
}

static void connect_draw(void) {
    Sheet *s = &g_sheet;
    Font *f = &A.font, *fs = &A.font_small;
    int nsv = 0, nvis = 0; // (saved servers are edited from the sidebar: right-click > Edit)
    for (int k = 0; k < CN_COUNT; k++) nvis += cn_field_visible(k) && k != CN_PORT; // port shares the server row
    f32 pad = roundf(S(20)), rh = roundf(S(44)), srh = roundf(S(40)), gap = roundf(S(16)), hh = roundf(fs->line_h + S(6));
    f32 h = pad + roundf(S(62)) + (nsv ? hh + nsv * srh + gap : 0) + hh + roundf(S(32)) + gap + hh + nvis * rh + gap +
            roundf(S(48)) + (cn_field_visible(CN_PASS) && s->cn_edit < 0 ? roundf(S(28)) : 0) + roundf(S(32)) + pad +
            hh + rh * 2 + gap; // Name block (display name + icon)
    f32 w = roundf(MIN(S(560), A.W - S(40)));
    h = MIN(h, A.H - S(40));
    Rect r = {roundf((A.W - w) * 0.5f), roundf((A.H - h) * 0.42f), w, h};
    s->r = r;
    if (s->cn_edit >= 0 && s->cn_edit < A.nservers) sheet_frame(r, "Edit server", A.servers[s->cn_edit].name);
    else sheet_frame(r, "Connect to server", "Files on another computer, NAS or server");
    f32 x = r.x + pad, cw = w - pad * 2, y = r.y + pad + roundf(S(62));
    // saved servers
    for (int k = 0; k < 8; k++) s->cn_rsaved[k] = s->cn_rforget[k] = (Rect){0, 0, 0, 0};
    if (nsv) {
        y = props_heading(x, y, "Saved servers");
        props_card(x, y, cw, nsv, srh, 0);
        for (int k = 0; k < nsv; k++) {
            Rect rr = {x, y + k * srh, cw, srh};
            s->cn_rsaved[k] = rr;
            if (hot(rr)) draw_rrect(rr.x + S(3), rr.y + S(3), rr.w - S(6), rr.h - S(6), S(6), C(COL_HOVER));
            Rect fx = {roundf(rr.x + rr.w - S(34)), roundf(rr.y + (srh - S(24)) * 0.5f), roundf(S(24)), roundf(S(24))};
            s->cn_rforget[k] = fx;
            bool fh = hot(fx);
            if (fh) draw_rrect(fx.x, fx.y, fx.w, fx.h, S(5), C(COL_CHIP));
            draw_cross(fx.x + fx.w * 0.5f, fx.y + fx.h * 0.5f, S(4.5f), MAX(1.f, S(1.3f)), C(fh ? COL_TEXT : COL_TEXT_FAINT));
            f32 nx = rr.x + S(14), nw = text_width(f, A.servers[k].name, (int)strlen(A.servers[k].name));
            draw_text_fit(f, nx, text_y(f, rr.y, srh), cw * 0.45f, A.servers[k].name, (int)strlen(A.servers[k].name), C(COL_TEXT), 0);
            f32 ux = nx + MIN(nw, cw * 0.45f) + S(12);
            draw_text_fit(fs, ux, text_y(fs, rr.y, srh), fx.x - S(8) - ux, A.servers[k].uri, (int)strlen(A.servers[k].uri), C(COL_TEXT_FAINT), 0);
        }
        y += nsv * srh + gap;
    }
    // display name (optional): how the server is labelled in the sidebar
    {
        y = props_heading(x, y, "Name");
        props_card(x, y, cw, 2, rh, 0);
        f32 lw0 = roundf(S(110)), fh0 = roundf(S(30));
        draw_text(fs, x + S(14), text_y(fs, y, rh), "Display name", 12, C(COL_TEXT_DIM));
        Rect nr = {x + lw0, roundf(y + (rh - fh0) * 0.5f), cw - lw0 - S(10), fh0};
        s->cn_rf[CN_NAME] = nr;
        NetAddr na0;
        cn_addr(&na0);
        char auto_name[96] = "Same as the server name";
        if (na0.host[0]) {
            char tmp[64];
            const char *keep = s->cn[CN_NAME].buf; // cn_name prefers the typed name: compute the automatic one
            char saved_c = keep[0];
            s->cn[CN_NAME].buf[0] = 0;
            cn_name(&na0, tmp, sizeof tmp);
            s->cn[CN_NAME].buf[0] = saved_c;
            snprintf(auto_name, sizeof auto_name, "%s", tmp);
        }
        sheet_field(nr, &s->cn[CN_NAME], s->cn_focus == CN_NAME, auto_name);
        y += rh;
        sheet_icon_row(x, y, cw, rh, lw0);
        y += rh + gap;
    }
    // protocol chips
    y = props_heading(x, y, "Protocol");
    f32 chh = roundf(S(30)), cgap = roundf(S(6)), chw = roundf((cw - cgap * (NET_PROTO_COUNT - 1)) / NET_PROTO_COUNT);
    for (int i = 0; i < NET_PROTO_COUNT; i++) {
        Rect cr = {x + i * (chw + cgap), y, chw, chh};
        s->cn_rproto[i] = cr;
        bool on = s->cn_proto == i, hv = A.mouse_in && in_rect(cr, A.mx, A.my), ok = net_proto_available(i);
        hand_if(hv && ok);
        draw_rrect(cr.x, cr.y, cr.w, cr.h, S(7), C(on ? COL_ACCENT : hv ? COL_CONTROL_HOVER : COL_CONTROL));
        const char *l = g_net_protos[i].label;
        f32 tw = text_width(fs, l, (int)strlen(l));
        draw_text(fs, cr.x + (cr.w - tw) * 0.5f, text_y(fs, cr.y, cr.h), l, (int)strlen(l), C(on ? COL_ON_ACCENT : ok ? COL_TEXT : COL_TEXT_FAINT));
    }
    y += chh + gap;
    // fields
    y = props_heading(x, y, "Server");
    props_card(x, y, cw, nvis, rh, 0);
    const char *sch = g_net_protos[s->cn_proto].scheme;
    const char *labels[CN_COUNT] = {"Server", "Port", !strcmp(sch, "smb") ? "Share" : !strcmp(sch, "nfs") ? "Export" : "Folder", "User",
                                    "Password", "Domain"};
    char portph[16];
    snprintf(portph, sizeof portph, "%d", g_net_protos[s->cn_proto].port);
    const char *ph[CN_COUNT] = {"nas.local or 192.168.1.10", portph, !strcmp(sch, "smb") ? "share name (optional)" : "/ (optional)",
                                "username (optional)", "", "WORKGROUP (optional)"};
    for (int k = 0; k < CN_COUNT; k++) s->cn_rf[k] = (Rect){0, 0, 0, 0};
    f32 lw = roundf(S(110)), fh = roundf(S(30));
    int row = 0;
    for (int k = 0; k < CN_COUNT; k++) {
        if (!cn_field_visible(k) || k == CN_PORT) continue;
        f32 ry = y + row * rh, fy = roundf(ry + (rh - fh) * 0.5f);
        draw_text(fs, x + S(14), text_y(fs, ry, rh), labels[k], (int)strlen(labels[k]), C(COL_TEXT_DIM));
        f32 fx = x + lw, fw = cw - lw - S(10);
        if (k == CN_HOST) { // server + port on one row
            f32 pw = roundf(S(72));
            Rect pr = {x + cw - S(10) - pw, fy, pw, fh};
            s->cn_rf[CN_PORT] = pr;
            sheet_field(pr, &s->cn[CN_PORT], s->cn_focus == CN_PORT, ph[CN_PORT]);
            fw = pr.x - S(8) - fx;
        }
        Rect fr = {fx, fy, fw, fh};
        s->cn_rf[k] = fr;
        if (k == CN_PASS) { // masked: one dot per character
            bool foc = s->cn_focus == CN_PASS;
            draw_rrect(fr.x, fr.y, fr.w, fr.h, S(6), C(foc ? COL_FIELD_HOVER : COL_FIELD));
            if (foc) draw_rrect_outline(fr.x, fr.y, fr.w, fr.h, S(6), MAX(1.f, S(1.2f)), C(COL_ACCENT));
            int nc = 0;
            for (const char *c = s->cn[CN_PASS].buf; *c; c++) nc += ((u8)*c & 0xC0) != 0x80;
            f32 dr = roundf(S(3)), dx = fr.x + S(10), dcy = fr.y + fr.h * 0.5f;
            for (int i = 0; i < nc && dx + dr * 2 < fr.x + fr.w - S(8); i++, dx += dr * 2 + S(4))
                draw_rrect(dx, dcy - dr, dr * 2, dr * 2, dr, C(COL_TEXT));
            if (foc) draw_rect(roundf(dx), fr.y + S(6), MAX(1.f, roundf(S(1.2f))), fr.h - S(12), C(COL_ACCENT));
            if (!nc && !foc) draw_text(f, fr.x + S(10), text_y(f, fr.y, fr.h), "asked when needed", 17, C(COL_TEXT_FAINT));
        } else sheet_field(fr, &s->cn[k], s->cn_focus == k, ph[k]);
        row++;
    }
    y += nvis * rh + roundf(S(10));
    // address preview / availability, remember, buttons
    NetAddr a;
    cn_addr(&a);
    char uri[1024], line[1100];
    bool ok = net_proto_available(s->cn_proto);
    if (!ok) snprintf(line, sizeof line, "Not available: GVfs's %s backend (gvfsd-%s) isn't installed", g_net_protos[s->cn_proto].label,
                      g_net_protos[s->cn_proto].backend);
    else if (a.host[0]) { net_uri(&a, uri, sizeof uri); snprintf(line, sizeof line, "%s", uri); }
    else snprintf(line, sizeof line, "Enter a server name or address");
    Font *fm = A.mono_path ? &A.font_mono : fs;
    draw_text_fit(ok && a.host[0] ? fm : fs, x + S(2), y, cw, line, (int)strlen(line), C(ok ? COL_TEXT_DIM : COL_DANGER), 0);
    y += roundf(fs->line_h + S(10));
    s->cn_rremember = (Rect){0, 0, 0, 0};
    if (s->cn_edit < 0) {
        s->cn_rremember = (Rect){x, y, roundf(S(340)), roundf(S(22))};
        sheet_check(s->cn_rremember, "Remember this server", s->cn_remember || s->cn_remember_pw);
    }
    s->cn_rremember_pw = (Rect){0, 0, 0, 0};
    if (cn_field_visible(CN_PASS)) { // passwords are only kept when asked to, and then in the system keyring
        if (s->cn_edit < 0) y += roundf(S(28));
        s->cn_rremember_pw = (Rect){x, y, cw, roundf(S(22))};
        sheet_check(s->cn_rremember_pw, secret_available() ? "Remember password in the system keyring" : "Remember password (no system keyring found)",
                    s->cn_remember_pw && secret_available());
    }
    f32 bh = roundf(S(32)), bw = roundf(S(110));
    s->cn_rconnect = (Rect){r.x + w - pad - bw, r.y + h - pad - bh, bw, bh};
    s->cn_rsave = (Rect){0, 0, 0, 0};
    f32 lx = s->cn_rconnect.x;
    if (s->cn_edit >= 0) { // edit: Save (no connection) next to Connect
        s->cn_rsave = (Rect){lx - S(8) - roundf(S(96)), s->cn_rconnect.y, roundf(S(96)), bh};
        sheet_button(s->cn_rsave, "Save", false, a.host[0] != 0);
        lx = s->cn_rsave.x;
    }
    s->rcancel = (Rect){lx - S(8) - roundf(S(96)), s->cn_rconnect.y, roundf(S(96)), bh};
    sheet_button(s->rcancel, "Cancel", false, true);
    sheet_button(s->cn_rconnect, "Connect", true, ok && a.host[0]);
    if (s->cn_pick) connect_icon_picker_draw();
}

// --- password / passphrase prompt (a server asked while connecting) ---------------------------------------

static void secret_prompt_open(DevJob *j) {
    if (g_sheet.kind == SHEET_SECRET && g_sheet.sec_job) dev_net_answer(g_sheet.sec_job, -1);
    sheet_close();
    Sheet *s = &g_sheet;
    s->kind = SHEET_SECRET;
    s->sec_job = j;
    te_set(&s->cn[CN_PASS], "", 0, 0);
    s->cn_focus = CN_PASS;
    s->cn_remember_pw = false;
    A.redraw = true;
}

static void secret_prompt_answer(bool ok) {
    Sheet *s = &g_sheet;
    DevJob *j = s->sec_job;
    s->sec_job = NULL;
    if (j) {
        if (ok) {
            pthread_mutex_lock(&j->qmtx);
            snprintf(j->password, sizeof j->password, "%s", s->cn[CN_PASS].buf);
            j->store_secret = s->cn_remember_pw && secret_available();
            pthread_mutex_unlock(&j->qmtx);
        }
        dev_net_answer(j, ok ? 0 : -1);
    }
    sheet_close();
}

static void secret_prompt_draw(void) {
    Sheet *s = &g_sheet;
    DevJob *j = s->sec_job;
    if (!j) { sheet_close(); return; }
    Font *f = &A.font, *fs = &A.font_small;
    bool phrase = str_has_ci(j->question, "passphrase");
    f32 w = roundf(MIN(S(460), A.W - S(40))), pad = roundf(S(22)), fh = roundf(S(32)), bh = roundf(S(32));
    int st[6], ln[6];
    char msg[1100];
    NetAddr a;
    char host[300] = "";
    if (net_parse(j->target, &a)) snprintf(host, sizeof host, "%s%s%s", a.user, a.user[0] ? "@" : "", a.host);
    // gio's text, tidied: "Authentication Required." repeats the title; the trailing ':' was its prompt
    const char *q = j->question;
    while (*q == ' ') q++;
    if (!strncasecmp(q, "Authentication Required", 23)) { q += 23; while (*q == '.' || *q == ' ') q++; }
    snprintf(msg, sizeof msg, "%s", q[0] ? q : (phrase ? "Enter the passphrase of your key." : "Enter your password."));
    for (size_t l = strlen(msg); l && (msg[l - 1] == ':' || msg[l - 1] == ' '); ) msg[--l] = 0;
    if (msg[0] && !strchr(".!?", msg[strlen(msg) - 1]) && strlen(msg) + 1 < sizeof msg) strcat(msg, ".");
    int nl = wrap_text(fs, msg, w - pad * 2, st, ln, 6);
    f32 lh = roundf(fs->line_h * 1.3f);
    f32 srv = roundf(S(36)); // server row
    f32 h = pad + roundf(S(30)) + srv + nl * lh + (j->qretry ? lh : 0) + roundf(S(14)) + fh + roundf(S(14)) + (secret_available() ? roundf(S(30)) : 0) + roundf(S(14)) + bh + pad;
    Rect r = {roundf((A.W - w) * 0.5f), roundf((A.H - h) * 0.42f), w, h};
    s->r = r;
    draw_rect(0, 0, (f32)A.W, (f32)A.H, CA(0x000000, 0.45f));
    draw_rrect(r.x, r.y, r.w, r.h, S(12), C(COL_POPUP));
    draw_rrect_outline(r.x, r.y, r.w, r.h, S(12), 1, C(COL_POPUP_BORDER));
    f32 x = r.x + pad, y = r.y + pad;
    const char *title = phrase ? "Passphrase required" : "Password required";
    draw_text(&A.font_brand, x, y - S(1), title, (int)strlen(title), C(COL_TEXT_STRONG));
    y += roundf(S(30));
    { // which server: a globe + user@host in a small card
        Rect sc = {x, y, w - pad * 2, srv - S(8)};
        draw_rrect(sc.x, sc.y, sc.w, sc.h, S(7), C(props_well()));
        f32 gs = roundf(S(16));
        const char *sicon = ""; // a saved server: its icon
        for (int i = 0; i < A.nservers; i++) if (!strcmp(A.servers[i].uri, j->target)) sicon = A.servers[i].icon;
        draw_net_icon(sc.x + S(9), sc.y + (sc.h - gs) * 0.5f, gs, sicon, 1.f);
        draw_text_fit(fs, sc.x + S(10) + gs + S(8), text_y(fs, sc.y, sc.h), sc.w - gs - S(30), host, (int)strlen(host), C(COL_TEXT), 0);
        y += srv;
    }
    for (int i = 0; i < nl; i++, y += lh) draw_text(fs, x, y, msg + st[i], ln[i], C(COL_TEXT2));
    if (j->qretry) {
        const char *e = phrase ? "That passphrase wasn't accepted. Try again." : "That password wasn't accepted. Try again.";
        draw_text(fs, x, y, e, (int)strlen(e), C(COL_DANGER));
        y += lh;
    }
    y += roundf(S(14));
    Rect fr = {x, y, w - pad * 2, fh};
    s->cn_rf[CN_PASS] = fr;
    draw_rrect(fr.x, fr.y, fr.w, fr.h, S(6), C(COL_FIELD_HOVER));
    draw_rrect_outline(fr.x, fr.y, fr.w, fr.h, S(6), MAX(1.f, S(1.2f)), C(COL_ACCENT));
    int nc = 0;
    for (const char *c = s->cn[CN_PASS].buf; *c; c++) nc += ((u8)*c & 0xC0) != 0x80;
    f32 dr = roundf(S(3)), dx = fr.x + S(10), dcy = fr.y + fr.h * 0.5f;
    for (int i = 0; i < nc && dx + dr * 2 < fr.x + fr.w - S(8); i++, dx += dr * 2 + S(4)) draw_rrect(dx, dcy - dr, dr * 2, dr * 2, dr, C(COL_TEXT));
    draw_rect(roundf(dx), fr.y + S(6), MAX(1.f, roundf(S(1.2f))), fr.h - S(12), C(COL_ACCENT));
    y += fh + roundf(S(14));
    s->cn_rremember_pw = (Rect){0, 0, 0, 0};
    if (secret_available()) {
        s->cn_rremember_pw = (Rect){x, y, w - pad * 2, roundf(S(22))};
        sheet_check(s->cn_rremember_pw, phrase ? "Remember passphrase in the system keyring" : "Remember password in the system keyring", s->cn_remember_pw);
    }
    (void)f;
    s->cn_rconnect = (Rect){r.x + w - pad - roundf(S(110)), r.y + h - pad - bh, roundf(S(110)), bh};
    s->rcancel = (Rect){s->cn_rconnect.x - S(8) - roundf(S(96)), s->cn_rconnect.y, roundf(S(96)), bh};
    sheet_button(s->rcancel, "Cancel", false, true);
    sheet_button(s->cn_rconnect, "Connect", true, true);
}

static void connect_focus_step(int dir) {
    Sheet *s = &g_sheet;
    static const int order[CN_ALL] = {CN_NAME, CN_HOST, CN_PORT, CN_PATH, CN_USER, CN_PASS, CN_DOMAIN};
    int i = 0;
    for (int k = 0; k < CN_ALL; k++) if (order[k] == s->cn_focus) i = k;
    for (int step = 0; step < CN_ALL; step++) {
        i = (i + dir + CN_ALL) % CN_ALL;
        if (cn_field_visible(order[i])) { s->cn_focus = order[i]; return; }
    }
}

#include "options.c"

// --- dispatch ----------------------------------------------------------------

static void sheet_draw(void) {
    switch (g_sheet.kind) {
    case SHEET_OPENWITH: openwith_draw(); break;
    case SHEET_PROPS: props_draw(); break;
    case SHEET_RENAME: rename_draw(); break;
    case SHEET_OPTIONS: options_draw(); break;
    case SHEET_CONNECT: connect_draw(); break;
    case SHEET_SECRET: secret_prompt_draw(); break;
    case SHEET_ADDON: addon_sheet_draw(); break;
    default: break;
    }
}

static void sheet_key(SDL_Keycode k, SDL_Keymod mod) {
    Sheet *s = &g_sheet;
    A.redraw = true;
    if (s->kind == SHEET_OPTIONS) { options_key(k, mod); return; }
    if (k == SDLK_ESCAPE && (s->kind == SHEET_CONNECT || s->kind == SHEET_ADDON) && s->cn_pick) { s->cn_pick = false; return; }
    if (s->kind == SHEET_ADDON && addon_sheet_key(k, mod)) return;
    if (k == SDLK_ESCAPE) { sheet_close(); return; }
    switch (s->kind) {
    case SHEET_SECRET:
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) secret_prompt_answer(true);
        else te_key(&s->cn[CN_PASS], k, mod);
        break;
    case SHEET_CONNECT:
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) connect_go();
        else if (k == SDLK_TAB) connect_focus_step(mod & SDL_KMOD_SHIFT ? -1 : 1);
        else te_key(&s->cn[s->cn_focus], k, mod);
        break;
    case SHEET_OPENWITH:
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) openwith_go();
        else if (k == SDLK_DOWN) { s->sel = MIN(s->sel + 1, s->napps - 1); s->ow_reveal = true; }
        else if (k == SDLK_UP) { s->sel = MAX(s->sel - 1, 0); s->ow_reveal = true; }
        else if (k == SDLK_PAGEDOWN || k == SDLK_PAGEUP) {
            int pg = MAX(1, (int)(s->rlist.h / s->row_h) - 1);
            s->sel = CLAMP(s->sel + (k == SDLK_PAGEDOWN ? pg : -pg), 0, MAX(0, s->napps - 1));
            s->ow_reveal = true;
        } else if (te_key(&s->filter, k, mod)) openwith_refresh();
        break;
    case SHEET_PROPS:
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) sheet_close();
        break;
    case SHEET_RENAME: {
        TextEdit *tes[3] = {&s->tmpl, &s->find, &s->repl};
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) rename_apply();
        else if (k == SDLK_TAB) s->focus = (s->focus + (mod & SDL_KMOD_SHIFT ? 2 : 1)) % 3;
        else if (k == SDLK_DOWN) s->top++;
        else if (k == SDLK_UP) s->top = MAX(0, s->top - 1);
        else if (te_key(tes[s->focus], k, mod)) rename_recompute();
        break;
    }
    }
}

static void sheet_text(const char *text) {
    Sheet *s = &g_sheet;
    A.redraw = true;
    if (s->kind == SHEET_OPTIONS) options_text(text);
    else if (s->kind == SHEET_OPENWITH) { te_insert(&s->filter, text); openwith_refresh(); }
    else if (s->kind == SHEET_SECRET) te_insert(&s->cn[CN_PASS], text);
    else if (s->kind == SHEET_ADDON) addon_sheet_text(text);
    else if (s->kind == SHEET_CONNECT) {
        if (s->cn_focus == CN_PORT) { // digits only
            char d[8];
            size_t o = 0;
            for (const char *c = text; *c && o + 1 < sizeof d; c++) if (isdigit((u8)*c)) d[o++] = *c;
            d[o] = 0;
            if (o && strlen(s->cn[CN_PORT].buf) + o <= 5) te_insert(&s->cn[CN_PORT], d);
        } else te_insert(&s->cn[s->cn_focus], text);
    }
    else if (s->kind == SHEET_RENAME) {
        TextEdit *tes[3] = {&s->tmpl, &s->find, &s->repl};
        te_insert(tes[s->focus], text);
        rename_recompute();
    }
}

static void sheet_mouse(SDL_MouseButtonEvent *b) {
    Sheet *s = &g_sheet;
    f32 x = b->x, y = b->y;
    A.redraw = true;
    if (b->button != SDL_BUTTON_LEFT) return;
    if (s->kind == SHEET_OPTIONS) { options_click(x, y, b->clicks); return; }
    if ((s->kind == SHEET_CONNECT || s->kind == SHEET_ADDON) && s->cn_pick) { // icon picker open: a click picks an icon or closes it
        if (in_rect(s->cn_rpick, x, y)) {
            if (in_rect(s->cn_rpick_body, x, y))
                for (int c = 0; c < s->cn_ncell; c++)
                    if (in_rect(s->cn_rcell[c], x, y)) {
                        int i = s->cn_cell_idx[c];
                        snprintf(s->cn_icon, sizeof s->cn_icon, "%s", !strcmp(net_icon_key(i), cn_icon_default()) ? "" : net_icon_key(i)); // the default stays ""
                        s->cn_pick = false;
                        return;
                    }
            return;
        }
        s->cn_pick = false;
        if (in_rect(s->cn_ricon, x, y)) return; // (the button toggles)
    }
    if (s->kind == SHEET_ADDON) { addon_sheet_click(x, y); return; }
    if (!in_rect(s->r, x, y)) { sheet_close(); return; }
    switch (s->kind) {
    case SHEET_SECRET:
        if (in_rect(s->cn_rconnect, x, y)) { secret_prompt_answer(true); return; }
        if (in_rect(s->rcancel, x, y)) { secret_prompt_answer(false); return; }
        if (s->cn_rremember_pw.w > 0 && in_rect(s->cn_rremember_pw, x, y)) { s->cn_remember_pw = !s->cn_remember_pw; return; }
        break;
    case SHEET_CONNECT:
        if (in_rect(s->cn_ricon, x, y)) { s->cn_pick = true; s->cn_pick_scroll = 0; return; }
        if (in_rect(s->cn_rconnect, x, y)) { connect_go(); return; }
        if (s->cn_rsave.w > 0 && in_rect(s->cn_rsave, x, y)) {
            if (connect_save_edit()) { toast("Saved"); sheet_close(); init_places(); }
            return;
        }
        if (in_rect(s->rcancel, x, y)) { sheet_close(); return; }
        if (s->cn_rremember.w > 0 && in_rect(s->cn_rremember, x, y)) { s->cn_remember = !(s->cn_remember || s->cn_remember_pw); if (!s->cn_remember) s->cn_remember_pw = false; return; }
        if (s->cn_rremember_pw.w > 0 && in_rect(s->cn_rremember_pw, x, y) && secret_available()) {
            s->cn_remember_pw = !s->cn_remember_pw;
            if (s->cn_remember_pw) s->cn_remember = true;
            return;
        }
        for (int i = 0; i < NET_PROTO_COUNT; i++)
            if (in_rect(s->cn_rproto[i], x, y)) {
                s->cn_proto = i;
                if (!cn_field_visible(s->cn_focus)) s->cn_focus = CN_HOST;
                return;
            }
        for (int k = 0; k < CN_ALL; k++)
            if (s->cn_rf[k].w > 0 && in_rect(s->cn_rf[k], x, y)) { s->cn_focus = k; return; }
        break;
    case SHEET_OPENWITH:
        if (in_rect(s->ropen, x, y)) { openwith_go(); return; }
        if (in_rect(s->rcancel, x, y)) { sheet_close(); return; }
        if (in_rect(s->rcheck_def, x, y)) { s->set_default = !s->set_default; return; }
        if (in_rect(s->rcheck_all, x, y)) { s->show_all = !s->show_all; openwith_refresh(); return; }
        if (in_rect(s->rlist, x, y)) {
            int i = (int)((y - s->rlist.y + s->ow_scroll) / s->row_h);
            if (i >= 0 && i < s->napps) {
                s->sel = i;
                if (b->clicks >= 2) openwith_go();
            }
        }
        break;
    case SHEET_PROPS:
        if (in_rect(s->rclose, x, y)) { sheet_close(); return; }
        if (!in_rect(s->rbody, x, y)) return; // the rest only exists inside the scrolling body
        for (int k = 0; k < 9; k++)
            if (s->rperm[k].w > 0 && in_rect(s->rperm[k], x, y)) { props_toggle_perm(k); return; }
        if (s->rsv_admin.w > 0 && in_rect(s->rsv_admin, x, y)) { // list them as root (pkexec asks for the password)
            if (!admin_allowed()) return;
            snap_release(s->snap);
            s->snap = snap_start_admin(s->paths[0]);
            return;
        }
        if (s->snap && atomic_load(&s->snap->done))
            for (int i = 0; i < MIN(s->snap->n, 8); i++) {
                SnapVer *v = &s->snap->v[i];
                bool adm = s->snap->admin;
                if (s->rsv_open[i].w > 0 && in_rect(s->rsv_open[i], x, y)) { // the old one itself (read-only)
                    if (v->dir && adm) return;
                    if (v->dir) { char vp[PATH_MAX]; snprintf(vp, sizeof vp, "%s", v->path); sheet_close(); tab_new(vp, A.cur + 1, true); }
                    else if (adm) version_open_admin(s->paths[0], v);
                    else open_path(v->path);
                    return;
                }
                if (s->rsv_act[i].w > 0 && in_rect(s->rsv_act[i], x, y)) {
                    if (v->dir) { if (!adm) version_compare(s->paths[0], v->path); }
                    else if (!v->same_as_now) version_restore_ask(s->paths[0], v, adm);
                    return;
                }
            }
        if (s->rreveal.w > 0 && in_rect(s->rreveal, x, y)) { // open the containing folder with the item selected
            char par[PATH_MAX], nm[NAME_MAX + 1];
            parent_of(s->paths[0], par, sizeof par);
            snprintf(nm, sizeof nm, "%s", path_basename(s->paths[0]));
            sheet_close();
            Pane *p = active_pane();
            if (!strcmp(p->path, par) && p->order) { // already there: just put the cursor on it (no reload)
                for (u32 i = 0; i < p->count; i++)
                    if (!strcmp(p->L->entries[p->order[i]].name, nm)) { move_cursor(p, i, false, false); return; }
            }
            pane_navigate(p, par, true);
            snprintf(p->select_name, sizeof p->select_name, "%s", nm);
            return;
        }
        if (s->sum && atomic_load(&s->sum->done) && s->rcopy_md5.w > 0 && in_rect(s->rcopy_md5, x, y)) {
            SDL_SetClipboardText(s->sum->md5);
            toast("MD5 copied");
            return;
        }
        if (s->rchange.w > 0 && in_rect(s->rchange, x, y)) {
            char *p0 = strdup(s->paths[0]);
            openwith_open(&p0, 1);
            free(p0);
            return;
        }
        if (!s->sum && s->rsum.w > 0 && in_rect(s->rsum, x, y)) { s->sum = sum_start(s->paths[0]); return; }
        if (s->sum && atomic_load(&s->sum->done) && s->rcopy_sha.w > 0 && in_rect(s->rcopy_sha, x, y)) {
            SDL_SetClipboardText(s->sum->sha256);
            toast("SHA-256 copied");
        }
        break;
    case SHEET_RENAME:
        for (int i = 0; i < 3; i++)
            if (in_rect(s->rfield[i], x, y)) { s->focus = i; return; }
        for (int i = 0; i < 4; i++)
            if (in_rect(s->rcase[i], x, y)) { s->case_mode = i; rename_recompute(); return; }
        if (in_rect(s->rmatch, x, y)) { s->match_case = !s->match_case; rename_recompute(); return; }
        if (in_rect(s->rnum_minus, x, y) && s->num_start > 0) { s->num_start--; rename_recompute(); return; }
        if (in_rect(s->rnum_plus, x, y)) { s->num_start++; rename_recompute(); return; }
        if (in_rect(s->rapply, x, y)) { rename_apply(); return; }
        if (in_rect(s->rcancel, x, y)) { sheet_close(); return; }
        break;
    }
}

static void sheet_wheel(f32 dy) {
    Sheet *s = &g_sheet;
    if (s->kind == SHEET_OPTIONS) options_wheel(dy);
    else if ((s->kind == SHEET_CONNECT || s->kind == SHEET_ADDON) && s->cn_pick) s->cn_pick_scroll -= dy * S(48); // clamped when drawn
    else if (s->kind == SHEET_OPENWITH) s->ow_scroll -= dy * s->row_h * 1.5f; // clamped when drawn
    else if (s->kind == SHEET_PROPS) s->pv_scroll -= dy * S(48);
    else if (s->kind == SHEET_RENAME) s->top = MAX(0, s->top - (int)dy * 3);
    A.redraw = true;
}

// Background work in Properties needs periodic redraws.
static bool sheet_busy(void) {
    Sheet *s = &g_sheet;
    return (s->stats && !atomic_load(&s->stats->done)) || (s->sum && !atomic_load(&s->sum->done)) || (s->snap && !atomic_load(&s->snap->done));
}
