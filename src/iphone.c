// iphone.c - talking to iPhones / iPads ourselves (no usbmuxd, no libimobiledevice): usbmux and the small TCP it
// carries over USB, lockdown (plist requests, the TLS session made with a pairing record, "Trust this computer"
// pairing with certificates made here), and the services for files: AFC (the media area), house_arrest (an app's
// Documents) and installation_proxy (which apps share files).

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <openssl/rand.h>
#include <openssl/evp.h>

static void put_be16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void put_be32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u16 get_be16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static u32 get_be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static void put_le64(u8 *p, u64 v) { for (int i = 0; i < 8; i++) p[i] = (u8)(v >> (8 * i)); }
static u64 get_le64(const u8 *p) { u64 v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }

// Wait on a condition until an absolute CLOCK_REALTIME deadline ms from now.
static struct timespec deadline_in(int ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    return ts;
}

// --- usbmux: packets over the bulk endpoints, TCP-like connections to the device's ports --------------------

enum { MUX_VERSION = 0, MUX_CONTROL = 1, MUX_SETUP = 2, MUX_TCP = 6 };
enum { TH_FIN = 1, TH_SYN = 2, TH_RST = 4, TH_PUSH = 8, TH_ACK = 16 };
#define MUX_MAX_CONNS 32
#define MUX_INBUF (2u << 20) // what a connection buffers before the device has to wait (the window we offer)
#define MUX_CHUNK 32768      // payload per packet

typedef struct Mux Mux;
typedef struct {
    Mux *m;
    u16 sport, dport;
    u32 tx_seq;   // next byte we send
    u32 tx_ack;   // next byte we expect (what we acknowledge)
    u32 tx_acked; // what the device acknowledged
    u32 tx_win;   // the device's window (bytes)
    int state;    // 0 connecting, 1 open, 2 closed (refused / reset)
    u8 *in;
    size_t in_len, in_off, in_cap;
} MuxConn;

struct Mux {
    Usb *usb;
    pthread_mutex_t mtx;  // connection state
    pthread_mutex_t txm;  // one packet goes out at a time (taken before mtx)
    pthread_cond_t cond;
    pthread_t rx;
    bool rx_started;
    _Atomic bool stop, dead;
    int version;          // negotiated protocol version (2 on current devices)
    u16 tx_seq, rx_seq;
    u16 next_port;
    MuxConn *conns[MUX_MAX_CONNS];
    u8 *pkt;              // received bytes not yet a whole packet
    size_t pkt_len, pkt_cap;
};

// Send one packet: mux header, optional TCP header, payload. Caller holds txm.
static int mux_send_packet(Mux *m, int proto, const u8 *th, const void *data, size_t len) {
    size_t hl = m->version >= 2 ? 16 : 8, tl = th ? 20 : 0, total = hl + tl + len;
    u8 *b = xmalloc(total);
    put_be32(b, (u32)proto);
    put_be32(b + 4, (u32)total);
    if (hl == 16) {
        put_be32(b + 8, 0xfeedface);
        if (proto == MUX_SETUP) { m->tx_seq = 0; m->rx_seq = 0xffff; }
        put_be16(b + 12, m->tx_seq);
        put_be16(b + 14, m->rx_seq);
        m->tx_seq++;
    }
    if (th) memcpy(b + hl, th, 20);
    if (len) memcpy(b + hl + tl, data, len);
    if (getenv("FE_DEVICE_PKT"))
        dev_log("-> proto %d len %zu mux tx %u rx %u%s", proto, total, hl == 16 ? get_be16(b + 12) : 0, hl == 16 ? get_be16(b + 14) : 0, "");
    if (th && getenv("FE_DEVICE_PKT"))
        dev_log("   tcp %u->%u seq %u ack %u flags %02x win %u data %zu", get_be16(th), get_be16(th + 2), get_be32(th + 4), get_be32(th + 8), th[13], get_be16(th + 14), len);
    int r = usb_write(m->usb, b, (int)total);
    free(b);
    if (r < 0) { dev_log("usbmux: USB write failed: %s", strerror(-r)); atomic_store(&m->dead, true); }
    return r < 0 ? -1 : 0;
}

// TCP header for c (caller holds mtx): our sequence / acknowledgement and the room left in our buffer.
static void mux_tcp_hdr(u8 *th, const MuxConn *c, u8 flags) {
    memset(th, 0, 20);
    put_be16(th, c->sport);
    put_be16(th + 2, c->dport);
    put_be32(th + 4, c->tx_seq);
    put_be32(th + 8, c->tx_ack);
    th[12] = 5 << 4;
    th[13] = flags;
    size_t held = c->in_len - c->in_off, room = held < MUX_INBUF ? MUX_INBUF - held : 0;
    put_be16(th + 14, (u16)MIN(room / 256, 0xffff));
}

// A TCP packet on c (flags, optional payload); seq advances by the payload. Takes txm + mtx.
static int mux_send_tcp(MuxConn *c, u8 flags, const void *data, size_t len) {
    Mux *m = c->m;
    u8 th[20];
    pthread_mutex_lock(&m->txm);
    pthread_mutex_lock(&m->mtx);
    mux_tcp_hdr(th, c, flags);
    c->tx_seq += (u32)len;
    pthread_mutex_unlock(&m->mtx);
    int r = mux_send_packet(m, MUX_TCP, th, data, len);
    pthread_mutex_unlock(&m->txm);
    return r;
}

static MuxConn *mux_find(Mux *m, u16 our_port) {
    for (int i = 0; i < MUX_MAX_CONNS; i++)
        if (m->conns[i] && m->conns[i]->sport == our_port) return m->conns[i];
    return NULL;
}

// One whole packet from the device.
static void mux_handle(Mux *m, const u8 *p, size_t total) {
    size_t hl = m->version >= 2 ? 16 : 8;
    if (total < hl) return;
    u32 proto = get_be32(p);
    if (hl == 16) { // (what usbmuxd does; read under txm when sending)
        pthread_mutex_lock(&m->txm);
        m->rx_seq = get_be16(p + 14);
        pthread_mutex_unlock(&m->txm);
    }
    const u8 *pl = p + hl;
    size_t pll = total - hl;
    if (proto != MUX_TCP || pll < 20) { // control packets: device log messages
        if (getenv("FE_DEVICE_PKT") || proto == MUX_CONTROL) {
            char t[200];
            size_t k = 0;
            for (size_t i = proto == MUX_CONTROL ? 1 : 0; i < pll && k + 4 < sizeof t; i++) k += (size_t)snprintf(t + k, sizeof t - k, (pl[i] >= 32 && pl[i] < 127) ? "%c" : "\\x%02x", pl[i]);
            t[k] = 0;
            dev_log("usbmux: device sent protocol %u (%zu bytes) type %u: %s", proto, pll, pll ? pl[0] : 0, t);
        }
        return;
    }
    u16 dport = get_be16(pl + 2);
    u32 seq = get_be32(pl + 4), ack = get_be32(pl + 8);
    size_t off = (size_t)(pl[12] >> 4) * 4;
    u8 flags = pl[13];
    u16 win = get_be16(pl + 14);
    if (off < 20 || off > pll) return;
    if (getenv("FE_DEVICE_PKT"))
        dev_log("<- tcp %u->%u seq %u ack %u flags %02x win %u data %zu (mux tx %u rx %u)", get_be16(pl), dport, seq, ack, flags, win, pll - off,
                hl == 16 ? get_be16(p + 12) : 0, hl == 16 ? get_be16(p + 14) : 0);
    pthread_mutex_lock(&m->txm); // (a connection is only freed under txm: it stays valid while its ACK goes out)
    pthread_mutex_lock(&m->mtx);
    MuxConn *c = mux_find(m, dport);
    bool send_ack = false;
    if (c) {
        c->tx_win = (u32)win << 8;
        if (flags & TH_RST) { if (c->state == 1) dev_log("usbmux: the device reset the connection to port %u", c->dport); c->state = 2; }
        else if (c->state == 0) {
            if ((flags & (TH_SYN | TH_ACK)) == (TH_SYN | TH_ACK)) {
                c->tx_seq++;
                c->tx_ack = seq + 1;
                c->tx_acked = ack;
                c->state = 1;
                send_ack = true;
            } else c->state = 2; // anything else: refused
        } else if (c->state == 1) {
            c->tx_acked = ack;
            size_t dl = pll - off;
            if (dl && seq != c->tx_ack) { // bytes went missing before these: this connection can't continue
                dev_log("usbmux: %u bytes lost on the connection to port %u: closing it", seq - c->tx_ack, c->dport);
                c->state = 2;
                dl = 0;
            }
            if (dl) {
                if (c->in_off && c->in_off == c->in_len) c->in_off = c->in_len = 0;
                if (c->in_len + dl > c->in_cap) {
                    if (c->in_off) { memmove(c->in, c->in + c->in_off, c->in_len - c->in_off); c->in_len -= c->in_off; c->in_off = 0; }
                    if (c->in_len + dl > c->in_cap) { c->in_cap = (c->in_len + dl) * 2; c->in = xrealloc(c->in, c->in_cap); }
                }
                memcpy(c->in + c->in_len, pl + off, dl);
                c->in_len += dl;
                c->tx_ack += (u32)dl;
                send_ack = true;
            }
            if (flags & TH_FIN) { dev_log("usbmux: the device closed the connection to port %u", c->dport); c->state = 2; }
        }
        pthread_cond_broadcast(&m->cond);
    }
    u8 th[20];
    if (send_ack) mux_tcp_hdr(th, c, TH_ACK);
    pthread_mutex_unlock(&m->mtx);
    if (send_ack) mux_send_packet(m, MUX_TCP, th, NULL, 0);
    pthread_mutex_unlock(&m->txm);
}

