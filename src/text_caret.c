// Time-based caret motion and blink; independent of rendering and input APIs.
typedef struct {
    u64 moved, seen, input;
    f32 from, target;
    int caret, anchor;
    bool valid;
} TextCaret;

static f32 text_caret_position(const TextCaret *s, u64 now) {
    f32 t = CLAMP((f32)((now - s->moved) / 1000000.0) / 90.f, 0.f, 1.f);
    t = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
    return s->from + (s->target - s->from) * t;
}

static f32 text_caret_animate(TextCaret *s, f32 target, int caret, int anchor, u64 now, bool snap, f32 *alpha) {
    bool fresh = !s->valid || now - s->seen > 250000000ull;
    if (fresh || caret != s->caret || anchor != s->anchor || fabsf(target - s->target) > 0.1f) {
        s->from = fresh || snap ? target : text_caret_position(s, now);
        s->target = target; s->caret = caret; s->anchor = anchor;
        s->moved = now; s->valid = true;
    }
    s->seen = now;
    // Stay solid while typing; at rest breathe through a slow, soft blink.
    f32 elapsed = (f32)((now - MAX(s->moved, s->input)) / 1000000.0);
    *alpha = elapsed < 650.f || snap ? 1.f : 0.525f + 0.475f * cosf((elapsed - 650.f) * (2.f * (f32)M_PI / 1900.f));
    return snap ? target : text_caret_position(s, now);
}
