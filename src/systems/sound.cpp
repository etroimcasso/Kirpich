#include "systems/sound.h"

#include <cstdint>

#include <retropp/asset_policy.h>
#include <retropp/driver_binding.h>
#include <retropp/gb.h>
#include <retropp/isa.h>
#include <retropp/location.h>

#include "assets/extract.h"
#include "data/sfx.h"
#include "systems/audio_cues.h"

namespace kirpich::systems {

namespace {

// The bytes the game and the driver share. Five are the original's own mailboxes and command byte,
// in the driver's RAM window; the sixth lives with the game's own high-memory variables and is the
// one byte of the game's state the driver reads (see the demo gate below). Every one of them is
// pinned against the audio-state contract in tests/test_sound.cpp.
constexpr std::uint32_t kPauseCommandAddress  = 0xDF7F;  // wPauseUnpauseSound
constexpr std::uint32_t kSquareSfxAddress     = 0xDFE0;  // wNewSquareSFXID
constexpr std::uint32_t kMusicAddress         = 0xDFE8;  // wNewMusicID
constexpr std::uint32_t kCurrentMusicAddress  = 0xDFE9;  // wCurrentMusicID
constexpr std::uint32_t kWaveSfxAddress       = 0xDFF0;  // wNewWaveSFXID
constexpr std::uint32_t kNoiseSfxAddress      = 0xDFF8;  // wNewNoiseSFXID
constexpr std::uint32_t kDemoNumberAddress    = 0xFFE4;  // hDemoNumber

// Where the sound-hardware startup routine is placed. Anything outside the driver's own span is
// free in the machine the driver runs in, since only what the binding names exists there.
constexpr std::uint32_t kSoundHardwareBootBase = 0x6000;

// Where the game puts its stack (tetris.asm:309). The driver runs on this stack, and it must sit
// clear of the driver's own working memory: its pushes grow downward, and from the top of work RAM
// they would overwrite the request mailboxes at the top of its window.
constexpr std::uint32_t kGameStackTop = 0xCFFF;

// Register the driver: its image and where that image sits, the entry the audio engine runs every
// frame, the entry that initialises it, how the play and stop verbs reach it, and the six bytes the
// game may touch.
//
// The image ships beside the binary rather than inside it. Its bytes are the player's own cartridge
// content, extracted on first start, so they are never compiled into Kirpich.
retropp::DriverId<SoundDriverSlots> registerSoundDriver() {
    retropp::HostedDriverBinding binding{
        .images = {retropp::DriverImagePath{
                       .base   = static_cast<std::uint32_t>(assets::kSoundDriverImageBase),
                       .path   = "assets/audio/default/sound_driver.bin",
                       .policy = retropp::AssetPolicy::LoadFromPath,
                   },
                   // The sound-hardware startup the driver's image does not carry. Port-authored, so
                   // it is baked into the binary rather than read from disk.
                   retropp::DriverImagePath{
                       .base   = kSoundHardwareBootBase,
                       .path   = "src/vm/audio_boot.asm",
                       .policy = retropp::AssetPolicy::Embed,
                   }},
        .mapper    = {},  // the whole cartridge is 32 KiB and unbanked
        .tickEntry = static_cast<std::uint32_t>(assets::kSoundDriverTickEntry),
        // The game's own stack top (tetris.asm:309, `ld sp, $CFFF`). It has to be stated: the
        // driver's per-pass pushes grow down from here, and a stack left at the top of work RAM
        // would land them on the driver's own mailboxes.
        .stackTop = kGameStackTop,
        // Switch the sound hardware on, once, as the driver starts. The driver's own initialisation
        // follows at the first pass (SoundSystem::start), which is the order the game's startup uses
        // and the order that matters: with the hardware off, every register the initialisation
        // writes is ignored.
        .init = retropp::Instruction::call(kSoundHardwareBootBase, retropp::gb::A),
        .isa  = retropp::Isa::Sm83,
    };

    // Starting a song leaves its number in the music mailbox; the driver picks it up on its next
    // pass. Stopping calls the driver's initialisation entry directly — the original's stop routine
    // is nothing but a call to that entry, and performing it as a call runs it where the original
    // ran it: before the frame's sounds are handed over, not part-way through the driver's own pass.
    // The distinction is audible, because that entry clears the channels and their locks.
    //
    // No separate effect lane is declared: the three sound-effect channels are mailboxes, so they
    // are reached as slots (below).
    const retropp::DriverVerbs verbs{
        .play = {.music = retropp::Instruction::write(retropp::Location::memory(kMusicAddress), 1)},
        .stop = retropp::Instruction::call(
            static_cast<std::uint32_t>(assets::kSoundDriverInitEntry), retropp::gb::A),
    };

    return retropp::AudioLibrary::instance().registerDriver(
        binding, verbs,
        retropp::slots(
            retropp::slot(&SoundDriverSlots::pause, kPauseCommandAddress,
                          retropp::SlotDirection::Write),
            retropp::slot(&SoundDriverSlots::squareSfx, kSquareSfxAddress,
                          retropp::SlotDirection::Write),
            retropp::slot(&SoundDriverSlots::currentMusic, kCurrentMusicAddress,
                          retropp::SlotDirection::Read),
            // The wave mailbox is the one the game reads back as well as writes: one screen checks
            // whether the game-over sound is already queued before starting anything else.
            retropp::slot(&SoundDriverSlots::waveSfx, kWaveSfxAddress,
                          retropp::SlotDirection::ReadWrite),
            retropp::slot(&SoundDriverSlots::noiseSfx, kNoiseSfxAddress,
                          retropp::SlotDirection::Write),
            retropp::slot(&SoundDriverSlots::demoNumber, kDemoNumberAddress,
                          retropp::SlotDirection::Write)));
}

}  // namespace

retropp::DriverId<SoundDriverSlots> soundDriverId() {
    static const retropp::DriverId<SoundDriverSlots> id = registerSoundDriver();
    return id;
}

SoundGestures gesturesFor(const GameContext& game, std::optional<std::uint8_t> alreadyPublished) {
    const AudioCues& cues = game.audioCues;
    SoundGestures    gestures;

    // The demo gate: while an attract-mode demo is running the driver silences the frame's cues
    // before playing, so a recorded button press makes no sound. The driver keeps the byte once it
    // has it, so it is sent when it changes rather than every frame — a write per frame would be
    // inert and would fill the queue the gestures travel on. Its values are the game's own,
    // unaltered.
    const auto gate = static_cast<std::uint8_t>(game.demo.activeDemo);
    if (alreadyPublished != std::optional<std::uint8_t>{gate}) {
        gestures.demoGate.demoNumber = gate;
    }

    gestures.restartDriver = cues.driverRestartRequested;
    gestures.initDriver    = cues.resetRequested;

    if (cues.music != MusicId::NONE) {
        gestures.music = static_cast<std::uint8_t>(cues.music);
    }
    if (cues.square != SquareSfxId::NONE) {
        gestures.mailboxes.squareSfx = static_cast<std::uint8_t>(cues.square);
    }
    if (cues.wave != WaveSfxId::NONE) {
        gestures.mailboxes.waveSfx = static_cast<std::uint8_t>(cues.wave);
    }
    if (cues.noise != NoiseSfxId::NONE) {
        gestures.mailboxes.noiseSfx = static_cast<std::uint8_t>(cues.noise);
    }
    if (cues.pause != AudioPauseCommand::NONE) {
        gestures.mailboxes.pause = static_cast<std::uint8_t>(cues.pause);
    }

    return gestures;
}

// Both constructors are complete once the driver is placed: the startup routine the binding declares
// switches the sound hardware on and runs the driver's initialisation while the placement happens, so
// there is nothing left to do here and nothing that has to be timed against the driver's first pass.
SoundSystem::SoundSystem()
    : audio_{retropp::AudioKind::Chiptune}, driver_{audio_.host(soundDriverId())} {}

SoundSystem::SoundSystem(retropp::AudioSink& sink)
    : audio_{retropp::AudioKind::Chiptune, sink}, driver_{audio_.host(soundDriverId())} {}

void SoundSystem::tick(GameContext& game) {
    const SoundGestures gestures = gesturesFor(game, publishedDemoGate_);
    if (gestures.demoGate.demoNumber.has_value()) {
        publishedDemoGate_ = gestures.demoGate.demoNumber;
    }

    // Performed in the order the members are declared — see the note on SoundGestures.
    driver_.slots(gestures.demoGate);
    if (gestures.restartDriver) {
        // The driver's whole startup again: the sound hardware switched on, its work RAM cleared, and
        // its initialisation entry called (src/vm/audio_boot.asm). That last clear is the half the
        // initialisation entry does not do and a machine reset needs.
        driver_.restart();
    }
    if (gestures.initDriver) {
        driver_.stop();
    }
    if (gestures.music.has_value()) {
        driver_.play(*gestures.music);
    }
    driver_.slots(gestures.mailboxes);

    game.audioCues.reset();
}

SoundDriverSlots SoundSystem::published() const { return driver_.slots(); }

std::optional<MusicId> SoundSystem::musicFromReadback(std::optional<std::uint8_t> byte) {
    // Empty two ways: the slot not yet published, and the driver reporting byte 0 — which is what
    // its read-back holds when no song is playing. Returning NONE engaged here made "is the jingle
    // still playing" answer yes forever, and the Type B dance never left for the tally.
    if (!byte.has_value() || *byte == static_cast<std::uint8_t>(MusicId::NONE)) {
        return std::nullopt;
    }
    return static_cast<MusicId>(*byte);
}

std::optional<MusicId> SoundSystem::currentMusic() const {
    return musicFromReadback(published().currentMusic);
}

void installSoundTick(GameStateDispatcher& dispatcher, SoundSystem& sound, GameContext& game) {
    dispatcher.audioTick = [&sound, &game] { sound.tick(game); };
}

}  // namespace kirpich::systems
