// SPDX-License-Identifier: MIT
#include "binding_table.h"
#include <string.h>

bool binding_page_decode(const uint8_t *data, size_t length, const uint8_t own[8],
                         const uint8_t *coordinator, binding_page_t *out) {
    if (!data || !own || !out || length < 5 || data[1] != 0) return false;
    binding_page_t page = {.tsn=data[0], .total=data[2], .index=data[3], .count=data[4]};
    if (page.total > BINDING_CAPACITY || page.index > page.total ||
        page.count > page.total - page.index || (!page.count && page.index < page.total)) return false;
    size_t offset = 5;
    for (unsigned i=0; i<page.count; ++i) {
        if (length-offset < 12) return false;
        const uint8_t *record = data+offset;
        uint8_t mode = record[11];
        // Group records contain a 16-bit address and no destination endpoint.
        // IEEE records contain an eight-byte address and an endpoint.
        size_t size = mode == 1 ? 14 : mode == 3 ? 21 : 0;
        if (!size || length-offset < size) return false;
        uint16_t cluster = record[9] | ((uint16_t)record[10] << 8);
        bool local = !memcmp(record, own, 8);
        bool excluded = mode == 3 && (!memcmp(record+12, own, 8) ||
                        (coordinator && !memcmp(record+12, coordinator, 8)));
        page.mask = binding_mask_add(page.mask, record[8], cluster, mode, local, excluded);
        int ci = binding_cluster_index(cluster);
        if (ci >= 0) page.clusters[ci] = binding_mask_add(page.clusters[ci], record[8], cluster, mode, local, excluded);
        offset += size;
    }
    if (offset != length) return false;
    *out = page;
    return true;
}
