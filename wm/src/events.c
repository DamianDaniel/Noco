#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include "wm.h"

static void spawn(const char *cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execlp("/bin/sh", "/bin/sh", "-c", cmd, (char *)NULL);
        _exit(127);
    } else if (pid > 0) {
        signal(SIGCHLD, SIG_IGN);
    }
}

void handle_map_request(WmState *wm, XMapRequestEvent *ev) {
    if (client_find(wm, ev->window)) return;

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    int is_dock = 0;
    if (XGetWindowProperty(wm->dpy, ev->window, wm->net_wm_window_type, 0, 16,
            False, XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after,
            &data) == Success && data) {
        Atom *atoms = (Atom *)data;
        for (unsigned long i = 0; i < nitems; i++)
            if (atoms[i] == wm->net_wm_window_type_dock) is_dock = 1;
        XFree(data);
    }

    if (is_dock) {
        XMapWindow(wm->dpy, ev->window);
        update_work_area(wm);
        return;
    }

    Client *c = client_create(wm, ev->window);
    if (c) {
        client_focus(wm, c);
        update_net_client_list(wm);
    }
}

void handle_configure_request(WmState *wm, XConfigureRequestEvent *ev) {
    Client *c = client_find(wm, ev->window);
    XWindowChanges wc;
    wc.x = ev->x;
    wc.y = ev->y;
    wc.width = ev->width;
    wc.height = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;

    if (!c) {
        XConfigureWindow(wm->dpy, ev->window, ev->value_mask, &wc);
        return;
    }

    if (ev->value_mask & (CWWidth | CWHeight)) {
        int w = (ev->value_mask & CWWidth) ? ev->width : c->w;
        int h = (ev->value_mask & CWHeight) ? ev->height : c->h;
        client_move_resize(wm, c, c->x, c->y, w, h);
    }
}

void handle_unmap_notify(WmState *wm, XUnmapEvent *ev) {
    Client *c = client_find(wm, ev->window);
    if (c && !c->minimized) {
        client_destroy(wm, c);
        update_net_client_list(wm);
    }
    update_work_area(wm);
}

void handle_destroy_notify(WmState *wm, XDestroyWindowEvent *ev) {
    Client *c = client_find(wm, ev->window);
    if (c) {
        client_destroy(wm, c);
        update_net_client_list(wm);
    }
    update_work_area(wm);
}

void handle_property_notify(WmState *wm, XPropertyEvent *ev) {
    if (ev->atom == wm->net_wm_strut || ev->atom == wm->net_wm_strut_partial) {
        update_work_area(wm);
    }
}

void handle_client_message(WmState *wm, XClientMessageEvent *ev) {
    if (ev->message_type == wm->net_current_desktop) {
        workspace_switch(wm, (int)ev->data.l[0]);
    }
}

void handle_enter_notify(WmState *wm, XCrossingEvent *ev) {
    Client *c = client_find_by_frame(wm, ev->window);
    if (c) client_focus(wm, c);
}

void handle_button_press(WmState *wm, XButtonEvent *ev) {
    Client *c = client_find(wm, ev->window);
    if (!c) c = client_find_by_frame(wm, ev->window);
    if (!c) return;

    client_focus(wm, c);

    if (!(ev->state & wm->cfg.mod_mask)) return;

    wm->drag_active = 1;
    wm->drag_client = c;
    wm->drag_start_x = ev->x_root;
    wm->drag_start_y = ev->y_root;
    wm->drag_orig_x = c->x;
    wm->drag_orig_y = c->y;
    wm->drag_orig_w = c->w;
    wm->drag_orig_h = c->h;
    wm->drag_is_resize = (ev->button == 3);

    XGrabPointer(wm->dpy, c->frame, True,
        ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync, GrabModeAsync, None,
        None, CurrentTime);
}

