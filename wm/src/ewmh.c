#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include "wm.h"

void init_ewmh(WmState *wm) {
    wm->net_wm_strut = XInternAtom(wm->dpy, "_NET_WM_STRUT", False);
    wm->net_wm_strut_partial = XInternAtom(wm->dpy, "_NET_WM_STRUT_PARTIAL", False);
    wm->net_wm_window_type = XInternAtom(wm->dpy, "_NET_WM_WINDOW_TYPE", False);
    wm->net_wm_window_type_dock = XInternAtom(wm->dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    wm->net_current_desktop = XInternAtom(wm->dpy, "_NET_CURRENT_DESKTOP", False);
    wm->net_number_of_desktops = XInternAtom(wm->dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    wm->net_active_window = XInternAtom(wm->dpy, "_NET_ACTIVE_WINDOW", False);
    wm->net_client_list = XInternAtom(wm->dpy, "_NET_CLIENT_LIST", False);
    wm->net_wm_state = XInternAtom(wm->dpy, "_NET_WM_STATE", False);
    wm->net_wm_state_hidden = XInternAtom(wm->dpy, "_NET_WM_STATE_HIDDEN", False);
    wm->net_supported = XInternAtom(wm->dpy, "_NET_SUPPORTED", False);
    wm->net_wm_desktop = XInternAtom(wm->dpy, "_NET_WM_DESKTOP", False);

    Atom supported[] = {
        wm->net_wm_strut, wm->net_wm_strut_partial,
        wm->net_wm_window_type, wm->net_wm_window_type_dock,
        wm->net_current_desktop, wm->net_number_of_desktops,
        wm->net_active_window, wm->net_client_list,
        wm->net_wm_state, wm->net_wm_state_hidden
    };
    XChangeProperty(wm->dpy, wm->root, wm->net_supported, XA_ATOM, 32,
        PropModeReplace, (unsigned char *)supported,
        sizeof(supported) / sizeof(Atom));

    long ndesktops = NOCO_MAX_WORKSPACES;
    XChangeProperty(wm->dpy, wm->root, wm->net_number_of_desktops, XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *)&ndesktops, 1);

    long cur = wm->active_workspace;
    XChangeProperty(wm->dpy, wm->root, wm->net_current_desktop, XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *)&cur, 1);
}

void update_net_client_list(WmState *wm) {
    int count = 0;
    for (Client *c = wm->clients; c; c = c->next) count++;

    Window *wins = malloc(sizeof(Window) * (count > 0 ? count : 1));
    int i = 0;
    for (Client *c = wm->clients; c; c = c->next) wins[i++] = c->win;

    XChangeProperty(wm->dpy, wm->root, wm->net_client_list, XA_WINDOW, 32,
        PropModeReplace, (unsigned char *)wins, count);
    free(wins);
}

static int window_is_dock(WmState *wm, Window w) {
    Atom type, actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    int is_dock = 0;

    if (XGetWindowProperty(wm->dpy, w, wm->net_wm_window_type, 0, 16, False,
            XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after,
            &data) == Success && data) {
        Atom *atoms = (Atom *)data;
        for (unsigned long i = 0; i < nitems; i++) {
            if (atoms[i] == wm->net_wm_window_type_dock) is_dock = 1;
        }
        XFree(data);
    }
    (void)type;
    return is_dock;
}

void set_current_desktop_property(WmState *wm) {
    long cur = wm->active_workspace;
    XChangeProperty(wm->dpy, wm->root, wm->net_current_desktop, XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *)&cur, 1);
}

void update_work_area(WmState *wm) {
    int top = 0, bottom = 0, left = 0, right = 0;

    Window root_ret, parent_ret, *children;
    unsigned int n;
    if (XQueryTree(wm->dpy, wm->root, &root_ret, &parent_ret, &children, &n)) {
        for (unsigned int i = 0; i < n; i++) {
            if (!window_is_dock(wm, children[i])) continue;

            Atom actual_type;
            int actual_format;
            unsigned long nitems, bytes_after;
            unsigned char *data = NULL;

            if (XGetWindowProperty(wm->dpy, children[i], wm->net_wm_strut_partial,
                    0, 12, False, XA_CARDINAL, &actual_type, &actual_format,
                    &nitems, &bytes_after, &data) == Success && data && nitems >= 4) {
                long *strut = (long *)data;
                if (strut[0] > left) left = strut[0];
                if (strut[1] > right) right = strut[1];
                if (strut[2] > top) top = strut[2];
                if (strut[3] > bottom) bottom = strut[3];
                XFree(data);
            } else if (XGetWindowProperty(wm->dpy, children[i], wm->net_wm_strut,
                    0, 4, False, XA_CARDINAL, &actual_type, &actual_format,
                    &nitems, &bytes_after, &data) == Success && data && nitems >= 4) {
                long *strut = (long *)data;
                if (strut[0] > left) left = strut[0];
                if (strut[1] > right) right = strut[1];
                if (strut[2] > top) top = strut[2];
                if (strut[3] > bottom) bottom = strut[3];
                XFree(data);
            }
        }
        if (children) XFree(children);
    }

    wm->work_x = left;
    wm->work_y = top;
    wm->work_w = wm->screen_w - left - right;
    wm->work_h = wm->screen_h - top - bottom;
}
