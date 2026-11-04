// preview.c - inspector previews on a background thread: images (stb_image),
// thumbnails (own cache, freedesktop cache, installed thumbnailers), text, folders.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#include "../third_party/stb_image.h"
#pragma GCC diagnostic pop

#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>

extern char **environ;

// --- MD5 (RFC 1321), for freedesktop thumbnail names -----------------------

static void md5_hex(const void *data, size_t len, char out[33]) {
    static const u32 K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const u8 R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                             14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                             4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    u32 h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    size_t total = ((len + 8) / 64 + 1) * 64;
    u8 *msg = xcalloc(total, 1);
    memcpy(msg, data, len);
    msg[len] = 0x80;
    u64 bits = (u64)len * 8;
    memcpy(msg + total - 8, &bits, 8); // little-endian host
    for (size_t off = 0; off < total; off += 64) {
        u32 w[16];
        memcpy(w, msg + off, 64);
        u32 a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; i++) {
            u32 f, g;
            if (i < 16) { f = (b & c) | (~b & d); g = (u32)i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5u * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3u * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7u * i) % 16; }
            u32 tmp = d;
            d = c;
            c = b;
            u32 x = a + f + K[i] + w[g];
            b = b + ((x << R[i]) | (x >> (32 - R[i])));
            a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    }
    free(msg);
    const u8 *bytes = (const u8 *)h;
    for (int i = 0; i < 16; i++) sprintf(out + i * 2, "%02x", bytes[i]);
    out[32] = 0;
}

// --- requests / results ----------------------------------------------------

enum { PV_NONE, PV_IMAGE, PV_TEXT, PV_DIR, PV_INFO };

typedef struct {
    u32 gen;
    char path[PATH_MAX];
    int max_px;       // longest side for decoded images
    int kind;
    // image
    u8 *rgba;
    int w, h;         // decoded (possibly downscaled)
    int src_w, src_h; // original dimensions (0 if unknown, e.g. from a thumbnail)
    bool from_thumb;
    // text
    char *text;       // UTF-8, up to ~64 KB
    bool binary;
    int want_line;    // contents search: show this line (0 = the top)
    int first_line;   // line number of the first line in text (a window further down a big file)
    // folder
    u32 n_dirs, n_files;
    bool truncated;
    char note[160];   // e.g. "No preview available"
} PreviewResult;

static void preview_result_free(PreviewResult *r) {
    if (!r) return;
    if (r->rgba) stbi_image_free(r->rgba);
    free(r->text);
    free(r);
}

static void preview_done_cb(void *arg); // app.c

static struct {
    pthread_mutex_t mtx;
    pthread_cond_t cv;
    PreviewResult *pending; // latest request wins
    bool started;
} g_pv = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, NULL, false};

// --- helpers ---------------------------------------------------------------

static const char *path_ext_lower(const char *path, char *buf, size_t cap) {
    const char *base = path_basename(path);
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base) { buf[0] = 0; return buf; }
    size_t i = 0;
    for (dot++; *dot && i + 1 < cap; dot++) buf[i++] = (char)ascii_lower((u8)*dot);
    buf[i] = 0;
    return buf;
}

static bool ext_in(const char *ext, const char *list) {
    char key[24];
    if (!ext[0] || strlen(ext) > 20) return false;
    snprintf(key, sizeof key, " %s ", ext);
    return strstr(list, key) != NULL;
}

#define STB_EXTS " png jpg jpeg jpe gif bmp tga psd hdr pnm ppm pgm pic "
#define TEXT_EXTS " txt md markdown rst log csv tsv json jsonc yaml yml toml ini cfg conf c h cc cpp hpp cxx rs go py rb js mjs ts tsx jsx java kt swift cs php lua sh bash zsh fish ps1 sql html htm xml css scss less vue svelte zig nim hs ml ex exs erl clj lisp el vim diff patch gitignore env makefile cmake mk gradle properties tex bib srt vtt desktop service "

