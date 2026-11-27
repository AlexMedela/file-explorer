// devices.c - drives and network locations: block devices (lsblk), mount / unmount / eject (udisksctl),
// network mounts (GVfs FUSE, sshfs, CIFS, NFS), phones and cameras on a USB cable (GVfs MTP / AFC / gphoto2),
// and a watcher thread that notices hotplug and mount changes.

typedef struct {
    char dev[64];        // /dev/sdb1
    char disk[64];       // parent disk name (sdb) for power-off
    char label[64];
    char fstype[24];
    char uuid[64];        // filesystem UUID (several partitions share one in a multi-device btrfs)
    char mount[PATH_MAX]; // "" = not mounted
    u64 size;
    bool removable;       // RM or HOTPLUG (USB sticks, SD cards, external disks), a disc, an opened image
    bool optical;         // a CD / DVD / Blu-ray in a drive (eject opens the tray)
    char image[PATH_MAX]; // a loop device: the disc / disk image file it shows (.iso, .img), else ""
} BlockDev;

// A phone or camera on a USB cable, as GVfs's volume monitors see it (browsed through GVfs's FUSE bridge).
typedef struct {
    char name[96];        // "Pixel 7", "Alex's iPhone", "Documents on Alex's iPhone"
    char uri[512];        // what `gio mount` connects (mtp://Google_Pixel_7_2A1.../); "" = can't be connected (see need)
    char mount[PATH_MAX]; // its folder in the FUSE bridge, "" = not connected
    char kind;            // 'm' MTP (Android...), 'a' iPhone / iPad (AFC), 'c' camera (PTP via gphoto2)
    char need[32];        // a plugged-in device whose GVfs backend isn't installed: the package that adds it
    bool own;             // ours: connected by this program's own connector (devfs.c), not GVfs
    char sys[32];         // own: its USB device in sysfs ("1-2")
    char serial[96];      // own: USB serial
    char gvfs_uri[512];   // own: GVfs's way to the same device (tried when ours can't have it)
} PhoneDev;

#define MAX_BLOCKDEVS 32
#define MAX_PHONES 8
typedef struct {
    BlockDev d[MAX_BLOCKDEVS];
    int n;
    PhoneDev ph[MAX_PHONES];
    int nph;
} DevList;

// Parse one KEY="value" field of an `lsblk -P` line (values are \xNN-escaped).
static bool lsblk_field(const char *line, const char *key, char *out, size_t cap) {
    char pat[32];
    snprintf(pat, sizeof pat, "%s=\"", key);
    const char *p = line;
    for (;;) {
        p = strstr(p, pat);
        if (!p) { out[0] = 0; return false; }
        if (p == line || p[-1] == ' ') break;
        p++;
    }
    p += strlen(pat);
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < cap) {
        if (p[0] == '\\' && p[1] == 'x' && isxdigit((u8)p[2]) && isxdigit((u8)p[3])) {
            char h[3] = {p[2], p[3], 0};
            out[o++] = (char)strtol(h, NULL, 16);
            p += 4;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = 0;
    return true;
}

// Mount table: "maj:min" and source device of every mount (btrfs reports anonymous maj:min, so both).
static bool dev_mount_point(const char *majmin, const char *devpath, char *out, size_t cap) {
    FILE *f = fopen("/proc/self/mountinfo", "r");
    if (!f) return false;
    char line[PATH_MAX * 2];
    bool found = false;
    while (!found && fgets(line, sizeof line, f)) {
        char mm[32], root[PATH_MAX], mp[PATH_MAX];
        if (sscanf(line, "%*d %*d %31s %4095s %4095s", mm, root, mp) != 3) continue;
        const char *dash = strstr(line, " - ");
        char fstype[64] = "", src[PATH_MAX] = "";
        if (dash) sscanf(dash + 3, "%63s %4095s", fstype, src);
        if (!strcmp(mm, majmin) || (devpath[0] && !strcmp(src, devpath))) {
            mount_unescape(mp, out, cap);
            found = true;
        }
    }
    fclose(f);
    return found;
}

// Partition types that never belong in a sidebar (EFI, MS reserved, Windows recovery, BIOS boot, swap).
static bool dev_hidden_parttype(const char *t) {
    static const char *hide[] = {"c12a7328-f81f-11d2-ba4b-00a0c93ec93b", "e3c9e316-0b5c-4db8-817d-f92df00215ae",
                                 "de94bba4-06d1-4d40-a16a-bfd50179d6ac", "21686148-6449-6e6f-744e-656564454649",
                                 "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f", "0xef", "0x27", "0x82"};
    for (size_t i = 0; i < ARRAY_COUNT(hide); i++)
        if (str_ieq(t, hide[i])) return true;
    return false;
}

// The image file behind a loop device ("loop0"), from sysfs; false: none (a free loop device).
static bool loop_backing_file(const char *loop, char *out, size_t cap) {
    const char *sys = getenv("FE_SYS_BLOCK"); // (tests)
    char p[160];
    snprintf(p, sizeof p, "%s/%s/loop/backing_file", sys && sys[0] ? sys : "/sys/block", loop);
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t n = read(fd, out, cap - 1);
    close(fd);
    if (n <= 0) return false;
    out[n] = 0;
    out[strcspn(out, "\n")] = 0;
    return out[0] == '/';
}

// Build the list from `lsblk -P` output (separate for tests).
static void devices_parse(const char *text, DevList *L) {
    L->n = 0;
    const char *p = text;
    while (*p && L->n < MAX_BLOCKDEVS) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char line[1024];
        snprintf(line, sizeof line, "%.*s", (int)MIN(len, sizeof line - 1), p);
        p = e ? e + 1 : p + len;
        char path[64], type[16], rm[4], hp[4], mm[32], label[64], fstype[24], size[32], ptype[64], pk[64], uuid[64];
        lsblk_field(line, "PATH", path, sizeof path);
        lsblk_field(line, "TYPE", type, sizeof type);
        lsblk_field(line, "RM", rm, sizeof rm);
        lsblk_field(line, "HOTPLUG", hp, sizeof hp);
        lsblk_field(line, "MAJ:MIN", mm, sizeof mm);
        lsblk_field(line, "LABEL", label, sizeof label);
        lsblk_field(line, "FSTYPE", fstype, sizeof fstype);
        lsblk_field(line, "SIZE", size, sizeof size);
        lsblk_field(line, "PARTTYPE", ptype, sizeof ptype);
        lsblk_field(line, "PKNAME", pk, sizeof pk);
        lsblk_field(line, "UUID", uuid, sizeof uuid);
        if (!path[0] || !fstype[0]) continue;                   // nothing to mount
        bool rom = !strcmp(type, "rom"), loop = !strncmp(path, "/dev/loop", 9); // a disc; an opened image (or a snap...)
        if (strcmp(type, "part") && strcmp(type, "disk") && strcmp(type, "crypt") && strcmp(type, "lvm") && !rom && !(loop && !strcmp(type, "loop")))
            continue;
        char image[PATH_MAX] = "";
        if (loop && !loop_backing_file(pk[0] ? pk : path_basename(path), image, sizeof image)) continue;
        if (!strcmp(fstype, "swap") || !strncmp(fstype, "crypto_", 7) || !strcmp(fstype, "LVM2_member") ||
            !strcmp(fstype, "linux_raid_member") || !strcmp(fstype, "squashfs"))
            continue;
        if (!strncmp(path, "/dev/zram", 9) || dev_hidden_parttype(ptype)) continue;
        BlockDev *d = &L->d[L->n];
        memset(d, 0, sizeof *d);
        snprintf(d->dev, sizeof d->dev, "%s", path);
        snprintf(d->disk, sizeof d->disk, "%s", pk[0] ? pk : path_basename(path));
        snprintf(d->label, sizeof d->label, "%s", label);
        snprintf(d->fstype, sizeof d->fstype, "%s", fstype);
        snprintf(d->uuid, sizeof d->uuid, "%s", uuid);
        d->size = strtoull(size, NULL, 10);
        d->removable = !strcmp(rm, "1") || !strcmp(hp, "1") || rom || loop;
        d->optical = rom;
        snprintf(d->image, sizeof d->image, "%s", image);
        dev_mount_point(mm, path, d->mount, sizeof d->mount);
        L->n++;
    }
}

// Prefix for the `sh -c` wrappers below: the tools' messages in English (we read them: "Mounted ... at", gio's
// prompts, journal lines), the user's character set kept so non-ASCII names come through.
#define SH_ENGLISH "export LC_CTYPE=\"${LC_ALL:-${LC_CTYPE:-$LANG}}\"; unset LC_ALL LANGUAGE; export LC_MESSAGES=C; "

// Run a program, capture stdout+stderr (trimmed to cap). Returns the exit status (-1: couldn't run).
// SIGCHLD is ignored app-wide (children reap themselves), which throws exit codes away, so the command runs
// under `sh` and prints its status as a last line that we strip off.
static int run_capture(char *const argv[], char *out, size_t cap) {
    int pfd[2];
    if (out && cap) out[0] = 0;
    if (pipe2(pfd, O_CLOEXEC)) return -1;
    int na = 0;
    while (argv[na]) na++;
    char **av = xmalloc((size_t)(na + 5) * sizeof(char *));
    av[0] = "sh";
    av[1] = "-c";
    av[2] = SH_ENGLISH "\"$@\"; printf '\\n@rc=%d\\n' $?";
    av[3] = "sh";
    for (int i = 0; i <= na; i++) av[4 + i] = argv[i];
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);
    posix_spawn_file_actions_adddup2(&fa, pfd[1], 2);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    pid_t pid;
    int rc = posix_spawnp(&pid, "sh", &fa, spawn_attr(), av, environ);
    posix_spawn_file_actions_destroy(&fa);
    free(av);
    close(pfd[1]);
    if (rc) { close(pfd[0]); return -1; }
    size_t o = 0, bcap = 8192;
    char *buf = xmalloc(bcap), tmp[4096];
    for (;;) {
        ssize_t r = read(pfd[0], tmp, sizeof tmp);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        if (o + (size_t)r + 1 > bcap) { bcap = (o + (size_t)r + 1) * 2; buf = xrealloc(buf, bcap); }
        memcpy(buf + o, tmp, (size_t)r);
        o += (size_t)r;
    }
    close(pfd[0]);
    buf[o] = 0;
    int status = -1;
    char *m = NULL;
    for (char *q = buf; (q = strstr(q, "\n@rc=")); q++) m = q; // last marker
    if (m) { status = atoi(m + 5); *m = 0; }
    for (size_t l = strlen(buf); l && (buf[l - 1] == '\n' || buf[l - 1] == ' '); ) buf[--l] = 0;
    if (out && cap) snprintf(out, cap, "%s", buf);
    free(buf);
    return status == 127 ? -1 : status; // 127: command not found
}

