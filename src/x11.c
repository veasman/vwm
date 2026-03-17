#include "x11.h"

#include "actions.h"
#include "bar.h"
#include "client.h"
#include "config.h"
#include "layout.h"
#include "util.h"

#include <X11/Xatom.h>

static bool visual_has_alpha(Visual *visual) {
  if (!wm.dpy || !visual) {
    return false;
  }

  XRenderPictFormat *fmt = XRenderFindVisualFormat(wm.dpy, visual);
  if (!fmt) {
    return false;
  }

  return fmt->type == PictTypeDirect && fmt->direct.alphaMask != 0;
}

void setup_visuals(void) {
  wm.visual = DefaultVisual(wm.dpy, wm.xscreen);
  wm.colormap = DefaultColormap(wm.dpy, wm.xscreen);
  wm.depth = DefaultDepth(wm.dpy, wm.xscreen);
  wm.visual_id = (xcb_visualid_t)XVisualIDFromVisual(wm.visual);
  wm.has_argb_visual = false;
  wm.owns_colormap = false;

  XVisualInfo tpl = {0};
  tpl.screen = wm.xscreen;
  tpl.depth = 32;
  tpl.class = TrueColor;

  int nitems = 0;
  XVisualInfo *infos = XGetVisualInfo(
      wm.dpy, VisualScreenMask | VisualDepthMask | VisualClassMask, &tpl,
      &nitems);

  if (!infos) {
    return;
  }

  for (int i = 0; i < nitems; i++) {
    if (!infos[i].visual) {
      continue;
    }

    if (!visual_has_alpha(infos[i].visual)) {
      continue;
    }

    wm.visual = infos[i].visual;
    wm.depth = infos[i].depth;
    wm.visual_id = (xcb_visualid_t)infos[i].visualid;
    wm.colormap = XCreateColormap(wm.dpy, RootWindow(wm.dpy, wm.xscreen),
                                  wm.visual, AllocNone);
    wm.has_argb_visual = true;
    wm.owns_colormap = true;
    break;
  }

  XFree(infos);
}

void setup_shape_extension(void) {
  wm.shape_supported =
      XShapeQueryExtension(wm.dpy, &wm.shape_event_base, &wm.shape_error_base);
}

xcb_atom_t intern_atom(const char *name) {
  xcb_intern_atom_cookie_t ck =
      xcb_intern_atom(wm.conn, 0, (uint16_t)strlen(name), name);
  xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(wm.conn, ck, NULL);
  xcb_atom_t atom = XCB_ATOM_NONE;

  if (reply) {
    atom = reply->atom;
    free(reply);
  }

  return atom;
}

void setup_atoms(void) {
  wm.wm_protocols = intern_atom("WM_PROTOCOLS");
  wm.wm_delete_window = intern_atom("WM_DELETE_WINDOW");
  wm.net_wm_state = intern_atom("_NET_WM_STATE");
  wm.net_wm_state_fullscreen = intern_atom("_NET_WM_STATE_FULLSCREEN");
  wm.net_active_window = intern_atom("_NET_ACTIVE_WINDOW");
  wm.utf8_string = intern_atom("UTF8_STRING");
  wm.net_wm_name = intern_atom("_NET_WM_NAME");

  wm.net_wm_window_type = intern_atom("_NET_WM_WINDOW_TYPE");
  wm.net_wm_window_type_dock = intern_atom("_NET_WM_WINDOW_TYPE_DOCK");
  wm.net_wm_state_above = intern_atom("_NET_WM_STATE_ABOVE");
  wm.net_wm_state_sticky = intern_atom("_NET_WM_STATE_STICKY");
  wm.net_wm_state_skip_taskbar = intern_atom("_NET_WM_STATE_SKIP_TASKBAR");
  wm.net_wm_state_skip_pager = intern_atom("_NET_WM_STATE_SKIP_PAGER");
}

