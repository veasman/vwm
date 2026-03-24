#include "bar.h"

#include "bar_modules.h"
#include "client.h"
#include "config.h"
#include "util.h"
#include "x11.h"

static double clamp_bar_radius(double r, int w, int h) {
    if (r < 0.0) {
        return 0.0;
    }

    double max_r = (double)MIN(w, h) / 2.0;
    if (r > max_r) {
        r = max_r;
    }

    return r;
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
        "%s:size=%.1f:"
        "antialias=true:"
        "hinting=true:"
        "hintstyle=hintslight:"
        "rgba=none:"
        "lcdfilter=lcddefault",
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
    if (!draw || !s || !*s) {
        return;
    }

    XftColor *use = color ? color : &wm.xft_bar_fg;

    XftDrawStringUtf8(
        draw,
        use,
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
    int step = step_px > 0 ? step_px : (wm.font_char_width + 4);

    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        Workspace *ws = &m->workspaces[i];
        bool current = (i == m->current_ws);
        bool occupied = workspace_has_clients(ws);

        const char *glyph = "○";
        XftColor *color = &wm.xft_ws_empty;

        if (current) {
            glyph = "●";
            color = &wm.xft_ws_current;
        } else if (occupied) {
            glyph = "●";
            color = &wm.xft_ws_occupied;
        }

        draw_utf8(draw, color, x, baseline, glyph);
        x += step;
    }
}

void draw_bar(Monitor *m) {
    if (!m || !m->barwin || !dynconfig.bar_enabled) {
        return;
    }

    Workspace *ws = ws_of(m, m->current_ws);
    if (!ws || ws->hide_bar) {
        return;
    }

    ensure_default_bar_modules();

    int bar_w = m->bar_w;
    int bar_h = m->bar_h;

    if (bar_w <= 0 || bar_h <= 0) {
        return;
    }

    bool draw_background = dynconfig.bar_style.background_enabled;

    Pixmap pix = XCreatePixmap(
        wm.dpy,
        RootWindow(wm.dpy, wm.xscreen),
        (unsigned int)bar_w,
        (unsigned int)bar_h,
        (unsigned int)wm.depth
    );
    if (!pix) {
        return;
    }

    cairo_surface_t *surface = cairo_xlib_surface_create(
        wm.dpy,
        pix,
        wm.visual,
        bar_w,
        bar_h
    );
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface) {
            cairo_surface_destroy(surface);
        }
        XFreePixmap(wm.dpy, pix);
        return;
    }

    cairo_t *cr = cairo_create(surface);
    if (!cr || cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        if (cr) {
            cairo_destroy(cr);
        }
        cairo_surface_destroy(surface);
        XFreePixmap(wm.dpy, pix);
        return;
    }

    XftDraw *xftdraw = XftDrawCreate(
        wm.dpy,
        pix,
        wm.visual,
        wm.colormap
    );
    if (!xftdraw) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        XFreePixmap(wm.dpy, pix);
        return;
    }

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    if (draw_background) {
        double r = ((wm.config.bar_bg >> 16) & 0xff) / 255.0;
        double g = ((wm.config.bar_bg >> 8) & 0xff) / 255.0;
        double b = (wm.config.bar_bg & 0xff) / 255.0;
        double rr = clamp_bar_radius((double)dynconfig.bar_style.radius, bar_w, bar_h);

        if (rr > 0.0) {
            cairo_new_path(cr);
            cairo_arc(cr, bar_w - rr, rr, rr, -M_PI / 2.0, 0.0);
            cairo_arc(cr, bar_w - rr, bar_h - rr, rr, 0.0, M_PI / 2.0);
            cairo_arc(cr, rr, bar_h - rr, rr, M_PI / 2.0, M_PI);
            cairo_arc(cr, rr, rr, rr, M_PI, 3.0 * M_PI / 2.0);
            cairo_close_path(cr);
        } else {
            cairo_rectangle(cr, 0.0, 0.0, (double)bar_w, (double)bar_h);
        }

        cairo_set_source_rgb(cr, r, g, b);
        cairo_fill(cr);
    }

    cairo_restore(cr);

    const int baseline = bar_text_baseline();
    const int item_gap = module_gap_px();

    int content_left = MAX(0, dynconfig.bar_style.content_margin_x);
    int content_right = MAX(0, dynconfig.bar_style.content_margin_x);

    int left_x = content_left;
    for (size_t i = 0; i < dynconfig.bar_left_count; i++) {
        int w = bar_module_width_px(m, &dynconfig.bar_left[i]);
        if (w <= 0) {
            continue;
        }

        bar_draw_module(m, &dynconfig.bar_left[i], cr, xftdraw, left_x, baseline, bar_h, w);
        left_x += w + item_gap;
    }

    int right_x = bar_w - content_right;
    for (size_t idx = dynconfig.bar_right_count; idx > 0; idx--) {
        BarModule *mod = &dynconfig.bar_right[idx - 1];
        int w = bar_module_width_px(m, mod);
        if (w <= 0) {
            continue;
        }

        right_x -= w;
        bar_draw_module(m, mod, cr, xftdraw, right_x, baseline, bar_h, w);
        right_x -= item_gap;
    }

    if (dynconfig.bar_center_count > 0) {
        int total_center_w = 0;
        for (size_t i = 0; i < dynconfig.bar_center_count; i++) {
            int w = bar_module_width_px(m, &dynconfig.bar_center[i]);
            if (w > 0) {
                total_center_w += w;
                if (i + 1 < dynconfig.bar_center_count) {
                    total_center_w += item_gap;
                }
            }
        }

        int safe_left = left_x;
        int safe_right = right_x;
        int safe_width = safe_right - safe_left;

        bool render_center = (safe_width >= total_center_w + 40);

        if (render_center && total_center_w > 0) {
            int start_x = (bar_w - total_center_w) / 2;

            if (start_x < safe_left) {
                start_x = safe_left;
            }
            if (start_x + total_center_w > safe_right) {
                start_x = safe_right - total_center_w;
            }

            if (start_x >= safe_left && start_x + total_center_w <= safe_right) {
                int x = start_x;

                for (size_t i = 0; i < dynconfig.bar_center_count; i++) {
                    int w = bar_module_width_px(m, &dynconfig.bar_center[i]);
                    if (w <= 0) {
                        continue;
                    }

                    if (x + w > safe_right) {
                        break;
                    }

                    bar_draw_module(m, &dynconfig.bar_center[i], cr, xftdraw, x, baseline, bar_h, w);
                    x += w + item_gap;
                }
            }
        }
    }

    cairo_surface_flush(surface);

    GC gc = XCreateGC(wm.dpy, (Drawable)m->barwin, 0, NULL);
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

    XftDrawDestroy(xftdraw);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    XFreePixmap(wm.dpy, pix);
}

void draw_all_bars(void) {
    for (Monitor *m = wm.mons; m; m = m->next) {
        draw_bar(m);
    }
    xcb_flush(wm.conn);
}

void bar_tick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    long now_ms = (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
    static long last_ms = 0;

    if (now_ms - last_ms >= 1000) {
        last_ms = now_ms;
        refresh_system_status(false);
        update_status_cache();
        draw_all_bars();
    }
}
