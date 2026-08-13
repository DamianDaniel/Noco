#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include "wm.h"

Client *client_find(WmState *wm, Window w) {
    for (Client *c = wm->clients; c; c = c->next)
        if (c->win == w) return c;
    return NULL;
}

Client *client_find_by_frame(WmState *wm, Window frame) {
    for (Client *c = wm->clients; c; c = c->next)
        if (c->frame == frame) return c;
    return NULL;
}

Client *client_create(WmState *wm, Window w) {
    XWindowAttributes attr;
    if (!XGetWindowAttributes(wm->dpy, w, &attr)) return NULL;

    Client *c = calloc(1, sizeof(Client));
    c->win = w;
    c->x = attr.x;
    c->y = attr.y;
    c->w = attr.width;
    c->h = attr.height;
    c->workspace = wm->active_workspace;
    c->snap = SNAP_NONE;

    XSetWindowAttributes fattr;
    fattr.background_pixel = BlackPixel(wm->dpy, wm->screen);
    fattr.border_pixel = wm->cfg.border_color;
    fattr.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                        ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                        EnterWindowMask;

    c->frame = XCreateWindow(wm->dpy, wm->root,
        c->x, c->y, c->w, c->h, wm->cfg.border_width,
        CopyFromParent, InputOutput, CopyFromParent,
        CWBackPixel | CWBorderPixel | CWEventMask, &fattr);

    XSetWindowBorderWidth(wm->dpy, c->frame, wm->cfg.border_width);

    XAddToSaveSet(wm->dpy, w);
    XReparentWindow(wm->dpy, w, c->frame, 0, 0);
    XResizeWindow(wm->dpy, w, c->w, c->h);
    XMapWindow(wm->dpy, w);
    XMapWindow(wm->dpy, c->frame);

    grab_buttons(wm, w);

    c->next = wm->clients;
    wm->clients = c;

    thumbnail_path_for(c->win, c->thumb_path, sizeof(c->thumb_path));
    c->has_thumb = 0;

    return c;
}

static void client_unlink(WmState *wm, Client *c) {
    if (wm->clients == c) {
        wm->clients = c->next;
    } else {
        for (Client *p = wm->clients; p; p = p->next) {
            if (p->next == c) {
                p->next = c->next;
                break;
            }
        }
    }
    if (wm->focused == c) wm->focused = NULL;
}

void client_destroy(WmState *wm, Client *c) {
    client_unlink(wm, c);
    XUnmapWindow(wm->dpy, c->frame);
    XReparentWindow(wm->dpy, c->win, wm->root, c->x, c->y);
    XDestroyWindow(wm->dpy, c->frame);
    free(c);
}

void client_focus(WmState *wm, Client *c) {
    if (wm->focused && wm->focused != c) {
        XSetWindowBorder(wm->dpy, wm->focused->frame, wm->cfg.border_color);
    }
    wm->focused = c;
    if (!c) return;
    XSetWindowBorder(wm->dpy, c->frame, wm->cfg.focus_color);
    XSetInputFocus(wm->dpy, c->win, RevertToPointerRoot, CurrentTime);
    XRaiseWindow(wm->dpy, c->frame);
}

void client_close(WmState *wm, Client *c) {
    int has_proto = 0;
    Atom *protocols;
    int n;
    if (XGetWMProtocols(wm->dpy, c->win, &protocols, &n)) {
        for (int i = 0; i < n; i++) {
            if (protocols[i] == wm->wm_delete_window) has_proto = 1;
        }
        XFree(protocols);
    }
    if (has_proto) {
        XEvent ev = {0};
        ev.xclient.type = ClientMessage;
        ev.xclient.window = c->win;
        ev.xclient.message_type = wm->wm_protocols;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = wm->wm_delete_window;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(wm->dpy, c->win, False, NoEventMask, &ev);
    } else {
        XKillClient(wm->dpy, c->win);
    }
}

