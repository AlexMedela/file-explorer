// plist.c - Apple property lists for talking to iPhones: values (dict, array, string, data, integer, bool, real,
// date), reading XML and binary ("bplist00") ones, writing XML; base64.

typedef enum { PL_DICT, PL_ARRAY, PL_STRING, PL_DATA, PL_INT, PL_BOOL, PL_REAL, PL_DATE } PlType;
typedef struct Plist Plist;
struct Plist {
    PlType t;
    char *key;        // as a dict member
    char *s;          // string
    u8 *data;         // data
    size_t len;       // data length
    i64 i;            // integer, bool
    f64 r;            // real, date (seconds since 2001-01-01)
    Plist **kids;     // dict members / array items
    int n;
};

static Plist *pl_new(PlType t) { Plist *p = xcalloc(1, sizeof *p); p->t = t; return p; }
static Plist *pl_dict(void) { return pl_new(PL_DICT); }
static Plist *pl_array(void) { return pl_new(PL_ARRAY); }
static Plist *pl_str(const char *s) { Plist *p = pl_new(PL_STRING); p->s = strdup(s ? s : ""); return p; }
static Plist *pl_int(i64 i) { Plist *p = pl_new(PL_INT); p->i = i; return p; }
static Plist *pl_bool(bool b) { Plist *p = pl_new(PL_BOOL); p->i = b; return p; }
static Plist *pl_data(const void *d, size_t n) {
    Plist *p = pl_new(PL_DATA);
    p->data = xmalloc(n ? n : 1);
    if (n) memcpy(p->data, d, n);
    p->len = n;
    return p;
}

static void pl_free(Plist *p) {
    if (!p) return;
    for (int i = 0; i < p->n; i++) pl_free(p->kids[i]);
    free(p->kids);
    free(p->key);
    free(p->s);
    free(p->data);
    free(p);
}

static Plist *pl_push(Plist *arr, Plist *v) {
    arr->kids = xrealloc(arr->kids, (size_t)(arr->n + 1) * sizeof *arr->kids);
    arr->kids[arr->n++] = v;
    return v;
}

// Set (or replace) a dict member; takes v.
static Plist *pl_set(Plist *d, const char *key, Plist *v) {
    for (int i = 0; i < d->n; i++)
        if (!strcmp(d->kids[i]->key, key)) {
            v->key = d->kids[i]->key;
            d->kids[i]->key = NULL;
            pl_free(d->kids[i]);
            d->kids[i] = v;
            return v;
        }
    free(v->key);
    v->key = strdup(key);
    return pl_push(d, v);
}

static Plist *pl_get(const Plist *d, const char *key) {
    if (!d || d->t != PL_DICT) return NULL;
    for (int i = 0; i < d->n; i++)
        if (d->kids[i]->key && !strcmp(d->kids[i]->key, key)) return d->kids[i];
    return NULL;
}
static const char *pl_get_str(const Plist *d, const char *key) { Plist *v = pl_get(d, key); return v && v->t == PL_STRING ? v->s : NULL; }
static i64 pl_get_int(const Plist *d, const char *key, i64 def) { Plist *v = pl_get(d, key); return v && (v->t == PL_INT || v->t == PL_BOOL) ? v->i : def; }
static bool pl_get_bool(const Plist *d, const char *key) { return pl_get_int(d, key, 0) != 0; }

static Plist *pl_copy(const Plist *p) {
    Plist *c = pl_new(p->t);
    c->key = p->key ? strdup(p->key) : NULL;
    c->s = p->s ? strdup(p->s) : NULL;
    if (p->data) { c->data = xmalloc(p->len ? p->len : 1); memcpy(c->data, p->data, p->len); }
    c->len = p->len, c->i = p->i, c->r = p->r;
    for (int i = 0; i < p->n; i++) pl_push(c, pl_copy(p->kids[i]));
    return c;
}

// --- base64 ---------------------------------------------------------------------------------------------------

