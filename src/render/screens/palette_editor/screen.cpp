#include "render/screens/palette_editor/screen.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "render/components/arrow.h"
#include "render/components/backdrop.h"
#include "render/components/background_layer.h"
#include "render/components/glyphs.h"
#include "render/components/page_arrows.h"
#include "render/components/sprite_layer.h"
#include "render/components/yes_no_question.h"
#include "render/palettes.h"                               // clampShadeRamp, kCustomRamp
#include "render/screens/palette_editor/layout.h"
#include "render/screens/palette_editor/palette_editor.h"  // the logic's own answers
#include "render/screens/palette_editor/palette_preview.h"
#include "render/screens/palette_editor/preview_backdrop.h"

namespace kirpich::render {

using namespace palette_editor;

namespace {

using kirpich::ColorChannel;
using kirpich::DigitPlace;
using kirpich::PaletteEditorQuestion;
using kirpich::PaletteEditorRow;

// A number as `width` digits with leading zeros.
std::string padded(unsigned value, std::size_t width) {
    std::string text = std::to_string(value);
    if (text.size() < width) {
        text.insert(0, width - text.size(), '0');
    }
    return text;
}

constexpr std::array kChannels{ColorChannel::RED, ColorChannel::GREEN, ColorChannel::BLUE};

[[nodiscard]] int rowY(PaletteEditorRow row) noexcept {
    switch (row) {
        case PaletteEditorRow::PRESET:
            return kPresetY;
        case PaletteEditorRow::LOAD:
            return kLoadY;
        case PaletteEditorRow::SAVE:
            return kSaveY;
        case PaletteEditorRow::CLEAR:
            return kClearY;
        default:
            break;
    }
    return entryY(*kirpich::shadeOf(row));
}

// What each question says. The second line names the preset as two digits where it is about one.
std::string_view questionTitle(PaletteEditorQuestion q) noexcept {
    switch (q) {
        case PaletteEditorQuestion::LOAD_UNSAVED:
            return "load preset";
        case PaletteEditorQuestion::SAVE_OVER:
            return "save preset";
        default:
            break;
    }
    return "clear preset";
}

std::string_view questionFirstLine(PaletteEditorQuestion q) noexcept {
    switch (q) {
        case PaletteEditorQuestion::LOAD_UNSAVED:
            return "your palette is";
        case PaletteEditorQuestion::SAVE_OVER:
            return "replace";
        default:
            break;
    }
    return "empty";
}

std::string questionSecondLine(PaletteEditorQuestion q, std::uint8_t preset) {
    if (q == PaletteEditorQuestion::LOAD_UNSAVED) {
        return "not saved";
    }
    return "preset " + padded(preset, 2);
}

// The preset's number as two digits, dimmed while its slot is empty, so an unused slot reads as one
// without consulting the actions below it.
Sprites PresetNumber(std::uint8_t preset, const kirpich::CustomPalettes& palettes,
                     const TileAtlas& atlas, std::uint8_t ramp) {
    const std::string text  = padded(preset, 2);
    const bool        empty = preset < 1 || preset > kirpich::kCustomPaletteSlotCount ||
                       !palettes.slots[preset - 1].present;
    return empty ? DimGlyphs(text, kPresetValueX, kPresetY, kGlyph, atlas, ramp)
                 : Glyphs(text, kPresetValueX, kPresetY, kGlyph, atlas, ramp);
}

// One action's label, dimmed while the action can do nothing.
Sprites ActionLabel(std::string_view label, PaletteEditorRow action, std::uint8_t preset,
                    const kirpich::CustomPalettes& palettes, const TileAtlas& atlas,
                    std::uint8_t ramp) {
    const int y = rowY(action);
    return systems::paletteEditorActionCanAct(action, preset, &palettes)
               ? Glyphs(label, kLabelX, y, kGlyph, atlas, ramp)
               : DimGlyphs(label, kLabelX, y, kGlyph, atlas, ramp);
}

// One shade's row: its number, then its three values, the entered one drawn selected.
Sprites ShadeRow(std::size_t shade, const kirpich::PaletteEditorState& ui,
                 const kirpich::CustomPalettes& palettes, const TileAtlas& atlas,
                 std::uint8_t ramp) {
    const int y = entryY(shade);
    Sprites   out = Glyphs(std::to_string(shade + 1), kEntryNumberX, y, kGlyph, atlas, ramp);

    const std::optional<std::size_t> editing = kirpich::shadeOf(ui.row);
    for (const ColorChannel channel : kChannels) {
        const std::string text =
            padded(kirpich::channelValue(palettes.working, shade, channel), kValueDigits);
        const bool entered = ui.entering && editing == shade && ui.channel == channel;
        const Sprites value = entered ? SelectedGlyphs(text, valueX(channel), y, kGlyph, atlas, ramp)
                                      : Glyphs(text, valueX(channel), y, kGlyph, atlas, ramp);
        out.insert(out.end(), value.begin(), value.end());
    }
    return out;
}

// The row cursor: before a label on the four upper rows, before the channel's value on a shade row.
// Not drawn while a value is entered.
Sprites RowCursor(const kirpich::PaletteEditorState& ui, bool blinkOn, const TileAtlas& atlas,
                  std::uint8_t ramp) {
    if (ui.entering || !blinkOn) {
        return {};
    }
    const int x = kirpich::shadeOf(ui.row) ? valueX(ui.channel) - kValueCursorGap : kActionCursorX;
    return Glyphs("-", x, rowY(ui.row), kGlyph, atlas, ramp);
}

// The entered value's selector: an arrow either side where the place can still move, and one over and
// one under the digit being stepped where the value can still move.
Sprites ValueSelector(const kirpich::PaletteEditorState& ui, const kirpich::CustomPalettes& palettes,
                      const TileAtlas& atlas, std::uint8_t ramp) {
    const std::optional<std::size_t> shade = kirpich::shadeOf(ui.row);
    if (!ui.entering || !shade) {
        return {};
    }
    const int          x     = valueX(ui.channel);
    const int          y     = entryY(*shade);
    const std::uint8_t value = kirpich::channelValue(palettes.working, *shade, ui.channel);
    const int          digitX = x + static_cast<int>(ui.place) * kGlyph;

    Sprites    out;
    const auto add = [&out](const Sprites& part) { out.insert(out.end(), part.begin(), part.end()); };
    if (ui.place != DigitPlace::HUNDREDS) {
        add(Arrow("value-left", x - kValueCursorGap, y, ArrowDirection::LEFT, kArrowZ, atlas, ramp));
    }
    if (ui.place != DigitPlace::ONES) {
        add(Arrow("value-right", x + kValueWidth, y, ArrowDirection::RIGHT, kArrowZ, atlas, ramp));
    }
    if (systems::steppedValue(value, ui.place, +1) != value) {
        add(Arrow("digit-up", digitX, y - kArrowRise, ArrowDirection::UP, kArrowZ, atlas, ramp));
    }
    if (systems::steppedValue(value, ui.place, -1) != value) {
        add(Arrow("digit-down", digitX, y + kArrowRise, ArrowDirection::DOWN, kArrowZ, atlas, ramp));
    }
    return out;
}

// The working palette's four colors, abutting down the right of the shade rows.
Regions ShadeBand(const ShadeRamp& working) {
    const std::array<retropp::Rgba8, 4> colors{working.darkest, working.dark, working.light,
                                               working.lightest};
    Regions out;
    out.reserve(colors.size());

    const auto left  = static_cast<float>(kBandX);
    const auto right = static_cast<float>(kBandX + kBandWidth);
    for (std::size_t i = 0; i < colors.size(); ++i) {
        const auto top    = static_cast<float>(entryY(i));
        const auto bottom = top + static_cast<float>(kEntryPitch);

        retropp::Rgba8 fill = colors[i];
        fill.a              = 255;
        out.push_back(retropp::Region{
            .key   = retropp::ObjectKey{"editor-shade-" + std::to_string(i)},
            .shape = retropp::ShapePoints{
                .points = {{left, top}, {right, top}, {right, bottom}, {left, bottom}}},
            .effects = {retropp::ScreenSpaceEffect{
                .kind = retropp::ScreenSpaceEffectKind::ColorFill, .fill = fill}},
        });
    }
    return out;
}

// The heading, and the asterisk after it while the working palette has unsaved changes. The editor and
// the held preview both draw it from here.
Sprites Heading(const kirpich::CustomPalettes& palettes, const TileAtlas& atlas,
                std::uint8_t ramp) {
    Sprites out = Glyphs(kHeading, kHeadingX, kHeadingY, kGlyph, atlas, ramp);
    if (kirpich::hasUnsavedChanges(palettes)) {
        const Sprites asterisk = Glyphs("*", kAsteriskX, kHeadingY, kGlyph, atlas, ramp);
        out.insert(out.end(), asterisk.begin(), asterisk.end());
    }
    return out;
}

// The held preview: the heading, the three words and the seven pieces, all through the custom set.
retropp::DrawLayer Preview(const kirpich::CustomPalettes& palettes, const TileAtlas& atlas) {
    return SpriteLayer("editor-preview", kContentZ, {
        Heading(palettes, atlas, kCustomRamp),
        PalettePreview(atlas, kCustomRamp),
    });
}

retropp::DrawLayer Editor(const kirpich::PaletteEditorState& ui,
                          const kirpich::CustomPalettes& palettes, bool blinkOn,
                          const TileAtlas& atlas, std::uint8_t ramp) {
    return SpriteLayer("editor-content", kContentZ, {
        PageArrows("editor", /*up=*/true, /*down=*/false, atlas, ramp),
        Heading(palettes, atlas, ramp),
        Glyphs("preset", kLabelX, kPresetY, kGlyph, atlas, ramp),
        PresetNumber(ui.preset, palettes, atlas, ramp),
        systems::presetCanStep(ui.preset, &palettes, -1)
            ? Arrow("preset-left", kPresetLeftArrowX, kPresetY, ArrowDirection::LEFT, kArrowZ,
                    atlas, ramp)
            : Sprites{},
        systems::presetCanStep(ui.preset, &palettes, +1)
            ? Arrow("preset-right", kPresetRightArrowX, kPresetY, ArrowDirection::RIGHT, kArrowZ,
                    atlas, ramp)
            : Sprites{},
        ActionLabel("load preset", PaletteEditorRow::LOAD, ui.preset, palettes, atlas, ramp),
        ActionLabel("save preset", PaletteEditorRow::SAVE, ui.preset, palettes, atlas, ramp),
        ActionLabel("clear preset", PaletteEditorRow::CLEAR, ui.preset, palettes, atlas, ramp),
        ShadeRow(0, ui, palettes, atlas, ramp),
        ShadeRow(1, ui, palettes, atlas, ramp),
        ShadeRow(2, ui, palettes, atlas, ramp),
        ShadeRow(3, ui, palettes, atlas, ramp),
        RowCursor(ui, blinkOn, atlas, ramp),
        ValueSelector(ui, palettes, atlas, ramp),
        ui.entering ? Glyphs(kHint, kHintX, kHintY, kGlyph, atlas, ramp) : Sprites{},
    }, LayerOptions{.regions = ShadeBand(palettes.working)});
}

}  // namespace

Layers PaletteEditorScreen(const kirpich::PaletteEditorState& ui, const kirpich::Settings& s,
                           const kirpich::CustomPalettes& palettes, bool blinkOn,
                           const TileAtlas& atlas) {
    // The editor draws through the built-in ramp underneath, never the custom set, so stepping a
    // value never recolors the editor itself. The held preview is the one part drawn through the
    // custom set: it is where the palette is judged.
    const std::uint8_t ramp = clampShadeRamp(s.shadeRamp);
    return {
        ui.previewing ? BackgroundLayer("editor-backdrop", 0, PreviewBackdrop(atlas, kCustomRamp))
                      : BackgroundLayer("editor-backdrop", 0, Backdrop(atlas, ramp)),
        ui.previewing ? Preview(palettes, atlas)
        : ui.question != PaletteEditorQuestion::NONE
            ? YesNoQuestion("editor-question", questionTitle(ui.question),
                            questionFirstLine(ui.question),
                            questionSecondLine(ui.question, ui.preset), ui.answerYes, blinkOn,
                            atlas, ramp)
            : Editor(ui, palettes, blinkOn, atlas, ramp),
    };
}

}  // namespace kirpich::render
