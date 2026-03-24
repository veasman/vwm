#include "vwm.h"
#include "bar.h"
#include "client.h"
#include "config.h"
#include "system_status.h"
#include "util.h"
#include "wm.h"
#include "x11.h"

WM wm = {0};

volatile sig_atomic_t g_should_exit = 0;
volatile sig_atomic_t g_should_reload = 0;

int main(void) {
    struct sigaction sa = {0};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    struct sigaction chld = {0};
    chld.sa_handler = SIG_IGN;
    sigemptyset(&chld.sa_mask);
    chld.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &chld, NULL);

    wm.dpy = XOpenDisplay(NULL);
    if (!wm.dpy) {
        die("failed to open X display");
    }

    wm.xscreen = DefaultScreen(wm.dpy);
    wm.shape_supported = false;
    wm.shape_event_base = 0;
    wm.shape_error_base = 0;

    wm.xft_font = NULL;
    wm.status_cache[0] = '\0';

    wm.scratch_overlay_visible = false;
    wm.scratch_monitor = NULL;
    memset(&wm.scratch_workspace, 0, sizeof(wm.scratch_workspace));

    setup_visuals();

    wm.conn = XGetXCBConnection(wm.dpy);
    if (!wm.conn || xcb_connection_has_error(wm.conn)) {
        die("failed to get XCB connection from Xlib");
    }

    XSetEventQueueOwner(wm.dpy, XCBOwnsEventQueue);

    setup_shape_extension();

    load_default_config();
    setup_atoms();
    setup_monitors();
    setup_root();
    apply_config();
    refresh_system_status(true);
    update_status_cache();
    scan_existing_windows();
    draw_all_bars();
    xcb_flush(wm.conn);

    fprintf(stderr, "vwm: started (%s visual, depth %d)\n",
            wm.has_argb_visual ? "ARGB" : "default",
            wm.depth);

    event_loop();
    cleanup();
    return 0;
}
