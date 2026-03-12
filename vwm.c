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

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xft/Xft.h>
#include <fontconfig/fontconfig.h>

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
    uint32_t workspace_current;
    uint32_t workspace_occupied;
    uint32_t workspace_empty;

    char font_family[256];
    char terminal[256];
    char launcher[256];
    char scratchpad[256];

    char terminal_arg0[256];
    char launcher_arg0[256];
    char scratchpad_arg0[256];
    char scratchpad_arg1[256];
    char scratchpad_arg2[256];

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

    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_window;
    xcb_atom_t net_wm_state;
    xcb_atom_t net_wm_state_fullscreen;
    xcb_atom_t net_active_window;
    xcb_atom_t utf8_string;
    xcb_atom_t net_wm_name;

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
    bool scratchpad_spawn_pending;

    char status_cache[512];

    Config config;
    Client *scratchpad;
    Monitor *mons;
    Monitor *selmon;
    size_t mon_count;
} WM;

static WM wm = {0};

static volatile sig_atomic_t g_should_exit = 0;
static volatile sig_atomic_t g_should_reload = 0;

static void cleanup(void);
static void die(const char *msg);
static void event_loop(void);
static void focus_client(Client *c);
static void focus_workspace(Monitor *m);
static void grab_keys(void);
static void layout_monitor(Monitor *m);
static void draw_bar(Monitor *m);
static void draw_all_bars(void);
static void handle_expose(xcb_generic_event_t *gev);
static void handle_property_notify(xcb_generic_event_t *gev);
static void manage_window(xcb_window_t win);
static Client *find_client(xcb_window_t win);
static void map_request(xcb_generic_event_t *gev);
static void key_press(xcb_generic_event_t *gev);
static void destroy_notify(xcb_generic_event_t *gev);
static void unmap_notify(xcb_generic_event_t *gev);
static void configure_request(xcb_generic_event_t *gev);
static void scan_existing_windows(void);
static void setup_atoms(void);
static void open_font_from_config(void);
static void setup_monitors(void);
static void setup_root(void);
static void unmanage_client(Client *c);
static Workspace *ws_of(Monitor *m, int idx);
static void center_client_on_monitor(Client *c, Monitor *m);
static void get_client_title(Client *c, char *buf, size_t buflen);
static void update_monitor_workarea(Monitor *m);
static void clear_focus_borders_except(Client *keep);
static void get_root_status_text(char *buf, size_t buflen);
static Client *find_scratchpad_client(void);
static Client *find_fullscreen_client(Workspace *ws);
static Client *first_tiled_client(Workspace *ws);
static bool workspace_has_clients(Workspace *ws);
static int bar_text_baseline(void);
static int text_width_px(const char *s);
static void sanitize_config(void);
static void set_client_fullscreen_state(Client *c, bool enabled);
static bool client_supports_protocol(Client *c, xcb_atom_t protocol);
static void send_wm_delete(Client *c);

static void load_default_config(void);
static void rebuild_config_commands(void);
static void apply_config(void);
static void reload_config(const void *arg);
static void init_default_keybinds(void);
static void dispatch_action(Action action);

static void load_config_file(const char *path);
static char *trim_whitespace(char *s);
static void strip_comment(char *s);
static bool parse_bool_value(const char *s, bool *out);
static bool parse_color_value(const char *s, uint32_t *out);
static void toml_unquote_inplace(char *s);

static void free_xft_resources(void);
static void alloc_xft_color(XftColor *dst, uint32_t rgb);
static void refresh_xft_resources(void);
static int utf8_text_width(const char *s);
static void draw_utf8(XftDraw *draw, XftColor *color, int x, int y, const char *s);
static void update_status_cache(void);
static int utf8_char_len(unsigned char c);
static size_t utf8_prev_boundary(const char *s, size_t len);
static void utf8_truncate_to_width(const char *src, int max_width, char *dst, size_t dstsz);
static bool get_text_property_utf8(xcb_window_t win, xcb_atom_t prop_atom, char *buf, size_t buflen);
static bool get_text_property_legacy(xcb_window_t win, char *buf, size_t buflen);
static void draw_workspace_dots(Monitor *m, XftDraw *draw, int start_x, int baseline, int step_px);


static void spawn(const void *arg);
static void quit(const void *arg);
static void focus_next(const void *arg);
static void focus_prev(const void *arg);
static void focus_monitor_next(const void *arg);
static void focus_monitor_prev(const void *arg);
static void send_to_monitor_next(const void *arg);
static void send_to_monitor_prev(const void *arg);
static void view_workspace(const void *arg);
static void send_to_workspace(const void *arg);
static void toggle_sync_workspaces(const void *arg);
static void toggle_fullscreen(const void *arg);
static void toggle_scratchpad(const void *arg);
static void kill_client(const void *arg);
static void decrease_mfact(const void *arg);
static void increase_mfact(const void *arg);
static void zoom_master(const void *arg);

static const char *launcher_fallback[] = {"dmenu_run", NULL};

typedef struct {
    int workspace;
} WorkspaceArg;

static void die(const char *msg) {
    fprintf(stderr, "fatal: %s\n", msg);
    cleanup();
    exit(1);
}

static void free_xft_resources(void) {
    if (!wm.dpy) {
        return;
    }

    if (wm.xft_font) {
        XftFontClose(wm.dpy, wm.xft_font);
        wm.xft_font = NULL;
    }

    XftColorFree(wm.dpy, wm.visual, wm.colormap, &wm.xft_bar_fg);
    XftColorFree(wm.dpy, wm.visual, wm.colormap, &wm.xft_bar_bg);
    XftColorFree(wm.dpy, wm.visual, wm.colormap, &wm.xft_ws_current);
    XftColorFree(wm.dpy, wm.visual, wm.colormap, &wm.xft_ws_occupied);
    XftColorFree(wm.dpy, wm.visual, wm.colormap, &wm.xft_ws_empty);

    memset(&wm.xft_bar_fg, 0, sizeof(wm.xft_bar_fg));
    memset(&wm.xft_bar_bg, 0, sizeof(wm.xft_bar_bg));
    memset(&wm.xft_ws_current, 0, sizeof(wm.xft_ws_current));
    memset(&wm.xft_ws_occupied, 0, sizeof(wm.xft_ws_occupied));
    memset(&wm.xft_ws_empty, 0, sizeof(wm.xft_ws_empty));
}

static void cleanup(void) {
    Monitor *m = wm.mons;
    while (m) {
        Monitor *next = m->next;
        if (m->barwin) {
            xcb_destroy_window(wm.conn, m->barwin);
        }
        free(m);
        m = next;
    }
    wm.mons = NULL;
    wm.selmon = NULL;
    wm.scratchpad = NULL;

    free_xft_resources();

    if (wm.dpy) {
        XCloseDisplay(wm.dpy);
        wm.dpy = NULL;
        wm.conn = NULL;
    } else if (wm.conn) {
        xcb_flush(wm.conn);
        xcb_disconnect(wm.conn);
        wm.conn = NULL;
    }
}

static Workspace *ws_of(Monitor *m, int idx) {
    if (!m || idx < 0 || idx >= WORKSPACE_COUNT) {
        return NULL;
    }
    return &m->workspaces[idx];
}

static xcb_atom_t intern_atom(const char *name) {
    xcb_intern_atom_cookie_t ck = xcb_intern_atom(wm.conn, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(wm.conn, ck, NULL);
    xcb_atom_t atom = XCB_ATOM_NONE;
    if (reply) {
        atom = reply->atom;
        free(reply);
    }
    return atom;
}

static void setup_atoms(void) {
    wm.wm_protocols = intern_atom("WM_PROTOCOLS");
    wm.wm_delete_window = intern_atom("WM_DELETE_WINDOW");
    wm.net_wm_state = intern_atom("_NET_WM_STATE");
    wm.net_wm_state_fullscreen = intern_atom("_NET_WM_STATE_FULLSCREEN");
    wm.net_active_window = intern_atom("_NET_ACTIVE_WINDOW");
    wm.utf8_string = intern_atom("UTF8_STRING");
    wm.net_wm_name = intern_atom("_NET_WM_NAME");
}

static void alloc_xft_color(XftColor *dst, uint32_t rgb) {
    XRenderColor xr = {
        .red   = (unsigned short)(((rgb >> 16) & 0xff) * 257),
        .green = (unsigned short)(((rgb >> 8) & 0xff) * 257),
        .blue  = (unsigned short)((rgb & 0xff) * 257),
        .alpha = 0xffff
    };

    if (!XftColorAllocValue(wm.dpy, wm.visual, wm.colormap, &xr, dst)) {
        die("failed to allocate Xft color");
    }
}

static void refresh_xft_resources(void) {
    free_xft_resources();

    char pattern[512];
    snprintf(
        pattern,
        sizeof(pattern),
        "%s:size=%.1f:antialias=true:hinting=true",
        wm.config.font_family[0] ? wm.config.font_family : "monospace",
        wm.config.font_size > 0.0f ? wm.config.font_size : 11.0f
    );

    wm.xft_font = XftFontOpenName(wm.dpy, wm.xscreen, pattern);
    if (!wm.xft_font) {
        wm.xft_font = XftFontOpenName(wm.dpy, wm.xscreen, "monospace:size=11");
    }
    if (!wm.xft_font) {
        die("failed to open Xft font");
    }

    wm.font_ascent = wm.xft_font->ascent;
    wm.font_descent = wm.xft_font->descent;
    wm.font_height = wm.font_ascent + wm.font_descent;

    XGlyphInfo ext;
    const FcChar8 sample[] = "M";
    XftTextExtentsUtf8(wm.dpy, wm.xft_font, sample, 1, &ext);
    wm.font_char_width = ext.xOff > 0 ? ext.xOff : 8;

    alloc_xft_color(&wm.xft_bar_fg, wm.config.bar_fg);
    alloc_xft_color(&wm.xft_bar_bg, wm.config.bar_bg);
    alloc_xft_color(&wm.xft_ws_current, wm.config.workspace_current);
    alloc_xft_color(&wm.xft_ws_occupied, wm.config.workspace_occupied);
    alloc_xft_color(&wm.xft_ws_empty, wm.config.workspace_empty);
}

static void open_font_from_config(void) {
    refresh_xft_resources();
}

static void create_bar(Monitor *m) {
    if (!m) {
        return;
    }

    int outer = MAX(0, wm.config.bar_outer_gap);
    int bar_x = m->geom.x + outer;
    int bar_y = m->geom.y + outer;
    int bar_w = MAX(1, m->geom.w - outer * 2);

    uint32_t values[] = {
        wm.config.bar_bg,
        XCB_EVENT_MASK_EXPOSURE,
        1
    };

    m->barwin = xcb_generate_id(wm.conn);
    xcb_create_window(
        wm.conn,
        XCB_COPY_FROM_PARENT,
        m->barwin,
        wm.root,
        bar_x,
        bar_y,
        (uint16_t)bar_w,
        (uint16_t)wm.config.bar_height,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        wm.screen->root_visual,
        XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_OVERRIDE_REDIRECT,
        values
    );
    xcb_map_window(wm.conn, m->barwin);

    update_monitor_workarea(m);
}

static Monitor *add_monitor(int id, int x, int y, int w, int h, xcb_randr_output_t output) {
    Monitor *m = calloc(1, sizeof(*m));
    if (!m) {
        die("calloc monitor failed");
    }

    m->id = id;
    m->output = output;
    m->geom = (Rect){ .x = x, .y = y, .w = w, .h = h };
    m->current_ws = 0;
    m->previous_ws = 0;

    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        m->workspaces[i].id = i;
        m->workspaces[i].layout = LAYOUT_TILE;
        m->workspaces[i].gap_px = wm.config.gap_px;
        m->workspaces[i].mfact = wm.config.default_mfact;
        m->workspaces[i].nmaster = 1;
        m->workspaces[i].hide_bar = false;
    }

    create_bar(m);

    if (!wm.mons) {
        wm.mons = m;
    } else {
        Monitor *tail = wm.mons;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = m;
    }

    wm.mon_count++;
    if (!wm.selmon) {
        wm.selmon = m;
    }
    return m;
}