static const char g_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *b64_encode(const u8 *d, size_t n) {
    char *o = xmalloc(n / 3 * 4 + 8);
    size_t k = 0;
    for (size_t i = 0; i < n; i += 3) {
        u32 v = (u32)d[i] << 16 | (i + 1 < n ? (u32)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        o[k++] = g_b64[v >> 18 & 63];
        o[k++] = g_b64[v >> 12 & 63];
        o[k++] = i + 1 < n ? g_b64[v >> 6 & 63] : '=';
        o[k++] = i + 2 < n ? g_b64[v & 63] : '=';
    }
    o[k] = 0;
    return o;
}

// Whitespace is skipped (plists wrap their data lines).
static u8 *b64_decode(const char *s, size_t sl, size_t *outn) {
    u8 *o = xmalloc(sl / 4 * 3 + 4);
    size_t k = 0;
    u32 acc = 0;
    int bits = 0;
    for (size_t i = 0; i < sl; i++) {
        const char *c = s[i] ? strchr(g_b64, s[i]) : NULL;
        if (!c) continue;
        acc = acc << 6 | (u32)(c - g_b64);
        bits += 6;
        if (bits >= 8) { bits -= 8; o[k++] = (u8)(acc >> bits); }
    }
    *outn = k;
    return o;
}

// --- writing XML ----------------------------------------------------------------------------------------------

typedef struct { char *b; size_t n, cap; } PlBuf;

static void pb_add(PlBuf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2; b->b = xrealloc(b->b, b->cap); }
    memcpy(b->b + b->n, s, n);
    b->n += n;
    b->b[b->n] = 0;
}
static void pb_puts(PlBuf *b, const char *s) { pb_add(b, s, strlen(s)); }
static void pb_esc(PlBuf *b, const char *s) {
    for (; *s; s++) {
        if (*s == '<') pb_puts(b, "&lt;");
        else if (*s == '>') pb_puts(b, "&gt;");
        else if (*s == '&') pb_puts(b, "&amp;");
        else pb_add(b, s, 1);
    }
}

static void pl_xml_val(PlBuf *b, const Plist *p, int depth) {
    char tmp[64];
    for (int i = 0; i < depth; i++) pb_puts(b, "\t");
    switch (p->t) {
    case PL_DICT:
        pb_puts(b, "<dict>\n");
        for (int i = 0; i < p->n; i++) {
            for (int k = 0; k <= depth; k++) pb_puts(b, "\t");
            pb_puts(b, "<key>");
            pb_esc(b, p->kids[i]->key ? p->kids[i]->key : "");
            pb_puts(b, "</key>\n");
            pl_xml_val(b, p->kids[i], depth + 1);
        }
        for (int i = 0; i < depth; i++) pb_puts(b, "\t");
        pb_puts(b, "</dict>\n");
        return;
    case PL_ARRAY:
        pb_puts(b, "<array>\n");
        for (int i = 0; i < p->n; i++) pl_xml_val(b, p->kids[i], depth + 1);
        for (int i = 0; i < depth; i++) pb_puts(b, "\t");
        pb_puts(b, "</array>\n");
        return;
    case PL_STRING: pb_puts(b, "<string>"); pb_esc(b, p->s); pb_puts(b, "</string>\n"); return;
    case PL_DATA: {
        char *e = b64_encode(p->data, p->len);
        pb_puts(b, "<data>");
        pb_puts(b, e);
        pb_puts(b, "</data>\n");
        free(e);
        return;
    }
    case PL_INT: snprintf(tmp, sizeof tmp, "<integer>%lld</integer>\n", (long long)p->i); pb_puts(b, tmp); return;
    case PL_BOOL: pb_puts(b, p->i ? "<true/>\n" : "<false/>\n"); return;
    case PL_REAL: snprintf(tmp, sizeof tmp, "<real>%.17g</real>\n", p->r); pb_puts(b, tmp); return;
    case PL_DATE: {
        time_t t = (time_t)(p->r + 978307200.0);
        struct tm tm;
        gmtime_r(&t, &tm);
        strftime(tmp, sizeof tmp, "<date>%Y-%m-%dT%H:%M:%SZ</date>\n", &tm);
        pb_puts(b, tmp);
        return;
    }
    }
}

static char *pl_to_xml(const Plist *p, size_t *len) {
    PlBuf b = {0};
    pb_puts(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n<plist version=\"1.0\">\n");
    pl_xml_val(&b, p, 0);
    pb_puts(&b, "</plist>\n");
    if (len) *len = b.n;
    return b.b;
}

