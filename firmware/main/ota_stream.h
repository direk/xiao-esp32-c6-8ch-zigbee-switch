// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t header[6], header_used;
    uint32_t capacity, expected, received;
    bool failed;
} ota_stream_t;
typedef bool (*ota_stream_writer_t)(void *context, const uint8_t *data, size_t length);
void ota_stream_init(ota_stream_t *stream, uint32_t capacity);
bool ota_stream_feed(ota_stream_t *stream, const uint8_t *data, size_t length,
                     ota_stream_writer_t writer, void *context);
bool ota_stream_complete(const ota_stream_t *stream);
bool ota_version_matches(const char *version, uint32_t file_version);
