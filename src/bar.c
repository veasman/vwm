#include "bar.h"

#include "client.h"
#include "config.h"
#include "util.h"
#include "x11.h"

#include <dirent.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void cairo_set_source_rgb_u32(cairo_t *cr, uint32_t rgb) {
    double r = ((rgb >> 16) & 0xff) / 255.0;
    double g = ((rgb >> 8) & 0xff) / 255.0;
    double b = (rgb & 0xff) / 255.0;
    cairo_set_source_rgb(cr, r, g, b);
}

static void cairo_round_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    if (w <= 0.0 || h <= 0.0) {
        return;
    }

    double rr = r;
    if (rr < 0.0) rr = 0.0;
    if (rr * 2.0 > w) rr = w / 2.0;
    if (rr * 2.0 > h) rr = h / 2.0;

    if (rr <= 0.0) {
        cairo_new_path(cr);
        cairo_rectangle(cr, x, y, w, h);
        return;
    }

    const double x2 = x + w;
    const double y2 = y + h;
    const double deg = M_PI / 180.0;

    cairo_new_path(cr);
    cairo_arc(cr, x2 - rr, y + rr, rr, -90.0 * deg,   0.0 * deg);
    cairo_arc(cr, x2 - rr, y2 - rr, rr,   0.0 * deg,  90.0 * deg);
    cairo_arc(cr, x + rr, y2 - rr, rr,  90.0 * deg, 180.0 * deg);
    cairo_arc(cr, x + rr, y + rr, rr, 180.0 * deg, 270.0 * deg);
    cairo_close_path(cr);
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

static int module_box_y(int bar_h) {
    (void)bar_h;
    return 0;
}

static int module_box_h(int bar_h) {
    return MAX(1, bar_h);
}

static int module_text_baseline_for_box(int box_y, int box_h) {
    int baseline = box_y + ((box_h - wm.font_height) / 2) + wm.font_ascent;

    /*
     * Keep a minimum visual inset from the top/bottom using module_padding_y,
     * but do not shrink the pill background itself.
     */
    int min_baseline = box_y + module_padding_y() + wm.font_ascent;
    int max_baseline = box_y + box_h - module_padding_y() - wm.font_descent;

    if (max_baseline < min_baseline) {
        return box_y + ((box_h - wm.font_height) / 2) + wm.font_ascent;
    }

    if (baseline < min_baseline) {
        baseline = min_baseline;
    }
    if (baseline > max_baseline) {
        baseline = max_baseline;
    }

    return baseline;
}

static int module_gap_px(void) {
    return MAX(0, dynconfig.bar_theme.module_gap);
}

typedef struct {
    XRectangle rects[128];
    int count;
} ShapeRects;

static void shape_rects_reset(ShapeRects *sr) {
    if (!sr) {
        return;
    }
    sr->count = 0;
    memset(sr->rects, 0, sizeof(sr->rects));
}

static void shape_rects_add(ShapeRects *sr, int x, int y, int w, int h) {
    if (!sr || sr->count >= (int)LENGTH(sr->rects)) {
        return;
    }

    if (w <= 0 || h <= 0) {
        return;
    }

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    sr->rects[sr->count].x = (short)x;
    sr->rects[sr->count].y = (short)y;
    sr->rects[sr->count].width = (unsigned short)w;
    sr->rects[sr->count].height = (unsigned short)h;
    sr->count++;
}

static void apply_bar_shape(Monitor *m, int bar_w, int bar_h, const ShapeRects *sr, bool shaped_mode) {
    (void)m;
    (void)bar_w;
    (void)bar_h;
    (void)sr;
    (void)shaped_mode;
}

