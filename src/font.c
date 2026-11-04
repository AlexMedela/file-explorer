// font.c - FreeType glyph rasterization into a shared GPU atlas, fontconfig fallback

static int g_atlas_size = 1024; // grows to 2048 when one frame needs more glyphs/icons than fit
#define ATLAS_SIZE g_atlas_size
#define FONT_MAX_FACES 8
#define GLYPH_EMPTY 0xFFFFFFFFu

typedef struct {
    u32 cp;
    u16 x, y, w, h;
    i16 bx, by;
    f32 adv;
    bool color; // colour bitmap (emoji): drawn untinted
} Glyph;

typedef struct {
    FT_Face faces[FONT_MAX_FACES];
    char *paths[FONT_MAX_FACES];
    f32 fscale[FONT_MAX_FACES]; // bitmap-only faces (colour emoji): strike -> pixel size scale
    int nfaces;
    int px;
    f32 ascent, descent, line_h;
    Glyph *tab;
    u32 tab_cap, tab_count;
} Font;

static FT_Library g_ft;
static const char *g_ui_families[] = {"Inter", "Segoe UI", "Noto Sans", "Open Sans", "Cantarell", "DejaVu Sans", "sans-serif"};
static u8 *g_atlas; // RGBA, white + coverage in alpha
static SDL_Texture *g_atlas_tex;
static int g_shelf_x, g_shelf_y, g_shelf_h;
static int g_dirty_x0 = 1 << 30, g_dirty_y0 = 1 << 30, g_dirty_x1, g_dirty_y1;
static SDL_Renderer *g_atlas_ren;
static bool g_atlas_full;
static Font *g_fonts[16]; // every font sharing the atlas (UI, small, brand, mono, terminal regular + bold)
static int g_icon_cache_n; // icons.c: rasterized icon slots, invalid after an atlas reset
static int g_nfonts;

static bool atlas_pack(int w, int h, int *ox, int *oy) {
    if (g_shelf_x + w + 1 > ATLAS_SIZE) {
        g_shelf_y += g_shelf_h + 1;
        g_shelf_x = 0;
        g_shelf_h = 0;
    }
    if (g_shelf_y + h + 1 > ATLAS_SIZE) {
        g_atlas_full = true;
        return false;
    }
    *ox = g_shelf_x;
    *oy = g_shelf_y;
    g_shelf_x += w + 1;
    g_shelf_h = MAX(g_shelf_h, h);
    g_dirty_x0 = MIN(g_dirty_x0, *ox);
    g_dirty_y0 = MIN(g_dirty_y0, *oy);
    g_dirty_x1 = MAX(g_dirty_x1, *ox + w);
    g_dirty_y1 = MAX(g_dirty_y1, *oy + h);
    return true;
}

static void font_clear_cache(Font *f) {
    for (u32 i = 0; i < f->tab_cap; i++) f->tab[i].cp = GLYPH_EMPTY;
    f->tab_count = 0;
}

// Reset atlas; the 4x4 white block at (0,0) is used for solid shapes.
static void atlas_reset(void) {
    memset(g_atlas, 0, (size_t)ATLAS_SIZE * ATLAS_SIZE * 4);
    g_shelf_x = g_shelf_y = g_shelf_h = 0;
    g_atlas_full = false;
    int x, y;
    atlas_pack(4, 4, &x, &y);
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) memset(&g_atlas[((size_t)j * ATLAS_SIZE + i) * 4], 255, 4);
    g_dirty_x0 = 0;
    g_dirty_y0 = 0;
    g_dirty_x1 = ATLAS_SIZE;
    g_dirty_y1 = ATLAS_SIZE;
    for (int i = 0; i < g_nfonts; i++) font_clear_cache(g_fonts[i]);
    g_icon_cache_n = 0;
}

