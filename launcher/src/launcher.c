#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <string.h>
#include "wallpaper.h"
#include "config.h"
#include "render.h"

#define MENU_WIDTH 460
#define MENU_HEIGHT 520
#define SIDEBAR_WIDTH 150

typedef struct {
    const char *bucket;
    GList *apps;
} Category;

typedef struct {
    NocoConfig cfg;
    GtkWidget *window;
    GtkWidget *drawing_area;
    GtkWidget *entry;
    GtkWidget *sidebar_list;
    GtkWidget *apps_list;
    GtkWidget *power_btn;
    WallpaperCache wc;
    Display *dpy;
    Window root;
    int win_x, win_y, win_w, win_h;
    GList *all_apps;
    Category categories[12];
    int category_count;
    int power_armed;
    guint power_reset_source;
} Launcher;

static const char *bucket_for_xdg_category(const char *cat) {
    if (!cat) return NULL;
    if (!strcmp(cat, "Utility") || !strcmp(cat, "Accessibility")) return "Accessories";
    if (!strcmp(cat, "Development")) return "Development";
    if (!strcmp(cat, "Education")) return "Education";
    if (!strcmp(cat, "Game")) return "Games";
    if (!strcmp(cat, "Graphics")) return "Graphics";
    if (!strcmp(cat, "Network") || !strcmp(cat, "WebBrowser")) return "Internet";
    if (!strcmp(cat, "Office")) return "Office";
    if (!strcmp(cat, "AudioVideo") || !strcmp(cat, "Audio") || !strcmp(cat, "Video")) return "Sound & Video";
    if (!strcmp(cat, "System") || !strcmp(cat, "Settings")) return "System Tools";
    return NULL;
}

static Category *find_or_add_category(Launcher *l, const char *bucket) {
    for (int i = 0; i < l->category_count; i++) {
        if (!strcmp(l->categories[i].bucket, bucket)) return &l->categories[i];
    }
    if (l->category_count >= 12) return NULL;
    Category *c = &l->categories[l->category_count++];
    c->bucket = bucket;
    c->apps = NULL;
    return c;
}

static const char *category_order[] = {
    "All Applications", "Accessories", "Development", "Education", "Games",
    "Graphics", "Internet", "Office", "Sound & Video", "System Tools", "Other"
};

static void build_categories(Launcher *l) {
    l->category_count = 0;
    Category *all = find_or_add_category(l, "All Applications");

    for (GList *it = l->all_apps; it; it = it->next) {
        GAppInfo *info = G_APP_INFO(it->data);
        if (!g_app_info_should_show(info)) continue;

        all->apps = g_list_append(all->apps, info);

        const char *matched_bucket = NULL;
        if (G_IS_DESKTOP_APP_INFO(info)) {
            char *cats_str = g_desktop_app_info_get_string(
                G_DESKTOP_APP_INFO(info), "Categories");
            if (cats_str) {
                char **parts = g_strsplit(cats_str, ";", -1);
                for (int i = 0; parts[i] && !matched_bucket; i++) {
                    if (!*parts[i]) continue;
                    matched_bucket = bucket_for_xdg_category(parts[i]);
                }
                g_strfreev(parts);
                g_free(cats_str);
            }
        }
        if (!matched_bucket) matched_bucket = "Other";

        Category *cat = find_or_add_category(l, matched_bucket);
        if (cat) cat->apps = g_list_append(cat->apps, info);
    }
}

static Category *category_by_bucket(Launcher *l, const char *bucket) {
    for (int i = 0; i < l->category_count; i++) {
        if (!strcmp(l->categories[i].bucket, bucket)) return &l->categories[i];
    }
    return NULL;
}

static void launch_app(Launcher *l, GAppInfo *info) {
    (void)l;
    GError *err = NULL;
    g_app_info_launch(info, NULL, NULL, &err);
    if (err) {
        g_printerr("noco-launcher: failed to launch: %s\n", err->message);
        g_error_free(err);
    }
    gtk_main_quit();
}

