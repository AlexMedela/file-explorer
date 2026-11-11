// dragout.c - dragging files out of the window into other apps (included from app.c, "drag & drop")
//
// SDL takes drops but can't start a drag, so this is the drag source for both window systems, loaded at run time
// (nothing to build against): X11's XDND on the connection SDL already has (its event hook shows us the replies), and
// Wayland's wl_data_source on SDL's connection, with our own objects on our own event queue, read on a thread of ours:
// when the files are dropped back onto this window, SDL reads them on the main thread while we have to answer.
// An item drag becomes one when the pointer leaves the window. Offered: the files as text/uri-list and their paths
// as plain text on X11 (terminals, text fields). Actions follow the drop settings (dragout_pick).

#include <sys/mman.h> // memfd_create, mmap (the drag icon)

enum { DO_COPY = 1, DO_MOVE = 2, DO_ASK = 4, DO_LINK = 8 };
enum { DOS_NONE, DOS_X11, DOS_WAYLAND };

static struct {
    int sys;               // DOS_*: what this window can drag out with (DOS_NONE: offscreen, failed to load)
    bool active;           // a drag out of ours is going on
    bool over_self;        // (Wayland) it's over this window again: SDL reports it as a drop target
    pthread_mutex_t mtx;   // the offered data (read by the Wayland thread)
    char *uri, *plain;
    int offer, pref;       // DO_* offered / asked for
    Pane *src;             // where the items came from (a drop back onto this window is an internal one)
    char **paths;
    int n;
    u64 t_end;             // when the last one ended
    bool pft_tried;        // the portal key for this drag (asked for the first time a target wants it)
    char pft[256];
} g_do = {.mtx = PTHREAD_MUTEX_INITIALIZER};

static void dragout_ended(void); // app side: the drag is over (dropped elsewhere or cancelled)
static u32 *drag_badge_pixels(bool square, int *w, int *h); // render.c: the card shown next to the pointer
static void pft_probe(void);                                // integration.c: the documents portal's file transfer,
static bool pft_available(void);                            // for sandboxed (Flatpak, Snap) targets
static bool pft_key(char **paths, int n, char *key, size_t cap);
#define PFT_MIME "application/vnd.portal.filetransfer"

// What the target may do, from the same keys and setting as a drop inside: Ctrl copy, Shift move, Ctrl+Shift link,
// else the "ask" setting, else move (the target, which knows its own folder, may still pick copy - other drives).
static void dragout_pick(int *offer, int *pref) {
    SDL_Keymod mod = SDL_GetModState();
    bool c = mod & SDL_KMOD_CTRL, s = mod & SDL_KMOD_SHIFT;
    if (c && s) { *offer = DO_LINK | DO_COPY; *pref = DO_LINK; }
    else if (c) { *offer = DO_COPY; *pref = DO_COPY; }
    else if (s) { *offer = DO_MOVE; *pref = DO_MOVE; }
    else if (A.drop_ask) { *offer = DO_COPY | DO_MOVE | DO_LINK | DO_ASK; *pref = DO_ASK; }
    else { *offer = DO_COPY | DO_MOVE; *pref = DO_MOVE; }
}

// The offered data: "file:///a%20b\r\n..." and "/a b\n...".
static void dragout_data(char **paths, int n) {
    size_t cap = 16;
    for (int i = 0; i < n; i++) cap += strlen(paths[i]) * 3 + 16;
    char *uri = xmalloc(cap), *plain = xmalloc(cap);
    size_t u = 0, t = 0;
    uri[0] = plain[0] = 0;
    for (int i = 0; i < n; i++) {
        char enc[PATH_MAX * 3];
        url_encode_path(paths[i], enc, sizeof enc);
        u += (size_t)snprintf(uri + u, cap - u, "file://%s\r\n", enc);
        t += (size_t)snprintf(plain + t, cap - t, "%s%s", i ? "\n" : "", paths[i]);
    }
    pthread_mutex_lock(&g_do.mtx);
    free(g_do.uri);
    free(g_do.plain);
    g_do.uri = uri;
    g_do.plain = plain;
    pthread_mutex_unlock(&g_do.mtx);
    strv_free(g_do.paths, g_do.n);
    g_do.paths = strv_dup(paths, n);
    g_do.n = n;
    g_do.pft_tried = false;
    g_do.pft[0] = 0;
}

// The portal key for this drag's files ("" if there's none), made the first time a target asks (any thread).
static void dragout_pft(char *out, size_t cap) {
    pthread_mutex_lock(&g_do.mtx);
    if (!g_do.pft_tried) {
        g_do.pft_tried = true;
        if (!pft_key(g_do.paths, g_do.n, g_do.pft, sizeof g_do.pft)) g_do.pft[0] = 0;
    }
    snprintf(out, cap, "%s", g_do.pft);
    pthread_mutex_unlock(&g_do.mtx);
}

static void dragout_finish(void) { // (main thread) the drag is over, whatever happened
    if (!g_do.active) return;
    g_do.active = g_do.over_self = false;
    g_do.t_end = now_ns();
    dragout_ended();
}

// --- X11 (XDND source) -------------------------------------------------------------------------------------------
// Find the XdndAware window under the pointer, Enter / Position (one at a time: the next after its Status) / Leave
// to it, Drop on release when it said yes, then wait for Finished. The data is read with XConvertSelection on
// XdndSelection, which we own: SelectionRequest -> the property, SelectionNotify.

typedef struct { int type; unsigned long serial; int send_event; void *display; unsigned long window, message_type; int format;
                 union { char b[20]; short s[10]; long l[5]; } data; } DoXClient;
typedef struct { int type; unsigned long serial; int send_event; void *display; unsigned long owner, requestor, selection, target, property, time; } DoXSelReq;
typedef struct { int type; unsigned long serial; int send_event; void *display; unsigned long requestor, selection, target, property, time; } DoXSelNotify;
typedef union { int type; DoXClient cm; DoXSelReq sr; DoXSelNotify sn; long pad[24]; } DoXEvent;
typedef struct { unsigned long background_pixmap, background_pixel, border_pixmap, border_pixel; int bit_gravity, win_gravity, backing_store;
                 unsigned long backing_planes, backing_pixel; int save_under; long event_mask, do_not_propagate_mask; int override_redirect;
                 unsigned long colormap, cursor; } DoXWinAttr; // XSetWindowAttributes
