#include "systems/boot.h"

#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/music_type.h>

#include "state/high_score_persistence.h"

namespace kirpich::systems {

namespace {

// ClearTilemap9800 (tetris.asm:6343-6344), which loads $9BFF and falls into ClearTilemap
// (:6345-6354): $400 bytes of the character map's space glyph, walked downward.
//
// The value is the space glyph, not zero - the routine writes `" "` through the character map, which
// puts $2F in every cell. The walk's direction is not reproduced: nothing writes the map between the
// first cell and the last, so the order cannot be observed and the fill says what it means.
//
// This covers the FIRST map only. The second was zeroed by the video-memory clear (:329-338) and
// nothing fills it here, which is why the two maps hold different values after a boot.
void clearFirstBackgroundMap(DisplayState& display) {
    for (auto& row : display.map) {
        row.fill(static_cast<std::uint8_t>(CharTile::SPACE));
    }
}

}  // namespace

void coldBoot(GameContext& game) {
    // The six clear loops at :265-274, :311-317, :319-327, :329-338, :340-345 and :347-352, together.
    // Each one covers a whole region of the original's memory and every piece of state this port holds
    // boots to zero, so one whole-image reset is all six - contract §3 maps each loop to the members it
    // reaches. The two that land on nothing the port models (the sprite-attribute overrun and the byte
    // below high memory) are recorded there rather than spelled here.
    game.reset();

    clearFirstBackgroundMap(game.display);

    // The sound driver's startup: the hardware writes at :301-306, the work-RAM clear at :311-317, and
    // the InitAudio call at :367. All three are the driver's own startup routine here
    // (src/vm/audio_boot.asm), which the frame's sound step asks to be run again.
    //
    // Not the plain initialisation the game asks for elsewhere. That entry leaves the driver's
    // pause-tune timer latched, and a driver with that byte set never reaches its sound routines
    // again — every effect and the music stop for good. The work-RAM clear is what the original clears
    // it with, and only the whole startup performs one.
    //
    // It arrives a frame later than the original's inline call, and contract §12 argues why nothing
    // can observe that.
    game.audioCues.driverRestartRequested = true;

    // What the boot leaves behind (:371-376). The two selections double as cursor positions and sprite
    // numbers on the screens that read them, which is why they carry these byte values.
    game.flow.gameType  = GameType::TYPE_A;      // :371-372, $37
    game.flow.musicType = MusicType::MUSIC_A;    // :373-374, $1C
    game.flow.gameState = GameState::INIT_COPYRIGHT;  // :375-376, $24
}

void softReset(GameContext& game) {
    // The one difference between the two entry points. The cold entry clears the work-RAM bank holding
    // the cartridge's two tables (:265-274) and .softReset enters below it, so the tables are the only
    // game state that survives a reset. Type C's table is the port's own and keeps the same company:
    // a reset that wiped it while sparing the other two would be arbitrary.
    //
    // Saved by member, never by structure: HighScoreState's four high-memory bytes are inside the
    // clear at :347-352, which this path DOES run, so they return to boot with everything else. See the
    // note on this function's declaration.
    auto typeA = game.highScores.typeA;
    auto typeB = game.highScores.typeB;
    auto typeC = game.highScores.typeC;

    coldBoot(game);

    game.highScores.typeA = typeA;
    game.highScores.typeB = typeB;
    game.highScores.typeC = typeC;
}

void bootGame(GameContext& game, retropp::SaveStore& saves) {
    coldBoot(game);
    loadTopScores(saves, game.highScores);
}

}  // namespace kirpich::systems
