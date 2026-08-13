#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <string.h>
#include <time.h>
#include "wallpaper.h"
#include "config.h"
#include "render.h"

typedef struct {
    NocoConfig cfg;
    GtkWidget *window;
    GtkWidget *drawing_area;
    GtkWidget *clock_label;
    GtkWidget *ws_box;
    GtkWidget *ws_buttons[9];
    WallpaperCache wc;
    Display *dpy;
    Window root;
    int panel_x, panel_y, panel_w, panel_h;
    int active_workspace;
    Atom net_current_desktop;
    Atom net_wm_strut_partial;
    Atom net_wm_window_type;
    Atom net_wm_window_type_dock;
    guint root_watch_source;
} Panel;

static void set_dock_hints(Panel *p) {
    Window win = gdk_x11_window_get_xid(gtk_widget_get_window(p->window));

    Atom dock = p->net_wm_window_type_dock;
    XChangeProperty(p->dpy, win, p->net_wm_window_type, XA_ATOM, 32,
        PropModeReplace, (unsigned char *)&dock, 1);

    long strut[4] = {0, 0, 0, 0};
    long strut_partial[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    if (p->cfg.panel_position == NOCO_PANEL_TOP) {
        strut[2] = p->panel_h;
        strut_partial[2] = p->panel_h;
        strut_partial[8] = 0;
        strut_partial[9] = p->panel_w;
    } else {
        strut[3] = p->panel_h;
        strut_partial[3] = p->panel_h;
        strut_partial[10] = 0;
        strut_partial[11] = p->panel_w;
    }

    XChangeProperty(p->dpy, p->root, XInternAtom(p->dpy, "_NET_WM_STRUT", False),
        XA_CARDINAL, 32, PropModeReplace, (unsigned char *)strut, 4);
    XChangeProperty(p->dpy, win, p->net_wm_strut_partial, XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *)strut_partial, 12);
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    Panel *p = (Panel *)data;
    (void)widget;

    noco_draw_pseudo_transparent(cr, &p->wc, &p->cfg,
        p->panel_x, p->panel_y, p->panel_w, p->panel_h, 0, NOCO_CORNER_NONE);

    int border_y = (p->cfg.panel_position == NOCO_PANEL_TOP) ? 0 : p->panel_h - 1;
    cairo_set_source_rgba(cr, 0, 0, 0, p->cfg.border_alpha);
    cairo_rectangle(cr, 0, border_y, p->panel_w, 1);
    cairo_fill(cr);

    return FALSE;
}

static gboolean update_clock(gpointer data) {
    Panel *p = (Panel *)data;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M", tm);
    gtk_label_set_text(GTK_LABEL(p->clock_label), buf);
    return G_SOURCE_CONTINUE;
}

static void refresh_ws_buttons(Panel *p) {
    for (int i = 0; i < 9; i++) {
        GtkStyleContext *ctx = gtk_widget_get_style_context(p->ws_buttons[i]);
        if (i == p->active_workspace) {
            gtk_style_context_add_class(ctx, "ws-active");
        } else {
            gtk_style_context_remove_class(ctx, "ws-active");
        }
    }
}

static void on_ws_button_clicked(GtkButton *btn, gpointer data) {
    Panel *p = (Panel *)data;
    int ws = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "ws-index"));

    XEvent ev = {0};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = p->root;
    ev.xclient.message_type = p->net_current_desktop;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = ws;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(p->dpy, p->root, False,
        SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(p->dpy);

    p->active_workspace = ws;
    refresh_ws_buttons(p);
}

static GdkFilterReturn root_event_filter(GdkXEvent *xevent, GdkEvent *event, gpointer data) {
    Panel *p = (Panel *)data;
    (void)event;
    XEvent *ev = (XEvent *)xevent;

    if (ev->type == PropertyNotify) {
        Atom root_pmap = XInternAtom(p->dpy, "_XROOTPMAP_ID", False);
        Atom cur_desktop = p->net_current_desktop;

        if (ev->xproperty.atom == root_pmap) {
            wallpaper_cache_refresh(&p->wc);
            gtk_widget_queue_draw(p->drawing_area);
        } else if (ev->xproperty.atom == cur_desktop) {
            Atom actual_type;
            int actual_format;
            unsigned long nitems, bytes_after;
            unsigned char *data_ptr = NULL;
            if (XGetWindowProperty(p->dpy, p->root, cur_desktop, 0, 1, False,
                    XA_CARDINAL, &actual_type, &actual_format, &nitems,
                    &bytes_after, &data_ptr) == Success && data_ptr) {
                p->active_workspace = *(long *)data_ptr;
                XFree(data_ptr);
                refresh_ws_buttons(p);
            }
        }
    }
    return GDK_FILTER_CONTINUE;
}

