#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include "wm.h"
#include "render.h"
#include "wallpaper.h"

#define CARD_W 180
#define CARD_H 140
#define CARD_GAP 12
#define CARD_PADDING 24

static void compute_mod_keycodes(WmState *wm) {
    wm->switcher_mod_keycode_count = 0;
    XModifierKeymap *map = XGetModifierMapping(wm->dpy);
    if (!map) return;

    int mod_index = -1;
    unsigned int masks[8] = {
        ShiftMask, LockMask, ControlMask, Mod1Mask,
        Mod2Mask, Mod3Mask, Mod4Mask, Mod5Mask
    };
    for (int i = 0; i < 8; i++) {
        if (masks[i] == wm->cfg.mod_mask) mod_index = i;
    }

    if (mod_index >= 0) {
        for (int k = 0; k < map->max_keypermod; k++) {
            KeyCode kc = map->modifiermap[mod_index * map->max_keypermod + k];
            if (kc != 0 && wm->switcher_mod_keycode_count < 8) {
                wm->switcher_mod_keycodes[wm->switcher_mod_keycode_count++] = kc;
            }
        }
    }
    XFreeModifiermap(map);
}

static int is_switcher_mod_keycode(WmState *wm, KeyCode kc) {
    for (int i = 0; i < wm->switcher_mod_keycode_count; i++) {
        if (wm->switcher_mod_keycodes[i] == kc) return 1;
    }
    return 0;
}

static void get_window_title(WmState *wm, Client *c, char *buf, size_t buflen) {
    char *name = NULL;
    if (XFetchName(wm->dpy, c->win, &name) && name) {
        snprintf(buf, buflen, "%s", name);
        XFree(name);
    } else {
        snprintf(buf, buflen, "Untitled");
    }
}