static void setup_monitors(void) {
    const xcb_setup_t *setup = xcb_get_setup(wm.conn);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    wm.screen = it.data;
    wm.root = wm.screen->root;

    xcb_randr_query_version_cookie_t ver_cookie =
        xcb_randr_query_version(wm.conn, 1, 5);

    xcb_randr_query_version_reply_t *ver_reply =
        xcb_randr_query_version_reply(wm.conn, ver_cookie, NULL);

    if (!ver_reply) {
        die("RandR not available");
    }

    free(ver_reply);

    xcb_randr_get_screen_resources_current_cookie_t res_cookie =
        xcb_randr_get_screen_resources_current(wm.conn, wm.root);

    xcb_randr_get_screen_resources_current_reply_t *res =
        xcb_randr_get_screen_resources_current_reply(wm.conn, res_cookie, NULL);

    if (!res) {
        die("RandR screen resources failed");
    }

    int output_count = xcb_randr_get_screen_resources_current_outputs_length(res);
    xcb_randr_output_t *outputs =
        xcb_randr_get_screen_resources_current_outputs(res);

    int mon_id = 0;

    for (int i = 0; i < output_count; i++) {
        xcb_randr_get_output_info_cookie_t out_cookie =
            xcb_randr_get_output_info(wm.conn, outputs[i], XCB_CURRENT_TIME);

        xcb_randr_get_output_info_reply_t *out =
            xcb_randr_get_output_info_reply(wm.conn, out_cookie, NULL);

        if (!out) {
            continue;
        }

        if (out->connection != XCB_RANDR_CONNECTION_CONNECTED || out->crtc == XCB_NONE) {
            free(out);
            continue;
        }

        xcb_randr_get_crtc_info_cookie_t crtc_cookie =
            xcb_randr_get_crtc_info(wm.conn, out->crtc, XCB_CURRENT_TIME);

        xcb_randr_get_crtc_info_reply_t *crtc =
            xcb_randr_get_crtc_info_reply(wm.conn, crtc_cookie, NULL);

        if (!crtc) {
            free(out);
            continue;
        }

        if (crtc->width > 0 && crtc->height > 0) {
            add_monitor(mon_id++, crtc->x, crtc->y, crtc->width, crtc->height, outputs[i]);
        }

        free(crtc);
        free(out);
    }

    free(res);

    if (!wm.mons) {
        add_monitor(0, 0, 0, wm.screen->width_in_pixels, wm.screen->height_in_pixels, XCB_NONE);
    }
}

static void setup_root(void) {
    uint32_t values[] = {
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
        XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
        XCB_EVENT_MASK_ENTER_WINDOW |
        XCB_EVENT_MASK_LEAVE_WINDOW |
        XCB_EVENT_MASK_STRUCTURE_NOTIFY |
        XCB_EVENT_MASK_PROPERTY_CHANGE
    };

    xcb_void_cookie_t ck = xcb_change_window_attributes_checked(
        wm.conn,
        wm.root,
        XCB_CW_EVENT_MASK,
        values
    );

    xcb_generic_error_t *err = xcb_request_check(wm.conn, ck);
    if (err) {
        free(err);
        die("another window manager is already running");
    }
}

static uint32_t border_width_for_client(Client *c) {
    if (!c || c->is_fullscreen) {
        return 0;
    }
    return (uint32_t)MAX(0, wm.config.border_width);
}

static void configure_client(Client *c, Rect r) {
    if (!c) {
        return;
    }

    c->frame = r;

    int bw = (int)border_width_for_client(c);
    int w = MAX(1, r.w - (bw * 2));
    int h = MAX(1, r.h - (bw * 2));

    uint32_t vals[] = {
        (uint32_t)r.x,
        (uint32_t)r.y,
        (uint32_t)w,
        (uint32_t)h,
        (uint32_t)bw,
    };

    xcb_configure_window(
        wm.conn,
        c->win,
        XCB_CONFIG_WINDOW_X |
        XCB_CONFIG_WINDOW_Y |
        XCB_CONFIG_WINDOW_WIDTH |
        XCB_CONFIG_WINDOW_HEIGHT |
        XCB_CONFIG_WINDOW_BORDER_WIDTH,
        vals
    );
}

static void center_client_on_monitor(Client *c, Monitor *m) {
    if (!c || !m) {
        return;
    }

    int w = m->work.w * 3 / 4;
    int h = m->work.h * 3 / 4;

    if (w < 480) {
        w = MIN(m->work.w, 480);
    }
    if (h < 320) {
        h = MIN(m->work.h, 320);
    }

    Rect r = {
        .x = m->work.x + (m->work.w - w) / 2,
        .y = m->work.y + (m->work.h - h) / 2,
        .w = w,
        .h = h,
    };

    c->frame = r;
    configure_client(c, r);
}

static Client *first_tiled_client(Workspace *ws) {
    for (Client *c = ws ? ws->clients : NULL; c; c = c->next) {
        if (!c->is_hidden && !c->is_floating && !c->is_fullscreen) {
            return c;
        }
    }
    return NULL;
}

static Client *find_fullscreen_client(Workspace *ws) {
    if (!ws) {
        return NULL;
    }

    for (Client *c = ws->clients; c; c = c->next) {
        if (c->is_fullscreen) {
            return c;
        }
    }
    return NULL;
}

static bool workspace_has_clients(Workspace *ws) {
    return ws && ws->clients;
}

static int visible_tiled_count(Workspace *ws) {
    int count = 0;
    for (Client *c = ws ? ws->clients : NULL; c; c = c->next) {
        if (!c->is_hidden && !c->is_floating && !c->is_fullscreen) {
            count++;
        }
    }
    return count;
}

static void layout_tile(Monitor *m, Workspace *ws) {
    if (!m || !ws) {
        return;
    }

    int count = visible_tiled_count(ws);
    if (count == 0) {
        return;
    }

    int gap = ws->gap_px;
    Rect area = m->work;
    area.x += gap;
    area.y += gap;
    area.w -= gap * 2;
    area.h -= gap * 2;

    if (count == 1) {
        for (Client *c = ws->clients; c; c = c->next) {
            if (!c->is_hidden && !c->is_floating && !c->is_fullscreen) {
                configure_client(c, area);
                xcb_map_window(wm.conn, c->win);
                return;
            }
        }
    }

    int nmaster = MAX(1, ws->nmaster);
    if (nmaster > count) {
        nmaster = count;
    }

    int master_w = (count > nmaster) ? (int)(area.w * ws->mfact) : area.w;
    int stack_w = area.w - master_w - gap;

    int mi = 0;
    int si = 0;
    int master_h_each = nmaster ? (area.h - gap * (nmaster - 1)) / nmaster : area.h;
    int stack_count = count - nmaster;
    int stack_h_each = stack_count > 0 ? (area.h - gap * (stack_count - 1)) / stack_count : area.h;

    for (Client *c = ws->clients; c; c = c->next) {
        if (c->is_hidden || c->is_floating || c->is_fullscreen) {
            continue;
        }

        Rect r;
        if (mi < nmaster) {
            r.x = area.x;
            r.y = area.y + (master_h_each + gap) * mi;
            r.w = master_w;
            r.h = master_h_each;
            mi++;
        } else {
            r.x = area.x + master_w + gap;
            r.y = area.y + (stack_h_each + gap) * si;
            r.w = MAX(0, stack_w);
            r.h = stack_h_each;
            si++;
        }
        configure_client(c, r);
        xcb_map_window(wm.conn, c->win);
    }
}

static void layout_monocle(Monitor *m, Workspace *ws) {
    if (!m || !ws) {
        return;
    }

    int gap = ws->gap_px;
    Rect r = m->work;
    r.x += gap;
    r.y += gap;
    r.w -= gap * 2;
    r.h -= gap * 2;

    for (Client *c = ws->clients; c; c = c->next) {
        if (c->is_hidden || c->is_floating || c->is_fullscreen) {
            continue;
        }
        configure_client(c, r);
        xcb_map_window(wm.conn, c->win);
    }
}

