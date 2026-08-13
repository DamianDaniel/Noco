#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <X11/Xatom.h>
#include "wm.h"
#include "render.h"

#define BTN_SIZE 22
#define BTN_PAD 4

void get_client_title(WmState *wm, Client *c, char *buf, size_t buflen) {
    char *name = NULL;
    if (XFetchName(wm->dpy, c->win, &name) && name) {
        snprintf(buf, buflen, "%s", name);
        XFree(name);
    } else {
        snprintf(buf, buflen, "Untitled");
    }
}

static void button_rects(WmState *wm, Client *c, int rects[3][4]) {
    int tb = wm->cfg.titlebar_height;
    int y = (tb - BTN_SIZE) / 2;
    int right = c->w - BTN_PAD - BTN_SIZE;

    rects[0][0] = right; rects[0][1] = y; rects[0][2] = BTN_SIZE; rects[0][3] = BTN_SIZE;
    right -= BTN_SIZE + BTN_PAD;
    rects[1][0] = right; rects[1][1] = y; rects[1][2] = BTN_SIZE; rects[1][3] = BTN_SIZE;
    right -= BTN_SIZE + BTN_PAD;
    rects[2][0] = right; rects[2][1] = y; rects[2][2] = BTN_SIZE; rects[2][3] = BTN_SIZE;
}

int hit_test_frame(WmState *wm, Client *c, int lx, int ly, int *out_edges) {
    int tb = wm->cfg.titlebar_height;
    int m = wm->cfg.frame_margin;
    *out_edges = EDGE_NONE;

    if (ly < tb) {
        int rects[3][4];
        button_rects(wm, c, rects);
        if (lx >= rects[0][0] && lx < rects[0][0] + rects[0][2]) return HIT_CLOSE;
        if (lx >= rects[1][0] && lx < rects[1][0] + rects[1][2]) return HIT_MAXIMIZE;
        if (lx >= rects[2][0] && lx < rects[2][0] + rects[2][2]) return HIT_MINIMIZE;
        return HIT_TITLEBAR;
    }

    int edges = EDGE_NONE;
    if (lx < m) edges |= EDGE_W;
    if (lx >= c->w - m) edges |= EDGE_E;
    if (ly >= c->h - m) edges |= EDGE_S;

    if (edges != EDGE_NONE) {
        *out_edges = edges;
        return HIT_RESIZE;
    }

    return HIT_NONE;
}

static void draw_button_glyph(cairo_t *cr, int type, double cx, double cy, double s) {
    cairo_set_line_width(cr, 1.4);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.85);

    if (type == HIT_CLOSE) {
        cairo_move_to(cr, cx - s, cy - s);
        cairo_line_to(cr, cx + s, cy + s);
        cairo_move_to(cr, cx + s, cy - s);
        cairo_line_to(cr, cx - s, cy + s);
        cairo_stroke(cr);
    } else if (type == HIT_MAXIMIZE) {
        cairo_rectangle(cr, cx - s, cy - s, s * 2, s * 2);
        cairo_stroke(cr);
    } else if (type == HIT_MINIMIZE) {
        cairo_move_to(cr, cx - s, cy + s);
        cairo_line_to(cr, cx + s, cy + s);
        cairo_stroke(cr);
    }
}

void redraw_decorations(WmState *wm, Client *c) {
    XSetWindowBackground(wm->dpy, c->frame,
        (wm->focused == c) ? wm->cfg.focus_color : wm->cfg.border_color);
    XClearWindow(wm->dpy, c->frame);

    cairo_surface_t *surface = cairo_xlib_surface_create(
        wm->dpy, c->frame, DefaultVisual(wm->dpy, wm->screen), c->w, c->h);
    cairo_t *cr = cairo_create(surface);

    noco_draw_pseudo_transparent(cr, &wm->wc, &wm->cfg,
        c->x, c->y, c->w, wm->cfg.titlebar_height,
        wm->cfg.corner_radius, NOCO_CORNER_TOP_LEFT | NOCO_CORNER_TOP_RIGHT);

    if (wm->focused == c) {
        cairo_set_source_rgba(cr,
            ((wm->cfg.focus_color >> 16) & 0xFF) / 255.0,
            ((wm->cfg.focus_color >> 8) & 0xFF) / 255.0,
            (wm->cfg.focus_color & 0xFF) / 255.0, 0.35);
        cairo_rectangle(cr, 0, 0, c->w, wm->cfg.titlebar_height);
        cairo_fill(cr);
    }

    char title[128];
    get_client_title(wm, c, title, sizeof(title));
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr,
        ((wm->cfg.titlebar_text_color >> 16) & 0xFF) / 255.0,
        ((wm->cfg.titlebar_text_color >> 8) & 0xFF) / 255.0,
        (wm->cfg.titlebar_text_color & 0xFF) / 255.0,
        wm->focused == c ? 0.95 : 0.55);

    cairo_text_extents_t ext;
    cairo_text_extents(cr, title, &ext);
    double text_y = wm->cfg.titlebar_height / 2.0 - ext.height / 2.0 - ext.y_bearing;
    cairo_move_to(cr, 10, text_y);
    cairo_show_text(cr, title);

    int rects[3][4];
    button_rects(wm, c, rects);
    int types[3] = {HIT_CLOSE, HIT_MAXIMIZE, HIT_MINIMIZE};
    for (int i = 0; i < 3; i++) {
        double bx = rects[i][0], by = rects[i][1], bw = rects[i][2], bh = rects[i][3];
        if (types[i] == HIT_CLOSE) {
            cairo_set_source_rgba(cr, 0.85, 0.25, 0.25, 0.9);
        } else {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
        }
        noco_rounded_rect_path(cr, bw, bh, 5, NOCO_CORNER_ALL);
        cairo_save(cr);
        cairo_translate(cr, bx, by);
        noco_rounded_rect_path(cr, bw, bh, 5, NOCO_CORNER_ALL);
        cairo_fill(cr);
        draw_button_glyph(cr, types[i], bw / 2.0, bh / 2.0, 3.5);
        cairo_restore(cr);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}