enum { X_SELECTION_REQUEST = 30, X_SELECTION_NOTIFY = 31, X_CLIENT_MESSAGE = 33, X_EXPOSE = 12, X_ATOM = 4, X_STRING = 31 };
enum { X_CW_BACKING_STORE = 1 << 6, X_CW_OVERRIDE_REDIRECT = 1 << 9, X_CW_SAVE_UNDER = 1 << 10, X_CW_EVENT_MASK = 1 << 11,
       X_EXPOSURE_MASK = 1 << 15, X_ALWAYS = 2, X_ZPIXMAP = 2, X_INPUT_OUTPUT = 1 };

static struct {
    void *dpy;
    unsigned long win, root;
    unsigned long (*intern)(void *, const char *, int);
    int (*change)(void *, unsigned long, unsigned long, unsigned long, int, int, const unsigned char *, int);
    int (*set_owner)(void *, unsigned long, unsigned long, unsigned long);
    int (*send)(void *, unsigned long, int, long, DoXEvent *);
    int (*translate)(void *, unsigned long, unsigned long, int, int, int *, int *, unsigned long *);
    int (*getprop)(void *, unsigned long, unsigned long, long, long, int, unsigned long, unsigned long *, int *, unsigned long *,
                   unsigned long *, unsigned char **);
    int (*xfree)(void *);
    int (*flush)(void *);
    int (*query)(void *, unsigned long, unsigned long *, unsigned long *, int *, int *, int *, int *, unsigned int *);
    unsigned long (*root_of)(void *);
    // the card following the pointer outside the window (X11 has no drag image: a window of ours, like GTK's)
    unsigned long (*create_window)(void *, unsigned long, int, int, unsigned int, unsigned int, unsigned int, int, unsigned int, void *,
                                   unsigned long, DoXWinAttr *);
    void *(*create_image)(void *, void *, unsigned int, int, int, char *, unsigned int, unsigned int, int, int);
    int (*put_image)(void *, unsigned long, void *, void *, int, int, int, int, unsigned int, unsigned int);
    void *(*default_gc)(void *, int);
    void *(*default_visual)(void *, int);
    int (*default_depth)(void *, int);
    int (*default_screen)(void *);
    int (*map_raised)(void *, unsigned long);
    int (*unmap)(void *, unsigned long);
    int (*move)(void *, unsigned long, int, int);
    int (*destroy_window)(void *, unsigned long);
    unsigned long card;  // its window (0: none)
    void *card_img;      // (XImage over card_px)
    u32 *card_px;
    int card_w, card_h;
    bool card_shown;
    unsigned long a_aware, a_enter, a_position, a_status, a_leave, a_drop, a_finished, a_sel, a_typelist, a_actlist, a_actdesc;
    unsigned long a_copy, a_move, a_link, a_ask, a_uri, a_textutf8, a_utf8, a_text, a_targets, a_pft;
    unsigned long target; // the XdndAware window under the pointer (0: none, or this window)
    int target_ver;
    bool status_wait, pos_pending, accepted, dropped;
    int rx, ry;
    u64 t_drop;
} g_xd;

static void x_msg(unsigned long to, unsigned long type, long l0, long l1, long l2, long l3, long l4) {
    DoXEvent e;
    memset(&e, 0, sizeof e);
    e.cm.type = X_CLIENT_MESSAGE;
    e.cm.display = g_xd.dpy;
    e.cm.window = to;
    e.cm.message_type = type;
    e.cm.format = 32;
    e.cm.data.l[0] = l0;
    e.cm.data.l[1] = l1;
    e.cm.data.l[2] = l2;
    e.cm.data.l[3] = l3;
    e.cm.data.l[4] = l4;
    g_xd.send(g_xd.dpy, to, 0, 0, &e);
}

static unsigned long x_action(int a) {
    return a == DO_COPY ? g_xd.a_copy : a == DO_LINK ? g_xd.a_link : a == DO_ASK ? g_xd.a_ask : g_xd.a_move;
}

// The window under root position (rx, ry) that takes drops (its XDND version in *ver), 0 if none.
static unsigned long x_find_target(int rx, int ry, int *ver) {
    unsigned long w = g_xd.root;
    for (int depth = 0; depth < 32; depth++) {
        if (w != g_xd.root) {
            unsigned long type = 0, n = 0, after = 0;
            int fmt = 0;
            unsigned char *data = NULL;
            if (g_xd.getprop(g_xd.dpy, w, g_xd.a_aware, 0, 1, 0, X_ATOM, &type, &fmt, &n, &after, &data) == 0 && data) {
                bool aware = type == X_ATOM && n >= 1;
                if (aware) *ver = (int)*(long *)data;
                g_xd.xfree(data);
                if (aware) return w;
            }
        }
        int x = 0, y = 0;
        unsigned long child = 0;
        if (!g_xd.translate(g_xd.dpy, g_xd.root, w, rx, ry, &x, &y, &child) || !child) return 0;
        w = child;
    }
    return 0;
}

static void x_position(void) {
    x_msg(g_xd.target, g_xd.a_position, (long)g_xd.win, 0, ((long)g_xd.rx << 16) | (g_xd.ry & 0xFFFF), 0, (long)x_action(g_do.pref));
    g_xd.status_wait = true;
    g_xd.pos_pending = false;
}

static void x_leave(void) {
    if (g_xd.target) x_msg(g_xd.target, g_xd.a_leave, (long)g_xd.win, 0, 0, 0, 0);
    g_xd.target = 0;
    g_xd.accepted = g_xd.status_wait = g_xd.pos_pending = false;
}

