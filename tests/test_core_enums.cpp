// Core enums. Three source routes, three kinds of check:
//   Class A (parser-emitted headers from constants.asm EQU): byte-value assertions.
//   Class B (hand-authored names, parser-emitted value fixture): drift-check the header values
//           against the fixture scanned from source - the transcription guard.
//   Class C (hand-authored, reverse-derived): byte-value assertions against the contract.

#include <array>
#include <cstdint>
#include <set>

#include <gtest/gtest.h>

#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/music_type.h>
#include <kirpich/piece.h>
#include <kirpich/serial_clock_mode.h>
#include <kirpich/serial_role.h>
#include <kirpich/serial_state.h>

#include "fixtures/core_enums_expected.h"
#include "systems/game_state_dispatcher.h"  // kGameStateCount

namespace {

using kirpich::GameState;
using kirpich::GameType;
using kirpich::MusicType;
using kirpich::Piece;
using kirpich::SerialClockMode;
using kirpich::SerialRole;
using kirpich::SerialState;

template <typename E>
constexpr std::uint8_t raw(E value) {
    return static_cast<std::uint8_t>(value);
}

// --- Class A: parser-emitted from constants.asm -----------------------------------------------

static_assert(raw(SerialRole::MASTER) == 0x29);
static_assert(raw(SerialRole::SLAVE) == 0x55);
static_assert(raw(SerialClockMode::EXTERNAL) == 0x80);
static_assert(raw(SerialClockMode::INTERNAL) == 0x81);

TEST(CoreEnums, SerialRoleValuesMatchRom) {
    EXPECT_EQ(raw(SerialRole::MASTER), 0x29);
    EXPECT_EQ(raw(SerialRole::SLAVE), 0x55);
}

TEST(CoreEnums, SerialClockModeValuesMatchRom) {
    EXPECT_EQ(raw(SerialClockMode::EXTERNAL), 0x80);
    EXPECT_EQ(raw(SerialClockMode::INTERNAL), 0x81);
}

// --- Class C: reverse-derived, contract-pinned ------------------------------------------------

static_assert(raw(GameType::TYPE_A) == 0x37);
static_assert(raw(GameType::TYPE_B) == 0x77);
static_assert(raw(GameType::TYPE_C) == 0xC7);
static_assert(raw(MusicType::MUSIC_A) == 0x1C);
static_assert(raw(MusicType::OFF) == 0x1F);

TEST(CoreEnums, GameTypeValuesMatchRom) {
    EXPECT_EQ(raw(GameType::TYPE_A), 0x37);
    EXPECT_EQ(raw(GameType::TYPE_B), 0x77);

    // Type C is the port's own mode, so its byte answers to nothing in the cartridge - only to being
    // distinct from the two that do.
    EXPECT_EQ(raw(GameType::TYPE_C), 0xC7);
    EXPECT_NE(raw(GameType::TYPE_C), raw(GameType::TYPE_A));
    EXPECT_NE(raw(GameType::TYPE_C), raw(GameType::TYPE_B));
}

TEST(CoreEnums, MusicTypeValuesMatchRom) {
    EXPECT_EQ(raw(MusicType::MUSIC_A), 0x1C);
    EXPECT_EQ(raw(MusicType::MUSIC_B), 0x1D);
    EXPECT_EQ(raw(MusicType::MUSIC_C), 0x1E);
    EXPECT_EQ(raw(MusicType::OFF), 0x1F);
}

// Piece: raw byte kind*4 + rotation, valid 0..27, no sentinel.
static_assert(sizeof(Piece) == 1);

TEST(CoreEnums, PieceDecodesKindAndRotation) {
    for (int k = 0; k <= 6; ++k) {
        for (int r = 0; r <= 3; ++r) {
            const Piece p{static_cast<std::uint8_t>(k * 4 + r)};
            EXPECT_EQ(raw(p.kind()), k);
            EXPECT_EQ(p.rotation(), r);
        }
    }
}

TEST(CoreEnums, PieceRoundTrips) {
    for (int k = 0; k <= 6; ++k) {
        for (int r = 0; r <= 3; ++r) {
            const Piece p = Piece::of(static_cast<std::uint8_t>(k), static_cast<std::uint8_t>(r));
            EXPECT_EQ(p.raw, k * 4 + r);
            EXPECT_EQ(raw(p.kind()), k);
            EXPECT_EQ(p.rotation(), r);
        }
    }
}

// --- Class B: hand-authored names drift-checked against the parser fixture ---------------------

// Every GameState enumerator, listed once. Referencing each by name means a wrong value in the
// header surfaces here as a set mismatch against the fixture below.
constexpr std::array<GameState, 54> kAllGameStates{{
    GameState::NORMAL_GAMEPLAY,       GameState::INIT_GAME_OVER,
    GameState::BURAN_LIFTOFF,         GameState::BURAN_RISING,
    GameState::GAME_OVER_SCREEN,      GameState::TYPE_B_VICTORY_JINGLE,
    GameState::INIT_TITLE_SCREEN,     GameState::TITLE_SCREEN,
    GameState::INIT_TYPE_SELECTION,   GameState::STATE_09_UNUSED,
    GameState::INIT_GAME,             GameState::INIT_TYPE_B_SCOREBOARD,
    GameState::STATE_0C_UNKNOWN,      GameState::GAME_OVER_CURTAIN,
    GameState::SELECT_GAME_TYPE,      GameState::SELECT_MUSIC_TYPE,
    GameState::INIT_TYPE_A_DIFFICULTY, GameState::TYPE_A_LEVEL_SELECTION,
    GameState::INIT_TYPE_B_DIFFICULTY, GameState::TYPE_B_LEVEL_SELECTION,
    GameState::TYPE_B_HEIGHT_SELECTION, GameState::ENTER_TOP_SCORE,
    GameState::INIT_2P_DIFFICULTY,    GameState::SELECT_2P_HEIGHT,
    GameState::INIT_2P_GAME,          GameState::INIT_2P_GAME_2,
    GameState::TWO_PLAYER_GAME,       GameState::TWO_PLAYER_END_JINGLE,
    GameState::PREPARE_GARBAGE,       GameState::INIT_2P_VICTORY,
    GameState::INIT_2P_DEFEAT,        GameState::INIT_2P_GAME_3,
    GameState::TWO_PLAYER_VICTORY,    GameState::TWO_PLAYER_DEFEAT,
    GameState::INIT_TYPE_B_BONUS,     GameState::DANCERS,
    GameState::INIT_COPYRIGHT,        GameState::COPYRIGHT_SCREEN,
    GameState::INIT_BURAN,            GameState::PREPARE_BURAN_LAUNCH,
    GameState::BURAN_IGNITION,        GameState::BURAN_IGNITION_2,
    GameState::INIT_2P_MUSIC_SELECTION, GameState::SELECT_2P_MUSIC,
    GameState::PRINT_CONGRATULATIONS, GameState::CONGRATULATIONS,
    GameState::INIT_ROCKET_LAUNCH,    GameState::ROCKET,
    GameState::ROCKET_IGNITION,       GameState::ROCKET_LIFTOFF,
    GameState::ROCKET_MAIN_ENGINE_FIRE, GameState::END_OF_BONUS_SCENE,
    GameState::GAME_OVER_TO_BONUS,    GameState::COPYRIGHT_SCREEN_SKIPPABLE,
}};

TEST(CoreEnums, GameStateValuesMatchFixture) {
    ASSERT_EQ(kirpich::fixtures::kGameStateExpected.size(), 54u);
    ASSERT_EQ(kAllGameStates.size(), 54u);

    std::set<std::uint8_t> from_enum;
    for (const GameState s : kAllGameStates) {
        from_enum.insert(raw(s));
    }
    std::set<std::uint8_t> from_fixture;
    for (const auto& row : kirpich::fixtures::kGameStateExpected) {
        from_fixture.insert(row.value);
    }

    // No duplicate values in the header (54 distinct enumerators), and the two sets agree.
    EXPECT_EQ(from_enum.size(), 54u);
    EXPECT_EQ(from_enum, from_fixture);

    // And the shared set is exactly the contiguous run 0x00..0x35.
    std::set<std::uint8_t> contiguous;
    for (int v = 0x00; v <= 0x35; ++v) {
        contiguous.insert(static_cast<std::uint8_t>(v));
    }
    EXPECT_EQ(from_fixture, contiguous);
}

// The port's own screens: states the cartridge never had. They answer to no fixture — nothing in the
// disassembly describes them — so what is pinned is the shape they have to keep. They start at 0x40,
// they are distinct, they sit clear of every cartridge state, and the dispatch table is exactly big
// enough to hold the highest of them.
constexpr std::array<GameState, 20> kAllPortGameStates{{
    GameState::INIT_SETTINGS,          GameState::SETTINGS,
    GameState::INIT_RESET_CONFIRM,     GameState::RESET_CONFIRM,
    GameState::INIT_MODE_SCREEN,       GameState::MODE_SCREEN,
    GameState::SELECT_MODE_OPTION,     GameState::INIT_TYPE_C_DIFFICULTY,
    GameState::TYPE_C_LEVEL_SELECTION, GameState::INIT_FIXES_SCREEN,
    GameState::FIXES_SCREEN,           GameState::INIT_GHOST_SCREEN,
    GameState::GHOST_SCREEN,           GameState::TYPE_C_RISE_SELECTION,
    GameState::INIT_STATS_SCREEN,      GameState::STATS_SCREEN,
    GameState::INIT_STATS_MENU,        GameState::STATS_MENU,
    GameState::INIT_STATS_LIST,        GameState::STATS_LIST,
}};

TEST(CoreEnums, PortGameStatesSitAboveTheCartridgeRange) {
    std::set<std::uint8_t> values;
    for (const GameState s : kAllPortGameStates) {
        values.insert(raw(s));
    }
    EXPECT_EQ(values.size(), kAllPortGameStates.size()) << "the port's states are distinct";

    for (const GameState s : kAllPortGameStates) {
        EXPECT_GE(raw(s), 0x40) << "the port's own range begins at 0x40";
        EXPECT_GT(raw(s), 0x36) << "and stays clear of the cartridge's states and its table over-read";
    }

    // The dispatch table is indexed by the state byte, so it has to reach the highest one there is.
    const std::uint8_t highest = *values.rbegin();
    EXPECT_EQ(kirpich::systems::kGameStateCount, static_cast<std::size_t>(highest) + 1)
        << "every state the game can write must have a slot to dispatch through";
}

constexpr std::array<SerialState, 4> kAllSerialStates{{
    SerialState::HANDSHAKE, SerialState::RECEIVE,
    SerialState::EXCHANGE,  SerialState::ACKNOWLEDGE,
}};

TEST(CoreEnums, SerialStateValuesMatchFixture) {
    ASSERT_EQ(kirpich::fixtures::kSerialStateExpected.size(), 4u);
    ASSERT_EQ(kAllSerialStates.size(), 4u);
    for (std::size_t i = 0; i < kAllSerialStates.size(); ++i) {
        EXPECT_EQ(raw(kAllSerialStates[i]), kirpich::fixtures::kSerialStateExpected[i].index);
    }
}

}  // namespace
