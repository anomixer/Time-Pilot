#!/usr/bin/env python3
"""applesoft.py - Tokenize Applesoft BASIC source into an Apple II program image.

Replaces the external `compileApplesoftBasic` dependency the disk builders used
to import from outside this project, so TimePilot-IIvera builds with nothing but
a compiler, CMake and Python 3.

Output format (Applesoft II, program text based at $0801):

    per line:  <next-line addr, 2 bytes LE>
               <line number, 2 bytes LE>
               <tokenized body>
               <$00>
    end:       <$0000>

Keywords become single bytes $80..$EA; everything else is stored as plain ASCII
with bit 7 clear, which is how the interpreter tells them apart. Spaces outside
quoted strings are dropped (the interpreter re-inserts them when LISTing);
spaces inside strings, and everything after REM, are preserved verbatim.
"""

import argparse
import os
import re
import sys

BASE_ADDR = 0x0801

# Applesoft II token table, in table order. Order matters: the scanner takes the
# first entry that matches at the current position, so longer keywords that
# share a prefix with a later one (GOTO before TO, HTAB before TAB() must come
# first -- which the canonical $80..$EA ordering already arranges.
TOKENS = [
    (0x80, "END"),      (0x81, "FOR"),      (0x82, "NEXT"),     (0x83, "DATA"),
    (0x84, "INPUT"),    (0x85, "DEL"),      (0x86, "DIM"),      (0x87, "READ"),
    (0x88, "GR"),       (0x89, "TEXT"),     (0x8A, "PR#"),      (0x8B, "IN#"),
    (0x8C, "CALL"),     (0x8D, "PLOT"),     (0x8E, "HLIN"),     (0x8F, "VLIN"),
    (0x90, "HGR2"),     (0x91, "HGR"),      (0x92, "HCOLOR="),  (0x93, "HPLOT"),
    (0x94, "DRAW"),     (0x95, "XDRAW"),    (0x96, "HTAB"),     (0x97, "HOME"),
    (0x98, "ROT="),     (0x99, "SCALE="),   (0x9A, "SHLOAD"),   (0x9B, "TRACE"),
    (0x9C, "NOTRACE"),  (0x9D, "NORMAL"),   (0x9E, "INVERSE"),  (0x9F, "FLASH"),
    (0xA0, "COLOR="),   (0xA1, "POP"),      (0xA2, "VTAB"),     (0xA3, "HIMEM:"),
    (0xA4, "LOMEM:"),   (0xA5, "ONERR"),    (0xA6, "RESUME"),   (0xA7, "RECALL"),
    (0xA8, "STORE"),    (0xA9, "SPEED="),   (0xAA, "LET"),      (0xAB, "GOTO"),
    (0xAC, "RUN"),      (0xAD, "IF"),       (0xAE, "RESTORE"),  (0xAF, "&"),
    (0xB0, "GOSUB"),    (0xB1, "RETURN"),   (0xB2, "REM"),      (0xB3, "STOP"),
    (0xB4, "ON"),       (0xB5, "WAIT"),     (0xB6, "LOAD"),     (0xB7, "SAVE"),
    (0xB8, "DEF"),      (0xB9, "POKE"),     (0xBA, "PRINT"),    (0xBB, "CONT"),
    (0xBC, "LIST"),     (0xBD, "CLEAR"),    (0xBE, "GET"),      (0xBF, "NEW"),
    (0xC0, "TAB("),     (0xC1, "TO"),       (0xC2, "FN"),       (0xC3, "SPC("),
    (0xC4, "THEN"),     (0xC5, "AT"),       (0xC6, "NOT"),      (0xC7, "STEP"),
    (0xC8, "+"),        (0xC9, "-"),        (0xCA, "*"),        (0xCB, "/"),
    (0xCC, "^"),        (0xCD, "AND"),      (0xCE, "OR"),       (0xCF, ">"),
    (0xD0, "="),        (0xD1, "<"),        (0xD2, "SGN"),      (0xD3, "INT"),
    (0xD4, "ABS"),      (0xD5, "USR"),      (0xD6, "FRE"),      (0xD7, "SCRN("),
    (0xD8, "PDL"),      (0xD9, "POS"),      (0xDA, "SQR"),      (0xDB, "RND"),
    (0xDC, "LOG"),      (0xDD, "EXP"),      (0xDE, "COS"),      (0xDF, "SIN"),
    (0xE0, "TAN"),      (0xE1, "ATN"),      (0xE2, "PEEK"),     (0xE3, "LEN"),
    (0xE4, "STR$"),     (0xE5, "VAL"),      (0xE6, "ASC"),      (0xE7, "CHR$"),
    (0xE8, "LEFT$"),    (0xE9, "RIGHT$"),   (0xEA, "MID$"),
]

