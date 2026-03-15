#ifndef VWM_CONFIG_H
#define VWM_CONFIG_H

#include "vwm.h"

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

#endif