static void draw_text_any(cairo_t *cr, XftDraw *xftdraw, XftColor *color, int x, int y, const char *s, uint32_t fallback_rgb) {
    if (!s || !*s) {
        return;
    }

    if (xftdraw) {
        draw_utf8(xftdraw, color, x, y, s);
        return;
    }

    if (!cr) {
        return;
    }

    cairo_save(cr);

    cairo_select_font_face(
        cr,
        wm.config.font_family[0] ? wm.config.font_family : "monospace",
        CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_NORMAL
    );
    cairo_set_font_size(cr, wm.config.font_size > 0.0 ? wm.config.font_size : 11.0);

    cairo_set_source_rgb_u32(cr, fallback_rgb);
    cairo_move_to(cr, (double)x, (double)y);
    cairo_show_text(cr, s);

    cairo_restore(cr);
}

static void draw_workspace_dots_any(Monitor *m, cairo_t *cr, XftDraw *xftdraw, int start_x, int baseline, int step_px) {
    if (!m) {
        return;
    }

    int x = start_x;
    int step = step_px > 0 ? step_px : (wm.font_char_width + 4);

    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        Workspace *ws = &m->workspaces[i];
        bool current = (i == m->current_ws);
        bool occupied = workspace_has_clients(ws);

        const char *glyph;
        XftColor *xft_color = NULL;
        uint32_t cairo_color = wm.config.workspace_empty;

        if (current) {
            glyph = "●";
            xft_color = &wm.xft_ws_current;
            cairo_color = wm.config.workspace_current;
        } else if (occupied) {
            glyph = "●";
            xft_color = &wm.xft_ws_occupied;
            cairo_color = wm.config.workspace_occupied;
        } else {
            glyph = "○";
            xft_color = &wm.xft_ws_empty;
            cairo_color = wm.config.workspace_empty;
        }

        draw_text_any(cr, xftdraw, xft_color, x, baseline, glyph, cairo_color);
        x += step;
    }
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

typedef struct {
    bool valid;
    bool muted;
    int percent;
} VolumeState;

typedef struct {
    bool valid;
    bool connected;
    bool wireless;
    char ifname[64];
    char ssid[128];
} NetworkState;

typedef struct {
    bool valid;
    bool charging;
    bool full;
    int percent;
} BatteryState;

static VolumeState get_volume_state(void);
static const char *volume_icon_for_state(VolumeState st);
static uint32_t volume_fill_color(VolumeState st);

static NetworkState get_network_state(void);
static BatteryState get_battery_state(void);

static void build_module_text(Monitor *m, BarModule *mod, char *buf, size_t buflen);

static bool bar_is_fancy(void) {
    return dynconfig.bar_theme.presentation_mode == BAR_PRESENTATION_ACCENT;
}

static bool bar_is_lean(void) {
    return dynconfig.bar_theme.presentation_mode == BAR_PRESENTATION_MINIMAL;
}

static bool volume_bar_should_draw(void) {
    return bar_is_fancy() && dynconfig.bar_theme.volume_bar_enabled;
}

static uint32_t module_text_color_for_kind(BarModuleKind kind) {
    if (bar_is_lean()) {
        return dynconfig.bar_theme.module_fg;
    }

    switch (kind) {
        case BAR_MOD_MONITOR:
            return dynconfig.bar_theme.accent_monitor;

        case BAR_MOD_SYNC:
            return wm.config.sync_workspaces
                ? dynconfig.bar_theme.accent_sync_enabled
                : dynconfig.bar_theme.accent_sync_disabled;

        case BAR_MOD_NETWORK: {
            NetworkState st = get_network_state();
            return (!st.valid || !st.connected)
                ? dynconfig.bar_theme.accent_network_down
                : dynconfig.bar_theme.accent_network_up;
        }

        case BAR_MOD_BATTERY: {
            BatteryState st = get_battery_state();

            if (!st.valid) {
                return dynconfig.bar_theme.module_fg;
            }
            if (st.full) {
                return dynconfig.bar_theme.accent_battery_full;
            }
            if (st.charging) {
                return dynconfig.bar_theme.accent_battery_charging;
            }
            if (st.percent <= 15) {
                return dynconfig.bar_theme.accent_battery_critical;
            }
            if (st.percent <= 35) {
                return dynconfig.bar_theme.accent_battery_low;
            }
            return dynconfig.bar_theme.accent_battery_normal;
        }

        case BAR_MOD_VOLUME: {
            VolumeState st = get_volume_state();
            if (!st.valid || st.muted) {
                return dynconfig.bar_theme.volume_bar_fg_muted;
            }
            return volume_fill_color(st);
        }

        case BAR_MOD_CLOCK:
            return dynconfig.bar_theme.accent_clock;

        default:
            return dynconfig.bar_theme.module_fg;
    }
}