// Box-filter downscale RGBA so the longest side is <= max_px.
static u8 *downscale(u8 *src, int w, int h, int max_px, int *ow, int *oh) {
    int f = 1;
    while ((w / f) > max_px || (h / f) > max_px) f++;
    if (f == 1) { *ow = w; *oh = h; return src; }
    int nw = w / f, nh = h / f;
    u8 *dst = malloc((size_t)nw * nh * 4);
    if (!dst) { *ow = w; *oh = h; return src; }
    for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++) {
            u32 acc[4] = {0, 0, 0, 0};
            for (int yy = 0; yy < f; yy++) {
                const u8 *row = src + ((size_t)(y * f + yy) * w + (size_t)x * f) * 4;
                for (int xx = 0; xx < f; xx++)
                    for (int c = 0; c < 4; c++) acc[c] += row[xx * 4 + c];
            }
            u8 *o = dst + ((size_t)y * nw + x) * 4;
            for (int c = 0; c < 4; c++) o[c] = (u8)(acc[c] / (u32)(f * f));
        }
    stbi_image_free(src);
    *ow = nw;
    *oh = nh;
    return dst;
}

static bool load_image(PreviewResult *r, const char *file) {
    int w, h, n;
    u8 *px = stbi_load(file, &w, &h, &n, 4);
    if (!px) return false;
    r->rgba = downscale(px, w, h, r->max_px, &r->w, &r->h);
    r->kind = PV_IMAGE;
    return true;
}

static bool newer_than(const char *a, i64 mtime) {
    struct stat st;
    return stat(a, &st) == 0 && st.st_size > 0 && st.st_mtime >= mtime;
}

// --- freedesktop thumbnailers ----------------------------------------------

typedef struct { char *exec; char *mimes; } Thumbnailer; // mimes: ";a/b;c/d;"
static Thumbnailer *g_tn;
static int g_ntn;

static void thumbnailers_load_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l < 12 || strcmp(e->d_name + l - 12, ".thumbnailer")) continue;
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char line[4096], exec[2048] = "", tryexec[1024] = "", mimes[4096] = "";
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!strncmp(line, "Exec=", 5)) snprintf(exec, sizeof exec, "%s", line + 5);
            else if (!strncmp(line, "TryExec=", 8)) snprintf(tryexec, sizeof tryexec, "%s", line + 8);
            else if (!strncmp(line, "MimeType=", 9)) snprintf(mimes, sizeof mimes, ";%s;", line + 9);
        }
        fclose(f);
        if (!exec[0] || !mimes[0]) continue;
        if (tryexec[0]) { // skip thumbnailers whose program isn't installed
            char prog[PATH_MAX];
            bool found = false;
            if (tryexec[0] == '/') found = access(tryexec, X_OK) == 0;
            else {
                const char *pe = getenv("PATH");
                char pathv[4096];
                snprintf(pathv, sizeof pathv, "%s", pe ? pe : "/usr/bin");
                for (char *s = strtok(pathv, ":"); s && !found; s = strtok(NULL, ":")) {
                    snprintf(prog, sizeof prog, "%s/%s", s, tryexec);
                    found = access(prog, X_OK) == 0;
                }
            }
            if (!found) continue;
        }
        g_tn = xrealloc(g_tn, (size_t)(g_ntn + 1) * sizeof(Thumbnailer));
        g_tn[g_ntn].exec = strdup(exec);
        g_tn[g_ntn].mimes = strdup(mimes);
        g_ntn++;
    }
    closedir(d);
}

