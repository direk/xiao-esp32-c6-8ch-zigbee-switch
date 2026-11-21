#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { INPUT_PUSH = 0, INPUT_ROCKER = 1 };
enum { ACTION_NONE = -1, ACTION_OFF = 0, ACTION_ON = 1, ACTION_TOGGLE = 2 };
#define DEBOUNCE_MS 35

typedef struct {
    bool raw, stable;
    uint64_t changed_ms;
} input_t;

void input_init(input_t *s, bool active, uint64_t now);
int input_sample(input_t *s, bool active, uint8_t mode, uint64_t now);

bool channel_uses_binding(bool bound);
uint8_t binding_mask_add(uint8_t mask, uint8_t endpoint, uint16_t cluster,
                         uint8_t address_mode, bool local_source, bool excluded_target);
typedef struct {
    uint8_t next, total, mask;
} binding_scan_t;
// -1 malformed/inconsistent page, 0 more pages, 1 complete snapshot.
int binding_scan_page(binding_scan_t *scan, uint8_t index, uint8_t total,
                      uint8_t count, uint8_t page_mask);
