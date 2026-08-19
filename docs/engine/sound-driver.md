# Sound driver

How Kirpich makes sound: it runs the game's original sound driver as a resident machine on the
engine's audio system and hands it the frame's requests once per frame.

Nothing here synthesizes music or effects. The driver does that, and it is the cartridge's own code
running unmodified. This page covers the surface around it — what is declared, what runs each
frame, what you can read back, and what to edit to change any of it.

The behavioral specification this implements is
[`../contracts/sound-driver.md`](../contracts/sound-driver.md); the byte-level adjudication of the
driver's RAM window is [`../contracts/audio-state.md`](../contracts/audio-state.md).

## Files

| File | Holds |
|---|---|
| `src/systems/sound.h` | `SoundDriverSlots`, `SoundGestures`, `gesturesFor`, `soundDriverId`, `SoundSystem`, `installSoundTick` |
| `src/systems/sound.cpp` | The registration, the frame decision, and the sound system |
| `src/vm/audio_boot.asm` | The startup routine — switches the sound hardware on, clears the driver's work RAM, calls the driver's own initialisation |
| `tests/test_sound.cpp` | The registration pins, the frame-decision sweeps, the startup-routine pins, and the placement check |

The driver's image is not in this repository, and it is never compiled into the binary: those bytes
are the player's own cartridge content. It is extracted on first start to
`assets/audio/default/sound_driver.bin` under the asset root — the player's per-user data directory,
see [assets.md](assets.md) — and read from there at run time, which is what its `LoadFromPath` policy
means.

The startup routine registered alongside it is the opposite case. `src/vm/audio_boot.asm` is
port-authored, so it is declared `Embed` and the build bakes it into the binary; there is no file to
find at run time. The two images in one binding having different policies is the point — each is
resolved on its own.

## The three pieces

### The registration

`soundDriverId()` returns the registered driver, registering it on the first call and returning the
same handle thereafter.

```cpp
retropp::DriverId<SoundDriverSlots> soundDriverId();
```

The registration describes the driver to the engine: the cartridge's driver image at `$6480` read
from disk, the port's startup routine placed beside it, the per-frame entry at `$7FF0`, the stack
top, no bank controller, and the six bytes the game shares with it.

It is a description, not an action — nothing runs until a `SoundSystem` hosts it.

### The startup routine, and why it exists

The driver's image is not self-sufficient. It ran inside a machine the game had already prepared,
and it needs three things the game's startup does — none of which are in the audio section:

1. **The sound hardware switched on**, routed, and at volume. Without it the chip is powered down
   and every register the driver writes is discarded: it plays to silence.
2. **The stack somewhere clear of its own memory.** The driver pushes four register pairs every
   pass. Left at the top of work RAM those pushes land on `$DFF8`/`$DFF9` — its own noise request
   mailbox — and overwrite the request before it is read.
3. **Its work RAM cleared.** A machine's memory does not start zeroed, and the driver keeps all of
   its state there: the pause countdown it reads first each pass, the four mailboxes, and one data
   pointer per music channel.

`src/vm/audio_boot.asm` does all three and then calls the driver's own initialisation. That order is
required — with the hardware still off, the register writes that initialisation makes have no effect.

The hardware writes have to be **machine code**, not slot writes: switching the chip on is an effect
of the processor's write reaching it, and setting the same bytes from outside powers nothing on.

### The frame decision

```cpp
SoundGestures gesturesFor(const GameContext& game);
```

A pure function: it reads the cue mailbox (`systems/audio_cues.h`) and the demo state and returns
what this frame asks of the driver. It touches neither the driver nor the context, so the whole
request protocol can be exercised with no sound hardware present — which is what the tests do.

```cpp
struct SoundGestures {
    SoundDriverSlots            demoGate;             // published first, every frame
    bool                        initDriver = false;   // re-initialise the driver
    std::optional<std::uint8_t> music;                // song to start
    SoundDriverSlots            mailboxes;            // the frame's effect cues and pause command
};
```

**The members are performed in the order they are declared, and that order is load-bearing.** An
initialisation performed after a sound effect has started silences that effect on the frame it
began, because initialising clears every channel and its lock. The contract works the case through.

### The sound system

```cpp
class SoundSystem {
public:
    SoundSystem();                                   // opens the default audio output
    explicit SoundSystem(retropp::AudioSink& sink);  // plays into a sink you own
    void tick(GameContext& game);
    std::optional<MusicId> currentMusic() const;
};
```

