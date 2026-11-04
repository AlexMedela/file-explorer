// demo.c - --demo SCRIPT OUT.mp4: play a script of keys and typing through the real input handlers, headless, at an
// even 30 fps, and pipe every frame to ffmpeg (launch videos, docs). Included from app.c after selftest.c.
//
// Script, one command per line (# comments):
//   size W H        window size in pixels (before the first recorded frame)
//   zoom Z          UI zoom (1.0 = 100%)
//   wait            until listings, scans, thumbnails, the palette and search have settled (not recorded:
//                   the video cuts straight past loading)
//   hold S          record S seconds
//   key CHORD       press a key ("Ctrl+P", "Down", "Enter"), logged for key overlays
//   type TEXT       type the rest of the line one character at a time (speed: typespeed S, per character)
//   cmd SLUG        run a command by its shortcut name in keys.c ("panel.split_right")
//   view N          the active pane's view, an index into g_zoom (0 details, 1 list, 2.. icon sizes)
//   mark NAME       note the current video time in the log (scene boundaries for editing)
//   shot FILE.bmp   save the current frame as a still

// --- demo recording -------------------------------------------------------------------------------------------

#define DEMO_FPS 30
static const char *g_demo_out;
static FILE *g_demo_pipe, *g_demo_log;
static pid_t g_demo_ffmpeg;
static int g_demo_w, g_demo_h, g_demo_frames;
static char g_demo_shot[PATH_MAX];

static u64 g_demo_t0; // when frame 0 (or the first frame after a wait) was due

static bool demo_open_pipe(int w, int h) {
    int fd[2];
    if (pipe(fd)) return false;
    char size[32];
    snprintf(size, sizeof size, "%dx%d", w, h);
    pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return false; }
    if (pid == 0) {
        dup2(fd[0], 0);
        close(fd[0]);
        close(fd[1]);
        execlp("ffmpeg", "ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgba", "-s", size, "-r", "30",
               "-i", "-", "-c:v", "libx264", "-preset", "medium", "-crf", "14", "-pix_fmt", "yuv420p", g_demo_out, (char *)NULL);
        _exit(127);
    }
    close(fd[0]);
    g_demo_pipe = fdopen(fd[1], "w");
    g_demo_ffmpeg = pid;
    g_demo_w = w;
    g_demo_h = h;
    return g_demo_pipe != NULL;
}

// Called from frame() just before presenting a recorded frame.
static void demo_capture(void) {
    SDL_Surface *s = SDL_RenderReadPixels(A.ren, NULL);
    if (!s) return;
    SDL_Surface *c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!c) return;
    if (g_demo_shot[0]) { SDL_SaveBMP(c, g_demo_shot); g_demo_shot[0] = 0; }
    if (!g_demo_pipe && !demo_open_pipe(c->w & ~1, c->h & ~1)) { fprintf(stderr, "demo: can't run ffmpeg\n"); A.quit = true; }
    if (g_demo_pipe && c->w >= g_demo_w && c->h >= g_demo_h) { // (cropped to even sizes for yuv420p)
        for (int y = 0; y < g_demo_h; y++) fwrite((u8 *)c->pixels + (size_t)y * (size_t)c->pitch, 4, (size_t)g_demo_w, g_demo_pipe);
        g_demo_frames++;
    }
    SDL_DestroySurface(c);
}

// One frame of video, due exactly 1/30 s after the previous one: rendering is faster than that, so waiting for the
// moment keeps frames evenly spaced in real time (animations, and the timings the app shows, stay true).
static void demo_frame(bool record) {
    if (record) {
        u64 due = g_demo_t0 + (u64)g_demo_frames * 1000000000ull / DEMO_FPS, now = now_ns();
        if (now < due) SDL_DelayNS(due - now);
    }
    A.demo_rec = record;
    A.redraw = true;
    st_frames(1);
    A.demo_rec = false;
}

static double demo_time(void) { return (double)g_demo_frames / DEMO_FPS; }

static void demo_wait(void) {
    for (int k = 0; k < 2000; k++) {
        st_frames(1);
        if (shot_ready() && !atomic_load(&g_index_building) && !A.pal_pending && !A.search_due && !animating() && !sheet_busy()) break;
        SDL_Delay(5);
        if (k == 1999) fprintf(stderr, "demo: timeout waiting for the app to settle\n");
    }
    g_demo_t0 = now_ns() - (u64)g_demo_frames * 1000000000ull / DEMO_FPS; // the video carries on from here
}

static void demo_hold(double secs) {
    int n = (int)lround(secs * DEMO_FPS);
    for (int i = 0; i < n && !A.quit; i++) demo_frame(true);
}

