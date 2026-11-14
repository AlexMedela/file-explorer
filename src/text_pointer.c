// Pointer input shared by every focused single-line editor. Geometry comes from
// te_draw, so padding, scrolling, font and scale agree with the visible caret.
typedef struct { TextEdit *edit; Font *font; Rect rect; f32 pad; int layer; } TextFieldHit;
static TextFieldHit g_text_fields[32], g_text_drag;
static int g_text_field_count;
static bool g_text_selecting;

static int te_pointer_layer(void) { return sheet_open() ? 2 : A.pal_open ? 1 : 0; }

static void te_register(TextEdit *edit, Font *font, Rect rect, f32 pad) {
    if (g_text_field_count < (int)ARRAY_COUNT(g_text_fields))
        g_text_fields[g_text_field_count++] = (TextFieldHit){edit, font, rect, pad, te_pointer_layer()};
}

static int te_hit(const TextFieldHit *field, f32 x) {
    TextEdit *t = field->edit;
    int n = (int)strlen(t->buf), best = 0;
    f32 at = x - field->rect.x - field->pad + t->scroll, dist = INFINITY;
    bool complex = text_complex(t->buf, n);
    f32 width = 0; int ng = complex ? text_shape(field->font, t->buf, n, &width) : 0;
    f32 pos = 0;
    for (int off = 0, k = 0;; k++) {
        f32 cx = pos;
        if (complex && g_sh_n) {
            f32 a, b;
            bool rtl = shaped_span(ng, MIN(k, g_sh_n - 1), &a, &b);
            cx = k >= g_sh_n ? (rtl ? a : b) : (rtl ? b : a);
        }
        if (fabsf(cx - at) < dist) { dist = fabsf(cx - at); best = off; }
        if (off >= n) break;
        const char *p = t->buf + off;
        u32 cp = utf8_decode(&p, t->buf + n);
        if (!complex) pos += font_glyph(field->font, cp)->adv;
        off = (int)(p - t->buf);
    }
    return best;
}

static bool te_pointer_down(SDL_MouseButtonEvent *b) {
    if (b->button != SDL_BUTTON_LEFT) return false;
    for (int i = g_text_field_count - 1; i >= 0; i--) {
        TextFieldHit *field = &g_text_fields[i];
        if (field->layer != te_pointer_layer() || !in_rect(field->rect, b->x, b->y)) continue;
        TextEdit *t = field->edit;
        // A queued key event may have closed one of these editors since drawing.
        if ((t == &A.edit && !A.edit_pane) || (t == &A.ren_te && !A.ren_pane) ||
            (t == &A.search_te && !A.search_pane)) continue;
        t->animation.input = now_ns();
        int at = te_hit(field, b->x), n = (int)strlen(t->buf);
        if (b->clicks >= 3) { t->anchor = 0; t->caret = n; }
        else if (b->clicks == 2) {
            int lo = at == n && n ? te_prev(t, n) : at, hi = lo;
            if (lo < n) {
                bool sep = te_sep(t->buf[lo]);
                while (lo > 0 && te_sep(t->buf[te_prev(t, lo)]) == sep) lo = te_prev(t, lo);
                while (hi < n && te_sep(t->buf[hi]) == sep) hi = te_next(t, hi);
            }
            t->anchor = lo; t->caret = hi;
        } else {
            if (!(SDL_GetModState() & SDL_KMOD_SHIFT)) t->anchor = at;
            t->caret = at;
        }
        g_text_drag = *field; g_text_selecting = b->clicks == 1;
        SDL_CaptureMouse(g_text_selecting);
        return true;
    }
    return false;
}

static bool te_pointer_motion(SDL_MouseMotionEvent *m) {
    if (!g_text_selecting) return false;
    if (g_text_drag.layer != te_pointer_layer()) {
        g_text_selecting = false; SDL_CaptureMouse(false); return false;
    }
    // Keep geometry current after resizing or horizontally scrolling the field.
    bool found = false;
    for (int i = 0; i < g_text_field_count; i++) if (g_text_fields[i].edit == g_text_drag.edit) {
        g_text_drag = g_text_fields[i]; found = true; break;
    }
    if (!found) { g_text_selecting = false; SDL_CaptureMouse(false); return false; }
    g_text_drag.edit->caret = te_hit(&g_text_drag, m->x);
    return true;
}

static bool te_pointer_up(void) {
    if (!g_text_selecting) return false;
    g_text_selecting = false; SDL_CaptureMouse(false);
    return true;
}