static void get_client_title(Client *c, char *buf, size_t buflen) {
    if (!buf || buflen == 0) {
        return;
    }

    buf[0] = '\0';

    if (!c) {
        return;
    }

    if (get_text_property_utf8(c->win, wm.net_wm_name, buf, buflen)) {
        return;
    }

    if (get_text_property_legacy(c->win, buf, buflen)) {
        return;
    }

    snprintf(buf, buflen, "untitled");
}

static void update_monitor_workarea(Monitor *m) {
    if (!m) {
        return;
    }

    Workspace *ws = ws_of(m, m->current_ws);
    bool hide_bar = ws ? ws->hide_bar : false;
    int outer = MAX(0, wm.config.bar_outer_gap);

    if (hide_bar) {
        xcb_unmap_window(wm.conn, m->barwin);
        m->work.x = m->geom.x;
        m->work.y = m->geom.y;
        m->work.w = m->geom.w;
        m->work.h = m->geom.h;
    } else {
        xcb_map_window(wm.conn, m->barwin);
        m->work.x = m->geom.x;
        m->work.y = m->geom.y + outer + wm.config.bar_height;
        m->work.w = m->geom.w;
        m->work.h = MAX(0, m->geom.h - outer - wm.config.bar_height);
    }
}

static void clear_focus_borders_except(Client *keep) {
    uint32_t inactive[] = { wm.config.border_inactive };

    for (Monitor *m = wm.mons; m; m = m->next) {
        for (int i = 0; i < WORKSPACE_COUNT; i++) {
            Workspace *ws = &m->workspaces[i];
            for (Client *c = ws->clients; c; c = c->next) {
                if (c != keep && !c->is_fullscreen) {
                    xcb_change_window_attributes(
                        wm.conn,
                        c->win,
                        XCB_CW_BORDER_PIXEL,
                        inactive
                    );
                }
            }
        }
    }
}

static void update_status_cache(void) {
    char buf[sizeof(wm.status_cache)];
    buf[0] = '\0';

    xcb_icccm_get_text_property_reply_t prop;
    if (xcb_icccm_get_wm_name_reply(
            wm.conn,
            xcb_icccm_get_wm_name(wm.conn, wm.root),
            &prop,
            NULL)) {
        int n = prop.name_len;
        if (n < 0) {
            n = 0;
        }
        if ((size_t)n >= sizeof(buf)) {
            n = (int)sizeof(buf) - 1;
        }
        if (n > 0) {
            memcpy(buf, prop.name, (size_t)n);
        }
        buf[n] = '\0';
        xcb_icccm_get_text_property_reply_wipe(&prop);
    }

    if (buf[0] != '\0') {
        snprintf(wm.status_cache, sizeof(wm.status_cache), "%s", buf);
    }
}

static void get_root_status_text(char *buf, size_t buflen) {
    if (!buf || buflen == 0) {
        return;
    }

    if (wm.status_cache[0] == '\0') {
        update_status_cache();
    }

    snprintf(buf, buflen, "%s", wm.status_cache);
}

static void handle_property_notify(xcb_generic_event_t *gev) {
    xcb_property_notify_event_t *ev = (xcb_property_notify_event_t *)gev;

    if (ev->window == wm.root) {
        update_status_cache();
        draw_all_bars();
        return;
    }

    Client *c = find_client(ev->window);
    if (!c) {
        return;
    }

    Workspace *ws = c->ws;
    if (!ws) {
        return;
    }

    if (ws->focused == c) {
        draw_all_bars();
    }
}

static int bar_text_baseline(void) {
    int baseline = (wm.config.bar_height - wm.font_height) / 2 + wm.font_ascent;
    return MAX(wm.font_ascent, baseline);
}

static int utf8_text_width(const char *s) {
    if (!s || !*s) {
        return 0;
    }

    XGlyphInfo ext;
    XftTextExtentsUtf8(
        wm.dpy,
        wm.xft_font,
        (const FcChar8 *)s,
        (int)strlen(s),
        &ext
    );
    return ext.xOff;
}

static int text_width_px(const char *s) {
    return utf8_text_width(s);
}

static void draw_utf8(XftDraw *draw, XftColor *color, int x, int y, const char *s) {
    if (!draw || !color || !s || !*s) {
        return;
    }

    XftDrawStringUtf8(
        draw,
        color,
        wm.xft_font,
        x,
        y,
        (const FcChar8 *)s,
        (int)strlen(s)
    );
}

static int utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0x00) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static size_t utf8_prev_boundary(const char *s, size_t len) {
    if (!s || len == 0) {
        return 0;
    }

    size_t i = len;
    if (i > 0) {
        i--;
    }

    while (i > 0 && (((unsigned char)s[i] & 0xC0) == 0x80)) {
        i--;
    }

    return i;
}

static void utf8_truncate_to_width(const char *src, int max_width, char *dst, size_t dstsz) {
    if (!dst || dstsz == 0) {
        return;
    }

    dst[0] = '\0';

    if (!src || !*src || max_width <= 0) {
        return;
    }

    if (utf8_text_width(src) <= max_width) {
        snprintf(dst, dstsz, "%s", src);
        return;
    }

    const char *ellipsis = "...";
    const int ellipsis_w = utf8_text_width(ellipsis);
    if (ellipsis_w > max_width) {
        return;
    }

    size_t src_len = strlen(src);
    size_t out_len = 0;

    for (size_t i = 0; i < src_len;) {
        int clen = utf8_char_len((unsigned char)src[i]);
        if (i + (size_t)clen > src_len) {
            clen = 1;
        }

        if (out_len + (size_t)clen >= dstsz) {
            break;
        }

        memcpy(dst + out_len, src + i, (size_t)clen);
        out_len += (size_t)clen;
        dst[out_len] = '\0';

        char probe[512];
        snprintf(probe, sizeof(probe), "%s%s", dst, ellipsis);

        if (utf8_text_width(probe) > max_width) {
            out_len = utf8_prev_boundary(dst, out_len);
            dst[out_len] = '\0';
            break;
        }

        i += (size_t)clen;
    }

    while (out_len > 0) {
        char probe[512];
        snprintf(probe, sizeof(probe), "%s%s", dst, ellipsis);
        if (utf8_text_width(probe) <= max_width) {
            break;
        }
        out_len = utf8_prev_boundary(dst, out_len);
        dst[out_len] = '\0';
    }

    if (dst[0] != '\0') {
        strncat(dst, ellipsis, dstsz - strlen(dst) - 1);
    }
}

static bool get_text_property_utf8(xcb_window_t win, xcb_atom_t prop_atom, char *buf, size_t buflen) {
    if (!buf || buflen == 0 || prop_atom == XCB_ATOM_NONE) {
        return false;
    }

    buf[0] = '\0';

    xcb_get_property_cookie_t ck = xcb_get_property(
        wm.conn,
        0,
        win,
        prop_atom,
        wm.utf8_string != XCB_ATOM_NONE ? wm.utf8_string : XCB_GET_PROPERTY_TYPE_ANY,
        0,
        2048
    );

    xcb_get_property_reply_t *reply = xcb_get_property_reply(wm.conn, ck, NULL);
    if (!reply) {
        return false;
    }

    int len = xcb_get_property_value_length(reply);
    if (len <= 0) {
        free(reply);
        return false;
    }

    if ((size_t)len >= buflen) {
        len = (int)buflen - 1;
    }

    memcpy(buf, xcb_get_property_value(reply), (size_t)len);
    buf[len] = '\0';
    free(reply);

    return buf[0] != '\0';
}

static bool get_text_property_legacy(xcb_window_t win, char *buf, size_t buflen) {
    if (!buf || buflen == 0) {
        return false;
    }

    buf[0] = '\0';

    xcb_icccm_get_text_property_reply_t prop;
    if (!xcb_icccm_get_wm_name_reply(
            wm.conn,
            xcb_icccm_get_wm_name(wm.conn, win),
            &prop,
            NULL)) {
        return false;
    }

    int n = prop.name_len;
    if (n < 0) {
        n = 0;
    }
    if ((size_t)n >= buflen) {
        n = (int)buflen - 1;
    }

    if (n > 0) {
        memcpy(buf, prop.name, (size_t)n);
    }
    buf[n] = '\0';
    xcb_icccm_get_text_property_reply_wipe(&prop);

    return buf[0] != '\0';
}

static void draw_workspace_dots(Monitor *m, XftDraw *draw, int start_x, int baseline, int step_px) {
    int x = start_x;

    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        Workspace *ws = &m->workspaces[i];
        bool current = (i == m->current_ws);
        bool occupied = workspace_has_clients(ws);

        const char *glyph;
        XftColor *color;

        if (current) {
            glyph = "●";
            color = &wm.xft_ws_current;
        } else if (occupied) {
            glyph = "●";
            color = &wm.xft_ws_occupied;
        } else {
            glyph = "○";
            color = &wm.xft_ws_empty;
        }

        draw_utf8(draw, color, x, baseline, glyph);
        x += step_px;
    }
}

