// cc -std=gnu11 -Wall -Wextra -Wno-unused-function tools/local-shares-selftest.c -o /tmp/fe-local-shares-test -pthread
#include "../src/base.h"
#include <ctype.h>
#include <assert.h>
#include "../src/local_shares.c"
static LocalShares shares;
static void fixture(const char *file, const char *text) {
    FILE *f = fopen(file, "w"); assert(f); fputs(text, f); fclose(f);
}
int main(void) {
    char dir[] = "/tmp/fe-shares-XXXXXX"; assert(mkdtemp(dir));
    char file[PATH_MAX], inc[PATH_MAX], config[8192];
    path_join(file, sizeof file, dir, "config");
    path_join(inc, sizeof inc, dir, "included");
    fixture(inc, "[extra]\npath = /srv/included\n");
    snprintf(config, sizeof config, "[global]\npath=/not-a-share\ninclude = %s\n[media]\npath = /srv/media/\n[spaces]\npath = \"/srv/My Files\"\n[homes]\npath = /home/%%U\n[printers]\npath=/var/spool/samba\n", inc);
    fixture(file, config); shares_read(&shares, file, SHARE_SAMBA, 0);
    assert(shares.count == 3);
    assert(local_share_kind(&shares, "/srv/media") == LOCAL_SHARE_SMB);
    assert(local_share_contains(&shares, "/srv/media"));
    assert(local_share_contains(&shares, "/srv/media/"));
    assert(local_share_contains(&shares, "/srv/My Files"));
    assert(local_share_contains(&shares, "/srv/included"));
    assert(!local_share_contains(&shares, "/srv/media/child"));
    assert(!local_share_contains(&shares, "/srv/mediabox"));
    fixture(file, "#VERSION 2\npath=/home/me/Public\nusershare_acl=Everyone:F\n");
    shares_read(&shares, file, SHARE_USER, 0);
    assert(local_share_kind(&shares, "/home/me/Public") == LOCAL_SHARE_SMB);
    fixture(file, "# exports\n/srv/nfs *(ro)\n/srv/with\\040space host(rw)\n\"/srv/quoted space\" host(ro)\n# /ignored *(rw)\n");
    shares_read(&shares, file, SHARE_NFS, 0);
    assert(local_share_kind(&shares, "/srv/nfs") == LOCAL_SHARE_NFS);
    assert(local_share_contains(&shares, "/srv/with space"));
    assert(local_share_contains(&shares, "/srv/quoted space"));
    fixture(file, "<Directory \"/srv/dav files\">\n DAV On\n</Directory>\n<Directory /srv/plain>\nDav Off\n</Directory>\n<Directory /srv/wild*>\nDav On\n</Directory>\n");
    shares_read(&shares, file, SHARE_APACHE, 0);
    assert(local_share_kind(&shares, "/srv/dav files") == LOCAL_SHARE_OTHER);
    assert(!local_share_contains(&shares, "/srv/plain"));
    assert(!local_share_contains(&shares, "/srv/wild*"));
    // A root exported by several protocols retains every detected kind.
    share_add(&shares, "/srv/media", LOCAL_SHARE_NFS);
    assert(local_share_kind(&shares, "/srv/media") == (LOCAL_SHARE_SMB | LOCAL_SHARE_NFS));
    // An export configured via the real path also badges the same folder
    // when its parent is reached through a symlink.
    char actual[PATH_MAX], alias[PATH_MAX], exported[PATH_MAX], via_alias[PATH_MAX];
    path_join(actual, sizeof actual, dir, "actual"); assert(!mkdir(actual, 0700));
    path_join(alias, sizeof alias, dir, "alias"); assert(!symlink(actual, alias));
    path_join(exported, sizeof exported, actual, "backups"); assert(!mkdir(exported, 0700));
    path_join(via_alias, sizeof via_alias, alias, "backups");
    share_add(&shares, exported, LOCAL_SHARE_NFS);
    assert(local_share_kind(&shares, via_alias) == LOCAL_SHARE_NFS);
    char via_alias_slash[PATH_MAX]; size_t alias_len = strlen(via_alias);
    assert(alias_len + 1 < sizeof via_alias_slash);
    memcpy(via_alias_slash, via_alias, alias_len);
    via_alias_slash[alias_len] = '/'; via_alias_slash[alias_len + 1] = 0;
    assert(local_share_kind(&shares, via_alias_slash) == LOCAL_SHARE_NFS);
    assert(!local_share_contains(&shares, alias));
    assert(!local_share_contains(&shares, actual));
    // Cyclic Samba includes terminate, and re-reading never duplicates roots.
    int before = shares.count;
    snprintf(config, sizeof config, "[global]\ninclude=%s\n", file);
    fixture(file, config);
    shares_read(&shares, file, SHARE_SAMBA, 0);
    assert(shares.count == before);
    shares_read(&shares, "/nonexistent/fe-shares", SHARE_SAMBA, 0);
    // Host configuration includes resolve inside the host root; the exported
    // folder remains a normal host path, without a /run/host prefix.
    char hostinc[PATH_MAX]; path_join(hostinc, sizeof hostinc, dir, "hostinclude");
    fixture(hostinc, "[hostshare]\npath=/home/me/hostshare\n");
    fixture(file, "[global]\ninclude=/hostinclude\n");
    shares_read_at(&shares, file, SHARE_SAMBA, 0, dir);
    assert(local_share_contains(&shares, "/home/me/hostshare"));
    assert(!local_share_contains(&shares, "/run/host/home/me/hostshare"));
    // Samba can configure a usershare directory other than the default.
    char users[PATH_MAX], userfile[PATH_MAX];
    path_join(users, sizeof users, dir, "users"); assert(!mkdir(users, 0700));
    path_join(userfile, sizeof userfile, users, "share");
    fixture(userfile, "path=/home/me/custom-user-share\n");
    fixture(file, "[global]\nusershare path=/users\n");
    shares_read_at(&shares, file, SHARE_SAMBA, 0, dir);
    assert(local_share_contains(&shares, "/home/me/custom-user-share"));
    unlink(userfile); rmdir(users); unlink(hostinc);
    unlink(file); unlink(inc); rmdir(exported); unlink(alias); rmdir(actual); rmdir(dir);
    puts("local shares: passed");
}
