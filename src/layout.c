#include "layout.h"

#include "bar.h"
#include "client.h"
#include "config.h"
#include "x11.h"

static Workspace *active_focus_ws(Monitor *m) {
    if (!m) {
        return NULL;
    }

    if (scratch_visible_on_monitor(m)) {
        ensure_scratch_workspace_ready();
        return &wm.scratch_workspace;
    }

    return ws_of(m, m->current_ws);
}

uint32_t border_pixel_for_rgb(uint32_t rgb) {
    uint32_t packed = rgb & 0x00ffffffu;
    packed |= 0xff000000u;
    return packed;
}

uint32_t border_width_for_client(Client *c) {
    if (!c || c->is_fullscreen) {
        return 0;
    }
    return (uint32_t)MAX(0, wm.config.border_width);
}

void configure_client(Client *c, Rect r) {
    if (!c) {
        return;
    }

    c->frame = r;

    int bw = (int)border_width_for_client(c);
    int w = MAX(1, r.w - (bw * 2));
    int h = MAX(1, r.h - (bw * 2));

    uint32_t vals[] = {
        (uint32_t)r.x,
        (uint32_t)r.y,
        (uint32_t)w,
        (uint32_t)h,
        (uint32_t)bw,
    };

    xcb_configure_window(
        wm.conn,
        c->win,
        XCB_CONFIG_WINDOW_X |
        XCB_CONFIG_WINDOW_Y |
        XCB_CONFIG_WINDOW_WIDTH |
        XCB_CONFIG_WINDOW_HEIGHT |
        XCB_CONFIG_WINDOW_BORDER_WIDTH,
        vals
    );
}

void center_client_on_monitor(Client *c, Monitor *m) {
    if (!c || !m) {
        return;
    }

    int w, h;

    /* If the client already has a requested size (from its X geometry),
       respect it.  Otherwise fall back to scratchpad dimensions. */
    if (c->frame.w > 0 && c->frame.h > 0) {
        w = MIN(c->frame.w, m->work.w);
        h = MIN(c->frame.h, m->work.h);
    } else {
        int wpct = CLAMP(wm.config.scratchpad_width_pct, 40, 100);
        int hpct = CLAMP(wm.config.scratchpad_height_pct, 40, 100);

        w = (m->work.w * wpct) / 100;
        h = (m->work.h * hpct) / 100;

        if (w < 900) {
            w = MIN(m->work.w, 900);
        }
        if (h < 650) {
            h = MIN(m->work.h, 650);
        }
    }

    Rect r = {
        .x = m->work.x + (m->work.w - w) / 2,
        .y = m->work.y + (m->work.h - h) / 2,
        .w = w,
        .h = h,
    };

    c->frame = r;
    configure_client(c, r);
}

void update_monitor_workarea(Monitor *m) {
    if (!m) {
        return;
    }

    Workspace *base_ws = ws_of(m, m->current_ws);
    bool hide_bar = base_ws ? base_ws->hide_bar : false;

    if (!dynconfig.bar_enabled || hide_bar) {
        if (m->barwin && hide_bar) {
            XUnmapWindow(wm.dpy, (Window)m->barwin);
        }

        m->work = m->geom;
        return;
    }

    if (m->barwin) {
        XMapRaised(wm.dpy, (Window)m->barwin);
    }

    m->work = m->geom;

    if (dynconfig.bar_style.position == BAR_POSITION_BOTTOM) {
        m->work.h = MAX(0, m->bar_y - m->geom.y);
    } else {
        int bar_bottom = m->bar_y + m->bar_h;
        int geom_bottom = m->geom.y + m->geom.h;
        m->work.y = bar_bottom;
        m->work.h = MAX(0, geom_bottom - bar_bottom);
    }
}

