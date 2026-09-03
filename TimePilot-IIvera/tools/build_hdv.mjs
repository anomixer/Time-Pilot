// build_hdv.mjs — Package Time Pilot IIvera into a bootable ProDOS 8 HDV (.hdv).
//
// Boot chain: ProDOS HDV -> BASIC.SYSTEM -> Applesoft STARTUP -> BRUN MAIN.BIN.
// Audio (pcm.blob) is placed at FIXED blocks (PCM_START_BLOCK) and read by the
// 6502 directly via MLI READ_BLOCK — it is NOT a named ProDOS file (the veratest
// "fixed music slot" pattern). Only MAIN.BIN and STARTUP get directory entries.
//
// Modeled on C:\dev\veratest\src\slideshow\slideshow_hdv.mjs (proven on apple2ts).
import fs from "fs"
import path from "path"
import { fileURLToPath } from "url"
import { compileApplesoftBasic } from "../../../veratest/src/applebasic.mjs"

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const projectRoot = path.resolve(__dirname, "..")
const srcDir = path.join(projectRoot, "src")
const buildDir = path.join(projectRoot, "build")
const baseHdvPath = "C:/dev/veratest/assets/ProDOS 2.4.3.hdv"
const BLOCK = 512

if (!fs.existsSync(baseHdvPath)) throw new Error(`Base HDV not found: ${baseHdvPath}`)
if (!fs.existsSync(path.join(buildDir, "main.bin"))) throw new Error("build/main.bin missing — run build.bat first")
if (!fs.existsSync(path.join(buildDir, "pcm.blob"))) throw new Error("build/pcm.blob missing — run tools/mkpcm_blob.mjs first")

const disk = new Uint8Array(fs.readFileSync(baseHdvPath))
const TOTAL = disk.length / BLOCK
const mainBinRaw = new Uint8Array(fs.readFileSync(path.join(buildDir, "main.bin")))
// Strip the 4-byte ProDOS BIN header (load addr LE + length LE). BRUN loads at
// the aux-type address and the file content must be raw code starting at byte 0.
// (Header: 00 20 = $2000 LE, then length LE. Code follows.)
const mainBin = mainBinRaw.subarray(4)
const pcm = new Uint8Array(fs.readFileSync(path.join(buildDir, "pcm.blob")))

// ---- Audio: fixed block range (keep in sync with mkpcm_blob.mjs PCM_START_BLOCK) ----
const PCM_START_BLOCK = 200
const PCM_BLOCKS = Math.ceil(pcm.length / BLOCK)
{
  const start = PCM_START_BLOCK * BLOCK
  disk.set(pcm, start)
  // pad the final block with zeros (MLI reads whole blocks)
  disk.fill(0, start + pcm.length, (PCM_START_BLOCK + PCM_BLOCKS) * BLOCK)
}
console.log(`  audio: pcm.blob ${pcm.length}B at blocks ${PCM_START_BLOCK}..${PCM_START_BLOCK + PCM_BLOCKS - 1}`)

// ---- Art: fixed block range (keep in sync with mkart.mjs ART_START_BLOCK) ----
const artPath = path.join(buildDir, "art.blob")
let ART_START_BLOCK = 0, ART_BLOCKS = 0
if (fs.existsSync(artPath)) {
  const art = new Uint8Array(fs.readFileSync(artPath))
  ART_START_BLOCK = 900
  ART_BLOCKS = Math.ceil(art.length / BLOCK)
  const start = ART_START_BLOCK * BLOCK
  disk.set(art, start)
  disk.fill(0, start + art.length, (ART_START_BLOCK + ART_BLOCKS) * BLOCK)
  console.log(`  art:   art.blob ${art.length}B at blocks ${ART_START_BLOCK}..${ART_START_BLOCK + ART_BLOCKS - 1}`)
}

// ---- Demo: fixed block range (keep in sync with demo_data.h DEMO_START_BLOCK) ----
const demoPath = path.join(buildDir, "demo.blob")
let DEMO_START_BLOCK = 0, DEMO_BLOCKS = 0
if (fs.existsSync(demoPath)) {
  const demo = new Uint8Array(fs.readFileSync(demoPath))
  DEMO_START_BLOCK = 980
  DEMO_BLOCKS = Math.ceil(demo.length / BLOCK)
  const start = DEMO_START_BLOCK * BLOCK
  disk.set(demo, start)
  disk.fill(0, start + demo.length, (DEMO_START_BLOCK + DEMO_BLOCKS) * BLOCK)
  console.log(`  demo:  demo.blob ${demo.length}B at blocks ${DEMO_START_BLOCK}..${DEMO_START_BLOCK + DEMO_BLOCKS - 1}`)
}

