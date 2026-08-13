#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/X.h>
#include <string.h>
#include <stdio.h>
#include "wallpaper.h"
#include "config.h"
#include "render.h"

typedef struct {
    NocoConfig cfg;
    GtkWidget *window;
    GtkWidget *drawing_area;
    WallpaperCache wc;
    Display *dpy;
    Window root;
    int win_x, win_y, win_w, win_h;

    GtkWidget *mod_key_combo;
    GtkWidget *gap_spin;
    GtkWidget *border_width_spin;
    GtkWidget *workspace_count_spin;

    GtkWidget *border_color_btn;
    GtkWidget *focus_color_btn;
    GtkWidget *panel_height_spin;
    GtkWidget *corner_radius_spin;
    GtkWidget *tint_color_btn;
    GtkWidget *border_alpha_spin;
    GtkWidget *highlight_alpha_spin;
    GtkWidget *blur_downscale_spin;
    GtkWidget *panel_position_combo;

    GtkWidget *notif_width_spin;
    GtkWidget *notif_timeout_spin;
    GtkWidget *notif_margin_spin;
    GtkWidget *notif_spacing_spin;
    GtkWidget *notif_position_combo;

    GtkWidget *launcher_cmd_entry;
    GtkWidget *terminal_cmd_entry;

    GtkWidget *status_label;
} SettingsApp;

static GdkRGBA color_from_ulong(unsigned long v) {
    GdkRGBA c;
    c.red = ((v >> 16) & 0xFF) / 255.0;
    c.green = ((v >> 8) & 0xFF) / 255.0;
    c.blue = (v & 0xFF) / 255.0;
    c.alpha = 1.0;
    return c;
}

static unsigned long color_to_ulong(GdkRGBA *c) {
    return ((unsigned long)(c->red * 255) << 16) |
           ((unsigned long)(c->green * 255) << 8) |
           (unsigned long)(c->blue * 255);
}

static char *config_file_path(void) {
    const char *xdg = g_get_user_config_dir();
    char *dir = g_build_filename(xdg, "noco", NULL);
    g_mkdir_with_parents(dir, 0755);
    char *path = g_build_filename(dir, "noco.conf", NULL);
    g_free(dir);
    return path;
}

static void populate_from_config(SettingsApp *a) {
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(a->mod_key_combo),
        a->cfg.mod_mask == Mod1Mask ? "alt" : "super");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->gap_spin), a->cfg.gap);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->border_width_spin), a->cfg.border_width);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->workspace_count_spin), a->cfg.workspace_count);

    GdkRGBA bc = color_from_ulong(a->cfg.border_color);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(a->border_color_btn), &bc);
    GdkRGBA fc = color_from_ulong(a->cfg.focus_color);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(a->focus_color_btn), &fc);

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->panel_height_spin), a->cfg.panel_height);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->corner_radius_spin), a->cfg.corner_radius);

    GdkRGBA tint;
    tint.red = a->cfg.tint_r;
    tint.green = a->cfg.tint_g;
    tint.blue = a->cfg.tint_b;
    tint.alpha = a->cfg.tint_alpha;
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(a->tint_color_btn), &tint);

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->border_alpha_spin), a->cfg.border_alpha);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->highlight_alpha_spin), a->cfg.highlight_alpha);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->blur_downscale_spin), a->cfg.blur_downscale);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(a->panel_position_combo),
        a->cfg.panel_position == NOCO_PANEL_BOTTOM ? "bottom" : "top");

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->notif_width_spin), a->cfg.notif_width);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->notif_timeout_spin), a->cfg.notif_default_timeout_ms);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->notif_margin_spin), a->cfg.notif_margin);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->notif_spacing_spin), a->cfg.notif_spacing);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(a->notif_position_combo), a->cfg.notif_position);

    gtk_entry_set_text(GTK_ENTRY(a->launcher_cmd_entry), a->cfg.launcher_cmd);
    gtk_entry_set_text(GTK_ENTRY(a->terminal_cmd_entry), a->cfg.terminal_cmd);
}

