#include "bar_modules.h"

#include "client.h"

static void cairo_set_source_rgb_u32_local(cairo_t *cr, uint32_t rgb) {
    double r = ((rgb >> 16) & 0xff) / 255.0;
    double g = ((rgb >> 8) & 0xff) / 255.0;
    double b = (rgb & 0xff) / 255.0;
    cairo_set_source_rgb(cr, r, g, b);
}

static void cairo_round_rect_local(cairo_t *cr, double x, double y, double w, double h, double r) {
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
    cairo_arc(cr, x2 - rr, y + rr, rr, -90.0 * deg, 0.0 * deg);
    cairo_arc(cr, x2 - rr, y2 - rr, rr, 0.0 * deg, 90.0 * deg);
    cairo_arc(cr, x + rr, y2 - rr, rr, 90.0 * deg, 180.0 * deg);
    cairo_arc(cr, x + rr, y + rr, rr, 180.0 * deg, 270.0 * deg);
    cairo_close_path(cr);
}

static bool bar_use_icons(void) {
    return dynconfig.bar_style.use_icons;
}

static bool bar_use_colors(void) {
    return dynconfig.bar_style.use_colors;
}

bool bar_is_floating(void) {
    return dynconfig.bar_style.radius > 0;
}

static bool bar_uses_pills(void) {
    return dynconfig.bar_style.modules == BAR_MODULE_STYLE_PILL;
}

int module_padding_x(void) {
    return bar_uses_pills() ? MAX(0, dynconfig.bar_style.padding_x) : 0;
}

int module_padding_y_budget(void) {
    return bar_uses_pills() ? MAX(0, dynconfig.bar_style.padding_y) : 0;
}

int module_gap_px(void) {
    return MAX(0, dynconfig.bar_style.gap);
}

int module_box_y(int bar_h) {
    int y = MAX(0, dynconfig.bar_style.content_margin_y);
    int h = module_box_h(bar_h);

    if (y + h > bar_h) {
        y = MAX(0, bar_h - h);
    }

    return y;
}

int module_box_h(int bar_h) {
    int h = bar_h - MAX(0, dynconfig.bar_style.padding_y);
    return MAX(1, h);
}

