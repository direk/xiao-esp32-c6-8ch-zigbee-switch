// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <limits.h>
#include "device_status.h"
int main(void) {
    assert(device_reset_count(500,false)==0);
    assert(device_reset_count(0,true)==1);
    assert(device_reset_count(UINT16_MAX-1,true)==UINT16_MAX);
    assert(device_reset_count(UINT16_MAX,true)==UINT16_MAX);
    device_status_t s;device_status_init(&s,42,9);
    assert(s.resets==42 && s.reset_reason==9 && !s.tx_request_errors);
    assert(device_status_poll(&s,0,false)==1);
    assert(device_status_poll(&s,1234,true)==0);
    assert(device_status_poll(&s,1483,true)==0);
    assert(device_status_poll(&s,1484,true)==1);
    assert(device_status_poll(&s,1734,true)==0);
    assert(device_status_poll(&s,1735,false)==1); // Stop does not wait for blink phase.
    assert(device_status_poll(&s,1736,true)==0); // A fresh request starts illuminated.
    assert(device_status_poll(&s,2987,false)==1 && s.uptime==2);
    assert(!s.identifying);
    device_status_tx_result(&s,true);assert(!s.tx_request_errors);
    device_status_tx_result(&s,false);assert(s.tx_request_errors==1);
    s.tx_request_errors=UINT32_MAX;device_status_tx_result(&s,false);assert(s.tx_request_errors==UINT32_MAX);
    device_status_poll(&s,((uint64_t)UINT32_MAX+100)*1000,false);assert(s.uptime==UINT32_MAX);
    device_status_init(&s,43,3);assert(!s.tx_request_errors && !s.uptime && s.resets==43);
    puts("PASS: active-low nonblocking Identify, restart persistence/saturation, uptime and request error counters");
}