static void atlas_upload(void) {
    if (g_dirty_x1 <= g_dirty_x0 || g_dirty_y1 <= g_dirty_y0) return;
    SDL_Rect r = {g_dirty_x0, g_dirty_y0, g_dirty_x1 - g_dirty_x0, g_dirty_y1 - g_dirty_y0};
    // Submit what's queued before changing the texture it samples: SDL 3.4's Vulkan renderer otherwise
    // drops geometry queued earlier in the frame (rows flickered away when new glyphs appeared mid-frame).
    SDL_FlushRenderer(g_atlas_ren);
    SDL_UpdateTexture(g_atlas_tex, &r, &g_atlas[((size_t)r.y * ATLAS_SIZE + r.x) * 4], ATLAS_SIZE * 4);
    g_dirty_x0 = g_dirty_y0 = ATLAS_SIZE;
    g_dirty_x1 = g_dirty_y1 = 0;
}

static bool atlas_init(SDL_Renderer *ren) {
    g_atlas_ren = ren;
    if (FT_Init_FreeType(&g_ft)) return false;
    FcInit();
    g_atlas = xmalloc((size_t)ATLAS_SIZE * ATLAS_SIZE * 4);
    g_atlas_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, ATLAS_SIZE, ATLAS_SIZE);
    if (!g_atlas_tex) return false;
    SDL_SetTextureBlendMode(g_atlas_tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(g_atlas_tex, SDL_SCALEMODE_NEAREST);
    atlas_reset();
    return true;
}

// Find a font file for the first family in the list that is actually installed.
static char *find_font_file(const char **families, int n) {
    for (int i = 0; i < n; i++) {
        FcPattern *pat = FcNameParse((const FcChar8 *)families[i]);
        FcConfigSubstitute(NULL, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);
        FcResult res;
        FcPattern *m = FcFontMatch(NULL, pat, &res);
        char *out = NULL;
        if (m) {
            FcChar8 *fam = NULL, *file = NULL;
            bool last = i == n - 1;
            if (FcPatternGetString(m, FC_FILE, 0, &file) == FcResultMatch &&
                (last || (FcPatternGetString(m, FC_FAMILY, 0, &fam) == FcResultMatch &&
                          str_ieq((const char *)fam, families[i])))) {
                out = strdup((const char *)file);
            }
            FcPatternDestroy(m);
        }
        FcPatternDestroy(pat);
        if (out) return out;
    }
    return NULL;
}

static void font_update_metrics(Font *f) {
    FT_Size_Metrics *m = &f->faces[0]->size->metrics;
    f->ascent = m->ascender / 64.f;
    f->descent = -m->descender / 64.f;
    f->line_h = m->height / 64.f;
}

static bool font_add_face(Font *f, const char *path, int index) {
    if (f->nfaces >= FONT_MAX_FACES) return false;
    FT_Face face;
    if (FT_New_Face(g_ft, path, index, &face)) return false;
    f32 sc = 1.f;
    if (!FT_IS_SCALABLE(face)) { // colour bitmap font (Noto Color Emoji): pick a strike, scale it at draw time
        if (!FT_HAS_COLOR(face) || !face->num_fixed_sizes || FT_Select_Size(face, 0)) {
            FT_Done_Face(face);
            return false;
        }
        sc = (f32)f->px * 1.15f / (f32)MAX(1, face->available_sizes[0].height);
    } else if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)f->px)) {
        FT_Done_Face(face);
        return false;
    }
    f->fscale[f->nfaces] = sc;
    f->faces[f->nfaces] = face;
    f->paths[f->nfaces] = strdup(path);
    f->nfaces++;
    return true;
}

static bool font_init(Font *f, const char *path, int px) {
    memset(f, 0, sizeof *f);
    f->px = px;
    if (!font_add_face(f, path, 0)) return false;
    font_update_metrics(f);
    f->tab_cap = 1024;
    f->tab = xmalloc(f->tab_cap * sizeof(Glyph));
    font_clear_cache(f);
    if (g_nfonts < (int)ARRAY_COUNT(g_fonts)) g_fonts[g_nfonts++] = f;
    return true;
}

static void font_set_px(Font *f, int px) {
    f->px = px;
    for (int i = 0; i < f->nfaces; i++) {
        if (FT_IS_SCALABLE(f->faces[i])) FT_Set_Pixel_Sizes(f->faces[i], 0, (FT_UInt)px);
        else f->fscale[i] = (f32)px * 1.15f / (f32)MAX(1, f->faces[i]->available_sizes[0].height);
    }
    font_update_metrics(f);
    font_clear_cache(f);
}

