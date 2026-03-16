#include "bar.h"

#include "client.h"
#include "config.h"
#include "util.h"

#include <time.h>

typedef struct {
    bool valid;
    bool muted;
    int percent;
} VolumeState;

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
        x += step;
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

static VolumeState get_volume_state(void) {
    VolumeState st = {0};

    FILE *fp = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@", "r");
    if (!fp) {
        return st;
    }

    char line[256];
    line[0] = '\0';

    if (!fgets(line, sizeof(line), fp)) {
        pclose(fp);
        return st;
    }

    pclose(fp);
    trim_trailing_whitespace(line);

    st.valid = true;
    st.muted = (strstr(line, "MUTED") != NULL);

    char *last_space = strrchr(line, ' ');
    if (last_space && *(last_space + 1)) {
        double volume = atof(last_space + 1);
        st.percent = (int)(volume * 100.0 + 0.5);
        if (st.percent < 0) st.percent = 0;
        if (st.percent > 150) st.percent = 150;
    }

    return st;
}

static const char *volume_icon_for_state(VolumeState st) {
    if (!st.valid) return "󰖁";
    if (st.muted || st.percent == 0) return "󰖁";
    if (st.percent < 35) return "󰕿";
    if (st.percent < 70) return "󰖀";
    return "󰕾";
}

static uint32_t volume_fill_color(VolumeState st) {
    if (!st.valid || st.muted) {
        return dynconfig.bar_theme.volume_bar_fg_muted;
    }
    if (st.percent < 35) {
        return dynconfig.bar_theme.volume_bar_fg_low;
    }
    if (st.percent < 70) {
        return dynconfig.bar_theme.volume_bar_fg_mid;
    }
    return dynconfig.bar_theme.volume_bar_fg_high;
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
            build_clock_text(mod->arg[0] ? mod->arg : "%H:%M", buf, buflen);
            break;

        case BAR_MOD_CUSTOM:
            read_custom_command(mod->arg, buf, buflen);
            break;

        case BAR_MOD_VOLUME: {
            VolumeState st = get_volume_state();
            if (!st.valid) {
                snprintf(buf, buflen, "󰖁 ?");
                break;
            }

            const char *icon = volume_icon_for_state(st);
            if (st.muted) {
                snprintf(buf, buflen, "%s muted", icon);
            } else {
                snprintf(buf, buflen, "%s %d%%", icon, st.percent);
            }
            break;
        }

        default:
            break;
    }
}

static int module_padding_x(void) {
    return dynconfig.bar_theme.mode == BAR_STYLE_FLOATING
        ? MAX(0, dynconfig.bar_theme.module_padding_x)
        : 0;
}

static int module_padding_y(void) {
    return dynconfig.bar_theme.mode == BAR_STYLE_FLOATING
        ? MAX(0, dynconfig.bar_theme.module_padding_y)
        : 0;
}

static int module_gap_px(void) {
    if (dynconfig.bar_theme.mode == BAR_STYLE_FLOATING) {
        return MAX(0, dynconfig.bar_theme.module_gap);
    }
    return 10;
}

static void fill_round_rect(Display *dpy, Drawable d, GC gc, int x, int y, unsigned int w, unsigned int h, unsigned int r) {
    if (!dpy || !gc || w == 0 || h == 0) {
        return;
    }

    if (r == 0) {
        XFillRectangle(dpy, d, gc, x, y, w, h);
        return;
    }

    unsigned int rr = r;
    if (rr * 2 > w) rr = w / 2;
    if (rr * 2 > h) rr = h / 2;

    if (rr == 0) {
        XFillRectangle(dpy, d, gc, x, y, w, h);
        return;
    }

    unsigned int dia = rr * 2;

    if (w > dia) {
        XFillRectangle(dpy, d, gc, x + (int)rr, y, w - dia, h);
    }
    if (h > dia) {
        XFillRectangle(dpy, d, gc, x, y + (int)rr, w, h - dia);
    }

    XFillArc(dpy, d, gc, x, y, dia, dia, 90 * 64, 90 * 64);
    XFillArc(dpy, d, gc, x + (int)w - (int)dia, y, dia, dia, 0, 90 * 64);
    XFillArc(dpy, d, gc, x, y + (int)h - (int)dia, dia, dia, 180 * 64, 90 * 64);
    XFillArc(dpy, d, gc, x + (int)w - (int)dia, y + (int)h - (int)dia, dia, dia, 270 * 64, 90 * 64);
}

