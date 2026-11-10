// shape.c - complex scripts: right-to-left reordering (a compact Unicode bidi algorithm for one line) and
// shaping with HarfBuzz (Arabic/Persian joining, Indic clusters, combining marks). Text without any of
// those characters skips all of it and is laid out glyph by glyph as before.

// Does cp need shaping or reordering? (RTL scripts, scripts with clusters, combining marks, bidi controls)
static bool cp_complex(u32 cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0483 && cp <= 0x0489) || (cp >= 0x0590 && cp <= 0x0FFF) ||
           (cp >= 0x1000 && cp <= 0x109F) || (cp >= 0x1700 && cp <= 0x18AF) || (cp >= 0x1A00 && cp <= 0x1BFF) ||
           (cp >= 0x1CD0 && cp <= 0x1CFF) || (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x200C && cp <= 0x200F) ||
           (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069) || (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xA800 && cp <= 0xABFF) || (cp >= 0xFB1D && cp <= 0xFDFF) || (cp >= 0xFE20 && cp <= 0xFE2F) ||
           (cp >= 0xFE70 && cp <= 0xFEFE) || (cp >= 0x10800 && cp <= 0x10FFF) || (cp >= 0x11000 && cp <= 0x11FFF) ||
           (cp >= 0x1E800 && cp <= 0x1EFFF);
}

static bool text_complex(const char *s, int len) {
    const char *end = s + len;
    for (const char *p = s; p < end;) {
        if ((u8)*p < 0xCC) { p++; continue; } // ASCII, Latin, Greek, Cyrillic letters: never complex
        if (cp_complex(utf8_decode(&p, end))) return true;
    }
    return false;
}

// --- bidi ---
// One paragraph, no explicit embeddings (they're rare in names and treated as marks): rules W1-W7, N1-N2,
// I1-I2 and L2 of UAX #9 over coarse character classes.

enum { BD_L, BD_R, BD_AL, BD_EN, BD_AN, BD_ES, BD_CS, BD_ET, BD_NSM, BD_ON };

static u8 bidi_class(u32 cp) {
    if (cp < 0x80) {
        if (cp >= '0' && cp <= '9') return BD_EN;
        if ((cp | 0x20) >= 'a' && (cp | 0x20) <= 'z') return BD_L;
        if (cp == '+' || cp == '-') return BD_ES;
        if (cp == ',' || cp == '.' || cp == ':' || cp == '/') return BD_CS;
        if (cp == '#' || cp == '$' || cp == '%') return BD_ET;
        return BD_ON;
    }
    if (cp < 0x0300) {
        if (cp == 0xA0) return BD_CS;
        if ((cp >= 0xA2 && cp <= 0xA5) || cp == 0xB0 || cp == 0xB1) return BD_ET;
        if (cp < 0xC0 || cp == 0xD7 || cp == 0xF7) return BD_ON;
        return BD_L;
    }
    if (cp <= 0x036F || (cp >= 0x0483 && cp <= 0x0489)) return BD_NSM;
    if (cp >= 0x0590 && cp <= 0x05FF) {
        if ((cp >= 0x0591 && cp <= 0x05BD) || cp == 0x05BF || cp == 0x05C1 || cp == 0x05C2 || cp == 0x05C4 ||
            cp == 0x05C5 || cp == 0x05C7)
            return BD_NSM;
        return BD_R;
    }
    if (cp >= 0x0600 && cp <= 0x06FF) {
        if (cp <= 0x0605 || (cp >= 0x0660 && cp <= 0x0669) || cp == 0x066B || cp == 0x066C) return BD_AN;
        if (cp >= 0x06F0 && cp <= 0x06F9) return BD_EN; // Persian digits
        if (cp == 0x066A) return BD_ET;
        if (cp == 0x060C) return BD_CS;
        if ((cp >= 0x0610 && cp <= 0x061A) || (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 ||
            (cp >= 0x06D6 && cp <= 0x06DC) || (cp >= 0x06DF && cp <= 0x06E4) || cp == 0x06E7 || cp == 0x06E8 ||
            (cp >= 0x06EA && cp <= 0x06ED))
            return BD_NSM;
        return BD_AL;
    }
    if (cp >= 0x0700 && cp <= 0x08FF) {
        if (cp == 0x0711 || (cp >= 0x0730 && cp <= 0x074A) || (cp >= 0x07A6 && cp <= 0x07B0) ||
            (cp >= 0x07EB && cp <= 0x07F3) || (cp >= 0x0816 && cp <= 0x082D) || (cp >= 0x0859 && cp <= 0x085B) ||
            (cp >= 0x0898 && cp <= 0x089F) || (cp >= 0x08CA && cp <= 0x08FF && cp != 0x08E2))
            return BD_NSM;
        return (cp >= 0x07C0 && cp <= 0x085F) ? BD_R : BD_AL; // NKo, Samaritan, Mandaic are R; Syriac, Thaana AL
    }
    if ((cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE20 && cp <= 0xFE2F))
        return BD_NSM;
    if (cp == 0x200E) return BD_L; // LRM
    if (cp == 0x200F) return BD_R; // RLM
    if (cp == 0x061C) return BD_AL;
    if ((cp >= 0x200B && cp <= 0x200D) || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x206F) ||
        cp == 0xFEFF)
        return BD_NSM; // zero-width joiners and ignored bidi controls stay with what's before them
    if (cp >= 0x2000 && cp <= 0x206F) return (cp >= 0x2030 && cp <= 0x2034) ? BD_ET : BD_ON;
    if (cp >= 0x20A0 && cp <= 0x20CF) return BD_ET;
    if ((cp >= 0x2100 && cp <= 0x2BFF) || (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFE50 && cp <= 0xFE6F) ||
        (cp >= 0x1F000 && cp <= 0x1FAFF))
        return BD_ON;
    if (cp >= 0xFF10 && cp <= 0xFF19) return BD_EN;
    if (cp >= 0xFB1D && cp <= 0xFB4F) return cp == 0xFB1E ? BD_NSM : BD_R;
    if ((cp >= 0xFB50 && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFE)) return (cp == 0xFD3E || cp == 0xFD3F) ? BD_ON : BD_AL;
    if (cp >= 0x10800 && cp <= 0x10FFF) return BD_R;
    if (cp >= 0x1E800 && cp <= 0x1EFFF) return (cp >= 0x1EE00 && cp <= 0x1EEFF) ? BD_AL : BD_R;
    return BD_L;
}

