// settings.c - persistence: settings.conf, named layouts (layouts.conf), per-folder views (folders.conf)
// (included from app.c)

// --- settings ($XDG_CONFIG_HOME/file-explorer/settings.conf) ---------------

static void settings_file(char *out, size_t cap, bool dir_only) {
    const char *x = getenv("XDG_CONFIG_HOME");
    char base[PATH_MAX];
    if (x && x[0] == '/') snprintf(base, sizeof base, "%s/file-explorer", x);
    else snprintf(base, sizeof base, "%s/.config/file-explorer", getenv("HOME") ? getenv("HOME") : "/tmp");
    if (dir_only) snprintf(out, cap, "%s", base);
    else snprintf(out, cap, "%s/settings.conf", base);
}

static const char *g_sort_names[SORT_COUNT] = {"name", "date", "type", "size", "created", "ext", "perms", "owner"};

static void settings_defaults(void) {
    A.zoom = 1.f;
    A.show_hidden = false;
    A.folder_sizes = true;
    A.git_status = true;
    A.du_list = true;
    A.search_recursive = false;
    A.def_sort_key = SORT_NAME;
    A.def_sort_desc = false;
    A.restore_session = true;
    A.preview_on = false;
    A.preview_w = 340;
    A.side_w = 216;
    A.win_w = 1280;
    A.win_h = 800;
    A.maximized = false;
    snprintf(A.renderer, sizeof A.renderer, "auto");
    strv_free(A.pins, A.npins);
    A.pins = NULL;
    A.npins = 0;
    qa_free();
    strv_free(A.sess_tabs, A.nsess_tabs);
    A.sess_tabs = NULL;
    A.nsess_tabs = 0;
    A.sess_cur = 0;
    A.sess_layout[0] = 0;
    A.dim_inactive = true;
    A.title_bar = TITLEBAR_AUTO;
    for (int i = 0; i < A.ndev_names; i++) { free(A.dev_names[i].key); free(A.dev_names[i].name); }
    A.ndev_names = 0;
    A.devs_collapsed = A.nets_collapsed = false;
    g_term_look_setting = 0;
    A.nservers = 0;
    addon_settings_defaults();
    g_cloud_collapsed = false;
    for (int i = 0; i < g_ncloud; i++) g_cloud[i].login = false;
    A.close_to_tray = false;
    cols_default();
    A.def_view = VIEW_DETAILS;
    A.def_icon = 96;
    A.def_group = GROUP_NONE;
    snprintf(A.icon_theme_name, sizeof A.icon_theme_name, "Material");
    A.alt_rows = false;
    A.size_bars = true;
    A.load_times = true;
    A.verify_copies = false;
    A.transfers_at_once = 1;
    A.conflict_last = CF_KEEP_BOTH;
    snprintf(A.theme_name, sizeof A.theme_name, "Dark");
    g_accent_custom = 0;
    A.ui_font[0] = 0;
    A.font_size = 13;
    A.row_spacing = 1;
    A.smooth_scroll = true;
    A.single_click = false;
    A.dblclick_up = true;
    A.def_tree = false;
    A.single_instance = true;
    keys_defaults();
}

static void themes_dir(char *out, size_t cap) {
    char dir[PATH_MAX];
    settings_file(dir, sizeof dir, true);
    snprintf(out, cap, "%s/themes", dir);
}

static void icon_themes_dir(char *out, size_t cap) {
    char dir[PATH_MAX];
    settings_file(dir, sizeof dir, true);
    snprintf(out, cap, "%s/icons", dir);
}

static void icon_themes_reload(void) {
    char dir[PATH_MAX];
    icon_themes_dir(dir, sizeof dir);
    icon_themes_scan(dir);
}

// Session lines shared by settings.conf and saved layouts: layout=, ptab=, active_panel=
static bool session_line(const char *k, const char *v) {
    if (!strcmp(k, "ptab")) {
        A.sess_tabs = xrealloc(A.sess_tabs, (size_t)(A.nsess_tabs + 1) * sizeof(char *));
        A.sess_tabs[A.nsess_tabs++] = strdup(v);
    } else if (!strcmp(k, "active_panel")) A.sess_cur = atoi(v);
    else if (!strcmp(k, "layout")) snprintf(A.sess_layout, sizeof A.sess_layout, "%s", v);
    else return false;
    return true;
}

static void session_clear(void) {
    strv_free(A.sess_tabs, A.nsess_tabs);
    A.sess_tabs = NULL;
    A.nsess_tabs = 0;
    A.sess_cur = 0;
    A.sess_layout[0] = 0;
}

