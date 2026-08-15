#include "vm/piece_random.h"

#include <cstdint>

#include "retropp/gb.h"  // retropp::gb::A (pulls retropp/vm.h)

namespace kirpich::vm {

retropp::Routine<std::uint8_t()> registerPieceRandom(retropp::Vm& vm) {
    // Registers and the asset path appear only here, inside the binding (the VM host contract; the
    // path is a literal at its use site). Output is register A — the fold leaves the candidate
    // there. Embed: the .asm is assembled and baked into the binary at build time — no runtime file.
    return vm.registerRoutine<std::uint8_t()>(
        "src/vm/random.asm",
        retropp::RoutineBinding{.output = retropp::gb::A,
                                .throttle = retropp::Throttle::HostSpeed},
        retropp::AssetPolicy::Embed);
}

Piece pickRandomPiece(const retropp::Routine<std::uint8_t()>& draw, GameFlowState& flow) {
    // tetris.asm:1565-1606. The reference `c` is the temp-preview kind; the next-preview does not
    // change across the loop, so it is read once (the ROM re-reads it each try — same value).
    const std::uint8_t next = flow.nextPreviewPiece.raw;                    // `e` in the ROM
    const std::uint8_t c = static_cast<std::uint8_t>(flow.tempPreviewPiece.raw & ~0x03);

    std::uint8_t cand = 0;
    for (int tries = 3;;) {
        cand = draw();  // folded candidate: kind * 4, low two bits clear (a fresh, advanced rDIV
                        // read each retry — docs/contracts/piece-random.md section 3)
        --tries;        // the counter drops before the test, so the third draw is accepted
        if (tries == 0) {
            break;      // third draw: auto-accept (tetris.asm:1592-1593)
        }
        // Reject a candidate whose kind, OR-ed with the next-preview and the reference kind, adds no
        // bit outside the reference kind (tetris.asm:1594-1598).
        const std::uint8_t masked = static_cast<std::uint8_t>((next | cand | c) & ~0x03);
        if (masked != c) {
            break;      // accept
        }
        // else reject and draw again
    }

    flow.nextPreviewPiece = Piece{cand};  // next-preview <- accepted candidate
    flow.tempPreviewPiece = Piece{next};  // temp-preview <- old next-preview
    return Piece{next};                   // return the old next-preview (the one-stage pipeline)
}

}  // namespace kirpich::vm