LINE_RE = re.compile(r"^\s*(\d+)\s?(.*)$")


class BasicError(Exception):
    pass


def tokenize_body(text, lineno):
    """Tokenize everything after the line number of a single BASIC line."""
    out = bytearray()
    i = 0
    n = len(text)
    while i < n:
        c = text[i]

        # Quoted string: copy verbatim, spaces included.
        if c == '"':
            out.append(0x22)
            i += 1
            while i < n and text[i] != '"':
                out.append(ord(text[i]) & 0x7F)
                i += 1
            if i < n:
                out.append(0x22)
                i += 1
            continue

        # Spaces outside strings carry no meaning; drop them.
        if c == " ":
            i += 1
            continue

        # Keyword?
        for tok, kw in TOKENS:
            if text.startswith(kw, i):
                out.append(tok)
                i += len(kw)
                # REM swallows the rest of the line exactly as written.
                if tok == 0xB2:
                    out.extend(ch & 0x7F for ch in text[i:].encode("ascii", "replace"))
                    i = n
                break
        else:
            ch = ord(c)
            if ch > 0x7F:
                raise BasicError(
                    f"line {lineno}: non-ASCII character {c!r}")
            out.append(ch)
            i += 1

    return bytes(out)


def compile_applesoft(source, base=BASE_ADDR):
    """Compile Applesoft source text into a tokenized program image."""
    parsed = []
    for raw in source.splitlines():
        if not raw.strip():
            continue
        m = LINE_RE.match(raw)
        if not m:
            raise BasicError(f"missing line number: {raw!r}")
        num = int(m.group(1))
        if not 0 <= num <= 63999:
            raise BasicError(f"line number out of range: {num}")
        parsed.append((num, m.group(2).rstrip("\r\n")))

    if not parsed:
        raise BasicError("no BASIC lines found")

    prev = -1
    for num, _ in parsed:
        if num <= prev:
            raise BasicError(
                f"line numbers must ascend: {num} follows {prev}")
        prev = num

    out = bytearray()
    addr = base
    for num, text in parsed:
        body = tokenize_body(text, num)
        nxt = addr + 4 + len(body) + 1
        out += bytes((nxt & 0xFF, (nxt >> 8) & 0xFF,
                      num & 0xFF, (num >> 8) & 0xFF))
        out += body
        out.append(0x00)
        addr = nxt
    out += b"\x00\x00"
    return bytes(out)


def compile_file(path, base=BASE_ADDR):
    with open(path, "r", encoding="utf-8") as f:
        return compile_applesoft(f.read(), base)


def main():
    ap = argparse.ArgumentParser(description="Tokenize Applesoft BASIC.")
    ap.add_argument("source", help="input .bas file")
    ap.add_argument("-o", "--output", help="output file (default: stdout hex dump)")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=BASE_ADDR,
                    help="program base address (default: 0x0801)")
    args = ap.parse_args()

    try:
        data = compile_file(args.source, args.base)
    except (BasicError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    if args.output:
        with open(args.output, "wb") as f:
            f.write(data)
        print(f"{os.path.basename(args.source)}: {len(data)} bytes -> {args.output}")
    else:
        for o in range(0, len(data), 16):
            row = data[o:o + 16]
            print(f"{o:04X}  " + " ".join(f"{b:02X}" for b in row))
    return 0


if __name__ == "__main__":
    sys.exit(main())