// Swap the primary face (font family change); fallback faces are reloaded on demand.
static bool font_replace(Font *f, const char *path) {
    FT_Face face;
    if (FT_New_Face(g_ft, path, 0, &face)) return false;
    if (!FT_IS_SCALABLE(face) || FT_Set_Pixel_Sizes(face, 0, (FT_UInt)f->px)) {
        FT_Done_Face(face);
        return false;
    }
    for (int i = 0; i < f->nfaces; i++) {
        FT_Done_Face(f->faces[i]);
        free(f->paths[i]);
    }
    f->faces[0] = face;
    f->paths[0] = strdup(path);
    f->nfaces = 1;
    font_update_metrics(f);
    font_clear_cache(f);
    return true;
}

// Exact family lookup (case-insensitive); NULL if that family isn't installed.
static char *font_family_file(const char *family) {
    FcPattern *pat = FcNameParse((const FcChar8 *)family);
    if (!pat) return NULL;
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult res;
    FcPattern *m = FcFontMatch(NULL, pat, &res);
    char *out = NULL;
    if (m) {
        FcChar8 *got = NULL, *file = NULL;
        if (FcPatternGetString(m, FC_FILE, 0, &file) == FcResultMatch &&
            FcPatternGetString(m, FC_FAMILY, 0, &got) == FcResultMatch && str_ieq((const char *)got, family))
            out = strdup((const char *)file);
        FcPatternDestroy(m);
    }
    FcPatternDestroy(pat);
    return out;
}

// The same family as the font file at path, at (about) the given fontconfig weight; NULL if it has none.
static char *font_weight_file(const char *path, int weight) {
    int count = 0;
    FcPattern *q = FcFreeTypeQuery((const FcChar8 *)path, 0, NULL, &count);
    FcChar8 *fam = NULL;
    if (!q || FcPatternGetString(q, FC_FAMILY, 0, &fam) != FcResultMatch) {
        if (q) FcPatternDestroy(q);
        return NULL;
    }
    FcPattern *pat = FcPatternCreate();
    FcPatternAddString(pat, FC_FAMILY, fam);
    FcPatternAddInteger(pat, FC_WEIGHT, weight);
    FcPatternAddInteger(pat, FC_SLANT, FC_SLANT_ROMAN);
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult res;
    FcPattern *m = FcFontMatch(NULL, pat, &res);
    char *out = NULL;
    if (m) {
        FcChar8 *got = NULL, *file = NULL;
        int w = 0;
        if (FcPatternGetString(m, FC_FILE, 0, &file) == FcResultMatch &&
            FcPatternGetString(m, FC_FAMILY, 0, &got) == FcResultMatch && str_ieq((const char *)got, (const char *)fam) &&
            FcPatternGetInteger(m, FC_WEIGHT, 0, &w) == FcResultMatch && w > FC_WEIGHT_MEDIUM)
            out = strdup((const char *)file);
        FcPatternDestroy(m);
    }
    FcPatternDestroy(pat);
    FcPatternDestroy(q);
    return out;
}

// Installed scalable font families, sorted, de-duplicated (caller frees with strv_free).
static int font_families(char ***out) {
    FcPattern *pat = FcPatternCreate();
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
    FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, (char *)0);
    FcFontSet *fs = FcFontList(NULL, pat, os);
    char **v = NULL;
    int n = 0;
    if (fs) {
        v = xmalloc((size_t)(fs->nfont + 1) * sizeof(char *));
        for (int i = 0; i < fs->nfont; i++) {
            FcChar8 *fam = NULL;
            if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, &fam) == FcResultMatch && fam[0] != '.')
                v[n++] = strdup((const char *)fam);
        }
        FcFontSetDestroy(fs);
    }
    FcObjectSetDestroy(os);
    FcPatternDestroy(pat);
    for (int i = 1; i < n; i++) // insertion sort is fine for a few hundred names
        for (int j = i; j > 0 && natcmp(v[j - 1], v[j]) > 0; j--) { char *t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (k && !strcmp(v[k - 1], v[i])) { free(v[i]); continue; }
        v[k++] = v[i];
    }
    *out = v;
    return k;
}

