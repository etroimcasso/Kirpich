#include "render/config_section.h"

#include <cstddef>
#include <string>

#include "data/charmap.h"          // encodeCharmapText
#include "systems/menu_screens.h"  // where the section is placed

namespace kirpich::render {

namespace {

constexpr int kCell = 8;  // a background cell's side, in viewport pixels

// Above everything the object buffer can produce, so the section is never hidden behind a cursor.
constexpr std::int32_t kSectionZ = 200;

// The screen's own box art, read off the stored config screen. The two shipped boxes are built from
// these, and so is this one.
constexpr std::uint8_t kPlateLeft   = 0x50;  // the title plate's top rule, and its two ends
constexpr std::uint8_t kPlateTop    = 0x51;
constexpr std::uint8_t kPlateRight  = 0x52;
constexpr std::uint8_t kPlateSideL  = 0x53;  // the plate's sides, either side of the title
constexpr std::uint8_t kPlateSideR  = 0x54;
constexpr std::uint8_t kFrameTopL   = 0x55;  // the frame's top rule, corners outward
constexpr std::uint8_t kFrameTopPad = 0x56;
constexpr std::uint8_t kFrameTopR   = 0x5A;
constexpr std::uint8_t kFrameNotchL = 0x6D;  // where the plate meets the rule
constexpr std::uint8_t kFrameNotchR = 0x6E;
constexpr std::uint8_t kFrameTop    = 0x58;
constexpr std::uint8_t kFrameTee    = 0xA9;  // the rule's junction with the divider below it
constexpr std::uint8_t kWallLeft    = 0x5B;
constexpr std::uint8_t kWallRight   = 0x5C;
constexpr std::uint8_t kDivider     = 0xAA;
constexpr std::uint8_t kBottomLeft  = 0x2D;
constexpr std::uint8_t kBottomRule  = 0x4F;
constexpr std::uint8_t kBottomRight = 0x2E;
constexpr std::uint8_t kInside      = 0x2F;  // the empty cell, which is the inside of a box

// Where the section's parts sit. The frame spans the same columns the two shipped boxes do and its
// divider falls on the same column, so the three sections read as one screen.
constexpr std::size_t kFrameFirstCol = 2;
constexpr std::size_t kFrameLastCol  = 17;
constexpr std::size_t kDividerCol    = 10;
constexpr std::size_t kPlateFirstCol = 4;
constexpr std::size_t kPlateLastCol  = 15;

// How much room a title has on the plate, and where each label starts. The two labels sit the same
// distance either side of the screen's centre, so they read as a pair.
constexpr std::size_t kTitleCells = kPlateLastCol - kPlateFirstCol - 1;
constexpr std::size_t kLeftCol    = 3;
constexpr std::size_t kRightCol   = 12;

// The section's own rows, top to bottom.
constexpr std::size_t kPlateTopRow   = 0;
constexpr std::size_t kPlateTextRow  = 1;
constexpr std::size_t kFrameRuleRow  = 2;
constexpr std::size_t kChoiceRow     = 3;
constexpr std::size_t kBottomRuleRow = 4;

static_assert(kBottomRuleRow + 1 == systems::kConfigSectionRows,
              "every row of the box has to be drawn");

// One cell of the section. Columns are the screen's own, because the section lines up with the boxes
// above and below it; rows are counted from the section's own top and land wherever the pixel origin
// puts them, which is half a row above the grid.
retropp::Sprite tileSprite(std::string key, std::size_t row, std::size_t col, std::uint8_t index,
                           const TileAtlas& atlas, std::uint8_t ramp, bool dim) {
    // The background palette, not an object one: an object's lightest shade is see-through, and here
    // it is the inside of the box.
    const ResolvedTile art = resolveTile(index, kirpich::TileSheet::GAMEPLAY, atlas, ramp);
    return retropp::Sprite{
        .key     = retropp::ObjectKey{std::move(key)},
        .x       = static_cast<int>(col) * kCell,
        .y       = systems::kConfigSectionTop + static_cast<int>(row) * kCell,
        .z       = kSectionZ,
        .atlas   = art.atlas,
        .tile    = art.cell,
        .palette = dim ? atlas.palettes[clampShadeRamp(ramp)].fontDim : art.palette,
    };
}

std::vector<kirpich::CharTile> glyphsOf(std::string_view text) {
    const auto encoded = kirpich::encodeCharmapText(text);
    return encoded ? *encoded : std::vector<kirpich::CharTile>{};
}

}  // namespace

std::vector<retropp::Sprite> configSectionSprites(const ConfigSectionLabels& labels,
                                                  bool rightChosen, bool selectedVisible,
                                                  const TileAtlas& atlas, std::uint8_t ramp) {
    std::vector<retropp::Sprite> sprites;

    // One row of the box, as a run of cells. `at` returns the tile for a column and `dimAt` whether
    // it is inked in grey.
    const auto emitRow = [&](std::string_view name, std::size_t row, std::size_t firstCol,
                             std::size_t lastCol, auto at, auto dimAt) {
        for (std::size_t col = firstCol; col <= lastCol; ++col) {
            sprites.push_back(tileSprite(std::string{name} + std::to_string(col), row, col, at(col),
                                         atlas, ramp, dimAt(col)));
        }
    };
    const auto never = [](std::size_t) { return false; };

    // The title plate: its own top rule, then the row its title sits on between the plate's sides. A
    // title longer than the plate is clipped rather than running over the sides.
    emitRow("config-plate-", kPlateTopRow, kPlateFirstCol, kPlateLastCol,
            [](std::size_t col) -> std::uint8_t {
                if (col == kPlateFirstCol) return kPlateLeft;
                if (col == kPlateLastCol) return kPlateRight;
                return kPlateTop;
            },
            never);

    const std::vector<kirpich::CharTile> titleGlyphs = glyphsOf(labels.title);
    emitRow("config-title-", kPlateTextRow, kPlateFirstCol, kPlateLastCol,
            [&](std::size_t col) -> std::uint8_t {
                if (col == kPlateFirstCol) return kPlateSideL;
                if (col == kPlateLastCol) return kPlateSideR;
                const std::size_t i = col - kPlateFirstCol - 1;
                return i < titleGlyphs.size() && i < kTitleCells
                           ? static_cast<std::uint8_t>(titleGlyphs[i])
                           : kInside;
            },
            never);

    // The frame's top rule, with the notches where the plate lands on it and the tee where the
    // divider below meets it.
    emitRow("config-frame-", kFrameRuleRow, kFrameFirstCol, kFrameLastCol,
            [](std::size_t col) -> std::uint8_t {
                if (col == kFrameFirstCol) return kFrameTopL;
                if (col == kFrameFirstCol + 1 || col == kFrameLastCol - 1) return kFrameTopPad;
                if (col == kFrameLastCol) return kFrameTopR;
                if (col == kPlateFirstCol) return kFrameNotchL;
                if (col == kPlateLastCol) return kFrameNotchR;
                if (col == kDividerCol) return kFrameTee;
                return kFrameTop;
            },
            never);

    // The choices. Both labels are always drawn; which one is current is said by its ink, and the
    // blink drops the current one back to grey for its off frames.
    const std::vector<kirpich::CharTile> leftGlyphs  = glyphsOf(labels.left);
    const std::vector<kirpich::CharTile> rightGlyphs = glyphsOf(labels.right);

    const auto inLeft = [&](std::size_t col) {
        return col >= kLeftCol && col < kLeftCol + leftGlyphs.size();
    };
    const auto inRight = [&](std::size_t col) {
        return col >= kRightCol && col < kRightCol + rightGlyphs.size();
    };
    const bool leftInk  = !rightChosen && selectedVisible;
    const bool rightInk = rightChosen && selectedVisible;

    emitRow("config-choice-", kChoiceRow, kFrameFirstCol, kFrameLastCol,
            [&](std::size_t col) -> std::uint8_t {
                if (col == kFrameFirstCol) return kWallLeft;
                if (col == kFrameLastCol) return kWallRight;
                if (col == kDividerCol) return kDivider;
                if (inLeft(col)) return static_cast<std::uint8_t>(leftGlyphs[col - kLeftCol]);
                if (inRight(col)) return static_cast<std::uint8_t>(rightGlyphs[col - kRightCol]);
                return kInside;
            },
            [&](std::size_t col) {
                if (inLeft(col)) return !leftInk;
                if (inRight(col)) return !rightInk;
                return false;
            });

    // The frame's bottom rule.
    emitRow("config-bottom-", kBottomRuleRow, kFrameFirstCol, kFrameLastCol,
            [](std::size_t col) -> std::uint8_t {
                if (col == kFrameFirstCol) return kBottomLeft;
                if (col == kFrameLastCol) return kBottomRight;
                return kBottomRule;
            },
            never);

    return sprites;
}

}  // namespace kirpich::render