static SDL_Keymod demo_mods(u8 km) {
    return (SDL_Keymod)((km & KM_CTRL ? SDL_KMOD_LCTRL : 0) | (km & KM_SHIFT ? SDL_KMOD_LSHIFT : 0) |
                        (km & KM_ALT ? SDL_KMOD_LALT : 0) | (km & KM_SUPER ? SDL_KMOD_LGUI : 0));
}

static void demo_key(const char *name) {
    Chord c;
    if (!chord_parse(name, &c)) { fprintf(stderr, "demo: unknown key '%s'\n", name); return; }
    SDL_KeyboardEvent e;
    SDL_zero(e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key = c.key;
    e.mod = demo_mods(c.mod);
    e.down = true;
    SDL_SetModState(e.mod);
    layout();
    on_key(&e);
    SDL_SetModState(SDL_KMOD_NONE);
    if (g_demo_log) fprintf(g_demo_log, "%.3f key %s\n", demo_time(), name);
    demo_frame(true);
}

static void demo_cmd(const char *slug) {
    for (int i = 0; i < KEYDEF_COUNT; i++)
        if (!strcmp(g_keys[i].slug, slug)) {
            layout();
            run_command(g_keys[i].id);
            demo_frame(true);
            return;
        }
    fprintf(stderr, "demo: unknown command '%s'\n", slug);
}

static int run_demo(const char *script) {
    FILE *f = fopen(script, "r");
    if (!f) { perror(script); return 1; }
    signal(SIGCHLD, SIG_DFL); // (main ignores it; we wait for ffmpeg to finish the file)
    char logpath[PATH_MAX];
    snprintf(logpath, sizeof logpath, "%s.log", g_demo_out);
    g_demo_log = fopen(logpath, "w");
    g_demo_t0 = now_ns();
    double type_secs = 0.07;
    char line[1024];
    int lineno = 0;
    while (!A.quit && fgets(line, sizeof line, f)) {
        lineno++;
        line[strcspn(line, "\r\n")] = 0;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') continue;
        char *arg = p + strcspn(p, " \t");
        if (*arg) *arg++ = 0;
        while (*arg == ' ' || *arg == '\t') arg++;
        if (!strcmp(p, "size")) {
            int w = 0, h = 0;
            if (sscanf(arg, "%d %d", &w, &h) == 2 && !g_demo_pipe) {
                SDL_SetWindowSize(A.win, w, h);
                SDL_SyncWindow(A.win);
                st_frames(2);
            }
        } else if (!strcmp(p, "zoom")) {
            A.zoom = CLAMP((f32)atof(arg), 0.6f, 2.5f);
            A.scale = A.dpi_scale * A.zoom;
            reload_fonts();
            st_frames(1);
        } else if (!strcmp(p, "wait")) demo_wait();
        else if (!strcmp(p, "hold")) demo_hold(atof(arg));
        else if (!strcmp(p, "key")) demo_key(arg);
        else if (!strcmp(p, "typespeed")) type_secs = atof(arg);
        else if (!strcmp(p, "type")) {
            for (const char *c = arg; *c && !A.quit;) {
                char ch[8] = {0};
                int n = 1;
                while (n < 4 && (c[n] & 0xC0) == 0x80) n++; // whole UTF-8 sequences
                memcpy(ch, c, (size_t)n);
                c += n;
                on_text(ch);
                demo_hold(type_secs);
            }
        } else if (!strcmp(p, "cmd")) demo_cmd(arg);
        else if (!strcmp(p, "view")) { set_zoom_level(active_pane(), CLAMP(atoi(arg), 0, (int)ARRAY_COUNT(g_zoom) - 1)); demo_frame(true); }
        else if (!strcmp(p, "mark")) { if (g_demo_log) fprintf(g_demo_log, "%.3f mark %s\n", demo_time(), arg); }
        else if (!strcmp(p, "shot")) { snprintf(g_demo_shot, sizeof g_demo_shot, "%s", arg); demo_frame(true); }
        else fprintf(stderr, "demo: %s:%d: unknown command '%s'\n", script, lineno, p);
    }
    fclose(f);
    if (g_demo_log) { fprintf(g_demo_log, "%.3f end\n", demo_time()); fclose(g_demo_log); }
    int rc = 0;
    if (g_demo_pipe) {
        fclose(g_demo_pipe);
        int st = 0;
        waitpid(g_demo_ffmpeg, &st, 0);
        rc = WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : 1;
        printf("demo: %d frames (%.2fs) -> %s\n", g_demo_frames, demo_time(), g_demo_out);
    }
    SDL_DestroyRenderer(A.ren);
    SDL_DestroyWindow(A.win);
    SDL_Quit();
    return rc;
}
