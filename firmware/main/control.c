#include "control.h"

void input_init(input_t *s, bool active, uint64_t now) {
    s->raw = s->stable = active;
    s->changed_ms = now;
}

int input_sample(input_t *s, bool active, uint8_t mode, uint64_t now) {
    if (active != s->raw) {
        s->raw = active;
        s->changed_ms = now;
    }
    if (s->stable == s->raw || now - s->changed_ms < DEBOUNCE_MS) return ACTION_NONE;
    s->stable = s->raw;
    if (mode == INPUT_ROCKER) return s->stable ? ACTION_ON : ACTION_OFF;
    return s->stable ? ACTION_TOGGLE : ACTION_NONE;
}

bool channel_uses_binding(bool bound) {
    return bound;
}

uint8_t binding_mask_add(uint8_t mask, uint8_t endpoint, uint16_t cluster,
                         uint8_t address_mode, bool local_source, bool excluded_target) {
    if (local_source && !excluded_target && cluster == 0x0006 &&
        endpoint >= 1 && endpoint <= 4 && (address_mode == 1 || address_mode == 3)) {
        mask |= (uint8_t)(1U << (endpoint - 1));
    }
    return mask;
}

int binding_scan_page(binding_scan_t *scan, uint8_t index, uint8_t total,
                      uint8_t count, uint8_t page_mask) {
    if (index != scan->next || total > 32 || index + count > total ||
        (index && total != scan->total) || (!count && index < total)) return -1;
    scan->total = total;
    scan->next = index + count;
    scan->mask |= page_mask;
    return scan->next == total ? 1 : 0;
}
