// SPDX-License-Identifier: MIT
#include "device_status.h"
#include <limits.h>

uint16_t device_reset_count(uint16_t previous, bool stored) {
    return !stored ? 0 : previous==UINT16_MAX ? UINT16_MAX : previous+1;
}
void device_status_init(device_status_t *s, uint16_t resets, uint8_t reason) {
    *s=(device_status_t){.resets=resets,.reset_reason=reason};
}
int device_status_poll(device_status_t *s, uint64_t now, bool identify) {
    uint64_t seconds=now/1000;
    s->uptime=seconds>UINT32_MAX ? UINT32_MAX : (uint32_t)seconds;
    if(identify && !s->identifying)s->identify_started=now;
    s->identifying=identify;
    return !identify || ((now-s->identify_started)/250)%2 ? 1 : 0;
}
void device_status_tx_result(device_status_t *s, bool accepted) {
    if(!accepted && s->tx_request_errors<UINT32_MAX)++s->tx_request_errors;
}
