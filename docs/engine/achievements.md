# Achievements

What the player has earned, kept across launches, and the screen that shows it. This page covers the
definition set and its condition vocabulary, the round-end check that awards an achievement, the
records and their save document, the screen, and where each call is wired.

## The set

One `AchievementDef` per achievement, the whole set a single table in `AchievementId` order
(`src/systems/achievements.cpp`):

```cpp
struct AchievementDef {
    AchievementId      id{};
    AchievementSection section{};
    std::uint8_t       tier   = 1;   // 1..4, display ordering only — there are no points
    bool               hidden = false;
    std::string_view   title{};
    std::string_view   description{};
    Condition          condition{};
};

std::span<const AchievementDef> achievementSet();
const AchievementDef& achievementDef(AchievementId id) noexcept;
```

The set is one table in id order, so an id indexes it; `achievementDef` is the lookup every surface
that draws an achievement goes through.

`title` and `description` are data beside the id, so either can be rewritten without touching logic
or the save format. The description states the criterion in words; the condition is what is actually
evaluated. `hidden` changes only what the screen draws for one not yet earned — it has no bearing on
how it is awarded.

**`hidden` means naming it would spoil a discovery, so it defaults to shown.** Three of the set carry
it, the launch scenes: the rocket and the Buran are the game's secret endings. The rest is a ladder,
and a ladder is there to be aimed at.

`AchievementSection` is the themed grouping, nine of them, one to a page on the screen. The set is
grouped by section in id order, so **a section is one contiguous run of the table** — the screen's
walk depends on that, and an assertion holds it.

### Conditions

A condition is one of a closed vocabulary, kept as plain data rather than a callable so the set stays
a literal table and each shape is tested on its own:

```cpp
enum class ConditionKind : std::uint8_t {
    ScoreAtLeast, LinesInRoundAtLeast, LifetimeLines, TetrisesInRound, LifetimeTetrises,
    TypeBWin, LevelReached, HeartRoundFinished, SceneReached, LifetimeRounds,
    AllGameTypesPlayed, PlayedAllMusic, LifetimeDrops, ApplicationSecondsAtLeast,
};
```

`Condition` carries `type`, `threshold`, `minLevel`, `minVariant`, `variantMatters`, `requireHeart`
and `scene`; only the fields its kind reads are meaningful and the rest keep their defaults. The
evaluator is one switch over the vocabulary, so **a new achievement is a new row and needs no
evaluator change** unless it needs a condition shape that does not exist yet.

## Awarding

```cpp
using NowDate = std::function<AchievementDate()>;
void evaluateRoundEnd(GameContext& game, const NowDate& now);
```

The check runs once, when a round ends. By then the level has finished climbing and the lines, score,
drops and tetrises are accumulated, so one pass answers every condition. It reads the round's final
state, the bonus scenes the round reached, and the player's lifetime slice totals — already updated
with this round — and stamps every newly met achievement with the date and the play time at that
moment. **First unlock wins:** a met condition never overwrites an existing stamp.

`NowDate` is separate from the statistics' `NowNanos` because a date is not a duration: the engine
measures elapsed time and cannot report a calendar date, so the port injects one. Without a clock the
date is zero, which is how a test pins an unlock deterministically.

**The date is the player's local one.** The system clock counts from an epoch and carries no zone, so
the host converts through local time before taking the day; reading the UTC day directly stamps an
evening's play with tomorrow's date anywhere west of Greenwich. The conversion goes through the C
library rather than a `<chrono>` time zone, whose database is not dependably present across the five
platforms this builds on.

Three properties worth knowing before adding a firing point:

- **It does nothing unless a round has concluded and is awaiting the check**, so a second call from
  another exit is a no-op and a firing point reached without a round is harmless.
- **A demo earns nothing, with no gate of its own.** `active` is set only by
  `beginAchievementRound`, which the recording layer calls only for a real round; `noteRoundConcluded`
  sets `pendingEval` only while `active`.
- **It clears the round's observations when it returns**, which is what makes it once-per-round.

Whatever it awards it also queues on `AchievementNoticeState`, in the set's order, emptying that queue
first — so a round that earns nothing leaves nothing to announce rather than repeating the round
before it. That queue is what the end-of-round notice shows.

### What the round observes