// Lines with keys this build doesn't know (add-ons', a newer version's settings): written back as
// they were, so running a build without them doesn't lose them.
static char g_settings_other[32768];
static size_t g_settings_other_len;

static void settings_keep_other(const char *k, const char *v) {
    size_t n = strlen(k) + strlen(v) + 2;
    if (g_settings_other_len + n >= sizeof g_settings_other) return;
    g_settings_other_len += (size_t)snprintf(g_settings_other + g_settings_other_len, sizeof g_settings_other - g_settings_other_len, "%s=%s\n", k, v);
}

static void settings_load(void) {
    settings_defaults();
    g_settings_other_len = 0;
    g_settings_other[0] = 0;
    char path[PATH_MAX];
    settings_file(path, sizeof path, false);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[PATH_MAX * 2 + 64];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        if (!strcmp(k, "zoom")) A.zoom = CLAMP((f32)atof(v), 0.6f, 2.5f);
        else if (!strcmp(k, "show_hidden")) A.show_hidden = atoi(v);
        else if (!strcmp(k, "folder_sizes")) A.folder_sizes = atoi(v);
        else if (!strcmp(k, "git_status")) A.git_status = atoi(v);
        else if (!strcmp(k, "disk_usage_style")) A.du_style = !strcmp(v, "sunburst");
        else if (!strcmp(k, "disk_usage_list")) A.du_list = atoi(v);
        else if (!strcmp(k, "search_subfolders")) A.search_recursive = atoi(v);
        else if (!strcmp(k, "search_contents")) A.search_contents = atoi(v);
        else if (!strcmp(k, "search_options")) { // contents search: "case,word,regex"
            int c = 0, w = 0, r = 0;
            sscanf(v, "%d,%d,%d", &c, &w, &r);
            A.cs_case = c; A.cs_word = w; A.cs_regex = r;
        }
        else if (!strcmp(k, "restore_session")) A.restore_session = atoi(v);
        else if (!strcmp(k, "preview")) A.preview_on = atoi(v);
        else if (!strcmp(k, "preview_width")) A.preview_w = CLAMP((f32)atof(v), 200.f, 1200.f);
        else if (!strcmp(k, "sidebar_width")) A.side_w = CLAMP((f32)atof(v), 150.f, 480.f);
        else if (!strcmp(k, "sort_desc")) A.def_sort_desc = atoi(v);
        else if (!strcmp(k, "maximized")) A.maximized = atoi(v);
        else if (!strcmp(k, "renderer")) snprintf(A.renderer, sizeof A.renderer, "%s", v);
        else if (!strcmp(k, "window")) sscanf(v, "%dx%d", &A.win_w, &A.win_h);
        else if (!strcmp(k, "sort")) {
            for (int i = 0; i < SORT_COUNT; i++)
                if (!strcmp(v, g_sort_names[i])) A.def_sort_key = i;
        } else if (!strcmp(k, "dev_name")) { // custom device name: key\tname
            const char *tab = strchr(v, '\t');
            if (tab) {
                char key[PATH_MAX];
                snprintf(key, sizeof key, "%.*s", (int)(tab - v), v);
                dev_name_set(key, tab + 1);
            }
        } else if (!strcmp(k, "server")) { // saved server: name\turi[\ticon]
            const char *tab = strchr(v, '\t');
            if (tab && A.nservers < (int)ARRAY_COUNT(A.servers)) {
                const char *tab2 = strchr(tab + 1, '\t');
                snprintf(A.servers[A.nservers].name, sizeof A.servers[0].name, "%.*s", (int)MIN(63, tab - v), v);
                snprintf(A.servers[A.nservers].uri, sizeof A.servers[0].uri, "%.*s", (int)(tab2 ? tab2 - tab - 1 : (ptrdiff_t)strlen(tab + 1)), tab + 1);
                snprintf(A.servers[A.nservers].icon, sizeof A.servers[0].icon, "%s", tab2 ? tab2 + 1 : "");
                A.nservers++;
            }
        } else if (!strcmp(k, "term_look")) g_term_look_setting = !strcmp(v, "app");
        else if (!strcmp(k, "devices_collapsed")) A.devs_collapsed = atoi(v);
        else if (!strcmp(k, "network_collapsed")) A.nets_collapsed = atoi(v);
        else if (!strcmp(k, "close_to_tray")) A.close_to_tray = atoi(v);
        else if (addon_settings_line(k, v)) {}
        else if (cloud_settings_line(k, v)) {}
        else if (!strcmp(k, "qa_group")) { // Quick Access group: name\tcollapsed
            char nm[64];
            snprintf(nm, sizeof nm, "%.*s", (int)strcspn(v, "\t"), v);
            int g = qa_add_group(nm);
            const char *tab = strchr(v, '\t');
            A.qa[g].collapsed = tab && atoi(tab + 1);
        } else if (!strcmp(k, "qa") && A.nqa) { // item of the last group: label\tpath
            const char *tab = strchr(v, '\t');
            if (tab && tab[1] == '/') {
                char lb[64];
                snprintf(lb, sizeof lb, "%.*s", (int)(tab - v), v);
                qa_insert(A.nqa - 1, -1, lb, tab + 1);
            }
        } else if (!strcmp(k, "pin") && v[0] == '/') {
            A.pins = xrealloc(A.pins, (size_t)(A.npins + 1) * sizeof(char *));
            A.pins[A.npins++] = strdup(v);
        } else if (session_line(k, v)) {
        } else if (!strcmp(k, "tab")) {
            char conv[PATH_MAX + 32];
            if (!strcmp(k, "tab")) { // pre-panels format: active\tsplit\tleft\tright -> left path in panel 0
                const char *p3 = v;
                for (int f = 0; f < 2 && p3; f++) { p3 = strchr(p3, '\t'); if (p3) p3++; }
                if (!p3) continue;
                snprintf(conv, sizeof conv, "0\t0\t%.*s", (int)strcspn(p3, "\t"), p3);
                v = conv;
            }
            session_line("ptab", v);
        } else if (!strcmp(k, "dim_inactive")) A.dim_inactive = atoi(v);
        else if (!strcmp(k, "title_bar")) A.title_bar = !strcmp(v, "system") ? TITLEBAR_SYSTEM : !strcmp(v, "app") ? TITLEBAR_APP : TITLEBAR_AUTO;
        else if (!strcmp(k, "alt_rows")) A.alt_rows = atoi(v);
        else if (!strcmp(k, "load_times")) A.load_times = atoi(v);
        else if (!strcmp(k, "verify_copies")) A.verify_copies = atoi(v);
        else if (!strcmp(k, "transfers_at_once")) A.transfers_at_once = CLAMP(atoi(v), 0, 8);
        else if (!strcmp(k, "view")) A.def_view = !strcmp(v, "grid") ? VIEW_GRID : !strcmp(v, "list") ? VIEW_LIST : VIEW_DETAILS;
        else if (!strcmp(k, "icon_size")) A.def_icon = CLAMP(atoi(v), 32, 256);
        else if (!strcmp(k, "group")) A.def_group = CLAMP(atoi(v), 0, GROUP_COUNT - 1);
        else if (!strcmp(k, "icon_theme")) snprintf(A.icon_theme_name, sizeof A.icon_theme_name, "%s", v);
        else if (!strcmp(k, "size_bars")) A.size_bars = atoi(v);
        else if (!strcmp(k, "theme")) snprintf(A.theme_name, sizeof A.theme_name, "%s", v);
        else if (!strcmp(k, "accent")) {
            u32 c;
            g_accent_custom = 0;
            for (int i = 0; i < ACCENT_COUNT; i++)
                if (str_ieq(v, g_accents[i].name)) g_accent_custom = 0x01000000u | g_accents[i].rgb;
            if (!g_accent_custom && parse_hex_color(v, &c)) g_accent_custom = 0x01000000u | c;
        } else if (!strcmp(k, "font")) snprintf(A.ui_font, sizeof A.ui_font, "%s", v);
        else if (!strcmp(k, "font_size")) A.font_size = CLAMP(atoi(v), 9, 24);
        else if (!strcmp(k, "row_spacing")) A.row_spacing = CLAMP(atoi(v), 0, 2);
        else if (!strcmp(k, "smooth_scroll")) A.smooth_scroll = atoi(v);
        else if (!strcmp(k, "single_click")) A.single_click = atoi(v);
        else if (!strcmp(k, "dblclick_up")) A.dblclick_up = atoi(v);
        else if (!strcmp(k, "tree_view")) A.def_tree = atoi(v);
        else if (!strcmp(k, "single_instance")) A.single_instance = atoi(v);
        else if (!strcmp(k, "key")) keys_load_line(v);
        else if (!strcmp(k, "columns")) { // name,date:140,size:88 (order + widths)
            char buf[512];
            snprintf(buf, sizeof buf, "%s", v);
            int n = 0, cols[COL_COUNT];
            bool name = false;
            for (char *s2 = buf, *tok; (tok = strsep(&s2, ","));) {
                char *colon = strchr(tok, ':');
                if (colon) *colon = 0;
                for (int c = 0; c < COL_COUNT; c++) {
                    if (strcmp(tok, g_coldef[c].key)) continue;
                    bool dup = false;
                    for (int k2 = 0; k2 < n; k2++) dup |= cols[k2] == c;
                    if (dup) break;
                    cols[n++] = c;
                    name |= c == COL_NAME;
                    if (colon && c != COL_NAME) A.colw[c] = CLAMP((f32)atof(colon + 1), 40.f, 600.f);
                }
            }
            if (!name && n < COL_COUNT) { memmove(&cols[1], &cols[0], (size_t)n * sizeof(int)); cols[0] = COL_NAME; n++; }
            memcpy(A.cols, cols, (size_t)n * sizeof(int));
            A.ncols = n;
        }
        else settings_keep_other(k, v);
    }
    fclose(f);
    A.win_w = CLAMP(A.win_w, 760, 10000);
    A.win_h = CLAMP(A.win_h, 420, 10000);
}

