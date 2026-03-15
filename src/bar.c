#include "bar.h"

#include "client.h"
#include "config.h"
#include "util.h"

#include <time.h>

static time_t g_last_bar_tick = 0;

void free_xft_resources(void) {
    if (!wm.dpy) {
        return;
    }

    if (wm.xft_font) {
        XftFontClose(wm.dpy, wm.xft_font);
        wm.xft_font = NULL;
    }

    XftColorFree(wm.dpy, wm.visual, wm.colormap, &wm.xft_bar_fg);
    XftColorFree(wm.dpy, wm.visual, wm.colormap, &wm.xft_bar_bg);
    XftColorFree(wm.dpy, wm.visual, wm.colormap, &wm.xft_ws_current);
    XftColorFree(wm.dpy, wm.visual, wm.colormap, &wm.xft_ws_occupied);
    XftColorFree(wm.dpy, wm.visual, wm.colormap, &wm.xft_ws_empty);

    memset(&wm.xft_bar_fg, 0, sizeof(wm.xft_bar_fg));
    memset(&wm.xft_bar_bg, 0, sizeof(wm.xft_bar_bg));
    memset(&wm.xft_ws_current, 0, sizeof(wm.xft_ws_current));
    memset(&wm.xft_ws_occupied, 0, sizeof(wm.xft_ws_occupied));
    memset(&wm.xft_ws_empty, 0, sizeof(wm.xft_ws_empty));
}

void alloc_xft_color(XftColor *dst, uint32_t rgb) {
    XRenderColor xr = {
        .red   = (unsigned short)(((rgb >> 16) & 0xff) * 257),
        .green = (unsigned short)(((rgb >> 8) & 0xff) * 257),
        .blue  = (unsigned short)((rgb & 0xff) * 257),
        .alpha = 0xffff
    };

    if (!XftColorAllocValue(wm.dpy, wm.visual, wm.colormap, &xr, dst)) {
        die("failed to allocate Xft color");
    }
}

void refresh_xft_resources(void) {
    free_xft_resources();

    char pattern[512];
    snprintf(
        pattern,
        sizeof(pattern),
        "%s:size=%.1f:antialias=true:hinting=true",
        wm.config.font_family[0] ? wm.config.font_family : "monospace",
        wm.config.font_size > 0.0f ? wm.config.font_size : 11.0f
    );

    wm.xft_font = XftFontOpenName(wm.dpy, wm.xscreen, pattern);
    if (!wm.xft_font) {
        wm.xft_font = XftFontOpenName(wm.dpy, wm.xscreen, "monospace:size=11");
    }
    if (!wm.xft_font) {
        die("failed to open Xft font");
    }

    wm.font_ascent = wm.xft_font->ascent;
    wm.font_descent = wm.xft_font->descent;
    wm.font_height = wm.font_ascent + wm.font_descent;

    XGlyphInfo ext;
    const FcChar8 sample[] = "M";
    XftTextExtentsUtf8(wm.dpy, wm.xft_font, sample, 1, &ext);
    wm.font_char_width = ext.xOff > 0 ? ext.xOff : 8;

    alloc_xft_color(&wm.xft_bar_fg, wm.config.bar_fg);
    alloc_xft_color(&wm.xft_bar_bg, wm.config.bar_bg);
    alloc_xft_color(&wm.xft_ws_current, wm.config.workspace_current);
    alloc_xft_color(&wm.xft_ws_occupied, wm.config.workspace_occupied);
    alloc_xft_color(&wm.xft_ws_empty, wm.config.workspace_empty);
}

void open_font_from_config(void) {
    refresh_xft_resources();
}

void update_status_cache(void) {
    char buf[sizeof(wm.status_cache)];
    buf[0] = '\0';

    xcb_icccm_get_text_property_reply_t prop;
    if (xcb_icccm_get_wm_name_reply(
            wm.conn,
            xcb_icccm_get_wm_name(wm.conn, wm.root),
            &prop,
            NULL)) {
        int n = prop.name_len;
        if (n < 0) {
            n = 0;
        }
        if ((size_t)n >= sizeof(buf)) {
            n = (int)sizeof(buf) - 1;
        }
        if (n > 0) {
            memcpy(buf, prop.name, (size_t)n);
        }
        buf[n] = '\0';
        xcb_icccm_get_text_property_reply_wipe(&prop);
    }

    if (buf[0] != '\0') {
        snprintf(wm.status_cache, sizeof(wm.status_cache), "%s", buf);
    }
}

