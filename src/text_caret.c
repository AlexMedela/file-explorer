// Time-based caret motion and blink; independent of rendering and input APIs.
// The caret glides to a new position, stays solid while typing, then breathes softly for a while and settles solid,
// so an idle window stops drawing frames (text_caret_due).
#define CARET_GLIDE_MS 90.f
#define CARET_SOLID_MS 650.f   // after the last key or move, before it starts breathing
#define CARET_PULSE_MS 1900.f  // one breath
#define CARET_REST_MS 10000.f  // then solid again until the next key or move (like GTK's cursor-blink-timeout)

typedef struct {
    u64 moved, input;
    f32 from, target;
    int caret, anchor;
    bool valid;
} TextCaret;

static f32 text_caret_position(const TextCaret *s, u64 now) {
    f32 t = CLAMP((f32)((now - s->moved) / 1000000.0) / CARET_GLIDE_MS, 0.f, 1.f);
    t = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
    return s->from + (s->target - s->from) * t;
}

static f32 text_caret_idle_ms(const TextCaret *s, u64 now) { return (f32)((now - MAX(s->moved, s->input)) / 1000000.0); }

// x of the caret to draw now (target = where it belongs) and its opacity; snap: no motion, no breathing.
static f32 text_caret_animate(TextCaret *s, f32 target, int caret, int anchor, u64 now, bool snap, f32 *alpha) {
    if (!s->valid || caret != s->caret || anchor != s->anchor || fabsf(target - s->target) > 0.1f) {
        s->from = !s->valid || snap ? target : text_caret_position(s, now);
        s->target = target;
        s->caret = caret;
        s->anchor = anchor;
        s->moved = now;
        s->valid = true;
    }
    f32 idle = text_caret_idle_ms(s, now);
    *alpha = snap || idle < CARET_SOLID_MS || idle >= CARET_REST_MS ? 1.f
             : 0.65f + 0.35f * cosf((idle - CARET_SOLID_MS) * (2.f * (f32)M_PI / CARET_PULSE_MS));
    return snap ? target : text_caret_position(s, now);
}

// When the caret needs its next frame (ns, 0 = never: it rests until something changes).
static u64 text_caret_due(const TextCaret *s, u64 now) {
    if (!s->valid) return 0;
    f32 since = (f32)((now - s->moved) / 1000000.0), idle = text_caret_idle_ms(s, now);
    if (since < CARET_GLIDE_MS) return now + 16000000ull;
    if (idle < CARET_SOLID_MS) return now + (u64)((CARET_SOLID_MS - idle) * 1e6f);
    if (idle < CARET_REST_MS) return now + 33000000ull;
    return 0;
}

// The earliest frame a visible caret asked for this frame (te_draw); the main loop waits at most that long.
static u64 g_caret_due;

static int caret_wait_ms(void) {
    if (!g_caret_due) return 0;
    u64 now = now_ns();
    return g_caret_due > now ? (int)((g_caret_due - now) / 1000000ull) + 1 : 1;
}
