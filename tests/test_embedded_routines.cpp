// The baked routines are actually in the binary — behavioral test against a shipping property.
//
// All three virtual-machine routines (src/vm/random.asm, src/vm/random_new.asm, src/vm/garbage.asm)
// are baked into the binary at build time and registered before main by a generated translation
// unit. If that registration is missing, registering a routine falls back to reading its .asm from
// the asset root — which succeeds inside a source tree and fails everywhere else, so the failure is
// invisible to a suite that runs from the source tree and to a developer who never moves the binary.
//
// These cases close that gap by pointing the asset root at a directory with no source in it. A
// routine that still registers is coming from the baked copy; one that throws was relying on the
// fallback, which is the defect. This is the regression guard for a shipped build that aborted at
// startup on the first registerRoutine call.
//
// Device-free: a ROM-less virtual machine, no renderer, no audio device.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include <retropp/asset_registry.h>
#include <retropp/timing.h>
#include <retropp/vm.h>

#include "data/new_pieces.h"
#include "vm/garbage_fill.h"
#include "vm/piece_random.h"

namespace {

namespace fs = std::filesystem;

// Points the asset root somewhere with no .asm in it for the duration of a test, and puts back
// whatever was there. The root is process-wide, and the other virtual-machine suites set it to the
// project tree, so restoring is what keeps this from changing their behavior.
class AssetRootWithoutSources {
public:
    AssetRootWithoutSources()
        : previous_{retropp::assetRoot()},
          empty_{fs::temp_directory_path() /
                 ("kirpich-no-sources-" + std::to_string(::testing::UnitTest::GetInstance()
                                                             ->random_seed()))} {
        fs::create_directories(empty_);
        retropp::setAssetRoot(empty_);
    }
    ~AssetRootWithoutSources() {
        retropp::setAssetRoot(previous_);
        std::error_code ec;
        fs::remove_all(empty_, ec);
    }

    [[nodiscard]] const fs::path& path() const { return empty_; }

private:
    fs::path previous_;
    fs::path empty_;
};

}  // namespace

// ── Test 1: RoutinesRegisterWithNoSourceOnDisk ──────────────────────────────────────────────────────
// The property a shipped binary needs: every routine registers, and produces values, with nothing to
// read. Registering is what threw in the field, so the assertion is that it does not.
TEST(EmbeddedRoutines, RoutinesRegisterWithNoSourceOnDisk) {
    const AssetRootWithoutSources noSources;

    // Nothing to fall back to: the asset root holds no .asm at all.
    ASSERT_TRUE(fs::is_empty(noSources.path()));

    retropp::Vm vm{retropp::VMPlatform::GameBoy, retropp::TimingProfile::GameBoy};

    ASSERT_NO_THROW({
        const auto draw    = kirpich::vm::registerPieceRandom(vm);
        const auto drawNew = kirpich::vm::registerNewPieceRandom(vm);
        const auto fold    = kirpich::vm::registerGarbageFold(vm);

        // Registered is not enough — they have to run. A draw is a piece byte, and a fold is either
        // the empty tile or one of the eight block tiles.
        vm.advanceClock(retropp::TimingProfile::GameBoy.cpuCyclesPerTick());
        const std::uint8_t piece    = draw();
        const std::uint8_t newPiece = drawNew();
        const std::uint8_t cell     = fold();

        EXPECT_EQ(piece % 4, 0) << "a drawn piece is kind * 4, orientation 0";
        EXPECT_LT(piece, 28);

        // The New-mode fold is a separate baked routine with a separate wrap, so it is checked
        // against its own pool rather than being assumed to ride along with the first.
        EXPECT_EQ(newPiece % 4, 0) << "a drawn piece is kind * 4, orientation 0";
        EXPECT_LT(newPiece, kirpich::kNewModeRawEnd);

        const bool legalCell =
            cell == kirpich::kGarbageEmptyTile ||
            (cell >= kirpich::kGarbageBlockTileBase &&
             cell < kirpich::kGarbageBlockTileBase + kirpich::kGarbageBlockTileCount);
        EXPECT_TRUE(legalCell) << "fold produced " << static_cast<int>(cell);
    });
}

// The shared-divider wiring requirement is not re-pinned here: tests/test_garbage_fill.cpp already
// asserts it where it is observable, over a whole field rather than a single cell.