static const char *mime_for_ext(const char *ext) {
    static const struct { const char *ext, *mime; } map[] = {
        {"mp4", "video/mp4"}, {"m4v", "video/x-m4v"}, {"mkv", "video/x-matroska"}, {"webm", "video/webm"},
        {"avi", "video/x-msvideo"}, {"mov", "video/quicktime"}, {"wmv", "video/x-ms-wmv"}, {"flv", "video/x-flv"},
        {"mpg", "video/mpeg"}, {"mpeg", "video/mpeg"}, {"ts", "video/mp2t"}, {"3gp", "video/3gpp"},
        {"mp3", "audio/mpeg"}, {"flac", "audio/flac"}, {"m4a", "audio/mp4"}, {"ogg", "audio/ogg"}, {"opus", "audio/ogg"},
        {"pdf", "application/pdf"}, {"epub", "application/epub+zip"}, {"djvu", "image/vnd.djvu"},
        {"cbz", "application/vnd.comicbook+zip"}, {"cbr", "application/vnd.comicbook-rar"},
        {"webp", "image/webp"}, {"avif", "image/avif"}, {"heic", "image/heic"}, {"heif", "image/heif"},
        {"jxl", "image/jxl"}, {"svg", "image/svg+xml"}, {"svgz", "image/svg+xml-compressed"}, {"tif", "image/tiff"},
        {"tiff", "image/tiff"}, {"ico", "image/vnd.microsoft.icon"}, {"exr", "image/x-exr"}, {"qoi", "image/qoi"},
        {"odt", "application/vnd.oasis.opendocument.text"}, {"ods", "application/vnd.oasis.opendocument.spreadsheet"},
        {"odp", "application/vnd.oasis.opendocument.presentation"},
        {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        {"exe", "application/x-ms-dos-executable"}, {"xopp", "application/x-xopp"},
    };
    for (size_t i = 0; i < ARRAY_COUNT(map); i++)
        if (!strcmp(ext, map[i].ext)) return map[i].mime;
    return NULL;
}

static const Thumbnailer *thumbnailer_for(const char *mime) {
    char key[256];
    snprintf(key, sizeof key, ";%s;", mime);
    for (int i = 0; i < g_ntn; i++)
        if (strstr(g_tn[i].mimes, key)) return &g_tn[i];
    return NULL;
}

// Run a thumbnailer with a timeout. Returns true if `out` was produced.
static bool run_thumbnailer(const Thumbnailer *t, const char *in, const char *uri, const char *out, int size) {
    char *argv[64];
    int argc = 0;
    char buf[4096];
    snprintf(buf, sizeof buf, "%s", t->exec);
    char sz[16];
    snprintf(sz, sizeof sz, "%d", size);
    for (char *tok = strtok(buf, " \t"); tok && argc < 63; tok = strtok(NULL, " \t")) {
        if (!strcmp(tok, "%i")) argv[argc++] = (char *)in;
        else if (!strcmp(tok, "%u")) argv[argc++] = (char *)uri;
        else if (!strcmp(tok, "%o")) argv[argc++] = (char *)out;
        else if (!strcmp(tok, "%s")) argv[argc++] = sz;
        else argv[argc++] = tok;
    }
    argv[argc] = NULL;
    if (!argc) return false;
    unlink(out);
    pid_t pid;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    int rc = posix_spawnp(&pid, argv[0], &fa, spawn_attr(), argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc) return false;
    // SIGCHLD is ignored (children reap themselves), so poll until the pid is gone.
    for (int i = 0; i < 800; i++) {
        int st;
        pid_t w = waitpid(pid, &st, WNOHANG);
        if (w == pid || (w < 0 && errno == ECHILD)) break;
        if (i == 799) { kill(pid, SIGKILL); return false; }
        usleep(10000);
    }
    struct stat st;
    return stat(out, &st) == 0 && st.st_size > 0;
}

static void cache_dir(char *out, size_t cap) {
    const char *x = getenv("XDG_CACHE_HOME");
    if (x && x[0] == '/') snprintf(out, cap, "%s/file-explorer/thumbs", x);
    else snprintf(out, cap, "%s/.cache/file-explorer/thumbs", getenv("HOME") ? getenv("HOME") : "/tmp");
}

static bool try_thumbnail(PreviewResult *r, const char *path, i64 mtime, const char *ext) {
    char enc[PATH_MAX * 3], uri[PATH_MAX * 3 + 8], md5[33], dir[PATH_MAX], f[PATH_MAX + 64];
    url_encode_path(path, enc, sizeof enc);
    snprintf(uri, sizeof uri, "file://%s", enc);
    md5_hex(uri, strlen(uri), md5);
    // 1. our own cache
    cache_dir(dir, sizeof dir);
    snprintf(f, sizeof f, "%s/%s.png", dir, md5);
    if (newer_than(f, mtime) && load_image(r, f)) return true;
    // 2. thumbnails other apps already made
    const char *home = getenv("HOME");
    const char *sizes[] = {"xx-large", "x-large", "large", "normal"};
    for (size_t i = 0; i < ARRAY_COUNT(sizes) && home; i++) {
        char g[PATH_MAX + 96];
        const char *xc = getenv("XDG_CACHE_HOME");
        if (xc && xc[0] == '/') snprintf(g, sizeof g, "%s/thumbnails/%s/%s.png", xc, sizes[i], md5);
        else snprintf(g, sizeof g, "%s/.cache/thumbnails/%s/%s.png", home, sizes[i], md5);
        if (newer_than(g, mtime) && load_image(r, g)) return true;
    }
    // 3. run an installed thumbnailer
    const char *mime = mime_for_ext(ext);
    const Thumbnailer *t = mime ? thumbnailer_for(mime) : NULL;
    if (!t) return false;
    mkdir_p(dir, 0700);
    char tmp[PATH_MAX + 80];
    snprintf(tmp, sizeof tmp, "%s/.%s.tmp.png", dir, md5);
    if (!run_thumbnailer(t, path, uri, tmp, 512)) { unlink(tmp); return false; }
    rename(tmp, f);
    return load_image(r, f);
}

// --- worker ----------------------------------------------------------------

static void preview_build(PreviewResult *r) {
    struct stat st;
    if (stat(r->path, &st)) {
        r->kind = PV_INFO;
        snprintf(r->note, sizeof r->note, "%s", strerror(errno));
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        r->kind = PV_DIR;
        int fd = open(r->path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd < 0) { snprintf(r->note, sizeof r->note, "%s", strerror(errno)); return; }
        char *buf = xmalloc(64 * 1024);
        for (;;) {
            long n = syscall(SYS_getdents64, fd, buf, 64 * 1024);
            if (n <= 0) break;
            for (long off = 0; off < n;) {
                struct linux_dirent64_ *d = (struct linux_dirent64_ *)(buf + off);
                off += d->d_reclen;
                if (is_dot_or_dotdot(d->d_name)) continue;
                if (d->d_type == DT_DIR) r->n_dirs++;
                else r->n_files++;
            }
            if (r->n_dirs + r->n_files > 1000000) { r->truncated = true; break; }
        }
        free(buf);
        close(fd);
        return;
    }
    char ext[24];
    path_ext_lower(r->path, ext, sizeof ext);
    if (ext_in(ext, STB_EXTS) && st.st_size < (200ll << 20)) {
        int w = 0, h = 0, n = 0;
        if (stbi_info(r->path, &w, &h, &n)) {
            r->src_w = w;
            r->src_h = h;
            if ((i64)w * h <= 120000000 && load_image(r, r->path)) return; // skip absurdly large images
        }
    }
    if (try_thumbnail(r, r->path, st.st_mtime, ext)) {
        r->from_thumb = true;
        return;
    }
    // text: known extension, or small file without NUL bytes
    bool textish = ext_in(ext, TEXT_EXTS) || !ext[0] || st.st_size < (1 << 20);
    if (textish && S_ISREG(st.st_mode)) {
        int fd = open(r->path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            size_t cap = 64 * 1024;
            char *buf = xmalloc(cap + 1);
            off_t start = 0;
            r->first_line = 1;
            if (r->want_line > 40 && st.st_size > (off_t)cap) { // a line further down: start a little above it
                int target = r->want_line - 30, line = 1;
                for (off_t off = 0; line < target && off < (256 << 20);) {
                    ssize_t k = pread(fd, buf, cap, off);
                    if (k <= 0) break;
                    for (ssize_t i = 0; i < k; i++)
                        if (buf[i] == '\n' && ++line == target) { start = off + i + 1; break; }
                    off += k;
                }
                if (line == target) r->first_line = target;
                else start = 0;
            }
            ssize_t n = pread(fd, buf, cap, start);
            close(fd);
            if (n > 0) {
                bool bin = memchr(buf, 0, (size_t)n) != NULL;
                if (!bin) {
                    buf[n] = 0;
                    r->text = buf;
                    r->kind = PV_TEXT;
                    return;
                }
                r->binary = true;
            }
            free(buf);
        }
    }
    r->kind = PV_INFO;
    snprintf(r->note, sizeof r->note, "%s", r->binary ? "Binary file" : "No preview available");
}

static pthread_once_t g_tn_once = PTHREAD_ONCE_INIT;

static void thumbnailers_init(void) {
    char tdir[PATH_MAX];
    const char *xd = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xd && xd[0] == '/') snprintf(tdir, sizeof tdir, "%s/thumbnailers", xd);
    else snprintf(tdir, sizeof tdir, "%s/.local/share/thumbnailers", home ? home : "/");
    thumbnailers_load_dir(tdir);
    char dirs[32][PATH_MAX];
    int n = xdg_dirs("XDG_DATA_DIRS", XDG_DATA_DEF, dirs, 32);
    for (int i = 0; i < n; i++) {
        snprintf(tdir, sizeof tdir, "%s/thumbnailers", dirs[i]);
        thumbnailers_load_dir(tdir);
    }
}

