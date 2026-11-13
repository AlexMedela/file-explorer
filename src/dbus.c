// dbus.c - optional libdbus loader shared by desktop integrations

typedef struct { const char *name, *message; unsigned dummy; void *pad; void *more[4]; } IgDBusError;
typedef struct { void *p[16]; } IgDBusIter; // (libdbus's DBusMessageIter is smaller; it only writes its own part)

static struct {
    void *lib, *conn;
    void (*error_init)(IgDBusError *);
    void (*error_free)(IgDBusError *);
    void *(*bus_get_private)(int, IgDBusError *);
    int (*request_name)(void *, const char *, unsigned, IgDBusError *);
    int (*release_name)(void *, const char *, IgDBusError *);
    unsigned (*read_write)(void *, int);
    void *(*pop_message)(void *);
    unsigned (*is_method_call)(void *, const char *, const char *);
    unsigned (*iter_init)(void *, IgDBusIter *);
    int (*iter_get_arg_type)(IgDBusIter *);
    void (*iter_recurse)(IgDBusIter *, IgDBusIter *);
    void (*iter_get_basic)(IgDBusIter *, void *);
    unsigned (*iter_next)(IgDBusIter *);
    void *(*new_method_return)(void *);
    unsigned (*append_args)(void *, int, ...);
    unsigned (*send)(void *, void *, unsigned *);
    void (*flush)(void *);
    void (*message_unref)(void *);
    void (*connection_close)(void *);
    void (*connection_unref)(void *);
    atomic_bool stop, running;
    int state; // IG_FM1_*
} g_fm1;

enum { IG_FM1_OFF, IG_FM1_SERVING, IG_FM1_TAKEN, IG_FM1_NO_DBUS };

static bool ig_dbus_load(void) {
    if (g_fm1.lib) return true;
    void *l = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!l) return false;
    #define IG_SYM(field, name) *(void **)&g_fm1.field = dlsym(l, name); if (!g_fm1.field) { dlclose(l); return false; }
    IG_SYM(error_init, "dbus_error_init")
    IG_SYM(error_free, "dbus_error_free")
    IG_SYM(bus_get_private, "dbus_bus_get_private")
    IG_SYM(request_name, "dbus_bus_request_name")
    IG_SYM(release_name, "dbus_bus_release_name")
    IG_SYM(read_write, "dbus_connection_read_write")
    IG_SYM(pop_message, "dbus_connection_pop_message")
    IG_SYM(is_method_call, "dbus_message_is_method_call")
    IG_SYM(iter_init, "dbus_message_iter_init")
    IG_SYM(iter_get_arg_type, "dbus_message_iter_get_arg_type")
    IG_SYM(iter_recurse, "dbus_message_iter_recurse")
    IG_SYM(iter_get_basic, "dbus_message_iter_get_basic")
    IG_SYM(iter_next, "dbus_message_iter_next")
    IG_SYM(new_method_return, "dbus_message_new_method_return")
    IG_SYM(append_args, "dbus_message_append_args")
    IG_SYM(send, "dbus_connection_send")
    IG_SYM(flush, "dbus_connection_flush")
    IG_SYM(message_unref, "dbus_message_unref")
    IG_SYM(connection_close, "dbus_connection_close")
    IG_SYM(connection_unref, "dbus_connection_unref")
    #undef IG_SYM
    g_fm1.lib = l;
    return true;
}