static void on_save_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    SettingsApp *a = (SettingsApp *)data;

    GKeyFile *kf = g_key_file_new();

    const char *modkey = gtk_combo_box_get_active_id(GTK_COMBO_BOX(a->mod_key_combo));
    g_key_file_set_string(kf, "general", "mod_key", modkey ? modkey : "super");
    g_key_file_set_integer(kf, "general", "gap",
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->gap_spin)));
    g_key_file_set_integer(kf, "general", "border_width",
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->border_width_spin)));
    g_key_file_set_integer(kf, "general", "workspace_count",
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->workspace_count_spin)));

    GdkRGBA bc, fc, tint;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(a->border_color_btn), &bc);
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(a->focus_color_btn), &fc);
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(a->tint_color_btn), &tint);

    char hex[8];
    snprintf(hex, sizeof(hex), "#%06lx", color_to_ulong(&bc));
    g_key_file_set_string(kf, "theme", "border_color", hex);
    snprintf(hex, sizeof(hex), "#%06lx", color_to_ulong(&fc));
    g_key_file_set_string(kf, "theme", "focus_color", hex);

    g_key_file_set_integer(kf, "theme", "panel_height",
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->panel_height_spin)));
    g_key_file_set_integer(kf, "theme", "corner_radius",
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->corner_radius_spin)));
    g_key_file_set_double(kf, "theme", "tint_r", tint.red);
    g_key_file_set_double(kf, "theme", "tint_g", tint.green);
    g_key_file_set_double(kf, "theme", "tint_b", tint.blue);
    g_key_file_set_double(kf, "theme", "tint_alpha", tint.alpha);
    g_key_file_set_double(kf, "theme", "border_alpha",
        gtk_spin_button_get_value(GTK_SPIN_BUTTON(a->border_alpha_spin)));
    g_key_file_set_double(kf, "theme", "highlight_alpha",
        gtk_spin_button_get_value(GTK_SPIN_BUTTON(a->highlight_alpha_spin)));
    g_key_file_set_integer(kf, "theme", "blur_downscale",
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->blur_downscale_spin)));

    const char *panel_pos = gtk_combo_box_get_active_id(GTK_COMBO_BOX(a->panel_position_combo));
    g_key_file_set_string(kf, "panel", "position", panel_pos ? panel_pos : "top");

    g_key_file_set_integer(kf, "notifications", "width",
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->notif_width_spin)));
    g_key_file_set_integer(kf, "notifications", "default_timeout_ms",
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->notif_timeout_spin)));
    g_key_file_set_integer(kf, "notifications", "margin",
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->notif_margin_spin)));
    g_key_file_set_integer(kf, "notifications", "spacing",
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->notif_spacing_spin)));
    const char *notif_pos = gtk_combo_box_get_active_id(GTK_COMBO_BOX(a->notif_position_combo));
    g_key_file_set_string(kf, "notifications", "position", notif_pos ? notif_pos : "top-right");

    g_key_file_set_string(kf, "bindings", "launcher_cmd",
        gtk_entry_get_text(GTK_ENTRY(a->launcher_cmd_entry)));
    g_key_file_set_string(kf, "bindings", "terminal_cmd",
        gtk_entry_get_text(GTK_ENTRY(a->terminal_cmd_entry)));

    char *path = config_file_path();
    GError *err = NULL;
    if (g_key_file_save_to_file(kf, path, &err)) {
        gtk_label_set_text(GTK_LABEL(a->status_label),
            "Saved. Restart noco-wm / noco-panel / noco-notifyd to apply.");
    } else {
        char msg[512];
        snprintf(msg, sizeof(msg), "Failed to save: %s", err->message);
        gtk_label_set_text(GTK_LABEL(a->status_label), msg);
        g_error_free(err);
    }

    g_free(path);
    g_key_file_free(kf);
}

static void on_reset_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    SettingsApp *a = (SettingsApp *)data;
    noco_config_load(&a->cfg);
    populate_from_config(a);
    gtk_label_set_text(GTK_LABEL(a->status_label), "Reloaded from disk.");
}

static GtkWidget *labeled_row(GtkWidget *grid, int row, const char *label, GtkWidget *widget) {
    GtkWidget *l = gtk_label_new(label);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_widget_set_name(l, "field-label");
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    gtk_widget_set_hexpand(widget, TRUE);
    gtk_grid_attach(GTK_GRID(grid), widget, 1, row, 1, 1);
    return widget;
}

static GtkWidget *make_spin(double min, double max, double step, int digits) {
    GtkWidget *s = gtk_spin_button_new_with_range(min, max, step);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(s), digits);
    return s;
}

