#include "system_status.h"

#include "util.h"

#include <dirent.h>
#include <time.h>

typedef struct {
    VolumeState volume;
    NetworkState network;
    BatteryState battery;
    BrightnessState brightness;
    MediaState media;
    MemoryState memory;
    WeatherState weather;

    long last_volume_ms;
    long last_network_ms;
    long last_battery_ms;
    long last_brightness_ms;
    long last_media_ms;
    long last_memory_ms;
    long last_weather_ms;
} StatusCache;

static StatusCache g_status = {0};

static long now_ms_monotonic(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

static void trim_trailing_whitespace_local(char *s) {
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

static bool path_exists_local(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

static bool read_first_line_file_local(const char *path, char *buf, size_t buflen) {
    if (!path || !buf || buflen == 0) {
        return false;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        return false;
    }

    bool ok = false;
    if (fgets(buf, (int)buflen, fp)) {
        trim_trailing_whitespace_local(buf);
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
    st->percent = CLAMP(st->percent, 0, 150);
    return true;
}

const char *volume_icon_for_state(VolumeState st) {
    if (!st.valid) return "󰖁";
    if (st.muted || st.percent == 0) return "󰝟";
    if (st.percent < 35) return "󰕿";
    if (st.percent < 70) return "󰖀";
    return "󰕾";
}

static void update_volume_state(void) {
    VolumeState st = {0};
    char line[256] = {0};

    FILE *fp = popen(
        "env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin "
        "sh -c '/usr/bin/wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null || wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null'",
        "r"
    );

    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            trim_trailing_whitespace_local(line);
            if (parse_wpctl_volume_line(line, &st)) {
                st.valid = true;
            }
        }
        pclose(fp);
    }

    if (!st.valid) {
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
                        st.percent = CLAMP(st.percent, 0, 150);
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
    }

    g_status.volume = st;
}

static bool is_wireless_interface_name(const char *ifname) {
    if (!ifname || !*ifname) {
        return false;
    }

    return strncmp(ifname, "wl", 2) == 0 || strncmp(ifname, "wlan", 4) == 0;
}

static int wireless_signal_percent_from_proc(const char *ifname) {
    if (!ifname || !*ifname) {
        return -1;
    }

    FILE *fp = fopen("/proc/net/wireless", "r");
    if (!fp) {
        return -1;
    }

    char line[512];
    int result = -1;

    while (fgets(line, sizeof(line), fp)) {
        char name[64] = {0};
        float link = 0.0f;

        if (sscanf(line, " %63[^:]: %*d %f", name, &link) == 2) {
            if (strcmp(name, ifname) == 0) {
                int pct = (int)((link / 70.0f) * 100.0f + 0.5f);
                result = CLAMP(pct, 0, 100);
                break;
            }
        }
    }

    fclose(fp);
    return result;
}

static void update_network_state(void) {
    NetworkState st = {0};
    st.signal_percent = -1;

    DIR *dir = opendir("/sys/class/net");
    if (!dir) {
        g_status.network = st;
        return;
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

        bool have_oper = read_first_line_file_local(oper_path, state, sizeof(state));
        bool have_carrier = read_first_line_file_local(carrier_path, carrier, sizeof(carrier));

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
        st.wireless = path_exists_local(wireless_path) || is_wireless_interface_name(ent->d_name);
        snprintf(st.ifname, sizeof(st.ifname), "%.63s", ent->d_name);

        if (st.wireless) {
            st.signal_percent = wireless_signal_percent_from_proc(ent->d_name);
        }

        break;
    }

    closedir(dir);
    g_status.network = st;
}

static void update_battery_state(void) {
    BatteryState st = {0};

    DIR *dir = opendir("/sys/class/power_supply");
    if (!dir) {
        g_status.battery = st;
        return;
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
        if (!read_first_line_file_local(type_path, typebuf, sizeof(typebuf))) {
            continue;
        }

        if (strcmp(typebuf, "Battery") != 0) {
            continue;
        }

        snprintf(cap_path, sizeof(cap_path), "/sys/class/power_supply/%s/capacity", ent->d_name);
        if (!read_first_line_file_local(cap_path, capbuf, sizeof(capbuf))) {
            continue;
        }

        st.valid = true;
        st.percent = atoi(capbuf);
        st.percent = CLAMP(st.percent, 0, 100);

        snprintf(status_path, sizeof(status_path), "/sys/class/power_supply/%s/status", ent->d_name);
        if (read_first_line_file_local(status_path, statbuf, sizeof(statbuf))) {
            st.charging = (strcmp(statbuf, "Charging") == 0);
            st.full = (strcmp(statbuf, "Full") == 0);
        }

        break;
    }

    closedir(dir);
    g_status.battery = st;
}

static void update_brightness_state(void) {
    BrightnessState st = {0};

    DIR *dir = opendir("/sys/class/backlight");
    if (!dir) {
        g_status.brightness = st;
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }

        char cur_path[512];
        char max_path[512];
        char cur_buf[64] = {0};
        char max_buf[64] = {0};

        snprintf(cur_path, sizeof(cur_path), "/sys/class/backlight/%s/brightness", ent->d_name);
        snprintf(max_path, sizeof(max_path), "/sys/class/backlight/%s/max_brightness", ent->d_name);

        if (!read_first_line_file_local(cur_path, cur_buf, sizeof(cur_buf))) {
            continue;
        }
        if (!read_first_line_file_local(max_path, max_buf, sizeof(max_buf))) {
            continue;
        }

        long cur = atol(cur_buf);
        long max = atol(max_buf);
        if (max > 0) {
            st.percent = (int)((cur * 100L) / max);
            st.percent = CLAMP(st.percent, 0, 100);
            st.valid = true;
            break;
        }
    }

    closedir(dir);
    g_status.brightness = st;
}

static void update_media_state(void) {
    MediaState st = {0};
    char status[64] = {0};
    char meta[256] = {0};

    FILE *fp = popen(
        "env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin "
        "sh -c '/usr/bin/playerctl status 2>/dev/null || playerctl status 2>/dev/null'",
        "r"
    );
    if (fp) {
        if (fgets(status, sizeof(status), fp)) {
            trim_trailing_whitespace_local(status);
        }
        pclose(fp);
    }

    fp = popen(
        "env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin "
        "sh -c '/usr/bin/playerctl metadata --format \"{{artist}} - {{title}}\" 2>/dev/null || playerctl metadata --format \"{{artist}} - {{title}}\" 2>/dev/null'",
        "r"
    );
    if (fp) {
        if (fgets(meta, sizeof(meta), fp)) {
            trim_trailing_whitespace_local(meta);
        }
        pclose(fp);
    }

    if (status[0] == '\0' && meta[0] == '\0') {
        g_status.media = st;
        return;
    }

    st.valid = true;
    st.playing = (strcmp(status, "Playing") == 0);
    st.paused = (strcmp(status, "Paused") == 0);

    if (meta[0] != '\0') {
        snprintf(st.text, sizeof(st.text), "%s", meta);
    } else if (st.playing) {
        snprintf(st.text, sizeof(st.text), "Now playing");
    } else if (st.paused) {
        snprintf(st.text, sizeof(st.text), "Paused");
    } else {
        snprintf(st.text, sizeof(st.text), "Idle");
    }

    g_status.media = st;
}

static void update_memory_state(void) {
    MemoryState st = {0};
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        g_status.memory = st;
        return;
    }

    long total_kb = 0;
    long avail_kb = 0;
    char line[256];

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "MemTotal: %ld kB", &total_kb) == 1) {
            continue;
        }
        if (sscanf(line, "MemAvailable: %ld kB", &avail_kb) == 1) {
            continue;
        }
    }

    fclose(fp);

    if (total_kb > 0 && avail_kb >= 0) {
        long used_kb = total_kb - avail_kb;
        st.valid = true;
        st.total_mb = total_kb / 1024;
        st.used_mb = used_kb / 1024;
        st.used_percent = (int)((used_kb * 100L) / total_kb);
        st.used_percent = CLAMP(st.used_percent, 0, 100);
    }

    g_status.memory = st;
}