static void *preview_thread(void *arg) {
    (void)arg;
    thread_background_priority();
    pthread_once(&g_tn_once, thumbnailers_init);
    for (;;) {
        pthread_mutex_lock(&g_pv.mtx);
        while (!g_pv.pending) pthread_cond_wait(&g_pv.cv, &g_pv.mtx);
        PreviewResult *r = g_pv.pending;
        g_pv.pending = NULL;
        pthread_mutex_unlock(&g_pv.mtx);
        preview_build(r);
        main_post(preview_done_cb, r);
    }
    return NULL;
}

static void preview_request(u32 gen, const char *path, int max_px, int line) {
    PreviewResult *r = xcalloc(1, sizeof *r);
    r->gen = gen;
    r->want_line = line;
    snprintf(r->path, sizeof r->path, "%s", path);
    r->max_px = max_px;
    pthread_mutex_lock(&g_pv.mtx);
    if (!g_pv.started) {
        g_pv.started = true;
        pthread_t t;
        pthread_create(&t, NULL, preview_thread, NULL);
        pthread_setname_np(t, "fe-preview");
        pthread_detach(t);
    }
    if (g_pv.pending) preview_result_free(g_pv.pending); // superseded before it started
    g_pv.pending = r;
    pthread_cond_signal(&g_pv.cv);
    pthread_mutex_unlock(&g_pv.mtx);
}

