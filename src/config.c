#include "config.h"

#include "actions.h"
#include "bar.h"
#include "client.h"
#include "layout.h"
#include "util.h"
#include "x11.h"

DynamicConfig dynconfig = {0};

typedef enum {
    CFG_BLOCK_NONE = 0,
    CFG_BLOCK_GENERAL,
    CFG_BLOCK_THEME,
    CFG_BLOCK_BAR,
    CFG_BLOCK_BAR_MODULES,
    CFG_BLOCK_RULES,
    CFG_BLOCK_AUTOSTART,
    CFG_BLOCK_COMMANDS,
    CFG_BLOCK_SCRATCHPAD,
    CFG_BLOCK_BINDS,
} ConfigBlock;

static bool ascii_case_equal(const char *a, const char *b) {
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

static size_t split_line_tokens(const char *src, char storage[16][256], const char **argv, size_t max_args) {
    if (!src || !storage || !argv || max_args == 0) {
        return 0;
    }

    for (size_t i = 0; i < max_args; i++) {
        storage[i][0] = '\0';
        argv[i] = NULL;
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
    }

    argv[argc] = NULL;
    return argc;
}

static void clear_bar_modules(void) {
    dynconfig.bar_left_count = 0;
    dynconfig.bar_center_count = 0;
    dynconfig.bar_right_count = 0;

    memset(dynconfig.bar_left, 0, sizeof(dynconfig.bar_left));
    memset(dynconfig.bar_center, 0, sizeof(dynconfig.bar_center));
    memset(dynconfig.bar_right, 0, sizeof(dynconfig.bar_right));
}

static void add_float_rule(const char *class_name) {
    if (!class_name || !*class_name) {
        return;
    }

    if (dynconfig.float_rule_count >= MAX_FLOAT_RULES) {
        fprintf(stderr, "vwm: too many float rules, ignoring '%s'\n", class_name);
        return;
    }

    FloatRule *rule = &dynconfig.float_rules[dynconfig.float_rule_count++];
    snprintf(rule->class_name, sizeof(rule->class_name), "%s", class_name);
}

bool class_should_float(const char *class_name) {
    if (!class_name || !*class_name) {
        return false;
    }

    for (size_t i = 0; i < dynconfig.float_rule_count; i++) {
        if (ascii_case_equal(dynconfig.float_rules[i].class_name, class_name)) {
            return true;
        }
    }

    return false;
}

static void add_workspace_rule_full(int workspace, int monitor, const char *class_name) {
    if (!class_name || !*class_name) {
        return;
    }

    if (workspace < 0 || workspace >= WORKSPACE_COUNT) {
        fprintf(stderr, "vwm: invalid workspace rule target %d\n", workspace + 1);
        return;
    }

    if (dynconfig.workspace_rule_count >= MAX_WORKSPACE_RULES) {
        fprintf(stderr, "vwm: too many workspace rules, ignoring '%s'\n", class_name);
        return;
    }

    WorkspaceRule *rule = &dynconfig.workspace_rules[dynconfig.workspace_rule_count++];
    rule->workspace = workspace;
    rule->monitor = monitor;
    snprintf(rule->class_name, sizeof(rule->class_name), "%s", class_name);
}

int class_workspace_rule(const char *class_name) {
    if (!class_name || !*class_name) {
        return -1;
    }

    for (size_t i = 0; i < dynconfig.workspace_rule_count; i++) {
        if (ascii_case_equal(dynconfig.workspace_rules[i].class_name, class_name)) {
            return dynconfig.workspace_rules[i].workspace;
        }
    }

    return -1;
}

int class_monitor_rule(const char *class_name) {
    if (!class_name || !*class_name) {
        return -1;
    }

    for (size_t i = 0; i < dynconfig.workspace_rule_count; i++) {
        if (ascii_case_equal(dynconfig.workspace_rules[i].class_name, class_name) &&
            dynconfig.workspace_rules[i].monitor >= 0) {
            return dynconfig.workspace_rules[i].monitor;
        }
    }

    return -1;
}

DynamicCommand *find_dynamic_command(const char *name) {
    if (!name || !*name) {
        return NULL;
    }

    for (size_t i = 0; i < dynconfig.command_count; i++) {
        if (strcmp(dynconfig.commands[i].name, name) == 0) {
            return &dynconfig.commands[i];
        }
    }

    return NULL;
}

DynamicScratchpad *find_dynamic_scratchpad(const char *name) {
    if (!name || !*name) {
        return NULL;
    }

    for (size_t i = 0; i < dynconfig.scratchpad_count; i++) {
        if (strcmp(dynconfig.scratchpads[i].name, name) == 0) {
            return &dynconfig.scratchpads[i];
        }
    }

    return NULL;
}

static bool add_dynamic_scratchpad(const char *name, const char *cmdline, const char *class_name) {
    if (!name || !*name || !cmdline || !*cmdline) {
        return false;
    }

    if (dynconfig.scratchpad_count >= MAX_DYNAMIC_SCRATCHPADS) {
        fprintf(stderr, "vwm: too many named scratchpads\n");
        return false;
    }

    DynamicScratchpad *sp = &dynconfig.scratchpads[dynconfig.scratchpad_count];
    memset(sp, 0, sizeof(*sp));

    snprintf(sp->name, sizeof(sp->name), "%s", name);
    if (class_name && *class_name) {
        snprintf(sp->class_name, sizeof(sp->class_name), "%s", class_name);
    }
    split_command_argv(cmdline, sp->storage, sp->argv, CMD_MAX_ARGS);

    if (!sp->argv[0]) {
        fprintf(stderr, "vwm: scratchpad '%s' has empty command\n", name);
        return false;
    }

    dynconfig.scratchpad_count++;
    return true;
}

static void sync_named_command_to_builtin(const char *name, const char *cmdline) {
    if (!name || !cmdline) {
        return;
    }

    if (strcmp(name, "terminal") == 0) {
        snprintf(wm.config.terminal, sizeof(wm.config.terminal), "%s", cmdline);
        return;
    }

    if (strcmp(name, "launcher") == 0) {
        snprintf(wm.config.launcher, sizeof(wm.config.launcher), "%s", cmdline);
        return;
    }
}

static bool add_dynamic_command(const char *name, const char *cmdline) {
    if (!name || !*name || !cmdline || !*cmdline) {
        return false;
    }

    DynamicCommand *existing = find_dynamic_command(name);
    if (existing) {
        memset(existing->storage, 0, sizeof(existing->storage));
        memset(existing->argv, 0, sizeof(existing->argv));
        split_command_argv(cmdline, existing->storage, existing->argv, CMD_MAX_ARGS);

        if (!existing->argv[0]) {
            fprintf(stderr, "vwm: command '%s' has empty argv\n", name);
            return false;
        }

        sync_named_command_to_builtin(name, cmdline);
        return true;
    }

    if (dynconfig.command_count >= MAX_DYNAMIC_COMMANDS) {
        fprintf(stderr, "vwm: too many commands, ignoring '%s'\n", name);
        return false;
    }

    DynamicCommand *cmd = &dynconfig.commands[dynconfig.command_count++];
    memset(cmd, 0, sizeof(*cmd));

    snprintf(cmd->name, sizeof(cmd->name), "%s", name);
    split_command_argv(cmdline, cmd->storage, cmd->argv, CMD_MAX_ARGS);

    if (!cmd->argv[0]) {
        dynconfig.command_count--;
        fprintf(stderr, "vwm: command '%s' has empty argv\n", name);
        return false;
    }

    sync_named_command_to_builtin(name, cmdline);
    return true;
}

static bool parse_combo(const char *combo, xcb_keysym_t *sym_out, uint16_t *mod_out) {
    if (!combo || !*combo || !sym_out || !mod_out) {
        return false;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "%s", combo);

    uint16_t mod = 0;
    xcb_keysym_t sym = XCB_NO_SYMBOL;

    char *saveptr = NULL;
    char *tok = strtok_r(buf, "+", &saveptr);

    while (tok) {
        if (ascii_case_equal(tok, "mod") || ascii_case_equal(tok, "super") || ascii_case_equal(tok, "win")) {
            mod |= MOD_MASK;
        } else if (ascii_case_equal(tok, "shift")) {
            mod |= XCB_MOD_MASK_SHIFT;
        } else if (ascii_case_equal(tok, "ctrl") || ascii_case_equal(tok, "control")) {
            mod |= XCB_MOD_MASK_CONTROL;
        } else if (ascii_case_equal(tok, "alt")) {
            mod |= XCB_MOD_MASK_1;
        } else {
            KeySym ks = XStringToKeysym(tok);
            if (ks == NoSymbol && strlen(tok) == 1) {
                char one[2] = { tok[0], '\0' };
                ks = XStringToKeysym(one);
            }
            if (ks == NoSymbol) {
                return false;
            }
            sym = (xcb_keysym_t)ks;
        }

        tok = strtok_r(NULL, "+", &saveptr);
    }

    if (sym == XCB_NO_SYMBOL) {
        return false;
    }

    *sym_out = sym;
    *mod_out = mod;
    return true;
}

static bool parse_builtin_action_name(const char *name, Action *out) {
    if (!name || !out) {
        return false;
    }

    if (strcmp(name, "reload") == 0) { *out = ACTION_RELOAD_CONFIG; return true; }
    if (strcmp(name, "quit") == 0) { *out = ACTION_QUIT; return true; }
    if (strcmp(name, "scratchpad") == 0) { *out = ACTION_TOGGLE_SCRATCHPAD; return true; }
    if (strcmp(name, "focus_next") == 0) { *out = ACTION_FOCUS_NEXT; return true; }
    if (strcmp(name, "focus_prev") == 0) { *out = ACTION_FOCUS_PREV; return true; }
    if (strcmp(name, "focus_monitor_prev") == 0) { *out = ACTION_FOCUS_MONITOR_PREV; return true; }
    if (strcmp(name, "focus_monitor_next") == 0) { *out = ACTION_FOCUS_MONITOR_NEXT; return true; }
    if (strcmp(name, "send_monitor_prev") == 0) { *out = ACTION_SEND_MONITOR_PREV; return true; }
    if (strcmp(name, "send_monitor_next") == 0) { *out = ACTION_SEND_MONITOR_NEXT; return true; }
    if (strcmp(name, "monocle") == 0 || strcmp(name, "toggle_monocle") == 0) { *out = ACTION_TOGGLE_MONOCLE; return true; }
    if (strcmp(name, "fullscreen") == 0 || strcmp(name, "toggle_fullscreen") == 0) { *out = ACTION_TOGGLE_TRUE_FULLSCREEN; return true; }
    if (strcmp(name, "toggle_sync") == 0 || strcmp(name, "sync") == 0) { *out = ACTION_TOGGLE_SYNC; return true; }
    if (strcmp(name, "kill_client") == 0 || strcmp(name, "kill") == 0) { *out = ACTION_KILL_CLIENT; return true; }
    if (strcmp(name, "decrease_mfact") == 0) { *out = ACTION_DECREASE_MFACT; return true; }
    if (strcmp(name, "increase_mfact") == 0) { *out = ACTION_INCREASE_MFACT; return true; }
    if (strcmp(name, "zoom_master") == 0) { *out = ACTION_ZOOM_MASTER; return true; }

    if (strcmp(name, "view_ws_1") == 0) { *out = ACTION_VIEW_WS_1; return true; }
    if (strcmp(name, "view_ws_2") == 0) { *out = ACTION_VIEW_WS_2; return true; }
    if (strcmp(name, "view_ws_3") == 0) { *out = ACTION_VIEW_WS_3; return true; }
    if (strcmp(name, "view_ws_4") == 0) { *out = ACTION_VIEW_WS_4; return true; }
    if (strcmp(name, "view_ws_5") == 0) { *out = ACTION_VIEW_WS_5; return true; }
    if (strcmp(name, "view_ws_6") == 0) { *out = ACTION_VIEW_WS_6; return true; }
    if (strcmp(name, "view_ws_7") == 0) { *out = ACTION_VIEW_WS_7; return true; }
    if (strcmp(name, "view_ws_8") == 0) { *out = ACTION_VIEW_WS_8; return true; }
    if (strcmp(name, "view_ws_9") == 0) { *out = ACTION_VIEW_WS_9; return true; }

    if (strcmp(name, "send_ws_1") == 0) { *out = ACTION_SEND_WS_1; return true; }
    if (strcmp(name, "send_ws_2") == 0) { *out = ACTION_SEND_WS_2; return true; }
    if (strcmp(name, "send_ws_3") == 0) { *out = ACTION_SEND_WS_3; return true; }
    if (strcmp(name, "send_ws_4") == 0) { *out = ACTION_SEND_WS_4; return true; }
    if (strcmp(name, "send_ws_5") == 0) { *out = ACTION_SEND_WS_5; return true; }
    if (strcmp(name, "send_ws_6") == 0) { *out = ACTION_SEND_WS_6; return true; }
    if (strcmp(name, "send_ws_7") == 0) { *out = ACTION_SEND_WS_7; return true; }
    if (strcmp(name, "send_ws_8") == 0) { *out = ACTION_SEND_WS_8; return true; }
    if (strcmp(name, "send_ws_9") == 0) { *out = ACTION_SEND_WS_9; return true; }

    return false;
}

static bool parse_workspace_action(const char *verb, const char *arg, Action *out) {
    if (!verb || !arg || !out) {
        return false;
    }

    int n = atoi(arg);
    if (n < 1 || n > WORKSPACE_COUNT) {
        return false;
    }

    if (strcmp(verb, "view_ws") == 0) {
        switch (n) {
            case 1: *out = ACTION_VIEW_WS_1; return true;
            case 2: *out = ACTION_VIEW_WS_2; return true;
            case 3: *out = ACTION_VIEW_WS_3; return true;
            case 4: *out = ACTION_VIEW_WS_4; return true;
            case 5: *out = ACTION_VIEW_WS_5; return true;
            case 6: *out = ACTION_VIEW_WS_6; return true;
            case 7: *out = ACTION_VIEW_WS_7; return true;
            case 8: *out = ACTION_VIEW_WS_8; return true;
            case 9: *out = ACTION_VIEW_WS_9; return true;
            default: return false;
        }
    }

    if (strcmp(verb, "send_ws") == 0) {
        switch (n) {
            case 1: *out = ACTION_SEND_WS_1; return true;
            case 2: *out = ACTION_SEND_WS_2; return true;
            case 3: *out = ACTION_SEND_WS_3; return true;
            case 4: *out = ACTION_SEND_WS_4; return true;
            case 5: *out = ACTION_SEND_WS_5; return true;
            case 6: *out = ACTION_SEND_WS_6; return true;
            case 7: *out = ACTION_SEND_WS_7; return true;
            case 8: *out = ACTION_SEND_WS_8; return true;
            case 9: *out = ACTION_SEND_WS_9; return true;
            default: return false;
        }
    }

    return false;
}

static bool add_dynamic_keybind_command(const char *combo, const char *command_name) {
    if (!combo || !*combo || !command_name || !*command_name) {
        return false;
    }

    if (dynconfig.keybind_count >= MAX_DYNAMIC_KEYBINDS) {
        fprintf(stderr, "vwm: too many dynamic keybinds\n");
        return false;
    }

    DynamicKeybind *kb = &dynconfig.keybinds[dynconfig.keybind_count];
    memset(kb, 0, sizeof(*kb));

    if (!parse_combo(combo, &kb->sym, &kb->mod)) {
        fprintf(stderr, "vwm: invalid key combo '%s'\n", combo);
        return false;
    }

    kb->kind = DYNKEY_COMMAND;
    snprintf(kb->target_name, sizeof(kb->target_name), "%s", command_name);

    dynconfig.keybind_count++;
    return true;
}

static bool add_dynamic_keybind_scratchpad(const char *combo, const char *scratchpad_name) {
    if (!combo || !*combo || !scratchpad_name || !*scratchpad_name) {
        return false;
    }

    if (dynconfig.keybind_count >= MAX_DYNAMIC_KEYBINDS) {
        fprintf(stderr, "vwm: too many dynamic keybinds\n");
        return false;
    }

    DynamicKeybind *kb = &dynconfig.keybinds[dynconfig.keybind_count];
    memset(kb, 0, sizeof(*kb));

    if (!parse_combo(combo, &kb->sym, &kb->mod)) {
        fprintf(stderr, "vwm: invalid key combo '%s'\n", combo);
        return false;
    }

    kb->kind = DYNKEY_SCRATCHPAD;
    snprintf(kb->target_name, sizeof(kb->target_name), "%s", scratchpad_name);

    dynconfig.keybind_count++;
    return true;
}

static bool add_dynamic_keybind_builtin(const char *combo, Action action) {
    if (!combo || !*combo) {
        return false;
    }

    if (dynconfig.keybind_count >= MAX_DYNAMIC_KEYBINDS) {
        fprintf(stderr, "vwm: too many dynamic keybinds\n");
        return false;
    }

    DynamicKeybind *kb = &dynconfig.keybinds[dynconfig.keybind_count];
    memset(kb, 0, sizeof(*kb));

    if (!parse_combo(combo, &kb->sym, &kb->mod)) {
        fprintf(stderr, "vwm: invalid key combo '%s'\n", combo);
        return false;
    }

    kb->kind = DYNKEY_BUILTIN;
    kb->action = action;

    dynconfig.keybind_count++;
    return true;
}

bool execute_dynamic_keybind(xcb_keysym_t sym, uint16_t mod) {
    for (size_t i = 0; i < dynconfig.keybind_count; i++) {
        DynamicKeybind *kb = &dynconfig.keybinds[i];

        if (kb->sym != sym || kb->mod != mod) {
            continue;
        }

        if (kb->kind == DYNKEY_COMMAND) {
            DynamicCommand *cmd = find_dynamic_command(kb->target_name);
            if (!cmd || !cmd->argv[0]) {
                fprintf(stderr, "vwm: dynamic keybind references unknown command '%s'\n", kb->target_name);
                return true;
            }
            spawn(cmd->argv);
            return true;
        }

        if (kb->kind == DYNKEY_BUILTIN) {
            dispatch_action(kb->action);
            return true;
        }

        if (kb->kind == DYNKEY_SCRATCHPAD) {
            toggle_named_scratchpad(kb->target_name);
            return true;
        }

        return true;
    }

    return false;
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

    if (strcmp(s, "1") == 0 || strcmp(s, "true") == 0 || strcmp(s, "yes") == 0 || strcmp(s, "on") == 0) {
        *out = true;
        return true;
    }

    if (strcmp(s, "0") == 0 || strcmp(s, "false") == 0 || strcmp(s, "no") == 0 || strcmp(s, "off") == 0) {
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

    dynconfig.bar_style.height = MAX(18, dynconfig.bar_style.height);
    dynconfig.bar_style.gap = MAX(0, dynconfig.bar_style.gap);
    dynconfig.bar_style.padding_x = MAX(0, dynconfig.bar_style.padding_x);
    dynconfig.bar_style.padding_y = MAX(0, dynconfig.bar_style.padding_y);
    dynconfig.bar_style.radius = MAX(0, dynconfig.bar_style.radius);
    dynconfig.bar_style.margin_x = MAX(0, dynconfig.bar_style.margin_x);
    dynconfig.bar_style.margin_y = MAX(0, dynconfig.bar_style.margin_y);
    dynconfig.bar_style.content_margin_x = MAX(0, dynconfig.bar_style.content_margin_x);
    dynconfig.bar_style.content_margin_y = MAX(0, dynconfig.bar_style.content_margin_y);
    dynconfig.bar_style.volume_bar_width = MAX(0, dynconfig.bar_style.volume_bar_width);
    dynconfig.bar_style.volume_bar_height = MAX(0, dynconfig.bar_style.volume_bar_height);
    dynconfig.bar_style.volume_bar_radius = MAX(0, dynconfig.bar_style.volume_bar_radius);

    wm.config.scratchpad_width_pct = CLAMP(wm.config.scratchpad_width_pct, 40, 100);
    wm.config.scratchpad_height_pct = CLAMP(wm.config.scratchpad_height_pct, 40, 100);
    wm.config.scratchpad_dim_alpha = CLAMP(wm.config.scratchpad_dim_alpha, 0, 255);

    wm.config.bar_height = dynconfig.bar_style.height;

    if (wm.font_height > 0) {
        int minimum_bar_h = wm.font_height + 8 + dynconfig.bar_style.content_margin_y;
        wm.config.bar_height = MAX(wm.config.bar_height, minimum_bar_h);
        dynconfig.bar_style.height = wm.config.bar_height;
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

static bool parse_bar_modules_style_value(const char *name, BarModuleStyle *out) {
    if (!name || !out) {
        return false;
    }

    if (strcmp(name, "flat") == 0) {
        *out = BAR_MODULE_STYLE_FLAT;
        return true;
    }

    if (strcmp(name, "pill") == 0 || strcmp(name, "pills") == 0) {
        *out = BAR_MODULE_STYLE_PILL;
        return true;
    }

    return false;
}

static bool parse_bar_module_kind(const char *name, BarModuleKind *out) {
    if (!name || !out) {
        return false;
    }

    if (strcmp(name, "workspaces") == 0) { *out = BAR_MOD_WORKSPACES; return true; }
    if (strcmp(name, "monitor") == 0) { *out = BAR_MOD_MONITOR; return true; }
    if (strcmp(name, "sync") == 0) { *out = BAR_MOD_SYNC; return true; }
    if (strcmp(name, "title") == 0) { *out = BAR_MOD_TITLE; return true; }
    if (strcmp(name, "status") == 0) { *out = BAR_MOD_STATUS; return true; }
    if (strcmp(name, "clock") == 0) { *out = BAR_MOD_CLOCK; return true; }
    if (strcmp(name, "custom") == 0) { *out = BAR_MOD_CUSTOM; return true; }
    if (strcmp(name, "volume") == 0) { *out = BAR_MOD_VOLUME; return true; }
    if (strcmp(name, "network") == 0 || strcmp(name, "wifi") == 0) { *out = BAR_MOD_NETWORK; return true; }
    if (strcmp(name, "battery") == 0) { *out = BAR_MOD_BATTERY; return true; }
    if (strcmp(name, "brightness") == 0) { *out = BAR_MOD_BRIGHTNESS; return true; }
    if (strcmp(name, "media") == 0) { *out = BAR_MOD_MEDIA; return true; }
    if (strcmp(name, "memory") == 0 || strcmp(name, "ram") == 0) { *out = BAR_MOD_MEMORY; return true; }
    if (strcmp(name, "weather") == 0) { *out = BAR_MOD_WEATHER; return true; }

    if (strncmp(name, "script:", 7) == 0) {
        const char *script_name = name + 7;
        if (*script_name) {
            find_or_create_script_module(script_name);
            *out = BAR_MOD_SCRIPT;
            return true;
        }
    }

    return false;
}

static bool parse_bar_position_value(const char *name, BarPosition *out) {
    if (!name || !out) {
        return false;
    }

    if (strcmp(name, "top") == 0) {
        *out = BAR_POSITION_TOP;
        return true;
    }

    if (strcmp(name, "bottom") == 0) {
        *out = BAR_POSITION_BOTTOM;
        return true;
    }

    return false;
}

static void init_default_theme(void) {
    dynconfig.theme.bg = 0x111111;
    dynconfig.theme.surface = 0x1b1b1b;
    dynconfig.theme.text = 0xf2f2f2;
    dynconfig.theme.text_muted = 0x8c8c8c;
    dynconfig.theme.accent = 0x6bacac;
    dynconfig.theme.accent_soft = 0x458588;
    dynconfig.theme.border = 0x353535;
}

static void init_default_bar_style(void) {
    dynconfig.bar_enabled = true;

    dynconfig.bar_style.height = 24;
    dynconfig.bar_style.position = BAR_POSITION_TOP;
    dynconfig.bar_style.modules = BAR_MODULE_STYLE_FLAT;

    dynconfig.bar_style.background_enabled = true;
    dynconfig.bar_style.use_icons = true;
    dynconfig.bar_style.use_colors = true;

    dynconfig.bar_style.gap = 18;

    dynconfig.bar_style.padding_x = 12;
    dynconfig.bar_style.padding_y = 6;

    dynconfig.bar_style.radius = 18;

    dynconfig.bar_style.margin_x = 18;
    dynconfig.bar_style.margin_y = 10;

    dynconfig.bar_style.content_margin_x = 14;
    dynconfig.bar_style.content_margin_y = 2;

    dynconfig.bar_style.volume_bar_enabled = true;
    dynconfig.bar_style.volume_bar_width = 46;
    dynconfig.bar_style.volume_bar_height = 6;
    dynconfig.bar_style.volume_bar_radius = 10;
}

static void derive_theme_to_runtime(void) {
    wm.config.bar_bg = dynconfig.theme.bg;
    wm.config.bar_fg = dynconfig.theme.text;

    wm.config.border_active = dynconfig.theme.accent;
    wm.config.border_inactive = dynconfig.theme.border;
    wm.config.border_urgent = dynconfig.theme.accent;

    wm.config.workspace_current = dynconfig.theme.accent;
    wm.config.workspace_occupied = dynconfig.theme.border;
    wm.config.workspace_empty = dynconfig.theme.text_muted;

    wm.config.bar_height = dynconfig.bar_style.height;
}

void rebuild_config_commands(void) {
    memset(wm.config.term_cmd_storage, 0, sizeof(wm.config.term_cmd_storage));
    memset(wm.config.launcher_cmd_storage, 0, sizeof(wm.config.launcher_cmd_storage));
    memset(wm.config.scratchpad_cmd_storage, 0, sizeof(wm.config.scratchpad_cmd_storage));

    memset(wm.config.term_cmd, 0, sizeof(wm.config.term_cmd));
    memset(wm.config.launcher_cmd, 0, sizeof(wm.config.launcher_cmd));
    memset(wm.config.scratchpad_cmd, 0, sizeof(wm.config.scratchpad_cmd));

    split_command_argv(wm.config.terminal, wm.config.term_cmd_storage, wm.config.term_cmd, CMD_MAX_ARGS);
    split_command_argv(wm.config.launcher, wm.config.launcher_cmd_storage, wm.config.launcher_cmd, CMD_MAX_ARGS);
    split_command_argv(wm.config.scratchpad, wm.config.scratchpad_cmd_storage, wm.config.scratchpad_cmd, CMD_MAX_ARGS);
}

void init_default_keybinds(void) {
    wm.config.keybind_count = 0;
}

static bool add_bar_module(BarModule *arr, size_t *count, const char *kind_name, const char *arg, const char *context) {
    if (!arr || !count || !kind_name) {
        return false;
    }

    if (*count >= MAX_BAR_MODULES_PER_SECTION) {
        fprintf(stderr, "vwm: too many bar modules in %s\n", context ? context : "modules");
        return false;
    }

    BarModuleKind kind;
    if (!parse_bar_module_kind(kind_name, &kind)) {
        fprintf(stderr, "vwm: unknown bar module '%s'\n", kind_name);
        return false;
    }

    BarModule *mod = &arr[*count];
    memset(mod, 0, sizeof(*mod));
    mod->kind = kind;

    if (kind == BAR_MOD_SCRIPT && strncmp(kind_name, "script:", 7) == 0) {
        snprintf(mod->arg, sizeof(mod->arg), "%s", kind_name + 7);
    } else if (arg && *arg) {
        snprintf(mod->arg, sizeof(mod->arg), "%s", arg);
    }

    (*count)++;
    return true;
}

static bool parse_general_line(const char *raw) {
    char storage[16][256] = {{0}};
    const char *argv[16] = {0};
    size_t argc = split_line_tokens(raw, storage, argv, LENGTH(argv));

    if (argc < 2) {
        return false;
    }

    if (strcmp(argv[0], "font") == 0 || strcmp(argv[0], "font_family") == 0) {
        snprintf(wm.config.font_family, sizeof(wm.config.font_family), "%s", argv[1]);
        return true;
    }

    if (strcmp(argv[0], "font_size") == 0) {
        wm.config.font_size = strtof(argv[1], NULL);
        return true;
    }

    if (strcmp(argv[0], "border_px") == 0 || strcmp(argv[0], "border_width") == 0) {
        wm.config.border_width = atoi(argv[1]);
        return true;
    }

    if (strcmp(argv[0], "gap_px") == 0) {
        wm.config.gap_px = atoi(argv[1]);
        return true;
    }

    if (strcmp(argv[0], "default_mfact") == 0) {
        wm.config.default_mfact = strtof(argv[1], NULL);
        return true;
    }

    if (strcmp(argv[0], "sync_workspaces") == 0) {
        bool b = false;
        if (parse_bool_value(argv[1], &b)) {
            wm.config.sync_workspaces = b;
        }
        return true;
    }

    fprintf(stderr, "vwm: unknown general key '%s'\n", argv[0]);
    return true;
}

static bool parse_theme_line(const char *raw) {
    char storage[16][256] = {{0}};
    const char *argv[16] = {0};
    size_t argc = split_line_tokens(raw, storage, argv, LENGTH(argv));

    if (argc < 2) {
        return false;
    }

    uint32_t c = 0;
    if (!parse_color_value(argv[1], &c)) {
        fprintf(stderr, "vwm: invalid theme color '%s'\n", argv[1]);
        return true;
    }

    if (strcmp(argv[0], "bg") == 0 || strcmp(argv[0], "background") == 0) { dynconfig.theme.bg = c; return true; }
    if (strcmp(argv[0], "surface") == 0) { dynconfig.theme.surface = c; return true; }
    if (strcmp(argv[0], "text") == 0) { dynconfig.theme.text = c; return true; }
    if (strcmp(argv[0], "text_muted") == 0 || strcmp(argv[0], "muted") == 0) { dynconfig.theme.text_muted = c; return true; }
    if (strcmp(argv[0], "accent") == 0) { dynconfig.theme.accent = c; return true; }
    if (strcmp(argv[0], "accent_soft") == 0) { dynconfig.theme.accent_soft = c; return true; }
    if (strcmp(argv[0], "border") == 0) { dynconfig.theme.border = c; return true; }

    fprintf(stderr, "vwm: unknown theme key '%s'\n", argv[0]);
    return true;
}

static bool parse_bar_line(const char *raw) {
    char storage[16][256] = {{0}};
    const char *argv[16] = {0};
    size_t argc = split_line_tokens(raw, storage, argv, LENGTH(argv));

    if (argc < 2) {
        return false;
    }

    if (strcmp(argv[0], "enabled") == 0) {
        bool b = false;
        if (parse_bool_value(argv[1], &b)) {
            dynconfig.bar_enabled = b;
        }
        return true;
    }

    if (strcmp(argv[0], "background") == 0) {
        bool b = false;
        if (parse_bool_value(argv[1], &b)) {
            dynconfig.bar_style.background_enabled = b;
            return true;
        }
        fprintf(stderr, "vwm: invalid bar background '%s'\n", argv[1]);
        return true;
    }

    if (strcmp(argv[0], "modules") == 0) {
        BarModuleStyle mode;
        if (parse_bar_modules_style_value(argv[1], &mode)) {
            dynconfig.bar_style.modules = mode;
        } else {
            fprintf(stderr, "vwm: invalid bar modules style '%s'\n", argv[1]);
        }
        return true;
    }

    if (strcmp(argv[0], "icons") == 0) {
        bool b = false;
        if (parse_bool_value(argv[1], &b)) {
            dynconfig.bar_style.use_icons = b;
        }
        return true;
    }

    if (strcmp(argv[0], "colors") == 0) {
        bool b = false;
        if (parse_bool_value(argv[1], &b)) {
            dynconfig.bar_style.use_colors = b;
        }
        return true;
    }

    if (strcmp(argv[0], "minimal") == 0) {
        bool b = false;
        if (parse_bool_value(argv[1], &b) && b) {
            dynconfig.bar_style.use_icons = false;
            dynconfig.bar_style.use_colors = false;
        }
        return true;
    }

    if (strcmp(argv[0], "position") == 0) {
        BarPosition pos;
        if (parse_bar_position_value(argv[1], &pos)) {
            dynconfig.bar_style.position = pos;
        } else {
            fprintf(stderr, "vwm: invalid bar position '%s'\n", argv[1]);
        }
        return true;
    }

    if (strcmp(argv[0], "height") == 0) {
        dynconfig.bar_style.height = atoi(argv[1]);
        return true;
    }

    if (strcmp(argv[0], "padding_x") == 0) {
        dynconfig.bar_style.padding_x = atoi(argv[1]);
        return true;
    }

    if (strcmp(argv[0], "padding_y") == 0) {
        dynconfig.bar_style.padding_y = atoi(argv[1]);
        return true;
    }

    if (strcmp(argv[0], "gap") == 0) {
        dynconfig.bar_style.gap = atoi(argv[1]);
        return true;
    }

    if (strcmp(argv[0], "radius") == 0) {
        dynconfig.bar_style.radius = atoi(argv[1]);
        return true;
    }

    if (strcmp(argv[0], "margin_x") == 0) {
        dynconfig.bar_style.margin_x = atoi(argv[1]);
        return true;
    }

    if (strcmp(argv[0], "margin_y") == 0) {
        dynconfig.bar_style.margin_y = atoi(argv[1]);
        return true;
    }

    if (strcmp(argv[0], "content_margin_x") == 0) {
        dynconfig.bar_style.content_margin_x = atoi(argv[1]);
        return true;
    }

    if (strcmp(argv[0], "content_margin_y") == 0) {
        dynconfig.bar_style.content_margin_y = atoi(argv[1]);
        return true;
    }

    if (strcmp(argv[0], "volume_bar_enabled") == 0) {
        bool b = false;
        if (parse_bool_value(argv[1], &b)) {
            dynconfig.bar_style.volume_bar_enabled = b;
        }
        return true;
    }

    if (strcmp(argv[0], "volume_bar_width") == 0) {
        dynconfig.bar_style.volume_bar_width = atoi(argv[1]);
        return true;
    }

    if (strcmp(argv[0], "volume_bar_height") == 0) {
        dynconfig.bar_style.volume_bar_height = atoi(argv[1]);
        return true;
    }

    if (strcmp(argv[0], "volume_bar_radius") == 0) {
        dynconfig.bar_style.volume_bar_radius = atoi(argv[1]);
        return true;
    }

    fprintf(stderr, "vwm: unknown bar key '%s'\n", argv[0]);
    return true;
}

static bool parse_rules_line(const char *raw) {
    char storage[16][256] = {{0}};
    const char *argv[16] = {0};
    size_t argc = split_line_tokens(raw, storage, argv, LENGTH(argv));

    if (argc < 3) {
        return false;
    }

    if (strcmp(argv[0], "float") == 0 && strcmp(argv[1], "class") == 0 && argc >= 3) {
        add_float_rule(argv[2]);
        return true;
    }

    if (strcmp(argv[0], "workspace") == 0 && argc >= 4 && strcmp(argv[2], "class") == 0) {
        int workspace = atoi(argv[1]);
        if (workspace >= 1 && workspace <= WORKSPACE_COUNT) {
            int monitor = -1;
            if (argc >= 6 && strcmp(argv[4], "monitor") == 0) {
                monitor = atoi(argv[5]) - 1;
            }
            add_workspace_rule_full(workspace - 1, monitor, argv[3]);
        } else {
            fprintf(stderr, "vwm: invalid workspace number '%s'\n", argv[1]);
        }
        return true;
    }

    fprintf(stderr, "vwm: invalid rules directive\n");
    return true;
}

static bool add_autostart_entry(const char *cmdline) {
    if (!cmdline || !*cmdline) {
        return false;
    }

    if (dynconfig.autostart_count >= MAX_AUTOSTART) {
        fprintf(stderr, "vwm: too many autostart entries\n");
        return false;
    }

    AutostartEntry *entry = &dynconfig.autostart[dynconfig.autostart_count++];
    memset(entry, 0, sizeof(*entry));
    split_command_argv(cmdline, entry->storage, entry->argv, CMD_MAX_ARGS);

    if (!entry->argv[0]) {
        dynconfig.autostart_count--;
        return false;
    }

    return true;
}

static bool parse_autostart_line(const char *raw) {
    char storage[16][256] = {{0}};
    const char *argv[16] = {0};
    size_t argc = split_line_tokens(raw, storage, argv, LENGTH(argv));

    if (argc < 2) {
        return false;
    }

    if (strcmp(argv[0], "run") != 0) {
        fprintf(stderr, "vwm: unknown autostart directive '%s'\n", argv[0]);
        return true;
    }

    if (!add_autostart_entry(argv[1])) {
        return false;
    }

    const char *class_name = NULL;
    int workspace = -1;
    int monitor = -1;

    for (size_t i = 2; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "class") == 0) {
            class_name = argv[i + 1];
        } else if (strcmp(argv[i], "workspace") == 0) {
            workspace = atoi(argv[i + 1]) - 1;
        } else if (strcmp(argv[i], "monitor") == 0) {
            monitor = atoi(argv[i + 1]) - 1;
        }
    }

    if (workspace >= 0 && workspace < WORKSPACE_COUNT && class_name) {
        add_workspace_rule_full(workspace, monitor, class_name);
    } else if (monitor >= 0 && class_name) {
        add_workspace_rule_full(0, monitor, class_name);
    }

    return true;
}

static bool parse_commands_line(const char *raw) {
    char storage[16][256] = {{0}};
    const char *argv[16] = {0};
    size_t argc = split_line_tokens(raw, storage, argv, LENGTH(argv));

    if (argc < 2) {
        return false;
    }

    add_dynamic_command(argv[0], argv[1]);
    return true;
}

static bool add_scratchpad_autostart_entry(const char *cmdline) {
    if (!cmdline || !*cmdline) {
        return false;
    }

    if (dynconfig.scratchpad_autostart_count >= MAX_SCRATCHPAD_AUTOSTART) {
        fprintf(stderr, "vwm: too many scratchpad autostart entries\n");
        return false;
    }

    AutostartEntry *entry = &dynconfig.scratchpad_autostart[dynconfig.scratchpad_autostart_count++];
    memset(entry, 0, sizeof(*entry));
    split_command_argv(cmdline, entry->storage, entry->argv, CMD_MAX_ARGS);

    if (!entry->argv[0]) {
        dynconfig.scratchpad_autostart_count--;
        return false;
    }

    return true;
}

static bool parse_scratchpad_overlay_line(const char *raw) {
    char storage[16][256] = {{0}};
    const char *argv[16] = {0};
    size_t argc = split_line_tokens(raw, storage, argv, LENGTH(argv));

    if (argc < 2) {
        return false;
    }

    if (strcmp(argv[0], "autostart") == 0) {
        add_scratchpad_autostart_entry(argv[1]);
        return true;
    }

    if (strcmp(argv[0], "command") == 0) {
        snprintf(wm.config.scratchpad, sizeof(wm.config.scratchpad), "%s", argv[1]);
        return true;
    }

    if (strcmp(argv[0], "width_pct") == 0) {
        wm.config.scratchpad_width_pct = CLAMP(atoi(argv[1]), 40, 100);
        return true;
    }

    if (strcmp(argv[0], "height_pct") == 0) {
        wm.config.scratchpad_height_pct = CLAMP(atoi(argv[1]), 40, 100);
        return true;
    }

    if (strcmp(argv[0], "dim_alpha") == 0) {
        wm.config.scratchpad_dim_alpha = CLAMP(atoi(argv[1]), 0, 255);
        return true;
    }

    if (strcmp(argv[0], "define") == 0 && argc >= 3) {
        const char *name = argv[1];
        const char *cmd = argv[2];
        const char *cls = NULL;

        for (size_t i = 3; i + 1 < argc; i += 2) {
            if (strcmp(argv[i], "class") == 0) {
                cls = argv[i + 1];
            }
        }

        add_dynamic_scratchpad(name, cmd, cls);
        return true;
    }

    fprintf(stderr, "vwm: unknown scratchpad key '%s'\n", argv[0]);
    return true;
}


static bool parse_binds_line(const char *raw) {
    char storage[16][256] = {{0}};
    const char *argv[16] = {0};
    size_t argc = split_line_tokens(raw, storage, argv, LENGTH(argv));

    if (argc < 2) {
        return false;
    }

    size_t base = 0;
    if (strcmp(argv[0], "bind") == 0) {
        base = 1;
    }

    if (argc < base + 2) {
        return false;
    }

    const char *combo = argv[base + 0];
    const char *verb = argv[base + 1];

    if (strcmp(verb, "spawn") == 0 && argc >= base + 3) {
        add_dynamic_keybind_command(combo, argv[base + 2]);
        return true;
    }

    if (strcmp(verb, "scratchpad") == 0 && argc >= base + 3) {
        add_dynamic_keybind_scratchpad(combo, argv[base + 2]);
        return true;
    }

    Action ws_action;
    if ((strcmp(verb, "view_ws") == 0 || strcmp(verb, "send_ws") == 0) && argc >= base + 3) {
        if (parse_workspace_action(verb, argv[base + 2], &ws_action)) {
            add_dynamic_keybind_builtin(combo, ws_action);
            return true;
        }

        fprintf(stderr, "vwm: invalid workspace bind argument '%s'\n", argv[base + 2]);
        return true;
    }

    Action action;
    if (parse_builtin_action_name(verb, &action)) {
        add_dynamic_keybind_builtin(combo, action);
        return true;
    }

    fprintf(stderr, "vwm: unknown bind action '%s'\n", verb);
    return true;
}

static bool parse_bar_modules_line(const char *raw) {
    char storage[16][256] = {{0}};
    const char *argv[16] = {0};
    size_t argc = split_line_tokens(raw, storage, argv, LENGTH(argv));

    if (argc < 2) {
        return false;
    }

    const char *section = argv[0];
    const char *kind = argv[1];
    const char *arg = (argc >= 3) ? argv[2] : "";

    if (strcmp(section, "left") == 0) {
        add_bar_module(dynconfig.bar_left, &dynconfig.bar_left_count, kind, arg, "bar.modules.left");
        return true;
    }

    if (strcmp(section, "center") == 0) {
        add_bar_module(dynconfig.bar_center, &dynconfig.bar_center_count, kind, arg, "bar.modules.center");
        return true;
    }

    if (strcmp(section, "right") == 0) {
        add_bar_module(dynconfig.bar_right, &dynconfig.bar_right_count, kind, arg, "bar.modules.right");
        return true;
    }

    fprintf(stderr, "vwm: invalid modules section '%s'\n", section);
    return true;
}

static bool is_root_block_name(const char *name) {
    if (!name) {
        return false;
    }

    return
        strcmp(name, "general") == 0 ||
        strcmp(name, "theme") == 0 ||
        strcmp(name, "bar") == 0 ||
        strcmp(name, "rules") == 0 ||
        strcmp(name, "autostart") == 0 ||
        strcmp(name, "commands") == 0 ||
        strcmp(name, "scratchpad") == 0 ||
        strcmp(name, "binds") == 0;
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
    ConfigBlock block = CFG_BLOCK_NONE;
    ConfigBlock parent_block = CFG_BLOCK_NONE;

    while (fgets(line, sizeof(line), fp)) {
        strip_comment(line);

        char *raw = trim_whitespace(line);
        if (*raw == '\0') {
            continue;
        }

        if (strcmp(raw, "}") == 0) {
            if (parent_block != CFG_BLOCK_NONE) {
                block = parent_block;
                parent_block = CFG_BLOCK_NONE;
            } else {
                block = CFG_BLOCK_NONE;
            }
            continue;
        }

        if (block == CFG_BLOCK_NONE) {
            char storage[16][256] = {{0}};
            const char *argv[16] = {0};
            size_t argc = split_line_tokens(raw, storage, argv, LENGTH(argv));

            if (argc >= 2 && strcmp(argv[0], "include") == 0) {
                char include_path[CONFIG_PATH_MAX];
                resolve_include_path(resolved_path, argv[1], include_path, sizeof(include_path));

                if (strcmp(include_path, resolved_path) == 0) {
                    fprintf(stderr, "vwm: skipping self-include: %s\n", include_path);
                    continue;
                }

                load_config_file_recursive(include_path, depth + 1);
                continue;
            }
        }

        size_t len = strlen(raw);
        if (len >= 1 && raw[len - 1] == '{') {
            raw[len - 1] = '\0';
            char *name = trim_whitespace(raw);

            if (block == CFG_BLOCK_NONE) {
                if (!is_root_block_name(name)) {
                    fprintf(stderr, "vwm: unknown block '%s' in %s\n", name, resolved_path);
                    continue;
                }

                if (strcmp(name, "general") == 0) { block = CFG_BLOCK_GENERAL; continue; }
                if (strcmp(name, "theme") == 0) { block = CFG_BLOCK_THEME; continue; }
                if (strcmp(name, "bar") == 0) { block = CFG_BLOCK_BAR; continue; }
                if (strcmp(name, "rules") == 0) { block = CFG_BLOCK_RULES; continue; }
                if (strcmp(name, "autostart") == 0) { block = CFG_BLOCK_AUTOSTART; continue; }
                if (strcmp(name, "commands") == 0) { block = CFG_BLOCK_COMMANDS; continue; }
                if (strcmp(name, "scratchpad") == 0) { block = CFG_BLOCK_SCRATCHPAD; continue; }
                if (strcmp(name, "binds") == 0) { block = CFG_BLOCK_BINDS; continue; }

                continue;
            }

            if (parent_block != CFG_BLOCK_NONE) {
                fprintf(stderr, "vwm: nested block depth > 1 is not supported in %s\n", resolved_path);
                continue;
            }

            if (block == CFG_BLOCK_BAR && strcmp(name, "modules") == 0) {
                clear_bar_modules();
                parent_block = block;
                block = CFG_BLOCK_BAR_MODULES;
                continue;
            }

            fprintf(stderr, "vwm: nested block '%s' is not valid here in %s\n", name, resolved_path);
            continue;
        }

        if (block == CFG_BLOCK_NONE) {
            fprintf(stderr, "vwm: directive outside block in %s: %s\n", resolved_path, raw);
            continue;
        }

        switch (block) {
            case CFG_BLOCK_GENERAL:
                parse_general_line(raw);
                break;
            case CFG_BLOCK_THEME:
                parse_theme_line(raw);
                break;
            case CFG_BLOCK_BAR:
                parse_bar_line(raw);
                break;
            case CFG_BLOCK_BAR_MODULES:
                parse_bar_modules_line(raw);
                break;
            case CFG_BLOCK_RULES:
                parse_rules_line(raw);
                break;
            case CFG_BLOCK_AUTOSTART:
                parse_autostart_line(raw);
                break;
            case CFG_BLOCK_COMMANDS:
                parse_commands_line(raw);
                break;
            case CFG_BLOCK_SCRATCHPAD:
                parse_scratchpad_overlay_line(raw);
                break;
            case CFG_BLOCK_BINDS:
                parse_binds_line(raw);
                break;
            default:
                break;
        }
    }

    fclose(fp);
    rebuild_config_commands();
}

void load_config_file(const char *path) {
    load_config_file_recursive(path, 0);
}

void load_default_config(void) {
    memset(&wm.config, 0, sizeof(wm.config));
    memset(&dynconfig, 0, sizeof(dynconfig));

    clear_bar_modules();
    init_default_theme();
    init_default_bar_style();

    snprintf(
        wm.config.path,
        sizeof(wm.config.path),
        "%s/.config/vwm/vwm.conf",
        getenv("HOME") ? getenv("HOME") : ""
    );

    wm.config.border_width = BORDER_WIDTH;
    wm.config.gap_px = GAP_PX;
    wm.config.bar_outer_gap = 0;
    wm.config.default_mfact = 0.5f;
    wm.config.font_size = 11.0f;
    wm.config.sync_workspaces = true;

    snprintf(wm.config.font_family, sizeof(wm.config.font_family), "monospace");
    snprintf(wm.config.terminal, sizeof(wm.config.terminal), "kitty");
    snprintf(wm.config.launcher, sizeof(wm.config.launcher), "rofi -show drun");
    snprintf(wm.config.scratchpad, sizeof(wm.config.scratchpad), "kitty");

    wm.config.scratchpad_width_pct = 92;
    wm.config.scratchpad_height_pct = 92;
    wm.config.scratchpad_dim_alpha = 48;

    derive_theme_to_runtime();
    rebuild_config_commands();
    init_default_keybinds();
    load_config_file(wm.config.path);
    derive_theme_to_runtime();
}

void apply_config(void) {
    derive_theme_to_runtime();
    open_font_from_config();
    sanitize_config();

    for (Monitor *m = wm.mons; m; m = m->next) {
        create_bar(m);

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
    xcb_flush(wm.conn);
}

void reload_config(const void *arg) {
    (void)arg;

    load_default_config();
    apply_config();

    fprintf(stderr, "vwm: config reloaded from %s\n", wm.config.path);
}

void run_autostart(void) {
    for (size_t i = 0; i < dynconfig.autostart_count; i++) {
        if (dynconfig.autostart[i].argv[0]) {
            spawn(dynconfig.autostart[i].argv);
        }
    }
}

void run_scratchpad_autostart(void) {
    for (size_t i = 0; i < dynconfig.scratchpad_autostart_count; i++) {
        if (dynconfig.scratchpad_autostart[i].argv[0]) {
            spawn(dynconfig.scratchpad_autostart[i].argv);
        }
    }
}

typedef struct {
    const char *name;
    const char *env_var;
    const char *default_cmd;
    const char *icon;
    uint32_t color;
    int interval_ms;
} PrepackagedScript;

static const PrepackagedScript prepackaged_scripts[] = {
    {
        .name = "weather",
        .env_var = "VWM_WEATHER_CMD",
        .default_cmd = "",
        .icon = "",
        .color = 0,
        .interval_ms = 900000,
    },
    {
        .name = "printer",
        .env_var = "VWM_PRINTER_CMD",
        .default_cmd = "sh -c 'lpstat -p 2>/dev/null | grep -c idle'",
        .icon = "󰐪",
        .color = 0,
        .interval_ms = 30000,
    },
    {
        .name = "mail",
        .env_var = "VWM_MAIL_CMD",
        .default_cmd = "",
        .icon = "󰇮",
        .color = 0,
        .interval_ms = 60000,
    },
    {
        .name = "updates",
        .env_var = "VWM_UPDATES_CMD",
        .default_cmd = "sh -c 'checkupdates 2>/dev/null | wc -l'",
        .icon = "󰏔",
        .color = 0,
        .interval_ms = 1800000,
    },
    {
        .name = "uptime",
        .env_var = "VWM_UPTIME_CMD",
        .default_cmd = "sh -c \"uptime -p | sed 's/up //'\"",
        .icon = "󰔟",
        .color = 0,
        .interval_ms = 60000,
    },
    {
        .name = "cpu",
        .env_var = "VWM_CPU_CMD",
        .default_cmd = "sh -c \"top -bn1 | awk '/Cpu/{printf \\\"%d%%\\\", 100-$8}'\"",
        .icon = "󰻠",
        .color = 0,
        .interval_ms = 2000,
    },
    {
        .name = "disk",
        .env_var = "VWM_DISK_CMD",
        .default_cmd = "sh -c \"df -h / | awk 'NR==2{print $5}'\"",
        .icon = "󰋊",
        .color = 0,
        .interval_ms = 60000,
    },
    {
        .name = "swap",
        .env_var = "VWM_SWAP_CMD",
        .default_cmd = "sh -c \"free -h | awk '/Swap/{print $3}'\"",
        .icon = "󰾅",
        .color = 0,
        .interval_ms = 5000,
    },
    {
        .name = "loadavg",
        .env_var = "VWM_LOADAVG_CMD",
        .default_cmd = "sh -c \"awk '{print $1}' /proc/loadavg\"",
        .icon = "󰊚",
        .color = 0,
        .interval_ms = 5000,
    },
    {
        .name = "kernel",
        .env_var = "VWM_KERNEL_CMD",
        .default_cmd = "sh -c 'uname -r'",
        .icon = "󰌽",
        .color = 0,
        .interval_ms = 3600000,
    },
    {
        .name = "packages",
        .env_var = "VWM_PACKAGES_CMD",
        .default_cmd = "sh -c 'pacman -Q 2>/dev/null | wc -l'",
        .icon = "󰏖",
        .color = 0,
        .interval_ms = 3600000,
    },
};

static const PrepackagedScript *find_prepackaged_script(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < LENGTH(prepackaged_scripts); i++) {
        if (strcmp(prepackaged_scripts[i].name, name) == 0) {
            return &prepackaged_scripts[i];
        }
    }
    return NULL;
}

ScriptModule *find_script_module(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < dynconfig.script_module_count; i++) {
        if (strcmp(dynconfig.script_modules[i].name, name) == 0) {
            return &dynconfig.script_modules[i];
        }
    }
    return NULL;
}

int find_or_create_script_module(const char *name) {
    if (!name) return -1;

    for (size_t i = 0; i < dynconfig.script_module_count; i++) {
        if (strcmp(dynconfig.script_modules[i].name, name) == 0) {
            return (int)i;
        }
    }

    if (dynconfig.script_module_count >= MAX_SCRIPT_MODULES) {
        fprintf(stderr, "vwm: too many script modules\n");
        return -1;
    }

    int idx = (int)dynconfig.script_module_count++;
    ScriptModule *sm = &dynconfig.script_modules[idx];
    memset(sm, 0, sizeof(*sm));
    snprintf(sm->name, sizeof(sm->name), "%s", name);

    const PrepackagedScript *pp = find_prepackaged_script(name);
    if (pp) {
        snprintf(sm->env_var, sizeof(sm->env_var), "%s", pp->env_var);
        if (pp->default_cmd[0]) {
            snprintf(sm->command, sizeof(sm->command), "%s", pp->default_cmd);
        }
        if (pp->icon[0]) {
            snprintf(sm->icon, sizeof(sm->icon), "%s", pp->icon);
        }
        sm->color = pp->color;
        sm->interval_ms = pp->interval_ms;
    } else {
        sm->interval_ms = 10000;
    }

    return idx;
}

static void update_script_module(ScriptModule *sm) {
    if (!sm) return;

    char buf[256] = {0};
    const char *cmd = NULL;

    if (sm->env_var[0]) {
        const char *env_cmd = getenv(sm->env_var);
        if (env_cmd && *env_cmd) {
            cmd = env_cmd;
        }
    }

    if (!cmd && sm->command[0]) {
        cmd = sm->command;
    }

    if (!cmd) {
        sm->cached_text[0] = '\0';
        return;
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        sm->cached_text[0] = '\0';
        return;
    }

    if (fgets(buf, sizeof(buf), fp)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r' ||
               buf[len-1] == ' ' || buf[len-1] == '\t')) {
            buf[--len] = '\0';
        }
    }
    pclose(fp);

    snprintf(sm->cached_text, sizeof(sm->cached_text), "%s", buf);
}

void refresh_script_modules(bool force) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long now = (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);

    for (size_t i = 0; i < dynconfig.script_module_count; i++) {
        ScriptModule *sm = &dynconfig.script_modules[i];
        if (force || now - sm->last_update_ms >= sm->interval_ms) {
            sm->last_update_ms = now;
            update_script_module(sm);
        }
    }
}
