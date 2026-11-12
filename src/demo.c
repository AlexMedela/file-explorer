// demo.c - the intro video (--demo OUT.mp4; only in builds made with ./build.sh demo, tools/demo.sh runs it):
// scripted scenes through the real input handlers in a made-up home (tools/demo-home.sh), each frame put on a
// 1920x1080 backdrop with a pointer, keycaps and captions, and piped into ffmpeg (included from app.c)

// --- recorder: window on a backdrop, pointer, keycaps, captions, ffmpeg ---

#define DM_W 1920
#define DM_H 1080
#define DM_FPS 30
#define DM_WX 160 // the app window in the video
#define DM_WY 40
#define DM_WW 1600
#define DM_WH 900
#define SQR(x) ((x) * (x))

static struct {
    FILE *ff;
    SDL_Texture *target, *bg, *shadow, *app;
    int frame;
    u64 t0;
    Font title, sub, key, huge, mid;
    f32 cx, cy;           // pointer, in window pixels
    bool cursor;          // pointer shown
    int click_frame;      // last click (ripple)
    char cap[128], capsub[256];
    int cap_frame;        // caption appeared
    char prev_cap[128], prev_capsub[256];
    char keys[64];
    int keys_frame;
    f32 win;              // app window shown 0..1 (title cards)
    int card;             // 1 intro, 2 outro: drawn over the backdrop
    f32 card_a;
} D = {.click_frame = -1000, .keys_frame = -1000, .cap_frame = -1000};