// --- grid thumbnails: small worker pool, newest requests first --------------

typedef struct ThumbReq {
    char path[PATH_MAX];
    i64 mtime;
    int px;
    u8 *rgba;
    int w, h;
    struct ThumbReq *next;
} ThumbReq;

static void thumb_done_cb(void *arg); // app.c

static struct {
    pthread_mutex_t mtx;
    pthread_cond_t cv;
    ThumbReq *head; // LIFO: the latest requests are the visible ones
    int n;
    bool started;
} g_tq = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, NULL, 0, false};

static bool thumb_capable(const char *name) {
    char ext[24];
    path_ext_lower(name, ext, sizeof ext);
    return ext_in(ext, STB_EXTS) || mime_for_ext(ext) != NULL;
}

static void thumb_build(ThumbReq *r) {
    char ext[24];
    path_ext_lower(r->path, ext, sizeof ext);
    PreviewResult tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.max_px = r->px;
    struct stat st;
    bool ok = false;
    if (stat(r->path, &st) == 0) {
        if (ext_in(ext, STB_EXTS) && st.st_size < (80ll << 20)) {
            int w = 0, h = 0, n = 0;
            if (stbi_info(r->path, &w, &h, &n) && (i64)w * h <= 60000000) ok = load_image(&tmp, r->path);
        }
        if (!ok) ok = try_thumbnail(&tmp, r->path, st.st_mtime, ext);
    }
    if (ok && tmp.rgba) {
        r->rgba = tmp.rgba;
        r->w = tmp.w;
        r->h = tmp.h;
    }
}

static void *thumb_thread(void *arg) {
    (void)arg;
    thread_background_priority();
    pthread_once(&g_tn_once, thumbnailers_init); // shared with the preview thread
    for (;;) {
        pthread_mutex_lock(&g_tq.mtx);
        while (!g_tq.head) pthread_cond_wait(&g_tq.cv, &g_tq.mtx);
        ThumbReq *r = g_tq.head;
        g_tq.head = r->next;
        g_tq.n--;
        pthread_mutex_unlock(&g_tq.mtx);
        thumb_build(r);
        main_post(thumb_done_cb, r);
    }
    return NULL;
}

static void thumb_request(const char *path, i64 mtime, int px) {
    ThumbReq *r = xcalloc(1, sizeof *r);
    snprintf(r->path, sizeof r->path, "%s", path);
    r->mtime = mtime;
    r->px = px;
    pthread_mutex_lock(&g_tq.mtx);
    if (!g_tq.started) {
        g_tq.started = true;
        for (int i = 0; i < 2; i++) {
            pthread_t t;
            pthread_create(&t, NULL, thumb_thread, NULL);
            pthread_setname_np(t, "fe-thumb");
            pthread_detach(t);
        }
    }
    r->next = g_tq.head;
    g_tq.head = r;
    g_tq.n++;
    if (g_tq.n > 600) { // drop the oldest (no longer visible) requests; report them as failed-free
        ThumbReq *q = g_tq.head;
        for (int i = 0; i < 400 && q; i++) q = q->next;
        if (q) {
            ThumbReq *rest = q->next;
            q->next = NULL;
            while (rest) {
                ThumbReq *nx = rest->next;
                g_tq.n--;
                main_post(thumb_done_cb, rest); // unbuilt: the UI forgets the pending slot
                rest = nx;
            }
        }
    }
    pthread_cond_signal(&g_tq.cv);
    pthread_mutex_unlock(&g_tq.mtx);
}