static void draw_module_background(Pixmap pix, GC gc, int x, int y, int w, int h) {
    if (dynconfig.bar_theme.mode != BAR_STYLE_FLOATING) {
        return;
    }

    if (w <= 0 || h <= 0) {
        return;
    }

    XSetForeground(wm.dpy, gc, dynconfig.bar_theme.module_bg);
    fill_round_rect(
        wm.dpy,
        pix,
        gc,
        x,
        y,
        (unsigned int)w,
        (unsigned int)h,
        (unsigned int)MAX(0, dynconfig.bar_theme.module_radius)
    );
}

static int module_width_px(Monitor *m, BarModule *mod) {
    int content_w = 0;

    if (!m || !mod) {
        return 0;
    }

    if (mod->kind == BAR_MOD_WORKSPACES) {
        int step = wm.font_char_width + 4;
        int glyph_w = text_width_px("●");
        if (glyph_w <= 0) {
            glyph_w = wm.font_char_width;
        }
        content_w = ((WORKSPACE_COUNT - 1) * step) + glyph_w;
    } else {
        char buf[512];
        build_module_text(m, mod, buf, sizeof(buf));
        content_w = text_width_px(buf);

        if (mod->kind == BAR_MOD_VOLUME && dynconfig.bar_theme.volume_bar_enabled) {
            content_w += 8 + MAX(0, dynconfig.bar_theme.volume_bar_width);
        }
    }

    if (content_w <= 0) {
        return 0;
    }

    if (dynconfig.bar_theme.mode == BAR_STYLE_FLOATING) {
        content_w += module_padding_x() * 2;
    }

    return content_w;
}

