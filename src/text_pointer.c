// Pointer input shared by every single-line editor: click places the caret, drag selects, Shift-click extends,
// double-click picks a word, triple-click everything. Each frame's te_draw records where its field is, so padding,
// scrolling, font and scale agree with the visible caret.
typedef struct { TextEdit *edit; Font *font; Rect rect, hit; f32 pad; int layer; bool masked, focused; } TextFieldHit;
static TextFieldHit g_text_fields[32], g_text_drag;
static int g_text_field_count;
static bool g_text_selecting;

static int te_pointer_layer(void) { return sheet_open() ? 2 : A.pal_open ? 1 : 0; }

// focused = has the keyboard; in a sheet, sheet_text_focus decides (and moves the keyboard to a clicked field it
// knows). The hit area stops at the current clip (scrolled pages).
static void te_register_ex(TextEdit *edit, Font *font, Rect rect, f32 pad, bool masked, bool focused) {
    if (g_text_field_count >= (int)ARRAY_COUNT(g_text_fields)) return;
    Rect hit = rect;
    if (g_clip_on) {
        f32 x0 = MAX(hit.x, (f32)g_clip_cur.x), y0 = MAX(hit.y, (f32)g_clip_cur.y);
        f32 x1 = MIN(hit.x + hit.w, (f32)(g_clip_cur.x + g_clip_cur.w)), y1 = MIN(hit.y + hit.h, (f32)(g_clip_cur.y + g_clip_cur.h));
        hit = (Rect){x0, y0, MAX(0.f, x1 - x0), MAX(0.f, y1 - y0)};
    }
    g_text_fields[g_text_field_count++] = (TextFieldHit){edit, font, rect, hit, pad, te_pointer_layer(), masked, focused};
}

static void te_register(TextEdit *edit, Font *font, Rect rect, f32 pad) { te_register_ex(edit, font, rect, pad, false, true); }

// Byte offset of the caret position nearest to x.
static int te_hit(const TextFieldHit *field, f32 x) {
    TextEdit *t = field->edit;
    int n = (int)strlen(t->buf), best = 0;
    f32 at = x - field->rect.x - field->pad + t->scroll, dist = INFINITY;
    if (field->masked) { // one dot per character, the caret in the gaps (as te_draw_masked draws them)
        f32 step = roundf(S(3)) * 2 + S(4);
        for (int off = 0, k = 0;; k++) {
            f32 d = fabsf(k * step - S(2) - at);
            if (d < dist) { dist = d; best = off; }
            if (off >= n) break;
            off = te_next(t, off);
        }
        return best;
    }
    bool complex = text_complex(t->buf, n);
    f32 width = 0;
    int ng = complex ? text_shape(field->font, t->buf, n, &width) : 0;
    f32 pos = 0;
    for (int off = 0, k = 0;; k++) {
        f32 cx = pos;
        if (complex && g_sh_n) { // the same edges text_caret_x uses (right-to-left runs flip them)
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
        if (field->layer != te_pointer_layer() || !in_rect(field->hit, b->x, b->y)) continue;
        TextEdit *t = field->edit;
        // A queued key event may have closed one of these editors since drawing.
        if ((t == &A.edit && !A.edit_pane) || (t == &A.ren_te && !A.ren_pane) || (t == &A.search_te && !A.search_pane) ||
            (t == &A.qa_te && A.qa_edit_g < 0))
            continue;
        if (sheet_open() ? !sheet_text_focus(t, field->focused) : !field->focused) continue;
        int at = te_hit(field, b->x), n = (int)strlen(t->buf);
        if (b->clicks >= 3 || (b->clicks == 2 && field->masked)) { t->anchor = 0; t->caret = n; } // (a password: no words to show)
        else if (b->clicks == 2) {
            int lo = at == n && n ? te_prev(t, n) : at, hi = lo;
            if (lo < n) {
                bool sep = te_sep(t->buf[lo]);
                while (lo > 0 && te_sep(t->buf[te_prev(t, lo)]) == sep) lo = te_prev(t, lo);
                while (hi < n && te_sep(t->buf[hi]) == sep) hi = te_next(t, hi);
            }
            t->anchor = lo;
            t->caret = hi;
        } else {
            if (!(SDL_GetModState() & SDL_KMOD_SHIFT)) t->anchor = at;
            t->caret = at;
        }
        g_text_drag = *field;
        g_text_selecting = b->clicks == 1;
        SDL_CaptureMouse(g_text_selecting);
        return true;
    }
    return false;
}

static bool te_pointer_motion(SDL_MouseMotionEvent *m) {
    if (!g_text_selecting) return false;
    if (g_text_drag.layer != te_pointer_layer()) { g_text_selecting = false; SDL_CaptureMouse(false); return false; }
    // Keep geometry current after resizing or horizontally scrolling the field.
    bool found = false;
    for (int i = 0; i < g_text_field_count; i++)
        if (g_text_fields[i].edit == g_text_drag.edit) { g_text_drag = g_text_fields[i]; found = true; break; }
    if (!found) { g_text_selecting = false; SDL_CaptureMouse(false); return false; }
    g_text_drag.edit->caret = te_hit(&g_text_drag, m->x);
    return true;
}

static bool te_pointer_up(void) {
    if (!g_text_selecting) return false;
    g_text_selecting = false;
    SDL_CaptureMouse(false);
    return true;
}
