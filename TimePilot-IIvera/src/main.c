//-----------------------------------------------------------------------------
// main.c
// Time Pilot IIvera - 100% original CX16 artwork
// Apple IIe + VERA (VIDHD-style card).
//
//   - Player plane is FIXED at the screen center; rotate 32 directions.
//   - The world scrolls opposite the facing (flying-into-distance feel).
//   - Enemies home in from the edges and fire back; per-era enemy/boss art.
//   - Scrolling cloud layer; explosion animations; stage progression.
//
// Sprite artwork is extracted verbatim from the CX16 Time Pilot (art.h).
// Sprites read palette entries 16..31 (palette_offset=1), recolored per stage.
//
// Controls:  WASD / arrows / joystick snap the heading
//            Q / E toggle continuous CCW / CW spin (same turn rate)
//            SPACE / 1 = fire
// Build:  mos-apple2e-clang -Os -o build/main.bin src/main.c
//-----------------------------------------------------------------------------
#include <stdint.h>
#include "apple2e.h"
#include "art_table.h"
#include "audio.h"
#include "audio_table.h"
#include "disk.h"

/* Bank 1 layout: 0xF000 font, 0x8000.. sprite patterns, 0xFA00 palette, 0xFC00 sprites */
#define TILE_BASE      0xF000   // font tiles (128 glyphs, 1KB) bank 1
#define PALETTE_ADDR   0xFA00   // 256-color palette bank 1
#define SPRITE_ATTR    0xFC00   // sprite attribute table bank 1
#define LAYER0_MAP     0x0000   // layer 0 tilemap bank 0

/* Sprite patterns in bank 1: all 5 eras are permanently resident in VRAM! */
#define PAT_PLAYER     ((uint16_t)ART_PLAYER_FRAMES_OFF)       // 0x0000 (32 frames x 256 B = 8192 B)
#define PAT_EXPL       ((uint16_t)ART_EXPL_FRAMES_OFF)         // 0x8900 (4 frames x 256 B = 1024 B)
#define PAT_BULLET     ((uint16_t)ART_BULLET_FRAMES_OFF)       // 0x8D00 (8x8 = 64 B)
#define PAT_EBULLET    ((uint16_t)ART_EBULLET_FRAMES_OFF)      // 0x8D40 (8x8 = 64 B)
#define PAT_CLOUD0     ((uint16_t)ART_CLOUD0_FRAMES_OFF)       // 0x8D80 (16x16 = 256 B)
#define PAT_CLOUD1     ((uint16_t)ART_CLOUD1_FRAMES_OFF)       // 0x8E80 (32x16 = 512 B)
#define PAT_CLOUD2     ((uint16_t)ART_CLOUD2_FRAMES_OFF)       // 0x9080 (64x16 = 1024 B)
#define PAT_ASTRO0     ((uint16_t)ART_ASTRO0_FRAMES_OFF)       // 0x9480 (16x16 = 256 B)
#define PAT_ASTRO1     ((uint16_t)ART_ASTRO1_FRAMES_OFF)       // 0x9580 (16x16 = 256 B)
#define PAT_ASTRO2     ((uint16_t)ART_ASTRO2_FRAMES_OFF)       // 0x9680 (32x16 = 512 B)
#define PAT_PARACHUTE  ((uint16_t)ART_PARACHUTE_FRAMES_OFF)    // 0x9880 (4 frames x 256 B = 1024 B)
#define PAT_LOGO_TIME  ((uint16_t)ART_LOGO_TIME_FRAMES_OFF)    // 0x9C80 (64x16 = 1024 B)
#define PAT_LOGO_PILOT ((uint16_t)ART_LOGO_PILOT_FRAMES_OFF)   // 0xA080 (64x16 = 1024 B)
#define PAT_BOMBER     ((uint16_t)ART_L1BOMBER_FRAMES_OFF)     // 0xC680 (8 frames x 512 B = 4096 B)
#define PAT_EXPL32     ((uint16_t)ART_EXPL32X16_FRAMES_OFF)    // 0xD680 (4 frames x 512 B = 2048 B)
#define PAT_NUMBERS    ((uint16_t)ART_NUMBER_FRAMES_OFF)       // 0xDE80 (6 frames x 256 B = 1536 B)
#define PAT_STAGE_ICON ((uint16_t)ART_STAGE_FRAMES_OFF)        // 0xE480 (8x8 = 64 B)
#define PAT_PROG_ICON  0xE840                                  // 8 frames x 16x8 = 1024 B (0xE840..0xEC3F)

#define POPUP_1000     0
#define POPUP_2000     1
#define POPUP_3000     2
#define POPUP_4000     3
#define POPUP_5000     4
#define POPUP_1500     5

static const uint16_t patEnemyBase[5] = {
    (uint16_t)ART_ENEMY0_FRAMES_OFF,
    (uint16_t)ART_ENEMY1_FRAMES_OFF,
    (uint16_t)ART_ENEMY2_FRAMES_OFF,
    (uint16_t)ART_ENEMY3_FRAMES_OFF,
    (uint16_t)ART_ENEMY4_FRAMES_OFF
};

static const uint16_t patBossBase[5] = {
    (uint16_t)ART_BOSS0_FRAMES_OFF,
    (uint16_t)ART_BOSS1_FRAMES_OFF,
    (uint16_t)ART_BOSS2_FRAMES_OFF,
    (uint16_t)ART_BOSS3_FRAMES_OFF,
    (uint16_t)ART_BOSS4_FRAMES_OFF
};

#define PAT_ENEMY      (patEnemyBase[stage])
#define PAT_BOSS       (patBossBase[stage])
#define PAT_BOMB       ((uint16_t)ART_BOMB_FRAMES_OFF)
#define PAT_BOOMERANG  ((uint16_t)ART_BOOMERANG_FRAMES_OFF)
#define PAT_ROCKET     ((uint16_t)ART_ROCKET_FRAMES_OFF)
#define PAT_SBULLET    ((uint16_t)ART_SBULLET_FRAMES_OFF)

/* CX16 weapon kinds. One in-flight shot per plane (enemyShot[]); specials and
 * flyer spray also count against numTrackedMax (2, then 3). */
#define EB_BULLET  0
#define EB_SPACE   1
#define EB_BOMB    2
#define EB_ROCKET  3
#define EB_BOOMER  4
#define EB_KMASK   7
#define EB_TRACKED 0x40
#define EB_RIGHT   0x80          /* bomb thrown from the left, heading swings CW */
#define WEAPON_BORDER 32         /* CX16 4*8 */
#define SPR_BYTES_8    64u       /* 8x8 8bpp frame */
#define SPR_BYTES_16   256u      /* 16x16 8bpp frame */
/* CX16 layerWidth/Height. Drawn 8x8 (rocket 16x16); box is the PNG. */
static const uint8_t ebHitW[5] = { 2, 7, 6, 9, 6 };
static const uint8_t ebHitH[5] = { 2, 7, 3, 9, 6 };
static const uint8_t ebDims[5]  = { 0, 0, 0, 0x50, 0 };
/* CX16 layerHeight[LAYER_ENEMY] per era. Width stays 16. */
static const uint8_t enemyHitH[5] = { 16, 16, 9, 16, 8 };

#ifndef VERA_INC_1
#define VERA_INC_1     (((1 << 1) | 0) << 3)
#endif
#ifndef VERA_INC_0
#define VERA_INC_0     (((0 << 1) | 0) << 3)
#endif

#define VERA_INC_BANK1  (VERA_INC_1 | 1)       // INC_1 + bank 1
#define VERA_INC_BANK0  (VERA_INC_1)           // INC_1 + bank 0

/* Sprite pool (VERA supports 128).
 *
 * The slot index IS the draw order: among sprites sharing a Z-depth, a LOWER
 * slot draws in FRONT. Depth must therefore be encoded in the slot numbering.
 * This mirrors TimePilot-CX16's layerToThingTable, which allocates descending
 * slots from back to front:
 *
 *     CLOUDS0 63 (small, furthest back) ... ENEMY 47 ... PLAYER 7,
 *     CLOUDS2 6 (large clouds, in FRONT of the player), SCORES 4.
 *
 * So the large clouds deliberately pass over the player, while the small and
 * medium clouds sit behind the whole playfield.
 *
 * Every gameplay sprite must stay below SPR_LIFE_BASE: set_sprite() gives slots
 * >= SPR_LIFE_BASE the UI Z-depth (0x0C, in front of layer 1). Gameplay needs
 * exactly 36 sprites, which fills 0..35 precisely.
 */
#define SPR_POPUP        0       // floating score popup (CX16 LAYER_SCORES)
#define SPR_CLOUD2_BASE  1       // 1..2   large clouds - IN FRONT of the player
#define NUM_CLOUD2       2
#define SPR_PLAYER       3
#define SPR_BOSS         4
#define SPR_BOMBER       5       // 1940 bomber formation
#define SPR_PARACHUTE    6       // rescue pilot pickup
#define SPR_BULLET_BASE  7       // 7..13
#define NUM_BULLETS      7
#define SPR_ENEMY_BASE   14      // 14..21
#define NUM_ENEMIES      8
#define SPR_EBULLET_BASE 22      // 22..29
#define NUM_EBULLETS     8
#define SPR_CLOUD1_BASE  30      // 30..33 medium clouds - behind the playfield
#define NUM_CLOUD1       4
#define SPR_CLOUD0_BASE  34      // 34..35 small clouds - furthest back
#define NUM_CLOUD0       2
#define NUM_CLOUDS       8
#define SPR_LOGO_TIME    5       // Title screen reuse (bomber slot, idle at title)
#define SPR_LOGO_PILOT   6       // Title screen reuse (parachute slot, idle at title)
#define SPR_LIFE_BASE   36       // 36..39 (up to 4 reserve ships)
#define NUM_LIFE_SPR    4
#define SPR_STAGE_BASE  40       // 40..44 (up to 5 stage era craft icons, y = 128)
#define NUM_STAGE_SPR   5
#define SPR_PROG_BASE   45       // 45..50 (6 stage progress planes, y = 192)
#define NUM_PROG_SPR    6

/* Playfield: left 28 columns (224px) — CX16 PLAYFIELDW=28. The right 12 columns
 * (224..320px) hold the arcade status bar (CX16 LAYER_SCORES). Player pinned at
 * the playfield center (CX16 PLAYER_X=104, PLAYER_Y=112). All bounds below use these. */
#define PF_W            224       /* playfield width (px) */
#define PF_XMIN         8         /* left sprite bound */
#define PF_XMAX         216       /* rightmost 16px-sprite origin (224-8) */
#define PF_YMIN         8
#define PF_YMAX         232
#define PLAYER_X0       104
#define HUD_COL         28        /* status bar left column (x = 224) */
#define PLAYER_Y0       112
#define LIVES_MAX       3
#define SCORE_PER_KILL  100
#define ENEMIES_TO_BOSS 48        /* 48 kills to trigger Boss (matches CX16 ENEMIES_TO_KILL_TO_CLEAR) */
#define BOSS_HP         8          /* CX16 LEVELBOSS_HEALTH */

/* update_game() runs once every two vsyncs (30 Hz), so a duration the CX16
 * expresses in 60 Hz frames is half as many ticks here. CX16's frame counts had
 * been transplanted verbatim (540, 300, 180, 60, ...), which made every one of
 * these run for twice its intended wall-clock time. */
#define TICKS(f)        ((f) / 2)
#define T_PARACHUTE     TICKS(540)  /* CX16 PARACHUTE_TIMER            */
#define T_BOMBER        TICKS(300)  /* CX16 BOMBER_TIMER               */
#define T_WAVE          TICKS(640)  /* CX16 ENEMY_SPAWN_WAVE_TIMER     */
#define T_SPAWN         TICKS(32)   /* CX16 ENEMY_SPAWN_TIMER+1        */
#define T_PLAYER_DIED   TICKS(180)  /* CX16 PLAYER_DIED_TIMER          */
#define T_POPUP         TICKS(60)   /* CX16 gameAddBonus activeTimer   */
/* CX16 explosions run EXPLOSION_HOLD_TIMER (10) plus explode_*_hold_table:
 * 32x16 = 10+8+16+8+8 = 50 frames, 16x16 = 10+4+8+4+4 = 30 frames. Frames run
 * 3..0 (aiExplodeThing decrements activeFrame), never 0..3. */
#define T_BOOM32        25
#define T_BOOM16        16
#define T_HS_CYCLE      15          /* CX16 UI_COLORCYCLE_TIMER  (60/4) */
#define T_HS_ENTRY      136         /* CX16 HIGHSCORE_ENTRY_TIME (~34s) */
/* The AI thinks once per 8 frames in both games, so these transfer unscaled. */
#define T_STEADY_MIN    22          /* CX16 ENEMY_STEADY_MIN_TIME  (3*60)/8   */
#define T_WAVE_ACTIVE   11          /* CX16 ENEMY_WAVE_ACTIVE_TIMER (1.5*60)/8 */
#define T_WAVE_ENTRY    7           /* CX16 ENEMY_WAVE_ENTRY_TIMER  60/8      */
#define T_RECALL        TICKS(576)  /* CX16 ENEMY_RECALL_TIMER (32*18)        */
#define T_BOSS          TICKS(120)  /* CX16 LEVELBOSS_TIMER (2 s)             */
#define T_ANN_STAGE     300         /* CX16 STAGE_ANNOUNCE_TIMER 5s (vsync)   */
#define T_ANN_READY     180         /* CX16 PLAYER_ANNOUNCE_TIMER 3s (vsync)  */
#define NUM_STAGES      5          /* 1910 / 1940 / 1970 / 1982 / 2001 (CX16 order) */

/* 32-direction movement vectors (clockwise from up), magnitude ~2 px.
 * Cardinals are a single heading; the two neighbours keep a 1px cross
 * component so they do not share (dx,dy) with the cardinal. CX16's 8.8
 * tables already do that — without it, heading 7/15/23/31 move exactly
 * like 8/16/24/0, so an 8-frame sprite still shows NE/SE/SW/NW while
 * world-scroll cancels thrust and the plane hangs. */
static const int8_t velDx[32] = {0,0,1,1,1,2,2,2,2,2,2,2,1,1,1,1,0,0,-1,-1,-1,-2,-2,-2,-2,-2,-2,-2,-1,-1,-1,-1};
static const int8_t velDy[32] = {-2,-2,-2,-2,-1,-1,-1,-1,0,0,1,1,1,2,2,2,2,2,2,2,1,1,1,1,0,0,-1,-1,-1,-2,-2,-2};

/* 84.4% velocity scaling table for Stages 0..2 (CX16 parity: 1.00 / 1.19 = 84.0%)
 * Maps velDx/velDy values (-2, -1, 0, 1, 2) shifted by +2 (indices 0..4) into 8.8 fixed-point deltas.
 * Index 0 (v = -2): -2 +  80/256 = -432/256 = -1.6875 px (-2 * 84.375%)
 * Index 1 (v = -1): -1 +  40/256 = -216/256 = -0.84375 px (-1 * 84.375%)
 * Index 2 (v =  0):  0 +   0/256 =    0/256 =  0.00000 px ( 0 * 84.375%)
 * Index 3 (v = +1):  0 + 216/256 = +216/256 = +0.84375 px (+1 * 84.375%)
 * Index 4 (v = +2): +1 + 176/256 = +432/256 = +1.6875 px (+2 * 84.375%)
 */
static const int8_t  step84_whole[5] = { -2, -1, 0, 0, 1 };
static const uint8_t step84_frac[5]  = { 80, 40, 0, 216, 176 };

/* 32 perimeter launch coordinates in the direction of flight (matches CX16 data.c launchPosX/Y) */
static const int16_t launchX[32] = {
    104, 137, 169, 198, 224, 224, 224, 224,
    224, 224, 224, 224, 224, 198, 169, 137,
    104,  71,  39,  10, -16, -16, -16, -16,
    -16, -16, -16, -16, -16,  10,  39,  71
};
static const int16_t launchY[32] = {
    -16, -16, -16, -16, -16,  11,  43,  77,
    112, 147, 181, 213, 240, 240, 240, 240,
    240, 240, 240, 240, 240, 213, 181, 147,
    112,  77,  43,  11, -16, -16, -16, -16
};

/* CX16 data.c rays[32][32] — 16 px cells, angle 0 = RIGHT. Add 8 at lookup
 * for IIvera (0 = UP). Follow/fire use inset 0; rockets/boomerangs inset 3. */