static void dm_write(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

// Before settings are read: a private home (FE_DEMO_HOME, made by tools/demo-home.sh; tools/demo.sh shows it as
// /home/alex), config and caches under FE_DEMO_DIR, made-up drives, a window the size of the video's, larger text.
// Returns the folder to start in.
static const char *demo_setup(void) {
    static char home[PATH_MAX], path[PATH_MAX + 64];
    const char *base = getenv("FE_DEMO_DIR") ? getenv("FE_DEMO_DIR") : "/tmp/fe-demo";
    snprintf(home, sizeof home, "%s", getenv("FE_DEMO_HOME") ? getenv("FE_DEMO_HOME") : "/tmp/fe-demo/home");
    setenv("HOME", home, 1);
#define DM_ENV(var, sub) do { snprintf(path, sizeof path, "%s/" sub, base); mkdir_p(path, 0755); setenv(var, path, 1); } while (0)
    DM_ENV("XDG_CONFIG_HOME", "config");
    DM_ENV("XDG_DATA_HOME", "data");
    DM_ENV("XDG_CACHE_HOME", "cache");
    DM_ENV("FE_GVFS_DIR", "gvfs"); // (none of this machine's network mounts)
    snprintf(path, sizeof path, "%s/bin/lsblk", base);
    setenv("FE_LSBLK", path, 1);
    setenv("SHELL", "/bin/bash", 1); // its prompt comes from the demo home's .bashrc
    setenv("FE_TITLE_BAR", "app", 1); // our own title bar, as on a desktop
    snprintf(path, sizeof path, "%s/config/file-explorer", base);
    mkdir_p(path, 0755);
    snprintf(path, sizeof path, "%s/config/file-explorer/settings.conf", base);
    dm_write(path, "settings_version=99\nwindow=1600x900\nmaximized=0\nzoom=1.25\nrestore_session=0\ngit_status=1\nload_times=1\n"
                   "integration_asked=1\ncheck_updates=0\nfolder_sizes=1\nserver=Home NAS\tsmb://nas.local/media\n");
    snprintf(path, sizeof path, "%s/config/user-dirs.dirs", base);
    dm_write(path, "XDG_DESKTOP_DIR=\"$HOME/Desktop\"\nXDG_DOCUMENTS_DIR=\"$HOME/Documents\"\nXDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\n"
                   "XDG_MUSIC_DIR=\"$HOME/Music\"\nXDG_PICTURES_DIR=\"$HOME/Pictures\"\nXDG_VIDEOS_DIR=\"$HOME/Videos\"\n"
                   "XDG_PUBLICSHARE_DIR=\"$HOME/Public\"\nXDG_TEMPLATES_DIR=\"$HOME/Templates\"\n");
    return home;
}

static f32 dm_ease(f32 t) { t = CLAMP(t, 0.f, 1.f); return t * t * (3.f - 2.f * t); }

// Backdrop (dark gradient with two soft glows) and the window's shadow, made once on the CPU.
static SDL_Texture *dm_bg_texture(bool shadow) {
    u8 *px = xmalloc((size_t)DM_W * DM_H * 4);
    u32 seed = 12345;
    for (int y = 0; y < DM_H; y++)
        for (int x = 0; x < DM_W; x++) {
            u8 *o = &px[((size_t)y * DM_W + x) * 4];
            seed = seed * 1664525u + 1013904223u;
            f32 n = ((f32)(seed >> 24) / 255.f - 0.5f) * 2.f; // dither against banding
            if (shadow) {
                // distance outside the window's rounded rectangle, shifted down
                f32 r = 14, hx = DM_WW * 0.5f - r, hy = DM_WH * 0.5f - r;
                f32 dx = fabsf(x - (DM_WX + DM_WW * 0.5f)) - hx, dy = fabsf(y - (DM_WY + 18 + DM_WH * 0.5f)) - hy;
                f32 d = sqrtf(SQR(MAX(dx, 0.f)) + SQR(MAX(dy, 0.f))) + MIN(MAX(dx, dy), 0.f) - r;
                f32 a = d < 0 ? 0.6f : 0.6f * expf(-SQR(d / 46.f));
                o[0] = o[1] = o[2] = 0;
                o[3] = (u8)CLAMP(a * 255.f + n, 0.f, 255.f);
                continue;
            }
            f32 t = (f32)y / DM_H, u = (f32)x / DM_W;
            f32 rgb[3] = {0x16 + (0x0B - 0x16) * t, 0x14 + (0x0C - 0x14) * t, 0x27 + (0x15 - 0x27) * t};
            f32 g1 = expf(-(SQR(u - 0.12f) * 3.2f + SQR(t - 1.0f) * 5.f)) * 0.30f; // blue glow, bottom left
            f32 g2 = expf(-(SQR(u - 0.92f) * 3.2f + SQR(t - 0.0f) * 5.f)) * 0.26f; // purple glow, top right
            u8 b1[3] = {0x7A, 0xA2, 0xF7}, b2[3] = {0xBB, 0x9A, 0xF7};
            for (int k = 0; k < 3; k++) o[k] = (u8)CLAMP(rgb[k] + b1[k] * g1 + b2[k] * g2 + n, 0.f, 255.f);
            o[3] = 255;
        }
    SDL_Texture *tex = SDL_CreateTexture(A.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, DM_W, DM_H);
    SDL_UpdateTexture(tex, NULL, px, DM_W * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    free(px);
    return tex;
}

static void dm_start(void) {
    char cmd[PATH_MAX + 512];
    // A near-lossless intermediate: tools/demo.sh encodes the final files from it.
    snprintf(cmd, sizeof cmd, "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - "
             "-c:v libx264 -preset ultrafast -crf 6 -pix_fmt yuv444p '%s'", DM_W, DM_H, DM_FPS, g_demo_out);
    D.ff = popen(cmd, "we"); // (close-on-exec: shells started later mustn't keep the pipe open)
    if (!D.ff) { fprintf(stderr, "demo: can't run ffmpeg\n"); exit(1); }
    D.target = SDL_CreateTexture(A.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, DM_W, DM_H);
    D.app = SDL_CreateTexture(A.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, A.W, A.H);
    SDL_SetTextureBlendMode(D.app, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(D.app, SDL_SCALEMODE_LINEAR);
    D.bg = dm_bg_texture(false);
    D.shadow = dm_bg_texture(true);
    char *bold = font_weight_file(A.font_path, FC_WEIGHT_BOLD);
    char *semi = font_weight_file(A.font_path, FC_WEIGHT_SEMIBOLD);
    font_init(&D.title, semi ? semi : A.font_path, 30);
    font_init(&D.sub, A.font_path, 22);
    font_init(&D.key, semi ? semi : A.font_path, 22);
    font_init(&D.huge, bold ? bold : A.font_path, 84);
    font_init(&D.mid, A.font_path, 30);
    free(bold);
    free(semi);
    D.cx = A.W * 0.6f;
    D.cy = A.H * 0.55f;
    D.t0 = now_ns();
}

// The pointer: a classic arrow, white with a dark edge, at (x, y) in video pixels.
static void dm_draw_pointer(f32 x, f32 y, f32 s) {
    static const f32 P[][2] = {{0, 0}, {0, 17}, {4.2f, 13.2f}, {7, 19.6f}, {9.4f, 18.6f}, {6.6f, 12.4f}, {12, 12}};
    f32 q[7][2];
    for (int i = 0; i < 7; i++) { q[i][0] = x + P[i][0] * s; q[i][1] = y + P[i][1] * s; }
    Color sh = CA(0x000000, 0.25f);
    draw_tri(q[0][0] + 2, q[0][1] + 3, q[1][0] + 2, q[1][1] + 3, q[6][0] + 2, q[6][1] + 3, sh);
    Color w = C(0xFFFFFF);
    draw_tri(q[0][0], q[0][1], q[1][0], q[1][1], q[2][0], q[2][1], w);
    draw_tri(q[0][0], q[0][1], q[2][0], q[2][1], q[5][0], q[5][1], w);
    draw_tri(q[0][0], q[0][1], q[5][0], q[5][1], q[6][0], q[6][1], w);
    draw_tri(q[2][0], q[2][1], q[3][0], q[3][1], q[4][0], q[4][1], w);
    draw_tri(q[2][0], q[2][1], q[4][0], q[4][1], q[5][0], q[5][1], w);
    for (int i = 0; i < 7; i++) {
        int j = (i + 1) % 7;
        draw_line(q[i][0], q[i][1], q[j][0], q[j][1], 1.6f, C(0x111111));
    }
}

// One keycap row ("Ctrl+Shift+P": a cap per key) centred at cx, top at y; returns its width.
static f32 dm_draw_keys(const char *keys, f32 cx, f32 y, f32 alpha, bool measure) {
    char buf[64];
    snprintf(buf, sizeof buf, "%s", keys);
    const char *parts[8];
    int n = 0;
    for (char *p = buf, *t; n < 8 && (t = strsep(&p, "+"));) if (*t) parts[n++] = t;
    if (!n) return 0;
    f32 pad = 16, gap = 10, h = 46, plus = text_width(&D.sub, "+", 1) + 2 * gap, w = 0;
    for (int i = 0; i < n; i++) w += MAX(h, text_width(&D.key, parts[i], (int)strlen(parts[i])) + 2 * pad) + (i ? plus : 0);
    if (measure) return w;
    f32 x = roundf(cx - w * 0.5f);
    for (int i = 0; i < n; i++) {
        if (i) {
            draw_text(&D.sub, x + gap, y + (h - D.sub.line_h) * 0.5f, "+", 1, CA(0xC8CCE0, alpha));
            x += plus;
        }
        f32 tw = text_width(&D.key, parts[i], (int)strlen(parts[i])), cw = MAX(h, tw + 2 * pad);
        draw_rrect(x, y + 4, cw, h, 9, CA(0x000000, 0.35f * alpha));
        draw_rrect(x, y, cw, h, 9, CA(0x3B3F57, alpha));
        draw_rrect(x + 1, y + 1, cw - 2, h - 5, 8, CA(0x4A4F6C, alpha));
        draw_text(&D.key, x + (cw - tw) * 0.5f, y + (h - 4 - D.key.line_h) * 0.5f, parts[i], (int)strlen(parts[i]), CA(0xF2F4FF, alpha));
        x += cw;
    }
    return w;
}

static void dm_text_centered(Font *f, f32 cy, const char *s, Color c) {
    int n = (int)strlen(s);
    draw_text(f, roundf((DM_W - text_width(f, s, n)) * 0.5f), roundf(cy - f->line_h * 0.5f), s, n, c);
}

// Title cards: the logo, the name and a line or two.
static void dm_draw_card(int card, f32 a) {
    f32 lh = 150, lw = roundf(lh * 624.f / 456.f);
    f32 top = card == 1 ? 300 : 260;
    Glyph *g = icon_glyph(ICON_LOGO, 256);
    if (g) {
        // the glyph is a square raster with the logo centred: draw it scaled to the logo's width
        f32 s = lw / 256.f, gx = (DM_W - lw) * 0.5f, gy = top - (lw - lh) * 0.5f;
        draw_reserve(4, 6);
        f32 u0 = g->x / (f32)ATLAS_SIZE, v0 = g->y / (f32)ATLAS_SIZE, u1 = (g->x + g->w) / (f32)ATLAS_SIZE, v1 = (g->y + g->h) / (f32)ATLAS_SIZE;
        Color c = {1, 1, 1, a};
        int i0 = dv(gx, gy, c, u0, v0), i1 = dv(gx + g->w * s, gy, c, u1, v0), i2 = dv(gx + g->w * s, gy + g->h * s, c, u1, v1), i3 = dv(gx, gy + g->h * s, c, u0, v1);
        di4(i0, i1, i2, i3);
    }
    dm_text_centered(&D.huge, top + lh + 90, "File Explorer", CA(0xF2F4FF, a));
    if (card == 1) {
        dm_text_centered(&D.mid, top + lh + 170, "Fast, keyboard-first file manager for Linux", CA(0xA9B1D6, a));
    } else {
        dm_text_centered(&D.mid, top + lh + 170, "github.com/AlexMedela/file-explorer", CA(0x7AA2F7, a));
        dm_text_centered(&D.sub, top + lh + 235, "Also: phones and cameras, network shares, cloud storage, snapshots, undo, batch rename and more", CA(0xA9B1D6, a));
        dm_text_centered(&D.sub, top + lh + 280, "Free and open source (MIT)  \xC2\xB7  Wayland and X11  \xC2\xB7  AppImage, Arch, Fedora, openSUSE, Debian, Ubuntu", CA(0x8B91B0, a));
    }
}

static void demo_capture(void) {
    if (!D.ff) return;
    SDL_Surface *s = SDL_RenderReadPixels(A.ren, NULL);
    if (!s) return;
    SDL_Surface *c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!c) return;
    // rounded window corners: clear what's outside a 14 px radius (anti-aliased)
    const int R = 14;
    for (int y = 0; y < R; y++)
        for (int x = 0; x < R; x++) {
            f32 d = sqrtf(SQR(R - x - 0.5f) + SQR(R - y - 0.5f)) - R, a = CLAMP(0.5f - d, 0.f, 1.f);
            int xs[2] = {x, c->w - 1 - x}, ys[2] = {y, c->h - 1 - y};
            for (int i = 0; i < 2; i++)
                for (int j = 0; j < 2; j++) {
                    u8 *p = (u8 *)c->pixels + (size_t)ys[j] * c->pitch + (size_t)xs[i] * 4;
                    p[3] = (u8)(p[3] * a);
                }
        }
    SDL_UpdateTexture(D.app, NULL, c->pixels, c->pitch);
    SDL_DestroySurface(c);

    // compose the video frame
    SDL_SetRenderTarget(A.ren, D.target);
    draw_clip(NULL);
    g_alpha = 1.f;
    SDL_RenderTexture(A.ren, D.bg, NULL, NULL);
    f32 w = dm_ease(D.win);
    if (w > 0) {
        SDL_SetTextureAlphaModFloat(D.shadow, w);
        SDL_RenderTexture(A.ren, D.shadow, NULL, NULL);
        f32 sc = 0.94f + 0.06f * w, ww = DM_WW * sc, wh = DM_WH * sc;
        SDL_FRect dst = {DM_WX + (DM_WW - ww) * 0.5f, DM_WY + (DM_WH - wh) * 0.5f + (1 - w) * 30, ww, wh};
        SDL_SetTextureAlphaModFloat(D.app, w);
        SDL_RenderTexture(A.ren, D.app, NULL, &dst);
        draw_rrect_outline(dst.x, dst.y, dst.w, dst.h, 14, 1, CA(0xFFFFFF, 0.10f * w));
    }
    if (D.card && D.card_a > 0) dm_draw_card(D.card, dm_ease(D.card_a));

    // caption: title + line under the window, sliding in
    f32 base = DM_WY + DM_WH + 26;
    int since = D.frame - D.cap_frame;
    f32 ca = dm_ease(since / 9.f), cprev = 1 - dm_ease(since / 6.f);
    if (D.prev_cap[0] && cprev > 0) {
        draw_text(&D.title, DM_WX, base - 8 * (1 - cprev), D.prev_cap, (int)strlen(D.prev_cap), CA(0xF2F4FF, cprev * w));
        draw_text(&D.sub, DM_WX, base + 44 - 8 * (1 - cprev), D.prev_capsub, (int)strlen(D.prev_capsub), CA(0xA9B1D6, cprev * w));
    }
    if (D.cap[0] && ca > 0) {
        f32 dy = 12 * (1 - ca);
        draw_rect(DM_WX - 18, base + 6 + dy, 4, 64, CA(0x7AA2F7, ca * w));
        draw_text(&D.title, DM_WX, base + dy, D.cap, (int)strlen(D.cap), CA(0xF2F4FF, ca * w));
        draw_text(&D.sub, DM_WX, base + 44 + dy, D.capsub, (int)strlen(D.capsub), CA(0xA9B1D6, ca * w));
    }
    // keycaps: right of the caption, for a moment after each shortcut
    int ks = D.frame - D.keys_frame;
    if (D.keys[0] && ks < 60) {
        f32 ka = MIN(dm_ease(ks / 4.f), 1 - dm_ease((ks - 45) / 15.f));
        f32 kw = dm_draw_keys(D.keys, 0, 0, 0, true);
        dm_draw_keys(D.keys, DM_WX + DM_WW - kw * 0.5f, base + 14 - 6 * (1 - dm_ease(ks / 4.f)), ka * w, false);
    }
    // pointer + click ripple
    if (D.cursor && w > 0.5f) {
        f32 px = DM_WX + D.cx, py = DM_WY + D.cy;
        int cs = D.frame - D.click_frame;
        if (cs >= 0 && cs < 12) {
            f32 t = cs / 12.f, r = 10 + 26 * dm_ease(t);
            draw_rrect_outline(px - r, py - r, 2 * r, 2 * r, r, 3, CA(0x7AA2F7, 0.8f * (1 - t)));
        }
        dm_draw_pointer(px, py, 1.5f);
    }
    draw_flush();
    SDL_Surface *out = SDL_RenderReadPixels(A.ren, NULL);
    SDL_SetRenderTarget(A.ren, NULL);
    if (out) {
        SDL_Surface *o = out->format == SDL_PIXELFORMAT_RGBA32 ? out : SDL_ConvertSurface(out, SDL_PIXELFORMAT_RGBA32);
        if (o) {
            for (int y = 0; y < o->h; y++) fwrite((u8 *)o->pixels + (size_t)y * o->pitch, 4, (size_t)o->w, D.ff);
            if (o != out) SDL_DestroySurface(o);
        }
        SDL_DestroySurface(out);
    }
    D.frame++;
    // keep the video's clock with the real one (toasts, debounces and shells run on real time)
    u64 due = D.t0 + (u64)D.frame * 1000000000ull / DM_FPS, now = now_ns();
    if (due > now) SDL_DelayNS(due - now);
}

// --- script helpers: time, pointer, keys, typing, captions ---

static void dm_frames(int n) {
    for (int i = 0; i < n; i++) {
        if (A.search_due && now_ns() >= A.search_due && A.search_pane) search_apply(A.search_pane, false); // (main loop's job)
        if (A.nnets) { A.nnets = 0; init_places(); } // this machine's network mounts
        st_frames(1);
    }
}
static void dm_hold(f32 sec) { dm_frames((int)(sec * DM_FPS + 0.5f)); }

static void dm_caption(const char *title, const char *sub) {
    snprintf(D.prev_cap, sizeof D.prev_cap, "%s", D.cap);
    snprintf(D.prev_capsub, sizeof D.prev_capsub, "%s", D.capsub);
    snprintf(D.cap, sizeof D.cap, "%s", title);
    snprintf(D.capsub, sizeof D.capsub, "%s", sub);
    D.cap_frame = D.frame;
}

// Glide the pointer to (x, y) (window pixels) over sec, along a slight arc like a hand.
static void dm_move(f32 x, f32 y, f32 sec) {
    f32 x0 = D.cx, y0 = D.cy;
    int n = MAX(1, (int)(sec * DM_FPS));
    f32 len = sqrtf(SQR(x - x0) + SQR(y - y0)), bow = MIN(len * 0.08f, 40.f);
    for (int i = 1; i <= n; i++) {
        f32 t = dm_ease((f32)i / n), arc = sinf(t * (f32)M_PI) * bow;
        D.cx = x0 + (x - x0) * t + (len > 0 ? -(y - y0) / len * arc : 0);
        D.cy = y0 + (y - y0) * t + (len > 0 ? (x - x0) / len * arc : 0);
        st_mouse(SDL_EVENT_MOUSE_MOTION, D.cx, D.cy, 0, 0, 0);
    }
}

static void dm_click_ex(f32 x, f32 y, int button, int clicks, SDL_Keymod mod) {
    f32 d = sqrtf(SQR(x - D.cx) + SQR(y - D.cy));
    if (d > 2) dm_move(x, y, CLAMP(d / 900.f, 0.35f, 0.9f));
    dm_hold(0.12f);
    for (int k = 1; k <= clicks; k++) {
        D.click_frame = D.frame;
        st_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y, button, k, mod);
        st_mouse(SDL_EVENT_MOUSE_BUTTON_UP, x, y, button, k, mod);
        if (k < clicks) dm_frames(2);
    }
}
static void dm_click(f32 x, f32 y) { dm_click_ex(x, y, SDL_BUTTON_LEFT, 1, 0); }
static void dm_dblclick(f32 x, f32 y) { dm_click_ex(x, y, SDL_BUTTON_LEFT, 2, 0); }

// A shortcut, shown as keycaps.
static void dm_key(SDL_Keycode k, SDL_Keymod mod, const char *label) {
    if (label) {
        snprintf(D.keys, sizeof D.keys, "%s", label);
        D.keys_frame = D.frame;
    }
    dm_hold(0.15f);
    st_key(k, mod);
}

// Space: a key and a typed character (the preview listens to the latter, as with a real keyboard).
static void dm_space(void) {
    dm_key(SDLK_SPACE, 0, "Space");
    on_text(" ");
    dm_frames(1);
}

// Typing, a few characters per second (UTF-8 sequences stay together).
static void dm_type(const char *s, f32 cps) {
    for (const char *p = s; *p;) {
        const char *q = p + 1;
        while ((*q & 0xC0) == 0x80) q++;
        char ch[8];
        snprintf(ch, sizeof ch, "%.*s", (int)(q - p), p);
        on_text(ch);
        p = q;
        dm_hold(1.f / cps);
    }
}

static void dm_wait_load(void) { st_wait(); }

// Where things are, in window pixels.
static bool dm_item(const char *name, f32 *x, f32 *y) { // an item of the active pane, by name: its name (details) or icon (grid)
    Pane *p = active_pane();
    for (u32 i = 0; p->order && i < p->count; i++)
        if (!strcmp(p->L->entries[p->order[i]].name, name)) {
            Rect r = item_rect(p, i);
            *x = p->view == VIEW_DETAILS ? r.x + S(70) : r.x + r.w * 0.5f;
            *y = p->view == VIEW_DETAILS ? r.y + r.h * 0.5f : r.y + r.h * 0.4f;
            return true;
        }
    fprintf(stderr, "demo: no item %s\n", name);
    return false;
}

static bool dm_place(const char *label, f32 *x, f32 *y) {
    for (int i = 0; i < A.nplaces; i++)
        if (!strcmp(A.places[i].label, label)) {
            Rect r = place_rect(i);
            *x = r.x + S(60);
            *y = r.y + r.h * 0.5f;
            return true;
        }
    fprintf(stderr, "demo: no place %s\n", label);
    return false;
}

static void dm_open(const char *name) { // double-click an item
    f32 x, y;
    if (dm_item(name, &x, &y)) dm_dblclick(x, y);
    dm_wait_load();
}

static void dm_select(const char *name, SDL_Keymod mod) {
    f32 x, y;
    if (dm_item(name, &x, &y)) dm_click_ex(x, y, SDL_BUTTON_LEFT, 1, mod);
}

static void dm_go_place(const char *label) {
    f32 x, y;
    if (dm_place(label, &x, &y)) dm_click(x, y);
    dm_wait_load();
}

static void dm_hover(const char *name, f32 sec) {
    f32 x, y;
    if (dm_item(name, &x, &y)) dm_move(x, y, sec);
}

// Ctrl+L, a path typed, Enter.
static void dm_go_typed(const char *path) {
    dm_key(SDLK_L, SDL_KMOD_CTRL, "Ctrl+L");
    dm_hold(0.2f);
    dm_type(path, 16);
    dm_hold(0.3f);
    dm_key(SDLK_RETURN, 0, NULL);
    dm_wait_load();
}

static void dm_pointer_away(void) { // out of the way of what's being shown
    Pane *p = active_pane();
    dm_move(p->ly.list.x + p->ly.list.w - S(40), p->ly.list.y + p->ly.list.h - S(30), 0.5f);
}

// Title cards: fade a value (card or window) over sec.
static void dm_fade(f32 *v, f32 to, f32 sec) {
    f32 from = *v;
    int n = MAX(1, (int)(sec * DM_FPS));
    for (int i = 1; i <= n; i++) { *v = from + (to - from) * (f32)i / n; dm_frames(1); }
}

// --- the scenes ---

static void scene_huge(void) {
    dm_caption("Instant on huge folders", "200,000 files listed with sizes and dates in under 0.1 s - the UI never waits on the disk");
    dm_hold(0.8f);
    dm_open("Datasets");
    dm_hold(0.4f);
    dm_open("sensor-logs");
    dm_hold(1.0f);
    dm_pointer_away();
    dm_key(SDLK_END, 0, "End");
    dm_hold(0.9f);
    dm_key(SDLK_HOME, 0, "Home");
    dm_hold(0.6f);
}

static void scene_filter(void) {
    dm_caption("Filter as you type", "Ctrl+F narrows the folder instantly - fuzzy, wildcards, or everything below it");
    dm_key(SDLK_F, SDL_KMOD_CTRL, "Ctrl+F");
    dm_hold(0.3f);
    dm_type("error-1999", 9);
    dm_hold(1.6f);
    dm_key(SDLK_ESCAPE, 0, "Esc");
    dm_hold(0.3f);
    dm_key(SDLK_BACKSPACE, 0, "Backspace");
    dm_wait_load();
    dm_key(SDLK_BACKSPACE, 0, "Backspace");
    dm_wait_load();
    dm_hold(0.4f);
}

static void scene_goto(void) {
    dm_caption("Go to anything", "Ctrl+P fuzzy-finds any folder or file in your home, your recent folders and places");
    dm_key(SDLK_P, SDL_KMOD_CTRL, "Ctrl+P");
    dm_hold(0.4f);
    dm_type("wallpapers", 9);
    st_wait_pal();
    dm_hold(1.2f);
    dm_key(SDLK_RETURN, 0, "Enter");
    dm_wait_load();
    dm_hold(0.5f);
}

static void scene_preview(void) {
    dm_caption("Thumbnails and preview", "Grid views from small to huge; Space previews images, text, PDFs and videos");
    dm_key(SDLK_2, SDL_KMOD_CTRL | SDL_KMOD_SHIFT, "Ctrl+Shift+2");
    for (int k = 0; k < 60 && g_nthumbs && g_thumbs[0].state == 1; k++) dm_frames(1);
    dm_hold(0.8f);
    dm_select("nebula.jpg", 0);
    dm_hold(0.3f);
    dm_space();
    dm_hold(1.2f);
    dm_key(SDLK_RIGHT, 0, "\xE2\x86\x92");
    dm_hold(0.7f);
    dm_key(SDLK_RIGHT, 0, "\xE2\x86\x92");
    dm_hold(1.0f);
    dm_space();
    dm_hold(0.4f);
}

static void scene_panels(void) {
    dm_caption("Split panels, each with its own tabs", "Ctrl+\\ splits right, Ctrl+Shift+\\ down; Shift+F5 copies to the other panel");
    dm_key(SDLK_BACKSLASH, SDL_KMOD_CTRL, "Ctrl+\\");
    dm_wait_load();
    dm_hold(0.4f);
    dm_go_place("Desktop");
    dm_hold(0.5f);
    dm_key(SDLK_TAB, 0, "Tab");
    dm_hold(0.3f);
    dm_select("dusk.jpg", 0);
    dm_select("ember.jpg", SDL_KMOD_CTRL);
    dm_select("frost.jpg", SDL_KMOD_CTRL);
    dm_hold(0.4f);
    dm_key(SDLK_F5, SDL_KMOD_SHIFT, "Shift+F5");
    dm_hold(1.6f);
    dm_key(SDLK_TAB, 0, "Tab");
    dm_key(SDLK_T, SDL_KMOD_CTRL, "Ctrl+T");
    dm_wait_load();
    dm_hold(0.4f);
    dm_go_place("Projects");
    dm_hold(0.3f);
}

static void scene_git(void) {
    dm_caption("Git status at a glance", "Changes marked on files and folders; branch, ahead and behind right in the path bar");
    dm_open("file-explorer");
    dm_hold(1.5f);
    Pane *p = active_pane();
    f32 cw = git_chip_width(p);
    if (cw > 0) {
        dm_click(p->ly.pathbar.x + p->ly.pathbar.w - cw * 0.5f, p->ly.pathbar.y + p->ly.pathbar.h * 0.5f);
        dm_hold(2.6f);
        dm_key(SDLK_ESCAPE, 0, "Esc");
    }
    dm_hold(0.3f);
}

static void scene_terminal(void) {
    dm_caption("A terminal that follows you", "F4 opens a shell under the folder - going somewhere moves it, and a cd moves the folder");
    dm_key(SDLK_F4, 0, "F4");
    for (int k = 0; k < 90; k++) { // the prompt
        Tab *t = A.ap->cur;
        if (t->dock && term_settled(t->dock)) break;
        dm_frames(1);
    }
    dm_hold(0.5f);
    dm_type("git log --oneline -4", 14);
    dm_key(SDLK_RETURN, 0, "Enter");
    dm_hold(1.4f);
    dm_open("src");
    dm_hold(1.6f);
    dm_key(SDLK_F4, 0, "F4");
    dm_hold(0.3f);
}

static void scene_grep(void) {
    dm_caption("Search inside files", "Every matching line with its number, highlighted - ripgrep when it's installed");
    dm_key(SDLK_F, SDL_KMOD_CTRL | SDL_KMOD_SHIFT, "Ctrl+Shift+F");
    dm_hold(0.3f);
    dm_type("SDL_CreateWindow", 12);
    for (int k = 0; k < 60; k++) dm_frames(1);
    dm_hold(1.5f);
    dm_key(SDLK_ESCAPE, 0, "Esc");
    dm_key(SDLK_ESCAPE, 0, NULL);
    dm_hold(0.3f);
}

static void scene_du(void) {
    dm_caption("See what takes the space", "Ctrl+Shift+U maps a folder or a drive by size - clean up right there");
    dm_go_place("Home");
    dm_hold(0.3f);
    dm_key(SDLK_U, SDL_KMOD_CTRL | SDL_KMOD_SHIFT, "Ctrl+Shift+U");
    for (int k = 0; k < 90 && du_busy(); k++) dm_frames(1);
    dm_hold(0.6f);
    Pane *p = active_pane();
    Rect l = p->ly.list;
    dm_move(l.x + l.w * 0.2f, l.y + l.h * 0.3f, 0.6f);
    dm_hold(0.7f);
    dm_move(l.x + l.w * 0.45f, l.y + l.h * 0.6f, 0.6f);
    dm_hold(0.9f);
    dm_move(l.x + l.w * 0.3f, l.y + l.h * 0.75f, 0.5f);
    dm_hold(1.0f);
}

static void scene_compare(void) {
    dm_caption("Compare and sync folders", "New, newer and different files marked on both sides; sync one way, both, or mirror");
    dm_key(SDLK_6, SDL_KMOD_CTRL | SDL_KMOD_SHIFT, "Ctrl+Shift+6"); // details (leaves the map)
    dm_go_typed("~/Projects/website");
    dm_key(SDLK_TAB, 0, "Tab");
    dm_go_typed("~/Backup/website");
    st_key(SDLK_6, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
    dm_hold(0.3f);
    dm_key(SDLK_K, SDL_KMOD_CTRL | SDL_KMOD_SHIFT, "Ctrl+Shift+K");
    for (int k = 0; k < 90 && !cmp_ready(); k++) dm_frames(1);
    dm_hold(1.0f);
    dm_open("css");
    dm_hold(2.0f);
}

static void scene_archive(void) {
    dm_caption("Archives open like folders", "zip, tar, 7z, rar and iso - browse inside, drag files out, extract or compress");
    dm_key(SDLK_K, SDL_KMOD_CTRL | SDL_KMOD_SHIFT, "Ctrl+Shift+K");
    dm_go_place("Downloads");
    dm_hold(0.4f);
    dm_open("website-export.zip");
    dm_hold(0.6f);
    dm_open("website");
    dm_hold(1.6f);
}

static void scene_themes(void) {
    dm_caption("Make it yours", "Themes, accent colours, icon sets and fonts - and every shortcut can be rebound");
    dm_key(SDLK_COMMA, SDL_KMOD_CTRL, "Ctrl+,");
    dm_hold(1.0f);
    int swatches[] = {3, 6, 0}; // pink, green, the theme's own
    for (int i = 0; i < 3; i++) {
        OptHit *h = st_opt_hit(HP_SWATCH, OPT_ACCENT, swatches[i]);
        if (h) dm_click(h->r.x + h->r.w * 0.5f, h->r.y + h->r.h * 0.5f);
        dm_hold(0.8f);
    }
    int themes[] = {4, 1, 0}; // Nord, Light, Dark
    for (int i = 0; i < 3; i++) {
        OptHit *h = st_opt_hit(HP_DROP, OPT_THEME, 0);
        if (h) dm_click(h->r.x + h->r.w * 0.5f, h->r.y + h->r.h * 0.5f);
        dm_hold(0.4f);
        h = st_opt_hit(HP_DROPITEM, OPT_THEME, themes[i]);
        if (h) dm_click(h->r.x + h->r.w * 0.5f, h->r.y + h->r.h * 0.5f);
        dm_hold(1.3f);
    }
    dm_key(SDLK_ESCAPE, 0, "Esc");
    dm_hold(0.5f);
}

static int demo_run(void) {
    A.nnets = 0; // (this machine's network mounts)
    init_places();
    dm_start();
    st_wait();
    D.win = 0;
    D.card = 1;
    dm_fade(&D.card_a, 1, 0.8f);
    dm_hold(1.8f);
    dm_fade(&D.card_a, 0, 0.5f);
    D.card = 0;
    dm_fade(&D.win, 1, 0.7f);
    D.cursor = true;

    const char *only = getenv("FE_DEMO_SCENES"); // while working on it: "1,3" runs just those (from the start state)
    void (*scenes[])(void) = {scene_huge, scene_filter, scene_goto, scene_preview, scene_panels, scene_git, scene_terminal,
                              scene_grep, scene_du, scene_compare, scene_archive, scene_themes};
    for (int i = 0; i < (int)ARRAY_COUNT(scenes); i++) {
        char num[8];
        snprintf(num, sizeof num, "%d", i + 1);
        if (only && !strstr(only, num)) continue;
        scenes[i]();
    }

    dm_caption("", "");
    D.cursor = false;
    dm_fade(&D.win, 0, 0.6f);
    D.card = 2;
    dm_fade(&D.card_a, 1, 0.8f);
    dm_hold(3.5f);
    pclose(D.ff);
    D.ff = NULL;
    fprintf(stderr, "demo: %d frames (%.1f s) -> %s\n", D.frame, (f64)D.frame / DM_FPS, g_demo_out);
    return 0;
}
