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
#define CH_PLAYER2 4   /* Layered unison voice for player laser: doubles acoustic power (+6dB) */
#define CH_EXPL2   5   /* Layered bass rumble voice for explosions */

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

static uint8_t  pcmBank      = 0;
static uint16_t pcmStartAddr = 0;
static uint16_t pcmTotalLen  = 0;
static uint8_t  pcmActive    = 0;
static uint16_t pcmOffset    = 0;
static uint8_t  pcmLoops     = 0;

static void pcm_play(uint8_t bank, uint16_t vram_addr, uint16_t length, uint8_t loops, uint8_t vol) {
    if (length == 0) return;
    pcmActive = 1;
    pcmBank = bank;
    pcmStartAddr = vram_addr;
    pcmTotalLen = length;
    pcmOffset = 0;
    pcmLoops = loops;
    VERA_PCM_RATE_REG = 0;
    VERA_PCM_CTRL_REG = 0x80; /* Reset FIFO */

    /* Pre-buffer up to 384 bytes (or full length if shorter) directly into FIFO.
     * 384 bytes provides ~3.6 frames of audio cushion at 6.5kHz (~108 B/frame),
     * taking only ~5.7k cycles (33% of 1 frame) with zero frame drops during gameplay. */
    uint16_t pre = (length > 384) ? 384 : length;
    vera_set_addr(bank ? VERA_INC_BANK1 : VERA_INC_BANK0, vram_addr);
    for (uint16_t i = 0; i < pre; i++) {
        VERA_PCM_DATA_REG = VERA.data0;
    }
    pcmOffset = pre;

    /* Start playback: 8-bit Signed Mono, Volume (0..15), Rate */
    VERA_PCM_CTRL_REG = (vol & 0x0F);
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
    if (source >= 0 && source < NUM_AUDIO_SOURCES && audioData[source].length > 0) {
        return pcmActive && (activePlaying == source);
    }
    return (activePlaying == source);
}

void audioStopSource(int8_t source) {
    if (source < 0 || activePlaying == source) {
        audioInit();
    }
}

