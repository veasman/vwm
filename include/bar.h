#ifndef VWM_BAR_H
#define VWM_BAR_H

#include "vwm.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void free_xft_resources(void);
void alloc_xft_color(XftColor *dst, uint32_t rgb);
void refresh_xft_resources(void);
void open_font_from_config(void);

int utf8_text_width(const char *s);
int text_width_px(const char *s);
void draw_utf8(XftDraw *draw, XftColor *color, int x, int y, const char *s);

void update_status_cache(void);
void get_root_status_text(char *buf, size_t buflen);

int utf8_char_len(unsigned char c);
size_t utf8_prev_boundary(const char *s, size_t len);
void utf8_truncate_to_width(const char *src, int max_width, char *dst, size_t dstsz);

int bar_text_baseline(void);
void draw_workspace_dots(Monitor *m, XftDraw *draw, int start_x, int baseline, int step_px);

void draw_bar(Monitor *m);
void draw_all_bars(void);
void bar_tick(void);

#endif
