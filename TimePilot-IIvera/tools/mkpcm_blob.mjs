// mkpcm_blob.mjs — Downsample CX16 Time Pilot .pcm samples to ~6.5 kHz
// (VERA audio.rate = 17) to fit within VERA VRAM Bank 0 and Bank 1.
// Bank 0 ($1000..$FE58): Opening theme (8.03s) + Big Explosion (1.37s)
// Bank 1 ($2800..$7241): Coin drop, bomb whistle, rocket launch, wave alert, boss audio
// Emits build/pcm.blob and src/audio_table.h.
import fs from "fs"
import path from "path"
import { fileURLToPath } from "url"

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const projectRoot = path.resolve(__dirname, "..")
const cx16Root = path.resolve(projectRoot, "..", "TimePilot-CX16")
const buildDir = path.join(projectRoot, "build")
fs.mkdirSync(buildDir, { recursive: true })

const PCM_START_BLOCK = 200
const BLOCK_SIZE = 512
const VRAM_AUDIO_BASE = 0x1000       // Bank 0 offset ($1000..$FFFF is 60KB)
const VRAM_AUDIO_LIMIT = 65536 - VRAM_AUDIO_BASE  // 61,440 bytes
const VRAM_AUDIO_BANK1_BASE = 0x1200 // Bank 1 offset ($1200..$7FFF is 28,160 bytes)
const VRAM_AUDIO_BANK1_LIMIT = 0x8000 - VRAM_AUDIO_BANK1_BASE // 28,160 bytes

// Target rate: ~6485 Hz (VERA audio.rate = 17: 48828.125 * 17 / 128 = 6484.7 Hz)
const VERA_PCM_RATE = 17
const SRC_RATE = 12207
const DST_RATE = Math.round(48828.125 * VERA_PCM_RATE / 128)

const SOURCES = [
  { name: "AUDIO_COINDROP",      file: "coindrop.pcm",      bank: 1, maxSec: 0.45 },
  { name: "AUDIO_GAME_START",    file: "game_start.pcm",    bank: 0, maxSec: 7.10, tailSec: 0.0 },
  { name: "AUDIO_HIGHSCORE",     file: "highscore.pcm",     bank: 0, maxSec: 0 },
  { name: "AUDIO_NEXT_LEVEL",    file: "next_level.pcm",    bank: 0, maxSec: 0 },
  { name: "AUDIO_PLAYER_SHOOT",  file: "player_shoot.pcm",  bank: 0, maxSec: 0 },
  { name: "AUDIO_ROCKET_FLY",    file: "rocket_fly.pcm",    bank: 1, maxSec: 0.30, loops: 1 },
  { name: "AUDIO_BOSSL0",        file: "bossl0.pcm",        bank: 1, maxSec: 0.20, loops: 1 },
  { name: "AUDIO_BOSSL1",        file: "bossl1.pcm",        bank: 1, maxSec: 0.55, loops: 1 },
  { name: "AUDIO_BOSSL2",        file: "bossl2.pcm",        bank: 1, maxSec: 0.17, loops: 1 },
  { name: "AUDIO_BOSSL3",        file: "bossl3.pcm",        bank: 1, maxSec: 0.20, loops: 1 },
  { name: "AUDIO_WAPON_EXPLODE", file: "wapon_explode.pcm", bank: 0, maxSec: 0 },
  { name: "AUDIO_ENEMY_EXPLODE", file: "enemy_explode.pcm", bank: 0, maxSec: 0 },
  { name: "AUDIO_ENEMY_SHOOT",   file: "enemy_shoot.pcm",   bank: 0, maxSec: 0 },
  { name: "AUDIO_BOMB",          file: "bomb.pcm",          bank: 0, maxSec: 0.60 },
  { name: "AUDIO_ROCKET_LAUNCH", file: "rocket_launch.pcm", bank: 1, maxSec: 0.35 },
  { name: "AUDIO_PICKUP",        file: "pickup.pcm",        bank: 0, maxSec: 0 },
  { name: "AUDIO_EXTRA_LIFE",    file: "extra_life.pcm",    bank: 0, maxSec: 0 },
  { name: "AUDIO_WAVE_START",    file: "wave_start.pcm",    bank: 1, maxSec: 0.35 },
  { name: "AUDIO_BIG_EXPLOSION", file: "big_explosion.pcm", bank: 0, maxSec: 1.38, tailSec: 0.0 },
  { name: "AUDIO_TIMEWARP",      file: "timewarp.pcm",      bank: 1, maxSec: 1.20 },
]

