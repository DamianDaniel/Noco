#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gio/gio.h>
#include <string.h>
#include "wallpaper.h"
#include "config.h"
#include "render.h"

#define LAUNCHER_WIDTH 560
#define LAUNCHER_HEIGHT 420

typedef struct {
    NocoConfig cfg;
    GtkWidget *window;
    GtkWidget *overlay;
    GtkWidget *drawing_area;
    GtkWidget *entry;
    GtkWidget *listbox;
    WallpaperCache wc;
    Display *dpy;
    Window root;
    int win_x, win_y, win_w, win_h;
    GList *all_apps;
} Launcher;

static void launch_selected(Launcher *l, GAppInfo *info) {
    (void)l;
    GError *err = NULL;
    g_app_info_launch(info, NULL, NULL, &err);
    if (err) {
        g_printerr("noco-launcher: failed to launch: %s\n", err->message);
        g_error_free(err);
    }
    gtk_main_quit();
}

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    Launcher *l = (Launcher *)data;
    GAppInfo *info = g_object_get_data(G_OBJECT(row), "app-info");
    if (info) launch_selected(l, info);
}

static GtkWidget *build_app_row(GAppInfo *info) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(hbox, 8);
    gtk_widget_set_margin_end(hbox, 8);
    gtk_widget_set_margin_top(hbox, 6);
    gtk_widget_set_margin_bottom(hbox, 6);

    GIcon *icon = g_app_info_get_icon(info);
    GtkWidget *image = icon
        ? gtk_image_new_from_gicon(icon, GTK_ICON_SIZE_LARGE_TOOLBAR)
        : gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_image_set_pixel_size(GTK_IMAGE(image), 28);

    GtkWidget *label = gtk_label_new(g_app_info_get_display_name(info));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_name(label, "app-label");

    gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(row), hbox);

    g_object_set_data_full(G_OBJECT(row), "app-info", g_object_ref(info), g_object_unref);
    return row;
}

static void populate_list(Launcher *l, const char *filter) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(l->listbox));
    for (GList *it = children; it; it = it->next) gtk_widget_destroy(GTK_WIDGET(it->data));
    g_list_free(children);

    char *needle = filter && *filter ? g_utf8_strdown(filter, -1) : NULL;
    int shown = 0;

    for (GList *it = l->all_apps; it; it = it->next) {
        GAppInfo *info = G_APP_INFO(it->data);
        if (!g_app_info_should_show(info)) continue;

        const char *name = g_app_info_get_display_name(info);
        if (needle) {
            char *hay = g_utf8_strdown(name, -1);
            gboolean match = strstr(hay, needle) != NULL;
            g_free(hay);
            if (!match) continue;
        }

        GtkWidget *row = build_app_row(info);
        gtk_list_box_insert(GTK_LIST_BOX(l->listbox), row, -1);
        shown++;
        if (shown >= 50) break;
    }

    g_free(needle);
    gtk_widget_show_all(l->listbox);
}

static void on_entry_changed(GtkEntry *entry, gpointer data) {
    Launcher *l = (Launcher *)data;
    populate_list(l, gtk_entry_get_text(entry));
}

static void on_entry_activate(GtkEntry *entry, gpointer data) {
    (void)entry;
    Launcher *l = (Launcher *)data;
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(l->listbox), 0);
    if (row) {
        GAppInfo *info = g_object_get_data(G_OBJECT(row), "app-info");
        if (info) launch_selected(l, info);
    }
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget;
    Launcher *l = (Launcher *)data;
    if (event->keyval == GDK_KEY_Escape) {
        gtk_main_quit();
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_Up) {
        gtk_widget_grab_focus(l->listbox);
        return FALSE;
    }
    return FALSE;
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    Launcher *l = (Launcher *)data;
    (void)widget;
    noco_draw_pseudo_transparent(cr, &l->wc, &l->cfg,
        l->win_x, l->win_y, l->win_w, l->win_h,
        l->cfg.corner_radius, NOCO_CORNER_ALL);
    return FALSE;
}

