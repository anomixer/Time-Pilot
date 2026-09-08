#!/usr/bin/env python3
"""
extract_cx16_sprites.py
Extracts sprite artwork from TimePilot-CX16/sprites/*.png using TimePilot-CX16/misc/palette.txt
and formats them as C array declarations for src/art.h.
"""
import os
import sys
from PIL import Image

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
CX16_ROOT = os.path.join(PROJECT_ROOT, "..", "TimePilot-CX16")
SPRITES_DIR = os.path.join(CX16_ROOT, "sprites")
PALETTE_FILE = os.path.join(CX16_ROOT, "misc", "palette.txt")

# Load palette
pal_dict = {}
with open(PALETTE_FILE, "r") as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) >= 2:
            pal_dict[parts[0].upper()] = int(parts[1])

def get_pixel_index(r, g, b, a):
    if a < 128:
        return 0
    key = f"{r:02X}{g:02X}{b:02X}".upper()
    return pal_dict.get(key, 0)

def extract_frames(filename, num_frames_x, out_w, out_h):
    path = os.path.join(SPRITES_DIR, filename)
    img = Image.open(path).convert("RGBA")
    in_frame_w = img.width // num_frames_x
    in_frame_h = img.height

    frames = []
    for f in range(num_frames_x):
        frame_bytes = []
        box = (f * in_frame_w, 0, (f + 1) * in_frame_w, in_frame_h)
        sub = img.crop(box)
        pixels = sub.load()
        for y in range(out_h):
            for x in range(out_w):
                if x < in_frame_w and y < in_frame_h:
                    r, g, b, a = pixels[x, y]
                    frame_bytes.append(get_pixel_index(r, g, b, a))
                else:
                    frame_bytes.append(0)
        frames.append(frame_bytes)
    return frames

def format_c_array(var_name, frames, out_w, out_h):
    num_frames = len(frames)
    frame_size = out_w * out_h
    lines = [f"static const uint8_t {var_name}[{num_frames}][{frame_size}] = {{"]
    for f_idx, frame in enumerate(frames):
        lines.append("  {")
        for y in range(out_h):
            row = frame[y * out_w : (y + 1) * out_w]
            row_str = ",".join(str(b) for b in row) + ","
            lines.append(f"    {row_str}")
        lines.append("  },")
    lines.append("};")
    return "\n".join(lines)

def main():
    specs = [
        ("cloud0_frames", "cloud0.png", 1, 16, 16),
        ("cloud1_frames", "cloud1.png", 1, 32, 16),
        ("cloud2_frames", "cloud2.png", 1, 64, 16),
        ("astro0_frames", "astro0.png", 1, 16, 16),
        ("astro1_frames", "astro1.png", 1, 16, 16),
        ("astro2_frames", "astro2.png", 1, 32, 16),
        ("parachute_frames", "parachute.png", 4, 16, 16),
        ("logo_time_frames", "time.png", 1, 64, 16),
        ("logo_pilot_frames", "pilot.png", 1, 64, 16),
        ("bomb_frames", "bomb.png", 2, 16, 16),
        ("boomerang_frames", "boomerang.png", 8, 16, 16),
        ("rocket_frames", "rocket.png", 16, 16, 16),
        ("sbullet_frames", "sbullet.png", 8, 16, 16),
        ("l1bomber_frames", "l1bomber.png", 8, 32, 16),
        ("expl32x16_frames", "expl32x16.png", 4, 32, 16),
        ("number_frames", "number.png", 6, 16, 16),
    ]

    output_blocks = []
    for var_name, filename, num_frames_x, out_w, out_h in specs:
        frames = extract_frames(filename, num_frames_x, out_w, out_h)
        output_blocks.append(format_c_array(var_name, frames, out_w, out_h))
        print(f"Extracted {filename} -> {var_name}: {len(frames)} frame(s) of {out_w}x{out_h} ({len(frames)*out_w*out_h} bytes)")

    out_file = os.path.join(PROJECT_ROOT, "src", "art_new.inc")
    with open(out_file, "w") as f:
        f.write("\n\n".join(output_blocks) + "\n")
    print(f"Saved generated C arrays to {out_file}")

if __name__ == "__main__":
    main()