// Bytes from the bulk IN endpoint: split into packets (a packet may span transfers, a transfer may hold several).
// false: what's buffered doesn't start with a packet header (the caller drops it; the device begins each packet
// in a new transfer, so the next one is in step again).
static bool mux_input(Mux *m, const u8 *d, size_t n) {
    if (m->pkt_len + n > m->pkt_cap) { m->pkt_cap = (m->pkt_len + n) * 2; m->pkt = xrealloc(m->pkt, m->pkt_cap); }
    memcpy(m->pkt + m->pkt_len, d, n);
    m->pkt_len += n;
    size_t hl = m->version >= 2 ? 16 : 8, o = 0;
    while (m->pkt_len - o >= hl) {
        u32 total = get_be32(m->pkt + o + 4);
        if (total < hl || total > (8u << 20)) return false; // lost sync
        if (m->pkt_len - o < total) break;
        mux_handle(m, m->pkt + o, total);
        o += total;
    }
    memmove(m->pkt, m->pkt + o, m->pkt_len - o);
    m->pkt_len -= o;
    return true;
}

static void *mux_rx_main(void *arg) {
    Mux *m = arg;
    u8 *buf = xmalloc(65536);
    while (!atomic_load(&m->stop)) {
        int r = usb_read(m->usb, buf, 65536, 500);
        if (r < 0 && r != -EINTR && r != -ETIMEDOUT) { dev_log("usbmux: USB read failed: %s", strerror(-r)); break; }
        if (r > 0 && !mux_input(m, buf, (size_t)r)) { // garbled (data lost on the wire): start again at the next transfer
            dev_log("usbmux: garbled data dropped (%zu bytes)", m->pkt_len);
            m->pkt_len = 0;
        }
    }
    free(buf);
    atomic_store(&m->dead, true);
    pthread_mutex_lock(&m->mtx);
    pthread_cond_broadcast(&m->cond);
    pthread_mutex_unlock(&m->mtx);
    return NULL;
}

// Negotiate the protocol version and start receiving. NULL (err set) if the device doesn't answer.
static Mux *mux_open(Usb *usb, char *err, size_t ecap) {
    Mux *m = xcalloc(1, sizeof *m);
    m->usb = usb;
    // our connections' port numbers: away from the low ones usbmuxd uses (it counts from 1), so a connection it left
    // open on the device before we took over isn't mistaken for one of ours
    m->next_port = (u16)(20000 + (u32)(getpid() * 7919u + (u32)now_ns()) % 30000u);
    pthread_mutex_init(&m->mtx, NULL);
    pthread_mutex_init(&m->txm, NULL);
    pthread_cond_init(&m->cond, NULL);
    u8 vh[12] = {0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0}; // major 2, minor 0
    pthread_mutex_lock(&m->txm);
    int sr = mux_send_packet(m, MUX_VERSION, NULL, vh, sizeof vh);
    pthread_mutex_unlock(&m->txm);
    u8 *buf = xmalloc(65536);
    u32 major = 0;
    u64 until = now_ns() + 5000000000ull;
    while (sr == 0 && !major && now_ns() < until) { // the answer has an 8-byte header (no version yet)
        int r = usb_read(usb, buf, 65536, 500);
        if (r < 0) break;
        if (r == 0) continue;
        if (m->pkt_len + (size_t)r > m->pkt_cap) { m->pkt_cap = (m->pkt_len + (size_t)r) * 2; m->pkt = xrealloc(m->pkt, m->pkt_cap); }
        memcpy(m->pkt + m->pkt_len, buf, (size_t)r);
        m->pkt_len += (size_t)r;
        while (m->pkt_len >= 8 && !major) {
            u32 total = get_be32(m->pkt + 4);
            if (total < 8 || m->pkt_len < total) break;
            if (get_be32(m->pkt) == MUX_VERSION && total >= 12) major = get_be32(m->pkt + 8);
            memmove(m->pkt, m->pkt + total, m->pkt_len - total);
            m->pkt_len -= total;
        }
    }
    free(buf);
    if (!major) {
        snprintf(err, ecap, "io: the device didn't answer over USB");
        pthread_mutex_destroy(&m->mtx); pthread_mutex_destroy(&m->txm); pthread_cond_destroy(&m->cond);
        free(m->pkt);
        free(m);
        return NULL;
    }
    m->version = major >= 2 ? 2 : 1;
    dev_log("usbmux: version %u", major);
    if (m->version >= 2) {
        pthread_mutex_lock(&m->txm);
        mux_send_packet(m, MUX_SETUP, NULL, "\x07", 1);
        pthread_mutex_unlock(&m->txm);
    }
    m->pkt_len = 0;
    m->rx_started = pthread_create(&m->rx, NULL, mux_rx_main, m) == 0;
    return m;
}

static void mux_conn_free(MuxConn *c) { if (c) { free(c->in); free(c); } }

// Open a connection to a port on the device. NULL: refused, timed out or the device is gone.
static MuxConn *mux_connect(Mux *m, u16 port, int timeout_ms) {
    if (atomic_load(&m->dead)) return NULL;
    MuxConn *c = xcalloc(1, sizeof *c);
    c->m = m;
    c->dport = port;
    c->tx_win = 131072;
    u8 th[20];
    pthread_mutex_lock(&m->txm);
    pthread_mutex_lock(&m->mtx);
    int slot = -1;
    for (int i = 0; i < MUX_MAX_CONNS && slot < 0; i++) if (!m->conns[i]) slot = i;
    if (slot < 0) { pthread_mutex_unlock(&m->mtx); pthread_mutex_unlock(&m->txm); free(c); return NULL; }
    do { c->sport = m->next_port++; if (!m->next_port) m->next_port = 1; } while (mux_find(m, c->sport));
    m->conns[slot] = c;
    mux_tcp_hdr(th, c, TH_SYN);
    pthread_mutex_unlock(&m->mtx);
    mux_send_packet(m, MUX_TCP, th, NULL, 0);
    pthread_mutex_unlock(&m->txm);
    struct timespec dl = deadline_in(timeout_ms);
    pthread_mutex_lock(&m->mtx);
    while (c->state == 0 && !atomic_load(&m->dead))
        if (pthread_cond_timedwait(&m->cond, &m->mtx, &dl) == ETIMEDOUT) break;
    bool ok = c->state == 1;
    int st = c->state;
    pthread_mutex_unlock(&m->mtx);
    if (!ok) dev_log("usbmux: connecting to port %u: %s", port, atomic_load(&m->dead) ? "the device is gone" : st == 2 ? "refused" : "no answer");
    if (ok) return c;
    pthread_mutex_lock(&m->txm);
    pthread_mutex_lock(&m->mtx);
    m->conns[slot] = NULL;
    pthread_mutex_unlock(&m->mtx);
    pthread_mutex_unlock(&m->txm);
    mux_conn_free(c);
    return NULL;
}