static GtkWidget *make_section_label(const char *text) {
    GtkWidget *l = gtk_label_new(text);
    gtk_widget_set_name(l, "section-label");
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_widget_set_margin_top(l, 14);
    gtk_widget_set_margin_bottom(l, 4);
    return l;
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    SettingsApp *a = (SettingsApp *)data;
    (void)widget;
    noco_draw_pseudo_transparent(cr, &a->wc, &a->cfg,
        a->win_x, a->win_y, a->win_w, a->win_h, a->cfg.corner_radius, NOCO_CORNER_ALL);
    return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget; (void)data;
    if (event->keyval == GDK_KEY_Escape) {
        gtk_main_quit();
        return TRUE;
    }
    return FALSE;
}

static void apply_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css =
        "#section-label { color: rgba(255,255,255,0.95); font-weight: 700; font-size: 13px; }"
        "#field-label { color: rgba(255,255,255,0.75); font-size: 12px; }"
        "#status-label { color: rgba(255,255,255,0.6); font-size: 11px; }"
        "entry, spinbutton { background: rgba(255,255,255,0.08); color: rgba(255,255,255,0.95); "
        "  border: none; border-radius: 6px; }"
        "combobox button { background: rgba(255,255,255,0.08); border-radius: 6px; }"
        "#save-btn { background: rgba(94,158,255,0.85); color: white; border-radius: 8px; "
        "  font-weight: 600; padding: 6px 16px; border: none; }"
        "#reset-btn { background: rgba(255,255,255,0.10); color: rgba(255,255,255,0.9); "
        "  border-radius: 8px; padding: 6px 16px; border: none; }";
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    apply_css();

    SettingsApp a;
    memset(&a, 0, sizeof(a));
    noco_config_load(&a.cfg);

    a.win_w = 480;
    a.win_h = 620;

    a.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(a.window), "Noco Settings");
    gtk_window_set_decorated(GTK_WINDOW(a.window), FALSE);
    gtk_window_set_position(GTK_WINDOW(a.window), GTK_WIN_POS_CENTER);
    gtk_widget_set_app_paintable(a.window, TRUE);
    gtk_widget_set_size_request(a.window, a.win_w, a.win_h);

    GdkScreen *screen = gtk_widget_get_screen(a.window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(a.window, visual);

    GtkWidget *overlay = gtk_overlay_new();
    a.drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(a.drawing_area, a.win_w, a.win_h);
    g_signal_connect(a.drawing_area, "draw", G_CALLBACK(on_draw), &a);
    gtk_container_add(GTK_CONTAINER(overlay), a.drawing_area);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, a.win_w, a.win_h - 50);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(outer, 18);
    gtk_widget_set_margin_end(outer, 18);
    gtk_widget_set_margin_top(outer, 16);
    gtk_widget_set_margin_bottom(outer, 8);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    int row = 0;

    gtk_grid_attach(GTK_GRID(grid), make_section_label("General"), 0, row++, 2, 1);

    a.mod_key_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a.mod_key_combo), "super", "Super");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a.mod_key_combo), "alt", "Alt");
    labeled_row(grid, row++, "Mod key", a.mod_key_combo);

    a.gap_spin = make_spin(0, 64, 1, 0);
    labeled_row(grid, row++, "Window gap", a.gap_spin);

    a.border_width_spin = make_spin(0, 12, 1, 0);
    labeled_row(grid, row++, "Border width", a.border_width_spin);

    a.workspace_count_spin = make_spin(1, 9, 1, 0);
    labeled_row(grid, row++, "Workspace count", a.workspace_count_spin);

    gtk_grid_attach(GTK_GRID(grid), make_section_label("Theme"), 0, row++, 2, 1);

    a.border_color_btn = gtk_color_button_new();
    labeled_row(grid, row++, "Border color", a.border_color_btn);

    a.focus_color_btn = gtk_color_button_new();
    labeled_row(grid, row++, "Focus color", a.focus_color_btn);

    a.panel_height_spin = make_spin(20, 80, 1, 0);
    labeled_row(grid, row++, "Panel height", a.panel_height_spin);

    a.corner_radius_spin = make_spin(0, 32, 1, 0);
    labeled_row(grid, row++, "Corner radius", a.corner_radius_spin);

    a.tint_color_btn = gtk_color_button_new();
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(a.tint_color_btn), TRUE);
    labeled_row(grid, row++, "Tint color + alpha", a.tint_color_btn);

    a.border_alpha_spin = make_spin(0, 1, 0.01, 2);
    labeled_row(grid, row++, "Border alpha", a.border_alpha_spin);

    a.highlight_alpha_spin = make_spin(0, 1, 0.01, 2);
    labeled_row(grid, row++, "Highlight alpha", a.highlight_alpha_spin);

    a.blur_downscale_spin = make_spin(1, 20, 1, 0);
    labeled_row(grid, row++, "Blur downscale", a.blur_downscale_spin);

    a.panel_position_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a.panel_position_combo), "top", "Top");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a.panel_position_combo), "bottom", "Bottom");
    labeled_row(grid, row++, "Panel position", a.panel_position_combo);

    gtk_grid_attach(GTK_GRID(grid), make_section_label("Notifications"), 0, row++, 2, 1);

    a.notif_width_spin = make_spin(200, 600, 10, 0);
    labeled_row(grid, row++, "Notification width", a.notif_width_spin);

    a.notif_timeout_spin = make_spin(1000, 30000, 500, 0);
    labeled_row(grid, row++, "Default timeout (ms)", a.notif_timeout_spin);

    a.notif_margin_spin = make_spin(0, 64, 1, 0);
    labeled_row(grid, row++, "Screen margin", a.notif_margin_spin);

    a.notif_spacing_spin = make_spin(0, 32, 1, 0);
    labeled_row(grid, row++, "Stack spacing", a.notif_spacing_spin);

    a.notif_position_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a.notif_position_combo), "top-right", "Top right");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a.notif_position_combo), "top-left", "Top left");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a.notif_position_combo), "bottom-right", "Bottom right");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a.notif_position_combo), "bottom-left", "Bottom left");
    labeled_row(grid, row++, "Notification corner", a.notif_position_combo);

    gtk_grid_attach(GTK_GRID(grid), make_section_label("Bindings"), 0, row++, 2, 1);

    a.launcher_cmd_entry = gtk_entry_new();
    labeled_row(grid, row++, "Launcher command", a.launcher_cmd_entry);

    a.terminal_cmd_entry = gtk_entry_new();
    labeled_row(grid, row++, "Terminal command", a.terminal_cmd_entry);

    gtk_box_pack_start(GTK_BOX(outer), grid, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(scroll), outer);

    GtkWidget *root_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(root_vbox), scroll, TRUE, TRUE, 0);

    GtkWidget *btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(btn_row, 18);
    gtk_widget_set_margin_end(btn_row, 18);
    gtk_widget_set_margin_top(btn_row, 6);
    gtk_widget_set_margin_bottom(btn_row, 10);

    a.status_label = gtk_label_new("");
    gtk_widget_set_name(a.status_label, "status-label");
    gtk_widget_set_halign(a.status_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(a.status_label, TRUE);

    GtkWidget *reset_btn = gtk_button_new_with_label("Reload");
    gtk_widget_set_name(reset_btn, "reset-btn");
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_reset_clicked), &a);

    GtkWidget *save_btn = gtk_button_new_with_label("Save");
    gtk_widget_set_name(save_btn, "save-btn");
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_clicked), &a);

    gtk_box_pack_start(GTK_BOX(btn_row), a.status_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_row), reset_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_row), save_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root_vbox), btn_row, FALSE, FALSE, 0);

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), root_vbox);
    gtk_container_add(GTK_CONTAINER(a.window), overlay);

    g_signal_connect(a.window, "key-press-event", G_CALLBACK(on_key_press), &a);
    g_signal_connect(a.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_realize(a.window);
    a.dpy = gdk_x11_get_default_xdisplay();
    a.root = gdk_x11_get_default_root_xwindow();
    wallpaper_cache_init(&a.wc, a.dpy, a.root);

    gtk_widget_show_all(a.window);

    gint wx, wy;
    gtk_window_get_position(GTK_WINDOW(a.window), &wx, &wy);
    a.win_x = wx;
    a.win_y = wy;

    populate_from_config(&a);

    (void)argc;
    (void)argv;

    gtk_main();

    wallpaper_cache_free(&a.wc);
    return 0;
}