static gboolean on_focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
    (void)widget;
    (void)event;
    (void)data;
    gtk_main_quit();
    return FALSE;
}

static void apply_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css =
        "#search-entry {"
        "  background: rgba(255,255,255,0.08);"
        "  border: none;"
        "  border-radius: 8px;"
        "  color: rgba(255,255,255,0.95);"
        "  padding: 8px 12px;"
        "  font-size: 15px;"
        "}"
        "#app-label { color: rgba(255,255,255,0.92); font-size: 13px; }"
        "list { background: transparent; }"
        "row { background: transparent; border-radius: 6px; }"
        "row:hover, row:selected { background: rgba(255,255,255,0.10); }"
        "scrolledwindow, viewport { background: transparent; }";
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    apply_css();

    Launcher l;
    memset(&l, 0, sizeof(l));
    noco_config_load(&l.cfg);
    l.all_apps = g_app_info_get_all();

    l.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(l.window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(l.window), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_position(GTK_WINDOW(l.window), GTK_WIN_POS_CENTER_ALWAYS);
    gtk_widget_set_app_paintable(l.window, TRUE);

    GdkScreen *screen = gtk_widget_get_screen(l.window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(l.window, visual);

    GdkRectangle geom;
    gdk_monitor_get_geometry(
        gdk_display_get_primary_monitor(gdk_display_get_default()), &geom);

    l.win_w = LAUNCHER_WIDTH;
    l.win_h = LAUNCHER_HEIGHT;
    l.win_x = geom.x + (geom.width - l.win_w) / 2;
    l.win_y = geom.y + (geom.height - l.win_h) / 3;

    gtk_window_move(GTK_WINDOW(l.window), l.win_x, l.win_y);
    gtk_widget_set_size_request(l.window, l.win_w, l.win_h);

    l.overlay = gtk_overlay_new();
    l.drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(l.drawing_area, l.win_w, l.win_h);
    g_signal_connect(l.drawing_area, "draw", G_CALLBACK(on_draw), &l);
    gtk_container_add(GTK_CONTAINER(l.overlay), l.drawing_area);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(vbox, 14);
    gtk_widget_set_margin_end(vbox, 14);
    gtk_widget_set_margin_top(vbox, 14);
    gtk_widget_set_margin_bottom(vbox, 14);

    l.entry = gtk_entry_new();
    gtk_widget_set_name(l.entry, "search-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(l.entry), "Search applications...");
    g_signal_connect(l.entry, "changed", G_CALLBACK(on_entry_changed), &l);
    g_signal_connect(l.entry, "activate", G_CALLBACK(on_entry_activate), &l);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    l.listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(l.listbox), GTK_SELECTION_SINGLE);
    g_signal_connect(l.listbox, "row-activated", G_CALLBACK(on_row_activated), &l);
    gtk_container_add(GTK_CONTAINER(scroll), l.listbox);

    gtk_box_pack_start(GTK_BOX(vbox), l.entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    gtk_overlay_add_overlay(GTK_OVERLAY(l.overlay), vbox);
    gtk_container_add(GTK_CONTAINER(l.window), l.overlay);

    g_signal_connect(l.window, "key-press-event", G_CALLBACK(on_key_press), &l);
    g_signal_connect(l.window, "focus-out-event", G_CALLBACK(on_focus_out), &l);

    gtk_widget_realize(l.window);
    l.dpy = gdk_x11_get_default_xdisplay();
    l.root = gdk_x11_get_default_root_xwindow();
    wallpaper_cache_init(&l.wc, l.dpy, l.root);

    gtk_widget_show_all(l.window);
    gtk_widget_grab_focus(l.entry);
    populate_list(&l, NULL);

    (void)argc;
    (void)argv;

    gtk_main();

    wallpaper_cache_free(&l.wc);
    g_list_free_full(l.all_apps, g_object_unref);
    return 0;
}
