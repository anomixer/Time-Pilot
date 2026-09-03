//-----------------------------------------------------------------------------
// disk.c — MLI READ_BLOCK streaming from the ProDOS HDV into RAM windows.
// TWO independent 512-byte windows: one for PCM audio, one for art/sprites.
// Keeping them separate prevents PCM reads from evicting the art cache and
// causing cloud sprites to flicker on every frame.
//-----------------------------------------------------------------------------
#include <stdint.h>
#include "disk.h"

// MLI interface (mli.s).
extern uint8_t mli_unit;
extern uint8_t mli_buf_lo, mli_buf_hi;
extern uint8_t mli_blk_lo, mli_blk_hi;
extern uint8_t mli_status;
extern void mlib_read_block(void);

#define BLOCK_BYTES 512



// --- Window 1: Art / sprite streaming buffer at $0800 (Text Page 2, 100% free) ---
static uint8_t * const artBuf = (uint8_t *)0x0800;
static uint32_t artWinStart = 0xFFFFFFFFUL;
static uint32_t artWinEnd   = 0;

static void mli_read(uint16_t abs_block, uint8_t *dest) {
    mli_buf_lo = (uint8_t)((uint32_t)(unsigned long)dest);
    mli_buf_hi = (uint8_t)(((uint32_t)(unsigned long)dest) >> 8);
    mli_blk_lo = (uint8_t)abs_block;
    mli_blk_hi = (uint8_t)(abs_block >> 8);
    mlib_read_block();
}

void disk_init(void) {
    mli_unit = *(volatile uint8_t *)0xBF30;  // ProDOS boot unit
    // buf pointers are set per-call in mli_read()
}



// Art window: dedicated for sprite/pattern streaming.
uint8_t *disk_ensure(uint32_t base_block, uint32_t total, uint32_t offset) {
    if (offset >= artWinStart && offset < artWinEnd) return &artBuf[offset - artWinStart];
    uint32_t block = offset / BLOCK_BYTES;
    mli_read((uint16_t)(base_block + block), artBuf);
    artWinStart = block * BLOCK_BYTES;
    artWinEnd   = artWinStart + BLOCK_BYTES;
    if (artWinEnd > total) artWinEnd = total;
    return &artBuf[offset - artWinStart];
}