static void layout_serialize(Node *n, char *buf, size_t cap, size_t *o);
static void folders_save(void);

// Session: panel tree + tabs (ptab=<panel>\t<is current>\t<path>\t<view>:<icon px>)
static void session_write(FILE *f) {
    char lay[1024];
    size_t o = 0;
    lay[0] = 0;
    layout_serialize(A.root, lay, sizeof lay, &o);
    fprintf(f, "layout=%s\n", lay);
    for (int i = 0; i < A.ntabs; i++) {
        Tab *t = A.tabs[i];
        if (t->term) continue; // terminals aren't restored
        if (strpbrk(t->pane[0].path, "\n\r\t")) continue; // can't be stored in a line-based file
        fprintf(f, "ptab=%d\t%d\t%s\t%d:%d\n", panel_index(t->panel), t->panel->cur == t, t->pane[0].path, t->pane[0].view,
                t->pane[0].icon_px);
    }
    fprintf(f, "active_panel=%d\n", panel_index(A.ap));
}

static void settings_save(void) {
    if (picker_active()) return; // a file chooser window never changes the settings or the session
    char dir[PATH_MAX], path[PATH_MAX], tmp[PATH_MAX + 8];
    settings_file(dir, sizeof dir, true);
    settings_file(path, sizeof path, false);
    mkdir_p(dir, 0700);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    if (A.win && !A.screenshot) {
        A.maximized = SDL_GetWindowFlags(A.win) & SDL_WINDOW_MAXIMIZED;
        if (!A.maximized) SDL_GetWindowSize(A.win, &A.win_w, &A.win_h);
    }
    fprintf(f, "# File Explorer settings (edited by the app; safe to edit while it's closed)\n");
    fprintf(f, "zoom=%.2f\nshow_hidden=%d\nfolder_sizes=%d\nsearch_subfolders=%d\n", A.zoom, A.show_hidden, A.folder_sizes,
            A.search_recursive);
    fprintf(f, "git_status=%d\n", A.git_status);
    fprintf(f, "disk_usage_style=%s\ndisk_usage_list=%d\n", A.du_style ? "sunburst" : "treemap", A.du_list);
    fprintf(f, "search_contents=%d\nsearch_options=%d,%d,%d\n", A.search_contents, A.cs_case, A.cs_word, A.cs_regex);
    fprintf(f, "sidebar_width=%.0f\n", A.side_w);
    fprintf(f, "sort=%s\nsort_desc=%d\npreview=%d\npreview_width=%.0f\n", g_sort_names[A.def_sort_key], A.def_sort_desc,
            A.preview_on, A.preview_w);
    fprintf(f, "window=%dx%d\nmaximized=%d\nrestore_session=%d\nrenderer=%s\n", A.win_w, A.win_h, A.maximized,
            A.restore_session, A.renderer);
    for (int g = 0; g < A.nqa; g++) {
        fprintf(f, "qa_group=%s\t%d\n", A.qa[g].name, A.qa[g].collapsed);
        for (int i = 0; i < A.qa[g].n; i++)
            if (!strpbrk(A.qa[g].items[i].path, "\n\r\t") && !strpbrk(A.qa[g].items[i].label, "\n\r\t"))
                fprintf(f, "qa=%s\t%s\n", A.qa[g].items[i].label, A.qa[g].items[i].path);
    }
    for (int i = 0; i < A.ndev_names; i++) fprintf(f, "dev_name=%s\t%s\n", A.dev_names[i].key, A.dev_names[i].name);
    fprintf(f, "devices_collapsed=%d\nnetwork_collapsed=%d\n", A.devs_collapsed, A.nets_collapsed);
    fprintf(f, "close_to_tray=%d\n", A.close_to_tray);
    addon_settings_save(f);
    fputs(g_settings_other, f);
    cloud_settings_save(f);
    fprintf(f, "term_look=%s\n", g_term_look_setting ? "app" : "match");
    for (int i = 0; i < A.nservers; i++) fprintf(f, "server=%s\t%s%s%s\n", A.servers[i].name, A.servers[i].uri, A.servers[i].icon[0] ? "\t" : "", A.servers[i].icon);
    fprintf(f, "icon_theme=%s\n", icon_theme_name(g_icon_theme));
    fprintf(f, "theme=%s\nfont=%s\nfont_size=%d\nrow_spacing=%d\nsmooth_scroll=%d\n",
            g_nthemes ? g_themes[g_theme_cur].name : A.theme_name, A.ui_font, A.font_size, A.row_spacing, A.smooth_scroll);
    fprintf(f, "single_click=%d\ndblclick_up=%d\ntree_view=%d\nsingle_instance=%d\n", A.single_click, A.dblclick_up,
            A.def_tree, A.single_instance);
    keys_save(f);
    if (g_accent_custom) fprintf(f, "accent=#%06X\n", g_accent_custom & 0xFFFFFF);
    else fprintf(f, "accent=\n");
    fprintf(f, "view=%s\nicon_size=%d\ngroup=%d\n", A.def_view == VIEW_GRID ? "grid" : A.def_view == VIEW_LIST ? "list" : "details",
            A.def_icon, A.def_group);
    fprintf(f, "verify_copies=%d\ntransfers_at_once=%d\n", A.verify_copies, A.transfers_at_once);
    fprintf(f, "title_bar=%s\n", A.title_bar == TITLEBAR_SYSTEM ? "system" : A.title_bar == TITLEBAR_APP ? "app" : "auto");
    fprintf(f, "dim_inactive=%d\nalt_rows=%d\nsize_bars=%d\nload_times=%d\ncolumns=", A.dim_inactive, A.alt_rows, A.size_bars, A.load_times);
    for (int i = 0; i < A.ncols; i++) {
        int c = A.cols[i];
        if (c == COL_NAME) fprintf(f, "%s%s", i ? "," : "", g_coldef[c].key);
        else fprintf(f, "%s%s:%.0f", i ? "," : "", g_coldef[c].key, A.colw[c]);
    }
    fprintf(f, "\n");
    session_write(f);
    if (fclose(f) == 0) rename(tmp, path);
    folders_save();
    A.settings_dirty = false;
    A.settings_saved = now_ns();
}

