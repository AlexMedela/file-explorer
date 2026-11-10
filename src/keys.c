// keys.c - rebindable keyboard shortcuts: chords ("Ctrl+Shift+T") and two-step sequences ("Ctrl+K Ctrl+T")
// (included from app.c after run_command)

enum { KM_CTRL = 1, KM_SHIFT = 2, KM_ALT = 4, KM_SUPER = 8 };
typedef struct { SDL_Keycode key; u8 mod; } Chord;
typedef struct { Chord c[2]; u8 n; } KeySeq;
#define MAX_KEYS_PER_CMD 4

typedef struct {
    int id;
    const char *slug;     // stable name used in settings.conf
    bool global;          // also works while typing in the path bar / search field
    const char *defaults; // "Ctrl+T|F3"
    KeySeq seq[MAX_KEYS_PER_CMD];
    int nseq;
} KeyDef;

#define K(i, s, g, d) {.id = (i), .slug = (s), .global = (g), .defaults = (d)}
static KeyDef g_keys[] = {
    K(CMD_NEW_TAB, "tab.new", true, "Ctrl+T"),
    K(ACT_TAB_REOPEN, "tab.reopen", true, "Ctrl+Shift+T"),
    K(CMD_TABS_POPUP, "tab.search", true, "Ctrl+Shift+A"),
    K(CMD_CLOSE_TAB, "tab.close", true, "Ctrl+W"),
    K(CMD_NEXT_TAB, "tab.next", true, "Ctrl+Tab|Ctrl+PageDown"),
    K(CMD_PREV_TAB, "tab.prev", true, "Ctrl+Shift+Tab|Ctrl+PageUp"),
    K(ACT_TAB_DUP, "tab.duplicate", true, ""),
    K(ACT_TAB_CLOSE_OTHERS, "tab.close_others", true, ""),
    K(CMD_SPLIT, "panel.split_right", true, "Ctrl+\\|F3"),
    K(CMD_SPLIT_DOWN, "panel.split_down", true, "Ctrl+Shift+\\"),
    K(CMD_SWITCH_PANE, "panel.next", false, "Tab"),
    K(CMD_PREV_PANE, "panel.prev", false, "Shift+Tab"),
    K(CMD_CLOSE_PANEL, "panel.close", true, ""),
    K(CMD_COPY_OTHER, "panel.copy_to_other", false, "Shift+F5"),
    K(CMD_MOVE_OTHER, "panel.move_to_other", false, "Shift+F6"),
    K(CMD_NEW_WINDOW, "window.new", true, "Ctrl+N"),
    K(CMD_QUIT, "app.quit", true, "Ctrl+Q"),
    K(CMD_GOTO, "app.goto", true, "Ctrl+P"),
    K(CMD_PALETTE, "app.commands", true, "Ctrl+Shift+P"),
    K(CMD_OPTIONS, "app.options", true, "Ctrl+,"),
    K(CMD_TRANSFERS, "app.transfers", true, "Ctrl+J"),
    K(CMD_SEARCH, "search.focus", true, "Ctrl+F"),
    K(CMD_SEARCH_SUBFOLDERS, "search.subfolders", false, "Alt+R"),
    K(CMD_SEARCH_CONTENTS, "search.contents", true, "Ctrl+Shift+F"),
    K(CMD_EDIT_PATH, "nav.edit_path", false, "Ctrl+L|F6"),
    K(CMD_BACK, "nav.back", false, "Alt+Left"),
    K(CMD_FORWARD, "nav.forward", false, "Alt+Right"),
    K(CMD_UP, "nav.up", false, "Alt+Up"),
    K(CMD_HOME, "nav.home", false, ""),
    K(CMD_CONNECT, "nav.connect", false, ""),
    K(ACT_REFRESH, "view.refresh", false, "F5|Ctrl+R"),
    K(ACT_HIDDEN, "view.hidden", false, "Ctrl+H"),
    K(CMD_TREE_VIEW, "view.tree", false, "Ctrl+Shift+E"),
    K(ACT_PREVIEW, "view.preview", true, "Ctrl+I"),
    K(CMD_ZOOM_IN, "view.zoom_in", true, "Ctrl+=|Ctrl+Shift+=|Ctrl++|Ctrl+Keypad +"),
    K(CMD_ZOOM_OUT, "view.zoom_out", true, "Ctrl+-|Ctrl+Keypad -"),
    K(CMD_ZOOM_RESET, "view.zoom_reset", true, "Ctrl+0|Ctrl+Keypad 0"),
    K(CMD_FOLDER_SIZES, "view.folder_sizes", false, ""),
    K(CMD_GIT_STATUS, "view.git_status", false, ""),
    K(CMD_DISK_USAGE, "view.disk_usage", false, "Ctrl+Shift+U"),
    K(CMD_COMPARE, "panel.compare", false, "Ctrl+Shift+K"),
    K(CMD_SNAPSHOTS, "view.snapshots", false, ""),
    K(CMD_SORT_NAME, "sort.name", false, ""),
    K(CMD_SORT_DATE, "sort.date", false, ""),
    K(CMD_SORT_TYPE, "sort.type", false, ""),
    K(CMD_SORT_SIZE, "sort.size", false, ""),
    K(ACT_RENAME, "file.rename", false, "F2"),
    K(ACT_OPEN_WITH, "file.open_with", false, ""),
    K(ACT_PROPERTIES, "file.properties", false, "Alt+Return"),
    K(ACT_COPY, "file.copy", false, "Ctrl+C"),
    K(ACT_CUT, "file.cut", false, "Ctrl+X"),
    K(ACT_PASTE, "file.paste", false, "Ctrl+V"),
    K(ACT_TRASH, "file.trash", false, "Delete"),
    K(ACT_DELETE, "file.delete", false, "Shift+Delete"),
    K(ACT_UNDO, "file.undo", false, "Ctrl+Z"),
    K(ACT_SELECT_ALL, "file.select_all", false, "Ctrl+A"),
    K(ACT_COPY_PATH, "file.copy_path", false, "Ctrl+Shift+C"),
    K(ACT_NEW_FOLDER, "file.new_folder", false, "Ctrl+Shift+N"),
    K(ACT_NEW_FILE, "file.new_file", false, "Ctrl+Alt+N"),
    K(ACT_TERMINAL, "file.terminal", false, "Shift+F4"),
    K(ACT_TERM_PANEL, "term.panel", false, "F4"),
    K(ACT_TERM_FOCUS, "term.panel_focus", false, "Ctrl+Shift+F4"),
    K(ACT_PIN, "file.pin", false, "Ctrl+D"),
    K(ACT_TRASH_EMPTY, "trash.empty", false, ""),
    K(ACT_TF_RESET, "view.all_types", false, ""),
    K(CMD_REINDEX, "app.reindex", false, ""),
    K(CMD_OVERLAY, "app.overlay", true, "F12"),
};
#undef K
#define KEYDEF_COUNT ((int)ARRAY_COUNT(g_keys))

