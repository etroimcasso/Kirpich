// The sound system: the registration that describes the game's own sound driver to the audio engine,
// and the per-frame decision that hands it the frame's cues.
//
// Two things are checked here, and they are checked separately because they fail separately. The
// REGISTRATION is a description — where the driver's image goes, which entries run, which bytes the
// game may touch — and a wrong byte in it is silent at build time and wrong forever at run time, so
// every field of it is pinned here against the addresses the audio-state contract adjudicated. The
// FRAME DECISION is a pure function of the game's state, so the whole cue protocol (what is sent,
// and in what order) is exercised without any sound hardware at all.
//
// Test 7 reads the real ROM to check one thing the other six cannot: that placing the extracted image
// at the base the registration names actually puts the driver's two entry points where it says they
// are. It is resolved from the CI provisioning path first, then the dev sibling; a machine with
// neither FAILS loudly - a missing ROM is a provisioning failure, never a skip.
//
// What no test here can prove is that the result SOUNDS right; that is a listening check on a running
// build.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <retropp/audio_library.h>
#include <retropp/driver_binding.h>
#include <retropp/gb.h>
#include <retropp/isa.h>
#include <retropp/vm.h>

#include "assets/extract.h"
#include "data/music.h"
#include "data/sfx.h"
#include "state/demo_state.h"
#include "systems/audio_cues.h"
#include "systems/game_context.h"
#include "systems/sound.h"

