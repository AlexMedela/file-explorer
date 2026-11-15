// Header label preferences; plain settings data, independent of SDL rendering.
enum { HEADER_APP, HEADER_HOSTNAME, HEADER_CUSTOM, HEADER_HIDDEN };
#define HEADER_APP_NAME "File-Explorer"
static int g_header_label = HEADER_APP;
static char g_header_custom[96];
static bool g_show_window_controls = true;
static bool g_show_header_icon = true;

static bool header_text_valid(const char *text) {
    size_t n = strlen(text);
    if (!n || n >= sizeof g_header_custom || strspn(text, " ") == n) return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++)
        if (*p < 32 || *p == 127) return false;
    return true;
}
static void header_label_defaults(void) {
    g_header_label = HEADER_APP;
    g_header_custom[0] = 0;
}
static void header_preferences_defaults(void) {
    header_label_defaults();
    g_show_window_controls = true;
    g_show_header_icon = true;
}
static bool header_preferences_read(const char *key, const char *value) {
    if (!strcmp(key, "show_window_controls")) { g_show_window_controls = atoi(value) != 0; return true; }
    if (!strcmp(key, "show_header_icon")) { g_show_header_icon = atoi(value) != 0; return true; }
    if (!strcmp(key, "header_label")) {
        g_header_label = !strcmp(value, "hostname") ? HEADER_HOSTNAME : !strcmp(value, "custom") ? HEADER_CUSTOM :
                         !strcmp(value, "hidden") ? HEADER_HIDDEN : HEADER_APP;
        return true;
    }
    if (!strcmp(key, "header_text")) {
        if (header_text_valid(value)) snprintf(g_header_custom, sizeof g_header_custom, "%s", value);
        return true;
    }
    return false;
}
static void header_preferences_write(FILE *f) {
    static const char *names[] = {"app", "hostname", "custom", "hidden"};
    fprintf(f, "header_label=%s\nheader_text=%s\nshow_window_controls=%d\nshow_header_icon=%d\n",
            names[CLAMP(g_header_label, HEADER_APP, HEADER_HIDDEN)], g_header_custom,
            g_show_window_controls, g_show_header_icon);
}
static const char *header_label_text(void) {
    static char hostname[256];
    switch (g_header_label) {
    case HEADER_HIDDEN: return "";
    case HEADER_CUSTOM: return g_header_custom[0] ? g_header_custom : HEADER_APP_NAME;
    case HEADER_HOSTNAME:
        if (!hostname[0]) {
            if (gethostname(hostname, sizeof hostname - 1)) snprintf(hostname, sizeof hostname, "localhost");
            hostname[sizeof hostname - 1] = 0;
        }
        return hostname;
    default: return HEADER_APP_NAME;
    }
}
