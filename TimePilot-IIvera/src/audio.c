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

void audioInit(void) {
    /* Mute all 16 PSG channels */
    for (uint8_t c = 0; c < 16; c++) {
        psg_silence(c);
    }
    for (uint8_t i = 0; i < 4; i++) {
        sfxTimer[i] = 0;
        sfxType[i] = 0;
    }
    activePlaying = -1;
}

void audioCleanup(void) {
    audioInit();
}

int8_t audioIsSourcePlaying(int8_t source) {
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
    case AUDIO_WAPON_EXPLODE:
        /* Snappy noise blast */
        sfxType[CH_EXPL] = 1;
        sfxTimer[CH_EXPL] = 14;
        sfxFreq[CH_EXPL] = 120;
        sfxVol[CH_EXPL] = 0x3F;
        psg_write(CH_EXPL, sfxFreq[CH_EXPL], 0xC0 | sfxVol[CH_EXPL], 0xC0); /* White Noise */
        break;

    case AUDIO_BIG_EXPLOSION:
        /* Deep rumbling boss/player explosion */
        sfxType[CH_EXPL] = 2;
        sfxTimer[CH_EXPL] = 30;
        sfxFreq[CH_EXPL] = 60;
        sfxVol[CH_EXPL] = 0x3F;
        psg_write(CH_EXPL, sfxFreq[CH_EXPL], 0xC0 | sfxVol[CH_EXPL], 0xC0); /* Heavy Noise */
        /* Accompany with sub-bass triangle */
        psg_write(CH_ENEMY, 80, 0xC0 | 0x38, 0x80); /* Triangle rumble */
        sfxType[CH_ENEMY] = 3;
        sfxTimer[CH_ENEMY] = 24;
        sfxVol[CH_ENEMY] = 0x38;
        break;

    case AUDIO_PICKUP:
    case AUDIO_COINDROP:
        /* Happy ascending arpeggio: C5 -> E5 -> G5 */
        sfxType[CH_PICKUP] = 1;
        sfxTimer[CH_PICKUP] = 15;
        sfxFreq[CH_PICKUP] = 860;  /* C5 */
        sfxVol[CH_PICKUP] = 0x3F;
        psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | sfxVol[CH_PICKUP], 0x20);
        break;

    case AUDIO_EXTRA_LIFE:
        /* Fanfare triple burst */
        sfxType[CH_PICKUP] = 2;
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

    case AUDIO_GAME_START:
    case AUDIO_NEXT_LEVEL:
        /* Classic stage fanfare start */
        sfxType[CH_PICKUP] = 4;
        sfxTimer[CH_PICKUP] = 24;
        sfxFreq[CH_PICKUP] = 650;
        sfxVol[CH_PICKUP] = 0x3F;
        psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | sfxVol[CH_PICKUP], 0x20);
        break;

    default:
        break;
    }
}

/* Service audio every frame (60Hz vsync hook) */
void audioServiceAudio(void) {
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
                /* Bass rumble decay */
                if (sfxVol[CH_ENEMY] >= 2) sfxVol[CH_ENEMY] -= 2;
                psg_write(CH_ENEMY, 70, 0xC0 | sfxVol[CH_ENEMY], 0x80);
            } else {
                if (sfxVol[CH_ENEMY] >= 8) sfxVol[CH_ENEMY] -= 8;
                psg_write(CH_ENEMY, sfxFreq[CH_ENEMY], 0xC0 | sfxVol[CH_ENEMY], 0x40);
            }
        }
    }

    /* Channel 2: Explosions (Noise decay) */
    if (sfxTimer[CH_EXPL]) {
        sfxTimer[CH_EXPL]--;
        if (sfxTimer[CH_EXPL] == 0) {
            psg_silence(CH_EXPL);
        } else {
            uint8_t step = (sfxType[CH_EXPL] == 2) ? 2 : 4;
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
            } else if (sfxType[CH_PICKUP] == 4) {
                /* Fanfare steps */
                if (sfxTimer[CH_PICKUP] == 16) {
                    sfxFreq[CH_PICKUP] = 860;
                    psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | 0x3F, 0x20);
                } else if (sfxTimer[CH_PICKUP] == 8) {
                    sfxFreq[CH_PICKUP] = 1289;
                    psg_write(CH_PICKUP, sfxFreq[CH_PICKUP], 0xC0 | 0x3F, 0x20);
                }
            }
        }
    }
}