// Layout tree as text: P<panel>, H<ratio>(a,b), V<ratio>(a,b)
static void layout_serialize(Node *n, char *buf, size_t cap, size_t *o) {
    if (!n || *o + 32 >= cap) return;
    if (n->kind == NODE_PANEL) {
        *o += (size_t)snprintf(buf + *o, cap - *o, "P%d", panel_index(n->panel));
        return;
    }
    *o += (size_t)snprintf(buf + *o, cap - *o, "%c%.3f(", n->kind == NODE_HSPLIT ? 'H' : 'V', n->ratio);
    layout_serialize(n->a, buf, cap, o);
    *o += (size_t)snprintf(buf + *o, cap - *o, ",");
    layout_serialize(n->b, buf, cap, o);
    *o += (size_t)snprintf(buf + *o, cap - *o, ")");
}

static Node *layout_parse(const char **s, Panel **map, int nmap) {
    const char *p = *s;
    if (*p == 'P') {
        p++;
        int id = (int)strtol(p, (char **)&p, 10);
        *s = p;
        if (id < 0 || id >= nmap || map[id]) return NULL; // bad or duplicate id
        Panel *pn = panel_new();
        if (!pn) return NULL;
        map[id] = pn;
        return node_leaf(pn);
    }
    if (*p != 'H' && *p != 'V') return NULL;
    int kind = *p == 'H' ? NODE_HSPLIT : NODE_VSPLIT;
    p++;
    f32 ratio = (f32)strtod(p, (char **)&p);
    if (*p++ != '(') return NULL;
    *s = p;
    Node *a = layout_parse(s, map, nmap);
    if (!a || **s != ',') { node_free(a); return NULL; }
    (*s)++;
    Node *b = layout_parse(s, map, nmap);
    if (!b || **s != ')') { node_free(a); node_free(b); return NULL; }
    (*s)++;
    Node *n = xcalloc(1, sizeof *n);
    n->kind = kind;
    n->ratio = CLAMP(ratio, 0.1f, 0.9f);
    n->a = a;
    n->b = b;
    a->parent = b->parent = n;
    return n;
}

