#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCENE_LIST_MAX 16
#define SCENE_LIST_BYTES 64 // ZCL string length byte + at most 63 characters.
#define CHANNELS 8
#define ENDPOINTS 24
#define BINDING_CAPACITY 96
#define DEBOUNCE_MS 35
#define GESTURE_MS 500
#define CL_ONOFF 0x0006
#define CL_LEVEL 0x0008
#define CL_COLOR 0x0300
#define CL_SCENES 0x0005

enum { INPUT_PUSH, INPUT_ROCKER };
enum { SOURCE_NONE, SOURCE_D0, SOURCE_D1, SOURCE_D2, SOURCE_D3, SOURCE_D4, SOURCE_D5, SOURCE_D8, SOURCE_D9 };
enum { GESTURE_LEGACY, GESTURE_SINGLE, GESTURE_DOUBLE, GESTURE_HOLD };
enum {
    MODE_TOGGLE, MODE_MOMENTARY, MODE_MULTIFUNCTION,
    MODE_LEVEL, MODE_LEVEL_UP, MODE_LEVEL_DOWN,
    MODE_COLOR, MODE_COLOR_UP, MODE_COLOR_DOWN, MODE_SCENE,
    MODE_LEVEL_NO_ONOFF, MODE_LEVEL_UP_NO_ONOFF, MODE_LEVEL_DOWN_NO_ONOFF,
    MODE_SCENE_CYCLE, MODE_COUNT
};
enum {
    ACT_OFF, ACT_ON, ACT_TOGGLE, ACT_SINGLE, ACT_DOUBLE, ACT_TRIPLE,
    ACT_HOLD, ACT_RELEASE, ACT_LEVEL_UP, ACT_LEVEL_DOWN,
    ACT_LEVEL_STEP_UP, ACT_LEVEL_STEP_DOWN, ACT_LEVEL_STOP,
    ACT_COLOR_UP, ACT_COLOR_DOWN, ACT_COLOR_STEP_UP, ACT_COLOR_STEP_DOWN,
    ACT_COLOR_STOP, ACT_SCENE, ACT_COUNT
};
// Configuration selector, not a new action/report ID (3 remains ACT_SINGLE).
enum { SWITCH_ACTION_ON_OFF = 3 };

typedef struct {
    uint8_t input_mode, invert, function, switch_action, min_level, max_level;
    uint16_t transition_time, color_min, color_max, scene_group;
    uint8_t scene_id;
} channel_config_v2_t;

typedef struct {
    uint8_t input_mode, invert, function, switch_action, min_level, max_level;
    uint16_t transition_time, color_min, color_max, scene_group;
    uint8_t scene_id, reserved;
    uint8_t scene_list[SCENE_LIST_BYTES]; // Length-prefixed canonical decimal IDs.
    uint8_t input_source, input_gesture;
} channel_config_t;
_Static_assert(sizeof(channel_config_v2_t)==16, "Legacy NVS channel layout changed");
_Static_assert(offsetof(channel_config_t,scene_list)==sizeof(channel_config_v2_t), "Legacy prefix changed");
// Validates a length-prefixed ZCL string; rejects duplicates and malformed IDs.
bool scene_list_parse(const uint8_t *text, uint8_t *ids, uint8_t *count);
uint8_t channel_legacy_store(const channel_config_t *channels, channel_config_v2_t *saved);
void channel_legacy_restore(channel_config_t *channels, const channel_config_v2_t *saved,
                           const uint8_t lists[CHANNELS][SCENE_LIST_BYTES], uint8_t cycle_mask);


typedef struct {
    uint16_t cluster, group;
    uint8_t id, data[12], length, action;
    bool report_only;
} command_t;

typedef struct {
    bool raw, stable;
    uint64_t changed_ms, pressed_ms;
    uint8_t clicks, scene_index;
    bool holding, direction_down, armed, on_off_next_off;
    uint16_t held_cluster;
    bool held_no_onoff;
} button_t;

void channel_defaults(channel_config_t *cfg);
bool channel_valid(const channel_config_t *cfg);
uint8_t channel_control_path(const channel_config_t *cfg, uint8_t binding_clusters);
void button_init(button_t *s, bool active, uint64_t now);
// Rebase the contact/gesture after configuration changes, retaining ON/OFF phase.
void button_rebase(button_t *s, bool active, uint64_t now);
// Keep config2 readable by old rollback firmware; the new selector uses an extra NVS mask.
uint8_t channel_actions_store(const channel_config_t *channels, channel_config_t *saved);
void channel_actions_restore(channel_config_t *channels, uint8_t mask);
// Produces at most one command per poll; holding is stopped explicitly on release.
bool button_poll(button_t *s, bool active, const channel_config_t *cfg, uint64_t now, command_t *out);
bool button_cancel(button_t *s, command_t *out);
// Complete ZCL frame (including header) for direct APS transmission; 0 for report-only.
size_t command_frame(const command_t *cmd, uint8_t sequence, uint8_t *out, size_t capacity);
int binding_cluster_index(uint16_t cluster);
uint32_t binding_mask_add(uint32_t mask, uint8_t endpoint, uint16_t cluster,
                         uint8_t address_mode, bool local_source, bool excluded_target);
typedef struct { uint8_t next, total; uint32_t mask; } binding_scan_t;
int binding_scan_page(binding_scan_t *scan, uint8_t index, uint8_t total, uint8_t count, uint32_t page_mask);

// Routing is independent of gesture generation and never disables direct delivery.
bool command_direct(const command_t *cmd, bool bound);
bool command_reports(const command_t *cmd, bool bound, bool binding_priority);