void clear_focus_borders_except(Client *keep) {
    uint32_t inactive[] = {
        border_pixel_for_rgb(wm.config.border_inactive)
    };

    for (Client *c = wm.scratch_workspace.clients; c; c = c->next) {
        if (c == keep) {
            continue;
        }

        if (c->is_fullscreen) {
            continue;
        }

        xcb_change_window_attributes(
            wm.conn,
            c->win,
            XCB_CW_BORDER_PIXEL,
            inactive
        );
    }

    for (Monitor *m = wm.mons; m; m = m->next) {
        for (int i = 0; i < WORKSPACE_COUNT; i++) {
            Workspace *ws = &m->workspaces[i];

            for (Client *c = ws->clients; c; c = c->next) {
                if (c == keep) {
                    continue;
                }

                if (c->is_fullscreen) {
                    continue;
                }

                xcb_change_window_attributes(
                    wm.conn,
                    c->win,
                    XCB_CW_BORDER_PIXEL,
                    inactive
                );
            }
        }
    }
}

int visible_tiled_count(Workspace *ws) {
    int count = 0;
    for (Client *c = ws ? ws->clients : NULL; c; c = c->next) {
        if (!c->is_hidden && !c->is_floating && !c->is_fullscreen) {
            count++;
        }
    }
    return count;
}

static void layout_tile_in_area(Monitor *m, Workspace *ws, Rect area) {
    if (!m || !ws) {
        return;
    }

    int count = visible_tiled_count(ws);
    if (count == 0) {
        return;
    }

    int gap = ws->gap_px;
    area.x += gap;
    area.y += gap;
    area.w -= gap * 2;
    area.h -= gap * 2;

    if (count == 1) {
        for (Client *c = ws->clients; c; c = c->next) {
            if (!c->is_hidden && !c->is_floating && !c->is_fullscreen) {
                configure_client(c, area);
                xcb_map_window(wm.conn, c->win);
                return;
            }
        }
    }

    int nmaster = MAX(1, ws->nmaster);
    if (nmaster > count) {
        nmaster = count;
    }

    int master_w = (count > nmaster) ? (int)(area.w * ws->mfact) : area.w;
    int stack_w = area.w - master_w - gap;

    int mi = 0;
    int si = 0;
    int master_h_each = nmaster ? (area.h - gap * (nmaster - 1)) / nmaster : area.h;
    int stack_count = count - nmaster;
    int stack_h_each = stack_count > 0 ? (area.h - gap * (stack_count - 1)) / stack_count : area.h;

    for (Client *c = ws->clients; c; c = c->next) {
        if (c->is_hidden || c->is_floating || c->is_fullscreen) {
            continue;
        }

        Rect r;
        if (mi < nmaster) {
            r.x = area.x;
            r.y = area.y + (master_h_each + gap) * mi;
            r.w = master_w;
            r.h = master_h_each;
            mi++;
        } else {
            r.x = area.x + master_w + gap;
            r.y = area.y + (stack_h_each + gap) * si;
            r.w = MAX(0, stack_w);
            r.h = stack_h_each;
            si++;
        }

        configure_client(c, r);
        xcb_map_window(wm.conn, c->win);
    }
}

static void layout_monocle_in_area(Monitor *m, Workspace *ws, Rect area) {
    if (!m || !ws) {
        return;
    }

    int gap = ws->gap_px;
    area.x += gap;
    area.y += gap;
    area.w -= gap * 2;
    area.h -= gap * 2;

    if (area.w <= 0 || area.h <= 0) {
        return;
    }

    Client *focused = ws->focused;
    if (!focused || focused->is_floating || focused->is_fullscreen) {
        focused = first_tiled_client(ws);
        ws->focused = focused;
    }

    for (Client *c = ws->clients; c; c = c->next) {
        if (c->is_floating || c->is_fullscreen) {
            continue;
        }

        if (c == focused) {
            c->is_hidden = false;
            configure_client(c, area);
            xcb_map_window(wm.conn, c->win);
        } else {
            c->is_hidden = true;
            xcb_unmap_window(wm.conn, c->win);
        }
    }
}

static void layout_floating_clients(Monitor *m, Workspace *ws, bool raise_above) {
    if (!m || !ws) {
        return;
    }

    for (Client *c = ws->clients; c; c = c->next) {
        if (c->is_hidden) {
            continue;
        }

        if (c->is_floating) {
            if (c->frame.w <= 0 || c->frame.h <= 0) {
                center_client_on_monitor(c, m);
            } else {
                configure_client(c, c->frame);
            }

            xcb_map_window(wm.conn, c->win);

            if (raise_above) {
                uint32_t above_vals[] = { XCB_STACK_MODE_ABOVE };
                xcb_configure_window(wm.conn, c->win, XCB_CONFIG_WINDOW_STACK_MODE, above_vals);
            }
        }
    }
}

