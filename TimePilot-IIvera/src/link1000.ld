/* Apple IIe Time Pilot IIvera — custom linker script.
 *
 * TPILOT.SYSTEM (not BASIC.SYSTEM/BRUN) loads this image at $0800 and jumps
 * there. BASIC.SYSTEM is not resident, so the old $9600 HIMEM ceiling is gone.
 *
 * $0800-$B7FF is the load image (45,056 bytes). $B800-$B9FF is the MLI
 * READ_BLOCK window (diskBuf). The C stack grows down from $BE00 through
 * $BA00-$BDFF (1 KB before it hits the disk window). $BF00-$BFFF is the
 * ProDOS global page and must not be overwritten.
 *
 * Entry point must equal the load address ($0800).
 */

/* Provide imaginary (zero page) registers.  Apple II zero page $00-$01 is
 * reserved; we start imaginary registers at $02. */
__rc0 = 0x02;
INCLUDE imag-regs.ld   /* __rc31 = 0x20 */

MEMORY {
    /* Zero page variables: $21-$BF.  ($C0-$FF left for system/monitor.) */
    zp : ORIGIN = __rc31 + 1, LENGTH = 0xBF - (__rc31 + 1)
    /* Main program RAM: $0800-$B7FF. */
    ram (rw) : ORIGIN = 0x0800, LENGTH = 0xB000
}

REGION_ALIAS("c_readonly", ram)
REGION_ALIAS("c_writeable", ram)

/* Entry point at $0800 (TPILOT.SYSTEM jumps here). */
SECTIONS {
    . = ORIGIN(ram);
    .text : { INCLUDE text-sections.ld } > c_readonly
    INCLUDE zp.ld
    INCLUDE rodata.ld
    INCLUDE data.ld
    INCLUDE bss.ld
    INCLUDE noinit.ld
}

/* Soft stack grows down from $BE00 (same as stock apple2e). */
__stack = 0xBE00;

OUTPUT_FORMAT {
    /* ProDOS BIN header: 2-byte load address ($0800) + 2-byte length.
     * TPILOT.SYSTEM ignores this header (the packager strips it); it is
     * kept so check_size.py can still read the load address. */
    SHORT(ORIGIN(ram))
    SHORT(__data_end - ORIGIN(ram))
    TRIM(ram)
}
