#include <gio/gio.h>
#include <string.h>

typedef struct _NotifyNotification NotifyNotification;
typedef void (*NotifyActionCallback)(NotifyNotification*, char*, gpointer);
typedef struct { char* action; char* label; NotifyActionCallback cb; gpointer data; GDestroyNotify fr; } NAction;
struct _NotifyNotification { GObject parent; guint id; char* summary; char* body; char* icon; char* app; int timeout; GHashTable* hints; GPtrArray* actions; int reason; guint sub_closed, sub_action; };
typedef struct _NotifyNotificationClass { GObjectClass parent; void (*closed)(NotifyNotification*); } NotifyNotificationClass;

enum { P_0, P_ID, P_SUMMARY, P_BODY, P_ICON, P_APP, P_REASON, P_N };
static char* g_app;
static GDBusConnection* g_bus;
static guint g_closed_sig;

G_DEFINE_TYPE(NotifyNotification, notify_notification, G_TYPE_OBJECT)

static void na_free(gpointer p) { NAction* a = p; g_free(a->action); g_free(a->label); if (a->fr && a->data) a->fr(a->data); g_free(a); }

static void nn_unsub(NotifyNotification* n) {
    if (g_bus && n->sub_closed) g_dbus_connection_signal_unsubscribe(g_bus, n->sub_closed);
    if (g_bus && n->sub_action) g_dbus_connection_signal_unsubscribe(g_bus, n->sub_action);
    n->sub_closed = n->sub_action = 0;
}
static void nn_dispose(GObject* o) { nn_unsub((NotifyNotification*)o); G_OBJECT_CLASS(notify_notification_parent_class)->dispose(o); }
static void nn_finalize(GObject* o) {
    NotifyNotification* n = (NotifyNotification*)o;
    g_free(n->summary); g_free(n->body); g_free(n->icon); g_free(n->app);
    g_hash_table_unref(n->hints); g_ptr_array_free(n->actions, TRUE);
    G_OBJECT_CLASS(notify_notification_parent_class)->finalize(o);
}
static void nn_set_prop(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    NotifyNotification* n = (NotifyNotification*)o;
    switch (id) {
    case P_ID: n->id = (guint)g_value_get_int(v); break;
    case P_SUMMARY: g_free(n->summary); n->summary = g_value_dup_string(v); if (!n->summary) n->summary = g_strdup(""); break;
    case P_BODY: g_free(n->body); n->body = g_value_dup_string(v); if (!n->body) n->body = g_strdup(""); break;
    case P_ICON: g_free(n->icon); n->icon = g_value_dup_string(v); if (!n->icon) n->icon = g_strdup(""); break;
    case P_APP: g_free(n->app); n->app = g_value_dup_string(v); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}
static void nn_get_prop(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    NotifyNotification* n = (NotifyNotification*)o;
    switch (id) {
    case P_ID: g_value_set_int(v, (int)n->id); break;
    case P_SUMMARY: g_value_set_string(v, n->summary); break;
    case P_BODY: g_value_set_string(v, n->body); break;
    case P_ICON: g_value_set_string(v, n->icon); break;
    case P_APP: g_value_set_string(v, n->app); break;
    case P_REASON: g_value_set_int(v, n->reason); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}
static void notify_notification_class_init(NotifyNotificationClass* k) {
    GObjectClass* oc = G_OBJECT_CLASS(k);
    oc->dispose = nn_dispose; oc->finalize = nn_finalize; oc->set_property = nn_set_prop; oc->get_property = nn_get_prop;
    g_object_class_install_property(oc, P_ID, g_param_spec_int("id", "id", "id", 0, G_MAXINT32, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
    g_object_class_install_property(oc, P_SUMMARY, g_param_spec_string("summary", "summary", "summary", "", G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
    g_object_class_install_property(oc, P_BODY, g_param_spec_string("body", "body", "body", "", G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
    g_object_class_install_property(oc, P_ICON, g_param_spec_string("icon-name", "icon-name", "icon-name", "", G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
    g_object_class_install_property(oc, P_APP, g_param_spec_string("app-name", "app-name", "app-name", NULL, G_PARAM_READWRITE));
    g_object_class_install_property(oc, P_REASON, g_param_spec_int("closed-reason", "closed-reason", "closed-reason", -1, G_MAXINT32, -1, G_PARAM_READABLE));
    g_closed_sig = g_signal_new("closed", G_TYPE_FROM_CLASS(k), G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET(NotifyNotificationClass, closed), NULL, NULL, NULL, G_TYPE_NONE, 0);
}
static void notify_notification_init(NotifyNotification* n) {
    n->timeout = -1; n->reason = -1;
    n->hints = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_variant_unref);
    n->actions = g_ptr_array_new_with_free_func(na_free);
}

static GDBusConnection* bus(void) {
    if (!g_bus) g_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    return g_bus;
}

gboolean notify_init(const char* app) { g_free(g_app); g_app = g_strdup(app ? app : ""); return TRUE; }
gboolean notify_is_initted(void) { return g_app != NULL; }
void notify_uninit(void) { g_free(g_app); g_app = NULL; }
const char* notify_get_app_name(void) { return g_app; }
void notify_set_app_name(const char* a) { g_free(g_app); g_app = g_strdup(a); }

GList* notify_get_server_caps(void) {
    GList* l = NULL;
    GDBusConnection* c = bus();
    if (c) {
        GVariant* r = g_dbus_connection_call_sync(c, "org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "GetCapabilities", NULL, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, NULL);
        if (r) { GVariantIter* it; const char* s; g_variant_get(r, "(as)", &it); while (g_variant_iter_next(it, "&s", &s)) l = g_list_append(l, g_strdup(s)); g_variant_iter_free(it); g_variant_unref(r); return l; }
    }
    return g_list_append(l, g_strdup("body"));
}

gboolean notify_get_server_info(char** name, char** vendor, char** version, char** spec) {
    GDBusConnection* c = bus();
    GVariant* r = c ? g_dbus_connection_call_sync(c, "org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "GetServerInformation", NULL, G_VARIANT_TYPE("(ssss)"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, NULL) : NULL;
    const char *a = "NoovexOS", *b = "NoovexOS", *v = "1.0", *s = "1.2";
    if (r) g_variant_get(r, "(&s&s&s&s)", &a, &b, &v, &s);
    if (name) *name = g_strdup(a);
    if (vendor) *vendor = g_strdup(b);
    if (version) *version = g_strdup(v);
    if (spec) *spec = g_strdup(s);
    if (r) g_variant_unref(r);
    return TRUE;
}

NotifyNotification* notify_notification_new(const char* summary, const char* body, const char* icon) {
    return g_object_new(notify_notification_get_type(), "summary", summary ? summary : "", "body", body ? body : "", "icon-name", icon ? icon : "", NULL);
}
gboolean notify_notification_update(NotifyNotification* n, const char* summary, const char* body, const char* icon) {
    if (!n) return FALSE;
    g_object_set(n, "summary", summary ? summary : "", "body", body ? body : "", "icon-name", icon ? icon : "", NULL);
    return TRUE;
}

static void on_closed(GDBusConnection* c, const char* s, const char* p, const char* i, const char* sig, GVariant* prm, gpointer d) {
    (void)c; (void)s; (void)p; (void)i; (void)sig;
    NotifyNotification* n = d; guint id = 0, reason = 0;
    if (!g_variant_is_of_type(prm, G_VARIANT_TYPE("(uu)"))) return;
    g_variant_get(prm, "(uu)", &id, &reason);
    if (!n->id || id != n->id) return;
    g_object_ref(n);
    n->reason = (int)reason; n->id = 0;
    nn_unsub(n);
    g_signal_emit(n, g_closed_sig, 0);
    g_object_unref(n);
}
static void on_action(GDBusConnection* c, const char* s, const char* p, const char* i, const char* sig, GVariant* prm, gpointer d) {
    (void)c; (void)s; (void)p; (void)i; (void)sig;
    NotifyNotification* n = d; guint id = 0; const char* act = NULL;
    if (!g_variant_is_of_type(prm, G_VARIANT_TYPE("(us)"))) return;
    g_variant_get(prm, "(u&s)", &id, &act);
    if (!n->id || id != n->id || !act) return;
    g_object_ref(n);
    for (guint k = 0; k < n->actions->len; k++) { NAction* a = g_ptr_array_index(n->actions, k); if (a->cb && !strcmp(a->action, act)) { a->cb(n, (char*)act, a->data); break; } }
    g_object_unref(n);
}

gboolean notify_notification_show(NotifyNotification* n, GError** err) {
    if (!n) return FALSE;
    GDBusConnection* c = bus();
    if (!c) { g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "no session bus"); return FALSE; }
    GVariantBuilder ab; g_variant_builder_init(&ab, G_VARIANT_TYPE("as"));
    for (guint i = 0; i < n->actions->len; i++) { NAction* a = g_ptr_array_index(n->actions, i); g_variant_builder_add(&ab, "s", a->action); g_variant_builder_add(&ab, "s", a->label); }
    GVariantBuilder hb; g_variant_builder_init(&hb, G_VARIANT_TYPE("a{sv}"));
    GHashTableIter hi; gpointer hk, hv; g_hash_table_iter_init(&hi, n->hints);
    while (g_hash_table_iter_next(&hi, &hk, &hv)) g_variant_builder_add(&hb, "{sv}", (const char*)hk, (GVariant*)hv);
    if (!n->sub_closed) n->sub_closed = g_dbus_connection_signal_subscribe(c, NULL, "org.freedesktop.Notifications", "NotificationClosed", "/org/freedesktop/Notifications", NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_closed, n, NULL);
    if (!n->sub_action) n->sub_action = g_dbus_connection_signal_subscribe(c, NULL, "org.freedesktop.Notifications", "ActionInvoked", "/org/freedesktop/Notifications", NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_action, n, NULL);
    GVariant* r = g_dbus_connection_call_sync(c, "org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify",
        g_variant_new("(susssasa{sv}i)", n->app ? n->app : (g_app ? g_app : ""), n->id, n->icon, n->summary, n->body, &ab, &hb, n->timeout),
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 5000, NULL, err);
    if (!r) return FALSE;
    g_variant_get(r, "(u)", &n->id);
    g_variant_unref(r);
    return TRUE;
}
gboolean notify_notification_close(NotifyNotification* n, GError** err) {
    if (!n) return FALSE;
    GDBusConnection* c = bus();
    if (c && n->id) {
        GVariant* r = g_dbus_connection_call_sync(c, "org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "CloseNotification", g_variant_new("(u)", n->id), NULL, G_DBUS_CALL_FLAGS_NONE, 3000, NULL, err);
        if (r) g_variant_unref(r);
    }
    if (n->reason < 0 || n->id) {
        g_object_ref(n);
        n->reason = 3; n->id = 0; nn_unsub(n);
        g_signal_emit(n, g_closed_sig, 0);
        g_object_unref(n);
    }
    return TRUE;
}
static void hint(NotifyNotification* n, const char* k, GVariant* v) { if (n && k && v) g_hash_table_replace(n->hints, g_strdup(k), g_variant_ref_sink(v)); }
void notify_notification_set_timeout(NotifyNotification* n, int t) { if (n) n->timeout = t; }
void notify_notification_set_category(NotifyNotification* n, const char* c) { if (c) hint(n, "category", g_variant_new_string(c)); }
void notify_notification_set_urgency(NotifyNotification* n, int u) { hint(n, "urgency", g_variant_new_byte((guchar)u)); }
void notify_notification_set_app_name(NotifyNotification* n, const char* a) { if (n) { g_free(n->app); n->app = g_strdup(a); } }
void notify_notification_set_hint(NotifyNotification* n, const char* k, GVariant* v) { if (!v) { if (n && k) g_hash_table_remove(n->hints, k); return; } hint(n, k, v); }
void notify_notification_set_hint_string(NotifyNotification* n, const char* k, const char* v) { if (v) hint(n, k, g_variant_new_string(v)); }
void notify_notification_set_hint_int32(NotifyNotification* n, const char* k, gint v) { hint(n, k, g_variant_new_int32(v)); }
void notify_notification_set_hint_uint32(NotifyNotification* n, const char* k, guint v) { hint(n, k, g_variant_new_uint32(v)); }
void notify_notification_set_hint_double(NotifyNotification* n, const char* k, gdouble v) { hint(n, k, g_variant_new_double(v)); }
void notify_notification_set_hint_byte(NotifyNotification* n, const char* k, guchar v) { hint(n, k, g_variant_new_byte(v)); }
void notify_notification_set_hint_boolean(NotifyNotification* n, const char* k, gboolean v) { hint(n, k, g_variant_new_boolean(v)); }
void notify_notification_clear_hints(NotifyNotification* n) { if (n) g_hash_table_remove_all(n->hints); }
void notify_notification_set_image_from_pixbuf(NotifyNotification* n, gpointer p) { (void)n; (void)p; }
void notify_notification_set_icon_from_pixbuf(NotifyNotification* n, gpointer p) { (void)n; (void)p; }
void notify_notification_add_action(NotifyNotification* n, const char* action, const char* label, NotifyActionCallback cb, gpointer data, GDestroyNotify fr) {
    if (!n || !action || !label) { if (fr && data) fr(data); return; }
    NAction* a = g_new0(NAction, 1); a->action = g_strdup(action); a->label = g_strdup(label); a->cb = cb; a->data = data; a->fr = fr;
    g_ptr_array_add(n->actions, a);
}
void notify_notification_clear_actions(NotifyNotification* n) { if (n) g_ptr_array_set_size(n->actions, 0); }
gint notify_notification_get_closed_reason(const NotifyNotification* n) { return n ? n->reason : -1; }