static void mux_close(MuxConn *c) {
    if (!c) return;
    Mux *m = c->m;
    pthread_mutex_lock(&m->txm);
    pthread_mutex_lock(&m->mtx);
    u8 th[20];
    bool rst = c->state == 1 && !atomic_load(&m->dead);
    if (rst) mux_tcp_hdr(th, c, TH_RST);
    for (int i = 0; i < MUX_MAX_CONNS; i++) if (m->conns[i] == c) m->conns[i] = NULL;
    pthread_mutex_unlock(&m->mtx);
    if (rst) mux_send_packet(m, MUX_TCP, th, NULL, 0);
    pthread_mutex_unlock(&m->txm);
    mux_conn_free(c);
}

// Send all of data, keeping inside the device's window. 0 ok, -1 closed / gone / stuck.
static int mux_send(MuxConn *c, const void *data, size_t len) {
    Mux *m = c->m;
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = MIN(len - sent, (size_t)MUX_CHUNK);
        struct timespec dl = deadline_in(30000);
        pthread_mutex_lock(&m->mtx);
        for (;;) {
            if (c->state != 1 || atomic_load(&m->dead)) { pthread_mutex_unlock(&m->mtx); return -1; }
            u32 inflight = c->tx_seq - c->tx_acked;
            if (inflight < c->tx_win) { chunk = MIN(chunk, (size_t)(c->tx_win - inflight)); break; }
            if (pthread_cond_timedwait(&m->cond, &m->mtx, &dl) == ETIMEDOUT) { pthread_mutex_unlock(&m->mtx); return -1; }
        }
        pthread_mutex_unlock(&m->mtx);
        if (mux_send_tcp(c, TH_ACK, (const u8 *)data + sent, chunk)) return -1;
        sent += chunk;
    }
    return 0;
}

// Up to cap bytes. >0 bytes, 0 timed out, -1 closed / gone.
static int mux_recv(MuxConn *c, void *buf, size_t cap, int timeout_ms) {
    Mux *m = c->m;
    struct timespec dl = deadline_in(timeout_ms);
    pthread_mutex_lock(&m->mtx);
    while (c->in_len == c->in_off && c->state == 1 && !atomic_load(&m->dead))
        if (pthread_cond_timedwait(&m->cond, &m->mtx, &dl) == ETIMEDOUT) break;
    size_t avail = c->in_len - c->in_off;
    if (!avail) {
        int r = c->state == 1 && !atomic_load(&m->dead) ? 0 : -1;
        pthread_mutex_unlock(&m->mtx);
        return r;
    }
    bool was_full = avail > MUX_INBUF / 2;
    size_t n = MIN(avail, cap);
    memcpy(buf, c->in + c->in_off, n);
    c->in_off += n;
    if (c->in_off == c->in_len) c->in_off = c->in_len = 0;
    bool reopen = was_full && c->in_len - c->in_off <= MUX_INBUF / 2 && c->state == 1;
    pthread_mutex_unlock(&m->mtx);
    if (reopen) mux_send_tcp(c, TH_ACK, NULL, 0); // the window opened again: tell the device
    return (int)n;
}

static void mux_free(Mux *m) {
    if (!m) return;
    atomic_store(&m->stop, true);
    if (m->rx_started) pthread_join(m->rx, NULL);
    for (int i = 0; i < MUX_MAX_CONNS; i++) mux_conn_free(m->conns[i]);
    pthread_mutex_destroy(&m->mtx);
    pthread_mutex_destroy(&m->txm);
    pthread_cond_destroy(&m->cond);
    free(m->pkt);
    free(m);
}

// --- service connections: 32-bit length + plist, optionally inside TLS ------------------------------------

typedef struct {
    MuxConn *c;
    SSL_CTX *ctx;
    SSL *ssl;
} Svc;

static int mux_bio_write(BIO *b, const char *d, int n) { return mux_send(BIO_get_data(b), d, (size_t)n) == 0 ? n : -1; }
static int mux_bio_read(BIO *b, char *d, int n) {
    int r = mux_recv(BIO_get_data(b), d, (size_t)n, 30000);
    return r > 0 ? r : -1;
}
static long mux_bio_ctrl(BIO *b, int cmd, long num, void *ptr) { return cmd == BIO_CTRL_FLUSH ? 1 : 0; }

static BIO_METHOD *g_mux_bio;
static void mux_bio_init(void) {
    g_mux_bio = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "usbmux");
    BIO_meth_set_write(g_mux_bio, mux_bio_write);
    BIO_meth_set_read(g_mux_bio, mux_bio_read);
    BIO_meth_set_ctrl(g_mux_bio, mux_bio_ctrl);
}

static void ssl_err(const char *what, char *err, size_t ecap) {
    char e[256] = "";
    unsigned long code = ERR_get_error();
    if (code) ERR_error_string_n(code, e, sizeof e);
    snprintf(err, ecap, "io: %s%s%s", what, e[0] ? ": " : "", e);
    ERR_clear_error();
}

// Switch the connection to TLS, our host certificate as the client's. (Old iOS versions use old TLS: allowed.)
static bool svc_start_tls(Svc *s, X509 *cert, EVP_PKEY *key, char *err, size_t ecap) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, mux_bio_init);
    s->ctx = SSL_CTX_new(TLS_client_method());
    if (!s->ctx) { ssl_err("TLS setup", err, ecap); return false; }
    SSL_CTX_set_security_level(s->ctx, 0);
    SSL_CTX_set_min_proto_version(s->ctx, TLS1_VERSION);
    SSL_CTX_set_cipher_list(s->ctx, "ALL:!aNULL:!eNULL:@SECLEVEL=0");
    SSL_CTX_set_verify(s->ctx, SSL_VERIFY_NONE, NULL);
    SSL_CTX_set_options(s->ctx, SSL_OP_LEGACY_SERVER_CONNECT | SSL_OP_IGNORE_UNEXPECTED_EOF); // (as libimobiledevice)
    if (SSL_CTX_use_certificate(s->ctx, cert) != 1 || SSL_CTX_use_PrivateKey(s->ctx, key) != 1) {
        ssl_err("the pairing certificate", err, ecap);
        return false;
    }
    s->ssl = SSL_new(s->ctx);
    BIO *b = BIO_new(g_mux_bio);
    BIO_set_data(b, s->c);
    BIO_set_init(b, 1);
    SSL_set_bio(s->ssl, b, b);
    dev_log("tls: starting");
    if (SSL_connect(s->ssl) != 1) { ssl_err("TLS with the device failed", err, ecap); dev_log("%s", err); return false; }
    dev_log("tls: %s", SSL_get_version(s->ssl));
    return true;
}

static void svc_close(Svc *s) {
    if (s->ssl) { SSL_shutdown(s->ssl); SSL_free(s->ssl); }
    if (s->ctx) SSL_CTX_free(s->ctx);
    mux_close(s->c);
    memset(s, 0, sizeof *s);
}

static int svc_write(Svc *s, const void *d, size_t n) {
    if (!s->c) return -1; // (a connection that couldn't be made again)
    if (!s->ssl) return mux_send(s->c, d, n);
    for (size_t o = 0; o < n;) {
        int r = SSL_write(s->ssl, (const u8 *)d + o, (int)MIN(n - o, (size_t)1 << 20));
        if (r <= 0) return -1;
        o += (size_t)r;
    }
    return 0;
}

// Read exactly n bytes. 0 ok, -1 error / closed / timeout.
static int svc_read(Svc *s, void *d, size_t n, int timeout_ms) {
    if (!s->c) return -1;
    for (size_t o = 0; o < n;) {
        int r = s->ssl ? SSL_read(s->ssl, (u8 *)d + o, (int)MIN(n - o, (size_t)1 << 20)) : mux_recv(s->c, (u8 *)d + o, n - o, timeout_ms);
        if (r <= 0) return -1;
        o += (size_t)r;
    }
    return 0;
}

