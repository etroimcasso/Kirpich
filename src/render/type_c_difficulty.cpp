#include "render/type_c_difficulty.h"

#include <string>

#include "systems/menu_screens.h"  // where the compartments are
#include "systems/rising_floor.h"  // kTypeCRiseValues

namespace kirpich::render {

namespace {

constexpr int kCell = 8;  // a background cell's side, in viewport pixels

// The first big digit. The screen's numbers are drawn from $90 up, which is where the gameplay art
// keeps them.
constexpr std::uint8_t kBigDigitZero = 0x90;

// Above the object buffer, so the values are never hidden behind a cursor left over from another
// screen; the highlight goes above them again.
constexpr std::int32_t kValueZ     = 200;
constexpr std::int32_t kHighlightZ = 201;

// Where a compartment's cell sits in the viewport. The map's top-left cell is the display's, so a
// cell's pixel origin is its index times the cell size.
int cellX(std::size_t col) { return static_cast<int>(col) * kCell; }
int cellY(std::size_t row) { return static_cast<int>(row) * kCell; }

// One digit of one value. `slot` names the sprite so a redraw replaces it rather than stacking.
//
// Everything here is drawn as an OBJECT, whose lightest shade is see-through. That is what lets a pair
// sit in a compartment at all: a background palette is opaque, so the pair's two tile squares would
// paint over the rules either side of the compartment and leave the box open between its numbers.
// Through an object palette only the ink lands, and the rules the pair reaches across survive under it.
//
// A value is drawn from the box's own big digits. The current one is then drawn again in the FONT
// digit, which is a different glyph in the darkest shade - which is exactly what the difficulty
// screens' blinking digit cursor is, laid over the digit the backdrop holds (see updateDigitCursor in
// systems/menu_screens.h, whose cursor sprite is SpriteId::DIGIT_0 + n). The two big-digit object
// palettes cannot serve here: they differ only at the shade a digit's ink does not use, so one drawn
// over the other is invisible.
retropp::Sprite digitSprite(std::string slot, int x, int y, std::uint8_t digit, bool highlight,
                            const TileAtlas& atlas, std::uint8_t ramp) {
    const std::uint8_t index =
        highlight ? digit : static_cast<std::uint8_t>(kBigDigitZero + digit);
    const ResolvedTile art =
        resolveSpriteTile(index, kirpich::TileSheet::GAMEPLAY, /*palette1=*/false, atlas, ramp);

    return retropp::Sprite{
        .key     = retropp::ObjectKey{std::move(slot)},
        .x       = x,
        .y       = y,
        .z       = highlight ? kHighlightZ : kValueZ,
        .atlas   = art.atlas,
        .tile    = art.cell,
        .palette = art.palette,
    };
}

// The value at `index`, at the compartment that index occupies.
//
// A leading zero is not drawn. A one-digit value is one glyph, sitting on the compartment's own cell
// where the stored digit sat; only a two-digit value takes the pair's wider placement. That is the law
// every number on screen follows - the panel's printer skips leading zeros and always draws the last
// digit (printNumber, systems/readouts.h) - and a rise of 6 written as "06" reads as a different number.
void emitValue(std::vector<retropp::Sprite>& out, std::size_t index, bool highlight,
               const TileAtlas& atlas, std::uint8_t ramp) {
    const std::uint8_t value = systems::kTypeCRiseValues[index];
    const int          cell  = cellX(systems::kRiseValueCols[index % 3]);
    const int          y     = cellY(systems::kRiseValueRows[index / 3]);

    const std::string stem =
        std::string{highlight ? "rise-cursor-" : "rise-value-"} + std::to_string(index);

    if (value < 10) {
        out.push_back(digitSprite(stem + "-ones", cell, y, value, highlight, atlas, ramp));
        return;
    }

    out.push_back(digitSprite(stem + "-tens", cell + kRiseTensOffset, y,
                              static_cast<std::uint8_t>(value / 10), highlight, atlas, ramp));
    out.push_back(digitSprite(stem + "-ones", cell + kRiseTensOffset + kRiseDigitPitch, y,
                              static_cast<std::uint8_t>(value % 10), highlight, atlas, ramp));
}

}  // namespace

bool riseValuesShown(kirpich::GameState state, kirpich::GameType type) noexcept {
    switch (state) {
        case kirpich::GameState::INIT_TYPE_C_DIFFICULTY:
        case kirpich::GameState::TYPE_C_LEVEL_SELECTION:
        case kirpich::GameState::TYPE_C_RISE_SELECTION:
            return true;
        case kirpich::GameState::ENTER_TOP_SCORE:
            // Name entry has no backdrop of its own. It is looking at whichever difficulty screen the
            // round came from, so the values belong on screen exactly when that round was Type C.
            return type == kirpich::GameType::TYPE_C;
        default:
            return false;
    }
}

std::vector<retropp::Sprite> riseValueSprites(const RiseSelection& selection, const TileAtlas& atlas,
                                              std::uint8_t ramp) {
    std::vector<retropp::Sprite> sprites;
    sprites.reserve(systems::kTypeCRiseChoiceCount * 2 + 2);

    for (std::size_t i = 0; i < systems::kTypeCRiseChoiceCount; ++i) {
        emitValue(sprites, i, /*highlight=*/false, atlas, ramp);
    }

    // The cursor: the current value again, over the top of itself, the way the other pickers lay their
    // cursor over the digit already on the screen. An index past the end draws none rather than
    // reaching outside the table.
    if (selection.selectedVisible && selection.rise < systems::kTypeCRiseChoiceCount) {
        emitValue(sprites, selection.rise, /*highlight=*/true, atlas, ramp);
    }

    return sprites;
}

}  // namespace kirpich::render
