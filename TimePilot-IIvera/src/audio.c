//-----------------------------------------------------------------------------
// audio.c
// Time Pilot IIvera — Pure Hardware VERA 16-Channel PSG Arcade Sound Engine
//
// 100% Zero-Disk, Zero-PCM, Zero-Flicker!
// Direct register writes to VERA PSG Channels at VRAM Bank 1 $1F9C0..$1F9FF.
// Authentic 1982 Konami arcade Chiptune sound effects:
// - Channel 0: Player Laser Fire (downward pulse sweep)
// - Channel 1: Enemy Fire / Bomb Whistle (descending saw/pulse)
// - Channel 2: Enemy & Boss Explosions (dynamic white noise decay)
// - Channel 3: Parachute Rescue & Extra Life (cheerful rising arpeggio)
//-----------------------------------------------------------------------------
#include <stdint.h>
#include "apple2e.h"
#include "audio.h"
#include "audio_table.h"

#define PSG_BASE 0xF9C0   /* in VRAM Bank 1: $1F9C0 */

/* Channel assignments */
#define CH_PLAYER  0
#define CH_ENEMY   1
#define CH_EXPL    2
#define CH_PICKUP  3

/* PSG Voice States */
static uint8_t  sfxType[4]   = { 0, 0, 0, 0 };
static uint8_t  sfxTimer[4]  = { 0, 0, 0, 0 };
static uint16_t sfxFreq[4]   = { 0, 0, 0, 0 };
static uint8_t  sfxVol[4]    = { 0, 0, 0, 0 };

static int8_t   activePlaying = -1;

/* Low-level PSG register write to VRAM Bank 1 $1F9C0 + ch*4 */
static void psg_write(uint8_t ch, uint16_t freq, uint8_t pan_vol, uint8_t wave_pw) {
    uint16_t addr = PSG_BASE + (uint16_t)ch * 4;
    VERA.address_hi = VERA_INC_BANK1;
    VERA.address = addr;
    VERA.data0 = (uint8_t)(freq & 0xFF);
    VERA.data0 = (uint8_t)((freq >> 8) & 0x3F);
    VERA.data0 = pan_vol;
    VERA.data0 = wave_pw;
}

static void psg_silence(uint8_t ch) {
    uint16_t addr = PSG_BASE + (uint16_t)ch * 4 + 2;
    VERA.address_hi = VERA_INC_BANK1;
    VERA.address = addr;
    VERA.data0 = 0x00;  /* Volume = 0 */
}

#define VERA_PCM_CTRL_REG (*(volatile uint8_t *)(VERA_BASE + 0x1B))
#define VERA_PCM_RATE_REG (*(volatile uint8_t *)(VERA_BASE + 0x1C))
#define VERA_PCM_DATA_REG (*(volatile uint8_t *)(VERA_BASE + 0x1D))

static uint16_t pcmStartAddr = 0;
static uint16_t pcmTotalLen  = 0;
static uint8_t  pcmActive    = 0;
static uint16_t pcmOffset    = 0;

static void pcm_play(uint16_t vram_addr, uint16_t length) {
    if (length == 0) return;
    pcmActive = 1;
    pcmStartAddr = vram_addr;
    pcmTotalLen = length;
    pcmOffset = 0;
    VERA_PCM_RATE_REG = 0;
    VERA_PCM_CTRL_REG = 0x80; /* Reset FIFO */

    /* Pre-buffer up to 2048 bytes (or full length if shorter) directly into FIFO */
    uint16_t pre = (length > 2048) ? 2048 : length;
    vera_set_addr(VERA_INC_BANK0, vram_addr);
    for (uint16_t i = 0; i < pre; i++) {
        VERA_PCM_DATA_REG = VERA.data0;
    }
    pcmOffset = pre;

    /* Start playback: 8-bit Signed Mono, Volume 15 (Max), Rate 17 (~6485 Hz) */
    VERA_PCM_CTRL_REG = 0x0F; /* Mono, 8-bit, Vol 15 */
    VERA_PCM_RATE_REG = VERA_PCM_RATE;
}

void audioInit(void) {
    /* Mute all 16 PSG channels */
    for (uint8_t c = 0; c < 16; c++) {
        psg_silence(c);
    }
    for (uint8_t i = 0; i < 4; i++) {
        sfxTimer[i] = 0;
        sfxType[i] = 0;
    }
    pcmActive = 0;
    pcmOffset = 0;
    pcmTotalLen = 0;
    VERA_PCM_RATE_REG = 0;
    VERA_PCM_CTRL_REG = 0x80; /* Reset FIFO */
    VERA_PCM_CTRL_REG = 0x00; /* Mute PCM */
    activePlaying = -1;
}