void audioPlaySource(int8_t source) {
    if (source < 0 || source >= NUM_AUDIO_SOURCES) return;
    activePlaying = source;

    switch (source) {
    case AUDIO_COINDROP:
        pcm_play(audioData[AUDIO_COINDROP].bank, audioData[AUDIO_COINDROP].start, audioData[AUDIO_COINDROP].length, audioData[AUDIO_COINDROP].loops, 11);
        break;

    case AUDIO_GAME_START:
        pcm_play(audioData[AUDIO_GAME_START].bank, audioData[AUDIO_GAME_START].start, audioData[AUDIO_GAME_START].length, audioData[AUDIO_GAME_START].loops, 11);
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
        /* Crisp descending pulse laser: dual channels (0 + 4) for doubled acoustic power (+6dB) */
        sfxType[CH_PLAYER] = 1;
        sfxTimer[CH_PLAYER] = 12;
        sfxFreq[CH_PLAYER] = 1600;
        sfxVol[CH_PLAYER] = 0x3F;
        psg_write(CH_PLAYER, 1600, 0xC0 | 0x3F, 0x20);  /* Channel 0: Pulse 50% */
        psg_write(CH_PLAYER2, 1550, 0xC0 | 0x3F, 0x10); /* Channel 4: Pulse 25% unison */
        break;

    case AUDIO_ENEMY_SHOOT:
        /* Authentic Arcade enemy bullet shoot PCM */
        pcm_play(audioData[AUDIO_ENEMY_SHOOT].bank, audioData[AUDIO_ENEMY_SHOOT].start, audioData[AUDIO_ENEMY_SHOOT].length, audioData[AUDIO_ENEMY_SHOOT].loops, 9);
        break;

    case AUDIO_ENEMY_EXPLODE:
        /* Enemy explosion: dual channels (2 + 5) for noise burst + heavy bass punch */
        sfxType[CH_EXPL] = 1;
        sfxTimer[CH_EXPL] = 22;
        sfxFreq[CH_EXPL] = 2800;
        sfxVol[CH_EXPL] = 0x3F;
        psg_write(CH_EXPL, 2800, 0xC0 | 0x3F, 0xC0);  /* Channel 2: White noise */
        psg_write(CH_EXPL2, 200, 0xC0 | 0x3F, 0x40);  /* Channel 5: Low-frequency rumble */
        break;

    case AUDIO_WAPON_EXPLODE:
        /* Authentic Arcade weapon/missile explode PCM */
        pcm_play(audioData[AUDIO_WAPON_EXPLODE].bank, audioData[AUDIO_WAPON_EXPLODE].start, audioData[AUDIO_WAPON_EXPLODE].length, audioData[AUDIO_WAPON_EXPLODE].loops, 11);
        break;

    case AUDIO_BIG_EXPLOSION:
        /* Authentic Arcade PCM Big Explosion for player crash, boss, and bomber! */
        psg_silence(CH_EXPL);
        psg_silence(CH_EXPL2);
        psg_silence(CH_ENEMY);
        pcm_play(audioData[AUDIO_BIG_EXPLOSION].bank, audioData[AUDIO_BIG_EXPLOSION].start, audioData[AUDIO_BIG_EXPLOSION].length, audioData[AUDIO_BIG_EXPLOSION].loops, 13);
        break;

    case AUDIO_PICKUP:
        /* Happy ascending arpeggio: C5 -> E5 -> G5 */
        sfxType[CH_PICKUP] = 1;
        sfxTimer[CH_PICKUP] = 18;
        sfxFreq[CH_PICKUP] = 860;  /* C5 */
        sfxVol[CH_PICKUP] = 0x3F;
        psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | sfxVol[CH_PICKUP], 0x20);
        break;

    case AUDIO_EXTRA_LIFE:
        /* Fanfare triple burst */
        sfxType[CH_PICKUP] = 4;
        sfxTimer[CH_PICKUP] = 24;
        sfxFreq[CH_PICKUP] = 1084; /* E5 */
        sfxVol[CH_PICKUP] = 0x3F;
        psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | sfxVol[CH_PICKUP], 0x20);
        break;

    case AUDIO_BOMB:
        /* Authentic WWII Heavy Bomber falling bomb whistle PCM */
        pcm_play(audioData[AUDIO_BOMB].bank, audioData[AUDIO_BOMB].start, audioData[AUDIO_BOMB].length, audioData[AUDIO_BOMB].loops, 10);
        break;

    case AUDIO_ROCKET_LAUNCH:
        /* Authentic 1982 Supersonic guided missile rocket launch PCM */
        pcm_play(audioData[AUDIO_ROCKET_LAUNCH].bank, audioData[AUDIO_ROCKET_LAUNCH].start, audioData[AUDIO_ROCKET_LAUNCH].length, audioData[AUDIO_ROCKET_LAUNCH].loops, 10);
        break;

    case AUDIO_ROCKET_FLY:
        /* Authentic 1982 guided missile tracking engine buzz PCM */
        pcm_play(audioData[AUDIO_ROCKET_FLY].bank, audioData[AUDIO_ROCKET_FLY].start, audioData[AUDIO_ROCKET_FLY].length, audioData[AUDIO_ROCKET_FLY].loops, 7);
        break;

    case AUDIO_BOSSL0:
    case AUDIO_BOSSL1:
    case AUDIO_BOSSL2:
    case AUDIO_BOSSL3:
        /* Authentic Arcade Boss Engine Roar / Air-raid siren PCM: ambient volume so it won't mask player fire */
        pcm_play(audioData[source].bank, audioData[source].start, audioData[source].length, audioData[source].loops, 7);
        break;

    case AUDIO_WAVE_START:
        /* Authentic 4-plane formation attack siren PCM */
        pcm_play(audioData[AUDIO_WAVE_START].bank, audioData[AUDIO_WAVE_START].start, audioData[AUDIO_WAVE_START].length, audioData[AUDIO_WAVE_START].loops, 10);
        break;

    case AUDIO_TIMEWARP:
        /* Authentic Arcade 1982 Time Warp hyperspace PCM */
        pcm_play(audioData[AUDIO_TIMEWARP].bank, audioData[AUDIO_TIMEWARP].start, audioData[AUDIO_TIMEWARP].length, audioData[AUDIO_TIMEWARP].loops, 13);
        break;

    default:
        break;
    }
}

