#ifndef VWM_SYSTEM_STATUS_H
#define VWM_SYSTEM_STATUS_H

#include "vwm.h"

typedef struct {
    bool valid;
    bool muted;
    int percent;
} VolumeState;

typedef struct {
    bool valid;
    bool connected;
    bool wireless;
    int signal_percent;
    char ifname[64];
    char ssid[128];
} NetworkState;

typedef struct {
    bool valid;
    bool charging;
    bool full;
    int percent;
} BatteryState;

typedef struct {
    bool valid;
    int percent;
} BrightnessState;

typedef struct {
    bool valid;
    bool playing;
    bool paused;
    char text[256];
} MediaState;

typedef struct {
    bool valid;
    int used_percent;
    long used_mb;
    long total_mb;
} MemoryState;

typedef struct {
    bool valid;
    char text[128];
} WeatherState;

void refresh_system_status(bool force);

VolumeState get_volume_state(void);
const char *volume_icon_for_state(VolumeState st);

NetworkState get_network_state(void);
BatteryState get_battery_state(void);
BrightnessState get_brightness_state(void);
MediaState get_media_state(void);
MemoryState get_memory_state(void);
WeatherState get_weather_state(void);

#endif