// --- reading XML ----------------------------------------------------------------------------------------------

typedef struct { const char *p, *e; int depth; } PlX;

// The next tag: name into nm, *close for </x>, *empty for <x/>; skips <?...?>, <!...> and text between tags.
static bool plx_tag(PlX *x, char *nm, size_t cap, bool *close, bool *empty) {
    for (;;) {
        while (x->p < x->e && *x->p != '<') x->p++;
        if (x->p >= x->e) return false;
        if (x->p + 1 < x->e && (x->p[1] == '?' || x->p[1] == '!')) {
            const char *g = memchr(x->p, '>', (size_t)(x->e - x->p));
            if (!g) return false;
            x->p = g + 1;
            continue;
        }
        x->p++;
        *close = x->p < x->e && *x->p == '/';
        if (*close) x->p++;
        size_t k = 0;
        while (x->p < x->e && *x->p != '>' && *x->p != '/' && *x->p != ' ' && k + 1 < cap) nm[k++] = *x->p++;
        nm[k] = 0;
        const char *g = memchr(x->p, '>', (size_t)(x->e - x->p));
        if (!g) return false;
        *empty = g > x->p && g[-1] == '/';
        x->p = g + 1;
        return true;
    }
}

// Text up to the closing tag (entities decoded); x->p ends after the closing tag.
static char *plx_text(PlX *x, size_t *outlen) {
    const char *start = x->p;
    const char *lt = memchr(x->p, '<', (size_t)(x->e - x->p));
    if (!lt) lt = x->e;
    char *o = xmalloc((size_t)(lt - start) + 1);
    size_t k = 0;
    for (const char *c = start; c < lt; c++) {
        if (*c == '&') {
            const char *sc = memchr(c, ';', (size_t)(lt - c));
            if (sc) {
                size_t l = (size_t)(sc - c - 1);
                if (l == 2 && !strncmp(c + 1, "lt", 2)) o[k++] = '<';
                else if (l == 2 && !strncmp(c + 1, "gt", 2)) o[k++] = '>';
                else if (l == 3 && !strncmp(c + 1, "amp", 3)) o[k++] = '&';
                else if (l == 4 && !strncmp(c + 1, "quot", 4)) o[k++] = '"';
                else if (l == 4 && !strncmp(c + 1, "apos", 4)) o[k++] = '\'';
                else if (l > 1 && c[1] == '#') {
                    long v = c[2] == 'x' ? strtol(c + 3, NULL, 16) : strtol(c + 2, NULL, 10);
                    if (v < 0x80) o[k++] = (char)v; // (plists from devices only escape ASCII)
                } else { o[k++] = *c; continue; }
                c = sc;
                continue;
            }
        }
        o[k++] = *c;
    }
    o[k] = 0;
    if (outlen) *outlen = k;
    x->p = lt;
    char nm[32];
    bool cl, em;
    plx_tag(x, nm, sizeof nm, &cl, &em); // its closing tag
    return o;
}

static Plist *plx_value(PlX *x, const char *nm, bool empty);

static Plist *plx_next(PlX *x, bool *closed) {
    char nm[32];
    bool cl, em;
    *closed = false;
    if (!plx_tag(x, nm, sizeof nm, &cl, &em)) return NULL;
    if (cl) { *closed = true; return NULL; }
    return plx_value(x, nm, em);
}

