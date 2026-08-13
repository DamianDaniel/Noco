#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include "wm.h"

static WmState g_wm;

static int xerror_handler(Display *dpy, XErrorEvent *ev) {
    char buf[256];
    XGetErrorText(dpy, ev->error_code, buf, sizeof(buf));
    fprintf(stderr, "noco-wm: X error: %s (request %d)\n", buf, ev->request_code);
    return 0;
}

static int xerror_start_handler(Display *dpy, XErrorEvent *ev) {
    (void)dpy;
    fprintf(stderr, "noco-wm: another WM is already running (error %d)\n", ev->error_code);
    exit(1);
}

static void scan_existing_windows(WmState *wm) {
    Window root_ret, parent_ret, *children;
    unsigned int n;
    if (!XQueryTree(wm->dpy, wm->root, &root_ret, &parent_ret, &children, &n))
        return;

    for (unsigned int i = 0; i < n; i++) {
        XWindowAttributes attr;
        if (!XGetWindowAttributes(wm->dpy, children[i], &attr)) continue;
        if (attr.override_redirect || attr.map_state != IsViewable) continue;
        client_create(wm, children[i]);
    }
    if (children) XFree(children);
}

int main(void) {
    WmState *wm = &g_wm;
    memset(wm, 0, sizeof(*wm));
    noco_config_load(&wm->cfg);

    wm->dpy = XOpenDisplay(NULL);
    if (!wm->dpy) {
        fprintf(stderr, "noco-wm: cannot open display\n");
        return 1;
    }

    wm->screen = DefaultScreen(wm->dpy);
    wm->root = RootWindow(wm->dpy, wm->screen);
    wm->screen_w = DisplayWidth(wm->dpy, wm->screen);
    wm->screen_h = DisplayHeight(wm->dpy, wm->screen);
    wm->active_workspace = 0;

    XSetErrorHandler(xerror_start_handler);
    XSelectInput(wm->dpy, wm->root,
        SubstructureRedirectMask | SubstructureNotifyMask |
        StructureNotifyMask | PropertyChangeMask);
    XSync(wm->dpy, False);
    XSetErrorHandler(xerror_handler);

    wm->wm_protocols = XInternAtom(wm->dpy, "WM_PROTOCOLS", False);
    wm->wm_delete_window = XInternAtom(wm->dpy, "WM_DELETE_WINDOW", False);
    init_ewmh(wm);

    wm->work_x = 0;
    wm->work_y = 0;
    wm->work_w = wm->screen_w;
    wm->work_h = wm->screen_h;

    grab_keys(wm);
    scan_existing_windows(wm);
    update_work_area(wm);
    update_net_client_list(wm);

    XEvent ev;
    while (1) {
        XNextEvent(wm->dpy, &ev);
        switch (ev.type) {
            case MapRequest:
                handle_map_request(wm, &ev.xmaprequest);
                break;
            case ConfigureRequest:
                handle_configure_request(wm, &ev.xconfigurerequest);
                break;
            case UnmapNotify:
                handle_unmap_notify(wm, &ev.xunmap);
                break;
            case DestroyNotify:
                handle_destroy_notify(wm, &ev.xdestroywindow);
                break;
            case ButtonPress:
                handle_button_press(wm, &ev.xbutton);
                break;
            case ButtonRelease:
                handle_button_release(wm, &ev.xbutton);
                break;
            case MotionNotify:
                while (XCheckTypedEvent(wm->dpy, MotionNotify, &ev));
                handle_motion_notify(wm, &ev.xmotion);
                break;
            case KeyPress:
                if (wm->switcher_active) {
                    switcher_handle_keypress(wm, &ev.xkey);
                } else {
                    handle_key_press(wm, &ev.xkey);
                }
                break;
            case KeyRelease:
                if (wm->switcher_active) {
                    switcher_handle_keyrelease(wm, &ev.xkey);
                }
                break;
            case EnterNotify:
                handle_enter_notify(wm, &ev.xcrossing);
                break;
            case PropertyNotify:
                handle_property_notify(wm, &ev.xproperty);
                break;
            case Expose:
                switcher_handle_expose(wm, &ev.xexpose);
                break;
            case ClientMessage:
                handle_client_message(wm, &ev.xclient);
                break;
            default:
                break;
        }
    }

    XCloseDisplay(wm->dpy);
    return 0;
}