static u32 bidi_mirror(u32 cp) {
    static const u32 pairs[][2] = {{'(', ')'}, {'[', ']'}, {'{', '}'}, {'<', '>'}, {0xAB, 0xBB}, {0x2039, 0x203A}};
    for (int i = 0; i < (int)ARRAY_COUNT(pairs); i++) {
        if (cp == pairs[i][0]) return pairs[i][1];
        if (cp == pairs[i][1]) return pairs[i][0];
    }
    return cp;
}

// Resolve embedding levels of cp[0..n) into lv; returns the paragraph level (first strong character).
static int bidi_levels(const u32 *cp, int n, u8 *cls, u8 *lv) {
    int para = 0;
    for (int i = 0; i < n; i++) cls[i] = bidi_class(cp[i]);
    for (int i = 0; i < n && cls[i] != BD_L; i++)
        if (cls[i] == BD_R || cls[i] == BD_AL) { para = 1; break; }
    u8 sos = para ? BD_R : BD_L;
    for (int i = 0; i < n; i++) // W1: marks take the class before them
        if (cls[i] == BD_NSM) cls[i] = i ? cls[i - 1] : sos;
    u8 strong = sos; // W2 + W3: digits after Arabic letters are Arabic numbers; AL becomes R
    for (int i = 0; i < n; i++) {
        if (cls[i] == BD_L || cls[i] == BD_R || cls[i] == BD_AL) strong = cls[i];
        if (cls[i] == BD_EN && strong == BD_AL) cls[i] = BD_AN;
    }
    for (int i = 0; i < n; i++)
        if (cls[i] == BD_AL) cls[i] = BD_R;
    for (int i = 1; i + 1 < n; i++) { // W4: one separator between two numbers of a kind
        if (cls[i] == BD_ES && cls[i - 1] == BD_EN && cls[i + 1] == BD_EN) cls[i] = BD_EN;
        if (cls[i] == BD_CS && cls[i - 1] == cls[i + 1] && (cls[i - 1] == BD_EN || cls[i - 1] == BD_AN)) cls[i] = cls[i - 1];
    }
    for (int i = 0; i < n; i++) { // W5: terminators (%, currency) next to European numbers
        if (cls[i] != BD_ET) continue;
        int j = i;
        while (j < n && cls[j] == BD_ET) j++;
        if ((i && cls[i - 1] == BD_EN) || (j < n && cls[j] == BD_EN))
            for (int k = i; k < j; k++) cls[k] = BD_EN;
        i = j - 1;
    }
    for (int i = 0; i < n; i++) // W6
        if (cls[i] == BD_ES || cls[i] == BD_CS || cls[i] == BD_ET) cls[i] = BD_ON;
    strong = sos; // W7: European numbers in left-to-right context are just L
    for (int i = 0; i < n; i++) {
        if (cls[i] == BD_L || cls[i] == BD_R) strong = cls[i];
        if (cls[i] == BD_EN && strong == BD_L) cls[i] = BD_L;
    }
    for (int i = 0; i < n; i++) { // N1 + N2: neutrals take the direction around them, else the paragraph's
        if (cls[i] != BD_ON) continue;
        int j = i;
        while (j < n && cls[j] == BD_ON) j++;
        u8 before = i ? (cls[i - 1] == BD_L ? BD_L : BD_R) : sos, after = j < n ? (cls[j] == BD_L ? BD_L : BD_R) : sos;
        u8 d = before == after ? before : sos;
        for (int k = i; k < j; k++) cls[k] = d;
        i = j - 1;
    }
    for (int i = 0; i < n; i++) { // I1 + I2
        u8 c = cls[i];
        if (!para) lv[i] = c == BD_R ? 1 : (c == BD_AN || c == BD_EN) ? 2 : 0;
        else lv[i] = c == BD_R ? 1 : 2;
    }
    return para;
}

