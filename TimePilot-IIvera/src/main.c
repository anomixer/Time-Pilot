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
// Controls:  A/left = rotate CCW, D/right = rotate CW
//            WASD + keypad 8/4/6/2 + 7/9/1/3 snap the heading
//            SPACE / 1 = fire
// Build:  mos-apple2e-clang -Os -o build/main.bin src/main.c
//-----------------------------------------------------------------------------
#include <stdint.h>
#include "apple2e.h"
#include "art_table.h"
#include "audio.h"
#include "audio_table.h"
#include "disk.h"
#include "demo_data.h"

/* Bank 1 layout: 0xF000 font, 0x8000.. sprite patterns, 0xFA00 palette, 0xFC00 sprites */
#define TILE_BASE      0xF000   // font tiles (128 glyphs, 1KB) bank 1
#define PALETTE_ADDR   0xFA00   // 256-color palette bank 1
#define SPRITE_ATTR    0xFC00   // sprite attribute table bank 1
#define LAYER0_MAP     0x0000   // layer 0 tilemap bank 0

/* Sprite patterns in bank 1. */
#define PAT_PLAYER     0x8000   // 32 frames x16x16 = 8192 bytes
#define PAT_ENEMY      0xA000   // 16 frames x16x16 = 4096 bytes
#define PAT_BOSS       0xB000   // 16 frames x16x16 = 4096 bytes
#define PAT_EXPL       0xC000   // 4 x 16x16 = 1024 bytes
#define PAT_BULLET     0xC400   // 8x8 = 64 bytes
#define PAT_EBULLET    0xC440   // 8x8 = 64 bytes
#define PAT_CLOUD0     0xC480   // 16x16 = 256 bytes (cloud0 / astro0)
#define PAT_CLOUD1     0xC580   // 32x16 = 512 bytes (cloud1 / astro1)
#define PAT_CLOUD2     0xC780   // 64x16 = 1024 bytes (cloud2 / astro2)
#define PAT_STAGE_ICON 0xCB80   // 8x8 = 64 bytes (stage.png)
#define PAT_PARACHUTE  0xCC00   // 4 frames x 16x16 = 1024 bytes
#define PAT_WEAPON     0xD000   // 2048 bytes (bomb/boomerang/rocket/sbullet)
#define PAT_BOMBER     0xD800   // 4096 bytes (32x16 x 8 frames for 1940: 0xD800..0xE7FF)
#define PAT_LOGO_TIME  0x2000   // 64x16 = 1024 bytes (Title: Bank 1 low RAM $12000..$123FF)
#define PAT_LOGO_PILOT 0x2400   // 64x16 = 1024 bytes (Title: Bank 1 low RAM $12400..$127FF)
#define PAT_EXPL32     0xE800   // 4 frames x 32x16 = 2048 bytes
#define PAT_NUMBERS    0x1000   // 6 frames x 16x16 = 1536 bytes (0x1000..0x15FF in Bank 1 low RAM)
#define PAT_PROG_ICON  0x1800   // 8 frames x 16x8 = 1024 bytes (0x1800..0x1BFF in Bank 1 low RAM)

#ifndef VERA_INC_1
#define VERA_INC_1     (((1 << 1) | 0) << 3)
#endif
#ifndef VERA_INC_0
#define VERA_INC_0     (((0 << 1) | 0) << 3)
#endif

#define VERA_INC_BANK1  (VERA_INC_1 | 1)       // INC_1 + bank 1
#define VERA_INC_BANK0  (VERA_INC_1)           // INC_1 + bank 0

/* Sprite pool (VERA supports 128). */
#define SPR_PLAYER      0
#define SPR_CLOUD_BASE  1        // 1..8 (8 clouds: 2 small, 4 medium, 2 large)
#define NUM_CLOUDS      8
#define SPR_BULLET_BASE 9        // 9..15
#define NUM_BULLETS     7
#define SPR_EBULLET_BASE 16      // 16..23
#define NUM_EBULLETS    8
#define SPR_ENEMY_BASE  24       // 24..31
#define NUM_ENEMIES     8
#define SPR_BOSS        32
#define SPR_PARACHUTE   33       // rescue pilot pickup
#define SPR_BOMBER      34       // 1940 bomber formation
#define SPR_POPUP       35       // floating score popup
#define SPR_LOGO_TIME   34       // Title screen reuse
#define SPR_LOGO_PILOT  35       // Title screen reuse
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
#define BOSS_HP         5
#define NUM_STAGES      5          /* 1910 / 1940 / 1970 / 1982 / 2001 (CX16 order) */

/* 32-direction movement vectors (clockwise from up), magnitude ~2 px (matches arcade/CX16 pacing). */
static const int8_t velDx[32] = {0,0,1,1,1,2,2,2,2,2,2,2,1,1,1,0,0,0,-1,-1,-1,-2,-2,-2,-2,-2,-2,-2,-1,-1,-1,0};
static const int8_t velDy[32] = {-2,-2,-2,-2,-1,-1,-1,0,0,0,1,1,1,2,2,2,2,2,2,2,1,1,1,0,0,0,-1,-1,-1,-2,-2,-2};

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
static const char sStageClear[]  = "STAGE CLEAR";
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
static uint16_t bulletX[NUM_BULLETS], bulletY[NUM_BULLETS];
static int8_t   bulletVX[NUM_BULLETS], bulletVY[NUM_BULLETS];
static uint8_t  bulletOn[NUM_BULLETS];
static uint16_t ebX[NUM_EBULLETS], ebY[NUM_EBULLETS];
static int8_t   ebVX[NUM_EBULLETS], ebVY[NUM_EBULLETS];
static uint8_t  ebOn[NUM_EBULLETS];
static int16_t  enemyX[NUM_ENEMIES], enemyY[NUM_ENEMIES];
static uint8_t  enemyFacing[NUM_ENEMIES];
static uint8_t  enemyOffscreen[NUM_ENEMIES];
static uint16_t enemyTimer[NUM_ENEMIES];
static uint8_t  enemyOn[NUM_ENEMIES];
static uint8_t  enemyFrame[NUM_ENEMIES];
static uint8_t  enemyBoom[NUM_ENEMIES];
static uint8_t  enemyWave[NUM_ENEMIES];        /* 1 if member of 4-plane wave */
static uint8_t  waveEnemiesAlive;             /* remaining count of current wave */
static uint16_t waveTimer;
static int16_t  cloudX[NUM_CLOUDS], cloudY[NUM_CLOUDS];
static const uint8_t cloudType[NUM_CLOUDS] = { 0, 1, 2, 1, 0, 1, 2, 1 };
static const int16_t cloudInitX[NUM_CLOUDS] = { 128, -8, 160, 32, 0, 112, 32, 160 };
static const int16_t cloudInitY[NUM_CLOUDS] = { 216, 212, 193, 146, 77, 72, 54, 0 };
static uint8_t  bossOn, bossHp, bossFire, bossBoom;
static uint16_t bossXpos;
static int16_t  bossVX;
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
static uint16_t stageClearTimer;   /* >0: showing STAGE CLEAR banner */
static uint16_t frameCount;        /* free-running, for blink/animation */
static uint8_t  g_titleDrawn;      /* title screen already drawn this visit */
static uint8_t  g_hudDirty;        /* force HUD redraw on next draw_hud() */
static uint8_t  g_annDrawn;        /* stage announce already drawn this visit */
static uint8_t  state = 0;         /* 0=title  1=playing  2=highscore entry  3=gameover  4=announce */
static uint8_t  titleClear = 1;
static uint8_t  cheatInfiniteLives = 0; /* 'C' key toggle: infinite fighters */
static uint8_t  playerInvuln = 0;       /* Invulnerability countdown (frames) */

/* Propeller animation via palette cycling */
static const uint16_t colorPaletteProps[3] = { 0x0680, 0x00C0, 0x0FFF };
static const uint16_t colorPaletteSky[5]   = { 0x0006, 0x0052, 0x0063, 0x0505, 0x0000 };
static uint8_t  propState = 0;

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

