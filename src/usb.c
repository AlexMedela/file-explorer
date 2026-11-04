// usb.c - USB devices through the kernel's usbfs (/dev/bus/usb), no libusb: finding phones and cameras in sysfs,
// reading descriptors, choosing the configuration, claiming an interface, bulk transfers. A Usb can also be a
// stand-in device (tests), whose callbacks replace the ioctls.

#include <poll.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>

// What happened with a device, for finding out why a connection went wrong: the device helper keeps it in
// DIR/.device.log (stderr before it's mounted). Time-stamped lines.
static void dev_log(const char *fmt, ...) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    char line[1024];
    int k = (int)strftime(line, sizeof line, "%H:%M:%S", &tm);
    k += snprintf(line + k, sizeof line - (size_t)k, ".%03ld ", ts.tv_nsec / 1000000);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + k, sizeof line - (size_t)k, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", line);
}

typedef struct Usb Usb;
struct Usb {
    int fd;
    int iface;           // claimed interface, -1 none
    u8 ep_in, ep_out;    // bulk endpoints
    int mps;             // bulk OUT max packet size: a transfer that's a multiple of it is ended by a zero-length one
    int halts;           // stalls / transaction errors cleared (a flaky link shows many)
    // stand-in device (tests): used instead of the ioctls when set
    int (*fake_out)(Usb *u, const u8 *data, int len);
    int (*fake_in)(Usb *u, u8 *buf, int cap, int timeout_ms);
    void *fake;
};

// A phone or camera on the bus (from sysfs; nothing is opened).
typedef struct {
    char sys[32];        // sysfs name ("1-2")
    char serial[96];     // iSerial: an iPhone's UDID (without its dash), an Android serial
    char product[96];    // "iPhone", "Pixel 7"
    char manufacturer[96];
    int busnum, devnum;
    u16 vendor, product_id;
    char kind;           // 'a' iPhone / iPad (usbmux), 'm' MTP (Android...), 'c' PTP camera
} UsbDevInfo;

static const char *usb_sys_dir(void) { return getenv("FE_SYS_USB") ? getenv("FE_SYS_USB") : "/sys/bus/usb/devices"; }

static bool usb_sys_read(const char *dev, const char *attr, char *out, size_t cap) {
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/%s/%s", usb_sys_dir(), dev, attr);
    out[0] = 0;
    FILE *f = fopen(p, "r");
    if (!f) return false;
    bool ok = fgets(out, (int)cap, f) != NULL;
    fclose(f);
    if (!ok) out[0] = 0;
    out[strcspn(out, "\n")] = 0;
    return ok;
}

static bool usb_is_apple_mux(u16 vendor, u16 product) { return vendor == 0x05ac && product >= 0x1290 && product <= 0x12af; }

// What udev knows about a device node (libmtp / libgphoto2 rules set these).
static bool usb_udev_has(int busnum, int devnum, const char *prop) {
    char p[64], line[256];
    snprintf(p, sizeof p, "/run/udev/data/c189:%d", (busnum - 1) * 128 + devnum - 1);
    FILE *f = fopen(p, "r");
    bool found = false;
    size_t pl = strlen(prop);
    while (f && !found && fgets(line, sizeof line, f))
        found = !strncmp(line, "E:", 2) && !strncmp(line + 2, prop, pl) && line[2 + pl] == '=' && line[3 + pl] == '1';
    if (f) fclose(f);
    return found;
}