static const char *sync_label(void) {
    return wm.config.sync_workspaces ? "sync" : "local";
}

static bool path_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

static bool read_first_line_file(const char *path, char *buf, size_t buflen) {
    if (!path || !buf || buflen == 0) {
        return false;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        return false;
    }

    bool ok = false;
    if (fgets(buf, (int)buflen, fp)) {
        trim_trailing_whitespace(buf);
        ok = true;
    }

    fclose(fp);
    return ok;
}

static bool parse_wpctl_volume_line(const char *line, VolumeState *st) {
    if (!line || !st) {
        return false;
    }

    st->muted = (strstr(line, "MUTED") != NULL);

    const char *p = strstr(line, "Volume:");
    if (!p) {
        p = line;
    }

    while (*p && !((*p >= '0' && *p <= '9') || *p == '.')) {
        p++;
    }

    if (!*p) {
        return false;
    }

    double volume = atof(p);
    st->percent = (int)(volume * 100.0 + 0.5);

    if (st->percent < 0) st->percent = 0;
    if (st->percent > 150) st->percent = 150;

    return true;
}

static VolumeState get_volume_state(void) {
    static VolumeState cached = {0};
    static struct timespec last_read = {0};

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if ((last_read.tv_sec != 0 || last_read.tv_nsec != 0)) {
        long elapsed_ms =
            (long)((now.tv_sec - last_read.tv_sec) * 1000L) +
            (long)((now.tv_nsec - last_read.tv_nsec) / 1000000L);

        if (elapsed_ms >= 0 && elapsed_ms < 250) {
            return cached;
        }
    }

    last_read = now;

    VolumeState st = {0};
    char line[256] = {0};

    FILE *fp = popen(
        "env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin "
        "sh -c '/usr/bin/wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null || wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null'",
        "r"
    );

    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            trim_trailing_whitespace(line);
            if (parse_wpctl_volume_line(line, &st)) {
                st.valid = true;
                pclose(fp);
                cached = st;
                return cached;
            }
        }
        pclose(fp);
    }

    memset(&st, 0, sizeof(st));

    fp = popen(
        "env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin "
        "sh -c '/usr/bin/pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null || pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null'",
        "r"
    );

    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            char *pct = strchr(line, '%');

            while (pct && pct > line && (*(pct - 1) < '0' || *(pct - 1) > '9')) {
                pct = strchr(pct + 1, '%');
            }

            if (pct) {
                char *start = pct;
                while (start > line && *(start - 1) >= '0' && *(start - 1) <= '9') {
                    start--;
                }

                if (start < pct) {
                    char numbuf[16];
                    size_t len = (size_t)(pct - start);
                    if (len >= sizeof(numbuf)) {
                        len = sizeof(numbuf) - 1;
                    }

                    memcpy(numbuf, start, len);
                    numbuf[len] = '\0';

                    st.percent = atoi(numbuf);
                    if (st.percent < 0) st.percent = 0;
                    if (st.percent > 150) st.percent = 150;
                    st.valid = true;
                }
            }
        }
        pclose(fp);
    }

    fp = popen(
        "env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin "
        "sh -c '/usr/bin/pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null || pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null'",
        "r"
    );

    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            st.muted = (strstr(line, "yes") != NULL);
        }
        pclose(fp);
    }

    cached = st;
    return cached;
}

static const char *volume_icon_for_state(VolumeState st) {
    if (!st.valid) return "󰖁";
    if (st.muted || st.percent == 0) return "󰝟";
    if (st.percent < 35) return "󰕿";
    if (st.percent < 70) return "󰖀";
    return "󰕾";
}

