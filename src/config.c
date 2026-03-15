#include "config.h"

#include "actions.h"
#include "bar.h"
#include "client.h"
#include "layout.h"
#include "util.h"
#include "x11.h"

static const char *launcher_fallback[] = {"rofi -show drun", NULL};

void rebuild_config_commands(void) {
    size_t argc = 0;

    memset(wm.config.term_cmd_storage, 0, sizeof(wm.config.term_cmd_storage));
    memset(wm.config.launcher_cmd_storage, 0, sizeof(wm.config.launcher_cmd_storage));
    memset(wm.config.scratchpad_cmd_storage, 0, sizeof(wm.config.scratchpad_cmd_storage));

    memset(wm.config.term_cmd, 0, sizeof(wm.config.term_cmd));
    memset(wm.config.launcher_cmd, 0, sizeof(wm.config.launcher_cmd));
    memset(wm.config.scratchpad_cmd, 0, sizeof(wm.config.scratchpad_cmd));

    split_command_argv(
        wm.config.terminal,
        wm.config.term_cmd_storage,
        wm.config.term_cmd,
        CMD_MAX_ARGS
    );

    split_command_argv(
        wm.config.launcher,
        wm.config.launcher_cmd_storage,
        wm.config.launcher_cmd,
        CMD_MAX_ARGS
    );

    argc = split_command_argv(
        wm.config.scratchpad,
        wm.config.scratchpad_cmd_storage,
        wm.config.scratchpad_cmd,
        CMD_MAX_ARGS
    );

    if (wm.config.scratchpad_class[0] != '\0' && argc + 2 < CMD_MAX_ARGS) {
        snprintf(
            wm.config.scratchpad_cmd_storage[argc],
            sizeof(wm.config.scratchpad_cmd_storage[argc]),
            "%s",
            "--class"
        );
        wm.config.scratchpad_cmd[argc] = wm.config.scratchpad_cmd_storage[argc];
        argc++;

        snprintf(
            wm.config.scratchpad_cmd_storage[argc],
            sizeof(wm.config.scratchpad_cmd_storage[argc]),
            "%s",
            wm.config.scratchpad_class
        );
        wm.config.scratchpad_cmd[argc] = wm.config.scratchpad_cmd_storage[argc];
        argc++;

        wm.config.scratchpad_cmd[argc] = NULL;
    }
}

void init_default_keybinds(void) {
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

void strip_comment(char *s) {
    if (!s) {
        return;
    }

    bool in_single = false;
    bool in_double = false;
    bool escaped = false;

    for (char *p = s; *p; p++) {
        if (escaped) {
            escaped = false;
            continue;
        }

        if (*p == '\\') {
            escaped = true;
            continue;
        }

        if (!in_single && *p == '"') {
            in_double = !in_double;
            continue;
        }

        if (!in_double && *p == '\'') {
            in_single = !in_single;
            continue;
        }

        if (!in_single && !in_double && *p == '#') {
            *p = '\0';
            return;
        }
    }
}

bool parse_bool_value(const char *s, bool *out) {
    if (!s || !out) {
        return false;
    }

    if (strcmp(s, "1") == 0 ||
        strcmp(s, "true") == 0 ||
        strcmp(s, "yes") == 0 ||
        strcmp(s, "on") == 0) {
        *out = true;
        return true;
    }

    if (strcmp(s, "0") == 0 ||
        strcmp(s, "false") == 0 ||
        strcmp(s, "no") == 0 ||
        strcmp(s, "off") == 0) {
        *out = false;
        return true;
    }

    return false;
}

bool parse_color_value(const char *s, uint32_t *out) {
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

void config_unquote_inplace(char *s) {
    if (!s) {
        return;
    }

    size_t len = strlen(s);
    if (len >= 2) {
        if ((s[0] == '"' && s[len - 1] == '"') || (s[0] == '\'' && s[len - 1] == '\'')) {
            memmove(s, s + 1, len - 2);
            s[len - 2] = '\0';
        }
    }
}

void sanitize_config(void) {
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

void expand_home_path(const char *in, char *out, size_t outsz) {
    if (!in || !out || outsz == 0) {
        return;
    }

    out[0] = '\0';

    if (in[0] != '~') {
        snprintf(out, outsz, "%s", in);
        return;
    }

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        snprintf(out, outsz, "%s", in);
        return;
    }

    if (in[1] == '\0') {
        snprintf(out, outsz, "%s", home);
        return;
    }

    if (in[1] == '/') {
        snprintf(out, outsz, "%s%s", home, in + 1);
        return;
    }

    snprintf(out, outsz, "%s", in);
}

void dir_from_path(const char *path, char *out, size_t outsz) {
    if (!path || !out || outsz == 0) {
        return;
    }

    out[0] = '\0';

    const char *slash = strrchr(path, '/');
    if (!slash) {
        snprintf(out, outsz, ".");
        return;
    }

    size_t len = (size_t)(slash - path);
    if (len == 0) {
        snprintf(out, outsz, "/");
        return;
    }

    if (len >= outsz) {
        len = outsz - 1;
    }

    memcpy(out, path, len);
    out[len] = '\0';
}

void resolve_include_path(const char *base_path, const char *include_path, char *out, size_t outsz) {
    if (!include_path || !out || outsz == 0) {
        return;
    }

    out[0] = '\0';

    char expanded[CONFIG_PATH_MAX];
    expand_home_path(include_path, expanded, sizeof(expanded));

    if (expanded[0] == '/') {
        snprintf(out, outsz, "%s", expanded);
        return;
    }

    char base_expanded[CONFIG_PATH_MAX];
    expand_home_path(base_path ? base_path : "", base_expanded, sizeof(base_expanded));

    char base_dir[CONFIG_PATH_MAX];
    dir_from_path(base_expanded, base_dir, sizeof(base_dir));

    snprintf(out, outsz, "%s/%s", base_dir, expanded);
}

size_t split_command_argv(const char *src, char storage[CMD_MAX_ARGS][256], const char **argv, size_t max_args) {
    if (!src || !argv || !storage || max_args == 0) {
        return 0;
    }

    for (size_t i = 0; i < max_args; i++) {
        argv[i] = NULL;
        storage[i][0] = '\0';
    }

    size_t argc = 0;
    const char *p = src;

    while (*p) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }

        if (!*p) {
            break;
        }

        if (argc + 1 >= max_args) {
            break;
        }

        char *dst = storage[argc];
        size_t di = 0;
        bool in_single = false;
        bool in_double = false;

        while (*p) {
            unsigned char ch = (unsigned char)*p;

            if (!in_single && !in_double && isspace(ch)) {
                break;
            }

            if (!in_double && ch == '\'') {
                in_single = !in_single;
                p++;
                continue;
            }

            if (!in_single && ch == '"') {
                in_double = !in_double;
                p++;
                continue;
            }

            if (ch == '\\') {
                p++;
                if (!*p) {
                    break;
                }
                ch = (unsigned char)*p;
            }

            if (di + 1 < 256) {
                dst[di++] = (char)ch;
            }

            p++;
        }

        dst[di] = '\0';

        if (dst[0] != '\0') {
            argv[argc++] = dst;
        }

        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
    }

    argv[argc] = NULL;
    return argc;
}