// Find (or load via fontconfig) a fallback face containing cp. Returns face index or -1.
static int font_fallback(Font *f, u32 cp) {
    for (int i = 1; i < f->nfaces; i++)
        if (FT_Get_Char_Index(f->faces[i], cp)) return i;
    if (f->nfaces >= FONT_MAX_FACES) return -1;
    int result = -1;
    FcPattern *pat = FcPatternCreate();
    FcCharSet *cs = FcCharSetCreate();
    FcCharSetAddChar(cs, cp);
    FcPatternAddCharSet(pat, FC_CHARSET, cs);
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult res;
    FcPattern *m = FcFontMatch(NULL, pat, &res);
    if (m) {
        FcChar8 *file = NULL;
        FcCharSet *mcs = NULL;
        int index = 0;
        FcPatternGetInteger(m, FC_INDEX, 0, &index);
        if (FcPatternGetCharSet(m, FC_CHARSET, 0, &mcs) == FcResultMatch && FcCharSetHasChar(mcs, cp) &&
            FcPatternGetString(m, FC_FILE, 0, &file) == FcResultMatch) {
            bool loaded = false;
            for (int i = 0; i < f->nfaces; i++)
                if (!strcmp(f->paths[i], (const char *)file)) loaded = true;
            if (!loaded && font_add_face(f, (const char *)file, index) &&
                FT_Get_Char_Index(f->faces[f->nfaces - 1], cp))
                result = f->nfaces - 1;
        }
        FcPatternDestroy(m);
    }
    FcCharSetDestroy(cs);
    FcPatternDestroy(pat);
    return result;
}

static void font_grow(Font *f) {
    Glyph *old = f->tab;
    u32 oldcap = f->tab_cap;
    f->tab_cap *= 2;
    f->tab = xmalloc(f->tab_cap * sizeof(Glyph));
    font_clear_cache(f);
    for (u32 i = 0; i < oldcap; i++) {
        if (old[i].cp == GLYPH_EMPTY) continue;
        u32 h = (old[i].cp * 2654435761u) & (f->tab_cap - 1);
        while (f->tab[h].cp != GLYPH_EMPTY) h = (h + 1) & (f->tab_cap - 1);
        f->tab[h] = old[i];
        f->tab_count++;
    }
    free(old);
}