Constructing one starts the driver: its image is placed, its initialisation entry runs once, and it
begins producing sound continuously. From then on `tick` is the only thing the game does to it —
it performs the frame's gestures and clears the cue mailbox.

The engine runs the driver's per-frame entry itself, on its own thread, at the console's clock.
`tick` does not step the driver and does not pace it.

`currentMusic()` reads back the song the driver reports it is playing, or nothing if it has not
published one yet.

## Wiring it up

The frame dispatcher ([dispatcher.md](dispatcher.md)) has an audio beat that runs after the state
handler. Point it at the sound system:

```cpp
kirpich::systems::GameContext         game;
kirpich::systems::GameStateDispatcher dispatcher;
kirpich::systems::SoundSystem         sound;

kirpich::systems::installSoundTick(dispatcher, sound, game);
```

Both `sound` and `game` must outlive the dispatcher. That is the one line that makes the port
audible.

## Asking for sound

Gameplay never calls this system. It writes into the cue mailbox on `GameContext`, and the tick
drains it:

```cpp
game.audioCues.music  = MusicId::TYPE_A;              // start a song
game.audioCues.music  = MusicId::STOP;                // stop all audio
game.audioCues.square = SquareSfxId::ROTATE_PIECE;    // a square-channel effect
game.audioCues.pause  = AudioPauseCommand::PAUSE;     // suspend the current song
game.audioCues.resetRequested = true;                 // re-initialise the driver
```

A cue written and then overwritten within the same frame never reaches the driver — only the last
value written each frame is handed over. The piece rotation relies on this: it cues its sound and
withdraws it when the rotation turns out to collide.

The identifier spaces are the existing ones — `MusicId` ([music.md](music.md)) and the three effect
enums ([sfx.md](sfx.md)).

## The shared bytes

`SoundDriverSlots` names the six bytes the game and the driver share. A batch names only the fields
it changes, each is written once on the driver's next pass, and none is held or re-asserted.

| Field | Address | Direction |
|---|---|---|
| `pause` | `$DF7F` | write |
| `squareSfx` | `$DFE0` | write |
| `currentMusic` | `$DFE9` | read |
| `waveSfx` | `$DFF0` | read/write |
| `noiseSfx` | `$DFF8` | write |
| `demoNumber` | `$FFE4` | write |

`demoNumber` is the one byte outside the driver's own RAM window. The driver reads it before it
plays anything and silences the frame's requests when it is non-zero, so a running attract demo's
recorded button presses make no sound. It is sent when it changes: a write applies once, but the
byte itself persists in the driver's memory, so re-sending an unchanged value every frame would be
inert.

Writing `currentMusic` throws: it is the driver's to publish.

## What to edit

| To change | Edit |
|---|---|
| Where the driver's image sits, or which entries run | The binding in `registerSoundDriver` (`sound.cpp`) |
| What the machine looks like before the driver starts | `src/vm/audio_boot.asm`, and its pins in `test_sound.cpp` |
| Where the stack sits | `kGameStackTop` (`sound.cpp`) — keep it clear of `$DF70`–`$DFFF` or the driver overwrites its own mailboxes |
| Which bytes the game shares, or a direction | The slot list in `registerSoundDriver`, and the pins in `test_sound.cpp` |
| How a song request or the initialisation reaches the driver | The verbs in `registerSoundDriver` |
| What a frame asks for, or the order it asks in | `gesturesFor` (`sound.cpp`) and the `SoundGestures` member order |
| Where a sound is cued from | The gameplay code that writes `game.audioCues`, not this unit |

Changing an address or a direction without changing its pin in `test_sound.cpp` fails the suite —
the pins are hand-entered from the contract precisely so a silent edit cannot pass.

## Status

The chiptune path is the only audio backend; there is no audio-file replacement path and no backend
selector.

Sound-effect channels have no separate play lane — all three are request mailboxes, reached as
shared bytes — so asking the driver for one is a loud error rather than a silent nothing.

The read-back accessor `currentMusic()` has no consumer yet. Two screens read it in the original —
one to decide when to switch to the danger music, one to hold an animation until the music ends —
and those arrive with their own screens.

Anti-channel-stealing, which would route music and effects to separate sound chips instead of
letting them contend as they did on hardware, is an engine capability that is not built yet. Until
it is, channel stealing happens exactly as it did on the original, which is the intended default.