static void draw_bar(Monitor *m) {
    if (!m || !m->barwin) {
        return;
    }

    Workspace *ws = ws_of(m, m->current_ws);
    if (!ws || ws->hide_bar) {
        return;
    }

    char raw_title[512];
    char title[512];
    char status[512];
    char monlabel[32];
    char sync_label[4];

    get_client_title(ws->focused, raw_title, sizeof(raw_title));
    get_root_status_text(status, sizeof(status));
    snprintf(monlabel, sizeof(monlabel), "M%d%s", m->id + 1, (m == wm.selmon) ? "*" : "");
    snprintf(sync_label, sizeof(sync_label), "%s", wm.config.sync_workspaces ? "S" : "L");

    xcb_get_geometry_cookie_t gck = xcb_get_geometry(wm.conn, m->barwin);
    xcb_get_geometry_reply_t *geom = xcb_get_geometry_reply(wm.conn, gck, NULL);
    if (!geom) {
        return;
    }

    int bar_w = geom->width;
    int bar_h = geom->height;
    free(geom);

    Pixmap pix = XCreatePixmap(
        wm.dpy,
        (Drawable)m->barwin,
        (unsigned int)bar_w,
        (unsigned int)bar_h,
        (unsigned int)DefaultDepth(wm.dpy, wm.xscreen)
    );

    XftDraw *draw = XftDrawCreate(wm.dpy, pix, wm.visual, wm.colormap);
    if (!draw) {
        XFreePixmap(wm.dpy, pix);
        return;
    }

    GC gc = XCreateGC(wm.dpy, pix, 0, NULL);

    XftDrawRect(draw, &wm.xft_bar_bg, 0, 0, (unsigned int)bar_w, (unsigned int)bar_h);

    const int baseline = bar_text_baseline();
    const int left_pad = 8;
    const int right_pad = 8;
    const int section_gap = 10;
    const int item_gap = 10;

    int dot_step = 16;
    if (bar_w < 700) dot_step = 14;
    if (bar_w < 520) dot_step = 12;
    if (bar_w < 380) dot_step = 10;

    int dots_w = WORKSPACE_COUNT * dot_step;
    int mon_w = text_width_px(monlabel);
    int sync_w = text_width_px(sync_label);
    int status_w = text_width_px(status);

    bool show_mon = true;
    bool show_sync = true;
    bool show_status = true;
    bool show_title = true;

    int left_cluster_w = left_pad + dots_w;
    int extra_left = 0;

    if (show_sync) {
        extra_left += item_gap + sync_w;
    }
    if (show_mon) {
        extra_left += item_gap + mon_w;
    }

    left_cluster_w += extra_left;

    int title_start = left_cluster_w + section_gap;
    int right_reserved = right_pad + (show_status && status[0] ? status_w + section_gap : 0);
    int title_space = bar_w - title_start - right_reserved;

    if (title_space < 120) {
        show_title = false;
    }

    if (bar_w < 520) {
        show_status = false;
        right_reserved = right_pad;
        title_space = bar_w - title_start - right_reserved;
        if (title_space < 120) {
            show_title = false;
        }
    }

    if (bar_w < 420) {
        show_mon = false;
        left_cluster_w = left_pad + dots_w + (show_sync ? item_gap + sync_w : 0);
        title_start = left_cluster_w + section_gap;
        right_reserved = right_pad + (show_status && status[0] ? status_w + section_gap : 0);
        title_space = bar_w - title_start - right_reserved;
        if (title_space < 120) {
            show_title = false;
        }
    }

    if (bar_w < 320) {
        show_sync = false;
        left_cluster_w = left_pad + dots_w;
        title_start = left_cluster_w + section_gap;
        right_reserved = right_pad + (show_status && status[0] ? status_w + section_gap : 0);
        title_space = bar_w - title_start - right_reserved;
        show_title = false;
        show_status = false;
    }

    title[0] = '\0';
    if (show_title && title_space > 24) {
        utf8_truncate_to_width(raw_title, title_space, title, sizeof(title));
    }

    int x = left_pad;

    if (show_mon) {
        draw_utf8(draw, &wm.xft_bar_fg, x, baseline, monlabel);
        x += mon_w + item_gap;
    }

    if (show_sync) {
        draw_utf8(draw, &wm.xft_bar_fg, x, baseline, sync_label);
        x += sync_w + item_gap;
    }

    draw_workspace_dots(m, draw, x, baseline, dot_step);

    if (show_title && title[0]) {
        draw_utf8(draw, &wm.xft_bar_fg, title_start, baseline, title);
    }

    if (show_status && status[0]) {
        int status_x = bar_w - right_pad - status_w;
        if (status_x > title_start + 20) {
            draw_utf8(draw, &wm.xft_bar_fg, status_x, baseline, status);
        }
    }

    XCopyArea(
        wm.dpy,
        pix,
        (Drawable)m->barwin,
        gc,
        0,
        0,
        (unsigned int)bar_w,
        (unsigned int)bar_h,
        0,
        0
    );

    XFreeGC(wm.dpy, gc);
    XftDrawDestroy(draw);
    XFreePixmap(wm.dpy, pix);
    XFlush(wm.dpy);
}

static void draw_all_bars(void) {
    for (Monitor *m = wm.mons; m; m = m->next) {
        draw_bar(m);
    }
}

static void handle_expose(xcb_generic_event_t *gev) {
    xcb_expose_event_t *ev = (xcb_expose_event_t *)gev;

    for (Monitor *m = wm.mons; m; m = m->next) {
        if (ev->window == m->barwin) {
            draw_bar(m);
            return;
        }
    }
}

static void layout_monitor(Monitor *m) {
    if (!m) {
        return;
    }

    Workspace *ws = ws_of(m, m->current_ws);
    if (!ws) {
        return;
    }

    Client *fullscreen = find_fullscreen_client(ws);
    ws->hide_bar = (fullscreen != NULL);

    update_monitor_workarea(m);

    uint32_t inactive[] = { wm.config.border_inactive };

    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        Workspace *other = &m->workspaces[i];

        if (other == ws) {
            continue;
        }

        for (Client *c = other->clients; c; c = c->next) {
            c->is_hidden = true;
            if (!c->is_fullscreen) {
                xcb_change_window_attributes(wm.conn, c->win, XCB_CW_BORDER_PIXEL, inactive);
            }
            xcb_unmap_window(wm.conn, c->win);
        }
    }

    if (fullscreen) {
        for (Client *c = ws->clients; c; c = c->next) {
            if (c == fullscreen) {
                c->is_hidden = false;
                configure_client(c, m->work);
                xcb_map_window(wm.conn, c->win);
            } else {
                c->is_hidden = true;
                xcb_unmap_window(wm.conn, c->win);
            }
        }

        if (m == wm.selmon) {
            focus_workspace(m);
        } else {
            ws->focused = fullscreen;
            m->focused = fullscreen;
            draw_bar(m);
        }
        return;
    }

    for (Client *c = ws->clients; c; c = c->next) {
        if (c->is_scratchpad) {
            if (!c->is_hidden) {
                c->is_floating = true;
                center_client_on_monitor(c, m);
                xcb_map_window(wm.conn, c->win);
            }
            continue;
        }

        c->is_hidden = false;
    }

    switch (ws->layout) {
        case LAYOUT_TILE:
            layout_tile(m, ws);
            break;

        case LAYOUT_MONOCLE:
            layout_monocle(m, ws);
            break;

        case LAYOUT_FLOAT:
            break;
    }

    for (Client *c = ws->clients; c; c = c->next) {
        if (c->is_scratchpad) {
            continue;
        }

        if (!c->is_hidden && c->is_floating) {
            if (c->frame.w <= 0 || c->frame.h <= 0) {
                center_client_on_monitor(c, m);
            } else {
                configure_client(c, c->frame);
            }
            xcb_map_window(wm.conn, c->win);
        }
    }

    if (m == wm.selmon) {
        focus_workspace(m);
    } else {
        Client *c = ws->focused;
        if (!c || c->is_hidden) {
            c = ws->clients;
            while (c && c->is_hidden) {
                c = c->next;
            }
            ws->focused = c;
        }
        m->focused = ws->focused;
        draw_bar(m);
    }
}

static void focus_client(Client *c) {
    if (!c || !c->mon || c->is_hidden) {
        return;
    }

    Workspace *ws = c->ws;
    if (!ws) {
        return;
    }

    uint32_t inactive[] = { wm.config.border_inactive };
    uint32_t active[] = { wm.config.border_active };

    for (Monitor *m = wm.mons; m; m = m->next) {
        for (int i = 0; i < WORKSPACE_COUNT; i++) {
            for (Client *it = m->workspaces[i].clients; it; it = it->next) {
                if (it != c && !it->is_fullscreen) {
                    xcb_change_window_attributes(
                        wm.conn,
                        it->win,
                        XCB_CW_BORDER_PIXEL,
                        inactive
                    );
                }
            }
        }
    }

    wm.selmon = c->mon;
    c->mon->focused = c;
    ws->focused = c;

    if (!c->is_fullscreen) {
        xcb_change_window_attributes(wm.conn, c->win, XCB_CW_BORDER_PIXEL, active);
    }
    xcb_set_input_focus(wm.conn, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);

    if (wm.net_active_window != XCB_ATOM_NONE) {
        xcb_change_property(
            wm.conn,
            XCB_PROP_MODE_REPLACE,
            wm.root,
            wm.net_active_window,
            XCB_ATOM_WINDOW,
            32,
            1,
            &c->win
        );
    }

    draw_all_bars();
}

static void attach_client(Workspace *ws, Client *c) {
    if (!ws || !c) {
        return;
    }

    if (!ws->clients) {
        c->next = NULL;
        c->prev = NULL;
        ws->clients = c;
        return;
    }

    Client *tail = ws->clients;
    while (tail->next) {
        tail = tail->next;
    }

    c->next = NULL;
    c->prev = tail;
    tail->next = c;
}

static void attach_client_head(Workspace *ws, Client *c) {
    if (!ws || !c) {
        return;
    }

    c->next = ws->clients;
    c->prev = NULL;
    if (ws->clients) {
        ws->clients->prev = c;
    }
    ws->clients = c;
}

static void manage_window(xcb_window_t win) {
    if (!wm.selmon) {
        return;
    }

    for (Monitor *m = wm.mons; m; m = m->next) {
        if (win == m->barwin) {
            return;
        }
    }

    if (find_client(win)) {
        return;
    }

    xcb_get_window_attributes_cookie_t attr_cookie =
        xcb_get_window_attributes(wm.conn, win);
    xcb_get_window_attributes_reply_t *attr =
        xcb_get_window_attributes_reply(wm.conn, attr_cookie, NULL);

    if (!attr) {
        return;
    }

    if (attr->override_redirect) {
        free(attr);
        return;
    }

    free(attr);

    Client *c = calloc(1, sizeof(*c));
    if (!c) {
        die("calloc client failed");
    }

    c->win = win;
    c->mon = wm.selmon;
    c->ws = ws_of(wm.selmon, wm.selmon->current_ws);
    c->frame = (Rect){ .x = 0, .y = 0, .w = 0, .h = 0 };
    c->old_frame = c->frame;

    if (wm.scratchpad_spawn_pending) {
        c->is_scratchpad = true;
        c->is_floating = true;
        c->is_hidden = false;
        wm.scratchpad = c;
        wm.scratchpad_spawn_pending = false;
    }

    uint32_t values[] = {
        wm.config.border_inactive,
        XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE,
    };

    xcb_change_window_attributes(
        wm.conn,
        win,
        XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK,
        values
    );

    attach_client(c->ws, c);

    if (c->is_scratchpad || c->is_floating) {
        center_client_on_monitor(c, c->mon);
    }

    xcb_map_window(wm.conn, win);
    layout_monitor(c->mon);
    focus_client(c);
}

