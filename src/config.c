#include "config.h"

#include "actions.h"
#include "bar.h"
#include "client.h"
#include "layout.h"
#include "util.h"
#include "x11.h"

DynamicConfig dynconfig = {0};

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

static void add_workspace_rule(int workspace, const char *class_name) {
    if (!class_name || !*class_name) {
        return;
    }

    if (workspace < 0 || workspace >= WORKSPACE_COUNT) {
        fprintf(stderr, "vwm: invalid workspace_rule target %d\n", workspace + 1);
        return;
    }

    if (dynconfig.workspace_rule_count >= MAX_WORKSPACE_RULES) {
        fprintf(stderr, "vwm: too many workspace rules, ignoring '%s'\n", class_name);
        return;
    }

    WorkspaceRule *rule = &dynconfig.workspace_rules[dynconfig.workspace_rule_count++];
    rule->workspace = workspace;
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

static bool add_dynamic_command(const char *name, const char *cmdline) {
    if (!name || !*name || !cmdline || !*cmdline) {
        return false;
    }

    if (dynconfig.command_count >= MAX_DYNAMIC_COMMANDS) {
        fprintf(stderr, "vwm: too many commands, ignoring '%s'\n", name);
        return false;
    }

    if (find_dynamic_command(name)) {
        fprintf(stderr, "vwm: duplicate command '%s', ignoring later definition\n", name);
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

    return true;
}

static void extract_class_from_argv(const char **argv, char *out, size_t outsz) {
    if (!out || outsz == 0) {
        return;
    }

    out[0] = '\0';

    if (!argv) {
        return;
    }

    for (size_t i = 0; argv[i]; i++) {
        if (strcmp(argv[i], "--class") == 0 && argv[i + 1]) {
            snprintf(out, outsz, "%s", argv[i + 1]);
            return;
        }
    }
}

static bool add_dynamic_scratchpad(const char *name, const char *cmdline) {
    if (!name || !*name || !cmdline || !*cmdline) {
        return false;
    }

    if (dynconfig.scratchpad_count >= MAX_DYNAMIC_SCRATCHPADS) {
        fprintf(stderr, "vwm: too many scratchpads, ignoring '%s'\n", name);
        return false;
    }

    if (find_dynamic_scratchpad(name)) {
        fprintf(stderr, "vwm: duplicate scratchpad '%s', ignoring later definition\n", name);
        return false;
    }

    DynamicScratchpad *sp = &dynconfig.scratchpads[dynconfig.scratchpad_count++];
    memset(sp, 0, sizeof(*sp));

    snprintf(sp->name, sizeof(sp->name), "%s", name);
    split_command_argv(cmdline, sp->storage, sp->argv, CMD_MAX_ARGS);

    if (!sp->argv[0]) {
        dynconfig.scratchpad_count--;
        fprintf(stderr, "vwm: scratchpad '%s' has empty argv\n", name);
        return false;
    }

    extract_class_from_argv(sp->argv, sp->class_name, sizeof(sp->class_name));

    if (sp->class_name[0] == '\0') {
        fprintf(stderr, "vwm: scratchpad '%s' should include --class <name>\n", name);
    }

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
    if (strcmp(name, "fullscreen") == 0 || strcmp(name, "toggle_fullscreen") == 0) { *out = ACTION_TOGGLE_FULLSCREEN; return true; }
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

        if (kb->kind == DYNKEY_SCRATCHPAD) {
            toggle_named_scratchpad(kb->target_name);
            return true;
        }

        if (kb->kind == DYNKEY_BUILTIN) {
            dispatch_action(kb->action);
            return true;
        }

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

    return false;
}

static bool parse_bar_style_value(const char *name, BarStyleMode *out) {
    if (!name || !out) {
        return false;
    }

    if (strcmp(name, "flat") == 0) {
        *out = BAR_STYLE_FLAT;
        return true;
    }

    if (strcmp(name, "floating") == 0) {
        *out = BAR_STYLE_FLOATING;
        return true;
    }

    return false;
}

static void init_default_bar_theme(void) {
    dynconfig.bar_theme.mode = BAR_STYLE_FLAT;
    dynconfig.bar_theme.module_padding_x = 10;
    dynconfig.bar_theme.module_padding_y = 4;
    dynconfig.bar_theme.module_gap = 8;
    dynconfig.bar_theme.module_radius = 12;

    dynconfig.bar_theme.floating_margin_x = 24;
    dynconfig.bar_theme.floating_margin_y = 8;

    dynconfig.bar_theme.transparent_background = false;

    dynconfig.bar_theme.volume_bar_enabled = true;
    dynconfig.bar_theme.volume_bar_width = 42;
    dynconfig.bar_theme.volume_bar_height = 6;
    dynconfig.bar_theme.volume_bar_radius = 3;

    dynconfig.bar_theme.module_bg = 0x1b1b1b;
    dynconfig.bar_theme.module_fg = 0xd0d0d0;
    dynconfig.bar_theme.module_border = 0x2a2a2a;
    dynconfig.bar_theme.module_border_width = 0;

    dynconfig.bar_theme.volume_bar_bg = 0x2a2a2a;
    dynconfig.bar_theme.volume_bar_fg_low = 0x88c0d0;
    dynconfig.bar_theme.volume_bar_fg_mid = 0xa3be8c;
    dynconfig.bar_theme.volume_bar_fg_high = 0xebcb8b;
    dynconfig.bar_theme.volume_bar_fg_muted = 0xbf616a;
}

static bool add_bar_module(BarModule *arr, size_t *count, const char *kind_name, const char *arg, const char *resolved_path) {
    if (!arr || !count || !kind_name) {
        return false;
    }

    if (*count >= MAX_BAR_MODULES_PER_SECTION) {
        fprintf(stderr, "vwm: too many bar modules in %s\n", resolved_path);
        return false;
    }

    BarModuleKind kind;
    if (!parse_bar_module_kind(kind_name, &kind)) {
        fprintf(stderr, "vwm: unknown bar module '%s' in %s\n", kind_name, resolved_path);
        return false;
    }

    BarModule *mod = &arr[*count];
    memset(mod, 0, sizeof(*mod));
    mod->kind = kind;

    if (arg && *arg) {
        snprintf(mod->arg, sizeof(mod->arg), "%s", arg);
    }

    (*count)++;
    return true;
}

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

static bool parse_dynamic_command_line(char *raw, const char *resolved_path) {
    char *rest = raw + strlen("command");
    rest = trim_whitespace(rest);
    if (*rest == '\0') {
        fprintf(stderr, "vwm: invalid command line in %s\n", resolved_path);
        return true;
    }

    char *name = rest;
    while (*rest && !isspace((unsigned char)*rest)) {
        rest++;
    }

    if (*rest == '\0') {
        fprintf(stderr, "vwm: command missing argv in %s\n", resolved_path);
        return true;
    }

    *rest = '\0';
    rest++;
    rest = trim_whitespace(rest);
    config_unquote_inplace(rest);

    add_dynamic_command(name, rest);
    return true;
}

static bool parse_dynamic_scratchpad_line(char *raw, const char *resolved_path) {
    char *rest = raw + strlen("scratchpad");
    rest = trim_whitespace(rest);
    if (*rest == '\0') {
        fprintf(stderr, "vwm: invalid scratchpad line in %s\n", resolved_path);
        return true;
    }

    if (*rest == '"' || *rest == '\'') {
        return false;
    }

    char *name = rest;
    while (*rest && !isspace((unsigned char)*rest)) {
        rest++;
    }

    if (*rest == '\0') {
        fprintf(stderr, "vwm: scratchpad missing argv in %s\n", resolved_path);
        return true;
    }

    *rest = '\0';
    rest++;
    rest = trim_whitespace(rest);
    config_unquote_inplace(rest);

    add_dynamic_scratchpad(name, rest);
    return true;
}

static bool parse_dynamic_key_line(char *raw, const char *resolved_path) {
    char storage[CMD_MAX_ARGS][256] = {{0}};
    const char *argv[CMD_MAX_ARGS] = {0};

    char *rest = raw + strlen("key");
    rest = trim_whitespace(rest);

    size_t argc = split_command_argv(rest, storage, argv, CMD_MAX_ARGS);
    if (argc < 2) {
        fprintf(stderr, "vwm: invalid key line in %s\n", resolved_path);
        return true;
    }

    const char *combo = argv[0];
    const char *verb = argv[1];

    if (strcmp(verb, "spawn") == 0) {
        if (argc < 3) {
            fprintf(stderr, "vwm: key spawn missing command name in %s\n", resolved_path);
            return true;
        }
        add_dynamic_keybind_command(combo, argv[2]);
        return true;
    }

    if (strcmp(verb, "scratchpad") == 0) {
        if (argc >= 3) {
            add_dynamic_keybind_scratchpad(combo, argv[2]);
        } else {
            add_dynamic_keybind_builtin(combo, ACTION_TOGGLE_SCRATCHPAD);
        }
        return true;
    }

    Action action;
    if (parse_builtin_action_name(verb, &action)) {
        add_dynamic_keybind_builtin(combo, action);
        return true;
    }

    fprintf(stderr, "vwm: unknown key action '%s' in %s\n", verb, resolved_path);
    return true;
}

static bool parse_bar_module_line(char *raw, const char *resolved_path) {
    char storage[CMD_MAX_ARGS][256] = {{0}};
    const char *argv[CMD_MAX_ARGS] = {0};

    size_t argc = split_command_argv(raw, storage, argv, CMD_MAX_ARGS);
    if (argc < 2) {
        fprintf(stderr, "vwm: invalid bar module line in %s\n", resolved_path);
        return true;
    }

    BarModule *arr = NULL;
    size_t *count = NULL;

    if (strcmp(argv[0], "bar_left") == 0) {
        arr = dynconfig.bar_left;
        count = &dynconfig.bar_left_count;
    } else if (strcmp(argv[0], "bar_center") == 0) {
        arr = dynconfig.bar_center;
        count = &dynconfig.bar_center_count;
    } else if (strcmp(argv[0], "bar_right") == 0) {
        arr = dynconfig.bar_right;
        count = &dynconfig.bar_right_count;
    } else {
        return false;
    }

    const char *kind_name = argv[1];
    const char *arg = (argc >= 3) ? argv[2] : "";

    add_bar_module(arr, count, kind_name, arg, resolved_path);
    return true;
}

static bool parse_workspace_rule_line(char *raw, const char *resolved_path) {
    char storage[CMD_MAX_ARGS][256] = {{0}};
    const char *argv[CMD_MAX_ARGS] = {0};

    size_t argc = split_command_argv(raw, storage, argv, CMD_MAX_ARGS);
    if (argc < 4) {
        fprintf(stderr, "vwm: invalid workspace_rule line in %s\n", resolved_path);
        return true;
    }

    if (strcmp(argv[0], "workspace_rule") != 0) {
        return false;
    }

    int workspace = atoi(argv[1]);
    if (workspace < 1 || workspace > WORKSPACE_COUNT) {
        fprintf(stderr, "vwm: workspace_rule target must be 1-%d in %s\n", WORKSPACE_COUNT, resolved_path);
        return true;
    }

    if (strcmp(argv[2], "class") != 0) {
        fprintf(stderr, "vwm: workspace_rule currently only supports 'class' in %s\n", resolved_path);
        return true;
    }

    add_workspace_rule(workspace - 1, argv[3]);
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

        if (strncmp(raw, "command", 7) == 0 && isspace((unsigned char)raw[7])) {
            parse_dynamic_command_line(raw, resolved_path);
            continue;
        }

        if (strncmp(raw, "key", 3) == 0 && isspace((unsigned char)raw[3])) {
            parse_dynamic_key_line(raw, resolved_path);
            continue;
        }

        if (strncmp(raw, "scratchpad", 10) == 0 && isspace((unsigned char)raw[10])) {
            if (parse_dynamic_scratchpad_line(raw, resolved_path)) {
                continue;
            }
        }

        if ((strncmp(raw, "bar_left", 8) == 0 && isspace((unsigned char)raw[8])) ||
            (strncmp(raw, "bar_center", 10) == 0 && isspace((unsigned char)raw[10])) ||
            (strncmp(raw, "bar_right", 9) == 0 && isspace((unsigned char)raw[9]))) {
            parse_bar_module_line(raw, resolved_path);
            continue;
        }

        if (strncmp(raw, "workspace_rule", 14) == 0 && isspace((unsigned char)raw[14])) {
            parse_workspace_rule_line(raw, resolved_path);
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

        if (strcmp(key, "float") == 0) {
            char *subkey = val;
            while (*subkey && !isspace((unsigned char)*subkey)) {
                subkey++;
            }

            if (*subkey != '\0') {
                *subkey = '\0';
                subkey++;
                subkey = trim_whitespace(subkey);

                if (strcmp(val, "class") == 0) {
                    config_unquote_inplace(subkey);
                    add_float_rule(subkey);
                    continue;
                }
            }

            fprintf(stderr, "vwm: invalid float rule in %s\n", resolved_path);
            continue;
        }

        if (strcmp(key, "bar_style") == 0) {
            BarStyleMode mode;
            if (parse_bar_style_value(val, &mode)) {
                dynconfig.bar_theme.mode = mode;
            } else {
                fprintf(stderr, "vwm: invalid bar_style '%s' in %s\n", val, resolved_path);
            }
            continue;
        }

        if (strcmp(key, "bar_transparent_background") == 0) {
            bool b = false;
            if (parse_bool_value(val, &b)) {
                dynconfig.bar_theme.transparent_background = b;
            }
            continue;
        }

        if (strcmp(key, "bar_float_margin_x") == 0) {
            dynconfig.bar_theme.floating_margin_x = atoi(val);
            continue;
        }

        if (strcmp(key, "bar_float_margin_y") == 0) {
            dynconfig.bar_theme.floating_margin_y = atoi(val);
            continue;
        }

        if (strcmp(key, "module_gap") == 0) {
            dynconfig.bar_theme.module_gap = atoi(val);
            continue;
        }

        if (strcmp(key, "module_padding_x") == 0) {
            dynconfig.bar_theme.module_padding_x = atoi(val);
            continue;
        }

        if (strcmp(key, "module_padding_y") == 0) {
            dynconfig.bar_theme.module_padding_y = atoi(val);
            continue;
        }

        if (strcmp(key, "module_radius") == 0) {
            dynconfig.bar_theme.module_radius = atoi(val);
            continue;
        }

        if (strcmp(key, "module_bg") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                dynconfig.bar_theme.module_bg = c;
            }
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

        if (strcmp(key, "volume_bar_enabled") == 0) {
            bool b = false;
            if (parse_bool_value(val, &b)) {
                dynconfig.bar_theme.volume_bar_enabled = b;
            }
            continue;
        }

        if (strcmp(key, "volume_bar_width") == 0) {
            dynconfig.bar_theme.volume_bar_width = atoi(val);
            continue;
        }

        if (strcmp(key, "volume_bar_height") == 0) {
            dynconfig.bar_theme.volume_bar_height = atoi(val);
            continue;
        }

        if (strcmp(key, "volume_bar_radius") == 0) {
            dynconfig.bar_theme.volume_bar_radius = atoi(val);
            continue;
        }

        if (strcmp(key, "volume_bar_bg") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                dynconfig.bar_theme.volume_bar_bg = c;
            }
            continue;
        }

        if (strcmp(key, "volume_bar_fg_low") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                dynconfig.bar_theme.volume_bar_fg_low = c;
            }
            continue;
        }

        if (strcmp(key, "volume_bar_fg_mid") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                dynconfig.bar_theme.volume_bar_fg_mid = c;
            }
            continue;
        }

        if (strcmp(key, "volume_bar_fg_high") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                dynconfig.bar_theme.volume_bar_fg_high = c;
            }
            continue;
        }

        if (strcmp(key, "volume_bar_fg_muted") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                dynconfig.bar_theme.volume_bar_fg_muted = c;
            }
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

        if (strcmp(key, "bar_style") == 0) {
            config_unquote_inplace(val);

            if (strcmp(val, "flat") == 0) {
                dynconfig.bar_theme.mode = BAR_STYLE_FLAT;
            } else if (strcmp(val, "floating") == 0) {
                dynconfig.bar_theme.mode = BAR_STYLE_FLOATING;
            } else {
                fprintf(stderr, "vwm: invalid bar_style '%s' in %s\n", val, resolved_path);
            }
            continue;
        }

        if (strcmp(key, "module_padding_x") == 0) {
            dynconfig.bar_theme.module_padding_x = atoi(val);
            continue;
        }

        if (strcmp(key, "module_padding_y") == 0) {
            dynconfig.bar_theme.module_padding_y = atoi(val);
            continue;
        }

        if (strcmp(key, "module_gap") == 0) {
            dynconfig.bar_theme.module_gap = atoi(val);
            continue;
        }

        if (strcmp(key, "module_radius") == 0) {
            dynconfig.bar_theme.module_radius = atoi(val);
            continue;
        }

        if (strcmp(key, "module_bg") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                dynconfig.bar_theme.module_bg = c;
            }
            continue;
        }

        if (strcmp(key, "module_fg") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                dynconfig.bar_theme.module_fg = c;
            }
            continue;
        }

        if (strcmp(key, "module_border") == 0) {
            uint32_t c = 0;
            if (parse_color_value(val, &c)) {
                dynconfig.bar_theme.module_border = c;
            }
            continue;
        }

        if (strcmp(key, "module_border_width") == 0) {
            dynconfig.bar_theme.module_border_width = atoi(val);
            continue;
        }

        fprintf(stderr, "vwm: unknown config key '%s' in %s\n", key, resolved_path);
    }

    fclose(fp);
    rebuild_config_commands();
}

