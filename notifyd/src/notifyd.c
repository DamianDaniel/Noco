#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gio/gio.h>
#include <string.h>
#include "wallpaper.h"
#include "config.h"
#include "render.h"

typedef struct {
    guint32 id;
    GtkWidget *window;
    GtkWidget *drawing_area;
    int x, y, w, h;
    guint timeout_source;
} NotifPopup;

typedef struct {
    NocoConfig cfg;
    WallpaperCache wc;
    Display *dpy;
    Window root;
    GList *popups;
    guint32 next_id;
    guint owner_id;
    GDBusNodeInfo *introspection;
} NotifyDaemon;

static const char *introspection_xml =
"<node>"
"  <interface name='org.freedesktop.Notifications'>"
"    <method name='Notify'>"
"      <arg type='s' name='app_name' direction='in'/>"
"      <arg type='u' name='replaces_id' direction='in'/>"
"      <arg type='s' name='app_icon' direction='in'/>"
"      <arg type='s' name='summary' direction='in'/>"
"      <arg type='s' name='body' direction='in'/>"
"      <arg type='as' name='actions' direction='in'/>"
"      <arg type='a{sv}' name='hints' direction='in'/>"
"      <arg type='i' name='expire_timeout' direction='in'/>"
"      <arg type='u' name='id' direction='out'/>"
"    </method>"
"    <method name='CloseNotification'>"
"      <arg type='u' name='id' direction='in'/>"
"    </method>"
"    <method name='GetCapabilities'>"
"      <arg type='as' name='capabilities' direction='out'/>"
"    </method>"
"    <method name='GetServerInformation'>"
"      <arg type='s' name='name' direction='out'/>"
"      <arg type='s' name='vendor' direction='out'/>"
"      <arg type='s' name='version' direction='out'/>"
"      <arg type='s' name='spec_version' direction='out'/>"
"    </method>"
"    <signal name='NotificationClosed'>"
"      <arg type='u' name='id'/>"
"      <arg type='u' name='reason'/>"
"    </signal>"
"    <signal name='ActionInvoked'>"
"      <arg type='u' name='id'/>"
"      <arg type='s' name='action_key'/>"
"    </signal>"
"  </interface>"
"</node>";

static void reflow_popups(NotifyDaemon *d) {
    GdkRectangle geom;
    gdk_monitor_get_geometry(
        gdk_display_get_primary_monitor(gdk_display_get_default()), &geom);

    int is_right = strstr(d->cfg.notif_position, "right") != NULL;
    int is_top = strstr(d->cfg.notif_position, "top") != NULL;
    int margin = d->cfg.notif_margin;
    int spacing = d->cfg.notif_spacing;

    int cursor_y = is_top ? geom.y + margin : geom.y + geom.height - margin;

    for (GList *it = d->popups; it; it = it->next) {
        NotifPopup *p = it->data;
        int x = is_right ? geom.x + geom.width - margin - p->w : geom.x + margin;
        int y = is_top ? cursor_y : cursor_y - p->h;

        p->x = x;
        p->y = y;
        gtk_window_move(GTK_WINDOW(p->window), x, y);

        cursor_y = is_top ? (y + p->h + spacing) : (y - spacing);
    }
}

static void destroy_popup(NotifyDaemon *d, NotifPopup *p) {
    if (p->timeout_source) g_source_remove(p->timeout_source);
    gtk_widget_destroy(p->window);
    d->popups = g_list_remove(d->popups, p);
    g_free(p);
    reflow_popups(d);
}

static NotifPopup *find_popup(NotifyDaemon *d, guint32 id) {
    for (GList *it = d->popups; it; it = it->next) {
        NotifPopup *p = it->data;
        if (p->id == id) return p;
    }
    return NULL;
}

static gboolean on_popup_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    NotifPopup *p = (NotifPopup *)data;
    NotifyDaemon *d = g_object_get_data(G_OBJECT(widget), "daemon");
    (void)widget;
    noco_draw_pseudo_transparent(cr, &d->wc, &d->cfg,
        p->x, p->y, p->w, p->h, d->cfg.corner_radius, NOCO_CORNER_ALL);
    return FALSE;
}

static gboolean on_popup_click(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    (void)event;
    NotifPopup *p = (NotifPopup *)data;
    NotifyDaemon *d = g_object_get_data(G_OBJECT(p->window), "daemon");
    destroy_popup(d, p);
    return TRUE;
}

static gboolean on_popup_timeout(gpointer data) {
    NotifPopup *p = (NotifPopup *)data;
    NotifyDaemon *d = g_object_get_data(G_OBJECT(p->window), "daemon");
    p->timeout_source = 0;
    destroy_popup(d, p);
    return G_SOURCE_REMOVE;
}