static void unmanage_client(Client *c) {
    if (!c || !c->ws) {
        return;
    }

    Workspace *ws = c->ws;
    Monitor *m = c->mon;

    if (c->prev) {
        c->prev->next = c->next;
    } else {
        ws->clients = c->next;
    }

    if (c->next) {
        c->next->prev = c->prev;
    }

    if (ws->focused == c) {
        ws->focused = NULL;
    }

    if (m && m->focused == c) {
        m->focused = NULL;
    }

    if (wm.scratchpad == c) {
        wm.scratchpad = NULL;
        wm.scratchpad_spawn_pending = false;
    }

    free(c);

    if (m) {
        layout_monitor(m);
    }
}

static Client *find_client(xcb_window_t win) {
    for (Monitor *m = wm.mons; m; m = m->next) {
        for (int i = 0; i < WORKSPACE_COUNT; i++) {
            for (Client *c = m->workspaces[i].clients; c; c = c->next) {
                if (c->win == win) {
                    return c;
                }
            }
        }
    }
    return NULL;
}

static void scan_existing_windows(void) {
    xcb_query_tree_cookie_t ck = xcb_query_tree(wm.conn, wm.root);
    xcb_query_tree_reply_t *reply = xcb_query_tree_reply(wm.conn, ck, NULL);
    if (!reply) {
        return;
    }

    int len = xcb_query_tree_children_length(reply);
    xcb_window_t *wins = xcb_query_tree_children(reply);

    for (int i = 0; i < len; i++) {
        bool skip = false;

        for (Monitor *m = wm.mons; m; m = m->next) {
            if (wins[i] == m->barwin) {
                skip = true;
                break;
            }
        }

        if (skip) {
            continue;
        }

        xcb_get_window_attributes_cookie_t attr_cookie = xcb_get_window_attributes(wm.conn, wins[i]);
        xcb_get_window_attributes_reply_t *attr = xcb_get_window_attributes_reply(wm.conn, attr_cookie, NULL);

        if (!attr) {
            continue;
        }

        bool should_manage = !attr->override_redirect && attr->map_state == XCB_MAP_STATE_VIEWABLE;

        free(attr);

        if (should_manage) {
            manage_window(wins[i]);
        }
    }

    free(reply);
}

static void spawn(const void *arg) {
    const char *const *cmd = arg;
    if (!cmd || !cmd[0]) {
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        setsid();

        if (wm.conn) {
            close(xcb_get_file_descriptor(wm.conn));
        }

        execvp(cmd[0], (char *const *)cmd);
        fprintf(stderr, "execvp failed for %s: %s\n", cmd[0], strerror(errno));
        _exit(1);
    }
}

static void quit(const void *arg) {
    (void)arg;
    wm.running = false;
}

static void focus_next(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }
    Workspace *ws = ws_of(wm.selmon, wm.selmon->current_ws);
    if (!ws || !ws->clients) {
        return;
    }
    Client *c = ws->focused && ws->focused->next ? ws->focused->next : ws->clients;
    while (c && c->is_hidden) {
        c = c->next ? c->next : ws->clients;
        if (c == ws->focused) {
            break;
        }
    }
    if (c && !c->is_hidden) {
        focus_client(c);
    }
}

static void focus_prev(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }
    Workspace *ws = ws_of(wm.selmon, wm.selmon->current_ws);
    if (!ws || !ws->clients) {
        return;
    }

    Client *last = ws->clients;
    while (last->next) {
        last = last->next;
    }

    Client *c = ws->focused && ws->focused->prev ? ws->focused->prev : last;
    while (c && c->is_hidden) {
        c = c->prev ? c->prev : last;
        if (c == ws->focused) {
            break;
        }
    }
    if (c && !c->is_hidden) {
        focus_client(c);
    }
}

static Monitor *next_monitor(Monitor *m) {
    if (!m) {
        return wm.mons;
    }
    return m->next ? m->next : wm.mons;
}

static Monitor *prev_monitor(Monitor *m) {
    if (!m || m == wm.mons) {
        Monitor *tail = wm.mons;
        if (!tail) {
            return NULL;
        }
        while (tail->next) {
            tail = tail->next;
        }
        return tail;
    }

    Monitor *cur = wm.mons;
    while (cur && cur->next != m) {
        cur = cur->next;
    }
    return cur;
}

static void focus_monitor_next(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }

    wm.selmon = next_monitor(wm.selmon);
    Workspace *ws = ws_of(wm.selmon, wm.selmon->current_ws);

    if (!ws || !ws->focused) {
        clear_focus_borders_except(NULL);
        wm.selmon->focused = NULL;
        draw_all_bars();
        return;
    }

    focus_workspace(wm.selmon);
    draw_all_bars();
}

static void focus_monitor_prev(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }

    wm.selmon = prev_monitor(wm.selmon);
    Workspace *ws = ws_of(wm.selmon, wm.selmon->current_ws);

    if (!ws || !ws->focused) {
        clear_focus_borders_except(NULL);
        wm.selmon->focused = NULL;
        draw_all_bars();
        return;
    }

    focus_workspace(wm.selmon);
    draw_all_bars();
}

static void focus_workspace(Monitor *m) {
    if (!m) {
        return;
    }

    Workspace *ws = ws_of(m, m->current_ws);
    if (!ws) {
        return;
    }

    Client *c = ws->focused;

    if (!c || c->is_hidden) {
        c = ws->clients;
        while (c && c->is_hidden) {
            c = c->next;
        }
    }

    if (!c) {
        m->focused = NULL;
        draw_all_bars();
        return;
    }

    ws->focused = c;
    focus_client(c);
}

static void detach_from_workspace(Client *c) {
    if (!c || !c->ws) {
        return;
    }
    Workspace *ws = c->ws;
    if (c->prev) {
        c->prev->next = c->next;
    } else {
        ws->clients = c->next;
    }
    if (c->next) {
        c->next->prev = c->prev;
    }
    if (ws->focused == c) {
        ws->focused = ws->clients;
    }
    c->next = NULL;
    c->prev = NULL;
}

static void move_client_to_monitor(Client *c, Monitor *dst) {
    if (!c || !dst || c->mon == dst) {
        return;
    }

    Monitor *old_mon = c->mon;

    detach_from_workspace(c);

    c->mon = dst;
    c->ws = ws_of(dst, dst->current_ws);
    attach_client(c->ws, c);

    if (c->is_floating || c->is_scratchpad) {
        center_client_on_monitor(c, dst);
    }

    layout_monitor(old_mon);
    layout_monitor(dst);
    focus_client(c);
}

static void send_to_monitor_next(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }
    Workspace *ws = ws_of(wm.selmon, wm.selmon->current_ws);
    if (!ws || !ws->focused) {
        return;
    }
    move_client_to_monitor(ws->focused, next_monitor(wm.selmon));
}

static void send_to_monitor_prev(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }
    Workspace *ws = ws_of(wm.selmon, wm.selmon->current_ws);
    if (!ws || !ws->focused) {
        return;
    }
    move_client_to_monitor(ws->focused, prev_monitor(wm.selmon));
}

static void set_monitor_workspace(Monitor *m, int idx) {
    if (!m || idx < 0 || idx >= WORKSPACE_COUNT || m->current_ws == idx) {
        return;
    }
    m->previous_ws = m->current_ws;
    m->current_ws = idx;
    layout_monitor(m);
}

static void view_workspace(const void *arg) {
    const WorkspaceArg *wa = arg;
    if (!wa || !wm.selmon) {
        return;
    }

    if (wm.config.sync_workspaces) {
        for (Monitor *m = wm.mons; m; m = m->next) {
            set_monitor_workspace(m, wa->workspace);
        }
    } else {
        set_monitor_workspace(wm.selmon, wa->workspace);
    }

    draw_all_bars();
}

static void send_to_workspace(const void *arg) {
    const WorkspaceArg *wa = arg;

    if (!wa || !wm.selmon) {
        return;
    }

    Workspace *cur = ws_of(wm.selmon, wm.selmon->current_ws);
    if (!cur || !cur->focused) {
        return;
    }

    Client *c = cur->focused;

    if (wa->workspace == wm.selmon->current_ws || c->is_scratchpad) {
        return;
    }

    detach_from_workspace(c);

    Workspace *dst = ws_of(wm.selmon, wa->workspace);
    c->ws = dst;
    attach_client(dst, c);

    layout_monitor(wm.selmon);
    focus_workspace(wm.selmon);
}

static void toggle_sync_workspaces(const void *arg) {
    (void)arg;
    wm.config.sync_workspaces = !wm.config.sync_workspaces;
    draw_all_bars();
}

static void set_client_fullscreen_state(Client *c, bool enabled) {
    if (!c || wm.net_wm_state == XCB_ATOM_NONE || wm.net_wm_state_fullscreen == XCB_ATOM_NONE) {
        return;
    }

    if (enabled) {
        xcb_atom_t value = wm.net_wm_state_fullscreen;
        xcb_change_property(
            wm.conn,
            XCB_PROP_MODE_REPLACE,
            c->win,
            wm.net_wm_state,
            XCB_ATOM_ATOM,
            32,
            1,
            &value
        );
    } else {
        xcb_delete_property(wm.conn, c->win, wm.net_wm_state);
    }
}

static void toggle_fullscreen(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }

    Workspace *ws = ws_of(wm.selmon, wm.selmon->current_ws);
    if (!ws || !ws->focused) {
        return;
    }

    Client *c = ws->focused;

    if (!c->is_fullscreen) {
        for (Client *it = ws->clients; it; it = it->next) {
            if (it != c && it->is_fullscreen) {
                it->is_fullscreen = false;
                set_client_fullscreen_state(it, false);
            }
        }
    }

    c->is_fullscreen = !c->is_fullscreen;
    set_client_fullscreen_state(c, c->is_fullscreen);

    layout_monitor(c->mon);
    focus_client(c);
}

