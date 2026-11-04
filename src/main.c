// main.c - unity build entry point
#include "base.h"
#include <ctype.h>
#include <regex.h>
#include <dlfcn.h>
#include <SDL3/SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>

#if defined(__SANITIZE_ADDRESS__)
// Debug (ASan) builds: SDL unloads the video/GPU libraries at exit, which makes memory they still own look
// leaked (reported from "<unknown module>"). Keep them loaded so LeakSanitizer only reports real leaks.
int dlclose(void *handle) {
    (void)handle;
    return 0;
}
// Known leaks in system libraries (not ours): libX11 leaks the converted title on every SDL_SetWindowTitle; Mesa's
// EGL driver keeps its device list (libdrm) and driver state until process exit.
const char *__lsan_default_suppressions(void) { return "leak:libX11.so\nleak:libEGL_mesa.so\nleak:libgallium-\n"; }
#endif

#include "pool.c"
#include "ipc.c"
#include "fs.c"
#include "ops.c"
#include "archive.c"
#include "search.c"
#include "diskuse.c"
#include "compare.c"
#include "snapshots.c"
#include "preview.c"
#include "usb.c"
#include "plist.c"
#include "iphone.c"
#include "mtp.c"
#include "devfs.c"
#include "devices.c"
#include "git.c"
#include "apps.c"
#include "font.c"
#include "draw.c"
#include "theme.c"
#include "icons.c"
#include "app.c"