static const char *tool(const char *env, const char *def) { // tests swap in fake tools
    const char *v = getenv(env);
    return v && v[0] ? v : def;
}

static void phones_scan(DevList *L);

// Drives only (lsblk, a few ms); phones_scan fills in the rest (`gio mount -li` can take half a second).
static void drives_scan(DevList *L) {
    char *out = xmalloc(65536);
    char *argv[] = {(char *)tool("FE_LSBLK", "lsblk"), "-P", "-b", "-o",
                    "PATH,PKNAME,TYPE,RM,HOTPLUG,MAJ:MIN,LABEL,FSTYPE,SIZE,PARTTYPE,UUID", NULL};
    L->n = 0;
    if (run_capture(argv, out, 65536) == 0) devices_parse(out, L);
    free(out);
}

// --- disk health: temperature (hwmon) and SMART (udisks), both without root ------------------------------------
// Read on a worker about once a minute for the drives listed in the sidebar; shown only when something's wrong (a dot),
// on hover, and in the details panel at a drive's top folder.

#define MAX_DISKS 16
#define DH_NO_TEMP (-100000)
typedef struct {
    char disk[32];     // nvme0n1, sda
    char model[64];
    int temp;          // tenths of a degree Celsius, DH_NO_TEMP: unknown
    bool smart;        // udisks knows its SMART data
    bool failing;      // the drive says it's failing (SATA)
    char warning[96];  // NVMe critical warnings, bad sectors... ("" none)
    i64 hours;         // powered on (-1 unknown)
} DiskHealth;
typedef struct {
    DiskHealth d[MAX_DISKS];
    int n;
    struct { char dev[32]; int di; } map[MAX_BLOCKDEVS]; // a listed device (sdb1, dm-0) -> its disk in d
    int nmap;
} DiskHealthList;

static const char *sys_class_block(void) {
    const char *e = getenv("FE_SYS_CLASS_BLOCK"); // (tests)
    return e && e[0] ? e : "/sys/class/block";
}

// The physical disk under a block device (a partition's disk, what LUKS / LVM sit on); false: none (zram, loop...).
static bool disk_of(const char *name, char *out, size_t cap) {
    char cur[64], p[PATH_MAX];
    snprintf(cur, sizeof cur, "%s", name);
    for (int depth = 0; depth < 8; depth++) {
        if (!strncmp(cur, "loop", 4) || !strncmp(cur, "zram", 4) || !strncmp(cur, "ram", 3) || !strncmp(cur, "sr", 2)) return false;
        snprintf(p, sizeof p, "%s/%s/slaves", sys_class_block(), cur);
        DIR *d = opendir(p);
        bool down = false;
        if (d) { // device-mapper (LUKS, LVM): what it's built on
            struct dirent *de;
            while ((de = readdir(d)))
                if (de->d_name[0] != '.') { snprintf(cur, sizeof cur, "%s", de->d_name); down = true; break; }
            closedir(d);
        }
        if (down) continue;
        snprintf(p, sizeof p, "%s/%s/partition", sys_class_block(), cur);
        if (access(p, F_OK) == 0) { // a partition: its disk is the folder above it
            char link[PATH_MAX], real[PATH_MAX];
            snprintf(link, sizeof link, "%s/%s", sys_class_block(), cur);
            if (!realpath(link, real)) return false;
            char *sl = strrchr(real, '/');
            if (!sl) return false;
            *sl = 0;
            snprintf(cur, sizeof cur, "%s", path_basename(real));
            continue;
        }
        snprintf(out, cap, "%s", cur);
        return true;
    }
    return false;
}

// A disk's temperature from its hwmon sensor (NVMe always has one; SATA with the drivetemp module), DH_NO_TEMP: none.
static int hwmon_temp(const char *disk) {
    char base[PATH_MAX];
    for (int k = 0; k < 2; k++) {
        snprintf(base, sizeof base, k ? "%s/%s/device/hwmon" : "%s/%s/device", sys_class_block(), disk);
        DIR *d = opendir(base);
        if (!d) continue;
        struct dirent *de;
        int t = DH_NO_TEMP;
        while ((de = readdir(d)) && t == DH_NO_TEMP) {
            if (strncmp(de->d_name, "hwmon", 5) || !de->d_name[5]) continue;
            char f[PATH_MAX + 64], buf[32] = "";
            snprintf(f, sizeof f, "%s/%s/temp1_input", base, de->d_name);
            int fd = open(f, O_RDONLY | O_CLOEXEC);
            if (fd < 0) continue;
            ssize_t n = read(fd, buf, sizeof buf - 1);
            close(fd);
            if (n > 0) { buf[n] = 0; t = (int)(atol(buf) / 100); } // millidegrees -> tenths
        }
        closedir(d);
        if (t != DH_NO_TEMP) return t;
    }
    return DH_NO_TEMP;
}

// `udisksctl dump`: block devices name their drive object; drives carry the model and the SMART properties.
static void udisks_health_parse(const char *text, DiskHealthList *L) {
    enum { MAXD = 32 };
    struct { char id[160], model[64], warn[96]; f64 kelvin; i64 hours; bool smart, failing; i64 bad; } dr[MAXD];
    struct { char name[32], id[160]; } bl[64];
    int nd = 0, nb = 0, cur_d = -1, cur_b = -1;
    const char *p = text;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char line[512];
        snprintf(line, sizeof line, "%.*s", (int)MIN(len, sizeof line - 1), p);
        p = e ? e + 1 : p + len;
        if (!strncmp(line, "/org/freedesktop/UDisks2/", 25)) {
            cur_d = cur_b = -1;
            char *c = strrchr(line, ':');
            if (c) *c = 0;
            if (!strncmp(line + 25, "drives/", 7) && nd < MAXD) {
                cur_d = nd++;
                memset(&dr[cur_d], 0, sizeof dr[cur_d]);
                dr[cur_d].hours = -1;
                snprintf(dr[cur_d].id, sizeof dr[cur_d].id, "%s", line + 32);
            } else if (!strncmp(line + 25, "block_devices/", 14) && nb < 64) {
                cur_b = nb++;
                snprintf(bl[cur_b].name, sizeof bl[cur_b].name, "%s", line + 39);
                bl[cur_b].id[0] = 0;
            }
            continue;
        }
        char *k = line;
        while (*k == ' ') k++;
        char *colon = strchr(k, ':');
        if (!colon || k - line < 4) continue; // (interface names: two spaces in)
        *colon = 0;
        char *v = colon + 1;
        while (*v == ' ') v++;
        for (size_t l = strlen(v); l && v[l - 1] == ' '; ) v[--l] = 0;
        if (cur_b >= 0 && !strcmp(k, "Drive")) {
            const char *q = strstr(v, "/drives/");
            if (q) { snprintf(bl[cur_b].id, sizeof bl[cur_b].id, "%s", q + 8); bl[cur_b].id[strcspn(bl[cur_b].id, "'")] = 0; }
        } else if (cur_d >= 0) {
            if (!strcmp(k, "Model")) snprintf(dr[cur_d].model, sizeof dr[cur_d].model, "%s", v);
            else if (!strcmp(k, "SmartTemperature")) { dr[cur_d].kelvin = atof(v); dr[cur_d].smart = true; }
            else if (!strcmp(k, "SmartPowerOnHours")) { dr[cur_d].hours = atoll(v); dr[cur_d].smart = true; }
            else if (!strcmp(k, "SmartPowerOnSeconds")) { dr[cur_d].hours = atoll(v) / 3600; dr[cur_d].smart = true; }
            else if (!strcmp(k, "SmartFailing")) { dr[cur_d].failing = !strcmp(v, "true"); dr[cur_d].smart = true; }
            else if (!strcmp(k, "SmartNumBadSectors")) dr[cur_d].bad = atoll(v);
            else if (!strcmp(k, "SmartCriticalWarning")) snprintf(dr[cur_d].warn, sizeof dr[cur_d].warn, "%s", v);
        }
    }
    for (int i = 0; i < L->n; i++) {
        DiskHealth *h = &L->d[i];
        for (int b = 0; b < nb; b++) {
            if (strcmp(bl[b].name, h->disk) || !bl[b].id[0]) continue;
            for (int k = 0; k < nd; k++) {
                if (strcmp(dr[k].id, bl[b].id)) continue;
                if (dr[k].model[0]) snprintf(h->model, sizeof h->model, "%s", dr[k].model);
                h->smart = dr[k].smart;
                h->failing = dr[k].failing;
                h->hours = dr[k].hours;
                if (dr[k].warn[0]) snprintf(h->warning, sizeof h->warning, "%s", dr[k].warn);
                else if (dr[k].bad > 0) snprintf(h->warning, sizeof h->warning, "%lld bad sectors", (long long)dr[k].bad);
                if (h->temp == DH_NO_TEMP && dr[k].kelvin > 200) h->temp = (int)lround((dr[k].kelvin - 273.15) * 10);
            }
        }
    }
}

// Fill in L's disks (their names set): model, temperature, then udisks' SMART data (when it's there).
static void disk_health_scan(DiskHealthList *L) {
    for (int i = 0; i < L->n; i++) {
        DiskHealth *h = &L->d[i];
        h->temp = hwmon_temp(h->disk);
        h->hours = -1;
        char p[PATH_MAX], buf[96] = "";
        snprintf(p, sizeof p, "%s/%s/device/model", sys_class_block(), h->disk);
        int fd = open(p, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            ssize_t n = read(fd, buf, sizeof buf - 1);
            close(fd);
            buf[n > 0 ? n : 0] = 0;
            for (size_t l = strlen(buf); l && (buf[l - 1] == '\n' || buf[l - 1] == ' '); ) buf[--l] = 0;
            snprintf(h->model, sizeof h->model, "%s", buf);
        }
    }
    char *out = xmalloc(1 << 20);
    char *argv[] = {(char *)tool("FE_UDISKSCTL", "udisksctl"), "dump", NULL};
    if (L->n && run_capture(argv, out, 1 << 20) == 0) udisks_health_parse(out, L);
    free(out);
}

// How bad (0 fine, 1 a warning or hot, 2 failing) and a line for it ("Good", "Failing: back it up now"...).
static int disk_health_level(const DiskHealth *h, char *what, size_t cap) {
    int hot = !strncmp(h->disk, "nvme", 4) ? 700 : 550; // tenths of a degree
    if (h->failing) { snprintf(what, cap, "Failing: back it up now"); return 2; }
    if (h->warning[0]) { snprintf(what, cap, "Warning: %s", h->warning); return 1; }
    if (h->temp != DH_NO_TEMP && h->temp >= hot) { snprintf(what, cap, "Running hot"); return 1; }
    snprintf(what, cap, h->smart ? "Good" : "");
    return 0;
}

