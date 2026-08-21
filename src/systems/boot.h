#pragma once

// The boot path: what the machine does before the first screen runs, and what it does again when the
// player asks for a reset.
//
// The original has one routine with two entry points (tetris.asm:264-384). Entered at the top it is a
// cold boot; entered four instructions in, at .softReset, it is the reset the Start+Select+B+A chord
// jumps to. They differ in exactly one thing: the cold entry clears the work-RAM bank holding the
// top-score tables and the reset entry does not, which is the whole reason a player's scores survive a
// reset. Everything else - the other five clear loops, the sound startup, the tile-map fill, and the
// three values the following screens read - is shared, so coldBoot is the sequence and softReset is
// coldBoot with the two tables carried across it.
//
// Most of that routine has no counterpart here. Roughly a third of it writes the original's display,
// interrupt, stack and timer registers, and this port draws through a display the engine owns and runs
// its frame from the engine's run loop, so there is nothing for those writes to reach. The full
// line-by-line accounting - every line of :264-384 and what became of it - is in
// docs/contracts/boot.md §4, which is the place to look before concluding something was dropped.
//
// bootGame is the one function here with nothing behind it in the original, and the comment on it
// explains why it has to exist.

#include <retropp/save_store.h>

#include "systems/game_context.h"

namespace kirpich::systems {

// A cold boot - the original's Init entered at its top (tetris.asm:264).
//
// Returns the whole machine to its power-on state, including the top-score tables, then fills the
// first background map with the space glyph, leaves the second map zeroed (the original's tile-map
// clear covers the first map only, and that asymmetry is real - contract §5), asks the sound driver to
// start, and writes the three values the following screens read: Type A, music A, and the copyright
// screen as the first state to dispatch (:371-376).
void coldBoot(GameContext& game);

// A soft reset - the original's Init entered at .softReset (tetris.asm:276), which the four-button
// chord jumps to from both of its detection sites.
//
// Identical to coldBoot except that the two top-score tables are carried across it. Note that
// HighScoreState sits on both sides of that line: its tables live in the work-RAM bank the reset
// skips, while its four high-memory bytes are inside a clear the reset does run, so those four return
// to their boot values with everything else. Preserving the whole structure would be as wrong as
// preserving none of it.
void softReset(GameContext& game);

// Start a session: a cold boot, then the player's saved top scores read back over the tables it just
// cleared.
//
// The order is the point. The original has nowhere to keep a score between sessions, so its cold boot
// simply zeroes the tables; this port writes them to the player's save file, and a launch has to clear
// before it loads. Reversed, every launch would wipe the scores it had just read. That is a property
// worth stating in one place rather than leaving to whoever writes the call, which is the whole reason
// this function exists rather than two statements at the call site.
//
// A missing save document is an ordinary first run and leaves the boot zeros in place. A soft reset
// deliberately does not come through here: the original keeps its tables in memory across a reset, and
// so does this, so re-reading the file would be a difference rather than a fidelity gain.
void bootGame(GameContext& game, retropp::SaveStore& saves);

}  // namespace kirpich::systems