static bool is_dir_path(const char *p) {
    if (p[0] == '/' && path_is_remote(p)) return true; // don't block on a dead server (the scan reports errors)
    struct stat st;
    return p[0] == '/' && stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

// Rebuild the saved panel layout and tabs. Expects an empty window (no tabs yet).
static bool session_restore(void) {
    if (A.ntabs || !A.nsess_tabs) return false;
    Panel *map[MAX_PANELS] = {0};
    const char *lp = A.sess_layout;
    A.root = A.sess_layout[0] ? layout_parse(&lp, map, MAX_PANELS) : NULL;
    if (!A.root) { // missing/corrupt layout: one panel
        for (int i = 0; i < A.npanels; i++) free(A.panels[i]);
        A.npanels = 0;
        memset(map, 0, sizeof map);
        map[0] = panel_new();
        A.root = node_leaf(map[0]);
    }
    A.ap = node_first_panel(A.root);
    for (int i = 0; i < A.nsess_tabs; i++) {
        char buf[PATH_MAX + 32];
        snprintf(buf, sizeof buf, "%s", A.sess_tabs[i]);
        char *f[4] = {0};
        int n = 0;
        for (char *s2 = buf, *tok; n < 4 && (tok = strsep(&s2, "\t"));) f[n++] = tok;
        if (n < 3 || !is_dir_path(f[2])) continue;
        int pid = atoi(f[0]);
        Panel *pn = pid >= 0 && pid < MAX_PANELS && map[pid] ? map[pid] : A.ap;
        Tab *t = tab_new_in(pn, f[2], A.ntabs, false);
        if (t && atoi(f[1])) pn->cur = t;
        if (t && n == 4) {
            int v = 0, px = 96;
            if (sscanf(f[3], "%d:%d", &v, &px) == 2) {
                t->pane[0].view = CLAMP(v, 0, 2);
                t->pane[0].icon_px = CLAMP(px, 32, 256);
            }
        }
    }
    for (int i = A.npanels - 1; i >= 0; i--) // drop panels whose folders are all gone
        if (!panel_ntabs(A.panels[i])) {
            if (A.npanels == 1) break;
            panel_remove(A.panels[i]);
        }
    if (!A.ntabs) {
        layout_reset();
        return false;
    }
    int ap = A.sess_cur;
    Panel *want = ap >= 0 && ap < MAX_PANELS && map[ap] && panel_alive(map[ap]) ? map[ap] : node_first_panel(A.root);
    A.ap = NULL;
    panel_activate(want);
    return true;
}

// --- named layouts ($XDG_CONFIG_HOME/file-explorer/layouts.conf) ---------------
// [name] followed by the same session lines settings.conf uses.


static void layouts_file(char *out, size_t cap) {
    char dir[PATH_MAX];
    settings_file(dir, sizeof dir, true);
    snprintf(out, cap, "%s/layouts.conf", dir);
}

static void layout_count(SavedLayout *l) {
    l->npanels = l->ntabs = 0;
    for (const char *p = l->text; p && *p;) {
        if (!strncmp(p, "ptab=", 5)) l->ntabs++;
        if (!strncmp(p, "layout=", 7))
            for (const char *q = p; *q && *q != '\n'; q++) l->npanels += *q == 'P';
        p = strchr(p, '\n');
        if (p) p++;
    }
    l->npanels = MAX(l->npanels, 1);
}

static void layouts_load(void) {
    for (int i = 0; i < g_nlayouts; i++) free(g_layouts[i].text);
    g_nlayouts = 0;
    char path[PATH_MAX];
    layouts_file(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[PATH_MAX + 64];
    SavedLayout *cur = NULL;
    size_t len = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') {
            char *e = strrchr(line, ']');
            if (!e || g_nlayouts == MAX_LAYOUTS) { cur = NULL; continue; }
            *e = 0;
            cur = &g_layouts[g_nlayouts++];
            snprintf(cur->name, sizeof cur->name, "%s", line + 1);
            cur->text = NULL;
            len = 0;
        } else if (cur && strchr(line, '=')) {
            size_t l = strlen(line);
            cur->text = xrealloc(cur->text, len + l + 2);
            memcpy(cur->text + len, line, l);
            cur->text[len + l] = '\n';
            cur->text[len + l + 1] = 0;
            len += l + 1;
        }
    }
    fclose(f);
    for (int i = 0; i < g_nlayouts; i++) layout_count(&g_layouts[i]);
}