// --- network mounts ------------------------------------------------------------

typedef struct { char path[PATH_MAX]; char label[64]; bool sshfs; bool rclone; } NetMount; // sshfs/rclone: label is the mount source

// "smb-share:server=nas,share=media" -> "media on nas"; "sftp:host=box,user=me" -> "me@box"
static void gvfs_label(const char *name, char *out, size_t cap) {
    char server[64] = "", share[64] = "", user[64] = "";
    const char *colon = strchr(name, ':');
    char scheme[24];
    snprintf(scheme, sizeof scheme, "%.*s", colon ? (int)(colon - name) : (int)strlen(name), name);
    for (const char *p = colon ? colon + 1 : ""; *p;) {
        const char *e = strchr(p, ',');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        char kv[160];
        snprintf(kv, sizeof kv, "%.*s", (int)MIN(l, sizeof kv - 1), p);
        char *eq = strchr(kv, '=');
        if (eq) {
            *eq = 0;
            char *dst = !strcmp(kv, "server") || !strcmp(kv, "host") ? server : !strcmp(kv, "share") ? share : !strcmp(kv, "user") ? user : NULL;
            if (dst) {
                size_t o = 0; // values are %-escaped
                for (const char *c = eq + 1; *c && o + 1 < 64; c++) {
                    if (c[0] == '%' && isxdigit((u8)c[1]) && isxdigit((u8)c[2])) {
                        char h[3] = {c[1], c[2], 0};
                        dst[o++] = (char)strtol(h, NULL, 16);
                        c += 2;
                    } else dst[o++] = *c;
                }
                dst[o] = 0;
            }
        }
        p += l + (e ? 1 : 0);
    }
    if (share[0] && server[0]) snprintf(out, cap, "%s on %s", share, server);
    else if (user[0] && server[0]) snprintf(out, cap, "%s@%s", user, server);
    else if (server[0]) snprintf(out, cap, "%s (%s)", server, scheme);
    else snprintf(out, cap, "%s", name);
}

// Where GVfs's FUSE bridge shows its mounts as plain folders.
static void gvfs_dir(char *out, size_t cap) {
    const char *ov = getenv("FE_GVFS_DIR");
    if (ov && ov[0]) snprintf(out, cap, "%s", ov);
    else snprintf(out, cap, "%s/gvfs", getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "/run/user/0");
}

static bool gvfs_fuse_running(void) {
    if (getenv("FE_GVFS_DIR")) return true; // tests: a plain folder stands in
    char gdir[PATH_MAX], line[PATH_MAX * 2], src[PATH_MAX], mp[PATH_MAX], type[64];
    gvfs_dir(gdir, sizeof gdir);
    FILE *f = fopen("/proc/self/mounts", "r");
    bool up = false;
    while (f && !up && fgets(line, sizeof line, f)) {
        char m2[PATH_MAX];
        if (sscanf(line, "%4095s %4095s %63s", src, mp, type) != 3) continue;
        mount_unescape(mp, m2, sizeof m2);
        up = !strcmp(m2, gdir) && strstr(type, "fuse");
    }
    if (f) fclose(f);
    return up;
}

// Where a GVfs helper program is installed (gvfsd-fuse, gvfsd-mtp...): distros put them in different places
// (Arch /usr/lib, Fedora /usr/libexec, Debian /usr/libexec/gvfs or /usr/lib/<multiarch>/gvfs).
static bool gvfs_helper_path(const char *name, char *out, size_t cap) {
    static const char *dirs[] = {"/usr/lib", "/usr/libexec", "/usr/libexec/gvfs", "/usr/lib/gvfs", "/usr/lib64/gvfs"};
    for (size_t k = 0; k < ARRAY_COUNT(dirs); k++) {
        snprintf(out, cap, "%s/%s", dirs[k], name);
        if (access(out, X_OK) == 0) return true;
    }
    DIR *d = opendir("/usr/lib"); // multiarch: /usr/lib/x86_64-linux-gnu/gvfs, /usr/lib/aarch64-linux-gnu/gvfs...
    struct dirent *e;
    bool found = false;
    while (d && !found && (e = readdir(d))) {
        if (!strstr(e->d_name, "-linux-")) continue;
        snprintf(out, cap, "/usr/lib/%s/gvfs/%s", e->d_name, name);
        found = access(out, X_OK) == 0;
    }
    if (d) closedir(d);
    return found;
}

// GVfs connects fine without its FUSE bridge (gvfsd-fuse), but then its mounts have no folder to browse. A desktop
// session normally keeps it running; if it isn't (it exited, or the session never started it), start it.
static bool gvfs_fuse_ensure(void) {
    if (gvfs_fuse_running()) return true;
    char bin[PATH_MAX];
    if (!gvfs_helper_path("gvfsd-fuse", bin, sizeof bin)) return false;
    char gdir[PATH_MAX];
    gvfs_dir(gdir, sizeof gdir);
    mkdir(gdir, 0700);
    char *argv[] = {bin, gdir, NULL}; // daemonizes itself
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t at;
    spawn_attr_init(&at, POSIX_SPAWN_SETSID);
    pid_t pid;
    int rc = posix_spawn(&pid, bin, &fa, &at, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    if (rc) return false;
    for (int k = 0; k < 40; k++) { // up to ~2 s
        if (gvfs_fuse_running()) return true;
        usleep(50000);
    }
    return false;
}

static char phone_kind(const char *s);

static int network_mounts(NetMount *out, int max) {
    int n = 0;
    char gdir[PATH_MAX];
    gvfs_dir(gdir, sizeof gdir);
    DIR *d = opendir(gdir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) && n < max) {
            if (de->d_name[0] == '.' || phone_kind(de->d_name)) continue; // phones are listed under Devices
            path_join(out[n].path, sizeof out[n].path, gdir, de->d_name);
            gvfs_label(de->d_name, out[n].label, sizeof out[n].label);
            out[n].sshfs = false;
            out[n].rclone = false;
            n++;
        }
        closedir(d);
    }
    const char *ov = getenv("FE_NET_MOUNTS"); // tests: a made-up mount table
    FILE *f = fopen(ov ? ov : "/proc/self/mounts", "r");
    if (f) {
        char line[PATH_MAX * 2], src[PATH_MAX], mp[PATH_MAX], type[64];
        while (n < max && fgets(line, sizeof line, f)) {
            if (sscanf(line, "%4095s %4095s %63s", src, mp, type) != 3) continue;
            if (strcmp(type, "fuse.sshfs") && strcmp(type, "cifs") && strcmp(type, "smb3") && strcmp(type, "nfs") &&
                strcmp(type, "nfs4") && strcmp(type, "fuse.rclone"))
                continue;
            mount_unescape(mp, out[n].path, sizeof out[n].path);
            mount_unescape(src, out[n].label, sizeof out[n].label); // (sshfs: user@host:/dir, which can have spaces too)
            out[n].sshfs = !strcmp(type, "fuse.sshfs");
            out[n].rclone = !strcmp(type, "fuse.rclone");
            n++;
        }
        fclose(f);
    }
    return n;
}

// --- phones and cameras on a USB cable (GVfs: MTP for Android, AFC for iPhone / iPad, gphoto2 for cameras) ------

// GVfs backend daemon installed? (gvfsd-mtp, gvfsd-afc...; FE_GVFS_LIBEXEC: tests)
static bool gvfs_backend_installed(const char *backend) {
    const char *ov = getenv("FE_GVFS_LIBEXEC");
    char p[PATH_MAX], name[64];
    snprintf(name, sizeof name, "gvfsd-%s", backend);
    if (!ov) return gvfs_helper_path(name, p, sizeof p);
    snprintf(p, sizeof p, "%s/%s", ov, name);
    return access(p, X_OK) == 0;
}

// The kind of device a URI ("mtp://...") or a FUSE folder name ("mtp:host=...") is for; 0 = not a phone / camera.
static char phone_kind(const char *s) {
    size_t l = strcspn(s, ":");
    if (!s[l]) return 0;
    if (l == 3 && !strncmp(s, "mtp", 3)) return 'm';
    if (l == 3 && !strncmp(s, "afc", 3)) return 'a';
    if (l == 7 && !strncmp(s, "gphoto2", 7)) return 'c';
    return 0;
}

static void pct_decode(const char *s, size_t n, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < n && s[i] && o + 1 < cap; i++) {
        if (s[i] == '%' && i + 2 < n && isxdigit((u8)s[i + 1]) && isxdigit((u8)s[i + 2])) {
            char h[3] = {s[i + 1], s[i + 2], 0};
            out[o++] = (char)strtol(h, NULL, 16);
            i += 2;
        } else out[o++] = s[i];
    }
    out[o] = 0;
}

// Which device a URI (mtp://HOST/, afc://UDID:3/) or a FUSE folder name (mtp:host=HOST, afc:host=UDID,port=3) is,
// as "kind|host|port" with the host unescaped (gphoto2 hosts like [usb:001,005] come %-escaped). uri (optional):
// the URI that connects it again.
static bool phone_key(const char *s, char *key, size_t cap, char *uri, size_t ucap) {
    char k = phone_kind(s);
    if (!k) return false;
    size_t sl = strcspn(s, ":");
    const char *p = s + sl + 1;
    char host[256] = "", raw[256] = "", port[16] = "";
    if (!strncmp(p, "//", 2)) { // URI
        p += 2;
        size_t hl = strcspn(p, ":/");
        snprintf(raw, sizeof raw, "%.*s", (int)MIN(hl, sizeof raw - 1), p);
        if (p[hl] == ':') snprintf(port, sizeof port, "%.*s", (int)MIN(strcspn(p + hl + 1, "/"), sizeof port - 1), p + hl + 1);
    } else {
        while (*p) {
            size_t l = strcspn(p, ",");
            if (!strncmp(p, "host=", 5)) snprintf(raw, sizeof raw, "%.*s", (int)MIN(l - 5, sizeof raw - 1), p + 5);
            else if (!strncmp(p, "port=", 5)) snprintf(port, sizeof port, "%.*s", (int)MIN(l - 5, sizeof port - 1), p + 5);
            p += l + (p[l] == ',');
        }
    }
    pct_decode(raw, strlen(raw), host, sizeof host);
    snprintf(key, cap, "%c|%s|%s", k, host, port);
    if (uri) snprintf(uri, ucap, "%.*s://%s%s%s/", (int)sl, s, raw, port[0] ? ":" : "", port);
    return host[0] != 0;
}