void audioCleanup(void) {
    audioInit();
}

int8_t audioIsSourcePlaying(int8_t source) {
    if (source == AUDIO_GAME_START || source == AUDIO_BIG_EXPLOSION) return pcmActive;
    return (activePlaying == source);
}

void audioStopSource(int8_t source) {
    if (source < 0 || activePlaying == source || source == AUDIO_GAME_START || source == AUDIO_BIG_EXPLOSION) {
        audioInit();
    }
}

void audioPlaySource(int8_t source) {
    if (source < 0 || source >= NUM_AUDIO_SOURCES) return;
    activePlaying = source;

    switch (source) {
    case AUDIO_COINDROP:
        /* Crisp arcade coin drop metallic ping: ~1560Hz */
        sfxType[CH_PICKUP] = 2;
        sfxTimer[CH_PICKUP] = 24;
        sfxFreq[CH_PICKUP] = 1560;
        sfxVol[CH_PICKUP] = 0x3F;
        psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | sfxVol[CH_PICKUP], 0x10);
        break;

    case AUDIO_GAME_START:
        pcm_play(audioData[AUDIO_GAME_START].start, audioData[AUDIO_GAME_START].length);
        break;

    case AUDIO_NEXT_LEVEL:
        /* Stage clear victory fanfare: rising pulse chord */
        sfxType[CH_PICKUP] = 1;
        sfxTimer[CH_PICKUP] = 30;
        sfxFreq[CH_PICKUP] = 680;  /* A4 */
        sfxVol[CH_PICKUP] = 0x3F;
        psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | sfxVol[CH_PICKUP], 0x20);

        sfxType[CH_PLAYER] = 1;
        sfxTimer[CH_PLAYER] = 30;
        sfxFreq[CH_PLAYER] = 860;  /* C5 */
        sfxVol[CH_PLAYER] = 0x3F;
        psg_write(CH_PLAYER, sfxFreq[CH_PLAYER], 0xC0 | sfxVol[CH_PLAYER], 0x20);
        break;

    case AUDIO_PLAYER_SHOOT:
        /* Crisp descending pulse laser: ~1400 down to 700 */
        sfxType[CH_PLAYER] = 1;
        sfxTimer[CH_PLAYER] = 8;
        sfxFreq[CH_PLAYER] = 1500;
        sfxVol[CH_PLAYER] = 0x3F;
        psg_write(CH_PLAYER, sfxFreq[CH_PLAYER], 0xC0 | sfxVol[CH_PLAYER], 0x20); /* Pulse 50% */
        break;

    case AUDIO_ENEMY_SHOOT:
        /* Quick enemy chirp */
        sfxType[CH_ENEMY] = 1;
        sfxTimer[CH_ENEMY] = 6;
        sfxFreq[CH_ENEMY] = 900;
        sfxVol[CH_ENEMY] = 0x38;
        psg_write(CH_ENEMY, sfxFreq[CH_ENEMY], 0xC0 | sfxVol[CH_ENEMY], 0x40); /* Sawtooth */
        break;

    case AUDIO_ENEMY_EXPLODE:
        /* Enemy explosion: rapid descending pulse sweep from high pitch down */
        sfxType[CH_EXPL] = 1;   /* descending sweep */
        sfxTimer[CH_EXPL] = 18;
        sfxFreq[CH_EXPL] = 3000;
        sfxVol[CH_EXPL] = 0x3F;
        psg_write(CH_EXPL, sfxFreq[CH_EXPL], 0xC0 | sfxVol[CH_EXPL], 0xC0); /* Noise burst */
        break;

    case AUDIO_WAPON_EXPLODE:
        /* Weapon impact: heavier descending pulse sweep */
        sfxType[CH_EXPL] = 1;
        sfxTimer[CH_EXPL] = 24;
        sfxFreq[CH_EXPL] = 2000;
        sfxVol[CH_EXPL] = 0x3F;
        psg_write(CH_EXPL, sfxFreq[CH_EXPL], 0xC0 | sfxVol[CH_EXPL], 0xC0); /* Noise burst */
        break;

    case AUDIO_BIG_EXPLOSION:
        /* Authentic Arcade PCM Big Explosion for player crash, boss, and bomber! */
        psg_silence(CH_EXPL);
        psg_silence(CH_ENEMY);
        pcm_play(audioData[AUDIO_BIG_EXPLOSION].start, audioData[AUDIO_BIG_EXPLOSION].length);
        break;

    case AUDIO_PICKUP:
        /* Happy ascending arpeggio: C5 -> E5 -> G5 */
        sfxType[CH_PICKUP] = 1;
        sfxTimer[CH_PICKUP] = 15;
        sfxFreq[CH_PICKUP] = 860;  /* C5 */
        sfxVol[CH_PICKUP] = 0x3F;
        psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | sfxVol[CH_PICKUP], 0x20);
        break;

    case AUDIO_EXTRA_LIFE:
        /* Fanfare triple burst */
        sfxType[CH_PICKUP] = 4;
        sfxTimer[CH_PICKUP] = 20;
        sfxFreq[CH_PICKUP] = 1084; /* E5 */
        sfxVol[CH_PICKUP] = 0x3F;
        psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | sfxVol[CH_PICKUP], 0x20);
        break;

    case AUDIO_BOMB:
        /* Whistling descending bomb */
        sfxType[CH_ENEMY] = 2;
        sfxTimer[CH_ENEMY] = 20;
        sfxFreq[CH_ENEMY] = 2200;
        sfxVol[CH_ENEMY] = 0x3A;
        psg_write(CH_ENEMY, sfxFreq[CH_ENEMY], 0xC0 | sfxVol[CH_ENEMY], 0x20);
        break;

    case AUDIO_BOSSL0:
    case AUDIO_BOSSL1:
    case AUDIO_BOSSL2:
    case AUDIO_BOSSL3:
        /* Boss heartbeat pulse */
        sfxType[CH_PICKUP] = 3;
        sfxTimer[CH_PICKUP] = 10;
        sfxFreq[CH_PICKUP] = 180;
        sfxVol[CH_PICKUP] = 0x30;
        psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | sfxVol[CH_PICKUP], 0x80); /* Triangle */
        break;

    case AUDIO_WAVE_START:
        /* Stage clear fanfare: rising pulse chord */
        sfxType[CH_PICKUP] = 1;
        sfxTimer[CH_PICKUP] = 20;
        sfxFreq[CH_PICKUP] = 680;  /* A4 */
        sfxVol[CH_PICKUP] = 0x3F;
        psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | sfxVol[CH_PICKUP], 0x20);
        /* Add a harmony tone on CH_PLAYER */
        sfxType[CH_PLAYER] = 0;
        sfxTimer[CH_PLAYER] = 20;
        sfxFreq[CH_PLAYER] = 1020; /* E5 - major third up */
        sfxVol[CH_PLAYER] = 0x38;
        psg_write(CH_PLAYER, sfxFreq[CH_PLAYER], 0xC0 | sfxVol[CH_PLAYER], 0x20);
        break;

    case AUDIO_TIMEWARP:
        /* Time warp ascending pitch sweep + space whoosh */
        sfxType[CH_PICKUP] = 5;
        sfxTimer[CH_PICKUP] = 80;
        sfxFreq[CH_PICKUP] = 250;
        sfxVol[CH_PICKUP] = 0x3F;
        psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | sfxVol[CH_PICKUP], 0x20);

        sfxType[CH_EXPL] = 1;
        sfxTimer[CH_EXPL] = 60;
        sfxFreq[CH_EXPL] = 1000;
        sfxVol[CH_EXPL] = 0x38;
        psg_write(CH_EXPL, sfxFreq[CH_EXPL], 0xC0 | sfxVol[CH_EXPL], 0xC0);
        break;

    default:
        break;
    }
}