static void layouts_save(void) {
    char dir[PATH_MAX], path[PATH_MAX], tmp[PATH_MAX + 8];
    settings_file(dir, sizeof dir, true);
    mkdir_p(dir, 0700);
    layouts_file(path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "# File Explorer saved layouts\n");
    for (int i = 0; i < g_nlayouts; i++) fprintf(f, "[%s]\n%s", g_layouts[i].name, g_layouts[i].text ? g_layouts[i].text : "");
    if (fclose(f) == 0) rename(tmp, path);
}

static int layout_find(const char *name) {
    for (int i = 0; i < g_nlayouts; i++)
        if (str_ieq(g_layouts[i].name, name)) return i;
    return -1;
}

// Save (or overwrite) the current panels and tabs under name.
static bool layout_save_named(const char *name_in) {
    char name[64];
    snprintf(name, sizeof name, "%s", name_in);
    for (char *c = name; *c; c++)
        if (*c == '[' || *c == ']' || *c == '\n') *c = ' ';
    while (name[0] == ' ') memmove(name, name + 1, strlen(name));
    for (size_t l = strlen(name); l && name[l - 1] == ' ';) name[--l] = 0;
    if (!name[0]) return false;
    char *buf = NULL;
    size_t sz = 0;
    FILE *m = open_memstream(&buf, &sz);
    if (!m) return false;
    session_write(m);
    fclose(m);
    int i = layout_find(name);
    if (i < 0) {
        if (g_nlayouts == MAX_LAYOUTS) { free(buf); return false; }
        i = g_nlayouts++;
    } else {
        free(g_layouts[i].text);
    }
    snprintf(g_layouts[i].name, sizeof g_layouts[i].name, "%s", name);
    g_layouts[i].text = buf;
    layout_count(&g_layouts[i]);
    layouts_save();
    return true;
}

