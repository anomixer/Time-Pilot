// build_disk.mjs — Package Time Pilot IIvera into two 140KB ProDOS floppy disks (.po).
//
// Disk 1 (TimePilot-IIvera-D1.po):
//   - PRODOS + BASIC.SYSTEM (from 140kb.po)
//   - STARTUP (Applesoft BASIC launcher)
//   - MAIN.BIN (Slot 2 binary)
//   - ART (art.blob at fixed block 128)
//
// Disk 2 (TimePilot-IIvera-D2.po):
//   - Blank volume (TIMEPILOT2)
//   - PCM (pcm.blob audio at fixed block 7)
//   - MAIN4.BIN (Slot 4 binary)
//
// Boot sequence:
//   1. Apple II boots Disk 1 in Drive 1.
//   2. STARTUP detects VERA slot (Slot 2 or 4).
//   3. If Slot 2: BRUN MAIN.BIN (from Drive 1).
//      If Slot 4: BRUN MAIN4.BIN,D2 (from Drive 2).
//   4. In game, disk_init detects 140KB floppy mode:
//      - ART is streamed from Drive 1 (block 128).
//      - PCM is streamed from Drive 2 (block 7).
import fs from "fs"
import path from "path"
import { fileURLToPath } from "url"
import { compileApplesoftBasic } from "../../../veratest/src/applebasic.mjs"

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const projectRoot = path.resolve(__dirname, "..")
const srcDir = path.join(projectRoot, "src")
const buildDir = path.join(projectRoot, "build")
const basePoPath = path.join(projectRoot, "140kb.po")
const BLOCK = 512
const TOTAL_BLOCKS = 280

if (!fs.existsSync(basePoPath)) throw new Error(`Base 140KB PO image not found: ${basePoPath}`)
if (!fs.existsSync(path.join(buildDir, "main.bin"))) throw new Error("build/main.bin missing — run build.bat first")
if (!fs.existsSync(path.join(buildDir, "main4.bin"))) throw new Error("build/main4.bin missing — run build.bat first")
if (!fs.existsSync(path.join(buildDir, "art.blob"))) throw new Error("build/art.blob missing — run tools/mkart.mjs first")
if (!fs.existsSync(path.join(buildDir, "pcm.blob"))) throw new Error("build/pcm.blob missing — run tools/mkpcm_blob.mjs first")

const mainBinRaw = new Uint8Array(fs.readFileSync(path.join(buildDir, "main.bin")))
const mainLoadAddr = mainBinRaw[0] | (mainBinRaw[1] << 8)
const mainBin = mainBinRaw.subarray(4)

const main4BinRaw = new Uint8Array(fs.readFileSync(path.join(buildDir, "main4.bin")))
const main4LoadAddr = main4BinRaw[0] | (main4BinRaw[1] << 8)
const main4Bin = main4BinRaw.subarray(4)

const art = new Uint8Array(fs.readFileSync(path.join(buildDir, "art.blob")))
const pcm = new Uint8Array(fs.readFileSync(path.join(buildDir, "pcm.blob")))

const startupBas = path.join(srcDir, "startup_d1.bas")
const startupBytes = new Uint8Array(compileApplesoftBasic(srcDir, "startup_d1.bas"))

function setVolumeHeader(disk, name) {
  const vol = disk.subarray(2 * BLOCK, 3 * BLOCK)
  const nameLen = Math.min(15, name.length)
  vol[4] = 0xF0 | nameLen
  for (let i = 0; i < 15; i++) {
    vol[5 + i] = i < nameLen ? name.charCodeAt(i) : 0
  }
  vol[0x29] = TOTAL_BLOCKS & 0xFF
  vol[0x2A] = (TOTAL_BLOCKS >> 8) & 0xFF
}

function writeDirEntry(disk, entryIdx, e) {
  const vol = disk.subarray(2 * BLOCK, 3 * BLOCK)
  const off = 4 + entryIdx * 39
  vol.fill(0, off, off + 39)
  vol[off] = (e.stType << 4) | (e.name.length & 0x0F)
  for (let i = 0; i < 15; i++) {
    vol[off + 1 + i] = i < e.name.length ? e.name.charCodeAt(i) : 0
  }
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
  vol[off + 0x25] = 2 // parent is volume block 2
  vol[off + 0x26] = 0
}

function setFileCount(disk, count) {
  const vol = disk.subarray(2 * BLOCK, 3 * BLOCK)
  vol[0x25] = count & 0xFF
  vol[0x26] = (count >> 8) & 0xFF
}

function markBitmap(disk, usedBlocks) {
  const bm = disk.subarray(6 * BLOCK, 7 * BLOCK)
  // Fill all 280 blocks as free (bit = 1)
  bm.fill(0xFF, 0, 35)
  bm.fill(0, 35, BLOCK) // unused bitmap space = 0
  // Clear bits for used blocks (0 = used)
  for (const b of usedBlocks) {
    const byteIdx = Math.floor(b / 8)
    const bit = 7 - (b % 8)
    bm[byteIdx] &= ~(1 << bit)
  }
}