static void build_workspace_switcher(Panel *p) {
    p->ws_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    for (int i = 0; i < 9; i++) {
        char label[4];
        snprintf(label, sizeof(label), "%d", i + 1);
        GtkWidget *btn = gtk_button_new_with_label(label);
        gtk_widget_set_name(btn, "ws-button");
        g_object_set_data(G_OBJECT(btn), "ws-index", GINT_TO_POINTER(i));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_ws_button_clicked), p);
        gtk_box_pack_start(GTK_BOX(p->ws_box), btn, FALSE, FALSE, 0);
        p->ws_buttons[i] = btn;
    }
}

static void apply_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css =
        "#ws-button {"
        "  background: transparent;"
        "  border: none;"
        "  color: rgba(255,255,255,0.7);"
        "  min-width: 22px;"
        "  min-height: 22px;"
        "  padding: 0;"
        "  border-radius: 5px;"
        "}"
        "#ws-button.ws-active {"
        "  background: rgba(255,255,255,0.16);"
        "  color: rgba(255,255,255,0.95);"
        "}"
        "#clock-label {"
        "  color: rgba(255,255,255,0.85);"
        "  font-weight: 600;"
        "}";
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    apply_css();

    Panel p;
    memset(&p, 0, sizeof(p));
    noco_config_load(&p.cfg);

    p.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(p.window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(p.window), GDK_WINDOW_TYPE_HINT_DOCK);
    gtk_window_set_resizable(GTK_WINDOW(p.window), FALSE);
    gtk_widget_set_app_paintable(p.window, TRUE);

    GdkScreen *screen = gtk_widget_get_screen(p.window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(p.window, visual);

    GdkRectangle geom;
    gdk_monitor_get_geometry(
        gdk_display_get_primary_monitor(gdk_display_get_default()), &geom);

    p.panel_x = geom.x;
    p.panel_w = geom.width;
    p.panel_h = p.cfg.panel_height;
    p.panel_y = (p.cfg.panel_position == NOCO_PANEL_TOP)
        ? geom.y : geom.y + geom.height - p.panel_h;

    gtk_window_move(GTK_WINDOW(p.window), p.panel_x, p.panel_y);
    gtk_window_set_default_size(GTK_WINDOW(p.window), p.panel_w, p.panel_h);
    gtk_widget_set_size_request(p.window, p.panel_w, p.panel_h);

    p.drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(p.drawing_area, p.panel_w, p.panel_h);
    g_signal_connect(p.drawing_area, "draw", G_CALLBACK(on_draw), &p);

    GtkWidget *overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(overlay), p.drawing_area);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_start(hbox, 8);
    gtk_widget_set_margin_end(hbox, 8);
    gtk_widget_set_valign(hbox, GTK_ALIGN_CENTER);

    build_workspace_switcher(&p);
    gtk_widget_set_valign(p.ws_box, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(hbox), p.ws_box, FALSE, FALSE, 0);

    p.clock_label = gtk_label_new("--:--");
    gtk_widget_set_name(p.clock_label, "clock-label");
    gtk_widget_set_halign(p.clock_label, GTK_ALIGN_END);
    gtk_widget_set_valign(p.clock_label, GTK_ALIGN_CENTER);
    gtk_box_pack_end(GTK_BOX(hbox), p.clock_label, TRUE, TRUE, 0);

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), hbox);
    gtk_container_add(GTK_CONTAINER(p.window), overlay);

    gtk_widget_realize(p.window);
    p.dpy = gdk_x11_get_default_xdisplay();
    p.root = gdk_x11_get_default_root_xwindow();
    p.net_current_desktop = XInternAtom(p.dpy, "_NET_CURRENT_DESKTOP", False);
    p.net_wm_strut_partial = XInternAtom(p.dpy, "_NET_WM_STRUT_PARTIAL", False);
    p.net_wm_window_type = XInternAtom(p.dpy, "_NET_WM_WINDOW_TYPE", False);
    p.net_wm_window_type_dock = XInternAtom(p.dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);

    wallpaper_cache_init(&p.wc, p.dpy, p.root);

    XSelectInput(p.dpy, p.root, PropertyChangeMask);
    gdk_window_add_filter(NULL, root_event_filter, &p);

    gtk_widget_show_all(p.window);
    set_dock_hints(&p);

    refresh_ws_buttons(&p);
    g_timeout_add_seconds(1, update_clock, &p);
    update_clock(&p);

    gtk_main();

    wallpaper_cache_free(&p.wc);
    return 0;
}
