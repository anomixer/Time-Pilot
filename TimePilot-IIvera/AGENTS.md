# Time Pilot IIvera — Development Log & Project Guide

Port of **Time Pilot** to an Apple IIe running the VERA (VIDHD-style) card,
using **llvm-mos** (mos-apple2e target) and verified on the **apple2ts** emulator.

This document records the development journey, current progress, and the
technical constraints that shape the codebase. Read it before making changes.

---

## What This Project Is

A faithful Time Pilot arcade experience on Apple II + VERA:

- The player plane is **FIXED at the playfield center**; the **world scrolls**
  opposite the plane's facing (flying-into-the-distance feel).
- The plane rotates through **32 directions** (11.25° per step), matching the arcade.
- Enemies home in from the screen edges and fire back; per-era enemy/boss artwork.
- Scrolling cloud layer, explosion animations, 5-era stage progression with a boss each era.
- Right-side arcade **status bar** (HIGH SCORE / 1-UP / 2-UP / LIVES / progress), black background.

The sprite art is **100% original CX16 Time Pilot artwork**, streamed from the HDV
into VERA pattern RAM at boot (no placeholder graphics). Audio is the **literal CX16
PCM samples** (game-start jingle, per-boss themes, shoot/explosion SFX), also on the HDV.

---

## Key Technical Constraints (read these first)

These constraints drive most architectural decisions. They are hard limits.

### 1. ProDOS 2.4.3 Safe Memory Map & Standard `$1400` Load Address
- **Standard ProDOS BIN Load Base (`$1400`)**: In ProDOS 2.4.3 / BASIC.SYSTEM, when executing `BRUN`, `$1000–$12FF` is used by ProDOS QUIT code, and `$1300–$13FF` is BASIC.SYSTEM scratch space. Therefore, `MAIN.BIN` loads cleanly at **`$1400`**.
- **ProDOS Buffer Ceiling (`$9600`)**: ProDOS file buffers grow downward from `$BF00` to `$9600`. Any binary extending to `$9600` or above triggers the fatal `NO BUFFERS AVAILABLE` error during `BRUN`. The maximum safe program ceiling is `$95FF` (maximum size 33,280 bytes from `$1400`).
- **`-Oz` Extreme Optimization & Current Footprint**: `MAIN.BIN` and `MAIN4.BIN` are compiled with **`-Oz`** down to **28,056 bytes** (spans **`$1400–$8198`**), providing **over 5.2 KB of verified safe headroom** strictly below the `$9600` buffer boundary without `NO BUFFERS AVAILABLE` and 100% free of memory corruption! Art, audio, and demo live on HDV and are streamed via MLI `$80`.

### 2. apple2ts palette rendering is **4-bit-per-channel** (RRRR GGGG BBBB)
CX16's `colorPalette[]` values (`0x0F00` red, `0x0FFF` white, `0x00C0` green, …) are the
correct format. Standard RGB565 values render **BLACK** in apple2ts — this caused the early
"screen too dark" bug.

### 3. Sprites read palette entries **16–31**
Every sprite is written with `palette_offset = 1` (attr byte 7 = `dims | 1`), so it colors
from palette rows 16–31, which duplicate rows 0–15. The stage sky is recolored only via
`palette[0]` (layer background).

### 4. **16-color text mode (T256C=0): background nibble provides pure black status bar**
In earlier revisions, `VERA.layer0.config` was set to `0x18` (T256C = 1, Tile 256-color mode).
In T256C mode, VERA hardware hardwires all non-glyph pixels (0s) in 1bpp tiles to `palette[0]`
(the blue sky). No matter what was written to the attribute byte, non-glyph pixels always rendered
in sky blue, causing status bar text (`HIGH SCORE`, `1-UP`, `00`) to have an ugly blue background!
**Solution**: Switched to authentic 16-color text mode (`VERA.layer0.config = 0x10`, T256C=0).
In 16-color mode, Byte 1 is `(BG << 4) | FG`. Status bar text (`col >= 28`) forces `attr = 0xF0 | color`,
setting the background to Palette 15 (`0x0000` pure black). Now every character, space, and loop
in the status bar is 100% pitch black with zero blue leakage!

### 5. Frame pacing is locked to VERA vsync (60 Hz)
The main loop ends with `waitvsync()` — it polls the VERA ISR flag (`$C207`, bit 0), which
apple2ts sets each frame and clears on write. This makes timed sequences (stage announce,
STAGE CLEAR banner, explosions) a **true** number of frames regardless of CPU speed setting.
Do NOT replace this with a busy-wait `delay()` — that made the stage announce take 15–20 s.

### 6. Sprite memory budget (VERA bank 1 pattern RAM)
| Pattern | Address (bank 1) | Size |
|--------|------------------|------|
| Player 32 frames | `0x8000` | 8 KB |
| Enemy 16 frames | `0xA000` | 4 KB |
| Boss 16 frames | `0xB000` | 4 KB |
| Explosion 4 frames | `0xC000` | 1 KB |
| Bullet / enemy bullet | `0xC400` / `0xC440` | 64 B each |
| Cloud 0 / Astro 0 (16x16) | `0xC480` | 256 B |
| Cloud 1 / Astro 1 (32x16) | `0xC580` | 512 B |
| Cloud 2 / Astro 2 (64x16 / 32x16) | `0xC780` | 1 KB |
| Parachute 4 frames (16x16) | `0xCC00` | 1 KB |
| Stage Weapons / Logo Time | `0xD000` | 2 KB |
| 1940 Bomber / Logo Pilot | `0xD800` | 2 KB |
| Score numbers popup (1000..5000) | `0xE000` | 1.5 KB |
| Big Explosion 32x16 (4 frames) | `0xE800` | 2 KB |
| Font tiles (128) + SOLID_BLOCK(127) | `0xF000` | 1 KB |
| Palette | `0xFA00` | 512 B |
| Sprite attrs | `0xFC00` | 1 KB |

---

## Rendering & Geometry Facts

### Player frame mapping (32 frames)
Facing is `0..31`, **clockwise from up**, frame 0 = pointing right.
```c
uint8_t frame = (facing - 8) & 31;   // frame 0 = right, frame 24 = up
```

### Enemy frame mapping (8 frames, full 360°)
The enemy sheets are sampled **every other frame** (stride 2) so 8 frames cover a full circle.
Direction toward the player is found via dot-product against `velDx[32]`/`velDy[32]`.

### Screen / playfield
- Playfield = left **28 columns** (`PF_W` = 224 px). Player pinned at `(104,120)`.
- Right **12 columns** (cols 28–39) = the arcade status bar.
- Layer 0 config `0x18`: text 256-col, map width 64, map height 32. Map cell = `row*128 + col*2`.
- Sprite dims: `0` = 8×8, `0x50` = 16×16, `0x60` = 32×16.

---