void get_root_status_text(char *buf, size_t buflen) {
    if (!buf || buflen == 0) {
        return;
    }

    if (wm.status_cache[0] == '\0') {
        update_status_cache();
    }

    snprintf(buf, buflen, "%s", wm.status_cache);
}

int bar_text_baseline(void) {
    int baseline = (wm.config.bar_height - wm.font_height) / 2 + wm.font_ascent;
    return MAX(wm.font_ascent, baseline);
}

int utf8_text_width(const char *s) {
    if (!s || !*s) {
        return 0;
    }

    XGlyphInfo ext;
    XftTextExtentsUtf8(
        wm.dpy,
        wm.xft_font,
        (const FcChar8 *)s,
        (int)strlen(s),
        &ext
    );
    return ext.xOff;
}

int text_width_px(const char *s) {
    return utf8_text_width(s);
}

void draw_utf8(XftDraw *draw, XftColor *color, int x, int y, const char *s) {
    if (!draw || !color || !s || !*s) {
        return;
    }

    XftDrawStringUtf8(
        draw,
        color,
        wm.xft_font,
        x,
        y,
        (const FcChar8 *)s,
        (int)strlen(s)
    );
}

int utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0x00) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

size_t utf8_prev_boundary(const char *s, size_t len) {
    if (!s || len == 0) {
        return 0;
    }

    size_t i = len;
    if (i > 0) {
        i--;
    }

    while (i > 0 && (((unsigned char)s[i] & 0xC0) == 0x80)) {
        i--;
    }

    return i;
}

void utf8_truncate_to_width(const char *src, int max_width, char *dst, size_t dstsz) {
    if (!dst || dstsz == 0) {
        return;
    }

    dst[0] = '\0';

    if (!src || !*src || max_width <= 0) {
        return;
    }

    if (utf8_text_width(src) <= max_width) {
        snprintf(dst, dstsz, "%s", src);
        return;
    }

    const char *ellipsis = "...";
    const int ellipsis_w = utf8_text_width(ellipsis);
    if (ellipsis_w > max_width) {
        return;
    }

    size_t src_len = strlen(src);
    size_t out_len = 0;

    for (size_t i = 0; i < src_len;) {
        int clen = utf8_char_len((unsigned char)src[i]);
        if (i + (size_t)clen > src_len) {
            clen = 1;
        }

        if (out_len + (size_t)clen >= dstsz) {
            break;
        }

        memcpy(dst + out_len, src + i, (size_t)clen);
        out_len += (size_t)clen;
        dst[out_len] = '\0';

        char probe[512];
        snprintf(probe, sizeof(probe), "%s%s", dst, ellipsis);

        if (utf8_text_width(probe) > max_width) {
            out_len = utf8_prev_boundary(dst, out_len);
            dst[out_len] = '\0';
            break;
        }

        i += (size_t)clen;
    }

    while (out_len > 0) {
        char probe[512];
        snprintf(probe, sizeof(probe), "%s%s", dst, ellipsis);
        if (utf8_text_width(probe) <= max_width) {
            break;
        }
        out_len = utf8_prev_boundary(dst, out_len);
        dst[out_len] = '\0';
    }

    if (dst[0] != '\0') {
        strncat(dst, ellipsis, dstsz - strlen(dst) - 1);
    }
}

void draw_workspace_dots(Monitor *m, XftDraw *draw, int start_x, int baseline, int step_px) {
    int x = start_x;

    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        Workspace *ws = &m->workspaces[i];
        bool current = (i == m->current_ws);
        bool occupied = workspace_has_clients(ws);

        const char *glyph;
        XftColor *color;

        if (current) {
            glyph = "●";
            color = &wm.xft_ws_current;
        } else if (occupied) {
            glyph = "●";
            color = &wm.xft_ws_occupied;
        } else {
            glyph = "○";
            color = &wm.xft_ws_empty;
        }

        draw_utf8(draw, color, x, baseline, glyph);
        x += step_px;
    }
}