static Chord g_key_prefix; // first chord of a pending two-step sequence
static u64 g_key_prefix_t;

static u8 km_from_sdl(SDL_Keymod m) {
    return (u8)((m & SDL_KMOD_CTRL ? KM_CTRL : 0) | (m & SDL_KMOD_SHIFT ? KM_SHIFT : 0) | (m & SDL_KMOD_ALT ? KM_ALT : 0) |
                (m & SDL_KMOD_GUI ? KM_SUPER : 0));
}

static SDL_Keycode key_normalize(SDL_Keycode k) {
    if (k == SDLK_KP_ENTER) return SDLK_RETURN;
    if (k >= 'A' && k <= 'Z') return k + 32;
    return k;
}

static bool chord_is_modifier(SDL_Keycode k) {
    return k == SDLK_LCTRL || k == SDLK_RCTRL || k == SDLK_LSHIFT || k == SDLK_RSHIFT || k == SDLK_LALT || k == SDLK_RALT ||
           k == SDLK_LGUI || k == SDLK_RGUI || k == SDLK_MODE || k == SDLK_CAPSLOCK;
}

static bool chord_eq(Chord a, Chord b) { return a.key == b.key && a.mod == b.mod; }
static bool seq_eq(const KeySeq *a, const KeySeq *b) {
    return a->n == b->n && chord_eq(a->c[0], b->c[0]) && (a->n < 2 || chord_eq(a->c[1], b->c[1]));
}

static bool chord_parse(const char *s, Chord *out) {
    out->mod = 0;
    out->key = SDLK_UNKNOWN;
    char buf[64];
    snprintf(buf, sizeof buf, "%s", s);
    char *p = buf;
    for (;;) {
        char *plus = strchr(p, '+');
        if (!plus || plus == p + strlen(p) - 1 || plus == p) { // last token ("Ctrl++" -> '+')
            if (plus == p && !p[1]) { out->key = '+'; break; }
            const char *name = p;
            if (str_ieq(name, "Enter")) name = "Return";
            else if (str_ieq(name, "Del")) name = "Delete";
            else if (str_ieq(name, "Esc")) name = "Escape";
            else if (str_ieq(name, "PgDn")) name = "PageDown";
            else if (str_ieq(name, "PgUp")) name = "PageUp";
            else if (str_ieq(name, "Plus")) name = "+";
            out->key = SDL_GetKeyFromName(name);
            if (out->key == SDLK_UNKNOWN && strlen(name) == 1) out->key = (SDL_Keycode)ascii_lower((u8)name[0]);
            break;
        }
        *plus = 0;
        if (str_ieq(p, "Ctrl") || str_ieq(p, "Control")) out->mod |= KM_CTRL;
        else if (str_ieq(p, "Shift")) out->mod |= KM_SHIFT;
        else if (str_ieq(p, "Alt")) out->mod |= KM_ALT;
        else if (str_ieq(p, "Super") || str_ieq(p, "Meta") || str_ieq(p, "Win")) out->mod |= KM_SUPER;
        else if (!strncasecmp(p, "Keypad", 6)) { *plus = '+'; out->key = SDL_GetKeyFromName(p); break; } // "Keypad +"
        else return false;
        p = plus + 1;
    }
    out->key = key_normalize(out->key);
    return out->key != SDLK_UNKNOWN;
}