function processPCM(raw, maxSec, tailSec) {
  // Take up to maxSec of original samples (1:1 true tempo)
  const rawLen = (maxSec > 0) ? Math.min(raw.length, Math.round(maxSec * SRC_RATE)) : raw.length
  if (rawLen <= 0) return new Uint8Array(0)

  // Convert signed 8-bit to float
  const src = new Float32Array(rawLen)
  for (let i = 0; i < rawLen; i++) {
    src[i] = (raw[i] >= 128) ? raw[i] - 256 : raw[i]
  }

  // Target length reflects EXACT 1:1 playback duration at DST_RATE
  const targetLen = Math.round(rawLen * DST_RATE / SRC_RATE)
  const out = new Float32Array(targetLen)

  // Pure sample-rate conversion (NO pitch shifting, NO tempo speed-up!)
  const ratio = SRC_RATE / DST_RATE
  for (let i = 0; i < targetLen; i++) {
    const srcPos = i * ratio
    const idx = Math.floor(srcPos)
    const frac = srcPos - idx
    const s0 = src[idx] || 0
    const s1 = (idx + 1 < rawLen) ? src[idx + 1] : s0
    out[i] = s0 * (1 - frac) + s1 * frac
  }

  // Normalize: peak to 115
  let peak = 0
  for (let i = 0; i < targetLen; i++) {
    const v = Math.abs(out[i])
    if (v > peak) peak = v
  }
  const gain = (peak > 0) ? (115.0 / peak) : 1.0
  for (let i = 0; i < targetLen; i++) {
    out[i] *= gain
  }

  // Gentle fade-out only in the final 0.05s (400 samples) to prevent click
  const fadeLen = Math.min(400, targetLen)
  for (let i = 0; i < fadeLen; i++) {
    const fade = (fadeLen - i) / fadeLen
    out[targetLen - fadeLen + i] *= fade
  }

  // Append silence tail (0x80 = signed zero) for natural decay
  const silenceSamples = Math.round((tailSec || 0) * DST_RATE)
  const totalLen = targetLen + silenceSamples

  // Pack to Uint8Array (8-bit signed, 0x80 = silence)
  const res = new Uint8Array(totalLen)
  for (let i = 0; i < targetLen; i++) {
    let v = Math.round(out[i])
    if (v < -128) v = -128
    if (v > 127) v = 127
    res[i] = (v < 0) ? v + 256 : v
  }
  res.fill(0x80, targetLen) // silence tail
  return res
}

// Process Bank 0 samples
const bank0Blob = []
let bank0Offset = 0
const bank0Rows = new Map()

for (const s of SOURCES.filter(x => x.bank === 0)) {
  if (s.maxSec === 0) {
    bank0Rows.set(s.name, { ...s, bank: 0, start: 0, length: 0, loops: 0 })
    continue
  }
  const p = path.join(cx16Root, "audio", s.file)
  if (!fs.existsSync(p)) throw new Error(`Missing PCM sample: ${p}`)
  const raw = new Uint8Array(fs.readFileSync(p))
  const processed = processPCM(raw, s.maxSec, s.tailSec || 0)
  bank0Rows.set(s.name, { ...s, bank: 0, start: VRAM_AUDIO_BASE + bank0Offset, length: processed.length, loops: s.loops ? 1 : 0 })
  bank0Blob.push(processed)
  bank0Offset += processed.length
}

if (bank0Offset > VRAM_AUDIO_LIMIT) {
  throw new Error(`Bank 0 PCM blob size ${bank0Offset} exceeds limit ${VRAM_AUDIO_LIMIT}!`)
}

// Process Bank 1 samples
const bank1Blob = []
let bank1Offset = 0
const bank1Rows = new Map()

