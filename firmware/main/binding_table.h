// SPDX-License-Identifier: MIT
#pragma once
#include "control.h"

typedef struct {
    uint8_t tsn, index, total, count, mask, clusters[4];
} binding_page_t;

// Decode a complete Mgmt_Bind_rsp ASDU (including TSN). Output is atomic.
bool binding_page_decode(const uint8_t *data, size_t length, const uint8_t own[8],
                         const uint8_t *coordinator, binding_page_t *out);