static Plist *plx_value(PlX *x, const char *nm, bool empty) {
    if (++x->depth > 64) return NULL;
    Plist *p = NULL;
    bool closed;
    if (!strcmp(nm, "dict")) {
        p = pl_dict();
        while (!empty) {
            char kn[32];
            bool cl, em;
            if (!plx_tag(x, kn, sizeof kn, &cl, &em) || cl) break;
            if (strcmp(kn, "key")) { pl_free(plx_value(x, kn, em)); continue; }
            char *key = em ? strdup("") : plx_text(x, NULL);
            Plist *v = plx_next(x, &closed);
            if (!v) { free(key); break; }
            v->key = key;
            pl_push(p, v);
        }
    } else if (!strcmp(nm, "array")) {
        p = pl_array();
        while (!empty) {
            Plist *v = plx_next(x, &closed);
            if (!v) break;
            pl_push(p, v);
        }
    } else if (!strcmp(nm, "string")) {
        p = pl_new(PL_STRING);
        p->s = empty ? strdup("") : plx_text(x, NULL);
    } else if (!strcmp(nm, "data")) {
        p = pl_new(PL_DATA);
        size_t tl = 0;
        char *t = empty ? strdup("") : plx_text(x, &tl);
        p->data = b64_decode(t, tl, &p->len);
        free(t);
    } else if (!strcmp(nm, "integer")) {
        p = pl_new(PL_INT);
        char *t = empty ? strdup("0") : plx_text(x, NULL);
        p->i = strtoll(t, NULL, 10);
        free(t);
    } else if (!strcmp(nm, "real")) {
        p = pl_new(PL_REAL);
        char *t = empty ? strdup("0") : plx_text(x, NULL);
        p->r = strtod(t, NULL);
        free(t);
    } else if (!strcmp(nm, "true") || !strcmp(nm, "false")) {
        p = pl_bool(nm[0] == 't');
        if (!empty) free(plx_text(x, NULL));
    } else if (!strcmp(nm, "date")) {
        p = pl_new(PL_DATE);
        char *t = empty ? strdup("") : plx_text(x, NULL);
        struct tm tm;
        memset(&tm, 0, sizeof tm);
        if (strptime(t, "%Y-%m-%dT%H:%M:%S", &tm)) p->r = (f64)timegm(&tm) - 978307200.0;
        free(t);
    } else if (!strcmp(nm, "plist")) {
        p = empty ? NULL : plx_next(x, &closed);
    } else if (!empty) {
        free(plx_text(x, NULL)); // unknown: skipped
    }
    x->depth--;
    return p;
}

// --- reading binary plists ------------------------------------------------------------------------------------

typedef struct { const u8 *d; size_t n; int osz, rsz; u64 nobj, otab; int depth; } PlB;

static u64 plb_uint(const u8 *p, int sz) { u64 v = 0; for (int i = 0; i < sz; i++) v = v << 8 | p[i]; return v; }

static Plist *plb_obj(PlB *b, u64 idx);

// Length of an object (its low nibble, or an int object after the marker when that is 0xF); *at: where data starts.
static bool plb_len(PlB *b, size_t off, u64 *len, size_t *at) {
    u8 m = b->d[off];
    *at = off + 1;
    if ((m & 15) != 15) { *len = m & 15; return true; }
    if (*at >= b->n || (b->d[*at] & 0xf0) != 0x10) return false;
    int sz = 1 << (b->d[*at] & 15);
    if (sz > 8 || *at + 1 + (size_t)sz > b->n) return false;
    *len = plb_uint(b->d + *at + 1, sz);
    *at += 1 + (size_t)sz;
    return true;
}