void client_move_resize(WmState *wm, Client *c, int x, int y, int w, int h) {
    if (w < 100) w = 100;
    if (h < 60) h = 60;
    c->x = x;
    c->y = y;
    c->w = w;
    c->h = h;
    c->snap = SNAP_NONE;
    XMoveResizeWindow(wm->dpy, c->frame, x, y, w, h);
    XResizeWindow(wm->dpy, c->win, w, h);
}

void client_snap(WmState *wm, Client *c, SnapState snap) {
    int wx = wm->work_x, wy = wm->work_y, ww = wm->work_w, wh = wm->work_h;
    int gap = wm->cfg.gap;
    int x, y, w, h;

    switch (snap) {
        case SNAP_LEFT:
            x = wx + gap; y = wy + gap; w = ww / 2 - gap - gap / 2; h = wh - 2 * gap;
            break;
        case SNAP_RIGHT:
            x = wx + ww / 2 + gap / 2; y = wy + gap; w = ww / 2 - gap - gap / 2; h = wh - 2 * gap;
            break;
        case SNAP_TOP:
            x = wx + gap; y = wy + gap; w = ww - 2 * gap; h = wh / 2 - gap - gap / 2;
            break;
        case SNAP_BOTTOM:
            x = wx + gap; y = wy + wh / 2 + gap / 2; w = ww - 2 * gap; h = wh / 2 - gap - gap / 2;
            break;
        case SNAP_MAXIMIZED:
            x = wx + gap; y = wy + gap; w = ww - 2 * gap; h = wh - 2 * gap;
            break;
        default:
            return;
    }

    client_move_resize(wm, c, x, y, w, h);
    c->snap = snap;
}

void client_toggle_maximize(WmState *wm, Client *c) {
    if (c->snap == SNAP_MAXIMIZED) {
        client_move_resize(wm, c, c->saved_x, c->saved_y, c->saved_w, c->saved_h);
        c->snap = SNAP_NONE;
    } else {
        c->saved_x = c->x;
        c->saved_y = c->y;
        c->saved_w = c->w;
        c->saved_h = c->h;
        client_snap(wm, c, SNAP_MAXIMIZED);
    }
}

void client_minimize(WmState *wm, Client *c) {
    if (c->minimized) return;
    capture_client_thumbnail(wm, c);
    c->minimized = 1;
    XUnmapWindow(wm->dpy, c->frame);
    if (wm->focused == c) wm->focused = NULL;
}

void client_restore(WmState *wm, Client *c) {
    if (!c->minimized) return;
    c->minimized = 0;
    XMapWindow(wm->dpy, c->frame);
    client_focus(wm, c);
}

static void set_client_desktop_prop(WmState *wm, Client *c) {
    long ws = c->workspace;
    XChangeProperty(wm->dpy, c->win, wm->net_wm_desktop, XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *)&ws, 1);
}

void client_set_workspace(WmState *wm, Client *c, int workspace) {
    c->workspace = workspace;
    set_client_desktop_prop(wm, c);
    if (workspace == wm->active_workspace) {
        if (!c->minimized) XMapWindow(wm->dpy, c->frame);
    } else {
        if (!c->minimized) capture_client_thumbnail(wm, c);
        XUnmapWindow(wm->dpy, c->frame);
    }
}

void workspace_switch(WmState *wm, int workspace) {
    if (workspace < 0 || workspace >= NOCO_MAX_WORKSPACES) return;
    if (workspace == wm->active_workspace) return;

    for (Client *c = wm->clients; c; c = c->next) {
        if (c->workspace == wm->active_workspace && !c->minimized) {
            capture_client_thumbnail(wm, c);
        }
    }

    wm->active_workspace = workspace;
    wm->focused = NULL;

    for (Client *c = wm->clients; c; c = c->next) {
        if (c->workspace == workspace) {
            if (!c->minimized) XMapWindow(wm->dpy, c->frame);
        } else {
            XUnmapWindow(wm->dpy, c->frame);
        }
    }

    set_current_desktop_property(wm);
}
