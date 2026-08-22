#pragma once

// The sound system: the game's original sound driver, running as a resident machine, and the once-a-
// frame tick that hands it the frame's cues.
//
// Kirpich does not synthesize the game's music and effects itself. It runs the driver the cartridge
// shipped with — the same code, at the same addresses — on the audio engine's virtual sound hardware,
// and talks to it exactly the way the original did: by leaving a sound's number in a memory mailbox
// the driver reads on its next pass. Everything about how a song sounds is therefore the driver's own
// doing, not a reimplementation of it.
//
// Three pieces make that work:
//
//   * the REGISTRATION (soundDriverId) — where the driver's image sits in the machine, which entry
//     runs once per frame, which entry initialises it, and which of its bytes the game may read or
//     write;
//   * the FRAME DECISION (gesturesFor) — a pure function from the game's state to what this frame
//     asks of the driver, so the whole protocol is decidable and testable without any sound hardware;
//   * the SOUND SYSTEM (SoundSystem) — which owns the audio system and the hosted driver and performs
//     that decision.
//
// The audio engine runs the driver's per-frame entry itself, at the console's own clock, on its own
// thread. Nothing here paces the driver or steps it: the tick below only drains the cue mailbox
// (systems/audio_cues.h) that gameplay has been filling all frame. See docs/contracts/sound-driver.md
// for the reverse-derived hosting contract and docs/contracts/audio-state.md for the byte-by-byte
// adjudication of the driver's RAM window.

#include <cstdint>
#include <optional>

#include <retropp/audio.h>          // AudioSink
#include <retropp/audio_library.h>  // DriverId
#include <retropp/audio_system.h>   // AudioSystem, HostedDriver

#include "data/music.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"

namespace kirpich::systems {

// The driver's state as a plain value: the six bytes the game is allowed to touch, each engaged only
// when a frame actually names it. Five are written (the pause command, the three sound-effect
// mailboxes, and the demo gate); one is read back (the song the driver is currently playing).
//
// A batch names only what changes, and each named byte is written ONCE, on the driver's next pass —
// it is never held or re-asserted, which is exactly how the original's mailboxes behave.
struct SoundDriverSlots {
    // Shared with the driver — the game's half of the sound interface.
    std::optional<std::uint8_t> pause;         // pause / resume command
    std::optional<std::uint8_t> squareSfx;     // square-channel effect to start
    std::optional<std::uint8_t> currentMusic;  // read-back: the song now playing
    std::optional<std::uint8_t> waveSfx;       // wave-channel effect to start (also read by the game)
    std::optional<std::uint8_t> noiseSfx;      // noise-channel effect to start
    std::optional<std::uint8_t> demoNumber;    // which attract-mode demo is running, if any

    friend bool operator==(const SoundDriverSlots&, const SoundDriverSlots&) = default;
};

// What one frame asks of the driver. The members are performed in the order they are declared, and
// that order is the original's: the demo gate is published before anything else, because the driver
// reads it before it plays anything; a restart next, since a machine reset precedes everything the
// frame after it does; then the initialisation, because the game code that asks for one runs it
// before the frame's sounds are requested; then the song; then the frame's mailbox batch. Getting
// this order wrong is audible — an initialisation performed after a sound effect has started silences
// that effect on the frame it began.
//
// A restart and an initialisation are not alternatives and a frame may ask for both, though nothing
// in the game does today: a restart runs the driver's whole startup, an initialisation only its
// initialisation entry. See AudioCues.
struct SoundGestures {
    SoundDriverSlots demoGate;              // published first, every frame
    bool             restartDriver = false; // run the driver's startup again — a machine reset
    bool             initDriver = false;    // re-initialise the driver (clears channels and locks)
    std::optional<std::uint8_t> music;      // song to start; the stop id is a legal value here
    SoundDriverSlots mailboxes;             // the frame's effect cues and pause command

    friend bool operator==(const SoundGestures&, const SoundGestures&) = default;
};

// What `game` asks of the driver this frame, derived from the cue mailbox and the demo state. Pure:
// it reads the context and returns a value, touching neither the driver nor the context — so the
// whole protocol can be exercised without sound hardware. SoundSystem::tick performs the result and
// then clears the cues.
// `alreadyPublished` is the demo-gate value the driver was last given, or nothing if it has never
// been given one. The gate is re-sent only when it differs: the driver keeps the byte, so re-sending
// an unchanged value each frame adds a write per frame for no effect.
[[nodiscard]] SoundGestures gesturesFor(const GameContext&                 game,
                                        std::optional<std::uint8_t> alreadyPublished = {});

// The registered sound driver, registered on first call and returned unchanged thereafter. The image
// is the file the ROM extractor writes (assets/audio/default/sound_driver.bin); it is read from disk
// rather than compiled in, because those bytes come from the player's own cartridge.
[[nodiscard]] retropp::DriverId<SoundDriverSlots> soundDriverId();


// The running sound: an audio system with the game's driver resident on it. Constructing one starts
// the driver — its initialisation entry runs once — after which it produces sound continuously and
// tick() is the only thing the game does to it.
class SoundSystem {
public:
    // Play through the default audio output. Requires an initialised platform (the audio device is
    // opened here), so this is the constructor a running game uses.
    SoundSystem();

    // Play into `sink` instead of opening an audio device. `sink` must outlive this system.
    explicit SoundSystem(retropp::AudioSink& sink);

    // Hand this frame's gestures to the driver and clear the cue mailbox. Called once per frame,
    // after the state handler has run and before the frame ends.
    void tick(GameContext& game);

    // The song the driver reports it is playing, or nothing — because the slot has not been
    // published yet, or because the driver's read-back byte is 0, which is how it says no song is
    // playing. The game reads this to hold the Type B dance until its jingle ends; treating that 0
    // as an engaged value is what once held the dance forever.
    [[nodiscard]] std::optional<MusicId> currentMusic() const;

    // Everything the driver publishes back, as one value. Readable bytes come back engaged with
    // their current contents; write-only ones come back disengaged.
    [[nodiscard]] SoundDriverSlots published() const;

    // The read-back byte's meaning, as the pure relation currentMusic() applies: disengaged in, or
    // 0 in, means no song; anything else is that song's id.
    [[nodiscard]] static std::optional<MusicId> musicFromReadback(std::optional<std::uint8_t> byte);

private:
    // The demo-gate value the driver has been given, so an unchanged one is not re-sent.
    std::optional<std::uint8_t> publishedDemoGate_;

    retropp::AudioSystem::GB                audio_;
    retropp::HostedDriver<SoundDriverSlots> driver_;
};

// Point the frame dispatcher's audio beat at `sound`, running against `game`. Both must outlive the
// dispatcher. This is the one line that makes the port audible.
void installSoundTick(GameStateDispatcher& dispatcher, SoundSystem& sound, GameContext& game);

}  // namespace kirpich::systems