static void x_card_paint(void) {
    if (g_xd.card && g_xd.card_img) {
        int scr = g_xd.default_screen(g_xd.dpy);
        g_xd.put_image(g_xd.dpy, g_xd.card, g_xd.default_gc(g_xd.dpy, scr), g_xd.card_img, 0, 0, 0, 0, (unsigned)g_xd.card_w, (unsigned)g_xd.card_h);
    }
}

static void x_card_make(void) {
    if (!g_xd.create_window || g_xd.card) return;
    g_xd.card_px = drag_badge_pixels(true, &g_xd.card_w, &g_xd.card_h);
    if (!g_xd.card_px) return;
    int scr = g_xd.default_screen(g_xd.dpy);
    if (g_xd.default_depth(g_xd.dpy, scr) < 24) { free(g_xd.card_px); g_xd.card_px = NULL; return; } // (32-bit pixels only)
    DoXWinAttr at;
    memset(&at, 0, sizeof at);
    at.override_redirect = 1; // no window manager: no frame, no focus, it just floats
    at.save_under = 1;
    at.backing_store = X_ALWAYS;
    at.event_mask = X_EXPOSURE_MASK;
    g_xd.card = g_xd.create_window(g_xd.dpy, g_xd.root, -10000, -10000, (unsigned)g_xd.card_w, (unsigned)g_xd.card_h, 0, 0, X_INPUT_OUTPUT, NULL,
                                   X_CW_OVERRIDE_REDIRECT | X_CW_SAVE_UNDER | X_CW_BACKING_STORE | X_CW_EVENT_MASK, &at);
    g_xd.card_img = g_xd.create_image(g_xd.dpy, g_xd.default_visual(g_xd.dpy, scr), (unsigned)g_xd.default_depth(g_xd.dpy, scr), X_ZPIXMAP, 0,
                                      (char *)g_xd.card_px, (unsigned)g_xd.card_w, (unsigned)g_xd.card_h, 32, 0);
    g_xd.card_shown = false;
}

static void x_card_free(void) {
    if (g_xd.card) g_xd.destroy_window(g_xd.dpy, g_xd.card);
    if (g_xd.card_img) g_xd.xfree(g_xd.card_img); // (the struct; its pixels are ours)
    free(g_xd.card_px);
    g_xd.card = 0;
    g_xd.card_img = NULL;
    g_xd.card_px = NULL;
}

// Show it next to the pointer outside the window (inside, the window draws its own).
static void x_card_follow(bool outside) {
    if (!g_xd.card) return;
    if (!outside) {
        if (g_xd.card_shown) g_xd.unmap(g_xd.dpy, g_xd.card);
        g_xd.card_shown = false;
        return;
    }
    g_xd.move(g_xd.dpy, g_xd.card, g_xd.rx + (int)roundf(S(18)), g_xd.ry + (int)roundf(S(20)));
    if (!g_xd.card_shown) {
        g_xd.map_raised(g_xd.dpy, g_xd.card);
        x_card_paint();
    }
    g_xd.card_shown = true;
}

// Pointer moved during the drag: follow the window under it (this window itself: none - the drag inside goes on).
static void x_motion(void) {
    unsigned long r = 0, c = 0;
    int wx, wy;
    unsigned int mask;
    if (!g_xd.query(g_xd.dpy, g_xd.root, &r, &c, &g_xd.rx, &g_xd.ry, &wx, &wy, &mask)) return;
    int ver = 0;
    unsigned long t = x_find_target(g_xd.rx, g_xd.ry, &ver);
    x_card_follow(t != g_xd.win && !(A.mx >= 0 && A.my >= 0 && A.mx < A.W && A.my < A.H));
    if (t == g_xd.win || ver < 3) t = 0;
    if (t != g_xd.target) {
        x_leave();
        g_xd.target = t;
        g_xd.target_ver = MIN(ver, 5);
        if (t) x_msg(t, g_xd.a_enter, (long)g_xd.win, ((long)g_xd.target_ver << 24) | 1, (long)g_xd.a_uri, (long)g_xd.a_textutf8, (long)g_xd.a_utf8);
    }
    if (g_xd.target) {
        if (g_xd.status_wait) g_xd.pos_pending = true;
        else x_position();
    }
    g_xd.flush(g_xd.dpy);
}

static void x_end(void) {
    x_card_free();
    x_leave();
    g_xd.dropped = false;
    g_xd.flush(g_xd.dpy);
    dragout_finish();
}

// SDL's X event hook: the target's replies and its requests for the data.
static bool SDLCALL x_hook(void *ud, XEvent *xe) {
    (void)ud;
    DoXEvent *e = (DoXEvent *)xe;
    if (e->type == X_CLIENT_MESSAGE && e->cm.window == g_xd.win) {
        if (e->cm.message_type == g_xd.a_status && g_xd.target && (unsigned long)e->cm.data.l[0] == g_xd.target) {
            g_xd.accepted = e->cm.data.l[1] & 1;
            g_xd.status_wait = false;
            if (g_xd.pos_pending && !g_xd.dropped) x_position();
            g_xd.flush(g_xd.dpy);
            return false;
        }
        if (e->cm.message_type == g_xd.a_finished) {
            if (g_xd.dropped) x_end();
            return false;
        }
    }
    if (e->type == X_EXPOSE && g_xd.card && e->cm.window == g_xd.card) { x_card_paint(); return false; } // (window: same place)
    if (e->type == X_SELECTION_REQUEST && e->sr.selection == g_xd.a_sel && e->sr.owner == g_xd.win) {
        DoXEvent r;
        memset(&r, 0, sizeof r);
        r.sn.type = X_SELECTION_NOTIFY;
        r.sn.display = g_xd.dpy;
        r.sn.requestor = e->sr.requestor;
        r.sn.selection = e->sr.selection;
        r.sn.target = e->sr.target;
        r.sn.time = e->sr.time;
        unsigned long prop = e->sr.property ? e->sr.property : e->sr.target;
        r.sn.property = prop;
        pthread_mutex_lock(&g_do.mtx);
        if (e->sr.target == g_xd.a_targets) {
            long tl[6] = {(long)g_xd.a_targets, (long)g_xd.a_uri, (long)g_xd.a_textutf8, (long)g_xd.a_utf8, (long)g_xd.a_text, (long)g_xd.a_pft};
            g_xd.change(g_xd.dpy, e->sr.requestor, prop, X_ATOM, 32, 0, (const unsigned char *)tl, pft_available() ? 6 : 5);
        } else if (e->sr.target == g_xd.a_pft && pft_available()) {
            pthread_mutex_unlock(&g_do.mtx); // (dragout_pft takes it)
            char key[256];
            dragout_pft(key, sizeof key);
            pthread_mutex_lock(&g_do.mtx);
            if (key[0]) g_xd.change(g_xd.dpy, e->sr.requestor, prop, g_xd.a_pft, 8, 0, (const unsigned char *)key, (int)strlen(key) + 1);
            else r.sn.property = 0;
        } else if (e->sr.target == g_xd.a_uri && g_do.uri) {
            g_xd.change(g_xd.dpy, e->sr.requestor, prop, g_xd.a_uri, 8, 0, (const unsigned char *)g_do.uri, (int)strlen(g_do.uri));
        } else if ((e->sr.target == g_xd.a_textutf8 || e->sr.target == g_xd.a_utf8 || e->sr.target == g_xd.a_text) && g_do.plain) {
            g_xd.change(g_xd.dpy, e->sr.requestor, prop, e->sr.target, 8, 0, (const unsigned char *)g_do.plain, (int)strlen(g_do.plain));
        } else r.sn.property = 0; // (None: not something we have)
        pthread_mutex_unlock(&g_do.mtx);
        g_xd.send(g_xd.dpy, e->sr.requestor, 0, 0, &r);
        g_xd.flush(g_xd.dpy);
        return false;
    }
    return true;
}

