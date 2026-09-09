; =============================================================================
; loader.s — page-3 trampoline for TPILOT.SYSTEM
;
; Assembled into the SYS image at $2000, then copied to $300 before use.
; Internal control flow is relative (safe after the copy). Absolute addresses
; are only the MLI, the sapling index at $0400, dest $0800, and the param
; block we plant at $3C0.
;
; $3C0  READ_BLOCK param: count, unit, buf, block
; $3C8  number of 512-byte blocks to load
; $B800 sapling index (NOT text page 1 — that splash stays visible under VERA)
; =============================================================================
        .section .rodata
        .global trampoline_src
        .global trampoline_src_end
trampoline_src:
        LDA #3
        STA $3C0
        LDX #0
load_loop:
        LDA $B800,X             ; sapling index: block lo
        STA $3C4
        LDA $B900,X             ; block hi
        STA $3C5
        LDA #0
        STA $3C2                ; dest lo = $00
        TXA
        ASL A                   ; dest = $0800 + X * $200
        CLC
        ADC #$08
        STA $3C3
        STX $3C9
        JSR $BF00
        .byte $80               ; READ_BLOCK
        .word $3C0
        BCS hang
        LDX $3C9
        INX
        CPX $3C8
        BCC load_loop
        JMP $0800
hang:
        SEC
        BCS hang
trampoline_src_end:

        .section .text
        .global launch_game
launch_game:
        JMP $0300
