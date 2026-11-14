// cc -std=gnu11 -Wno-unused-function tools/svg-preview-selftest.c -o /tmp/fe-svg-test -lm -pthread
#include "../src/base.h"
#include <assert.h>
#include "../src/svg_preview.c"
static void put(const char *path, const char *data) {
    FILE *f = fopen(path, "w"); assert(f); fputs(data, f); fclose(f);
}
int main(void) {
    char path[] = "/tmp/fe-svg-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    put(path, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\"><rect width=\"100\" height=\"100\" fill=\"#ff0000\"/></svg>");
    int w, h, sw, sh;
    u8 *p = svg_preview_pixels(path, 64, &w, &h, &sw, &sh);
    assert(p && w == 64 && h == 32 && sw == 200 && sh == 100);
    size_t red = ((size_t)16 * w + 16) * 4, clear = ((size_t)16 * w + 48) * 4;
    assert(p[red] > 240 && p[red + 3] > 240 && !p[clear + 3]); free(p);
    put(path, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 20\"><rect width=\"10\" height=\"20\" fill=\"blue\"/></svg>");
    p = svg_preview_pixels(path, 64, &w, &h, &sw, &sh); assert(p && w == 32 && h == 64); free(p);
    put(path, "not an svg"); assert(!svg_preview_pixels(path, 64, &w, &h, &sw, &sh));
    put(path, "<svg width=\"0\" height=\"0\"></svg>"); assert(!svg_preview_pixels(path, 64, &w, &h, &sw, &sh));
    assert(!svg_preview_pixels(path, 50000, &w, &h, &sw, &sh));
    unlink(path); puts("SVG previews: passed");
}