static void draw_module(Monitor *m, BarModule *mod, XftDraw *draw, Pixmap pix, GC gc, int x, int baseline, int bar_h, int width) {
    if (!m || !mod || !draw || width <= 0) {
        return;
    }

    int pad_x = module_padding_x();
    int pad_y = module_padding_y();

    int box_h = bar_h;
    int box_y = 0;
    int local_baseline = baseline;

    if (dynconfig.bar_theme.mode == BAR_STYLE_FLOATING) {
        box_h = wm.font_height + (pad_y * 2);
        box_h = MIN(bar_h, MAX(1, box_h));
        box_y = (bar_h - box_h) / 2;

        draw_module_background(pix, gc, x, box_y, width, box_h);

        local_baseline = box_y + ((box_h - wm.font_height) / 2) + wm.font_ascent;
    }

    int text_x = x + pad_x;

    if (mod->kind == BAR_MOD_WORKSPACES) {
        int step = wm.font_char_width + 4;
        draw_workspace_dots(m, draw, text_x, local_baseline, step);
        return;
    }

    if (mod->kind == BAR_MOD_VOLUME) {
        VolumeState st = get_volume_state();

        char raw[256];
        char shown[256];
        build_module_text(m, mod, raw, sizeof(raw));

        int bar_w = 0;
        int bar_h_px = 0;
        int bar_radius = 0;
        int text_available = width - (pad_x * 2);

        if (dynconfig.bar_theme.volume_bar_enabled) {
            bar_w = MAX(0, dynconfig.bar_theme.volume_bar_width);
            bar_h_px = MAX(1, dynconfig.bar_theme.volume_bar_height);
            bar_radius = MAX(0, dynconfig.bar_theme.volume_bar_radius);
            text_available -= (bar_w + 8);
        }

        if (text_available > 0) {
            utf8_truncate_to_width(raw, text_available, shown, sizeof(shown));
            if (shown[0] != '\0') {
                draw_utf8(draw, &wm.xft_bar_fg, text_x, local_baseline, shown);
            }
        }

        if (dynconfig.bar_theme.volume_bar_enabled && bar_w > 0) {
            int text_w = text_width_px(shown);
            int bx = text_x + MAX(0, text_w) + 8;
            int by = box_y + (box_h - bar_h_px) / 2;

            XSetForeground(wm.dpy, gc, dynconfig.bar_theme.volume_bar_bg);
            fill_round_rect(
                wm.dpy,
                pix,
                gc,
                bx,
                by,
                (unsigned int)bar_w,
                (unsigned int)bar_h_px,
                (unsigned int)bar_radius
            );

            int pct = st.valid ? st.percent : 0;
            if (st.muted) pct = 0;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;

            int fill_w = (bar_w * pct) / 100;
            if (fill_w > 0) {
                XSetForeground(wm.dpy, gc, volume_fill_color(st));
                fill_round_rect(
                    wm.dpy,
                    pix,
                    gc,
                    bx,
                    by,
                    (unsigned int)fill_w,
                    (unsigned int)bar_h_px,
                    (unsigned int)bar_radius
                );
            }
        }

        return;
    }

    char raw[512];
    char shown[512];
    build_module_text(m, mod, raw, sizeof(raw));

    if (raw[0] == '\0') {
        return;
    }

    int available = width - (pad_x * 2);
    if (available <= 0) {
        return;
    }

    utf8_truncate_to_width(raw, available, shown, sizeof(shown));
    if (shown[0] == '\0') {
        return;
    }

    draw_utf8(draw, &wm.xft_bar_fg, text_x, local_baseline, shown);
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

        dynconfig.bar_right[0].kind = BAR_MOD_VOLUME;
        dynconfig.bar_right[1].kind = BAR_MOD_CLOCK;
        snprintf(dynconfig.bar_right[1].arg, sizeof(dynconfig.bar_right[1].arg), "%s", "%Y-%m-%d %H:%M");
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

    XSetForeground(wm.dpy, gc, wm.config.bar_bg);
    XFillRectangle(wm.dpy, pix, gc, 0, 0, (unsigned int)bar_w, (unsigned int)bar_h);

    const int baseline = bar_text_baseline();
    const int left_pad = 8;
    const int right_pad = 8;
    const int item_gap = dynconfig.bar_theme.mode == BAR_STYLE_FLOATING ? module_gap_px() : 10;

    int left_x = left_pad;
    for (size_t i = 0; i < dynconfig.bar_left_count; i++) {
        int w = module_width_px(m, &dynconfig.bar_left[i]);
        if (w <= 0) {
            continue;
        }
        draw_module(m, &dynconfig.bar_left[i], draw, pix, gc, left_x, baseline, bar_h, w);
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
        draw_module(m, mod, draw, pix, gc, right_x, baseline, bar_h, w);
        right_x -= item_gap;
    }

    if (dynconfig.bar_center_count > 0) {
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

        int start_x = (bar_w - total_center_w) / 2;
        int min_x = left_x + item_gap;
        int max_x = right_x - item_gap - total_center_w;

        if (start_x < min_x) {
            start_x = min_x;
        }
        if (start_x > max_x) {
            start_x = max_x;
        }

        int x = start_x;
        for (size_t i = 0; i < dynconfig.bar_center_count; i++) {
            int preferred = module_width_px(m, &dynconfig.bar_center[i]);
            if (preferred <= 0) {
                continue;
            }

            int available = right_x - item_gap - x;
            if (available <= 0) {
                break;
            }

            int draw_w = MIN(preferred, available);
            if (draw_w <= 0) {
                break;
            }

            draw_module(m, &dynconfig.bar_center[i], draw, pix, gc, x, baseline, bar_h, draw_w);
            x += draw_w + item_gap;
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
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    time_t now_ms = (time_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    static time_t last_ms = 0;

    if (now_ms - last_ms >= 150) {
        last_ms = now_ms;
        update_status_cache();
        draw_all_bars();
    }
}
