#include "actions.h"

#include "bar.h"
#include "client.h"
#include "config.h"
#include "layout.h"
#include "x11.h"

static const char *launcher_fallback[] = {"rofi", "-show", "drun", NULL};

static Workspace *active_workspace_for_monitor(Monitor *m) {
    if (!m) {
        return NULL;
    }

    if (scratch_visible_on_monitor(m)) {
        ensure_scratch_workspace_ready();
        return &wm.scratch_workspace;
    }

    return ws_of(m, m->current_ws);
}

static void retarget_scratch_workspace_clients(Monitor *m) {
    if (!m) {
        return;
    }

    for (Client *c = wm.scratch_workspace.clients; c; c = c->next) {
        c->mon = m;
    }
}

static void set_monitor_workspace(Monitor *m, int idx) {
    if (!m || idx < 0 || idx >= WORKSPACE_COUNT || m->current_ws == idx) {
        return;
    }

    m->previous_ws = m->current_ws;
    m->current_ws = idx;
    layout_monitor(m);
}

static void close_scratch_overlay(void) {
    Monitor *old = wm.scratch_monitor;

    wm.scratch_overlay_visible = false;
    wm.scratch_monitor = NULL;

    if (old) {
        hide_scratch_overlay(old);
        layout_monitor(old);
    }

    draw_all_bars();
}

static void open_scratch_overlay_on(Monitor *m) {
    if (!m) {
        return;
    }

    ensure_scratch_workspace_ready();

    Monitor *old = wm.scratch_monitor;
    wm.scratch_overlay_visible = true;
    wm.scratch_monitor = m;

    retarget_scratch_workspace_clients(m);

    if (old && old != m) {
        hide_scratch_overlay(old);
        layout_monitor(old);
    }

    show_scratch_overlay(m);

    if (!wm.scratch_workspace.clients) {
        if (dynconfig.scratchpad_autostart_count > 0) {
            run_scratchpad_autostart();
        } else if (wm.config.scratchpad_cmd[0]) {
            spawn(wm.config.scratchpad_cmd);
        }
    }

    layout_monitor(m);

    Client *c = wm.scratch_workspace.focused;
    if (!c || c->is_hidden) {
        c = wm.scratch_workspace.clients;
        while (c && c->is_hidden) {
            c = c->next;
        }
        wm.scratch_workspace.focused = c;
    }

    if (c) {
        focus_client(c);
    }

    draw_all_bars();
}

static char *expand_spawn_arg(const char *arg) {
    if (!arg) {
        return NULL;
    }

    if (arg[0] != '~') {
        return strdup(arg);
    }

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        return strdup(arg);
    }

    if (arg[1] == '\0') {
        return strdup(home);
    }

    if (arg[1] == '/') {
        size_t need = strlen(home) + strlen(arg + 1) + 1;
        char *out = calloc(1, need);
        if (!out) {
            return NULL;
        }

        snprintf(out, need, "%s%s", home, arg + 1);
        return out;
    }

    return strdup(arg);
}

static void free_spawn_argv(char **argv, size_t argc) {
    if (!argv) {
        return;
    }

    for (size_t i = 0; i < argc; i++) {
        free(argv[i]);
        argv[i] = NULL;
    }
}

void spawn(const void *arg) {
    const char *const *cmd = arg;
    if (!cmd || !cmd[0]) {
        return;
    }

    char *argv[CMD_MAX_ARGS] = {0};
    size_t argc = 0;

    while (cmd[argc] && argc + 1 < CMD_MAX_ARGS) {
        argv[argc] = expand_spawn_arg(cmd[argc]);
        if (!argv[argc]) {
            free_spawn_argv(argv, argc);
            fprintf(stderr, "vwm: failed to expand command argument\n");
            return;
        }
        argc++;
    }
    argv[argc] = NULL;

    pid_t pid = fork();
    if (pid == 0) {
        setsid();

        if (wm.conn) {
            close(xcb_get_file_descriptor(wm.conn));
        }

        const char *path = getenv("PATH");
        if (!path || path[0] == '\0') {
            setenv(
                "PATH",
                "/usr/local/sbin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",
                1
            );
        }

        execvp(argv[0], argv);
        fprintf(stderr, "execvp failed for %s: %s\n", argv[0], strerror(errno));
        _exit(1);
    }

    free_spawn_argv(argv, argc);
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

    Workspace *ws = active_workspace_for_monitor(wm.selmon);
    if (!ws || !ws->clients) {
        return;
    }

    bool monocle = (ws->layout == LAYOUT_MONOCLE);

    Client *start = ws->focused ? ws->focused : ws->clients;
    Client *c = start->next ? start->next : ws->clients;

    while (c && c != start) {
        if (!c->is_floating && !c->is_fullscreen) {
            if (monocle || !c->is_hidden) {
                ws->focused = c;
                layout_monitor(wm.selmon);
                focus_client(c);
                return;
            }
        }

        c = c->next ? c->next : ws->clients;
    }

    if (start && !start->is_floating && !start->is_fullscreen) {
        if (monocle || !start->is_hidden) {
            ws->focused = start;
            layout_monitor(wm.selmon);
            focus_client(start);
        }
    }
}