for (const s of SOURCES.filter(x => x.bank === 1)) {
  if (s.maxSec === 0) {
    bank1Rows.set(s.name, { ...s, bank: 1, start: 0, length: 0, loops: 0 })
    continue
  }
  const p = path.join(cx16Root, "audio", s.file)
  if (!fs.existsSync(p)) throw new Error(`Missing PCM sample: ${p}`)
  const raw = new Uint8Array(fs.readFileSync(p))
  const processed = processPCM(raw, s.maxSec, s.tailSec || 0)
  bank1Rows.set(s.name, { ...s, bank: 1, start: VRAM_AUDIO_BANK1_BASE + bank1Offset, length: processed.length, loops: s.loops ? 1 : 0 })
  bank1Blob.push(processed)
  bank1Offset += processed.length
}

if (bank1Offset > VRAM_AUDIO_BANK1_LIMIT) {
  throw new Error(`Bank 1 PCM blob size ${bank1Offset} exceeds limit ${VRAM_AUDIO_BANK1_LIMIT}!`)
}

// Combine both blobs into single pcm.blob on disk
const totalBlob = Buffer.concat([...bank0Blob, ...bank1Blob])
fs.writeFileSync(path.join(buildDir, "pcm.blob"), totalBlob)
const numBlocks = Math.ceil(totalBlob.length / BLOCK_SIZE)

const hex16 = (n) => "0x" + n.toString(16).toUpperCase().padStart(4, "0")
let header = [
  "// AUTO-GENERATED by tools/mkpcm_blob.mjs — do not edit.",
  `// Time Pilot IIvera 6.5kHz PCM table (Bank 0: $${VRAM_AUDIO_BASE.toString(16).toUpperCase()}, Bank 1: $${VRAM_AUDIO_BANK1_BASE.toString(16).toUpperCase()}).`,
  "#pragma once",
  "#include <stdint.h>",
  "typedef struct { uint8_t bank; uint16_t start; uint16_t length; uint8_t loops; } TpAudioData;",
  "#define NUM_AUDIO_SOURCES 20",
  `#define VERA_PCM_RATE ${VERA_PCM_RATE}  // ~${DST_RATE} Hz (25MHz/512 * ${VERA_PCM_RATE}/128)`,
  `#define VRAM_AUDIO_BASE 0x${VRAM_AUDIO_BASE.toString(16).toUpperCase()}`,
  `#define VRAM_AUDIO_BANK1_BASE 0x${VRAM_AUDIO_BANK1_BASE.toString(16).toUpperCase()}`,
  `#define PCM_START_BLOCK ${PCM_START_BLOCK}`,
  `#define PCM_BANK0_BYTES ${bank0Offset}`,
  `#define PCM_BANK1_BYTES ${bank1Offset}`,
  `#define PCM_TOTAL_BYTES ${totalBlob.length}`,
  `#define PCM_NUM_BLOCKS  ${numBlocks}`,
  "",
  "static const TpAudioData audioData[NUM_AUDIO_SOURCES] = {",
]

for (const s of SOURCES) {
  const r = (s.bank === 0) ? bank0Rows.get(s.name) : bank1Rows.get(s.name)
  header.push(`    { ${r.bank}, ${hex16(r.start)}, ${hex16(r.length)}, ${r.loops} }, // ${r.name}`)
}
header.push("};")
header.push("")
fs.writeFileSync(path.join(projectRoot, "src", "audio_table.h"), header.join("\n"))

console.log(`pcm.blob: ${totalBlob.length} bytes (${numBlocks} blocks) -> build/pcm.blob`)
console.log(`  Bank 0: ${bank0Offset} bytes ($${VRAM_AUDIO_BASE.toString(16).toUpperCase()}..$${(VRAM_AUDIO_BASE + bank0Offset).toString(16).toUpperCase()}, free: ${VRAM_AUDIO_LIMIT - bank0Offset} bytes)`)
console.log(`  Bank 1: ${bank1Offset} bytes ($${VRAM_AUDIO_BANK1_BASE.toString(16).toUpperCase()}..$${(VRAM_AUDIO_BANK1_BASE + bank1Offset).toString(16).toUpperCase()}, free: ${VRAM_AUDIO_BANK1_LIMIT - bank1Offset} bytes)`)
console.log(`audio_table.h written (${SOURCES.length} sources).`)
