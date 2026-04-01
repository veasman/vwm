#ifndef VWM_CONFIG_H
#define VWM_CONFIG_H

#include "vwm.h"

#define MAX_AUTOSTART 64
#define MAX_SCRATCHPAD_AUTOSTART 16
#define MAX_FLOAT_RULES 64
#define MAX_WORKSPACE_RULES 64
#define MAX_DYNAMIC_COMMANDS 64
#define MAX_DYNAMIC_KEYBINDS 128
#define MAX_DYNAMIC_SCRATCHPADS 32
#define MAX_BAR_MODULES_PER_SECTION 16

typedef struct {
    char storage[CMD_MAX_ARGS][256];
    const char *argv[CMD_MAX_ARGS];
} AutostartEntry;

typedef struct {
    char class_name[128];
} FloatRule;

typedef struct {
    int workspace;
    char class_name[128];
} WorkspaceRule;

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
    BAR_MOD_VOLUME,
    BAR_MOD_NETWORK,
    BAR_MOD_BATTERY,
    BAR_MOD_BRIGHTNESS,
    BAR_MOD_MEDIA,
    BAR_MOD_MEMORY,
    BAR_MOD_WEATHER,
} BarModuleKind;

typedef struct {
    BarModuleKind kind;
    char arg[256];
} BarModule;

typedef enum {
    BAR_PRESENTATION_MINIMAL = 0,
    BAR_PRESENTATION_ACCENT,
} BarPresentationMode;

typedef enum {
    BAR_POSITION_TOP = 0,
    BAR_POSITION_BOTTOM,
} BarPosition;

typedef enum {
    BAR_BACKGROUND_NONE = 0,
    BAR_BACKGROUND_FLAT,
    BAR_BACKGROUND_FLOATING,
} BarBackgroundMode;

typedef enum {
    BAR_MODULE_STYLE_FLAT = 0,
    BAR_MODULE_STYLE_PILL,
} BarModuleStyle;

typedef struct {
    uint32_t bg;
    uint32_t surface;
    uint32_t text;
    uint32_t text_muted;
    uint32_t accent;
    uint32_t accent_soft;
    uint32_t border;
} ThemeConfig;

typedef struct {
    int height;
    BarPosition position;
    BarModuleStyle modules;

    bool background_enabled;
    bool use_icons;
    bool use_colors;

    int gap;

    int padding_x;
    int padding_y;

    int radius;

    int margin_x;
    int margin_y;

    int content_margin_x;
    int content_margin_y;

    bool volume_bar_enabled;
    int volume_bar_width;
    int volume_bar_height;
    int volume_bar_radius;
} BarStyleConfig;

typedef struct {
    AutostartEntry autostart[MAX_AUTOSTART];
    size_t autostart_count;

    AutostartEntry scratchpad_autostart[MAX_SCRATCHPAD_AUTOSTART];
    size_t scratchpad_autostart_count;

    FloatRule float_rules[MAX_FLOAT_RULES];
    size_t float_rule_count;

    WorkspaceRule workspace_rules[MAX_WORKSPACE_RULES];
    size_t workspace_rule_count;

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

    ThemeConfig theme;
    BarStyleConfig bar_style;

    LayoutKind default_layout;
    int configured_workspace_count;

    bool bar_enabled;
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

void run_autostart(void);
void run_scratchpad_autostart(void);

bool class_should_float(const char *class_name);
int class_workspace_rule(const char *class_name);

DynamicCommand *find_dynamic_command(const char *name);
DynamicScratchpad *find_dynamic_scratchpad(const char *name);
bool execute_dynamic_keybind(xcb_keysym_t sym, uint16_t mod);

#endif
