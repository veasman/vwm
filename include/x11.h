#ifndef VWM_X11_H
#define VWM_X11_H

#include "vwm.h"

xcb_atom_t intern_atom(const char *name);
void setup_atoms(void);
void setup_shape_extension(void);
void setup_visuals(void);

void create_bar(Monitor *m);
Monitor *add_monitor(int id, int x, int y, int w, int h, xcb_randr_output_t output);
void setup_monitors(void);
void setup_root(void);

void grab_keys(void);

void handle_expose(xcb_generic_event_t *gev);
void handle_property_notify(xcb_generic_event_t *gev);
void map_request(xcb_generic_event_t *gev);
void key_press(xcb_generic_event_t *gev);
void destroy_notify(xcb_generic_event_t *gev);
void unmap_notify(xcb_generic_event_t *gev);
void configure_request(xcb_generic_event_t *gev);

#endif