static bool seq_parse(const char *s, KeySeq *out) {
    out->n = 0;
    while (*s == ' ') s++;
    if (!*s) return false;
    // a space separates the two steps, except inside a key name ("Keypad +")
    const char *sp = NULL;
    for (const char *q = s; *q; q++)
        if (*q == ' ' && strncasecmp(q - (q - s >= 6 ? 6 : q - s), "Keypad", 6)) { sp = q; break; }
    char a[64];
    snprintf(a, sizeof a, "%.*s", sp ? (int)(sp - s) : (int)strlen(s), s);
    if (!chord_parse(a, &out->c[0])) return false;
    out->n = 1;
    if (sp) {
        while (*sp == ' ') sp++;
        if (*sp && chord_parse(sp, &out->c[1])) out->n = 2;
    }
    return true;
}

static void chord_format(Chord c, char *out, size_t cap) {
    const char *name = SDL_GetKeyName(c.key);
    if (c.key == SDLK_RETURN) name = "Enter";
    else if (c.key == SDLK_DELETE) name = "Del";
    else if (c.key == SDLK_ESCAPE) name = "Esc";
    else if (!name || !name[0]) name = "?";
    snprintf(out, cap, "%s%s%s%s%s", c.mod & KM_CTRL ? "Ctrl+" : "", c.mod & KM_SUPER ? "Super+" : "",
             c.mod & KM_ALT ? "Alt+" : "", c.mod & KM_SHIFT ? "Shift+" : "", name);
}

static void seq_format(const KeySeq *s, char *out, size_t cap) {
    char a[48], b[48] = "";
    chord_format(s->c[0], a, sizeof a);
    if (s->n > 1) chord_format(s->c[1], b, sizeof b);
    snprintf(out, cap, "%s%s%s", a, s->n > 1 ? " " : "", b);
}

// Parseable form for settings.conf (Return/Delete instead of Enter/Del is fine: both parse).
static void seq_store(const KeySeq *s, char *out, size_t cap) { seq_format(s, out, cap); }

static void keydef_reset(KeyDef *d) {
    d->nseq = 0;
    char buf[160];
    snprintf(buf, sizeof buf, "%s", d->defaults);
    for (char *p = buf, *tok; (tok = strsep(&p, "|"));)
        if (d->nseq < MAX_KEYS_PER_CMD && seq_parse(tok, &d->seq[d->nseq])) d->nseq++;
}

static void keys_defaults(void) {
    for (int i = 0; i < KEYDEF_COUNT; i++) keydef_reset(&g_keys[i]);
}

static KeyDef *keydef_find(int id) {
    for (int i = 0; i < KEYDEF_COUNT; i++)
        if (g_keys[i].id == id) return &g_keys[i];
    return NULL;
}

static bool keydef_is_default(const KeyDef *d) {
    KeyDef t = *d;
    keydef_reset(&t);
    if (t.nseq != d->nseq) return false;
    for (int i = 0; i < t.nseq; i++)
        if (!seq_eq(&t.seq[i], &d->seq[i])) return false;
    return true;
}

// settings.conf: key=<slug>=<seq>|<seq>  (empty = unbound)
static void keys_load_line(const char *v) {
    const char *eq = strchr(v, '=');
    if (!eq) return;
    for (int i = 0; i < KEYDEF_COUNT; i++) {
        KeyDef *d = &g_keys[i];
        if (strlen(d->slug) != (size_t)(eq - v) || strncmp(d->slug, v, (size_t)(eq - v))) continue;
        d->nseq = 0;
        char buf[256];
        snprintf(buf, sizeof buf, "%s", eq + 1);
        for (char *p = buf, *tok; (tok = strsep(&p, "|"));)
            if (d->nseq < MAX_KEYS_PER_CMD && seq_parse(tok, &d->seq[d->nseq])) d->nseq++;
    }
}

