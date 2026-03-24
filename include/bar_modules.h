#ifndef VWM_BAR_MODULES_H
#define VWM_BAR_MODULES_H

#include "bar.h"
#include "config.h"
#include "system_status.h"

typedef struct {
    XRectangle rects[128];
    int count;
} ShapeRects;

void bar_shape_rects_reset(ShapeRects *sr);
void bar_shape_rects_add(ShapeRects *sr, int x, int y, int w, int h);

bool bar_is_floating(void);
int module_padding_x(void);
int module_padding_y_budget(void);
int module_gap_px(void);
int module_box_y(int bar_h);
int module_box_h(int bar_h);
int module_text_baseline_for_box(int box_y, int box_h);

void bar_draw_module(
    Monitor *m,
    BarModule *mod,
    cairo_t *cr,
    XftDraw *xftdraw,
    int x,
    int baseline,
    int bar_h,
    int width
);

int bar_module_width_px(Monitor *m, BarModule *mod);
void ensure_default_bar_modules(void);

#endif
