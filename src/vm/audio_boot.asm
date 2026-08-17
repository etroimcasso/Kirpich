; Sound-driver startup — the part of the game's own startup the sound driver depends on.
;
; Source: tetris.asm:301-306, inside the game's startup code:
;
;     ld hl, rNR52
;     ld a, $80
;     ldd [hl], a         ; Turn the sound on
;     ld a, $FF
;     ldd [hl], a         ; Output all sounds to both terminals
;     ld [hl], $77        ; Maximum volume
;
; Those writes are in the startup path, not in the sound driver, so the driver's own image does not
; carry them and its initialisation routine never makes them either — it sets the routing register
; and mutes the channels, but never switches the hardware on or sets the master volume. Hosting the
; driver alone therefore leaves the sound hardware powered down at zero volume, and every register
; the driver writes is ignored: the driver's own bookkeeping advances normally and a song "plays"
; while nothing is heard.
;
; This runs once, when the driver starts, and ends by calling the driver's own initialisation so the
; whole startup is a single routine the audio engine runs while it places the driver. Two reasons it
; is machine code rather than writes performed from outside:
;
;   * switching the sound hardware on is an effect of the processor's write reaching the sound chip.
;     Poking the same addresses in memory sets the bytes without powering anything on, and every
;     later register write is then ignored — the driver's bookkeeping advances and a song "plays"
;     while nothing is heard;
;   * running as the declared startup routine means it happens while the driver is being placed,
;     which is the only point guaranteed to precede the driver's first pass.
;
; The driver's initialisation is called last, not first: with the hardware off its register writes
; would have no effect. That is the order the game's own startup uses (tetris.asm:301-306, then the
; InitAudio call at :367).
;
; The work-RAM clear in the middle is the other half of what the driver depends on, and leaving it
; out is not a subtle fault. A machine's RAM does not start zeroed, and the driver keeps all of its
; state in the block this clears: the pause command and the pause-tune countdown it reads at the top
; of every pass, the four request mailboxes, and the music workspace holding one data pointer per
; channel. Started on uninitialised memory the driver diverts to its pause-tune path and never plays,
; consumes requests that were never made, or follows a garbage channel pointer into memory that holds
; no code at all. The game clears this same block at startup (tetris.asm:311-317, "Clears the upper
; 256 bytes"), 256 bytes down from $DFFF, and so does this.
;
; The `ldd` walk of the original is spelled as three direct writes here: it targets three descending
; addresses ($FF26, $FF25, $FF24) and the register names make that legible where the pointer walk
; does not. Same three values at the same three addresses, in the same order.

ld a, $80
ldh [rNR52], a      ; sound on
ld a, $FF
ldh [rNR51], a      ; every channel to both outputs
ld a, $77
ldh [rNR50], a      ; maximum volume, both outputs

xor a               ; clear the driver's work RAM: 256 bytes down from $DFFF
ld hl, $DFFF
ld b, $00
clearWorkRam:
ld [hl-], a         ; the ROM spells this `ldd [hl], a`; this assembler has only the `[hl-]` form
dec b
jr nz, clearWorkRam

call $7FF3          ; InitAudio - the driver's own initialisation, now that the hardware is on
ret