static bool x_init(void) {
    const char *drv = SDL_GetCurrentVideoDriver();
    void *l = drv && !strcmp(drv, "x11") ? dlopen("libX11.so.6", RTLD_NOW | RTLD_NOLOAD) : NULL;
    if (!l) return false;
    *(void **)&g_xd.intern = dlsym(l, "XInternAtom");
    *(void **)&g_xd.change = dlsym(l, "XChangeProperty");
    *(void **)&g_xd.set_owner = dlsym(l, "XSetSelectionOwner");
    *(void **)&g_xd.send = dlsym(l, "XSendEvent");
    *(void **)&g_xd.translate = dlsym(l, "XTranslateCoordinates");
    *(void **)&g_xd.getprop = dlsym(l, "XGetWindowProperty");
    *(void **)&g_xd.xfree = dlsym(l, "XFree");
    *(void **)&g_xd.flush = dlsym(l, "XFlush");
    *(void **)&g_xd.query = dlsym(l, "XQueryPointer");
    *(void **)&g_xd.root_of = dlsym(l, "XDefaultRootWindow");
    *(void **)&g_xd.create_window = dlsym(l, "XCreateWindow");
    *(void **)&g_xd.create_image = dlsym(l, "XCreateImage");
    *(void **)&g_xd.put_image = dlsym(l, "XPutImage");
    *(void **)&g_xd.default_gc = dlsym(l, "XDefaultGC");
    *(void **)&g_xd.default_visual = dlsym(l, "XDefaultVisual");
    *(void **)&g_xd.default_depth = dlsym(l, "XDefaultDepth");
    *(void **)&g_xd.default_screen = dlsym(l, "XDefaultScreen");
    *(void **)&g_xd.map_raised = dlsym(l, "XMapRaised");
    *(void **)&g_xd.unmap = dlsym(l, "XUnmapWindow");
    *(void **)&g_xd.move = dlsym(l, "XMoveWindow");
    *(void **)&g_xd.destroy_window = dlsym(l, "XDestroyWindow");
    if (!g_xd.create_image || !g_xd.put_image || !g_xd.default_gc || !g_xd.default_visual || !g_xd.default_depth || !g_xd.default_screen ||
        !g_xd.map_raised || !g_xd.unmap || !g_xd.move || !g_xd.destroy_window)
        g_xd.create_window = NULL; // (no card then)
    if (!g_xd.intern || !g_xd.change || !g_xd.set_owner || !g_xd.send || !g_xd.translate || !g_xd.getprop || !g_xd.xfree || !g_xd.flush ||
        !g_xd.query || !g_xd.root_of)
        return false;
    SDL_PropertiesID pr = SDL_GetWindowProperties(A.win);
    g_xd.dpy = SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    g_xd.win = (unsigned long)SDL_GetNumberProperty(pr, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (!g_xd.dpy || !g_xd.win) return false;
    g_xd.root = g_xd.root_of(g_xd.dpy);
    static const struct { unsigned long *a; const char *name; } atoms[] = {
        {&g_xd.a_aware, "XdndAware"}, {&g_xd.a_enter, "XdndEnter"}, {&g_xd.a_position, "XdndPosition"}, {&g_xd.a_status, "XdndStatus"},
        {&g_xd.a_leave, "XdndLeave"}, {&g_xd.a_drop, "XdndDrop"}, {&g_xd.a_finished, "XdndFinished"}, {&g_xd.a_sel, "XdndSelection"},
        {&g_xd.a_typelist, "XdndTypeList"}, {&g_xd.a_actlist, "XdndActionList"}, {&g_xd.a_actdesc, "XdndActionDescription"},
        {&g_xd.a_copy, "XdndActionCopy"}, {&g_xd.a_move, "XdndActionMove"}, {&g_xd.a_link, "XdndActionLink"},
        {&g_xd.a_ask, "XdndActionAsk"}, {&g_xd.a_uri, "text/uri-list"}, {&g_xd.a_textutf8, "text/plain;charset=utf-8"},
        {&g_xd.a_utf8, "UTF8_STRING"}, {&g_xd.a_text, "text/plain"}, {&g_xd.a_targets, "TARGETS"}, {&g_xd.a_pft, PFT_MIME},
    };
    for (size_t i = 0; i < ARRAY_COUNT(atoms); i++) *atoms[i].a = g_xd.intern(g_xd.dpy, atoms[i].name, 0);
    SDL_SetX11EventHook(x_hook, NULL);
    return true;
}

static void x_start(void) {
    g_xd.win = (unsigned long)SDL_GetNumberProperty(SDL_GetWindowProperties(A.win), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, (Sint64)g_xd.win);
    g_xd.target = 0;
    g_xd.dropped = g_xd.accepted = g_xd.status_wait = g_xd.pos_pending = false;
    g_xd.set_owner(g_xd.dpy, g_xd.a_sel, g_xd.win, 0);
    long types[5] = {(long)g_xd.a_uri, (long)g_xd.a_textutf8, (long)g_xd.a_utf8, (long)g_xd.a_text, (long)g_xd.a_pft};
    g_xd.change(g_xd.dpy, g_xd.win, g_xd.a_typelist, X_ATOM, 32, 0, (const unsigned char *)types, pft_available() ? 5 : 4);
    long acts[3] = {(long)g_xd.a_copy, (long)g_xd.a_move, (long)g_xd.a_link};
    static const char desc[] = "Copy here\0Move here\0Link here";
    g_xd.change(g_xd.dpy, g_xd.win, g_xd.a_actlist, X_ATOM, 32, 0, (const unsigned char *)acts, 3);
    g_xd.change(g_xd.dpy, g_xd.win, g_xd.a_actdesc, X_STRING, 8, 0, (const unsigned char *)desc, (int)sizeof desc);
    x_card_make();
    x_motion();
}

// Mouse button released: true if it was dropped on another app (else the drop is ours, or nothing: the drag inside
// ends as usual, so this one just stops).
static bool x_release(void) {
    x_card_free();
    if (g_xd.target && g_xd.accepted) {
        x_msg(g_xd.target, g_xd.a_drop, (long)g_xd.win, 0, 0, 0, 0);
        g_xd.dropped = true;
        g_xd.t_drop = now_ns();
        g_xd.flush(g_xd.dpy);
        return true;
    }
    x_leave();
    g_xd.flush(g_xd.dpy);
    g_do.active = false;
    g_do.t_end = now_ns();
    return false;
}

// --- Wayland (wl_data_source) ----------------------------------------------------------------------------------
// Our own wl_seat + wl_pointer on SDL's connection (every pointer of a seat gets the same events: the serial of the
// button press that started the drag), a data device, and per drag a data source started from SDL's surface.
// The protocol is spoken through libwayland-client's generic calls (opcodes from wayland.xml).

static struct {
    void *dpy, *surface, *queue, *wrap, *registry, *seat, *ddm, *dd, *pointer, *src;
    void *compositor, *shm, *icon, *icon_pool, *icon_buf; // the card the compositor shows next to the pointer
    _Atomic uint32_t press_serial;
    _Atomic int result; // (thread -> main) 1 dropped, 2 cancelled
    pthread_t thr;
    int stop[2];
    void *(*create_queue)(void *);
    void *(*create_wrapper)(void *);
    void (*set_queue)(void *, void *);
    void *(*marshal)(void *, uint32_t, const void *, uint32_t, uint32_t, ...);
    int (*add_listener)(void *, void (**)(void), void *);
    uint32_t (*version)(void *);
    void (*destroy)(void *);
    int (*roundtrip)(void *, void *);
    int (*dispatch_pending)(void *, void *);
    int (*prepare_read)(void *, void *);
    int (*read_events)(void *);
    void (*cancel_read)(void *);
    int (*flush)(void *);
    int (*get_fd)(void *);
    const void *i_registry, *i_seat, *i_pointer, *i_ddm, *i_dd, *i_src, *i_offer;
    const void *i_compositor, *i_shm, *i_pool, *i_buffer, *i_surface;
} g_wl;

#define W_DESTROY 1 // WL_MARSHAL_FLAG_DESTROY

static const char *w_iface_name(const void *iface) { return *(const char *const *)iface; } // (struct wl_interface's first field)

static void *w_bind(uint32_t name, const void *iface, uint32_t ver) {
    return g_wl.marshal(g_wl.registry, 0, iface, ver, 0, name, w_iface_name(iface), ver, NULL);
}

// listeners (event order from wayland.xml; versions above what we bind never arrive)
static void w_noop_ptr(void *d, void *o) { (void)d; (void)o; }
static void w_pointer_enter(void *d, void *p, uint32_t serial, void *surf, int32_t x, int32_t y) { (void)d; (void)p; (void)serial; (void)surf; (void)x; (void)y; }
static void w_pointer_leave(void *d, void *p, uint32_t serial, void *surf) { (void)d; (void)p; (void)serial; (void)surf; }
static void w_pointer_motion(void *d, void *p, uint32_t t, int32_t x, int32_t y) { (void)d; (void)p; (void)t; (void)x; (void)y; }
static void w_pointer_button(void *d, void *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state) {
    (void)d; (void)p; (void)t; (void)button;
    if (state == 1) atomic_store(&g_wl.press_serial, serial);
}
static void w_pointer_axis(void *d, void *p, uint32_t t, uint32_t axis, int32_t v) { (void)d; (void)p; (void)t; (void)axis; (void)v; }
static void (*g_wl_pointer_l[])(void) = {(void (*)(void))w_pointer_enter, (void (*)(void))w_pointer_leave, (void (*)(void))w_pointer_motion,
                                        (void (*)(void))w_pointer_button, (void (*)(void))w_pointer_axis};

static void w_seat_caps(void *d, void *seat, uint32_t caps) {
    (void)d;
    if ((caps & 1) && !g_wl.pointer) {
        g_wl.pointer = g_wl.marshal(seat, 0, g_wl.i_pointer, g_wl.version(seat), 0, NULL);
        if (g_wl.pointer) g_wl.add_listener(g_wl.pointer, g_wl_pointer_l, NULL);
    }
}
static void w_seat_name(void *d, void *seat, const char *name) { (void)d; (void)seat; (void)name; }
static void (*g_wl_seat_l[])(void) = {(void (*)(void))w_seat_caps, (void (*)(void))w_seat_name};

static void w_global(void *d, void *reg, uint32_t name, const char *iface, uint32_t ver) {
    (void)d; (void)reg;
    if (!g_wl.seat && !strcmp(iface, "wl_seat")) {
        g_wl.seat = w_bind(name, g_wl.i_seat, 1);
        if (g_wl.seat) g_wl.add_listener(g_wl.seat, g_wl_seat_l, NULL); // (now: its capabilities come right away)
    }
    else if (!g_wl.ddm && !strcmp(iface, "wl_data_device_manager") && ver >= 3) g_wl.ddm = w_bind(name, g_wl.i_ddm, 3);
    else if (!g_wl.compositor && !strcmp(iface, "wl_compositor") && g_wl.i_compositor) g_wl.compositor = w_bind(name, g_wl.i_compositor, MIN(ver, 4u));
    else if (!g_wl.shm && !strcmp(iface, "wl_shm") && g_wl.i_shm) g_wl.shm = w_bind(name, g_wl.i_shm, 1);
}
static void w_global_remove(void *d, void *reg, uint32_t name) { (void)d; (void)reg; (void)name; }
static void (*g_wl_registry_l[])(void) = {(void (*)(void))w_global, (void (*)(void))w_global_remove};

// Our data device also hears about drags over this window and the clipboard: SDL handles those; drop the offers.
static void w_dd_offer(void *d, void *dd, void *offer) {
    (void)d; (void)dd;
    if (offer) g_wl.marshal(offer, 2, NULL, g_wl.version(offer), W_DESTROY); // wl_data_offer.destroy
}
static void w_dd_enter(void *d, void *dd, uint32_t serial, void *surf, int32_t x, int32_t y, void *offer) { (void)d; (void)dd; (void)serial; (void)surf; (void)x; (void)y; (void)offer; }
static void w_dd_motion(void *d, void *dd, uint32_t t, int32_t x, int32_t y) { (void)d; (void)dd; (void)t; (void)x; (void)y; }
static void w_dd_selection(void *d, void *dd, void *offer) { (void)d; (void)dd; (void)offer; }
static void (*g_wl_dd_l[])(void) = {(void (*)(void))w_dd_offer, (void (*)(void))w_dd_enter, (void (*)(void))w_noop_ptr,
                                   (void (*)(void))w_dd_motion, (void (*)(void))w_noop_ptr, (void (*)(void))w_dd_selection};

// the data source (our thread): hand out the data; dropped / cancelled -> the main thread
static void w_src_target(void *d, void *s, const char *mime) { (void)d; (void)s; (void)mime; }
static void w_src_send(void *d, void *s, const char *mime, int32_t fd) {
    (void)d; (void)s;
    char *data;
    size_t len;
    if (!strcmp(mime, PFT_MIME)) { // the portal key, with its NUL like GTK sends it
        char key[256];
        dragout_pft(key, sizeof key);
        data = strdup(key);
        len = key[0] ? strlen(key) + 1 : 0;
    } else {
        pthread_mutex_lock(&g_do.mtx);
        data = strdup(!strcmp(mime, "text/uri-list") ? (g_do.uri ? g_do.uri : "") : (g_do.plain ? g_do.plain : ""));
        pthread_mutex_unlock(&g_do.mtx);
        len = strlen(data);
    }
    size_t o = 0;
    while (o < len) { // (SIGPIPE is blocked on this thread: a reader that went away is just EPIPE)
        ssize_t w = write(fd, data + o, len - o);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) break;
        o += (size_t)w;
    }
    close(fd);
    free(data);
}
static void w_src_end(int how) {
    atomic_store(&g_wl.result, how);
    main_wake();
}
static void w_src_cancelled(void *d, void *s) { (void)d; (void)s; w_src_end(2); }
static void w_src_finished(void *d, void *s) { (void)d; (void)s; w_src_end(1); }
static void w_src_action(void *d, void *s, uint32_t a) { (void)d; (void)s; (void)a; }
static void (*g_wl_src_l[])(void) = {(void (*)(void))w_src_target, (void (*)(void))w_src_send, (void (*)(void))w_src_cancelled,
                                    (void (*)(void))w_noop_ptr, (void (*)(void))w_src_finished, (void (*)(void))w_src_action};