void create_bar(Monitor *m) {
    if (!m) {
        return;
    }

    if (m->barwin != XCB_WINDOW_NONE) {
        XDestroyWindow(wm.dpy, (Window)m->barwin);
        m->barwin = XCB_WINDOW_NONE;
    }

    int margin_x = 0;
    int margin_y = 0;

    if (dynconfig.bar_theme.mode == BAR_STYLE_FLOATING) {
        margin_x = MAX(0, dynconfig.bar_theme.floating_margin_x);
        margin_y = MAX(0, dynconfig.bar_theme.floating_margin_y);
    }

    int bar_x = m->geom.x + margin_x;
    int bar_y = m->geom.y + margin_y;
    int bar_w = MAX(1, m->geom.w - (margin_x * 2));
    int bar_h = MAX(1, wm.config.bar_height);

    m->bar_x = bar_x;
    m->bar_y = bar_y;
    m->bar_w = bar_w;
    m->bar_h = bar_h;

    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.override_redirect = True;
    attrs.event_mask = ExposureMask;
    attrs.colormap = wm.colormap;
    attrs.border_pixel = 0;
    attrs.background_pixel = wm.has_argb_visual ? 0x00000000u : wm.config.bar_bg;

    unsigned long mask =
        CWOverrideRedirect |
        CWEventMask |
        CWColormap |
        CWBorderPixel |
        CWBackPixel;

    Window win = XCreateWindow(
        wm.dpy,
        RootWindow(wm.dpy, wm.xscreen),
        bar_x,
        bar_y,
        (unsigned int)bar_w,
        (unsigned int)bar_h,
        0,
        wm.depth,
        InputOutput,
        wm.visual,
        mask,
        &attrs
    );

    if (!win) {
        die("failed to create bar window");
    }

    m->barwin = (xcb_window_t)win;

    if (wm.net_wm_window_type != XCB_ATOM_NONE &&
        wm.net_wm_window_type_dock != XCB_ATOM_NONE) {
        Atom type_atom = (Atom)wm.net_wm_window_type;
        Atom dock_atom = (Atom)wm.net_wm_window_type_dock;
        XChangeProperty(
            wm.dpy,
            win,
            type_atom,
            XA_ATOM,
            32,
            PropModeReplace,
            (unsigned char *)&dock_atom,
            1
        );
    }

    if (wm.net_wm_state != XCB_ATOM_NONE) {
        Atom net_wm_state = (Atom)wm.net_wm_state;
        Atom states[4];
        int n = 0;

        if (wm.net_wm_state_above != XCB_ATOM_NONE) {
            states[n++] = (Atom)wm.net_wm_state_above;
        }
        if (wm.net_wm_state_sticky != XCB_ATOM_NONE) {
            states[n++] = (Atom)wm.net_wm_state_sticky;
        }
        if (wm.net_wm_state_skip_taskbar != XCB_ATOM_NONE) {
            states[n++] = (Atom)wm.net_wm_state_skip_taskbar;
        }
        if (wm.net_wm_state_skip_pager != XCB_ATOM_NONE) {
            states[n++] = (Atom)wm.net_wm_state_skip_pager;
        }

        if (n > 0) {
            XChangeProperty(
                wm.dpy,
                win,
                net_wm_state,
                XA_ATOM,
                32,
                PropModeReplace,
                (unsigned char *)states,
                n
            );
        }
    }

    XMapRaised(wm.dpy, win);
    XSync(wm.dpy, False);

    update_monitor_workarea(m);
}

Monitor *add_monitor(int id, int x, int y, int w, int h,
                     xcb_randr_output_t output) {
  Monitor *m = calloc(1, sizeof(*m));
  if (!m) {
    die("calloc monitor failed");
  }

  m->id = id;
  m->output = output;
  m->geom = (Rect){.x = x, .y = y, .w = w, .h = h};
  m->current_ws = 0;
  m->previous_ws = 0;

  for (int i = 0; i < WORKSPACE_COUNT; i++) {
    m->workspaces[i].id = i;
    m->workspaces[i].layout = LAYOUT_TILE;
    m->workspaces[i].gap_px = wm.config.gap_px;
    m->workspaces[i].mfact = wm.config.default_mfact;
    m->workspaces[i].nmaster = 1;
    m->workspaces[i].hide_bar = false;
  }

  create_bar(m);

  if (!wm.mons) {
    wm.mons = m;
  } else {
    Monitor *tail = wm.mons;
    while (tail->next) {
      tail = tail->next;
    }
    tail->next = m;
  }

  wm.mon_count++;
  if (!wm.selmon) {
    wm.selmon = m;
  }

  return m;
}

