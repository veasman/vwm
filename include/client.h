#ifndef VWM_CLIENT_H
#define VWM_CLIENT_H

#include "vwm.h"

Workspace *ws_of(Monitor *m, int idx);
Client *find_client(xcb_window_t win);

void get_client_title(Client *c, char *buf, size_t buflen);
Client *find_scratchpad_client(void);
Client *find_fullscreen_client(Workspace *ws);
Client *first_tiled_client(Workspace *ws);
bool workspace_has_clients(Workspace *ws);

void focus_client(Client *c);
void focus_workspace(Monitor *m);

void attach_client(Workspace *ws, Client *c);
void attach_client_head(Workspace *ws, Client *c);
void detach_from_workspace(Client *c);

void manage_window(xcb_window_t win);
void unmanage_client(Client *c);
void scan_existing_windows(void);

Monitor *next_monitor(Monitor *m);
Monitor *prev_monitor(Monitor *m);
void move_client_to_monitor(Client *c, Monitor *dst);

bool get_text_property_utf8(xcb_window_t win, xcb_atom_t prop_atom, char *buf, size_t buflen);
bool get_text_property_legacy(xcb_window_t win, char *buf, size_t buflen);

#endif
