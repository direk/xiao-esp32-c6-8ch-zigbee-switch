#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHANNELS 8
#define BINDING_CAPACITY 32
#define DEBOUNCE_MS 35
#define GESTURE_MS 500
#define CL_ONOFF 0x0006
#define CL_LEVEL 0x0008
#define CL_COLOR 0x0300
#define CL_SCENES 0x0005

enum { INPUT_PUSH, INPUT_ROCKER };
enum {
    MODE_TOGGLE, MODE_MOMENTARY, MODE_MULTIFUNCTION,
    MODE_LEVEL, MODE_LEVEL_UP, MODE_LEVEL_DOWN,
    MODE_COLOR, MODE_COLOR_UP, MODE_COLOR_DOWN, MODE_SCENE,
    MODE_LEVEL_NO_ONOFF, MODE_LEVEL_UP_NO_ONOFF, MODE_LEVEL_DOWN_NO_ONOFF,
    MODE_COUNT
};
enum {
    ACT_OFF, ACT_ON, ACT_TOGGLE, ACT_SINGLE, ACT_DOUBLE, ACT_TRIPLE,
    ACT_HOLD, ACT_RELEASE, ACT_LEVEL_UP, ACT_LEVEL_DOWN,
    ACT_LEVEL_STEP_UP, ACT_LEVEL_STEP_DOWN, ACT_LEVEL_STOP,
    ACT_COLOR_UP, ACT_COLOR_DOWN, ACT_COLOR_STEP_UP, ACT_COLOR_STEP_DOWN,
    ACT_COLOR_STOP, ACT_SCENE, ACT_COUNT
};

typedef struct {
    uint8_t input_mode, invert, function, switch_action, min_level, max_level;
    uint16_t transition_time, color_min, color_max, scene_group;
    uint8_t scene_id;
} channel_config_t;

typedef struct {
    uint16_t cluster, group;
    uint8_t id, data[12], length, action;
    bool report_only;
} command_t;

typedef struct {
    bool raw, stable;
    uint64_t changed_ms, pressed_ms;
    uint8_t clicks;
    bool holding, direction_down, armed;
    uint16_t held_cluster;
    bool held_no_onoff;
} button_t;

void channel_defaults(channel_config_t *cfg);
bool channel_valid(const channel_config_t *cfg);
void button_init(button_t *s, bool active, uint64_t now);
// Produces at most one command per poll; holding is stopped explicitly on release.
bool button_poll(button_t *s, bool active, const channel_config_t *cfg, uint64_t now, command_t *out);
bool button_cancel(button_t *s, command_t *out);
// Complete ZCL frame (including header) for direct APS transmission; 0 for report-only.
size_t command_frame(const command_t *cmd, uint8_t sequence, uint8_t *out, size_t capacity);
int binding_cluster_index(uint16_t cluster);
uint8_t binding_mask_add(uint8_t mask, uint8_t endpoint, uint16_t cluster,
                         uint8_t address_mode, bool local_source, bool excluded_target);
typedef struct { uint8_t next, total, mask; } binding_scan_t;
int binding_scan_page(binding_scan_t *scan, uint8_t index, uint8_t total, uint8_t count, uint8_t page_mask);

// Routing is independent of gesture generation and never disables direct delivery.
bool command_direct(const command_t *cmd, bool bound);
bool command_reports(const command_t *cmd, bool bound, bool binding_priority);
