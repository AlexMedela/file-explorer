// mtp.c - PTP and MTP (Android phones in "File transfer" mode, cameras) ourselves, over usb.c: the container
// protocol, device / storage / object info, fast folder listings (GetObjectPropList), partial reads, uploads,
// folders, rename, move, delete.

enum { PTP_CMD = 1, PTP_DATA = 2, PTP_RESP = 3 };
enum {
    PTP_GET_DEVICE_INFO = 0x1001, PTP_OPEN_SESSION = 0x1002, PTP_CLOSE_SESSION = 0x1003, PTP_GET_STORAGE_IDS = 0x1004,
    PTP_GET_STORAGE_INFO = 0x1005, PTP_GET_OBJECT_HANDLES = 0x1007, PTP_GET_OBJECT_INFO = 0x1008, PTP_GET_OBJECT = 0x1009,
    PTP_DELETE_OBJECT = 0x100B, PTP_SEND_OBJECT_INFO = 0x100C, PTP_SEND_OBJECT = 0x100D, PTP_MOVE_OBJECT = 0x1019,
    PTP_GET_PARTIAL_OBJECT = 0x101B, MTP_GET_PARTIAL_OBJECT_64 = 0x95C1, MTP_SET_OBJECT_PROP_VALUE = 0x9804,
    MTP_GET_OBJECT_PROP_LIST = 0x9805,
};
enum {
    PTP_OK = 0x2001, PTP_GENERAL_ERROR = 0x2002, PTP_SESSION_NOT_OPEN = 0x2003, PTP_NOT_SUPPORTED = 0x2005,
    PTP_INVALID_HANDLE = 0x2009, PTP_STORE_FULL = 0x200C, PTP_STORE_READ_ONLY = 0x200E, PTP_ACCESS_DENIED = 0x200F,
    PTP_STORE_NOT_AVAILABLE = 0x2013, PTP_DEVICE_BUSY = 0x2019, PTP_SESSION_ALREADY_OPEN = 0x201E,
};
enum { MTP_PROP_STORAGE = 0xDC01, MTP_PROP_FORMAT = 0xDC02, MTP_PROP_SIZE = 0xDC04, MTP_PROP_NAME = 0xDC07,
       MTP_PROP_MODIFIED = 0xDC09, MTP_PROP_PARENT = 0xDC0B };
#define PTP_FMT_FOLDER 0x3001
#define PTP_ROOT 0xFFFFFFFFu
#define PTP_RBUF (256u << 10)

typedef struct {
    Usb *usb;
    u32 tid;
    pthread_mutex_t mtx;   // one transaction at a time
    u16 *ops;              // operations the device supports
    int nops;
    char model[128], manufacturer[128], serial[128];
    bool mtp;              // Microsoft's MTP extensions (vs a plain PTP camera)
    bool proplist_bad;     // GetObjectPropList gave something unusable once: not asked again
    u8 *rbuf;              // bulk IN data not used yet (a transfer can end inside the next container)
    size_t rlen, roff;
} Ptp;

typedef struct {
    u32 handle, storage, parent;
    u16 format;
    u64 size;
    time_t mtime;
    char name[256];
} PtpObj;

static bool ptp_supports(const Ptp *p, u16 op) {
    for (int i = 0; i < p->nops; i++) if (p->ops[i] == op) return true;
    return false;
}