static uint32_t volume_fill_color(VolumeState st) {
    if (!st.valid || st.muted) return dynconfig.bar_theme.volume_bar_fg_muted;
    if (st.percent < 35) return dynconfig.bar_theme.volume_bar_fg_low;
    if (st.percent < 70) return dynconfig.bar_theme.volume_bar_fg_mid;
    return dynconfig.bar_theme.volume_bar_fg_high;
}

static bool is_wireless_interface_name(const char *ifname) {
    if (!ifname || !*ifname) {
        return false;
    }

    return strncmp(ifname, "wl", 2) == 0 || strncmp(ifname, "wlan", 4) == 0;
}

static NetworkState get_network_state(void) {
    static NetworkState cached = {0};
    static struct timespec last_read = {0};

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (last_read.tv_sec != 0 || last_read.tv_nsec != 0) {
        long elapsed_ms =
            (long)((now.tv_sec - last_read.tv_sec) * 1000L) +
            (long)((now.tv_nsec - last_read.tv_nsec) / 1000000L);

        if (elapsed_ms >= 0 && elapsed_ms < 1000) {
            return cached;
        }
    }

    last_read = now;

    NetworkState st = {0};

    /*
     * First try nmcli so wireless connections can show the real SSID instead of
     * the raw interface name. This is the polished path.
     */
    FILE *fp = popen(
        "env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin "
        "sh -c '"
        "/usr/bin/nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device 2>/dev/null "
        "|| nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device 2>/dev/null"
        "'",
        "r"
    );

    if (fp) {
        char line[512];

        while (fgets(line, sizeof(line), fp)) {
            trim_trailing_whitespace(line);

            char *saveptr = NULL;
            char *dev = strtok_r(line, ":", &saveptr);
            char *type = strtok_r(NULL, ":", &saveptr);
            char *state = strtok_r(NULL, ":", &saveptr);
            char *conn = strtok_r(NULL, "", &saveptr);

            if (!dev || !type || !state) {
                continue;
            }

            if (strcmp(state, "connected") != 0) {
                continue;
            }

            if (strcmp(dev, "lo") == 0 ||
                strncmp(dev, "br-", 3) == 0 ||
                strncmp(dev, "docker", 6) == 0 ||
                strncmp(dev, "veth", 4) == 0 ||
                strncmp(dev, "tailscale", 9) == 0 ||
                strncmp(dev, "tun", 3) == 0) {
                continue;
            }

            st.valid = true;
            st.connected = true;
            st.wireless =
                (strcmp(type, "wifi") == 0) ||
                (strcmp(type, "wireless") == 0) ||
                is_wireless_interface_name(dev);

            snprintf(st.ifname, sizeof(st.ifname), "%.63s", dev);

            if (conn && *conn && strcmp(conn, "--") != 0) {
                snprintf(st.ssid, sizeof(st.ssid), "%.127s", conn);
            } else {
                st.ssid[0] = '\0';
            }

            pclose(fp);
            cached = st;
            return cached;
        }

        pclose(fp);
    }

    /*
     * Fallback to sysfs if nmcli is unavailable or returns nothing usable.
     */
    DIR *dir = opendir("/sys/class/net");
    if (!dir) {
        cached = st;
        return cached;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }

        if (strcmp(ent->d_name, "lo") == 0) {
            continue;
        }

        if (strncmp(ent->d_name, "br-", 3) == 0 ||
            strncmp(ent->d_name, "docker", 6) == 0 ||
            strncmp(ent->d_name, "veth", 4) == 0 ||
            strncmp(ent->d_name, "tailscale", 9) == 0 ||
            strncmp(ent->d_name, "tun", 3) == 0) {
            continue;
        }

        char oper_path[512];
        char carrier_path[512];
        char wireless_path[512];
        char state[64] = {0};
        char carrier[64] = {0};

        snprintf(oper_path, sizeof(oper_path), "/sys/class/net/%s/operstate", ent->d_name);
        snprintf(carrier_path, sizeof(carrier_path), "/sys/class/net/%s/carrier", ent->d_name);
        snprintf(wireless_path, sizeof(wireless_path), "/sys/class/net/%s/wireless", ent->d_name);

        bool have_oper = read_first_line_file(oper_path, state, sizeof(state));
        bool have_carrier = read_first_line_file(carrier_path, carrier, sizeof(carrier));

        bool connected = false;
        if (have_carrier && strcmp(carrier, "1") == 0) {
            connected = true;
        } else if (have_oper &&
                   (strcmp(state, "up") == 0 ||
                    strcmp(state, "unknown") == 0 ||
                    strcmp(state, "dormant") == 0)) {
            connected = true;
        }

        if (!connected) {
            continue;
        }

        st.valid = true;
        st.connected = true;
        st.wireless = path_exists(wireless_path) || is_wireless_interface_name(ent->d_name);
        snprintf(st.ifname, sizeof(st.ifname), "%.63s", ent->d_name);
        st.ssid[0] = '\0';

        break;
    }

    closedir(dir);
    cached = st;
    return cached;
}