static void lose_life(void);
static void draw_hud(void);
static void game_over_screen(void);
static void check_extra_life(void);
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
    VERA.data0 = c & 0xff;
    VERA.data0 = c >> 8;
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
    uint16_t row, col;
    for (row = 0; row < 32; row++) {
        for (col = 0; col < 28; col++) {
            uint16_t map_off = row * 128 + col * 2;
            vera_set_addr(VERA_INC_BANK0, LAYER0_MAP + map_off);
            VERA.data0 = 32;
            VERA.data0 = (uint8_t)((color & 0x0F) << 4);
        }
    }
}

/* Paint the right-hand status bar (cols 28..39) BLACK. */
static void paint_status_bar(void) {
    uint16_t row, col;
    for (row = 0; row < 32; row++) {
        for (col = 28; col < 64; col++) {
            uint16_t map_off = row * 128 + col * 2;
            vera_set_addr(VERA_INC_BANK0, LAYER0_MAP + map_off);
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

static void screen_wipe(uint8_t color) {
    int8_t counter;
    uint8_t cx = 13, cy = 15; /* center of 28x30 playfield */

    /* 1. Top edge: center to top-left (counter-clockwise) */
    for (counter = cx; counter >= 0; counter--) {
        screen_draw_line(cx, cy, (uint8_t)counter, 0, color);
        if (!(counter & 1)) { waitvsync(); audioServiceAudio(); }
    }
    /* 2. Left edge: top-left to bottom-left */
    for (counter = 1; counter < 30; counter++) {
        screen_draw_line(cx, cy, 0, (uint8_t)counter, color);
        if (!(counter & 1)) { waitvsync(); audioServiceAudio(); }
    }
    /* 3. Bottom edge: bottom-left to bottom-right */
    for (counter = 1; counter < 28; counter++) {
        screen_draw_line(cx, cy, (uint8_t)counter, 29, color);
        if (!(counter & 1)) { waitvsync(); audioServiceAudio(); }
    }
    /* 4. Right edge: bottom-right to top-right */
    for (counter = 28; counter >= 0; counter--) {
        screen_draw_line(cx, cy, 27, (uint8_t)counter, color);
        if (!(counter & 1)) { waitvsync(); audioServiceAudio(); }
    }
    /* 5. Top edge: top-right back to center */
    for (counter = 26; counter > cx; counter--) {
        screen_draw_line(cx, cy, (uint8_t)counter, 0, color);
        if (!(counter & 1)) { waitvsync(); audioServiceAudio(); }
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

    /* Commit new sky color to palette 0 and restore palette 14 */
    vera_set_addr(VERA_INC_BANK1, (uint16_t)(PALETTE_ADDR + 0 * 2));
    VERA.data0 = (uint8_t)(new_sky & 0xFF);
    VERA.data0 = (uint8_t)(new_sky >> 8);

    vera_set_addr(VERA_INC_BANK1, (uint16_t)(PALETTE_ADDR + 14 * 2));
    VERA.data0 = (uint8_t)(pal[14] & 0xFF);
    VERA.data0 = (uint8_t)(pal[14] >> 8);

    /* Reset playfield to standard spaces */
    clear_playfield(0);
}

static void draw_number(uint8_t row, uint8_t col, uint32_t n, uint8_t color) {
    char buf[10];
    for (int i = 7; i >= 0; i--) {
        buf[i] = (char)('0' + (n % 10));
        n /= 10;
    }
    buf[8] = 0;
    int first = 0;
    while (first < 7 && buf[first] == '0') {
        buf[first] = ' ';
        first++;
    }
    draw_text(row, col, buf, color);
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

static void upload_stream_frames(uint16_t pat_base, uint32_t art_off, uint8_t count, uint16_t fsz) {
    while (count--) {
        upload_pattern_stream(pat_base, art_off, fsz);
        pat_base += fsz;
        art_off += fsz;
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

    /* Lock player plane horizontally facing right in center of playfield */
    facing = 8;
    playerX = PLAYER_X0;
    playerY = PLAYER_Y0;
    set_sprite(SPR_PLAYER, PAT_PLAYER, playerX, playerY, 1, 0x50);

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
        uint8_t t = cloudType[i];
        set_sprite(SPR_CLOUD_BASE + i, get_cloud_pat(t), (uint16_t)cloudX[i], (uint16_t)cloudY[i], 1, get_cloud_dims(t));
    }
}

static void update_clouds(int16_t scrollDx, int16_t scrollDy) {
    uint8_t i;
    for (i = 0; i < NUM_CLOUDS; i++) {
        uint8_t t = cloudType[i];
        int16_t spd = (t == 0) ? 1 : (t == 1) ? 2 : 3;
        int16_t w = (t == 0) ? 16 : (t == 1) ? 32 : 64;
        int16_t spanX = (int16_t)(224 + w);
        int16_t cx = (int16_t)cloudX[i] + (scrollDx * spd) / 2;
        int16_t cy = (int16_t)cloudY[i] + (scrollDy * spd) / 2;
        if (cx < -w)       cx += spanX;
        else if (cx > 224) cx -= spanX;
        if (cy < -16)      cy += 256;
        else if (cy > 240) cy -= 256;
        cloudX[i] = cx;
        cloudY[i] = cy;
        move_sprite(SPR_CLOUD_BASE + i, (uint16_t)cx, (uint16_t)cy);
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
    }
}

/* ------------------------- Setup ------------------------- */
static const uint32_t enemyArtOff[5] = {
    ART_ENEMY0_FRAMES_OFF, ART_ENEMY1_FRAMES_OFF, ART_ENEMY2_FRAMES_OFF,
    ART_ENEMY3_FRAMES_OFF, ART_ENEMY4_FRAMES_OFF
};
static const uint16_t enemyArtLen[5] = {
    ART_ENEMY0_FRAMES_LEN, ART_ENEMY1_FRAMES_LEN, ART_ENEMY2_FRAMES_LEN,
    ART_ENEMY3_FRAMES_LEN, ART_ENEMY4_FRAMES_LEN
};
static const uint32_t bossArtOff[5] = {
    ART_BOSS0_FRAMES_OFF, ART_BOSS1_FRAMES_OFF, ART_BOSS2_FRAMES_OFF,
    ART_BOSS3_FRAMES_OFF, ART_BOSS4_FRAMES_OFF
};
static const uint32_t wepArtOff[5] = {
    0, ART_BOMB_FRAMES_OFF, ART_BOOMERANG_FRAMES_OFF, ART_ROCKET_FRAMES_OFF, ART_SBULLET_FRAMES_OFF
};
static const uint16_t wepArtLen[5] = {
    0, 512, 2048, 4096, 2048
};

static void upload_stage_art(void) {
    upload_pattern_stream(PAT_ENEMY, enemyArtOff[stage], enemyArtLen[stage]);
    upload_pattern_stream(PAT_BOSS, bossArtOff[stage], (stage == 4) ? 1024 : 4096);

    static int8_t lastCloudEra = -1;
    uint8_t cloudEra = (stage == 4) ? 4 : 0;
    if (lastCloudEra != (int8_t)cloudEra) {
        lastCloudEra = (int8_t)cloudEra;
        upload_pattern_stream(PAT_CLOUD0, (stage == 4) ? ART_ASTRO0_FRAMES_OFF : ART_CLOUD0_FRAMES_OFF, 256);
        upload_pattern_stream(PAT_CLOUD1, (stage == 4) ? ART_ASTRO1_FRAMES_OFF : ART_CLOUD1_FRAMES_OFF, (stage == 4) ? 256 : 512);
        upload_pattern_stream(PAT_CLOUD2, (stage == 4) ? ART_ASTRO2_FRAMES_OFF : ART_CLOUD2_FRAMES_OFF, (stage == 4) ? 512 : 1024);
    }

    upload_pattern_stream(PAT_EXPL32, ART_EXPL32X16_FRAMES_OFF, 2048);
    upload_pattern_stream(PAT_NUMBERS, ART_NUMBER_FRAMES_OFF, 1536);

    if (wepArtLen[stage]) {
        upload_pattern_stream(PAT_WEAPON, wepArtOff[stage], wepArtLen[stage]);
    }
    if (stage == 1) {
        upload_pattern_stream(PAT_BOMBER, ART_L1BOMBER_FRAMES_OFF, 4096);
    }
}

static void setup_sprites(void) {
    upload_stream_frames(PAT_PLAYER, ART_PLAYER_FRAMES_OFF, 32, 256);
    upload_stage_art();
    upload_stream_frames(PAT_EXPL, ART_EXPL_FRAMES_OFF, 4, 256);
    upload_pattern_stream(PAT_BULLET,  ART_BULLET_FRAMES_OFF,  64);
    upload_pattern_stream(PAT_EBULLET, ART_EBULLET_FRAMES_OFF, 64);
    upload_stream_frames(PAT_PARACHUTE, ART_PARACHUTE_FRAMES_OFF, 4, 256);
    upload_pattern_stream(PAT_LOGO_TIME,  ART_LOGO_TIME_FRAMES_OFF,  1024);
    upload_pattern_stream(PAT_LOGO_PILOT, ART_LOGO_PILOT_FRAMES_OFF, 1024);
    upload_pattern_stream(PAT_STAGE_ICON, ART_STAGE_FRAMES_OFF,    64);
    /* Pre-generate 8 progressive slice frames (cutting 2px per subkill from left to right).
     * ART_PROGRESS_FRAMES_OFF (0x99C0) crosses a 512-byte block boundary (bytes 0..63
     * in block 76, bytes 64..127 in block 77). Buffer the entire 128-byte sprite across
     * both blocks so the lower half (Row 4..7) isn't truncated to zeros! */
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
    /* 1. Stream Bank 0 audio blob ($1000..$FE58) */
    vera_set_addr(VERA_INC_BANK0, VRAM_AUDIO_BASE);
    uint32_t off = 0;
    while (off < PCM_BANK0_BYTES) {
        uint8_t *chunk = disk_ensure(PCM_START_BLOCK, PCM_TOTAL_BYTES, off);
        uint16_t in_blk = 512 - (uint16_t)(off & 511);
        uint16_t rem = (uint16_t)(PCM_BANK0_BYTES - off);
        uint16_t n = (rem < in_blk) ? rem : in_blk;
        for (uint16_t i = 0; i < n; i++) {
            VERA.data0 = chunk[i];
        }
        off += n;
    }

    /* 2. Stream Bank 1 audio blob ($2800..$7616) */
    vera_set_addr(VERA_INC_BANK1, VRAM_AUDIO_BANK1_BASE);
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

static void spawn_enemy(void) {
    uint8_t i;
    for (i = 0; i < NUM_ENEMIES; i++) {
        if (!enemyOn[i]) {
            enemyOn[i] = 1;
            enemyWave[i] = 0;
            /* Spawn ahead of the player's flight direction (+/- 3 directions jitter) */
            int8_t jitter = (int8_t)(((frameCount * 5 + i * 7) & 7) - 3);
            uint8_t a = (uint8_t)((facing + jitter) & 31);
            enemyX[i] = launchX[a];
            enemyY[i] = launchY[a];
            /* Enemy initially flies directly toward player (opposite to launch angle) */
            uint8_t target = (uint8_t)((a + 16) & 31);
            enemyFacing[i] = target;
            enemyBoom[i] = 0;
            enemyOffscreen[i] = 0;
            enemyTimer[i] = (uint16_t)(30 + ((i * 13) % 25));
            set_sprite(SPR_ENEMY_BASE + i, get_enemy_pat(i), (uint16_t)enemyX[i], (uint16_t)enemyY[i], 1, 0x50);
            return;
        }
    }
}

static void spawn_wave(void) {
    uint8_t count = 0, i;
    uint8_t a = facing; /* launch from ahead of player */
    audioPlaySource(AUDIO_WAVE_START);
    waveEnemiesAlive = 0;
    for (i = 0; i < NUM_ENEMIES; i++) {
        if (!enemyOn[i]) {
            enemyOn[i] = 1;
            enemyWave[i] = 1;
            int16_t ox = (int16_t)((count - 1) * 16);
            enemyX[i] = launchX[a] + ox;
            enemyY[i] = launchY[a];
            enemyFacing[i] = (uint8_t)((a + 16) & 31);
            enemyBoom[i] = 0;
            enemyOffscreen[i] = 0;
            enemyTimer[i] = (uint16_t)(25 + count * 8);
            set_sprite(SPR_ENEMY_BASE + i, get_enemy_pat(i), (uint16_t)enemyX[i], (uint16_t)enemyY[i], 1, 0x50);
            count++;
            waveEnemiesAlive++;
            if (count >= 4) break;
        }
    }
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

static void spawn_ebullet(uint16_t x, uint16_t y, uint8_t speed) {
    uint8_t i;
    int16_t dx = (int16_t)playerX - (int16_t)x;
    int16_t dy = (int16_t)playerY - (int16_t)y;
    for (i = 0; i < NUM_EBULLETS; i++) {
        if (!ebOn[i]) {
            ebOn[i] = 1;
            ebX[i] = x; ebY[i] = y;
            ebVX[i] = (int8_t)((dx > 0) ? speed : (dx < 0) ? -speed : 0);
            ebVY[i] = (int8_t)((dy > 0) ? speed : (dy < 0) ? -speed : 0);
            uint8_t dims = (stage == 0) ? 0 : 0x50;
            uint16_t pat = (stage == 0) ? PAT_EBULLET : PAT_WEAPON;
            set_sprite(SPR_EBULLET_BASE + i, pat, x, y, 1, dims);
            if (speed == 1) {
                audioPlaySource(AUDIO_BOMB);
            } else if (stage >= 2) {
                audioPlaySource(AUDIO_ROCKET_LAUNCH);
            } else {
                audioPlaySource(AUDIO_ENEMY_SHOOT);
            }
            return;
        }
    }
}

static void spawn_boss(void) {
    if (!bossOn) {
        bossOn = 1;
        bossHp = BOSS_HP;
        bossFire = 30;
        bossBoom = 0;
        bossXpos = 104;
        bossVX = 3;
        set_sprite(SPR_BOSS, PAT_BOSS, 104, 60, 1, 0x60);   /* 32x16 huge boss! */
        audioPlaySource((int8_t)(AUDIO_BOSSL0 + (stage & 3)));   // looping boss theme
    }
}

/* Blazing-fast octant direction solver (0..31, 0=UP, 8=RIGHT, 16=DOWN, 24=LEFT).
 * Replaces 32-iteration loop & 64 16-bit multiplications with instant comparisons. */
static uint8_t frame_toward(int16_t dx, int16_t dy) {
    if (dx == 0 && dy == 0) return 0;
    int16_t ax = (dx < 0) ? -dx : dx;
    int16_t ay = (dy < 0) ? -dy : dy;
    while (ax > 240 || ay > 240) {
        ax >>= 1;
        ay >>= 1;
    }
    uint8_t oct;
    if (ay >= ax) {
        /* North / South dominant: octants 0, 3, 4, 7 */
        uint16_t num = (uint16_t)ax << 8;
        uint8_t step = (num < (uint16_t)ay * 25) ? 0 :
                       (num < (uint16_t)ay * 78) ? 1 :
                       (num < (uint16_t)ay * 137) ? 2 :
                       (num < (uint16_t)ay * 210) ? 3 : 4;
        if (dy < 0) {
            oct = (dx >= 0) ? step : (32 - step);
        } else {
            oct = (dx >= 0) ? (16 - step) : (16 + step);
        }
    } else {
        /* East / West dominant: octants 1, 2, 5, 6 */
        uint16_t num = (uint16_t)ay << 8;
        uint8_t step = (num < (uint16_t)ax * 25) ? 0 :
                       (num < (uint16_t)ax * 78) ? 1 :
                       (num < (uint16_t)ax * 137) ? 2 :
                       (num < (uint16_t)ax * 210) ? 3 : 4;
        if (dx >= 0) {
            oct = (dy < 0) ? (8 - step) : (8 + step);
        } else {
            oct = (dy < 0) ? (24 + step) : (24 - step);
        }
    }
    return (uint8_t)(oct & 31);
}

/* ------------------------- Update ------------------------- */
static void update_game(void) {
    uint8_t i;
    int16_t scrollDx = -(int16_t)velDx[facing];
    int16_t scrollDy = -(int16_t)velDy[facing];
    uint8_t eW = 16;

    /* Player invulnerability flicker */
    if (playerInvuln > 0) {
        playerInvuln--;
        if (playerBoom == 0 && playerDeadTimer == 0) {
            if ((playerInvuln & 2) != 0) {
                hide_sprite(SPR_PLAYER);
            } else {
                set_sprite(SPR_PLAYER, PAT_PLAYER + (uint16_t)((facing - 8) & 31) * 256, playerX, playerY, 1, 0x50);
            }
        }
    }

    /* Player explosion handling */
    if (playerBoom > 0) {
        playerBoom--;
        uint8_t fi = (uint8_t)(((12 - playerBoom) * 4) / 12);
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
                if (lives == 0) {
                    players[activePlayer].alive = 0;
                }

                uint8_t other = activePlayer ^ 1;
                if (players[other].alive) {
                    /* Switch to the other player */
                    activePlayer = other;
                    score = players[activePlayer].score;
                    lives = players[activePlayer].lives;
                    stage = players[activePlayer].stage;
                    enemiesKilled = players[activePlayer].enemiesKilled;
                    nextExtraLife = players[activePlayer].nextExtraLife;
                    killMultiplier = 1;
                    killTimer = 0;

                    for (i = 0; i < NUM_ENEMIES; i++) {
                        enemyOn[i] = 0; enemyBoom[i] = 0; enemyWave[i] = 0;
                        hide_sprite(SPR_ENEMY_BASE + i);
                    }
                    for (i = 0; i < NUM_EBULLETS; i++) {
                        ebOn[i] = 0;
                        hide_sprite(SPR_EBULLET_BASE + i);
                    }
                    for (i = 0; i < NUM_BULLETS; i++) {
                        bulletOn[i] = 0;
                        hide_sprite(SPR_BULLET_BASE + i);
                    }
                    paraOn = 0;
                    paraTimer = 540;
                    set_sprite(SPR_PARACHUTE, PAT_PARACHUTE, 0, 0, 0, 0);
                    bomberOn = 0;
                    bomberBoom = 0;
                    bomberTimer = 300;
                    set_sprite(SPR_BOMBER, PAT_BOMBER, 0, 0, 0, 0);
                    popupOn = 0;
                    set_sprite(SPR_POPUP, PAT_NUMBERS, 0, 0, 0, 0);
                    bossOn = 0;
                    bossBoom = 0;
                    set_sprite(SPR_BOSS, PAT_BOSS, 0, 0, 0, 0);

                    playerX = PLAYER_X0;
                    playerY = PLAYER_Y0;
                    facing = 8;
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

                    announceT = 50;
                    g_annDrawn = 0;
                    stageIntroState = 0;
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

            /* Single player respawn (or 2P when only 1 is still alive) */
            for (i = 0; i < NUM_ENEMIES; i++) {
                enemyOn[i] = 0; enemyBoom[i] = 0; enemyWave[i] = 0;
                hide_sprite(SPR_ENEMY_BASE + i);
            }
            for (i = 0; i < NUM_EBULLETS; i++) {
                ebOn[i] = 0;
                hide_sprite(SPR_EBULLET_BASE + i);
            }
            for (i = 0; i < NUM_BULLETS; i++) {
                bulletOn[i] = 0;
                hide_sprite(SPR_BULLET_BASE + i);
            }
            playerX = PLAYER_X0;
            playerY = PLAYER_Y0;
            facing = 8; /* Facing RIGHT */
            set_sprite(SPR_PLAYER, PAT_PLAYER + (uint16_t)((facing - 8) & 31) * 256, playerX, playerY, 1, 0x50);
            reset_clouds();
            g_hudDirty = 1;
            draw_hud();
            announceT = 35; /* ~1.5s READY countdown */
            g_annDrawn = 0;
            stageIntroState = 1;
            playerInvuln = 90;  /* 1.5s spawn invulnerability */
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

    /* Enemy bullets: world scroll + heading, with era weapons:
     * Stage 1: bombs (gravity)
     * Stage 2: boomerangs (spinning)
     * Stage 3: rockets (tracking)
     * Stage 4: space bullets (pulsing) */
    for (i = 0; i < NUM_EBULLETS; i++) {
        if (ebOn[i]) {
            int16_t bx, by;
            if (stage == 1) {
                bx = (int16_t)ebX[i] + ebVX[i] / 2 + scrollDx;
                by = (int16_t)ebY[i] + ebVY[i] + scrollDy + 2;
                set_sprite_pat(SPR_EBULLET_BASE + i, PAT_WEAPON + (uint16_t)(frameCount & 1) * 256);
            } else if (stage == 2) {
                bx = (int16_t)ebX[i] + ebVX[i] + scrollDx;
                by = (int16_t)ebY[i] + ebVY[i] + scrollDy;
                set_sprite_pat(SPR_EBULLET_BASE + i, PAT_WEAPON + (uint16_t)(frameCount & 7) * 256);
            } else if (stage == 3) {
                int16_t rdx = (int16_t)playerX - (int16_t)ebX[i];
                int16_t rdy = (int16_t)playerY - (int16_t)ebY[i];
                uint8_t a = frame_toward(rdx, rdy);
                bx = (int16_t)ebX[i] + ((int16_t)velDx[a] >> 1) + scrollDx;
                by = (int16_t)ebY[i] + ((int16_t)velDy[a] >> 1) + scrollDy;
                set_sprite_pat(SPR_EBULLET_BASE + i, PAT_WEAPON + (uint16_t)(a >> 1) * 256);
            } else if (stage == 4) {
                bx = (int16_t)ebX[i] + ebVX[i] + scrollDx;
                by = (int16_t)ebY[i] + ebVY[i] + scrollDy;
                set_sprite_pat(SPR_EBULLET_BASE + i, PAT_WEAPON + (uint16_t)(frameCount & 7) * 256);
            } else {
                bx = (int16_t)ebX[i] + ebVX[i] + scrollDx;
                by = (int16_t)ebY[i] + ebVY[i] + scrollDy;
            }
            if (bx < PF_XMIN || bx > PF_XMAX || by < PF_YMIN || by > PF_YMAX) {
                ebOn[i] = 0;
                hide_sprite(SPR_EBULLET_BASE + i);
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
                enemyBoom[i]--;
                if (enemyBoom[i] == 0) {
                    enemyOn[i] = 0;
                    set_sprite(SPR_ENEMY_BASE + i, PAT_ENEMY, 0, 0, 0, 0);
                } else {
                    uint8_t fi = (uint8_t)((enemyBoom[i] * 4) / 8);
                    if (fi > 3) fi = 3;
                    set_sprite_pat(SPR_ENEMY_BASE + i, PAT_EXPL + (uint16_t)fi * 256);
                }
                continue;
            }

            /* 1. Steer toward player using shortest angular arc */
            if (((frameCount + i) & 3) == 0) {
                int16_t dx = (int16_t)playerX - enemyX[i];
                int16_t dy = (int16_t)playerY - enemyY[i];
                uint8_t targetAngle = frame_toward(dx, dy);
                uint8_t diff = (uint8_t)((targetAngle - enemyFacing[i]) & 31);
                if (diff != 0) {
                    if (diff < 16) {
                        enemyFacing[i] = (uint8_t)((enemyFacing[i] + 1) & 31); /* turn clockwise */
                    } else {
                        enemyFacing[i] = (uint8_t)((enemyFacing[i] - 1) & 31); /* turn counter-clockwise */
                    }
                }
                /* Update sprite rotation / animation frame */
                set_sprite_pat(SPR_ENEMY_BASE + i, get_enemy_pat(i));
            }

            /* 2. Move with world scroll + enemy's OWN engine thrust */
            int16_t ex = enemyX[i] + scrollDx + (int16_t)velDx[enemyFacing[i]];
            int16_t ey = enemyY[i] + scrollDy + (int16_t)velDy[enemyFacing[i]];

            /* 3. Off-screen tolerance: only despawn after drifting far off */
            if (ex < -32 || ex > 240 || ey < -32 || ey > 260) {
                if (++enemyOffscreen[i] > 30) {
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

                /* 4. Fire bullet when player is in sight */
                if (--enemyTimer[i] == 0) {
                    enemyTimer[i] = (uint16_t)(45 + ((i * 7) % 30));
                    int16_t dx = (int16_t)playerX - ex;
                    int16_t dy = (int16_t)playerY - ey;
                    uint8_t target = frame_toward(dx, dy);
                    uint8_t adiff = (uint8_t)((target - enemyFacing[i]) & 31);
                    if (adiff <= 2 || adiff >= 30) {
                        spawn_ebullet((uint16_t)(ex + 8), (uint16_t)(ey + 8), 3);
                    }
                }
            }
        }
    }

    /* Boss: fly horizontally, fire; player bullets vs boss; boss vs player. */
    if (bossOn) {
        bossXpos = (uint16_t)((int16_t)bossXpos + bossVX + scrollDx);
        if (bossXpos < 10)  bossVX = 3;
        if (bossXpos > 180) bossVX = -3;

        /* Directional sprite & propeller animation: 32x16 */
        uint16_t bPat = PAT_BOSS;
        if (stage == 4) {
            bPat += (uint16_t)((frameCount >> 2) & 1) * 512;
        } else {
            uint8_t dirOff = (bossVX > 0) ? 0 : 4;  /* frames 0..3 right, 4..7 left */
            bPat += (uint16_t)(dirOff + ((frameCount >> 2) & 3)) * 512;
        }
        set_sprite_pat(SPR_BOSS, bPat);
        move_sprite(SPR_BOSS, bossXpos, 60);

        if (--bossFire == 0) {
            bossFire = 25;
            spawn_ebullet(bossXpos + 16, 60 + 8, 4);
        }
        for (i = 0; i < NUM_BULLETS; i++) {
            if (bulletOn[i]) {
                int16_t bx = (int16_t)bulletX[i], by = (int16_t)bulletY[i];
                if (bx + 8 > bossXpos && bx < bossXpos + 32 &&
                    by + 8 > 60 && by < 60 + 16) {
                    bulletOn[i] = 0;
                    hide_sprite(SPR_BULLET_BASE + i);
                    if (--bossHp == 0) {
                        bossHp = 0;
                        bossBoom = 25;
                        bossOn = 0;
                        set_sprite(SPR_BOSS, PAT_EXPL32, bossXpos, 60, 1, 0x60);
                        score += 3000;
                        check_extra_life();
                        popupOn = 1; popupX = (int16_t)bossXpos + 8; popupY = 60; popupFrame = 3; popupTimer = 45; /* "3000" popup */
                        g_hudDirty = 1;
                        audioPlaySource(AUDIO_BIG_EXPLOSION);
                        /* Spectacular boss defeat: all airborne enemies and bomber explode! */
                        for (uint8_t k = 0; k < NUM_ENEMIES; k++) {
                            if (enemyOn[k] && enemyBoom[k] == 0) {
                                enemyBoom[k] = 20;
                                set_sprite_pat(SPR_ENEMY_BASE + k, PAT_EXPL);
                            }
                        }
                        for (uint8_t k = 0; k < NUM_EBULLETS; k++) {
                            if (ebOn[k]) {
                                ebOn[k] = 0;
                                hide_sprite(SPR_EBULLET_BASE + k);
                            }
                        }
                        if (bomberOn && bomberBoom == 0) {
                            bomberBoom = 20;
                            bomberOn = 0;
                            set_sprite(SPR_BOMBER, PAT_EXPL32, (uint16_t)bomberX, (uint16_t)bomberY, 1, 0x60);
                        }
                    }
                }
            }
        }
        if (playerInvuln == 0 && bossXpos + 32 > playerX && bossXpos < playerX + 16 &&
            60 + 16 > playerY && 60 < playerY + 16) {
            bossBoom = 20;
            bossOn = 0;
            set_sprite(SPR_BOSS, PAT_EXPL32, bossXpos, 60, 1, 0x60);
            lose_life();
        }
    }

    /* Boss explosion countdown -> stage clear banner in CURRENT era. */
    if (bossBoom > 0) {
        bossBoom--;
        if (bossBoom == 0) {
            set_sprite(SPR_BOSS, PAT_BOSS, 0, 0, 0, 0);
            /* Clear leftover enemies/bullets from current era */
            for (uint8_t j = 0; j < NUM_ENEMIES; j++) {
                if (enemyOn[j]) { enemyOn[j] = 0; enemyBoom[j] = 0; hide_sprite(SPR_ENEMY_BASE + j); }
            }
            for (uint8_t j = 0; j < NUM_BULLETS; j++) { if (bulletOn[j]) { bulletOn[j] = 0; hide_sprite(SPR_BULLET_BASE + j); } }
            for (uint8_t j = 0; j < NUM_EBULLETS; j++) { if (ebOn[j]) { ebOn[j] = 0; hide_sprite(SPR_EBULLET_BASE + j); } }
            paraOn = 0;
            paraTimer = 540;
            set_sprite(SPR_PARACHUTE, PAT_PARACHUTE, 0, 0, 0, 0);
            bomberOn = 0;
            bomberBoom = 0;
            bomberTimer = 300;
            set_sprite(SPR_BOMBER, PAT_BOMBER, 0, 0, 0, 0);
            popupOn = 0;
            set_sprite(SPR_POPUP, PAT_NUMBERS, 0, 0, 0, 0);

            /* Display STAGE CLEAR perfectly centered in playfield (col 8 of 28 cols) for exactly 3 seconds (180 frames) */
            draw_text(14, 8, sStageClear, 9);
            stageClearTimer = 180;
            audioPlaySource(AUDIO_NEXT_LEVEL);
        } else {
            uint8_t fi = (uint8_t)((bossBoom * 4) / 20);
            if (fi > 3) fi = 3;
            set_sprite(SPR_BOSS, PAT_EXPL32 + (uint16_t)fi * 512, (uint16_t)bossXpos, 60, 1, 0x60);
        }
    }

    /* Auto-clear STAGE CLEAR after 3s, play TIMEWARP sound at 1s remaining, then execute time warp! */
    if (stageClearTimer > 0) {
        stageClearTimer--;
        if (stageClearTimer == 60) {
            audioPlaySource(AUDIO_TIMEWARP);
        }
        if (stageClearTimer == 0) {
            /* Erase STAGE CLEAR text banner */
            draw_text(14, 8, "           ", 0);

            /* Wipe all leftover enemies, bullets, bomber, and boss from previous era */
            for (i = 0; i < NUM_ENEMIES; i++) {
                enemyOn[i] = 0; enemyBoom[i] = 0; enemyWave[i] = 0;
                hide_sprite(SPR_ENEMY_BASE + i);
            }
            for (i = 0; i < NUM_EBULLETS; i++) {
                ebOn[i] = 0;
                hide_sprite(SPR_EBULLET_BASE + i);
            }
            for (i = 0; i < NUM_BULLETS; i++) {
                bulletOn[i] = 0;
                hide_sprite(SPR_BULLET_BASE + i);
            }
            paraOn = 0; hide_sprite(SPR_PARACHUTE);
            bomberOn = 0; bomberBoom = 0; hide_sprite(SPR_BOMBER);
            bossOn = 0; bossBoom = 0; hide_sprite(SPR_BOSS);

            /* Hide all clouds during Time Warp so the hyperspace beam is completely unobstructed */
            for (uint8_t c = 0; c < NUM_CLOUDS; c++) {
                hide_sprite(SPR_CLOUD_BASE + c);
            }

            /* Execute authentic CX16 / Arcade hyperspace Time Warp sequence! */
            screen_time_warp();

            stage = (uint8_t)((stage + 1) % NUM_STAGES);
            enemiesKilled = 0;
            playerInvuln = 90;  /* 1.5s invulnerability upon entering new era */

            screen_wipe_to_sky(stage);      /* counter-clockwise radar sweep to next era! */
            set_stage_palette();
            upload_stage_art();
            paint_status_bar();
            g_hudDirty = 1;
            facing = 8;
            playerX = PLAYER_X0;
            playerY = PLAYER_Y0;
            set_sprite(SPR_PLAYER, PAT_PLAYER, playerX, playerY, 1, 0x50);
            reset_clouds();

            /* Transition to authentic stage announce screen (PLAYER 1 / A.D. XXXX / STAGE X) */
            announceT = 100;
            g_annDrawn = 0;
            stageIntroState = 0;
            state = 4;
            return;
        }
    }

    /* Player bullet vs enemy. */
    for (i = 0; i < NUM_BULLETS; i++) {
        if (bulletOn[i]) {
            int16_t bx = (int16_t)bulletX[i], by = (int16_t)bulletY[i];
            for (uint8_t j = 0; j < NUM_ENEMIES; j++) {
                if (enemyOn[j] && enemyBoom[j] == 0) {
                    int16_t ddx = bx - enemyX[j];
                    if ((uint16_t)(ddx + 7) < (uint16_t)(eW + 7)) {
                        int16_t ddy = by - enemyY[j];
                        if ((uint16_t)(ddy + 7) < (uint16_t)(eW + 7)) {
                            int16_t ex = enemyX[j], ey = enemyY[j];
                            enemyBoom[j] = 8;
                            set_sprite_pat(SPR_ENEMY_BASE + j, PAT_EXPL);
                            bulletOn[i] = 0;
                            hide_sprite(SPR_BULLET_BASE + i);

                            /* Multiplier scoring: 100 -> 200 -> 300 -> 400 */
                            if (killTimer >= 30) {
                                killMultiplier = 1;
                            }
                            killTimer = 0;
                            score += (uint32_t)killMultiplier * 100;
                            if (killMultiplier < 4) killMultiplier++;
                            enemiesKilled++;
                            check_extra_life();
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
                                        popupOn = 1; popupX = ex; popupY = ey; popupFrame = 2; popupTimer = 45; /* "2000" frame */
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

    /* Spawn enemies; periodic formation wave; after kills the boss appears. */
    if (!bossOn && bossBoom == 0 && stageClearTimer == 0) {
        static uint8_t spawnCounter = 0;
        uint8_t scMax = isDemoMode ? 60 : 25;
        if (++spawnCounter >= scMax) {
            spawnCounter = 0;
            spawn_enemy();
        }
        if (++waveTimer >= 600) {
            waveTimer = 0;
            spawn_wave();
        }
        if (enemiesKilled >= ENEMIES_TO_BOSS) {
            spawn_boss();
        }
    }

    /* Enemy bullet vs player; enemy vs player (only if player alive and not invulnerable). */
    if (playerBoom == 0 && playerDeadTimer == 0 && playerInvuln == 0) {
        for (i = 0; i < NUM_EBULLETS; i++) {
            if (ebOn[i]) {
                int16_t bx = (int16_t)ebX[i], by = (int16_t)ebY[i];
                if ((uint16_t)(bx - (PLAYER_X0 - 7)) < 23 && (uint16_t)(by - (PLAYER_Y0 - 7)) < 23) {
                    ebOn[i] = 0;
                    hide_sprite(SPR_EBULLET_BASE + i);
                    lose_life();
                    break;
                }
            }
        }
        for (i = 0; i < NUM_ENEMIES; i++) {
            if (enemyOn[i] && enemyBoom[i] == 0) {
                int16_t dex = enemyX[i] - (int16_t)playerX;
                if ((uint16_t)(dex + eW - 1) < (uint16_t)(15 + eW)) {
                    int16_t dey = enemyY[i] - (int16_t)playerY;
                    if ((uint16_t)(dey + eW - 1) < (uint16_t)(15 + eW)) {
                        enemyBoom[i] = 8;
                        set_sprite_pat(SPR_ENEMY_BASE + i, PAT_EXPL);
                        lose_life();
                        break;
                    }
                }
            }
        }
    }

    /* Parachute bonus pickup (stages 0..3) */
    if (stage < 4) {
        if (!paraOn) {
            if (paraTimer > 0) {
                paraTimer--;
                if (paraTimer == 0) {
                    paraOn = 1;
                    paraX = (int16_t)(30 + ((frameCount * 37) % 150));
                    paraY = PF_YMIN;
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
            paraY += scrollDy + 1; /* drifts downward */
            if (paraX < -20 || paraX > PF_W + 10 || paraY < -20 || paraY > PF_YMAX + 20) {
                paraOn = 0;
                paraTimer = 540;
                paraBonusStreak = 0; /* missed parachute resets streak */
                set_sprite(SPR_PARACHUTE, PAT_PARACHUTE, 0, 0, 0, 0);
            } else {
                move_sprite(SPR_PARACHUTE, (uint16_t)paraX, (uint16_t)paraY);
                if (paraX + 14 > (int16_t)playerX && paraX < (int16_t)playerX + 14 &&
                    paraY + 14 > (int16_t)playerY && paraY < (int16_t)playerY + 14) {
                    static const uint16_t bonusScores[5] = { 1000, 2000, 3000, 4000, 5000 };
                    score += bonusScores[paraBonusStreak];
                    check_extra_life();
                    popupOn = 1; popupX = paraX; popupY = paraY; popupFrame = (paraBonusStreak == 0) ? 0 : (paraBonusStreak + 1); popupTimer = 45;
                    if (paraBonusStreak < 4) paraBonusStreak++;
                    g_hudDirty = 1;
                    audioPlaySource(AUDIO_PICKUP);
                    paraOn = 0;
                    paraTimer = 540;
                    set_sprite(SPR_PARACHUTE, PAT_PARACHUTE, 0, 0, 0, 0);
                }
            }
        }
    } else if (paraOn) {
        paraOn = 0;
        set_sprite(SPR_PARACHUTE, PAT_PARACHUTE, 0, 0, 0, 0);
    }

    /* 1940 Bomber formation (Stage 1 only) */
    if (stage == 1) {
        if (!bomberOn) {
            if (bomberTimer > 0) {
                bomberTimer--;
                if (bomberTimer == 0) {
                    bomberOn = 1;
                    bomberHealth = 4;
                    bomberDir = (frameCount & 1) ? 1 : -1;
                    bomberX = (bomberDir > 0) ? -32 : 224;
                    bomberY = (int16_t)(35 + ((frameCount * 31) % 90));
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
                    bomberTimer = 480;
                    set_sprite(SPR_BOMBER, PAT_BOMBER, 0, 0, 0, 0);
                } else {
                    uint8_t fi = (uint8_t)((bomberBoom * 4) / 16);
                    if (fi > 3) fi = 3;
                    set_sprite(SPR_BOMBER, PAT_EXPL32 + (uint16_t)fi * 512, (uint16_t)bomberX, (uint16_t)bomberY, 1, 0x60);
                }
            } else {
                bomberX += scrollDx + bomberDir * 2;
                bomberY += scrollDy;
                if (bomberX < -48 || bomberX > 256 || bomberY < -20 || bomberY > 260) {
                    bomberOn = 0;
                    bomberTimer = 480;
                    set_sprite(SPR_BOMBER, PAT_BOMBER, 0, 0, 0, 0);
                } else {
                    uint8_t bframe = (bomberDir > 0) ? (4 - bomberHealth) : (4 + (4 - bomberHealth));
                    set_sprite(SPR_BOMBER, PAT_BOMBER + (uint16_t)bframe * 512, (uint16_t)bomberX, (uint16_t)bomberY, 1, 0x60);
                    /* Bomber drops bomb every 32 frames */
                    if (!(frameCount & 31)) {
                        spawn_ebullet((uint16_t)(bomberX + 16), (uint16_t)(bomberY + 12), 1);
                    }
                    /* Check player bullets hitting bomber */
                    for (uint8_t bi = 0; bi < NUM_BULLETS; bi++) {
                        if (bulletOn[bi]) {
                            int16_t bx = (int16_t)bulletX[bi], by = (int16_t)bulletY[bi];
                            if (bx + 8 > bomberX && bx < bomberX + 32 &&
                                by + 8 > bomberY && by < bomberY + 16) {
                                bulletOn[bi] = 0;
                                hide_sprite(SPR_BULLET_BASE + bi);
                                if (bomberHealth > 0) bomberHealth--;
                                if (bomberHealth == 0) {
                                    bomberBoom = 16;
                                    score += 1500;
                                    check_extra_life();
                                    g_hudDirty = 1;
                                    popupOn = 1; popupX = bomberX + 8; popupY = bomberY; popupFrame = 1; popupTimer = 45; /* "1500" popup */
                                    audioPlaySource(AUDIO_BIG_EXPLOSION);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    } else if (bomberOn) {
        bomberOn = 0;
        set_sprite(SPR_BOMBER, PAT_BOMBER, 0, 0, 0, 0);
    }

    /* Score popup float animation */
    if (popupOn) {
        popupY -= 1;
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
    playerBoom = 12;        /* ~0.5s explosion */
    playerDeadTimer = 30;   /* ~1.5s post-mortem world review */
    audioPlaySource(AUDIO_BIG_EXPLOSION);
    if (paraOn) {
        paraOn = 0;
        paraTimer = 540;
        set_sprite(SPR_PARACHUTE, PAT_PARACHUTE, 0, 0, 0, 0);
    }
    if (bomberOn) {
        bomberOn = 0;
        set_sprite(SPR_BOMBER, PAT_BOMBER, 0, 0, 0, 0);
    }
    if (popupOn) {
        popupOn = 0;
        set_sprite(SPR_POPUP, PAT_NUMBERS, 0, 0, 0, 0);
    }
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
        draw_text(21, 33, "CHEAT", 2); /* GREEN "CHEAT" in status bar */
    } else {
        draw_text(21, 33, "     ", 0xF0);
    }
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

    if (numPlayers == 2) {
        players[1].score = 0;
        players[1].lives = LIVES_MAX;
        players[1].stage = stage;
        players[1].enemiesKilled = 0;
        players[1].nextExtraLife = 10000;
        players[1].alive = 1;
    } else {
        players[1].alive = 0;
    }

    playerX = PLAYER_X0;
    playerY = PLAYER_Y0;
    facing = 8; /* Facing RIGHT (matches cx16-2.jpg and arcade original) */
    score = 0;
    lives = LIVES_MAX;
    enemiesKilled = 0;
    nextExtraLife = 10000;
    killMultiplier = 1;
    killTimer = 0;
    bossOn = 0;
    bossBoom = 0;
    stageClearTimer = 0;
    stageIntroState = 0;
    announceT = 0;
    playerBoom = 0;
    playerDeadTimer = 0;
    playerInvuln = 90;  /* 1.5s initial game start invulnerability */
    set_stage_palette();
    for (i = 0; i < NUM_BULLETS; i++) { bulletOn[i] = 0; }
    for (i = 0; i < NUM_EBULLETS; i++) { ebOn[i] = 0; }
    for (i = 0; i < NUM_ENEMIES; i++) { enemyOn[i] = 0; enemyBoom[i] = 0; enemyWave[i] = 0; }
    paraOn = 0;
    paraTimer = 540;
    paraBonusStreak = 0;
    bomberOn = 0;
    bomberBoom = 0;
    bomberTimer = 300;
    waveTimer = 300;
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

static void title_screen(void) {
    if (g_titleDrawn) return;
    g_titleDrawn = 1;
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
    draw_text(19, 7, sKonami, 9);
    draw_text(21, 6, sVersion, 3);
    draw_text(22, 4, sWessels, 3);
    draw_text(24, 2, sIIveraVer, 7);
    draw_text(25, 7, sAnomixer, 7);
    /* Right-side score bar (matches cx16-1.jpg: HIGH SCORE + 1-UP only). */
    draw_text(1, 29, sHighScore, 1);
    draw_text(2, 32, format_score_right(highScore[0]), 9);
    draw_text(4, 35, sOneUp, 1);
    draw_text(5, 32, format_score_right(0), 9);
    draw_text(7, 29, "          ", 0);
    draw_text(8, 29, "          ", 0);
    /* Attract text (CX16 uiShowTitle). */
    draw_text(0, 12, sPlay, 7);
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

/* Stage announce (CX16 uiShowPreGameLabels): PLAYER 1 / A.D. yyyy / STAGE n or READY (matches cx16-2.jpg) */
static void stage_announce(void) {
    if (g_annDrawn) return;
    g_annDrawn = 1;
    draw_text(11, 10, (activePlayer == 1) ? sPlayer2 : sPlayer1, 9);     /* Row 11: WHITE PLAYER 1 */
    if (!stageIntroState) {
        draw_text(15, 9, eraLabel[stage], 1);                            /* Row 15: RED A.D. yyyy */
        char stagebuf[8];
        stagebuf[0] = 'S'; stagebuf[1] = 'T'; stagebuf[2] = 'A';
        stagebuf[3] = 'G'; stagebuf[4] = 'E'; stagebuf[5] = ' ';
        stagebuf[6] = (char)('1' + (stage & 0xF)); stagebuf[7] = 0;
        draw_text(19, 10, stagebuf, 9);                                  /* Row 19: WHITE STAGE n */
    } else {
        draw_text(15, 11, sReady, 9);                                    /* Row 15: white READY */
    }
}

static const int8_t joyDirAngles[16] = {
    -1,  0,  8,  4, 16, -1, 12, -1,
    24, 28, -1, -1, 20, -1, -1, -1
};

static void start_game_from_ui(uint8_t mode) {
    attractCycleCount = 0;
    isDemoMode = 0;

    /* Hide sprites during asset streaming to eliminate any mid-load flicker */
    VERA.display.video = 0x11;
    hide_all_sprites();

    stage = 0;
    upload_stage_art();
    set_stage_palette();

    screen_wipe_to_sky(0);
    init_game(mode);
    paint_status_bar();
    g_hudDirty = 1;
    draw_hud();

    waitvsync();
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

/* Draw Score Ranking Table (verbatim match with cx16-1.jpg) */
static void draw_hs_table(void) {
    uint8_t i;
    draw_text(5, 4, sRanking, 6); /* Row 5: SCORE RANKING TABLE in Magenta (color 6) */
    for (i = 0; i < NUM_HIGHSCORES; i++) {
        uint8_t y = (uint8_t)(7 + i * 2);
        draw_text(y, 4, hsRank[i], hsColor[i]);
        /* Format score right-aligned 5 digits at col 11..15 */
        char sbuf[8];
        uint32_t ns = highScore[i];
        for (int b = 4; b >= 0; b--) {
            sbuf[b] = (char)('0' + (ns % 10));
            ns /= 10;
        }
        sbuf[5] = 0;
        int z = 0;
        while (z < 4 && sbuf[z] == '0') {
            sbuf[z] = ' ';
            z++;
        }
        draw_text(y, 11, sbuf, hsColor[i]);
        draw_initials(y, 20, highScoreInitials[i], hsColor[i]);
    }
}

static int8_t hs_pending_player = -1;

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
    highScoreInitials[pos][0] = 'A';
    highScoreInitials[pos][1] = 'A';
    highScoreInitials[pos][2] = 'A';
    return pos;
}

static uint8_t check_and_start_hs_entry(int8_t playerIdx) {
    uint32_t pscore = players[playerIdx].score;
    int8_t r = hs_insert_score(pscore);
    if (r >= 0) {
        hs_pending_player = playerIdx;
        hs_row = r;
        hs_char_idx = 0;
        hs_curr_char = 'A';
        hs_entry_timer = 60 * 30; /* 30 seconds timeout */
        paint_screen();
        if (numPlayers == 2) {
            draw_text(1, 10, (playerIdx == 0) ? sPlayer1 : sPlayer2, 9);
        }
        draw_text(3, 4, sEnterInitials, 3); /* col 4 aligned with SCORE RANKING TABLE (19 chars) */
        draw_hs_table();
        state = 2;
        audioPlaySource(AUDIO_HIGHSCORE);
        return 1;
    }
    return 0;
}

static void hs_entry_completed(void) {
    audioPlaySource(AUDIO_PICKUP);
    if (numPlayers == 2 && hs_pending_player == 0) {
        /* Player 1 finished signing! Now check if Player 2 qualified */
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

    /* Keyboard steering with authentic arcade turn rate (2 frames per step = ~1s full 360) */
    if (ku == 'A' || k == 8) {
        if (steerStall & 1) {
            facing = (facing + 31) & 31;
        }
    } else if (ku == 'D' || k == 21) {
        if (steerStall & 1) {
            facing = (facing + 1) & 31;
        }
    } else if (ku == 'W' || k == 11) {
        /* Up Arrow / W: steer toward heading UP (0) */
        if (steerStall & 1 && facing != 0) {
            facing = (facing <= 16) ? ((facing + 31) & 31) : ((facing + 1) & 31);
        }
    } else if (ku == 'S' || k == 10) {
        /* Down Arrow / S: steer toward heading DOWN (16) */
        if (steerStall & 1 && facing != 16) {
            facing = (facing < 16) ? ((facing + 1) & 31) : ((facing + 31) & 31);
        }
    }

    if (useJoystick) {
        uint8_t jx = read_pdl(0);
        uint8_t jy = read_pdl(1);
        uint8_t jl = (jx < 85), jr = (jx > 170);
        uint8_t ju = (jy < 85), jd = (jy > 170);
        uint8_t mask = (ju ? 1 : 0) | (jr ? 2 : 0) | (jd ? 4 : 0) | (jl ? 8 : 0);
        int8_t target = joyDirAngles[mask];

        if (target >= 0 && facing != (uint8_t)target) {
            if (steerStall & 1) {
                uint8_t diff = (uint8_t)(((uint8_t)target - facing) & 31);
                if (diff <= 16) {
                    facing = (facing + 1)  & 31;
                } else {
                    facing = (facing + 31) & 31;
                }
            }
        }
    }

    set_sprite_pat(SPR_PLAYER, PAT_PLAYER + (uint16_t)((facing - 8) & 31) * 256);
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
                uint16_t d = (uint16_t)(dx * dx + dy * dy);
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
                fire_bullet();
            }
        }
    } else {
        if (!(frameCount & 7)) {
            facing = (facing + 1) & 31;
            set_sprite_pat(SPR_PLAYER, PAT_PLAYER + (uint16_t)((facing - 8) & 31) * 256);
        }
        if (!(frameCount & 15)) {
            fire_bullet();
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
    audioPlaySource(AUDIO_COINDROP);
    VERA.display.video = 0x51;  /* NOW enable sprites! */
    VERA.irq_flags = VERA_IRQ_VSYNC; /* Prime VSYNC flag for main loop lock */

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
                        set_black_palette();
                        set_sprite(SPR_LOGO_TIME,  PAT_LOGO_TIME,  48,  16, 1, 0x70);
                        set_sprite(SPR_LOGO_PILOT, PAT_LOGO_PILOT, 120, 16, 1, 0x70);
                        set_sprite(SPR_STAGE_BASE, PAT_STAGE_ICON, 312, 128, 1, 0x00);
                        for (uint8_t si = 1; si < NUM_STAGE_SPR; si++) {
                            set_sprite(SPR_STAGE_BASE + si, PAT_STAGE_ICON, 0, 0, 0, 0);
                        }
                        for (uint8_t li = 0; li < 3; li++) {
                            set_sprite(SPR_LIFE_BASE + li, PAT_PLAYER + 24 * 256, (uint16_t)(304 - li * 16), 152, 1, 0x50);
                        }
                        for (uint8_t pi = 0; pi < NUM_PROG_SPR; pi++) set_sprite(SPR_PROG_BASE + pi, PAT_ENEMY, 0, 0, 0, 0);
                        draw_text(0, 12, sPlay, 7);
                        draw_text(19, 7, sKonami, 9);
                        draw_text(21, 6, sVersion, 3);
                        draw_text(22, 4, sWessels, 3);
                        draw_text(24, 2, sIIveraVer, 7);
                        draw_text(25, 7, sAnomixer, 7);
                        draw_text(1, 29, sHighScore, 1);
                        draw_text(2, 32, format_score_right(highScore[0]), 9);
                        draw_text(4, 35, sOneUp, 1);
                        draw_text(5, 32, format_score_right(0), 9);
                        draw_text(7, 29, "          ", 0);
                        draw_text(8, 29, "          ", 0);
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

                            /* Hide sprites during disk streaming to eliminate start-of-demo flicker */
                            VERA.display.video = 0x11;
                            hide_all_sprites();

                            stage = 1;                /* Stage 1 = A.D. 1940 (Green Sky) */
                            upload_stage_art();       /* Upload 1940 Bomber, weapons, green fighters, expl32 */

                            /* Green counter-clockwise radar sweep wipes title screen directly to green! */
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
                if (ku == 'C') {
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
                        announceT = 480;   /* 8.0s (480 frames @ 60Hz: full opening theme + trailing reverb) */
                        audioPlaySource(AUDIO_GAME_START);
                    } else {
                        announceT = 100;   /* ~1.6s brief era announce */
                    }
                    isGameStartIntro = 0;
                    g_hudDirty = 1;
                }
                stage_announce();
                draw_hud();
                /* Allow player to steer the plane freely */
                if (!isDemoMode) {
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
                    draw_text(15, 9, "         ", 0);
                    draw_text(19, 10, "       ", 0);
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
                if (ku == 'C') {
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
                if (playerBoom == 0 && playerDeadTimer == 0) {
                    update_player_steering(k, ku);
                    if (k == ' ' || ku == ' ' || k == '1') fire_bullet();
                    if (useJoystick && (((*(volatile uint8_t *)0xC061 & 0x80) != 0) ||
                                       ((*(volatile uint8_t *)0xC062 & 0x80) != 0))) {
                        fire_bullet();
                    }
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

        case 2: /* High-score interactive initials entry */
            {
                draw_hs_table();
                uint8_t y = (uint8_t)(7 + hs_row * 2);

                /* Blinking cursor for the active initial slot directly in-place */
                char dispInitials[4];
                dispInitials[0] = highScoreInitials[hs_row][0];
                dispInitials[1] = highScoreInitials[hs_row][1];
                dispInitials[2] = highScoreInitials[hs_row][2];
                dispInitials[3] = 0;

                if ((frameCount & 16) == 0) {
                    dispInitials[hs_char_idx] = ' '; /* blink blank */
                } else {
                    dispInitials[hs_char_idx] = hs_curr_char;
                }
                draw_initials(y, 20, dispInitials, hsColor[hs_row]);

                /* Timeout check */
                if (--hs_entry_timer == 0) {
                    highScoreInitials[hs_row][hs_char_idx] = hs_curr_char;
                    draw_initials(y, 20, highScoreInitials[hs_row], hsColor[hs_row]);
                    hs_entry_completed();
                    break;
                }

                /* Direct letter input */
                if (ku >= 'A' && ku <= 'Z') {
                    highScoreInitials[hs_row][hs_char_idx] = (char)ku;
                    hs_curr_char = (char)ku;
                    draw_initials(y, 20, highScoreInitials[hs_row], hsColor[hs_row]);
                    hs_char_idx++;
                    if (hs_char_idx >= 3) {
                        hs_entry_completed();
                    } else {
                        hs_curr_char = 'A';
                    }
                } else if (ku == 'A' || k == 8) {
                    /* Move letter backward */
                    if (hs_curr_char == '.') {
                        hs_curr_char = 'Z';
                    } else if (hs_curr_char == 'A') {
                        hs_curr_char = '.';
                    } else {
                        hs_curr_char--;
                    }
                } else if (ku == 'D' || k == 21) {
                    /* Move letter forward */
                    if (hs_curr_char == 'Z') {
                        hs_curr_char = '.';
                    } else if (hs_curr_char == '.') {
                        hs_curr_char = 'A';
                    } else {
                        hs_curr_char++;
                    }
                } else if (k == ' ' || ku == ' ' || k == 13 || k == '1') {
                    /* Confirm current letter */
                    highScoreInitials[hs_row][hs_char_idx] = hs_curr_char;
                    draw_initials(y, 20, highScoreInitials[hs_row], hsColor[hs_row]);
                    hs_char_idx++;
                    if (hs_char_idx >= 3) {
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
