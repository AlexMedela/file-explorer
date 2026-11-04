// archive.c - browse archives as read-only folders, extract and compress (libarchive).
//
// Virtual paths: "/dir/photos.zip::inner/folder" (inner part empty = archive root).

#include <archive.h>
#include <archive_entry.h>

#define ARC_SEP "::"

static bool is_archive_name(const char *name) {
    static const char *exts[] = {".zip", ".tar", ".tgz", ".tar.gz", ".tar.xz", ".txz", ".tar.zst", ".tzst", ".tar.bz2",
                                 ".tbz2", ".tar.lz", ".tar.lzma", ".7z", ".rar", ".cpio", ".iso", ".jar", ".apk",
                                 ".cbz", ".cbr", ".xpi", ".whl", ".deb", ".a"};
    size_t n = strlen(name);
    for (size_t i = 0; i < ARRAY_COUNT(exts); i++) {
        size_t l = strlen(exts[i]);
        if (n > l && !strcasecmp(name + n - l, exts[i])) return true;
    }
    return false;
}

// Split "file::inner" (returns false if not an archive path). inner has no leading/trailing '/'.
static bool arc_split(const char *vpath, char *file, size_t fcap, char *inner, size_t icap) {
    const char *sep = strstr(vpath, ARC_SEP);
    if (!sep) return false;
    snprintf(file, fcap, "%.*s", (int)(sep - vpath), vpath);
    const char *in = sep + 2;
    while (*in == '/') in++;
    snprintf(inner, icap, "%s", in);
    size_t l = strlen(inner);
    while (l && inner[l - 1] == '/') inner[--l] = 0;
    return true;
}

// Normalized member path: no leading "./" or "/", no trailing '/'. False for unsafe paths (".." parts).
static bool arc_member(const char *raw, char *out, size_t cap) {
    while (raw[0] == '.' && raw[1] == '/') raw += 2;
    while (*raw == '/') raw++;
    snprintf(out, cap, "%s", raw);
    size_t l = strlen(out);
    while (l && out[l - 1] == '/') out[--l] = 0;
    if (!l) return false;
    for (const char *p = out; *p;) { // reject ".." components
        const char *e = strchr(p, '/');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n == 2 && p[0] == '.' && p[1] == '.') return false;
        p = e ? e + 1 : p + n;
    }
    return true;
}

static struct archive *arc_open(const char *file) {
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, file, 1 << 16) != ARCHIVE_OK) {
        archive_read_free(a);
        return NULL;
    }
    return a;
}

typedef struct { char *name; bool dir; u64 size; i64 mtime; u16 mode; } ArcChild;

static int cmp_arcchild(const void *a, const void *b) { return strcmp(((const ArcChild *)a)->name, ((const ArcChild *)b)->name); }

// Listing for a folder inside an archive (called from scan_job on a worker thread).
static void arc_scan(Listing *L) {
    char file[PATH_MAX], inner[PATH_MAX];
    arc_split(L->path, file, sizeof file, inner, sizeof inner);
    struct archive *a = arc_open(file);
    if (!a) {
        L->err = EIO;
        atomic_store(&L->state, LST_ERROR);
        main_wake();
        return;
    }
    size_t il = strlen(inner);
    int cap = 256, n = 0;
    ArcChild *kids = xmalloc((size_t)cap * sizeof(ArcChild));
    struct archive_entry *ae;
    while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
        if (atomic_load_explicit(&L->cancel, memory_order_relaxed)) break;
        const char *raw = archive_entry_pathname_utf8(ae);
        if (!raw) raw = archive_entry_pathname(ae);
        char m[PATH_MAX];
        if (!raw || !arc_member(raw, m, sizeof m)) continue;
        const char *rest = m;
        if (il) {
            if (strncmp(m, inner, il) || m[il] != '/') continue;
            rest = m + il + 1;
        }
        const char *slash = strchr(rest, '/');
        bool dir = slash || archive_entry_filetype(ae) == AE_IFDIR;
        size_t nl = slash ? (size_t)(slash - rest) : strlen(rest);
        if (!nl || nl > NAME_MAX) continue;
        if (n == cap) { cap *= 2; kids = xrealloc(kids, (size_t)cap * sizeof(ArcChild)); }
        ArcChild *k = &kids[n++];
        k->name = strndup(rest, nl);
        k->dir = dir;
        k->size = slash ? 0 : (u64)archive_entry_size(ae);
        k->mtime = slash ? 0 : (i64)archive_entry_mtime(ae);
        k->mode = (u16)(slash ? (AE_IFDIR | 0755) : archive_entry_mode(ae));
    }
    archive_read_free(a);
    // one entry per child name (folders can appear many times through their members)
    qsort(kids, (size_t)n, sizeof(ArcChild), cmp_arcchild);
    u32 cnt = 0;
    Entry *ents = xmalloc((size_t)MAX(n, 1) * sizeof(Entry));
    for (int i = 0; i < n; i++) {
        if (cnt && !strcmp(ents[cnt - 1].name, kids[i].name)) {
            Entry *e = &ents[cnt - 1];
            if (kids[i].dir) e->flags |= ENT_DIR;
            if (!kids[i].dir && kids[i].mtime) { e->mtime = kids[i].mtime; e->size = kids[i].size; }
            free(kids[i].name);
            continue;
        }
        Entry *e = &ents[cnt++];
        size_t len = strlen(kids[i].name);
        char *s = arena_alloc(&L->names, len + 1, 1);
        memcpy(s, kids[i].name, len + 1);
        free(kids[i].name);
        e->name = s;
        e->name_len = (u32)len;
        e->flags = (kids[i].dir ? ENT_DIR : 0) | (s[0] == '.' ? ENT_HIDDEN : 0);
        e->mflags = 0;
        e->size = kids[i].size;
        e->mtime = kids[i].mtime;
        e->btime = 0;
        e->mode = kids[i].mode;
        e->uid = getuid();
        atomic_init(&e->meta, META_READY);
        e->ext = 0;
        if (!kids[i].dir) {
            const char *dot = strrchr(s, '.');
            if (dot && dot != s && dot[1]) e->ext = (u16)(dot - s + 1);
        }
    }
    free(kids);
    L->entries = ents;
    L->count = cnt;
    L->is_archive = true;
    OrderOpts oo = {L->sort_key, L->sort_desc, L->show_hidden, true, L->filter, L->group, L->tf_mode, L->tf_exts, false};
    listing_build_order_ex(L, &oo, &L->order, &L->order_count);
    L->t_names = L->t_meta = now_ns();
    atomic_store(&L->state, LST_COMPLETE);
    main_wake();
}

