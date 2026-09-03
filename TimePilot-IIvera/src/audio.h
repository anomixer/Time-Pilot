//-----------------------------------------------------------------------------
// audio.h
// Time Pilot IIvera — VERA PCM streaming (IIe port of the CX16 audio API).
//-----------------------------------------------------------------------------
#pragma once

void audioInit(void);
void audioCleanup(void);
int8_t audioIsSourcePlaying(int8_t source);
void audioPlaySource(int8_t source);
void audioServiceAudio(void);
void audioStopSource(int8_t source);

#define NUM_AUDIO_SOURCES     20

// Audio source indices (identical order to the CX16 AUDIO_* enum, 0..19).
#define AUDIO_COINDROP        0
#define AUDIO_GAME_START      1
#define AUDIO_HIGHSCORE       2
#define AUDIO_NEXT_LEVEL      3
#define AUDIO_PLAYER_SHOOT    4
#define AUDIO_ROCKET_FLY      5
#define AUDIO_BOSSL0          6
#define AUDIO_BOSSL1          7
#define AUDIO_BOSSL2          8
#define AUDIO_BOSSL3          9
#define AUDIO_WAPON_EXPLODE   10
#define AUDIO_ENEMY_EXPLODE   11
#define AUDIO_ENEMY_SHOOT     12
#define AUDIO_BOMB            13
#define AUDIO_ROCKET_LAUNCH   14
#define AUDIO_PICKUP          15
#define AUDIO_EXTRA_LIFE      16
#define AUDIO_WAVE_START      17
#define AUDIO_BIG_EXPLOSION   18
#define AUDIO_TIMEWARP        19