// Phones and cameras on the bus: Apple devices that speak usbmux, and anything with a PTP / MTP interface (an
// Android phone in "File transfer" mode, a camera).
static int usb_list_phones(UsbDevInfo *out, int max) {
    DIR *d = opendir(usb_sys_dir());
    if (!d) return 0;
    int n = 0;
    struct dirent *de;
    while (n < max && (de = readdir(d))) {
        if (de->d_name[0] == '.' || strchr(de->d_name, ':') || !strncmp(de->d_name, "usb", 3)) continue; // interfaces, root hubs
        char v[16], pr[16], b[16];
        if (!usb_sys_read(de->d_name, "idVendor", v, sizeof v) || !usb_sys_read(de->d_name, "idProduct", pr, sizeof pr)) continue;
        UsbDevInfo info;
        memset(&info, 0, sizeof info);
        snprintf(info.sys, sizeof info.sys, "%s", de->d_name);
        info.vendor = (u16)strtol(v, NULL, 16);
        info.product_id = (u16)strtol(pr, NULL, 16);
        if (usb_sys_read(de->d_name, "busnum", b, sizeof b)) info.busnum = atoi(b);
        if (usb_sys_read(de->d_name, "devnum", b, sizeof b)) info.devnum = atoi(b);
        usb_sys_read(de->d_name, "serial", info.serial, sizeof info.serial);
        usb_sys_read(de->d_name, "product", info.product, sizeof info.product);
        usb_sys_read(de->d_name, "manufacturer", info.manufacturer, sizeof info.manufacturer);
        if (usb_is_apple_mux(info.vendor, info.product_id)) info.kind = 'a';
        else { // interfaces of the active configuration: "1-2:1.0"...
            DIR *id = opendir(usb_sys_dir());
            struct dirent *ie;
            bool ptp = false, mtp = false;
            size_t dl = strlen(de->d_name);
            while (id && (ie = readdir(id))) {
                if (strncmp(ie->d_name, de->d_name, dl) || ie->d_name[dl] != ':') continue;
                char cls[8], sub[8], name[64];
                usb_sys_read(ie->d_name, "bInterfaceClass", cls, sizeof cls);
                usb_sys_read(ie->d_name, "bInterfaceSubClass", sub, sizeof sub);
                usb_sys_read(ie->d_name, "interface", name, sizeof name);
                if (!strcmp(cls, "06") && !strcmp(sub, "01")) ptp = true;
                if (!strcmp(name, "MTP") && (!strcmp(cls, "06") || !strcmp(cls, "ff"))) mtp = true;
            }
            if (id) closedir(id);
            mtp |= usb_udev_has(info.busnum, info.devnum, "ID_MTP_DEVICE"); // libmtp's udev rules know vendor-class ones
            if (mtp) info.kind = 'm';
            else if (ptp) info.kind = 'c';
        }
        if (info.kind) out[n++] = info;
    }
    closedir(d);
    return n;
}

// --- opening a device -----------------------------------------------------------------------------------------

typedef struct { u8 cfg, iface, cls, sub, proto, nep, ep_in, ep_out; int mps; bool found; } UsbTarget;

// Walk the raw descriptors (device + every configuration, as read() on the node returns them) for the interface
// to use: usbmux (class ff/fe/02) for Apple; PTP / MTP (06/01) otherwise, else a vendor-class interface with three
// endpoints (how some Android phones expose MTP).
static UsbTarget usb_find_target(const u8 *d, size_t n, char kind, int curcfg) {
    UsbTarget ifs[32];
    int nif = 0;
    u8 cfg = 0;
    for (size_t o = n >= 18 ? d[0] : n; o + 2 <= n && d[o] >= 2 && o + d[o] <= n; o += d[o]) {
        u8 len = d[o], type = d[o + 1];
        if (type == 2 && len >= 9) cfg = d[o + 5];
        else if (type == 4 && len >= 9 && nif < (int)ARRAY_COUNT(ifs)) {
            UsbTarget *t = &ifs[nif++];
            memset(t, 0, sizeof *t);
            t->found = d[o + 3] == 0; // alternate setting 0 only
            t->cfg = cfg, t->iface = d[o + 2], t->nep = d[o + 4], t->cls = d[o + 5], t->sub = d[o + 6], t->proto = d[o + 7];
        } else if (type == 5 && len >= 7 && nif) {
            UsbTarget *t = &ifs[nif - 1];
            u8 addr = d[o + 2], attr = d[o + 3] & 3;
            if (attr == 2 && (addr & 0x80) && !t->ep_in) t->ep_in = addr;
            if (attr == 2 && !(addr & 0x80) && !t->ep_out) { t->ep_out = addr; t->mps = d[o + 4] | (d[o + 5] & 7) << 8; }
        }
    }
    // the current configuration if it has one (switching would drop the others, an iPhone's tethering), else
    // the highest-numbered (what usbmuxd picks)
    for (int pass = 0; pass < 2; pass++) {
        UsbTarget *best = NULL;
        for (int i = 0; i < nif; i++) {
            UsbTarget *t = &ifs[i];
            if (!t->found || !t->ep_in || !t->ep_out) continue;
            bool want = kind == 'a' ? t->cls == 0xff && t->sub == 0xfe && t->proto == 2
                      : pass == 0   ? t->cls == 6 && t->sub == 1
                                    : t->cls == 0xff && t->sub == 0xff && t->nep == 3;
            if (!want) continue;
            if (t->cfg == curcfg) return *t;
            if (!best || t->cfg > best->cfg) best = t;
        }
        if (best) return *best;
    }
    UsbTarget none;
    memset(&none, 0, sizeof none);
    return none;
}

