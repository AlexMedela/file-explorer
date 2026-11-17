// svg_preview.c - nanosvg (compiled here, also used by icons.c) and SVG files drawn for previews and thumbnails.
// Runs on the preview / thumbnail workers: each call has its own parser and rasterizer (nanosvg keeps no globals).

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#define NANOSVG_IMPLEMENTATION
#include "../third_party/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../third_party/nanosvgrast.h"
#pragma GCC diagnostic pop

// RGBA of an .svg file, its longest side max_px (<= 4096); src_w/src_h: its own size. NULL if it isn't a usable SVG
// (over 8 MB, no <svg, empty or absurd size). Caller frees.
static u8 *svg_preview_pixels(const char *path, int max_px, int *w, int *h, int *src_w, int *src_h) {
    if (max_px < 1 || max_px > 4096) return NULL;
    FILE *f = fopen(path, "re");
    if (!f) return NULL;
    struct stat st;
    if (fstat(fileno(f), &st) || !S_ISREG(st.st_mode) || st.st_size < 1 || st.st_size > (8 << 20)) {
        fclose(f);
        return NULL;
    }
    size_t len = (size_t)st.st_size;
    char *text = xmalloc(len + 1);
    size_t got = fread(text, 1, len, f);
    fclose(f);
    text[got] = 0;
    if (got != len || !strstr(text, "<svg")) { free(text); return NULL; }
    NSVGimage *img = nsvgParse(text, "px", 96.f); // (modifies text)
    free(text);
    if (!img) return NULL;
    // width="1e999" parses as infinity: the scale below would be 0 and the rasterizer would get NaNs
    if (!isfinite(img->width) || !isfinite(img->height) || img->width <= 0 || img->height <= 0 ||
        img->width > 10000000 || img->height > 10000000) { nsvgDelete(img); return NULL; }
    float scale = (float)max_px / MAX(img->width, img->height);
    *w = MAX(1, (int)ceilf(img->width * scale));
    *h = MAX(1, (int)ceilf(img->height * scale));
    *src_w = MAX(1, (int)roundf(img->width));
    *src_h = MAX(1, (int)roundf(img->height));
    NSVGrasterizer *rast = nsvgCreateRasterizer();
    if (!rast) { nsvgDelete(img); return NULL; }
    u8 *pixels = xcalloc((size_t)*w * *h, 4);
    nsvgRasterize(rast, img, 0, 0, scale, pixels, *w, *h, *w * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);
    return pixels;
}