// --- extract (runs inside an Op thread) ----------------------------------------

// op->arc: archive file; op->src: member paths to extract (empty = everything); op->dst_dir: target.
static void run_extract(Op *op) {
    atomic_store(&op->phase, OPS_RUNNING);
    struct archive *a = arc_open(op->arc);
    if (!a) { op_error(op, "Can't open archive \xE2\x80\x9C%s\xE2\x80\x9D", path_basename(op->arc)); return; }
    struct stat dst_st;
    bool created_dst = stat(op->dst_dir, &dst_st) != 0; // a folder we make is itself the result
    mkdir_p(op->dst_dir, 0755);
    char *buf = xmalloc(1 << 18);
    struct archive_entry *ae;
    while (!op_cancelled(op) && archive_read_next_header(a, &ae) == ARCHIVE_OK) {
        const char *raw = archive_entry_pathname_utf8(ae);
        if (!raw) raw = archive_entry_pathname(ae);
        char m[PATH_MAX];
        if (!raw || !arc_member(raw, m, sizeof m)) continue; // unsafe or empty: skip (zip-slip guard)
        // selected? strip the selection's parent so "a/b" extracts as "b/..."
        const char *rel = m;
        if (op->nsrc) {
            bool hit = false;
            for (int i = 0; i < op->nsrc && !hit; i++) {
                size_t sl = strlen(op->src[i]);
                if (!strncmp(m, op->src[i], sl) && (m[sl] == 0 || m[sl] == '/')) {
                    const char *ps = strrchr(op->src[i], '/');
                    rel = ps ? m + (ps - op->src[i]) + 1 : m;
                    hit = true;
                }
            }
            if (!hit) continue;
        }
        char out[PATH_MAX * 2];
        snprintf(out, sizeof out, "%s/%s", op->dst_dir, rel);
        op_set_cur(op, path_basename(rel));
        int ft = archive_entry_filetype(ae);
        if (ft == AE_IFDIR) {
            mkdir_p(out, 0755);
        } else if (ft == AE_IFREG) {
            char par[PATH_MAX * 2], base[NAME_MAX + 1];
            path_split(out, par, sizeof par, base, sizeof base);
            mkdir_p(par, 0755);
            int fd = open(out, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, (archive_entry_mode(ae) & 0777) | 0600);
            if (fd < 0 && errno == EEXIST) { // keep both
                int dfd = open(par, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
                char nn[NAME_MAX + 1];
                unique_name(dfd, base, false, false, nn, sizeof nn);
                close(dfd);
                snprintf(out, sizeof out, "%s/%s", par, nn);
                fd = open(out, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, (archive_entry_mode(ae) & 0777) | 0600);
            }
            if (fd < 0) { op_error(op, "Can't create \xE2\x80\x9C%s\xE2\x80\x9D: %s", base, strerror(errno)); continue; }
            la_ssize_t k;
            bool ok = true;
            while ((k = archive_read_data(a, buf, 1 << 18)) > 0 && !op_cancelled(op)) {
                if (write(fd, buf, (size_t)k) != k) { ok = false; break; }
                atomic_fetch_add(&op->bytes_done, (u64)k);
            }
            close(fd);
            if (!ok || k < 0 || op_cancelled(op)) {
                unlink(out);
                if (!op_cancelled(op)) op_error(op, "Error extracting \xE2\x80\x9C%s\xE2\x80\x9D", base);
                continue;
            }
            struct timespec ts[2] = {{archive_entry_mtime(ae), 0}, {archive_entry_mtime(ae), 0}};
            utimensat(AT_FDCWD, out, ts, 0);
        } else if (ft == AE_IFLNK) {
            const char *target = archive_entry_symlink(ae);
            if (target && target[0] != '/' && !strstr(target, "..")) { // relative, contained links only
                char par[PATH_MAX * 2], base[NAME_MAX + 1];
                path_split(out, par, sizeof par, base, sizeof base);
                mkdir_p(par, 0755);
                if (symlink(target, out) != 0) op_error(op, "Couldn\xE2\x80\x99t create the link \xE2\x80\x9C%s\xE2\x80\x9D", base);
            }
        }
        atomic_fetch_add(&op->files_done, 1);
        if (!strchr(rel, '/')) op_record(op, NULL, out, NULL); // top-level results get selected afterwards
    }
    free(buf);
    archive_read_free(a);
    if (created_dst && !op->open_after && !op_cancelled(op)) { // report (and undo) the new folder, not its contents
        strv_free(op->in, op->nout);
        strv_free(op->out, op->nout);
        strv_free(op->aux, op->nout);
        op->in = op->out = op->aux = NULL;
        op->nout = op->capout = 0;
        op_record(op, NULL, op->dst_dir, NULL);
    }
}

// --- compress ------------------------------------------------------------------

static void compress_add(Op *op, struct archive *w, const char *path, const char *rel, char *buf) {
    if (op_cancelled(op)) return;
    struct stat st;
    if (lstat(path, &st)) return;
    struct archive_entry *e = archive_entry_new();
    archive_entry_copy_stat(e, &st);
    archive_entry_set_pathname_utf8(e, rel);
    if (S_ISLNK(st.st_mode)) {
        char t[PATH_MAX];
        ssize_t n = readlink(path, t, sizeof t - 1);
        if (n >= 0) { t[n] = 0; archive_entry_set_symlink(e, t); }
    }
    op_set_cur(op, path_basename(path));
    if (archive_write_header(w, e) != ARCHIVE_OK) {
        op_error(op, "Can't add \xE2\x80\x9C%s\xE2\x80\x9D: %s", rel, archive_error_string(w));
    } else if (S_ISREG(st.st_mode)) {
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            ssize_t n;
            while ((n = read(fd, buf, 1 << 18)) > 0 && !op_cancelled(op)) {
                archive_write_data(w, buf, (size_t)n);
                atomic_fetch_add(&op->bytes_done, (u64)n);
            }
            close(fd);
        }
    }
    archive_entry_free(e);
    atomic_fetch_add(&op->files_done, 1);
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return;
        struct dirent *de;
        while ((de = readdir(d)) && !op_cancelled(op)) {
            if (is_dot_or_dotdot(de->d_name)) continue;
            char cp[PATH_MAX], cr[PATH_MAX];
            snprintf(cp, sizeof cp, "%s/%s", path, de->d_name);
            snprintf(cr, sizeof cr, "%s/%s", rel, de->d_name);
            compress_add(op, w, cp, cr, buf);
        }
        closedir(d);
    }
}