// A name for a connected device the volume monitor doesn't list: "Google_Pixel_7_2A111FDH" -> "Google Pixel 7".
static void phone_default_name(const char *key, char *out, size_t cap) {
    const char *host = key + 2, *bar = strchr(host, '|');
    int hl = bar ? (int)(bar - host) : (int)strlen(host);
    if (key[0] == 'a') { snprintf(out, cap, bar && bar[1] ? "Documents on iPhone" : "iPhone"); return; }
    if (key[0] == 'c' && host[0] == '[') { snprintf(out, cap, "Camera"); return; }
    snprintf(out, cap, "%.*s", hl, host);
    for (char *c = out; *c; c++) if (*c == '_') *c = ' ';
    char *sp = strrchr(out, ' '); // a trailing serial number
    if (sp && strlen(sp + 1) >= 8 && strpbrk(sp + 1, "0123456789")) *sp = 0;
}

// Phones and cameras in `gio mount -li` output (separate for tests): every "Volume(n): NAME" whose activation_root
// is an mtp:// / afc:// / gphoto2:// URI. A volume's lines are indented under it; its mount (if any) nests deeper.
static void phones_parse(const char *text, DevList *L) {
    L->nph = 0;
    char name[96] = "", uri[512] = "";
    int vind = -1; // indentation of the volume being read, -1: none
    for (const char *p = text;;) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char line[1024];
        snprintf(line, sizeof line, "%.*s", (int)MIN(len, sizeof line - 1), p);
        int ind = (int)strspn(line, " ");
        const char *t = line + ind;
        bool head = !strncmp(t, "Volume(", 7) || !strncmp(t, "Drive(", 6) || !strncmp(t, "Mount(", 6);
        if (vind >= 0 && ((head && ind <= vind) || !e)) { // the volume ends: keep it if it's a phone / camera
            if (vind >= 0 && !e && !strncmp(t, "activation_root=", 16)) snprintf(uri, sizeof uri, "%s", t + 16);
            bool dup = false;
            for (int i = 0; i < L->nph && !dup; i++) dup = !strcmp(L->ph[i].uri, uri);
            if (phone_kind(uri) && !dup && L->nph < MAX_PHONES) {
                PhoneDev *ph = &L->ph[L->nph++];
                memset(ph, 0, sizeof *ph);
                snprintf(ph->name, sizeof ph->name, "%s", name);
                snprintf(ph->uri, sizeof ph->uri, "%s", uri);
                ph->kind = phone_kind(uri);
            }
            vind = -1;
        }
        if (!strncmp(t, "Volume(", 7)) {
            const char *c = strstr(t, "): ");
            snprintf(name, sizeof name, "%s", c ? c + 3 : "");
            uri[0] = 0;
            vind = ind;
        } else if (vind >= 0 && !strncmp(t, "activation_root=", 16)) snprintf(uri, sizeof uri, "%s", t + 16);
        if (!e) break;
        p = e + 1;
    }
}

// An iPhone / iPad on the USB bus (Apple's vendor id, their product id range), whatever GVfs makes of it.
static bool usb_has_iphone(void) {
    const char *dir = getenv("FE_SYS_USB") ? getenv("FE_SYS_USB") : "/sys/bus/usb/devices";
    DIR *d = opendir(dir);
    if (!d) return false;
    bool found = false;
    struct dirent *de;
    while (!found && (de = readdir(d))) {
        if (de->d_name[0] == '.' || strchr(de->d_name, ':')) continue; // interfaces
        char path[PATH_MAX], v[16] = "", pr[16] = "";
        snprintf(path, sizeof path, "%s/%s/idVendor", dir, de->d_name);
        FILE *f = fopen(path, "r");
        if (f) { if (!fgets(v, sizeof v, f)) v[0] = 0; fclose(f); }
        if (strncmp(v, "05ac", 4)) continue;
        snprintf(path, sizeof path, "%s/%s/idProduct", dir, de->d_name);
        f = fopen(path, "r");
        if (f) { if (!fgets(pr, sizeof pr, f)) pr[0] = 0; fclose(f); }
        long id = strtol(pr, NULL, 16);
        found = id >= 0x1290 && id <= 0x12af;
    }
    closedir(d);
    return found;
}

// Phones and cameras as GVfs sees them: what the volume monitors list, which of them are connected (their folders
// in the FUSE bridge - also ones another program connected), and (hint) an iPhone GVfs can't open because
// gvfs-afc isn't installed.
static void phones_scan_gvfs(DevList *L, bool hint) {
    char *out = xmalloc(65536);
    char *argv[] = {(char *)tool("FE_GIO", "gio"), "mount", "-li", NULL};
    L->nph = 0;
    if (run_capture(argv, out, 65536) == 0) phones_parse(out, L);
    free(out);
    char gdir[PATH_MAX];
    gvfs_dir(gdir, sizeof gdir);
    DIR *d = opendir(gdir);
    struct dirent *de;
    while (d && (de = readdir(d))) {
        char key[400], k2[400], uri[512];
        if (de->d_name[0] == '.' || !phone_key(de->d_name, key, sizeof key, uri, sizeof uri)) continue;
        PhoneDev *ph = NULL;
        for (int i = 0; i < L->nph && !ph; i++)
            if (phone_key(L->ph[i].uri, k2, sizeof k2, NULL, 0) && !strcmp(key, k2)) ph = &L->ph[i];
        if (!ph && L->nph < MAX_PHONES) { // connected, but not listed (its volume monitor isn't running)
            ph = &L->ph[L->nph++];
            memset(ph, 0, sizeof *ph);
            ph->kind = key[0];
            snprintf(ph->uri, sizeof ph->uri, "%s", uri);
            phone_default_name(key, ph->name, sizeof ph->name);
        }
        if (ph) path_join(ph->mount, sizeof ph->mount, gdir, de->d_name);
    }
    if (d) closedir(d);
    // an iPhone also shows up as a camera (PTP, photos only): its AFC media entry covers that, so the camera goes;
    // without one (the volume monitor lists only "Documents on ...") it stays, named for what it holds
    for (int i = 0; i < L->nph; i++) {
        char key[400], ak[400];
        if (L->ph[i].kind != 'c' || !phone_key(L->ph[i].uri, key, sizeof key, NULL, 0)) continue;
        bool dup = false;
        for (int k = 0; k < L->nph && !dup; k++) {
            if (L->ph[k].kind != 'a' || !phone_key(L->ph[k].uri, ak, sizeof ak, NULL, 0) || ak[strlen(ak) - 1] != '|') continue;
            char udid[256];
            size_t o = 0;
            for (const char *c = ak + 2; *c && *c != '|' && o + 1 < sizeof udid; c++) if (*c != '-') udid[o++] = *c;
            udid[o] = 0;
            dup = o >= 8 && strcasestr(key, udid); // gphoto2 host "Apple_Inc._iPhone_<UDID without dashes>"
        }
        if (dup) { memmove(&L->ph[i], &L->ph[i + 1], (size_t)(L->nph - i - 1) * sizeof L->ph[0]); L->nph--; i--; }
        else if (strstr(key, "Apple_Inc.") && strstr(L->ph[i].name, "iPhone") && !strstr(L->ph[i].name, "hotos"))
            snprintf(L->ph[i].name, sizeof L->ph[i].name, "Photos on iPhone");
    }
    bool have_iphone = false;
    for (int i = 0; i < L->nph; i++) have_iphone |= L->ph[i].kind == 'a';
    if (hint && !have_iphone && L->nph < MAX_PHONES && !gvfs_backend_installed("afc") && usb_has_iphone()) {
        PhoneDev *ph = &L->ph[L->nph++];
        memset(ph, 0, sizeof *ph);
        ph->kind = 'a';
        snprintf(ph->name, sizeof ph->name, "iPhone");
        snprintf(ph->need, sizeof ph->need, "gvfs-afc");
    }
}

// Our own connector (usb.c + iphone.c / mtp.c, served by devfs.c) for what it can talk to, GVfs for the rest.
// FE_NO_OWN_PHONES turns ours off; self-tests use it only with a made-up USB bus.
static bool own_phones_enabled(void) { return !getenv("FE_NO_OWN_PHONES") && (!getenv("FE_GIO") || getenv("FE_SYS_USB")); }

// Where our helper mounted a device: its /proc/self/mounts line reads "fe-device:<id> <mount point> fuse.fe-device".
static bool own_mount_of(const char *id, char *out, size_t cap) {
    FILE *f = fopen("/proc/self/mounts", "r");
    char line[PATH_MAX * 2], src[PATH_MAX], mp[PATH_MAX], type[64], want[200];
    snprintf(want, sizeof want, "fe-device:%s", id);
    for (char *c = want; *c; c++) if (*c == ' ') *c = '_';
    bool found = false;
    while (f && !found && fgets(line, sizeof line, f)) {
        if (sscanf(line, "%4095s %4095s %63s", src, mp, type) != 3 || strcmp(type, "fuse.fe-device") || strcmp(src, want)) continue;
        mount_unescape(mp, out, cap);
        found = true;
    }
    if (f) fclose(f);
    return found;
}

// Letters and digits only, upper case: an Apple UDID with or without its dash, an Android serial inside GVfs's
// "Google_Pixel_7_<serial>" all compare alike.
static void serial_norm(const char *s, char *out, size_t cap) {
    size_t o = 0;
    for (; *s && o + 1 < cap; s++) if (isalnum((u8)*s)) out[o++] = (char)toupper((u8)*s);
    out[o] = 0;
}

// --- Android over ADB (adbfs): the phone's whole storage, also what MTP doesn't show -------------------------------
// Only when both adb and adbfs are installed (so nobody else gets an adb server started). A phone with USB debugging on
// is listed as "<model> (ADB)"; connecting runs adbfs (FUSE, no root) on a folder of ours; Eject unmounts it.

static bool app_selftest(void); // app.c

static const char *adb_tool(void) { return tool("FE_ADB", "adb"); }
static const char *adbfs_tool(void) { return tool("FE_ADBFS", "adbfs"); }
static bool adb_available(void) {
    const char *a = adb_tool(), *f = adbfs_tool();
    return (a[0] == '/' ? access(a, X_OK) == 0 : in_path(a)) && (f[0] == '/' ? access(f, X_OK) == 0 : in_path(f));
}

// Our folder for a phone's adbfs mount (by its serial).
static void adb_mount_dir(const char *serial, char *out, size_t cap) {
    const char *d = getenv("FE_ADB_DIR"), *rt = getenv("XDG_RUNTIME_DIR"); // (tests)
    if (d && d[0]) snprintf(out, cap, "%s/%s", d, serial);
    else snprintf(out, cap, "%s/file-explorer-adb/%s", rt && rt[0] ? rt : "/tmp", serial);
}

