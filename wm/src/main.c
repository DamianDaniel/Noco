#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include "wm.h"

static WmState g_wm;

static void set_default_cursor(WmState *wm) {
    Cursor cursor = XCreateFontCursor(wm->dpy, XC_left_ptr);
    XDefineCursor(wm->dpy, wm->root, cursor);
    XFreeCursor(wm->dpy, cursor);
}

static void set_default_wallpaper(WmState *wm) {
    Atom rootpmap_atom = XInternAtom(wm->dpy, "_XROOTPMAP_ID", False);
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    if (XGetWindowProperty(wm->dpy, wm->root, rootpmap_atom, 0, 1, False,
            XA_PIXMAP, &actual_type, &actual_format, &nitems, &bytes_after,
            &data) == Success && data) {
        Pixmap existing = *(Pixmap *)data;
        XFree(data);
        if (existing != None) return;
    }

    int depth = DefaultDepth(wm->dpy, wm->screen);
    Pixmap pm = XCreatePixmap(wm->dpy, wm->root, wm->screen_w, wm->screen_h, depth);

    cairo_surface_t *surface = cairo_xlib_surface_create(
        wm->dpy, pm, DefaultVisual(wm->dpy, wm->screen), wm->screen_w, wm->screen_h);
    cairo_t *cr = cairo_create(surface);

    cairo_pattern_t *grad = cairo_pattern_create_linear(0, 0, wm->screen_w, wm->screen_h);
    cairo_pattern_add_color_stop_rgb(grad, 0.0, 0.11, 0.12, 0.16);
    cairo_pattern_add_color_stop_rgb(grad, 1.0, 0.05, 0.06, 0.09);
    cairo_set_source(cr, grad);
    cairo_paint(cr);
    cairo_pattern_destroy(grad);

    cairo_destroy(cr);
    cairo_surface_flush(surface);
    cairo_surface_destroy(surface);

    XSetWindowBackgroundPixmap(wm->dpy, wm->root, pm);
    XClearWindow(wm->dpy, wm->root);

    XChangeProperty(wm->dpy, wm->root, rootpmap_atom, XA_PIXMAP, 32,
        PropModeReplace, (unsigned char *)&pm, 1);

    Atom esetroot_atom = XInternAtom(wm->dpy, "ESETROOT_PMAP_ID", False);
    XChangeProperty(wm->dpy, wm->root, esetroot_atom, XA_PIXMAP, 32,
        PropModeReplace, (unsigned char *)&pm, 1);
}

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

    set_default_cursor(wm);
    set_default_wallpaper(wm);
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