void setup_monitors(void) {
  const xcb_setup_t *setup = xcb_get_setup(wm.conn);
  xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
  wm.screen = it.data;
  wm.root = wm.screen->root;

  xcb_randr_query_version_cookie_t ver_cookie =
      xcb_randr_query_version(wm.conn, 1, 5);

  xcb_randr_query_version_reply_t *ver_reply =
      xcb_randr_query_version_reply(wm.conn, ver_cookie, NULL);

  if (!ver_reply) {
    die("RandR not available");
  }

  free(ver_reply);

  xcb_randr_get_screen_resources_current_cookie_t res_cookie =
      xcb_randr_get_screen_resources_current(wm.conn, wm.root);

  xcb_randr_get_screen_resources_current_reply_t *res =
      xcb_randr_get_screen_resources_current_reply(wm.conn, res_cookie, NULL);

  if (!res) {
    die("RandR screen resources failed");
  }

  int output_count = xcb_randr_get_screen_resources_current_outputs_length(res);
  xcb_randr_output_t *outputs =
      xcb_randr_get_screen_resources_current_outputs(res);

  int mon_id = 0;

  for (int i = 0; i < output_count; i++) {
    xcb_randr_get_output_info_cookie_t out_cookie =
        xcb_randr_get_output_info(wm.conn, outputs[i], XCB_CURRENT_TIME);

    xcb_randr_get_output_info_reply_t *out =
        xcb_randr_get_output_info_reply(wm.conn, out_cookie, NULL);

    if (!out) {
      continue;
    }

    if (out->connection != XCB_RANDR_CONNECTION_CONNECTED ||
        out->crtc == XCB_NONE) {
      free(out);
      continue;
    }

    xcb_randr_get_crtc_info_cookie_t crtc_cookie =
        xcb_randr_get_crtc_info(wm.conn, out->crtc, XCB_CURRENT_TIME);

    xcb_randr_get_crtc_info_reply_t *crtc =
        xcb_randr_get_crtc_info_reply(wm.conn, crtc_cookie, NULL);

    if (!crtc) {
      free(out);
      continue;
    }

    if (crtc->width > 0 && crtc->height > 0) {
      add_monitor(mon_id++, crtc->x, crtc->y, crtc->width, crtc->height,
                  outputs[i]);
    }

    free(crtc);
    free(out);
  }

  free(res);

  if (!wm.mons) {
    add_monitor(0, 0, 0, wm.screen->width_in_pixels,
                wm.screen->height_in_pixels, XCB_NONE);
  }
}

void setup_root(void) {
  uint32_t values[] = {
      XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
      XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_ENTER_WINDOW |
      XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
      XCB_EVENT_MASK_PROPERTY_CHANGE};

  xcb_void_cookie_t ck = xcb_change_window_attributes_checked(
      wm.conn, wm.root, XCB_CW_EVENT_MASK, values);

  xcb_generic_error_t *err = xcb_request_check(wm.conn, ck);
  if (err) {
    free(err);
    die("another window manager is already running");
  }
}