int module_text_baseline_for_box(int box_y, int box_h) {
    int baseline = box_y + ((box_h - wm.font_height) / 2) + wm.font_ascent;

    int min_baseline = box_y + wm.font_ascent;
    int max_baseline = box_y + box_h - wm.font_descent;

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

void bar_shape_rects_reset(ShapeRects *sr) {
    if (!sr) {
        return;
    }

    sr->count = 0;
    memset(sr->rects, 0, sizeof(sr->rects));
}

void bar_shape_rects_add(ShapeRects *sr, int x, int y, int w, int h) {
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

static uint32_t color_module_bg(void) { return dynconfig.theme.surface; }
static uint32_t color_module_fg(void) { return dynconfig.theme.text; }
static uint32_t color_module_border(void) { return dynconfig.theme.border; }
static uint32_t color_volume_bar_bg(void) { return dynconfig.theme.border; }

static uint32_t color_monitor(void) { return dynconfig.theme.accent; }
static uint32_t color_clock(void) { return dynconfig.theme.accent_soft; }
static uint32_t color_sync_enabled(void) { return dynconfig.theme.accent; }
static uint32_t color_sync_disabled(void) { return dynconfig.theme.text_muted; }
static uint32_t color_network_up(void) { return dynconfig.theme.text; }
static uint32_t color_network_down(void) { return dynconfig.theme.text_muted; }
static uint32_t color_battery_full(void) { return dynconfig.theme.text; }
static uint32_t color_battery_charging(void) { return dynconfig.theme.accent_soft; }
static uint32_t color_battery_normal(void) { return dynconfig.theme.text; }
static uint32_t color_battery_low(void) { return dynconfig.theme.accent_soft; }
static uint32_t color_battery_critical(void) { return dynconfig.theme.accent; }
static uint32_t color_brightness_normal(void) { return dynconfig.theme.text; }
static uint32_t color_brightness_high(void) { return dynconfig.theme.accent_soft; }
static uint32_t color_brightness_low(void) { return dynconfig.theme.text_muted; }
static uint32_t color_media_playing(void) { return dynconfig.theme.accent_soft; }
static uint32_t color_media_paused(void) { return dynconfig.theme.text_muted; }
static uint32_t color_volume_low(void) { return dynconfig.theme.text_muted; }
static uint32_t color_volume_mid(void) { return dynconfig.theme.accent_soft; }
static uint32_t color_volume_high(void) { return dynconfig.theme.accent; }
static uint32_t color_volume_muted(void) { return dynconfig.theme.text_muted; }

static uint32_t volume_fill_color(VolumeState st) {
    if (!st.valid || st.muted) return color_volume_muted();
    if (st.percent < 35) return color_volume_low();
    if (st.percent < 70) return color_volume_mid();
    return color_volume_high();
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
    cairo_set_font_size(cr, wm.config.font_size > 0.0f ? wm.config.font_size : 11.0f);
    cairo_set_source_rgb_u32_local(cr, fallback_rgb);
    cairo_move_to(cr, (double)x, (double)y);
    cairo_show_text(cr, s);
    cairo_restore(cr);
}

static void trim_trailing_whitespace_local(char *s) {
    if (!s) return;
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
    if (!buf || buflen == 0) return;
    buf[0] = '\0';
    if (!cmd || !*cmd) return;

    FILE *fp = popen(cmd, "r");
    if (!fp) return;

    if (fgets(buf, (int)buflen, fp)) {
        trim_trailing_whitespace_local(buf);
    }

    pclose(fp);
}

static void build_clock_text(const char *fmt, char *buf, size_t buflen) {
    if (!buf || buflen == 0) return;
    buf[0] = '\0';

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);

    const char *use_fmt = (fmt && *fmt) ? fmt : "%a %d %b  %H:%M";
    strftime(buf, buflen, use_fmt, &tmv);
}

static void draw_workspace_dots_any(Monitor *m, cairo_t *cr, XftDraw *xftdraw, int start_x, int baseline, int step_px) {
    if (!m) {
        return;
    }

    int x = start_x;
    int step = step_px > 0 ? step_px : (wm.font_char_width + 10);
    bool minimal_numbers = !bar_use_icons();

    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        Workspace *ws = &m->workspaces[i];
        bool current = (i == m->current_ws);
        bool occupied = workspace_has_clients(ws);

        char label[16];
        const char *glyph = NULL;
        XftColor *xft_color = &wm.xft_ws_empty;
        uint32_t cairo_color = wm.config.workspace_empty;

        if (current) {
            xft_color = &wm.xft_ws_current;
            cairo_color = wm.config.workspace_current;
        } else if (occupied) {
            xft_color = &wm.xft_ws_occupied;
            cairo_color = wm.config.workspace_occupied;
        }

        if (minimal_numbers) {
            snprintf(label, sizeof(label), "%d", i + 1);
            draw_text_any(cr, xftdraw, xft_color, x, baseline, label, cairo_color);
            x += text_width_px(label) + 12;
        } else {
            glyph = current || occupied ? "●" : "○";
            draw_text_any(cr, xftdraw, xft_color, x, baseline, glyph, cairo_color);
            x += step;
        }
    }
}

