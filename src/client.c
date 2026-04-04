#include "client.h"

#include "bar.h"
#include "config.h"
#include "layout.h"
#include "util.h"
#include "x11.h"

Workspace *ws_of(Monitor *m, int idx) {
    if (!m || idx < 0 || idx >= WORKSPACE_COUNT) {
        return NULL;
    }
    return &m->workspaces[idx];
}

void ensure_scratch_workspace_ready(void) {
    Workspace *ws = &wm.scratch_workspace;

    if (ws->nmaster <= 0) {
        ws->id = WORKSPACE_COUNT;
        ws->layout = LAYOUT_TILE;
        ws->gap_px = wm.config.gap_px;
        ws->mfact = wm.config.default_mfact;
        ws->nmaster = 1;
        ws->hide_bar = false;
    }

    if (ws->mfact <= 0.0f || ws->mfact >= 1.0f) {
        ws->mfact = wm.config.default_mfact;
    }

    ws->gap_px = wm.config.gap_px;
}

bool scratch_visible_on_monitor(Monitor *m) {
    return m && wm.scratch_overlay_visible && wm.scratch_monitor == m;
}

static Workspace *overlay_ws_of(void) {
    ensure_scratch_workspace_ready();
    return &wm.scratch_workspace;
}

Client *first_tiled_client(Workspace *ws) {
    for (Client *c = ws ? ws->clients : NULL; c; c = c->next) {
        if (!c->is_floating && !c->is_fullscreen) {
            return c;
        }
    }
    return NULL;
}

