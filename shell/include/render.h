#ifndef NOCO_RENDER_H
#define NOCO_RENDER_H

#include <cairo.h>
#include "wallpaper.h"
#include "config.h"

typedef enum {
    NOCO_CORNER_TOP_LEFT = 1 << 0,
    NOCO_CORNER_TOP_RIGHT = 1 << 1,
    NOCO_CORNER_BOTTOM_RIGHT = 1 << 2,
    NOCO_CORNER_BOTTOM_LEFT = 1 << 3,
    NOCO_CORNER_ALL = 0xF,
    NOCO_CORNER_NONE = 0
} NocoCornerMask;

void noco_rounded_rect_path(cairo_t *cr, double w, double h, double r, int corners);

void noco_draw_pseudo_transparent(
    cairo_t *cr, WallpaperCache *wc, const NocoConfig *cfg,
    int screen_x, int screen_y, int w, int h,
    double corner_radius, int corners);

#endif