// ---- Block allocator (skip 0..99 system reserve + audio + art + demo ranges) ----
const used = new Set()
for (let b = 0; b < 100; b++) used.add(b)
for (let b = PCM_START_BLOCK; b < PCM_START_BLOCK + PCM_BLOCKS; b++) used.add(b)
for (let b = ART_START_BLOCK; b < ART_START_BLOCK + ART_BLOCKS; b++) used.add(b)
if (DEMO_BLOCKS > 0) {
  for (let b = DEMO_START_BLOCK; b < DEMO_START_BLOCK + DEMO_BLOCKS; b++) used.add(b)
}
const newlyAllocated = []   // for the bitmap update
let nextFree = 100
const allocate = () => {
  while (used.has(nextFree)) nextFree++
  const b = nextFree++
  used.add(b)
  newlyAllocated.push(b)
  return b
}

// Write a ProDOS file (type 1 = single block, type 2 = index + data blocks).
function writeFile(name, fileType, aux, data) {
  const size = data.length
  let stType, keyBlock, totalBlocks
  if (size <= BLOCK) {
    stType = 1
    keyBlock = allocate()
    disk.set(data, keyBlock * BLOCK)
    totalBlocks = 1
  } else {
    stType = 2
    keyBlock = allocate()
    const idx = new Uint8Array(BLOCK)
    const n = Math.ceil(size / BLOCK)
    for (let i = 0; i < n; i++) {
      const db = allocate()
      disk.set(data.subarray(i * BLOCK, Math.min(size, (i + 1) * BLOCK)), db * BLOCK)
      idx[i] = db & 0xFF
      idx[i + 256] = (db >> 8) & 0xFF
    }
    disk.set(idx, keyBlock * BLOCK)
    totalBlocks = 1 + n
  }
  return { name, stType, fileType, keyBlock, totalBlocks, eof: size, aux }
}

// Register a range of ALREADY-PLACED data blocks (at fixed addresses the game
// reads directly via MLI) as one or more ProDOS "sapling" files so CATALOG can
// list them — same pattern as veratest's /DATA images. A single ProDOS file has
// at most 256 index slots (=128KB), so larger blobs are split into parts. The
// data blocks themselves are NOT moved; we only add an index block + dir entry.
function addDataFile(name, fileType, baseBlock, numBlocks) {
  const entries = []
  let offset = 0, part = 1
  while (offset < numBlocks) {
    const chunk = Math.min(256, numBlocks - offset)
    const idxBlock = allocate()
    const idx = new Uint8Array(BLOCK)
    for (let i = 0; i < chunk; i++) {
      const db = baseBlock + offset + i
      idx[i] = db & 0xFF
      idx[i + 256] = (db >> 8) & 0xFF
    }
    disk.set(idx, idxBlock * BLOCK)
    entries.push({
      name: numBlocks > 256 ? `${name}${String(part).padStart(1, "0")}` : name,
      stType: 2, fileType, keyBlock: idxBlock,
      totalBlocks: 1 + chunk, eof: chunk * BLOCK, aux: 0x2000
    })
    offset += chunk
    part++
  }
  return entries
}

const startupBas = path.join(srcDir, "startup.bas")
if (!fs.existsSync(startupBas)) throw new Error(`startup.bas not found at ${startupBas}`)
const startupBytes = new Uint8Array(compileApplesoftBasic(srcDir, "startup.bas"))
const fStartup = writeFile("STARTUP", 0xFC, 0x0801, startupBytes)
const mainLoadAddr = mainBinRaw[0] | (mainBinRaw[1] << 8)
const fMain = writeFile("MAIN.BIN", 0x06, mainLoadAddr, mainBin)
console.log(`  MAIN.BIN  ${mainBin.length}B (load=$${mainLoadAddr.toString(16).toUpperCase()}, key=${fMain.keyBlock}, ${fMain.totalBlocks} blocks)`)
const appFiles = [fStartup, fMain]
const main4Path = path.join(buildDir, "main4.bin")
if (fs.existsSync(main4Path)) {
  const raw4 = new Uint8Array(fs.readFileSync(main4Path))
  const main4LoadAddr = raw4[0] | (raw4[1] << 8)
  const main4Bin = raw4.subarray(4)
  const fMain4 = writeFile("MAIN4.BIN", 0x06, main4LoadAddr, main4Bin)
  appFiles.push(fMain4)
  console.log(`  MAIN4.BIN ${main4Bin.length}B (load=$${main4LoadAddr.toString(16).toUpperCase()}, key=${fMain4.keyBlock}, ${fMain4.totalBlocks} blocks)`)
}
console.log(`  STARTUP   ${startupBytes.length}B (key=${fStartup.keyBlock}, ${fStartup.totalBlocks} blocks)`)