static void put_le16(u8 *b, u16 v) { b[0] = (u8)v; b[1] = (u8)(v >> 8); }
static void put_le32(u8 *b, u32 v) { for (int i = 0; i < 4; i++) b[i] = (u8)(v >> (8 * i)); }
static u16 get_le16(const u8 *b) { return (u16)(b[0] | b[1] << 8); }
static u32 get_le32(const u8 *b) { return (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24; }

// --- datasets: a cursor over received data -----------------------------------------------------------------

typedef struct { const u8 *d; size_t n, o; bool bad; } PtpCur;

static u8 pc_u8(PtpCur *c) { if (c->o + 1 > c->n) { c->bad = true; return 0; } return c->d[c->o++]; }
static u16 pc_u16(PtpCur *c) { if (c->o + 2 > c->n) { c->bad = true; return 0; } u16 v = get_le16(c->d + c->o); c->o += 2; return v; }
static u32 pc_u32(PtpCur *c) { if (c->o + 4 > c->n) { c->bad = true; return 0; } u32 v = get_le32(c->d + c->o); c->o += 4; return v; }
static u64 pc_u64(PtpCur *c) { u64 lo = pc_u32(c); return lo | (u64)pc_u32(c) << 32; }
static void pc_skip(PtpCur *c, size_t n) { if (c->o + n > c->n) { c->bad = true; c->o = c->n; } else c->o += n; }

// PTP string: a count of UTF-16 units (with the terminator), then UTF-16LE. Into UTF-8.
static void pc_str(PtpCur *c, char *out, size_t cap) {
    u8 n = pc_u8(c);
    size_t k = 0;
    for (int i = 0; i < n; i++) {
        u32 ch = pc_u16(c);
        if (ch >= 0xd800 && ch < 0xdc00 && i + 1 < n) { u32 lo = pc_u16(c); i++; ch = 0x10000 + ((ch - 0xd800) << 10) + (lo - 0xdc00); }
        if (!ch) continue;
        char t[4];
        int l = ch < 0x80 ? (t[0] = (char)ch, 1)
              : ch < 0x800 ? (t[0] = (char)(0xc0 | ch >> 6), t[1] = (char)(0x80 | (ch & 63)), 2)
              : ch < 0x10000 ? (t[0] = (char)(0xe0 | ch >> 12), t[1] = (char)(0x80 | (ch >> 6 & 63)), t[2] = (char)(0x80 | (ch & 63)), 3)
              : (t[0] = (char)(0xf0 | ch >> 18), t[1] = (char)(0x80 | (ch >> 12 & 63)), t[2] = (char)(0x80 | (ch >> 6 & 63)), t[3] = (char)(0x80 | (ch & 63)), 4);
        if (k + (size_t)l < cap) { memcpy(out + k, t, (size_t)l); k += (size_t)l; }
    }
    if (cap) out[MIN(k, cap - 1)] = 0;
}

// u32 count, then elements of size esz: returns the count, *at the first element.
static u32 pc_array(PtpCur *c, size_t esz, size_t *at) {
    u32 n = pc_u32(c);
    *at = c->o;
    if ((u64)n * esz > c->n - MIN(c->o, c->n)) { c->bad = true; n = 0; }
    pc_skip(c, n * esz);
    return n;
}

// Builder for data we send.
typedef struct { u8 *b; size_t n, cap; } PtpBuf;
static void pbuf_need(PtpBuf *b, size_t k) { if (b->n + k > b->cap) { b->cap = (b->n + k) * 2 + 64; b->b = xrealloc(b->b, b->cap); } }
static void pbuf_u8(PtpBuf *b, u8 v) { pbuf_need(b, 1); b->b[b->n++] = v; }
static void pbuf_u16(PtpBuf *b, u16 v) { pbuf_need(b, 2); put_le16(b->b + b->n, v); b->n += 2; }
static void pbuf_u32(PtpBuf *b, u32 v) { pbuf_need(b, 4); put_le32(b->b + b->n, v); b->n += 4; }
static void pbuf_str(PtpBuf *b, const char *s) { // UTF-8 -> PTP string (count includes the terminator; 0 = empty)
    u16 units[256];
    int n = 0;
    for (const u8 *c = (const u8 *)s; *c && n < 250;) {
        u32 ch = *c++;
        if (ch >= 0xf0 && c[0] && c[1] && c[2]) { ch = (ch & 7) << 18 | (u32)(c[0] & 63) << 12 | (u32)(c[1] & 63) << 6 | (c[2] & 63); c += 3; }
        else if (ch >= 0xe0 && c[0] && c[1]) { ch = (ch & 15) << 12 | (u32)(c[0] & 63) << 6 | (c[1] & 63); c += 2; }
        else if (ch >= 0xc0 && c[0]) { ch = (ch & 31) << 6 | (c[0] & 63); c += 1; }
        if (ch >= 0x10000) { ch -= 0x10000; units[n++] = (u16)(0xd800 + (ch >> 10)); units[n++] = (u16)(0xdc00 + (ch & 0x3ff)); }
        else units[n++] = (u16)ch;
    }
    if (!n) { pbuf_u8(b, 0); return; }
    pbuf_u8(b, (u8)(n + 1));
    for (int i = 0; i < n; i++) pbuf_u16(b, units[i]);
    pbuf_u16(b, 0);
}

// --- transactions ---------------------------------------------------------------------------------------------

// Next bytes from the bulk IN endpoint (buffered: a transfer may run into the next container). 0 / -errno.
static int ptp_fill(Ptp *p, int timeout_ms) {
    if (p->roff < p->rlen) return 0;
    if (!p->rbuf) p->rbuf = xmalloc(PTP_RBUF);
    p->roff = p->rlen = 0;
    u64 until = now_ns() + (u64)timeout_ms * 1000000ull;
    for (;;) {
        int r = usb_read(p->usb, p->rbuf, PTP_RBUF, 2000);
        if (r < 0) return r;
        if (r > 0) { p->rlen = (size_t)r; return 0; }
        if (now_ns() > until) return -ETIMEDOUT;
    }
}

static int ptp_take(Ptp *p, u8 *dst, size_t n, int timeout_ms) { // exactly n bytes
    for (size_t o = 0; o < n;) {
        int r = ptp_fill(p, timeout_ms);
        if (r) return r;
        size_t k = MIN(n - o, p->rlen - p->roff);
        memcpy(dst + o, p->rbuf + p->roff, k);
        p->roff += k;
        o += k;
    }
    return 0;
}

typedef struct { const u8 *buf; size_t len; int fd; } PtpOut;        // data we send (fd >= 0: len bytes read from it)
typedef struct { u8 *buf; size_t len; int fd; u64 written; } PtpIn;  // data we get (fd >= 0: written to it, else buf)

// One transaction: command (+ data out) -> (data in) + response. Returns the response code, -1 on I/O errors.
static int ptp_transact(Ptp *p, u16 code, const u32 *params, int np, const PtpOut *out, PtpIn *in, u32 *rparams, int *nrp) {
    u8 cmd[32];
    if (in) { in->buf = NULL; in->len = 0; in->written = 0; }
    pthread_mutex_lock(&p->mtx);
    u32 tid = p->tid++;
    put_le32(cmd, 12 + 4 * (u32)np);
    put_le16(cmd + 4, PTP_CMD);
    put_le16(cmd + 6, code);
    put_le32(cmd + 8, tid);
    for (int i = 0; i < np; i++) put_le32(cmd + 12 + 4 * i, params[i]);
    int rc = usb_write(p->usb, cmd, 12 + 4 * np) < 0 ? -1 : 0;
    if (!rc && out) { // data phase out: container header + payload, one transfer
        u64 total = 12 + (u64)out->len;
        size_t chunk = 1u << 20;
        u8 *b = xmalloc(chunk + 12);
        put_le32(b, total > 0xffffffffu ? 0xffffffffu : (u32)total);
        put_le16(b + 4, PTP_DATA);
        put_le16(b + 6, code);
        put_le32(b + 8, tid);
        size_t first = MIN(out->len, chunk - 12); // header + data fill whole packets: no short packet mid-phase
        if (out->fd >= 0) { if (read(out->fd, b + 12, first) != (ssize_t)first) rc = -1; }
        else if (first) memcpy(b + 12, out->buf, first);
        if (!rc && usb_write_part(p->usb, b, (int)(12 + first), first == out->len, total) < 0) rc = -1;
        for (size_t o = first; !rc && o < out->len;) {
            size_t k = MIN(out->len - o, chunk);
            const u8 *src = out->buf + o;
            if (out->fd >= 0) { if (read(out->fd, b, k) != (ssize_t)k) { rc = -1; break; } src = b; }
            if (usb_write_part(p->usb, src, (int)k, o + k == out->len, total) < 0) rc = -1;
            o += k;
        }
        free(b);
    }
    int resp = -1;
    while (!rc) { // containers until the response
        u8 h[12];
        if (ptp_take(p, h, 12, 60000)) { rc = -1; break; }
        u32 len = get_le32(h);
        u16 type = get_le16(h + 4);
        if (type == PTP_DATA) {
            bool until_short = len == 0xffffffffu; // > 4 GB: up to the end of the transfer
            u64 left = until_short ? UINT64_MAX : (len >= 12 ? len - 12 : 0);
            size_t cap = 0;
            while (!rc && left) {
                if (until_short && p->roff >= p->rlen && p->rlen < PTP_RBUF) break;
                if (ptp_fill(p, 60000)) { rc = -1; break; }
                size_t k = (size_t)MIN(left, (u64)(p->rlen - p->roff));
                const u8 *src = p->rbuf + p->roff;
                if (in && in->fd >= 0) { if (write(in->fd, src, k) != (ssize_t)k) rc = -1; in->written += k; }
                else if (in) {
                    if (in->len + k + 1 > cap) { cap = (in->len + k + 1) * 2; in->buf = xrealloc(in->buf, cap); }
                    memcpy(in->buf + in->len, src, k);
                    in->len += k;
                }
                p->roff += k;
                left -= k;
            }
            continue;
        }
        if (type != PTP_RESP || len < 12 || len > 40) { rc = -1; break; }
        u8 pr[28];
        if (len > 12 && ptp_take(p, pr, len - 12, 60000)) { rc = -1; break; }
        int n = (int)(len - 12) / 4;
        if (rparams) for (int i = 0; i < n && i < 5; i++) rparams[i] = get_le32(pr + 4 * i);
        if (nrp) *nrp = n;
        resp = get_le16(h + 6);
        break;
    }
    if (rc) p->rlen = p->roff = 0; // out of step: drop what's buffered
    pthread_mutex_unlock(&p->mtx);
    if (rc && in) { free(in->buf); in->buf = NULL; in->len = 0; }
    return rc ? -1 : resp;
}

static int ptp_errno(int resp) {
    switch (resp) {
    case PTP_OK: return 0;
    case PTP_INVALID_HANDLE: return ENOENT;
    case PTP_STORE_FULL: return ENOSPC;
    case PTP_STORE_READ_ONLY: case PTP_ACCESS_DENIED: return EACCES;
    case PTP_NOT_SUPPORTED: return ENOTSUP;
    case PTP_DEVICE_BUSY: return EBUSY;
    default: return EIO;
    }
}

static int ptp_simple(Ptp *p, u16 code, const u32 *params, int np) { return ptp_transact(p, code, params, np, NULL, NULL, NULL, NULL); }

// --- session and device -----------------------------------------------------------------------------------------

static bool ptp_open(Usb *usb, Ptp *p, char *err, size_t ecap) {
    memset(p, 0, sizeof *p);
    p->usb = usb;
    pthread_mutex_init(&p->mtx, NULL);
    PtpIn in = {.fd = -1};
    int r = ptp_transact(p, PTP_GET_DEVICE_INFO, NULL, 0, NULL, &in, NULL, NULL);
    if (r != PTP_OK) { snprintf(err, ecap, "io: the device didn't answer (%s)", r < 0 ? "no reply" : "device info"); return false; }
    PtpCur c = {in.buf, in.len, 0, false};
    pc_u16(&c);                // standard version
    u32 vendor_ext = pc_u32(&c);
    pc_u16(&c);
    char ext[256];
    pc_str(&c, ext, sizeof ext);
    pc_u16(&c);                // functional mode
    size_t at;
    u32 nops = pc_array(&c, 2, &at);
    if (!c.bad) {
        p->ops = xmalloc((nops ? nops : 1) * sizeof *p->ops);
        for (u32 i = 0; i < nops; i++) p->ops[i] = get_le16(c.d + at + 2 * i);
        p->nops = (int)nops;
    }
    pc_array(&c, 2, &at);      // events
    pc_array(&c, 2, &at);      // device properties
    pc_array(&c, 2, &at);      // capture formats
    pc_array(&c, 2, &at);      // playback formats
    pc_str(&c, p->manufacturer, sizeof p->manufacturer);
    pc_str(&c, p->model, sizeof p->model);
    char ver[64];
    pc_str(&c, ver, sizeof ver);
    pc_str(&c, p->serial, sizeof p->serial);
    free(in.buf);
    p->mtp = vendor_ext == 6 || strstr(ext, "microsoft") || ptp_supports(p, MTP_GET_OBJECT_PROP_LIST);
    p->tid = 0;
    u32 sid = 1;
    r = ptp_simple(p, PTP_OPEN_SESSION, &sid, 1);
    if (r == PTP_SESSION_ALREADY_OPEN) { // left open by someone else: start over
        ptp_simple(p, PTP_CLOSE_SESSION, NULL, 0);
        p->tid = 0; // OpenSession always goes with transaction 0
        r = ptp_simple(p, PTP_OPEN_SESSION, &sid, 1);
        if (r == PTP_SESSION_ALREADY_OPEN) r = PTP_OK;
    }
    if (r != PTP_OK) { snprintf(err, ecap, "%s: the device didn't open a session (0x%04x)", r == PTP_DEVICE_BUSY ? "busy" : "io", r < 0 ? 0 : r); return false; }
    return true;
}

static void ptp_close(Ptp *p) {
    if (p->usb) ptp_simple(p, PTP_CLOSE_SESSION, NULL, 0);
    free(p->ops);
    free(p->rbuf);
    pthread_mutex_destroy(&p->mtx);
}

typedef struct { u32 id; u64 total, avail; char name[128]; bool ro; } PtpStorage;

// Storages that are available (an Android phone that's locked lists none). Returns the count or -errno.
static int ptp_storages(Ptp *p, PtpStorage *out, int max) {
    PtpIn in = {.fd = -1};
    int r = ptp_transact(p, PTP_GET_STORAGE_IDS, NULL, 0, NULL, &in, NULL, NULL);
    if (r != PTP_OK) return -ptp_errno(r);
    PtpCur c = {in.buf, in.len, 0, false};
    size_t at;
    u32 n = pc_array(&c, 4, &at);
    int k = 0;
    for (u32 i = 0; i < n && k < max; i++) {
        u32 id = get_le32(in.buf + at + 4 * i);
        if ((id & 0xffff) == 0) continue; // no logical storage (a card slot without a card)
        PtpIn si = {.fd = -1};
        if (ptp_transact(p, PTP_GET_STORAGE_INFO, &id, 1, NULL, &si, NULL, NULL) != PTP_OK) continue;
        PtpCur s = {si.buf, si.len, 0, false};
        pc_u16(&s); pc_u16(&s);
        u16 access = pc_u16(&s);
        PtpStorage *st = &out[k];
        memset(st, 0, sizeof *st);
        st->id = id;
        st->total = pc_u64(&s);
        st->avail = pc_u64(&s);
        pc_u32(&s);
        pc_str(&s, st->name, sizeof st->name);
        char label[128];
        pc_str(&s, label, sizeof label);
        if (!st->name[0]) snprintf(st->name, sizeof st->name, "%s", label[0] ? label : "Storage");
        st->ro = access == 1 || access == 2;
        free(si.buf);
        if (!s.bad) k++;
    }
    free(in.buf);
    return k;
}

// "20240131T235959" (".5", "Z" or "+0100" may follow) -> time.
static time_t ptp_date(const char *s) {
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    if (sscanf(s, "%4d%2d%2dT%2d%2d%2d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) < 3) return 0;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = -1;
    return strchr(s, 'Z') ? timegm(&tm) : mktime(&tm);
}

static void ptp_date_str(time_t t, char *out, size_t cap) {
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, cap, "%Y%m%dT%H%M%S", &tm);
}

static bool ptp_parse_info(const u8 *d, size_t n, u32 handle, PtpObj *o) {
    PtpCur c = {d, n, 0, false};
    memset(o, 0, sizeof *o);
    o->handle = handle;
    o->storage = pc_u32(&c);
    o->format = pc_u16(&c);
    pc_u16(&c);
    o->size = pc_u32(&c);
    pc_skip(&c, 2 + 4 + 4 + 4 + 4 + 4 + 4);
    o->parent = pc_u32(&c);
    pc_skip(&c, 2 + 4 + 4);
    pc_str(&c, o->name, sizeof o->name);
    char cd[64], md[64];
    pc_str(&c, cd, sizeof cd);
    pc_str(&c, md, sizeof md);
    o->mtime = ptp_date(md[0] ? md : cd);
    return !c.bad || o->name[0];
}

static int ptp_object_info(Ptp *p, u32 handle, PtpObj *o) {
    PtpIn in = {.fd = -1};
    int r = ptp_transact(p, PTP_GET_OBJECT_INFO, &handle, 1, NULL, &in, NULL, NULL);
    if (r != PTP_OK) return -ptp_errno(r);
    bool ok = ptp_parse_info(in.buf, in.len, handle, o);
    free(in.buf);
    return ok ? 0 : -EIO;
}

// GetObjectPropList for a folder's children: name, size, format, date, parent for each, in one request. Fills
// the objects whose handles are listed (handles not described stay unfilled). false: not usable here.
static bool ptp_proplist(Ptp *p, u32 parent, PtpObj *objs, int n) {
    u32 prm[5] = {parent, 0, 0xffffffffu, 0, 1};
    PtpIn in = {.fd = -1};
    if (ptp_transact(p, MTP_GET_OBJECT_PROP_LIST, prm, 5, NULL, &in, NULL, NULL) != PTP_OK) return false;
    PtpCur c = {in.buf, in.len, 0, false};
    u32 count = pc_u32(&c);
    int idx = -1;
    u32 last = 0;
    bool consistent = true;
    for (u32 i = 0; i < count && !c.bad; i++) {
        u32 h = pc_u32(&c);
        u16 prop = pc_u16(&c), type = pc_u16(&c);
        if (h != last || idx < 0) { // objects come in runs: find this one
            last = h;
            idx = -1;
            for (int k = 0; k < n; k++) if (objs[k].handle == h) { idx = k; break; }
        }
        u64 v = 0;
        char str[256] = "";
        switch (type) {
        case 0x0001: case 0x0002: v = pc_u8(&c); break;
        case 0x0003: case 0x0004: v = pc_u16(&c); break;
        case 0x0005: case 0x0006: v = pc_u32(&c); break;
        case 0x0007: case 0x0008: v = pc_u64(&c); break;
        case 0x0009: case 0x000A: v = pc_u64(&c); pc_u64(&c); break;
        case 0xFFFF: pc_str(&c, str, sizeof str); break;
        default:
            if (type & 0x4000) { // arrays: element size from the element type
                u16 et = type & 0xff;
                size_t es = et <= 2 ? 1 : et <= 4 ? 2 : et <= 6 ? 4 : et <= 8 ? 8 : 16;
                size_t at;
                pc_array(&c, es, &at);
            } else c.bad = true;
        }
        if (idx < 0) continue;
        PtpObj *o = &objs[idx];
        switch (prop) {
        case MTP_PROP_STORAGE: o->storage = (u32)v; break;
        case MTP_PROP_FORMAT: o->format = (u16)v; break;
        case MTP_PROP_SIZE: o->size = v; break;
        case MTP_PROP_NAME: snprintf(o->name, sizeof o->name, "%s", str); break;
        case MTP_PROP_MODIFIED: o->mtime = ptp_date(str); break;
        case MTP_PROP_PARENT: o->parent = (u32)v; if (v != parent) consistent = false; break;
        }
    }
    bool bad = c.bad || !consistent;
    free(in.buf);
    return !bad;
}

// A folder's objects (parent PTP_ROOT: the storage's top level). Returns the count (*out malloc'd) or -errno.
static int ptp_list(Ptp *p, u32 storage, u32 parent, PtpObj **out) {
    *out = NULL;
    u32 prm[3] = {storage, 0, parent};
    PtpIn in = {.fd = -1};
    int r = ptp_transact(p, PTP_GET_OBJECT_HANDLES, prm, 3, NULL, &in, NULL, NULL);
    if (r != PTP_OK) return -ptp_errno(r);
    PtpCur c = {in.buf, in.len, 0, false};
    size_t at;
    u32 n = pc_array(&c, 4, &at);
    PtpObj *objs = xcalloc(n ? n : 1, sizeof *objs);
    for (u32 i = 0; i < n; i++) objs[i].handle = get_le32(in.buf + at + 4 * i);
    free(in.buf);
    bool fast = n > 1 && parent != PTP_ROOT && p->mtp && !p->proplist_bad && ptp_supports(p, MTP_GET_OBJECT_PROP_LIST);
    if (fast && !ptp_proplist(p, parent, objs, (int)n)) {
        p->proplist_bad = true;
        for (u32 i = 0; i < n; i++) { u32 h = objs[i].handle; memset(&objs[i], 0, sizeof objs[i]); objs[i].handle = h; }
    }
    int k = 0;
    for (u32 i = 0; i < n; i++) {
        PtpObj o = objs[i];
        if (!o.name[0] && ptp_object_info(p, o.handle, &o)) continue; // gone meanwhile
        if (!o.name[0]) continue;
        if (!o.storage) o.storage = storage;
        objs[k++] = o;
    }
    *out = objs;
    return k;
}

// Read len bytes at off (partial reads; a plain PTP device without them: the whole object into fd_whole).
static ssize_t ptp_read(Ptp *p, u32 handle, u64 off, void *buf, size_t len) {
    PtpIn in = {.fd = -1};
    int r;
    u32 got[5] = {0};
    if (ptp_supports(p, MTP_GET_PARTIAL_OBJECT_64)) {
        u32 prm[4] = {handle, (u32)off, (u32)(off >> 32), (u32)len};
        r = ptp_transact(p, MTP_GET_PARTIAL_OBJECT_64, prm, 4, NULL, &in, got, NULL);
    } else if (ptp_supports(p, PTP_GET_PARTIAL_OBJECT) && off <= 0xffffffffu) {
        u32 prm[3] = {handle, (u32)off, (u32)len};
        r = ptp_transact(p, PTP_GET_PARTIAL_OBJECT, prm, 3, NULL, &in, got, NULL);
    } else return -ENOTSUP;
    if (r != PTP_OK) return -ptp_errno(r);
    size_t n = MIN(in.len, len);
    memcpy(buf, in.buf, n);
    free(in.buf);
    return (ssize_t)n;
}

// The whole object into fd.
static int ptp_get_object(Ptp *p, u32 handle, int fd) {
    PtpIn in = {.fd = fd};
    int r = ptp_transact(p, PTP_GET_OBJECT, &handle, 1, NULL, &in, NULL, NULL);
    return r == PTP_OK ? 0 : -ptp_errno(r);
}

// Create a file (size bytes from fd, or a folder when fd < 0) in parent. Returns its handle via *handle.
static int ptp_create(Ptp *p, u32 storage, u32 parent, const char *name, bool folder, int fd, u64 size, time_t mtime, u32 *handle) {
    PtpBuf b = {0};
    pbuf_u32(&b, storage);
    pbuf_u16(&b, folder ? PTP_FMT_FOLDER : 0x3000);
    pbuf_u16(&b, 0);
    pbuf_u32(&b, size > 0xffffffffu ? 0xffffffffu : (u32)size);
    pbuf_u16(&b, 0); pbuf_u32(&b, 0); pbuf_u32(&b, 0); pbuf_u32(&b, 0); pbuf_u32(&b, 0); pbuf_u32(&b, 0); pbuf_u32(&b, 0);
    pbuf_u32(&b, parent);
    pbuf_u16(&b, folder ? 1 : 0); // association: generic folder
    pbuf_u32(&b, 0);
    pbuf_u32(&b, 0);
    pbuf_str(&b, name);
    char ds[32];
    ptp_date_str(mtime ? mtime : time(NULL), ds, sizeof ds);
    pbuf_str(&b, ds);
    pbuf_str(&b, ds);
    pbuf_str(&b, "");
    u32 prm[2] = {storage, parent}, rp[5] = {0};
    int nr = 0;
    PtpOut out = {b.b, b.n, -1};
    int r = ptp_transact(p, PTP_SEND_OBJECT_INFO, prm, 2, &out, NULL, rp, &nr);
    free(b.b);
    if (r != PTP_OK) return -ptp_errno(r);
    *handle = nr >= 3 ? rp[2] : 0;
    if (folder) return 0;
    PtpOut data = {NULL, (size_t)size, fd};
    if (fd >= 0) lseek(fd, 0, SEEK_SET);
    r = ptp_transact(p, PTP_SEND_OBJECT, NULL, 0, &data, NULL, NULL, NULL);
    return r == PTP_OK ? 0 : -ptp_errno(r);
}

static int ptp_delete(Ptp *p, u32 handle) {
    u32 prm[2] = {handle, 0};
    return -ptp_errno(ptp_simple(p, PTP_DELETE_OBJECT, prm, 2));
}

static int ptp_rename(Ptp *p, u32 handle, const char *name) {
    if (!ptp_supports(p, MTP_SET_OBJECT_PROP_VALUE)) return -ENOTSUP;
    PtpBuf b = {0};
    pbuf_str(&b, name);
    u32 prm[2] = {handle, MTP_PROP_NAME};
    PtpOut out = {b.b, b.n, -1};
    int r = ptp_transact(p, MTP_SET_OBJECT_PROP_VALUE, prm, 2, &out, NULL, NULL, NULL);
    free(b.b);
    return -ptp_errno(r);
}

static int ptp_move(Ptp *p, u32 handle, u32 storage, u32 parent) {
    if (!ptp_supports(p, PTP_MOVE_OBJECT)) return -EXDEV; // the caller copies instead
    u32 prm[3] = {handle, storage, parent == PTP_ROOT ? 0 : parent}; // (MoveObject: 0 is the storage's top)
    int r = ptp_simple(p, PTP_MOVE_OBJECT, prm, 3);
    return r == PTP_NOT_SUPPORTED ? -EXDEV : -ptp_errno(r);
}
