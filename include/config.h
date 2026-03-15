#ifndef VWM_CONFIG_H
#define VWM_CONFIG_H

#include "vwm.h"

#define MAX_FLOAT_RULES 64
#define MAX_DYNAMIC_COMMANDS 64
#define MAX_DYNAMIC_KEYBINDS 128
#define MAX_DYNAMIC_SCRATCHPADS 16
#define MAX_BAR_MODULES_PER_SECTION 16

typedef struct {
    char class_name[128];
} FloatRule;

typedef struct {
    char name[64];
    char storage[CMD_MAX_ARGS][256];
    const char *argv[CMD_MAX_ARGS];
} DynamicCommand;

typedef struct {
    char name[64];
    char class_name[128];
    char storage[CMD_MAX_ARGS][256];
    const char *argv[CMD_MAX_ARGS];
} DynamicScratchpad;

typedef enum {
    DYNKEY_NONE = 0,
    DYNKEY_BUILTIN,
    DYNKEY_COMMAND,
    DYNKEY_SCRATCHPAD,
} DynamicKeybindKind;

typedef struct {
    xcb_keysym_t sym;
    uint16_t mod;
    DynamicKeybindKind kind;
    Action action;
    char target_name[64];
} DynamicKeybind;

typedef enum {
    BAR_MOD_NONE = 0,
    BAR_MOD_WORKSPACES,
    BAR_MOD_MONITOR,
    BAR_MOD_SYNC,
    BAR_MOD_TITLE,
    BAR_MOD_STATUS,
    BAR_MOD_CLOCK,
    BAR_MOD_CUSTOM,
} BarModuleKind;

typedef struct {
    BarModuleKind kind;
    char arg[256];
} BarModule;

typedef struct {
    FloatRule float_rules[MAX_FLOAT_RULES];
    size_t float_rule_count;

    DynamicCommand commands[MAX_DYNAMIC_COMMANDS];
    size_t command_count;

    DynamicScratchpad scratchpads[MAX_DYNAMIC_SCRATCHPADS];
    size_t scratchpad_count;

    DynamicKeybind keybinds[MAX_DYNAMIC_KEYBINDS];
    size_t keybind_count;

    BarModule bar_left[MAX_BAR_MODULES_PER_SECTION];
    size_t bar_left_count;

    BarModule bar_center[MAX_BAR_MODULES_PER_SECTION];
    size_t bar_center_count;

    BarModule bar_right[MAX_BAR_MODULES_PER_SECTION];
    size_t bar_right_count;
} DynamicConfig;

extern DynamicConfig dynconfig;

void load_default_config(void);
void rebuild_config_commands(void);
void apply_config(void);
void reload_config(const void *arg);
void init_default_keybinds(void);

void load_config_file(const char *path);
void load_config_file_recursive(const char *path, int depth);
void resolve_include_path(const char *base_path, const char *include_path, char *out, size_t outsz);
void expand_home_path(const char *in, char *out, size_t outsz);
void dir_from_path(const char *path, char *out, size_t outsz);
size_t split_command_argv(const char *src, char storage[CMD_MAX_ARGS][256], const char **argv, size_t max_args);
bool split_config_kv(char *line, char **key_out, char **val_out);

void strip_comment(char *s);
bool parse_bool_value(const char *s, bool *out);
bool parse_color_value(const char *s, uint32_t *out);
void config_unquote_inplace(char *s);
void sanitize_config(void);

bool class_should_float(const char *class_name);

DynamicCommand *find_dynamic_command(const char *name);
DynamicScratchpad *find_dynamic_scratchpad(const char *name);
bool execute_dynamic_keybind(xcb_keysym_t sym, uint16_t mod);

#endif