static void update_weather_state(void) {
    WeatherState st = {0};
    char buf[128] = {0};

    const char *cmd = getenv("VWM_WEATHER_CMD");
    if (!cmd || !*cmd) {
        g_status.weather = st;
        return;
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        g_status.weather = st;
        return;
    }

    if (fgets(buf, sizeof(buf), fp)) {
        trim_trailing_whitespace_local(buf);
    }
    pclose(fp);

    if (buf[0] != '\0') {
        st.valid = true;
        snprintf(st.text, sizeof(st.text), "%s", buf);
    }

    g_status.weather = st;
}

void refresh_system_status(bool force) {
    long now = now_ms_monotonic();

    if (force || now - g_status.last_volume_ms >= 500) {
        g_status.last_volume_ms = now;
        update_volume_state();
    }

    if (force || now - g_status.last_network_ms >= 2000) {
        g_status.last_network_ms = now;
        update_network_state();
    }

    if (force || now - g_status.last_battery_ms >= 5000) {
        g_status.last_battery_ms = now;
        update_battery_state();
    }

    if (force || now - g_status.last_brightness_ms >= 1500) {
        g_status.last_brightness_ms = now;
        update_brightness_state();
    }

    if (force || now - g_status.last_media_ms >= 2000) {
        g_status.last_media_ms = now;
        update_media_state();
    }

    if (force || now - g_status.last_memory_ms >= 1000) {
        g_status.last_memory_ms = now;
        update_memory_state();
    }

    if (force || now - g_status.last_weather_ms >= (15 * 60 * 1000)) {
        g_status.last_weather_ms = now;
        update_weather_state();
    }
}

VolumeState get_volume_state(void) { return g_status.volume; }
NetworkState get_network_state(void) { return g_status.network; }
BatteryState get_battery_state(void) { return g_status.battery; }
BrightnessState get_brightness_state(void) { return g_status.brightness; }
MediaState get_media_state(void) { return g_status.media; }
MemoryState get_memory_state(void) { return g_status.memory; }
WeatherState get_weather_state(void) { return g_status.weather; }
