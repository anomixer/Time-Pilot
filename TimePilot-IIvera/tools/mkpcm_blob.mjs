// mkpcm_blob.mjs — Downsample the 20 CX16 Time Pilot .pcm samples to ~6.5 kHz
// (VERA audio.rate = 17) to fit within VERA VRAM Bank 0 ($02000..$0FFFF, 56KB).
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
const VRAM_AUDIO_BASE = 0x2000  // Bank 0 offset where audio blob is uploaded
const VRAM_AUDIO_LIMIT = 57344  // $02000..$0FFFF is 56KB

// Target rate: ~6866 Hz (VERA audio.rate = 18: 48828.125 * 18 / 128 = 6866.5 Hz)
const VERA_PCM_RATE = 18
const SRC_RATE = 12207
const DST_RATE = Math.round(48828.125 * VERA_PCM_RATE / 128)

const SOURCES = [
  { name: "AUDIO_COINDROP",      file: "coindrop.pcm",      maxSec: 0 },
  { name: "AUDIO_GAME_START",    file: "game_start.pcm",    maxSec: 8.05, tailSec: 0.0 },
  { name: "AUDIO_HIGHSCORE",     file: "highscore.pcm",     maxSec: 0 },
  { name: "AUDIO_NEXT_LEVEL",    file: "next_level.pcm",    maxSec: 0 },
  { name: "AUDIO_PLAYER_SHOOT",  file: "player_shoot.pcm",  maxSec: 0 },
  { name: "AUDIO_ROCKET_FLY",    file: "rocket_fly.pcm",    maxSec: 0 },
  { name: "AUDIO_BOSSL0",        file: "bossl0.pcm",        maxSec: 0 },
  { name: "AUDIO_BOSSL1",        file: "bossl1.pcm",        maxSec: 0 },
  { name: "AUDIO_BOSSL2",        file: "bossl2.pcm",        maxSec: 0 },
  { name: "AUDIO_BOSSL3",        file: "bossl3.pcm",        maxSec: 0 },
  { name: "AUDIO_WAPON_EXPLODE", file: "wapon_explode.pcm", maxSec: 0 },
  { name: "AUDIO_ENEMY_EXPLODE", file: "enemy_explode.pcm", maxSec: 0 },
  { name: "AUDIO_ENEMY_SHOOT",   file: "enemy_shoot.pcm",   maxSec: 0 },
  { name: "AUDIO_BOMB",          file: "bomb.pcm",          maxSec: 0 },
  { name: "AUDIO_ROCKET_LAUNCH", file: "rocket_launch.pcm", maxSec: 0 },
  { name: "AUDIO_PICKUP",        file: "pickup.pcm",        maxSec: 0 },
  { name: "AUDIO_EXTRA_LIFE",    file: "extra_life.pcm",    maxSec: 0 },
  { name: "AUDIO_WAVE_START",    file: "wave_start.pcm",    maxSec: 0 },
  { name: "AUDIO_BIG_EXPLOSION", file: "big_explosion.pcm", maxSec: 0 },
  { name: "AUDIO_TIMEWARP",      file: "timewarp.pcm",      maxSec: 0 },
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

const blob = []
let offset = 0
const rows = []

for (const s of SOURCES) {
  if (s.maxSec === 0) {
    rows.push({ ...s, start: VRAM_AUDIO_BASE + offset, length: 0, loops: 0 })
    continue
  }
  const p = path.join(cx16Root, "audio", s.file)
  if (!fs.existsSync(p)) throw new Error(`Missing PCM sample: ${p}`)
  const raw = new Uint8Array(fs.readFileSync(p))

  const processed = processPCM(raw, s.maxSec, s.tailSec || 0)
  rows.push({ ...s, start: VRAM_AUDIO_BASE + offset, length: processed.length, loops: s.loops ? 1 : 0 })
  blob.push(processed)
  offset += processed.length
}

const total = offset
if (total > VRAM_AUDIO_LIMIT) {
  throw new Error(`PCM blob size ${total} exceeds VRAM limit ${VRAM_AUDIO_LIMIT}!`)
}

fs.writeFileSync(path.join(buildDir, "pcm.blob"), Buffer.concat(blob))
const numBlocks = Math.ceil(total / BLOCK_SIZE)

const hex16 = (n) => "0x" + n.toString(16).toUpperCase().padStart(4, "0")
let header = [
  "// AUTO-GENERATED by tools/mkpcm_blob.mjs — do not edit.",
  `// Time Pilot IIvera 6.5kHz PCM table resident in VRAM Bank 0 ($${VRAM_AUDIO_BASE.toString(16).toUpperCase()}..$0FFFF).`,
  "#pragma once",
  "#include <stdint.h>",
  "typedef struct { uint16_t start; uint16_t length; uint8_t loops; } TpAudioData;",
  "#define NUM_AUDIO_SOURCES 20",
  `#define VERA_PCM_RATE ${VERA_PCM_RATE}  // ~${DST_RATE} Hz (25MHz/512 * ${VERA_PCM_RATE}/128)`,
  `#define VRAM_AUDIO_BASE 0x${VRAM_AUDIO_BASE.toString(16).toUpperCase()}`,
  `#define PCM_START_BLOCK ${PCM_START_BLOCK}`,
  `#define PCM_TOTAL_BYTES ${total}`,
  `#define PCM_NUM_BLOCKS  ${numBlocks}`,
  "",
  "static const TpAudioData audioData[NUM_AUDIO_SOURCES] = {",
]
for (const r of rows) {
  header.push(`    { ${hex16(r.start)}, ${hex16(r.length)}, ${r.loops} }, // ${r.name}`)
}
header.push("};")
header.push("")
fs.writeFileSync(path.join(projectRoot, "src", "audio_table.h"), header.join("\n"))

console.log(`pcm.blob: ${total} bytes (${numBlocks} blocks) -> build/pcm.blob`)
console.log(`audio_table.h written (${rows.length} sources).`)
console.log(`  VRAM Bank 0 resident: $${VRAM_AUDIO_BASE.toString(16).toUpperCase()}..$${(VRAM_AUDIO_BASE + total).toString(16).toUpperCase()} (free in Bank 0: ${VRAM_AUDIO_LIMIT - total} bytes)`)
