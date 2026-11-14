// cc -std=gnu11 -Wno-unused-function -Wno-format-truncation tools/apps-discovery-selftest.c -o /tmp/fe-apps-test -pthread
#include "../src/base.h"
#include <assert.h>
#include <sys/sysmacros.h>
struct linux_dirent64_ { u64 ino; i64 off; unsigned short d_reclen; unsigned char type; char d_name[]; };
static void main_wake(void) {}
static void thread_background_priority(void) {}
static void strv_free(char **v, int n) { for (int i = 0; i < n; i++) free(v[i]); free(v); }
static char **strv_dup(char **v, int n) { char **out = xcalloc((size_t)n, sizeof *out); for (int i = 0; i < n; i++) out[i] = strdup(v[i]); return out; }
static void md5_hex(const void *data, size_t len, char out[33]) { (void)data; (void)len; out[0] = 0; }
static void url_encode_path(const char *path, char *out, size_t cap) { snprintf(out, cap, "%s", path); }
static int mkdir_p(const char *path, mode_t mode) { return mkdir(path, mode); }
static void spawn_terminal(const char *dir, char *const cmd[]) { (void)dir; (void)cmd; }
#include "../src/apps.c"
static void put(const char *dir, const char *name, const char *text) {
    char path[PATH_MAX]; path_join(path, sizeof path, dir, name);
    FILE *f = fopen(path, "w"); assert(f); fputs(text, f); fclose(f);
}
int main(void) {
    char dir[] = "/tmp/fe-apps-XXXXXX"; assert(mkdtemp(dir));
    put(dir, "inkscape.desktop", "[Desktop Entry]\nType=Application\nName=Inkscape\nExec=inkscape %F\nTryExec=fe-host-only-inkscape\nMimeType=image/svg+xml;image/svg+xml-compressed;\n");
    apps_scan_dir_at(dir, "", false); assert(!app_by_id("inkscape.desktop"));
    // A host TryExec cannot be checked against the container's PATH.
    apps_scan_dir_at(dir, "", true);
    DesktopApp *ink = app_by_id("inkscape.desktop"); assert(ink && app_handles(ink, "image/svg+xml"));
    assert(app_handles(ink, "image/svg+xml-compressed"));
    apps_scan_dir_at(dir, "", true); assert(g_napps == 1);
    g_apps_loaded = true;
    DesktopApp *handlers[8]; assert(apps_for_mime("image/svg+xml", handlers, 8) == 1 && handlers[0] == ink);
    char path[PATH_MAX]; path_join(path, sizeof path, dir, "inkscape.desktop"); unlink(path); rmdir(dir);
    puts("app discovery: passed");
}