void handle_button_release(WmState *wm, XButtonEvent *ev) {
    (void)ev;
    if (wm->drag_active) {
        XUngrabPointer(wm->dpy, CurrentTime);
        wm->drag_active = 0;
        wm->drag_client = NULL;
    }
}

void handle_motion_notify(WmState *wm, XMotionEvent *ev) {
    if (!wm->drag_active || !wm->drag_client) return;

    int dx = ev->x_root - wm->drag_start_x;
    int dy = ev->y_root - wm->drag_start_y;
    Client *c = wm->drag_client;

    if (wm->drag_is_resize) {
        client_move_resize(wm, c, c->x, c->y,
            wm->drag_orig_w + dx, wm->drag_orig_h + dy);
    } else {
        client_move_resize(wm, c,
            wm->drag_orig_x + dx, wm->drag_orig_y + dy, c->w, c->h);
    }
}

void handle_key_press(WmState *wm, XKeyEvent *ev) {
    KeySym key = XkbKeycodeToKeysym(wm->dpy, ev->keycode, 0, 0);

    if (!(ev->state & wm->cfg.mod_mask)) return;

    if (key >= XK_1 && key <= XK_9) {
        int ws = key - XK_1;
        if (ws >= wm->cfg.workspace_count) return;
        if (ev->state & ShiftMask) {
            if (wm->focused) client_set_workspace(wm, wm->focused, ws);
        } else {
            workspace_switch(wm, ws);
        }
        return;
    }

    switch (key) {
        case XK_q:
            if (wm->focused) client_close(wm, wm->focused);
            break;
        case XK_Tab: {
            switcher_begin(wm, !(ev->state & ShiftMask));
            break;
        }
        case XK_f:
            if (wm->focused) client_toggle_maximize(wm, wm->focused);
            break;
        case XK_m:
            if (wm->focused) client_minimize(wm, wm->focused);
            break;
        case XK_Left:
            if (wm->focused) client_snap(wm, wm->focused, SNAP_LEFT);
            break;
        case XK_Right:
            if (wm->focused) client_snap(wm, wm->focused, SNAP_RIGHT);
            break;
        case XK_Up:
            if (wm->focused) client_snap(wm, wm->focused, SNAP_TOP);
            break;
        case XK_Down:
            if (wm->focused) client_snap(wm, wm->focused, SNAP_BOTTOM);
            break;
        case XK_d:
            spawn(wm->cfg.launcher_cmd);
            break;
        case XK_Return:
            spawn(wm->cfg.terminal_cmd);
            break;
        default:
            break;
    }
}

void grab_keys(WmState *wm) {
    XUngrabKey(wm->dpy, AnyKey, AnyModifier, wm->root);

    KeySym keys[] = {
        XK_1, XK_2, XK_3, XK_4, XK_5, XK_6, XK_7, XK_8, XK_9,
        XK_q, XK_Tab, XK_f, XK_m, XK_Left, XK_Right, XK_Up, XK_Down,
        XK_d, XK_Return
    };
    unsigned int mods[] = {0, ShiftMask, LockMask, ShiftMask | LockMask};

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        KeyCode kc = XKeysymToKeycode(wm->dpy, keys[i]);
        for (size_t m = 0; m < sizeof(mods) / sizeof(mods[0]); m++) {
            XGrabKey(wm->dpy, kc, wm->cfg.mod_mask | mods[m], wm->root,
                True, GrabModeAsync, GrabModeAsync);
        }
    }
}

void grab_buttons(WmState *wm, Window win) {
    unsigned int mods[] = {0, ShiftMask, LockMask, ShiftMask | LockMask};
    for (size_t m = 0; m < sizeof(mods) / sizeof(mods[0]); m++) {
        XGrabButton(wm->dpy, Button1, wm->cfg.mod_mask | mods[m], win, True,
            ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
        XGrabButton(wm->dpy, Button3, wm->cfg.mod_mask | mods[m], win, True,
            ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
    }
    XGrabButton(wm->dpy, Button1, 0, win, True,
        ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
}
