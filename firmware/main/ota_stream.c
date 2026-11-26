// SPDX-License-Identifier: MIT
#include "ota_stream.h"

void ota_stream_init(ota_stream_t *s, uint32_t capacity) {
    *s = (ota_stream_t){.capacity=capacity};
}
bool ota_stream_feed(ota_stream_t *s, const uint8_t *data, size_t length,
                     ota_stream_writer_t writer, void *context) {
    if (s->failed) return false;
    if ((!data && length) || !writer) { s->failed=true; return false; }
    while (length && s->header_used < sizeof(s->header)) {
        s->header[s->header_used++] = *data++; --length;
        if (s->header_used == sizeof(s->header)) {
            // One standard Upgrade Image subelement, little-endian tag and size.
            s->expected = (uint32_t)s->header[2] | ((uint32_t)s->header[3]<<8) |
                          ((uint32_t)s->header[4]<<16) | ((uint32_t)s->header[5]<<24);
            if (s->header[0] || s->header[1] || !s->expected || s->expected > s->capacity) {
                s->failed=true; return false;
            }
        }
    }
    if (length) {
        if (length > s->expected-s->received || !writer(context, data, length)) {
            s->failed=true; return false;
        }
        s->received += length;
    }
    return true;
}
bool ota_stream_complete(const ota_stream_t *s) {
    return !s->failed && s->header_used == 6 && s->expected && s->received == s->expected;
}
bool ota_version_matches(const char *text, uint32_t file_version) {
    if (!text || (file_version & 255)) return false;
    unsigned values[3] = {0};
    for (unsigned i=0; i<3; ++i) {
        if (*text < '0' || *text > '9') return false;
        do {
            values[i] = values[i]*10 + (unsigned)(*text++-'0');
            if (values[i] > 255) return false;
        } while (*text >= '0' && *text <= '9');
        if (i<2 && *text++ != '.') return false;
    }
    return !*text && file_version == ((uint32_t)values[0]<<24 | (uint32_t)values[1]<<16 | values[2]<<8);
}