static void layout_delete(int i) {
    if (i < 0 || i >= g_nlayouts) return;
    free(g_layouts[i].text);
    memmove(&g_layouts[i], &g_layouts[i + 1], (size_t)(g_nlayouts - i - 1) * sizeof(SavedLayout));
    g_nlayouts--;
    layouts_save();
}

// Replace the window's panels and tabs with a saved layout.
static bool layout_open(int i) {
    if (i < 0 || i >= g_nlayouts || !g_layouts[i].text) return false;
    session_clear();
    char *text = strdup(g_layouts[i].text);
    for (char *p = text, *line; (line = strsep(&p, "\n"));) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        session_line(line, eq + 1);
    }
    free(text);
    bool any = false;
    for (int k = 0; k < A.nsess_tabs && !any; k++) { // keep the current window if every folder is gone
        const char *p3 = A.sess_tabs[k];
        for (int f = 0; f < 2 && p3; f++) { p3 = strchr(p3, '\t'); if (p3) p3++; }
        if (p3) {
            char dir[PATH_MAX];
            snprintf(dir, sizeof dir, "%.*s", (int)strcspn(p3, "\t"), p3);
            any = is_dir_path(dir);
        }
    }
    if (!any) { session_clear(); toast("None of the folders in \xE2\x80\x9C%s\xE2\x80\x9D exist any more", g_layouts[i].name); return false; }
    A.menu.open = false;
    A.pal_open = false;
    layout_reset();
    if (!session_restore()) tab_new(getenv("HOME") ? getenv("HOME") : "/", 0, true);
    session_clear();
    update_title();
    A.settings_dirty = true;
    layout();
    return true;
}

// --- per-folder view settings ($XDG_CONFIG_HOME/file-explorer/folders.conf) --

static FolderView *g_fv;
static int g_nfv;
static bool g_fv_dirty;

static void folders_file(char *out, size_t cap) {
    char dir[PATH_MAX];
    settings_file(dir, sizeof dir, true);
    snprintf(out, cap, "%s/folders.conf", dir);
}

static void folders_load(void) {
    for (int i = 0; i < g_nfv; i++) free(g_fv[i].path);
    free(g_fv);
    g_fv = NULL;
    g_nfv = 0;
    char path[PATH_MAX];
    folders_file(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[PATH_MAX + 64];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        // view icon sort desc group recursive<TAB>path
        int v, ic, so, de, gr, rec, off = 0;
        if (sscanf(line, "%d %d %d %d %d %d\t%n", &v, &ic, &so, &de, &gr, &rec, &off) != 6 || !off || line[off] != '/') continue;
        g_fv = xrealloc(g_fv, (size_t)(g_nfv + 1) * sizeof(FolderView));
        g_fv[g_nfv++] = (FolderView){strdup(line + off), CLAMP(v, 0, 2), CLAMP(ic, 32, 256), CLAMP(so, 0, SORT_COUNT - 1),
                                     CLAMP(gr, 0, GROUP_COUNT - 1), de != 0, rec != 0};
    }
    fclose(f);
}