static void trim_trailing_whitespace(char *s) {
    if (!s) {
        return;
    }

    size_t len = strlen(s);
    while (len > 0) {
        unsigned char c = (unsigned char)s[len - 1];
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            s[len - 1] = '\0';
            len--;
        } else {
            break;
        }
    }
}

static void read_custom_command(const char *cmd, char *buf, size_t buflen) {
    if (!buf || buflen == 0) {
        return;
    }

    buf[0] = '\0';

    if (!cmd || !*cmd) {
        return;
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return;
    }

    if (fgets(buf, (int)buflen, fp)) {
        trim_trailing_whitespace(buf);
    }

    pclose(fp);
}

static void build_clock_text(const char *fmt, char *buf, size_t buflen) {
    if (!buf || buflen == 0) {
        return;
    }

    buf[0] = '\0';

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);

    const char *use_fmt = (fmt && *fmt) ? fmt : "%H:%M";
    strftime(buf, buflen, use_fmt, &tmv);
}

static void build_module_text(Monitor *m, BarModule *mod, char *buf, size_t buflen) {
    if (!buf || buflen == 0) {
        return;
    }

    buf[0] = '\0';

    if (!m || !mod) {
        return;
    }

    Workspace *ws = ws_of(m, m->current_ws);

    switch (mod->kind) {
        case BAR_MOD_MONITOR:
            snprintf(buf, buflen, "M%d%s", m->id + 1, (m == wm.selmon) ? "*" : "");
            break;
        case BAR_MOD_SYNC:
            snprintf(buf, buflen, "%s", wm.config.sync_workspaces ? "S" : "L");
            break;
        case BAR_MOD_TITLE:
            get_client_title(ws ? ws->focused : NULL, buf, buflen);
            break;
        case BAR_MOD_STATUS:
            get_root_status_text(buf, buflen);
            break;
        case BAR_MOD_CLOCK:
            build_clock_text(mod->arg, buf, buflen);
            break;
        case BAR_MOD_CUSTOM:
            read_custom_command(mod->arg, buf, buflen);
            break;
        default:
            break;
    }
}

static int module_width_px(Monitor *m, BarModule *mod) {
    if (!m || !mod) {
        return 0;
    }

    if (mod->kind == BAR_MOD_WORKSPACES) {
        return WORKSPACE_COUNT * 16;
    }

    char buf[512];
    build_module_text(m, mod, buf, sizeof(buf));
    return text_width_px(buf);
}

static void draw_module(Monitor *m, BarModule *mod, XftDraw *draw, int x, int baseline, int max_width) {
    if (!m || !mod || !draw || max_width <= 0) {
        return;
    }

    if (mod->kind == BAR_MOD_WORKSPACES) {
        int step = 16;
        draw_workspace_dots(m, draw, x, baseline, step);
        return;
    }

    char raw[512];
    char shown[512];
    build_module_text(m, mod, raw, sizeof(raw));

    if (raw[0] == '\0') {
        return;
    }

    utf8_truncate_to_width(raw, max_width, shown, sizeof(shown));
    if (shown[0] == '\0') {
        return;
    }

    draw_utf8(draw, &wm.xft_bar_fg, x, baseline, shown);
}

static void ensure_default_bar_modules(void) {
    if (dynconfig.bar_left_count == 0 &&
        dynconfig.bar_center_count == 0 &&
        dynconfig.bar_right_count == 0) {
        dynconfig.bar_left[0].kind = BAR_MOD_MONITOR;
        dynconfig.bar_left[1].kind = BAR_MOD_SYNC;
        dynconfig.bar_left[2].kind = BAR_MOD_WORKSPACES;
        dynconfig.bar_left_count = 3;

        dynconfig.bar_center[0].kind = BAR_MOD_TITLE;
        dynconfig.bar_center_count = 1;

        dynconfig.bar_right[0].kind = BAR_MOD_STATUS;
        dynconfig.bar_right[1].kind = BAR_MOD_CLOCK;
        snprintf(dynconfig.bar_right[1].arg, sizeof(dynconfig.bar_right[1].arg), "%s", "%H:%M");
        dynconfig.bar_right_count = 2;
    }
}