The gameplay handlers report through seams rather than reaching into fields
(`src/state/achievement_state.h`), each taking the state directly so a handler need not depend on the
achievements system:

```cpp
void beginAchievementRound(AchievementState&);
void noteRoundConcluded(AchievementState&, bool wonTypeB);
void noteRoundTetris(AchievementState&);
void noteRocketScene(AchievementState&, bool topTier);
void noteBuranScene(AchievementState&);
```

## The records

```cpp
struct AchievementUnlock {
    bool          unlocked            = false;
    std::uint32_t datePacked          = 0;  // year-month-day as a decimal number
    std::uint32_t playSecondsAtUnlock = 0;  // applicationSeconds when it fired
};

struct AchievementState {
    std::array<AchievementUnlock, kAchievementCount> unlocked{};  // indexed by AchievementId
    AchievementRound                                 round{};     // never written to disk
    void reset();
};
```

A date packs to a decimal number (2026-09-07 becomes 20260907) so it stores in one field and reads
back without calendar arithmetic; zero means no date.

**The array is indexed by `AchievementId`, so the save is keyed by identity rather than by table
position.** The set can grow — new ids appended, the array widened, a migration padding older records
forward — without disturbing what a player has already earned. Reordering an existing enumerator
would move its saved record, so the enumerators are fixed once shipped.

`kAchievementCount` is tied to the last enumerator, so the persisted array and the definition table
cannot drift from the enum they are indexed by.

```cpp
[[nodiscard]] std::size_t achievementsUnlocked(const GameContext&);
[[nodiscard]] constexpr std::size_t achievementsTotal() noexcept;
```

Both count surfaces and the screen's hidden reveal read these, so the figure lives in one place.

## Persistence

`src/state/achievement_persistence.h` — document `"achievements"`, schema version 1, a fixed image of
`kAchievementCount × kAchievementRecordBytes`: one record per achievement in id order, each a flag
and its two 32-bit stamps. The round in progress is never written.

```cpp
bool saveAchievements(const AchievementState&, retropp::SaveStore&);
bool loadAchievements(retropp::SaveStore&, AchievementState&);
```

An absent document is an ordinary first run and leaves the records empty. A wrong-length or corrupt
one is logged, leaves them empty, and leaves the damaged file where it is.

**`loadAchievements` declares the schema version on the store immediately before its own read.** This
store also carries the settings, the top scores and the statistics at versions of their own, so
whichever loader is about to read has to be the one that last said which version it means.

The reset chord keeps the records by hand, because they outlive it.

## The screen

Built as declarative components — see [declarative-screens.md](declarative-screens.md) for the model
itself. One section to a page: a heading, a grid of badges three across, a corner selector on the one
under the cursor, and a panel that opens on a badge.

### The logic

`src/systems/achievements_screen.h`. It draws nothing and names no drawing type.

```cpp
AchievementSection   achievementSectionAt(std::uint8_t index) noexcept;
std::string_view     achievementSectionTitle(AchievementSection) noexcept;
std::size_t          achievementSectionBadgeCount(AchievementSection) noexcept;
const AchievementDef& achievementSectionBadge(AchievementSection, std::size_t position) noexcept;
bool                 achievementUnlocked(const AchievementState&, AchievementId) noexcept;
```

A stale index clamps to a real section, and a position past a section's run clamps to its last badge,
so no stored value can name something that is not there.

The cursor walks the section's badges; **walking off the bottom turns to the next section and off the
top to the previous**, so the whole set is one continuous grid whose only end stops are the first
section's top and the last section's bottom. A turn **keeps the cursor's column** and lands in the row
it stepped into — the first row of the section below, the last row of the section above — clamping to
the last badge when that row is shorter. A vertical step therefore means the same thing at a section
boundary as inside one. Left and right stop at a row's ends. `A` opens the badge under the cursor;
`B` closes an open one, or leaves the screen.

Input is a dispatch table, one per mode — `kGridBinds` while walking, `kPanelBinds` while reading a
badge — and the frame handler blinks the cursor, then runs whichever the current mode selects.

`initAchievementsScreen` opens at the first section and first badge with nothing open, empties the
object buffer (whichever screen was up before left its entries there), and arms the shared cursor
blink. It writes no background map and saves no caller: the chooser it was opened from draws itself
again when the player comes back.