static void keys_save(FILE *f) {
    for (int i = 0; i < KEYDEF_COUNT; i++) {
        KeyDef *d = &g_keys[i];
        if (keydef_is_default(d)) continue;
        fprintf(f, "key=%s=", d->slug);
        for (int k = 0; k < d->nseq; k++) {
            char buf[96];
            seq_store(&d->seq[k], buf, sizeof buf);
            fprintf(f, "%s%s", k ? "|" : "", buf);
        }
        fprintf(f, "\n");
    }
}

// Label for menus and the palette: the first binding, "" when unbound. NULL if the command isn't rebindable.
static const char *keymap_label(int id) {
    static char ring[8][64];
    static int ri;
    KeyDef *d = keydef_find(id);
    if (!d) return NULL;
    char *out = ring[ri++ & 7];
    out[0] = 0;
    if (d->nseq) seq_format(&d->seq[0], out, 64);
    return out;
}

static void keydef_remove_seq(KeyDef *d, int k) {
    memmove(&d->seq[k], &d->seq[k + 1], (size_t)(d->nseq - k - 1) * sizeof(KeySeq));
    d->nseq--;
}

// Bind seq to d (slot -1 = add). The same keys are removed from any other command; returns that command's id or -1.
static int keys_assign(KeyDef *d, int slot, const KeySeq *s) {
    int taken = -1;
    for (int i = 0; i < KEYDEF_COUNT; i++) {
        KeyDef *o = &g_keys[i];
        for (int k = o->nseq - 1; k >= 0; k--) {
            if (!seq_eq(&o->seq[k], s)) continue;
            if (o == d) { if (slot < 0) return -1; } // already bound here
            if (o != d) taken = o->id;
            if (o != d || k != slot) {
                keydef_remove_seq(o, k);
                if (o == d && slot > k) slot--;
            }
        }
    }
    if (slot >= 0 && slot < d->nseq) d->seq[slot] = *s;
    else if (d->nseq < MAX_KEYS_PER_CMD) d->seq[d->nseq++] = *s;
    else d->seq[MAX_KEYS_PER_CMD - 1] = *s;
    return taken;
}

static const char *cmd_label(int id) {
    for (size_t i = 0; i < ARRAY_COUNT(g_cmds); i++)
        if (g_cmds[i].id == id) return g_cmds[i].label;
    return "?";
}

// Try the keymap. text_mode: a text field has focus (only global commands fire).
// Returns true when the key was consumed (including the first half of a sequence).
// Is k one of id's one-key shortcuts? (For keys the app takes back from a focused terminal.)
static bool key_is(SDL_KeyboardEvent *k, int id) {
    KeyDef *d = keydef_find(id);
    Chord c = {key_normalize(k->key), km_from_sdl(k->mod)};
    for (int s = 0; d && s < d->nseq; s++)
        if (d->seq[s].n == 1 && chord_eq(d->seq[s].c[0], c)) return true;
    return false;
}

static bool keys_dispatch(SDL_KeyboardEvent *k, bool text_mode) {
    if (chord_is_modifier(k->key)) return false;
    Chord c = {key_normalize(k->key), km_from_sdl(k->mod)};
    bool pending = g_key_prefix.key && now_ns() - g_key_prefix_t < 3000000000ull;
    if (pending) {
        Chord first = g_key_prefix;
        g_key_prefix.key = 0;
        for (int i = 0; i < KEYDEF_COUNT; i++) {
            KeyDef *d = &g_keys[i];
            if (text_mode && !d->global) continue;
            for (int s = 0; s < d->nseq; s++)
                if (d->seq[s].n == 2 && chord_eq(d->seq[s].c[0], first) && chord_eq(d->seq[s].c[1], c)) {
                    run_command(d->id);
                    return true;
                }
        }
        toast("No command for that key sequence");
        return true;
    }
    for (int i = 0; i < KEYDEF_COUNT; i++) { // first chord of a sequence?
        KeyDef *d = &g_keys[i];
        if (text_mode && !d->global) continue;
        for (int s = 0; s < d->nseq; s++)
            if (d->seq[s].n == 2 && chord_eq(d->seq[s].c[0], c)) {
                g_key_prefix = c;
                g_key_prefix_t = now_ns();
                char a[48];
                chord_format(c, a, sizeof a);
                toast("%s was pressed. Waiting for the second key\xE2\x80\xA6", a);
                return true;
            }
    }
    for (int i = 0; i < KEYDEF_COUNT; i++) {
        KeyDef *d = &g_keys[i];
        if (text_mode && !d->global) continue;
        for (int s = 0; s < d->nseq; s++)
            if (d->seq[s].n == 1 && chord_eq(d->seq[s].c[0], c)) {
                run_command(d->id);
                return true;
            }
    }
    return false;
}