static int svc_send_plist(Svc *s, const Plist *p) {
    size_t n;
    char *x = pl_to_xml(p, &n);
    u8 *b = xmalloc(n + 4);
    put_be32(b, (u32)n);
    memcpy(b + 4, x, n);
    int r = svc_write(s, b, n + 4);
    free(b);
    free(x);
    return r;
}

static Plist *svc_recv_plist(Svc *s, int timeout_ms) {
    u8 h[4];
    if (svc_read(s, h, 4, timeout_ms)) return NULL;
    u32 n = get_be32(h);
    if (!n || n > (16u << 20)) return NULL;
    u8 *b = xmalloc(n);
    Plist *p = svc_read(s, b, n, timeout_ms) ? NULL : pl_parse(b, n);
    free(b);
    return p;
}

// --- pairing records ------------------------------------------------------------------------------------------

typedef struct {
    char host_id[64], system_buid[64];
    char *host_cert, *host_key, *root_cert, *root_key, *device_cert; // PEM
    u8 *escrow;
    size_t escrow_len;
    bool ours;        // kept in our data folder (not the system's /var/lib/lockdown)
} PairRec;

static void pair_free(PairRec *p) {
    free(p->host_cert); free(p->host_key); free(p->root_cert); free(p->root_key); free(p->device_cert); free(p->escrow);
    memset(p, 0, sizeof *p);
}

static void lockdown_dir(char *out, size_t cap) { // ours (tests: FE_LOCKDOWN_DIR)
    const char *ov = getenv("FE_LOCKDOWN_DIR");
    if (ov && ov[0]) { snprintf(out, cap, "%s", ov); return; }
    const char *x = getenv("XDG_DATA_HOME");
    if (x && x[0]) snprintf(out, cap, "%s/file-explorer/lockdown", x);
    else snprintf(out, cap, "%s/.local/share/file-explorer/lockdown", getenv("HOME") ? getenv("HOME") : "/tmp");
}

static const char *sys_lockdown_dir(void) { return getenv("FE_LOCKDOWN_DIR") ? "/nonexistent" : "/var/lib/lockdown"; }

static Plist *pl_load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 65536, n = 0;
    u8 *b = xmalloc(cap);
    for (size_t r; (r = fread(b + n, 1, cap - n, f)) > 0;) { n += r; if (n == cap) { cap *= 2; b = xrealloc(b, cap); } }
    fclose(f);
    Plist *p = pl_parse(b, n);
    free(b);
    return p;
}

static char *pl_get_pem(const Plist *d, const char *key) { // certificates are data (PEM text inside)
    Plist *v = pl_get(d, key);
    if (!v) return NULL;
    if (v->t == PL_STRING) return strdup(v->s);
    if (v->t != PL_DATA) return NULL;
    char *s = xmalloc(v->len + 1);
    memcpy(s, v->data, v->len);
    s[v->len] = 0;
    return s;
}

static bool pair_load_file(const char *path, PairRec *pr) {
    Plist *p = pl_load_file(path);
    if (!p) return false;
    memset(pr, 0, sizeof *pr);
    const char *hid = pl_get_str(p, "HostID"), *buid = pl_get_str(p, "SystemBUID");
    snprintf(pr->host_id, sizeof pr->host_id, "%s", hid ? hid : "");
    snprintf(pr->system_buid, sizeof pr->system_buid, "%s", buid ? buid : "");
    pr->host_cert = pl_get_pem(p, "HostCertificate");
    pr->host_key = pl_get_pem(p, "HostPrivateKey");
    pr->root_cert = pl_get_pem(p, "RootCertificate");
    pr->root_key = pl_get_pem(p, "RootPrivateKey");
    pr->device_cert = pl_get_pem(p, "DeviceCertificate");
    Plist *eb = pl_get(p, "EscrowBag");
    if (eb && eb->t == PL_DATA && eb->len) { pr->escrow = xmalloc(eb->len); memcpy(pr->escrow, eb->data, eb->len); pr->escrow_len = eb->len; }
    pl_free(p);
    if (!pr->host_id[0] || !pr->host_cert || !pr->host_key) { pair_free(pr); return false; }
    return true;
}

// The system's record (made by usbmuxd / idevicepair) if this user can read it, else ours.
static bool pair_load(const char *udid, PairRec *pr) {
    char p[PATH_MAX], d[PATH_MAX];
    snprintf(p, sizeof p, "%s/%s.plist", sys_lockdown_dir(), udid);
    if (pair_load_file(p, pr)) return true;
    lockdown_dir(d, sizeof d);
    snprintf(p, sizeof p, "%s/%s.plist", d, udid);
    if (!pair_load_file(p, pr)) return false;
    pr->ours = true;
    return true;
}

static void mkdir_parents(const char *dir) {
    char t[PATH_MAX];
    snprintf(t, sizeof t, "%s", dir);
    for (char *c = t + 1; *c; c++) if (*c == '/') { *c = 0; mkdir(t, 0700); *c = '/'; }
    mkdir(t, 0700);
}