static void on_app_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    Launcher *l = (Launcher *)data;
    GAppInfo *info = g_object_get_data(G_OBJECT(row), "app-info");
    if (info) launch_app(l, info);
}

static GtkWidget *build_app_row(GAppInfo *info) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(hbox, 8);
    gtk_widget_set_margin_end(hbox, 8);
    gtk_widget_set_margin_top(hbox, 5);
    gtk_widget_set_margin_bottom(hbox, 5);

    GIcon *icon = g_app_info_get_icon(info);
    GtkWidget *image = icon
        ? gtk_image_new_from_gicon(icon, GTK_ICON_SIZE_LARGE_TOOLBAR)
        : gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_image_set_pixel_size(GTK_IMAGE(image), 26);

    GtkWidget *label = gtk_label_new(g_app_info_get_display_name(info));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_name(label, "app-label");
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);

    gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(row), hbox);

    g_object_set_data_full(G_OBJECT(row), "app-info", g_object_ref(info), g_object_unref);
    return row;
}

static void populate_apps_list(Launcher *l, GList *apps) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(l->apps_list));
    for (GList *it = children; it; it = it->next) gtk_widget_destroy(GTK_WIDGET(it->data));
    g_list_free(children);

    int shown = 0;
    for (GList *it = apps; it && shown < 200; it = it->next, shown++) {
        GtkWidget *row = build_app_row(G_APP_INFO(it->data));
        gtk_list_box_insert(GTK_LIST_BOX(l->apps_list), row, -1);
    }
    gtk_widget_show_all(l->apps_list);
}

static void select_category(Launcher *l, const char *bucket) {
    Category *cat = category_by_bucket(l, bucket);
    if (cat) populate_apps_list(l, cat->apps);
}

static void on_sidebar_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    Launcher *l = (Launcher *)data;
    const char *bucket = g_object_get_data(G_OBJECT(row), "bucket");
    if (bucket) select_category(l, bucket);
}

static void apply_search_filter(Launcher *l, const char *text) {
    if (!text || !*text) {
        GtkListBoxRow *sel = gtk_list_box_get_selected_row(GTK_LIST_BOX(l->sidebar_list));
        const char *bucket = sel ? g_object_get_data(G_OBJECT(sel), "bucket") : "All Applications";
        select_category(l, bucket ? bucket : "All Applications");
        return;
    }

    char *needle = g_utf8_strdown(text, -1);
    Category *all = category_by_bucket(l, "All Applications");
    GList *matches = NULL;

    for (GList *it = all->apps; it; it = it->next) {
        GAppInfo *info = G_APP_INFO(it->data);
        char *hay = g_utf8_strdown(g_app_info_get_display_name(info), -1);
        if (strstr(hay, needle)) matches = g_list_append(matches, info);
        g_free(hay);
    }

    populate_apps_list(l, matches);
    g_list_free(matches);
    g_free(needle);
}

static void on_entry_changed(GtkEntry *entry, gpointer data) {
    Launcher *l = (Launcher *)data;
    apply_search_filter(l, gtk_entry_get_text(entry));
}

static void on_entry_activate(GtkEntry *entry, gpointer data) {
    (void)entry;
    Launcher *l = (Launcher *)data;
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(l->apps_list), 0);
    if (row) {
        GAppInfo *info = g_object_get_data(G_OBJECT(row), "app-info");
        if (info) launch_app(l, info);
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
        gtk_widget_grab_focus(l->apps_list);
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

static gboolean reset_power_button(gpointer data) {
    Launcher *l = (Launcher *)data;
    l->power_armed = 0;
    l->power_reset_source = 0;
    gtk_button_set_label(GTK_BUTTON(l->power_btn), "Power");
    return G_SOURCE_REMOVE;
}

static void on_power_clicked(GtkButton *btn, gpointer data) {
    Launcher *l = (Launcher *)data;
    if (!l->power_armed) {
        l->power_armed = 1;
        gtk_button_set_label(btn, "Confirm?");
        l->power_reset_source = g_timeout_add_seconds(3, reset_power_button, l);
        return;
    }
    if (l->power_reset_source) g_source_remove(l->power_reset_source);
    g_spawn_command_line_async(l->cfg.power_cmd, NULL);
    gtk_main_quit();
}

static void on_lock_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    Launcher *l = (Launcher *)data;
    GError *err = NULL;
    if (!g_spawn_command_line_async(l->cfg.lock_cmd, &err)) {
        if (err) g_error_free(err);
    }
}

