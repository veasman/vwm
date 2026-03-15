#ifndef VWM_LAYOUT_H
#define VWM_LAYOUT_H

#include "vwm.h"

uint32_t border_width_for_client(Client *c);
void configure_client(Client *c, Rect r);
void center_client_on_monitor(Client *c, Monitor *m);
void update_monitor_workarea(Monitor *m);
void clear_focus_borders_except(Client *keep);

int visible_tiled_count(Workspace *ws);
void layout_tile(Monitor *m, Workspace *ws);
void layout_monocle(Monitor *m, Workspace *ws);
void layout_monitor(Monitor *m);

#endif