### The components

`src/render/achievements/`, with every position in `layout.h` in viewport pixels — shared by the
components that draw and the tests that read them back.

```cpp
Sprites AchievementBadge(AchievementId, int x, int y, bool unlocked,
                         const TileAtlas&, std::uint8_t ramp);
Sprites BadgeGrid(AchievementSection, const AchievementState&, const TileAtlas&, std::uint8_t ramp);
Sprites BadgePanel(AchievementId, const AchievementState&, const TileAtlas&, std::uint8_t ramp);
Layers  AchievementsScreen(const AchievementScreenState&, const AchievementState&, bool blinkOn,
                           const TileAtlas&, std::uint8_t ramp);
bool    achievementScreenShown(GameState) noexcept;
```

An earned badge and a locked one are the same art through a different palette: a locked one draws
faded, through the player's own ramp inked in its light shade, so an earned one is the solid one. It
fades **within** the ramp rather than falling back to a fixed one — twenty-nine of the eighty ramps
declare `mayBottomOutAtBlack`, and on those a badge drawn through another ramp's black is the same
black an earned one draws in. A badge does not know whether it is selected — the cursor is its own
object drawn around it — so a grid reads the same however it is being walked.

Three things a change here depends on:

- **A badge's key carries where it is drawn**, not just which achievement and which part. The same
  achievement appears in the grid and again on its own panel; those are two objects, and keying them
  alike makes the engine reconcile them as one and slide the grid badge across the screen when a
  badge is opened.
- **Which tile sheet a badge names matters as much as which sprite.** A tile index means different
  art under each regime, so art the game only ever draws while another sheet is loaded — the rocket,
  under `TileSheet::MULTIPLAYER_BURAN` — has to name that sheet, or its indices resolve to whatever
  the gameplay art keeps at them.
- **The selector is sized from the badge**, via `badgeExtent`, because the emblems are different
  shapes: a square of four, a bar of four across, a figure three tall.

```cpp
struct BadgeExtent { int width, height; };
BadgeExtent badgeExtent(AchievementId);
```

### The hidden reveal

A hidden achievement not yet earned shows its badge, the word `hidden`, and one line saying it is
revealed when earned — nothing about itself and nothing beyond that rule. It still says so rather
than showing a bare badge, which would read as a page that failed to draw. One that is not hidden, or
that has been earned, shows its title and description; an earned one also shows the date and the play
time it was earned at.

A description wraps to `kAchPanelTextCells` through the shared `wrapText`
(`src/render/glyphs.h`), on word boundaries; a word longer than the width takes its own line rather
than being cut mid-word.

## The end-of-round notice

What a finished round just earned, shown before the player is returned to a difficulty screen. One
badge to a screen, a press for the next; no heading, just the banner, and each banner arrives on the
level-up cue.

### How a round reaches it

A round ends in one of two places — the game-over screen, which every top-out and every Type B win
reaches, and the rocket scene, which a Type A or Type C round scoring past the first rocket boundary
leaves through instead. Both hand their destination to one seam and take back where the player
actually goes (`src/systems/game_context.h`):

```cpp
using RoundExit = std::function<GameState(GameContext& game, GameState destination)>;
```

`main.cpp` composes the check and the routing into the closure both are given. Unset, each handler
goes to its own destination unchanged, which is what every test that does not care about achievements
relies on.

```cpp
GameState achievementNoticeExit(GameContext& game, GameState destination);
AchievementId achievementOnNotice(const GameContext& game) noexcept;
void achievementNotice(GameContext& game);
void installAchievementNotice(GameStateDispatcher&);
```

`achievementNoticeExit` returns `destination` when the queue is empty. Otherwise it holds that
destination, points at the head of the queue, and returns `GameState::ACHIEVEMENT_NOTICE`.

### Its state

```cpp
struct AchievementNoticeState {
    BoundedVec<AchievementId, kAchievementCount> pending{};
    std::uint8_t shown  = 0;
    GameState    resume = GameState::INIT_TYPE_A_DIFFICULTY;
};
```

The queue is the one record of what was just earned — what is on screen is `pending[shown]`, never a
second copy — and it is written in exactly one place, the round-end check. It is not persisted: it
means nothing after the program stops, and **a soft reset clears it**, so a reset taken mid-round
cannot leave badges to be announced after the next one. What was earned at all does persist, in the
records above.

