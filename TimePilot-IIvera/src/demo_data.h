//-----------------------------------------------------------------------------
// demo_data.h — Attract demo constants and MLI streaming block offset
// (The 1472-byte replay data is streamed from HDV at DEMO_START_BLOCK via MLI
// to prevent "NO BUFFERS AVAILABLE" on Apple IIe ProDOS).
//-----------------------------------------------------------------------------
#ifndef _DEMO_DATA_H
#define _DEMO_DATA_H

#include <stdint.h>

#define DEMO_START_BLOCK    1020
#define DEMO_ATTRACT_LENGTH 1472

#endif /* _DEMO_DATA_H */
