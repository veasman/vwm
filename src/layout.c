#include "layout.h"

#include "bar.h"
#include "client.h"
#include "config.h"
#include "x11.h"

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

    int w = m->work.w * 3 / 4;
    int h = m->work.h * 3 / 4;

    if (w < 480) {
        w = MIN(m->work.w, 480);
    }
    if (h < 320) {
        h = MIN(m->work.h, 320);
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

    Workspace *ws = ws_of(m, m->current_ws);
    bool hide_bar = ws ? ws->hide_bar : false;
    int outer = MAX(0, wm.config.bar_outer_gap);
    int extra_y = 0;

    if (dynconfig.bar_theme.mode == BAR_STYLE_FLOATING) {
        extra_y = MAX(0, dynconfig.bar_theme.floating_margin_y);
    }

    if (hide_bar) {
        xcb_unmap_window(wm.conn, m->barwin);
        m->work.x = m->geom.x;
        m->work.y = m->geom.y;
        m->work.w = m->geom.w;
        m->work.h = m->geom.h;
    } else {
        xcb_map_window(wm.conn, m->barwin);

        m->work.x = m->geom.x;
        m->work.y = m->geom.y + outer + extra_y + wm.config.bar_height;
        m->work.w = m->geom.w;
        m->work.h = MAX(0, m->geom.h - (outer + extra_y + wm.config.bar_height));
    }
}

void clear_focus_borders_except(Client *keep) {
    uint32_t inactive[] = { wm.config.border_inactive };

    for (Monitor *m = wm.mons; m; m = m->next) {
        for (int i = 0; i < WORKSPACE_COUNT; i++) {
            Workspace *ws = &m->workspaces[i];
            for (Client *c = ws->clients; c; c = c->next) {
                if (c != keep && !c->is_fullscreen) {
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

void layout_tile(Monitor *m, Workspace *ws) {
    if (!m || !ws) {
        return;
    }

    int count = visible_tiled_count(ws);
    if (count == 0) {
        return;
    }

    int gap = ws->gap_px;
    Rect area = m->work;
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

void layout_monocle(Monitor *m, Workspace *ws) {
    if (!m || !ws) {
        return;
    }

    int gap = ws->gap_px;
    Rect r = m->work;
    r.x += gap;
    r.y += gap;
    r.w -= gap * 2;
    r.h -= gap * 2;

    for (Client *c = ws->clients; c; c = c->next) {
        if (c->is_hidden || c->is_floating || c->is_fullscreen) {
            continue;
        }
        configure_client(c, r);
        xcb_map_window(wm.conn, c->win);
    }
}

void layout_monitor(Monitor *m) {
    if (!m) {
        return;
    }

    Workspace *ws = ws_of(m, m->current_ws);
    if (!ws) {
        return;
    }

    Client *fullscreen = find_fullscreen_client(ws);
    ws->hide_bar = (fullscreen != NULL);

    update_monitor_workarea(m);

    uint32_t inactive[] = { wm.config.border_inactive };

    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        Workspace *other = &m->workspaces[i];

        if (other == ws) {
            continue;
        }

        for (Client *c = other->clients; c; c = c->next) {
            c->is_hidden = true;
            if (!c->is_fullscreen) {
                xcb_change_window_attributes(wm.conn, c->win, XCB_CW_BORDER_PIXEL, inactive);
            }
            xcb_unmap_window(wm.conn, c->win);
        }
    }

    if (fullscreen) {
        for (Client *c = ws->clients; c; c = c->next) {
            if (c == fullscreen) {
                c->is_hidden = false;
                configure_client(c, m->work);
                xcb_map_window(wm.conn, c->win);
            } else {
                c->is_hidden = true;
                xcb_unmap_window(wm.conn, c->win);
            }
        }

        if (m == wm.selmon) {
            focus_workspace(m);
        } else {
            ws->focused = fullscreen;
            m->focused = fullscreen;
            draw_bar(m);
        }
        return;
    }

    for (Client *c = ws->clients; c; c = c->next) {
        if (c->is_scratchpad) {
            if (!c->is_hidden) {
                c->is_floating = true;
                center_client_on_monitor(c, m);
                xcb_map_window(wm.conn, c->win);
            }
            continue;
        }

        c->is_hidden = false;
    }

    switch (ws->layout) {
        case LAYOUT_TILE:
            layout_tile(m, ws);
            break;
        case LAYOUT_MONOCLE:
            layout_monocle(m, ws);
            break;
        case LAYOUT_FLOAT:
            break;
    }

    for (Client *c = ws->clients; c; c = c->next) {
        if (c->is_scratchpad) {
            continue;
        }

        if (!c->is_hidden && c->is_floating) {
            if (c->frame.w <= 0 || c->frame.h <= 0) {
                center_client_on_monitor(c, m);
            } else {
                configure_client(c, c->frame);
            }
            xcb_map_window(wm.conn, c->win);
        }
    }

    if (m == wm.selmon) {
        focus_workspace(m);
    } else {
        Client *c = ws->focused;
        if (!c || c->is_hidden) {
            c = ws->clients;
            while (c && c->is_hidden) {
                c = c->next;
            }
            ws->focused = c;
        }
        m->focused = ws->focused;
        draw_bar(m);
    }
}
