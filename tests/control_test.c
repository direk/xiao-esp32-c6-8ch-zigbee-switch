#include <assert.h>
#include <stdio.h>
#include "control.h"

int main(void) {
    // Routing depends exclusively on each channel's binding, never Z2M health.
    assert(!channel_uses_binding(false));
    assert(channel_uses_binding(true));
    uint8_t mask = 0;
    for (unsigned ep = 1; ep <= 4; ++ep) {
        mask = binding_mask_add(mask, ep, 6, ep % 2 ? 1 : 3, true, false);
        assert(mask == (1U << ep) - 1);
    }
    assert(binding_mask_add(0, 1, 6, 3, true, true) == 0); // Coordinator/self excluded.
    assert(binding_mask_add(0, 1, 6, 3, false, false) == 0); // Foreign source excluded.
    assert(binding_mask_add(0, 1, 8, 3, true, false) == 0); // Other cluster excluded.
    assert(binding_mask_add(0, 5, 6, 3, true, false) == 0);
    assert(binding_mask_add(0, 1, 6, 2, true, false) == 0); // Invalid binding address mode.
    for (unsigned ep = 1; ep <= 4; ++ep) {
        bool bound = (5 & (1U << (ep - 1))) != 0; // Only CH1 and CH3 bound.
        assert(channel_uses_binding(bound) == (ep == 1 || ep == 3));
    }
    binding_scan_t scan = {0};
    assert(binding_scan_page(&scan, 0, 0, 0, 0) == 1); // Empty table is a valid snapshot.
    scan = (binding_scan_t){0};
    assert(binding_scan_page(&scan, 0, 5, 2, 1) == 0);
    assert(binding_scan_page(&scan, 2, 5, 2, 4) == 0);
    assert(binding_scan_page(&scan, 4, 5, 1, 8) == 1);
    assert(scan.mask == 13);
    scan = (binding_scan_t){0};
    assert(binding_scan_page(&scan, 0, 3, 1, 1) == 0);
    assert(binding_scan_page(&scan, 1, 4, 1, 4) == -1); // Changed table: no partial commit.
    assert(scan.mask == 1);
    assert(binding_scan_page(&scan, 2, 3, 1, 4) == -1); // Skipped page.
    assert(binding_scan_page(&scan, 1, 3, 0, 0) == -1); // No-progress response.
    scan = (binding_scan_t){0};
    assert(binding_scan_page(&scan, 0, 33, 1, 1) == -1);
    input_t p;
    input_init(&p, false, 0);
    assert(input_sample(&p, true, INPUT_PUSH, 10) == ACTION_NONE);
    assert(input_sample(&p, false, INPUT_PUSH, 15) == ACTION_NONE);
    assert(input_sample(&p, true, INPUT_PUSH, 20) == ACTION_NONE);
    assert(input_sample(&p, true, INPUT_PUSH, 54) == ACTION_NONE);
    assert(input_sample(&p, true, INPUT_PUSH, 55) == ACTION_TOGGLE);
    assert(input_sample(&p, true, INPUT_PUSH, 5000) == ACTION_NONE); // No hold repeat.
    assert(input_sample(&p, false, INPUT_PUSH, 5010) == ACTION_NONE);
    assert(input_sample(&p, false, INPUT_PUSH, 5045) == ACTION_NONE); // No release toggle.
    assert(input_sample(&p, true, INPUT_PUSH, 5050) == ACTION_NONE);
    assert(input_sample(&p, true, INPUT_PUSH, 5085) == ACTION_TOGGLE);
    input_init(&p, true, 6000); // Rocker closed at boot/config change: no synthetic action.
    assert(input_sample(&p, true, INPUT_ROCKER, 6100) == ACTION_NONE);
    assert(input_sample(&p, false, INPUT_ROCKER, 6200) == ACTION_NONE);
    assert(input_sample(&p, false, INPUT_ROCKER, 6235) == ACTION_OFF);
    assert(input_sample(&p, true, INPUT_ROCKER, 6300) == ACTION_NONE);
    assert(input_sample(&p, true, INPUT_ROCKER, 6335) == ACTION_ON);
    input_t channels[4];
    for (int i = 0; i < 4; ++i) input_init(&channels[i], false, 0);
    for (int i = 0; i < 4; ++i) assert(input_sample(&channels[i], i == 2, INPUT_PUSH, 10) == ACTION_NONE);
    for (int i = 0; i < 4; ++i) assert(input_sample(&channels[i], i == 2, INPUT_PUSH, 45) == (i == 2 ? ACTION_TOGGLE : ACTION_NONE));
    puts("Control tests passed: per-channel routing, binding snapshots, debounce, push/rocker and isolation");
}