static Glyph *font_glyph(Font *f, u32 cp) {
    if (cp < 0x20 || cp == 0x7F) cp = 0x2400 + (cp == 0x7F ? 0x21 : cp); // control chars in names: \u240A etc.
    if ((f->tab_count + 1) * 10 >= f->tab_cap * 7) font_grow(f);
    u32 h = (cp * 2654435761u) & (f->tab_cap - 1);
    while (f->tab[h].cp != GLYPH_EMPTY) {
        if (f->tab[h].cp == cp) return &f->tab[h];
        h = (h + 1) & (f->tab_cap - 1);
    }

    int fi = 0;
    FT_UInt gi = FT_Get_Char_Index(f->faces[0], cp);
    if (!gi && cp >= 32) {
        int k = font_fallback(f, cp);
        if (k > 0) {
            fi = k;
            gi = FT_Get_Char_Index(f->faces[k], cp);
        }
    }
    FT_Face face = f->faces[fi];
    Glyph g = {0};
    g.cp = cp;
    if (FT_HAS_COLOR(face) && FT_Load_Glyph(face, gi, FT_LOAD_COLOR | FT_LOAD_RENDER) == 0 &&
        face->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) { // colour emoji: box-filter the strike down
        FT_GlyphSlot sl = face->glyph;
        FT_Bitmap *bm = &sl->bitmap;
        f32 sc = MIN(f->fscale[fi], 1.f);
        int sw = (int)bm->width, sh = (int)bm->rows;
        int w = MAX(1, (int)lroundf(sw * sc)), hh = MAX(1, (int)lroundf(sh * sc)), ax, ay;
        g.color = true;
        g.adv = roundf(sl->advance.x / 64.f * sc);
        g.bx = (i16)lroundf(sl->bitmap_left * sc);
        g.by = (i16)lroundf(sl->bitmap_top * sc);
        if (!atlas_pack(w, hh, &ax, &ay)) {
            static Glyph tmpc;
            tmpc = g;
            return &tmpc;
        }
        for (int y = 0; y < hh; y++) {
            u8 *dst = &g_atlas[((size_t)(ay + y) * ATLAS_SIZE + ax) * 4];
            int y0 = y * sh / hh, y1 = MAX(y0 + 1, (y + 1) * sh / hh);
            for (int x = 0; x < w; x++) {
                int x0 = x * sw / w, x1 = MAX(x0 + 1, (x + 1) * sw / w);
                u32 acc[4] = {0}, n = 0;
                for (int yy = y0; yy < y1; yy++)
                    for (int xx = x0; xx < x1; xx++) {
                        const u8 *sp = bm->buffer + (ptrdiff_t)yy * bm->pitch + xx * 4; // premultiplied BGRA
                        acc[0] += sp[2]; acc[1] += sp[1]; acc[2] += sp[0]; acc[3] += sp[3];
                        n++;
                    }
                u32 a = acc[3] / n;
                for (int k = 0; k < 3; k++) dst[x * 4 + k] = a ? (u8)MIN(255u, acc[k] / n * 255 / a) : 0; // un-premultiply
                dst[x * 4 + 3] = (u8)a;
            }
        }
        g.x = (u16)ax;
        g.y = (u16)ay;
        g.w = (u16)w;
        g.h = (u16)hh;
    } else if (FT_Load_Glyph(face, gi, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) == 0) {
        FT_GlyphSlot s = face->glyph;
        FT_Bitmap *bm = &s->bitmap;
        g.adv = s->advance.x / 64.f;
        g.bx = (i16)s->bitmap_left;
        g.by = (i16)s->bitmap_top;
        int w = (int)bm->width, hh = (int)bm->rows, ax, ay;
        if (w && hh) {
            if (!atlas_pack(w, hh, &ax, &ay)) {
                static Glyph tmp; // atlas full: draw nothing this frame, retry after reset
                tmp = g;
                return &tmp;
            }
            for (int y = 0; y < hh; y++) {
                const u8 *row = bm->buffer + (ptrdiff_t)y * bm->pitch;
                u8 *dst = &g_atlas[((size_t)(ay + y) * ATLAS_SIZE + ax) * 4];
                for (int x = 0; x < w; x++) {
                    u8 a = bm->pixel_mode == FT_PIXEL_MODE_MONO ? ((row[x >> 3] >> (7 - (x & 7))) & 1) * 255 : row[x];
                    dst[x * 4 + 0] = 255;
                    dst[x * 4 + 1] = 255;
                    dst[x * 4 + 2] = 255;
                    dst[x * 4 + 3] = a;
                }
            }
            g.x = (u16)ax;
            g.y = (u16)ay;
            g.w = (u16)w;
            g.h = (u16)hh;
        }
    }
    f->tab[h] = g;
    f->tab_count++;
    return &f->tab[h];
}

static f32 text_width(Font *f, const char *s, int len) {
    const char *p = s, *end = s + len;
    f32 w = 0;
    while (p < end) w += font_glyph(f, utf8_decode(&p, end))->adv;
    return w;
}

// One frame's text + icons didn't fit: double the atlas (once; 2048^2 is plenty even at 4K).
static bool atlas_grow(void) {
    if (g_atlas_size >= 2048) return false;
    SDL_Texture *t = SDL_CreateTexture(g_atlas_ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 2048, 2048);
    if (!t) return false;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    SDL_DestroyTexture(g_atlas_tex);
    g_atlas_tex = t;
    g_atlas_size = 2048;
    free(g_atlas);
    g_atlas = xmalloc((size_t)g_atlas_size * g_atlas_size * 4);
    atlas_reset();
    return true;
}