// =============================================================================
// Build Disk 1 (TimePilot-IIvera-D1.po)
// =============================================================================
console.log("Building Disk 1: TimePilot-IIvera-D1.po ...")
const disk1 = new Uint8Array(fs.readFileSync(basePoPath))

// Preserve PRODOS and BASIC.SYSTEM entries from 140kb.po
const d1Vol = disk1.subarray(2 * BLOCK, 3 * BLOCK)
const nameOf = (off, len) => String.fromCharCode(...d1Vol.subarray(off + 1, off + 1 + len))
const d1Preserved = []
for (let i = 1; i <= 12; i++) {
  const off = 4 + i * 39
  if (d1Vol[off] === 0) continue
  const nameLen = d1Vol[off] & 0x0F
  const name = nameOf(off, nameLen)
  if (name === "PRODOS" || name === "BASIC.SYSTEM") {
    d1Preserved.push(d1Vol.slice(off, off + 39))
  }
}

// Blocks 0..61 are used by system + PRODOS + BASIC.SYSTEM
const d1Used = new Set()
for (let b = 0; b <= 61; b++) d1Used.add(b)

// ART blob at fixed block 128
const ART_BASE_BLOCK = 128
const ART_NUM_BLOCKS = Math.ceil(art.length / BLOCK)
for (let b = ART_BASE_BLOCK; b < ART_BASE_BLOCK + ART_NUM_BLOCKS; b++) d1Used.add(b)

disk1.set(art, ART_BASE_BLOCK * BLOCK)
disk1.fill(0, ART_BASE_BLOCK * BLOCK + art.length, (ART_BASE_BLOCK + ART_NUM_BLOCKS) * BLOCK)

let d1NextFree = 62
const d1Allocate = () => {
  while (d1Used.has(d1NextFree)) d1NextFree++
  if (d1NextFree >= TOTAL_BLOCKS) throw new Error("Disk 1 full!")
  const b = d1NextFree++
  d1Used.add(b)
  return b
}

function d1WriteFile(name, fileType, aux, data) {
  const size = data.length
  let stType, keyBlock, totalBlocks
  if (size <= BLOCK) {
    stType = 1
    keyBlock = d1Allocate()
    disk1.set(data, keyBlock * BLOCK)
    totalBlocks = 1
  } else {
    stType = 2
    keyBlock = d1Allocate()
    const idx = new Uint8Array(BLOCK)
    const n = Math.ceil(size / BLOCK)
    for (let i = 0; i < n; i++) {
      const db = d1Allocate()
      disk1.set(data.subarray(i * BLOCK, Math.min(size, (i + 1) * BLOCK)), db * BLOCK)
      idx[i] = db & 0xFF
      idx[i + 256] = (db >> 8) & 0xFF
    }
    disk1.set(idx, keyBlock * BLOCK)
    totalBlocks = 1 + n
  }
  return { name, stType, fileType, keyBlock, totalBlocks, eof: size, aux }
}

const fStartup = d1WriteFile("STARTUP", 0xFC, 0x0801, startupBytes)
const fMain = d1WriteFile("MAIN.BIN", 0x06, mainLoadAddr, mainBin)

// Create sapling index block for ART
const artIdxBlock = d1Allocate()
const artIdx = new Uint8Array(BLOCK)
for (let i = 0; i < ART_NUM_BLOCKS; i++) {
  const db = ART_BASE_BLOCK + i
  artIdx[i] = db & 0xFF
  artIdx[i + 256] = (db >> 8) & 0xFF
}
disk1.set(artIdx, artIdxBlock * BLOCK)
const fArt = {
  name: "ART",
  stType: 2,
  fileType: 0x06,
  keyBlock: artIdxBlock,
  totalBlocks: 1 + ART_NUM_BLOCKS,
  eof: art.length,
  aux: 0x2000
}

// Clear all dir entries in block 2
for (let i = 1; i <= 12; i++) d1Vol.fill(0, 4 + i * 39, 4 + (i + 1) * 39)

// Write preserved system files
let d1EntryIdx = 1
for (const raw of d1Preserved) {
  d1Vol.set(raw, 4 + d1EntryIdx * 39)
  d1EntryIdx++
}

// Write app files
const d1AppFiles = [fStartup, fMain, fArt]
for (const f of d1AppFiles) {
  writeDirEntry(disk1, d1EntryIdx++, f)
}

setVolumeHeader(disk1, "TIMEPILOT1")
setFileCount(disk1, d1Preserved.length + d1AppFiles.length)
markBitmap(disk1, d1Used)

