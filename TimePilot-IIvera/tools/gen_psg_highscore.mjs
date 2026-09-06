import fs from 'fs';
import path from 'path';
import zlib from 'zlib';

export function getHighScorePsgEvents() {
  const vgmPath = 'build/name_entry.vgm';
  let vgm;
  if (fs.existsSync(vgmPath)) {
    vgm = fs.readFileSync(vgmPath);
  } else {
    throw new Error('Missing build/name_entry.vgm!');
  }
  if (vgm[0] === 0x1f && vgm[1] === 0x8b) vgm = zlib.gunzipSync(vgm);

  let p = 128;
  let sampleTime = 0;
  const frameEvents = new Map();

  while (p < vgm.length) {
    const cmd = vgm[p++];
    if (cmd === 0x66) break;
    else if (cmd === 0x61) { sampleTime += vgm.readUInt16LE(p); p += 2; }
    else if (cmd === 0x62) sampleTime += 735;
    else if (cmd === 0x63) sampleTime += 882;
    else if ((cmd & 0xF0) === 0x70) sampleTime += (cmd & 0x0F) + 1;
    else if (cmd === 0xA0) {
      const r = vgm[p++];
      const v = vgm[p++];
      // SPEEDUP: CX16 highscore.pcm runs ~6% faster than raw arcade VGM tempo.
      // Apply same speedup so Apple II PSG matches what player hears in CX16 version.
      const SPEEDUP = 1.06;
      const frame = Math.round(sampleTime * (60 * SPEEDUP) / 44100);
      if (!frameEvents.has(frame)) frameEvents.set(frame, []);
      frameEvents.get(frame).push({ r, v });
    }
  }

  const maxFrame = Math.max(...frameEvents.keys());
  const regs = new Uint8Array(256);
  const AY_TO_VERA = [ 0, 8, 16, 24, 32, 40, 48, 56, 63, 63, 63, 63, 63, 63, 63, 63 ];

  let chState = [ {freq:0, vol:0}, {freq:0, vol:0}, {freq:0, vol:0}, {freq:0, vol:0} ];
  const events = [];
  let lastFrame = 0;

  for (let f = 0; f <= maxFrame + 5; f++) {
    if (frameEvents.has(f)) {
      for (const { r, v } of frameEvents.get(f)) regs[r] = v;
    }
    const p0 = regs[0] | ((regs[1] & 0x0F) << 8);
    const p1 = regs[2] | ((regs[3] & 0x0F) << 8);
    const p2 = regs[4] | ((regs[5] & 0x0F) << 8);
    const v1A = (regs[135] & 1) === 0 ? (regs[136] & 0x0F) : 0;
    const v1B = (regs[135] & 2) === 0 ? (regs[137] & 0x0F) : 0;
    let p3 = (v1A > 0) ? (regs[128] | ((regs[129] & 0x0F) << 8)) : (regs[130] | ((regs[131] & 0x0F) << 8));
    let v3 = (v1A > 0) ? v1A : v1B;

    const periods = [ p0, p1, p2, p3 ];
    const vols = [
      (regs[7] & 1) === 0 ? (regs[8] & 0x0F) : 0,
      (regs[7] & 2) === 0 ? (regs[9] & 0x0F) : 0,
      (regs[7] & 4) === 0 ? (regs[10] & 0x0F) : 0,
      v3
    ];

    let changesInFrame = [];
    for (let ch = 0; ch < 4; ch++) {
      const period = periods[ch];
      const ayVol = vols[ch];
      const veraVol = AY_TO_VERA[ayVol];
      const veraFreq = (period > 0 && veraVol > 0) ? Math.round(150137 / period) : 0;

      if (chState[ch].freq !== veraFreq || chState[ch].vol !== veraVol) {
        changesInFrame.push({ ch, freq: veraFreq, vol: veraVol });
        chState[ch].freq = veraFreq;
        chState[ch].vol = veraVol;
      }
    }

    if (changesInFrame.length > 0) {
      let delta = f - lastFrame;
      lastFrame = f;

      while (delta > 254) {
        events.push({ delta: 254, ch: 0, vol: chState[0].vol, freq: chState[0].freq });
        delta -= 254;
      }

      for (let i = 0; i < changesInFrame.length; i++) {
        const c = changesInFrame[i];
        events.push({
          delta: (i === 0) ? delta : 0,
          ch: c.ch,
          vol: c.vol,
          freq: c.freq
        });
      }
    }
  }

  // End marker (delta = 255)
  events.push({ delta: 255, ch: 0, vol: 0, freq: 0 });
  return events;
}

if (process.argv[1] && process.argv[1].endsWith('gen_psg_highscore.mjs')) {
  const ev = getHighScorePsgEvents();
  console.log(`Generated ${ev.length} High Score PSG events (${ev.length * 4} bytes).`);
}