// Reads and handles our queue's events next to SDL (libwayland lets several threads read one connection).
static void *w_thread(void *arg) {
    (void)arg;
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &ss, NULL);
    struct pollfd pf[2] = {{.fd = g_wl.get_fd(g_wl.dpy), .events = POLLIN}, {.fd = g_wl.stop[0], .events = POLLIN}};
    for (;;) {
        while (g_wl.prepare_read(g_wl.dpy, g_wl.queue) != 0) g_wl.dispatch_pending(g_wl.dpy, g_wl.queue);
        g_wl.flush(g_wl.dpy);
        pf[0].revents = pf[1].revents = 0;
        if (poll(pf, 2, -1) < 0 && errno != EINTR) { g_wl.cancel_read(g_wl.dpy); break; }
        if (pf[1].revents) { g_wl.cancel_read(g_wl.dpy); break; }
        if (pf[0].revents & POLLIN) g_wl.read_events(g_wl.dpy);
        else g_wl.cancel_read(g_wl.dpy);
        g_wl.dispatch_pending(g_wl.dpy, g_wl.queue);
    }
    return NULL;
}

static bool w_init(void) {
    const char *drv = SDL_GetCurrentVideoDriver();
    void *l = drv && !strcmp(drv, "wayland") ? dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_NOLOAD) : NULL;
    if (!l) return false;
    *(void **)&g_wl.create_queue = dlsym(l, "wl_display_create_queue");
    *(void **)&g_wl.create_wrapper = dlsym(l, "wl_proxy_create_wrapper");
    *(void **)&g_wl.set_queue = dlsym(l, "wl_proxy_set_queue");
    *(void **)&g_wl.marshal = dlsym(l, "wl_proxy_marshal_flags");
    *(void **)&g_wl.add_listener = dlsym(l, "wl_proxy_add_listener");
    *(void **)&g_wl.version = dlsym(l, "wl_proxy_get_version");
    *(void **)&g_wl.destroy = dlsym(l, "wl_proxy_destroy");
    *(void **)&g_wl.roundtrip = dlsym(l, "wl_display_roundtrip_queue");
    *(void **)&g_wl.dispatch_pending = dlsym(l, "wl_display_dispatch_queue_pending");
    *(void **)&g_wl.prepare_read = dlsym(l, "wl_display_prepare_read_queue");
    *(void **)&g_wl.read_events = dlsym(l, "wl_display_read_events");
    *(void **)&g_wl.cancel_read = dlsym(l, "wl_display_cancel_read");
    *(void **)&g_wl.flush = dlsym(l, "wl_display_flush");
    *(void **)&g_wl.get_fd = dlsym(l, "wl_display_get_fd");
    g_wl.i_registry = dlsym(l, "wl_registry_interface");
    g_wl.i_seat = dlsym(l, "wl_seat_interface");
    g_wl.i_pointer = dlsym(l, "wl_pointer_interface");
    g_wl.i_ddm = dlsym(l, "wl_data_device_manager_interface");
    g_wl.i_dd = dlsym(l, "wl_data_device_interface");
    g_wl.i_src = dlsym(l, "wl_data_source_interface");
    g_wl.i_offer = dlsym(l, "wl_data_offer_interface");
    g_wl.i_compositor = dlsym(l, "wl_compositor_interface"); // (the card: optional)
    g_wl.i_shm = dlsym(l, "wl_shm_interface");
    g_wl.i_pool = dlsym(l, "wl_shm_pool_interface");
    g_wl.i_buffer = dlsym(l, "wl_buffer_interface");
    g_wl.i_surface = dlsym(l, "wl_surface_interface");
    if (!g_wl.i_pool || !g_wl.i_buffer || !g_wl.i_surface) g_wl.i_compositor = g_wl.i_shm = NULL;
    if (!g_wl.create_queue || !g_wl.create_wrapper || !g_wl.set_queue || !g_wl.marshal || !g_wl.add_listener || !g_wl.version ||
        !g_wl.destroy || !g_wl.roundtrip || !g_wl.dispatch_pending || !g_wl.prepare_read || !g_wl.read_events || !g_wl.cancel_read ||
        !g_wl.flush || !g_wl.get_fd || !g_wl.i_registry || !g_wl.i_seat || !g_wl.i_pointer || !g_wl.i_ddm || !g_wl.i_dd || !g_wl.i_src || !g_wl.i_offer)
        return false;
    SDL_PropertiesID pr = SDL_GetWindowProperties(A.win);
    g_wl.dpy = SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
    g_wl.surface = SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    if (!g_wl.dpy || !g_wl.surface) return false;
    g_wl.queue = g_wl.create_queue(g_wl.dpy);
    g_wl.wrap = g_wl.create_wrapper(g_wl.dpy);
    if (!g_wl.queue || !g_wl.wrap) return false;
    g_wl.set_queue(g_wl.wrap, g_wl.queue);
    g_wl.registry = g_wl.marshal(g_wl.wrap, 1, g_wl.i_registry, 1, 0, NULL); // wl_display.get_registry
    if (!g_wl.registry) return false;
    g_wl.add_listener(g_wl.registry, g_wl_registry_l, NULL);
    g_wl.roundtrip(g_wl.dpy, g_wl.queue); // the globals
    if (!g_wl.seat || !g_wl.ddm) return false;
    g_wl.roundtrip(g_wl.dpy, g_wl.queue); // the seat's pointer
    g_wl.dd = g_wl.marshal(g_wl.ddm, 1, g_wl.i_dd, g_wl.version(g_wl.ddm), 0, NULL, g_wl.seat); // get_data_device
    if (!g_wl.dd || !g_wl.pointer) return false;
    g_wl.add_listener(g_wl.dd, g_wl_dd_l, NULL);
    if (pipe2(g_wl.stop, O_CLOEXEC)) return false;
    pthread_create(&g_wl.thr, NULL, w_thread, NULL);
    return true;
}