const outD1Path = path.join(projectRoot, "TimePilot-IIvera-D1.po")
fs.writeFileSync(outD1Path, disk1)
console.log(`  OK: TimePilot-IIvera-D1.po (${d1Used.size}/${TOTAL_BLOCKS} blocks used, ${TOTAL_BLOCKS - d1Used.size} free)`)
console.log(`      STARTUP (${fStartup.totalBlocks} blk), MAIN.BIN (${fMain.totalBlocks} blk), ART (${fArt.totalBlocks} blk at block ${ART_BASE_BLOCK})`)

// =============================================================================
// Build Disk 2 (TimePilot-IIvera-D2.po)
// =============================================================================
console.log("\nBuilding Disk 2: TimePilot-IIvera-D2.po ...")
const disk2 = new Uint8Array(fs.readFileSync(basePoPath))

// Clear directory entries in block 2
const d2Vol = disk2.subarray(2 * BLOCK, 3 * BLOCK)
for (let i = 1; i <= 12; i++) d2Vol.fill(0, 4 + i * 39, 4 + (i + 1) * 39)

// Blocks 0..6 used by system reserve (boot, dir, bitmap)
const d2Used = new Set()
for (let b = 0; b <= 6; b++) d2Used.add(b)

// PCM blob at fixed block 7
const PCM_BASE_BLOCK = 7
const PCM_NUM_BLOCKS = Math.ceil(pcm.length / BLOCK)
for (let b = PCM_BASE_BLOCK; b < PCM_BASE_BLOCK + PCM_NUM_BLOCKS; b++) d2Used.add(b)

disk2.set(pcm, PCM_BASE_BLOCK * BLOCK)
disk2.fill(0, PCM_BASE_BLOCK * BLOCK + pcm.length, (PCM_BASE_BLOCK + PCM_NUM_BLOCKS) * BLOCK)

let d2NextFree = PCM_BASE_BLOCK + PCM_NUM_BLOCKS
const d2Allocate = () => {
  while (d2Used.has(d2NextFree)) d2NextFree++
  if (d2NextFree >= TOTAL_BLOCKS) throw new Error("Disk 2 full!")
  const b = d2NextFree++
  d2Used.add(b)
  return b
}

// Create sapling index block for PCM
const pcmIdxBlock = d2Allocate()
const pcmIdx = new Uint8Array(BLOCK)
for (let i = 0; i < PCM_NUM_BLOCKS; i++) {
  const db = PCM_BASE_BLOCK + i
  pcmIdx[i] = db & 0xFF
  pcmIdx[i + 256] = (db >> 8) & 0xFF
}
disk2.set(pcmIdx, pcmIdxBlock * BLOCK)
const fPcm = {
  name: "PCM",
  stType: 2,
  fileType: 0x06,
  keyBlock: pcmIdxBlock,
  totalBlocks: 1 + PCM_NUM_BLOCKS,
  eof: pcm.length,
  aux: 0x2000
}

function d2WriteFile(name, fileType, aux, data) {
  const size = data.length
  let stType, keyBlock, totalBlocks
  if (size <= BLOCK) {
    stType = 1
    keyBlock = d2Allocate()
    disk2.set(data, keyBlock * BLOCK)
    totalBlocks = 1
  } else {
    stType = 2
    keyBlock = d2Allocate()
    const idx = new Uint8Array(BLOCK)
    const n = Math.ceil(size / BLOCK)
    for (let i = 0; i < n; i++) {
      const db = d2Allocate()
      disk2.set(data.subarray(i * BLOCK, Math.min(size, (i + 1) * BLOCK)), db * BLOCK)
      idx[i] = db & 0xFF
      idx[i + 256] = (db >> 8) & 0xFF
    }
    disk2.set(idx, keyBlock * BLOCK)
    totalBlocks = 1 + n
  }
  return { name, stType, fileType, keyBlock, totalBlocks, eof: size, aux }
}

const fMain4 = d2WriteFile("MAIN4.BIN", 0x06, main4LoadAddr, main4Bin)

let d2EntryIdx = 1
const d2AppFiles = [fPcm, fMain4]
for (const f of d2AppFiles) {
  writeDirEntry(disk2, d2EntryIdx++, f)
}

setVolumeHeader(disk2, "TIMEPILOT2")
setFileCount(disk2, d2AppFiles.length)
markBitmap(disk2, d2Used)

const outD2Path = path.join(projectRoot, "TimePilot-IIvera-D2.po")
fs.writeFileSync(outD2Path, disk2)
console.log(`  OK: TimePilot-IIvera-D2.po (${d2Used.size}/${TOTAL_BLOCKS} blocks used, ${TOTAL_BLOCKS - d2Used.size} free)`)
console.log(`      PCM (${fPcm.totalBlocks} blk at block ${PCM_BASE_BLOCK}), MAIN4.BIN (${fMain4.totalBlocks} blk)`)
console.log("\nDual 140KB Floppy Disks built successfully!")
