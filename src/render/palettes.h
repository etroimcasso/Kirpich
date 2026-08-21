#pragma once

// The colour ramps the game can be drawn in.
//
// The Game Boy draws everything through four shades. The port keeps that shape and only changes what
// the four shades ARE: a ramp is four colours, darkest first, and every palette the renderer uploads
// is built from one of them (src/render/tile_atlas.h). Nothing about the art changes - a tile still
// stores the same sample per pixel - so switching ramps recolours the whole game at once and cannot
// alter a single pixel's shape.
//
// Ramp 0 is the greyscale the hardware's own shades map to, and it is the default: a player who never
// opens the settings screen sees exactly what they saw before ramps existed.
//
// Darkest first is the port's order throughout, because the extractor's decode inverts (see
// docs/contracts/tile-graphics.md). Objects take the same ramp with its LAST entry made see-through,
// which is the hardware's rule - an object's lightest colour is transparency, not a shade - so a ramp
// contributes three visible colours to a sprite and all four to the background.

#include <array>
#include <cstddef>
#include <cstdint>

#include <retropp/palette.h>  // Rgba8

namespace kirpich::render {

// Four colours, darkest to lightest.
struct ShadeRamp {
    retropp::Rgba8 darkest{};
    retropp::Rgba8 dark{};
    retropp::Rgba8 light{};
    retropp::Rgba8 lightest{};

    friend constexpr bool operator==(const ShadeRamp&, const ShadeRamp&) = default;
};

// The ramps, in the order the settings screen numbers them: the screen shows 1 for the first.
inline constexpr std::array<ShadeRamp, 12> kShadeRamps{{
    // 1 - greyscale. The hardware's four shades as the port has always drawn them.
    {.darkest  = {.r = 0x00, .g = 0x00, .b = 0x00},
     .dark     = {.r = 0x55, .g = 0x55, .b = 0x55},
     .light    = {.r = 0xAA, .g = 0xAA, .b = 0xAA},
     .lightest = {.r = 0xFF, .g = 0xFF, .b = 0xFF}},

    // 2 - the green of the original handheld's screen.
    {.darkest  = {.r = 0x0F, .g = 0x38, .b = 0x0F},
     .dark     = {.r = 0x30, .g = 0x62, .b = 0x30},
     .light    = {.r = 0x8B, .g = 0xAC, .b = 0x0F},
     .lightest = {.r = 0x9B, .g = 0xBC, .b = 0x0F}},

    // 3 - amber, the way an old monitor burned.
    {.darkest  = {.r = 0x1A, .g = 0x0D, .b = 0x00},
     .dark     = {.r = 0x6B, .g = 0x38, .b = 0x00},
     .light    = {.r = 0xC8, .g = 0x7A, .b = 0x0A},
     .lightest = {.r = 0xFF, .g = 0xC7, .b = 0x4A}},

    // 4 - dusk, a cold blue.
    {.darkest  = {.r = 0x0B, .g = 0x10, .b = 0x26},
     .dark     = {.r = 0x2A, .g = 0x3A, .b = 0x6B},
     .light    = {.r = 0x5C, .g = 0x7F, .b = 0xC4},
     .lightest = {.r = 0xC2, .g = 0xE0, .b = 0xFF}},

    // 5 - brick, after the name this port carries.
    {.darkest  = {.r = 0x2B, .g = 0x0C, .b = 0x0A},
     .dark     = {.r = 0x7A, .g = 0x28, .b = 0x1E},
     .light    = {.r = 0xC0, .g = 0x5B, .b = 0x3C},
     .lightest = {.r = 0xF2, .g = 0xC9, .b = 0xA8}},

    // 6 - a plum ultraviolet.
    {.darkest  = {.r = 0x16, .g = 0x08, .b = 0x2B},
     .dark     = {.r = 0x4B, .g = 0x1D, .b = 0x77},
     .light    = {.r = 0x9B, .g = 0x53, .b = 0xC4},
     .lightest = {.r = 0xE8, .g = 0xC6, .b = 0xF7}},

    // 7 - sea, a blue-green with a pale sand at the top.
    {.darkest  = {.r = 0x04, .g = 0x24, .b = 0x28},
     .dark     = {.r = 0x11, .g = 0x64, .b = 0x66},
     .light    = {.r = 0x3F, .g = 0xB2, .b = 0xA1},
     .lightest = {.r = 0xE4, .g = 0xF7, .b = 0xDA}},

    // 8 - ink, a near-black blue under a cold white.
    {.darkest  = {.r = 0x08, .g = 0x0A, .b = 0x10},
     .dark     = {.r = 0x2C, .g = 0x33, .b = 0x44},
     .light    = {.r = 0x74, .g = 0x82, .b = 0x9B},
     .lightest = {.r = 0xE9, .g = 0xEF, .b = 0xF7}},

    // 9 - cherry, a deep red rising to pink.
    {.darkest  = {.r = 0x2B, .g = 0x05, .b = 0x12},
     .dark     = {.r = 0x7A, .g = 0x0F, .b = 0x2E},
     .light    = {.r = 0xC4, .g = 0x3A, .b = 0x5B},
     .lightest = {.r = 0xF7, .g = 0xC9, .b = 0xD4}},

    // 10 - moss, olive under a pale khaki.
    {.darkest  = {.r = 0x14, .g = 0x1A, .b = 0x0A},
     .dark     = {.r = 0x3E, .g = 0x4A, .b = 0x1C},
     .light    = {.r = 0x7E, .g = 0x8F, .b = 0x3C},
     .lightest = {.r = 0xD8, .g = 0xE0, .b = 0xA8}},

    // 11 - sepia, brown to cream.
    {.darkest  = {.r = 0x23, .g = 0x1A, .b = 0x12},
     .dark     = {.r = 0x5E, .g = 0x46, .b = 0x32},
     .light    = {.r = 0xA9, .g = 0x8A, .b = 0x63},
     .lightest = {.r = 0xF0, .g = 0xE2, .b = 0xC8}},

    // 12 - sunset, purple through red to a warm gold.
    {.darkest  = {.r = 0x2A, .g = 0x10, .b = 0x38},
     .dark     = {.r = 0x7A, .g = 0x2A, .b = 0x5C},
     .light    = {.r = 0xD9, .g = 0x61, .b = 0x4A},
     .lightest = {.r = 0xFF, .g = 0xD9, .b = 0xA0}},
}};

// How many ramps there are, and which one a build draws in until a player says otherwise.
inline constexpr std::size_t  kShadeRampCount   = kShadeRamps.size();
inline constexpr std::uint8_t kDefaultShadeRamp = 0;

// Bring a stored ramp number into the range this build offers, the same way the window scale is
// clamped: a file written by a build that offered more ramps costs the player that one value.
[[nodiscard]] constexpr std::uint8_t clampShadeRamp(int ramp) noexcept {
    if (ramp < 0) return 0;
    if (ramp >= static_cast<int>(kShadeRampCount)) {
        return static_cast<std::uint8_t>(kShadeRampCount - 1);
    }
    return static_cast<std::uint8_t>(ramp);
}

// The ramp's four colours in order, for anything that wants to show them rather than draw through
// them - the settings screen's preview reads them this way.
[[nodiscard]] constexpr std::array<retropp::Rgba8, 4> rampColours(std::uint8_t ramp) noexcept {
    const ShadeRamp& r = kShadeRamps[clampShadeRamp(ramp)];
    return {r.darkest, r.dark, r.light, r.lightest};
}

}  // namespace kirpich::render
