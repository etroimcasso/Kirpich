# Declarative screens

How a screen the port writes itself puts a picture together: as components, each a function that
returns what it is, assembled into layers the engine reconciles. This page covers the return types,
the two layer aggregates and the options they forward, the lifetime rule that makes a span safe, the
components any screen can use, and how a screen reaches the frame.

Screens carried over from the cartridge work the other way — they write into a background map and an
object buffer, and [rendering.md](rendering.md) covers that path. The two are separate: a screen is
built one way or the other, never half of each.

## The shape

A component is a function of what it is given, and it returns the primitives it is. Nothing is
remembered between frames, and nothing is drawn — the whole picture is a value, computed from state
each frame, and the engine compares it against last frame's to decide what to upload.

Three kinds, and what each returns (`src/render/types.h`):

| Kind | Returns |
|---|---|
| A leaf, or a group of them — a badge, a run of text, a grid, a panel | its primitives: `Sprites` or `Cells` |
| An aggregate — one that gathers components onto a layer | a `DrawLayer` |
| A screen | its layers: `Layers` |

```cpp
using Sprites = std::vector<retropp::Sprite>;
using Cells   = std::vector<retropp::TileCell>;
using Regions = std::vector<retropp::Region>;
using Layers  = std::vector<retropp::DrawLayer>;
```

A component is named for the component it is — `AchievementBadge`, `BadgeGrid`, `Glyphs` — not for
the act of producing one. The call is the declaration.

## The two aggregates

```cpp
DrawLayer BackgroundLayer(std::string_view key, std::int32_t z, Cells cells,
                          LayerOptions options = {});
DrawLayer SpriteLayer(std::string_view key, std::int32_t z,
                      std::initializer_list<Sprites> children, LayerOptions options = {});
```

`BackgroundLayer` takes the cells a background component returned; `SpriteLayer` takes its children
declared as a list and concatenates them in order. Both size the layer to the Game Boy viewport, and
`BackgroundLayer` wraps with `TileWrap::Blank`, so a layer ends where its cells do rather than
repeating.

An aggregate owns a layer's name, its depth and what is on it. Everything else passes through:

```cpp
struct LayerOptions {
    retropp::LayerScroll                    scroll{};
    float                                   alpha = 1.0f;
    retropp::BlendMode                      blend = retropp::BlendMode::Normal;
    std::vector<retropp::ScreenSpaceEffect> effects{};
    std::vector<retropp::Region>            regions{};
    retropp::Transform                      transform{};
    retropp::DisplacementEdge               transformEdge = retropp::DisplacementEdge::Blank;
};
```

The defaults are the engine's own, so leaving `options` out draws exactly as the layer would have. A
caller fades, tints, transforms or confines an effect to a shape without the aggregate knowing what
any of it means.

### Lifetime — why an aggregate keeps what it is handed

`TileContent::cells` and `SpriteContent::sprites` are spans the renderer reads while it draws the
frame, so the storage behind them has to outlive the submission. Each aggregate keeps what it was
handed under its own key and replaces it the next time that key is drawn. Nothing is appended, and a
component never takes storage as an out-parameter.

The consequence for a caller: **a layer key is an identity, not a label.** Two layers submitted under
one key in the same frame would have the second overwrite the first's content while both still point
at it.

## The components any screen can use

These live at the render layer rather than under a screen's own folder, because more than one screen
can use them.

```cpp
Sprites Glyphs(std::string_view text, int x, int y, int pitch,
               const TileAtlas& atlas, std::uint8_t ramp);
std::vector<std::string_view> wrapText(std::string_view text, std::size_t width);
Cells   Backdrop(const TileAtlas& atlas, std::uint8_t ramp);
Regions SelectionCorners(std::string_view key, float x, float y, float w, float h,
                         retropp::Rgba8 colour);
```

`Glyphs` is a run of text as one sprite per character, placed by pixel rather than on the eight-pixel
cell grid, drawn through an object palette so only the ink lands. **The font has the letters, the
digits, a period and a hyphen and nothing else** — a character it cannot spell keeps its place in the
run and draws nothing. Each glyph is named for where it sits, so a run holds its identity between
frames without the caller naming it.

`wrapText` breaks a passage into lines of at most `width` characters on word boundaries, for a caller
that then draws each line with `Glyphs`. A word longer than the width takes a line to itself and runs
past the edge rather than being cut mid-word. The lines point into the text it was given, so that text
has to outlive them.

`Backdrop` is the plain field of cells behind everything else — the one part of such a screen that is
tiles rather than sprites.

