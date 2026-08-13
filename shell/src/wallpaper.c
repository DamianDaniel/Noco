#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include "wallpaper.h"

static Pixmap get_root_pixmap(Display *dpy, Window root) {
    Atom atoms[2];
    atoms[0] = XInternAtom(dpy, "_XROOTPMAP_ID", False);
    atoms[1] = XInternAtom(dpy, "ESETROOT_PMAP_ID", False);

    for (int i = 0; i < 2; i++) {
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char *data = NULL;

        if (XGetWindowProperty(dpy, root, atoms[i], 0, 1, False, XA_PIXMAP,
                &actual_type, &actual_format, &nitems, &bytes_after,
                &data) == Success && data) {
            Pixmap p = *(Pixmap *)data;
            XFree(data);
            if (p != None) return p;
        }
    }
    return None;
}

void wallpaper_cache_init(WallpaperCache *wc, Display *dpy, Window root) {
    memset(wc, 0, sizeof(*wc));
    wc->dpy = dpy;
    wc->root = root;
    Screen *scr = DefaultScreenOfDisplay(dpy);
    wc->screen_w = WidthOfScreen(scr);
    wc->screen_h = HeightOfScreen(scr);
    wallpaper_cache_refresh(wc);
}

int wallpaper_cache_refresh(WallpaperCache *wc) {
    Pixmap p = get_root_pixmap(wc->dpy, wc->root);
    if (p == None) {
        wc->root_pixmap = None;
        if (wc->full_snapshot) cairo_surface_destroy(wc->full_snapshot);
        wc->full_snapshot = NULL;
        return 0;
    }

    wc->root_pixmap = p;
    if (wc->full_snapshot) cairo_surface_destroy(wc->full_snapshot);

    int screen_num = DefaultScreen(wc->dpy);
    Visual *visual = DefaultVisual(wc->dpy, screen_num);

    cairo_surface_t *src = cairo_xlib_surface_create(
        wc->dpy, p, visual, wc->screen_w, wc->screen_h);

    wc->full_snapshot = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, wc->screen_w, wc->screen_h);
    cairo_t *cr = cairo_create(wc->full_snapshot);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(src);

    return 1;
}

cairo_surface_t *wallpaper_sample_region(WallpaperCache *wc, int x, int y, int w, int h) {
    if (!wc->full_snapshot) return NULL;
    if (w <= 0 || h <= 0) return NULL;

    cairo_surface_t *region = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(region);
    cairo_set_source_surface(cr, wc->full_snapshot, -x, -y);
    cairo_paint(cr);
    cairo_destroy(cr);
    return region;
}

cairo_surface_t *wallpaper_sample_blurred(WallpaperCache *wc, int x, int y, int w, int h, int downscale) {
    if (!wc->full_snapshot) return NULL;
    if (downscale < 1) downscale = 1;
    if (w <= 0 || h <= 0) return NULL;

    int small_w = w / downscale;
    int small_h = h / downscale;
    if (small_w < 1) small_w = 1;
    if (small_h < 1) small_h = 1;

    cairo_surface_t *small = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, small_w, small_h);
    cairo_t *cr = cairo_create(small);
    cairo_scale(cr, (double)small_w / w, (double)small_h / h);
    cairo_set_source_surface(cr, wc->full_snapshot, -x, -y);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);
    cairo_destroy(cr);

    cairo_surface_t *result = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cr = cairo_create(result);
    cairo_scale(cr, (double)w / small_w, (double)h / small_h);
    cairo_set_source_surface(cr, small, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(small);

    return result;
}

void wallpaper_cache_free(WallpaperCache *wc) {
    if (wc->full_snapshot) cairo_surface_destroy(wc->full_snapshot);
    wc->full_snapshot = NULL;
}