static Plist *plb_obj(PlB *b, u64 idx) {
    if (idx >= b->nobj || ++b->depth > 64) return NULL;
    size_t oe = b->otab + idx * (u64)b->osz;
    if (oe + (size_t)b->osz > b->n) { b->depth--; return NULL; }
    size_t off = plb_uint(b->d + oe, b->osz);
    if (off >= b->n) { b->depth--; return NULL; }
    u8 m = b->d[off], hi = m >> 4;
    Plist *p = NULL;
    u64 len;
    size_t at;
    switch (hi) {
    case 0x0: if (m == 0x08 || m == 0x09) p = pl_bool(m == 0x09); break;
    case 0x1: {
        int sz = 1 << (m & 15);
        if (sz <= 8 && off + 1 + (size_t)sz <= b->n) { p = pl_int((i64)plb_uint(b->d + off + 1, sz)); }
        break;
    }
    case 0x2: {
        int sz = 1 << (m & 15);
        if (off + 1 + (size_t)sz > b->n) break;
        p = pl_new(PL_REAL);
        u64 v = plb_uint(b->d + off + 1, sz);
        if (sz == 4) { u32 v32 = (u32)v; f32 f; memcpy(&f, &v32, 4); p->r = f; }
        else if (sz == 8) { f64 f; memcpy(&f, &v, 8); p->r = f; }
        break;
    }
    case 0x3:
        if (off + 9 > b->n) break;
        p = pl_new(PL_DATE);
        { u64 v = plb_uint(b->d + off + 1, 8); f64 f; memcpy(&f, &v, 8); p->r = f; }
        break;
    case 0x4:
        if (!plb_len(b, off, &len, &at) || at + len > b->n) break;
        p = pl_data(b->d + at, len);
        break;
    case 0x5:
        if (!plb_len(b, off, &len, &at) || at + len > b->n) break;
        p = pl_new(PL_STRING);
        p->s = xmalloc(len + 1);
        memcpy(p->s, b->d + at, len);
        p->s[len] = 0;
        break;
    case 0x6: { // UTF-16BE -> UTF-8
        if (!plb_len(b, off, &len, &at) || at + len * 2 > b->n) break;
        p = pl_new(PL_STRING);
        p->s = xmalloc(len * 4 + 1);
        size_t k = 0;
        for (u64 i = 0; i < len; i++) {
            u32 c = (u32)b->d[at + i * 2] << 8 | b->d[at + i * 2 + 1];
            if (c >= 0xd800 && c < 0xdc00 && i + 1 < len) {
                u32 c2 = (u32)b->d[at + i * 2 + 2] << 8 | b->d[at + i * 2 + 3];
                c = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00);
                i++;
            }
            if (c < 0x80) p->s[k++] = (char)c;
            else if (c < 0x800) { p->s[k++] = (char)(0xc0 | c >> 6); p->s[k++] = (char)(0x80 | (c & 63)); }
            else if (c < 0x10000) { p->s[k++] = (char)(0xe0 | c >> 12); p->s[k++] = (char)(0x80 | (c >> 6 & 63)); p->s[k++] = (char)(0x80 | (c & 63)); }
            else { p->s[k++] = (char)(0xf0 | c >> 18); p->s[k++] = (char)(0x80 | (c >> 12 & 63)); p->s[k++] = (char)(0x80 | (c >> 6 & 63)); p->s[k++] = (char)(0x80 | (c & 63)); }
        }
        p->s[k] = 0;
        break;
    }
    case 0x8: p = pl_int((i64)plb_uint(b->d + off + 1, (m & 15) + 1)); break; // UID: as an integer
    case 0xA:
        if (!plb_len(b, off, &len, &at) || at + len * (u64)b->rsz > b->n) break;
        p = pl_array();
        for (u64 i = 0; i < len; i++) {
            Plist *v = plb_obj(b, plb_uint(b->d + at + i * (u64)b->rsz, b->rsz));
            if (v) pl_push(p, v);
        }
        break;
    case 0xD:
        if (!plb_len(b, off, &len, &at) || at + len * 2 * (u64)b->rsz > b->n) break;
        p = pl_dict();
        for (u64 i = 0; i < len; i++) {
            Plist *k = plb_obj(b, plb_uint(b->d + at + i * (u64)b->rsz, b->rsz));
            Plist *v = plb_obj(b, plb_uint(b->d + at + (len + i) * (u64)b->rsz, b->rsz));
            if (k && v && k->t == PL_STRING) { v->key = k->s; k->s = NULL; pl_push(p, v); }
            else pl_free(v);
            pl_free(k);
        }
        break;
    }
    b->depth--;
    return p;
}

// Parse an XML or binary plist. NULL if it isn't one.
static Plist *pl_parse(const void *buf, size_t n) {
    const u8 *d = buf;
    if (n >= 40 && !memcmp(d, "bplist00", 8)) {
        const u8 *t = d + n - 32;
        PlB b = {d, n, t[6], t[7], plb_uint(t + 8, 8), plb_uint(t + 24, 8), 0};
        u64 top = plb_uint(t + 16, 8);
        if (b.osz < 1 || b.osz > 8 || b.rsz < 1 || b.rsz > 8 || b.otab >= n) return NULL;
        return plb_obj(&b, top);
    }
    PlX x = {buf, (const char *)buf + n, 0};
    bool closed;
    return plx_next(&x, &closed);
}