static uint32_t module_text_color_for_kind(BarModuleKind kind) {
    if (!bar_use_colors()) {
        return color_module_fg();
    }

    switch (kind) {
        case BAR_MOD_MONITOR:
            return color_monitor();

        case BAR_MOD_SYNC:
            return wm.config.sync_workspaces ? color_sync_enabled() : color_sync_disabled();

        case BAR_MOD_NETWORK: {
            NetworkState st = get_network_state();
            return (!st.valid || !st.connected) ? color_network_down() : color_network_up();
        }

        case BAR_MOD_BATTERY: {
            BatteryState st = get_battery_state();
            if (!st.valid) return color_module_fg();
            if (st.full) return color_battery_full();
            if (st.charging) return color_battery_charging();
            if (st.percent <= 15) return color_battery_critical();
            if (st.percent <= 35) return color_battery_low();
            return color_battery_normal();
        }

        case BAR_MOD_BRIGHTNESS: {
            BrightnessState st = get_brightness_state();
            if (!st.valid) return color_module_fg();
            if (st.percent >= 75) return color_brightness_high();
            if (st.percent <= 20) return color_brightness_low();
            return color_brightness_normal();
        }

        case BAR_MOD_MEDIA: {
            MediaState st = get_media_state();
            if (!st.valid) return dynconfig.theme.text_muted;
            if (st.playing) return color_media_playing();
            if (st.paused) return color_media_paused();
            return dynconfig.theme.text_muted;
        }

        case BAR_MOD_VOLUME: {
            VolumeState st = get_volume_state();
            if (!st.valid || st.muted) return color_volume_muted();
            return color_module_fg();
        }

        case BAR_MOD_MEMORY:
            return dynconfig.theme.text;

        case BAR_MOD_WEATHER:
            return dynconfig.theme.accent_soft;

        case BAR_MOD_CLOCK:
            return color_clock();

        case BAR_MOD_TITLE:
        case BAR_MOD_STATUS:
        case BAR_MOD_CUSTOM:
        default:
            return color_module_fg();
    }
}

