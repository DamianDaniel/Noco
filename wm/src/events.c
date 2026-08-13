#include <stdio.h>
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
        fprintf(stderr, "noco-wm: failed to spawn '%s'\n", cmd);
        _exit(127);
    } else if (pid > 0) {
        signal(SIGCHLD, SIG_IGN);
    } else {
        fprintf(stderr, "noco-wm: fork failed for '%s'\n", cmd);
    }
}

void handle_map_request(WmState *wm, XMapRequestEvent *ev) {
    if (client_find(wm, ev->window)) return;

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    int is_dock = 0, is_utility = 0, is_notification = 0;
    if (XGetWindowProperty(wm->dpy, ev->window, wm->net_wm_window_type, 0, 16,
            False, XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after,
            &data) == Success && data) {
        Atom *atoms = (Atom *)data;
        Atom net_wm_window_type_utility = XInternAtom(wm->dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
        Atom net_wm_window_type_notification = XInternAtom(wm->dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
        for (unsigned long i = 0; i < nitems; i++) {
            if (atoms[i] == wm->net_wm_window_type_dock) is_dock = 1;
            if (atoms[i] == net_wm_window_type_utility) is_utility = 1;
            if (atoms[i] == net_wm_window_type_notification) is_notification = 1;
        }
        XFree(data);
    }

    if (is_dock || is_utility || is_notification) {
        XMapWindow(wm->dpy, ev->window);
        if (is_dock) update_work_area(wm);
        if (is_utility) {
            XSetInputFocus(wm->dpy, ev->window, RevertToPointerRoot, CurrentTime);
        }
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
        int content_w = (ev->value_mask & CWWidth) ? ev->width : c->w - 2 * wm->cfg.frame_margin;
        int content_h = (ev->value_mask & CWHeight) ? ev->height
            : c->h - wm->cfg.titlebar_height - wm->cfg.frame_margin;
        int frame_w = content_w + 2 * wm->cfg.frame_margin;
        int frame_h = content_h + wm->cfg.titlebar_height + wm->cfg.frame_margin;
        client_move_resize(wm, c, c->x, c->y, frame_w, frame_h);
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
    if (ev->window == wm->root) {
        if (ev->atom == wm->net_wm_strut || ev->atom == wm->net_wm_strut_partial) {
            update_work_area(wm);
        } else {
            Atom rootpmap = XInternAtom(wm->dpy, "_XROOTPMAP_ID", False);
            if (ev->atom == rootpmap) {
                wallpaper_cache_refresh(&wm->wc);
                for (Client *c = wm->clients; c; c = c->next) redraw_decorations(wm, c);
            }
        }
        return;
    }

    Client *c = client_find(wm, ev->window);
    if (!c) return;

    Atom wm_name = XInternAtom(wm->dpy, "WM_NAME", False);
    Atom net_wm_name = XInternAtom(wm->dpy, "_NET_WM_NAME", False);
    if (ev->atom == wm_name || ev->atom == net_wm_name) {
        redraw_decorations(wm, c);
    }
}

void handle_client_message(WmState *wm, XClientMessageEvent *ev) {
    if (ev->message_type == wm->net_current_desktop) {
        workspace_switch(wm, (int)ev->data.l[0]);
        return;
    }

    if (ev->message_type == wm->net_active_window) {
        Client *c = client_find(wm, ev->window);
        if (!c) return;
        if (c->workspace != wm->active_workspace) workspace_switch(wm, c->workspace);
        if (c->minimized) client_restore(wm, c);
        else client_focus(wm, c);
        return;
    }

    if (ev->message_type == wm->net_wm_state) {
        Client *c = client_find(wm, ev->window);
        if (!c) return;
        Atom prop = (Atom)ev->data.l[1];
        if (prop != wm->net_wm_state_hidden) return;

        long action = ev->data.l[0];
        if (action == 1) {
            client_minimize(wm, c);
        } else if (action == 0) {
            client_restore(wm, c);
        } else {
            if (c->minimized) client_restore(wm, c);
            else client_minimize(wm, c);
        }
    }
}

void handle_enter_notify(WmState *wm, XCrossingEvent *ev) {
    (void)wm;
    (void)ev;
}

void handle_button_press(WmState *wm, XButtonEvent *ev) {
    Client *c = client_find_by_frame(wm, ev->window);
    if (c) {
        client_focus(wm, c);

        int edges;
        int hit = hit_test_frame(wm, c, ev->x, ev->y, &edges);

        if (hit == HIT_CLOSE) {
            client_close(wm, c);
            return;
        }
        if (hit == HIT_MINIMIZE) {
            client_minimize(wm, c);
            return;
        }
        if (hit == HIT_MAXIMIZE) {
            client_toggle_maximize(wm, c);
            return;
        }

        if (hit == HIT_TITLEBAR) {
            if (wm->last_titlebar_click_client == c &&
                    ev->time - wm->last_titlebar_click_time < 400) {
                client_toggle_maximize(wm, c);
                wm->last_titlebar_click_client = NULL;
                return;
            }
            wm->last_titlebar_click_client = c;
            wm->last_titlebar_click_time = ev->time;

            wm->drag_active = 1;
            wm->drag_mode = DRAG_MOVE;
            wm->drag_client = c;
            wm->drag_start_x = ev->x_root;
            wm->drag_start_y = ev->y_root;
            wm->drag_orig_x = c->x;
            wm->drag_orig_y = c->y;
            wm->drag_orig_w = c->w;
            wm->drag_orig_h = c->h;
            XGrabPointer(wm->dpy, c->frame, True,
                ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
            return;
        }

        if (hit == HIT_RESIZE) {
            wm->drag_active = 1;
            wm->drag_mode = DRAG_RESIZE;
            wm->drag_edges = edges;
            wm->drag_client = c;
            wm->drag_start_x = ev->x_root;
            wm->drag_start_y = ev->y_root;
            wm->drag_orig_x = c->x;
            wm->drag_orig_y = c->y;
            wm->drag_orig_w = c->w;
            wm->drag_orig_h = c->h;
            XGrabPointer(wm->dpy, c->frame, True,
                ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
            return;
        }
        return;
    }

    c = client_find(wm, ev->window);
    if (!c) return;

    client_focus(wm, c);

    if (!(ev->state & wm->cfg.mod_mask)) {
        XAllowEvents(wm->dpy, ReplayPointer, CurrentTime);
        return;
    }

    wm->drag_active = 1;
    wm->drag_mode = (ev->button == 3) ? DRAG_RESIZE : DRAG_MOVE;
    wm->drag_edges = EDGE_E | EDGE_S;
    wm->drag_client = c;
    wm->drag_start_x = ev->x_root;
    wm->drag_start_y = ev->y_root;
    wm->drag_orig_x = c->x;
    wm->drag_orig_y = c->y;
    wm->drag_orig_w = c->w;
    wm->drag_orig_h = c->h;

    XGrabPointer(wm->dpy, c->frame, True,
        ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync, GrabModeAsync, None,
        None, CurrentTime);
}

void handle_button_release(WmState *wm, XButtonEvent *ev) {
    (void)ev;
    if (wm->drag_active) {
        if (wm->drag_mode == DRAG_MOVE && wm->drag_client) {
            redraw_decorations(wm, wm->drag_client);
        }
        XUngrabPointer(wm->dpy, CurrentTime);
        wm->drag_active = 0;
        wm->drag_mode = DRAG_NONE;
        wm->drag_edges = EDGE_NONE;
        wm->drag_client = NULL;
    }
}

void handle_motion_notify(WmState *wm, XMotionEvent *ev) {
    if (!wm->drag_active || !wm->drag_client) return;

    int dx = ev->x_root - wm->drag_start_x;
    int dy = ev->y_root - wm->drag_start_y;
    Client *c = wm->drag_client;

    if (wm->drag_mode == DRAG_MOVE) {
        client_reposition(wm, c, wm->drag_orig_x + dx, wm->drag_orig_y + dy);
    } else if (wm->drag_mode == DRAG_RESIZE) {
        int x = wm->drag_orig_x, y = wm->drag_orig_y;
        int w = wm->drag_orig_w, h = wm->drag_orig_h;

        if (wm->drag_edges & EDGE_E) w = wm->drag_orig_w + dx;
        if (wm->drag_edges & EDGE_S) h = wm->drag_orig_h + dy;
        if (wm->drag_edges & EDGE_W) {
            w = wm->drag_orig_w - dx;
            x = wm->drag_orig_x + dx;
        }
        if (wm->drag_edges & EDGE_N) {
            h = wm->drag_orig_h - dy;
            y = wm->drag_orig_y + dy;
        }

        client_move_resize(wm, c, x, y, w, h);
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