bool split_config_kv(char *line, char **key_out, char **val_out) {
    if (!line || !key_out || !val_out) {
        return false;
    }

    *key_out = NULL;
    *val_out = NULL;

    char *p = trim_whitespace(line);
    if (*p == '\0') {
        return false;
    }

    char *key = p;
    while (*p && !isspace((unsigned char)*p)) {
        p++;
    }

    if (*p == '\0') {
        return false;
    }

    *p = '\0';
    p++;

    char *val = trim_whitespace(p);
    if (*val == '\0') {
        return false;
    }

    *key_out = key;
    *val_out = val;
    return true;
}

void load_config_file_recursive(const char *path, int depth) {
    if (!path || path[0] == '\0') {
        return;
    }

    if (depth > CONFIG_INCLUDE_MAX_DEPTH) {
        fprintf(stderr, "vwm: config include depth exceeded while loading %s\n", path);
        return;
    }

    char resolved_path[CONFIG_PATH_MAX];
    expand_home_path(path, resolved_path, sizeof(resolved_path));

    FILE *fp = fopen(resolved_path, "r");
    if (!fp) {
        fprintf(stderr, "vwm: could not open config file: %s\n", resolved_path);
        return;
    }

    char line[1024];

    while (fgets(line, sizeof(line), fp)) {
        strip_comment(line);

        char *raw = trim_whitespace(line);
        if (*raw == '\0') {
            continue;
        }

        char *key = NULL;
        char *val = NULL;
        if (!split_config_kv(raw, &key, &val)) {
            continue;
        }

        if (strcmp(key, "include") == 0) {
            config_unquote_inplace(val);

            char include_path[CONFIG_PATH_MAX];
            resolve_include_path(resolved_path, val, include_path, sizeof(include_path));

            if (strcmp(include_path, resolved_path) == 0) {
                fprintf(stderr, "vwm: skipping self-include: %s\n", include_path);
                continue;
            }

            load_config_file_recursive(include_path, depth + 1);
            continue;
        }

        if (strcmp(key, "terminal") == 0) {
            config_unquote_inplace(val);
            snprintf(wm.config.terminal, sizeof(wm.config.terminal), "%s", val);
            continue;
        }

        if (strcmp(key, "launcher") == 0) {
            config_unquote_inplace(val);
            snprintf(wm.config.launcher, sizeof(wm.config.launcher), "%s", val);
            continue;
        }

        if (strcmp(key, "scratchpad") == 0) {
            config_unquote_inplace(val);
            snprintf(wm.config.scratchpad, sizeof(wm.config.scratchpad), "%s", val);
            continue;
        }

        if (strcmp(key, "scratchpad_class") == 0) {
            config_unquote_inplace(val);
            snprintf(wm.config.scratchpad_class, sizeof(wm.config.scratchpad_class), "%s", val);
            continue;
        }

        if (strcmp(key, "font") == 0 || strcmp(key, "font_family") == 0) {
            config_unquote_inplace(val);
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

        fprintf(stderr, "vwm: unknown config key '%s' in %s\n", key, resolved_path);
    }

    fclose(fp);
    rebuild_config_commands();
}

void load_config_file(const char *path) {
    load_config_file_recursive(path, 0);
}

void load_default_config(void) {
    memset(&wm.config, 0, sizeof(wm.config));

    snprintf(
        wm.config.path,
        sizeof(wm.config.path),
        "%s/.config/vwm/vwm.conf",
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
    snprintf(wm.config.launcher, sizeof(wm.config.launcher), "rofi -show drun");
    snprintf(wm.config.scratchpad, sizeof(wm.config.scratchpad), "kitty");
    snprintf(wm.config.scratchpad_class, sizeof(wm.config.scratchpad_class), "vwm-scratchpad");

    rebuild_config_commands();
    init_default_keybinds();
    load_config_file(wm.config.path);
}

void apply_config(void) {
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

    (void)launcher_fallback;
}

void reload_config(const void *arg) {
    (void)arg;

    load_default_config();
    apply_config();

    fprintf(stderr, "vwm: config reloaded from %s\n", wm.config.path);
}
