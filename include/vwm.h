#ifndef VWM_H
#define VWM_H

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <ctype.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <fontconfig/fontconfig.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

#include <X11/keysym.h>
#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xproto.h>

#define WORKSPACE_COUNT 9
#define BORDER_WIDTH 2
#define GAP_PX 10
#define MOD_MASK XCB_MOD_MASK_4
#define BAR_HEIGHT 22
#define CMD_MAX_ARGS 16
#define KEYBIND_COUNT 35
#define CONFIG_PATH_MAX 4096
#define CONFIG_INCLUDE_MAX_DEPTH 16
#define MFAC_STEP 0.05f

#define LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

typedef enum {
    ACTION_NONE = 0,
    ACTION_SPAWN_TERM,
    ACTION_SPAWN_LAUNCHER,
    ACTION_TOGGLE_SCRATCHPAD,
    ACTION_FOCUS_NEXT,
    ACTION_FOCUS_PREV,
    ACTION_FOCUS_MONITOR_PREV,
    ACTION_FOCUS_MONITOR_NEXT,
    ACTION_SEND_MONITOR_PREV,
    ACTION_SEND_MONITOR_NEXT,
    ACTION_TOGGLE_FULLSCREEN,
    ACTION_TOGGLE_SYNC,
    ACTION_KILL_CLIENT,
    ACTION_QUIT,
    ACTION_RELOAD_CONFIG,
    ACTION_DECREASE_MFACT,
    ACTION_INCREASE_MFACT,
    ACTION_ZOOM_MASTER,
    ACTION_VIEW_WS_1,
    ACTION_VIEW_WS_2,
    ACTION_VIEW_WS_3,
    ACTION_VIEW_WS_4,
    ACTION_VIEW_WS_5,
    ACTION_VIEW_WS_6,
    ACTION_VIEW_WS_7,
    ACTION_VIEW_WS_8,
    ACTION_VIEW_WS_9,
    ACTION_SEND_WS_1,
    ACTION_SEND_WS_2,
    ACTION_SEND_WS_3,
    ACTION_SEND_WS_4,
    ACTION_SEND_WS_5,
    ACTION_SEND_WS_6,
    ACTION_SEND_WS_7,
    ACTION_SEND_WS_8,
    ACTION_SEND_WS_9
} Action;

typedef struct {
    xcb_keysym_t sym;
    uint16_t mod;
    Action action;
} Keybind;

typedef struct {
    char path[CONFIG_PATH_MAX];

    int border_width;
    int gap_px;
    int bar_height;
    int bar_outer_gap;
    float default_mfact;
    float font_size;
    bool sync_workspaces;

    uint32_t bar_bg;
    uint32_t bar_fg;
    uint32_t border_active;
    uint32_t border_inactive;
    uint32_t border_urgent;
    uint32_t workspace_current;
    uint32_t workspace_occupied;
    uint32_t workspace_empty;

    char font_family[256];
    char terminal[256];
    char launcher[256];
    char scratchpad[256];

    int scratchpad_width_pct;
    int scratchpad_height_pct;
    int scratchpad_dim_alpha;

    char term_cmd_storage[CMD_MAX_ARGS][256];
    char launcher_cmd_storage[CMD_MAX_ARGS][256];
    char scratchpad_cmd_storage[CMD_MAX_ARGS][256];

    const char *term_cmd[CMD_MAX_ARGS];
    const char *launcher_cmd[CMD_MAX_ARGS];
    const char *scratchpad_cmd[CMD_MAX_ARGS];

    Keybind keybinds[KEYBIND_COUNT];
    size_t keybind_count;
} Config;

typedef struct Client Client;
typedef struct Workspace Workspace;
typedef struct Monitor Monitor;

typedef enum {
    LAYOUT_TILE = 0,
    LAYOUT_MONOCLE,
    LAYOUT_FLOAT,
} LayoutKind;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} Rect;

struct Client {
    xcb_window_t win;
    Monitor *mon;
    Workspace *ws;

    Rect frame;
    Rect old_frame;

    bool is_floating;
    bool is_fullscreen;
    bool is_scratchpad;
    bool is_urgent;
    bool is_hidden;

    Client *next;
    Client *prev;
};

struct Workspace {
    int id;
    LayoutKind layout;
    int gap_px;
    float mfact;
    int nmaster;
    bool hide_bar;

    Client *clients;
    Client *focused;
};

struct Monitor {
    int id;
    xcb_randr_output_t output;
    xcb_window_t barwin;
    xcb_window_t scratch_overlay_win;

    int bar_x;
    int bar_y;
    int bar_w;
    int bar_h;

    Rect geom;
    Rect work;

    int current_ws;
    int previous_ws;

    Workspace workspaces[WORKSPACE_COUNT];
    Client *focused;

    Monitor *next;
};

typedef struct {
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    xcb_window_t root;

    Display *dpy;
    Visual *visual;
    Colormap colormap;
    int xscreen;
    int depth;
    xcb_visualid_t visual_id;
    bool has_argb_visual;
    bool owns_colormap;

    bool shape_supported;
    int shape_event_base;
    int shape_error_base;

    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_window;
    xcb_atom_t net_wm_state;
    xcb_atom_t net_wm_state_fullscreen;
    xcb_atom_t net_active_window;
    xcb_atom_t utf8_string;
    xcb_atom_t net_wm_name;
    xcb_atom_t net_wm_window_type;
    xcb_atom_t net_wm_window_type_dock;
    xcb_atom_t net_wm_state_above;
    xcb_atom_t net_wm_state_sticky;
    xcb_atom_t net_wm_state_skip_taskbar;
    xcb_atom_t net_wm_state_skip_pager;

    XftFont *xft_font;
    XftColor xft_bar_fg;
    XftColor xft_bar_bg;
    XftColor xft_ws_current;
    XftColor xft_ws_occupied;
    XftColor xft_ws_empty;

    int font_ascent;
    int font_descent;
    int font_height;
    int font_char_width;

    bool running;

    char status_cache[512];

    Cursor hidden_cursor;

    Config config;
    Workspace scratch_workspace;
    bool scratch_overlay_visible;
    Monitor *scratch_monitor;

    Monitor *mons;
    Monitor *selmon;
    size_t mon_count;
} WM;

typedef struct {
    int workspace;
} WorkspaceArg;

extern WM wm;
extern volatile sig_atomic_t g_should_exit;
extern volatile sig_atomic_t g_should_reload;

typedef struct {
    char name[64];
    const char **argv;
} CommandRef;

#endif
