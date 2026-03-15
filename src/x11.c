#include "x11.h"

#include "actions.h"
#include "bar.h"
#include "client.h"
#include "config.h"
#include "layout.h"
#include "util.h"

xcb_atom_t intern_atom(const char *name) {
    xcb_intern_atom_cookie_t ck = xcb_intern_atom(wm.conn, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(wm.conn, ck, NULL);
    xcb_atom_t atom = XCB_ATOM_NONE;
    if (reply) {
        atom = reply->atom;
        free(reply);
    }
    return atom;
}

void setup_atoms(void) {
    wm.wm_protocols = intern_atom("WM_PROTOCOLS");
    wm.wm_delete_window = intern_atom("WM_DELETE_WINDOW");
    wm.net_wm_state = intern_atom("_NET_WM_STATE");
    wm.net_wm_state_fullscreen = intern_atom("_NET_WM_STATE_FULLSCREEN");
    wm.net_active_window = intern_atom("_NET_ACTIVE_WINDOW");
    wm.utf8_string = intern_atom("UTF8_STRING");
    wm.net_wm_name = intern_atom("_NET_WM_NAME");
}

void create_bar(Monitor *m) {
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
        (int16_t)bar_x,
        (int16_t)bar_y,
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

Monitor *add_monitor(int id, int x, int y, int w, int h, xcb_randr_output_t output) {
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

void setup_monitors(void) {
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

void setup_root(void) {
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

void grab_keys(void) {
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

    for (size_t k = 0; k < dynconfig.keybind_count; k++) {
        code = xcb_key_symbols_get_keycode(symbols, dynconfig.keybinds[k].sym);
        if (!code) {
            continue;
        }

        for (int i = 0; code[i] != XCB_NO_SYMBOL; i++) {
            for (size_t m = 0; m < LENGTH(masks); m++) {
                xcb_grab_key(
                    wm.conn,
                    1,
                    wm.root,
                    dynconfig.keybinds[k].mod | masks[m],
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

void handle_expose(xcb_generic_event_t *gev) {
    xcb_expose_event_t *ev = (xcb_expose_event_t *)gev;

    for (Monitor *m = wm.mons; m; m = m->next) {
        if (ev->window == m->barwin) {
            draw_bar(m);
            return;
        }
    }
}

void handle_property_notify(xcb_generic_event_t *gev) {
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

void key_press(xcb_generic_event_t *gev) {
    xcb_key_press_event_t *ev = (xcb_key_press_event_t *)gev;

    xcb_key_symbols_t *symbols = xcb_key_symbols_alloc(wm.conn);
    if (!symbols) {
        return;
    }

    xcb_keysym_t sym = xcb_key_symbols_get_keysym(symbols, ev->detail, 0);
    uint16_t cleaned = ev->state & ~(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2);

    if (execute_dynamic_keybind(sym, cleaned)) {
        xcb_key_symbols_free(symbols);
        return;
    }

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

void map_request(xcb_generic_event_t *gev) {
    xcb_map_request_event_t *ev = (xcb_map_request_event_t *)gev;
    if (!find_client(ev->window)) {
        manage_window(ev->window);
    } else {
        xcb_map_window(wm.conn, ev->window);
    }
}

void destroy_notify(xcb_generic_event_t *gev) {
    xcb_destroy_notify_event_t *ev = (xcb_destroy_notify_event_t *)gev;
    Client *c = find_client(ev->window);
    if (c) {
        unmanage_client(c);
    }
}

void unmap_notify(xcb_generic_event_t *gev) {
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

void configure_request(xcb_generic_event_t *gev) {
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