void grab_keys(void) {
  xcb_key_symbols_t *symbols = xcb_key_symbols_alloc(wm.conn);
  xcb_keycode_t *code;

  if (!symbols) {
    return;
  }

  xcb_ungrab_key(wm.conn, XCB_GRAB_ANY, wm.root, XCB_MOD_MASK_ANY);

  uint16_t masks[] = {0, XCB_MOD_MASK_LOCK, XCB_MOD_MASK_2,
                      XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2};

  for (size_t k = 0; k < wm.config.keybind_count; k++) {
    code = xcb_key_symbols_get_keycode(symbols, wm.config.keybinds[k].sym);
    if (!code) {
      continue;
    }

    for (int i = 0; code[i] != XCB_NO_SYMBOL; i++) {
      for (size_t m = 0; m < LENGTH(masks); m++) {
        xcb_grab_key(wm.conn, 1, wm.root, wm.config.keybinds[k].mod | masks[m],
                     code[i], XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
      }
    }

    free(code);
  }

  for (size_t k = 0; k < dynconfig.keybind_count; k++) {
    code = xcb_key_symbols_get_keycode(symbols, dynconfig.keybinds[k].sym);
    if (!code) {
      continue;
    }

    for (int i = 0; code[i] != XCB_NO_SYMBOL; i++) {
      for (size_t m = 0; m < LENGTH(masks); m++) {
        xcb_grab_key(wm.conn, 1, wm.root, dynconfig.keybinds[k].mod | masks[m],
                     code[i], XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
      }
    }

    free(code);
  }

  xcb_key_symbols_free(symbols);
}

void handle_expose(xcb_generic_event_t *gev) {
    xcb_expose_event_t *ev = (xcb_expose_event_t *)gev;

    if (ev->count != 0) {
        return;
    }

    for (Monitor *m = wm.mons; m; m = m->next) {
        if (ev->window == m->barwin) {
            draw_bar(m);
            return;
        }
    }
}

void handle_property_notify(xcb_generic_event_t *gev) {
  xcb_property_notify_event_t *ev = (xcb_property_notify_event_t *)gev;

  if (ev->window == wm.root) {
    update_status_cache();
    draw_all_bars();
    return;
  }

  Client *c = find_client(ev->window);
  if (!c) {
    return;
  }

  Workspace *ws = c->ws;
  if (!ws) {
    return;
  }

  if (ws->focused == c) {
    draw_all_bars();
  }
}

void key_press(xcb_generic_event_t *gev) {
  xcb_key_press_event_t *ev = (xcb_key_press_event_t *)gev;

  xcb_key_symbols_t *symbols = xcb_key_symbols_alloc(wm.conn);
  if (!symbols) {
    return;
  }

  xcb_keysym_t sym = xcb_key_symbols_get_keysym(symbols, ev->detail, 0);
  uint16_t cleaned = ev->state & ~(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2);

  if (execute_dynamic_keybind(sym, cleaned)) {
    xcb_key_symbols_free(symbols);
    return;
  }

  for (size_t i = 0; i < wm.config.keybind_count; i++) {
    Keybind *kb = &wm.config.keybinds[i];
    if (kb->sym == sym && kb->mod == cleaned) {
      dispatch_action(kb->action);
      xcb_key_symbols_free(symbols);
      return;
    }
  }

  xcb_key_symbols_free(symbols);
}

void map_request(xcb_generic_event_t *gev) {
    xcb_map_request_event_t *ev = (xcb_map_request_event_t *)gev;

    for (Monitor *m = wm.mons; m; m = m->next) {
        if (ev->window == m->barwin) {
            XMapRaised(wm.dpy, (Window)ev->window);
            XSync(wm.dpy, False);
            return;
        }
    }

    if (!find_client(ev->window)) {
        manage_window(ev->window);
    } else {
        xcb_map_window(wm.conn, ev->window);
    }
}

void destroy_notify(xcb_generic_event_t *gev) {
    xcb_destroy_notify_event_t *ev = (xcb_destroy_notify_event_t *)gev;
    Client *c = find_client(ev->window);
    if (c) {
        unmanage_client(c);
    }
}

void unmap_notify(xcb_generic_event_t *gev) {
    xcb_unmap_notify_event_t *ev = (xcb_unmap_notify_event_t *)gev;
    Client *c = find_client(ev->window);
    if (!c) {
        return;
    }

    if (c->is_hidden) {
        return;
    }

    unmanage_client(c);
}

void configure_request(xcb_generic_event_t *gev) {
    xcb_configure_request_event_t *ev = (xcb_configure_request_event_t *)gev;

    for (Monitor *m = wm.mons; m; m = m->next) {
        if (ev->window == m->barwin) {
            XWindowChanges wc;
            memset(&wc, 0, sizeof(wc));
            unsigned int mask = 0;

            if (ev->value_mask & XCB_CONFIG_WINDOW_X) {
                mask |= CWX;
                wc.x = ev->x;
                m->bar_x = ev->x;
            }
            if (ev->value_mask & XCB_CONFIG_WINDOW_Y) {
                mask |= CWY;
                wc.y = ev->y;
                m->bar_y = ev->y;
            }
            if (ev->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
                mask |= CWWidth;
                wc.width = ev->width;
                m->bar_w = ev->width;
            }
            if (ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
                mask |= CWHeight;
                wc.height = ev->height;
                m->bar_h = ev->height;
            }
            if (ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
                mask |= CWBorderWidth;
                wc.border_width = ev->border_width;
            }
            if (ev->value_mask & XCB_CONFIG_WINDOW_SIBLING) {
                mask |= CWSibling;
                wc.sibling = (Window)ev->sibling;
            }
            if (ev->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
                mask |= CWStackMode;
                wc.stack_mode = ev->stack_mode;
            }

            if (mask != 0) {
                XConfigureWindow(wm.dpy, (Window)ev->window, mask, &wc);
                XSync(wm.dpy, False);
            }
            return;
        }
    }

  Client *c = find_client(ev->window);

  if (!c || c->is_floating) {
    uint32_t values[7];
    int i = 0;
    uint16_t mask = 0;

    if (ev->value_mask & XCB_CONFIG_WINDOW_X) {
      mask |= XCB_CONFIG_WINDOW_X;
      values[i++] = (uint32_t)ev->x;
    }
    if (ev->value_mask & XCB_CONFIG_WINDOW_Y) {
      mask |= XCB_CONFIG_WINDOW_Y;
      values[i++] = (uint32_t)ev->y;
    }
    if (ev->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
      mask |= XCB_CONFIG_WINDOW_WIDTH;
      values[i++] = (uint32_t)ev->width;
    }
    if (ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
      mask |= XCB_CONFIG_WINDOW_HEIGHT;
      values[i++] = (uint32_t)ev->height;
    }
    if (ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
      mask |= XCB_CONFIG_WINDOW_BORDER_WIDTH;
      values[i++] = (uint32_t)ev->border_width;
    }
    if (ev->value_mask & XCB_CONFIG_WINDOW_SIBLING) {
      mask |= XCB_CONFIG_WINDOW_SIBLING;
      values[i++] = (uint32_t)ev->sibling;
    }
    if (ev->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
      mask |= XCB_CONFIG_WINDOW_STACK_MODE;
      values[i++] = (uint32_t)ev->stack_mode;
    }

    xcb_configure_window(wm.conn, ev->window, mask, values);

    if (c) {
      c->frame.x = ev->x;
      c->frame.y = ev->y;
      c->frame.w = ev->width;
      c->frame.h = ev->height;
    }
    return;
  }

  layout_monitor(c->mon);
}
