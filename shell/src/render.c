#include <math.h>
#include <glib.h>
#include "render.h"

void noco_rounded_rect_path(cairo_t *cr, double w, double h, double r, int corners) {
    double tl = (corners & NOCO_CORNER_TOP_LEFT) ? r : 0;
    double tr = (corners & NOCO_CORNER_TOP_RIGHT) ? r : 0;
    double br = (corners & NOCO_CORNER_BOTTOM_RIGHT) ? r : 0;
    double bl = (corners & NOCO_CORNER_BOTTOM_LEFT) ? r : 0;

    cairo_new_sub_path(cr);
    cairo_move_to(cr, tl, 0);
    cairo_line_to(cr, w - tr, 0);
    if (tr > 0) cairo_arc(cr, w - tr, tr, tr, -G_PI_2, 0);
    cairo_line_to(cr, w, h - br);
    if (br > 0) cairo_arc(cr, w - br, h - br, br, 0, G_PI_2);
    cairo_line_to(cr, bl, h);
    if (bl > 0) cairo_arc(cr, bl, h - bl, bl, G_PI_2, G_PI);
    cairo_line_to(cr, 0, tl);
    if (tl > 0) cairo_arc(cr, tl, tl, tl, G_PI, 3 * G_PI_2);
    cairo_close_path(cr);
}

void noco_draw_pseudo_transparent(
    cairo_t *cr, WallpaperCache *wc, const NocoConfig *cfg,
    int screen_x, int screen_y, int w, int h,
    double corner_radius, int corners) {

    cairo_surface_t *sample = wallpaper_sample_blurred(
        wc, screen_x, screen_y, w, h, cfg->blur_downscale);

    noco_rounded_rect_path(cr, w, h, corner_radius, corners);
    cairo_clip(cr);

    if (sample) {
        cairo_set_source_surface(cr, sample, 0, 0);
        cairo_paint(cr);
        cairo_surface_destroy(sample);
    } else {
        cairo_set_source_rgb(cr, 0.08, 0.08, 0.1);
        cairo_paint(cr);
    }

    cairo_set_source_rgba(cr, cfg->tint_r, cfg->tint_g, cfg->tint_b, cfg->tint_alpha);
    cairo_paint(cr);

    cairo_set_line_width(cr, 1);
    noco_rounded_rect_path(cr, w - 0.5, h - 0.5, corner_radius, corners);
    cairo_set_source_rgba(cr, 1, 1, 1, cfg->highlight_alpha);
    cairo_stroke(cr);
}