// The card as the drag's icon: a surface of ours with the card's pixels in shared memory, a little below and right of
// the pointer (where the window draws it inside). Whole pixels per logical one (buffer scale), so it's sharp.
static void w_icon_make(void) {
    if (!g_wl.compositor || !g_wl.shm || g_wl.icon) return;
    int w, h;
    u32 *px = drag_badge_pixels(false, &w, &h);
    if (!px) return;
    int bs = MAX(1, (int)floorf(A.dpi_scale + 0.01f));
    size_t size = (size_t)w * (size_t)h * 4;
    int fd = memfd_create("file-explorer-drag", MFD_CLOEXEC);
    void *mem = fd >= 0 && ftruncate(fd, (off_t)size) == 0 ? mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
    if (mem != MAP_FAILED) {
        memcpy(mem, px, size);
        munmap(mem, size);
        g_wl.icon_pool = g_wl.marshal(g_wl.shm, 0, g_wl.i_pool, g_wl.version(g_wl.shm), 0, NULL, fd, (int32_t)size); // create_pool
        g_wl.icon_buf = g_wl.icon_pool ? g_wl.marshal(g_wl.icon_pool, 0, g_wl.i_buffer, g_wl.version(g_wl.icon_pool), 0, NULL, 0, w, h, w * 4,
                                                      0 /* ARGB8888 */) : NULL;
        g_wl.icon = g_wl.icon_buf ? g_wl.marshal(g_wl.compositor, 0, g_wl.i_surface, g_wl.version(g_wl.compositor), 0, NULL) : NULL;
        if (g_wl.icon) {
            uint32_t sv = g_wl.version(g_wl.icon);
            if (sv >= 3) g_wl.marshal(g_wl.icon, 8, NULL, sv, 0, bs); // set_buffer_scale
            g_wl.marshal(g_wl.icon, 1, NULL, sv, 0, g_wl.icon_buf, (int32_t)(roundf(S(18)) / bs), (int32_t)(roundf(S(20)) / bs)); // attach
            g_wl.marshal(g_wl.icon, 2, NULL, sv, 0, 0, 0, w, h); // damage
        }
    }
    if (fd >= 0) close(fd); // (the compositor has its own mapping)
    free(px);
}

