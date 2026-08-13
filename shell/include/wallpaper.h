#ifndef NOCO_WALLPAPER_H
#define NOCO_WALLPAPER_H

#include <X11/Xlib.h>
#include <cairo.h>
#include <cairo-xlib.h>

typedef struct {
    Display *dpy;
    Window root;
    Pixmap root_pixmap;
    cairo_surface_t *full_snapshot;
    int screen_w, screen_h;
} WallpaperCache;

void wallpaper_cache_init(WallpaperCache *wc, Display *dpy, Window root);
int wallpaper_cache_refresh(WallpaperCache *wc);
cairo_surface_t *wallpaper_sample_region(WallpaperCache *wc, int x, int y, int w, int h);
cairo_surface_t *wallpaper_sample_blurred(WallpaperCache *wc, int x, int y, int w, int h, int downscale);
void wallpaper_cache_free(WallpaperCache *wc);

#endif