static Client *find_scratchpad_client(void) {
    for (Monitor *m = wm.mons; m; m = m->next) {
        for (int i = 0; i < WORKSPACE_COUNT; i++) {
            for (Client *c = m->workspaces[i].clients; c; c = c->next) {
                if (c->is_scratchpad) {
                    return c;
                }
            }
        }
    }
    return NULL;
}

static void toggle_scratchpad(const void *arg) {
    (void)arg;

    if (!wm.selmon) {
        return;
    }

    if (!wm.scratchpad) {
        wm.scratchpad = find_scratchpad_client();
    }

    if (wm.scratchpad_spawn_pending) {
        return;
    }

    if (!wm.scratchpad) {
        wm.scratchpad_spawn_pending = true;
        spawn(wm.config.scratchpad_cmd);
        return;
    }

    Client *c = wm.scratchpad;
    Workspace *target_ws = ws_of(wm.selmon, wm.selmon->current_ws);

    if (!c->is_hidden && c->mon == wm.selmon) {
        c->is_hidden = true;
        xcb_unmap_window(wm.conn, c->win);

        if (c->ws && c->ws->focused == c) {
            c->ws->focused = NULL;
        }
        if (c->mon && c->mon->focused == c) {
            c->mon->focused = NULL;
        }

        layout_monitor(wm.selmon);
        return;
    }

    if (c->ws != target_ws) {
        detach_from_workspace(c);
        c->ws = target_ws;
        attach_client(target_ws, c);
    }

    c->mon = wm.selmon;
    c->is_hidden = false;
    c->is_floating = true;

    center_client_on_monitor(c, wm.selmon);
    xcb_map_window(wm.conn, c->win);
    layout_monitor(wm.selmon);
    focus_client(c);
}

static void rebuild_config_commands(void) {
    memset(wm.config.term_cmd, 0, sizeof(wm.config.term_cmd));
    memset(wm.config.launcher_cmd, 0, sizeof(wm.config.launcher_cmd));
    memset(wm.config.scratchpad_cmd, 0, sizeof(wm.config.scratchpad_cmd));

    wm.config.term_cmd[0] = wm.config.terminal_arg0;
    wm.config.term_cmd[1] = NULL;

    wm.config.launcher_cmd[0] = wm.config.launcher_arg0;
    wm.config.launcher_cmd[1] = NULL;

    wm.config.scratchpad_cmd[0] = wm.config.scratchpad_arg0;
    wm.config.scratchpad_cmd[1] = wm.config.scratchpad_arg1;
    wm.config.scratchpad_cmd[2] = wm.config.scratchpad_arg2;
    wm.config.scratchpad_cmd[3] = NULL;
}

static void init_default_keybinds(void) {
    Keybind defaults[] = {
        { XK_Return, MOD_MASK, ACTION_SPAWN_TERM },
        { XK_Return, MOD_MASK | XCB_MOD_MASK_SHIFT, ACTION_ZOOM_MASTER },
        { XK_p, MOD_MASK, ACTION_SPAWN_LAUNCHER },
        { XK_grave, MOD_MASK, ACTION_TOGGLE_SCRATCHPAD },

        { XK_j, MOD_MASK, ACTION_FOCUS_NEXT },
        { XK_k, MOD_MASK, ACTION_FOCUS_PREV },
        { XK_h, MOD_MASK, ACTION_FOCUS_MONITOR_PREV },
        { XK_l, MOD_MASK, ACTION_FOCUS_MONITOR_NEXT },

        { XK_h, MOD_MASK | XCB_MOD_MASK_SHIFT, ACTION_SEND_MONITOR_PREV },
        { XK_l, MOD_MASK | XCB_MOD_MASK_SHIFT, ACTION_SEND_MONITOR_NEXT },

        { XK_bracketleft, MOD_MASK, ACTION_DECREASE_MFACT },
        { XK_bracketright, MOD_MASK, ACTION_INCREASE_MFACT },

        { XK_f, MOD_MASK, ACTION_TOGGLE_FULLSCREEN },
        { XK_s, MOD_MASK, ACTION_TOGGLE_SYNC },
        { XK_q, MOD_MASK, ACTION_KILL_CLIENT },
        { XK_q, MOD_MASK | XCB_MOD_MASK_SHIFT, ACTION_QUIT },
        { XK_r, MOD_MASK | XCB_MOD_MASK_SHIFT, ACTION_RELOAD_CONFIG },

        { XK_1, MOD_MASK, ACTION_VIEW_WS_1 },
        { XK_2, MOD_MASK, ACTION_VIEW_WS_2 },
        { XK_3, MOD_MASK, ACTION_VIEW_WS_3 },
        { XK_4, MOD_MASK, ACTION_VIEW_WS_4 },
        { XK_5, MOD_MASK, ACTION_VIEW_WS_5 },
        { XK_6, MOD_MASK, ACTION_VIEW_WS_6 },
        { XK_7, MOD_MASK, ACTION_VIEW_WS_7 },
        { XK_8, MOD_MASK, ACTION_VIEW_WS_8 },
        { XK_9, MOD_MASK, ACTION_VIEW_WS_9 },

        { XK_1, MOD_MASK | XCB_MOD_MASK_SHIFT, ACTION_SEND_WS_1 },
        { XK_2, MOD_MASK | XCB_MOD_MASK_SHIFT, ACTION_SEND_WS_2 },
        { XK_3, MOD_MASK | XCB_MOD_MASK_SHIFT, ACTION_SEND_WS_3 },
        { XK_4, MOD_MASK | XCB_MOD_MASK_SHIFT, ACTION_SEND_WS_4 },
        { XK_5, MOD_MASK | XCB_MOD_MASK_SHIFT, ACTION_SEND_WS_5 },
        { XK_6, MOD_MASK | XCB_MOD_MASK_SHIFT, ACTION_SEND_WS_6 },
        { XK_7, MOD_MASK | XCB_MOD_MASK_SHIFT, ACTION_SEND_WS_7 },
        { XK_8, MOD_MASK | XCB_MOD_MASK_SHIFT, ACTION_SEND_WS_8 },
        { XK_9, MOD_MASK | XCB_MOD_MASK_SHIFT, ACTION_SEND_WS_9 },
    };

    wm.config.keybind_count = LENGTH(defaults);
    for (size_t i = 0; i < wm.config.keybind_count; i++) {
        wm.config.keybinds[i] = defaults[i];
    }
}

static char *trim_whitespace(char *s) {
    if (!s) {
        return s;
    }

    while (*s && isspace((unsigned char)*s)) {
        s++;
    }

    if (*s == '\0') {
        return s;
    }

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return s;
}

static void strip_comment(char *s) {
    if (!s) {
        return;
    }

    bool in_string = false;
    for (char *p = s; *p; p++) {
        if (*p == '"' && (p == s || p[-1] != '\\')) {
            in_string = !in_string;
            continue;
        }
        if (!in_string && *p == '#') {
            *p = '\0';
            return;
        }
    }
}

static bool parse_bool_value(const char *s, bool *out) {
    if (!s || !out) {
        return false;
    }

    if (strcmp(s, "true") == 0) {
        *out = true;
        return true;
    }

    if (strcmp(s, "false") == 0) {
        *out = false;
        return true;
    }

    return false;
}

static bool parse_color_value(const char *s, uint32_t *out) {
    if (!s || !out) {
        return false;
    }

    char *end = NULL;
    unsigned long v = strtoul(s, &end, 0);
    if (end == s || *end != '\0') {
        return false;
    }
    *out = (uint32_t)(v & 0x00ffffffu);
    return true;
}

static void toml_unquote_inplace(char *s) {
    if (!s) {
        return;
    }

    size_t len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
    }
}

static void sanitize_config(void) {
    wm.config.border_width = MAX(0, wm.config.border_width);
    wm.config.gap_px = MAX(0, wm.config.gap_px);
    wm.config.bar_outer_gap = MAX(0, wm.config.bar_outer_gap);
    wm.config.default_mfact = CLAMP(wm.config.default_mfact, 0.05f, 0.95f);
    wm.config.font_size = wm.config.font_size > 0.0f ? wm.config.font_size : 11.0f;

    if (wm.font_height > 0) {
        wm.config.bar_height = MAX(wm.config.bar_height, wm.font_height + 8);
    } else {
        wm.config.bar_height = MAX(wm.config.bar_height, 18);
    }
}

