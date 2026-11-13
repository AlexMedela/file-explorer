// SVG/PNG desktop icons fitted into the same RGBA boxes as bundled icons.
static char *icon_read_svg(const char *path);
static u8 *system_icon_raster_path(const char *path, int w, int h) {
    const char *ext = strrchr(path, '.');
    if (ext && !strcasecmp(ext, ".svg")) {
        char *src = icon_read_svg(path);
        if (!src) return NULL;
        // NanoSVG does not inherit CSS currentColor. Give symbolic icons the UI
        // foreground instead of black, which is invisible on a dark surface.
        char *p = src;
        while ((p = strstr(p, "currentColor"))) {
            char col[8];
            snprintf(col, sizeof col, "#%06X", COL_TEXT2);
            memcpy(p, col, 7);
            memmove(p + 7, p + 12, strlen(p + 12) + 1);
            p += 7;
        }
        NSVGimage *img = nsvgParse(src, "px", 96.f);
        free(src);
        if (!img || img->width <= 0 || img->height <= 0) { if (img) nsvgDelete(img); return NULL; }
        if (!g_rast) g_rast = nsvgCreateRasterizer();
        f32 scale = MIN((f32)w / img->width, (f32)h / img->height);
        u8 *out = xcalloc((size_t)w * h, 4);
        nsvgRasterize(g_rast, img, (w - img->width * scale) * 0.5f, (h - img->height * scale) * 0.5f,
                      scale, out, w, h, w * 4);
        nsvgDelete(img);
        return out;
    }
    int iw, ih, channels;
    u8 *src = stbi_load(path, &iw, &ih, &channels, 4);
    if (!src || iw <= 0 || ih <= 0) { stbi_image_free(src); return NULL; }
    f32 scale = MIN((f32)w / iw, (f32)h / ih);
    int dw = MAX(1, (int)roundf(iw * scale)), dh = MAX(1, (int)roundf(ih * scale));
    dw = MIN(dw, w); dh = MIN(dh, h);
    u8 *out = xcalloc((size_t)w * h, 4);
    for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++) {
        // Bilinear interpolation preserves smooth edges at fractional UI scales.
        f32 fx = (x + 0.5f) * iw / dw - 0.5f, fy = (y + 0.5f) * ih / dh - 0.5f;
        int x0 = CLAMP((int)floorf(fx), 0, iw - 1), y0 = CLAMP((int)floorf(fy), 0, ih - 1);
        int x1 = MIN(x0 + 1, iw - 1), y1 = MIN(y0 + 1, ih - 1);
        f32 tx = CLAMP(fx - x0, 0.f, 1.f), ty = CLAMP(fy - y0, 0.f, 1.f);
        u8 *dst = out + ((size_t)(y + (h - dh) / 2) * w + x + (w - dw) / 2) * 4;
        const u8 *samples[] = {src + ((size_t)y0 * iw + x0) * 4, src + ((size_t)y0 * iw + x1) * 4,
                               src + ((size_t)y1 * iw + x0) * 4, src + ((size_t)y1 * iw + x1) * 4};
        f32 weights[] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
        f32 alpha = 0, rgb[3] = {0};
        // Interpolate premultiplied colors, then unpremultiply for SDL's atlas.
        // Transparent pixels must not introduce dark fringes around the icon.
        for (int i = 0; i < 4; i++) {
            f32 coverage = samples[i][3] * weights[i];
            alpha += coverage;
            for (int c = 0; c < 3; c++) rgb[c] += samples[i][c] * coverage;
        }
        dst[3] = (u8)lroundf(alpha);
        for (int c = 0; c < 3; c++) dst[c] = alpha > 0 ? (u8)lroundf(rgb[c] / alpha) : 0;
    }
    stbi_image_free(src);
    return out;
}

// Window actions commonly have symbolic-only assets. Normalize their RGB to
// white so the renderer can tint the shape for normal/hover/close-button states.
static u8 *system_action_raster(const char *theme, const char *name, int w, int h, bool monochrome) {
    char path[PATH_MAX], symbolic[96];
    bool found = system_icon_lookup(theme, name, MAX(w, h), path, sizeof path);
    if (!found && monochrome) {
        snprintf(symbolic, sizeof symbolic, "%s-symbolic", name);
        found = system_icon_lookup(theme, symbolic, MAX(w, h), path, sizeof path);
    }
    if (!found) return NULL;
    u8 *pixels = system_icon_raster_path(path, w, h);
    if (pixels && monochrome)
        for (size_t i = 0; i < (size_t)w * h; i++) pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = 255;
    return pixels;
}