// ---- Register the audio + art blobs as ProDOS files so CATALOG lists them.
// Data stays at its fixed block range; we only add index blocks + dir entries.
const dataFiles = []
dataFiles.push(...addDataFile("PCM", 0x06, PCM_START_BLOCK, PCM_BLOCKS))
if (ART_BLOCKS > 0) dataFiles.push(...addDataFile("ART", 0x06, ART_START_BLOCK, ART_BLOCKS))
if (DEMO_BLOCKS > 0) dataFiles.push(...addDataFile("DEMO", 0x06, DEMO_START_BLOCK, DEMO_BLOCKS))
for (const e of dataFiles) console.log(`  ${e.name.padEnd(8)} ${e.eof}B (${e.totalBlocks} blocks, key=${e.keyBlock})`)

// ---- Rewrite the root directory (block 2) ----
const vol = disk.subarray(2 * BLOCK, 3 * BLOCK)
const nameOf = (off, len) => String.fromCharCode(...vol.subarray(off + 1, off + 1 + len))
// Preserve PRODOS / CLOCK.SYSTEM / BASIC.SYSTEM; drop the rest (BITSY.BOOT, QUIT.SYSTEM).
const keep = []
for (let i = 1; i <= 12; i++) {
  const off = 4 + i * 39
  if (vol[off] === 0) continue
  const nameLen = vol[off] & 0x0F
  const name = nameOf(off, nameLen)
  if (name === "PRODOS" || name === "CLOCK.SYSTEM" || name === "BASIC.SYSTEM") keep.push(vol.slice(off, off + 39))
}
// Clear all file entries.
for (let i = 1; i <= 12; i++) vol.fill(0, 4 + i * 39, 4 + (i + 1) * 39)

// Write back the preserved system files verbatim, then the new app files.
let idx = 1
for (const raw of keep) {
  vol.set(raw, 4 + idx * 39)
  idx++
}
for (const e of [...appFiles, ...dataFiles]) {
  const off = 4 + idx * 39
  vol[off] = (e.stType << 4) | (e.name.length & 0x0F)
  vol.set([...e.name].map((c) => c.charCodeAt(0)), off + 1)
  vol[off + 0x10] = e.fileType
  vol[off + 0x11] = e.keyBlock & 0xFF
  vol[off + 0x12] = (e.keyBlock >> 8) & 0xFF
  vol[off + 0x13] = e.totalBlocks & 0xFF
  vol[off + 0x14] = (e.totalBlocks >> 8) & 0xFF
  vol[off + 0x15] = e.eof & 0xFF
  vol[off + 0x16] = (e.eof >> 8) & 0xFF
  vol[off + 0x17] = (e.eof >> 16) & 0xFF
  vol[off + 0x1E] = 0xC3
  vol[off + 0x1F] = e.aux & 0xFF
  vol[off + 0x20] = (e.aux >> 8) & 0xFF
  vol[off + 0x25] = 2          // parent = volume block 2
  vol[off + 0x26] = 0
  idx++
}
const entryCount = keep.length + appFiles.length + dataFiles.length
// Volume file count (entries after the header).
vol[0x25] = entryCount & 0xFF
vol[0x26] = (entryCount >> 8) & 0xFF

// ---- Mark newly-allocated + audio blocks as used in the ProDOS bitmap ----
// (Blocks 6..21 hold the 16-bit bitmap; bit set = FREE, clear = USED.)
const setUsed = (b) => {
  const bmpBlock = 6 + Math.floor(b / 64)
  const byteIdx = Math.floor((b % 64) / 8)
  const bit = 7 - ((b % 64) % 8)
  disk[bmpBlock * BLOCK + byteIdx] &= ~(1 << bit)
}
for (const b of newlyAllocated) setUsed(b)
for (let b = PCM_START_BLOCK; b < PCM_START_BLOCK + PCM_BLOCKS; b++) setUsed(b)
if (ART_BLOCKS > 0) for (let b = ART_START_BLOCK; b < ART_START_BLOCK + ART_BLOCKS; b++) setUsed(b)

const outPath = path.join(projectRoot, "TimePilot-IIvera.hdv")
fs.writeFileSync(outPath, disk)
console.log(`\n  Built ${outPath} (${disk.length} bytes = ${(disk.length / 1024 / 1024).toFixed(1)} MB)`)
console.log(`  Root files: ${keep.length} system + ${appFiles.map(f => f.name).join(" + ")}`)