static void load_config_file(const char *path) {
    if (!path || path[0] == '\0') {
        return;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        return;
    }

    char line[1024];

    while (fgets(line, sizeof(line), fp)) {
        strip_comment(line);

        char *raw = trim_whitespace(line);
        if (*raw == '\0') {
            continue;
        }

        char *eq = strchr(raw, '=');
        if (!eq) {
            continue;
        }

        *eq = '\0';
        char *key = trim_whitespace(raw);
        char *val = trim_whitespace(eq + 1);

        if (*key == '\0' || *val == '\0') {
            continue;
        }

        if (strcmp(key, "terminal") == 0) {
            toml_unquote_inplace(val);
            snprintf(wm.config.terminal, sizeof(wm.config.terminal), "%s", val);
            snprintf(wm.config.terminal_arg0, sizeof(wm.config.terminal_arg0), "%s", val);
            continue;
        }

        if (strcmp(key, "launcher") == 0) {
            toml_unquote_inplace(val);
            snprintf(wm.config.launcher, sizeof(wm.config.launcher), "%s", val);
            snprintf(wm.config.launcher_arg0, sizeof(wm.config.launcher_arg0), "%s", val);
            continue;
        }

        if (strcmp(key, "scratchpad") == 0) {
            toml_unquote_inplace(val);
            snprintf(wm.config.scratchpad, sizeof(wm.config.scratchpad), "%s", val);
            snprintf(wm.config.scratchpad_arg0, sizeof(wm.config.scratchpad_arg0), "%s", val);
            continue;
        }

        if (strcmp(key, "scratchpad_class") == 0) {
            toml_unquote_inplace(val);
            snprintf(wm.config.scratchpad_arg2, sizeof(wm.config.scratchpad_arg2), "%s", val);
            continue;
        }

        if (strcmp(key, "font") == 0) {
            toml_unquote_inplace(val);
            snprintf(wm.config.font_family, sizeof(wm.config.font_family), "%s", val);
            continue;
        }

        if (strcmp(key, "gap_px") == 0) {
            wm.config.gap_px = atoi(val);
            continue;
        }

        if (strcmp(key, "bar_height") == 0) {
            wm.config.bar_height = atoi(val);
            continue;
        }

        if (strcmp(key, "border_width") == 0) {
            wm.config.border_width = atoi(val);
            continue;
        }

        if (strcmp(key, "default_mfact") == 0) {
            wm.config.default_mfact = strtof(val, NULL);
            continue;
        }

        if (strcmp(key, "sync_workspaces") == 0) {
            bool b = false;
            if (parse_bool_value(val, &b)) {
                wm.config.sync_workspaces = b;
            }
            continue;
        }

        if (strcmp(key, "bar_bg") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                wm.config.bar_bg = c;
            }
            continue;
        }

        if (strcmp(key, "bar_fg") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                wm.config.bar_fg = c;
            }
            continue;
        }

        if (strcmp(key, "border_active") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                wm.config.border_active = c;
            }
            continue;
        }

        if (strcmp(key, "border_inactive") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                wm.config.border_inactive = c;
            }
            continue;
        }

        if (strcmp(key, "font_family") == 0) {
            toml_unquote_inplace(val);
            snprintf(wm.config.font_family, sizeof(wm.config.font_family), "%s", val);
            continue;
        }

        if (strcmp(key, "font_size") == 0) {
            wm.config.font_size = strtof(val, NULL);
            continue;
        }

        if (strcmp(key, "bar_outer_gap") == 0) {
            wm.config.bar_outer_gap = atoi(val);
            continue;
        }

        if (strcmp(key, "workspace_current") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                wm.config.workspace_current = c;
            }
            continue;
        }

        if (strcmp(key, "workspace_occupied") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                wm.config.workspace_occupied = c;
            }
            continue;
        }

        if (strcmp(key, "workspace_empty") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                wm.config.workspace_empty = c;
            }
            continue;
        }
    }

    fclose(fp);
    rebuild_config_commands();
}

static void load_default_config(void) {
    memset(&wm.config, 0, sizeof(wm.config));

    snprintf(
        wm.config.path,
        sizeof(wm.config.path),
        "%s/.config/vwm/config.toml",
        getenv("HOME") ? getenv("HOME") : ""
    );

    wm.config.border_width = BORDER_WIDTH;
    wm.config.gap_px = GAP_PX;
    wm.config.bar_height = BAR_HEIGHT;
    wm.config.bar_outer_gap = 0;
    wm.config.default_mfact = 0.5f;
    wm.config.font_size = 11.0f;
    wm.config.sync_workspaces = true;

    wm.config.bar_bg = 0x111111;
    wm.config.bar_fg = 0xd0d0d0;
    wm.config.border_active = 0xff8800;
    wm.config.border_inactive = 0x444444;
    wm.config.workspace_current = 0xff8800;
    wm.config.workspace_occupied = 0x8c8c8c;
    wm.config.workspace_empty = 0x4a4a4a;

    snprintf(wm.config.font_family, sizeof(wm.config.font_family), "monospace");
    snprintf(wm.config.terminal, sizeof(wm.config.terminal), "kitty");
    snprintf(wm.config.launcher, sizeof(wm.config.launcher), "dmenu_run");
    snprintf(wm.config.scratchpad, sizeof(wm.config.scratchpad), "kitty");

    snprintf(wm.config.terminal_arg0, sizeof(wm.config.terminal_arg0), "%s", wm.config.terminal);
    snprintf(wm.config.launcher_arg0, sizeof(wm.config.launcher_arg0), "%s", wm.config.launcher);
    snprintf(wm.config.scratchpad_arg0, sizeof(wm.config.scratchpad_arg0), "%s", wm.config.scratchpad);
    snprintf(wm.config.scratchpad_arg1, sizeof(wm.config.scratchpad_arg1), "--class");
    snprintf(wm.config.scratchpad_arg2, sizeof(wm.config.scratchpad_arg2), "vwm-scratchpad");

    rebuild_config_commands();
    init_default_keybinds();
    load_config_file(wm.config.path);
}

static void apply_config(void) {
    open_font_from_config();
    sanitize_config();

    for (Monitor *m = wm.mons; m; m = m->next) {
        int outer = MAX(0, wm.config.bar_outer_gap);
        uint32_t vals[] = {
            (uint32_t)(m->geom.x + outer),
            (uint32_t)(m->geom.y + outer),
            (uint32_t)MAX(1, m->geom.w - outer * 2),
            (uint32_t)wm.config.bar_height
        };

        xcb_change_window_attributes(
            wm.conn,
            m->barwin,
            XCB_CW_BACK_PIXEL,
            &wm.config.bar_bg
        );

        xcb_configure_window(
            wm.conn,
            m->barwin,
            XCB_CONFIG_WINDOW_X |
            XCB_CONFIG_WINDOW_Y |
            XCB_CONFIG_WINDOW_WIDTH |
            XCB_CONFIG_WINDOW_HEIGHT,
            vals
        );

        for (int i = 0; i < WORKSPACE_COUNT; i++) {
            Workspace *ws = &m->workspaces[i];
            ws->gap_px = wm.config.gap_px;
            ws->mfact = CLAMP(ws->mfact, 0.05f, 0.95f);
            if (ws->mfact <= 0.0f || ws->mfact >= 1.0f) {
                ws->mfact = wm.config.default_mfact;
            }
        }

        update_monitor_workarea(m);
        layout_monitor(m);
    }

    grab_keys();
    draw_all_bars();
}

static void reload_config(const void *arg) {
    (void)arg;

    load_default_config();
    apply_config();

    fprintf(stderr, "vwm: config reloaded from %s\n", wm.config.path);
}

static void dispatch_action(Action action) {
    WorkspaceArg arg = {0};

    switch (action) {
        case ACTION_SPAWN_TERM:
            spawn(wm.config.term_cmd);
            break;
        case ACTION_SPAWN_LAUNCHER:
            spawn(wm.config.launcher_cmd[0] ? wm.config.launcher_cmd : launcher_fallback);
            break;
        case ACTION_TOGGLE_SCRATCHPAD:
            toggle_scratchpad(NULL);
            break;
        case ACTION_FOCUS_NEXT:
            focus_next(NULL);
            break;
        case ACTION_FOCUS_PREV:
            focus_prev(NULL);
            break;
        case ACTION_FOCUS_MONITOR_PREV:
            focus_monitor_prev(NULL);
            break;
        case ACTION_FOCUS_MONITOR_NEXT:
            focus_monitor_next(NULL);
            break;
        case ACTION_SEND_MONITOR_PREV:
            send_to_monitor_prev(NULL);
            break;
        case ACTION_SEND_MONITOR_NEXT:
            send_to_monitor_next(NULL);
            break;
        case ACTION_TOGGLE_FULLSCREEN:
            toggle_fullscreen(NULL);
            break;
        case ACTION_TOGGLE_SYNC:
            toggle_sync_workspaces(NULL);
            break;
        case ACTION_KILL_CLIENT:
            kill_client(NULL);
            break;
        case ACTION_QUIT:
            quit(NULL);
            break;
        case ACTION_RELOAD_CONFIG:
            reload_config(NULL);
            break;
        case ACTION_DECREASE_MFACT:
            decrease_mfact(NULL);
            break;
        case ACTION_INCREASE_MFACT:
            increase_mfact(NULL);
            break;
        case ACTION_ZOOM_MASTER:
            zoom_master(NULL);
            break;

        case ACTION_VIEW_WS_1: arg.workspace = 0; view_workspace(&arg); break;
        case ACTION_VIEW_WS_2: arg.workspace = 1; view_workspace(&arg); break;
        case ACTION_VIEW_WS_3: arg.workspace = 2; view_workspace(&arg); break;
        case ACTION_VIEW_WS_4: arg.workspace = 3; view_workspace(&arg); break;
        case ACTION_VIEW_WS_5: arg.workspace = 4; view_workspace(&arg); break;
        case ACTION_VIEW_WS_6: arg.workspace = 5; view_workspace(&arg); break;
        case ACTION_VIEW_WS_7: arg.workspace = 6; view_workspace(&arg); break;
        case ACTION_VIEW_WS_8: arg.workspace = 7; view_workspace(&arg); break;
        case ACTION_VIEW_WS_9: arg.workspace = 8; view_workspace(&arg); break;

        case ACTION_SEND_WS_1: arg.workspace = 0; send_to_workspace(&arg); break;
        case ACTION_SEND_WS_2: arg.workspace = 1; send_to_workspace(&arg); break;
        case ACTION_SEND_WS_3: arg.workspace = 2; send_to_workspace(&arg); break;
        case ACTION_SEND_WS_4: arg.workspace = 3; send_to_workspace(&arg); break;
        case ACTION_SEND_WS_5: arg.workspace = 4; send_to_workspace(&arg); break;
        case ACTION_SEND_WS_6: arg.workspace = 5; send_to_workspace(&arg); break;
        case ACTION_SEND_WS_7: arg.workspace = 6; send_to_workspace(&arg); break;
        case ACTION_SEND_WS_8: arg.workspace = 7; send_to_workspace(&arg); break;
        case ACTION_SEND_WS_9: arg.workspace = 8; send_to_workspace(&arg); break;

        default:
            break;
    }
}

