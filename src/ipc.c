// ipc.c - single instance: the first window listens on a Unix socket in $XDG_RUNTIME_DIR; later launches
// connect, hand over their (already resolved) arguments as text lines and exit.
//
// Message: lines of "<verb>\t<argument>", e.g. "open\t/home/me/Downloads", "split\t/tmp", "token\t<xdg token>".
// The server answers "ok\n" once the message is queued for the main thread.

#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

static int g_ipc_fd = -1;
static char g_ipc_path[108];
static void (*g_ipc_handler)(char *msg); // runs on the main thread, owns msg

static void ipc_socket_path(char *out, size_t cap) {
    const char *ov = getenv("FE_IPC_SOCKET"); // tests: a private instance
    if (ov && ov[0] == '/') { snprintf(out, cap, "%s", ov); return; }
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt && rt[0] == '/') snprintf(out, cap, "%s/file-explorer.sock", rt);
    else snprintf(out, cap, "/tmp/file-explorer-%u.sock", (unsigned)getuid());
}

static bool ipc_write_all(int fd, const char *p, size_t n) {
    while (n) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL); // a client that went away must not SIGPIPE us
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        p += w;
        n -= (size_t)w;
    }
    return true;
}

static int ipc_connect(void) {
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    ipc_socket_path(a.sun_path, sizeof a.sun_path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// Client: deliver msg to a running instance. True when it accepted it (the caller can exit).
static bool ipc_send(const char *msg) {
    int fd = ipc_connect();
    if (fd < 0) return false;
    bool ok = ipc_write_all(fd, msg, strlen(msg));
    shutdown(fd, SHUT_WR);
    char ack[8] = {0};
    struct pollfd pf = {fd, POLLIN, 0};
    if (ok && poll(&pf, 1, 3000) == 1) ok = read(fd, ack, sizeof ack - 1) >= 2 && !strncmp(ack, "ok", 2);
    else ok = false;
    close(fd);
    return ok;
}

static void ipc_deliver(void *arg) {
    if (g_ipc_handler) g_ipc_handler(arg);
    else free(arg);
}

static void *ipc_thread(void *arg) {
    (void)arg;
    for (;;) {
        int c = accept4(g_ipc_fd, NULL, NULL, SOCK_CLOEXEC);
        if (c < 0) {
            if (errno == EINTR || errno == ECONNABORTED) continue;
            break;
        }
        size_t cap = 4096, n = 0;
        char *buf = xmalloc(cap);
        for (;;) {
            struct pollfd pf = {c, POLLIN, 0};
            if (poll(&pf, 1, 2000) != 1) break; // a client that never finishes doesn't block us
            if (n + 1024 > cap) {
                if (cap >= (1u << 20)) break;
                cap *= 2;
                buf = xrealloc(buf, cap);
            }
            ssize_t r = read(c, buf + n, cap - n - 1);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) break;
            n += (size_t)r;
        }
        buf[n] = 0;
        if (n) { // empty: just a probe ("is anyone serving?")
            main_post(ipc_deliver, buf);
            ipc_write_all(c, "ok\n", 3);
        } else {
            free(buf);
        }
        close(c);
    }
    return NULL;
}

// Server: become the instance others talk to. Replaces a stale socket left by a crashed instance.
static bool ipc_listen(void (*handler)(char *msg)) {
    g_ipc_handler = handler;
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    ipc_socket_path(a.sun_path, sizeof a.sun_path);
    int probe = ipc_connect();
    if (probe >= 0) { close(probe); return false; } // someone else is serving (raced us)
    unlink(a.sun_path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    mode_t old = umask(0077);
    bool ok = bind(fd, (struct sockaddr *)&a, sizeof a) == 0 && listen(fd, 8) == 0;
    umask(old);
    if (!ok) { close(fd); return false; }
    g_ipc_fd = fd;
    snprintf(g_ipc_path, sizeof g_ipc_path, "%s", a.sun_path);
    pthread_t t;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &at, ipc_thread, NULL);
    pthread_setname_np(t, "fe-ipc");
    pthread_attr_destroy(&at);
    return true;
}

static void ipc_shutdown(void) {
    if (g_ipc_fd < 0) return;
    unlink(g_ipc_path); // the listener thread ends with the process
    g_ipc_path[0] = 0;
}