## Stage & Art Swapping (5 eras)

| Stage | Era | Sky color `palette[0]` |
|-------|-----|------------------------|
| 0 | A.D. 1910 | `0x0006` |
| 1 | A.D. 1940 | `0x0056` |
| 2 | A.D. 1970 | `0x0065` |
| 3 | A.D. 1982 | `0x0505` |
| 4 | A.D. 2001 | `0x0000` (enemy is 8×8, dims 0) |

Enemies → `PAT_ENEMY`, bosses → `PAT_BOSS`. On stage change and game over:
`set_stage_palette()` + `upload_stage_art()`. **On boss death the leftover enemies/bullets are
cleared** — otherwise they persist into the next era, get re-killed fast, respawn the boss, and
STAGE CLEAR keeps reappearing.

---

## Audio (VERA PCM, streamed from HDV)

- The 20 CX16 PCM samples (8-bit signed mono @ 12207 Hz) are concatenated into one blob on the
  HDV (`build/pcm.blob`, block `PCM_START_BLOCK` = 200). `audioServiceAudio()` streams bytes from
  that blob → the VERA PCM FIFO, refilling a 512-byte RAM window with MLI READ_BLOCK.
- **VERA PCM is driven through SLOT-RELATIVE registers `$1B/$$1C/$1D`** — the apple2e.h `.audio`
  C struct field is misaligned with the real VERA map, so we do NOT use it (see `src/audio.c`).
  Rate = **64** for 12207 Hz on apple2ts (`48828 * 64/256`).
- Priority model mirrors CX16: higher source index preempts lower; looping samples restart.