static void usb_close(Usb *u) {
    if (!u) return;
    if (u->fd >= 0 && u->iface >= 0) {
        int i = u->iface;
        ioctl(u->fd, USBDEVFS_RELEASEINTERFACE, &i);
    }
    if (u->fd >= 0) close(u->fd);
    u->fd = -1;
    u->iface = -1;
}

// Open the device, switch to the configuration holding the interface if needed, claim it. Apple: usbmuxd holds
// the usbmux interface whenever an iPhone is plugged in, so it's taken over (steal); for anything else an
// interface another program is using is left alone. Errors: err gets "busy: ...", "perm: ...", "io: ...".
static bool usb_open(const UsbDevInfo *info, Usb *u, bool steal, char *err, size_t ecap) {
    memset(u, 0, sizeof *u);
    u->fd = -1;
    u->iface = -1;
    char node[64];
    snprintf(node, sizeof node, "/dev/bus/usb/%03d/%03d", info->busnum, info->devnum);
    u->fd = open(node, O_RDWR | O_CLOEXEC);
    if (u->fd < 0) {
        snprintf(err, ecap, errno == EACCES ? "perm: no permission to use %s (the udev rule from install.sh grants it)" : "io: can't open %s: %s",
                 node, strerror(errno));
        return false;
    }
    char cv[8];
    int curcfg = usb_sys_read(info->sys, "bConfigurationValue", cv, sizeof cv) ? atoi(cv) : -1;
    u8 *desc = xmalloc(65536);
    ssize_t dn = read(u->fd, desc, 65536);
    UsbTarget t = usb_find_target(desc, dn > 0 ? (size_t)dn : 0, info->kind, curcfg);
    free(desc);
    if (!t.found) { snprintf(err, ecap, "unsupported: the device has no %s interface", info->kind == 'a' ? "usbmux" : "PTP / MTP"); usb_close(u); return false; }
    if (curcfg != t.cfg) { // (kernel drivers on the old one are unbound by the switch)
        unsigned int c = t.cfg;
        if (ioctl(u->fd, USBDEVFS_SETCONFIGURATION, &c)) {
            snprintf(err, ecap, "io: can't switch the device to configuration %d: %s", t.cfg, strerror(errno));
            usb_close(u);
            return false;
        }
    }
    struct usbdevfs_disconnect_claim dc;
    memset(&dc, 0, sizeof dc);
    dc.interface = t.iface;
    if (!steal) { // a kernel driver may go (none should be bound); another program's claim stays
        dc.flags = USBDEVFS_DISCONNECT_CLAIM_EXCEPT_DRIVER;
        snprintf(dc.driver, sizeof dc.driver, "usbfs");
    }
    if (ioctl(u->fd, USBDEVFS_DISCONNECT_CLAIM, &dc)) {
        snprintf(err, ecap, errno == EBUSY ? "busy: another program is using the device" : "io: can't claim the device: %s", strerror(errno));
        usb_close(u);
        return false;
    }
    u->iface = t.iface;
    u->ep_in = t.ep_in;
    u->ep_out = t.ep_out;
    u->mps = t.mps > 0 ? t.mps : 512;
    return true;
}