`SelectionCorners` is four corner brackets marking out a box: two bars to a corner, eight regions in
all, each a rectangle filled through a `ColorFill`. It marks what is selected without touching it, so
what is inside the box keeps its own colours. `kSelectorThickness` and `kSelectorArm` set how heavy
the brackets are and how far they run from each corner.

## Declaring a screen

A screen returns its layers as a list, and a gate is a ternary inside the declaration
(`src/render/achievements/screen.cpp`):

```cpp
Layers AchievementsScreen(const AchievementScreenState& ui, const AchievementState& earned,
                          bool blinkOn, const TileAtlas& atlas, std::uint8_t ramp) {
    // ...
    return {
        BackgroundLayer("ach-backdrop", 0, Backdrop(atlas, ramp)),
        SpriteLayer("ach-content", kAchBadgeZ,
                    {
                        Glyphs(achievementSectionTitle(section), kAchHeadingX, kAchHeadingY,
                               kAchGlyphPitch, atlas, ramp),
                        ui.open ? BadgePanel(ui.openId, earned, atlas, ramp)
                                : BadgeGrid(section, earned, atlas, ramp),
                    },
                    LayerOptions{.regions = std::move(cursor)}),
    };
}
```

Nothing is added by one path and cleared by another. A gate going false stops the component being
asked for, and the engine's reconciliation takes what it drew off the screen. There is no hide call,
and no cleanup.

## Logic is a separate module

The picture is a pure function of state, so the state and the law that moves it live apart from
anything that draws, in a module with no drawing type in it
(`src/systems/achievements_screen.h`). Input is a dispatch table — one table per mode, mapping an
action to what it does — rather than a chain of tests:

```cpp
using Effect = void (*)(GameContext&);
struct Bind { Action action; Effect effect; };

constexpr std::array kGridBinds{
    Bind{Action::MenuLeft,  [](GameContext& g) { moveCursor(g, -1, 0); }},
    // ...
    Bind{Action::Confirm,   [](GameContext& g) { openBadge(g); }},
};
```

The frame handler blinks the cursor and runs whichever table the current mode selects. It mutates
state and nothing else.

A screen's own state — which page, where the cursor is, what is open — is a `GameContext` member of
its own, separate from what the game *is*. It does not persist: a cold boot and the reset chord both
return it to its defaults.

## Reaching the frame

A declarative screen is handed the whole frame while it is up (`src/main.cpp`):

```cpp
if (kirpich::render::achievementScreenShown(game.flow.gameState)) {
    retropp::FrameDrawState screen;
    screen.layers = kirpich::render::AchievementsScreen(/* ... */);
    renderer.renderFrame(screen);
    return;
}
```

The background compose and the sprite compose below it do not run, and the screen writes neither the
background map nor the object buffer. Its init empties the object buffer, because whichever screen
was up before left its own entries there.

## A screen with one state

A screen needs an init only when something has to be set up before its first frame — the achievements
screen seeds its section and cursor there, and empties the object buffer the previous screen left. A
screen whose state is already set when it is reached needs neither, and has one state rather than two.

The end-of-round notice is the port's example (`src/systems/achievement_notice.h`). It is not opened
from a menu: a finished round is routed to it by a seam that has already filled its queue, so an init
would be a handler with nothing to do. It does not empty the object buffer either — its frame is its
own layers, so whatever the screen before it left there is simply not drawn.

```cpp
GameState achievementNoticeExit(GameContext& game, GameState destination);
```

The pattern is worth naming: **a screen can be entered by returning its state from a seam**, rather
than by a handler writing it. The caller passes where it was going, the seam holds that destination
and returns the screen's state instead, and the screen writes the held destination back when it is
done. Neither of the two handlers that call it knows the screen exists.

## Adding a screen

1. Mint the game states — an init and a loop, or a loop alone (see above) — and raise
   `kGameStateCount`.
2. Add the screen's state as a `GameContext` member, with `reset()` and a defaulted `operator==`.
3. Write the logic module: the cursor law, the input tables, the two handlers, the installer. No
   drawing type appears in it.
4. Write `layout.h` — every position the screen uses, in viewport pixels, shared by the components
   that draw them and the tests that read them back.
5. Write the components, one to a file. Anything a second screen could use goes at the render layer
   instead of under this screen's folder.
6. Write the screen function returning its layers, and a predicate naming the state it is shown in.
7. Give `main.cpp` the branch that hands it the frame.
8. Test against what the components return — call one with known parameters and read back its
   sprites or cells. Nothing reads a shared buffer, because there is not one.