static void clear_bar_modules(void) {
    dynconfig.bar_left_count = 0;
    dynconfig.bar_center_count = 0;
    dynconfig.bar_right_count = 0;

    memset(dynconfig.bar_left, 0, sizeof(dynconfig.bar_left));
    memset(dynconfig.bar_center, 0, sizeof(dynconfig.bar_center));
    memset(dynconfig.bar_right, 0, sizeof(dynconfig.bar_right));
}

void load_config_file(const char *path) {
    load_config_file_recursive(path, 0);
}

void load_default_config(void) {
    memset(&wm.config, 0, sizeof(wm.config));
    memset(&dynconfig, 0, sizeof(dynconfig));
    clear_bar_modules();
    init_default_bar_theme();

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

    dynconfig.bar_theme.mode = BAR_STYLE_FLAT;
    dynconfig.bar_theme.floating_margin_x = 24;
    dynconfig.bar_theme.floating_margin_y = 8;
    dynconfig.bar_theme.module_gap = 8;
    dynconfig.bar_theme.module_padding_x = 10;
    dynconfig.bar_theme.module_padding_y = 4;
    dynconfig.bar_theme.module_radius = 10;
    dynconfig.bar_theme.module_bg = 0x1b1b1b;

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
        int extra_x = 0;
        int extra_y = 0;

        if (dynconfig.bar_theme.mode == BAR_STYLE_FLOATING) {
            extra_x = MAX(0, dynconfig.bar_theme.floating_margin_x);
            extra_y = MAX(0, dynconfig.bar_theme.floating_margin_y);
        }

        uint32_t vals[] = {
            (uint32_t)(m->geom.x + outer + extra_x),
            (uint32_t)(m->geom.y + outer + extra_y),
            (uint32_t)MAX(1, m->geom.w - ((outer + extra_x) * 2)),
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

void reload_config(const void *arg) {
    (void)arg;

    load_default_config();
    apply_config();

    fprintf(stderr, "vwm: config reloaded from %s\n", wm.config.path);
}
