#include "actions.h"

#include "bar.h"
#include "client.h"
#include "config.h"
#include "layout.h"

static const char *launcher_fallback[] = {"rofi", "-show", "drun", NULL};

static bool ascii_case_equal_local(const char *a, const char *b) {
    if (!a || !b) {
        return false;
    }

    while (*a && *b) {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;

        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');

        if (ca != cb) {
            return false;
        }

        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static bool window_has_class(xcb_window_t win, const char *wanted) {
    if (!wanted || !*wanted) {
        return false;
    }

    xcb_icccm_get_wm_class_reply_t reply;
    if (!xcb_icccm_get_wm_class_reply(
            wm.conn,
            xcb_icccm_get_wm_class(wm.conn, win),
            &reply,
            NULL)) {
        return false;
    }

    bool matched = false;

    if (reply.instance_name && ascii_case_equal_local(reply.instance_name, wanted)) {
        matched = true;
    }

    if (!matched && reply.class_name && ascii_case_equal_local(reply.class_name, wanted)) {
        matched = true;
    }

    xcb_icccm_get_wm_class_reply_wipe(&reply);
    return matched;
}

static Client *find_client_by_class_name(const char *class_name) {
    if (!class_name || !*class_name) {
        return NULL;
    }

    for (Monitor *m = wm.mons; m; m = m->next) {
        for (int i = 0; i < WORKSPACE_COUNT; i++) {
            for (Client *c = m->workspaces[i].clients; c; c = c->next) {
                if (window_has_class(c->win, class_name)) {
                    return c;
                }
            }
        }
    }

    return NULL;
}

static void set_pending_scratchpad_request(const char *name, const char *class_name) {
    wm.scratchpad_spawn_pending = true;

    if (name && *name) {
        snprintf(
            wm.pending_scratchpad_name,
            sizeof(wm.pending_scratchpad_name),
            "%s",
            name
        );
    } else {
        wm.pending_scratchpad_name[0] = '\0';
    }

    if (class_name && *class_name) {
        snprintf(
            wm.pending_scratchpad_class,
            sizeof(wm.pending_scratchpad_class),
            "%s",
            class_name
        );
    } else {
        wm.pending_scratchpad_class[0] = '\0';
    }
}

static void raise_client_above(Client *c) {
    if (!c) {
        return;
    }

    uint32_t values[] = { XCB_STACK_MODE_ABOVE };
    xcb_configure_window(wm.conn, c->win, XCB_CONFIG_WINDOW_STACK_MODE, values);
}

static void set_monitor_workspace(Monitor *m, int idx) {
    if (!m || idx < 0 || idx >= WORKSPACE_COUNT || m->current_ws == idx) {
        return;
    }
    m->previous_ws = m->current_ws;
    m->current_ws = idx;
    layout_monitor(m);
}

void spawn(const void *arg) {
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

void quit(const void *arg) {
    (void)arg;
    wm.running = false;
}

void focus_next(const void *arg) {
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

void focus_prev(const void *arg) {
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

void focus_monitor_next(const void *arg) {
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

void focus_monitor_prev(const void *arg) {
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

void send_to_monitor_next(const void *arg) {
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

void send_to_monitor_prev(const void *arg) {
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

void view_workspace(const void *arg) {
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

void send_to_workspace(const void *arg) {
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

void toggle_sync_workspaces(const void *arg) {
    (void)arg;
    wm.config.sync_workspaces = !wm.config.sync_workspaces;
    draw_all_bars();
}

void set_client_fullscreen_state(Client *c, bool enabled) {
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

void toggle_fullscreen(const void *arg) {
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

void toggle_scratchpad(const void *arg) {
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
        set_pending_scratchpad_request("legacy", wm.config.scratchpad_class);
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
    c->is_scratchpad = true;

    center_client_on_monitor(c, wm.selmon);
    xcb_map_window(wm.conn, c->win);
    raise_client_above(c);
    layout_monitor(wm.selmon);
    focus_client(c);
}

void toggle_named_scratchpad(const char *name) {
    if (!wm.selmon || !name || !*name) {
        return;
    }

    DynamicScratchpad *sp = find_dynamic_scratchpad(name);
    if (!sp) {
        fprintf(stderr, "vwm: unknown scratchpad '%s'\n", name);
        return;
    }

    if (wm.scratchpad_spawn_pending) {
        return;
    }

    Client *c = NULL;
    if (sp->class_name[0] != '\0') {
        c = find_client_by_class_name(sp->class_name);
    }

    if (!c) {
        if (sp->class_name[0] == '\0') {
            fprintf(stderr, "vwm: scratchpad '%s' has no class, cannot safely track spawn\n", name);
            return;
        }

        set_pending_scratchpad_request(name, sp->class_name);
        spawn(sp->argv);
        return;
    }

    Workspace *target_ws = ws_of(wm.selmon, wm.selmon->current_ws);

    if (!c->is_hidden && c->mon == wm.selmon && c->ws == target_ws) {
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
    c->is_scratchpad = true;

    center_client_on_monitor(c, wm.selmon);
    xcb_map_window(wm.conn, c->win);
    raise_client_above(c);
    layout_monitor(wm.selmon);
    focus_client(c);
}

bool client_supports_protocol(Client *c, xcb_atom_t protocol) {
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

void send_wm_delete(Client *c) {
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

void kill_client(const void *arg) {
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

void decrease_mfact(const void *arg) {
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

void increase_mfact(const void *arg) {
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

void zoom_master(const void *arg) {
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

void dispatch_action(Action action) {
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
