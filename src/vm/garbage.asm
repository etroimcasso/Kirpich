; Garbage cell fold — the per-cell random pick of the starting-garbage fill, transcribed for the
; port's VM.
;
; Source: tetris.asm:4332-4351, the span of InitGarbage from .fillLoop through the tile pick. Reads
; the free-running divider (rDIV) and answers one field cell: either the empty tile ($2F, a gap) or
; one of the eight block tiles ($80-$87). The answer is left in A, which the routine's binding
; returns.
;
; How the pick works, because the shape hides it: the first divider read seeds a countdown, and the
; loop toggles A between the block tile and the empty tile once per step. Whichever value A holds
; when the countdown reaches zero is the answer, so the gap-or-block choice is the divider byte's
; parity. The game's own source calls this out as "equivalent to a 50-50 random chance for either,
; which could have been done much more easily with a BIT test or an AND, without wasting on average
; ~1300 cycles every time a new block is picked". On a block, a second divider read picks which of
; the eight tiles it is.
;
; Why this runs on the VM and not as a native calculation: the countdown runs a data-dependent number
; of iterations (up to 256), and rDIV keeps incrementing while they run — so the second read inside a
; call sees a different divider byte than the first, and how different depends on how long the
; countdown ran. A bare native read would freeze the divider within the call and change which tile a
; block becomes. Running the pick on the SM83 machine reproduces that intra-call advancement by
; construction. Same reasoning, same shape as src/vm/random.asm.
;
; What this does NOT reproduce: the native work between cells — address arithmetic, the one-gap-per-row
; rule, the writes — burns no cycles on this machine, so the divider does not advance across it. Over
; a whole fill that difference accumulates, and the field this produces is not the field the original
; hardware would produce from the same starting divider. The mechanism is ported; the byte stream is
; not claimed. See docs/contracts/garbage-init.md.
;
; rDIV is a hardware register ($FF04); the assembler resolves the name. The routine has no game
; memory cells — its whole product is the byte in A.
;
; Three byte-equivalent adaptations from the ROM body:
;   1. The ROM falls through into its one-gap-per-row check; this leaf routine ends `jr z, done` +
;      `ret` instead. That check, the writes, and the walk over the field are native
;      (docs/contracts/garbage-init.md).
;   2. The ROM records "a gap landed on this row" with `ldh [$A0], a` on the gap branch. It is not
;      here: the returned byte already says which branch was taken (the empty tile means a gap), so
;      the native caller derives the flag and the write would be redundant. It costs this routine the
;      three cycles that write would have taken, which sits inside the divergence described above.
;   3. The ROM spells the empty tile `ld a, " "` through its character map. This assembler has no
;      character map, so it is spelled $2F — the same value, and the one src/data/garbage.h publishes
;      as kGarbageEmptyTile. The tests pin the two spellings against each other.
;
; The divider reading zero is a real case, not an edge to guard: `ld b, a` then `dec b` wraps to $FF,
; so the countdown runs 256 steps rather than none. It is left exactly as the original has it.

ldh a, [rDIV]       ; first read — seeds the countdown
ld  b, a
chooseBlock:
ld  a, $80          ; block arm
loop:
dec b
jr  z, writeTile
cp  a, $80
jr  nz, chooseBlock
ld  a, $2F          ; gap arm (the character map's space)
jr  loop
writeTile:
cp  a, $2F
jr  z, done         ; a gap: A already holds the empty tile
ldh a, [rDIV]       ; second read — which of the eight block tiles
and a, $07
or  a, $80
done:
ret
