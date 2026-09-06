//-----------------------------------------------------------------------------
// disk.c — MLI READ_BLOCK streaming supporting dual 140KB floppy drives (D1/D2)
// and 800KB ProDOS HDV images transparently.
//
// Dual 140KB Floppy Layout:
//   - Drive 1 (Disk 1): PRODOS, BASIC.SYSTEM, STARTUP, MAIN.BIN, ART (block 128)
//   - Drive 2 (Disk 2): PCM Audio (block 7), MAIN4.BIN
//
// 800KB HDV Layout:
//   - Boot Drive: PCM Audio (block 200), ART (block 900)
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

// Dedicated 512-byte streaming buffer at $0800 (Text Page 2, 100% free)
static uint8_t * const diskBuf = (uint8_t *)0x0800;
static uint16_t cached_abs_block = 0xFFFF;
static uint8_t  cached_unit = 0xFF;

static uint8_t boot_unit = 0;
static uint8_t drive1_unit = 0;
static uint8_t drive2_unit = 0;
static uint8_t is_floppy = 0;

static uint16_t art_base_block = 900;
static uint8_t  art_unit = 0;
static uint16_t pcm_base_block = 200;
static uint8_t  pcm_unit = 0;

static void prompt_disk2(void) {
    const char *m1 = "PLEASE INSERT DISK 2 IN DRIVE 2";
    const char *m2 = "PRESS ANY KEY TO CONTINUE...";
    volatile uint8_t *t10 = (volatile uint8_t *)0x0428;
    volatile uint8_t *t12 = (volatile uint8_t *)0x0528;

    for (uint8_t i = 0; m1[i]; i++) t10[i + 4] = (uint8_t)(m1[i] | 0x80);
    for (uint8_t i = 0; m2[i]; i++) t12[i + 6] = (uint8_t)(m2[i] | 0x80);

    // Clear strobe, wait for key, clear strobe
    *(volatile uint8_t *)0xC010 = 0;
    while ((*(volatile uint8_t *)0xC000) < 128) {}
    *(volatile uint8_t *)0xC010 = 0;

    for (uint8_t i = 0; i < 40; i++) {
        t10[i] = 0xA0;
        t12[i] = 0xA0;
    }
}

static void mli_read_unit(uint8_t unit, uint16_t abs_block, uint8_t *dest) {
    mli_unit = unit;
    mli_buf_lo = (uint8_t)((uint32_t)(unsigned long)dest);
    mli_buf_hi = (uint8_t)(((uint32_t)(unsigned long)dest) >> 8);
    mli_blk_lo = (uint8_t)abs_block;
    mli_blk_hi = (uint8_t)(abs_block >> 8);
    mlib_read_block();

    // If reading from Drive 2 in floppy mode fails, prompt and retry
    while (mli_status != 0 && unit == drive2_unit && is_floppy) {
        prompt_disk2();
        mli_unit = unit;
        mli_buf_lo = (uint8_t)((uint32_t)(unsigned long)dest);
        mli_buf_hi = (uint8_t)(((uint32_t)(unsigned long)dest) >> 8);
        mli_blk_lo = (uint8_t)abs_block;
        mli_blk_hi = (uint8_t)(abs_block >> 8);
        mlib_read_block();
    }
}

void disk_init(void) {
    boot_unit = *(volatile uint8_t *)0xBF30;

    // Read Key Block of Volume Directory (block 2) on the boot drive to inspect volume size
    mli_read_unit(boot_unit, 2, diskBuf);
    uint16_t total_blocks = (uint16_t)diskBuf[0x29] | ((uint16_t)diskBuf[0x2A] << 8);

    if (total_blocks <= 280) {
        // Dual 140KB Floppy Mode (Disk 1 in Drive 1, Disk 2 in Drive 2 of same slot)
        is_floppy = 1;
        drive1_unit = boot_unit & 0x7F;
        drive2_unit = boot_unit | 0x80;
        art_unit = drive1_unit;
        art_base_block = 128;
        pcm_unit = drive2_unit;
        pcm_base_block = 7;
    } else {
        // HDV / Hard Drive Mode: art & pcm live on the boot volume (Drive 1, Drive 2, or any drive n)
        is_floppy = 0;
        drive1_unit = boot_unit;
        drive2_unit = boot_unit;
        art_unit = boot_unit;
        art_base_block = 900;
        pcm_unit = boot_unit;
        pcm_base_block = 200;
    }

    cached_abs_block = 0xFFFF;
    cached_unit = 0xFF;
}

uint8_t *disk_ensure(uint32_t base_block, uint32_t total, uint32_t offset) {
    (void)total;
    uint16_t block_idx = (uint16_t)(offset / BLOCK_BYTES);
    uint8_t unit;
    uint16_t abs_block;

    if (base_block == 200) {        // PCM Audio stream
        unit = pcm_unit;
        abs_block = pcm_base_block + block_idx;
    } else if (base_block == 900) {  // Sprite / pattern art stream
        unit = art_unit;
        abs_block = art_base_block + block_idx;
    } else {
        unit = boot_unit;
        abs_block = (uint16_t)base_block + block_idx;
    }

    if (abs_block != cached_abs_block || unit != cached_unit) {
        mli_read_unit(unit, abs_block, diskBuf);
        cached_abs_block = abs_block;
        cached_unit = unit;
    }
    return &diskBuf[offset & (BLOCK_BYTES - 1)];
}