static void folders_save(void) {
    if (!g_fv_dirty) return;
    char path[PATH_MAX], tmp[PATH_MAX + 8], dir[PATH_MAX];
    settings_file(dir, sizeof dir, true);
    mkdir_p(dir, 0700);
    folders_file(path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    for (int i = 0; i < g_nfv; i++)
        if (!strpbrk(g_fv[i].path, "\n\r")) fprintf(f, "%d %d %d %d %d %d\t%s\n", g_fv[i].view, g_fv[i].icon, g_fv[i].sort, g_fv[i].desc, g_fv[i].group,
                g_fv[i].recursive, g_fv[i].path);
    if (fclose(f) == 0) rename(tmp, path);
    g_fv_dirty = false;
}

static FolderView *folder_view_find(const char *path, bool exact) {
    for (int i = 0; i < g_nfv; i++)
        if (!strcmp(g_fv[i].path, path)) return &g_fv[i];
    if (exact) return NULL;
    // nearest ancestor that applies to its subfolders
    FolderView *best = NULL;
    size_t bl = 0;
    for (int i = 0; i < g_nfv; i++) {
        size_t l = strlen(g_fv[i].path);
        if (!g_fv[i].recursive || l <= bl) continue;
        if (!strncmp(path, g_fv[i].path, l) && (path[l] == '/' || l == 1)) { best = &g_fv[i]; bl = l; }
    }
    return best;
}

// The pane changed view/sort/grouping: remember it for this folder.
static void folder_view_remember(Pane *p) {
    if (!p->path[0] || (p->L && p->L->is_search)) return;
    FolderView *fv = folder_view_find(p->path, true);
    if (!fv) {
        g_fv = xrealloc(g_fv, (size_t)(g_nfv + 1) * sizeof(FolderView));
        fv = &g_fv[g_nfv++];
        memset(fv, 0, sizeof *fv);
        fv->path = strdup(p->path);
    }
    fv->view = p->view;
    fv->icon = p->icon_px;
    fv->sort = p->sort_key;
    fv->desc = p->sort_desc;
    fv->group = p->group;
    g_fv_dirty = true;
    A.settings_dirty = true;
}

// Settings for a folder about to be shown: its own, an ancestor's (recursive), or the defaults.
static void folder_view_apply(Pane *p, const char *path) {
    FolderView *fv = folder_view_find(path, false);
    p->view = fv ? fv->view : A.def_view;
    p->icon_px = fv ? fv->icon : A.def_icon;
    p->sort_key = fv ? fv->sort : A.def_sort_key;
    p->sort_desc = fv ? fv->desc : A.def_sort_desc;
    p->group = fv ? fv->group : A.def_group;
}

static void folder_view_action(Pane *p, int act) {
    if (act == ACT_FV_SUBFOLDERS) {
        folder_view_remember(p);
        FolderView *fv = folder_view_find(p->path, true);
        if (fv) fv->recursive = !fv->recursive;
        toast(fv && fv->recursive ? "Subfolders of \xE2\x80\x9C%s\xE2\x80\x9D use this view" : "Subfolders use their own views",
              p->path[1] ? path_basename(p->path) : "/");
    } else if (act == ACT_FV_ALL) {
        A.def_view = p->view;
        A.def_icon = p->icon_px;
        A.def_sort_key = p->sort_key;
        A.def_sort_desc = p->sort_desc;
        A.def_group = p->group;
        for (int i = 0; i < g_nfv; i++) free(g_fv[i].path);
        g_nfv = 0;
        toast("All folders now use this view");
    } else if (act == ACT_FV_RESET) {
        FolderView *fv = folder_view_find(p->path, true);
        if (fv) {
            free(fv->path);
            *fv = g_fv[--g_nfv];
        }
        folder_view_apply(p, p->path);
        layout();
        pane_resort(p);
        toast("Folder view reset");
    }
    g_fv_dirty = true;
    A.settings_dirty = true;
}

static void pane_set_group(Pane *p, int g) {
    p->group = g;
    folder_view_remember(p);
    pane_resort(p);
}
