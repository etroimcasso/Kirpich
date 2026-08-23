; New-mode piece draw core — the game's own randomizer fold, widened to the New-mode pool.
;
; Byte for byte the body of src/vm/random.asm, with one number changed: the fold wraps at 52 rather
; than 28, because a New round draws from thirteen kinds (the cartridge's seven plus the six New
; shapes) instead of seven. Everything else is deliberately identical — the same divider read, the
; same four-at-a-time count, the same leave-the-candidate-in-A contract — so a New round's draws feel
; like the game's own rather than like something bolted on beside it.
;
; It runs on the VM for the same reason the Classic draw does: the fold loop runs a data-dependent
; number of iterations and rDIV keeps incrementing while they run, so a caller that rejects a
; candidate and folds again reads a DIFFERENT divider byte the second time. A native read would
; freeze the divider within a call and change the distribution. See docs/contracts/piece-random.md.
;
; It also shares ONE machine with the Classic draw and the garbage fill, so a draw advances the
; divider the next read sees. That coupling is the reason all three are registered on the same Vm.
;
; The candidate is left in A: a piece kind times 4, orientation 0, in the set
; {0,4,8,...,48}. Values from 28 up name a New shape (src/data/new_pieces.h).
;
; `cp a, 52` spells 13 * 4 — the engine assembler has no multiply operator, and the value is pinned
; in the tests against kNewModeRawEnd.

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
cp a, 52
jr z, wrap
jr loop
done:
ret