static bool pl_save_file(const char *path, const Plist *p) {
    size_t n;
    char *x = pl_to_xml(p, &n);
    char tmp[PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    bool ok = fd >= 0 && write(fd, x, n) == (ssize_t)n;
    if (fd >= 0) close(fd);
    free(x);
    return ok && rename(tmp, path) == 0;
}

static bool pair_save(const char *udid, const PairRec *pr) {
    char d[PATH_MAX], p[PATH_MAX];
    lockdown_dir(d, sizeof d);
    mkdir_parents(d);
    snprintf(p, sizeof p, "%s/%s.plist", d, udid);
    Plist *r = pl_dict();
    pl_set(r, "HostID", pl_str(pr->host_id));
    pl_set(r, "SystemBUID", pl_str(pr->system_buid));
    pl_set(r, "HostCertificate", pl_data(pr->host_cert, strlen(pr->host_cert)));
    pl_set(r, "HostPrivateKey", pl_data(pr->host_key, strlen(pr->host_key)));
    pl_set(r, "RootCertificate", pl_data(pr->root_cert, strlen(pr->root_cert)));
    pl_set(r, "RootPrivateKey", pl_data(pr->root_key, strlen(pr->root_key)));
    pl_set(r, "DeviceCertificate", pl_data(pr->device_cert, strlen(pr->device_cert)));
    if (pr->escrow) pl_set(r, "EscrowBag", pl_data(pr->escrow, pr->escrow_len));
    bool ok = pl_save_file(p, r);
    pl_free(r);
    return ok;
}

static void uuid_gen(char *out, size_t cap) {
    u8 b[16];
    if (RAND_bytes(b, sizeof b) != 1) for (int i = 0; i < 16; i++) b[i] = (u8)rand();
    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;
    snprintf(out, cap, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", b[0], b[1], b[2], b[3], b[4], b[5],
             b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

// This computer's id towards Apple devices: the system's (usbmuxd) if there is one, else ours (made once).
static void system_buid(char *out, size_t cap) {
    char p[PATH_MAX], d[PATH_MAX];
    snprintf(p, sizeof p, "%s/SystemConfiguration.plist", sys_lockdown_dir());
    Plist *c = pl_load_file(p);
    if (!c) { lockdown_dir(d, sizeof d); snprintf(p, sizeof p, "%s/SystemConfiguration.plist", d); c = pl_load_file(p); }
    const char *b = pl_get_str(c, "SystemBUID");
    if (b) snprintf(out, cap, "%s", b);
    else {
        uuid_gen(out, cap);
        lockdown_dir(d, sizeof d);
        mkdir_parents(d);
        snprintf(p, sizeof p, "%s/SystemConfiguration.plist", d);
        Plist *n = pl_dict();
        pl_set(n, "SystemBUID", pl_str(out));
        pl_save_file(p, n);
        pl_free(n);
    }
    pl_free(c);
}

// --- certificates for pairing (a root CA of ours signs our host certificate and the device's key) --------------

static char *pem_of_cert(X509 *x) {
    BIO *b = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(b, x);
    char *d;
    long n = BIO_get_mem_data(b, &d);
    char *s = xmalloc((size_t)n + 1);
    memcpy(s, d, (size_t)n);
    s[n] = 0;
    BIO_free(b);
    return s;
}

static char *pem_of_key(EVP_PKEY *k) {
    BIO *b = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(b, k, NULL, NULL, 0, NULL, NULL);
    char *d;
    long n = BIO_get_mem_data(b, &d);
    char *s = xmalloc((size_t)n + 1);
    memcpy(s, d, (size_t)n);
    s[n] = 0;
    BIO_free(b);
    return s;
}

static X509 *cert_from_pem(const char *pem) {
    BIO *b = BIO_new_mem_buf(pem, -1);
    X509 *x = PEM_read_bio_X509(b, NULL, NULL, NULL);
    BIO_free(b);
    return x;
}

static EVP_PKEY *key_from_pem(const char *pem) {
    BIO *b = BIO_new_mem_buf(pem, -1);
    EVP_PKEY *k = PEM_read_bio_PrivateKey(b, NULL, NULL, NULL);
    BIO_free(b);
    return k;
}

// The device's public key as lockdown hands it out: PEM of a PKCS#1 RSAPublicKey (or of a SubjectPublicKeyInfo).
static EVP_PKEY *device_pubkey(const u8 *pem, size_t n) {
    char *s = xmalloc(n + 1);
    memcpy(s, pem, n);
    s[n] = 0;
    bool spki = strstr(s, "BEGIN PUBLIC KEY") != NULL;
    char *body = strstr(s, "-----\n");
    char *end = body ? strstr(body + 6, "-----END") : NULL;
    EVP_PKEY *k = NULL;
    if (body && end) {
        size_t dn;
        u8 *der = b64_decode(body + 6, (size_t)(end - body - 6), &dn);
        const u8 *p = der;
        k = spki ? d2i_PUBKEY(NULL, &p, (long)dn) : d2i_PublicKey(EVP_PKEY_RSA, NULL, &p, (long)dn);
        free(der);
    }
    free(s);
    return k;
}

static void cert_ext(X509 *x, X509 *issuer, int nid, const char *val) {
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, issuer, x, NULL, NULL, 0);
    X509_EXTENSION *e = X509V3_EXT_conf_nid(NULL, &ctx, nid, val);
    if (e) { X509_add_ext(x, e, -1); X509_EXTENSION_free(e); }
}

static X509 *cert_make(EVP_PKEY *pub, EVP_PKEY *signer, X509 *issuer, bool ca) {
    X509 *x = X509_new();
    X509_set_version(x, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(x), 0);
    X509_gmtime_adj(X509_getm_notBefore(x), 0);
    X509_gmtime_adj(X509_getm_notAfter(x), 60L * 60 * 24 * 365 * 10);
    X509_set_pubkey(x, pub);
    if (issuer) X509_set_issuer_name(x, X509_get_subject_name(issuer));
    cert_ext(x, issuer ? issuer : x, NID_basic_constraints, ca ? "critical,CA:TRUE" : "critical,CA:FALSE");
    if (!ca) {
        cert_ext(x, issuer ? issuer : x, NID_key_usage, "critical,digitalSignature,keyEncipherment");
        cert_ext(x, issuer ? issuer : x, NID_subject_key_identifier, "hash");
    }
    if (!X509_sign(x, signer, EVP_sha256())) { X509_free(x); return NULL; }
    return x;
}

// A new pairing record for a device with this public key (sent to it in the Pair request).
static bool pair_generate(const u8 *devkey_pem, size_t devkey_len, PairRec *pr, char *err, size_t ecap) {
    memset(pr, 0, sizeof *pr);
    EVP_PKEY *dev = device_pubkey(devkey_pem, devkey_len);
    EVP_PKEY *root = EVP_RSA_gen(2048), *host = EVP_RSA_gen(2048);
    X509 *rc = dev && root && host ? cert_make(root, root, NULL, true) : NULL;
    X509 *hc = rc ? cert_make(host, root, rc, false) : NULL;
    X509 *dc = hc ? cert_make(dev, root, rc, false) : NULL;
    bool ok = dc != NULL;
    if (ok) {
        pr->root_cert = pem_of_cert(rc);
        pr->root_key = pem_of_key(root);
        pr->host_cert = pem_of_cert(hc);
        pr->host_key = pem_of_key(host);
        pr->device_cert = pem_of_cert(dc);
        uuid_gen(pr->host_id, sizeof pr->host_id);
        system_buid(pr->system_buid, sizeof pr->system_buid);
        pr->ours = true;
    } else snprintf(err, ecap, "io: couldn't make the pairing certificates%s", dev ? "" : " (the device's key wasn't readable)");
    X509_free(rc); X509_free(hc); X509_free(dc);
    EVP_PKEY_free(dev); EVP_PKEY_free(root); EVP_PKEY_free(host);
    return ok;
}

// --- lockdown -----------------------------------------------------------------------------------------------

typedef struct {
    Mux *m;
    Svc s;
    PairRec pr;
    char udid[96], name[128], product[64], version[32];
    pthread_mutex_t mtx; // one request at a time (services are started from several threads)
} Lockdown;

typedef void (*ProgressFn)(const char *what); // "trust" (waiting for Trust), "unlock" (locked)

// Send a request (Label added; takes req), return the answer.
static void ld_debug(const char *what, const Plist *p) { // FE_DEVICE_DEBUG=1: the conversation on stderr
    if (!getenv("FE_DEVICE_DEBUG")) return;
    if (!p) { dev_log("%s (nothing)", what); return; }
    const char *req = pl_get_str(p, "Request"), *e = pl_get_str(p, "Error"), *k = pl_get_str(p, "Key"), *sv = pl_get_str(p, "Service");
    dev_log("%s %s%s%s%s%s%s%s", what, req ? req : "?", k ? " " : "", k ? k : "", sv ? " " : "", sv ? sv : "", e ? " ERROR " : "", e ? e : "");
}

static Plist *ld_request(Lockdown *l, Plist *req) {
    pl_set(req, "Label", pl_str("file-explorer"));
    ld_debug("lockdown <-", req);
    Plist *r = svc_send_plist(&l->s, req) ? NULL : svc_recv_plist(&l->s, 30000);
    ld_debug("lockdown ->", r);
    pl_free(req);
    return r;
}

static Plist *ld_get_value(Lockdown *l, const char *key) {
    Plist *q = pl_dict();
    pl_set(q, "Request", pl_str("GetValue"));
    if (key) pl_set(q, "Key", pl_str(key));
    Plist *r = ld_request(l, q);
    Plist *v = pl_get(r, "Value");
    Plist *out = v ? pl_copy(v) : NULL;
    pl_free(r);
    return out;
}

static char *ld_get_string(Lockdown *l, const char *key) {
    Plist *v = ld_get_value(l, key);
    char *s = v && v->t == PL_STRING ? strdup(v->s) : NULL;
    pl_free(v);
    return s;
}

// Pair: send our certificates, retrying while the device waits for Trust (or for its passcode) - up to wait_s.
static bool ld_pair(Lockdown *l, ProgressFn progress, int wait_s, char *err, size_t ecap) {
    Plist *dk = NULL;
    char why[128] = "";
    for (int k = 0; k < 5 && !dk; k++) { // (right after the USB takeover it can answer with an error once)
        Plist *q = pl_dict();
        pl_set(q, "Request", pl_str("GetValue"));
        pl_set(q, "Key", pl_str("DevicePublicKey"));
        Plist *r = ld_request(l, q);
        Plist *v = pl_get(r, "Value");
        if (v && v->t == PL_DATA) dk = pl_copy(v);
        else snprintf(why, sizeof why, "%s", pl_get_str(r, "Error") ? pl_get_str(r, "Error") : r ? "no key in the answer" : "no answer");
        pl_free(r);
        if (!dk) usleep(500000);
    }
    if (!dk) {
        bool locked = strstr(why, "Password") != NULL;
        snprintf(err, ecap, "%s: the iPhone didn't give its public key (%s)%s", locked ? "locked" : "io", why, locked ? "" : ". Unlock it and try again");
        return false;
    }
    PairRec pr;
    bool ok = pair_generate(dk->data, dk->len, &pr, err, ecap);
    pl_free(dk);
    if (!ok) return false;
    u64 until = now_ns() + (u64)wait_s * 1000000000ull;
    const char *last = "";
    for (;;) {
        Plist *q = pl_dict(), *rec = pl_dict(), *opt = pl_dict();
        pl_set(rec, "DeviceCertificate", pl_data(pr.device_cert, strlen(pr.device_cert)));
        pl_set(rec, "HostCertificate", pl_data(pr.host_cert, strlen(pr.host_cert)));
        pl_set(rec, "HostID", pl_str(pr.host_id));
        pl_set(rec, "RootCertificate", pl_data(pr.root_cert, strlen(pr.root_cert)));
        pl_set(rec, "SystemBUID", pl_str(pr.system_buid));
        pl_set(opt, "ExtendedPairingErrors", pl_bool(true));
        pl_set(q, "PairRecord", rec);
        pl_set(q, "PairingOptions", opt);
        pl_set(q, "ProtocolVersion", pl_str("2"));
        pl_set(q, "Request", pl_str("Pair"));
        Plist *r = ld_request(l, q);
        if (!r) { snprintf(err, ecap, "io: the device stopped answering while pairing"); pair_free(&pr); return false; }
        const char *e = pl_get_str(r, "Error");
        if (!e) {
            Plist *eb = pl_get(r, "EscrowBag");
            if (eb && eb->t == PL_DATA) { pr.escrow = xmalloc(eb->len ? eb->len : 1); memcpy(pr.escrow, eb->data, eb->len); pr.escrow_len = eb->len; }
            pl_free(r);
            pair_save(l->udid, &pr);
            l->pr = pr;
            return true;
        }
        bool wait = !strcmp(e, "PairingDialogResponsePending") || !strcmp(e, "PasswordProtected");
        if (!wait) {
            snprintf(err, ecap, !strcmp(e, "UserDeniedPairing") ? "denied: the iPhone was told not to trust this computer" : "io: pairing failed (%s)", e);
            pl_free(r);
            pair_free(&pr);
            return false;
        }
        const char *what = !strcmp(e, "PasswordProtected") ? "unlock" : "trust";
        if (strcmp(what, last) && progress) progress(what);
        last = what;
        pl_free(r);
        if (now_ns() > until) {
            snprintf(err, ecap, "%s", !strcmp(what, "unlock") ? "locked: unlock the iPhone, then try again" : "trust: Trust wasn't tapped on the iPhone in time");
            pair_free(&pr);
            return false;
        }
        usleep(1000000);
    }
}

// StartSession with the pairing record and switch to TLS. 1 ok, 0 the device doesn't know this record, -1 error.
static int ld_session(Lockdown *l, char *err, size_t ecap) {
    Plist *q = pl_dict();
    pl_set(q, "Request", pl_str("StartSession"));
    pl_set(q, "HostID", pl_str(l->pr.host_id));
    pl_set(q, "SystemBUID", pl_str(l->pr.system_buid));
    Plist *r = ld_request(l, q);
    if (!r) { snprintf(err, ecap, "io: the device stopped answering"); return -1; }
    const char *e = pl_get_str(r, "Error");
    if (e) {
        int res = !strcmp(e, "InvalidHostID") ? 0 : -1;
        if (res < 0) snprintf(err, ecap, "%s: %s", !strcmp(e, "PasswordProtected") ? "locked" : "io", e);
        pl_free(r);
        return res;
    }
    bool ssl = pl_get_bool(r, "EnableSessionSSL");
    pl_free(r);
    if (!ssl) return 1;
    X509 *c = cert_from_pem(l->pr.host_cert);
    EVP_PKEY *k = key_from_pem(l->pr.host_key);
    bool ok = c && k && svc_start_tls(&l->s, c, k, err, ecap);
    if (!c || !k) snprintf(err, ecap, "io: the pairing record's certificate can't be read");
    X509_free(c);
    EVP_PKEY_free(k);
    return ok ? 1 : -1; // (a failed handshake is usually the device going away: the pairing is kept)
}

// Connect to lockdown: identify the device, pair if needed (progress: what the user has to do), open the session.
static bool ld_open(Mux *m, Lockdown *l, ProgressFn progress, int pair_wait_s, char *err, size_t ecap) {
    memset(l, 0, sizeof *l);
    l->m = m;
    pthread_mutex_init(&l->mtx, NULL);
    for (int attempt = 0; attempt < 2; attempt++) {
        l->s.c = mux_connect(m, 62078, 10000);
        if (!l->s.c) { snprintf(err, ecap, "io: the iPhone didn't answer (lockdown)"); return false; }
        Plist *q = pl_dict();
        pl_set(q, "Request", pl_str("QueryType"));
        Plist *r = ld_request(l, q);
        const char *t = pl_get_str(r, "Type");
        bool lockdown = t && !strcmp(t, "com.apple.mobile.lockdown");
        pl_free(r);
        if (!lockdown) { snprintf(err, ecap, "io: the device's lockdown service didn't answer"); svc_close(&l->s); return false; }
        if (!l->udid[0]) {
            char *u = ld_get_string(l, "UniqueDeviceID");
            snprintf(l->udid, sizeof l->udid, "%s", u ? u : "unknown");
            free(u);
        }
        bool have = attempt == 0 && pair_load(l->udid, &l->pr);
        if (!have && !ld_pair(l, progress, pair_wait_s, err, ecap)) { svc_close(&l->s); return false; }
        int s = ld_session(l, err, ecap);
        if (s == 1) break;
        bool ours = l->pr.ours;
        pair_free(&l->pr);
        svc_close(&l->s);
        if (s < 0) return false;
        if (ours) { // the device said it doesn't know this record (InvalidHostID: Trust was reset): pair again
            char d[PATH_MAX], p[PATH_MAX];
            lockdown_dir(d, sizeof d);
            snprintf(p, sizeof p, "%s/%s.plist", d, l->udid);
            unlink(p);
        }
        if (attempt == 1) { snprintf(err, ecap, "io: the device doesn't accept this computer's pairing"); return false; }
    }
    char *n = ld_get_string(l, "DeviceName"), *pt = ld_get_string(l, "ProductType"), *v = ld_get_string(l, "ProductVersion");
    snprintf(l->name, sizeof l->name, "%s", n ? n : "iPhone");
    snprintf(l->product, sizeof l->product, "%s", pt ? pt : "");
    snprintf(l->version, sizeof l->version, "%s", v ? v : "");
    free(n); free(pt); free(v);
    return true;
}

static void ld_close(Lockdown *l) {
    svc_close(&l->s);
    pair_free(&l->pr);
    pthread_mutex_destroy(&l->mtx);
}

// A new lockdown connection and session with the pairing record we have (the device drops idle ones). (mtx held)
static bool ld_reopen(Lockdown *l, char *err, size_t ecap) {
    dev_log("lockdown: connecting again");
    svc_close(&l->s);
    l->s.c = mux_connect(l->m, 62078, 10000);
    if (!l->s.c) { snprintf(err, ecap, "io: the iPhone didn't answer (lockdown)"); dev_log("lockdown: %s", err); return false; }
    bool ok = ld_session(l, err, ecap) == 1;
    if (!ok) dev_log("lockdown: %s", err);
    return ok;
}

// Start a service and connect to it (TLS if the device wants it).
static bool ld_service(Lockdown *l, const char *service, Svc *out, char *err, size_t ecap) {
    memset(out, 0, sizeof *out);
    pthread_mutex_lock(&l->mtx);
    Plist *r = NULL;
    for (int attempt = 0; attempt < 2 && !r; attempt++) {
        if (attempt && !ld_reopen(l, err, ecap)) break;
        Plist *q = pl_dict();
        pl_set(q, "Request", pl_str("StartService"));
        pl_set(q, "Service", pl_str(service));
        r = ld_request(l, q);
    }
    pthread_mutex_unlock(&l->mtx);
    const char *e = pl_get_str(r, "Error");
    i64 port = pl_get_int(r, "Port", 0);
    bool ssl = pl_get_bool(r, "EnableServiceSSL");
    if (!r || e || port <= 0) {
        snprintf(err, ecap, "%s: can't start %s%s%s", e && strstr(e, "Password") ? "locked" : "io", service, e ? ": " : "", e ? e : "");
        dev_log("lockdown: %s", err);
        pl_free(r);
        return false;
    }
    pl_free(r);
    out->c = mux_connect(l->m, (u16)port, 10000);
    if (!out->c) { snprintf(err, ecap, "io: can't connect to %s", service); return false; }
    if (ssl) {
        X509 *c = cert_from_pem(l->pr.host_cert);
        EVP_PKEY *k = key_from_pem(l->pr.host_key);
        bool ok = c && k && svc_start_tls(out, c, k, err, ecap);
        X509_free(c);
        EVP_PKEY_free(k);
        if (!ok) { svc_close(out); return false; }
    }
    return true;
}

// --- AFC: files in the media area (or an app's container) -----------------------------------------------------

enum {
    AFC_STATUS = 1, AFC_DATA = 2, AFC_READ_DIR = 3, AFC_TRUNCATE = 7, AFC_REMOVE = 8, AFC_MKDIR = 9, AFC_FILE_INFO = 10,
    AFC_DEV_INFO = 11, AFC_OPEN = 13, AFC_OPEN_RES = 14, AFC_READ = 15, AFC_WRITE = 16, AFC_SEEK = 17, AFC_CLOSE = 20,
    AFC_SET_SIZE = 21, AFC_RENAME = 24, AFC_SET_MTIME = 30, AFC_REMOVE_ALL = 34,
};
enum { AFC_RDONLY = 1, AFC_RW = 2, AFC_WRONLY_TRUNC = 3, AFC_RW_TRUNC = 4, AFC_APPEND = 5 };
#define AFC_IO_MAX (1u << 20) // bytes per read request (writes: AFC_WRITE_MAX)
#define AFC_WRITE_MAX (256u << 10)

typedef struct {
    Svc s;
    u64 pkt;
    pthread_mutex_t mtx;
    bool broken; // a request went wrong midway: answers could be out of step, so nothing more goes out
} Afc;

static Afc *afc_new(Svc *s) {
    Afc *a = xcalloc(1, sizeof *a);
    a->s = *s;
    pthread_mutex_init(&a->mtx, NULL);
    return a;
}

static void afc_free(Afc *a) {
    if (!a) return;
    svc_close(&a->s);
    pthread_mutex_destroy(&a->mtx);
    free(a);
}

// One request: header fields hd, then payload. 0 ok (*out: the answer's data), >0 an AFC error code, -1 I/O.
static int afc_op(Afc *a, u64 op, const void *hd, size_t hl, const void *pay, size_t pl, u8 **out, size_t *on) {
    if (out) *out = NULL;
    if (on) *on = 0;
    u8 h[40];
    memcpy(h, "CFA6LPAA", 8);
    put_le64(h + 8, 40 + hl + pl);
    put_le64(h + 16, 40 + hl);
    pthread_mutex_lock(&a->mtx);
    if (a->broken) { pthread_mutex_unlock(&a->mtx); return -1; }
    u64 num = a->pkt++;
    put_le64(h + 24, num);
    put_le64(h + 32, op);
    u8 *req = xmalloc(40 + hl);
    memcpy(req, h, 40);
    if (hl) memcpy(req + 40, hd, hl);
    const char *why = "";
    int rc = svc_write(&a->s, req, 40 + hl) || (pl && svc_write(&a->s, pay, pl)) ? (why = "sending", -1) : 0;
    free(req);
    u8 rh[40];
    if (!rc && svc_read(&a->s, rh, 40, 30000)) { rc = -1; why = a->s.c && a->s.c->state == 1 ? "no answer in 30 s" : "the device closed the connection"; }
    if (!rc && memcmp(rh, "CFA6LPAA", 8)) { rc = -1; why = "garbled answer"; }
    if (!rc && get_le64(rh + 24) != num) { rc = -1; why = "answer to another request"; }
    u64 total = rc ? 0 : get_le64(rh + 8), rop = rc ? 0 : get_le64(rh + 32);
    if (!rc && (total < 40 || total > (64u << 20))) { rc = -1; why = "bad length"; }
    u8 *body = NULL;
    size_t bn = rc ? 0 : (size_t)(total - 40);
    if (!rc) {
        body = xmalloc(bn + 1);
        if (bn && svc_read(&a->s, body, bn, 30000)) { rc = -1; why = "answer cut short"; }
        body[bn] = 0;
    }
    if (rc) a->broken = true;
    if (rc) dev_log("afc %p: request %llu (op %llu) failed: %s", (void *)a, (unsigned long long)num, (unsigned long long)op, why);
    pthread_mutex_unlock(&a->mtx);
    if (rc) { free(body); return -1; }
    if (rop == AFC_STATUS) {
        int code = bn >= 8 ? (int)get_le64(body) : 1;
        free(body);
        return code;
    }
    if (out) { *out = body; *on = bn; } else free(body);
    return 0;
}

static int afc_errno(int code) {
    switch (code) {
    case 0: return 0;
    case -1: return EIO;
    case 7: return EINVAL;
    case 8: return ENOENT;
    case 9: return EISDIR;
    case 10: return EACCES;
    case 15: return ENOTSUP;
    case 16: return EEXIST;
    case 17: return EBUSY;
    case 18: return ENOSPC;
    case 33: return ENOTEMPTY;
    default: return EIO;
    }
}

// path\0 as the header (most operations).
static int afc_path_op(Afc *a, u64 op, const char *path) { return afc_errno(afc_op(a, op, path, strlen(path) + 1, NULL, 0, NULL, NULL)); }

// Key\0value\0 pairs -> stat. 0 or -errno.
static int afc_info(Afc *a, const char *path, struct stat *st) {
    u8 *d;
    size_t n;
    int rc = afc_op(a, AFC_FILE_INFO, path, strlen(path) + 1, NULL, 0, &d, &n);
    if (rc) return -afc_errno(rc);
    memset(st, 0, sizeof *st);
    st->st_mode = S_IFREG | 0644;
    st->st_nlink = 1;
    for (size_t o = 0; o < n;) {
        const char *k = (const char *)d + o;
        size_t kl = strnlen(k, n - o);
        if (o + kl + 1 >= n) break;
        const char *v = k + kl + 1;
        size_t vl = strnlen(v, n - o - kl - 1);
        if (!strcmp(k, "st_size")) st->st_size = (off_t)strtoull(v, NULL, 10);
        else if (!strcmp(k, "st_blocks")) st->st_blocks = (blkcnt_t)strtoull(v, NULL, 10);
        else if (!strcmp(k, "st_nlink")) st->st_nlink = (nlink_t)strtoul(v, NULL, 10);
        else if (!strcmp(k, "st_ifmt")) st->st_mode = !strcmp(v, "S_IFDIR") ? S_IFDIR | 0755 : !strcmp(v, "S_IFLNK") ? S_IFLNK | 0777 : S_IFREG | 0644;
        else if (!strcmp(k, "st_mtime")) {
            u64 ns = strtoull(v, NULL, 10);
            st->st_mtim.tv_sec = (time_t)(ns / 1000000000ull);
            st->st_mtim.tv_nsec = (long)(ns % 1000000000ull);
        } else if (!strcmp(k, "st_birthtime")) {
            u64 ns = strtoull(v, NULL, 10);
            st->st_ctim.tv_sec = (time_t)(ns / 1000000000ull);
        }
        o += kl + 1 + vl + 1;
    }
    st->st_atim = st->st_mtim;
    if (!st->st_ctim.tv_sec) st->st_ctim = st->st_mtim;
    free(d);
    return 0;
}

// A folder's names (without . and ..), NUL-separated in *names; returns the count or -errno.
static int afc_list(Afc *a, const char *path, char **names, size_t *len) {
    u8 *d;
    size_t n;
    int rc = afc_op(a, AFC_READ_DIR, path, strlen(path) + 1, NULL, 0, &d, &n);
    if (rc) return -afc_errno(rc);
    char *o = xmalloc(n + 1);
    size_t k = 0;
    int cnt = 0;
    for (size_t i = 0; i < n;) {
        const char *nm = (const char *)d + i;
        size_t l = strnlen(nm, n - i);
        if (l && strcmp(nm, ".") && strcmp(nm, "..")) { memcpy(o + k, nm, l + 1); k += l + 1; cnt++; }
        i += l + 1;
    }
    free(d);
    *names = o;
    *len = k;
    return cnt;
}

static int afc_devinfo(Afc *a, u64 *total, u64 *avail, u64 *bsize) {
    u8 *d;
    size_t n;
    int rc = afc_op(a, AFC_DEV_INFO, NULL, 0, NULL, 0, &d, &n);
    if (rc) return -afc_errno(rc);
    *total = *avail = 0;
    *bsize = 4096;
    for (size_t o = 0; o < n;) {
        const char *k = (const char *)d + o;
        size_t kl = strnlen(k, n - o);
        if (o + kl + 1 >= n) break;
        const char *v = k + kl + 1;
        if (!strcmp(k, "FSTotalBytes")) *total = strtoull(v, NULL, 10);
        else if (!strcmp(k, "FSFreeBytes")) *avail = strtoull(v, NULL, 10);
        else if (!strcmp(k, "FSBlockSize")) *bsize = strtoull(v, NULL, 10);
        o += kl + 1 + strnlen(v, n - o - kl - 1) + 1;
    }
    free(d);
    return 0;
}

static int afc_open(Afc *a, const char *path, u64 mode, u64 *handle) {
    size_t pl = strlen(path) + 1;
    u8 *hd = xmalloc(8 + pl), *d;
    size_t n;
    put_le64(hd, mode);
    memcpy(hd + 8, path, pl);
    int rc = afc_op(a, AFC_OPEN, hd, 8 + pl, NULL, 0, &d, &n);
    free(hd);
    if (rc) return -afc_errno(rc);
    *handle = n >= 8 ? get_le64(d) : 0;
    free(d);
    return n >= 8 ? 0 : -EIO;
}

static int afc_close(Afc *a, u64 h) {
    u8 hd[8];
    put_le64(hd, h);
    return -afc_errno(afc_op(a, AFC_CLOSE, hd, 8, NULL, 0, NULL, NULL));
}

static int afc_seek(Afc *a, u64 h, i64 off) {
    u8 hd[24];
    put_le64(hd, h);
    put_le64(hd + 8, 0); // SEEK_SET
    put_le64(hd + 16, (u64)off);
    return -afc_errno(afc_op(a, AFC_SEEK, hd, 24, NULL, 0, NULL, NULL));
}

// Read up to len bytes at the current position; returns the count (0: end of file) or -errno.
static ssize_t afc_read(Afc *a, u64 h, void *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        u8 hd[16], *d;
        size_t n, want = MIN(len - got, (size_t)AFC_IO_MAX);
        put_le64(hd, h);
        put_le64(hd + 8, want);
        int rc = afc_op(a, AFC_READ, hd, 16, NULL, 0, &d, &n);
        if (rc) return got ? (ssize_t)got : -afc_errno(rc);
        n = MIN(n, want);
        memcpy((u8 *)buf + got, d, n);
        free(d);
        got += n;
        if (n < want) break;
    }
    return (ssize_t)got;
}

static ssize_t afc_write(Afc *a, u64 h, const void *buf, size_t len) {
    for (size_t o = 0; o < len;) {
        size_t n = MIN(len - o, (size_t)AFC_WRITE_MAX);
        u8 hd[8];
        put_le64(hd, h);
        int rc = afc_op(a, AFC_WRITE, hd, 8, (const u8 *)buf + o, n, NULL, NULL);
        if (rc) return -afc_errno(rc);
        o += n;
    }
    return (ssize_t)len;
}

static int afc_set_size(Afc *a, u64 h, u64 size) {
    u8 hd[16];
    put_le64(hd, h);
    put_le64(hd + 8, size);
    return -afc_errno(afc_op(a, AFC_SET_SIZE, hd, 16, NULL, 0, NULL, NULL));
}

static int afc_rename(Afc *a, const char *from, const char *to) {
    size_t fl = strlen(from) + 1, tl = strlen(to) + 1;
    u8 *hd = xmalloc(fl + tl);
    memcpy(hd, from, fl);
    memcpy(hd + fl, to, tl);
    int rc = afc_op(a, AFC_RENAME, hd, fl + tl, NULL, 0, NULL, NULL);
    free(hd);
    return -afc_errno(rc);
}

static int afc_set_mtime(Afc *a, const char *path, u64 ns) {
    size_t pl = strlen(path) + 1;
    u8 *hd = xmalloc(8 + pl);
    put_le64(hd, ns);
    memcpy(hd + 8, path, pl);
    int rc = afc_op(a, AFC_SET_MTIME, hd, 8 + pl, NULL, 0, NULL, NULL);
    free(hd);
    return -afc_errno(rc);
}

static int afc_truncate(Afc *a, const char *path, u64 size) {
    size_t pl = strlen(path) + 1;
    u8 *hd = xmalloc(8 + pl);
    put_le64(hd, size);
    memcpy(hd + 8, path, pl);
    int rc = afc_op(a, AFC_TRUNCATE, hd, 8 + pl, NULL, 0, NULL, NULL);
    free(hd);
    return -afc_errno(rc);
}

// --- apps that share files (installation_proxy) and their Documents (house_arrest) ----------------------------

typedef struct { char id[160], name[128]; } IosApp;

static int ios_list_apps(Lockdown *l, IosApp **out, char *err, size_t ecap) {
    *out = NULL;
    Svc s;
    if (!ld_service(l, "com.apple.mobile.installation_proxy", &s, err, ecap)) return -1;
    Plist *q = pl_dict(), *opt = pl_dict(), *attrs = pl_array();
    pl_push(attrs, pl_str("CFBundleIdentifier"));
    pl_push(attrs, pl_str("CFBundleDisplayName"));
    pl_push(attrs, pl_str("CFBundleName"));
    pl_push(attrs, pl_str("UIFileSharingEnabled"));
    pl_set(opt, "ApplicationType", pl_str("User"));
    pl_set(opt, "ReturnAttributes", attrs);
    pl_set(q, "ClientOptions", opt);
    pl_set(q, "Command", pl_str("Browse"));
    int n = 0;
    if (svc_send_plist(&s, q) == 0)
        for (;;) {
            Plist *r = svc_recv_plist(&s, 30000);
            if (!r) break;
            Plist *list = pl_get(r, "CurrentList");
            for (int i = 0; list && list->t == PL_ARRAY && i < list->n; i++) {
                Plist *a = list->kids[i];
                const char *id = pl_get_str(a, "CFBundleIdentifier");
                if (!id || !pl_get_bool(a, "UIFileSharingEnabled")) continue;
                const char *nm = pl_get_str(a, "CFBundleDisplayName");
                if (!nm || !nm[0]) nm = pl_get_str(a, "CFBundleName");
                *out = xrealloc(*out, (size_t)(n + 1) * sizeof **out);
                snprintf((*out)[n].id, sizeof (*out)[n].id, "%s", id);
                snprintf((*out)[n].name, sizeof (*out)[n].name, "%s", nm && nm[0] ? nm : id);
                n++;
            }
            const char *st = pl_get_str(r, "Status");
            bool done = (st && !strcmp(st, "Complete")) || pl_get(r, "Error");
            pl_free(r);
            if (done) break;
        }
    pl_free(q);
    svc_close(&s);
    return n;
}

// An AFC connection to one app's container (its files are under /Documents).
static Afc *ios_app_documents(Lockdown *l, const char *bundle, char *err, size_t ecap) {
    Svc s;
    if (!ld_service(l, "com.apple.mobile.house_arrest", &s, err, ecap)) return NULL;
    Plist *q = pl_dict();
    pl_set(q, "Command", pl_str("VendDocuments"));
    pl_set(q, "Identifier", pl_str(bundle));
    Plist *r = svc_send_plist(&s, q) ? NULL : svc_recv_plist(&s, 30000);
    pl_free(q);
    bool ok = r && !pl_get(r, "Error");
    if (!ok) snprintf(err, ecap, "io: the app's files aren't available (%s)", pl_get_str(r, "Error") ? pl_get_str(r, "Error") : "no answer");
    pl_free(r);
    if (!ok) { svc_close(&s); return NULL; }
    return afc_new(&s);
}