void focus_prev(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }

    Workspace *ws = active_workspace_for_monitor(wm.selmon);
    if (!ws || !ws->clients) {
        return;
    }

    bool monocle = (ws->layout == LAYOUT_MONOCLE);

    Client *last = ws->clients;
    while (last->next) {
        last = last->next;
    }

    Client *start = ws->focused ? ws->focused : last;
    Client *c = start->prev ? start->prev : last;

    while (c && c != start) {
        if (!c->is_floating && !c->is_fullscreen) {
            if (monocle || !c->is_hidden) {
                ws->focused = c;
                layout_monitor(wm.selmon);
                focus_client(c);
                return;
            }
        }

        c = c->prev ? c->prev : last;
    }

    if (start && !start->is_floating && !start->is_fullscreen) {
        if (monocle || !start->is_hidden) {
            ws->focused = start;
            layout_monitor(wm.selmon);
            focus_client(start);
        }
    }
}

void focus_monitor_next(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }

    wm.selmon = next_monitor(wm.selmon);

    if (wm.scratch_overlay_visible) {
        open_scratch_overlay_on(wm.selmon);
        return;
    }

    Workspace *ws = active_workspace_for_monitor(wm.selmon);
    if (!ws || !ws->focused) {
        clear_focus_borders_except(NULL);
        wm.selmon->focused = NULL;
        draw_all_bars();
        return;
    }

    focus_client(ws->focused);
    draw_all_bars();
}

void focus_monitor_prev(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }

    wm.selmon = prev_monitor(wm.selmon);

    if (wm.scratch_overlay_visible) {
        open_scratch_overlay_on(wm.selmon);
        return;
    }

    Workspace *ws = active_workspace_for_monitor(wm.selmon);
    if (!ws || !ws->focused) {
        clear_focus_borders_except(NULL);
        wm.selmon->focused = NULL;
        draw_all_bars();
        return;
    }

    focus_client(ws->focused);
    draw_all_bars();
}

void send_to_monitor_next(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }

    Workspace *ws = active_workspace_for_monitor(wm.selmon);
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

    Workspace *ws = active_workspace_for_monitor(wm.selmon);
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

    if (wm.scratch_overlay_visible) {
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

    Workspace *cur = active_workspace_for_monitor(wm.selmon);
    if (!cur || !cur->focused) {
        return;
    }

    Client *c = cur->focused;
    Workspace *dst = ws_of(wm.selmon, wa->workspace);
    if (!dst || cur == dst) {
        return;
    }

    detach_from_workspace(c);
    c->ws = dst;
    c->mon = wm.selmon;
    attach_client(dst, c);

    if (cur == &wm.scratch_workspace && cur->clients == NULL) {
        close_scratch_overlay();
    } else {
        layout_monitor(wm.selmon);
        focus_workspace(wm.selmon);
    }
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

void toggle_monocle(const void *arg) {
    (void)arg;

    if (!wm.selmon) {
        return;
    }

    Workspace *ws = active_workspace_for_monitor(wm.selmon);
    if (!ws) {
        return;
    }

    if (ws->layout == LAYOUT_MONOCLE) {
        ws->layout = LAYOUT_TILE;
    } else {
        ws->layout = LAYOUT_MONOCLE;
    }

    layout_monitor(wm.selmon);
}

void toggle_true_fullscreen(const void *arg) {
    (void)arg;
    if (!wm.selmon) {
        return;
    }

    Workspace *ws = active_workspace_for_monitor(wm.selmon);
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

    if (wm.scratch_overlay_visible && wm.scratch_monitor == wm.selmon) {
        close_scratch_overlay();
        return;
    }

    open_scratch_overlay_on(wm.selmon);
}

void toggle_named_scratchpad(const char *name) {
    (void)name;
    toggle_scratchpad(NULL);
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

    Workspace *ws = active_workspace_for_monitor(wm.selmon);
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

    Workspace *ws = active_workspace_for_monitor(wm.selmon);
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

    Workspace *ws = active_workspace_for_monitor(wm.selmon);
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

    Workspace *ws = active_workspace_for_monitor(wm.selmon);
    if (!ws || !ws->focused) {
        return;
    }

    Client *c = ws->focused;
    if (c->is_floating || c->is_fullscreen) {
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
        case ACTION_TOGGLE_MONOCLE:
            toggle_monocle(NULL);
            break;
        case ACTION_TOGGLE_TRUE_FULLSCREEN:
            toggle_true_fullscreen(NULL);
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