// --- shaping ---

typedef struct {
    u16 fi, gid;  // face + glyph index: font_glyph_id(f, fi, gid) right before drawing (the cache may move)
    f32 x, y;     // pen offset of the glyph origin from the start of the text; y up
    f32 pen, adv; // where its advance starts, and how far
    int cl;       // cluster: index of the first character it shows
    bool rtl;
} ShapedGlyph;

static ShapedGlyph *g_shaped;
static int g_shaped_cap;
static u32 *g_sh_cp;
static u8 *g_sh_cls, *g_sh_lv, *g_sh_face;
static int g_sh_cap, g_sh_n;
static hb_buffer_t *g_hb_buf;

// Face for the logical character at i: keep the one before for marks/joiners and whatever it covers (a word
// stays in one font), else the primary face, else a fallback.
static int shape_face(Font *f, u32 cp, int prev) {
    if (prev >= 0 && (bidi_class(cp) == BD_NSM || FT_Get_Char_Index(f->faces[prev], cp))) return prev;
    if (FT_Get_Char_Index(f->faces[0], cp)) return 0;
    int k = font_fallback(f, cp);
    return k > 0 ? k : prev >= 0 ? prev : 0;
}

// Lay out s in visual order into g_shaped; returns the glyph count, *width gets the advance.
static int text_shape(Font *f, const char *s, int len, f32 *width) {
    if (len + 1 > g_sh_cap) {
        g_sh_cap = len + 64;
        g_sh_cp = xrealloc(g_sh_cp, (size_t)g_sh_cap * sizeof *g_sh_cp);
        g_sh_cls = xrealloc(g_sh_cls, (size_t)g_sh_cap);
        g_sh_lv = xrealloc(g_sh_lv, (size_t)g_sh_cap);
        g_sh_face = xrealloc(g_sh_face, (size_t)g_sh_cap);
    }
    if (!g_hb_buf) g_hb_buf = hb_buffer_create();
    int n = 0;
    for (const char *p = s, *end = s + len; p < end;) {
        u32 c = utf8_decode(&p, end);
        if (c < 0x20 || c == 0x7F) c = 0x2400 + (c == 0x7F ? 0x21 : c); // control chars in names, as font_glyph
        g_sh_cp[n++] = c;
    }
    g_sh_n = n;
    bidi_levels(g_sh_cp, n, g_sh_cls, g_sh_lv);
    for (int i = 0; i < n; i++) {
        if (g_sh_lv[i] & 1) g_sh_cp[i] = bidi_mirror(g_sh_cp[i]); // L4
        g_sh_face[i] = (u8)shape_face(f, g_sh_cp[i], i ? g_sh_face[i - 1] : -1);
    }

    // Runs of one level and one face, in logical order, then put in visual order (L2 over whole runs).
    int run_start[256], run_n = 0, maxlv = 0, minodd = 3;
    for (int i = 0; i < n; i++) {
        if ((i == 0 || g_sh_lv[i] != g_sh_lv[i - 1] || g_sh_face[i] != g_sh_face[i - 1]) && run_n < 255) run_start[run_n++] = i;
        maxlv = MAX(maxlv, g_sh_lv[i]);
        if (g_sh_lv[i] & 1) minodd = MIN(minodd, g_sh_lv[i]);
    }
    run_start[run_n] = n;
    int order[256];
    for (int r = 0; r < run_n; r++) order[r] = r;
    for (int l = maxlv; l >= minodd; l--)
        for (int r = 0; r < run_n;) {
            if (g_sh_lv[run_start[order[r]]] < l) { r++; continue; }
            int e = r;
            while (e < run_n && g_sh_lv[run_start[order[e]]] >= l) e++;
            for (int a = r, b = e - 1; a < b; a++, b--) { int t = order[a]; order[a] = order[b]; order[b] = t; }
            r = e;
        }

    int ng = 0;
    f32 pen = 0;
    for (int k = 0; k < run_n; k++) {
        int r = order[k], a = run_start[r], b = run_start[r + 1], fi = g_sh_face[a];
        if (!f->hb[fi]) f->hb[fi] = hb_ft_font_create_referenced(f->faces[fi]);
        hb_buffer_clear_contents(g_hb_buf);
        hb_buffer_add_codepoints(g_hb_buf, g_sh_cp, n, (unsigned)a, b - a); // whole text as context: joining across runs
        hb_buffer_set_direction(g_hb_buf, (g_sh_lv[a] & 1) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        hb_buffer_guess_segment_properties(g_hb_buf);
        hb_shape(f->hb[fi], g_hb_buf, NULL, 0);
        unsigned gn = 0;
        hb_glyph_info_t *info = hb_buffer_get_glyph_infos(g_hb_buf, &gn);
        hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(g_hb_buf, NULL);
        f32 sc = MIN(f->fscale[fi], 1.f) / 64.f; // colour bitmap faces are shaped at their strike size
        if (ng + (int)gn > g_shaped_cap) {
            g_shaped_cap = ng + (int)gn + 64;
            g_shaped = xrealloc(g_shaped, (size_t)g_shaped_cap * sizeof *g_shaped);
        }
        for (unsigned i = 0; i < gn; i++) {
            f32 adv = pos[i].x_advance * sc;
            g_shaped[ng++] = (ShapedGlyph){(u16)fi, (u16)info[i].codepoint, pen + pos[i].x_offset * sc, pos[i].y_offset * sc,
                                           pen, adv, (int)info[i].cluster, (g_sh_lv[a] & 1) != 0};
            pen += adv;
        }
    }
    *width = pen;
    return ng;
}

static f32 text_width(Font *f, const char *s, int len) {
    f32 w = 0;
    if (text_complex(s, len)) {
        text_shape(f, s, len, &w);
        return w;
    }
    const char *p = s, *end = s + len;
    while (p < end) w += font_glyph(f, utf8_decode(&p, end))->adv;
    return w;
}

// Character index of byte offset off in s.
static int utf8_index(const char *s, int off) {
    int k = 0;
    for (int i = 0; i < off; i++) k += ((u8)s[i] & 0xC0) != 0x80;
    return k;
}

// Visual span of the character at index k after text_shape: [*x0, *x1), and whether it reads right to left.
// A character inside a cluster (ligature, base + marks) gets the cluster's span.
static bool shaped_span(int ng, int k, f32 *x0, f32 *x1) {
    int best = -1;
    for (int i = 0; i < ng; i++)
        if (g_shaped[i].cl <= k && g_shaped[i].cl > best) best = g_shaped[i].cl;
    *x0 = 1e9f, *x1 = -1e9f;
    bool rtl = false;
    for (int i = 0; i < ng; i++) {
        if (g_shaped[i].cl != best) continue;
        *x0 = MIN(*x0, g_shaped[i].pen);
        *x1 = MAX(*x1, g_shaped[i].pen + g_shaped[i].adv);
        rtl = g_shaped[i].rtl;
    }
    if (*x0 > *x1) *x0 = *x1 = 0;
    return rtl;
}

// Caret x for byte offset off of s: before the character there on its reading side (after the last one at the end).
static f32 text_caret_x(Font *f, const char *s, int len, int off) {
    if (!text_complex(s, len)) return text_width(f, s, off);
    f32 w, x0, x1;
    int ng = text_shape(f, s, len, &w), k = utf8_index(s, off);
    if (!g_sh_n) return 0;
    if (k >= g_sh_n) return shaped_span(ng, g_sh_n - 1, &x0, &x1) ? x0 : x1;
    return shaped_span(ng, k, &x0, &x1) ? x1 : x0;
}
