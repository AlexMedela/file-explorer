// cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -Wno-format-truncation tools/app-desktop-icon-selftest.c -o /tmp/fe-app-desktop-test
#include "../src/base.h"
#include <assert.h>
#include <ftw.h>
static int mkdir_p(const char *path, mode_t mode) {
    char tmp[PATH_MAX]; snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') {
        *p = 0; if (mkdir(tmp, mode) && errno != EEXIST) return -1; *p = '/';
    }
    return mkdir(tmp, mode) && errno != EEXIST ? -1 : 0;
}
#include "../src/app_desktop_icon.c"
static void put(const char *path, const char *text) {
    assert(app_desktop_write(path, text, strlen(text)) >= 0);
}
static char *get(const char *path) {
    size_t length;
    char *text = (char *)app_desktop_read(path, &length);
    assert(text); return text;
}
static char *icon_path(const char *entry) {
    const char *start = strstr(entry, "\nIcon=/"); assert(start); start += 6;
    const char *end = strchr(start, '\n'); assert(end);
    return strndup(start, (size_t)(end - start));
}
static int cleanup(const char *path, const struct stat *st, int type, struct FTW *ftw) {
    (void)st; (void)ftw;
    return type == FTW_DP ? rmdir(path) : unlink(path);
}
int main(void) {
    char temp[] = "/tmp/fe-desktop-icons-XXXXXX", data[PATH_MAX], installed[PATH_MAX], path[PATH_MAX];
    assert(mkdtemp(temp));
    snprintf(data, sizeof data, "%s/home with spaces/.local/share", temp);
    snprintf(installed, sizeof installed, "%s/system", temp);
    snprintf(path, sizeof path, "%s/applications", installed); assert(!mkdir_p(path, 0755));
    snprintf(path, sizeof path, "%s/applications/dev.fileexplorer.desktop", installed);
    const char *entry = "[Desktop Entry]\nName=File Explorer\nExec=file-explorer %F\nIcon=dev.fileexplorer\nStartupWMClass=dev.fileexplorer\n"
                        "[Desktop Action Other]\nExec=file-explorer --new-window\nIcon=unrelated-action\n";
    put(path, entry);
    setenv("XDG_DATA_DIRS", installed, 1);
    unsetenv("CONTAINER_ID"); unsetenv("DISTROBOX_HOST_HOME");
    AppDesktop desktop;
    assert(app_desktop_find(data, &desktop) && !strcmp(desktop.id, "dev.fileexplorer"));
    const char *svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
                      "<style>.a{fill:#3593e6}</style><rect class=\"a\" width=\"16\" height=\"16\"/></svg>";
    assert(app_desktop_install_icon(data, svg, strlen(svg), "svg") == APP_DESKTOP_UPDATED);
    assert(app_desktop_install_icon(data, svg, strlen(svg), "svg") == APP_DESKTOP_UNCHANGED);
    snprintf(path, sizeof path, "%s/applications/dev.fileexplorer.desktop", data);
    char *text = get(path), *image = icon_path(text), *art = get(image);
    assert(!strcmp(art, svg)); // preserve the complete original SVG; no raster flattening or tint
    assert(strstr(text, "Exec=file-explorer %F\n") && strstr(text, "Icon=unrelated-action\n"));
    free(art); free(text); free(image);

    // Reproduce the user's exact exported launcher and container identity.
    char home[PATH_MAX]; snprintf(home, sizeof home, "%s/host-home", temp);
    setenv("HOME", home, 1); setenv("DISTROBOX_HOST_HOME", home, 1); setenv("CONTAINER_ID", "fedora-build", 1);
    snprintf(path, sizeof path, "%s/.local/share/applications", home); assert(!mkdir_p(path, 0755));
    snprintf(path, sizeof path, "%s/.local/share/applications/fedora-build-dev.fileexplorer.desktop", home);
    const char *exported = "[Desktop Entry]\nName=File Explorer (on fedora-build)\n"
                           "Exec=/usr/bin/distrobox-enter  -n fedora-build  --   file-explorer  %F\n"
                           "Icon=dev.fileexplorer\nStartupWMClass=dev.fileexplorer\n";
    put(path, exported);
    assert(app_desktop_find(data, &desktop));
    assert(!strcmp(desktop.id, "fedora-build-dev.fileexplorer") && !strcmp(desktop.target, path));
    assert(app_desktop_install_icon(data, svg, strlen(svg), "svg") == APP_DESKTOP_UPDATED);
    text = get(path); image = icon_path(text); art = get(image);
    assert(!strcmp(art, svg));
    assert(strstr(text, "Exec=/usr/bin/distrobox-enter  -n fedora-build  --   file-explorer  %F\n"));
    assert(strstr(text, "StartupWMClass=fedora-build-dev.fileexplorer\n"));
    free(art); free(text);
    // A different image changes the Icon= filename, bypassing cached artwork.
    const char *other = "<svg xmlns=\"http://www.w3.org/2000/svg\"><path fill=\"#ff8040\"/></svg>";
    assert(app_desktop_install_icon(data, other, strlen(other), "svg") == APP_DESKTOP_UPDATED);
    text = get(path); char *different = icon_path(text);
    assert(strcmp(image, different));
    free(image); free(different); free(text);
    // Raster bytes, including NUL and alpha, are copied without conversion.
    const u8 png[] = {137, 80, 78, 71, 0, 255, 128, 0};
    assert(app_desktop_install_icon(data, png, sizeof png, "png") == APP_DESKTOP_UPDATED);
    text = get(path); image = icon_path(text);
    size_t n; u8 *bytes = app_desktop_read(image, &n);
    assert(bytes && n == sizeof png && !memcmp(bytes, png, n));
    free(bytes); free(image); free(text);
    // A second container's launcher is never touched, and missing exports fail clearly.
    setenv("CONTAINER_ID", "other-box", 1);
    assert(app_desktop_install_icon(data, svg, strlen(svg), "svg") == APP_DESKTOP_MISSING);
    text = get(path); assert(strstr(text, "fedora-build")); free(text);
    char standard[PATH_MAX];
    snprintf(standard, sizeof standard, "%s/.local/share/applications/dev.fileexplorer.desktop", home);
    put(standard, entry);
    assert(app_desktop_install_icon(data, svg, strlen(svg), "svg") == APP_DESKTOP_MISSING);
    text = get(standard); assert(!strcmp(text, entry)); free(text);
    // Renamed exports are accepted only when their Exec targets this container.
    put(standard, exported);
    assert(!app_desktop_find(data, &desktop));
    assert(app_desktop_is_export(standard, "fedora-build"));
    assert(!app_desktop_is_export(standard, "fedora"));
    assert(!app_desktop_rewrite("[Not Our Entry]\nIcon=x\n", "/tmp/icon.svg", "dev.fileexplorer"));
    assert(!app_desktop_rewrite(entry, "/tmp/bad\nicon.svg", "dev.fileexplorer"));
    char *escaped = app_desktop_rewrite(entry, "/tmp/back\\slash.svg", "dev.fileexplorer");
    assert(escaped && strstr(escaped, "Icon=/tmp/back\\\\slash.svg\n")); free(escaped);
    assert(!nftw(temp, cleanup, 16, FTW_DEPTH | FTW_PHYS));
    puts("Desktop icon checks passed: native and Distrobox launcher identity, host-visible artwork, original SVG/PNG bytes, cache invalidation, unchanged exports, preserved Exec/actions, missing launcher and escaping.");
}