Client *find_fullscreen_client(Workspace *ws) {
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

bool workspace_has_clients(Workspace *ws) {
    return ws && ws->clients;
}

bool get_text_property_utf8(xcb_window_t win, xcb_atom_t prop_atom, char *buf, size_t buflen) {
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

bool get_text_property_legacy(xcb_window_t win, char *buf, size_t buflen) {
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

void get_client_title(Client *c, char *buf, size_t buflen) {
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

void focus_client(Client *c) {
    if (!c || !c->mon || c->is_hidden) {
        return;
    }

    Workspace *ws = c->ws;
    if (!ws) {
        return;
    }

    uint32_t inactive[] = {
        border_pixel_for_rgb(wm.config.border_inactive)
    };
    uint32_t active[] = {
        border_pixel_for_rgb(wm.config.border_active)
    };
    uint32_t stack_above[] = {
        XCB_STACK_MODE_ABOVE
    };

    for (Client *it = wm.scratch_workspace.clients; it; it = it->next) {
        if (it == c) {
            continue;
        }

        if (it->is_fullscreen) {
            continue;
        }

        xcb_change_window_attributes(
            wm.conn,
            it->win,
            XCB_CW_BORDER_PIXEL,
            inactive
        );
    }

    for (Monitor *m = wm.mons; m; m = m->next) {
        for (int i = 0; i < WORKSPACE_COUNT; i++) {
            for (Client *it = m->workspaces[i].clients; it; it = it->next) {
                if (it == c) {
                    continue;
                }

                if (it->is_fullscreen) {
                    continue;
                }

                xcb_change_window_attributes(
                    wm.conn,
                    it->win,
                    XCB_CW_BORDER_PIXEL,
                    inactive
                );
            }
        }
    }

    if (ws->focused && ws->focused != c) {
        ws->last_focused = ws->focused;
    }

    wm.selmon = c->mon;
    c->mon->focused = c;
    ws->focused = c;

    if (!c->is_fullscreen) {
        xcb_change_window_attributes(
            wm.conn,
            c->win,
            XCB_CW_BORDER_PIXEL,
            active
        );
    }

    xcb_configure_window(
        wm.conn,
        c->win,
        XCB_CONFIG_WINDOW_STACK_MODE,
        stack_above
    );

    xcb_set_input_focus(
        wm.conn,
        XCB_INPUT_FOCUS_POINTER_ROOT,
        c->win,
        XCB_CURRENT_TIME
    );

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

void attach_client(Workspace *ws, Client *c) {
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

void attach_client_head(Workspace *ws, Client *c) {
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

void focus_workspace(Monitor *m) {
    if (!m) {
        return;
    }

    Workspace *ws = scratch_visible_on_monitor(m) ? overlay_ws_of() : ws_of(m, m->current_ws);
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

void detach_from_workspace(Client *c) {
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

Monitor *next_monitor(Monitor *m) {
    if (!m) {
        return wm.mons;
    }
    return m->next ? m->next : wm.mons;
}

Monitor *prev_monitor(Monitor *m) {
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

void move_client_to_monitor(Client *c, Monitor *dst) {
    if (!c || !dst || c->mon == dst) {
        return;
    }

    Monitor *old_mon = c->mon;
    bool came_from_scratch = (c->ws == &wm.scratch_workspace);

    detach_from_workspace(c);

    if (came_from_scratch) {
        c->ws = &wm.scratch_workspace;
        c->mon = dst;

        for (Client *it = wm.scratch_workspace.clients; it; it = it->next) {
            it->mon = dst;
        }

        if (wm.scratch_overlay_visible) {
            Monitor *prev = wm.scratch_monitor;
            wm.scratch_monitor = dst;
            if (prev && prev != dst) {
                hide_scratch_overlay(prev);
                layout_monitor(prev);
            }
        }
    } else {
        c->mon = dst;
        c->ws = ws_of(dst, dst->current_ws);
    }

    attach_client(c->ws, c);

    if (c->is_floating) {
        center_client_on_monitor(c, dst);
    }

    layout_monitor(old_mon);
    layout_monitor(dst);
    focus_client(c);
}

Client *find_client(xcb_window_t win) {
    for (Client *c = wm.scratch_workspace.clients; c; c = c->next) {
        if (c->win == win) {
            return c;
        }
    }

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

void scan_existing_windows(void) {
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
            if (wins[i] == m->barwin || wins[i] == m->scratch_overlay_win) {
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

void unmanage_client(Client *c) {
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

    bool was_focused = (ws->focused == c);

    if (ws->last_focused == c) {
        ws->last_focused = NULL;
    }

    if (was_focused) {
        Client *fallback = NULL;

        if (ws->last_focused &&
            ws->last_focused != c &&
            !ws->last_focused->is_hidden) {
            fallback = ws->last_focused;
        }

        if (!fallback) {
            fallback = c->next ? c->next : c->prev;
        }

        ws->focused = fallback;
    }

    if (m && m->focused == c) {
        m->focused = NULL;
    }

    free(c);

    if (was_focused && ws->focused && m) {
        layout_monitor(m);
        focus_workspace(m);
        return;
    }

    if (ws == &wm.scratch_workspace && wm.scratch_overlay_visible && wm.scratch_workspace.clients == NULL) {
        Monitor *sm = wm.scratch_monitor;
        wm.scratch_overlay_visible = false;
        wm.scratch_monitor = NULL;
        if (sm) {
            hide_scratch_overlay(sm);
            layout_monitor(sm);
        }
        draw_all_bars();
        return;
    }

    if (m) {
        layout_monitor(m);
    }
}

void manage_window(xcb_window_t win) {
    if (!wm.selmon) {
        return;
    }

    for (Monitor *m = wm.mons; m; m = m->next) {
        if (win == m->barwin || win == m->scratch_overlay_win) {
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

    bool in_scratch_overlay = wm.scratch_overlay_visible && wm.scratch_monitor;
    Monitor *target_mon = in_scratch_overlay ? wm.scratch_monitor : wm.selmon;

    c->win = win;
    c->mon = target_mon;
    c->frame = (Rect){ .x = 0, .y = 0, .w = 0, .h = 0 };
    c->old_frame = c->frame;
    c->is_hidden = false;
    c->ws = in_scratch_overlay ? overlay_ws_of() : ws_of(target_mon, target_mon->current_ws);

    xcb_icccm_get_wm_class_reply_t class_reply;
    bool have_class = xcb_icccm_get_wm_class_reply(
        wm.conn,
        xcb_icccm_get_wm_class(wm.conn, win),
        &class_reply,
        NULL
    );

    if (have_class) {
        if (!in_scratch_overlay) {
            if ((class_reply.instance_name && class_should_float(class_reply.instance_name)) ||
                (class_reply.class_name && class_should_float(class_reply.class_name))) {
                c->is_floating = true;
            }

            int rule_ws = -1;
            if (class_reply.instance_name) {
                rule_ws = class_workspace_rule(class_reply.instance_name);
            }
            if (rule_ws < 0 && class_reply.class_name) {
                rule_ws = class_workspace_rule(class_reply.class_name);
            }
            if (rule_ws >= 0) {
                c->ws = ws_of(target_mon, rule_ws);
            }
        } else {
            c->is_floating = false;
        }
    }

    uint32_t values[] = {
        border_pixel_for_rgb(wm.config.border_inactive),
        XCB_EVENT_MASK_ENTER_WINDOW |
        XCB_EVENT_MASK_FOCUS_CHANGE |
        XCB_EVENT_MASK_PROPERTY_CHANGE,
    };

    xcb_change_window_attributes(
        wm.conn,
        win,
        XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK,
        values
    );

    attach_client(c->ws, c);

    if (have_class) {
        xcb_icccm_get_wm_class_reply_wipe(&class_reply);
    }

    if (c->is_floating) {
        center_client_on_monitor(c, c->mon);
    }

    xcb_map_window(wm.conn, win);

    layout_monitor(c->mon);
    focus_client(c);
}