/* Service audio every frame (60Hz vsync hook) */
void audioServiceAudio(void) {
    if (pcmActive) {
        if (pcmOffset < pcmTotalLen) {
            uint16_t target = pcmOffset + 192;
            if (target > pcmTotalLen) target = pcmTotalLen;
            vera_set_addr(pcmBank ? VERA_INC_BANK1 : VERA_INC_BANK0, (uint16_t)(pcmStartAddr + pcmOffset));
            while (pcmOffset < target && !(VERA_PCM_CTRL_REG & 0x80)) {
                VERA_PCM_DATA_REG = VERA.data0;
                pcmOffset++;
            }
        } else if (pcmLoops) {
            /* Looping sample (e.g. boss engines / rocket fly) */
            pcmOffset = 0;
        } else if (VERA_PCM_CTRL_REG & 0x40) {
            /* FIFO completely drained — full audio track finished playing naturally */
            pcmActive = 0;
            VERA_PCM_RATE_REG = 0;
            VERA_PCM_CTRL_REG = 0x80;
            VERA_PCM_CTRL_REG = 0x00; /* Silence PCM */
            activePlaying = -1;
        }
    }
    /* Channel 0 & 4: Player Laser decay & frequency sweep */
    if (sfxTimer[CH_PLAYER]) {
        sfxTimer[CH_PLAYER]--;
        if (sfxTimer[CH_PLAYER] == 0) {
            psg_silence(CH_PLAYER);
            psg_silence(CH_PLAYER2);
        } else {
            if (sfxFreq[CH_PLAYER] > 80) sfxFreq[CH_PLAYER] -= 80;
            /* Hold full volume during initial attack punch (first 4 frames) */
            if (sfxTimer[CH_PLAYER] < 8 && sfxVol[CH_PLAYER] >= 5) {
                sfxVol[CH_PLAYER] -= 5;
            }
            psg_write(CH_PLAYER, sfxFreq[CH_PLAYER], 0xC0 | sfxVol[CH_PLAYER], 0x20);
            psg_write(CH_PLAYER2, sfxFreq[CH_PLAYER] - 30, 0xC0 | sfxVol[CH_PLAYER], 0x10);
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
                if (sfxVol[CH_ENEMY] >= 5) sfxVol[CH_ENEMY] -= 5;
                psg_write(CH_ENEMY, sfxFreq[CH_ENEMY], 0xC0 | sfxVol[CH_ENEMY], 0x40);
            }
        }
    }

    /* Channel 2 & 5: Explosions — noise burst + low-frequency bass punch */
    if (sfxTimer[CH_EXPL]) {
        sfxTimer[CH_EXPL]--;
        if (sfxTimer[CH_EXPL] == 0) {
            psg_silence(CH_EXPL);
            psg_silence(CH_EXPL2);
        } else {
            if (sfxFreq[CH_EXPL] > 90) sfxFreq[CH_EXPL] -= 90;
            if (sfxTimer[CH_EXPL] < 16 && sfxVol[CH_EXPL] >= 2) {
                sfxVol[CH_EXPL] -= 2;
            }
            psg_write(CH_EXPL, sfxFreq[CH_EXPL], 0xC0 | sfxVol[CH_EXPL], 0xC0);
            uint16_t bassFreq = 50 + (uint16_t)sfxTimer[CH_EXPL] * 6;
            psg_write(CH_EXPL2, bassFreq, 0xC0 | sfxVol[CH_EXPL], 0x40);
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
