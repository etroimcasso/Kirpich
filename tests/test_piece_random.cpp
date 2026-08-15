// Piece randomizer — behavioral tests against docs/contracts/piece-random.md.
//
// The draw core runs on a ROM-less, headless SM83 VM; these tests construct one directly (the first
// ctest unit to do so). The divider cannot be written from this surface, so the exact
// byte -> candidate map is pinned on a mirror of the fold and the VM side is checked against that
// relation's image, its determinism, and the intra-call advancement quirk. pickRandomPiece is driven
// with the real VM draw; reset() makes the draw sequence reproducible, which lets the rejection and
// auto-accept paths be exercised deterministically.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <set>
#include <vector>

#include <kirpich/piece.h>

#include "retropp/asset_registry.h"
#include "retropp/timing.h"
#include "retropp/vm.h"
#include "state/game_flow_state.h"
#include "vm/piece_random.h"

namespace {

using kirpich::GameFlowState;
using kirpich::Piece;

// A DMG-timed, ROM-less VM — the machine the randomizer is hosted on. The timing profile is passed
// explicitly because the constructor defaults to Game Boy Color.
//
// The asset root is pointed at the project tree so registerPieceRandom's routine load resolves
// src/vm/random.asm during the test (development builds define KIRPICH_PROJECT_ROOT — the same base
// src/main.cpp sets). The routine bytes are assembled from that source by the same assembler that
// bakes the shipped copy, so the behavior under test is identical.
retropp::Vm makeVm() {
#ifdef KIRPICH_PROJECT_ROOT
    retropp::setAssetRoot(std::filesystem::path{KIRPICH_PROJECT_ROOT});
#endif
    return retropp::Vm(retropp::VMPlatform::GameBoy, retropp::TimingProfile::GameBoy);
}

// One tick's worth of CPU cycles — what the game advances the divider by each sim tick.
constexpr std::uint64_t kCyclesPerTick = retropp::TimingProfile::GameBoy.cpuCyclesPerTick();

// The fold as docs/contracts/piece-random.md derives it (tetris.asm:1573-1586): a divider byte folds
// to kind * 4. A read of 0 behaves as 256 (it decrements to 255 before the byte reaches 0). This
// mirror is the reference the traced-vector test pins; the VM computes the same relation on the
// machine.
constexpr std::uint8_t foldCandidate(std::uint8_t b) {
    const int steps = (b == 0) ? 255 : (b - 1);  // +4 steps before the byte reaches 0
    return static_cast<std::uint8_t>(4 * (steps % 7));
}

const std::set<std::uint8_t> kDomain = {0, 4, 8, 12, 16, 20, 24};

}  // namespace

// Domain — the routine registers and every draw is a valid piece kind at orientation 0.
TEST(PieceRandom, RegistersAndDrawsInDomain) {
    auto vm = makeVm();
    const auto draw = kirpich::vm::registerPieceRandom(vm);
    for (int i = 0; i < 512; ++i) {
        const std::uint8_t r = draw();
        EXPECT_EQ(kDomain.count(r), 1u) << "draw " << i << " out of domain: " << int(r);
        vm.advanceClock(kCyclesPerTick);
    }
}

// Fold — the fold relation, pinned against the contract's hand-traced vectors, plus the VM's
// outputs shown to lie in that relation's image.
TEST(PieceRandom, FoldRelationMatchesTracedVectors) {
    EXPECT_EQ(foldCandidate(1), 0);    // b = 1: zero +4 steps
    EXPECT_EQ(foldCandidate(2), 4);
    EXPECT_EQ(foldCandidate(7), 24);   // last kind before the wrap
    EXPECT_EQ(foldCandidate(8), 0);    // wraps at 28 back to 0
    EXPECT_EQ(foldCandidate(9), 4);
    EXPECT_EQ(foldCandidate(0), 12);   // 256-iteration path: 255 mod 7 == 3
    for (int b = 0; b < 256; ++b) {
        const std::uint8_t c = foldCandidate(static_cast<std::uint8_t>(b));
        EXPECT_EQ(c % 4, 0);
        EXPECT_LT(c, 28u);
    }
    auto vm = makeVm();
    const auto draw = kirpich::vm::registerPieceRandom(vm);
    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(kDomain.count(draw()), 1u);
        vm.advanceClock(kCyclesPerTick);
    }
}

