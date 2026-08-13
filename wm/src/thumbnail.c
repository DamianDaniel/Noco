#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include "wm.h"

#define NOCO_THUMB_MAX_W 240
#define NOCO_THUMB_MAX_H 160

void thumbnail_path_for(Window win, char *buf, size_t buflen) {
    const char *dir = g_get_user_runtime_dir();
    if (!dir) dir = "/tmp";
    char dirpath[256];
    snprintf(dirpath, sizeof(dirpath), "%s/noco-thumbs", dir);
    g_mkdir_with_parents(dirpath, 0700);
    snprintf(buf, buflen, "%s/0x%lx.png", dirpath, (unsigned long)win);
}

void capture_client_thumbnail(WmState *wm, Client *c) {
    XWindowAttributes attr;
    if (!XGetWindowAttributes(wm->dpy, c->win, &attr)) return;
    if (attr.map_state != IsViewable) return;
    if (attr.width <= 0 || attr.height <= 0) return;

    cairo_surface_t *src = cairo_xlib_surface_create(
        wm->dpy, c->win, attr.visual, attr.width, attr.height);
    cairo_surface_flush(src);

    if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(src);
        return;
    }

    double scale = (double)NOCO_THUMB_MAX_W / attr.width;
    if ((double)NOCO_THUMB_MAX_H / attr.height < scale) scale = (double)NOCO_THUMB_MAX_H / attr.height;
    if (scale > 1.0) scale = 1.0;

    int tw = (int)(attr.width * scale);
    int th = (int)(attr.height * scale);
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;

    cairo_surface_t *thumb = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
    cairo_t *cr = cairo_create(thumb);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(src);

    if (cairo_surface_write_to_png(thumb, c->thumb_path) == CAIRO_STATUS_SUCCESS) {
        c->has_thumb = 1;
    }
    cairo_surface_destroy(thumb);
}
