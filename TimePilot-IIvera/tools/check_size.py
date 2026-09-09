#!/usr/bin/env python3
"""check_size.py - Verify a ProDOS BIN fits under the memory ceiling.

TPILOT.SYSTEM loads the game at $0800. BASIC.SYSTEM is not resident, so the
old $9600 HIMEM ceiling does not apply. The load image must still stay below
the C stack ($BE00) and the ProDOS global page ($BF00). The linker ram region
ends at $B800; this check uses that as the ceiling.

Code size varies with the llvm-mos version, so a binary that fit for one SDK
release can overflow on another without any source change.
"""

import argparse
import os
import sys

DEFAULT_CEILING = 0xB800


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("binaries", nargs="+", help="ProDOS BIN files to check")
    ap.add_argument("--ceiling", type=lambda s: int(s, 0),
                    default=DEFAULT_CEILING,
                    help="first address the binary must NOT reach "
                         f"(default: 0x{DEFAULT_CEILING:04X})")
    ap.add_argument("--strict", action="store_true",
                    help="exit non-zero when a binary crosses the ceiling")
    args = ap.parse_args()

    worst = 0
    for path in args.binaries:
        with open(path, "rb") as f:
            head = f.read(4)
        size = os.path.getsize(path) - 4          # minus the ProDOS BIN header
        load = head[0] | (head[1] << 8)
        end = load + size
        over = end - args.ceiling
        name = os.path.basename(path)
        status = "OK" if over <= 0 else "OVER"
        print(f"  {name:<12} {size:6d} bytes  "
              f"${load:04X}..${end:04X}  ceiling ${args.ceiling:04X}  "
              f"{'headroom' if over <= 0 else 'OVER BY'} "
              f"{abs(over):5d}  [{status}]")
        worst = max(worst, over)

    if worst > 0:
        msg = (f"binary exceeds the ${args.ceiling:04X} ceiling by {worst} bytes "
               f"- it will collide with the C stack / ProDOS global page")
        if args.strict:
            print(f"error: {msg}", file=sys.stderr)
            return 1
        print(f"WARNING: {msg}", file=sys.stderr)
        print("         Configure with -DTPV_STRICT_SIZE=ON to make this fatal.",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