// Is an adbfs mounted there? (/proc/self/mounts; FE_ADB_MOUNTS: tests)
static bool adb_mounted(const char *mp) {
    const char *ov = getenv("FE_ADB_MOUNTS");
    FILE *f = fopen(ov ? ov : "/proc/self/mounts", "r");
    char line[PATH_MAX * 2], src[PATH_MAX], m[PATH_MAX], type[64], clean[PATH_MAX];
    bool found = false;
    while (f && !found && fgets(line, sizeof line, f)) {
        if (sscanf(line, "%4095s %4095s %63s", src, m, type) != 3 || strcmp(type, "fuse.adbfs")) continue;
        mount_unescape(m, clean, sizeof clean);
        found = !strcmp(clean, mp);
    }
    if (f) fclose(f);
    return found;
}

// `adb devices -l`: "SERIAL  device usb:1-2 product:x model:Pixel_7 device:y transport_id:1" (or unauthorized,
// "no permissions (...)", offline) -> phones of kind 'd' after the others.
static void adb_parse(const char *text, DevList *L) {
    const char *p = text;
    while (*p && L->nph < MAX_PHONES) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char line[512], serial[96] = "", state[32] = "";
        snprintf(line, sizeof line, "%.*s", (int)MIN(len, sizeof line - 1), p);
        p = e ? e + 1 : p + len;
        if (sscanf(line, "%95s %31s", serial, state) != 2 || !strcmp(serial, "List") || serial[0] == '*') continue;
        if (strcmp(state, "device") && strcmp(state, "unauthorized") && strcmp(state, "no")) continue; // (offline...)
        PhoneDev *ph = &L->ph[L->nph++];
        memset(ph, 0, sizeof *ph);
        ph->kind = 'd';
        snprintf(ph->serial, sizeof ph->serial, "%s", serial);
        snprintf(ph->uri, sizeof ph->uri, "adb:%s", serial);
        const char *mo = strstr(line, " model:");
        char model[96] = "Android";
        if (mo) {
            snprintf(model, sizeof model, "%.*s", (int)strcspn(mo + 7, " \t"), mo + 7);
            for (char *c = model; *c; c++) if (*c == '_') *c = ' ';
        }
        snprintf(ph->name, sizeof ph->name, "%s (ADB)", model);
        if (!strcmp(state, "unauthorized")) snprintf(ph->need, sizeof ph->need, "allow");
        else if (!strcmp(state, "no")) snprintf(ph->need, sizeof ph->need, "permission");
        char mp[PATH_MAX];
        adb_mount_dir(serial, mp, sizeof mp);
        if (adb_mounted(mp)) snprintf(ph->mount, sizeof ph->mount, "%s", mp);
    }
}

// ADB phones into L; an adbfs mount of ours whose phone went away is let go (its folder would only hang).
static void phones_scan_adb(DevList *L) {
    if (!adb_available() || (app_selftest() && !getenv("FE_ADB"))) return; // (self-tests: only the stand-in)
    char *out = xmalloc(16384);
    char *argv[] = {(char *)adb_tool(), "devices", "-l", NULL};
    int n0 = L->nph;
    if (run_capture(argv, out, 16384) == 0) adb_parse(out, L);
    free(out);
    char dir[PATH_MAX];
    adb_mount_dir("", dir, sizeof dir);
    DIR *d = opendir(dir);
    struct dirent *de;
    while (d && (de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        bool listed = false;
        for (int i = n0; i < L->nph && !listed; i++) listed = !strcmp(L->ph[i].serial, de->d_name);
        char mp[PATH_MAX];
        adb_mount_dir(de->d_name, mp, sizeof mp);
        if (listed || !adb_mounted(mp)) continue;
        char *um[] = {(char *)tool("FE_FUSERMOUNT", "fusermount3"), "-u", "-z", mp, NULL}, tmp[256];
        run_capture(um, tmp, sizeof tmp);
    }
    if (d) closedir(d);
}

// Connect: adbfs on our folder for it (ANDROID_SERIAL picks the phone), then wait for the mount to show up.
static int adb_mount_run(const char *serial, char *mount, size_t mcap, char *out, size_t ocap) {
    char mp[PATH_MAX], env[160];
    adb_mount_dir(serial, mp, sizeof mp);
    char parent[PATH_MAX];
    snprintf(parent, sizeof parent, "%s", mp);
    char *sl = strrchr(parent, '/');
    if (sl) { *sl = 0; mkdir(parent, 0700); }
    mkdir(mp, 0700);
    if (adb_mounted(mp)) { snprintf(mount, mcap, "%s", mp); return 0; }
    snprintf(env, sizeof env, "ANDROID_SERIAL=%s", serial);
    char *argv[] = {"env", env, (char *)adbfs_tool(), mp, NULL};
    int rc = run_capture(argv, out, ocap);
    for (int k = 0; rc == 0 && k < 30 && !adb_mounted(mp); k++) usleep(100000);
    if (rc == 0 && !adb_mounted(mp)) { snprintf(out, ocap, "adbfs started but its folder didn't show up"); rc = 1; }
    if (rc == 0) snprintf(mount, mcap, "%s", mp);
    return rc;
}

// Phones and cameras: ours for every device our connector talks to (GVfs's entries for them folded in: kept only
// when GVfs has it connected already), then GVfs's other ones.
static void phones_scan_own(DevList *L, bool own);

static void phones_scan(DevList *L) {
    bool own = own_phones_enabled();
    phones_scan_own(L, own);
    phones_scan_adb(L);
}

static void phones_scan_own(DevList *L, bool own) {
    phones_scan_gvfs(L, !own);
    if (!own) return;
    UsbDevInfo u[MAX_PHONES];
    int nu = usb_list_phones(u, MAX_PHONES);
    PhoneDev *mine = xcalloc(MAX_PHONES, sizeof *mine);
    int nm = 0;
    for (int i = 0; i < nu; i++) {
        PhoneDev *ph = &mine[nm++];
        ph->own = true;
        ph->kind = u[i].kind;
        snprintf(ph->sys, sizeof ph->sys, "%s", u[i].sys);
        snprintf(ph->serial, sizeof ph->serial, "%s", u[i].serial);
        snprintf(ph->uri, sizeof ph->uri, "usb:%s", u[i].serial[0] ? u[i].serial : u[i].sys);
        snprintf(ph->name, sizeof ph->name, "%s", u[i].product[0] ? u[i].product : u[i].kind == 'a' ? "iPhone" : u[i].kind == 'c' ? "Camera" : "Phone");
        if (own_mount_of(u[i].serial[0] ? u[i].serial : u[i].sys, ph->mount, sizeof ph->mount)) // its own name once connected
            snprintf(ph->name, sizeof ph->name, "%s", path_basename(ph->mount));
    }
    bool *hide = xcalloc(MAX_PHONES, sizeof *hide);
    for (int g = 0; g < L->nph; g++) { // GVfs's entries for the same devices
        char key[400], host[300], gs[300];
        if (!phone_key(L->ph[g].uri, key, sizeof key, NULL, 0)) continue;
        snprintf(host, sizeof host, "%.*s", (int)strcspn(key + 2, "|"), key + 2);
        serial_norm(host, gs, sizeof gs);
        for (int k = 0; k < nm; k++) {
            char os[100];
            serial_norm(mine[k].serial, os, sizeof os);
            if (strlen(os) < 6 || !strstr(gs, os)) continue;
            bool docs = key[2 + strlen(host) + 1] != 0; // afc://UDID:3/: an app's Documents
            char gk = phone_kind(L->ph[g].uri);
            if (!mine[k].gvfs_uri[0] && !docs && (gk != 'c' || mine[k].kind == 'c')) // GVfs's way in, if ours can't
                snprintf(mine[k].gvfs_uri, sizeof mine[k].gvfs_uri, "%s", L->ph[g].uri);
            if (L->ph[g].mount[0]) { if (!mine[k].mount[0]) hide[k] = true; } // GVfs has it: that one stays, ours can't
            else L->ph[g].kind = 0; // ours shows it
            break;
        }
    }
    PhoneDev *all = xcalloc(MAX_PHONES, sizeof *all);
    int n = 0;
    for (int k = 0; k < nm && n < MAX_PHONES; k++) if (!hide[k]) all[n++] = mine[k];
    for (int g = 0; g < L->nph && n < MAX_PHONES; g++) if (L->ph[g].kind) all[n++] = L->ph[g];
    memcpy(L->ph, all, (size_t)n * sizeof *all);
    L->nph = n;
    free(all);
    free(mine);
    free(hide);
}

// Its folder in the FUSE bridge once `gio mount URI` connected it.
static bool phone_find_mount(const char *uri, char *out, size_t cap) {
    char want[400], key[400], gdir[PATH_MAX];
    if (!phone_key(uri, want, sizeof want, NULL, 0)) return false;
    gvfs_dir(gdir, sizeof gdir);
    DIR *d = opendir(gdir);
    bool found = false;
    struct dirent *de;
    while (d && !found && (de = readdir(d)))
        if (de->d_name[0] != '.' && phone_key(de->d_name, key, sizeof key, NULL, 0) && !strcmp(key, want)) {
            path_join(out, cap, gdir, de->d_name);
            found = true;
        }
    if (d) closedir(d);
    return found;
}

// GVfs's error for a device that wouldn't connect, turned into what to do about it.
static void phone_error_hint(char kind, const char *err, char *out, size_t cap) {
    if (kind == 'a' && strcasestr(err, "denied"))
        snprintf(out, cap, "the iPhone was told not to trust this computer. Unplug it, plug it back in and tap Trust");
    else if (kind == 'a' && strcasestr(err, "Lockdown error (-8)")) // usbmuxd couldn't reach the phone's lockdown service
        snprintf(out, cap, "the iPhone didn't answer. Unlock it, unplug it and plug it back in, and tap Trust; if that doesn't help, "
                           "restart usbmuxd (sudo systemctl restart usbmuxd)");
    else if (kind == 'a' && (strcasestr(err, "pair") || strcasestr(err, "trust") || strcasestr(err, "passcode") ||
                             strcasestr(err, "password") || strcasestr(err, "locked")))
        snprintf(out, cap, "unlock the iPhone and tap Trust if it asks, then try again");
    else if (kind == 'm' && (strcasestr(err, "MTP") || strcasestr(err, "busy") || !err[0]))
        snprintf(out, cap, "unlock the phone and choose File transfer in its USB notification, then try again "
                           "(another app using the phone has to let go of it first)");
    else if (kind == 'c' && (strcasestr(err, "claim") || strcasestr(err, "busy")))
        snprintf(out, cap, "another program is using the camera. Close it and try again");
    else snprintf(out, cap, "%s", err[0] ? err : "unknown error");
}

// --- actions (worker thread) ------------------------------------------------------

enum { DEV_MOUNT, DEV_UNMOUNT, DEV_EJECT, DEV_NET_MOUNT, DEV_NET_UNMOUNT, DEV_OWN_MOUNT, DEV_IMAGE_MOUNT, DEV_ADB_MOUNT };
typedef struct {
    int kind;
    char target[PATH_MAX]; // device, URI, or mount point
    char disk[64];
    char opts[32];         // mount options ("ro")
    bool open_after;       // navigate to the new mount point
    int rc;
    char out[1024];
    char mount[PATH_MAX];  // where it got mounted
    // network mounts: answers for GVfs's prompts (the password never leaves this struct; wiped after use)
    char user[128], domain[128], password[256];
    // a question GVfs asks (unknown SSH host key...): relayed to the main thread, answered by the user
    pthread_mutex_t qmtx;
    pthread_cond_t qcond;
    char question[1024], choices[4][128];
    int nchoices, answer; // answer: -2 waiting, -1 cancel, else the choice index
    int qkind;            // 0: a question with choices, 1: a password / passphrase the user types (into password)
    bool qretry;          // the previous password wasn't accepted
    bool lookup_secret;   // saved server: try the password from the system keyring first
    bool store_secret;    // remember the password in the keyring once the login worked
    bool secret_used;     // the password came from the keyring
    bool fuse_ok;         // GVfs's FUSE bridge is up (its mounts have folders)
    bool already;         // GVfs said it was already connected
    char phone;           // DEV_NET_MOUNT / DEV_NET_UNMOUNT of a phone or camera: its kind (see PhoneDev)
    char fallback[512];   // DEV_OWN_MOUNT (target: the USB device): GVfs's URI for it, tried if ours can't have it
} DevJob;

static void dev_job_done(void *arg); // app.c
static void dev_phone_wait(void *what); // app.c: our connector waits for the user ("trust", "unlock"; strdup'd)
static void dev_net_question(void *arg); // app.c: shows j->question, then dev_net_answer()

static void dev_net_answer(DevJob *j, int choice) {
    pthread_mutex_lock(&j->qmtx);
    j->answer = choice;
    pthread_cond_signal(&j->qcond);
    pthread_mutex_unlock(&j->qmtx);
}

static bool str_has_ci(const char *s, const char *w) { return strcasestr(s, w) != NULL; }

// --- system keyring (Secret Service via secret-tool): passwords for saved servers, only when asked to ---------
// The secret goes through pipes (never argv); attributes: application=file-explorer, uri=<server address>.
static int secret_cmd(char *const argv[], const char *input, char *out, size_t cap) {
    int in[2], o[2];
    if (out && cap) out[0] = 0;
    if (pipe2(in, O_CLOEXEC)) return -1;
    if (pipe2(o, O_CLOEXEC)) { close(in[0]); close(in[1]); return -1; }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in[0], 0);
    posix_spawn_file_actions_adddup2(&fa, o[1], 1);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], &fa, spawn_attr(), argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(in[0]);
    close(o[1]);
    if (rc) { close(in[1]); close(o[0]); return -1; }
    if (input) { ssize_t w = write(in[1], input, strlen(input)); (void)w; }
    close(in[1]);
    size_t n = 0;
    char tmp[512];
    for (;;) {
        ssize_t r = read(o[0], tmp, sizeof tmp);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        for (ssize_t i = 0; i < r && out && n + 1 < cap; i++) out[n++] = tmp[i];
    }
    explicit_bzero(tmp, sizeof tmp);
    close(o[0]);
    if (out && cap) out[n] = 0;
    return (int)n;
}