namespace {

namespace fs = std::filesystem;

using kirpich::ActiveDemo;
using kirpich::MusicId;
using kirpich::NoiseSfxId;
using kirpich::SquareSfxId;
using kirpich::WaveSfxId;
using kirpich::assets::kRomSize;
using kirpich::assets::kSoundDriverImageBase;
using kirpich::assets::kSoundDriverImageSize;
using kirpich::assets::kSoundDriverInitEntry;
using kirpich::assets::kSoundDriverTickEntry;
using kirpich::assets::soundDriverImage;
using kirpich::systems::AudioPauseCommand;
using kirpich::systems::GameContext;
using kirpich::systems::gesturesFor;
using kirpich::systems::SoundDriverSlots;
using kirpich::systems::SoundGestures;
using kirpich::systems::soundDriverId;

// The stored description of the driver, as the audio catalog holds it. Registration happens on the
// first call and is shared thereafter, so every case below reads the same one.
const retropp::DriverDefinition& definition() {
    const retropp::AudioLibrary::Entry& entry =
        retropp::AudioLibrary::instance().entry(soundDriverId().id());
    EXPECT_EQ(entry.kind, retropp::AudioKind::Driver);
    EXPECT_EQ(entry.type, retropp::AudioType::VMDriver);
    EXPECT_EQ(entry.isa, retropp::Isa::Sm83);
    return *entry.driver;
}

// The real ROM, resolved the way CI is provisioned: the fixed per-platform path first, then the
// development sibling. Registers a test failure naming both candidates when neither exists.
fs::path requireRomPath() {
#ifdef _WIN32
    const fs::path ciPath{"C:\\ci-assets\\kirpich\\tetris.gb"};
#else
    const char*    home   = std::getenv("HOME");
    const fs::path ciPath = (home != nullptr)
                                ? fs::path{home} / "ci-assets" / "kirpich" / "tetris.gb"
                                : fs::path{};
#endif
    if (!ciPath.empty() && fs::exists(ciPath)) {
        return ciPath;
    }
    const fs::path devPath =
        fs::path{KIRPICH_PROJECT_ROOT}.parent_path() / "rom" / "Tetris (World) (Rev 1).gb";
    if (fs::exists(devPath)) {
        return devPath;
    }
    ADD_FAILURE() << "The Tetris ROM is required and was not found. Provision it at\n  "
                  << (ciPath.empty() ? "(CI path unavailable: no HOME)" : ciPath.string())
                  << "\nor\n  " << devPath.string();
    return {};
}

std::vector<std::uint8_t> readBytes(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

// 1. The driver is described by its own entry points and placement: one image at the audio section's
//    base, the per-frame entry and the initialisation entry the cartridge published, no cartridge
//    banking, and the console's own instruction set. The image is read from disk, never compiled in -
//    those bytes belong to the player's cartridge.
TEST(Sound, RegistrationDescribesTheDriversOwnEntries) {
    const retropp::DriverDefinition& def = definition();

    // Two images: the cartridge's own driver, and the port's sound-hardware startup routine.
    ASSERT_EQ(def.images.size(), 2U);

    EXPECT_EQ(def.images[0].base, kSoundDriverImageBase);
    EXPECT_EQ(def.images[0].base, 0x6480U);
    EXPECT_EQ(def.images[0].path, "assets/audio/default/sound_driver.bin");
    ASSERT_TRUE(def.images[0].policy.has_value()) << "the image policy is stated, never defaulted";
    EXPECT_EQ(*def.images[0].policy, retropp::AssetPolicy::LoadFromPath)
        << "cartridge content is read from disk, never compiled in";
    EXPECT_TRUE(def.images[0].bytes.empty()) << "no cartridge bytes are held in the binary";

    // The startup routine is the port's own code, so it is baked in, and it is placed clear of the
    // driver's span so it overwrites none of the cartridge's bytes.
    EXPECT_EQ(def.images[1].path, "src/vm/audio_boot.asm");
    ASSERT_TRUE(def.images[1].policy.has_value());
    EXPECT_EQ(*def.images[1].policy, retropp::AssetPolicy::Embed);
    EXPECT_LT(def.images[1].base, kSoundDriverImageBase)
        << "the startup routine overlaps the driver's own image";

    EXPECT_EQ(def.tickEntry, kSoundDriverTickEntry);
    EXPECT_EQ(def.tickEntry, 0x7FF0U);
    EXPECT_TRUE(def.mapper.isNone()) << "a 32 KiB cartridge has no bank controller";

    // The stack is stated, not defaulted, and it is the game's own. The driver's per-pass pushes
    // grow down from here; from the top of work RAM they land on its own request mailboxes, which
    // silences whichever channel they overwrite.
    ASSERT_TRUE(def.stackTop.has_value()) << "the stack top must be stated, never defaulted";
    EXPECT_EQ(*def.stackTop, 0xCFFFU);
    EXPECT_LT(*def.stackTop, 0xDF70U) << "the stack must sit clear of the driver's working memory";

    // Startup runs the port's routine, not the driver's initialisation directly: the sound hardware
    // has to be switched on first, and that routine does it before calling the driver's own.
    ASSERT_TRUE(def.init.has_value());
    EXPECT_EQ(def.init->kind(), retropp::Instruction::Kind::Call);
    EXPECT_EQ(def.init->entry(), def.images[1].base);
    EXPECT_NE(def.init->entry(), kSoundDriverInitEntry);
}

// 2. The six bytes the game shares with the driver, at the addresses the audio-state contract
//    adjudicated, in declaration order, each one byte wide, each with the direction its use demands:
//    the current-song byte is published by the driver and only read; the wave mailbox is both written
//    and read; the rest are write-only.
TEST(Sound, SlotTableIsTheSharedAudioInterface) {
    const std::vector<retropp::SlotSpec>& slots = definition().slots;
    ASSERT_EQ(slots.size(), 6U);

    struct Expected {
        std::uint32_t          address;
        retropp::SlotDirection direction;
        const char*            label;
    };
    const Expected expected[] = {
        {0xDF7F, retropp::SlotDirection::Write, "wPauseUnpauseSound"},
        {0xDFE0, retropp::SlotDirection::Write, "wNewSquareSFXID"},
        {0xDFE9, retropp::SlotDirection::Read, "wCurrentMusicID"},
        {0xDFF0, retropp::SlotDirection::ReadWrite, "wNewWaveSFXID"},
        {0xDFF8, retropp::SlotDirection::Write, "wNewNoiseSFXID"},
        {0xFFE4, retropp::SlotDirection::Write, "hDemoNumber"},
    };

    for (std::size_t i = 0; i < slots.size(); ++i) {
        EXPECT_EQ(slots[i].address, expected[i].address) << "slot " << i << " (" << expected[i].label << ")";
        EXPECT_EQ(slots[i].direction, expected[i].direction)
            << "slot " << i << " (" << expected[i].label << ")";
        EXPECT_EQ(slots[i].width, 1) << "slot " << i << " (" << expected[i].label << ")";
    }
}

// 3. Starting a song leaves its number in the music mailbox; stopping calls the driver's own
//    initialisation entry. No separate effect lane is declared - the effect channels are mailboxes,
//    reached as slots - so asking for one is a loud error rather than a silent nothing.
TEST(Sound, VerbsReachTheDriverTheWayTheGameDid) {
    const retropp::DriverVerbs& verbs = definition().verbs;

    ASSERT_TRUE(verbs.play.music.has_value());
    EXPECT_EQ(verbs.play.music->kind(), retropp::Instruction::Kind::Write);
    EXPECT_EQ(verbs.play.music->location().kind(), retropp::Location::Kind::Memory);
    EXPECT_EQ(verbs.play.music->location().address(), 0xDFE8U);  // wNewMusicID
    EXPECT_EQ(verbs.play.music->width(), 1);
    EXPECT_FALSE(verbs.play.music->fixedValue().has_value())
        << "the played song number is the value, so nothing is fixed";

    EXPECT_FALSE(verbs.play.sfx.has_value());
    EXPECT_FALSE(verbs.play.vocals.has_value());

    ASSERT_TRUE(verbs.stop.has_value());
    EXPECT_EQ(verbs.stop->kind(), retropp::Instruction::Kind::Call);
    EXPECT_EQ(verbs.stop->entry(), kSoundDriverInitEntry);
    EXPECT_EQ(verbs.stop->location().kind(), retropp::Location::Kind::Register);
    EXPECT_EQ(verbs.stop->location().registerId(), retropp::gb::A.registerId());
}

// 4. A frame's cues become exactly the gestures that frame asks for, and a frame that asks for
//    nothing still publishes the demo gate and nothing else.
TEST(Sound, FrameCuesBecomeTheFramesGestures) {
    {
        GameContext          idle;
        const SoundGestures  gestures = gesturesFor(idle);
        EXPECT_EQ(gestures.demoGate.demoNumber, std::optional<std::uint8_t>{0});
        EXPECT_FALSE(gestures.initDriver);
        EXPECT_FALSE(gestures.music.has_value());
        EXPECT_EQ(gestures.mailboxes, SoundDriverSlots{}) << "an idle frame asks for no sound";
    }

    {
        GameContext game;
        game.audioCues.music  = MusicId::TYPE_A;
        game.audioCues.square = SquareSfxId::ROTATE_PIECE;
        game.audioCues.noise  = NoiseSfxId::LOCK_PIECE;
        game.audioCues.wave   = WaveSfxId::GAME_OVER;
        game.audioCues.pause  = AudioPauseCommand::PAUSE;

        const SoundGestures gestures = gesturesFor(game);
        EXPECT_EQ(gestures.music, std::optional<std::uint8_t>{0x05});
        EXPECT_EQ(gestures.mailboxes.squareSfx, std::optional<std::uint8_t>{0x03});
        EXPECT_EQ(gestures.mailboxes.noiseSfx, std::optional<std::uint8_t>{0x02});
        EXPECT_EQ(gestures.mailboxes.waveSfx, std::optional<std::uint8_t>{0x02});
        EXPECT_EQ(gestures.mailboxes.pause, std::optional<std::uint8_t>{1});
    }

    // The stop id is an ordinary song number as far as the mailbox is concerned; the driver
    // recognises it and silences itself.
    {
        GameContext game;
        game.audioCues.music = MusicId::STOP;
        EXPECT_EQ(gesturesFor(game).music, std::optional<std::uint8_t>{0xFF});
    }

    // A cue written and then withdrawn within the same frame never reaches the driver - the rotation
    // that turns out to collide does exactly this.
    {
        GameContext game;
        game.audioCues.square = SquareSfxId::ROTATE_PIECE;
        game.audioCues.square = SquareSfxId::NONE;
        EXPECT_FALSE(gesturesFor(game).mailboxes.squareSfx.has_value());
    }
}

// 5. The demo gate is published on every frame, whatever is running and whether or not it changed -
//    the byte is applied once and never re-asserted, so a frame that left it out would let an
//    attract-mode demo's recorded presses make sound.
TEST(Sound, DemoGateIsPublishedEveryFrame) {
    for (const ActiveDemo demo : {ActiveDemo::NONE, ActiveDemo::TYPE_B, ActiveDemo::TYPE_A}) {
        GameContext game;
        game.demo.activeDemo = demo;

        for (int frame = 0; frame < 3; ++frame) {
            const SoundGestures gestures = gesturesFor(game);
            ASSERT_TRUE(gestures.demoGate.demoNumber.has_value())
                << "frame " << frame << " did not publish the demo gate";
            EXPECT_EQ(*gestures.demoGate.demoNumber, static_cast<std::uint8_t>(demo));
        }
    }

    // The gate rides alone: publishing it never carries a cue with it.
    GameContext running;
    running.demo.activeDemo   = ActiveDemo::TYPE_A;
    running.audioCues.square  = SquareSfxId::TINK;
    const SoundGestures split = gesturesFor(running);
    EXPECT_FALSE(split.demoGate.squareSfx.has_value());
    EXPECT_TRUE(split.mailboxes.squareSfx.has_value());
}

// 6. The top-out frame - the one that asks for an initialisation and a sound in the same breath.
//    The initialisation is ordered ahead of the frame's mailbox batch, which is where the original
//    ran it; the other way round it would clear the channels the game-over sound had just claimed.
TEST(Sound, InitialisationIsOrderedAheadOfTheFramesSounds) {
    GameContext game;
    game.audioCues.resetRequested = true;
    game.audioCues.wave           = WaveSfxId::GAME_OVER;

    const SoundGestures gestures = gesturesFor(game);
    EXPECT_TRUE(gestures.initDriver);
    EXPECT_EQ(gestures.mailboxes.waveSfx, std::optional<std::uint8_t>{0x02});

    // The ordering claim in the struct's own terms: the sound the frame asks for is carried in the
    // batch performed AFTER the initialisation, never in the gate published before it.
    EXPECT_FALSE(gestures.demoGate.waveSfx.has_value());

    // A frame with no initialisation request does not perform one.
    GameContext quiet;
    quiet.audioCues.wave = WaveSfxId::GAME_OVER;
    EXPECT_FALSE(gesturesFor(quiet).initDriver);
}

// 6b. A machine reset asks for the driver's whole startup, which is a different gesture from the
//     initialisation the game asks for at a game over. The difference is the work-RAM clear: the
//     initialisation entry does not touch the driver's pause-tune timer (audio.asm:804-830), and while
//     that byte is set the driver plays the pause tune and never reaches its sound routines at all
//     (:69-71) - and it latches, so it stays set until something clears the memory (:145-148). A reset
//     that only initialised would silence every effect and the music for the rest of the session.
TEST(Sound, AMachineResetAsksForTheWholeStartupNotJustAnInitialisation) {
    GameContext resetting;
    resetting.audioCues.driverRestartRequested = true;

    const SoundGestures reset = gesturesFor(resetting);
    EXPECT_TRUE(reset.restartDriver);
    EXPECT_FALSE(reset.initDriver);

    // The game's own InitAudio sites are the other gesture, and they stay that way: a game over must
    // not wipe the driver's memory, because the original's game over does not.
    GameContext gameOver;
    gameOver.audioCues.resetRequested = true;

    const SoundGestures init = gesturesFor(gameOver);
    EXPECT_TRUE(init.initDriver);
    EXPECT_FALSE(init.restartDriver);

    // Neither is asked for on an ordinary frame.
    const SoundGestures idle = gesturesFor(GameContext{});
    EXPECT_FALSE(idle.restartDriver);
    EXPECT_FALSE(idle.initDriver);
}

// 7. The startup routine does the three things the driver depends on and the cartridge's own image
//    does not carry: switch the sound hardware on, clear the driver's work RAM, and only then call
//    the driver's initialisation. Each omission is silent and each one silences the driver in its
//    own way, so the routine is pinned instruction by instruction, through the assembler.
TEST(Sound, StartupRoutineSwitchesOnClearsAndInitialises) {
    const fs::path source = fs::path{KIRPICH_PROJECT_ROOT} / "src/vm/audio_boot.asm";
    ASSERT_TRUE(fs::exists(source)) << "the startup routine is missing: " << source.string();

    std::ifstream     in{source};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    ASSERT_FALSE(text.empty());

    retropp::Vm                     assembler{retropp::VMPlatform::GameBoy};
    const std::vector<std::uint8_t> code = assembler.assemble(text);
    ASSERT_FALSE(code.empty()) << "the startup routine did not assemble";

    const auto contains = [&code](const std::vector<std::uint8_t>& sequence) {
        return std::search(code.begin(), code.end(), sequence.begin(), sequence.end()) != code.end();
    };

    // The three sound-hardware writes, in the game's own order and with its own values
    // (tetris.asm:301-306): enable, then routing, then volume.
    EXPECT_TRUE(contains({0x3E, 0x80, 0xE0, 0x26,     // ld a,$80 : ldh [$FF26],a  - sound on
                          0x3E, 0xFF, 0xE0, 0x25,     // ld a,$FF : ldh [$FF25],a  - route all
                          0x3E, 0x77, 0xE0, 0x24}))   // ld a,$77 : ldh [$FF24],a  - volume
        << "the startup routine does not switch the sound hardware on the way the game does";

    // The work-RAM clear: a descending store walking down from $DFFF. Without it the driver starts
    // on whatever the machine's memory happens to hold.
    EXPECT_TRUE(contains({0x21, 0xFF, 0xDF})) << "the clear does not start at $DFFF";
    EXPECT_TRUE(contains({0x32})) << "no descending store - the work RAM is never cleared";

    // And it ends by calling the driver's own initialisation, which must come last: with the
    // hardware off, the register writes that initialisation makes are discarded.
    EXPECT_TRUE(contains({0xCD, 0xF3, 0x7F}))
        << "the startup routine does not call the driver's initialisation at $7FF3";

    const std::vector<std::uint8_t> initCall{0xCD, 0xF3, 0x7F};
    const std::vector<std::uint8_t> enableWrite{0x3E, 0x80, 0xE0, 0x26};
    const auto callAt   = std::search(code.begin(), code.end(), initCall.begin(), initCall.end());
    const auto enableAt = std::search(code.begin(), code.end(), enableWrite.begin(), enableWrite.end());
    EXPECT_LT(enableAt - code.begin(), callAt - code.begin())
        << "the driver is initialised before the sound hardware is switched on";
}

// 8. Placement, against the real cartridge: putting the extracted image at the base the registration
//    names lands the driver's two entry points exactly where it says they are, and both are jumps.
//    This is the one claim the description alone cannot make good on.
TEST(Sound, PlacingTheImageLandsTheEntriesWhereDeclared) {
    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const std::vector<std::uint8_t> rom = readBytes(romPath);
    ASSERT_EQ(rom.size(), kRomSize);

    const std::span<const std::uint8_t> image = soundDriverImage(rom);
    ASSERT_EQ(image.size(), kSoundDriverImageSize);

    const retropp::DriverDefinition& def  = definition();
    const std::uint32_t              base = def.images[0].base;

    // Both of the driver's own entry points -- the per-frame one the binding names, and the
    // initialisation the startup routine calls -- land on a jump once the image sits at its base.
    constexpr std::uint8_t kJumpOpcode = 0xC3;  // the console's absolute jump
    for (const std::size_t entry : {static_cast<std::size_t>(def.tickEntry), kSoundDriverInitEntry}) {
        ASSERT_GE(entry, base) << "entry " << entry << " sits below the placed image";
        const std::size_t offset = entry - base;
        ASSERT_LT(offset, image.size()) << "entry " << entry << " sits past the placed image";
        EXPECT_EQ(image[offset], kJumpOpcode)
            << "entry " << entry << " is not a jump once the image is placed";
    }

    // The two are distinct routines, not one trampoline named twice.
    EXPECT_NE(static_cast<std::size_t>(def.tickEntry), kSoundDriverInitEntry);
}

}  // namespace
