// build_disk.mjs - Package the Time Pilot IIvera vertical slice into a
// bootable ProDOS floppy disk image (.po) for apple2ts.
//
// Mirrors veratest's proven ProDOS disk-writing logic (addFile, allocateBlock)
// and its Applesoft BASIC STARTUP that BRUNs the binary.
import fs from "fs"
import path from "path"
import { fileURLToPath } from "url"
import { compileApplesoftBasic } from "../../../veratest/src/applebasic.mjs"

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const projectRoot = path.resolve(__dirname, "..")
const buildDir = path.join(projectRoot, "build")

// Base ProDOS 2.4.3 disk image from veratest.
const basePoPath = "C:/dev/veratest/assets/ProDOS 2.4.3.po"
if (!fs.existsSync(basePoPath)) {
  throw new Error(`Base ProDOS disk not found: ${basePoPath}`)
}
const disk = new Uint8Array(fs.readFileSync(basePoPath))

const bitmap = disk.subarray(6 * 512, 7 * 512)
const isBlockFree = (b) => (bitmap[Math.floor(b / 8)] & (1 << (7 - (b % 8)))) !== 0
const markBlockUsed = (b) => { bitmap[Math.floor(b / 8)] &= ~(1 << (7 - (b % 8))) }
const markBlockFree = (b) => { bitmap[Math.floor(b / 8)] |= (1 << (7 - (b % 8))) }

let freeBlockSearch = 7
const allocateBlock = () => {
  while (freeBlockSearch < 280) {
    if (isBlockFree(freeBlockSearch)) {
      const b = freeBlockSearch++
      markBlockUsed(b)
      disk.fill(0, b * 512, (b + 1) * 512)
      return b
    }
    freeBlockSearch++
  }
  throw new Error("Disk full: no free blocks")
}

// Remove existing non-system files from the directory (keep PRODOS, BASIC.SYSTEM).
let fileCount = 0
let currBlock = 2
while (currBlock !== 0) {
  const blk = disk.subarray(currBlock * 512, (currBlock + 1) * 512)
  const next = blk[0x02] | (blk[0x03] << 8)
  for (let i = 0; i < 13; i++) {
    const off = 4 + i * 39
    if (currBlock === 2 && i === 0) continue
    const stLen = blk[off]
    if (stLen === 0) continue
    const nameLen = stLen & 0x0F
    const name = String.fromCharCode(...blk.subarray(off + 1, off + 1 + nameLen))
    if (name === "PRODOS" || name === "BASIC.SYSTEM") { fileCount++; continue }
    const stType = (stLen >> 4) & 0x0F
    const keyBlk = blk[off + 0x11] | (blk[off + 0x12] << 8)
    if (stType === 1) markBlockFree(keyBlk)
    else if (stType === 2) {
      const idxBlk = disk.subarray(keyBlk * 512, (keyBlk + 1) * 512)
      markBlockFree(keyBlk)
      for (let b = 0; b < 256; b++) {
        const db = idxBlk[b] | (idxBlk[b + 256] << 8)
        if (db !== 0) markBlockFree(db)
      }
    }
    blk.fill(0, off, off + 39)
  }
  currBlock = next
}

const addFile = (filename, type, aux, data) => {
  const size = data.length
  let stType, keyBlock
  if (size <= 512) {
    stType = 1
    keyBlock = allocateBlock()
    disk.set(data, keyBlock * 512)
  } else {
    stType = 2
    keyBlock = allocateBlock()
    const indexBlk = disk.subarray(keyBlock * 512, (keyBlock + 1) * 512)
    const numBlocks = Math.ceil(size / 512)
    for (let i = 0; i < numBlocks; i++) {
      const db = allocateBlock()
      disk.set(data.subarray(i * 512, Math.min(size, (i + 1) * 512)), db * 512)
      indexBlk[i] = db & 0xFF
      indexBlk[i + 256] = (db >> 8) & 0xFF
    }
  }
  const blocksUsed = stType === 1 ? 1 : 1 + Math.ceil(size / 512)
  let blkNum = 2
  let found = false
  while (blkNum !== 0 && !found) {
    const blk = disk.subarray(blkNum * 512, (blkNum + 1) * 512)
    const next = blk[0x02] | (blk[0x03] << 8)
    for (let i = 0; i < 13; i++) {
      const off = 4 + i * 39
      if (blkNum === 2 && i === 0) continue
      if (blk[off] === 0) {
        blk[off] = (stType << 4) | (filename.length & 0x0F)
        for (let c = 0; c < 15; c++) {
          blk[off + 1 + c] = c < filename.length ? filename.charCodeAt(c) : 0x00
        }
        blk[off + 0x10] = type
        blk[off + 0x11] = keyBlock & 0xFF
        blk[off + 0x12] = (keyBlock >> 8) & 0xFF
        blk[off + 0x13] = blocksUsed & 0xFF
        blk[off + 0x14] = (blocksUsed >> 8) & 0xFF
        blk[off + 0x15] = size & 0xFF
        blk[off + 0x16] = (size >> 8) & 0xFF
        blk[off + 0x17] = (size >> 16) & 0xFF
        blk[off + 0x1E] = 0xC3
        blk[off + 0x1F] = aux & 0xFF
        blk[off + 0x20] = (aux >> 8) & 0xFF
        blk[off + 0x25] = 0x02
        blk[off + 0x26] = 0x00
        fileCount++
        found = true
        break
      }
    }
    blkNum = next
  }
}

// 1. Read llvm-mos binary, strip the 4-byte ProDOS BIN header (load addr + len).
const binPath = path.join(buildDir, "main.bin")
const raw = new Uint8Array(fs.readFileSync(binPath))
const program = raw.subarray(4)   // header stripped; raw code loads at $2000

// 2. Compile Applesoft BASIC STARTUP from src/startup.bas
const srcDir = path.join(projectRoot, "src")
const startupBytes = compileApplesoftBasic(srcDir, "startup.bas")

// 3. Write files into the ProDOS image.
addFile("MAIN.BIN", 0x06, 0x2000, program)
addFile("STARTUP", 0xFC, 0x0801, startupBytes)
disk[2 * 512 + 0x25] = fileCount & 0xFF
disk[2 * 512 + 0x26] = (fileCount >> 8) & 0xFF

const outPath = path.join(projectRoot, "TimePilot-IIvera.po")
fs.writeFileSync(outPath, disk)
console.log(`Built ${outPath} (${disk.length} bytes), files=${fileCount}`)
console.log(`  MAIN.BIN raw=${program.length} bytes (load $2000)`)