static NotifPopup *create_popup(NotifyDaemon *d, guint32 id, const char *app_name,
        const char *icon_name, const char *summary, const char *body, int timeout_ms) {
    NotifPopup *p = g_new0(NotifPopup, 1);
    p->id = id;
    p->w = d->cfg.notif_width;

    p->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(p->window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(p->window), GDK_WINDOW_TYPE_HINT_NOTIFICATION);
    gtk_window_set_resizable(GTK_WINDOW(p->window), FALSE);
    gtk_widget_set_app_paintable(p->window, TRUE);
    g_object_set_data(G_OBJECT(p->window), "daemon", d);

    GdkScreen *screen = gtk_widget_get_screen(p->window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(p->window, visual);

    GtkWidget *overlay = gtk_overlay_new();
    p->drawing_area = gtk_drawing_area_new();
    g_signal_connect(p->drawing_area, "draw", G_CALLBACK(on_popup_draw), p);
    gtk_container_add(GTK_CONTAINER(overlay), p->drawing_area);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(hbox, 12);
    gtk_widget_set_margin_end(hbox, 12);
    gtk_widget_set_margin_top(hbox, 10);
    gtk_widget_set_margin_bottom(hbox, 10);

    GtkWidget *icon = icon_name && *icon_name
        ? gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_DND)
        : gtk_image_new_from_icon_name("dialog-information", GTK_ICON_SIZE_DND);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
    gtk_widget_set_valign(icon, GTK_ALIGN_START);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    GtkWidget *title_label = gtk_label_new(summary && *summary ? summary : app_name);
    gtk_widget_set_name(title_label, "notif-title");
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(title_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(title_label), 32);

    GtkWidget *body_label = gtk_label_new(body);
    gtk_widget_set_name(body_label, "notif-body");
    gtk_widget_set_halign(body_label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(body_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(body_label), 32);
    gtk_label_set_lines(GTK_LABEL(body_label), 3);
    gtk_label_set_ellipsize(GTK_LABEL(body_label), PANGO_ELLIPSIZE_END);

    gtk_box_pack_start(GTK_BOX(vbox), title_label, FALSE, FALSE, 0);
    if (body && *body) gtk_box_pack_start(GTK_BOX(vbox), body_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), hbox);
    gtk_container_add(GTK_CONTAINER(p->window), overlay);

    GtkWidget *ebox = gtk_event_box_new();
    (void)ebox;
    g_signal_connect(p->window, "button-press-event", G_CALLBACK(on_popup_click), p);
    gtk_widget_add_events(p->window, GDK_BUTTON_PRESS_MASK);

    gtk_widget_show_all(p->window);

    GtkRequisition req;
    gtk_widget_get_preferred_size(p->window, NULL, &req);
    p->h = req.height > 0 ? req.height : 64;
    gtk_widget_set_size_request(p->window, p->w, p->h);

    if (timeout_ms > 0) {
        p->timeout_source = g_timeout_add(timeout_ms, on_popup_timeout, p);
    }

    return p;
}

static void handle_method_call(
    GDBusConnection *conn, const gchar *sender, const gchar *object_path,
    const gchar *interface_name, const gchar *method_name, GVariant *params,
    GDBusMethodInvocation *invocation, gpointer user_data) {

    (void)conn; (void)sender; (void)object_path; (void)interface_name;
    NotifyDaemon *d = (NotifyDaemon *)user_data;

    if (g_strcmp0(method_name, "Notify") == 0) {
        const char *app_name, *app_icon, *summary, *body;
        guint32 replaces_id;
        gint32 expire_timeout;
        GVariantIter *actions_iter;
        GVariantIter *hints_iter;

        g_variant_get(params, "(&su&s&s&sasa{sv}i)",
            &app_name, &replaces_id, &app_icon, &summary, &body,
            &actions_iter, &hints_iter, &expire_timeout);
        g_variant_iter_free(actions_iter);
        g_variant_iter_free(hints_iter);

        guint32 id = replaces_id != 0 ? replaces_id : d->next_id++;
        NotifPopup *existing = find_popup(d, id);
        if (existing) destroy_popup(d, existing);

        int timeout_ms = expire_timeout > 0 ? expire_timeout : d->cfg.notif_default_timeout_ms;
        NotifPopup *p = create_popup(d, id, app_name, app_icon, summary, body, timeout_ms);
        d->popups = g_list_append(d->popups, p);
        reflow_popups(d);

        g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", id));
    } else if (g_strcmp0(method_name, "CloseNotification") == 0) {
        guint32 id;
        g_variant_get(params, "(u)", &id);
        NotifPopup *p = find_popup(d, id);
        if (p) destroy_popup(d, p);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "GetCapabilities") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
        g_variant_builder_add(&builder, "s", "body");
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(as)", &builder));
    } else if (g_strcmp0(method_name, "GetServerInformation") == 0) {
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(ssss)", "noco-notifyd", "noco", "1.0", "1.2"));
    }
}

static const GDBusInterfaceVTable interface_vtable = {
    handle_method_call, NULL, NULL, {0}
};

static void on_bus_acquired(GDBusConnection *conn, const gchar *name, gpointer user_data) {
    (void)name;
    NotifyDaemon *d = (NotifyDaemon *)user_data;
    g_dbus_connection_register_object(conn, "/org/freedesktop/Notifications",
        d->introspection->interfaces[0], &interface_vtable, d, NULL, NULL);
}

static void on_name_lost(GDBusConnection *conn, const gchar *name, gpointer user_data) {
    (void)conn; (void)user_data;
    g_printerr("noco-notifyd: could not own %s, another notification daemon running?\n", name);
}

static void apply_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css =
        "#notif-title { color: rgba(255,255,255,0.95); font-weight: 600; font-size: 13px; }"
        "#notif-body { color: rgba(255,255,255,0.75); font-size: 12px; }";
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    apply_css();

    NotifyDaemon d;
    memset(&d, 0, sizeof(d));
    noco_config_load(&d.cfg);
    d.next_id = 1;

    gtk_widget_realize(gtk_invisible_new());
    d.dpy = gdk_x11_get_default_xdisplay();
    d.root = gdk_x11_get_default_root_xwindow();
    wallpaper_cache_init(&d.wc, d.dpy, d.root);

    d.introspection = g_dbus_node_info_new_for_xml(introspection_xml, NULL);

    d.owner_id = g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.Notifications",
        G_BUS_NAME_OWNER_FLAGS_NONE, on_bus_acquired, NULL, on_name_lost, &d, NULL);

    (void)argc;
    (void)argv;

    gtk_main();

    g_bus_unown_name(d.owner_id);
    g_dbus_node_info_unref(d.introspection);
    wallpaper_cache_free(&d.wc);
    return 0;
}
