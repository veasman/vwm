#include "bar.h"

#include "client.h"
#include "util.h"

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

void draw_bar(Monitor *m) {
    if (!m || !m->barwin) {
        return;
    }

    Workspace *ws = ws_of(m, m->current_ws);
    if (!ws || ws->hide_bar) {
        return;
    }

    char raw_title[512];
    char title[512];
    char status[512];
    char monlabel[32];
    char sync_label[4];

    get_client_title(ws->focused, raw_title, sizeof(raw_title));
    get_root_status_text(status, sizeof(status));
    snprintf(monlabel, sizeof(monlabel), "M%d%s", m->id + 1, (m == wm.selmon) ? "*" : "");
    snprintf(sync_label, sizeof(sync_label), "%s", wm.config.sync_workspaces ? "S" : "L");

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
    const int section_gap = 10;
    const int item_gap = 10;

    int dot_step = 16;
    if (bar_w < 700) dot_step = 14;
    if (bar_w < 520) dot_step = 12;
    if (bar_w < 380) dot_step = 10;

    int dots_w = WORKSPACE_COUNT * dot_step;
    int mon_w = text_width_px(monlabel);
    int sync_w = text_width_px(sync_label);
    int status_w = text_width_px(status);

    bool show_mon = true;
    bool show_sync = true;
    bool show_status = true;
    bool show_title = true;

    int left_cluster_w = left_pad + dots_w;
    int extra_left = 0;

    if (show_sync) {
        extra_left += item_gap + sync_w;
    }
    if (show_mon) {
        extra_left += item_gap + mon_w;
    }

    left_cluster_w += extra_left;

    int title_start = left_cluster_w + section_gap;
    int right_reserved = right_pad + (show_status && status[0] ? status_w + section_gap : 0);
    int title_space = bar_w - title_start - right_reserved;

    if (title_space < 120) {
        show_title = false;
    }

    if (bar_w < 520) {
        show_status = false;
        right_reserved = right_pad;
        title_space = bar_w - title_start - right_reserved;
        if (title_space < 120) {
            show_title = false;
        }
    }

    if (bar_w < 420) {
        show_mon = false;
        left_cluster_w = left_pad + dots_w + (show_sync ? item_gap + sync_w : 0);
        title_start = left_cluster_w + section_gap;
        right_reserved = right_pad + (show_status && status[0] ? status_w + section_gap : 0);
        title_space = bar_w - title_start - right_reserved;
        if (title_space < 120) {
            show_title = false;
        }
    }

    if (bar_w < 320) {
        show_sync = false;
        left_cluster_w = left_pad + dots_w;
        title_start = left_cluster_w + section_gap;
        right_reserved = right_pad + (show_status && status[0] ? status_w + section_gap : 0);
        title_space = bar_w - title_start - right_reserved;
        show_title = false;
        show_status = false;
    }

    title[0] = '\0';
    if (show_title && title_space > 24) {
        utf8_truncate_to_width(raw_title, title_space, title, sizeof(title));
    }

    int x = left_pad;

    if (show_mon) {
        draw_utf8(draw, &wm.xft_bar_fg, x, baseline, monlabel);
        x += mon_w + item_gap;
    }

    if (show_sync) {
        draw_utf8(draw, &wm.xft_bar_fg, x, baseline, sync_label);
        x += sync_w + item_gap;
    }

    draw_workspace_dots(m, draw, x, baseline, dot_step);

    if (show_title && title[0]) {
        draw_utf8(draw, &wm.xft_bar_fg, title_start, baseline, title);
    }

    if (show_status && status[0]) {
        int status_x = bar_w - right_pad - status_w;
        if (status_x > title_start + 20) {
            draw_utf8(draw, &wm.xft_bar_fg, status_x, baseline, status);
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