/* Service audio every frame (60Hz vsync hook) */
void audioServiceAudio(void) {
    if (pcmActive) {
        if (pcmOffset < pcmTotalLen) {
            uint16_t target = pcmOffset + 140;
            if (target > pcmTotalLen) target = pcmTotalLen;
            vera_set_addr(VERA_INC_BANK0, (uint16_t)(pcmStartAddr + pcmOffset));
            while (pcmOffset < target && !(VERA_PCM_CTRL_REG & 0x80)) {
                VERA_PCM_DATA_REG = VERA.data0;
                pcmOffset++;
            }
        } else if (VERA_PCM_CTRL_REG & 0x40) {
            /* FIFO completely drained — full audio track finished playing naturally */
            pcmActive = 0;
            VERA_PCM_RATE_REG = 0;
            VERA_PCM_CTRL_REG = 0x80;
            VERA_PCM_CTRL_REG = 0x00; /* Silence PCM */
            activePlaying = -1;
        }
    }
    /* Channel 0: Player Laser decay & frequency sweep */
    if (sfxTimer[CH_PLAYER]) {
        sfxTimer[CH_PLAYER]--;
        if (sfxTimer[CH_PLAYER] == 0) {
            psg_silence(CH_PLAYER);
        } else {
            if (sfxFreq[CH_PLAYER] > 120) sfxFreq[CH_PLAYER] -= 120;
            if (sfxVol[CH_PLAYER] >= 6)   sfxVol[CH_PLAYER] -= 6;
            psg_write(CH_PLAYER, sfxFreq[CH_PLAYER], 0xC0 | sfxVol[CH_PLAYER], 0x20);
        }
    }

    /* Channel 1: Enemy Fire / Bomb / Explosion rumble */
    if (sfxTimer[CH_ENEMY]) {
        sfxTimer[CH_ENEMY]--;
        if (sfxTimer[CH_ENEMY] == 0) {
            psg_silence(CH_ENEMY);
        } else {
            if (sfxType[CH_ENEMY] == 2) {
                /* Bomb falling pitch drop */
                if (sfxFreq[CH_ENEMY] > 100) sfxFreq[CH_ENEMY] -= 90;
                psg_write(CH_ENEMY, sfxFreq[CH_ENEMY], 0xC0 | sfxVol[CH_ENEMY], 0x20);
            } else if (sfxType[CH_ENEMY] == 3) {
                /* Big explosion bass rumble: descend from 1800 down to 100Hz */
                if (sfxFreq[CH_ENEMY] > 120) sfxFreq[CH_ENEMY] -= 36;
                if (sfxVol[CH_ENEMY] >= 1) sfxVol[CH_ENEMY] -= 1;
                psg_write(CH_ENEMY, sfxFreq[CH_ENEMY], 0xC0 | sfxVol[CH_ENEMY], 0x40);
            } else {
                if (sfxVol[CH_ENEMY] >= 8) sfxVol[CH_ENEMY] -= 8;
                psg_write(CH_ENEMY, sfxFreq[CH_ENEMY], 0xC0 | sfxVol[CH_ENEMY], 0x40);
            }
        }
    }

    /* Channel 2: Explosions — noise burst with freq+vol sweep */
    if (sfxTimer[CH_EXPL]) {
        sfxTimer[CH_EXPL]--;
        if (sfxTimer[CH_EXPL] == 0) {
            psg_silence(CH_EXPL);
        } else {
            /* Rapid descending freq sweep — gives classic arcade explosion 'bwoosh' */
            if (sfxFreq[CH_EXPL] > 120) sfxFreq[CH_EXPL] -= (sfxType[CH_EXPL] == 2) ? 80 : 120;
            uint8_t step = (sfxType[CH_EXPL] == 2) ? 1 : 3;
            if (sfxVol[CH_EXPL] >= step) sfxVol[CH_EXPL] -= step;
            psg_write(CH_EXPL, sfxFreq[CH_EXPL], 0xC0 | sfxVol[CH_EXPL], 0xC0);
        }
    }

    /* Channel 3: Rescue / Extra life / Fanfare arpeggios */
    if (sfxTimer[CH_PICKUP]) {
        sfxTimer[CH_PICKUP]--;
        if (sfxTimer[CH_PICKUP] == 0) {
            psg_silence(CH_PICKUP);
            activePlaying = -1;
        } else {
            if (sfxType[CH_PICKUP] == 1) {
                /* C5 -> E5 -> G5 step arpeggio */
                if (sfxTimer[CH_PICKUP] == 10) {
                    sfxFreq[CH_PICKUP] = 1084; /* E5 */
                    psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | sfxVol[CH_PICKUP], 0x20);
                } else if (sfxTimer[CH_PICKUP] == 5) {
                    sfxFreq[CH_PICKUP] = 1289; /* G5 */
                    psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | sfxVol[CH_PICKUP], 0x20);
                }
            } else if (sfxType[CH_PICKUP] == 2) {
                /* Coin drop metallic dual-tone ding: 1560Hz -> 920Hz */
                if (sfxTimer[CH_PICKUP] == 14) {
                    sfxFreq[CH_PICKUP] = 920;
                    psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | 0x3C, 0x10);
                }
                if (sfxVol[CH_PICKUP] >= 3) sfxVol[CH_PICKUP] -= 3;
                psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | sfxVol[CH_PICKUP], 0x10);
            } else if (sfxType[CH_PICKUP] == 4) {
                /* Fanfare steps */
                if (sfxTimer[CH_PICKUP] == 16) {
                    sfxFreq[CH_PICKUP] = 860;
                    psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | 0x3F, 0x20);
                } else if (sfxTimer[CH_PICKUP] == 8) {
                    sfxFreq[CH_PICKUP] = 1289;
                    psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | 0x3F, 0x20);
                }
            } else if (sfxType[CH_PICKUP] == 5) {
                /* Time warp ascending pitch sweep: 250Hz -> 2500Hz */
                if (sfxFreq[CH_PICKUP] < 2500) sfxFreq[CH_PICKUP] += 30;
                psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | sfxVol[CH_PICKUP], 0x20);
            }
        }
    }
}