static void switcher_draw(WmState *wm) {
    XWindowAttributes wa;
    XGetWindowAttributes(wm->dpy, wm->switcher_win, &wa);

    cairo_surface_t *surface = cairo_xlib_surface_create(
        wm->dpy, wm->switcher_win, DefaultVisual(wm->dpy, wm->screen), wa.width, wa.height);
    cairo_t *cr = cairo_create(surface);

    cairo_save(cr);
    noco_draw_pseudo_transparent(cr, &wm->wc, &wm->cfg,
        wa.x, wa.y, wa.width, wa.height, wm->cfg.corner_radius, NOCO_CORNER_ALL);
    cairo_restore(cr);

    for (int i = 0; i < wm->switcher_count; i++) {
        Client *c = wm->switcher_list[i];
        double cx = CARD_PADDING + i * (CARD_W + CARD_GAP);
        double cy = CARD_PADDING;

        cairo_save(cr);
        cairo_translate(cr, cx, cy);

        int selected = (i == wm->switcher_selected);
        noco_rounded_rect_path(cr, CARD_W, CARD_H, 8, NOCO_CORNER_ALL);
        cairo_set_source_rgba(cr, 1, 1, 1, selected ? 0.14 : 0.05);
        cairo_fill(cr);

        double thumb_area_h = CARD_H - 30;
        if (c->has_thumb) {
            cairo_surface_t *thumb = cairo_image_surface_create_from_png(c->thumb_path);
            if (cairo_surface_status(thumb) == CAIRO_STATUS_SUCCESS) {
                int tw = cairo_image_surface_get_width(thumb);
                int th = cairo_image_surface_get_height(thumb);
                double scale = (CARD_W - 16) / (double)tw;
                if ((thumb_area_h - 8) / (double)th < scale) scale = (thumb_area_h - 8) / (double)th;
                double ox = (CARD_W - tw * scale) / 2.0;
                double oy = 8 + (thumb_area_h - 8 - th * scale) / 2.0;
                cairo_save(cr);
                cairo_translate(cr, ox, oy);
                cairo_scale(cr, scale, scale);
                cairo_set_source_surface(cr, thumb, 0, 0);
                cairo_paint(cr);
                cairo_restore(cr);
            }
            cairo_surface_destroy(thumb);
        } else {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
            cairo_rectangle(cr, 8, 8, CARD_W - 16, thumb_area_h - 8);
            cairo_fill(cr);
        }

        char title[128];
        get_window_title(wm, c, title, sizeof(title));
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
        cairo_move_to(cr, 10, CARD_H - 10);
        cairo_show_text(cr, title);

        cairo_restore(cr);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

void switcher_begin(WmState *wm, int forward) {
    if (wm->switcher_active) return;

    int count = 0;
    for (Client *c = wm->clients; c; c = c->next) {
        if (c->workspace == wm->active_workspace && !c->minimized) count++;
    }
    if (count == 0) return;

    wm->switcher_list = malloc(sizeof(Client *) * count);
    int i = 0, focused_index = 0;
    for (Client *c = wm->clients; c; c = c->next) {
        if (c->workspace == wm->active_workspace && !c->minimized) {
            wm->switcher_list[i] = c;
            if (c == wm->focused) focused_index = i;
            i++;
        }
    }
    wm->switcher_count = count;
    wm->switcher_selected = (focused_index + (forward ? 1 : count - 1)) % count;

    wallpaper_cache_refresh(&wm->wc);

    int width = CARD_PADDING * 2 + count * CARD_W + (count - 1) * CARD_GAP;
    if (width > wm->screen_w - 40) width = wm->screen_w - 40;
    int height = CARD_PADDING * 2 + CARD_H;
    int x = (wm->screen_w - width) / 2;
    int y = (wm->screen_h - height) / 2;

    XSetWindowAttributes attr;
    attr.override_redirect = True;
    attr.background_pixel = 0;
    attr.event_mask = ExposureMask;

    wm->switcher_win = XCreateWindow(wm->dpy, wm->root, x, y, width, height, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWEventMask, &attr);

    XMapRaised(wm->dpy, wm->switcher_win);
    XSync(wm->dpy, False);

    compute_mod_keycodes(wm);
    XGrabKeyboard(wm->dpy, wm->root, False, GrabModeAsync, GrabModeAsync, CurrentTime);

    wm->switcher_active = 1;
    switcher_draw(wm);
}

void switcher_cycle(WmState *wm, int forward) {
    if (!wm->switcher_active || wm->switcher_count == 0) return;
    wm->switcher_selected = (wm->switcher_selected + (forward ? 1 : wm->switcher_count - 1))
        % wm->switcher_count;
    switcher_draw(wm);
}

void switcher_end(WmState *wm, int commit) {
    if (!wm->switcher_active) return;

    if (commit && wm->switcher_count > 0) {
        Client *chosen = wm->switcher_list[wm->switcher_selected];
        client_focus(wm, chosen);
    }

    XUngrabKeyboard(wm->dpy, CurrentTime);
    XDestroyWindow(wm->dpy, wm->switcher_win);
    free(wm->switcher_list);
    wm->switcher_list = NULL;
    wm->switcher_count = 0;
    wm->switcher_active = 0;
}

void switcher_handle_keypress(WmState *wm, XKeyEvent *ev) {
    KeySym key = XkbKeycodeToKeysym(wm->dpy, ev->keycode, 0, 0);
    if (key == XK_Tab) {
        switcher_cycle(wm, !(ev->state & ShiftMask));
    } else if (key == XK_Escape) {
        switcher_end(wm, 0);
    } else if (key == XK_grave || key == XK_asciitilde) {
        switcher_cycle(wm, !(ev->state & ShiftMask));
    }
}

void switcher_handle_keyrelease(WmState *wm, XKeyEvent *ev) {
    if (is_switcher_mod_keycode(wm, ev->keycode)) {
        switcher_end(wm, 1);
    }
}

void switcher_handle_expose(WmState *wm, XExposeEvent *ev) {
    if (wm->switcher_active && ev->window == wm->switcher_win) {
        switcher_draw(wm);
    }
}
