//-----------------------------------------------------------------------------
// disk.h — MLI block streaming for the HDV (audio + art).
// Two independent 512-byte windows to prevent cross-eviction.
//-----------------------------------------------------------------------------
#pragma once
#include <stdint.h>

void disk_init(void);
// Art/sprite window — used by upload_pattern_stream and friends.
uint8_t *disk_ensure(uint32_t base_block, uint32_t total, uint32_t offset);