// Determinism — a reset machine on a fixed advance schedule reproduces its draw sequence,
// and the sequence is not a single frozen value.
TEST(PieceRandom, DeterministicAcrossReset) {
    auto vm = makeVm();
    const auto draw = kirpich::vm::registerPieceRandom(vm);
    auto capture = [&] {
        vm.reset();
        std::vector<std::uint8_t> seq;
        for (int i = 0; i < 64; ++i) {
            seq.push_back(draw());
            vm.advanceClock(kCyclesPerTick);
        }
        return seq;
    };
    const std::vector<std::uint8_t> a = capture();
    const std::vector<std::uint8_t> b = capture();
    EXPECT_EQ(a, b);
    const std::set<std::uint8_t> distinct(a.begin(), a.end());
    EXPECT_GT(distinct.size(), 1u) << "the draw sequence never varied — the divider is not feeding it";
}

// Live divider — the divider feeds the draw: different clock schedules drive it to different positions and
// yield different sequences.
TEST(PieceRandom, DifferentClockSchedulesDrawDifferently) {
    auto vm = makeVm();
    const auto draw = kirpich::vm::registerPieceRandom(vm);
    auto run = [&](std::uint64_t advance) {
        vm.reset();
        std::vector<std::uint8_t> seq;
        for (int i = 0; i < 64; ++i) {
            seq.push_back(draw());
            vm.advanceClock(advance);
        }
        return seq;
    };
    EXPECT_NE(run(kCyclesPerTick), run(kCyclesPerTick * 3));
}

// Intra-call advancement (the quirk) — two draws with no host advance between them can differ, because the fold's own
// cycles tick the divider. This is exactly what a retry inside pickRandomPiece sees. A frozen byte
// source would make every back-to-back pair equal.
TEST(PieceRandom, DividerAdvancesWithinACall) {
    auto vm = makeVm();
    const auto draw = kirpich::vm::registerPieceRandom(vm);
    vm.reset();
    int differ = 0;
    for (int i = 0; i < 64; ++i) {
        const std::uint8_t first = draw();
        const std::uint8_t second = draw();  // immediately, no advanceClock
        if (first != second) {
            ++differ;
        }
        vm.advanceClock(kCyclesPerTick);  // reposition for the next pair
    }
    EXPECT_GT(differ, 0) << "back-to-back draws never differed — the divider froze within the call";
}

// Selection — pickRandomPiece semantics: the pipeline-delay return + field mutations, the accept path,
// and the unconditional third-draw accept. The draw is reproducible after reset(), so the reference
// kind can be chosen to force specific accept / reject outcomes.
TEST(PieceRandom, PickCommitsPipelineAndAutoAcceptsThirdDraw) {
    auto vm = makeVm();
    const auto draw = kirpich::vm::registerPieceRandom(vm);

    // The three candidates pickRandomPiece will draw back-to-back after a reset.
    vm.reset();
    const std::uint8_t s0 = draw();
    const std::uint8_t s1 = draw();
    const std::uint8_t s2 = draw();

    // Accept-on-first: with the reference kind c == 0 and a next-preview that carries a kind bit,
    // the rejection test can never equal c, so the first candidate is accepted unconditionally. This
    // pins the accept path and the one-stage pipeline (return + both field writes) exactly.
    {
        vm.reset();
        GameFlowState flow;
        flow.nextPreviewPiece = Piece{4};  // old next-preview, carries a kind bit
        flow.tempPreviewPiece = Piece{0};  // c == 0
        const Piece got = kirpich::vm::pickRandomPiece(draw, flow);
        EXPECT_EQ(got, Piece{4}) << "returns the old next-preview (pipeline delay), not the candidate";
        EXPECT_EQ(flow.tempPreviewPiece, Piece{4}) << "temp-preview takes the old next-preview";
        EXPECT_EQ(flow.nextPreviewPiece, Piece{s0}) << "next-preview takes the accepted first candidate";
    }

    // Forced double rejection: with next-preview 0 and a reference kind that is a superset of both
    // s0 and s1's kind bits, the first two candidates are always rejected, so the third draw is
    // accepted unconditionally — the auto-accept.
    {
        vm.reset();
        GameFlowState flow;
        flow.nextPreviewPiece = Piece{0};
        flow.tempPreviewPiece = Piece{static_cast<std::uint8_t>((s0 | s1) & ~0x03)};
        const Piece got = kirpich::vm::pickRandomPiece(draw, flow);
        EXPECT_EQ(flow.nextPreviewPiece, Piece{s2}) << "third draw accepted unconditionally";
        EXPECT_EQ(got, Piece{0}) << "still returns the old next-preview";
    }
}
