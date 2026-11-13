// cc -std=gnu11 -Wno-unused-function tools/system-icons-selftest.c -o /tmp/fe-system-icons-test -lm
#include "../src/base.h"
#include <assert.h>
#include <ftw.h>
#include "../src/desktop_config.c"
#include "../src/system_icons.c"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#define NANOSVG_IMPLEMENTATION
#include "../third_party/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../third_party/nanosvgrast.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"
#pragma GCC diagnostic pop
#define COL_TEXT2 0xABCDEFu
static NSVGrasterizer *g_rast;
static char *icon_read_svg(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *out = xmalloc((size_t)n + 1);
    size_t read = fread(out, 1, (size_t)n, f);
    out[read] = 0;
    fclose(f);
    return out;
}
#include "../src/system_icon_raster.c"

static void make_dir(const char *path) {
    char copy[PATH_MAX];
    snprintf(copy, sizeof copy, "%s", path);
    for (char *p = copy + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(copy, 0700); *p = '/'; }
    assert(!mkdir(copy, 0700) || errno == EEXIST);
}
static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(text, f);
    assert(!fclose(f));
}
static int remove_fixture(const char *path, const struct stat *st, int kind, struct FTW *info) {
    (void)st; (void)info;
    return kind == FTW_DP ? rmdir(path) : unlink(path);
}
int main(void) {
    char temp[] = "/tmp/fe-system-icons-XXXXXX", path[PATH_MAX], root[PATH_MAX];
    assert(mkdtemp(temp));
    snprintf(root, sizeof root, "%s/data", temp);
    setenv("XDG_DATA_HOME", root, 1);
    setenv("XDG_DATA_DIRS", "/nonexistent", 1);
    setenv("XDG_CONFIG_HOME", temp, 1);
    setenv("HOME", temp, 1);
    setenv("XDG_CURRENT_DESKTOP", "KDE", 1);
    snprintf(path, sizeof path, "%s/kdeglobals", temp);
    write_file(path, "[Icons]\nTheme=Reversal\n");
    const char *themes[] = {"Reversal", "Parent", "hicolor"};
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/icons/%s/16x16/places", root, themes[i]); make_dir(path);
        snprintf(path, sizeof path, "%s/icons/%s/scalable/places", root, themes[i]); make_dir(path);
        snprintf(path, sizeof path, "%s/icons/%s/index.theme", root, themes[i]);
        write_file(path, i == 0 ?
            "[Icon Theme]\nName=Reversal\nDirectories=16x16/places,scalable/places\nInherits=Parent\n"
            "[16x16/places]\nSize=16\nType=Fixed\n[scalable/places]\nSize=48\nType=Scalable\nMinSize=24\nMaxSize=256\n" :
            "[Icon Theme]\nName=Parent\nDirectories=16x16/places\nInherits=Reversal\n[16x16/places]\nSize=16\nType=Fixed\n");
    }
    const char *svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\"><rect width=\"16\" height=\"16\" fill=\"currentColor\"/></svg>";
    snprintf(path, sizeof path, "%s/icons/Reversal/16x16/places/folder.svg", root); write_file(path, svg);
    snprintf(path, sizeof path, "%s/icons/Reversal/scalable/places/folder.svg", root); write_file(path, svg);
    snprintf(path, sizeof path, "%s/icons/Parent/16x16/places/inherited.svg", root); write_file(path, svg);
    snprintf(path, sizeof path, "%s/icons/hicolor/16x16/places/fallback.svg", root); write_file(path, svg);
    snprintf(path, sizeof path, "%s/icons/Reversal/16x16/places/window-close-symbolic.svg", root); write_file(path, svg);
    system_icons_scan();
    assert(system_icon_theme_find("Reversal") >= 0);
    assert(!strcmp(g_system_icon_current, "Reversal"));
    assert(system_icon_lookup("Reversal", "folder", 16, path, sizeof path));
    assert(strstr(path, "/16x16/"));
    assert(system_icon_lookup("Reversal", "folder", 64, path, sizeof path));
    assert(strstr(path, "/scalable/"));
    u8 *pixels = system_icon_raster_path(path, 32, 32);
    assert(pixels);
    int center = (16 * 32 + 16) * 4;
    assert(pixels[center] == 0xAB && pixels[center + 1] == 0xCD && pixels[center + 2] == 0xEF && pixels[center + 3] == 255);
    free(pixels);
    pixels = system_action_raster("Reversal", "window-close", 16, 16, true);
    assert(pixels && pixels[(8 * 16 + 8) * 4] == 255 && pixels[(8 * 16 + 8) * 4 + 3] == 255);
    free(pixels);
    assert(!system_action_raster("Reversal", "missing-window-action", 16, 16, true));
    { // PNG files are decoded and fitted too, not just listed in the selector.
        static const unsigned char png[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,1,8,6,0,0,0,244,34,127,138,0,0,0,14,73,68,65,84,120,156,99,248,207,192,240,31,4,1,16,248,3,253,78,149,193,111,0,0,0,0,73,69,78,68,174,66,96,130};
        snprintf(path, sizeof path, "%s/icons/Reversal/16x16/places/bitmap.png", root);
        FILE *f = fopen(path, "wb"); assert(f);
        assert(fwrite(png, 1, sizeof png, f) == sizeof png); fclose(f);
        assert(system_icon_lookup("Reversal", "bitmap", 16, path, sizeof path));
        pixels = system_icon_raster_path(path, 2, 1);
        assert(pixels && pixels[0] == 255 && pixels[1] == 0 && pixels[4] == 0 && pixels[5] == 255);
        free(pixels);
    }
    { // Alpha-aware scaling preserves edge color next to transparent pixels.
        static const unsigned char png[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,1,8,6,0,0,0,244,34,127,138,0,0,0,17,73,68,65,84,120,156,99,248,207,192,240,159,129,129,129,1,0,12,252,1,255,253,45,119,109,0,0,0,0,73,69,78,68,174,66,96,130};
        snprintf(path, sizeof path, "%s/icons/Reversal/16x16/places/transparent.png", root);
        FILE *f = fopen(path, "wb"); assert(f);
        assert(fwrite(png, 1, sizeof png, f) == sizeof png); fclose(f);
        pixels = system_icon_raster_path(path, 4, 2);
        assert(pixels && pixels[4] == 255 && pixels[7] == 191);
        free(pixels);
    }
    assert(system_icon_lookup("Reversal", "inherited", 16, path, sizeof path) && strstr(path, "/Parent/"));
    assert(system_icon_lookup("Reversal", "fallback", 16, path, sizeof path) && strstr(path, "/hicolor/"));
    assert(!system_icon_lookup("Reversal", "missing", 16, path, sizeof path)); // cyclic inheritance terminates
    assert(!system_icon_lookup("Reversal", "../outside", 16, path, sizeof path));
    assert(!system_icon_relative("../../outside"));
    assert(!system_icon_relative("/absolute"));
    // Local user themes override the same theme distributed under XDG_DATA_DIRS.
    char second[PATH_MAX];
    snprintf(second, sizeof second, "%s/second/icons/Reversal/16x16/places", temp); make_dir(second);
    snprintf(second, sizeof second, "%s/second", temp); setenv("XDG_DATA_DIRS", second, 1);
    snprintf(second, sizeof second, "%s/second/icons/Reversal/index.theme", temp);
    write_file(second, "[Icon Theme]\nName=Reversal\nDirectories=16x16/places\n[16x16/places]\nSize=16\n");
    system_icons_scan();
    assert(system_icon_lookup("Reversal", "folder", 16, path, sizeof path) && strstr(path, "/data/icons/"));
    assert(!nftw(temp, remove_fixture, 16, FTW_DEPTH | FTW_PHYS));
    puts("System icon checks passed: discovery, current theme, SVG/PNG raster/color, symbolic window actions, size selection, inheritance, cycle/missing fallback, user overrides.");
    return 0;
}