// --- transfers ----------------------------------------------------------------------------------------------

// Send everything; end: this finishes the transfer (a zero-length packet follows when it ends on a packet
// boundary; a transfer sent in several calls - a big upload - counts its length from start, total).
static int usb_write_part(Usb *u, const void *data, int len, bool end, u64 total) {
    if (u->fake_out) return u->fake_out(u, data, len);
    int done = 0;
    while (done < len) {
        int chunk = MIN(len - done, 128 << 10); // (a multiple of the packet size: one continuous transfer)
        struct usbdevfs_bulktransfer bt = {u->ep_out, (unsigned)chunk, 20000, (u8 *)data + done};
        int r = ioctl(u->fd, USBDEVFS_BULK, &bt);
        if (r < 0 && errno == EPIPE) { // the device stalled its OUT endpoint: clear it, try once more
            unsigned int ep = u->ep_out;
            ioctl(u->fd, USBDEVFS_CLEAR_HALT, &ep);
            u->halts++;
            dev_log("usb: OUT endpoint stalled, cleared");
            r = ioctl(u->fd, USBDEVFS_BULK, &bt);
        }
        if (r < 0) return -errno;
        done += r;
        if (r < chunk) return -EIO;
    }
    if (end && total > 0 && total % (u64)u->mps == 0) {
        struct usbdevfs_bulktransfer bt = {u->ep_out, 0, 20000, NULL};
        ioctl(u->fd, USBDEVFS_BULK, &bt);
    }
    return len;
}

static int usb_write(Usb *u, const void *data, int len) { return usb_write_part(u, data, len, true, (u64)len); }

// What the device sent: one bulk IN transfer (it ends at a short packet or a full buffer), up to cap; 0 when
// nothing came within timeout_ms (the read is cancelled then: what had arrived is kept). A stall or a transaction
// error halts the endpoint: it's cleared and the read reports nothing, so the caller simply reads again (iPhones
// stall now and then, and a poor cable or port gives transaction errors). <0: -errno (-ENODEV: unplugged).
static int usb_read(Usb *u, void *buf, int cap, int timeout_ms) {
    if (u->fake_in) return u->fake_in(u, buf, cap, timeout_ms);
    struct usbdevfs_urb urb;
    memset(&urb, 0, sizeof urb);
    urb.type = USBDEVFS_URB_TYPE_BULK;
    urb.endpoint = u->ep_in;
    urb.buffer = buf;
    urb.buffer_length = cap;
    if (ioctl(u->fd, USBDEVFS_SUBMITURB, &urb)) return -errno;
    struct pollfd pf = {u->fd, POLLOUT, 0};
    int pr;
    do pr = poll(&pf, 1, timeout_ms); while (pr < 0 && errno == EINTR);
    if (pr == 0) ioctl(u->fd, USBDEVFS_DISCARDURB, &urb);
    struct usbdevfs_urb *done = NULL;
    while (ioctl(u->fd, USBDEVFS_REAPURB, &done)) {
        if (errno == EINTR) continue;
        return -errno;
    }
    int st = urb.status;
    if (st == -EPIPE || st == -EPROTO || st == -EILSEQ || st == -EOVERFLOW) {
        unsigned int ep = u->ep_in;
        ioctl(u->fd, USBDEVFS_CLEAR_HALT, &ep);
        if (u->halts++ < 20 || u->halts % 100 == 0) dev_log("usb: IN %s, cleared (%d so far)", st == -EPIPE ? "stalled" : strerror(-st), u->halts);
        return urb.actual_length > 0 ? urb.actual_length : 0;
    }
    if (urb.actual_length > 0) return urb.actual_length;
    if (st == -ENOENT || st == -ECONNRESET || st == 0) return 0; // timed out (cancelled) / zero-length packet
    return st;
}