static BatteryState get_battery_state(void) {
    static BatteryState cached = {0};
    static struct timespec last_read = {0};

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if ((last_read.tv_sec != 0 || last_read.tv_nsec != 0)) {
        long elapsed_ms =
            (long)((now.tv_sec - last_read.tv_sec) * 1000L) +
            (long)((now.tv_nsec - last_read.tv_nsec) / 1000000L);

        if (elapsed_ms >= 0 && elapsed_ms < 5000) {
            return cached;
        }
    }

    last_read = now;

    BatteryState st = {0};

    DIR *dir = opendir("/sys/class/power_supply");
    if (!dir) {
        cached = st;
        return cached;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }

        char type_path[512];
        char cap_path[512];
        char status_path[512];
        char typebuf[64] = {0};
        char capbuf[64] = {0};
        char statbuf[64] = {0};

        snprintf(type_path, sizeof(type_path), "/sys/class/power_supply/%s/type", ent->d_name);
        if (!read_first_line_file(type_path, typebuf, sizeof(typebuf))) {
            continue;
        }

        if (strcmp(typebuf, "Battery") != 0) {
            continue;
        }

        snprintf(cap_path, sizeof(cap_path), "/sys/class/power_supply/%s/capacity", ent->d_name);
        if (!read_first_line_file(cap_path, capbuf, sizeof(capbuf))) {
            continue;
        }

        st.valid = true;
        st.percent = atoi(capbuf);
        if (st.percent < 0) st.percent = 0;
        if (st.percent > 100) st.percent = 100;

        snprintf(status_path, sizeof(status_path), "/sys/class/power_supply/%s/status", ent->d_name);
        if (read_first_line_file(status_path, statbuf, sizeof(statbuf))) {
            st.charging = (strcmp(statbuf, "Charging") == 0);
            st.full = (strcmp(statbuf, "Full") == 0);
        }

        break;
    }

    closedir(dir);
    cached = st;
    return cached;
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
    const bool accent = bar_is_fancy();

    switch (mod->kind) {
        case BAR_MOD_MONITOR:
            if (accent) {
                snprintf(buf, buflen, "%sM%d", (m == wm.selmon) ? "󰍹 " : "󰍺 ", m->id + 1);
            } else {
                snprintf(buf, buflen, "M%d", m->id + 1);
            }
            break;

        case BAR_MOD_SYNC:
            if (accent) {
                snprintf(buf, buflen, "%s %s", wm.config.sync_workspaces ? "󰓦" : "󰍹", sync_label());
            } else {
                snprintf(buf, buflen, "%s", sync_label());
            }
            break;

        case BAR_MOD_TITLE:
            get_client_title(ws ? ws->focused : NULL, buf, buflen);
            break;

        case BAR_MOD_STATUS:
            get_root_status_text(buf, buflen);
            break;

        case BAR_MOD_CLOCK:
            if (accent) {
                char timebuf[128];
                build_clock_text(mod->arg[0] ? mod->arg : "%Y-%m-%d %H:%M", timebuf, sizeof(timebuf));
                snprintf(buf, buflen, "󰥔 %s", timebuf);
            } else {
                build_clock_text(mod->arg[0] ? mod->arg : "%Y-%m-%d %H:%M", buf, buflen);
            }
            break;

        case BAR_MOD_CUSTOM:
            read_custom_command(mod->arg, buf, buflen);
            break;

        case BAR_MOD_VOLUME: {
            VolumeState st = get_volume_state();

            if (!st.valid) {
                snprintf(buf, buflen, accent ? "󰝟 audio" : "vol n/a");
                break;
            }

            if (accent) {
                const char *icon = volume_icon_for_state(st);
                if (st.muted) {
                    snprintf(buf, buflen, "%s muted", icon);
                } else {
                    snprintf(buf, buflen, "%s %d%%", icon, st.percent);
                }
            } else {
                if (st.muted) {
                    snprintf(buf, buflen, "vol muted");
                } else {
                    snprintf(buf, buflen, "vol %d%%", st.percent);
                }
            }
            break;
        }

        case BAR_MOD_NETWORK: {
            NetworkState st = get_network_state();

            if (!st.valid || !st.connected) {
                snprintf(buf, buflen, accent ? "󰤭 offline" : "net down");
                break;
            }

            if (accent) {
                if (st.wireless) {
                    if (st.ssid[0] != '\0') {
                        snprintf(buf, buflen, "󰤨 %s", st.ssid);
                    } else if (st.ifname[0] != '\0') {
                        snprintf(buf, buflen, "󰤨 %s", st.ifname);
                    } else {
                        snprintf(buf, buflen, "󰤨 wifi");
                    }
                } else {
                    if (st.ifname[0] != '\0') {
                        snprintf(buf, buflen, "󰈀 %s", st.ifname);
                    } else {
                        snprintf(buf, buflen, "󰈀 wired");
                    }
                }
            } else {
                if (st.wireless) {
                    if (st.ssid[0] != '\0') {
                        snprintf(buf, buflen, "%s", st.ssid);
                    } else if (st.ifname[0] != '\0') {
                        snprintf(buf, buflen, "%s", st.ifname);
                    } else {
                        snprintf(buf, buflen, "wifi");
                    }
                } else {
                    snprintf(buf, buflen, "wired");
                }
            }
            break;
        }

        case BAR_MOD_BATTERY: {
            BatteryState st = get_battery_state();

            if (!st.valid) {
                snprintf(buf, buflen, accent ? "󰂑 battery" : "bat n/a");
                break;
            }

            if (accent) {
                const char *icon = "󰁹";

                if (st.full) {
                    icon = "󰁹";
                } else if (st.charging) {
                    icon = "󰂄";
                } else if (st.percent >= 90) {
                    icon = "󰂂";
                } else if (st.percent >= 70) {
                    icon = "󰂀";
                } else if (st.percent >= 50) {
                    icon = "󰁾";
                } else if (st.percent >= 30) {
                    icon = "󰁼";
                } else if (st.percent >= 10) {
                    icon = "󰁺";
                } else {
                    icon = "󰂎";
                }

                if (st.full) {
                    snprintf(buf, buflen, "%s full", icon);
                } else {
                    snprintf(buf, buflen, "%s %d%%", icon, st.percent);
                }
            } else {
                if (st.full) {
                    snprintf(buf, buflen, "BAT full");
                } else {
                    snprintf(buf, buflen, "BAT %d%%", st.percent);
                }
            }
            break;
        }

        default:
            break;
    }
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

        if (mod->kind == BAR_MOD_VOLUME && volume_bar_should_draw()) {
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

static void draw_module_background(cairo_t *cr, int x, int y, int w, int h) {
    if (dynconfig.bar_theme.mode != BAR_STYLE_FLOATING || w <= 0 || h <= 0) {
        return;
    }

    const int border_w = MAX(0, dynconfig.bar_theme.module_border_width);
    const double outer_x = (double)x;
    const double outer_y = (double)y;
    const double outer_w = (double)w;
    const double outer_h = (double)h;
    const double outer_r = (double)MAX(0, dynconfig.bar_theme.module_radius);

    cairo_save(cr);

    /*
     * Fill uses the full pill rect.
     */
    cairo_round_rect(
        cr,
        outer_x,
        outer_y,
        outer_w,
        outer_h,
        outer_r
    );
    cairo_set_source_rgb_u32(cr, dynconfig.bar_theme.module_bg);
    cairo_fill(cr);

    /*
     * Border must be inset by half the stroke width, otherwise thick borders
     * get centered on the outer edge and look clipped / mismatched.
     */
    if (border_w > 0) {
        const double inset = (double)border_w / 2.0;
        const double stroke_x = outer_x + inset;
        const double stroke_y = outer_y + inset;
        const double stroke_w = outer_w - (inset * 2.0);
        const double stroke_h = outer_h - (inset * 2.0);

        if (stroke_w > 0.0 && stroke_h > 0.0) {
            double stroke_r = outer_r - inset;
            if (stroke_r < 0.0) {
                stroke_r = 0.0;
            }

            cairo_round_rect(
                cr,
                stroke_x,
                stroke_y,
                stroke_w,
                stroke_h,
                stroke_r
            );
            cairo_set_source_rgb_u32(cr, dynconfig.bar_theme.module_border);
            cairo_set_line_width(cr, (double)border_w);
            cairo_stroke(cr);
        }
    }

    cairo_restore(cr);
}

static void draw_volume_bar(cairo_t *cr, int x, int y, int w, int h, VolumeState st) {
    if (w <= 0 || h <= 0) {
        return;
    }

    cairo_save(cr);

    cairo_round_rect(cr, (double)x, (double)y, (double)w, (double)h, (double)MAX(0, dynconfig.bar_theme.volume_bar_radius));
    cairo_set_source_rgb_u32(cr, dynconfig.bar_theme.volume_bar_bg);
    cairo_fill(cr);

    int pct = st.valid ? st.percent : 0;
    if (st.muted) pct = 0;
    pct = CLAMP(pct, 0, 100);

    int fill_w = (w * pct) / 100;
    if (fill_w > 0) {
        cairo_round_rect(cr, (double)x, (double)y, (double)fill_w, (double)h, (double)MAX(0, dynconfig.bar_theme.volume_bar_radius));
        cairo_set_source_rgb_u32(cr, volume_fill_color(st));
        cairo_fill(cr);
    }

    cairo_restore(cr);
}

static void draw_module(Monitor *m, BarModule *mod, cairo_t *cr, XftDraw *xftdraw, int x, int baseline, int bar_h, int width) {
    if (!m || !mod || !cr || width <= 0) {
        return;
    }

    int pad_x = module_padding_x();

    int box_h = bar_h;
    int box_y = 0;
    int local_baseline = baseline;

    if (dynconfig.bar_theme.mode == BAR_STYLE_FLOATING) {
        box_y = module_box_y(bar_h);
        box_h = module_box_h(bar_h);

        draw_module_background(cr, x, box_y, width, box_h);
        local_baseline = module_text_baseline_for_box(box_y, box_h);
    }

    int text_x = x + pad_x;

    if (mod->kind == BAR_MOD_WORKSPACES) {
        int step = wm.font_char_width + 4;
        draw_workspace_dots_any(m, cr, xftdraw, text_x, local_baseline, step);
        return;
    }

    if (mod->kind == BAR_MOD_VOLUME) {
        VolumeState st = get_volume_state();
        char raw[256];
        char shown[256];

        build_module_text(m, mod, raw, sizeof(raw));

        int bar_w = 0;
        int bar_h_px = 0;
        int text_available = width - (pad_x * 2);

        if (volume_bar_should_draw()) {
            bar_w = MAX(0, dynconfig.bar_theme.volume_bar_width);
            bar_h_px = MAX(1, dynconfig.bar_theme.volume_bar_height);
            text_available -= (bar_w + 8);
        }

        shown[0] = '\0';

        if (text_available > 0) {
            uint32_t fg = module_text_color_for_kind(mod->kind);
            XftColor text_color;

            utf8_truncate_to_width(raw, text_available, shown, sizeof(shown));

            if (shown[0] != '\0') {
                memset(&text_color, 0, sizeof(text_color));
                alloc_xft_color(&text_color, fg);
                draw_text_any(cr, xftdraw, &text_color, text_x, local_baseline, shown, fg);
                XftColorFree(wm.dpy, wm.visual, wm.colormap, &text_color);
            }
        }

        if (volume_bar_should_draw() && bar_w > 0) {
            int text_w = text_width_px(shown);
            int bx = text_x + MAX(0, text_w) + 8;
            int by = box_y + (box_h - bar_h_px) / 2;
            draw_volume_bar(cr, bx, by, bar_w, bar_h_px, st);
        }

        return;
    }

    {
        char raw[512];
        char shown[512];
        uint32_t fg = module_text_color_for_kind(mod->kind);
        XftColor text_color;

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

        memset(&text_color, 0, sizeof(text_color));
        alloc_xft_color(&text_color, fg);
        draw_text_any(cr, xftdraw, &text_color, text_x, local_baseline, shown, fg);
        XftColorFree(wm.dpy, wm.visual, wm.colormap, &text_color);
    }
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

        dynconfig.bar_right[0].kind = BAR_MOD_NETWORK;
        dynconfig.bar_right[1].kind = BAR_MOD_BATTERY;
        dynconfig.bar_right[2].kind = BAR_MOD_VOLUME;
        dynconfig.bar_right[3].kind = BAR_MOD_CLOCK;

        snprintf(
            dynconfig.bar_right[3].arg,
            sizeof(dynconfig.bar_right[3].arg),
            "%s",
            "%Y-%m-%d %H:%M"
        );
        dynconfig.bar_right_count = 4;
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

    int bar_w = m->bar_w;
    int bar_h = m->bar_h;

    if (bar_w <= 0 || bar_h <= 0) {
        return;
    }

    bool transparent_mode =
        dynconfig.bar_theme.mode == BAR_STYLE_FLOATING &&
        dynconfig.bar_theme.transparent_background &&
        wm.has_argb_visual;

    bool shaped_mode = false;

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

    ShapeRects sr;
    shape_rects_reset(&sr);

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    if (transparent_mode) {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    } else {
        cairo_set_source_rgb_u32(cr, wm.config.bar_bg);
    }
    cairo_paint(cr);
    cairo_restore(cr);

    const int baseline = bar_text_baseline();
    const int left_pad = 8;
    const int right_pad = 8;
    const int item_gap = module_gap_px();

    int left_x = left_pad;
    for (size_t i = 0; i < dynconfig.bar_left_count; i++) {
        int w = module_width_px(m, &dynconfig.bar_left[i]);
        if (w <= 0) {
            continue;
        }

        draw_module(m, &dynconfig.bar_left[i], cr, xftdraw, left_x, baseline, bar_h, w);

        if (shaped_mode) {
            int box_y = module_box_y(bar_h);
            int box_h = module_box_h(bar_h);
            shape_rects_add(&sr, left_x, box_y, w, box_h);
        }

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
        draw_module(m, mod, cr, xftdraw, right_x, baseline, bar_h, w);

        if (shaped_mode) {
            int box_y = module_box_y(bar_h);
            int box_h = module_box_h(bar_h);
            shape_rects_add(&sr, right_x, box_y, w, box_h);
        }

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

            draw_module(m, &dynconfig.bar_center[i], cr, xftdraw, x, baseline, bar_h, draw_w);

            if (shaped_mode) {
                int box_y = module_box_y(bar_h);
                int box_h = module_box_h(bar_h);
                shape_rects_add(&sr, x, box_y, draw_w, box_h);
            }

            x += draw_w + item_gap;
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

    apply_bar_shape(m, bar_w, bar_h, &sr, shaped_mode);

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

    if (now_ms - last_ms >= 120) {
        last_ms = now_ms;
        update_status_cache();
        draw_all_bars();
    }
}