static const int8_t rays[32][32] = {
    {4 ,4 ,5 ,5 ,6 ,7 ,7 ,8 ,9 ,9 ,10,11,11,12,12,12,13,13,13,13,13,14,14,14,2 ,2 ,3 ,3 ,3 ,3 ,3 ,4 },
    {4 ,4 ,4 ,5 ,6 ,6 ,7 ,8 ,9 ,10,10,11,12,12,12,13,13,13,13,14,14,14,14,14,2 ,2 ,2 ,2 ,3 ,3 ,3 ,3 },
    {3 ,4 ,4 ,5 ,5 ,6 ,7 ,8 ,9 ,10,11,11,12,12,13,13,13,14,14,14,14,14,14,14,2 ,2 ,2 ,2 ,2 ,2 ,3 ,3 },
    {3 ,3 ,3 ,4 ,5 ,6 ,7 ,8 ,9 ,10,11,12,13,13,13,14,14,14,14,14,14,15,15,15,1 ,1 ,2 ,2 ,2 ,2 ,2 ,2 },
    {2 ,2 ,3 ,3 ,4 ,5 ,6 ,8 ,10,11,12,13,13,14,14,14,14,15,15,15,15,15,15,15,1 ,1 ,1 ,1 ,1 ,1 ,2 ,2 },
    {1 ,2 ,2 ,2 ,3 ,4 ,6 ,8 ,10,12,13,14,14,14,15,15,15,15,15,15,15,15,15,15,1 ,1 ,1 ,1 ,1 ,1 ,1 ,1 },
    {1 ,1 ,1 ,1 ,2 ,2 ,4 ,8 ,12,14,14,15,15,15,15,15,15,15,16,16,16,16,16,16,0 ,0 ,0 ,0 ,0 ,1 ,1 ,1 },
    {0 ,0 ,0 ,0 ,0 ,0 ,0 ,0 ,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,0 ,0 ,0 ,0 ,0 ,0 ,0 ,0 },
    {31,31,31,31,30,30,28,24,20,18,18,17,17,17,17,17,17,17,16,16,16,16,16,16,0 ,0 ,0 ,0 ,0 ,31,31,31},
    {31,30,30,30,29,28,26,24,22,20,19,18,18,18,17,17,17,17,17,17,17,17,17,17,31,31,31,31,31,31,31,31},
    {30,30,29,29,28,27,26,24,22,21,20,19,19,18,18,18,18,17,17,17,17,17,17,17,31,31,31,31,31,31,30,30},
    {29,29,29,28,27,26,25,24,23,22,21,20,19,19,19,18,18,18,18,18,18,17,17,17,31,31,30,30,30,30,30,30},
    {29,28,28,27,27,26,25,24,23,22,21,21,20,20,19,19,19,18,18,18,18,18,18,18,30,30,30,30,30,30,29,29},
    {28,28,28,27,26,26,25,24,23,22,22,21,20,20,20,19,19,19,19,18,18,18,18,18,30,30,30,30,29,29,29,29},
    {28,28,27,27,26,25,25,24,23,23,22,21,21,20,20,20,19,19,19,19,19,18,18,18,30,30,29,29,29,29,29,28},
    {28,27,27,26,26,25,25,24,23,23,22,22,21,21,20,20,20,19,19,19,19,19,18,18,30,29,29,29,29,29,28,28},
    {27,27,27,26,26,25,25,24,23,23,22,22,21,21,21,20,20,20,19,19,19,19,19,19,29,29,29,29,29,28,28,28},
    {27,27,26,26,25,25,25,24,23,23,23,22,22,21,21,21,20,20,20,20,19,19,19,19,29,29,29,28,28,28,28,27},
    {27,27,26,26,25,25,24,24,24,23,23,22,22,21,21,21,21,20,20,20,20,19,19,19,29,29,28,28,28,28,27,27},
    {27,26,26,26,25,25,24,24,24,23,23,22,22,22,21,21,21,20,20,20,20,20,19,19,29,28,28,28,28,28,27,27},
    {27,26,26,26,25,25,24,24,24,23,23,22,22,22,21,21,21,21,20,20,20,20,20,19,28,28,28,28,28,27,27,27},
    {26,26,26,25,25,25,24,24,24,23,23,23,22,22,22,21,21,21,21,20,20,20,20,20,28,28,28,28,27,27,27,27},
    {26,26,26,25,25,25,24,24,24,23,23,23,22,22,22,22,21,21,21,21,20,20,20,20,28,28,28,27,27,27,27,26},
    {26,26,26,25,25,25,24,24,24,23,23,23,22,22,22,22,21,21,21,21,21,20,20,20,28,28,27,27,27,27,27,26},
    {6 ,6 ,6 ,7 ,7 ,7 ,8 ,8 ,8 ,9 ,9 ,9 ,10,10,10,10,11,11,11,11,12,12,12,12,4 ,4 ,4 ,5 ,5 ,5 ,5 ,6 },
    {6 ,6 ,6 ,7 ,7 ,7 ,8 ,8 ,8 ,9 ,9 ,9 ,10,10,10,11,11,11,11,12,12,12,12,12,4 ,4 ,4 ,4 ,5 ,5 ,5 ,5 },
    {5 ,6 ,6 ,6 ,7 ,7 ,8 ,8 ,8 ,9 ,9 ,10,10,10,11,11,11,11,12,12,12,12,12,13,4 ,4 ,4 ,4 ,4 ,5 ,5 ,5 },
    {5 ,6 ,6 ,6 ,7 ,7 ,8 ,8 ,8 ,9 ,9 ,10,10,10,11,11,11,12,12,12,12,12,13,13,3 ,4 ,4 ,4 ,4 ,4 ,5 ,5 },
    {5 ,5 ,6 ,6 ,7 ,7 ,8 ,8 ,8 ,9 ,9 ,10,10,11,11,11,11,12,12,12,12,13,13,13,3 ,3 ,4 ,4 ,4 ,4 ,5 ,5 },
    {5 ,5 ,6 ,6 ,7 ,7 ,7 ,8 ,9 ,9 ,9 ,10,10,11,11,11,12,12,12,12,13,13,13,13,3 ,3 ,3 ,4 ,4 ,4 ,4 ,5 },
    {5 ,5 ,5 ,6 ,6 ,7 ,7 ,8 ,9 ,9 ,10,10,11,11,11,12,12,12,13,13,13,13,13,13,3 ,3 ,3 ,3 ,3 ,4 ,4 ,4 },
    {4 ,5 ,5 ,6 ,6 ,7 ,7 ,8 ,9 ,9 ,10,10,11,11,12,12,12,13,13,13,13,13,14,14,2 ,3 ,3 ,3 ,3 ,3 ,4 ,4 },
};

static uint8_t ray_heading(int16_t px, int16_t py, uint8_t inset) {
    uint8_t x = (uint8_t)(((px >> 4) + inset) & 31);
    uint8_t y = (uint8_t)(((py >> 4) + inset) & 31);
    return (uint8_t)((rays[y][x] + 8) & 31);
}

/* String tables (verbatim from the CX16 text.c). */
static const char sPlay[]        = "PLAY";
static const char sTitle[]       = "TIME PILOT";
static const char sDeposit[]     = "PLEASE PRESS 1 OR 2";
static const char sTryGame[]     = "AND TRY THIS GAME";
static const char sControlsK[]   = "KEYBOARD OR JOYSTICK";
static const char sOptKeyboard[] = "[K]EYBOARD";
static const char sOptJoystick[] = "[J]OYSTICK";
static const char sBonus1[]      = "1ST BONUS 10000 PTS.";
static const char sBonus2[]      = "AND EVERY 50000 PTS.";
static const char sKonami[]      = "^\x20KONAMI 1982";
static const char sVersion[]     = "CX16 VERSION BY";
static const char sWessels[]     = "STEFAN WESSELS 2024";
static const char sIIveraVer[]   = "APPLE II VERA VERSION BY";
static const char sAnomixer[]    = "ANOMIXER 2026";
static const char sHighScore[]   = "HIGH SCORE";
static const char sOneUp[]       = "1-UP";
static const char sTwoUp[]       = "2-UP";
static const char sLives[]       = "LIVES";
static const char sGameOver[]    = "GAME OVER";
static const char sPlayer1[]     = "PLAYER 1";
static const char sPlayer2[]     = "PLAYER 2";
static const char sReady[]       = "READY";
static const char sStage[]       = "STAGE";
static uint8_t stageIntroState   = 0;
static uint16_t announceT        = 0;
static uint8_t isGameStartIntro  = 0;
static uint8_t playerBoom        = 0;
static uint8_t playerDeadTimer   = 0;
static const char sPaused[]      = "PAUSED";
static const char sRanking[]     = "SCORE RANKING TABLE";
static const char sEnterInitials[] = "INPUT YOUR INITIALS";
static const char sPressSpace[]  = "PRESS SPACE OR 1";
/* Era labels (CX16 TEXT_PERIOD*). */
static const char *eraLabel[5] = {
    "A.D. 1910", "A.D. 1940", "A.D. 1970", "A.D. 1982", "A.D. 2001"
};

/* ------------------------- Game state ------------------------- */
static uint16_t playerX, playerY;
static uint8_t  facing;                        /* 0..31 heading */
static int8_t   targetFacing;                  /* 0..31 target heading (-1 if none) */
static int8_t   spinDir;                       /* 0=off, -1=Q CCW, +1=E CW */
static uint8_t  spinKey;                       /* last Q/E until IIe any-key-down clears */
static uint16_t bulletX[NUM_BULLETS], bulletY[NUM_BULLETS];
static int8_t   bulletVX[NUM_BULLETS], bulletVY[NUM_BULLETS];
static uint8_t  bulletOn[NUM_BULLETS];
static uint16_t ebX[NUM_EBULLETS], ebY[NUM_EBULLETS];
static uint8_t  ebHead[NUM_EBULLETS];          /* CX16 activeHeading[] / bomb swing */
static uint8_t  ebOn[NUM_EBULLETS];
static uint8_t  ebKind[NUM_EBULLETS];          /* kind | EB_TRACKED | EB_RIGHT */
static uint8_t  ebOwner[NUM_EBULLETS];         /* enemy index, or >=NUM_ENEMIES if flyer/orphan */
static uint8_t  enemyShot[NUM_ENEMIES];        /* 0xFF free, else ebullet slot (CX16 enemyWeapon[]) */
static uint8_t  numTracked, numTrackedMax, numRockets, launchSide;
static int16_t  enemyX[NUM_ENEMIES], enemyY[NUM_ENEMIES];
static uint8_t  enemyFacing[NUM_ENEMIES];
static uint8_t  enemyOffscreen[NUM_ENEMIES];
static uint8_t  enemyOn[NUM_ENEMIES];
static uint8_t  enemyHeading[NUM_ENEMIES];     /* CX16 enemyHeading[] - desired heading */
static uint8_t  enemyMode[NUM_ENEMIES];        /* 0 patrol, 1 AI_FOLLOW, 2 AI_FLEE */
static uint8_t  enemyThink[NUM_ENEMIES];       /* CX16 activeTimer[], in think ticks */
static uint8_t  enemyBoom[NUM_ENEMIES];
static uint8_t  enemyWave[NUM_ENEMIES];        /* 1 if member of 4-plane wave */
static uint8_t  enemyXfrac[NUM_ENEMIES], enemyYfrac[NUM_ENEMIES];
static uint8_t  waveEnemiesAlive;             /* remaining count of current wave */
static uint8_t  waveSpawnL, waveSpawnR, waveSpawnN, waveSpawnDir, waveSpawnDur;
static uint8_t  numFollowersMax;              /* CX16 numberOfAIFollowersMax */
static uint16_t lifeFrames;                   /* CX16 frameCounter - reset each life */
static uint16_t waveTimer;
static int16_t  cloudX[NUM_CLOUDS], cloudY[NUM_CLOUDS];
static const uint8_t cloudType[NUM_CLOUDS] = { 0, 1, 2, 1, 0, 1, 2, 1 };
/* Sprite slot per cloud, chosen by type so that draw order follows size:
 * large clouds in front of the player, medium then small behind the playfield.
 * Indices track cloudType above (0=small, 1=medium, 2=large). */
static const uint8_t cloudSprite[NUM_CLOUDS] = {
    SPR_CLOUD0_BASE + 0,   /* i=0  small  */
    SPR_CLOUD1_BASE + 0,   /* i=1  medium */
    SPR_CLOUD2_BASE + 0,   /* i=2  LARGE  */
    SPR_CLOUD1_BASE + 1,   /* i=3  medium */
    SPR_CLOUD0_BASE + 1,   /* i=4  small  */
    SPR_CLOUD1_BASE + 2,   /* i=5  medium */
    SPR_CLOUD2_BASE + 1,   /* i=6  LARGE  */
    SPR_CLOUD1_BASE + 3,   /* i=7  medium */
};
/* CX16 moves the three cloud tiers at VELOCITY_050 / _075 / _119 while the
 * world itself scrolls at VELOCITY_119 - so the near tier tracks the world
 * exactly and the far tiers lag it by 0.42x and 0.63x. The port used
 * (scroll * 1,2,3)/2, i.e. 0.5x / 1.0x / 1.5x, which made the big foreground
 * clouds outrun everything else in the world by half again. These are the
 * CX16 ratios as 8.8 fixed point, carried the way aiAddVelocity() does it. */
static const int8_t  cloudWhole[3][5] = {
    { -1, -1, 0,   0, 0 },      /* 0.420x - CX16 VELOCITY_050 / VELOCITY_119 */
    { -2, -1, 0,   0, 1 },      /* 0.630x - CX16 VELOCITY_075 / VELOCITY_119 */
    { -2, -1, 0,   1, 2 },      /* 1.000x - CX16 VELOCITY_119 (world speed)  */
};
static const uint8_t cloudFrac[3][5] = {
    {  41, 149, 0, 107, 215 },
    { 189,  95, 0, 161,  67 },
    {   0,   0, 0,   0,   0 },
};
static uint8_t cloudXf[NUM_CLOUDS], cloudYf[NUM_CLOUDS];
static const int16_t cloudInitX[NUM_CLOUDS] = { 128, -8, 160, 32, 0, 112, 32, 160 };
static const int16_t cloudInitY[NUM_CLOUDS] = { 216, 212, 193, 146, 77, 72, 54, 0 };
static uint8_t  bossOn, bossHp, bossFire, bossBoom, bossRam;
static int16_t  bossX, bossY, bossTimer;
static int8_t   bossDir;                    /* +1 right, -1 left */
/* Parachute score pickup: falls from above, worth bonus points on player hit. */
static uint8_t  paraOn;
static int16_t  paraX, paraY;
static uint16_t paraTimer;
static uint8_t  paraAnim;
static uint8_t  paraBonusStreak;
/* 1940 Bomber formation */
static uint8_t  bomberOn;
static int16_t  bomberX, bomberY;
static int8_t   bomberDir;
static uint8_t  bomberHealth;
static uint16_t bomberTimer;
static uint8_t  bomberBoom;
/* Floating score popup */
static uint8_t  popupOn;
static int16_t  popupX, popupY;
static uint8_t  popupFrame;
static uint8_t  popupTimer;
static uint32_t score;
static uint8_t  lives, stage;
static uint16_t enemiesKilled;
static uint16_t stageClearTimer;   /* >0: post-boss 3s hold (CX16 playerExitTimer) */
static uint16_t frameCount;        /* free-running, for blink/animation */
static uint8_t  g_titleDrawn;      /* title screen already drawn this visit */
static uint8_t  g_hudDirty;        /* force HUD redraw on next draw_hud() */
static uint8_t  g_annDrawn;        /* stage announce already drawn this visit */
static uint8_t  state = 0;         /* 0=title  1=playing  2=highscore entry  3=gameover  4=announce */
static uint8_t  titleClear = 1;
static uint8_t  cheatInfiniteLives = 0; /* 'C' key toggle: infinite fighters */

/* Propeller animation via palette cycling */
static const uint16_t colorPaletteProps[3] = { 0x0680, 0x00C0, 0x0FFF };
static const uint16_t colorPaletteSky[5]   = { 0x0006, 0x0056, 0x0065, 0x0505, 0x0000 };
static uint8_t  propState = 0;

/* CX16 data.c bossAnimFrames[] — damage-smoke cycle length.
 * Boss indexes by health>>1 (HP 8); bomber indexes by health (HP 4). */
static const uint8_t bossAnimFrames[4] = { 3, 3, 2, 1 };

/* CX16 data.c horizontalLaunchRayTable[], rotated into IIvera's angle space
 * (IIvera angle = CX16 angle + 8). Snaps the player's heading onto the launch
 * ray that sits on a left or right screen edge; rays 16..31 are the left edge,
 * which is CX16's horizontalDirectionTable[] DIR_RIGHT range. Used to decide
 * which side the bomber and the level boss fly in from. */
static const uint8_t horizontalLaunchRay[32] = {
     5,  5,  5,  5,  5,  5,  6,  7,
     8,  9, 10, 11, 11, 11, 11, 11,
    21, 21, 21, 21, 21, 21, 22, 23,
    24, 25, 26, 27, 27, 27, 27, 27
};

/* Multiplier scoring: 0.5s (30 frames) kill window (100 -> 200 -> 300 -> 400 pts) */
static uint8_t  killMultiplier = 1;
static uint8_t  killTimer = 0;

/* Extra life tracking: 10,000 pts 1st bonus, then every 50,000 pts */
static uint32_t nextExtraLife = 10000;

/* 2-Player Game State */
typedef struct {
    uint32_t score;
    uint8_t  lives;
    uint8_t  stage;
    uint16_t enemiesKilled;
    uint32_t nextExtraLife;
    uint8_t  alive;
    uint8_t  stageIntroState;
} PlayerState;

static PlayerState players[2];
static uint8_t  numPlayers = 1;       /* 1 or 2 */
static uint8_t  activePlayer = 0;     /* 0 = 1P, 1 = 2P */
static uint8_t  useJoystick = 1;      /* Default 1: Joystick enabled (toggle via K/J) */

/* Attract Demo Mode (1940 Sea-Green Sky, authentic CX16 replay) */
static uint8_t  isDemoMode = 0;
static uint16_t demoIndex = 0;
static uint8_t  attractCycleCount = 0;

/* High-score interactive initials entry */
static int8_t   hs_row = -1;
static uint8_t  hs_char_idx = 0;      /* 0..2 */
static char     hs_curr_char = 'A';
static uint16_t hs_entry_timer = 0;
static uint8_t  hs_color_timer = 0;
static uint8_t  hs_initials_color = 9;
static uint8_t  hs_fire_held = 0;
static uint8_t  hs_rep_timer = 0;

static void lose_life(void);
static void draw_hud(void);
static void game_over_screen(void);
static void check_extra_life(void);
static uint8_t frame_toward(int16_t dx, int16_t dy);
static uint8_t turn_on_ray(uint8_t cur, uint8_t target);
static void upload_pattern_stream(uint16_t addr, uint32_t art_off, uint16_t len);

/* High-score table (arcade 5 entries). Initials entry is the "high-score table
 * with initials" parity feature; the score is the only thing that persists. */
#define NUM_HIGHSCORES 5
static uint32_t highScore[NUM_HIGHSCORES] = { 65816, 8086, 6809, 6502, 4040 };
static char     highScoreInitials[NUM_HIGHSCORES][4] = { "K.O","N.A","M.I","O.O","Y.A" };

/* Base palette, verbatim from CX16 colorPalette[] (data.c).
 * apple2ts renders each entry as 4-bit-per-channel (RRRR GGGG BBBB),
 * so these exact CX16 values produce the correct bright colors. */
static const uint16_t pal[16] = {
    0x0000, 0x0F00, 0x00C0, 0x005F, 0x0FF0, 0x0F80, 0x0C0C, 0x00CF,
    0x0888, 0x0FFF, 0x0800, 0x0680, 0x000A, 0x0BB0, 0x6135, 0x0000
};

static void load_palette(void) {
    uint16_t i;
    vera_set_addr(VERA_INC_BANK1, PALETTE_ADDR);
    for (i = 0; i < 256; i++) {
        uint16_t c = pal[i & 15];          /* rows 16..31 duplicate 0..15 */
        VERA.data0 = c & 0xff;
        VERA.data0 = c >> 8;
    }
}

/* Per-stage sky color (palette 0 = layer background), from CX16 colorPaletteSky.
 * Order: 1910 / 1940 / 1970 / 1982 / 2001. */
static void set_stage_palette(void) {
    uint16_t c = colorPaletteSky[stage];
    vera_set_addr(VERA_INC_BANK1, PALETTE_ADDR);
    VERA.data0 = (uint8_t)(c & 0xff);
    VERA.data0 = (uint8_t)(c >> 8);

    /* Sprite palette index 14 (palette row 1, offset 32 + 14*2 = 60).
     * Stage 4 (A.D. 2001 Space UFO): Cyan (0x00CF) for UFO domes, lights, and mothership core.
     * Stages 0..3: Sky color for transparent propeller blend. */
    vera_set_addr(VERA_INC_BANK1, (uint16_t)(PALETTE_ADDR + 32 + 14 * 2));
    if (stage == 4) {
        VERA.data0 = 0xCF;
        VERA.data0 = 0x00;  /* 0x00CF = Cyan */
    } else {
        VERA.data0 = (uint8_t)(c & 0xFF);
        VERA.data0 = (uint8_t)(c >> 8);
    }
}

/* Set palette entry 0 to solid black for Title & Attract screens (matches cx16-1.jpg) */
static void set_black_palette(void) {
    vera_set_addr(VERA_INC_BANK1, PALETTE_ADDR);
    VERA.data0 = 0x00;
    VERA.data0 = 0x00;
}

/* Authentic CX16 time-warp beam tiles (tiles 22..31 in Bank 1 font RAM).
 * Used by screen_time_warp() to draw the hyperspace beam across rows 14 & 15. */
