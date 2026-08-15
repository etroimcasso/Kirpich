; Piece draw core — the game's piece randomizer fold, transcribed for the port's VM.
;
; Source: tetris.asm:1573-1586 (kaspermeerts/tetris, the body shared byte-for-byte with the
; NextPiece.randomChoice copy at tetris.asm:5119-5132). Reads the free-running divider (rDIV) and
; folds the byte into a piece candidate: a piece kind times 4, orientation 0, in the set
; {0,4,8,12,16,20,24}. The candidate is left in A, which the routine's binding returns.
;
; Why this runs on the VM and not as a native calculation: the fold loop runs a data-dependent
; number of iterations (up to ~256), and rDIV keeps incrementing while they run. A caller that
; rejects a candidate and folds again therefore reads a DIFFERENT divider byte the second time. A
; bare native read would freeze the divider within a call and change the piece distribution. Running
; the fold on the SM83 machine reproduces that intra-call advancement by construction. See
; docs/contracts/piece-random.md.
;
; rDIV is a hardware register ($FF04); the assembler resolves the name. The routine has no game
; memory cells — its whole product is the byte in A.
;
; Two byte-equivalent adaptations from the ROM body:
;   1. The ROM falls through into its rejection code; this leaf routine ends `jr z, done` + `ret`
;      instead. The reject/accept bookkeeping is native (docs/contracts/piece-random.md section 4);
;      only the fold lives here.
;   2. `cp a, 28` spells the ROM's `cp a, 7 * 4` — the engine assembler has no multiply operator.
;      28 is the same value (7 kinds x 4 orientations) and is pinned in the tests.

ldh a, [rDIV]
ld  b, a
wrap:
xor a
loop:
dec b
jr z, done
inc a
inc a
inc a
inc a
cp a, 28
jr z, wrap
jr loop
done:
ret