The notice has one state and no init, because the exit that routed to it has already set everything
it needs.

### The banner

```cpp
Sprites AchievementBanner(AchievementId, int x, int y, const TileAtlas&, std::uint8_t ramp);
Layers  AchievementNotice(AchievementId, const TileAtlas&, std::uint8_t ramp);
bool    achievementNoticeShown(GameState) noexcept;
```

The badge on the left, the title beside it, the description wrapped under the title;
`notice_layout.h` holds every position. **The text hangs off the badge's own width** rather than a
fixed column, and the wrap width is derived from where the text actually starts — the emblems are
different shapes, and a bar four tiles across would run under a title placed at a constant offset.

The badge draws earned, because a banner is only ever shown for one that has just been earned. That
is also why a hidden achievement shows its real title and description here: being earned is what
reveals it, and the placeholder copy belongs to the screen that lists what has not been.

A press steps to the next; the press past the last empties the queue and writes the held destination.
`Confirm` and `Start` both do it — the two the game-over screen accepts, so a player carrying on
pressing A steps through what they earned without learning a second button.

Every arriving banner writes `SquareSfxId::LEVEL_UP` to the cue mailbox, including the first, which
the exit cues as it routes. The press that ends the notice writes nothing: a cue marks an arrival, and
nothing arrives there.

## Where each call is wired

| Call | Site |
|---|---|
| `beginAchievementRound` | `beginRound` (`src/systems/stats.cpp`), after the demo is turned away |
| `noteRoundTetris` | the per-kind tally (`src/systems/stats.cpp`) |
| `noteRoundConcluded` | `initGameOver` (`src/systems/gameplay.cpp`), and both won-Type-B states (`src/systems/type_b_ending.cpp`) |
| `noteRocketScene`, `noteBuranScene` | the two scene handlers (`src/systems/launch_scenes.cpp`) |
| `evaluateRoundEnd` | one closure in `src/main.cpp`, handed to `gameOverScreen` and `endOfBonusScene` as a `RoundExit`; the reset closure and the exit guard call it directly |
| `achievementNoticeExit` | the same closure, immediately after the check |
| `installAchievementsScreen`, `installAchievementNotice` | `src/main.cpp` |
| `loadAchievements` | `bootGame` (`src/systems/boot.cpp`) |
| `saveAchievements` | `src/main.cpp`, beside the statistics |
| the screen's frame, the notice's frame | the render loop's first two branches (`src/main.cpp`) |

The two direct calls cover a round that had concluded but not yet been checked — a chord reset or a
quit from the game-over screen — so a last-round unlock is not lost. A mid-round reset or quit has
nothing pending and the call is a no-op.

## Where it is shown elsewhere

The statistics chooser's achievements row opens this screen directly and carries how many have been
earned; the All-Time page's combined view carries the same figure on an `achieved` line — the label
names what is counted, because alone on a page of durations and rounds a bare verb has no subject, and
it has to stay inside `kStatsLabelCol` through `kStatsValueEndCol` less the figure's own cells, which
a `static_assert` holds. Both read
`achievementsUnlocked` / `achievementsTotal`. The font has no slash, so the figure is joined by the
hyphen it does have.

The row's count is why `ListWiring::paintRow` is handed the game
(`src/systems/list_screen.h`): a row can carry a figure read from state rather than a fixed string.

## Adding an achievement

1. Append an `AchievementId` enumerator. Never reorder existing ones — an id is the saved key.
2. Add its row to the set, in id order, inside its section's contiguous run.
3. Give it a condition. If no existing `ConditionKind` expresses it, add a kind and its branch in
   `conditionMet`; otherwise the evaluator is untouched.
4. Leave `hidden` alone unless naming the achievement would spoil a discovery, in which case set it.
5. Nothing else. `kAchievementCount` follows the enum, the image size follows the count, and the
   screen's page count and grid follow the set.

A set that outgrows the current schema needs a version bump and a migration that pads older records
forward, since the image is a fixed length derived from the count.

## Adding a section

A section is an `AchievementSection` enumerator, a case in `achievementSectionTitle`, a contiguous run
in the set, and art in `artFor` (`src/render/achievements/badge.cpp`). The page count follows the
enum, so nothing else is raised.