static void hide_workspace_clients(Workspace *ws, uint32_t *inactive) {
    for (Client *c = ws->clients; c; c = c->next) {
        c->is_hidden = true;
        if (!c->is_fullscreen) {
            xcb_change_window_attributes(wm.conn, c->win, XCB_CW_BORDER_PIXEL, inactive);
        }
        xcb_unmap_window(wm.conn, c->win);
    }
}

static void hide_scratch_workspace_clients(uint32_t *inactive) {
    for (Client *c = wm.scratch_workspace.clients; c; c = c->next) {
        c->is_hidden = true;
        if (!c->is_fullscreen) {
            xcb_change_window_attributes(wm.conn, c->win, XCB_CW_BORDER_PIXEL, inactive);
        }
        xcb_unmap_window(wm.conn, c->win);
    }
}

void layout_monitor(Monitor *m) {
    if (!m) {
        return;
    }

    ensure_scratch_workspace_ready();

    Workspace *base_ws = ws_of(m, m->current_ws);
    if (!base_ws) {
        return;
    }

    Client *fullscreen = find_fullscreen_client(base_ws);
    base_ws->hide_bar = (fullscreen != NULL);

    update_monitor_workarea(m);

    uint32_t inactive[] = { border_pixel_for_rgb(wm.config.border_inactive) };

    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        Workspace *other = &m->workspaces[i];
        if (other == base_ws) {
            continue;
        }
        hide_workspace_clients(other, inactive);
    }

    if (fullscreen) {
        for (Client *c = base_ws->clients; c; c = c->next) {
            if (c == fullscreen) {
                c->is_hidden = false;
                configure_client(c, m->work);
                xcb_map_window(wm.conn, c->win);
            } else {
                c->is_hidden = true;
                xcb_unmap_window(wm.conn, c->win);
            }
        }
    } else {
        for (Client *c = base_ws->clients; c; c = c->next) {
            c->is_hidden = false;
        }

        if (base_ws->layout == LAYOUT_MONOCLE) {
            layout_monocle_in_area(m, base_ws, m->work);
        } else {
            layout_tile_in_area(m, base_ws, m->work);
        }

        layout_floating_clients(m, base_ws, false);
    }

    if (scratch_visible_on_monitor(m)) {
        show_scratch_overlay(m);

        for (Client *c = wm.scratch_workspace.clients; c; c = c->next) {
            c->mon = m;
            c->is_hidden = false;
        }

        layout_tile_in_area(m, &wm.scratch_workspace, m->work);
        layout_floating_clients(m, &wm.scratch_workspace, true);

        for (Client *c = wm.scratch_workspace.clients; c; c = c->next) {
            uint32_t above_vals[] = { XCB_STACK_MODE_ABOVE };
            xcb_configure_window(wm.conn, c->win, XCB_CONFIG_WINDOW_STACK_MODE, above_vals);
        }
    } else {
        hide_scratch_overlay(m);

        if (!wm.scratch_overlay_visible) {
            hide_scratch_workspace_clients(inactive);
        }
    }

    Workspace *focus_ws = active_focus_ws(m);
    if (m == wm.selmon) {
        Client *c = focus_ws ? focus_ws->focused : NULL;
        if (!c || c->is_hidden) {
            c = focus_ws ? focus_ws->clients : NULL;
            while (c && c->is_hidden) {
                c = c->next;
            }
            if (focus_ws) {
                focus_ws->focused = c;
            }
        }

        if (c) {
            focus_client(c);
        } else {
            m->focused = NULL;
            draw_all_bars();
        }
    } else {
        Client *c = focus_ws ? focus_ws->focused : NULL;
        if (!c || c->is_hidden) {
            c = focus_ws ? focus_ws->clients : NULL;
            while (c && c->is_hidden) {
                c = c->next;
            }
            if (focus_ws) {
                focus_ws->focused = c;
            }
        }
        m->focused = c;
        draw_bar(m);
    }
}