> ⚠️ **Audio is currently DISABLED** via `#define AUDIO_ENABLED 0` in `src/audio.c`. It was turned
> off to finish the gameplay (per-frame PCM FIFO feeding from the HDV was the main 1 MHz cost and
> stalled the stage announce). To re-enable: set it to `1`, then rework the streaming to be
> interrupt/batch-driven (veratest's `ENABLE_MUSIC_IRQ` pattern) so it doesn't block the main loop.

---

## HDV Layout & CATALOG

The game ships as a **ProDOS 8 HDV** (`TimePilot-IIvera.hdv`, 32 MB). Boot chain:
ProDOS → BASIC.SYSTEM → Applesoft `STARTUP` → `BRUN MAIN.BIN`.

CATALOG lists (all are real ProDOS files):

| File | Size | Blocks | Notes |
|------|------|--------|-------|
| MAIN.BIN | ~25.5 KB | 52 | Slot 2 core (type 0x06, load $2000, VERA at $C200) |
| MAIN4.BIN | ~25.5 KB | 52 | Slot 4 core (type 0x06, load $2000, VERA at $C400) |
| STARTUP | ~940 B | 3 | Applesoft BASIC launcher with Slot 2/4 hardware detection |
| PCM1 / PCM2 / PCM3 | 128K + 128K + 84K | 257+257+169 | the audio blob (split: a ProDOS file caps at 256 index slots = 128 KB) |
| ART | ~39 KB | 79 | the sprite-art blob |

The PCM/ART blobs are **registered as ProDOS "sapling" files** (index block + data blocks, same
pattern as veratest's `/DATA` images) so CATALOG can list them — but their **data blocks stay at
their fixed addresses** (`PCM_START_BLOCK`=200, `ART_START_BLOCK`=900), which the game reads
directly via MLI. So they are both "visible files" AND directly addressable by block number.

---

## Development History (what we did, in order)

1. **Gameplay core** — player pinned at center, 32-direction rotate + auto-thrust, world scrolls
   opposite facing, enemies/bullets/clouds scroll.
2. **100% original artwork** — extracted CX16 sprites verbatim; now streamed from the HDV.
3. **Fixed RAM overflow / `NO BUFFERS AVAILABLE`** — moved art+audio off-RAM onto the HDV (MLI).
4. **Fixed screen too dark** — palette uses CX16 `colorPalette[]` values exactly (4-bit/channel).
5. **Fixed enemy directionality** — stride-2 sampling so 8 frames cover full 360°.
6. **Added VERA PCM audio** (literal CX16 samples) + ProDOS MLI disk streaming (`disk.c`, `mli.s`).
7. **Switched to a 32 MB HDV** (floppy couldn't hold the ~350 KB audio set).
8. **Locked frame pacing to VERA vsync** — fixed the stage announce taking 15–20 s; game now runs at a true 60 Hz.
9. **Fixed LIVES underflow** — two hits in one frame wrapped `uint8_t` lives 1→0→255 and skipped
   game-over; added a floored `lose_life()`.
10. **Fixed the black status bar** — it was showing the sky because a space char renders as
    `palette[0]`; now filled with a solid tile + black foreground (see constraint #4).
11. **Fixed STAGE CLEAR reappearing forever** — leftover enemies from the previous era were not
    cleared on stage change; they respawned the boss repeatedly. Now cleared on boss death.
12. **Made PCM/ART visible in CATALOG** — registered them as ProDOS sapling files (data stays at
    fixed block addresses).
13. **Right Status Bar 3 Core Items Parity (cx16-2.jpg)**:
    - Item 1: Differentiated top era icon as 8×8 sprite (`stage.png`, `PAT_STAGE_ICON` at `(304, 132)`).
    - Item 2: Reserve fighter planes (pointing UP, 16×16) at `(304, 152)`.
    - Item 3: 6 stage progress enemy planes (16×8, `progress.png`, `PAT_PROG_ICON` at `y = 192`),
      erasing left-to-right as kills accumulate (1 plane erased per 8 kills, up to 48 kills to trigger Boss).
14. **Authentic CX16 Game Over Screen (`uiGameOver`)**:
    - Clears playfield to clean sky, wipes playfield sprites, centers `PLAYER 1` (white) and `GAME OVER` (red).
    - Erased all clutter ("PRESS SPACE OR 1", duplicate score text, yellow text).
15. **Real-time Radar Wipe Screen Transition**:
    - Keeps 3D TIME PILOT logo on screen when game start is pressed. The 360-degree counter-clockwise
      radar sweep (`screen_wipe_to_sky(0)`) draws solid blue tiles on Layer 0, sweeping over and wiping away
      the 3D logo and title text in real time. Logo sprites are only disabled after sweep finishes.
16. **Fixed (0,0) Big Cloud Glitch & Game Start Delay**:
    - Video mode starts with sprites disabled (`0x11`) and clears all 128 sprite attribute slots before enabling (`0x51`).
    - Removed rogue sprite positioning loop from `upload_stage_art()`. Removed redundant re-streaming of stage 0 art at game start.
17. **Fixed Status Bar Blue Background Leakage**:
    - Switched Layer 0 from T256C mode to authentic 16-color text mode (`config = 0x10`, T256C=0).
    - Forced background nibble to `0xF0` (Palette 15 = black) for `col >= 28`, completely eliminating blue text backgrounds.
18. **Authentic Player Death, Post-Mortem Review, and READY Re-announce**:
    - Multi-frame 32×16 explosion animation (12 frames, `AUDIO_BIG_EXPLOSION`).
    - Post-mortem review scene (~1.5s, 30 frames): player plane disappears while clouds drift and enemy biplanes fly across the sky.
    - If lives remain: clears enemies/bullets, resets player to center facing right, decrements HUD reserve ship, displays `PLAYER 1` + `READY` for ~1.5s (35 frames), then resumes gameplay.
    - Scaled down frame counters for Apple IIe 1MHz execution (~18-20 fps) so the death flow takes ~3.5s total instead of dragging 10-13s.
19. **Dual Slot 2 & Slot 4 Support + Hardware Detection**:
    - Extracted standalone [src/startup.bas](file:///c:/dev/Time-Pilot/TimePilot-IIvera/src/startup.bas) using proven `slideshow/startup.bas` logic.
    - Probes Slot 2 (`$C200`) and Slot 4 (`$C400`) registers and VRAM. If neither is installed, displays clear error and halts safely.
    - `build.bat` compiles both `MAIN.BIN` (Slot 2) and `MAIN4.BIN` (Slot 4, `-DVERA_BASE=0xC400`).
    - `build_hdv.mjs` registers both `MAIN.BIN` and `MAIN4.BIN` in the ProDOS root directory.
21. **Phase 1 & 2 Parity Upgrade (High-Fidelity Gameplay & 2-Player Mechanics)**:
    - **Extra Life System**: Awards bonus fighters at 10,000 pts (1st bonus) and every 50,000 pts thereafter (60k, 110k, 160k...), with instant HUD reserve update and `AUDIO_EXTRA_LIFE`.
    - **0.5s Multiplier Scoring**: 30-frame window; rapid consecutive kills escalate score: `100 -> 200 -> 300 -> 400` pts.
    - **Wave Squadron Wipeout Bonus (2,000 pts)**: Tracks 4-plane formation members; wiping out the full squad before they scatter triggers floating `2000` popup and 2,000 bonus points.
    - **Propeller Palette Cycling**: Stage 0, 1, 2 palette cycling of `colorPaletteSky` and `colorPaletteProps` (colors 14 & 15) every 4 frames for authentic spinning propellers/rotors.
    - **Spectacular Boss Defeat**: Killing the boss triggers simultaneous fiery explosions across all active enemies, bullets, and bombers.
    - **2-Player Alternating Mode**: Full state isolation (`score`, `lives`, `stage`, `enemiesKilled`, `nextExtraLife`, `alive`); alternates turn on death, dynamically updating `1-UP` and `2-UP` in HUD.
    - **Interactive High Score Initials Entry**: 30s timer, letter cycling (`A`..`Z`, `.`) with blinking cursor via `A`/`D`/Arrows, space/keypad confirmation, and ranking placement.
22. **Apple II Native Joystick Support & `[K]EYBOARD` / `[J]OYSTICK` Mode Toggle**:
    - **Hardware Discharge Routine (`read_pdl`)**: Inline 6502 discharge loop reading `$C070` trigger and `$C064` (PDL0 X-axis) / `$C065` (PDL1 Y-axis) timer decay (0..255).
    - **Smooth Heading Tracking**: 8-way directional polling maps stick direction to target angles (0, 4, 8, 12, 16, 20, 24, 28) and rotates player plane along the shortest angular arc.
    - **Pushbuttons**: Reads `$C061` (PB0 / Open Apple) and `$C062` (PB1 / Solid Apple) for laser cannon fire and game start.
    - **Intuitive UI & Default Joystick**: Defaults to Joystick mode (`useJoystick = 1`). Replaced confusing `[J]OYSTICK - 0` with side-by-side `[K]EYBOARD` and `[J]OYSTICK` on title screen, highlighting active mode in bright green.
    - **Dedicated `K` and `J` Keys**: Pressing `K` selects keyboard mode; pressing `J` selects joystick mode (audio pickup feedback on toggle).
23. **100% Visual Parity with CX16 (`cx16-1.jpg` & `cx16-2.jpg`) & Complete Author Credits**:
    - **Arcade Pitch-Black Title Background (`set_black_palette`)**: Title and attract loops lock VERA palette index 0 to solid black (`0x0000`), seamlessly integrating the 3D logo, attract text, and ranking table with the right-side status bar.
24. **A.D. 1940 Sea-Green Sky Dynamic Attract Demo AI & ProDOS Clean Loading**:
    - **One-Death Demo Climax & Game Over Wipe**: The demo fighter operates under authentic arcade rules without artificial godmode or infinite respawn loops. When shot down once, the full 12-frame explosion and 30-frame post-mortem world drift play out, followed immediately by the signature deep blue (`0x0006`) counter-clockwise radar sweep (`screen_wipe_to_sky(0)`) to clear the playfield and smoothly return to Title.
    - **Active Dogfight AI Autopilot (`demo_autopilot`)**: Autonomously tracks and locks onto airborne enemies, wave formations, WWII heavy bombers (`l1bomber`), and parachutes with shortest-arc rotation (`frame_toward`) and rapid 3-burst cannon fire.
    - **Paced Enemy Spawning & Dogfighting**: Enemy spawn interval in Demo Mode is set to 60 frames (1 second per plane), allowing genuine aerial dogfights and evasive maneuvers.
    - **PAT_NUMBERS VRAM Relocation (`0xF400`)**: Moved floating score popup patterns from `$E000` to `$F400`, resolving a 1536-byte overlap conflict with Stage 1's 4096-byte heavy bomber pattern (`PAT_BOMBER` at `$D800–$E800`).
    - **ProDOS Binary Shrink (30,139B at `$2000`)**: Adheres to standard ProDOS load address (`$2000`), protecting Applesoft BASIC interpreter workspace from memory corruption. Fits strictly within `$2000–$95BA`, leaving 70 bytes of verified safety headroom below `$9600`.
25. **Authentic Arcade Boss Victory & Stage Transition Flow**:
    - **Centered `STAGE CLEAR` in Current Era Sky**: Upon defeating the boss, the victory banner is displayed at Row 14, Col 8 (perfect horizontal centering across the 28-column playfield) for exactly 3.0 seconds (180 frames) directly on the current stage's background sky, preserving the thrill of triumph before transitioning.
    - **Inhibit Spawning During Celebration**: Leftover enemies and bullets are cleared and all spawning is suppressed during the 3-second victory celebration.
    - **Radar Sweep to Stage Announcement (`state = 4`)**: After 3 seconds, the counter-clockwise radar sweep (`screen_wipe_to_sky`) seamlessly rolls out the next era's palette and transitions into `state = 4` to display the authentic arcade era intro (`PLAYER 1` / `A.D. yyyy` / `STAGE n`) before combat begins.
    - **ProDOS Binary Footprint (30,105B at `$2000`)**: Spans `$2000–$9598`, preserving a verified 103-byte safety headroom strictly below the `$9600` ProDOS file buffer boundary.
26. **High Score Ranking Table Insertion Bugfix**:
    - **Top-Down Rank Traversal (`hs_insert`)**: Fixed a classic indexing bug where `hs_insert` looped from 4 down to 0 and broke on the first match, erroneously dumping all high scores into 5th place. Now traverses top-down from rank 1st (`0..4`), correctly placing scores (e.g. 14,800 pts properly awarded 2nd place above 8,086) and shifting lower entries downward.
    - **Streamlined Binary Footprint (30,000B at `$2000`)**: Binary measures an exact 30,000 bytes (mapped `$2000–$952F`), providing **208 bytes of verified headroom** safely below the `$9600` boundary.
27. **Right-Aligned Status Scores & 8-Slice Stage Fleet Elimination Progress**:
    - **Strict CX16 Score Alignment (`format_score_right`)**: All status labels (`HIGH SCORE`, `1-UP`, `2-UP`) and scores now align strictly to column 38. Numbers pad with leading spaces and format without artificial zero-padding (matching CX16 `65816`, `1400`, `00`), forming a vertical edge with the labels.
    - **8-Slice Progressive Fleet Depletion**: Relocated `PAT_PROG_ICON` to `0xFA00` with 8 pre-generated slice frames (cutting 2px per kill from left to right). The 6 fleet icon sprites slice away 1/8th per kill, progressively depleting across the 48 kills required for boss confrontation.
    - **Verified ProDOS Footprint (30,127B at `$2000`)**: Spans `$2000–$95AE`, retaining 81 bytes of headroom safely below the `$9600` boundary.
28. **3-Cycle Attract Loop Sequence Before Demo Play**:
    - **Precise 3-Cycle Cadence**: The attract state machine strictly enforces exactly 3 complete cycles of `Controls -> Hi Score Table` (each screen showing for 360 frames / 6 seconds, totaling 36 seconds of attract presentation). Only after the 3rd Hi Score Table finishes without any player input does the green-sky Demo Play launch.
    - **Reset on Input**: Any key/button activity or return from game/demo resets `attractCycleCount` to 0.
    - **Verified ProDOS Footprint (30,170B at `$2000`)**: Spans `$2000–$95D9`, safely retaining 38 bytes of headroom below the `$9600` boundary.
29. **VRAM Bank 1 Low RAM Relocation & Palette Corruption Fix**:
    - **Hardware Palette Overlap Root Cause**: VERA's 256-color hardware palette is mapped in VRAM Bank 1 at `$1FA00` (`PALETTE_ADDR = 0xFA00`). Previously, `PAT_PROG_ICON` was set to `0xFA00`, causing sprite pixel data to directly overwrite the hardware palette and crushing screen brightness.
    - **Safe Low-RAM Isolation (`0x1000` & `0x1800`)**: Relocated `PAT_NUMBERS` to `0x1000..0x15FF` and `PAT_PROG_ICON` to `0x1800..0x1BFF`. Both now reside safely in Bank 1's low RAM area, over 55KB away from the hardware palette, permanently eliminating color corruption.
    - **Verified ProDOS Footprint (30,143B at `$2000`)**: Spans `$2000–$95BE`, preserving 65 bytes of headroom below the `$9600` limit.
30. **Apple II VERA Slot I/O Boundary, PCM Stubs & Safe `$1400` ProDOS Base**:
    - **Apple II Slot I/O 16-Byte Register Limit**: VERA card registers on Apple II map strictly into a 16-byte slot I/O window (`$00..$0F`). PCM registers at `+$1B..+$1D` fall outside this window and hit adjacent Apple II slot spaces, causing hardware corruption and sprite instabilities. Removed out-of-range PCM accesses in favor of silent stubs pending future PSG chiptune implementation.
    - **ProDOS Safe Load Base (`$1400`)**: Memory between `$1000–$12FF` is reserved by ProDOS 8 for QUIT routine execution, making `$1000` unsafe for code loading. Implemented custom linker script `src/link1000.ld` targeting `$1400` as the program load base, cleanly expanding available memory to 33,280 bytes (`$1400–$95FF`) and leaving over 2.5KB of safe headroom.
31. **Stage Announce Steering & Disarmed Fire Parity with CX16 (`update_player_steering`)**:
    - **Interactive Steering During Stage Intro**: Extracted unified `update_player_steering(k, ku)` function and enabled it during `state = 4` (the 2.5s era announcement banner: `PLAYER 1 / A.D. XXXX / STAGE X` or `READY`). Players can freely rotate the fighter using keyboard (A/D/arrows) or Apple II analog joystick paddles.
    - **Disarmed Fire**: Space, 1, and joystick fire triggers are suppressed during the announcement countdown, preventing unintended bullet firing prior to dogfights.
    - **Heading-Coordinated Cloud Parallax**: Cloud drifting in `state = 4` now dynamically responds to the fighter's rotation vector (`scrollDx`, `scrollDy`), replacing static leftward drift with real-time parallax scrolling matching player orientation.
32. **Arcade Speed Pacing Calibration, 2-Frame Turn Stall & 60Hz Frame Lock**:
    - **Velocity Vector Calibration (`velDx/velDy ~2.0px/frame`)**: Reduced 32-angle movement vectors from 4.0 px/frame (3.3x overspeed) to a calibrated 2.0 px/frame. Brings player flight, enemy pursuit, and cloud parallax (1/2/3 px/frame) into exact alignment with Konami arcade and CX16 tempo.
    - **2-Frame Steering Stall (`KEY_READ_RATE = 2`)**: Implemented `steerStall` counter in `update_player_steering`. Holding down turn keys or joystick now steps 1 angle every 2 frames (30 turns/sec), locking full 360-degree rotation to exactly 1.0 second (60 frames) and eliminating turn overshoot.
33. **Deferred Life Deduction at Stage Announcement (CX16 & Arcade Parity)**:
    - **Post-Mortem Lives Preservation**: During the 12-frame explosion and 30-frame post-mortem world drift following fighter destruction, lives are no longer decremented immediately in `lose_life()`. The status bar reserve ships remain visually intact throughout the crash sequence.
    - **Synchronized Deduction on Stage Respawn**: Life decrement is now deferred to the exact moment `playerDeadTimer` expires, synchronizing the reserve ship deduction with the appearance of the `READY` announcement banner (or game-over transition if out of lives), matching authentic arcade presentation.
34. **Sprite Corruption Root-Cause Elimination, Ghost Sprite Purge (`hide_sprite`) & Progress Icon Height Fix**:
    - **Chunked Pattern Streaming**: Replaced byte-by-byte streaming with block-aligned chunk transfer in `upload_pattern_stream`. Ensures all ProDOS MLI disk I/O occurs strictly before programming VERA address registers, preventing mid-transfer disk reads from disrupting VERA VRAM address state (which previously corrupted the parachute and subsequent pattern assets).
    - **Cross-Block Progress Icon Buffering**: `ART_PROGRESS_FRAMES_OFF` (`0x99C0`) lands at offset 448 of block 76, straddling a 512-byte boundary with its lower 64 bytes in block 77. Previously, single-block reading truncated Row 4..7 to zeros, halving the sprite height from 8px to 4px. Now safely buffers all 128 bytes across both blocks, restoring the full vertical height of the biplane fleet depletion icons.
    - **Ghost Sprite Elimination (`hide_sprite`)**: Replaced `move_sprite(..., 0, 0)` with `hide_sprite(n)` across all bullet and enemy destruction paths. Setting Z-depth to 0 permanently purges decommissioned sprites from lingering visibly at the top-left origin `(0, 0)`.
35. **In-Place High-Score Initials Entry & Header Alignment**:
    - **Row Alignment Correction (`y = 7 + hs_row * 2`)**: Fixed a 1-row offset in `state = 2` where blinking initials cursor was rendered at row `8 + hs_row * 2` instead of the table row `7 + hs_row * 2`. Players now enter and edit their 3-letter initials directly in-place on the ranking line without splitting into a secondary row.
    - **Header Column Alignment (Col 4)**: Aligned `INPUT YOUR INITIALS` to column 4, matching `SCORE RANKING TABLE` (both 19 chars) and the left margin of the 1ST..5TH ranks.
36. **Stage Art Upload on Demo Play & Game Start Transition**:
    - **1940 Bomber & Weapon Assets Streaming**: Fixed missing `upload_stage_art()` call during `isDemoMode = 1` initialization. Now properly streams the 4096-byte 1940 Bomber (`PAT_BOMBER`), air-dropped bombs (`PAT_WEAPON`), green fighter planes, and clouds to VERA VRAM upon entering the attract demo, eliminating uninitialized random noise blocks entirely.
    - **Clean Return to Stage 0**: Added `upload_stage_art()` in `start_game_from_ui()` to ensure returning from the 1940 demo to a fresh 1910 game immediately swaps back the correct era assets.
37. **Title LOGO VRAM Isolation & Cloud Anti-Flicker Deadband Wraparound**:
    - **LOGO VRAM Collision Elimination (`0x2000/0x2400`)**: Relocated `PAT_LOGO_TIME` and `PAT_LOGO_PILOT` from `0xD000/0xD400` to Bank 1 low RAM (`0x2000/0x2400`). Prevents Stage 1 1940 bombs (`PAT_WEAPON = 0xD000`) from overwriting the title logo during demo play, keeping the title logo permanently pristine.
    - **Symmetric Deadband Cloud Wraparound**: Expanded horizontal cloud wrapping span (`224 + w + 32`, margins `[-w-16 .. 224+16]`) and vertical wrapping span (`288`, margins `[-32 .. 256]`). Eliminates cloud flickering caused by rapid AI dogfight steering reversals near playfield edges.
38. **Atomic Sprite Display Gating & Cloud Art Caching (Zero-Flicker Demo Transition)**:
    - **Sprites Display Blanking (`0x11 -> 0x51`)**: During all asset streaming in `start_game_from_ui()` and `isDemoMode = 1`, sprites are now held disabled (`VERA.display.video = 0x11`) while ProDOS MLI loads 1940 Bomber and weapon blobs. Once everything is initialized, sprites are re-enabled atomically at VSYNC (`0x51`), completely eliminating start-of-demo screen tearing and cloud flickering.
    - **Cloud Asset Cache Across Stages 0..3**: Implemented `lastCloudEra` cache check in `upload_stage_art()`. Clouds 0..2 are identical in eras 0 through 3, avoiding redundant 1792-byte disk reloads during transitions.
39. **Player Switch Cloud Reset & Apple2TS Negative Coordinates Strict Clamping**:
    - **Cloud Reset on Switch/Respawn (`reset_clouds()`)**: In 2-player switch (1P dead -> 2P, or 2P dead -> 1P) and 1P respawn, `reset_clouds()` now restores all 8 clouds to clean, non-edge center coordinates (`cloudInitX`, `cloudInitY`). Previously, clouds retained extreme death-drift positions, triggering rapid boundary wraparound flicker during stage announcements.
    - **Apple2TS Native Negative Coordinate Window**: Clamped wraparound boundaries strictly to `-w` and `-16`. Apple2TS `video.ts` only treats 10-bit coordinates as negative if `x >= 1024 - sprite_width` and `y >= 1024 - sprite_height`; exceeding these thresholds causes sprites to be interpreted as ~1007 positive pixels (disappearing from screen).
40. **Dual-Player High Score Entry Queue (`check_and_start_hs_entry`)**:
    - **Independent Player Score Qualification**: Replaced single global `score` checks in `hs_insert()` with parameterized `hs_insert_score(val)`. In two-player mode, 1P and 2P scores are preserved across player death switches. Upon Game Over, both players are sequentially evaluated against the ranking table.
    - **Sequential Initials Entry**: If both players qualify, Player 1 enters initials first, and upon completion the initials screen seamlessly sequences into Player 2 initials entry before returning to the title screen. If only 1P (or only 2P) qualifies, that player gets the interactive initials prompt directly.
    - **Prompt Identification**: Displays `PLAYER 1` or `PLAYER 2` at row 1 during high-score entry in 2-player mode.
41. **Pure Hardware VERA 16-Channel PSG Arcade Sound Engine**:
    - **Browser postMessage Flood Diagnosis**: Discovered that Apple2TS emulates VERA PCM by emitting a Web Worker `postMessage` on every single FIFO byte write (`$C21D`). At 60Hz and 128 bytes/frame, this bombarded the browser with 7,680 postMessages/sec, overwhelming the event loop, causing severe audio starvation (silence) and delaying display frame updates (cloud & sprite flickering).
    - **100% Zero-Disk, Zero-PCM, Zero-Flicker PSG Synthesis**: Completely replaced PCM streaming with direct VERA PSG synthesis in VRAM Bank 1 `$1F9C0..$1F9FF`. Channel 0 plays descending pulse laser fire; Channel 1 plays bomb whistle and sub-bass rumble; Channel 2 plays dynamic white noise explosions; Channel 3 plays cheerful rising arpeggios for parachute rescues and extra lives. Direct register writes require only ~4 bytes per sound event and ~50 cycles/frame for envelope decay, restoring 100% silky smooth 60 FPS graphics with zero flicker and crisp arcade audio.
42. **Game Pacing Halving, Announce Timer, and Seamless Demo Transition**:
    - **Blue Radar Wipe Transition to Demo**: Eliminated manual string blanking before demo play. `screen_wipe_to_sky(0)` now sweeps title text away directly with a clean counter-clockwise blue radar sweep, preventing blank-box flashing.
    - **Demo Abort to Title Screen**: Pressing Space, 1, 2, or any key during Demo Play now performs a clean radar wipe back to the Title Screen instead of instantly jumping into a live match, giving players full control over mode selection.
    - **Prolonged Stage Announce (~3.0s)**: Increased `announceT` to 180 frames (3.0s) for initial stage start and 100 frames (1.7s) for post-death respawn, ensuring era and stage numbers are clearly legible.
    - **Halved World Simulation Speed (30Hz Physics on 60Hz Display)**: Wrapped `update_game()` in `!(frameCount & 1)`. Frame rendering remains locked to 60 FPS VSYNC, but world physics, cloud drift, enemy pursuit, and bullet velocities now advance at 30Hz, halving the breakneck speed to authentic arcade pacing and making dogfights controllable and responsive.
    - **artBuf Relocation to $0800**: Replaced in-BSS `artBuf` with a pointer to Apple II Text Page 2 (`$0800`), reducing BSS size by 512 bytes and guaranteeing a safe 380-byte headroom below ProDOS `$9600`.

43. **Centered Fighter Bullet Spawn (+7 Pixel Offset Alignment)**:
    - **Sprite Coordinate Offset Diagnosis**: Diagnosed bullet offset where player missiles spawned 7 pixels to the left of the fighter nose. The player is a 16x16 sprite centered at offset +7..+8, while the 2x2 white bullet dot is positioned at `(0,0)..(1,1)` of its 8x8 sprite box. Spawning directly at `playerX` placed the dot at `playerX + 0` (far left wing). Added `+ 7` offset to `bulletX` and `bulletY` in `fire_bullet()`, perfectly aligning missile emission with the fighter nose tip across all 32 rotation headings, matching CX16 visual reference.
44. **Dynamic Flight Cruise in Intro Stage Announce & Decoupled Stage Audio Progression**:
    - **Active Flight Cruise During Opening Theme (`case 4`)**: While the 7.6-second `AUDIO_GAME_START` opening theme plays during the initial era announcement, player steering (`update_player_steering`), cloud parallax motion (`update_clouds`), and biplane propeller rotation (`update_propeller`) now run continuously in real-time. Players can steer freely across all 32 angles with clouds smoothly shifting drift vectors.
    - **Decoupled Opening Music for Stage 2+**: Added `isGameStartIntro` flag so `AUDIO_GAME_START` is exclusively triggered when starting a fresh game from the title screen. Advancing to Stage 2 (1940) and subsequent eras presents a clean ~1.6s (100 frames) stage announcement without re-triggering the long PCM music.
    - **PSG Fanfare for `AUDIO_NEXT_LEVEL`**: Decoupled `AUDIO_NEXT_LEVEL` from the PCM streaming path in `src/audio.c`, replacing it with a pure PSG two-tone rising chord fanfare so boss destruction stage clear transitions no longer replay the 7.6s PCM opening theme.

---

## Current Progress

### Done
- Full Time Pilot core loop: 32-way rotate, scroll world, home enemies, enemy/boss bullets,
  explosion animations, per-era sky/enemy/boss swap, score/lives/stage HUD.
- Right-side arcade status bar (black background), progress bar on row 27, reserve player fighter planes pointing UP, STAGE CLEAR banner (auto-clears).
- 5 eras with a boss each; stage progression clears the field between eras.
- **Title Screen 3D Logo & Attract Mode**: Dual 64x16 3D sprites (`time.png` + `pilot.png`) at `(48, 16)` and `(120, 16)`,
  100% authentic Konami arcade 8x8 font with `©` copyright symbol (`\x5E`), attract mode cycling with High Score Ranking table (`K.O`, `N.A`, `M.I`, `O.O`, `Y.A`).
- **Multi-scale clouds & asteroids**: 8 background sprites (small 16x16, medium 32x16, large 64x16),
  3-tier parallax scrolling, offscreen wrapping; stage 4 dynamically streams space asteroids (`astro0/1/2`).
- **Parachute rescue pilot**: 4-frame sway animation, 9-second spawn timer, downward drift, player rescue
  collision, escalating bonus points (1000..5000), pickup SFX, floating score popup.
- **All Era Weapons & 1940 Bomber Formation**:
  - Stage 1: 1940 bomber formation (`l1bomber.png`, 32x16, 8 frames, health 4), horizontal flight, drops bombs (`bomb.png`, 2 frames).
  - Stage 2: 1970 rotating boomerangs (`boomerang.png`, 8 frames).
  - Stage 3: 1982 homing tracking rockets (`rocket.png`, 16 frames).
  - Stage 4: 2001 space pulsating laser orbs (`sbullet.png`, 8 frames).
- **32x16 Large Explosions & Score Popups**:
  - Boss and bomber destruction uses `expl32x16.png` (32x16, 4 frames).
  - Floating score popups (`number.png`: 1000, 1500, 2000, 3000, 4000, 5000).
- **2-Player Mode**: Alternating 1-UP / 2-UP play with full state swap on death, dynamic HUD score highlighting.
- **Extra Life & Multiplier Scoring**: 10k / 50k thresholds and 0.5s rapid kill multipliers.
- **Formation Wave Wipeout Bonus**: 2,000 pts bonus on 4-plane squad wipeout.
- **Propeller Palette Cycling**: Hardware palette cycling for rotating propellers/rotors.
- **Interactive High Score Initials Entry**: Interactive letter cycling & cursor blinking.
- **Native Joystick Support & K/J Toggle**: Defaults to Joystick mode, title screen shows `[K]EYBOARD` and `[J]OYSTICK` with active highlight, PDL0/1 analog steering + PB0/1 fire.
- **CX16 Pixel-Exact Visual Parity & Full Author Credits**: Pitch-black title/attract background (`cx16-1.jpg`), right-aligned ranking scores, Anomixer 2026 author credits, overlay protection on ranking screen, rows 11/15/19 stage announce around centered plane (`cx16-2.jpg`).
- **1940 Sea-Green Sky Attract Demo Mode**: 1,472-frame replay recording, automatic launch on 4-cycle title idle, flashing `DEMO PLAY`, instant key/button break-out to real game.
- VERA PCM audio engine + ProDOS MLI streaming (currently gated off — see Audio note above).
- HDV packaging: MAIN.BIN + STARTUP + PCM1/2/3 + ART all visible in CATALOG.

### Active / In progress
- **Re-enable audio** cleanly: set `AUDIO_ENABLED=1` and move the PCM FIFO feeding to an
  interrupt/batch-driven path (veratest's music-IRQ pattern) so it doesn't block the main loop.

### Blocked / next
- Runtime verification is manual on apple2ts (`BRUN MAIN.BIN`).

---

## Build & Run

```bat
build.bat              # [1] compile C+asm -> build\main.bin
                       # [2] pack PCM audio + sprite art into blobs
                       # [3] node tools\build_hdv.mjs -> TimePilot-IIvera.hdv
```

Load `TimePilot-IIvera.hdv` into apple2ts; it boots to BASIC and BRUNs `MAIN.BIN`.

### Controls
- `A` / `←` : rotate counter-clockwise (1 step per press)
- `D` / `→` : rotate clockwise (1 step per press)
- WASD + keypad `8/4/6/2` + `7/9/1/3` : snap heading
- `SPACE` / `1` : fire / start 1P game
- `2` : start 2P game
- `K` : select `[K]EYBOARD` mode
- `J` : select `[J]OYSTICK` mode (default)
- `C` : toggle infinite lives cheat mode (displays green `CHEAT` on status bar)
- **Apple II Joystick**: PDL0/1 analog steering, PB0/1 fire buttons

> Note: Apple II keyboard strobe is **one-shot** — holding a key does not auto-repeat. This matters
> for the planned smooth-rotation feature (rotate one step per frame toward a target heading).

---

## File Map

| File | Purpose |
|------|---------|
| `src/main.c` | All game logic, VERA setup, HUD, screens, state machine |
| `src/apple2e.h` | **Apple IIe + VERA Hardware Definitions**: Native register mappings, `VERA_INC_*` strides, and softswitches |
| `src/startup.bas` | Applesoft BASIC startup script with Slot 2 & Slot 4 VERA card detection |
| `src/art.h` | **Authoritative sprite-art bitmaps** (player/enemy/boss/expl/bullet/cloud). `tools/mkart.mjs` reads this and packs it into `build/art.blob`. Do NOT delete. |
| `src/art_table.h` | Game-side constants for the art blob (`ART_START_BLOCK`, per-pattern byte offsets) — generated by `mkart.mjs`. Different from `art.h`: one is the data, one is the index. |
| `src/audio.c` | VERA PCM playback engine (gated by `AUDIO_ENABLED`) |
| `src/disk.c` | MLI READ_BLOCK streaming window (shared by audio + art) |
| `src/mli.s` | 6502 ProDOS MLI READ_BLOCK routine |
| `src/audio_table.h` | PCM sample table (`PCM_START_BLOCK`, per-source offsets/lengths) — generated by `mkpcm_blob.mjs`. |
| `src/font8x8.h` | 8×8 font bitmaps (+ SOLID_BLOCK tile at index 127) |
---

### 32. 800KB 輕量化 HDV 磁碟遷移（800KB Standard HDV Migration）
- **基礎映像來源改由 `800kb.hdv`（819,200 bytes，1,600 blocks）構建**：
  原先 32MB 巨大映像檔直接瘦身為標準 800KB。
- **無縫扇區分配**：
  - `pcm.blob`：固定於 Block 200..311（112 blocks）
  - `art.blob`：固定於 Block 900..1010（111 blocks）
  - `demo.blob`：固定於 Block 1020..1022（3 blocks）
  全部落於 1,600 blocks 範圍之內，6502 MLI `$80` 核心直接區塊讀取常數 100% 保持相容！
- **Block 6 Bitmap 精密點陣圖映射**：
  800KB 磁碟之 ProDOS 點陣圖完整存放於 Block 6 前 200 個位元組（$1600 / 8 = 200$），採用精準的 `byteIdx = b / 8` 與 `bit = 7 - (b % 8)` 標記。

---

### 33. `-Oz` 編譯器極限優化徹底消滅 `NO BUFFERS AVAILABLE`
- **根本成因**：
  加入全 5 關巨大 Boss 與無敵保護邏輯後，在 `-Os` 下 `MAIN.BIN` 體積增長至 33,645 bytes，載入跨度達 `$1400..$976D`，跨過 ProDOS `HIMEM`（`$9600`）達 365 bytes，導致 `BASIC.SYSTEM` 拒絕執行並拋出 `NO BUFFERS AVAILABLE BREAK IN 80`。
- **解決方案**：
  `build.bat` 全面升級為 `-Oz` 編譯旗標，二進位檔瞬間降至 **28,056 bytes**，結束位址縮回 **`$8198`**，距離 `$9600` 保留了超過 **5.2 KB** 的安全緩衝！

---

### 34. 全 5 關真·32x16 巨大 Boss 與方向轉動／螺旋槳動態到位
- **正版 32x16 巨大首領機陣容**：
  - **Stage 0 (1910)**：齊柏林巨型飛艇（Zeppelin / Blimp，長度 32 像素）
  - **Stage 1 (1940)**：四引擎重型轟炸機（Heavy Bomber，32x16）
  - **Stage 2 (1970)**：CH-47 雙旋翼契努克巨型運輸直升機（Chinook Helicopter，32x16）
  - **Stage 3 (1982)**：超音速隱形戰略轟炸機（B-52 / Supersonic Bomber，32x16）
  - **Stage 4 (2001)**：外星巨型指揮母艦（Alien Mothership，32x16）
- **左右航向面朝與轉動旋轉動畫**：
  往右飛使用 Frames 0..3，往左飛使用 Frames 4..7，帶有旋翼與螺旋槳旋轉特效。
- **真·32 像素碰撞盒**：
  碰撞寬度擴展為 32 像素（`bossXpos + 32`），開火點修正為機身正中心（`bossXpos + 16`）。

---

### 35. 第三關普通敵機直升機置中校準（Centered Helicopter Sprite Alignment）
- 原版 `l2enemy.png` 為 9x9 像素。原先直接貼在 16x16 畫布左上角，右下方留白導致視覺極度偏小。
- 現重新置中對齊（Offset x+3, y+3），端正座落於 16x16 中心，飛行與旋轉視覺比例正常飽滿。

---

### 36. 跨關卡幽靈殘留實體徹底清空 + 1.5 秒無敵防禦期
- **換關清空殘留物**：
  在 `STAGE CLEAR` 換關瞬間，徹底清空 `enemyOn`、`ebOn`、子彈與轟炸機，消滅上一關殘留子彈引發的「開局莫名暴斃」Bug。
- **1.5 秒（90 幀）無敵保護（`playerInvuln`）**：
  開局、換關與重生時，戰機享有 1.5 秒無敵閃爍保護，完全免疫任何撞擊與子彈。

---

### 37. `startup.bas` 第 65~67 行操作指引與全方向鍵盤支援
- **開機操作提示**：
  在檢測到 VERA 擴充卡後，清楚顯示操作說明：
  `CONTROLS: ARROWS / A,D = STEER`
  `SPACE = FIRE, 1/2 = START`
  `J/K = JOYSTICK/KEYBOARD`
- **全方向支援**：
  支援左右旋轉（`A/D`、`←/→`）以及上下導向（`W/S`、`↑/↓`）。

---

### 38. 廢棄 140KB `.po`，Git 全面納管 `TimePilot-IIvera.hdv` (800KB)
- 140KB 軟碟已無足夠空間容納 5 關巨大資產，已正式自工作目錄中刪除。
- `.gitignore` 放行 `!TimePilot-IIvera.hdv`，以標準 800KB 單一映像檔作為官方發行核心。

---

## File Map

| File | Purpose |
|------|---------|
| `src/main.c` | All game logic, VERA setup, HUD, screens, state machine |
| `src/apple2e.h` | **Apple IIe + VERA Hardware Definitions**: Native register mappings, `VERA_INC_*` strides, and softswitches |
| `src/startup.bas` | Applesoft BASIC startup script with Slot 2 & Slot 4 VERA card detection and control instructions |
| `src/art.h` | **Authoritative sprite-art bitmaps** (including authentic 32x16 bosses). `tools/mkart.mjs` reads this and packs it into `build/art.blob`. |
| `src/art_table.h` | Game-side constants for the art blob (`ART_START_BLOCK`, per-pattern byte offsets) — generated by `mkart.mjs`. |
| `src/audio.c` | VERA PCM streaming & 16-channel PSG sound engine |
| `src/disk.c` | MLI READ_BLOCK streaming window (shared by audio + art) |
| `src/mli.s` | 6502 ProDOS MLI READ_BLOCK routine |
| `src/audio_table.h` | PCM sample table (`PCM_START_BLOCK`, per-source offsets/lengths) — generated by `mkpcm_blob.mjs`. |
| `src/font8x8.h` | 8×8 font bitmaps (+ SOLID_BLOCK tile at index 127) |
| `800kb.hdv` | **Base 800KB ProDOS HDV Template** (1600 blocks) |
| `TimePilot-IIvera.hdv` | **Generated 800KB bootable hard disk image** (MAIN.BIN + MAIN4.BIN + STARTUP + PCM/ART/DEMO) |
| `tools/mkart.mjs` | Reads `src/art.h`, packs sprite art into `build/art.blob` + writes `art_table.h` |
| `tools/build_hdv.mjs` | ProDOS 800KB HDV builder (MAIN.BIN + MAIN4.BIN + STARTUP + PCM/ART files) |
| `build.bat` | Full build: compile Slot 2 & 4 (-Oz) → pack blobs → build 800KB HDV |

### Tools & external directories (what the build actually uses)

The build pulls from several sibling projects under `C:\dev`. If a path is missing,
the build fails fast with a clear error — check these exist before debugging code.

| Path | Purpose | Used by |
|------|---------|---------|
| `C:\dev\llvm-mos-sdk\install` | **llvm-mos SDK** (apple2e target). Compiler = `bin\mos-apple2e-clang.bat`; provides `apple2e.h`, `link.ld`. | `build.bat` step 1 |
| `C:\dev\llvm-mos-sdk\install\mos-platform\apple2e\include\apple2e.h` | **Apple IIe System Header**: Provides `apple2e.h`, hardware addresses, and VERA register mappings. | `#include "apple2e.h"` in `main.c:20` |
| `C:\dev\llvm-mos-sdk\install\mos-platform\common\include` | **MOS Common Headers**: Standard 6502 types and platform definitions. | llvm-mos compiler & IDE |
| `..\TimePilot-CX16\audio\*.pcm` | The **20 original CX16 PCM samples** (8-bit signed mono). Concatenated into the audio blob. | `tools/mkpcm_blob.mjs` |
| `..\TimePilot-CX16\src\*` | Full-fidelity reference source (palette, audio table, gameplay) to match against. | manual / reference only |
| `C:\dev\veratest\assets\ProDOS 2.4.3.hdv` | **Base ProDOS HDV** the game is packaged onto (bootable system disk). | `tools/build_hdv.mjs` |
| `C:\dev\veratest\src\applebasic.mjs` | Applesoft BASIC compiler — turns `startup.bas` into the BRUN launcher. | `tools/build_hdv.mjs` |
| `C:\dev\veratest\src\slideshow\*` | Proven patterns we copied: MLI streaming, music IRQ, ProDOS sapling-file layout. | reference only |
| `C:\dev\apple2ts` | The **emulator** used to verify (load the `.hdv`, BRUN MAIN.BIN). Also the source of truth for VERA render quirks. | manual / reference only |

### ⚠️ IDE & Language Server Notice (Antigravity IDE / VS Code)
> [!IMPORTANT]
> **Why is `'apple2e.h'` not in the local repo?**
> `apple2e.h` is **NOT** a missing file. It is the target-platform system header bundled with **llvm-mos SDK** at:
> ```text
> C:\dev\llvm-mos-sdk\install\mos-platform\apple2e\include\apple2e.h
> ```
> `mos-apple2e-clang` automatically injects this path during compilation (`build.bat` succeeds 100%).
> To prevent IDEs or AI agents from raising false red-squiggles:
> 1. **`.vscode/c_cpp_properties.json`** is configured with `includePath` pointing to `llvm-mos-sdk` include directories and defines `__MOS__`, `__MOS_APPLE2E__`, `VERA_BASE=0xC200`.
> 2. **`compile_flags.txt`** is provided in the repository root for `clangd`.
> **DO NOT** delete or replace `#include "apple2e.h"` in `src/main.c`.

Build pipeline (`build.bat`), in order:
1. **Compile** `src/main.c + audio.c + disk.c + mli.s` → `build\main.bin` (llvm-mos apple2e).
2. **Pack blobs**: `mkpcm_blob.mjs` reads CX16 `.pcm` → `build/pcm.blob` (+ writes `audio_table.h`);
   `mkart.mjs` reads `src/art.h` → `build/art.blob` (+ writes `art_table.h`).
3. **Build HDV**: `build_hdv.mjs` copies the base ProDOS HDV, adds MAIN.BIN + STARTUP + PCM/ART files → `TimePilot-IIvera.hdv`.

### Reference source files (elsewhere in repo)
- `TimePilot-CX16/src/data.c` — `colorPalette` / `colorPaletteSky` / `audioData[]` source of truth.
- `TimePilot-CX16/src/audio.c`, `game.c`, `input.c`, `ui.c` — the full-fidelity reference to match.
- `veratest/src/slideshow/slideshow.asm` + `slideshow_hdv.mjs` — proven MLI streaming, music IRQ,
  and ProDOS sapling-file layout (the patterns we copied).
- `apple2ts/src/worker/devices/vera/video.ts` — palette render (4-bit/channel), text-mode bg = `palette[0]`, vsync ISR flag.
- `llvm-mos-sdk/mos-platform/apple2e/link.ld` + `include/apple2e.h` — RAM region, VERA struct.