static const uint8_t warp_tiles[10][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF }, /* Tile 22: bottom 1 line */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF }, /* Tile 23: bottom 2 lines */
    { 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF }, /* Tile 24: bottom 4 lines */
    { 0x0F, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, /* Tile 25: bottom 6 lines + top-right flare */
    { 0xF0, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, /* Tile 26: bottom 6 lines + top-left flare */
    { 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* Tile 27: top 1 line */
    { 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* Tile 28: top 2 lines */
    { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 }, /* Tile 29: top 4 lines */
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x0F }, /* Tile 30: top 6 lines + bottom-right flare */
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xF0 }, /* Tile 31: top 6 lines + bottom-left flare */
};

static void load_font(void) {
    /* Clear first 32 tiles (0..31) */
    vera_set_addr(VERA_INC_BANK1, TILE_BASE);
    for (uint16_t b = 0; b < 256; b++) VERA.data0 = 0x00;
    /* Upload 10 time-warp beam tiles into tiles 22..31 */
    vera_set_addr(VERA_INC_BANK1, (uint16_t)(TILE_BASE + 22 * 8));
    for (uint8_t t = 0; t < 10; t++) {
        for (uint8_t r = 0; r < 8; r++) {
            VERA.data0 = warp_tiles[t][r];
        }
    }
    /* Stream 96 glyphs from HDV art blob */
    upload_pattern_stream((uint16_t)(TILE_BASE + 32 * 8), ART_FONT8X8_OFF, 768);
    /* Tile 127: a SOLID block */
    vera_set_addr(VERA_INC_BANK1, (uint16_t)(TILE_BASE + 127u * 8));
    for (uint8_t row = 0; row < 8; row++) {
        VERA.data0 = 0xFF;
    }
}

#define SOLID_BLOCK   127      /* solid-fill tile (see load_font) */

static void setup_layer0(void) {
    VERA.layer0.config = 0x10;     // 16-color text mode (T256C=0), mapw 64, maph 32
    VERA.layer0.mapbase = 0x00;
    VERA.layer0.tilebase = 0xF8;
    VERA.layer0.hscroll = 0;
    VERA.layer0.vscroll = 0;
}

static void draw_text(uint8_t row, uint8_t col, const char *s, uint8_t color) {
    while (*s) {
        uint16_t map_off = (uint16_t)row * 128 + (uint16_t)col * 2;
        vera_set_addr(VERA_INC_BANK0, LAYER0_MAP + map_off);
        VERA.data0 = (uint8_t)*s;
        uint8_t attr = (col >= 28) ? (uint8_t)(0xF0 | (color & 0x0F)) : (uint8_t)(color & 0x0F);
        VERA.data0 = attr;
        s++;
        col++;
    }
}


/* Erase the PLAYFIELD (cols 0..27) to `color` (0 = sky). The status bar
 * (cols 28..39) is left untouched so the HUD text is never wiped mid-game. */
static void clear_playfield(uint8_t color) {
    uint8_t row, col;
    uint8_t attr = (uint8_t)((color & 0x0F) << 4);
    for (row = 0; row < 32; row++) {
        uint16_t map_off = (uint16_t)row * 128;
        vera_set_addr(VERA_INC_BANK0, LAYER0_MAP + map_off);
        for (col = 0; col < 28; col++) {
            VERA.data0 = 32;
            VERA.data0 = attr;
        }
    }
}

/* Paint the right-hand status bar (cols 28..39) BLACK. */
static void paint_status_bar(void) {
    uint8_t row, col;
    for (row = 0; row < 32; row++) {
        uint16_t map_off = (uint16_t)row * 128 + 28 * 2;
        vera_set_addr(VERA_INC_BANK0, LAYER0_MAP + map_off);
        for (col = 28; col < 64; col++) {
            VERA.data0 = SOLID_BLOCK;   /* solid tile */
            VERA.data0 = 0xFF;          /* BG 15 (black), FG 15 (black) */
        }
    }
}

/* Full reset: playfield to sky, status bar to black. */
static void paint_screen(void) {
    clear_playfield(0);
    paint_status_bar();
}

/* Wait for the next VERA vertical blank (rock-solid 60Hz frame lock). */
static void waitvsync(void) {
    while ((VERA.irq_flags & VERA_IRQ_VSYNC) == 0) {}  /* poll until hardware raises VSYNC */
    VERA.irq_flags = VERA_IRQ_VSYNC;                   /* acknowledge/clear flag for next frame */
}
static void hide_sprite(uint8_t n);

/* Counter-clockwise circular radar screen wipe (matches CX16 screenWipe) */
static void screen_draw_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t color) {
    int8_t dx = (x1 > x0) ? (int8_t)(x1 - x0) : (int8_t)(x0 - x1);
    int8_t sx = (x0 < x1) ? 1 : -1;
    int8_t dy = (y1 > y0) ? (int8_t)-(y1 - y0) : (int8_t)-(y0 - y1);
    int8_t sy = (y0 < y1) ? 1 : -1;
    int8_t err = dx + dy;
    while (1) {
        if (x0 < 28 && y0 < 30) {
            uint16_t off = (uint16_t)y0 * 128 + (uint16_t)x0 * 2;
            vera_set_addr(VERA_INC_BANK0, LAYER0_MAP + off);
            VERA.data0 = SOLID_BLOCK;
            VERA.data0 = (uint8_t)(((color & 0x0F) << 4) | (color & 0x0F));
        }
        if (x0 == x1 && y0 == y1) break;
        int8_t e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static uint8_t wipe_step;
static void wipe_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t color) {
    screen_draw_line(x0, y0, x1, y1, color);
    if (++wipe_step >= 2) {
        wipe_step = 0;
        waitvsync();
        audioServiceAudio();
    }
}

static void screen_wipe(uint8_t color) {
    int8_t counter;
    uint8_t cx = 13, cy = 15; /* center of 28x30 playfield */
    wipe_step = 0;

    /* 1. Top edge: center to top-left (counter-clockwise) */
    for (counter = cx; counter >= 0; counter--) {
        wipe_line(cx, cy, (uint8_t)counter, 0, color);
    }
    /* 2. Left edge: top-left to bottom-left */
    for (counter = 1; counter < 30; counter++) {
        wipe_line(cx, cy, 0, (uint8_t)counter, color);
    }
    /* 3. Bottom edge: bottom-left to bottom-right */
    for (counter = 1; counter < 28; counter++) {
        wipe_line(cx, cy, (uint8_t)counter, 29, color);
    }
    /* 4. Right edge: bottom-right to top-right */
    for (counter = 28; counter >= 0; counter--) {
        wipe_line(cx, cy, 27, (uint8_t)counter, color);
    }
    /* 5. Top edge: top-right back to center */
    for (counter = 26; counter > cx; counter--) {
        wipe_line(cx, cy, (uint8_t)counter, 0, color);
    }
}

static void screen_wipe_to_sky(uint8_t new_stage) {
    uint16_t new_sky = colorPaletteSky[new_stage];

    /* Temporarily map palette index 14 to the new sky color */
    vera_set_addr(VERA_INC_BANK1, (uint16_t)(PALETTE_ADDR + 14 * 2));
    VERA.data0 = (uint8_t)(new_sky & 0xFF);
    VERA.data0 = (uint8_t)(new_sky >> 8);

    /* Sweep counter-clockwise from 12 o'clock using color 14 */
    screen_wipe(14);

    /* Hide 3D TIME PILOT logo now that radar sweep has fully covered the playfield */
    hide_sprite(SPR_LOGO_TIME);
    hide_sprite(SPR_LOGO_PILOT);

    /* Commit new sky color to palette 0 and clear playfield */
    vera_set_addr(VERA_INC_BANK1, (uint16_t)(PALETTE_ADDR + 0 * 2));
    VERA.data0 = (uint8_t)(new_sky & 0xFF);
    VERA.data0 = (uint8_t)(new_sky >> 8);

    /* Reset playfield to standard spaces */
    clear_playfield(0);

    /* Restore palette 14 */
    vera_set_addr(VERA_INC_BANK1, (uint16_t)(PALETTE_ADDR + 14 * 2));
    VERA.data0 = (uint8_t)(pal[14] & 0xFF);
    VERA.data0 = (uint8_t)(pal[14] >> 8);
}

/* ------------------------- Sprite helpers ------------------------- */
static void pattern_addr(uint16_t addr, uint8_t *lo, uint8_t *hi) {
    uint32_t full = (uint32_t)addr | 0x10000;   /* bank 1 */
    *lo = (uint8_t)((full >> 5) & 0xFF);
    *hi = (uint8_t)(0x80 | ((full >> 13) & 0x0F));
}

/* Stream a sprite pattern from the HDV art blob into VERA pattern RAM.
 * Ensures data is buffered via MLI BEFORE setting VERA VRAM pointer,
 * preventing MLI disk reads from corrupting VERA address state mid-transfer. */
static void upload_pattern_stream(uint16_t addr, uint32_t art_off, uint16_t len) {
    while (len > 0) {
        uint16_t in_blk = (uint16_t)(art_off & 511);
        uint16_t chunk = 512 - in_blk;
        if (chunk > len) chunk = len;

        const uint8_t *src = disk_ensure(ART_START_BLOCK, ART_TOTAL_BYTES, art_off);
        vera_set_addr(VERA_INC_BANK1, addr);
        uint16_t c = chunk;
        while (c--) {
            VERA.data0 = *src++;
        }

        addr += chunk;
        art_off += chunk;
        len -= chunk;
    }
}



/* dims: 0=8x8, 0x50=16x16, 0x60=32x16.  Palette offset forced to 1 (16..31). */
static void set_sprite(uint8_t n, uint16_t pat, uint16_t x, uint16_t y,
                       uint8_t visible, uint8_t dims) {
    uint8_t lo, hi;
    uint16_t base = SPRITE_ATTR + (uint16_t)n * 8;
    pattern_addr(pat, &lo, &hi);
    vera_set_addr(VERA_INC_BANK1, base);
    VERA.data0 = lo;
    VERA.data0 = hi;
    VERA.data0 = x & 0xFF;
    VERA.data0 = (x >> 8) & 0x03;
    VERA.data0 = y & 0xFF;
    VERA.data0 = (y >> 8) & 0x03;
    uint8_t z = (n >= SPR_LIFE_BASE) ? 0x0C : 0x04;
    VERA.data0 = visible ? z : 0x00;
    VERA.data0 = (uint8_t)(dims | 1);          // palette_offset=1 -> palette 16..31
}

static void hide_sprite(uint8_t n) {
    uint16_t base = SPRITE_ATTR + (uint16_t)n * 8 + 6;
    vera_set_addr(VERA_INC_BANK1, base);
    VERA.data0 = 0x00;                         // z-depth = 0: disabled/hidden
}

static void move_sprite(uint8_t n, uint16_t x, uint16_t y) {
    uint16_t base = SPRITE_ATTR + (uint16_t)n * 8 + 2;
    vera_set_addr(VERA_INC_BANK1, base);
    VERA.data0 = x & 0xFF;
    VERA.data0 = (x >> 8) & 0x03;
    VERA.data0 = y & 0xFF;
    VERA.data0 = (y >> 8) & 0x03;
}

static void set_sprite_pat(uint8_t n, uint16_t pat) {
    uint8_t lo, hi;
    uint16_t base = SPRITE_ATTR + (uint16_t)n * 8;
    pattern_addr(pat, &lo, &hi);
    vera_set_addr(VERA_INC_BANK1, base);
    VERA.data0 = lo;
    VERA.data0 = hi;
}

/* Authentic CX16 / Arcade hyperspace time-warp script.
 * Triplet format: x, length, tile_f (followed by -1 to terminate each step). */
static const int8_t timeWarpDrawScript[] = {
    11,  6, 22, -1,
     9, 10, 22, -1,
     7, 14, 22, -1,
     5, 18, 22, -1,
     3, 22, 22, -1,
     0, 28, 22, -1,
     0, 28, 22,  9, 10, 23, -1,
     0, 28, 22,  7, 14, 23, -1,
     0, 28, 22,  5, 18, 23,  9, 10, 24, -1,
     0, 28, 22,  3, 22, 23,  7, 14, 24, 13, 1, 25, 14, 1, 26, -1,
     0, 28, 22,  3, 22, 23,  7, 14, 24, 13, 1, 25, 14, 1, 26, -1,
     0, 28, 22,  3, 22, 23,  7, 14, 24, 13, 1, 25, 14, 1, 26, -1,
     0, 28, 22,  3, 22, 23,  7, 14, 24, 13, 1, 25, 14, 1, 26, -1,
     0, 28, 22,  5, 18, 23,  9, 10, 24, -1,
     0, 28, 22,  7, 14, 23, -1,
     0, 28, 22,  9, 10, 23, -1,
     0, 28, 22, -1,
     3, 22, 22, -1,
     5, 18, 22, -1,
     7, 14, 22, -1,
     9, 10, 22, -1,
    11,  6, 22, -1,
    -1
};

static void screen_time_warp(void) {
    int8_t x;
    uint16_t i = 0;

    /* CX16 keeps the current heading; the plane stays pinned at PLAYER_X/Y. */
    set_sprite(SPR_PLAYER, PAT_PLAYER + (uint16_t)((facing - 8) & 31) * 256,
               playerX, playerY, 1, 0x50);

    x = timeWarpDrawScript[0];
    do {
        while (x >= 0) {
            i++;
            int8_t l = timeWarpDrawScript[i++];
            int8_t f = timeWarpDrawScript[i++];
            while (l > 0) {
                uint16_t off_top = (uint16_t)14 * 128 + (uint16_t)x * 2;
                uint16_t off_bot = (uint16_t)15 * 128 + (uint16_t)x * 2;

                /* Top row 14: character f, white foreground (9) on sky background (0) */
                vera_set_addr(VERA_INC_BANK0, LAYER0_MAP + off_top);
                VERA.data0 = (uint8_t)f;
                VERA.data0 = 0x09;

                /* Bottom row 15: character f + 5, white foreground (9) on sky background (0) */
                vera_set_addr(VERA_INC_BANK0, LAYER0_MAP + off_bot);
                VERA.data0 = (uint8_t)(f + 5);
                VERA.data0 = 0x09;

                l--;
                x++;
            }
            x = timeWarpDrawScript[i];
        }

        /* Player plane ON (visible in beam) */
        set_sprite(SPR_PLAYER, PAT_PLAYER, playerX, playerY, 1, 0x50);
        waitvsync();
        audioServiceAudio();
        waitvsync();
        audioServiceAudio();

        /* Player plane OFF (pulsing flash) */
        hide_sprite(SPR_PLAYER);

        /* Erase row 14 & 15 back to sky across all 28 playfield columns */
        for (uint8_t c = 0; c < 28; c++) {
            uint16_t off_top = (uint16_t)14 * 128 + (uint16_t)c * 2;
            uint16_t off_bot = (uint16_t)15 * 128 + (uint16_t)c * 2;
            vera_set_addr(VERA_INC_BANK0, LAYER0_MAP + off_top);
            VERA.data0 = 32;
            VERA.data0 = 0x00;
            vera_set_addr(VERA_INC_BANK0, LAYER0_MAP + off_bot);
            VERA.data0 = 32;
            VERA.data0 = 0x00;
        }

        waitvsync();
        audioServiceAudio();
        waitvsync();
        audioServiceAudio();

        i++;
        x = timeWarpDrawScript[i];
    } while (x >= 0);

    hide_sprite(SPR_PLAYER);
}

static uint16_t get_cloud_pat(uint8_t type) {
    if (stage == 4) {
        if (type == 0) return PAT_ASTRO0;
        if (type == 1) return PAT_ASTRO1;
        return PAT_ASTRO2;
    }
    if (type == 0) return PAT_CLOUD0;
    if (type == 1) return PAT_CLOUD1;
    return PAT_CLOUD2;
}

static uint8_t get_cloud_dims(uint8_t type) {
    if (stage == 4) {
        if (type == 2) return 0x60; /* astro2 is 32x16 */
        return 0x50;                /* astro0, astro1 are 16x16 */
    } else {
        if (type == 0) return 0x50; /* cloud0 is 16x16 */
        if (type == 1) return 0x60; /* cloud1 is 32x16 */
        return 0x70;                /* cloud2 is 64x16 */
    }
}

static void hide_all_sprites(void) {
    uint8_t n;
    for (n = 0; n < 128; n++) {
        uint16_t base = SPRITE_ATTR + (uint16_t)n * 8 + 6;
        vera_set_addr(VERA_INC_BANK1, base);
        VERA.data0 = 0x00;
        VERA.data0 = 0x00;
    }
}

static void reset_clouds(void) {
    uint8_t i;
    for (i = 0; i < NUM_CLOUDS; i++) {
        cloudX[i] = cloudInitX[i];
        cloudY[i] = cloudInitY[i];
        cloudXf[i] = 0;
        cloudYf[i] = 0;
        uint8_t t = cloudType[i];
        set_sprite(cloudSprite[i], get_cloud_pat(t), (uint16_t)cloudX[i], (uint16_t)cloudY[i], 1, get_cloud_dims(t));
    }
}

static void update_clouds(int16_t scrollDx, int16_t scrollDy) {
    uint8_t i;
    for (i = 0; i < NUM_CLOUDS; i++) {
        uint8_t t = cloudType[i];
        int16_t w = (t == 0) ? 16 : (t == 1) ? 32 : 64;
        int16_t spanX = (int16_t)(224 + w);
        uint8_t xi = (uint8_t)(scrollDx + 2), yi = (uint8_t)(scrollDy + 2);
        uint16_t fx = (uint16_t)cloudXf[i] + cloudFrac[t][xi];
        uint16_t fy = (uint16_t)cloudYf[i] + cloudFrac[t][yi];
        cloudXf[i] = (uint8_t)fx;
        cloudYf[i] = (uint8_t)fy;
        int16_t cx = (int16_t)cloudX[i] + cloudWhole[t][xi] + (int16_t)(fx >> 8);
        int16_t cy = (int16_t)cloudY[i] + cloudWhole[t][yi] + (int16_t)(fy >> 8);
        if (cx < -w)       cx += spanX;
        else if (cx > 224) cx -= spanX;
        if (cy < -16)      cy += 256;
        else if (cy > 240) cy -= 256;
        cloudX[i] = cx;
        cloudY[i] = cy;
        move_sprite(cloudSprite[i], (uint16_t)cx, (uint16_t)cy);
    }
}

static void update_propeller(void) {
    if (stage < 3 && !(frameCount & 3)) {
        propState ^= 1;
        vera_set_addr(VERA_INC_BANK1, (uint16_t)(PALETTE_ADDR + 32 + 14 * 2));
        if (propState) {
            VERA.data0 = (uint8_t)(colorPaletteSky[stage] & 0xFF);
            VERA.data0 = (uint8_t)(colorPaletteSky[stage] >> 8);
            VERA.data0 = (uint8_t)(colorPaletteProps[stage] & 0xFF);
            VERA.data0 = (uint8_t)(colorPaletteProps[stage] >> 8);
        } else {
            VERA.data0 = (uint8_t)(colorPaletteProps[stage] & 0xFF);
            VERA.data0 = (uint8_t)(colorPaletteProps[stage] >> 8);
            VERA.data0 = (uint8_t)(colorPaletteSky[stage] & 0xFF);
            VERA.data0 = (uint8_t)(colorPaletteSky[stage] >> 8);
        }
    } else if (stage == 4) {
        /* Stage 4 Space Mothership: pulsate colors between Cyan and Magenta when damaged */
        if (bossOn && bossHp <= (BOSS_HP * 2) / 3) {
            if (!(frameCount & 7)) {
                propState ^= 1;
                vera_set_addr(VERA_INC_BANK1, (uint16_t)(PALETTE_ADDR + 32 + 14 * 2));
                if (propState) {
                    VERA.data0 = 0xCF; VERA.data0 = 0x00; /* 0x00CF: Cyan */
                } else {
                    VERA.data0 = 0x0C; VERA.data0 = 0x0C; /* 0x0C0C: Magenta */
                }
            }
        } else if ((frameCount & 31) == 0) {
            /* Keep steady Cyan when undamaged or no boss */
            vera_set_addr(VERA_INC_BANK1, (uint16_t)(PALETTE_ADDR + 32 + 14 * 2));
            VERA.data0 = 0xCF; VERA.data0 = 0x00;
        }
    }
}

static void upload_stage_art(void) {}

static void setup_sprites(void) {
    /* Stream the entire art blob into VRAM Bank 1 at 0x0000. All 5 eras of
     * sprites stay resident; zero disk I/O mid-game. */
    upload_pattern_stream(0x0000, 0, ART_TOTAL_BYTES);

    /* Pre-generate 8 progressive slice frames for HUD radar progress icon at 0xE840 */
    uint8_t progRaw[128];
    for (uint8_t i = 0; i < 128; i++) {
        progRaw[i] = *disk_ensure(ART_START_BLOCK, ART_TOTAL_BYTES, ART_PROGRESS_FRAMES_OFF + i);
    }
    for (uint8_t fr = 0; fr < 8; fr++) {
        uint8_t cut = (uint8_t)(fr << 1);
        uint16_t base = PAT_PROG_ICON + (uint16_t)fr * 128;
        vera_set_addr(VERA_INC_BANK1, base);
        for (uint8_t i = 0; i < 128; i++) {
            VERA.data0 = ((i & 15) >= cut) ? progRaw[i] : 0;
        }
    }
    hide_all_sprites();
}

static void upload_pcm_to_vram(void) {
    /* Stream Bank 0 audio blob ($1000..$B70E) */
    vera_set_addr(VERA_INC_BANK0, VRAM_AUDIO_BASE);
    uint32_t off = 0;
    while (off < PCM_TOTAL_BYTES) {
        uint8_t *chunk = disk_ensure(PCM_START_BLOCK, PCM_TOTAL_BYTES, off);
        uint16_t in_blk = 512 - (uint16_t)(off & 511);
        uint16_t rem = (uint16_t)(PCM_TOTAL_BYTES - off);
        uint16_t n = (rem < in_blk) ? rem : in_blk;
        for (uint16_t i = 0; i < n; i++) {
            VERA.data0 = chunk[i];
        }
        off += n;
    }
}

/* ------------------------- Gameplay ------------------------- */
/* CX16 ai.c aiRandom() - the 8-bit LFSR the reference drives every spawn from.
 * Seeded to -1 by globalsInit(). */
static int8_t randomSeed = -1;
static int8_t aiRandom(void) {
    int8_t a = (int8_t)(randomSeed << 1);
    if (randomSeed & 0x80) {
        a ^= 0x1d;
    }
    randomSeed = a;
    return randomSeed;
}

static uint16_t get_enemy_pat(uint8_t i) {
    uint8_t f;
    if (stage == 2) {
        /* Stage 2 (1970): 9-frame helicopter rotation mapping */
        static const uint8_t heliMap[32] = {
            0, 0, 1, 1, 2, 2, 3, 3,
            4, 5, 5, 6, 6, 7, 7, 8,
            8, 8, 7, 7, 6, 6, 5, 5,
            4, 3, 3, 2, 2, 1, 1, 0
        };
        f = heliMap[(enemyFacing[i] - 8) & 31];
    } else if (stage == 4) {
        /* Stage 4 (2001): 4-frame pulsating space UFO animation */
        f = (uint8_t)(((frameCount + (i << 2)) >> 2) & 3);
    } else {
        /* Stage 0 (1910 biplane), Stage 1 (1940 monoplane), Stage 3 (1982 jet): 8 rotation frames */
        f = (uint8_t)(((enemyFacing[i] - 8) & 31) >> 2);
    }
    return (uint16_t)(PAT_ENEMY + (uint16_t)f * 256);
}

static int8_t stageBossAudio(void) { return (int8_t)(AUDIO_BOSSL0 + (stage & 3)); }

static void check_extra_life(void) {
    if (score >= nextExtraLife) {
        if (lives < NUM_LIFE_SPR + 1) {
            lives++;
            g_hudDirty = 1;
        }
        audioPlaySource(AUDIO_EXTRA_LIFE);
        nextExtraLife += 50000;
    }
}

/* CX16 keeps a running numberOfAIFollowers, decremented from every death and
 * despawn path. Counting on demand is equivalent and cannot drift - and this
 * only runs when an enemy actually asks to become a follower. */
static uint8_t count_followers(void) {
    uint8_t n = 0, k;
    for (k = 0; k < NUM_ENEMIES; k++) {
        if (enemyOn[k] && !enemyBoom[k] && enemyMode[k] == 1) n++;
    }
    return n;
}

static void spawn_enemy(void) {
    uint8_t i;
    for (i = 0; i < NUM_ENEMIES; i++) {
        if (!enemyOn[i]) {
            enemyOn[i] = 1;
            enemyWave[i] = 0;
            /* CX16 ai.c aiSpawnEnemy(): (((aiRandom() & 7) - 4) + playerAngle) & 31 */
            uint8_t a = (uint8_t)((facing + (aiRandom() & 7) - 4) & 31);
            enemyX[i] = launchX[a];
            enemyY[i] = launchY[a];
            /* CX16 aiSpawnEnemy(): position is playerAngle ±4, but activeFrame
             * and enemyHeading are always invPlayerAngle (playerAngle^16). */
            uint8_t target = (uint8_t)((facing + 16) & 31);
            enemyFacing[i] = target;
            enemyHeading[i] = target;
            /* CX16 aiSpawnEnemy(): patrol by default, and only a 1-in-2 chance
             * of joining the (at most numberOfAIFollowersMax) active chasers. */
            enemyMode[i] = 0;
            if ((aiRandom() & 64) && count_followers() < numFollowersMax) {
                enemyMode[i] = 1;
            }
            enemyThink[i] = (uint8_t)(T_STEADY_MIN + (aiRandom() & 31));
            enemyBoom[i] = 0;
            enemyOffscreen[i] = 0;
            enemyShot[i] = 0xFF;
            enemyXfrac[i] = 0;
            enemyYfrac[i] = 0;
            set_sprite(SPR_ENEMY_BASE + i, get_enemy_pat(i), (uint16_t)enemyX[i], (uint16_t)enemyY[i], 1, 0x50);
            return;
        }
    }
}

/* Place one wave member on launch ray `a`. */
static void wave_place(uint8_t a) {
    uint8_t i;
    for (i = 0; i < NUM_ENEMIES; i++) {
        if (!enemyOn[i]) {
            enemyOn[i] = 1;
            enemyWave[i] = 1;
            enemyMode[i] = 0;
            enemyX[i] = launchX[a];
            enemyY[i] = launchY[a];
            enemyFacing[i] = waveSpawnDir;
            enemyHeading[i] = waveSpawnDir;
            enemyThink[i] = waveSpawnDur;
            enemyBoom[i] = 0;
            enemyOffscreen[i] = 0;
            enemyShot[i] = 0xFF;
            enemyXfrac[i] = 0;
            enemyYfrac[i] = 0;
            set_sprite(SPR_ENEMY_BASE + i, get_enemy_pat(i),
                       (uint16_t)enemyX[i], (uint16_t)enemyY[i], 1, 0x50);
            waveEnemiesAlive++;
            return;
        }
    }
}

/* CX16 aiEndFrame + aiSpawnWave: 3..5 planes, fanned, arriving in pairs
 * every 16 frames (odd-sized waves send a singleton first). */
static void spawn_wave_setup(void) {
    uint8_t i, live = 0;
    int8_t n;
    for (i = 0; i < NUM_ENEMIES; i++) {
        if (enemyOn[i]) live++;
    }
    n = (int8_t)(7 - live);
    if (n < 3) { waveSpawnN = 0; return; }
    if (n > 5) n = 5;
    waveSpawnL = waveSpawnR = facing;
    if (n == 4) {
        waveSpawnL = (uint8_t)((waveSpawnL - 1) & 31);
        waveSpawnR = (uint8_t)((waveSpawnR + 1) & 31);
    }
    waveSpawnDir = (uint8_t)((facing + 16) & 31);
    waveSpawnDur = (uint8_t)(T_WAVE_ENTRY + (aiRandom() & 15));
    waveSpawnN = (uint8_t)n;
    waveEnemiesAlive = 0;
    audioPlaySource(AUDIO_WAVE_START);
}

static void spawn_wave_pair(void) {
    wave_place(waveSpawnL);
    if ((--waveSpawnN) & 1) {
        wave_place(waveSpawnR);
        waveSpawnN--;
    }
    if (waveSpawnN) {
        waveSpawnL = (uint8_t)((waveSpawnL - 1) & 31);
        waveSpawnR = (uint8_t)((waveSpawnR + 1) & 31);
    }
}

/* CX16 input.c/ai.c: a trigger pull sets bulletTimer = PLAYER_BULLET_FIRE_TIMER
 * (24 frames) and aiEndFrame() emits one bullet every 8 frames while it runs -
 * a 3-round burst per press. The port fired one bullet per input poll instead,
 * so holding the joystick button produced a continuous 7-bullet stream. */
#define T_FIRE_BURST    TICKS(24)
static uint8_t bulletTimer;
static void request_fire(void) {
    if (!bulletTimer) bulletTimer = T_FIRE_BURST;
}

static void fire_bullet(void) {
    uint8_t i;
    for (i = 0; i < NUM_BULLETS; i++) {
        if (!bulletOn[i]) {
            bulletOn[i] = 1;
            /* Center the 2x2 white bullet (located at top-left of 8x8 sprite)
             * precisely at the nose/center of the 16x16 player fighter (+7). */
            bulletX[i] = playerX + 7 + (int16_t)velDx[facing] * 3;
            bulletY[i] = playerY + 7 + (int16_t)velDy[facing] * 3;
            bulletVX[i] = (int8_t)(velDx[facing] * 2);
            bulletVY[i] = (int8_t)(velDy[facing] * 2);
            set_sprite(SPR_BULLET_BASE + i, PAT_BULLET, bulletX[i], bulletY[i], 1, 0);
            audioPlaySource(AUDIO_PLAYER_SHOOT);
            return;
        }
    }
}

static void add_chain_score(void) {
    if (killTimer >= 15) killMultiplier = 1;
    killTimer = 0;
    score += (uint32_t)killMultiplier * 100;
    if (killMultiplier < 255) killMultiplier++;
    check_extra_life();
    g_hudDirty = 1;
}

static void kill_eb(uint8_t i, uint8_t scored) {
    uint8_t o = ebOwner[i];
    uint8_t k = (uint8_t)(ebKind[i] & EB_KMASK);
    ebOn[i] = 0;
    hide_sprite(SPR_EBULLET_BASE + i);
    if (o < NUM_ENEMIES && enemyShot[o] == i) enemyShot[o] = 0xFF;
    if ((ebKind[i] & EB_TRACKED) && numTracked) numTracked--;
    if (k == EB_ROCKET && numRockets && !(--numRockets)) {
        audioStopSource(AUDIO_ROCKET_FLY);
    }
    if (scored) {
        add_chain_score();
        if (k == EB_BOMB || k == EB_ROCKET) audioPlaySource(AUDIO_WAPON_EXPLODE);
    }
}

static void clear_ebullets(void) {
    uint8_t i;
    for (i = 0; i < NUM_EBULLETS; i++) {
        if (ebOn[i]) {
            ebOn[i] = 0;
            hide_sprite(SPR_EBULLET_BASE + i);
        }
    }
    for (i = 0; i < NUM_ENEMIES; i++) enemyShot[i] = 0xFF;
    numTracked = 0;
    if (numRockets) {
        numRockets = 0;
        audioStopSource(AUDIO_ROCKET_FLY);
    }
}

/* CX16 aiEnemy / aiHorizontalFlyer: shot along heading at VELOCITY_200
 * (VELOCITY_150 for rockets). Returns slot or 0xFF. */
static uint8_t spawn_eb(uint16_t x, uint16_t y, uint8_t heading,
                        uint8_t kind, uint8_t owner, uint8_t flags) {
    uint8_t i, dims;
    uint16_t pat;
    for (i = 0; i < NUM_EBULLETS; i++) {
        if (ebOn[i]) continue;
        ebOn[i] = 1;
        ebX[i] = x; ebY[i] = y;
        ebHead[i] = heading;
        ebKind[i] = (uint8_t)(kind | flags);
        ebOwner[i] = owner;
        if (owner < NUM_ENEMIES) enemyShot[owner] = i;
        if (flags & EB_TRACKED) numTracked++;
        dims = ebDims[kind];
        switch (kind) {
        case EB_BOMB:
            pat = PAT_BOMB + (uint16_t)((flags & EB_RIGHT) ? 0 : SPR_BYTES_8);
            audioPlaySource(AUDIO_BOMB);
            break;
        case EB_ROCKET:
            pat = PAT_ROCKET + (uint16_t)(((heading - 8) & 31) >> 1) * SPR_BYTES_16;
            audioPlaySource(AUDIO_ROCKET_LAUNCH);
            audioPlaySource(AUDIO_ROCKET_FLY);
            numRockets++;
            break;
        case EB_BOOMER:
            pat = PAT_BOOMERANG;
            audioPlaySource(AUDIO_ROCKET_LAUNCH);
            break;
        case EB_SPACE:
            pat = PAT_SBULLET;
            break;
        default:
            pat = PAT_EBULLET;
            audioPlaySource(AUDIO_ENEMY_SHOOT);
            break;
        }
        set_sprite(SPR_EBULLET_BASE + i, pat, x, y, 1, dims);
        return i;
    }
    return 0xFF;
}

static void spawn_flyer_shot(uint16_t x, uint16_t y) {
    uint8_t heading;
    if (numTracked >= numTrackedMax) return;
    heading = (uint8_t)((aiRandom() & 15) + ((x > PF_W / 2) ? 16 : 0));
    spawn_eb(x, y, heading, (stage == 4) ? EB_SPACE : EB_BULLET, 0xFF, EB_TRACKED);
}

static void try_enemy_fire(uint8_t i) {
    uint8_t target, a, ct, kind, flags, h;
    int16_t ex, ey;
    if (enemyShot[i] != 0xFF || playerBoom || playerDeadTimer) return;
    ex = enemyX[i];
    ey = enemyY[i];
    target = ray_heading(ex, ey, 0);
    if (target == enemyFacing[i]) {
        a = (uint8_t)((target - facing) & 31);
        if (a >= 30 || a < 2) {
            spawn_eb((uint16_t)(ex + 8), (uint16_t)(ey + 4), enemyFacing[i],
                     (stage == 4) ? EB_SPACE : EB_BULLET, i, 0);
        }
    } else if (stage != 1 && numTracked < numTrackedMax) {
        if (stage == 0) {
            if ((!launchSide && ex < WEAPON_BORDER) ||
                (launchSide && ex >= (int16_t)(PF_W - WEAPON_BORDER))) {
                ct = (uint8_t)((target - 8) & 31);
                if ((ct < 2 || ct == 31) || (ct >= 15 && ct <= 17)) {
                    flags = (uint8_t)(EB_TRACKED | (launchSide ? 0 : EB_RIGHT));
                    h = (flags & EB_RIGHT) ? 5 : 27;   /* CX16 29/19 in IIvera space */
                    if (spawn_eb((uint16_t)(ex + 8), (uint16_t)(ey + 4), h,
                                 EB_BOMB, i, flags) != 0xFF) {
                        launchSide ^= 1;
                    }
                }
            }
        } else if ((!launchSide && (ey < WEAPON_BORDER || ex < WEAPON_BORDER)) ||
                   (launchSide && (ey >= (int16_t)(240 - WEAPON_BORDER) ||
                                   ex >= (int16_t)(PF_W - WEAPON_BORDER)))) {
            kind = (stage == 4) ? EB_BOOMER : EB_ROCKET;
            if (spawn_eb((uint16_t)(ex + 8), (uint16_t)(ey + 4), enemyFacing[i],
                         kind, i, EB_TRACKED) != 0xFF) {
                launchSide ^= 1;
            }
        }
    }
}

static void spawn_boss(void) {
    uint8_t hray = horizontalLaunchRay[facing];
    uint8_t bframe;
    bossDir = (hray & 16) ? 1 : -1;
    bossX = (bossDir > 0) ? -32 : 224;
    bossY = launchY[hray] + (int16_t)(((aiRandom() & 3) - 2) << 4);
    bossOn = 1;
    bossFire = TICKS(32);
    bossBoom = 0;
    bframe = (stage == 4) ? 0 : ((bossDir > 0) ? 0 : 4);
    set_sprite(SPR_BOSS, PAT_BOSS + (uint16_t)bframe * 512,
               (uint16_t)bossX, (uint16_t)bossY, 1, 0x60);
}

static void boss_explode(void) {
    uint8_t k;
    score += 3000;
    check_extra_life();
    popupOn = 1; popupX = bossX + 8; popupY = bossY;
    popupFrame = POPUP_3000; popupTimer = T_POPUP;
    g_hudDirty = 1;
    bossHp = 0;
    bossOn = 0;
    bossBoom = T_BOOM32;
    set_sprite(SPR_BOSS, PAT_EXPL32, (uint16_t)bossX, (uint16_t)bossY, 1, 0x60);
    audioStopSource(stageBossAudio());
    audioStopSource(AUDIO_ROCKET_FLY);
    audioPlaySource(AUDIO_BIG_EXPLOSION);
    bulletTimer = 0;
    for (k = 0; k < NUM_ENEMIES; k++) {
        if (enemyOn[k] && enemyBoom[k] == 0) {
            enemyBoom[k] = T_BOOM16;
            set_sprite_pat(SPR_ENEMY_BASE + k, PAT_EXPL);
        }
    }
    clear_ebullets();
    if (bomberOn && bomberBoom == 0) {
        bomberBoom = T_BOOM32;
        set_sprite(SPR_BOMBER, PAT_EXPL32, (uint16_t)bomberX, (uint16_t)bomberY, 1, 0x60);
    }
    if (paraOn) {
        paraOn = 0;
        hide_sprite(SPR_PARACHUTE);
    }
    /* CX16 playerExitTimer: 3s of live explosions, then the beam. No banner.
     * Ram sets both EXIT_STAGE_CLEAR and EXIT_PLAYER_DIED — skip the warp. */
    if (!bossRam)
        stageClearTimer = T_PLAYER_DIED;
}

/* CX16 collideBomber death/ram: 1500 + AUDIO_ENEMY_EXPLODE (chips are silent). */
static void bomber_explode(void) {
    bomberBoom = T_BOOM32;
    bomberHealth = 0;
    bomberTimer = T_BOMBER;
    score += 1500;
    check_extra_life();
    g_hudDirty = 1;
    popupOn = 1;
    popupX = bomberX + 8;
    popupY = bomberY;
    popupFrame = POPUP_1500;
    popupTimer = T_POPUP;
    audioPlaySource(AUDIO_ENEMY_EXPLODE);
    set_sprite(SPR_BOMBER, PAT_EXPL32, (uint16_t)bomberX, (uint16_t)bomberY, 1, 0x60);
}

static uint8_t ray_step(uint16_t num, uint16_t den) {
    return (num < den * 25)  ? 0 :
           (num < den * 78)  ? 1 :
           (num < den * 137) ? 2 :
           (num < den * 210) ? 3 : 4;
}

/* Blazing-fast octant direction solver (0..31, 0=UP, 8=RIGHT, 16=DOWN, 24=LEFT).
 * Replaces 32-iteration loop & 64 16-bit multiplications with instant comparisons.
 * Returns 0xFF if dx=dy=0 (undefined); callers must keep the current heading.
 * Returning 0 (UP) made overlapping chasers all face north — and with world
 * scroll that is "face N, drift NE", which got worse while the player was a
 * corpse they were still homing on. */
static uint8_t frame_toward(int16_t dx, int16_t dy) {
    if (dx == 0 && dy == 0) return 0xFF;
    int16_t ax = (dx < 0) ? -dx : dx;
    int16_t ay = (dy < 0) ? -dy : dy;
    while (ax > 240 || ay > 240) {
        ax >>= 1;
        ay >>= 1;
    }
    uint8_t oct, step;
    if (ay >= ax) {
        /* North / South dominant: octants 0, 3, 4, 7 */
        step = ray_step((uint16_t)ax << 8, (uint16_t)ay);
        if (dy < 0) {
            oct = (dx >= 0) ? step : (32 - step);
        } else {
            oct = (dx >= 0) ? (16 - step) : (16 + step);
        }
    } else {
        /* East / West dominant: octants 1, 2, 5, 6 */
        step = ray_step((uint16_t)ay << 8, (uint16_t)ax);
        if (dx >= 0) {
            oct = (dy < 0) ? (8 - step) : (8 + step);
        } else {
            oct = (dy < 0) ? (24 + step) : (24 - step);
        }
    }
    return (uint8_t)(oct & 31);
}

/* CX16 ai.c aiTurnOnRay(): step the heading towards the target the short way
 * round - one step, or three when the target is 8 or more steps away. */
static uint8_t turn_on_ray(uint8_t cur, uint8_t target) {
    uint8_t a = (uint8_t)((target - cur) & 31);
    if (a) {
        if (a & 16) {
            cur--;
            if (!(a & 8)) cur -= 2;
        } else {
            cur++;
            if (a & 8) cur += 2;
        }
    }
    return (uint8_t)(cur & 31);
}

/* ------------------------- Update ------------------------- */
static void update_game(void) {
    uint8_t i;
    int16_t scrollDx = -(int16_t)velDx[facing];
    int16_t scrollDy = -(int16_t)velDy[facing];
    uint8_t eH = enemyHitH[stage];

    /* CX16 aiEndFrame returns before scoreTimer/spawns/bullets once the
     * boss is dead (levelBossHealth <= 0). Explosions and scroll still run. */
    if (bossHp > 0) {
        if (killTimer < 255) killTimer++;

        /* CX16 aiEndFrame(): once a life has lasted 1500 frames, allow a third
         * simultaneous chaser. frameCounter is reset per stage/life there. */
        if (lifeFrames < 1500) {
            lifeFrames += 2;
        } else {
            numFollowersMax = 3;
            numTrackedMax = 3;          /* CX16 numberOfTrackedMax */
        }

        if (bulletTimer) {                  /* CX16 aiEndFrame() bullet emitter */
            if (!(bulletTimer & 3)) fire_bullet();
            bulletTimer--;
        }
    }

    /* Player explosion handling */
    if (playerBoom > 0) {
        playerBoom--;
        /* CX16 explosion frames run 3..0; this ran 0..3, i.e. backwards, and
         * backwards relative to every other explosion in the port. */
        uint8_t fi = (uint8_t)(playerBoom >> 3);
        if (fi > 3) fi = 3;
        set_sprite(SPR_PLAYER, PAT_EXPL32 + (uint16_t)fi * 512, playerX, playerY, 1, 0x60);
        if (playerBoom == 0) {
            set_sprite(SPR_PLAYER, PAT_PLAYER, 0, 0, 0, 0); /* hide player during post-mortem */
        }
    } else if (playerDeadTimer > 0) {
        playerDeadTimer--;
        if (playerDeadTimer == 0) {
            if (isDemoMode) {
                isDemoMode = 0;
                draw_text(0, 9, "         ", 0);
                hide_all_sprites();
                stage = 0;
                upload_stage_art();
                screen_wipe_to_sky(0);    /* Blue counter-clockwise radar wipe to clean playfield! */
                set_black_palette();
                paint_screen();
                state = 0;
                titleClear = 1;
                attractCycleCount = 0;
                return;
            }

            /* Deduct life now that explosion and post-mortem review have finished */
            if (!cheatInfiniteLives) {
                if (lives > 0) lives--;
            }

            if (numPlayers == 2) {
                players[activePlayer].score = score;
                players[activePlayer].lives = lives;
                players[activePlayer].stage = stage;
                players[activePlayer].enemiesKilled = enemiesKilled;
                players[activePlayer].nextExtraLife = nextExtraLife;
                players[activePlayer].stageIntroState = stageIntroState;
                if (lives == 0) {
                    players[activePlayer].alive = 0;
                }

                uint8_t other = activePlayer ^ 1;
                if (players[other].alive) {
                    /* CX16 gameNextPlayer: swap on every death */
                    activePlayer = other;
                    score = players[activePlayer].score;
                    lives = players[activePlayer].lives;
                    stage = players[activePlayer].stage;
                    enemiesKilled = players[activePlayer].enemiesKilled;
                    nextExtraLife = players[activePlayer].nextExtraLife;
                    stageIntroState = players[activePlayer].stageIntroState;
                    killMultiplier = 1;
                    killTimer = 0;

                    for (i = 0; i < NUM_ENEMIES; i++) {
                        enemyOn[i] = 0; enemyBoom[i] = 0; enemyWave[i] = 0;
                        enemyXfrac[i] = 0; enemyYfrac[i] = 0;
                        hide_sprite(SPR_ENEMY_BASE + i);
                    }
                    clear_ebullets();
                    for (i = 0; i < NUM_BULLETS; i++) {
                        bulletOn[i] = 0;
                        hide_sprite(SPR_BULLET_BASE + i);
                    }
                    paraOn = 0;
                    paraBonusStreak = 0;
                    hide_sprite(SPR_PARACHUTE);
                    bomberOn = 0;
                    bomberBoom = 0;
                    hide_sprite(SPR_BOMBER);
                    popupOn = 0;
                    hide_sprite(SPR_POPUP);
                    bossOn = 0;
                    bossBoom = 0;
                    bossTimer = T_BOSS;
                    set_sprite(SPR_BOSS, PAT_BOSS, 0, 0, 0, 0);

                    playerX = PLAYER_X0;
                    playerY = PLAYER_Y0;
                    facing = 8;
                    targetFacing = 8;
                    spinDir = 0;
                    spinKey = 0;
                    set_sprite(SPR_PLAYER, PAT_PLAYER + (uint16_t)((facing - 8) & 31) * 256, playerX, playerY, 1, 0x50);

                    /* Gate sprites during player switch asset streaming */
                    VERA.display.video = 0x11;
                    set_stage_palette();
                    upload_stage_art();
                    paint_status_bar();
                    g_hudDirty = 1;
                    draw_hud();
                    reset_clouds();           /* Restore all clouds to clean center coordinates */

                    waitvsync();
                    VERA.display.video = 0x51; /* Atomic reveal */

                    announceT = 0;
                    g_annDrawn = 0;
                    lifeFrames = 0;
                    numFollowersMax = 2;
                    numTrackedMax = 2;
                    launchSide = 0;
                    state = 4;      /* Stage announce for other player */
                    return;
                } else if (lives == 0) {
                    /* Both players out of lives */
                    players[activePlayer].score = score;
                    players[activePlayer].lives = 0;
                    players[activePlayer].alive = 0;
                    audioStopSource(stageBossAudio());
                    game_over_screen();
                    state = 3;
                    titleClear = 1;
                    return;
                }
            } else {
                if (lives == 0) {
                    players[0].score = score;
                    players[0].lives = 0;
                    players[0].alive = 0;
                    audioStopSource(stageBossAudio());
                    game_over_screen();
                    state = 3;
                    titleClear = 1;
                    return;
                }
            }

            /* Single player respawn (or 2P when only 1 is still alive).
             * CX16 gameStageInit wipes every object; 1P was missing the chute. */
            for (i = 0; i < NUM_ENEMIES; i++) {
                enemyOn[i] = 0; enemyBoom[i] = 0; enemyWave[i] = 0;
                enemyXfrac[i] = 0; enemyYfrac[i] = 0;
                hide_sprite(SPR_ENEMY_BASE + i);
            }
            clear_ebullets();
            for (i = 0; i < NUM_BULLETS; i++) {
                bulletOn[i] = 0;
                hide_sprite(SPR_BULLET_BASE + i);
            }
            paraOn = 0;
            paraBonusStreak = 0;
            hide_sprite(SPR_PARACHUTE);
            bomberOn = 0;
            bomberBoom = 0;
            hide_sprite(SPR_BOMBER);
            popupOn = 0;
            hide_sprite(SPR_POPUP);
            if (bossOn) {
                bossOn = 0;
                hide_sprite(SPR_BOSS);
            }
            bossBoom = 0;
            bossTimer = T_BOSS;
            playerX = PLAYER_X0;
            playerY = PLAYER_Y0;
            facing = 8; /* Facing RIGHT */
            targetFacing = 8;
            spinDir = 0;
            spinKey = 0;
            set_sprite(SPR_PLAYER, PAT_PLAYER + (uint16_t)((facing - 8) & 31) * 256, playerX, playerY, 1, 0x50);
            reset_clouds();
            g_hudDirty = 1;
            lifeFrames = 0;
            waveTimer = 0;
            numFollowersMax = 2;        /* CX16 globalsStageInit() */
            numTrackedMax = 2;
            launchSide = 0;
            if (bossRam) {
                /* CX16 ram: skip warp, advance era, full stage announce */
                bossRam = 0;
                stage = (uint8_t)((stage + 1) % NUM_STAGES);
                enemiesKilled = 0;
                bossHp = BOSS_HP;
                upload_stage_art();
                screen_wipe_to_sky(stage);
                set_stage_palette();
                paint_status_bar();
                announceT = 0;
                stageIntroState = 0;
            } else {
                announceT = 0;
                stageIntroState = 1;
            }
            draw_hud();
            g_annDrawn = 0;
            state = 4;      /* Stage announce / READY */
            return;
        }
    }

    /* Clouds scroll with parallax and wrap around edges. */
    update_clouds(scrollDx, scrollDy);

    /* Player bullets. */
    for (i = 0; i < NUM_BULLETS; i++) {
        if (bulletOn[i]) {
            int16_t bx = (int16_t)bulletX[i] + bulletVX[i];
            int16_t by = (int16_t)bulletY[i] + bulletVY[i];
            if (bx < PF_XMIN || bx > PF_XMAX || by < PF_YMIN || by > PF_YMAX) {
                bulletOn[i] = 0;
                hide_sprite(SPR_BULLET_BASE + i);
            } else {
                bulletX[i] = (uint16_t)bx; bulletY[i] = (uint16_t)by;
                move_sprite(SPR_BULLET_BASE + i, (uint16_t)bx, (uint16_t)by);
            }
        }
    }

    /* Enemy weapons. Kind, not stage: a 1910 plane can have a bullet and a
     * later bomb in different slots; boss spray is always a dumb bullet. */
    for (i = 0; i < NUM_EBULLETS; i++) {
        if (ebOn[i]) {
            uint8_t k = (uint8_t)(ebKind[i] & EB_KMASK);
            uint8_t h = ebHead[i];
            int8_t hs = 4;              /* VELOCITY_200 ≈ vel * 2 */
            int16_t bx, by;
            if (k == EB_BOMB) {
                /* CX16 aiEnemyBombs: heading 29→6 (right) / 19→10 (left),
                 * +8 into IIvera space → 5→14 / 27→18. VELOCITY_150 until
                 * settled, then 200. Frame is throw direction, not heading. */
                if (ebKind[i] & EB_RIGHT) {
                    if (h != 14) {
                        if (!(frameCount & 3)) ebHead[i] = h = (uint8_t)((h + 1) & 31);
                        hs = 3;
                    }
                } else if (h != 18) {
                    if (!(frameCount & 3)) ebHead[i] = h = (uint8_t)((h - 1) & 31);
                    hs = 3;
                }
            } else if (k == EB_ROCKET) {
                if (!(frameCount & 15)) {
                    /* CX16 aiTurnOnRay: cell is (min>>4)+3, not the player vector. */
                    uint8_t t = ray_heading((int16_t)ebX[i], (int16_t)ebY[i], 3);
                    ebHead[i] = h = turn_on_ray(h, t);
                    set_sprite_pat(SPR_EBULLET_BASE + i,
                        PAT_ROCKET + (uint16_t)(((h - 8) & 31) >> 1) * SPR_BYTES_16);
                }
                hs = 3;                 /* VELOCITY_150 */
            } else if (k == EB_BOOMER) {
                if (!(frameCount & 15) &&
                    ebX[i] > 48 && ebX[i] < 176 && ebY[i] > 56 && ebY[i] < 184) {
                    uint8_t t = ray_heading((int16_t)ebX[i], (int16_t)ebY[i], 3);
                    ebHead[i] = h = turn_on_ray(h, t);
                }
                set_sprite_pat(SPR_EBULLET_BASE + i,
                    PAT_BOOMERANG + (uint16_t)(frameCount & 7) * SPR_BYTES_8);
            } else if (k == EB_SPACE) {
                set_sprite_pat(SPR_EBULLET_BASE + i,
                    PAT_SBULLET + (uint16_t)((frameCount >> 2) & 3) * SPR_BYTES_8);
            }
            bx = (int16_t)ebX[i] + (int16_t)((velDx[h] * hs) / 2) + scrollDx;
            by = (int16_t)ebY[i] + (int16_t)((velDy[h] * hs) / 2) + scrollDy;
            if (bx < PF_XMIN || bx > PF_XMAX || by < PF_YMIN || by > PF_YMAX) {
                kill_eb(i, 0);
            } else {
                ebX[i] = (uint16_t)bx; ebY[i] = (uint16_t)by;
                move_sprite(SPR_EBULLET_BASE + i, (uint16_t)bx, (uint16_t)by);
            }
        }
    }

    /* Enemies: home toward player, rotate, fire; play explosions. */
    for (i = 0; i < NUM_ENEMIES; i++) {
        if (enemyOn[i]) {
            if (enemyBoom[i] > 0) {
                if (enemyShot[i] != 0xFF) {
                    ebOwner[enemyShot[i]] = 0xFE;
                    enemyShot[i] = 0xFF;
                }
                enemyBoom[i]--;
                if (enemyBoom[i] == 0) {
                    enemyOn[i] = 0;
                    set_sprite(SPR_ENEMY_BASE + i, PAT_ENEMY, 0, 0, 0, 0);
                } else {
                    uint8_t fi = enemyBoom[i] >> 2;
                    if (fi > 3) fi = 3;
                    set_sprite_pat(SPR_ENEMY_BASE + i, PAT_EXPL + (uint16_t)fi * 256);
                }
                continue;
            }

            /* 1. CX16 ai.c aiEnemy(): every enemy slot thinks once per 8 frames
             * and turns at most one step of 1/32. Crucially, most enemies do NOT
             * chase - they patrol on a random heading held for 22..53 think
             * ticks, and only up to numberOfAIFollowersMax of them ride the beam
             * to the player at any time. The port made all eight home in on the
             * player, every think tick, permanently. */
            if (((frameCount + (i * 2)) & 7) == 0) {
                if (enemyWave[i]) {
                    if (!--enemyThink[i]) {
                        if (enemyMode[i]) {
                            enemyWave[i] = 0;           /* the wave disbands */
                        } else {
                            if (count_followers() < numFollowersMax) {
                                enemyMode[i] = 1;       /* AI_FOLLOW */
                            } else {
                                enemyMode[i] = 2;       /* AI_FLEE; CX16 still lets them fire */
                            }
                            enemyThink[i] = T_WAVE_ACTIVE;
                        }
                    }
                } else if (!--enemyThink[i]) {
                    enemyHeading[i] = (uint8_t)(aiRandom() & 31);
                    enemyThink[i] = (uint8_t)(T_STEADY_MIN + (aiRandom() & 31));
                }

                if (enemyMode[i]) {
                    uint8_t t = ray_heading(enemyX[i], enemyY[i], 0);
                    enemyHeading[i] = (enemyMode[i] == 2) ? (uint8_t)((t + 16) & 31) : t;
                }

                uint8_t diff = (uint8_t)((enemyHeading[i] - enemyFacing[i]) & 31);
                if (diff != 0) {
                    if (diff & 16) {
                        enemyFacing[i] = (uint8_t)((enemyFacing[i] - 1) & 31);
                    } else {
                        enemyFacing[i] = (uint8_t)((enemyFacing[i] + 1) & 31);
                    }
                }
                set_sprite_pat(SPR_ENEMY_BASE + i, get_enemy_pat(i));
                try_enemy_fire(i);
            } else if (stage == 4 && !(frameCount & 3)) {
                /* Space UFOs (Stage 4) animate continuous light pulsing even when not steering */
                set_sprite_pat(SPR_ENEMY_BASE + i, get_enemy_pat(i));
            }

            /* 2. Move with world scroll + enemy's OWN engine thrust.
             * CX16 parity: In Stages 0..2 (1910, 1940, 1970), enemy thrust is ~84% of player speed
             * (1.00 / 1.19 = 84.0%), allowing the player to outrun and shake off pursuers.
             * In Stages 3..4 (1982 jets, 2001 UFOs), enemy thrust is 100% of player speed. */
            int16_t vx, vy;
            if (stage < 3) {
                uint8_t xi = (uint8_t)(velDx[enemyFacing[i]] + 2);
                uint16_t fx = (uint16_t)enemyXfrac[i] + step84_frac[xi];
                enemyXfrac[i] = (uint8_t)fx;
                vx = (int16_t)step84_whole[xi] + (int16_t)(fx >> 8);

                uint8_t yi = (uint8_t)(velDy[enemyFacing[i]] + 2);
                uint16_t fy = (uint16_t)enemyYfrac[i] + step84_frac[yi];
                enemyYfrac[i] = (uint8_t)fy;
                vy = (int16_t)step84_whole[yi] + (int16_t)(fy >> 8);
            } else {
                vx = (int16_t)velDx[enemyFacing[i]];
                vy = (int16_t)velDy[enemyFacing[i]];
            }
            int16_t ex = enemyX[i] + scrollDx + vx;
            int16_t ey = enemyY[i] + scrollDy + vy;

            /* 3. Off-screen tolerance: only despawn after drifting far off */
            if (ex < -32 || ex > 240 || ey < -32 || ey > 260) {
                if (++enemyOffscreen[i] > 30) {
                    if (enemyShot[i] != 0xFF) {
                        ebOwner[enemyShot[i]] = 0xFE;
                        enemyShot[i] = 0xFF;
                    }
                    enemyOn[i] = 0;
                    set_sprite(SPR_ENEMY_BASE + i, PAT_ENEMY, 0, 0, 0, 0);
                } else {
                    enemyX[i] = ex;
                    enemyY[i] = ey;
                    move_sprite(SPR_ENEMY_BASE + i, (uint16_t)ex, (uint16_t)ey);
                }
            } else {
                enemyOffscreen[i] = 0;
                enemyX[i] = ex;
                enemyY[i] = ey;
                move_sprite(SPR_ENEMY_BASE + i, (uint16_t)ex, (uint16_t)ey);
            }
        }
    }

    /* Boss: CX16 aiHorizontalFlyer — enter from one side, cross with world
     * scroll + VELOCITY_100, leave, respawn after LEVELBOSS_TIMER. */
    if (bossOn) {
        uint16_t bPat;
        bossX += scrollDx + (int16_t)bossDir * 2;   /* world + VELOCITY_100-ish */
        bossY += scrollDy;

        if (bossX < -48 || bossX > 256 || bossY < -32 || bossY > 260) {
            bossOn = 0;
            hide_sprite(SPR_BOSS);
            bossTimer = T_BOSS;         /* CX16 aiLevelBoss REMOVE → timer reload */
        } else {
            bPat = PAT_BOSS;
            if (stage == 4) {
                bPat += (uint16_t)((frameCount >> 2) & 1) * 512;
            } else {
                uint8_t dirOff = (bossDir > 0) ? 0 : 4;
                uint8_t damageFrame = 0;
                if (bossHp < BOSS_HP) {
                    uint8_t maxD = bossAnimFrames[bossHp >> 1];
                    damageFrame = (uint8_t)(maxD - ((frameCount >> 2) % (maxD + 1)));
                }
                bPat += (uint16_t)(dirOff + damageFrame) * 512;
            }
            set_sprite_pat(SPR_BOSS, bPat);
            move_sprite(SPR_BOSS, (uint16_t)bossX, (uint16_t)bossY);

            if (--bossFire == 0) {
                bossFire = TICKS(32);
                spawn_flyer_shot((uint16_t)(bossX + 16), (uint16_t)(bossY + 8));
            }
            for (i = 0; i < NUM_BULLETS; i++) {
                if (bulletOn[i]) {
                    int16_t bx = (int16_t)bulletX[i], by = (int16_t)bulletY[i];
                    if (bx + 2 > bossX && bx < bossX + 32 &&
                        by + 2 > bossY && by < bossY + 16) {
                        bulletOn[i] = 0;
                        hide_sprite(SPR_BULLET_BASE + i);
                        if (--bossHp == 0) boss_explode();
                        else add_chain_score();
                        break;
                    }
                }
            }
            if (bossOn && playerBoom == 0 && playerDeadTimer == 0 &&
                bossX < (int16_t)playerX + 11 && bossX + 32 > (int16_t)playerX + 5 &&
                bossY < (int16_t)playerY + 11 && bossY + 16 > (int16_t)playerY + 5) {
                bossRam = 1;
                boss_explode();
                lose_life();
            }
        }
    }

    /* Boss explosion frames. CX16 leaves the rest of the world running for
     * the full 3s playerExitTimer — do not hide anyone here. */
    if (bossBoom > 0) {
        bossBoom--;
        if (bossBoom == 0) {
            hide_sprite(SPR_BOSS);
        } else {
            uint8_t fi = (uint8_t)(bossBoom >> 3);
            if (fi > 3) fi = 3;
            set_sprite(SPR_BOSS, PAT_EXPL32 + (uint16_t)fi * 512,
                       (uint16_t)bossX, (uint16_t)bossY, 1, 0x60);
        }
    }

    /* 3s hold (CX16 PLAYER_DIED_TIMER), TIMEWARP at 1s, then the beam.
     * Clouds stay; heading is current (CX16 screenTimeWarp). */
    if (stageClearTimer > 0) {
        stageClearTimer--;
        if (stageClearTimer == T_PLAYER_DIED - TICKS(60))
            audioPlaySource(AUDIO_TIMEWARP);
        if (stageClearTimer == 0) {
            for (i = 0; i < NUM_ENEMIES; i++) {
                enemyOn[i] = 0; enemyBoom[i] = 0; enemyWave[i] = 0;
                enemyXfrac[i] = 0; enemyYfrac[i] = 0;
                hide_sprite(SPR_ENEMY_BASE + i);
            }
            clear_ebullets();
            for (i = 0; i < NUM_BULLETS; i++) {
                bulletOn[i] = 0;
                hide_sprite(SPR_BULLET_BASE + i);
            }
            paraOn = 0; hide_sprite(SPR_PARACHUTE);
            bomberOn = 0; bomberBoom = 0; hide_sprite(SPR_BOMBER);
            bossOn = 0; bossBoom = 0; hide_sprite(SPR_BOSS);
            popupOn = 0; hide_sprite(SPR_POPUP);

            screen_time_warp();

            stage = (uint8_t)((stage + 1) % NUM_STAGES);
            enemiesKilled = 0;
            bossHp = BOSS_HP;
            bossTimer = T_BOSS;
            bossRam = 0;
            lifeFrames = 0;
            waveTimer = 0;
            numFollowersMax = 2;        /* CX16 globalsStageInit() */
            numTrackedMax = 2;
            launchSide = 0;
            bulletTimer = 0;

            screen_wipe_to_sky(stage);      /* counter-clockwise radar sweep to next era! */
            /* CX16 gameStageInit: AUDIO_NEXT_LEVEL at the sky change, not
             * on a banner, and not when wrapping to 1910. */
            if (stage && !isDemoMode)
                audioPlaySource(AUDIO_NEXT_LEVEL);
            set_stage_palette();
            upload_stage_art();
            paint_status_bar();
            g_hudDirty = 1;
            facing = 8;
            targetFacing = 8;
            spinDir = 0;
            spinKey = 0;
            playerX = PLAYER_X0;
            playerY = PLAYER_Y0;
            set_sprite(SPR_PLAYER, PAT_PLAYER, playerX, playerY, 1, 0x50);
            reset_clouds();

            announceT = 0;
            g_annDrawn = 0;
            stageIntroState = 0;
            state = 4;
            return;
        }
    }

    /* Player bullet vs shootable weapons, then vs enemy.
     * CX16: bombs/rockets/boomerangs/space bullets collide with player
     * bullets; plain LAYER_ENEMY_BULLETS do not. */
    for (i = 0; i < NUM_BULLETS; i++) {
        if (bulletOn[i]) {
            int16_t bx = (int16_t)bulletX[i], by = (int16_t)bulletY[i];
            uint8_t hit = 0;
            for (uint8_t j = 0; j < NUM_EBULLETS; j++) {
                uint8_t k, ww, wh;
                int16_t ex, ey;
                if (!ebOn[j]) continue;
                k = (uint8_t)(ebKind[j] & EB_KMASK);
                if (k == EB_BULLET) continue;
                ww = ebHitW[k]; wh = ebHitH[k];
                ex = (int16_t)ebX[j]; ey = (int16_t)ebY[j];
                /* CX16 2x2 player bullet vs weapon AABB (inclusive max = min+size) */
                if (bx <= ex + ww && bx + 2 >= ex &&
                    by <= ey + wh && by + 2 >= ey) {
                    kill_eb(j, 1);
                    bulletOn[i] = 0;
                    hide_sprite(SPR_BULLET_BASE + i);
                    hit = 1;
                    break;
                }
            }
            if (hit) continue;
            for (uint8_t j = 0; j < NUM_ENEMIES; j++) {
                if (enemyOn[j] && enemyBoom[j] == 0) {
                    int16_t ddx = bx - enemyX[j];
                    /* 2x2 bullet vs 16 x eH: ddx in [-2, 16] */
                    if ((uint16_t)(ddx + 2) < 19) {
                        int16_t ddy = by - enemyY[j];
                        if ((uint16_t)(ddy + 2) < (uint16_t)(eH + 3)) {
                            int16_t ex = enemyX[j], ey = enemyY[j];
                            enemyBoom[j] = T_BOOM16;
                            set_sprite_pat(SPR_ENEMY_BASE + j, PAT_EXPL);
                            bulletOn[i] = 0;
                            hide_sprite(SPR_BULLET_BASE + i);

                            add_chain_score();
                            enemiesKilled++;
                            audioPlaySource(AUDIO_ENEMY_EXPLODE);

                            /* Wave Squadron wipeout bonus */
                            if (enemyWave[j]) {
                                enemyWave[j] = 0;
                                if (waveEnemiesAlive > 0) {
                                    waveEnemiesAlive--;
                                    if (waveEnemiesAlive == 0) {
                                        /* 4-plane wave wiped out! 2000 pts bonus! */
                                        score += 2000;
                                        check_extra_life();
                                        popupOn = 1; popupX = ex; popupY = ey; popupFrame = POPUP_2000; popupTimer = T_POPUP; /* "2000" frame */
                                        audioPlaySource(AUDIO_PICKUP);
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    /* CX16: parachute/bomber stop once enemiesKilled > 48; regular enemies
     * and waves keep coming while the boss is in the air. Boss itself waits
     * LEVELBOSS_TIMER after the 49th kill, enters from a side, and can leave
     * and come back. */
    if (bossHp > 0 && bossBoom == 0 && stageClearTimer == 0) {
        if (enemiesKilled > ENEMIES_TO_BOSS) {
            if (!audioIsSourcePlaying(stageBossAudio())) {
                audioPlaySource(stageBossAudio());
            }
            if (!bossOn && bossTimer >= 0) {
                if (--bossTimer < 0) spawn_boss();
            }
        }

        static uint8_t spawnCounter = 0;
        uint8_t scMax = isDemoMode ? (T_SPAWN * 2) : T_SPAWN;
        if (stage != 4) {
            ++waveTimer;
            if (waveTimer < T_WAVE) {
                if (waveTimer == T_RECALL) {
                    uint8_t k;
                    for (k = 0; k < NUM_ENEMIES; k++) {
                        if (enemyOn[k]) enemyMode[k] = 2;
                    }
                }
            } else if (waveTimer == T_WAVE) {
                spawn_wave_setup();
                if (!waveSpawnN) waveTimer = 0;
            } else if (!(waveTimer & 7) && waveSpawnN) {
                spawn_wave_pair();
                if (!waveSpawnN) waveTimer = 0;
            }
        }
        if ((stage == 4 || waveTimer < T_RECALL) && ++spawnCounter >= scMax) {
            spawnCounter = 0;
            /* CX16 aiEndFrame: regular spawn is `&& !playerExitTimer`. */
            if (!playerBoom && !playerDeadTimer)
                spawn_enemy();
        }
    }

    /* Enemy bullet vs player; enemy vs player (only if player alive). */
    if (playerBoom == 0 && playerDeadTimer == 0) {
        for (i = 0; i < NUM_EBULLETS; i++) {
            if (ebOn[i]) {
                uint8_t k = (uint8_t)(ebKind[i] & EB_KMASK);
                uint8_t ww = ebHitW[k], wh = ebHitH[k];
                int16_t bx = (int16_t)ebX[i], by = (int16_t)ebY[i];
                /* CX16 tight player box vs weapon AABB: min < PX+11 && max > PX+5 */
                if (bx < (int16_t)PLAYER_X0 + 11 && bx + ww > (int16_t)PLAYER_X0 + 5 &&
                    by < (int16_t)PLAYER_Y0 + 11 && by + wh > (int16_t)PLAYER_Y0 + 5) {
                    kill_eb(i, 0);
                    lose_life();
                    break;
                }
            }
        }
        for (i = 0; i < NUM_ENEMIES; i++) {
            if (enemyOn[i] && enemyBoom[i] == 0) {
                int16_t dex = enemyX[i] - (int16_t)playerX;
                /* CX16 tight box: width always 16; height is era eH (heli 9, UFO 8) */
                if ((uint16_t)(dex + 10) < 21) {
                    int16_t dey = enemyY[i] - (int16_t)playerY;
                    if ((uint16_t)(dey + eH - 6) < (uint16_t)(eH + 5)) {
                        enemyBoom[i] = T_BOOM16;
                        set_sprite_pat(SPR_ENEMY_BASE + i, PAT_EXPL);
                        lose_life();
                        break;
                    }
                }
            }
        }
    }

    /* Parachute bonus pickup (stages 0..3). CX16 stops new chutes once the
     * boss kill-count is met; an already-airborne one still drifts. */
    if (stage < 4) {
        if (!paraOn) {
            if (enemiesKilled <= ENEMIES_TO_BOSS && paraTimer > 0) {
                paraTimer--;
                if (paraTimer == 0) {
                    paraOn = 1;
                    /* CX16 aiEndFrame(): X = (playerAngle + ((rand&3)-2)) & 31,
                     * then launchPos[X] - the parachute appears on the horizon
                     * ahead of you rather than dropping in from the top edge. */
                    uint8_t pa = (uint8_t)((facing + (aiRandom() & 3) - 2) & 31);
                    paraX = launchX[pa];
                    paraY = launchY[pa];
                    paraAnim = 0;
                    set_sprite(SPR_PARACHUTE, PAT_PARACHUTE, (uint16_t)paraX, (uint16_t)paraY, 1, 0x50);
                }
            }
        } else {
            static const uint8_t paraFrames[6] = { 0, 1, 2, 3, 2, 1 };
            if (!(frameCount & 15)) {
                paraAnim = (uint8_t)((paraAnim + 1) % 6);
                set_sprite_pat(SPR_PARACHUTE, PAT_PARACHUTE + (uint16_t)paraFrames[paraAnim] * 256);
            }
            paraX += scrollDx;
            paraY += scrollDy;     /* CX16 aiParachute: world scroll only */
            if (paraX < -20 || paraX > PF_W + 10 || paraY < -20 || paraY > PF_YMAX + 20) {
                paraOn = 0;
                paraTimer = T_PARACHUTE;
                paraBonusStreak = 0; /* missed parachute resets streak */
                set_sprite(SPR_PARACHUTE, PAT_PARACHUTE, 0, 0, 0, 0);
            } else {
                move_sprite(SPR_PARACHUTE, (uint16_t)paraX, (uint16_t)paraY);
                if (playerBoom == 0 && playerDeadTimer == 0 &&
                    paraX + 14 > (int16_t)playerX && paraX < (int16_t)playerX + 14 &&
                    paraY + 14 > (int16_t)playerY && paraY < (int16_t)playerY + 14) {
                    static const uint16_t bonusScores[5] = { 1000, 2000, 3000, 4000, 5000 };
                    score += bonusScores[paraBonusStreak];
                    check_extra_life();
                    popupOn = 1; popupX = paraX; popupY = paraY; popupFrame = paraBonusStreak; popupTimer = T_POPUP;
                    if (paraBonusStreak < 4) paraBonusStreak++;
                    g_hudDirty = 1;
                    audioPlaySource(AUDIO_PICKUP);
                    paraOn = 0;
                    paraTimer = T_PARACHUTE;
                    set_sprite(SPR_PARACHUTE, PAT_PARACHUTE, 0, 0, 0, 0);
                }
            }
        }
    } else if (paraOn) {
        paraOn = 0;
        set_sprite(SPR_PARACHUTE, PAT_PARACHUTE, 0, 0, 0, 0);
    }

    /* 1940 Bomber formation (Stage 1 only). New entries stop at the boss gate. */
    if (stage == 1) {
        if (!bomberOn) {
            if (enemiesKilled <= ENEMIES_TO_BOSS && bomberTimer > 0) {
                bomberTimer--;
                if (bomberTimer == 0) {
                    bomberOn = 1;
                    bomberHealth = 4;
                    /* CX16 ai.c aiEndFrame(): the bomber flies in from the side
                     * the player is heading towards, entering at the height of
                     * that launch ray +/- up to 2 rows. `frameCount & 1` was
                     * always 0 here (update_game only runs on even frames), so
                     * the bomber could only ever enter from the right. */
                    uint8_t hray = horizontalLaunchRay[facing];
                    bomberDir = (hray & 16) ? 1 : -1;
                    bomberX = (bomberDir > 0) ? -32 : 224;
                    bomberY = launchY[hray] + (int16_t)(((aiRandom() & 3) - 2) << 4);
                    bomberBoom = 0;
                    uint8_t bframe = (bomberDir > 0) ? 0 : 4;
                    set_sprite(SPR_BOMBER, PAT_BOMBER + (uint16_t)bframe * 512, (uint16_t)bomberX, (uint16_t)bomberY, 1, 0x60);
                }
            }
        } else {
            if (bomberBoom > 0) {
                bomberBoom--;
                if (bomberBoom == 0) {
                    bomberOn = 0;
                    bomberTimer = T_BOMBER;
                    set_sprite(SPR_BOMBER, PAT_BOMBER, 0, 0, 0, 0);
                } else {
                    uint8_t fi = (uint8_t)(bomberBoom >> 3);
                    if (fi > 3) fi = 3;
                    set_sprite(SPR_BOMBER, PAT_EXPL32 + (uint16_t)fi * 512, (uint16_t)bomberX, (uint16_t)bomberY, 1, 0x60);
                }
            } else {
                bomberX += scrollDx + bomberDir * 2;
                bomberY += scrollDy;
                if (bomberX < -48 || bomberX > 256 || bomberY < -20 || bomberY > 260) {
                    bomberOn = 0;
                    bomberTimer = T_BOMBER;
                    set_sprite(SPR_BOMBER, PAT_BOMBER, 0, 0, 0, 0);
                } else {
                    /* CX16 aiBomber: cycle 0..bossAnimFrames[health] while damaged. */
                    uint8_t dirOff = (bomberDir > 0) ? 0 : 4;
                    uint8_t damageFrame = 0;
                    if (bomberHealth < 4) {
                        uint8_t maxD = bossAnimFrames[bomberHealth];
                        damageFrame = (uint8_t)(maxD - ((frameCount >> 2) % (maxD + 1)));
                    }
                    set_sprite(SPR_BOMBER, PAT_BOMBER + (uint16_t)(dirOff + damageFrame) * 512,
                               (uint16_t)bomberX, (uint16_t)bomberY, 1, 0x60);
                    /* CX16 aiHorizontalFlyer: dumb-bullet spray, not era weapons */
                    if (!(frameCount & 31)) {
                        spawn_flyer_shot((uint16_t)(bomberX + 16), (uint16_t)(bomberY + 12));
                    }
                    /* CX16 collideBomber: chip = kill-chain silent; kill = 1500 + ENEMY_EXPLODE */
                    for (uint8_t bi = 0; bi < NUM_BULLETS; bi++) {
                        if (bulletOn[bi]) {
                            int16_t bx = (int16_t)bulletX[bi], by = (int16_t)bulletY[bi];
                            if (bx + 2 > bomberX && bx < bomberX + 32 &&
                                by + 2 > bomberY && by < bomberY + 16) {
                                bulletOn[bi] = 0;
                                hide_sprite(SPR_BULLET_BASE + bi);
                                if (bomberHealth > 0) bomberHealth--;
                                if (bomberHealth == 0) bomber_explode();
                                else add_chain_score();
                                break;
                            }
                        }
                    }
                    /* CX16 ram: collideBomber + collidePlayer — 1500 and you die. */
                    if (bomberBoom == 0 && playerBoom == 0 && playerDeadTimer == 0 &&
                        bomberX < (int16_t)playerX + 11 && bomberX + 32 > (int16_t)playerX + 5 &&
                        bomberY < (int16_t)playerY + 11 && bomberY + 16 > (int16_t)playerY + 5) {
                        bomber_explode();
                        lose_life();
                    }
                }
            }
        }
    } else if (bomberOn) {
        bomberOn = 0;
        bomberBoom = 0;
        bomberTimer = T_BOMBER;
        set_sprite(SPR_BOMBER, PAT_BOMBER, 0, 0, 0, 0);
    }

    /* CX16 aiScores: popup rides world scroll only, no independent float. */
    if (popupOn) {
        popupX += scrollDx;
        popupY += scrollDy;
        set_sprite(SPR_POPUP, PAT_NUMBERS + (uint16_t)popupFrame * 256, (uint16_t)popupX, (uint16_t)popupY, 1, 0x50);
        if (--popupTimer == 0) {
            popupOn = 0;
            set_sprite(SPR_POPUP, PAT_NUMBERS, 0, 0, 0, 0);
        }
    }

    /* Propeller & rotor animation via palette cycling in stages 0, 1, 2 */
    update_propeller();
}

/* ------------------------- HUD / screens ------------------------- */
/* When the player is hit, start the explosion and post-mortem drift.
 * Lives are intentionally NOT decremented here: they are decremented later
 * upon entering the stage announce / respawn screen, matching CX16 and arcade flow. */
static void lose_life(void) {
    if (playerBoom > 0 || playerDeadTimer > 0) return; /* already dead/exploding */
    playerBoom = T_BOOM32;
    bulletTimer = 0;                    /* CX16 collidePlayer() */
    playerDeadTimer = T_PLAYER_DIED - T_BOOM32;   /* post-mortem world review */
    audioPlaySource(AUDIO_BIG_EXPLOSION);
    /* Next chute is 1000. Miss also resets; only consecutive catches climb. */
    paraBonusStreak = 0;
    /* CX16 leaves chute/bomber in the world for the 3s death hold;
     * gameStageInit wipes them at READY. Do not hide here. */
}

/* Format a score right-aligned in a 7-column field (blank padded, "00" for 0). */
static char snum_buf[10];
static const char *format_score_right(uint32_t n) {
    if (n == 0) return "     00";
    for (int i = 6; i >= 0; i--) {
        snum_buf[i] = (char)('0' + (n % 10));
        n /= 10;
    }
    snum_buf[7] = 0;
    int first = 0;
    while (first < 6 && snum_buf[first] == '0') {
        snum_buf[first] = ' ';
        first++;
    }
    return snum_buf;
}
/* Right-side arcade status bar (CX16 LAYER_SCORES). Playfield = left 28 cols
 * (x 0..224); status bar = right 12 cols (28..39). Redrawn only when a value
 * actually changed — redrawing the whole bar every frame is a big 1MHz cost. */
static uint32_t hudScore;   /* last-drawn values, for change detection */
static uint8_t  hudLives, hudStage;
static uint16_t hudKilled;
static void draw_hud(void) {
    if (!g_hudDirty && score == hudScore && lives == hudLives &&
        stage == hudStage && enemiesKilled == hudKilled) {
        return;             /* nothing changed: skip the redraw */
    }
    g_hudDirty = 0;
    hudScore = score; hudLives = lives; hudStage = stage; hudKilled = enemiesKilled;

    /* HIGH SCORE + value: both strictly right-aligned to col 38 (matches CX16) */
    draw_text(1, 29, sHighScore, 1);
    draw_text(2, 32, format_score_right(highScore[0]), 9);

    /* 1-UP & 2-UP status and scores: strictly right-aligned to col 38 */
    uint32_t p1Score = (activePlayer == 0) ? score : players[0].score;
    uint8_t p1Color = (activePlayer == 0) ? 1 : 10;
    draw_text(4, 35, sOneUp, p1Color);
    draw_text(5, 32, format_score_right(p1Score), 9);

    uint8_t p2Color = (activePlayer == 1) ? 1 : 10;
    draw_text(7, 35, sTwoUp, p2Color);
    uint32_t p2Score = (activePlayer == 1) ? score : players[1].score;
    draw_text(8, 32, format_score_right((numPlayers == 2) ? p2Score : 0), 9);

    /* Item 1: Small 8x8 stage era craft icons at y = 128 (CX16 16*SROWH).
     * Shows (stage + 1) craft icons from right to left: 312 - si * 8 */
    for (uint8_t si = 0; si < NUM_STAGE_SPR; si++) {
        if (si <= stage) {
            uint16_t sx = (uint16_t)(312 - si * 8);
            set_sprite(SPR_STAGE_BASE + si, PAT_STAGE_ICON, sx, 128, 1, 0x00);
        } else {
            set_sprite(SPR_STAGE_BASE + si, PAT_STAGE_ICON, 0, 0, 0, 0);
        }
    }

    /* Item 2: Reserve fighter planes (pointing UP, 16x16) at y = 152 */
    for (uint8_t li = 0; li < NUM_LIFE_SPR; li++) {
        if (li + 1 < lives) {
            uint16_t lx = (uint16_t)(304 - li * 16);
            set_sprite(SPR_LIFE_BASE + li, PAT_PLAYER + 24 * 256, lx, 152, 1, 0x50);
        } else {
            set_sprite(SPR_LIFE_BASE + li, PAT_PLAYER, 0, 0, 0, 0);
        }
    }

    /* Item 3: Stage progress enemy planes (16x8) at y = 192 (matches cx16-2.jpg 6 planes).
     * Total 48 kills (8 kills per plane). Each plane is sliced into 8 equal parts (2px each)
     * moving from left to right as enemies are wiped out. */
    uint8_t activePlane = (uint8_t)(enemiesKilled >> 3);
    uint8_t subFr = (uint8_t)(enemiesKilled & 7);
    for (uint8_t pi = 0; pi < NUM_PROG_SPR; pi++) {
        uint16_t px = (uint16_t)(224 + pi * 16);
        if (pi < activePlane) {
            set_sprite(SPR_PROG_BASE + pi, PAT_PROG_ICON, 0, 0, 0, 0);
        } else if (pi == activePlane) {
            set_sprite(SPR_PROG_BASE + pi, PAT_PROG_ICON + (uint16_t)subFr * 128, px, 192, 1, 0x10);
        } else {
            set_sprite(SPR_PROG_BASE + pi, PAT_PROG_ICON, px, 192, 1, 0x10);
        }
    }
    if (cheatInfiniteLives) {
        draw_text(21, 32, "INFINITE", 2); /* GREEN "INFINITE" in status bar */
    } else {
        draw_text(21, 32, "        ", 0xF0);
    }
}

/* CX16 gameStageInit: announce does not tick these, so ANNOUNCE is baked in.
 * Full era intro → +5 s of play (bomber ~10 s, chute ~14 s). READY → +3 s. */
static void arm_chute_bomber_timers(uint8_t ready) {
    uint16_t extra = ready ? TICKS(180) : TICKS(300);
    paraTimer = T_PARACHUTE + extra;
    bomberTimer = T_BOMBER + extra;
    paraBonusStreak = 0;        /* death / new era: next catch is 1000 */
}

static void init_game(uint8_t players_mode) {
    uint8_t i;
    numPlayers = players_mode;
    activePlayer = 0;

    players[0].score = 0;
    players[0].lives = LIVES_MAX;
    players[0].stage = stage;
    players[0].enemiesKilled = 0;
    players[0].nextExtraLife = 10000;
    players[0].alive = 1;
    players[0].stageIntroState = 0;

    if (numPlayers == 2) {
        players[1].score = 0;
        players[1].lives = LIVES_MAX;
        players[1].stage = stage;
        players[1].enemiesKilled = 0;
        players[1].nextExtraLife = 10000;
        players[1].alive = 1;
        players[1].stageIntroState = 0;
    } else {
        players[1].alive = 0;
    }

    playerX = PLAYER_X0;
    playerY = PLAYER_Y0;
    facing = 8; /* Facing RIGHT (matches cx16-2.jpg and arcade original) */
    targetFacing = 8;
    spinDir = 0;
    spinKey = 0;
    score = 0;
    lives = LIVES_MAX;
    enemiesKilled = 0;
    nextExtraLife = 10000;
    killMultiplier = 1;
    killTimer = 0;
    bulletTimer = 0;
    bossOn = 0;
    bossBoom = 0;
    bossHp = BOSS_HP;
    bossTimer = T_BOSS;
    bossRam = 0;
    stageClearTimer = 0;
    stageIntroState = 0;
    announceT = 0;
    playerBoom = 0;
    playerDeadTimer = 0;
    lifeFrames = 0;
    numFollowersMax = 2;                /* CX16 globalsStageInit() */
    numTrackedMax = 2;
    numTracked = 0;
    numRockets = 0;
    launchSide = 0;
    set_stage_palette();
    for (i = 0; i < NUM_BULLETS; i++) { bulletOn[i] = 0; }
    for (i = 0; i < NUM_EBULLETS; i++) { ebOn[i] = 0; }
    for (i = 0; i < NUM_ENEMIES; i++) {
        enemyOn[i] = 0; enemyBoom[i] = 0; enemyWave[i] = 0;
        enemyXfrac[i] = 0; enemyYfrac[i] = 0;
        enemyShot[i] = 0xFF;
    }
    paraOn = 0;
    paraTimer = T_PARACHUTE;
    paraBonusStreak = 0;
    bomberOn = 0;
    bomberBoom = 0;
    bomberTimer = T_BOMBER;
    waveTimer = 0;
    popupOn = 0;
    popupTimer = 0;
    hide_all_sprites();
    set_sprite(SPR_PLAYER, PAT_PLAYER + (uint16_t)((facing - 8) & 31) * 256, playerX, playerY, 1, 0x50);
    reset_clouds();
    set_sprite(SPR_PARACHUTE, PAT_PARACHUTE, 0, 0, 0, 0);
    set_sprite(SPR_BOMBER, PAT_BOMBER, 0, 0, 0, 0);
    set_sprite(SPR_POPUP,  PAT_NUMBERS, 0, 0, 0, 0);
    g_hudDirty = 1;
    hudScore = 0xFFFFFFFF;
    hudLives = 0xFF;
    hudStage = 0xFF;
    hudKilled = 0xFFFF;
}

static void draw_controls_option(void) {
    draw_text(12, 3, sOptKeyboard, (useJoystick == 0) ? 2 : 10);
    draw_text(12, 15, sOptJoystick, (useJoystick == 1) ? 2 : 10);
}

static void draw_credits(void) {
    draw_text(19, 7, sKonami, 9);
    draw_text(21, 6, sVersion, 3);
    draw_text(22, 4, sWessels, 3);
    draw_text(24, 2, sIIveraVer, 7);
    draw_text(25, 7, sAnomixer, 7);
}

static void title_common(void) {
    set_black_palette();        /* Fullscreen arcade black background (matches cx16-1.jpg) */
    /* Show 3D TIME PILOT Logo sprites (y=16, centered) */
    set_sprite(SPR_LOGO_TIME,  PAT_LOGO_TIME,  48,  16, 1, 0x70);
    set_sprite(SPR_LOGO_PILOT, PAT_LOGO_PILOT, 120, 16, 1, 0x70);
    /* Right-side status bar icons (matches cx16-1.jpg: 3 reserve planes + stage icon) */
    set_sprite(SPR_STAGE_BASE, PAT_STAGE_ICON, 312, 128, 1, 0x00);
    for (uint8_t si = 1; si < NUM_STAGE_SPR; si++) {
        set_sprite(SPR_STAGE_BASE + si, PAT_STAGE_ICON, 0, 0, 0, 0);
    }
    for (uint8_t li = 0; li < 3; li++) {
        set_sprite(SPR_LIFE_BASE + li, PAT_PLAYER + 24 * 256, (uint16_t)(304 - li * 16), 152, 1, 0x50);
    }
    for (uint8_t pi = 0; pi < NUM_PROG_SPR; pi++) set_sprite(SPR_PROG_BASE + pi, PAT_ENEMY, 0, 0, 0, 0);
    /* Common copyright labels (CX16 + Apple II VERA credits). */
    draw_text(0, 12, sPlay, 7);
    draw_credits();
    /* Right-side score bar (matches cx16-1.jpg: HIGH SCORE + 1-UP only). */
    draw_text(1, 29, sHighScore, 1);
    draw_text(2, 32, format_score_right(highScore[0]), 9);
    draw_text(4, 35, sOneUp, 1);
    draw_text(5, 32, format_score_right(0), 9);
    draw_text(7, 29, "          ", 0);
    draw_text(8, 29, "          ", 0);
}

static void title_screen(void) {
    if (g_titleDrawn) return;
    g_titleDrawn = 1;
    title_common();
    /* Attract text (CX16 uiShowTitle). */
    draw_text(6, 4, sDeposit, 7);
    draw_text(8, 5, sTryGame, 1);
    draw_text(10, 4, sControlsK, 4);
    draw_controls_option();
    draw_text(14, 4, sBonus1, 7);
    draw_text(16, 4, sBonus2, 7);
}

static void game_over_screen(void) {
    set_black_palette();
    clear_playfield(0);
    hide_all_sprites();
    g_hudDirty = 1;
    draw_hud();
    draw_text(12, 10, (activePlayer == 1) ? sPlayer2 : sPlayer1, 9);      /* WHITE "PLAYER 1" or "PLAYER 2" */
    draw_text(17, 10, sGameOver, 1);     /* RED "GAME OVER" (row 17, col 10) */
}

/* Stage announce (CX16 uiShowPreGameLabels). Era name cycles WHITE/BLUE/RED. */
static const uint8_t stageLabelColor[3] = { 9, 3, 1 };
static void stage_announce(void) {
    static uint8_t introColorOffset;
    if (!g_annDrawn) {
        g_annDrawn = 1;
        introColorOffset = 2;           /* CX16 first colour is RED */
        draw_text(11, 10, (activePlayer == 1) ? sPlayer2 : sPlayer1, 9);
        if (stageIntroState) {
            draw_text(17, 11, sReady, 9);
        } else {
            char stagebuf[8];
            stagebuf[0] = 'S'; stagebuf[1] = 'T'; stagebuf[2] = 'A';
            stagebuf[3] = 'G'; stagebuf[4] = 'E'; stagebuf[5] = ' ';
            stagebuf[6] = (char)('1' + (stage & 0xF)); stagebuf[7] = 0;
            draw_text(21, 10, stagebuf, 9);
        }
    }
    if (!stageIntroState && !(frameCount & 7)) {
        draw_text(17, 9, eraLabel[stage], stageLabelColor[introColorOffset]);
        introColorOffset = introColorOffset ? (uint8_t)(introColorOffset - 1) : 2;
    }
}

static const int8_t joyDirAngles[16] = {
    -1,  0,  8,  4, 16, -1, 12, -1,
    24, 28, -1, -1, 20, -1, -1, -1
};

static void start_game_from_ui(uint8_t mode) {
    attractCycleCount = 0;
    isDemoMode = 0;

    stage = 0;
    upload_stage_art();
    set_stage_palette();

    /* Blue counter-clockwise radar sweep cleanly wipes title screen and 3D logo directly to blue sky! */
    screen_wipe_to_sky(0);

    waitvsync();
    audioServiceAudio();

    init_game(mode);
    paint_status_bar();
    g_hudDirty = 1;
    draw_hud();

    waitvsync();
    audioServiceAudio();
    VERA.display.video = 0x51; /* Clean atomic reveal at VSYNC */

    g_annDrawn = 0;
    isGameStartIntro = 1;
    announceT = 0;
    state = 4;
    titleClear = 1;
}

/* ------------------------- High-score table ------------------------- */
static int8_t  hs_row;          // row the new score landed in (-1 = not a high score)
static const uint8_t hsColor[5] = { 1, 5, 4, 2, 7 };  // red/orange/yellow/green/cyan (matches cx16-1.jpg)
static const char *hsRank[5]   = { "1ST", "2ND", "3RD", "4TH", "5TH" };

static void draw_initials(uint8_t row, uint8_t col, const char *s, uint8_t color) {
    for (int i = 0; i < 3; i++) {
        uint16_t off = (uint16_t)row * 128 + (uint16_t)(col + i) * 2;
        vera_set_addr(VERA_INC_BANK0, LAYER0_MAP + off);
        VERA.data0 = (uint8_t)s[i];
        VERA.data0 = color;
    }
}

/* CX16 uiShowHighScoreTable: ranking at row 6 col 5; ranks on 8,10,12,14,16. */
static void draw_hs_table(void) {
    uint8_t i;
    draw_text(6, 5, sRanking, 6);
    for (i = 0; i < NUM_HIGHSCORES; i++) {
        uint8_t y = (uint8_t)(8 + i * 2);
        draw_text(y, 5, hsRank[i], hsColor[i]);
        char sbuf[9];
        uint32_t ns = highScore[i];
        int b;
        sbuf[8] = 0;
        for (b = 7; b >= 0; b--) {
            sbuf[b] = (char)('0' + (ns % 10));
            ns /= 10;
        }
        b = 0;
        while (b < 7 && sbuf[b] == '0') {
            sbuf[b] = ' ';
            b++;
        }
        draw_text(y, 9, sbuf, hsColor[i]);
        draw_initials(y, 20, highScoreInitials[i], hsColor[i]);
    }
}

static int8_t hs_pending_player = -1;

/* CX16 uiGameOver after the wipe: logo, credits, ranking, prompt at (5,0). */
static void hs_show_entry_screen(void) {
    screen_wipe(0);
    set_sprite(SPR_LOGO_TIME,  PAT_LOGO_TIME,  48,  16, 1, 0x70);
    set_sprite(SPR_LOGO_PILOT, PAT_LOGO_PILOT, 120, 16, 1, 0x70);
    draw_credits();
    g_hudDirty = 1;
    draw_hud();
    draw_hs_table();
    draw_text(0, 5, sEnterInitials, 3);
}

/* Insert score val; return its rank row (0..4) or -1 if not a high score. */
static int8_t hs_insert_score(uint32_t val) {
    int8_t pos = -1;
    for (int8_t i = 0; i < NUM_HIGHSCORES; i++) {
        if (val >= highScore[i]) {
            pos = i;
            break;
        }
    }
    if (pos < 0) return -1;

    for (int8_t j = NUM_HIGHSCORES - 1; j > pos; j--) {
        highScore[j] = highScore[j - 1];
        highScoreInitials[j][0] = highScoreInitials[j - 1][0];
        highScoreInitials[j][1] = highScoreInitials[j - 1][1];
        highScoreInitials[j][2] = highScoreInitials[j - 1][2];
    }
    highScore[pos] = val;
    /* CX16 strcpy(TEXT_INITIALS[x], "A  "); */
    highScoreInitials[pos][0] = 'A';
    highScoreInitials[pos][1] = ' ';
    highScoreInitials[pos][2] = ' ';
    return pos;
}

static char hs_cycle_letter(char letter, int8_t dir) {
    uint8_t c = (uint8_t)letter;
    if (dir < 0) {
        c--;
        if (c < '.') c = 'Z';
        else if (c < 'A') c = '.';
    } else {
        c++;
        if (c < 'A') c = 'A';
        else if (c > 'Z') c = '.';
    }
    return (char)c;
}

static void hs_accept_letter(void) {
    highScoreInitials[hs_row][hs_char_idx] = hs_curr_char;
    if (++hs_char_idx < 3)
        highScoreInitials[hs_row][hs_char_idx] = hs_curr_char;
}

static uint8_t check_and_start_hs_entry(int8_t playerIdx) {
    uint32_t pscore = players[playerIdx].score;
    int8_t r = hs_insert_score(pscore);
    if (r >= 0) {
        hs_pending_player = playerIdx;
        hs_row = r;
        hs_char_idx = 0;
        hs_curr_char = 'A';
        hs_entry_timer = T_HS_ENTRY;
        hs_color_timer = T_HS_CYCLE;
        hs_initials_color = 9;
        hs_fire_held = 1;       /* ignore fire held in from game-over skip */
        hs_rep_timer = 0;
        hs_show_entry_screen();
        state = 2;
        titleClear = 0;
        audioPlaySource(AUDIO_HIGHSCORE);
        return 1;
    }
    return 0;
}

static void hs_entry_completed(void) {
    audioStopSource(AUDIO_HIGHSCORE);
    if (numPlayers == 2 && hs_pending_player == 0) {
        if (check_and_start_hs_entry(1)) {
            return;
        }
    }
    state = 0;
    titleClear = 1;
}

/* ------------------------- Keyboard ------------------------- */
static uint8_t key_pressed(void) {
    return (KBD_DATA & 0x80) != 0;
}
static unsigned char key_read(void) {
    unsigned char k = KBD_DATA & 0x7F;
    (void)KBD_STROBE;
    return k;
}
static unsigned char key_up(unsigned char k) {
    if (k >= 'a' && k <= 'z') return (unsigned char)(k - ('a' - 'A'));
    return k;
}

/* Apple II Native Paddle/Joystick Reading via Timer Discharge */
static uint8_t read_pdl(uint8_t pdl) {
    uint8_t count;
    __asm__ volatile(
        "ldx %1\n\t"
        "lda 0xC070\n\t"
        "ldy #0\n\t"
        "nop\n\t"
        "nop\n"
        "1:\n\t"
        "lda 0xC064,x\n\t"
        "bpl 2f\n\t"
        "iny\n\t"
        "bne 1b\n\t"
        "dey\n"
        "2:\n\t"
        "sty %0"
        : "=r"(count)
        : "r"(pdl)
        : "a", "x", "y"
    );
    return count;
}

static void update_player_steering(uint8_t k, unsigned char ku) {
    if (playerBoom > 0 || playerDeadTimer > 0) return;

    static uint8_t steerStall = 0;
    steerStall++;

    /* Q / E: toggle continuous spin at half the WASD/joystick turn rate.
     * Same key again stops; opposite key reverses. IIe auto-repeat of the
     * held key is ignored until $C010 any-key-down goes idle. */
    if (ku == 'Q' || ku == 'E') {
        if (spinKey != ku) {
            int8_t dir = (ku == 'E') ? 1 : -1;
            if (spinDir == dir) {
                spinDir = 0;
                targetFacing = (int8_t)facing;
            } else {
                spinDir = dir;
                targetFacing = -1;
            }
            spinKey = ku;
        }
    } else {
        if (ku == 'W' || k == 11) {
            spinDir = 0;
            targetFacing = 0;   /* UP */
        } else if (ku == 'D' || k == 21) {
            spinDir = 0;
            targetFacing = 8;   /* RIGHT */
        } else if (ku == 'S' || ku == 'X' || k == 10) {
            spinDir = 0;
            targetFacing = 16;  /* DOWN */
        } else if (ku == 'A' || k == 8) {
            spinDir = 0;
            targetFacing = 24;  /* LEFT */
        }
        /* k==0: no new strobe. Clear spinKey only once the key is actually up
         * (IIe $C010 bit 7 = any-key-down). Do not read $C010 when a key
         * event is in flight — that would eat the strobe. */
        if (k == 0 && spinKey && ((KBD_STROBE & 0x80) == 0))
            spinKey = 0;
    }

    if (useJoystick) {
        uint8_t jx = read_pdl(0);
        uint8_t jy = read_pdl(1);
        uint8_t jl = (jx < 85), jr = (jx > 170);
        uint8_t ju = (jy < 85), jd = (jy > 170);
        uint8_t mask = (ju ? 1 : 0) | (jr ? 2 : 0) | (jd ? 4 : 0) | (jl ? 8 : 0);
        int8_t target = joyDirAngles[mask];
        if (target >= 0) {
            spinDir = 0;
            targetFacing = target;
        }
    }

    if (spinDir) {
        if ((steerStall & 3) == 0)
            facing = (uint8_t)((facing + (spinDir > 0 ? 1 : 31)) & 31);
    } else if (targetFacing >= 0 && facing != (uint8_t)targetFacing) {
        if (steerStall & 1) {
            uint8_t diff = (uint8_t)(((uint8_t)targetFacing - facing) & 31);
            if (diff <= 16) {
                facing = (facing + 1)  & 31;
            } else {
                facing = (facing + 31) & 31;
            }
        }
    }

    set_sprite_pat(SPR_PLAYER, PAT_PLAYER + (uint16_t)((facing - 8) & 31) * 256);
}

/* Pause feature (CX16 uiPause parity): displays red "PAUSED" at Row 8, Col 11 */
static void ui_pause(void) {
    draw_text(8, 11, sPaused, 1); /* RED "PAUSED" centered at (col 11, row 8) */
    audioPlaySource(AUDIO_PICKUP);

    uint8_t debounce = 15;
    for (;;) {
        waitvsync();
        audioServiceAudio();
        if (debounce > 0) {
            debounce--;
            if (key_pressed()) (void)key_read();
            continue;
        }
        if (key_pressed()) {
            uint8_t k = key_read();
            unsigned char ku = key_up(k);
            if (ku == 'I') {
                cheatInfiniteLives ^= 1;
                g_hudDirty = 1;
                audioPlaySource(AUDIO_PICKUP);
                continue;
            }
            if (ku == 'P' || k == ' ' || ku == ' ' || k == 13 || k == 27) {
                break;
            }
        }
        if (useJoystick && (((*(volatile uint8_t *)0xC061 & 0x80) != 0) ||
                           ((*(volatile uint8_t *)0xC062 & 0x80) != 0))) {
            break;
        }
    }
    (void)KBD_STROBE;
    audioPlaySource(AUDIO_PICKUP);
    draw_text(8, 11, "      ", 0);
}

static void demo_autopilot(void) {
    if (playerBoom > 0 || playerDeadTimer > 0) return;

    int16_t targetX = -1, targetY = -1;
    uint16_t minDist = 0xFFFF;

    if (bomberOn && bomberBoom == 0) {
        targetX = (int16_t)bomberX + 16;
        targetY = (int16_t)bomberY + 8;
    } else if (paraOn) {
        targetX = paraX + 8;
        targetY = paraY + 8;
    } else {
        for (uint8_t i = 0; i < NUM_ENEMIES; i++) {
            if (enemyOn[i] && enemyBoom[i] == 0) {
                int16_t dx = (int16_t)enemyX[i] - (int16_t)playerX;
                int16_t dy = (int16_t)enemyY[i] - (int16_t)playerY;
                int16_t adx = (dx < 0) ? -dx : dx;
                int16_t ady = (dy < 0) ? -dy : dy;
                uint16_t d = (uint16_t)(adx + ady);
                if (d < minDist) {
                    minDist = d;
                    targetX = (int16_t)enemyX[i] + 8;
                    targetY = (int16_t)enemyY[i] + 8;
                }
            }
        }
    }

    if (targetX >= 0) {
        int16_t tdx = targetX - (int16_t)playerX;
        int16_t tdy = targetY - (int16_t)playerY;
        uint8_t targetAngle = frame_toward(tdx, tdy);
        if (targetAngle != 0xFF) {
            if (facing != targetAngle) {
                uint8_t diff = (uint8_t)((targetAngle - facing) & 31);
                if (diff & 16) {
                    facing = (facing + 31) & 31;
                } else {
                    facing = (facing + 1) & 31;
                }
                set_sprite_pat(SPR_PLAYER, PAT_PLAYER + (uint16_t)((facing - 8) & 31) * 256);
            }

            uint8_t aimDiff = (uint8_t)((targetAngle - facing) & 31);
            if (aimDiff <= 2 || aimDiff >= 30) {
                if (!(frameCount & 3)) {
                    request_fire();
                }
            }
        }
    } else {
        if (!(frameCount & 7)) {
            facing = (facing + 1) & 31;
            set_sprite_pat(SPR_PLAYER, PAT_PLAYER + (uint16_t)((facing - 8) & 31) * 256);
        }
        if (!(frameCount & 15)) {
            request_fire();
        }
    }
}

/* Returns 1 if VERA card responds at VERA_BASE ($C200), 0 if not present */
static uint8_t detect_vera(void) {
    VERA.control = 1;
    if (VERA.control != 1) return 0;
    VERA.control = 0;
    if (VERA.control != 0) return 0;
    vera_set_addr(VERA_INC_BANK0, 0x0000);
    VERA.data0 = 222;
    vera_set_addr(VERA_INC_BANK0, 0x0000);
    if (VERA.data0 != 222) return 0;
    vera_set_addr(VERA_INC_BANK0, 0x0000);
    VERA.data0 = 111;
    vera_set_addr(VERA_INC_BANK0, 0x0000);
    if (VERA.data0 != 111) return 0;
    return 1;
}

/* ------------------------- main ------------------------- */
int main(void) {
    if (!detect_vera()) {
        const char *msg = "ERROR: NO VERA CARD DETECTED IN SLOT 2!";
        for (uint8_t i = 0; msg[i]; i++) {
            ((volatile unsigned char *)0x0400)[i] = (unsigned char)(msg[i] | 0x80);
        }
        while (1) {}
    }

    VERA.control = 0x80;
    VERA.control = 0x00;
    VERA.display.video = 0x11;  /* Layer 0 ON, Sprites OFF during boot disk streaming */
    VERA.display.hscale = 0x40;
    VERA.display.vscale = 0x40;

    disk_init();          // MLI streaming window
    load_palette();
    load_font();
    setup_layer0();
    hide_all_sprites();         /* Zero out all 128 sprite attribute slots immediately */
    paint_screen();
    draw_text(14, 5, "INITIALIZING... PLEASE WAIT...", 9);
    setup_sprites();      // streams art from the HDV blob into VERA pattern RAM
    upload_pcm_to_vram(); // streams opening theme PCM into VRAM Bank 0
    audioInit();

    /* Pre-render complete title screen atomically before audio start & sprite reveal */
    paint_screen();
    title_screen();
    titleClear = 0;
    g_titleDrawn = 1;

    waitvsync();
    VERA.display.video = 0x51;  /* NOW enable sprites & reveal complete title screen atomically! */
    VERA.irq_flags = VERA_IRQ_VSYNC; /* Prime VSYNC flag for main loop lock */

    /* Play authentic coin drop sound right as title screen appears! */
    audioPlaySource(AUDIO_COINDROP);

    for (;;) {
        uint8_t k = 0;
        if (key_pressed()) {
            k = key_read();
        }
        unsigned char ku = key_up(k);
        frameCount++;

        switch (state) {
        case 0: /* Title & Attract Mode */
            {
                static uint16_t attractTimer = 360;
                static uint8_t  attractScreen = 0;
                if (titleClear) {
                    paint_screen();
                    titleClear = 0;
                    g_titleDrawn = 0;
                    attractTimer = 360;
                    attractScreen = 0;
                }
                if (!g_titleDrawn) {
                    if (attractScreen == 0) {
                        title_screen();
                    } else {
                        g_titleDrawn = 1;
                        title_common();
                        draw_hs_table();
                    }
                }
                if (--attractTimer == 0) {
                    attractTimer = 360;

                    if (attractScreen == 0) {
                        /* Control screen finished -> switch to Hi Score Table */
                        draw_text(0, 12, "    ", 0);
                        for (uint8_t r = 5; r <= 18; r++) draw_text(r, 0, "                            ", 0);
                        g_titleDrawn = 0;
                        attractScreen = 1;
                    } else {
                        /* Hi Score Table finished -> one complete Control -> Table cycle complete */
                        attractScreen = 0;
                        if (++attractCycleCount >= 3) {
                            /* Exactly 3 full cycles of Control -> Hi Score Table with no input: launch Demo!
                             * Stage 1 = A.D. 1940 (Green Sky, WWII dogfight) */
                            attractCycleCount = 0;
                            isDemoMode = 1;
                            demoIndex = 0;

                            stage = 1;                /* Stage 1 = A.D. 1940 (Green Sky) */
                            upload_stage_art();       /* Upload 1940 Bomber, weapons, green fighters, expl32 */

                            /* Green counter-clockwise radar sweep cleanly wipes title screen and 3D logo directly to green! */
                            screen_wipe_to_sky(1);

                            init_game(1);
                            paint_status_bar();
                            g_hudDirty = 1;
                            draw_hud();

                            waitvsync();
                            VERA.display.video = 0x51; /* Clean atomic reveal at VSYNC */

                            g_annDrawn = 0;
                            state = 1;
                            titleClear = 1;
                        } else {
                            /* Return to control screen */
                            draw_text(0, 12, "    ", 0);
                            for (uint8_t r = 5; r <= 18; r++) draw_text(r, 0, "                            ", 0);
                            g_titleDrawn = 0;
                        }
                    }
                }
                if (ku == 'I') {
                    cheatInfiniteLives ^= 1;
                    audioPlaySource(AUDIO_PICKUP);
                }
                if (ku == 'K') {
                    useJoystick = 0;
                    if (attractScreen == 0) draw_controls_option();
                    audioPlaySource(AUDIO_PICKUP);
                }
                if (ku == 'J') {
                    useJoystick = 1;
                    if (attractScreen == 0) draw_controls_option();
                    audioPlaySource(AUDIO_PICKUP);
                }
                uint8_t joyStart = useJoystick && (((*(volatile uint8_t *)0xC061 & 0x80) != 0) ||
                                                   ((*(volatile uint8_t *)0xC062 & 0x80) != 0));
                if (joyStart || ku == 'S' || k == '1' || k == '2' || k == ' ' || ku == ' ') {
                    start_game_from_ui((k == '2') ? 2 : 1);
                }
            }
            break;

        case 4: /* Stage announce (PLAYER 1 / A.D. yyyy / STAGE n or READY) */
            {
                if (announceT == 0) {
                    if (isGameStartIntro && !isDemoMode) {
                        audioPlaySource(AUDIO_GAME_START);
                    }
                    announceT = stageIntroState ? T_ANN_READY : T_ANN_STAGE;
                    arm_chute_bomber_timers(stageIntroState);
                    isGameStartIntro = 0;
                    g_hudDirty = 1;
                }
                stage_announce();
                draw_hud();
                /* Allow player to steer the plane freely */
                if (!isDemoMode) {
                    if (ku == 'P') {
                        ui_pause();
                        break;
                    }
                    if (ku == 'K') useJoystick = 0;
                    if (ku == 'J') useJoystick = 1;
                    update_player_steering(k, ku);
                } else {
                    demo_autopilot();
                }

                /* Move clouds and animate propeller during stage announce */
                if (!(frameCount & 1)) {
                    int16_t scrollDx = -(int16_t)velDx[facing];
                    int16_t scrollDy = -(int16_t)velDy[facing];
                    update_clouds(scrollDx, scrollDy);
                }
                update_propeller();

                if (--announceT == 0) {
                    /* Erase announce text only (sky and clouds remain undisturbed) */
                    draw_text(11, 10, "        ", 0);
                    draw_text(17, 9, "         ", 0);
                    draw_text(21, 10, "       ", 0);
                    stageIntroState = 1;
                    state = 1;
                }
            }
            break;

        case 1: /* Playing */
            if (isDemoMode) {
                /* Flashing DEMO PLAY banner at row 0, col 9 */
                draw_text(0, 9, "DEMO PLAY", ((frameCount >> 3) & 1) ? 4 : 1);

                /* Any key or joystick button aborts demo and returns to title screen! */
                uint8_t joyBtn = useJoystick && (((*(volatile uint8_t *)0xC061 & 0x80) != 0) ||
                                                 ((*(volatile uint8_t *)0xC062 & 0x80) != 0));
                if (k != 0 || joyBtn) {
                    isDemoMode = 0;
                    draw_text(0, 9, "         ", 0);
                    hide_all_sprites();
                    stage = 0;
                    upload_stage_art();
                    screen_wipe_to_sky(0);    /* Blue counter-clockwise radar sweep! */
                    set_black_palette();
                    paint_screen();
                    state = 0;
                    titleClear = 1;
                    attractCycleCount = 0;
                    break;
                }

                /* Active Dogfight AI autopilot */
                demo_autopilot();

                /* Demo plays for ~30 seconds (1800 frames), then returns to title */
                if (++demoIndex >= 1800) {
                    isDemoMode = 0;
                    draw_text(0, 9, "         ", 0);
                    hide_all_sprites();
                    stage = 0;
                    upload_stage_art();
                    screen_wipe_to_sky(0);    /* Blue counter-clockwise radar sweep! */
                    set_black_palette();
                    paint_screen();
                    state = 0;
                    titleClear = 1;
                    attractCycleCount = 0;
                    break;
                }
            } else {
                if (ku == 'P') {
                    ui_pause();
                    break;
                }
                if (ku == 'I') {
                    cheatInfiniteLives ^= 1;
                    g_hudDirty = 1;
                    audioPlaySource(AUDIO_PICKUP);
                }
                if (ku == 'K') {
                    useJoystick = 0;
                    audioPlaySource(AUDIO_PICKUP);
                }
                if (ku == 'J') {
                    useJoystick = 1;
                    audioPlaySource(AUDIO_PICKUP);
                }
                if (playerBoom == 0 && playerDeadTimer == 0 && stageClearTimer == 0) {
                    update_player_steering(k, ku);
                    if (k == ' ' || ku == ' ' || k == '1') request_fire();
                    if (useJoystick && (((*(volatile uint8_t *)0xC061 & 0x80) != 0) ||
                                       ((*(volatile uint8_t *)0xC062 & 0x80) != 0))) {
                        request_fire();
                    }
                } else if (stageClearTimer > 0 && playerBoom == 0) {
                    /* CX16 still lets you steer during the 3s fireworks. */
                    update_player_steering(k, ku);
                }
            }
            if (!(frameCount & 1)) {
                update_game();
            }
            draw_hud();
            break;

        case 3: /* Game over (matches CX16 uiGameOver) */
            {
                static uint16_t goTimer = 180;
                if (titleClear) {
                    goTimer = 180;
                    titleClear = 0;
                    game_over_screen();
                }
                if (--goTimer == 0 || (goTimer < 120 && (ku == 'S' || k == '1' || k == ' ' || ku == ' '))) {
                    if (check_and_start_hs_entry(0)) {
                        /* 1P entered initials screen */
                    } else if (numPlayers == 2 && check_and_start_hs_entry(1)) {
                        /* 1P didn't qualify, but 2P qualified and entered initials screen */
                    } else {
                        state = 0;
                        titleClear = 1;
                    }
                }
            }
            break;

        case 2: /* High-score interactive initials entry (CX16 uiGameOver) */
            {
                uint8_t y = (uint8_t)(8 + hs_row * 2);
                uint8_t accepted = 0;
                int8_t cycle = 0;
                uint8_t fire = 0;
                char keych = 0;

                if (k == '.' || ku == '.') {
                    keych = '.';
                } else if (ku >= 'A' && ku <= 'Z') {
                    keych = (char)ku;
                }
                if (keych) {
                    hs_curr_char = keych;
                    hs_accept_letter();
                    accepted = 1;
                } else {
                    if (k == ' ' || ku == ' ' || k == 13 || k == '1')
                        fire = 1;
                    if (k == 8) cycle = -1;
                    else if (k == 21) cycle = 1;
                    if (useJoystick) {
                        uint8_t jx = read_pdl(0);
                        if (jx < 85) cycle = -1;
                        else if (jx > 170) cycle = 1;
                        if (((*(volatile uint8_t *)0xC061 & 0x80) != 0) ||
                            ((*(volatile uint8_t *)0xC062 & 0x80) != 0))
                            fire = 1;
                    }
                    if (fire && !hs_fire_held) {
                        hs_accept_letter();
                        accepted = 1;
                    } else if (cycle) {
                        if (!hs_rep_timer) {
                            hs_curr_char = hs_cycle_letter(hs_curr_char, cycle);
                            highScoreInitials[hs_row][hs_char_idx] = hs_curr_char;
                            hs_rep_timer = T_HS_CYCLE;
                        }
                    } else {
                        hs_rep_timer = 0;
                    }
                    if (hs_rep_timer) hs_rep_timer--;
                }
                hs_fire_held = fire || (keych != 0);

                if (accepted) {
                    draw_initials(y, 20, highScoreInitials[hs_row], hsColor[hs_row]);
                    if (hs_char_idx >= 3) {
                        hs_entry_completed();
                        break;
                    }
                }

                /* CX16: current glyph colour-cycles; whole name redrawn in rank colour. */
                {
                    char ch[2];
                    ch[0] = hs_curr_char;
                    ch[1] = 0;
                    draw_text(y, (uint8_t)(20 + hs_char_idx), ch, hs_initials_color);
                }
                if (--hs_color_timer == 0) {
                    hs_color_timer = T_HS_CYCLE;
                    hs_initials_color = (uint8_t)((9 ^ hs_initials_color) | 1);
                    draw_initials(y, 20, highScoreInitials[hs_row], hsColor[hs_row]);
                    if (--hs_entry_timer == 0) {
                        highScoreInitials[hs_row][hs_char_idx] = hs_curr_char;
                        draw_initials(y, 20, highScoreInitials[hs_row], hsColor[hs_row]);
                        hs_entry_completed();
                    }
                }
            }
            break;
        }

        waitvsync();            /* lock to 60 Hz — wait BEFORE audio so jitter doesn't skip vsync */
        audioServiceAudio();   /* stream PCM from disk into VERA FIFO during the blanking interval */
    }
    return 0;
}
