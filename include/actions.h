#ifndef VWM_ACTIONS_H
#define VWM_ACTIONS_H

#include "vwm.h"

void spawn(const void *arg);
void quit(const void *arg);

void focus_next(const void *arg);
void focus_prev(const void *arg);
void focus_monitor_next(const void *arg);
void focus_monitor_prev(const void *arg);

void send_to_monitor_next(const void *arg);
void send_to_monitor_prev(const void *arg);

void view_workspace(const void *arg);
void send_to_workspace(const void *arg);
void toggle_sync_workspaces(const void *arg);

void set_client_fullscreen_state(Client *c, bool enabled);
void toggle_monocle(const void *arg);
void toggle_true_fullscreen(const void *arg);
void toggle_scratchpad(const void *arg);
void toggle_named_scratchpad(const char *name);

bool client_supports_protocol(Client *c, xcb_atom_t protocol);
void send_wm_delete(Client *c);
void kill_client(const void *arg);

void decrease_mfact(const void *arg);
void increase_mfact(const void *arg);
void zoom_master(const void *arg);

void dispatch_action(Action action);

#endif