void draw_bar(Monitor *m) {
    if (!m || !m->barwin) {
        return;
    }

    Workspace *ws = ws_of(m, m->current_ws);
    if (!ws || ws->hide_bar) {
        return;
    }

    ensure_default_bar_modules();

    xcb_get_geometry_cookie_t gck = xcb_get_geometry(wm.conn, m->barwin);
    xcb_get_geometry_reply_t *geom = xcb_get_geometry_reply(wm.conn, gck, NULL);
    if (!geom) {
        return;
    }

    int bar_w = geom->width;
    int bar_h = geom->height;
    free(geom);

    Pixmap pix = XCreatePixmap(
        wm.dpy,
        (Drawable)m->barwin,
        (unsigned int)bar_w,
        (unsigned int)bar_h,
        (unsigned int)DefaultDepth(wm.dpy, wm.xscreen)
    );

    XftDraw *draw = XftDrawCreate(wm.dpy, pix, wm.visual, wm.colormap);
    if (!draw) {
        XFreePixmap(wm.dpy, pix);
        return;
    }

    GC gc = XCreateGC(wm.dpy, pix, 0, NULL);
    XftDrawRect(draw, &wm.xft_bar_bg, 0, 0, (unsigned int)bar_w, (unsigned int)bar_h);

    const int baseline = bar_text_baseline();
    const int left_pad = 8;
    const int right_pad = 8;
    const int item_gap = 10;

    int left_x = left_pad;
    for (size_t i = 0; i < dynconfig.bar_left_count; i++) {
        int w = module_width_px(m, &dynconfig.bar_left[i]);
        if (w <= 0) {
            continue;
        }
        draw_module(m, &dynconfig.bar_left[i], draw, left_x, baseline, w);
        left_x += w + item_gap;
    }

    int right_x = bar_w - right_pad;
    for (size_t idx = dynconfig.bar_right_count; idx > 0; idx--) {
        BarModule *mod = &dynconfig.bar_right[idx - 1];
        int w = module_width_px(m, mod);
        if (w <= 0) {
            continue;
        }
        right_x -= w;
        draw_module(m, mod, draw, right_x, baseline, w);
        right_x -= item_gap;
    }

    int center_left_bound = left_x;
    int center_right_bound = right_x;
    int center_width = center_right_bound - center_left_bound;

    if (center_width > 20 && dynconfig.bar_center_count > 0) {
        int total_center_w = 0;
        for (size_t i = 0; i < dynconfig.bar_center_count; i++) {
            int w = module_width_px(m, &dynconfig.bar_center[i]);
            if (w > 0) {
                total_center_w += w;
                if (i + 1 < dynconfig.bar_center_count) {
                    total_center_w += item_gap;
                }
            }
        }

        int start_x = center_left_bound + MAX(0, (center_width - total_center_w) / 2);
        if (start_x < center_left_bound) {
            start_x = center_left_bound;
        }

        int x = start_x;
        for (size_t i = 0; i < dynconfig.bar_center_count; i++) {
            int available = center_right_bound - x;
            if (available <= 0) {
                break;
            }

            int preferred = module_width_px(m, &dynconfig.bar_center[i]);
            int draw_w = MIN(preferred, available);

            if (draw_w > 0) {
                draw_module(m, &dynconfig.bar_center[i], draw, x, baseline, draw_w);
                x += draw_w + item_gap;
            }
        }
    }

    XCopyArea(
        wm.dpy,
        pix,
        (Drawable)m->barwin,
        gc,
        0,
        0,
        (unsigned int)bar_w,
        (unsigned int)bar_h,
        0,
        0
    );

    XFreeGC(wm.dpy, gc);
    XftDrawDestroy(draw);
    XFreePixmap(wm.dpy, pix);
    XFlush(wm.dpy);
}

void draw_all_bars(void) {
    for (Monitor *m = wm.mons; m; m = m->next) {
        draw_bar(m);
    }
}

void bar_tick(void) {
    time_t now = time(NULL);
    if (now != g_last_bar_tick) {
        g_last_bar_tick = now;
        draw_all_bars();
    }
}