static void w_icon_free(void) {
    if (g_wl.icon) g_wl.marshal(g_wl.icon, 0, NULL, g_wl.version(g_wl.icon), W_DESTROY);                     // wl_surface.destroy
    if (g_wl.icon_buf) g_wl.marshal(g_wl.icon_buf, 0, NULL, g_wl.version(g_wl.icon_buf), W_DESTROY);         // wl_buffer.destroy
    if (g_wl.icon_pool) g_wl.marshal(g_wl.icon_pool, 1, NULL, g_wl.version(g_wl.icon_pool), W_DESTROY);      // wl_shm_pool.destroy
    g_wl.icon = g_wl.icon_buf = g_wl.icon_pool = NULL;
}

static bool w_start(void) {
    uint32_t serial = atomic_load(&g_wl.press_serial);
    void *surf = SDL_GetPointerProperty(SDL_GetWindowProperties(A.win), SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    if (!serial || !surf) return false;
    g_wl.surface = surf;
    atomic_store(&g_wl.result, 0);
    uint32_t v = g_wl.version(g_wl.ddm);
    g_wl.src = g_wl.marshal(g_wl.ddm, 0, g_wl.i_src, v, 0, NULL); // create_data_source
    if (!g_wl.src) return false;
    g_wl.add_listener(g_wl.src, g_wl_src_l, NULL);
    // Only the file list: apps that take files (browsers, chat apps, editors, terminals) read it; and SDL (3.4), when
    // a drag also has text, reads the text yet parses it as URIs - a drop back onto this window would bring nothing.
    g_wl.marshal(g_wl.src, 0, NULL, v, 0, "text/uri-list");
    if (pft_available()) g_wl.marshal(g_wl.src, 0, NULL, v, 0, PFT_MIME); // (sandboxed apps)
    uint32_t acts = (g_do.offer & (DO_COPY | DO_LINK) ? 1u : 0u) | (g_do.offer & DO_MOVE ? 2u : 0u) | (g_do.offer & DO_ASK ? 4u : 0u);
    g_wl.marshal(g_wl.src, 2, NULL, v, 0, acts); // set_actions (there's no "link": a copy)
    w_icon_make();
    g_wl.marshal(g_wl.dd, 0, NULL, g_wl.version(g_wl.dd), 0, g_wl.src, g_wl.surface, g_wl.icon, serial); // start_drag
    if (g_wl.icon) g_wl.marshal(g_wl.icon, 6, NULL, g_wl.version(g_wl.icon), 0); // commit (now that it's the drag's icon)
    g_wl.flush(g_wl.dpy);
    return true;
}

static void w_src_free(void) {
    w_icon_free();
    if (!g_wl.src) return;
    g_wl.marshal(g_wl.src, 1, NULL, g_wl.version(g_wl.src), W_DESTROY); // wl_data_source.destroy
    g_wl.src = NULL;
    g_wl.flush(g_wl.dpy);
}

// --- both -------------------------------------------------------------------------------------------------------

// After the window exists: what this session can drag out with.
static void dragout_init(void) {
    if (A.selftest || A.screenshot) return;
    if (w_init()) g_do.sys = DOS_WAYLAND;
    else if (x_init()) g_do.sys = DOS_X11;
    if (g_do.sys) pft_probe();
}

static void dragout_shutdown(void) {
    if (g_do.sys != DOS_WAYLAND) return;
    ssize_t w = write(g_wl.stop[1], "x", 1);
    (void)w;
    pthread_join(g_wl.thr, NULL);
}

// The pointer left the window while dragging paths out of pane p: start dragging them to other apps.
static bool dragout_start(Pane *p, char **paths, int n) {
    if (g_do.active || !g_do.sys || !n) return false;
    dragout_pick(&g_do.offer, &g_do.pref);
    dragout_data(paths, n);
    g_do.src = p;
    g_do.over_self = false;
    g_do.active = true;
    if (g_do.sys == DOS_X11) x_start();
    else if (!w_start()) { g_do.active = false; return false; }
    return true;
}

// Every loop: Wayland's verdict from our thread; X11: a target that never says it's finished.
static void dragout_tick(void) {
    if (g_do.sys == DOS_WAYLAND && atomic_load(&g_wl.result)) { // (also after a drop back here took it over)
        atomic_store(&g_wl.result, 0);
        w_src_free();
        dragout_finish();
    }
    if (g_do.active && g_do.sys == DOS_X11 && g_xd.dropped && now_ns() - g_xd.t_drop > 5000000000ull) x_end();
}

// (X11) Esc: the target hears it's over.
static void dragout_cancel(void) { if (g_do.active && g_do.sys == DOS_X11 && !g_xd.dropped) x_end(); }

// (Wayland) a drop back onto this window: it's handled as a drop inside; the source's end just cleans up.
static void dragout_forget(void) {
    if (!g_do.active) return;
    g_do.active = g_do.over_self = false;
    g_do.t_end = now_ns();
}

// (X11) pointer motion while dragging out.
static void dragout_motion(void) { if (g_do.active && g_do.sys == DOS_X11 && !g_xd.dropped) x_motion(); }

// (X11) button released: true if another app got the drop (else the drag inside ends as usual).
static bool dragout_release(void) { return g_do.active && g_do.sys == DOS_X11 && x_release(); }

// (Wayland) the compositor has the pointer now: mouse-ups SDL makes up aren't drops.
static bool dragout_owns_pointer(void) { return g_do.active && g_do.sys == DOS_WAYLAND; }

// A drop SDL reports: our own drag, dropped back onto this window? (Its paths, while it lasts or just after.)
static bool dragout_is_self(char **paths, int n) {
    if (g_do.sys != DOS_WAYLAND || n != g_do.n || !(g_do.active || now_ns() - g_do.t_end < 3000000000ull)) return false;
    for (int i = 0; i < n; i++)
        if (strcmp(paths[i], g_do.paths[i])) return false;
    return true;
}
