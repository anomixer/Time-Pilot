# Time Pilot for Apple II+ / IIe + VERA (TimePilot-IIvera)

<div align="center">

[English](#english) | [繁體中文](#繁體中文)

</div>

---

<a name="english"></a>
# English

100% authentic, high-fidelity port of Konami's 1982 arcade classic **Time Pilot** for Apple II+ / IIe equipped with the **VERA (Versatile Embedded Retro Adapter)** FPGA expansion card.  
Directly adapted from Stefan Wessels' 2024 **TimePilot-CX16** and Apple IIgs versions.

---

## About Time Pilot

Piloting a futuristic fighter across five distinct historical eras, players engage in seamless 360-degree dogfights against era-specific enemy fleets while rescuing marooned parachute pilots drifting across space and time.

* **Fixed Center Screen**: The player plane is locked at the playfield center `(104, 112)`. The sky, clouds, and asteroids scroll smoothly with 3-tier parallax based on the fighter's flight heading.
* **Boss Battles**: Down enough enemy fighters to summon the era's flagship Boss. Destroy the boss to trigger massive multi-stage explosions, celebrate `STAGE CLEAR`, and initiate hyperspace **Time Warp** to the next era!

---

## ⚡ Architectural Breakthrough: 100% Zero-Disk Runtime Execution

Traditional Apple II games attempting to play digitized sound effects or stream multi-stage artwork must continuously invoke ProDOS MLI disk routines during gameplay, incurring tens of milliseconds of drive seek latency that causes severe frame stutter, missed keyboard strobes, and audio dropouts.

**TimePilot-IIvera** achieves an uncompromising **100% Zero-Disk Runtime Engine**:
1. **One-Time Boot Streaming (VERA 128KB VRAM as High-Speed SSD)**:
   * During boot, ProDOS Direct Block MLI (`$80`) streams **86,272 bytes (169 disk blocks) of 14 authentic arcade PCM samples** (game start theme, heavy explosions, time warp whoosh, bombs, sirens, weapon explosions, gunfire, and missiles) and **56,640 bytes (111 disk blocks) of all-era sprite artwork** directly into VERA's 128 KB dual-bank VRAM (Bank 0 and Bank 1).
2. **Disk Drive Completely Silent During Entire Play Session**:
   * Once the title screen appears and throughout all active gameplay, **the disk drive goes completely silent and the activity LED remains off**.
   * Dogfights, heavy explosions, multi-squad formation attacks, guided missile tracking, and even inter-era stage transitions (Boss Explosion ➔ STAGE CLEAR ➔ Time Warp hyperspace beam) execute with **zero disk reads**.
3. **Rock-Solid 60 FPS with < 1% CPU Overhead**:
   * During 60Hz vsync, the 6502 CPU transfers ~140 bytes of PCM audio from VRAM to VERA's 4KB hardware FIFO in tens of microseconds, consuming **less than 1% CPU budget** on a stock 1.02 MHz Apple II!

---

## Platform Comparison (CX16 vs. Apple IIgs vs. TimePilot-IIvera)

### 1. Hardware Specifications

| Specification | Commander X16 (CX16) | Apple IIgs (Native GS/OS) | Apple II+ / IIe + VERA (TimePilot-IIvera) |
| :--- | :--- | :--- | :--- |
| **Host System** | Commander X16 Retro PC | Apple IIgs (16-bit, 1986) | **Apple II+ (with 64KB) / IIe / IIgs / Laser 128**<br>(ProDOS requires 64KB RAM; Apple II+ requires Slot 0 16KB Language Card) |
| **CPU** | WDC 65C02S (8-bit) | WDC 65C816 (16-bit) | **MOS 6502 / 65C02 (Pure 8-bit challenge)** |
| **Clock Speed** | **8.0 MHz** (High computational budget) | **2.8 MHz** (16-bit instruction set) | **1.02 MHz** (1/8th of CX16 clock speed) |
| **Host RAM** | 512 KB ~ 2 MB Banked RAM | 1.25 MB ~ 8 MB Fast RAM | **64 KB ~ 128 KB Host RAM**<br>(Game core binary fits strictly under 29 KB, `$1400..$88D1`) |
| **Video Chip** | VERA FPGA (Onboard) | Apple IIgs VGC (Video Graphics Controller) | **VERA FPGA Interface Card (Slot 2 or Slot 4)** |
| **Video Memory** | 128 KB VRAM | 32 KB Video RAM (Mirrored) | **128 KB Dual-Bank VRAM** (Card dedicated, 0 host RAM overhead) |
| **Resolution** | 320 × 240 @ 60Hz | 320 × 200 @ 60Hz (Super Hi-Res) | **320 × 240 @ 60Hz** (Full 1:1 arcade aspect ratio) |
| **Hardware Sprites** | 128 hardware sprites | **None** (Software CPU blitting) | **128 hardware sprites** (Zero-flicker FPGA compositing) |
| **Color Palette** | 256 colors (12-bit RGB) | 16 palettes per line (256 colors total) | **256 colors** (12-bit RGB, dynamic stage swap & propeller cycling) |
| **Audio Hardware** | VERA PCM FIFO + YM2151 FM | Ensoniq 5503 DOC (32-oscillator wavetable) | **VERA 16-Channel Stereo PSG (+6dB unison) + 14 Resident PCM Samples** |
| **Storage & Boot** | SD Card (FAT32, PRG) | 3.5" 800KB Disk / 2MG Image | **Bootable ProDOS 2.4.3 800KB HDV** (MLI `$80` direct streaming, zero runtime I/O) |

---

### 2. Game Presentation & Engine Features

| Feature | Commander X16 (CX16) | Apple IIgs (Native GS/OS) | Apple II+ / IIe + VERA (TimePilot-IIvera) |
| :--- | :--- | :--- | :--- |
| **Aspect Ratio** | 320 × 240 full field + right status bar | 320 × 200 compressed field | **320 × 240 Arcade Perfect**: Left 28 cols playfield + Right 12 cols pure black status bar |
| **Sprite Engine** | Hardware FPGA sprites | Software blitting (Racing the Beam) | **Hardware FPGA sprites**: 128 zero-flicker sprites, solid 60 FPS at 1.02 MHz |
| **Pre-Game Announce** | Static announce screen | Static announce with Ensoniq music | **Dynamic Flight Announce**: Plane and clouds actively cruise during the 7.1s intro theme! |
| **Sound System** | Single PCM queue | Ensoniq DOC wavetable | **Dual Hybrid Sound Engine**: 16-channel PSG (+6dB laser, explosion rumble) + 14 PCM samples |
| **Boss Destruction** | Short noise burst | Ensoniq synth explosion | **1.38s Authentic Arcade Heavy PCM Explosion** with 32×16 multi-stage billowing firestorm |
| **Time Warp Effect** | 22-step beam animation | Custom scene transition | **100% CX16 22-Step Hyperspace Beam Animation** + 1.20s PCM warp whoosh + 360° radar sweep |
| **Stage 5 UFO Colors** | Dynamic Cyan & Magenta | Software-rendered sprites | **Arcade-Exact Electric Cyan (`0x00CF`)** with dynamic Magenta (`0x0C0C`) damage alarm flashing |
| **Controls** | Keyboard / CX16 Gamepad | Keyboard / Joystick | **Seamless Dual Input**: Keyboard (WASD/Arrows/Numpad) + Apple II Analog Joystick (PDL0/1) |

---

## Controls

Supports Apple II keyboard and native analog joystick input:

| Input / Key | Action |
| :--- | :--- |
| `W` or `↑` (Up Arrow) | Steer Up (`0`): Single tap sets target; fighter smoothly turns through 32 directions along shortest arc |
| `S` or `X` or `↓` (Down Arrow) | Steer Down (`16`): Both `S` and `X` steer downward |
| `A` or `←` (Left Arrow) | Steer Left (`24`) |
| `D` or `→` (Right Arrow) | Steer Right (`8`) |
| `Q` | Rotate counter-clockwise (steps 1 heading in 16-dir scale / 2 steps in 32-dir) |
| `E` | Rotate clockwise (steps 1 heading in 16-dir scale / 2 steps in 32-dir) |
| `Space` or `1` | Fire laser cannons / Start 1-Player game (`1-UP`) |
| `2` | Start 2-Player alternating game (`2-UP`) |
| `P` | **Pause Game**: Toggles pause mode (displays red `PAUSED` banner, resumes on `P`/`Space`/fire) |
| `K` | **Toggle Keyboard Mode**: Activates keyboard controls (`[K]EYBOARD` highlighted in green; avoids paddle lag/drift) |
| `J` | **Toggle Joystick Mode**: Activates Apple II analog joystick (`[J]OYSTICK` highlighted in green, **Default**) |
| `I` | **Infinite Lives Cheat Toggle**: Toggles infinite lives practice mode (displays green `INFINITE` on status bar) |
| **Apple II Analog Joystick** | Smooth proportional steering toward stick angle, Button 0/1 (Open/Solid Apple) fires cannons |

---

## Gameplay & Rules

### 1. 360-Degree Parallax Skies
* The player plane remains centered at `(104, 112)`, while the sky and 8 multi-scale background clouds/asteroids drift smoothly with 3-tier parallax scrolling according to flight direction.

### 2. The 5 Historical Eras & Unique Weapons
* **Stage 1: A.D. 1910 (Biplane Era)**
  * **Enemies**: Classic World War I biplanes (16×16, 8-way rotation).
  * **Sky**: Deep Blue (`0x0006`).
  * **Boss**: **Zeppelin / Blimp** (32×16, dual animated propellers, 5 HP).
* **Stage 2: A.D. 1940 (WWII Propeller Era)**
  * **Enemies**: Monoplane WWII fighters (16×16, 8-way rotation).
  * **Sky**: Military Sea-Green (`0x0052`, also used in the 1,472-frame Attract Demo).
  * **Threat**: **Heavy Bomber** flying horizontally across the sky, dropping vertical bombs (`bomb`). Destroying it awards **1,500 bonus points**!
  * **Boss**: **4-Engine Heavy Bomber Flagship** (32×16, 6 HP).
* **Stage 3: A.D. 1970 (Helicopter Era)**
  * **Enemies**: Combat helicopters (16×16, animated rotor, landing skids), firing spinning boomerangs (`boomerang`).
  * **Sky**: Dark Forest Green (`0x0063`).
  * **Boss**: **CH-47 Chinook Twin-Rotor Helicopter** (32×16, dual spinning rotors, 6 HP).
* **Stage 4: A.D. 1982 (Supersonic Jet Era)**
  * **Enemies**: Delta-wing supersonic jet fighters (16×16, 8-way rotation).
  * **Sky**: Dark Dusk Magenta (`0x0505`).
  * **Threat**: Heat-seeking homing tracking rockets (`rocket`) that steer toward the player plane!
  * **Boss**: **B-52 / Supersonic Stealth Bomber** (32×16, 7 HP).
* **Stage 5: A.D. 2001 (Future Space Era)**
  * **Enemies**: Agile flying saucers (16×16 UFO, pulsating energy glow), firing 8-frame shifting laser pulses (`sbullet`).
  * **Sky**: Deep Black Space (`0x0000`), filled with floating space asteroids (`astro0/1/2`).
  * **Boss**: **Alien Command Mothership** (32×16, 8 HP). Domes and core render in authentic arcade Cyan (`0x00CF`), pulsating rapidly to Magenta (`0x0C0C`) when damaged below 66% health!

### 3. Dogfight AI & 4-Plane Wave Bonus
* **Lead-Angle Interception**: Enemy squadrons calculate 32-way headings to intercept the player head-on.
* **4-Plane Formation Bonus**: Air raid sirens sound every ~10 seconds as a 4-plane attack wing dives into the arena (`spawn_wave`). Destroy all 4 planes before they break formation to trigger a floating `2000` score popup and earn **2,000 bonus points**!

### 4. Parachute Pilot Rescue
* A drifting parachute pilot sways into the combat zone approximately every 9 seconds in eras 1910–1982.
* Fly into the pilot to rescue him and claim escalating bonus points:
  * 1st Rescue: **1,000 pts** | 2nd Rescue: **2,000 pts** | 3rd Rescue: **3,000 pts** | 4th Rescue: **4,000 pts** | 5th+ Rescue: **5,000 pts**!

### 5. Status Bar & 48-Kill Fleet Progress Bar
* Located in the right 12 columns (`x = 224..320`):
  * **Score Bar**: `HIGH SCORE`, `1-UP`, `2-UP` right-aligned.
  * **Era Marker**: Mini 8×8 era craft icons indicate current stage (1 to 5).
  * **Reserve Fleet**: Player fighter icons represent remaining lives.
  * **Kill Progress Bar**: 6 biplane icons smoothly decrement toward the **48-kill quota**. When depleted, sirens wail and the stage Boss descends!

### 6. Hyperspace Time Warp & Radar Transition
* Defeating the Boss displays `STAGE CLEAR` at Row 10 (24 pixels above the centered fighter plane).
* After 3 seconds, a 22-step white incandescent hyperspace beam envelops the fighter accompanied by the 1.20s PCM warp whoosh. The beam collapses, the plane jumps forward in time, and a 360-degree counter-clockwise radar sweep reveals the new era!

### 7. High Score Ranking Table
* Features a full **7-digit high score display** with the units digit strictly aligned under the `'G'` of `SCORE RANKING TABLE` at **Column 16**, verbatim matching the original Commander X16 (`cx16-1.jpg`) and arcade cabinet layout.

---

## Dual-Engine Sound Architecture

1. **16-Channel Stereo PSG Synthesizer**:
   * **Player Laser (`AUDIO_PLAYER_SHOOT`)**: Channels 0 and 4 in tight unison double acoustic output (+6 dB) with instant 4-frame attack.
   * **Explosion Rumble (`AUDIO_ENEMY_EXPLODE`)**: White noise combined with low-frequency sawtooth shockwave punch.
   * **Arpeggios & Fanfares**: Hardware-synthesized pickup arpeggios, missile sirens, and victory chords.
2. **Dual-Bank VRAM Resident 14 Arcade PCM Samples**:
   * **Bank 0 (`$1000..$F5C4`, 58.8 KB)**: `AUDIO_GAME_START` (7.10s opening theme), `AUDIO_BOMB` (0.60s whistle), `AUDIO_BIG_EXPLOSION` (1.38s arcade blast).
   * **Bank 1 (`$1200..$7D3B`, 27.5 KB)**: `AUDIO_COINDROP`, `AUDIO_ROCKET_LAUNCH`, `AUDIO_ROCKET_FLY`, `AUDIO_WAVE_START`, `AUDIO_BOSSL0..3`, `AUDIO_WAPON_EXPLODE`, `AUDIO_ENEMY_SHOOT`, and `AUDIO_TIMEWARP` (1.20s whoosh). 709 bytes safe headroom before sprite RAM (`$8000`).

---

## Build & Run

### Prerequisites
* **llvm-mos SDK** (target: `mos-apple2e`)
* **Node.js** (for HDV and asset packers)

### One-Step Build
In the `TimePilot-IIvera/` directory:
```bat
build.bat
```
Build pipeline:
1. `tools/mkpcm_blob.mjs` + `tools/mkart.mjs`: Packs PCM audio and 32×16 sprite assets into blobs.
2. `mos-apple2e-clang`: Compiles `MAIN.BIN` (Slot 2) and `MAIN4.BIN` (Slot 4) with `-Oz`.
3. `tools/build_hdv.mjs`: Packages a standard 800 KB ProDOS 2.4.3 bootable image `TimePilot-IIvera.hdv`.

### Running in Emulator
* Load `TimePilot-IIvera.hdv` into **Apple2TS** or any Apple II emulator supporting the VERA card. The disk image boots straight to BASIC and launches the game!

---

<a name="繁體中文"></a>
# 繁體中文

1982 Konami 街機經典《Time Pilot》（時空領航員）Apple II+ / IIe + VERA 擴充卡 100% 原版規格高傳真移植版。  
本專案直接繼承與改編自 Stefan Wessels 於 2024 年發布之 **TimePilot-CX16** 與 Apple IIgs 版本。

---

## 遊戲簡介 (About Time Pilot)

玩家駕駛一架超越時代的未來戰鬥機，穿梭於五個不同的歷史時空，在 360 度全向無邊界的天空中與各時代的敵機展開空中纏鬥，同時救援漂流在各個時空的受困跳傘飛行員。

* 戰機始終位於螢幕正中央 `(104, 112)`，背景天空與大小雲層／太空隕石隨戰機航向產生流暢的 3 階視差捲動。
* 擊墜足夠數量的敵機後，該時代的巨型母艦（Boss）將會登場。擊毀母艦後引發巨型連環爆炸並通關（`STAGE CLEAR`）躍遷至下一個時空！

---

## ⚡ 核心技術突破：運行中 100% 零磁碟 I/O (100% Zero-Disk Runtime Engine)

傳統 Apple II 遊戲若要在遊玩時播放豐富的語音或長取樣音效，CPU 必須頻繁調用 ProDOS MLI 讀取軟碟或硬碟，磁區尋道與傳輸動輒耗費數十毫秒，往往導致嚴重的畫面卡頓、按鍵漏判定或音效斷音。

本移植版在架構上實現了突破性的創新設計：
1. **開機一次載入，VRAM 充當超高速板載 SSD**：
   * 開機引導階段透過 ProDOS MLI 直讀，一口氣將 **86,272 位元組（169 個磁區）的 14 首街機 PCM 取樣**（包含開場音樂、重低音大爆炸、時空躍遷 Time Warp 穿梭音、機槍掃射、攔截爆破等）與 **56,640 位元組（111 個磁區）的全時代精靈圖庫**，直接寫入 VERA 擴充卡的 128 KB 獨立雙 Bank 記憶體（Bank 0 與 Bank 1）。
2. **戰鬥與換關全程零讀碟（Disk Drive Completely Silent）**：
   * 進入標題畫面與遊戲戰鬥後，**磁碟機完全靜音、讀取指示燈全程熄滅**！
   * 無論是激烈的空戰纏鬥、重低音大爆炸、飛彈發射、甚至是擊敗 Boss 後的「大爆炸 ➔ STAGE CLEAR 凱旋和弦 ➔ Time Warp 躍遷光束」與跨時代換關，**中途 100% 不讀取任何一個磁區**！
3. **滿幀 60 FPS 與 CPU 負載 < 1%**：
   * 戰鬥中播放 PCM 時，6502 CPU 僅在每幀 60Hz Vsync 中斷鉤子花費數十微秒，將約 140 位元組由 VERA VRAM 複製至 VERA 4KB 硬體 FIFO 暫存器，**CPU 負載小於 1%**，締造業界罕見的絲滑街機流暢度！

---

## 三大平台版本差異對照表 (Platform Comparison: CX16 vs. IIgs vs. IIvera)

### 1. 硬體規格與系統架構對照 (Hardware Specifications)

| 規格維度 | Commander X16 (CX16) | Apple IIgs (GS/OS 原生) | Apple II+ / IIe + VERA (TimePilot-IIvera) |
| :--- | :--- | :--- | :--- |
| **主機平台** | Commander X16 現代復古電腦 | Apple IIgs (1986) 16 位元個人電腦 | **Apple II+ (需 64KB) / IIe / IIgs / Laser 128**<br>(ProDOS 嚴格要求 64KB RAM；Apple II+ 需加裝 Slot 0 16KB 語言卡擴充至 64KB，未擴充之初代 48K Apple II 無法運行 ProDOS) |
| **CPU 處理器** | WDC 65C02S (8-bit) | WDC 65C816 (16-bit) | **MOS 6502 / 65C02 (純 8-bit 極限挑戰)** |
| **運作時脈** | **8.0 MHz** (算力極度充裕) | **2.8 MHz** (16 位元指令集) | **1.02 MHz** (算力僅 CX16 的 1/8) |
| **主機 RAM 記憶體** | 512 KB ~ 2 MB (Banked RAM) | 1.25 MB ~ 8 MB Fast RAM | **主機僅 64 KB ~ 128 KB**<br>程式碼嚴格壓在 29 KB (`$1400..$88D1`) |
| **圖形顯示晶片** | VERA FPGA (主機板內建) | Apple IIgs VGC (Video Graphics Controller) | **VERA FPGA 介面卡 (外接於 Slot 2 或 Slot 4)** |
| **獨立視訊記憶體** | 128 KB VRAM | 32 KB Video RAM (映照於 Fast RAM) | **128 KB VRAM** (擴充卡專屬，不耗主機 RAM) |
| **原生解析度** | 320 × 240 @ 60Hz | 320 × 200 @ 60Hz (Super Hi-Res) | **320 × 240 @ 60Hz** (全畫面 1:1 滿版輸出) |
| **精靈硬體支援** | 128 個硬體精靈 (FPGA 自動合成) | **無硬體精靈**<br>(需由 65816 CPU 軟體即時擦除與繪製) | **128 個硬體精靈** (FPGA 零撕裂硬體合成) |
| **色盤能力 (Palette)** | 256 色 (12-bit RGB，4096 色選 256) | 16 個調色盤 (每掃描線 16 色，共 256 色) | **256 色** (12-bit RGB，支援動態換關、螺旋槳輪色與母艦青藍/洋紅警報) |
| **音效硬體架構** | VERA PCM FIFO (單聲道) + YM2151 FM | Ensoniq 5503 DOC (32 振盪器波表晶片) | **VERA 16 通道立體聲 PSG (雙聲道齊奏 +6dB) + 雙 Bank VRAM 常駐 14 首 PCM 音效** |
| **儲存媒介與格式** | SD 卡 (FAT32 檔案系統，PRG 載入) | 3.5 吋 800KB 磁碟 / 2MG 映像檔 | **標準 ProDOS 2.4.3 800KB HDV 映像檔**<br>(MLI `$80` 核心直讀，開機全數常駐 VRAM，運行中 100% 零磁碟 I/O) |

---

### 2. 遊戲呈現狀態與核心機制差異 (Game Presentation & Engine Features)

| 遊戲呈現維度 | Commander X16 (CX16) | Apple IIgs (GS/OS 原生) | Apple II+ / IIe + VERA (TimePilot-IIvera) |
| :--- | :--- | :--- | :--- |
| **垂直視野與版面** | 320 × 240 完整視野 (40×30 比例)<br>右側黑底狀態列 | 320 × 200 壓縮視野 (垂直少 40 像素)<br>右側黑底狀態列 | **320 × 240 完美街機比例**<br>左側 28 欄戰場 + 右側 12 欄純黑狀態列 (T256C=0) |
| **精靈繪製技術** | FPGA 硬體精靈合成，無畫面閃爍 | 純 CPU 軟體貼圖 (Mr Sprite 產生之 65816 碼)<br>需靠 **Racing the Beam** 追光束防撕裂 | **FPGA 硬體精靈合成**，128 個精靈無閃爍撕裂，1.02MHz 即可滿幀 60 FPS 運行 |
| **開局宣告體驗** | 靜態宣告畫面，開場曲 7.13 秒 | 靜態宣告畫面，播放 Ensoniq 波表合成音樂 | **動態巡航宣告畫面**：開場曲播放 7.1 秒期間，**戰機與雲朵即時飛行流動**，支援 32 方位轉向與螺旋槳動態旋轉！ |
| **開場主題曲規格** | 7.13 秒 (CX16 原裝取樣，旋律自然淡出收尾) | Ensoniq DOC 晶片重製版 | **7.10 秒街機母帶完整版**<br>(消除 1.0 秒死音，旋律完結自然進入戰鬥) |
| **多音軌音效架構** | 單軌 PCM 優先權互斥佇列 | Ensoniq DOC 專屬多聲道波表合成 | **極限複合雙音效引擎**：<br>1. **PSG 雙聲道疊加齊奏 (+6dB)**：雷射、爆破、敵彈、凱旋和弦多聲道並行！<br>2. **雙 Bank 常駐 14 首 PCM**：開場曲、大爆炸、投幣、炸彈、飛彈、敵機機槍、攔截爆破、時空躍遷與四大 Boss 警報無縫串流！ |
| **玩家爆炸震撼度** | 單軌 PCM 短雜音爆破 | Ensoniq 爆炸波表合成音效 | **真·1.38 秒正宗大型電玩重低音 PCM 爆炸**<br>伴隨 32×16 烈焰連環爆破與破片黑煙，PSG 背景音效依然並行不悖！ |
| **過關躍遷特效** | 22 步白光曲速光束 (Time Warp)<br>戰機置中閃爍跳躍後縮為單點 | 專屬過關過場動畫 | **100% 完整還原 CX16 22 步曲速光束字型動畫**<br>白光聚能膨脹 + 戰機高頻閃爍 + 1.20s PCM 穿梭音 + 360° 雷達掃描換關 |
| **關卡過渡音樂** | 換關重複觸發開場 PCM 主題曲 | 原生過關轉場音效 | **過關音樂完全解耦**：第 2 關起換關宣告改為短暫 PSG 凱旋和弦，開場大曲僅在首局觸發 |
| **巨大 Boss 呈現** | 32 × 16 巨大首領機陣容<br>(Blimp/Bomber/Chinook/B-52/Mothership) | 32 × 16 巨大首領機 (65816 軟體繪製) | **正版 32 × 16 巨大首領機**，具備左右航向、4 階漸進中彈受創冒煙、第五關外星母艦正宗電光青藍 (Cyan) 原色與受創洋紅警報閃爍！ |
| **操作輸入支援** | 鍵盤 / CX16 遊戲手把 (數位 D-Pad) | 鍵盤 (Option/Apple) / 類比搖桿 | **雙模式無縫切換**：<br>1. 鍵盤（WASD / 方向鍵 / 數字鍵盤）<br>2. **Apple II 原生硬體放電類比搖桿 (PDL0/1)** 平滑 32 方位導向 |

---

## 操作說明 (Controls)

本遊戲支援 Apple II 鍵盤與原生類比搖桿即時輸入：

| 按鍵 / 控制器 | 功能說明 |
| :--- | :--- |
| `W` 或 `↑` (Up Arrow) | **朝上轉向 (`0`)**：單按一次設定目標，戰機沿最短路徑平滑經歷 32 向旋轉到位 |
| `S` 或 `X` 或 `↓` (Down Arrow) | **朝下轉向 (`16`)**：`S` 與 `X` 皆為朝下方向 |
| `A` 或 `←` (Left Arrow) | **朝左轉向 (`24`)** |
| `D` 或 `→` (Right Arrow) | **朝右轉向 (`8`)** |
| `Q` | **逆時針旋轉**：每按一次以 16 方位轉向一格（等同 32 向轉 2 格，平滑過渡） |
| `E` | **順時針旋轉**：每按一次以 16 方位轉向一格（等同 32 向轉 2 格，平滑過渡） |
| `Space` (空白鍵) 或 `1` | 雷射機砲發射 / 標題畫面啟動單人遊戲 (1-UP) |
| `2` | 標題畫面啟動雙人輪流遊戲 (2-UP) |
| `P` | **暫停遊戲 (Pause)**：即時凍結戰局（畫面中央紅字 `PAUSED`，按 `P`/`Space`/搖桿開火鍵解除） |
| `K` | **切換鍵盤控制**：啟用純鍵盤模式（標題畫面 `[K]EYBOARD` 綠色高亮，完全避免未接搖桿浮動飄移與 CPU 延遲） |
| `J` | **切換搖桿控制**：啟用 Apple II 類比搖桿模式（**預設啟用**，標題畫面 `[J]OYSTICK` 綠色高亮，鍵盤仍可隨時操作） |
| `I` | **密技開關**：切換無限生命練習模式（狀態列顯示綠色 `INFINITE`） |
| **Apple II 類比搖桿** | 8 向推桿即時向目標角度平滑旋轉，Button 0/1（Open/Solid Apple）發射機砲與開始遊戲 |

---

## 核心玩法與遊戲規則 (Gameplay & Rules)

### 1. 360 度視差飛行世界 (Parallax Skies)
* 玩家戰機固定於螢幕中央 `(104, 112)`，世界背景與 8 朵大小雲層（小雲 16×16、中雲 32×16、大雲 64×16）依照戰機航向產生 3 階視差滑動。
* 戰機轉向時，世界與雲層平滑改變飄移方向，帶來無邊界全方位翱翔的真實沉浸感。

### 2. 五大歷史時代與時代專屬武器 (The 5 Eras & Era Weapons)
* **第一關：A.D. 1910（雙翼機時代）**
  * **敵機**：一戰經典雙翼教練機（16×16，雙層機翼與尾翼，8 向旋轉），發射標準機槍子彈。
  * **天空**：蔚藍深空（Deep Blue，`0x0006`）。
  * **Boss**：**齊柏林巨型飛艇（Zeppelin / Blimp）**（長度達 32 像素，前後動態螺旋槳旋轉，HP 5）。
* **第二關：A.D. 1940（二戰螺旋槳時代）**
  * **敵機**：二戰單翼戰鬥機（16×16，8 向旋轉）。
  * **天空**：軍武深綠（Dark Green，`0x0052`，**Attract Demo 示範模式亦為此關**）。
  * **專屬威脅**：**四引擎重型轟炸機（Heavy Bomber）** 橫穿空域，沿途垂直空投毀滅炸彈（`bomb`）！擊落轟炸機可獲得 1,500 分獎勵！
  * **Boss**：**二戰四發重型轟炸機首領**（32×16，HP 6）。
* **第三關：A.D. 1970（直升機時代）**
  * **敵機**：武裝直升機（16×16，正宗 9 向旋轉機身、動態主旋翼與著陸滑橇），發射高速旋轉的導向迴力鏢（`boomerang`）。
  * **天空**：翡翠深綠（Forest Green，`0x0063`）。
  * **Boss**：**CH-47 雙旋翼巨型直升機（Chinook Helicopter）**（32×16，前後雙旋翼高速旋轉，HP 6）。
* **第四關：A.D. 1982（超音速噴射機時代）**
  * **敵機**：三角翼超音速噴射戰機（16×16，8 向旋轉）。
  * **天空**：暗紅暮色（Dark Red / Magenta，`0x0505`）。
  * **專屬威脅**：敵機發射**紅外線熱追蹤導向飛彈（`rocket`）**，飛彈會在飛行中依玩家航道自動修正追蹤軌跡！
  * **Boss**：**超音速隱形戰略轟炸機（B-52 / Supersonic Bomber）**（32×16，HP 7）。
* **第五關：A.D. 2001（未來太空時代）**
  * **背景**：漆黑深邃太空（Deep Black Space，`0x0000`，漂浮著大小不一的太空隕石取代雲層）。
  * **敵機**：敏捷的外星飛碟（16×16 UFO，4 影格能量光環持續頻閃），發射 8 影格色彩變換的能量雷射脈衝光球（`sbullet`）！
  * **Boss**：**外星指揮太空母艦（Alien Command Mothership）**（32×16，HP 8）。座艙罩與燈條還原 1982 街機電光青藍色（Cyan，`0x00CF`）；受創低於 66% 時在青藍與洋紅（Magenta，`0x0C0C`）之間高速閃爍警報！

### 3. 空戰纏鬥獵殺 AI 與 4 機編隊突襲 (Dogfight AI & Wave Bonus)
* **航向預判迎頭攔截**：敵機依照 32 向周界演算法，永遠生成於玩家戰機正前方航道，迎頭包抄壓迫。
* **動態迴旋咬尾**：敵機具備主動發動機推力，並依據夾角以最短弧度（順/逆時針）動態壓機側轉咬住玩家機尾。
* **編隊突襲與 2,000 分波次獎勵**：每約 10 秒防空警報響起，前方大軍壓境發動 4 機俯衝突襲編隊（`spawn_wave`）。若玩家在編隊散開前**全數將 4 架敵機擊落**，在最後一架爆炸處立即觸發浮動 `2000` 獎勵特效並獲得 **2,000 分**！

### 4. 跳傘飛行員救援系統 (Parachute Rescue)
* 在 1910～1982 關卡中，每約 9 秒會有一名跳傘飛行員（4 影格搖曳動畫）由上方隨風飄落。
* 駕駛戰機碰觸飛行員即可成功救援，獲得階梯遞增獎勵：
  * **第 1 次救援**：1,000 分 | **第 2 次救援**：2,000 分 | **第 3 次救援**：3,000 分 | **第 4 次救援**：4,000 分 | **第 5 次及以上**：**5,000 分**！

### 5. 右側狀態欄與 48 架擊墜進度隊列 (Status Bar & Stage Fleet Progress)
狀態欄設於畫面右側 12 欄（`x = 224..320`，純黑邊欄）：
* **頂部計分欄**：`HIGH SCORE`、`1-UP`、`2-UP` 分數均向右靠齊。
* **時代關卡圖標**：右側邊緣以 8×8 微型戰機精靈即時標示當前關卡數（1 到 5 架小飛機由右至左整齊排列）。
* **備用戰機**：右下方整齊排列白色噴射戰機精靈，即時反映剩餘備用命數。
* **擊墜進度敵機隊列**：底部排列 6 架扁平雙翼機圖標。關卡總擊墜門檻為 **48 架敵機**，每擊落 1 架敵機即平滑切削消減；當 6 架進度機全數消失時，空襲警報響起，巨型母艦 Boss 進場決戰！

### 6. 時空躍遷超空間光束 (Time Warp) 與 360 度雷達轉場
* **STAGE CLEAR 慶祝**：擊落母艦後畫面上空（Row 10）亮起 `STAGE CLEAR` 置中歡慶 3.0 秒，與中央戰機保持 24 像素乾淨天際空距，不再壓到戰機！
* **22 步動態超空間曲速光束與 1.2 秒正宗街機 PCM**：曲速光束啟動時同步引爆正宗 1982 街機 Time Warp 升頻穿梭 PCM 原音；背景雲朵自動隱藏確保視界純淨無阻；亮白光束橫貫 28 欄全寬劇烈激盪！戰機在光柱核心高速頻閃後，光束極速凝聚塌縮為單點躍遷消失！
* **360 度逆時針雷達轉場**：以戰機為軸心，逆時針由 12 點鐘方向掃描全場切換至新時代天空色調，進入新關卡宣告！

### 7. 雙人輪流遊玩模式與 7 位數高分榜簽名 (2-Player & 7-Digit High Scores)
* **2-Player 模式**：標題按 `2` 啟動，獨立記錄雙方分數、命數、時代、擊墜數，陣亡時自動換人接續戰鬥。
* **獨立高分簽名佇列**：若 1P 與 2P 雙雙名列前五名，兩人將依序獲得 30 秒專屬簽名資格，支援鍵盤與搖桿選字填入。
* **7 位數高分榜與精準對齊**：排行榜支援高達 7 位數分數顯示，個位數精準向右對齊於 `SCORE RANKING TABLE` 標題之 `'G'`（Column 16），絕不截斷十萬位數以上高分，100% 忠實呈現 1982 大型電玩與 CX16 原版視覺排版。

---

## 複合式雙音效引擎 (Dual-Engine Audio)

* **VERA 16 通道立體聲 PSG 晶片合成（雙聲道齊奏 +6dB 爆發力）**：
  * **玩家雷射機砲 (`AUDIO_PLAYER_SHOOT`)**：結合 Channel 0（50% 方波）與 Channel 4（25% 方波微調）雙聲道同度齊奏，輸出聲能直接翻倍（+6 dB），槍聲乾脆紮實！
  * **小兵爆炸破片 (`AUDIO_ENEMY_EXPLODE`)**：結合 Channel 2（白噪音急速降頻）與 Channel 5（鋸齒波低頻重低音震波），打擊感拳拳到肉！
  * **敵彈啾啾聲、跳傘員救援三音琶音、關卡凱旋和弦**：多聲道硬體獨立合成，毫無延遲，擊落再多敵機也絕不卡音！
* **雙 Bank VRAM 常駐 14 首街機 PCM 音訊（100% 零磁碟運行串流）**：
  * **Bank 0 (`$1000..$F5C4`, 58.8 KB)**：開場主題曲 (`AUDIO_GAME_START`, 7.10 秒完整尾韻無死音)、二戰轟炸機空投航彈呼嘯 (`AUDIO_BOMB`, 0.60 秒) 與重低音大爆炸 (`AUDIO_BIG_EXPLOSION`, 1.38 秒正宗震撼爆炸)，保有 2.6 KB 寬裕安全緩衝。
  * **Bank 1 (`$1200..$7D3B`, 27.5 KB)**：投幣音效 (`AUDIO_COINDROP`)、飛彈點火與巡航 (`AUDIO_ROCKET_LAUNCH` / `AUDIO_ROCKET_FLY`)、四機突襲警報 (`AUDIO_WAVE_START`)、四大關卡 Boss 巨型母艦專屬警報 (`AUDIO_BOSSL0 ~ 3`)、攔截爆炸 (`AUDIO_WAPON_EXPLODE`)、敵機機槍 (`AUDIO_ENEMY_SHOOT`)、以及**時空躍遷穿梭 PCM (`AUDIO_TIMEWARP`, 1.20 秒正宗原音)**，保有 709 位元組安全緩衝防禦精靈 RAM。

---

## 編譯與執行 (Build & Run)

### 開發環境需求
* **llvm-mos** (目標架構: `mos-apple2e`)
* **Node.js** (執行 HDV 與資產封裝工具)

### 一鍵建置
在 `TimePilot-IIvera/` 目錄下執行：
```bat
build.bat
```
建置流程：
1. `tools/mkpcm_blob.mjs` + `tools/mkart.mjs`：打包 PCM 音訊與正版 32x16 精靈資產。
2. `mos-apple2e-clang`：以 `-Oz` 極限優化編譯 `MAIN.BIN`（Slot 2）與 `MAIN4.BIN`（Slot 4）。
3. `tools/build_hdv.mjs`：生成標準 800 KB ProDOS 2.4.3 開機磁碟映像檔 `TimePilot-IIvera.hdv`。

### 模擬器載入執行
* 支援 **Apple2TS** 網頁模擬器或任何支援 VERA 擴充卡之 Apple II 模擬器。
* 將 `TimePilot-IIvera.hdv` 掛載至硬碟槽即可自動引導開機啟動！

---

## 專案架構與開發技術日誌

欲了解本專案的底層硬體暫存器映射、ProDOS 安全記憶體跨度、VRAM Bank 0/1 配置、零乘法八分圓幾何解角器與完整 65 項工程里程碑，請參閱詳細工程指南：  
👉 **[AGENTS.md — Time Pilot IIvera Developer Guide](AGENTS.md)**
