// draw.c - batched 2D geometry: everything (shapes + text) samples one atlas
// texture, so a typical frame is a handful of SDL_RenderGeometry calls.

typedef struct { f32 r, g, b, a; } Color;

static inline Color C(u32 hex) {
    return (Color){((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, 1.f};
}
static inline Color CA(u32 hex, f32 a) { Color c = C(hex); c.a = a; return c; }

static SDL_Renderer *g_ren;
static f32 g_alpha = 1.f; // multiplied into every vertex (used to dim cut items)
static SDL_Rect g_clip_cur;
static bool g_clip_on;
static SDL_Vertex *g_v;
static int g_nv, g_capv;
static int *g_idx;
static int g_ni, g_capi;

#define WU (2.f / ATLAS_SIZE) // uv of the white block

static inline void draw_reserve(int nv, int ni) {
    if (g_nv + nv > g_capv) {
        g_capv = MAX(g_capv * 2, g_nv + nv + 1024);
        g_v = xrealloc(g_v, (size_t)g_capv * sizeof(SDL_Vertex));
    }
    if (g_ni + ni > g_capi) {
        g_capi = MAX(g_capi * 2, g_ni + ni + 2048);
        g_idx = xrealloc(g_idx, (size_t)g_capi * sizeof(int));
    }
}

static inline int dv(f32 x, f32 y, Color c, f32 u, f32 v) {
    SDL_Vertex *p = &g_v[g_nv];
    p->position.x = x;
    p->position.y = y;
    p->color.r = c.r;
    p->color.g = c.g;
    p->color.b = c.b;
    p->color.a = c.a * g_alpha;
    p->tex_coord.x = u;
    p->tex_coord.y = v;
    return g_nv++;
}

static inline void di3(int a, int b, int c) {
    g_idx[g_ni++] = a;
    g_idx[g_ni++] = b;
    g_idx[g_ni++] = c;
}

static inline void di4(int a, int b, int c, int d) {
    di3(a, b, c);
    di3(a, c, d);
}

static void draw_flush(void) {
    atlas_upload();
    if (g_ni) SDL_RenderGeometry(g_ren, g_atlas_tex, g_v, g_nv, g_idx, g_ni);
    g_nv = g_ni = 0;
}

static void draw_clip(const SDL_Rect *r) {
    draw_flush();
    g_clip_on = r != NULL;
    if (r) g_clip_cur = *r;
    SDL_SetRenderClipRect(g_ren, r);
}

typedef struct { SDL_Rect r; bool on; } ClipSave;

// Nested clip: intersect with the current clip; restore with draw_clip_pop.
static ClipSave draw_clip_push(SDL_Rect r) {
    ClipSave s = {g_clip_cur, g_clip_on};
    SDL_Rect x = r;
    if (g_clip_on && !SDL_GetRectIntersection(&g_clip_cur, &r, &x)) x = (SDL_Rect){0, 0, 0, 0};
    draw_clip(&x);
    return s;
}

static void draw_clip_pop(ClipSave s) { draw_clip(s.on ? &s.r : NULL); }

static void draw_rect(f32 x, f32 y, f32 w, f32 h, Color c) {
    if (w <= 0 || h <= 0) return;
    draw_reserve(4, 6);
    int a = dv(x, y, c, WU, WU), b = dv(x + w, y, c, WU, WU);
    int d = dv(x + w, y + h, c, WU, WU), e = dv(x, y + h, c, WU, WU);
    di4(a, b, d, e);
}

// Anti-aliased rounded rectangle: solid fan + 1px feathered outer ring.
static void draw_rrect(f32 x, f32 y, f32 w, f32 h, f32 r, Color c) {
    if (w <= 0 || h <= 0) return;
    r = MIN(r, MIN(w, h) * 0.5f);
    if (r < 0.75f) { draw_rect(x, y, w, h, c); return; }
    int seg = CLAMP((int)(r * 0.6f) + 2, 2, 10);
    int nper = 4 * (seg + 1);
    draw_reserve(1 + nper * 2, nper * 9);
    Color co = c;
    co.a = 0;
    int center = dv(x + w * 0.5f, y + h * 0.5f, c, WU, WU);
    int base = g_nv;
    f32 cx[4] = {x + r, x + w - r, x + w - r, x + r};
    f32 cy[4] = {y + r, y + r, y + h - r, y + h - r};
    for (int k = 0; k < 4; k++) {
        f32 a0 = (f32)M_PI + k * (f32)M_PI * 0.5f;
        for (int i = 0; i <= seg; i++) {
            f32 a = a0 + (f32)M_PI * 0.5f * (f32)i / (f32)seg;
            f32 dx = cosf(a), dy = sinf(a);
            dv(cx[k] + dx * (r - 0.5f), cy[k] + dy * (r - 0.5f), c, WU, WU);
            dv(cx[k] + dx * (r + 0.5f), cy[k] + dy * (r + 0.5f), co, WU, WU);
        }
    }
    for (int i = 0; i < nper; i++) {
        int j = (i + 1) % nper;
        int ii = base + 2 * i, io = ii + 1, ji = base + 2 * j, jo = ji + 1;
        di3(center, ii, ji);
        di4(ii, io, jo, ji);
    }
}

// Rounded-rect outline of thickness t: one continuous ring around the whole shape, feathered (1 px) on both
// its outer and inner edge, so the inside of the corners is as smooth as the outside.
static void draw_rrect_outline(f32 x, f32 y, f32 w, f32 h, f32 r, f32 t, Color c) {
    if (w <= 0 || h <= 0 || t <= 0) return;
    // whole pixels: with the feathers centred on the edges, straight sides then cover exactly t pixel columns
    // (crisp) and only the curves get partial coverage
    f32 x1 = roundf(x + w), y1 = roundf(y + h);
    x = roundf(x);
    y = roundf(y);
    w = x1 - x;
    h = y1 - y;
    if (t < 0.75f) { c.a *= t; t = 1.f; } // hairline: one pixel, lighter
    else t = roundf(t);
    r = CLAMP(r, 0.f, MIN(w, h) * 0.5f);
    t = MIN(t, MIN(w, h) * 0.5f);
    int seg = CLAMP((int)(r * 0.8f) + 3, 3, 16);
    int nper = 4 * (seg + 1);
    draw_reserve(nper * 4, nper * 18);
    Color co = c;
    co.a = 0;
    int base = g_nv;
    f32 cx[4] = {x + r, x + w - r, x + w - r, x + r};
    f32 cy[4] = {y + r, y + r, y + h - r, y + h - r};
    f32 ri = MAX(0.f, r - t); // inner corner radius (square inside when the border is thicker than the radius)
    for (int k = 0; k < 4; k++) {
        f32 a0 = (f32)M_PI + k * (f32)M_PI * 0.5f;
        // inner contour: same corner direction, but its centre sits t further in when ri hits 0
        f32 icx = k == 0 || k == 3 ? MAX(cx[k], x + t) : MIN(cx[k], x + w - t);
        f32 icy = k < 2 ? MAX(cy[k], y + t) : MIN(cy[k], y + h - t);
        for (int i = 0; i <= seg; i++) {
            f32 a = a0 + (f32)M_PI * 0.5f * (f32)i / (f32)seg;
            f32 dx = cosf(a), dy = sinf(a);
            dv(cx[k] + dx * (r + 0.5f), cy[k] + dy * (r + 0.5f), co, WU, WU);   // outer feather
            dv(cx[k] + dx * (r - 0.5f), cy[k] + dy * (r - 0.5f), c, WU, WU);    // outer edge
            dv(icx + dx * (ri + 0.5f), icy + dy * (ri + 0.5f), c, WU, WU);      // inner edge
            dv(icx + dx * MAX(0.f, ri - 0.5f), icy + dy * MAX(0.f, ri - 0.5f), co, WU, WU); // inner feather
        }
    }
    for (int i = 0; i < nper; i++) {
        int j = (i + 1) % nper;
        int a = base + 4 * i, b = base + 4 * j;
        di4(a, a + 1, b + 1, b);
        di4(a + 1, a + 2, b + 2, b + 1);
        di4(a + 2, a + 3, b + 3, b + 2);
    }
}

// A right-angled polygon (vertices clockwise on screen, axis-aligned edges) with rounded corners - radius rc where it
// turns outward, ri where it turns inward - filled with fill and outlined (1 px) with line, either may be
// transparent. The same edge maths as draw_rrect / draw_rrect_outline (contour on the given edges, 1 px feathers),
// so a shape made of several boxes looks exactly like one of them, with no seams. The fill is a fan from (qx, qy),
// which must see the whole shape.
static void draw_rpoly(const f32 *vx, const f32 *vy, int nv, f32 rc, f32 ri, f32 qx, f32 qy, Color fill, Color line) {
    enum { MAXS = 16 * 17 };
    f32 px[MAXS], py[MAXS], nx[MAXS], ny[MAXS];
    int ns = 0;
    for (int i = 0; i < nv && ns < MAXS - 17; i++) {
        int pi = (i + nv - 1) % nv, ni = (i + 1) % nv;
        f32 ix = vx[i] - vx[pi], iy = vy[i] - vy[pi], ox = vx[ni] - vx[i], oy = vy[ni] - vy[i];
        f32 li = sqrtf(ix * ix + iy * iy), lo = sqrtf(ox * ox + oy * oy);
        if (li <= 0 || lo <= 0) continue;
        ix /= li; iy /= li; ox /= lo; oy /= lo;
        bool convex = ix * oy - iy * ox > 0;
        f32 r = MIN(convex ? rc : ri, MIN(li, lo) * 0.5f);
        f32 cx = vx[i] - ix * r + ox * r, cy = vy[i] - iy * r + oy * r; // the arc's centre
        f32 a0 = atan2f(vy[i] - iy * r - cy, vx[i] - ix * r - cx), sweep = (convex ? 1.f : -1.f) * (f32)M_PI * 0.5f;
        int seg = r < 0.5f ? 0 : CLAMP((int)(r * 0.8f) + 3, 3, 16);
        for (int k = 0; k <= seg; k++) {
            f32 a = a0 + sweep * (f32)k / (f32)MAX(seg, 1), dx = cosf(a), dy = sinf(a);
            px[ns] = cx + dx * r;
            py[ns] = cy + dy * r;
            if (seg) { nx[ns] = convex ? dx : -dx; ny[ns] = convex ? dy : -dy; }
            else { nx[ns] = -iy + -oy; ny[ns] = ix + ox; } // sharp corner: mitred
            ns++;
        }
    }
    if (ns < 3) return;
    if (fill.a) {
        Color fo = fill;
        fo.a = 0;
        draw_reserve(1 + ns * 2, ns * 9);
        int q = dv(qx, qy, fill, WU, WU), base = g_nv;
        for (int i = 0; i < ns; i++) {
            dv(px[i] - nx[i] * 0.5f, py[i] - ny[i] * 0.5f, fill, WU, WU);
            dv(px[i] + nx[i] * 0.5f, py[i] + ny[i] * 0.5f, fo, WU, WU);
        }
        for (int i = 0; i < ns; i++) {
            int j = (i + 1) % ns, ii = base + 2 * i, jj = base + 2 * j;
            di3(q, ii, jj);
            di4(ii, ii + 1, jj + 1, jj);
        }
    }
    if (line.a) {
        Color lo = line;
        lo.a = 0;
        draw_reserve(ns * 4, ns * 18);
        int base = g_nv;
        for (int i = 0; i < ns; i++) {
            dv(px[i] + nx[i] * 0.5f, py[i] + ny[i] * 0.5f, lo, WU, WU);   // outer feather
            dv(px[i] - nx[i] * 0.5f, py[i] - ny[i] * 0.5f, line, WU, WU); // outer edge
            dv(px[i] - nx[i] * 0.5f, py[i] - ny[i] * 0.5f, line, WU, WU); // inner edge (1 px: the same place)
            dv(px[i] - nx[i] * 1.5f, py[i] - ny[i] * 1.5f, lo, WU, WU);   // inner feather
        }
        for (int i = 0; i < ns; i++) {
            int j = (i + 1) % ns, a = base + 4 * i, b = base + 4 * j;
            di4(a, a + 1, b + 1, b);
            di4(a + 1, a + 2, b + 2, b + 1);
            di4(a + 2, a + 3, b + 3, b + 2);
        }
    }
}

// Concave corner ("flare"): fills the region between corner point (kx, ky) and a quarter arc of radius r
// around (cx, cy) from angle a0 to a0 + 90 degrees, anti-aliased along the arc. Only that region is painted,
// so whatever is underneath shows through the carved-out part.
static void draw_flare(f32 cx, f32 cy, f32 r, f32 a0, f32 kx, f32 ky, Color c) {
    if (r < 1) return;
    int seg = CLAMP((int)(r * 0.8f) + 3, 3, 16);
    draw_reserve(1 + (seg + 1) * 2, seg * 9);
    Color co = c;
    co.a = 0;
    int k = dv(kx, ky, c, WU, WU), base = g_nv;
    for (int i = 0; i <= seg; i++) {
        f32 a = a0 + (f32)M_PI * 0.5f * (f32)i / (f32)seg, dx = cosf(a), dy = sinf(a);
        dv(cx + dx * (r + 0.5f), cy + dy * (r + 0.5f), c, WU, WU);   // solid, just outside the arc
        dv(cx + dx * (r - 0.5f), cy + dy * (r - 0.5f), co, WU, WU);  // feather into the carved side
    }
    for (int i = 0; i < seg; i++) {
        int s0 = base + 2 * i, s1 = s0 + 2;
        di3(k, s0, s1);
        di4(s0, s0 + 1, s1 + 1, s1);
    }
}

// Pixel-snapped plus / minus: whole-pixel arms of thickness t, the length adjusted so both arms share the exact
// centre (crisp at any scale; no half-pixel blur or one-pixel offset between the two strokes).
static void draw_plus_ex(f32 cx, f32 cy, f32 len, f32 th, Color c, bool vertical) {
    int t = MAX(1, (int)roundf(th)), L = MAX(t, (int)roundf(len));
    if ((L - t) & 1) L++; // same parity as the stroke: the crossing sits exactly in the middle
    f32 x0 = roundf(cx - L * 0.5f), y0 = roundf(cy - L * 0.5f), o = (f32)((L - t) / 2);
    draw_rect(x0, y0 + o, (f32)L, (f32)t, c);
    if (vertical) {
        draw_rect(x0 + o, y0, (f32)t, o, c);                // above the horizontal arm
        draw_rect(x0 + o, y0 + o + t, (f32)t, (f32)L - o - t, c); // below it (no overlap: even with alpha)
    }
}
static void draw_plus(f32 cx, f32 cy, f32 len, f32 th, Color c) { draw_plus_ex(cx, cy, len, th, c, true); }
static void draw_minus(f32 cx, f32 cy, f32 len, f32 th, Color c) { draw_plus_ex(cx, cy, len, th, c, false); }

// Anti-aliased thick line.
static void draw_line(f32 x0, f32 y0, f32 x1, f32 y1, f32 t, Color c) {
    f32 dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-3f) return;
    dx /= l;
    dy /= l;
    f32 nx = -dy, ny = dx;
    f32 hi = MAX(t * 0.5f - 0.5f, 0.f), ho = t * 0.5f + 0.5f;
    // extend slightly so joints of polylines meet
    x0 -= dx * 0.5f; y0 -= dy * 0.5f; x1 += dx * 0.5f; y1 += dy * 0.5f;
    Color co = c;
    co.a = 0;
    draw_reserve(8, 18);
    int a0 = dv(x0 + nx * ho, y0 + ny * ho, co, WU, WU), a1 = dv(x0 + nx * hi, y0 + ny * hi, c, WU, WU);
    int a2 = dv(x0 - nx * hi, y0 - ny * hi, c, WU, WU), a3 = dv(x0 - nx * ho, y0 - ny * ho, co, WU, WU);
    int b0 = dv(x1 + nx * ho, y1 + ny * ho, co, WU, WU), b1 = dv(x1 + nx * hi, y1 + ny * hi, c, WU, WU);
    int b2 = dv(x1 - nx * hi, y1 - ny * hi, c, WU, WU), b3 = dv(x1 - nx * ho, y1 - ny * ho, co, WU, WU);
    di4(a0, a1, b1, b0);
    di4(a1, a2, b2, b1);
    di4(a2, a3, b3, b2);
}

// Filled triangle with anti-aliased edges (a hairline along each side).
static void draw_tri(f32 x0, f32 y0, f32 x1, f32 y1, f32 x2, f32 y2, Color c) {
    draw_reserve(3, 3);
    int a = dv(x0, y0, c, WU, WU), b = dv(x1, y1, c, WU, WU), d = dv(x2, y2, c, WU, WU);
    di3(a, b, d);
    draw_line(x0, y0, x1, y1, 1.f, c);
    draw_line(x1, y1, x2, y2, 1.f, c);
    draw_line(x2, y2, x0, y0, 1.f, c);
}

// Ring segment between radii r0 and r1 from angle a0 to a1 (radians, clockwise from +x on screen).
static void draw_arc_band(f32 cx, f32 cy, f32 r0, f32 r1, f32 a0, f32 a1, Color c) {
    if (a1 <= a0 || r1 <= r0) return;
    int seg = CLAMP((int)ceilf((a1 - a0) * MAX(r1, 8.f) / 6.f), 1, 256);
    draw_reserve((seg + 1) * 2, seg * 6);
    int base = g_nv;
    for (int i = 0; i <= seg; i++) {
        f32 a = a0 + (a1 - a0) * (f32)i / (f32)seg, dx = cosf(a), dy = sinf(a);
        dv(cx + dx * r0, cy + dy * r0, c, WU, WU);
        dv(cx + dx * r1, cy + dy * r1, c, WU, WU);
    }
    for (int i = 0; i < seg; i++) di4(base + 2 * i, base + 2 * i + 1, base + 2 * i + 3, base + 2 * i + 2);
}

static void draw_glyph(const Glyph *g, f32 x, f32 y, Color c) {
    draw_reserve(4, 6);
    f32 u0 = g->x / (f32)ATLAS_SIZE, v0 = g->y / (f32)ATLAS_SIZE;
    f32 u1 = (g->x + g->w) / (f32)ATLAS_SIZE, v1 = (g->y + g->h) / (f32)ATLAS_SIZE;
    int a = dv(x, y, c, u0, v0), b = dv(x + g->w, y, c, u1, v0);
    int d = dv(x + g->w, y + g->h, c, u1, v1), e = dv(x, y + g->h, c, u0, v1);
    di4(a, b, d, e);
}

// Draw text with its line box top at y. Returns pen x after the text.
static f32 draw_text(Font *f, f32 x, f32 y, const char *s, int len, Color c) {
    f32 bl = roundf(y + f->ascent);
    f32 pen = roundf(x);
    if (text_complex(s, len)) {
        f32 w;
        int n = text_shape(f, s, len, &w);
        for (int i = 0; i < n; i++) {
            ShapedGlyph *sg = &g_shaped[i];
            Glyph *g = font_glyph_id(f, sg->fi, sg->gid);
            if (g->w) draw_glyph(g, roundf(pen + sg->x) + g->bx, bl - g->by - roundf(sg->y), g->color ? (Color){1, 1, 1, c.a} : c);
        }
        return pen + w;
    }
    const char *p = s, *end = s + len;
    while (p < end) {
        Glyph *g = font_glyph(f, utf8_decode(&p, end));
        if (g->w) draw_glyph(g, roundf(pen) + g->bx, bl - g->by, g->color ? (Color){1, 1, 1, c.a} : c); // emoji untinted
        pen += g->adv;
    }
    return pen;
}

#define ELLIPSIS "\xE2\x80\xA6"

// Draw text fitting maxw, eliding with "...". align: 0 left, 1 right, 2 centre.
static void draw_text_fit(Font *f, f32 x, f32 y, f32 maxw, const char *s, int len, Color c, int align) {
    if (maxw <= 0) return;
    f32 w = text_width(f, s, len);
    if (w <= maxw) {
        draw_text(f, align == 2 ? roundf(x + (maxw - w) * 0.5f) : align ? x + maxw - w : x, y, s, len, c);
        return;
    }
    f32 ell = text_width(f, ELLIPSIS, 3), avail = maxw - ell, acc = 0;
    if (avail <= 0) return;
    if (text_complex(s, len) && len < 4096) {
        // Shaped widths don't add up per character: find the longest prefix that fits by bisection, and lay it
        // out together with the ellipsis so it lands on the correct side of right-to-left text.
        int lo = 0, hi = len;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            while (mid > lo && ((u8)s[mid] & 0xC0) == 0x80) mid--; // character boundary
            if (mid == lo) { // no boundary in (lo, hi]: step to the next one
                mid = lo + 1;
                while (mid < hi && ((u8)s[mid] & 0xC0) == 0x80) mid++;
            }
            if (text_width(f, s, mid) <= avail) lo = mid;
            else hi = mid - 1;
        }
        while (lo > 0 && ((u8)s[lo] & 0xC0) == 0x80) lo--;
        char buf[4096 + 4];
        memcpy(buf, s, (size_t)lo);
        memcpy(buf + lo, ELLIPSIS, 3);
        draw_text(f, x, y, buf, lo + 3, c);
        return;
    }
    const char *p = s, *end = s + len;
    while (p < end) {
        const char *q = p;
        f32 a = font_glyph(f, utf8_decode(&q, end))->adv;
        if (acc + a > avail) break;
        acc += a;
        p = q;
    }
    f32 ex = draw_text(f, x, y, s, (int)(p - s), c);
    draw_text(f, ex, y, ELLIPSIS, 3, c);
}

// Vertically center a line of text in a box of height h starting at y.
static inline f32 text_y(Font *f, f32 y, f32 h) { return roundf(y + (h - (f->ascent + f->descent)) * 0.5f); }
