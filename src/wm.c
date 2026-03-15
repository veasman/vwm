#include "wm.h"

#include "config.h"
#include "x11.h"
#include "bar.h"

void cleanup(void) {
    Monitor *m = wm.mons;
    while (m) {
        Monitor *next = m->next;
        if (m->barwin) {
            xcb_destroy_window(wm.conn, m->barwin);
        }
        free(m);
        m = next;
    }

    wm.mons = NULL;
    wm.selmon = NULL;
    wm.scratchpad = NULL;

    free_xft_resources();

    if (wm.dpy) {
        XCloseDisplay(wm.dpy);
        wm.dpy = NULL;
        wm.conn = NULL;
    } else if (wm.conn) {
        xcb_flush(wm.conn);
        xcb_disconnect(wm.conn);
        wm.conn = NULL;
    }
}

void event_loop(void) {
    wm.running = true;
    int fd = xcb_get_file_descriptor(wm.conn);

    while (wm.running && !g_should_exit) {
        if (g_should_reload) {
            g_should_reload = 0;
            reload_config(NULL);
            xcb_flush(wm.conn);
        }

        xcb_generic_event_t *ev = NULL;
        while ((ev = xcb_poll_for_event(wm.conn)) != NULL) {
            uint8_t type = ev->response_type & ~0x80;

            switch (type) {
                case XCB_EXPOSE:
                    handle_expose(ev);
                    break;
                case XCB_PROPERTY_NOTIFY:
                    handle_property_notify(ev);
                    break;
                case XCB_MAP_REQUEST:
                    map_request(ev);
                    break;
                case XCB_DESTROY_NOTIFY:
                    destroy_notify(ev);
                    break;
                case XCB_UNMAP_NOTIFY:
                    unmap_notify(ev);
                    break;
                case XCB_CONFIGURE_REQUEST:
                    configure_request(ev);
                    break;
                case XCB_KEY_PRESS:
                    key_press(ev);
                    break;
                default:
                    break;
            }

            xcb_flush(wm.conn);
            free(ev);
        }

        struct pollfd pfd = {
            .fd = fd,
            .events = POLLIN,
            .revents = 0
        };

        poll(&pfd, 1, 100);
    }
}

void handle_signal(int signo) {
    if (signo == SIGHUP) {
        g_should_reload = 1;
        return;
    }

    g_should_exit = 1;
    wm.running = false;
}