static const char *sync_label(void) {
    return wm.config.sync_workspaces ? "sync" : "local";
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
    bool icons = bar_use_icons();

    switch (mod->kind) {
        case BAR_MOD_MONITOR:
            if (icons) {
                snprintf(buf, buflen, "%sM%d", (m == wm.selmon) ? "󰍹 " : "󰍺 ", m->id + 1);
            } else {
                snprintf(buf, buflen, "M%d", m->id + 1);
            }
            break;

        case BAR_MOD_SYNC:
            if (icons) {
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

        case BAR_MOD_CLOCK: {
            char timebuf[128];
            build_clock_text(mod->arg[0] ? mod->arg : "%a %b %d • %H:%M", timebuf, sizeof(timebuf));
            snprintf(buf, buflen, icons ? "󰃭 %s" : "%s", timebuf);
            break;
        }

        case BAR_MOD_CUSTOM:
            read_custom_command(mod->arg, buf, buflen);
            break;

        case BAR_MOD_VOLUME: {
            VolumeState st = get_volume_state();
            if (!st.valid) {
                snprintf(buf, buflen, icons ? "󰖁 audio" : "audio");
                break;
            }

            if (icons) {
                const char *icon = volume_icon_for_state(st);
                if (st.muted) snprintf(buf, buflen, "%s muted", icon);
                else snprintf(buf, buflen, "%s %d%%", icon, st.percent);
            } else {
                if (st.muted) snprintf(buf, buflen, "vol muted");
                else snprintf(buf, buflen, "vol %d%%", st.percent);
            }
            break;
        }

        case BAR_MOD_NETWORK: {
            NetworkState st = get_network_state();

            if (!st.valid || !st.connected) {
                snprintf(buf, buflen, icons ? "󰤭 offline" : "offline");
                break;
            }

            if (st.wireless) {
                if (st.signal_percent >= 0) {
                    snprintf(buf, buflen, icons ? "󰤨 wifi %d%%" : "wifi %d%%", st.signal_percent);
                } else {
                    snprintf(buf, buflen, icons ? "󰤨 wifi" : "wifi");
                }
            } else {
                snprintf(buf, buflen, icons ? "󰈀 wired" : "wired");
            }
            break;
        }

        case BAR_MOD_BATTERY: {
            BatteryState st = get_battery_state();

            if (!st.valid) {
                snprintf(buf, buflen, icons ? "󰂑 battery" : "battery");
                break;
            }

            if (!icons) {
                if (st.full) snprintf(buf, buflen, "bat full");
                else snprintf(buf, buflen, "bat %d%%", st.percent);
                break;
            }

            const char *icon = "󰁹";
            if (st.full) icon = "󰁹";
            else if (st.charging) icon = "󰂄";
            else if (st.percent >= 90) icon = "󰂂";
            else if (st.percent >= 70) icon = "󰂀";
            else if (st.percent >= 50) icon = "󰁾";
            else if (st.percent >= 30) icon = "󰁼";
            else if (st.percent >= 10) icon = "󰁺";
            else icon = "󰂎";

            if (st.full) snprintf(buf, buflen, "%s full", icon);
            else snprintf(buf, buflen, "%s %d%%", icon, st.percent);
            break;
        }

        case BAR_MOD_BRIGHTNESS: {
            BrightnessState st = get_brightness_state();

            if (!st.valid) {
                snprintf(buf, buflen, icons ? "󰃞 brightness" : "brightness");
                break;
            }

            if (icons) {
                const char *icon = (st.percent >= 60) ? "󰃠" : ((st.percent >= 25) ? "󰃟" : "󰃞");
                snprintf(buf, buflen, "%s %d%%", icon, st.percent);
            } else {
                snprintf(buf, buflen, "bri %d%%", st.percent);
            }
            break;
        }

        case BAR_MOD_MEDIA: {
            MediaState st = get_media_state();

            if (!st.valid) {
                snprintf(buf, buflen, icons ? "󰎈 Idle" : "Idle");
                break;
            }

            if (icons) {
                if (st.playing) snprintf(buf, buflen, "󰐊 %s", st.text);
                else if (st.paused) snprintf(buf, buflen, "󰏤 %s", st.text);
                else snprintf(buf, buflen, "󰎈 Idle");
            } else {
                snprintf(buf, buflen, "%s", st.text);
            }
            break;
        }

        case BAR_MOD_MEMORY: {
            MemoryState st = get_memory_state();
            if (!st.valid) {
                snprintf(buf, buflen, icons ? "󰍛 memory" : "mem");
            } else {
                snprintf(buf, buflen, icons ? "󰍛 %d%%" : "mem %d%%", st.used_percent);
            }
            break;
        }

        case BAR_MOD_WEATHER: {
            WeatherState st = get_weather_state();
            if (!st.valid || st.text[0] == '\0') {
                buf[0] = '\0';
            } else {
                snprintf(buf, buflen, "%s", st.text);
            }
            break;
        }

        default:
            break;
    }
}

static int module_width_px_local(Monitor *m, BarModule *mod) {
    int content_w = 0;

    if (!m || !mod) {
        return 0;
    }

    if (mod->kind == BAR_MOD_WORKSPACES) {
        if (!bar_use_icons()) {
            content_w = 0;
            for (int i = 0; i < WORKSPACE_COUNT; i++) {
                char label[16];
                snprintf(label, sizeof(label), "%d", i + 1);
                content_w += text_width_px(label);
                if (i + 1 < WORKSPACE_COUNT) {
                    content_w += 12;
                }
            }
        } else {
            int step = wm.font_char_width + 4;
            int glyph_w = text_width_px("●");
            if (glyph_w <= 0) {
                glyph_w = wm.font_char_width;
            }
            content_w = ((WORKSPACE_COUNT - 1) * step) + glyph_w;
        }
    } else {
        char buf[512];
        build_module_text(m, mod, buf, sizeof(buf));
        content_w = text_width_px(buf);

        if (mod->kind == BAR_MOD_VOLUME && dynconfig.bar_style.volume_bar_enabled) {
            content_w += 8 + MAX(0, dynconfig.bar_style.volume_bar_width);
        }
    }

    if (content_w <= 0) {
        return 0;
    }

    if (bar_uses_pills()) {
        content_w += module_padding_x() * 2;
    }

    return content_w;
}

int bar_module_width_px(Monitor *m, BarModule *mod) {
    return module_width_px_local(m, mod);
}

static void draw_module_background(cairo_t *cr, int x, int y, int w, int h) {
    if (!bar_uses_pills() || w <= 0 || h <= 0) {
        return;
    }

    const int border_w = 1;
    const double outer_x = (double)x;
    const double outer_y = (double)y;
    const double outer_w = (double)w;
    const double outer_h = (double)h;
    const double outer_r = (double)MAX(0, dynconfig.bar_style.radius);

    cairo_save(cr);

    cairo_round_rect_local(cr, outer_x, outer_y, outer_w, outer_h, outer_r);
    cairo_set_source_rgb_u32_local(cr, color_module_bg());
    cairo_fill(cr);

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

            cairo_round_rect_local(cr, stroke_x, stroke_y, stroke_w, stroke_h, stroke_r);
            cairo_set_source_rgb_u32_local(cr, color_module_border());
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

    cairo_round_rect_local(cr, (double)x, (double)y, (double)w, (double)h, (double)MAX(0, dynconfig.bar_style.volume_bar_radius));
    cairo_set_source_rgb_u32_local(cr, color_volume_bar_bg());
    cairo_fill(cr);

    int pct = st.valid ? st.percent : 0;
    if (st.muted) pct = 0;
    pct = CLAMP(pct, 0, 100);

    int fill_w = (w * pct) / 100;
    if (fill_w > 0) {
        cairo_round_rect_local(cr, (double)x, (double)y, (double)fill_w, (double)h, (double)MAX(0, dynconfig.bar_style.volume_bar_radius));
        cairo_set_source_rgb_u32_local(cr, volume_fill_color(st));
        cairo_fill(cr);
    }

    cairo_restore(cr);
}

void bar_draw_module(
    Monitor *m,
    BarModule *mod,
    cairo_t *cr,
    XftDraw *xftdraw,
    int x,
    int baseline,
    int bar_h,
    int width
) {
    if (!m || !mod || !cr || width <= 0) {
        return;
    }

    int pad_x = module_padding_x();

    int box_y = 0;
    int box_h = bar_h;
    int local_baseline = baseline;

    if (bar_uses_pills()) {
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

        if (dynconfig.bar_style.volume_bar_enabled) {
            bar_w = MAX(0, dynconfig.bar_style.volume_bar_width);
            bar_h_px = MAX(1, dynconfig.bar_style.volume_bar_height);
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

        if (dynconfig.bar_style.volume_bar_enabled && bar_w > 0) {
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

        if (mod->kind == BAR_MOD_TITLE && bar_use_colors() && m != wm.selmon) {
            fg = dynconfig.theme.text_muted;
        }

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

void ensure_default_bar_modules(void) {
    if (dynconfig.bar_left_count == 0 &&
        dynconfig.bar_center_count == 0 &&
        dynconfig.bar_right_count == 0) {

        dynconfig.bar_left[0].kind = BAR_MOD_MONITOR;
        dynconfig.bar_left[1].kind = BAR_MOD_WORKSPACES;
        dynconfig.bar_left_count = 2;

        dynconfig.bar_center[0].kind = BAR_MOD_TITLE;
        dynconfig.bar_center_count = 1;

        dynconfig.bar_right[0].kind = BAR_MOD_NETWORK;
        dynconfig.bar_right[1].kind = BAR_MOD_MEMORY;
        dynconfig.bar_right[2].kind = BAR_MOD_WEATHER;
        dynconfig.bar_right[3].kind = BAR_MOD_VOLUME;
        dynconfig.bar_right[4].kind = BAR_MOD_CLOCK;
        snprintf(dynconfig.bar_right[4].arg, sizeof(dynconfig.bar_right[4].arg), "%s", "%a %d %b  %H:%M");
        dynconfig.bar_right_count = 5;
    }
}
