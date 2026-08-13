#ifndef NOCO_WM_H
#define NOCO_WM_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stddef.h>
#include "config.h"
#include "wallpaper.h"

#define NOCO_MAX_WORKSPACES 9

#define DRAG_NONE 0
#define DRAG_MOVE 1
#define DRAG_RESIZE 2

#define EDGE_NONE 0
#define EDGE_N (1 << 0)
#define EDGE_S (1 << 1)
#define EDGE_E (1 << 2)
#define EDGE_W (1 << 3)

#define HIT_NONE 0
#define HIT_TITLEBAR 1
#define HIT_CLOSE 2
#define HIT_MAXIMIZE 3
#define HIT_MINIMIZE 4
#define HIT_RESIZE 5

typedef enum {
    SNAP_NONE,
    SNAP_LEFT,
    SNAP_RIGHT,
    SNAP_TOP,
    SNAP_BOTTOM,
    SNAP_MAXIMIZED
} SnapState;

typedef struct Client {
    Window win;
    Window frame;
    int x, y, w, h;
    int saved_x, saved_y, saved_w, saved_h;
    int workspace;
    int minimized;
    SnapState snap;
    char thumb_path[256];
    int has_thumb;
    struct Client *next;
} Client;

typedef struct {
    NocoConfig cfg;
    Display *dpy;
    Window root;
    int screen;
    int screen_w, screen_h;
    int work_x, work_y, work_w, work_h;
    WallpaperCache wc;
    Client *clients;
    Client *focused;
    int active_workspace;

    int drag_active;
    int drag_mode;
    int drag_edges;
    Client *drag_client;
    int drag_start_x, drag_start_y;
    int drag_orig_x, drag_orig_y, drag_orig_w, drag_orig_h;
    Time last_titlebar_click_time;
    Client *last_titlebar_click_client;

    Atom wm_delete_window;
    Atom wm_protocols;
    Atom net_wm_strut;
    Atom net_wm_strut_partial;
    Atom net_wm_window_type;
    Atom net_wm_window_type_dock;
    Atom net_current_desktop;
    Atom net_number_of_desktops;
    Atom net_active_window;
    Atom net_client_list;
    Atom net_wm_state;
    Atom net_wm_state_hidden;
    Atom net_supported;
    Atom net_wm_desktop;

    int switcher_active;
    Window switcher_win;
    Client **switcher_list;
    int switcher_count;
    int switcher_selected;
    KeyCode switcher_mod_keycodes[8];
    int switcher_mod_keycode_count;
} WmState;

Client *client_create(WmState *wm, Window w);
void client_destroy(WmState *wm, Client *c);
Client *client_find(WmState *wm, Window w);
Client *client_find_by_frame(WmState *wm, Window frame);
void client_focus(WmState *wm, Client *c);
void client_close(WmState *wm, Client *c);
void client_move_resize(WmState *wm, Client *c, int x, int y, int w, int h);
void client_reposition(WmState *wm, Client *c, int x, int y);
void client_snap(WmState *wm, Client *c, SnapState snap);
void client_toggle_maximize(WmState *wm, Client *c);
void client_set_workspace(WmState *wm, Client *c, int workspace);
void client_minimize(WmState *wm, Client *c);
void client_restore(WmState *wm, Client *c);

void workspace_switch(WmState *wm, int workspace);
void update_work_area(WmState *wm);
void update_net_client_list(WmState *wm);
void init_ewmh(WmState *wm);

void handle_map_request(WmState *wm, XMapRequestEvent *ev);
void handle_configure_request(WmState *wm, XConfigureRequestEvent *ev);
void handle_unmap_notify(WmState *wm, XUnmapEvent *ev);
void handle_destroy_notify(WmState *wm, XDestroyWindowEvent *ev);
void handle_button_press(WmState *wm, XButtonEvent *ev);
void handle_button_release(WmState *wm, XButtonEvent *ev);
void handle_motion_notify(WmState *wm, XMotionEvent *ev);
void handle_key_press(WmState *wm, XKeyEvent *ev);
void handle_enter_notify(WmState *wm, XCrossingEvent *ev);
void handle_property_notify(WmState *wm, XPropertyEvent *ev);
void handle_client_message(WmState *wm, XClientMessageEvent *ev);
void set_current_desktop_property(WmState *wm);

void grab_keys(WmState *wm);
void grab_buttons(WmState *wm, Window win);

void capture_client_thumbnail(WmState *wm, Client *c);
void thumbnail_path_for(Window win, char *buf, size_t buflen);

void switcher_begin(WmState *wm, int forward);
void switcher_cycle(WmState *wm, int forward);
void switcher_end(WmState *wm, int commit);
void switcher_handle_keypress(WmState *wm, XKeyEvent *ev);
void switcher_handle_keyrelease(WmState *wm, XKeyEvent *ev);
void switcher_handle_expose(WmState *wm, XExposeEvent *ev);

void redraw_decorations(WmState *wm, Client *c);
int hit_test_frame(WmState *wm, Client *c, int lx, int ly, int *out_edges);
void get_client_title(WmState *wm, Client *c, char *buf, size_t buflen);

#endif