static bool secret_available(void) {
    const char *t = getenv("FE_SECRET_TOOL");
    if (t && t[0]) return true;
    return access("/usr/bin/secret-tool", X_OK) == 0 || access("/usr/local/bin/secret-tool", X_OK) == 0;
}

static bool secret_lookup(const char *uri, char *out, size_t cap) {
    char *argv[] = {(char *)tool("FE_SECRET_TOOL", "secret-tool"), "lookup", "application", "file-explorer", "uri", (char *)uri, NULL};
    return secret_cmd(argv, NULL, out, cap) > 0 && out[0];
}

static void secret_store(const char *uri, const char *password) {
    char label[1100];
    snprintf(label, sizeof label, "Files: %s", uri);
    char *argv[] = {(char *)tool("FE_SECRET_TOOL", "secret-tool"), "store", "--label", label, "application", "file-explorer", "uri", (char *)uri, NULL};
    secret_cmd(argv, password, NULL, 0);
}

static void secret_clear(const char *uri) {
    char *argv[] = {(char *)tool("FE_SECRET_TOOL", "secret-tool"), "clear", "application", "file-explorer", "uri", (char *)uri, NULL};
    secret_cmd(argv, NULL, NULL, 0);
}

// --- shared ssh connection to SFTP servers (git status runs over it) --------------------------------------
// Right after an SFTP connect worked, one ssh connection to the server is opened with the password / passphrase the
// user just gave (or the keyring's) and kept as a ControlMaster while the folder is connected: later ssh runs
// (git.c) reuse it and ask nothing.
// The secret reaches ssh through our own binary as SSH_ASKPASS reading a private file in the runtime folder (never
// the command line or ssh's environment); the file is gone once ssh has logged in.

static bool app_selftest(void); // app.c

static void ssh_control_path(char *out, size_t cap) { // "ControlPath=..." (%C: hash of host, port, user)
    const char *rt = getenv("XDG_RUNTIME_DIR");
    snprintf(out, cap, "ControlPath=%s/fe-ssh-%%C", rt && rt[0] ? rt : "/tmp");
}

// ssh running cmd on host (argv into a[24]; ctl holds the ControlPath option). Never asks for a password: it rides
// on the connection opened at connect time, else needs an agent or a key without a passphrase.
static void ssh_argv(char **a, char *ctl, size_t ccap, const char *host, const char *port, char *cmd) {
    ssh_control_path(ctl, ccap);
    char *opts[] = {(char *)tool("FE_SSH", "ssh"), "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "-o", "ServerAliveInterval=5",
                    "-o", "ServerAliveCountMax=2", "-o", "ControlMaster=auto", "-o", ctl, "-o", "ControlPersist=60"};
    int n = 0;
    for (; n < (int)ARRAY_COUNT(opts); n++) a[n] = opts[n];
    if (port && port[0]) { a[n++] = "-p"; a[n++] = (char *)port; }
    a[n++] = "--";
    a[n++] = (char *)host;
    a[n++] = cmd;
    a[n] = NULL;
}

typedef struct { char user[128], host[256], port[8], path[PATH_MAX]; } SshTarget;

