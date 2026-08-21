#pragma once

// The audio cue mailbox: the game's half of the game-to-sound-driver interface, carried on
// GameContext. Gameplay does not call the sound driver directly — it writes an identifier into one
// of these fields (a song to start, a square/noise/wave sound effect to play) and, once a frame, the
// audio tick drains the mailbox into the driver and clears it. The four cue fields mirror the four
// bytes the original leaves for its driver (wNewMusicID / wNewSquareSFXID / wNewNoiseSFXID /
// wNewWaveSFXID); the driver's own working RAM is not modelled here (see docs/contracts/audio-state.md).
//
// Pausing works the same way through a fifth field: the pause command. It is a command rather than a
// cue — it tells the driver to suspend or resume the current song rather than naming something to play —
// but its lifecycle is identical, so it lives here with the cues.
//
// Overwrite is the point, not a hazard. A handler may write a cue during its frame and then overwrite
// it to NONE before the audio tick reads it — the piece rotation does exactly this, cueing the rotate
// sound and cancelling it when the rotation turns out to collide. So the mailbox holds the LAST cue
// written each frame, and a cue cancelled within the frame never reaches the driver. resetRequested
// stands apart: it asks the audio tick to re-initialise the driver before it reads the cues (the
// top-out path re-inits audio, then cues the game-over sound).
//
// The tick that drains this mailbox is SoundSystem::tick (systems/sound.h); it runs once a frame,
// hands the driver what the frame asked for, and returns every field here to its boot value.

#include <cstdint>

#include "data/music.h"
#include "data/sfx.h"

namespace kirpich::systems {

// Whether the frame asks the driver to suspend or resume the current song. Pausing the game sends
// PAUSE, unpausing sends UNPAUSE; the driver acts on the command and clears it, so NONE is both the
// boot value and what the driver leaves behind.
enum class AudioPauseCommand : std::uint8_t {
    NONE    = 0,
    PAUSE   = 1,
    UNPAUSE = 2,
};

struct AudioCues {
    MusicId     music  = MusicId::NONE;      // song to start (wNewMusicID)
    SquareSfxId square = SquareSfxId::NONE;  // square-channel sound effect (wNewSquareSFXID)
    NoiseSfxId  noise  = NoiseSfxId::NONE;   // noise-channel sound effect (wNewNoiseSFXID)
    WaveSfxId   wave   = WaveSfxId::NONE;    // wave-channel sound effect (wNewWaveSFXID)
    bool resetRequested = false;             // re-initialise the driver before reading the cues

    // Put the driver back the way it started — a different and larger thing than resetRequested above.
    //
    // resetRequested is the game's own `call InitAudio`: the game-over path, the Type B scoreboard, the
    // rocket's exit. That entry clears the channels and their locks and nothing else. A machine reset
    // is more — the original wipes the driver's whole work RAM and then initialises it
    // (tetris.asm:311-317 then :367) — and the difference is audible rather than academic: the
    // initialisation alone leaves the driver's pause-tune timer latched, and while that byte is set the
    // driver plays the pause tune and never reaches its sound routines, so every effect and the music
    // stop for good (audio.asm:69-71, and :145-148 for why it latches).
    //
    // The audio tick performs this as the driver's own startup routine run again, which is exactly the
    // three things the game's startup does — see src/vm/audio_boot.asm.
    bool driverRestartRequested = false;

    // Suspend or resume the current song (wPauseUnpauseSound).
    AudioPauseCommand pause = AudioPauseCommand::NONE;

    // Return every cue to its boot (no-cue) value — what the audio tick does after draining.
    void reset() { *this = AudioCues{}; }

    friend bool operator==(const AudioCues&, const AudioCues&) = default;
};

}  // namespace kirpich::systems