static bool client_supports_protocol(Client *c, xcb_atom_t protocol) {
    if (!c || protocol == XCB_ATOM_NONE || wm.wm_protocols == XCB_ATOM_NONE) {
        return false;
    }

    xcb_get_property_cookie_t ck = xcb_get_property(
        wm.conn,
        0,
        c->win,
        wm.wm_protocols,
        XCB_ATOM_ATOM,
        0,
        32
    );

    xcb_get_property_reply_t *reply = xcb_get_property_reply(wm.conn, ck, NULL);
    if (!reply) {
        return false;
    }

    bool found = false;
    int len = xcb_get_property_value_length(reply) / (int)sizeof(xcb_atom_t);
    xcb_atom_t *atoms = (xcb_atom_t *)xcb_get_property_value(reply);

    for (int i = 0; i < len; i++) {
        if (atoms[i] == protocol) {
            found = true;
            break;
        }
    }

    free(reply);
    return found;
}

static void send_wm_delete(Client *c) {
    if (!c) {
        return;
    }

    xcb_client_message_event_t ev = {
        .response_type = XCB_CLIENT_MESSAGE,
        .format = 32,
        .window = c->win,
        .type = wm.wm_protocols,
        .data.data32 = {
            wm.wm_delete_window,
            XCB_CURRENT_TIME,
            0,
            0,
            0
        }
    };

    xcb_send_event(wm.conn, 0, c->win, XCB_EVENT_MASK_NO_EVENT, (const char *)&ev);
}

static void kill_client(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }
    Workspace *ws = ws_of(wm.selmon, wm.selmon->current_ws);
    if (!ws || !ws->focused) {
        return;
    }

    Client *c = ws->focused;
    if (client_supports_protocol(c, wm.wm_delete_window)) {
        send_wm_delete(c);
    } else {
        xcb_kill_client(wm.conn, c->win);
    }
}

static void decrease_mfact(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }
    Workspace *ws = ws_of(wm.selmon, wm.selmon->current_ws);
    if (!ws) {
        return;
    }
    ws->mfact = CLAMP(ws->mfact - MFAC_STEP, 0.05f, 0.95f);
    layout_monitor(wm.selmon);
}

static void increase_mfact(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }
    Workspace *ws = ws_of(wm.selmon, wm.selmon->current_ws);
    if (!ws) {
        return;
    }
    ws->mfact = CLAMP(ws->mfact + MFAC_STEP, 0.05f, 0.95f);
    layout_monitor(wm.selmon);
}

static void zoom_master(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }

    Workspace *ws = ws_of(wm.selmon, wm.selmon->current_ws);
    if (!ws || !ws->focused) {
        return;
    }

    Client *c = ws->focused;
    if (c->is_floating || c->is_scratchpad || c->is_fullscreen) {
        return;
    }

    Client *master = first_tiled_client(ws);
    if (!master || master == c) {
        return;
    }

    detach_from_workspace(c);
    c->ws = ws;
    attach_client_head(ws, c);

    layout_monitor(wm.selmon);
    focus_client(c);
}

static void grab_keys(void) {
    xcb_key_symbols_t *symbols = xcb_key_symbols_alloc(wm.conn);
    xcb_keycode_t *code;

    if (!symbols) {
        return;
    }

    xcb_ungrab_key(wm.conn, XCB_GRAB_ANY, wm.root, XCB_MOD_MASK_ANY);

    uint16_t masks[] = {
        0,
        XCB_MOD_MASK_LOCK,
        XCB_MOD_MASK_2,
        XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2
    };

    for (size_t k = 0; k < wm.config.keybind_count; k++) {
        code = xcb_key_symbols_get_keycode(symbols, wm.config.keybinds[k].sym);
        if (!code) {
            continue;
        }

        for (int i = 0; code[i] != XCB_NO_SYMBOL; i++) {
            for (size_t m = 0; m < LENGTH(masks); m++) {
                xcb_grab_key(
                    wm.conn,
                    1,
                    wm.root,
                    wm.config.keybinds[k].mod | masks[m],
                    code[i],
                    XCB_GRAB_MODE_ASYNC,
                    XCB_GRAB_MODE_ASYNC
                );
            }
        }

        free(code);
    }

    xcb_key_symbols_free(symbols);
}

static void key_press(xcb_generic_event_t *gev) {
    xcb_key_press_event_t *ev = (xcb_key_press_event_t *)gev;

    xcb_key_symbols_t *symbols = xcb_key_symbols_alloc(wm.conn);
    if (!symbols) {
        return;
    }

    xcb_keysym_t sym = xcb_key_symbols_get_keysym(symbols, ev->detail, 0);
    uint16_t cleaned = ev->state & ~(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2);

    for (size_t i = 0; i < wm.config.keybind_count; i++) {
        Keybind *kb = &wm.config.keybinds[i];
        if (kb->sym == sym && kb->mod == cleaned) {
            dispatch_action(kb->action);
            xcb_key_symbols_free(symbols);
            return;
        }
    }

    xcb_key_symbols_free(symbols);
}

static void map_request(xcb_generic_event_t *gev) {
    xcb_map_request_event_t *ev = (xcb_map_request_event_t *)gev;
    if (!find_client(ev->window)) {
        manage_window(ev->window);
    } else {
        xcb_map_window(wm.conn, ev->window);
    }
}

static void destroy_notify(xcb_generic_event_t *gev) {
    xcb_destroy_notify_event_t *ev = (xcb_destroy_notify_event_t *)gev;
    Client *c = find_client(ev->window);
    if (c) {
        unmanage_client(c);
    }
}

static void unmap_notify(xcb_generic_event_t *gev) {
    xcb_unmap_notify_event_t *ev = (xcb_unmap_notify_event_t *)gev;
    Client *c = find_client(ev->window);
    if (!c) {
        return;
    }

    if (c->is_hidden) {
        return;
    }

    unmanage_client(c);
}

static void configure_request(xcb_generic_event_t *gev) {
    xcb_configure_request_event_t *ev = (xcb_configure_request_event_t *)gev;
    Client *c = find_client(ev->window);
    if (!c || c->is_floating) {
        uint32_t values[7];
        int i = 0;
        uint16_t mask = 0;

        if (ev->value_mask & XCB_CONFIG_WINDOW_X) { mask |= XCB_CONFIG_WINDOW_X; values[i++] = (uint32_t)ev->x; }
        if (ev->value_mask & XCB_CONFIG_WINDOW_Y) { mask |= XCB_CONFIG_WINDOW_Y; values[i++] = (uint32_t)ev->y; }
        if (ev->value_mask & XCB_CONFIG_WINDOW_WIDTH) { mask |= XCB_CONFIG_WINDOW_WIDTH; values[i++] = (uint32_t)ev->width; }
        if (ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT) { mask |= XCB_CONFIG_WINDOW_HEIGHT; values[i++] = (uint32_t)ev->height; }
        if (ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) { mask |= XCB_CONFIG_WINDOW_BORDER_WIDTH; values[i++] = (uint32_t)ev->border_width; }
        if (ev->value_mask & XCB_CONFIG_WINDOW_SIBLING) { mask |= XCB_CONFIG_WINDOW_SIBLING; values[i++] = (uint32_t)ev->sibling; }
        if (ev->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) { mask |= XCB_CONFIG_WINDOW_STACK_MODE; values[i++] = (uint32_t)ev->stack_mode; }

        xcb_configure_window(wm.conn, ev->window, mask, values);
        if (c) {
            c->frame.x = ev->x;
            c->frame.y = ev->y;
            c->frame.w = ev->width;
            c->frame.h = ev->height;
        }
        return;
    }

    layout_monitor(c->mon);
}

static void event_loop(void) {
    wm.running = true;
    int fd = xcb_get_file_descriptor(wm.conn);

    while (wm.running && !g_should_exit) {
        if (g_should_reload) {
            g_should_reload = 0;
            reload_config(NULL);
            xcb_flush(wm.conn);
        }

        xcb_generic_event_t *ev = NULL;
        while ((ev = xcb_poll_for_event(wm.conn)) != NULL) {
            uint8_t type = ev->response_type & ~0x80;
            switch (type) {
                case XCB_EXPOSE:
                    handle_expose(ev);
                    break;
                case XCB_PROPERTY_NOTIFY:
                    handle_property_notify(ev);
                    break;
                case XCB_MAP_REQUEST:
                    map_request(ev);
                    break;
                case XCB_DESTROY_NOTIFY:
                    destroy_notify(ev);
                    break;
                case XCB_UNMAP_NOTIFY:
                    unmap_notify(ev);
                    break;
                case XCB_CONFIGURE_REQUEST:
                    configure_request(ev);
                    break;
                case XCB_KEY_PRESS:
                    key_press(ev);
                    break;
                default:
                    break;
            }

            xcb_flush(wm.conn);
            free(ev);
        }

        struct pollfd pfd = {
            .fd = fd,
            .events = POLLIN,
            .revents = 0
        };

        poll(&pfd, 1, 100);
    }
}

static void handle_signal(int signo) {
    if (signo == SIGHUP) {
        g_should_reload = 1;
        return;
    }

    g_should_exit = 1;
    wm.running = false;
}

int main(void) {
    struct sigaction sa = {0};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    struct sigaction chld = {0};
    chld.sa_handler = SIG_IGN;
    sigemptyset(&chld.sa_mask);
    chld.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &chld, NULL);

    wm.dpy = XOpenDisplay(NULL);
    if (!wm.dpy) {
        die("failed to open X display");
    }

    wm.xscreen = DefaultScreen(wm.dpy);
    wm.visual = DefaultVisual(wm.dpy, wm.xscreen);
    wm.colormap = DefaultColormap(wm.dpy, wm.xscreen);

    wm.conn = XGetXCBConnection(wm.dpy);
    if (!wm.conn || xcb_connection_has_error(wm.conn)) {
        die("failed to get XCB connection from Xlib");
    }

    XSetEventQueueOwner(wm.dpy, XCBOwnsEventQueue);

    wm.scratchpad_spawn_pending = false;
    wm.scratchpad = NULL;
    wm.xft_font = NULL;
    wm.status_cache[0] = '\0';

    load_default_config();
    setup_atoms();
    setup_monitors();
    setup_root();
    apply_config();
    update_status_cache();
    scan_existing_windows();
    draw_all_bars();
    xcb_flush(wm.conn);

    fprintf(stderr, "vwm: started\n");
    event_loop();
    cleanup();
    return 0;
}