// GVfs's folder of an SFTP connection: <runtime>/gvfs/sftp:host=H,user=U,port=P/<remote path>.
static bool ssh_target_gvfs(const char *path, SshTarget *t) {
    memset(t, 0, sizeof *t);
    const char *c = strstr(path, "/sftp:");
    if (!c) return false;
    const char *comp = c + 1, *end = strchr(comp, '/');
    size_t cl = end ? (size_t)(end - comp) : strlen(comp);
    char kv[512];
    snprintf(kv, sizeof kv, "%.*s", (int)MIN(cl - 5, sizeof kv - 1), comp + 5);
    char *save = NULL;
    for (char *tok = strtok_r(kv, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        if (!strcmp(tok, "host")) snprintf(t->host, sizeof t->host, "%s", eq + 1);
        else if (!strcmp(tok, "user")) snprintf(t->user, sizeof t->user, "%s", eq + 1);
        else if (!strcmp(tok, "port")) snprintf(t->port, sizeof t->port, "%.6s", eq + 1);
    }
    snprintf(t->path, sizeof t->path, "%s", end ? end : "/");
    return t->host[0] != 0;
}

static bool ssh_target_uri(const char *uri, SshTarget *t); // app.c: "sftp://user@host:port/dir" -> the server

static void ssh_dest(const SshTarget *t, char *out, size_t cap) {
    snprintf(out, cap, "%s%s%s", t->user, t->user[0] ? "@" : "", t->host);
}

// `file-explorer <prompt>` with FE_ASKPASS_FILE set: ssh asking for a password or passphrase (anything else, like
// an unknown host key, is refused). Called from main() before anything else.
static int askpass_main(const char *prompt) {
    const char *file = getenv("FE_ASKPASS_FILE");
    if (!str_has_ci(prompt, "password") && !str_has_ci(prompt, "passphrase")) return 1;
    int fd = open(file, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return 1;
    char buf[300];
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return 1;
    buf[n++] = '\n';
    bool ok = write(1, buf, (size_t)n) == n;
    explicit_bzero(buf, sizeof buf);
    return ok ? 0 : 1;
}

typedef struct { SshTarget t; char secret[256]; } SshOpen;

static void ssh_master_job(void *arg) {
    SshOpen *o = arg;
    char dest[400], ctl[PATH_MAX], out[512];
    ssh_dest(&o->t, dest, sizeof dest);
    ssh_control_path(ctl, sizeof ctl);
    const char *sshb = tool("FE_SSH", "ssh");
    char *check[] = {(char *)sshb, "-o", ctl, "-O", "check", "--", dest, NULL};
    if (run_capture(check, out, sizeof out) == 0) goto done; // connected already (an earlier connect)
    char file[PATH_MAX] = "", exe[PATH_MAX], ev_ask[PATH_MAX + 16], ev_file[PATH_MAX + 20];
    ssize_t el = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (o->secret[0] && el > 0) {
        static atomic_int seq;
        exe[el] = 0;
        const char *rt = getenv("XDG_RUNTIME_DIR");
        snprintf(file, sizeof file, "%s/fe-askpass-%d-%d", rt && rt[0] ? rt : "/tmp", (int)getpid(), atomic_fetch_add(&seq, 1));
        int fd = open(file, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        size_t sl = strlen(o->secret);
        if (fd < 0 || write(fd, o->secret, sl) != (ssize_t)sl) file[0] = 0;
        if (fd >= 0) close(fd);
    }
    snprintf(ev_ask, sizeof ev_ask, "SSH_ASKPASS=%s", exe);
    snprintf(ev_file, sizeof ev_file, "FE_ASKPASS_FILE=%s", file);
    // -f: back to us once logged in; the connection stays in the background as long as the SFTP folder is
    // connected (ssh_master_close when it goes away; it also ends with the server, ServerAlive)
    char *av[32] = {"env", ev_ask, "SSH_ASKPASS_REQUIRE=force", ev_file, (char *)sshb, "-o", ctl, "-o", "ControlMaster=yes",
                    "-o", "ControlPersist=yes", "-o", "ConnectTimeout=10", "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=3"};
    int n = 17;
    if (!file[0]) { av[n++] = "-o"; av[n++] = "BatchMode=yes"; } // nothing to answer with: an agent or key file only
    if (o->t.port[0]) { av[n++] = "-p"; av[n++] = o->t.port; }
    av[n++] = "-f";
    av[n++] = "-N";
    av[n++] = "--";
    av[n++] = dest;
    av[n] = NULL;
    run_capture(av, out, sizeof out);
    if (file[0]) unlink(file);
done:
    explicit_bzero(o->secret, sizeof o->secret);
    free(o);
}

// After `uri` (sftp://...) connected with secret (may be ""): open the shared connection in the background.
static void ssh_master_open(const char *uri, const char *secret) {
    SshTarget t;
    if (!ssh_target_uri(uri, &t) || (app_selftest() && !getenv("FE_SSH"))) return;
    SshOpen *o = xcalloc(1, sizeof *o);
    o->t = t;
    snprintf(o->secret, sizeof o->secret, "%s", secret ? secret : "");
    pool_submit(ssh_master_job, o);
}

// The SFTP folder (GVfs) was disconnected: close its shared connection too.
static void ssh_master_close(const char *mount) {
    SshTarget t;
    if (!ssh_target_gvfs(mount, &t) || (app_selftest() && !getenv("FE_SSH"))) return;
    char dest[400], ctl[PATH_MAX], out[256];
    ssh_dest(&t, dest, sizeof dest);
    ssh_control_path(ctl, sizeof ctl);
    char *av[12] = {(char *)tool("FE_SSH", "ssh"), "-o", ctl, "-O", "exit"};
    int n = 5;
    if (t.port[0]) { av[n++] = "-p"; av[n++] = t.port; }
    av[n++] = "--";
    av[n++] = dest;
    av[n] = NULL;
    run_capture(av, out, sizeof out);
}

// Ask the user on the main thread (question or password); blocks until answered. Returns the answer.
static int dev_net_ask(DevJob *j) {
    pthread_mutex_lock(&j->qmtx);
    j->answer = -2;
    pthread_mutex_unlock(&j->qmtx);
    main_post(dev_net_question, j);
    pthread_mutex_lock(&j->qmtx);
    while (j->answer == -2) pthread_cond_wait(&j->qcond, &j->qmtx);
    int a = j->answer;
    pthread_mutex_unlock(&j->qmtx);
    return a;
}

// `gio mount URI`, answering its prompts on stdin: User / Domain / Password from the job, questions (choices)
// by asking the user. A second password prompt means the password was wrong: stdin is closed so gio gives up.
static int net_mount_run(DevJob *j) {
    int in[2], out[2];
    if (pipe2(in, O_CLOEXEC)) return -1;
    if (pipe2(out, O_CLOEXEC)) { close(in[0]); close(in[1]); return -1; }
    char *av[] = {"sh", "-c", SH_ENGLISH "\"$@\"; printf '\\n@rc=%d\\n' $?", "sh", (char *)tool("FE_GIO", "gio"), "mount", j->target, NULL};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in[0], 0);
    posix_spawn_file_actions_adddup2(&fa, out[1], 1);
    posix_spawn_file_actions_adddup2(&fa, out[1], 2);
    posix_spawnattr_t at;
    spawn_attr_init(&at, POSIX_SPAWN_SETPGROUP); // own group: a timeout can stop gio too
    posix_spawnattr_setpgroup(&at, 0);
    pid_t pid;
    int rc = posix_spawnp(&pid, "sh", &fa, &at, av, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    close(in[0]);
    close(out[1]);
    if (rc) { close(in[1]); close(out[0]); return -1; }
    size_t cap = 16384, n = 0, consumed = 0;
    char *buf = xmalloc(cap);
    int wfd = in[1], npw = 0;
    bool asked_user = false;
    u64 deadline = now_ns() + 120ull * 1000000000ull;
    for (;;) {
        struct pollfd pf = {out[0], POLLIN, 0};
        int pr = poll(&pf, 1, 500);
        if (pr < 0 && errno == EINTR) continue;
        if (now_ns() > deadline) { kill(-pid, SIGTERM); snprintf(j->out, sizeof j->out, "Timed out connecting"); break; }
        if (pr == 0) {
            // a prompt is waiting when the output stops on an unanswered line ending in ':' / '?'
        } else {
            if (n + 4096 + 1 > cap) { cap *= 2; buf = xrealloc(buf, cap); }
            ssize_t r = read(out[0], buf + n, cap - n - 1);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) break;
            n += (size_t)r;
            buf[n] = 0;
        }
        // the current (unterminated) last line
        buf[n] = 0;
        const char *tail = buf + consumed;
        const char *nl = strrchr(tail, '\n');
        const char *line = nl ? nl + 1 : tail;
        // gio's prompts end in ": " ("Password: ", "Choice: ", "Login anonymously? [y/N]: "); its explanations
        // end in ":\n", so a finished line is never mistaken for a prompt
        size_t ll = strlen(line);
        if (ll < 2 || line[ll - 1] != ' ' || (line[ll - 2] != ':' && line[ll - 2] != '?' && line[ll - 2] != ']')) continue;
        char ans[300];
        ans[0] = 0;
        bool close_in = false;
        if (str_has_ci(line, "choice")) { // question: everything since the last prompt, with "[n] label" choices
            pthread_mutex_lock(&j->qmtx);
            j->qkind = 0;
            j->nchoices = 0;
            size_t qo = 0;
            for (const char *p = tail; p < line;) {
                const char *e = strchr(p, '\n');
                if (!e) e = line;
                int k;
                char lab[128];
                if (sscanf(p, " [%d] %127[^\n]", &k, lab) == 2 && j->nchoices < 4) snprintf(j->choices[j->nchoices++], sizeof j->choices[0], "%s", lab);
                else if (e > p && qo + (size_t)(e - p) + 2 < sizeof j->question) {
                    memcpy(j->question + qo, p, (size_t)(e - p));
                    qo += (size_t)(e - p);
                    j->question[qo++] = '\n';
                }
                p = e + (*e == '\n');
            }
            while (qo && (j->question[qo - 1] == '\n' || j->question[qo - 1] == ' ')) qo--;
            j->question[qo] = 0;
            pthread_mutex_unlock(&j->qmtx);
            int a = dev_net_ask(j);
            deadline = now_ns() + 120ull * 1000000000ull; // the user took their time
            if (a < 0) close_in = true;
            else snprintf(ans, sizeof ans, "%d\n", a + 1);
        } else if (str_has_ci(line, "password") || str_has_ci(line, "passphrase")) {
            // the known password (typed in the dialog or from the keyring) first; then, once, the user is asked
            // (a passphrase for an SSH key comes through the same prompt); after that gio is told nothing more
            npw++;
            if (npw == 1 && j->password[0]) snprintf(ans, sizeof ans, "%s\n", j->password);
            else if (!asked_user) {
                asked_user = true;
                pthread_mutex_lock(&j->qmtx);
                j->qkind = 1;
                j->qretry = npw > 1;
                size_t qo = 0; // gio's explanation ("Enter passphrase for secure key for ...")
                for (const char *p = tail; p < line && qo + 3 < sizeof j->question; p++) {
                    if (*p != '\n') { j->question[qo++] = *p; continue; }
                    if (qo && !strchr(".:!?", j->question[qo - 1])) j->question[qo++] = '.'; // lines are sentences
                    if (qo) j->question[qo++] = ' ';
                }
                while (qo && j->question[qo - 1] == ' ') qo--;
                j->question[qo] = 0;
                explicit_bzero(j->password, sizeof j->password);
                pthread_mutex_unlock(&j->qmtx);
                int a = dev_net_ask(j);
                deadline = now_ns() + 120ull * 1000000000ull;
                j->secret_used = false;
                if (a < 0) close_in = true;
                else snprintf(ans, sizeof ans, "%s\n", j->password);
            } else close_in = true;
        } else if (str_has_ci(line, "anonym")) {
            snprintf(ans, sizeof ans, "%s\n", j->user[0] ? "n" : "y");
        } else if (str_has_ci(line, "domain")) {
            snprintf(ans, sizeof ans, "%s\n", j->domain);
        } else if (str_has_ci(line, "user")) {
            snprintf(ans, sizeof ans, "%s\n", j->user);
        } else {
            continue; // some other line ending in ':' - keep reading
        }
        if (close_in && wfd >= 0) { close(wfd); wfd = -1; }
        else if (wfd >= 0 && ans[0]) {
            size_t al = strlen(ans);
            if (write(wfd, ans, al) != (ssize_t)al) { close(wfd); wfd = -1; }
        }
        explicit_bzero(ans, sizeof ans);
        consumed = n; // this prompt is answered
    }
    if (wfd >= 0) close(wfd);
    close(out[0]);
    buf[n] = 0;
    int status = -1;
    char *m = NULL;
    for (char *q = buf; (q = strstr(q, "\n@rc=")); q++) m = q;
    if (m) { status = atoi(m + 5); *m = 0; }
    if (status == 0 && j->store_secret && j->password[0] && !j->secret_used) secret_store(j->target, j->password);
    if (status == 0 || str_has_ci(buf, "already mounted")) ssh_master_open(j->target, j->password); // SFTP: for git status
    explicit_bzero(j->password, sizeof j->password);
    if (!j->out[0]) { // the last meaningful line (gio's error), without our prompts
        char *best = NULL;
        for (char *q = strtok(buf, "\n"); q; q = strtok(NULL, "\n")) {
            char *g = strstr(q, "gio:"); // an error printed right after an unanswered prompt ("Password: gio: ...")
            if (g) q = g;
            for (bool again = true; again;) { // drop leading prompts
                again = false;
                while (*q == ' ') q++;
                static const char *pre[] = {"Password: ", "Choice: ", "User: ", "Domain: "};
                for (size_t k = 0; k < ARRAY_COUNT(pre); k++)
                    if (!strncasecmp(q, pre[k], strlen(pre[k]))) { q += strlen(pre[k]); again = true; }
            }
            if (*q && (g || (!str_has_ci(q, "password") && !str_has_ci(q, "choice") && strncmp(q, "[", 1)))) best = q;
        }
        if (best) {
            const char *g = strstr(best, ": ");
            snprintf(j->out, sizeof j->out, "%s", !strncmp(best, "gio:", 4) && g && strstr(g + 2, ": ") ? strstr(g + 2, ": ") + 2 : best);
        }
    }
    free(buf);
    return status == 127 ? -1 : status;
}

// Where our helper puts its folders (tests: FE_DEVICES_DIR).
static void own_devices_dir(char *out, size_t cap) {
    const char *ov = getenv("FE_DEVICES_DIR");
    if (ov && ov[0]) snprintf(out, cap, "%s", ov);
    else snprintf(out, cap, "%s/file-explorer/devices", getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "/tmp");
}

// Start our helper for the device (`file-explorer --device-mount SYS DIR`: it stays running as the folder's FUSE
// server) and follow what it says until the folder is there or it gave up. 0 ok (j->mount), else j->out has
// "kind: text".
static int own_mount_run(DevJob *j) {
    char exe[PATH_MAX], dir[PATH_MAX];
    const char *helper = getenv("FE_DEVICE_HELPER"); // tests: a stand-in
    ssize_t el = helper ? 0 : readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (helper) snprintf(exe, sizeof exe, "%s", helper);
    else if (el > 0) exe[el] = 0;
    else { snprintf(j->out, sizeof j->out, "io: can't find this program to start its device helper"); return 1; }
    own_devices_dir(dir, sizeof dir);
    int out[2];
    if (pipe2(out, O_CLOEXEC)) return 1;
    char *av[] = {exe, "--device-mount", j->target, dir, NULL};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, out[1], 1);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t at;
    spawn_attr_init(&at, POSIX_SPAWN_SETSID); // outlives the window: it serves the folder
    pid_t pid;
    int rc = posix_spawn(&pid, exe, &fa, &at, av, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    close(out[1]);
    if (rc) { close(out[0]); snprintf(j->out, sizeof j->out, "io: can't start the device helper: %s", strerror(rc)); return 1; }
    char buf[2048];
    size_t n = 0;
    int res = -1;
    u64 deadline = now_ns() + 200ull * 1000000000ull; // pairing waits up to two minutes for Trust
    while (res < 0) {
        struct pollfd pf = {out[0], POLLIN, 0};
        int pr = poll(&pf, 1, 500);
        if (pr < 0 && errno == EINTR) continue;
        if (now_ns() > deadline) { kill(pid, SIGTERM); snprintf(j->out, sizeof j->out, "io: the device didn't answer in time"); res = 1; break; }
        if (pr == 0) continue;
        ssize_t r = read(out[0], buf + n, sizeof buf - 1 - n);
        if (r <= 0) { if (!j->out[0]) snprintf(j->out, sizeof j->out, "io: the device helper stopped"); res = 1; break; }
        n += (size_t)r;
        buf[n] = 0;
        char *nl;
        while (res < 0 && (nl = strchr(buf, '\n'))) {
            *nl = 0;
            if (!strncmp(buf, "wait ", 5)) main_post(dev_phone_wait, strdup(buf + 5));
            else if (!strncmp(buf, "ok ", 3)) { snprintf(j->mount, sizeof j->mount, "%s", buf + 3); res = 0; }
            else if (!strncmp(buf, "error ", 6)) { snprintf(j->out, sizeof j->out, "%s", buf + 6); res = 1; }
            size_t used = (size_t)(nl - buf) + 1;
            memmove(buf, nl + 1, n - used + 1);
            n -= used;
        }
        if (n >= sizeof buf - 1) n = 0;
    }
    close(out[0]);
    return res;
}

// udisksctl mount -b dev [-o opts]: rc, its output in out, where it went in mount. Already mounted (the desktop's
// automounter can be quicker for an image we just opened): that mount, rc 0.
static int udisks_mount(const char *ud, const char *dev, const char *opts, char *out, size_t cap, char *mount, size_t mcap) {
    char *argv[] = {(char *)ud, "mount", "-b", (char *)dev, opts && opts[0] ? "-o" : NULL, (char *)opts, NULL};
    int rc = run_capture(argv, out, cap);
    // "Mounted /dev/sdb1 at /run/media/me/STICK", "... is already mounted at `/run/media/me/X'."
    const char *at = rc ? strstr(out, "already mounted at `") : strstr(out, " at ");
    if (at) {
        snprintf(mount, mcap, "%s", at + (rc ? 20 : 4));
        mount[strcspn(mount, "\n'")] = 0;
        size_t l = strlen(mount);
        if (l && mount[l - 1] == '.') mount[l - 1] = 0;
        if (rc && mount[0] == '/') rc = 0;
    }
    return rc;
}

static void dev_job(void *arg) {
    DevJob *j = arg;
    const char *ud = tool("FE_UDISKSCTL", "udisksctl");
    switch (j->kind) {
    case DEV_MOUNT: j->rc = udisks_mount(ud, j->target, j->opts, j->out, sizeof j->out, j->mount, sizeof j->mount); break;
    case DEV_IMAGE_MOUNT: { // a disc / disk image: a read-only loop device (udisks: no root), then its filesystem
        char *argv[] = {(char *)ud, "loop-setup", "-r", "-f", j->target, NULL};
        j->rc = run_capture(argv, j->out, sizeof j->out);
        const char *as = strstr(j->out, " as /dev/"); // "Mapped file /x/a.iso as /dev/loop0."
        if (j->rc || !as) { if (!j->rc) j->rc = 1; break; }
        char loop[64];
        snprintf(loop, sizeof loop, "%s", as + 4);
        loop[strcspn(loop, " .\n")] = 0;
        j->rc = udisks_mount(ud, loop, NULL, j->out, sizeof j->out, j->mount, sizeof j->mount);
        // a disk image with partitions: its first partition that mounts (the others are in Devices); the kernel adds
        // their nodes a moment after the loop device
        for (int k = 1; j->rc && k <= 8; k++) {
            char part[80], o2[512];
            snprintf(part, sizeof part, "%sp%d", loop, k);
            struct stat st;
            for (int w = 0; w < 10 && stat(part, &st); w++) usleep(50000);
            if (stat(part, &st)) break;
            if (!udisks_mount(ud, part, NULL, o2, sizeof o2, j->mount, sizeof j->mount)) j->rc = 0;
        }
        if (j->rc) { // nothing in it to open: let the loop device go
            char *del[] = {(char *)ud, "loop-delete", "-b", loop, NULL}, tmp[256];
            run_capture(del, tmp, sizeof tmp);
        }
        break;
    }
    case DEV_UNMOUNT:
    case DEV_EJECT: {
        char *argv[] = {(char *)ud, "unmount", "-b", j->target, NULL};
        j->rc = run_capture(argv, j->out, sizeof j->out);
        if (!j->rc && j->kind == DEV_EJECT && j->disk[0]) {
            char disk[80], tmp[256];
            snprintf(disk, sizeof disk, "/dev/%s", j->disk);
            if (!strncmp(j->disk, "loop", 4)) { // an opened image: the loop device goes (the file stays)
                char *argv2[] = {(char *)ud, "loop-delete", "-b", disk, NULL};
                run_capture(argv2, tmp, sizeof tmp);
            } else if (!strncmp(j->disk, "sr", 2)) { // a disc: open the tray
                char *argv2[] = {(char *)tool("FE_EJECT", "eject"), disk, NULL};
                run_capture(argv2, tmp, sizeof tmp);
            } else { // spin down / power off so it can be pulled out
                char *argv2[] = {(char *)ud, "power-off", "-b", disk, NULL};
                run_capture(argv2, tmp, sizeof tmp); // not every device supports it; unmounted is what matters
            }
        }
        break;
    }
    case DEV_NET_MOUNT:
        j->fuse_ok = gvfs_fuse_ensure();
        if (j->lookup_secret && !j->password[0] && secret_available() && secret_lookup(j->target, j->password, sizeof j->password)) {
            size_t l = strlen(j->password);
            if (l && j->password[l - 1] == '\n') j->password[l - 1] = 0;
            j->secret_used = true;
        }
        j->rc = net_mount_run(j);
        if (j->rc != 0 && str_has_ci(j->out, "already mounted")) { j->rc = 0; j->already = true; } // connected before: fine
        for (int k = 0; j->phone && j->rc == 0 && k < 30 && !phone_find_mount(j->target, j->mount, sizeof j->mount); k++)
            usleep(100000); // its folder shows up in the FUSE bridge a moment later
        break;
    case DEV_OWN_MOUNT:
        j->rc = own_mount_run(j);
        break;
    case DEV_ADB_MOUNT: j->rc = adb_mount_run(j->target, j->mount, sizeof j->mount, j->out, sizeof j->out); break;
    case DEV_NET_UNMOUNT: {
        bool gvfs = strstr(j->target, "/gvfs/") != NULL;
        if (gvfs) {
            char *argv[] = {(char *)tool("FE_GIO", "gio"), "mount", "-u", j->target, NULL};
            j->rc = run_capture(argv, j->out, sizeof j->out);
            if (j->rc == 0) ssh_master_close(j->target);
        } else {
            char *argv[] = {"fusermount3", "-u", j->target, NULL};
            j->rc = run_capture(argv, j->out, sizeof j->out);
            if (j->rc) {
                char *argv2[] = {"umount", j->target, NULL};
                j->rc = run_capture(argv2, j->out, sizeof j->out);
            }
        }
        break;
    }
    }
    main_post(dev_job_done, j);
}

// --- watcher: mount table changes (POLLPRI on /proc/self/mounts), new / removed block and USB devices, GVfs mounts ---

static _Atomic int g_dev_changed;

static u64 dir_signature(const char *dir) {
    u64 h = 1469598103934665603ull;
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *de;
    while ((de = readdir(d)))
        for (const char *c = de->d_name; *c; c++) h = (h ^ (u8)*c) * 1099511628211ull, h += 0x9e37;
    closedir(d);
    return h;
}

static void *dev_watch_thread(void *arg) {
    (void)arg;
    int fd = open("/proc/self/mounts", O_RDONLY | O_CLOEXEC);
    const char *usb = getenv("FE_SYS_USB") ? getenv("FE_SYS_USB") : "/sys/bus/usb/devices";
    char gdir[PATH_MAX];
    gvfs_dir(gdir, sizeof gdir);
    u64 sig = dir_signature("/sys/class/block"), usig = dir_signature(usb), gsig = dir_signature(gdir);
    u64 later[2] = {0, 0}; // a USB device came or went: look again once GVfs's volume monitors have caught up
    for (;;) {
        bool changed = false;
        if (fd >= 0) {
            struct pollfd pf = {fd, POLLPRI | POLLERR, 0};
            int r = poll(&pf, 1, 2000);
            if (r > 0 && (pf.revents & (POLLPRI | POLLERR))) changed = true;
        } else {
            sleep(2);
        }
        u64 s2 = dir_signature("/sys/class/block"); // plugged / unplugged without a mount (cheap: one readdir)
        if (s2 != sig) { sig = s2; changed = true; }
        s2 = dir_signature(usb); // phones and cameras aren't block devices
        if (s2 != usig) {
            usig = s2;
            changed = true;
            later[0] = now_ns() + 1500000000ull;
            later[1] = now_ns() + 5000000000ull;
        }
        s2 = dir_signature(gdir); // GVfs connected or disconnected something (maybe another program did)
        if (s2 != gsig) { gsig = s2; changed = true; }
        for (int k = 0; k < 2; k++)
            if (later[k] && now_ns() >= later[k]) { later[k] = 0; changed = true; }
        if (changed) {
            atomic_store(&g_dev_changed, 1);
            main_wake();
        }
    }
    return NULL;
}

static void devices_watch_start(void) {
    pthread_t t;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &at, dev_watch_thread, NULL);
    pthread_setname_np(t, "fe-devwatch");
    pthread_attr_destroy(&at);
}