static void on_settings_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    (void)data;
    g_spawn_command_line_async("noco-settings", NULL);
    gtk_main_quit();
}

static void apply_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css =
        "#search-entry {"
        "  background: rgba(255,255,255,0.09);"
        "  border: none;"
        "  border-radius: 8px;"
        "  color: rgba(255,255,255,0.95);"
        "  padding: 8px 12px;"
        "  font-size: 14px;"
        "}"
        "#app-label { color: rgba(255,255,255,0.92); font-size: 13px; }"
        "#category-label { color: rgba(255,255,255,0.85); font-size: 12px; }"
        "list { background: transparent; }"
        "row { background: transparent; border-radius: 6px; }"
        "row:hover { background: rgba(255,255,255,0.08); }"
        "row:selected { background: rgba(94,158,255,0.35); }"
        "scrolledwindow, viewport { background: transparent; }"
        "#sidebar { border-right: 1px solid rgba(255,255,255,0.08); }"
        "#bottom-bar { border-top: 1px solid rgba(255,255,255,0.08); }"
        "#bottom-bar button {"
        "  background: rgba(255,255,255,0.07);"
        "  color: rgba(255,255,255,0.85);"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 5px 12px;"
        "  font-size: 12px;"
        "}"
        "#bottom-bar button:hover { background: rgba(255,255,255,0.14); }";
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
    build_categories(&l);

    l.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(l.window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(l.window), GDK_WINDOW_TYPE_HINT_UTILITY);
    gtk_widget_set_app_paintable(l.window, TRUE);

    GdkScreen *screen = gtk_widget_get_screen(l.window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(l.window, visual);

    GdkRectangle geom;
    gdk_monitor_get_geometry(
        gdk_display_get_primary_monitor(gdk_display_get_default()), &geom);

    l.win_w = MENU_WIDTH;
    l.win_h = MENU_HEIGHT;
    l.win_x = geom.x + 8;
    l.win_y = (l.cfg.panel_position == NOCO_PANEL_TOP)
        ? geom.y + l.cfg.panel_height + 4
        : geom.y + geom.height - l.cfg.panel_height - l.win_h - 4;

    gtk_window_move(GTK_WINDOW(l.window), l.win_x, l.win_y);
    gtk_widget_set_size_request(l.window, l.win_w, l.win_h);

    GtkWidget *overlay = gtk_overlay_new();
    l.drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(l.drawing_area, l.win_w, l.win_h);
    g_signal_connect(l.drawing_area, "draw", G_CALLBACK(on_draw), &l);
    gtk_container_add(GTK_CONTAINER(overlay), l.drawing_area);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *entry_wrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_start(entry_wrap, 14);
    gtk_widget_set_margin_end(entry_wrap, 14);
    gtk_widget_set_margin_top(entry_wrap, 14);
    gtk_widget_set_margin_bottom(entry_wrap, 10);
    l.entry = gtk_entry_new();
    gtk_widget_set_name(l.entry, "search-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(l.entry), "Search applications...");
    g_signal_connect(l.entry, "changed", G_CALLBACK(on_entry_changed), &l);
    g_signal_connect(l.entry, "activate", G_CALLBACK(on_entry_activate), &l);
    gtk_box_pack_start(GTK_BOX(entry_wrap), l.entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), entry_wrap, FALSE, FALSE, 0);

    GtkWidget *panes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(panes, TRUE);

    GtkWidget *sidebar_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_name(sidebar_scroll, "sidebar");
    gtk_widget_set_size_request(sidebar_scroll, SIDEBAR_WIDTH, -1);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    l.sidebar_list = gtk_list_box_new();
    g_signal_connect(l.sidebar_list, "row-activated", G_CALLBACK(on_sidebar_row_activated), &l);

    for (size_t i = 0; i < sizeof(category_order) / sizeof(category_order[0]); i++) {
        Category *cat = category_by_bucket(&l, category_order[i]);
        if (!cat) continue;
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *label = gtk_label_new(cat->bucket);
        gtk_widget_set_name(label, "category-label");
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_margin_start(label, 10);
        gtk_widget_set_margin_top(label, 6);
        gtk_widget_set_margin_bottom(label, 6);
        gtk_container_add(GTK_CONTAINER(row), label);
        g_object_set_data(G_OBJECT(row), "bucket", (gpointer)cat->bucket);
        gtk_list_box_insert(GTK_LIST_BOX(l.sidebar_list), row, -1);
    }

    gtk_container_add(GTK_CONTAINER(sidebar_scroll), l.sidebar_list);
    gtk_box_pack_start(GTK_BOX(panes), sidebar_scroll, FALSE, FALSE, 0);

    GtkWidget *apps_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(apps_scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(apps_scroll, TRUE);

    l.apps_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(l.apps_list), GTK_SELECTION_SINGLE);
    g_signal_connect(l.apps_list, "row-activated", G_CALLBACK(on_app_row_activated), &l);
    gtk_container_add(GTK_CONTAINER(apps_scroll), l.apps_list);
    gtk_box_pack_start(GTK_BOX(panes), apps_scroll, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), panes, TRUE, TRUE, 0);

    GtkWidget *bottom_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(bottom_bar, "bottom-bar");
    gtk_widget_set_margin_start(bottom_bar, 14);
    gtk_widget_set_margin_end(bottom_bar, 14);
    gtk_widget_set_margin_top(bottom_bar, 10);
    gtk_widget_set_margin_bottom(bottom_bar, 12);

    GtkWidget *settings_btn = gtk_button_new_with_label("Settings");
    g_signal_connect(settings_btn, "clicked", G_CALLBACK(on_settings_clicked), &l);
    gtk_box_pack_start(GTK_BOX(bottom_bar), settings_btn, FALSE, FALSE, 0);

    GtkWidget *lock_btn = gtk_button_new_with_label("Lock");
    g_signal_connect(lock_btn, "clicked", G_CALLBACK(on_lock_clicked), &l);
    gtk_box_pack_start(GTK_BOX(bottom_bar), lock_btn, FALSE, FALSE, 0);

    l.power_btn = gtk_button_new_with_label("Power");
    g_signal_connect(l.power_btn, "clicked", G_CALLBACK(on_power_clicked), &l);
    gtk_box_pack_end(GTK_BOX(bottom_bar), l.power_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), bottom_bar, FALSE, FALSE, 0);

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), vbox);
    gtk_container_add(GTK_CONTAINER(l.window), overlay);

    g_signal_connect(l.window, "key-press-event", G_CALLBACK(on_key_press), &l);
    g_signal_connect(l.window, "focus-out-event", G_CALLBACK(on_focus_out), &l);

    gtk_widget_realize(l.window);
    l.dpy = gdk_x11_get_default_xdisplay();
    l.root = gdk_x11_get_default_root_xwindow();
    wallpaper_cache_init(&l.wc, l.dpy, l.root);

    gtk_widget_show_all(l.window);
    gtk_widget_grab_focus(l.entry);
    select_category(&l, "All Applications");

    (void)argc;
    (void)argv;

    gtk_main();

    wallpaper_cache_free(&l.wc);
    g_list_free_full(l.all_apps, g_object_unref);
    return 0;
}