// op->src: items; op->arc: output archive path (".zip" or ".tar.gz").
static void run_compress(Op *op) {
    for (int i = 0; i < op->nsrc; i++) count_tree(op, AT_FDCWD, op->src[i]);
    atomic_store(&op->phase, OPS_RUNNING);
    struct archive *w = archive_write_new();
    size_t l = strlen(op->arc);
    if (l > 4 && !strcmp(op->arc + l - 4, ".zip")) archive_write_set_format_zip(w);
    else { archive_write_set_format_pax_restricted(w); archive_write_add_filter_gzip(w); }
    char tmp[PATH_MAX + 16];
    snprintf(tmp, sizeof tmp, "%s.part", op->arc);
    if (archive_write_open_filename(w, tmp) != ARCHIVE_OK) {
        op_error(op, "Can't create \xE2\x80\x9C%s\xE2\x80\x9D: %s", path_basename(op->arc), archive_error_string(w));
        archive_write_free(w);
        return;
    }
    char *buf = xmalloc(1 << 18);
    for (int i = 0; i < op->nsrc && !op_cancelled(op); i++) compress_add(op, w, op->src[i], path_basename(op->src[i]), buf);
    free(buf);
    archive_write_close(w);
    archive_write_free(w);
    if (op_cancelled(op) || op->nerr) { unlink(tmp); return; }
    if (rename_noreplace(AT_FDCWD, tmp, AT_FDCWD, op->arc)) {
        op_error(op, "Can't create \xE2\x80\x9C%s\xE2\x80\x9D: %s", path_basename(op->arc), strerror(errno));
        unlink(tmp);
        return;
    }
    op_record(op, NULL, op->arc, NULL);
}
