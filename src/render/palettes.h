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
inline constexpr std::array<ShadeRamp, 32> kShadeRamps{{
    // 1 - greyscale. The hardware's four shades as the port has always drawn them.
    {.darkest  = {.r =   0, .g =   0, .b =   0},
     .dark     = {.r =  85, .g =  85, .b =  85},
     .light    = {.r = 170, .g = 170, .b = 170},
     .lightest = {.r = 255, .g = 255, .b = 255}},

    // 2 - the green of the original handheld's screen.
    {.darkest  = {.r =  15, .g =  56, .b =  15},
     .dark     = {.r =  48, .g =  98, .b =  48},
     .light    = {.r = 139, .g = 172, .b =  15},
     .lightest = {.r = 155, .g = 188, .b =  15}},

    // 3 - amber, the way an old monitor burned.
    {.darkest  = {.r =  26, .g =  13, .b =   0},
     .dark     = {.r = 107, .g =  56, .b =   0},
     .light    = {.r = 200, .g = 122, .b =  10},
     .lightest = {.r = 255, .g = 199, .b =  74}},

    // 4 - dusk, a cold blue.
    {.darkest  = {.r =  11, .g =  16, .b =  38},
     .dark     = {.r =  42, .g =  58, .b = 107},
     .light    = {.r =  92, .g = 127, .b = 196},
     .lightest = {.r = 194, .g = 224, .b = 255}},

    // 5 - brick, after the name this port carries.
    {.darkest  = {.r =  43, .g =  12, .b =  10},
     .dark     = {.r = 122, .g =  40, .b =  30},
     .light    = {.r = 192, .g =  91, .b =  60},
     .lightest = {.r = 242, .g = 201, .b = 168}},

    // 6 - a plum ultraviolet.
    {.darkest  = {.r =  22, .g =   8, .b =  43},
     .dark     = {.r =  75, .g =  29, .b = 119},
     .light    = {.r = 155, .g =  83, .b = 196},
     .lightest = {.r = 232, .g = 198, .b = 247}},

    // 7 - sea, a blue-green with a pale sand at the top.
    {.darkest  = {.r =   4, .g =  36, .b =  40},
     .dark     = {.r =  17, .g = 100, .b = 102},
     .light    = {.r =  63, .g = 178, .b = 161},
     .lightest = {.r = 228, .g = 247, .b = 218}},

    // 8 - ink, a near-black blue under a cold white.
    {.darkest  = {.r =   8, .g =  10, .b =  16},
     .dark     = {.r =  44, .g =  51, .b =  68},
     .light    = {.r = 116, .g = 130, .b = 155},
     .lightest = {.r = 233, .g = 239, .b = 247}},

    // 9 - cherry, a deep red rising to pink.
    {.darkest  = {.r =  43, .g =   5, .b =  18},
     .dark     = {.r = 122, .g =  15, .b =  46},
     .light    = {.r = 196, .g =  58, .b =  91},
     .lightest = {.r = 247, .g = 201, .b = 212}},

    // 10 - moss, olive under a pale khaki.
    {.darkest  = {.r =  20, .g =  26, .b =  10},
     .dark     = {.r =  62, .g =  74, .b =  28},
     .light    = {.r = 126, .g = 143, .b =  60},
     .lightest = {.r = 216, .g = 224, .b = 168}},

    // 11 - sepia, brown to cream.
    {.darkest  = {.r =  35, .g =  26, .b =  18},
     .dark     = {.r =  94, .g =  70, .b =  50},
     .light    = {.r = 169, .g = 138, .b =  99},
     .lightest = {.r = 240, .g = 226, .b = 200}},

    // 12 - sunset, purple through red to a warm gold.
    {.darkest  = {.r =  42, .g =  16, .b =  56},
     .dark     = {.r = 122, .g =  42, .b =  92},
     .light    = {.r = 217, .g =  97, .b =  74},
     .lightest = {.r = 255, .g = 217, .b = 160}},

    // 13 - ice, deep water under a pale cyan.
    {.darkest  = {.r =   6, .g =  30, .b =  46},
     .dark     = {.r =  23, .g =  84, .b = 119},
     .light    = {.r =  89, .g = 168, .b = 196},
     .lightest = {.r = 223, .g = 245, .b = 255}},

    // 14 - forest, near-black green to a sunlit leaf.
    {.darkest  = {.r =   8, .g =  28, .b =  16},
     .dark     = {.r =  31, .g =  77, .b =  42},
     .light    = {.r =  84, .g = 150, .b =  79},
     .lightest = {.r = 203, .g = 232, .b = 166}},

    // 15 - wine, a dark red with the colour drained out of the top.
    {.darkest  = {.r =  30, .g =  10, .b =  14},
     .dark     = {.r =  90, .g =  30, .b =  44},
     .light    = {.r = 158, .g =  90, .b =  98},
     .lightest = {.r = 228, .g = 207, .b = 203}},

    // 16 - slate, cool greys with a blue cast.
    {.darkest  = {.r =  26, .g =  31, .b =  38},
     .dark     = {.r =  69, .g =  80, .b =  94},
     .light    = {.r = 138, .g = 152, .b = 168},
     .lightest = {.r = 221, .g = 228, .b = 236}},

    // 17 - lime, an acid green over near-black.
    {.darkest  = {.r =  12, .g =  18, .b =   4},
     .dark     = {.r =  56, .g =  84, .b =  12},
     .light    = {.r = 134, .g = 196, .b =  28},
     .lightest = {.r = 230, .g = 255, .b = 138}},

    // 18 - copper, dark metal to a bright tarnish.
    {.darkest  = {.r =  32, .g =  15, .b =   8},
     .dark     = {.r = 110, .g =  51, .b =  24},
     .light    = {.r = 194, .g = 107, .b =  46},
     .lightest = {.r = 248, .g = 210, .b = 154}},

    // 19 - lavender, indigo under a pale lilac.
    {.darkest  = {.r =  28, .g =  22, .b =  52},
     .dark     = {.r =  74, .g =  66, .b = 128},
     .light    = {.r = 146, .g = 138, .b = 200},
     .lightest = {.r = 230, .g = 224, .b = 248}},

    // 20 - mint, dark green-blue to a pale wash.
    {.darkest  = {.r =   7, .g =  34, .b =  30},
     .dark     = {.r =  29, .g =  94, .b =  80},
     .light    = {.r =  92, .g = 184, .b = 156},
     .lightest = {.r = 220, .g = 248, .b = 232}},

    // 21 - magenta, a hot pink over deep purple.
    {.darkest  = {.r =  30, .g =   4, .b =  28},
     .dark     = {.r = 110, .g =  18, .b =  94},
     .light    = {.r = 200, .g =  64, .b = 166},
     .lightest = {.r = 250, .g = 207, .b = 238}},

    // 22 - gold, dark bronze to a bright yellow.
    {.darkest  = {.r =  28, .g =  20, .b =   2},
     .dark     = {.r =  99, .g =  74, .b =  10},
     .light    = {.r = 198, .g = 158, .b =  30},
     .lightest = {.r = 252, .g = 236, .b = 158}},

    // 23 - storm, a low-contrast blue-grey that barely separates.
    {.darkest  = {.r =  34, .g =  39, .b =  44},
     .dark     = {.r =  78, .g =  87, .b =  96},
     .light    = {.r = 126, .g = 137, .b = 148},
     .lightest = {.r = 180, .g = 190, .b = 200}},

    // 24 - aurora, indigo through teal to a pale green.
    {.darkest  = {.r =  16, .g =  12, .b =  48},
     .dark     = {.r =  28, .g =  80, .b = 118},
     .light    = {.r =  62, .g = 174, .b = 160},
     .lightest = {.r = 214, .g = 246, .b = 196}},

    // 25-32 are the colour schemes Windows 3.1 shipped in its Control Panel, named as it named them.
    // They are louder than the twenty-four above and that is the point - they were designed for a
    // 16-colour VGA palette by people with sixteen colours to spend, and several of them commit
    // entirely to one idea. The four shades are chosen to run dark to light like every other ramp, so
    // a scheme's character survives being reduced to a ramp even where its own screen used more.

    // 25 - hot dog stand, the one everybody remembers and nobody chose twice.
    {.darkest  = {.r =   0, .g =   0, .b =   0},
     .dark     = {.r = 255, .g =   0, .b =   0},
     .light    = {.r = 255, .g = 255, .b =   0},
     .lightest = {.r = 255, .g = 255, .b = 255}},

    // 26 - bordeaux, a wine-dark purple rising to a dusty blush.
    {.darkest  = {.r =  43, .g =  10, .b =  30},
     .dark     = {.r = 122, .g =  18, .b =  71},
     .light    = {.r = 176, .g =  74, .b = 122},
     .lightest = {.r = 232, .g = 196, .b = 216}},

    // 27 - emerald city, a saturated green with nothing muted about it.
    {.darkest  = {.r =   4, .g =  42, .b =  18},
     .dark     = {.r =  14, .g = 122, .b =  46},
     .light    = {.r =  53, .g = 194, .b =  92},
     .lightest = {.r = 207, .g = 240, .b = 212}},

    // 28 - arizona, desert browns under the turquoise the scheme is remembered for.
    {.darkest  = {.r =  58, .g =  30, .b =  12},
     .dark     = {.r = 156, .g =  74, .b =  24},
     .light    = {.r = 216, .g = 154, .b =  76},
     .lightest = {.r = 127, .g = 224, .b = 212}},

    // 29 - fluorescent, magenta through cyan to acid. It does not blend and was never meant to.
    {.darkest  = {.r =  20, .g =   0, .b =  30},
     .dark     = {.r = 179, .g =   0, .b = 166},
     .light    = {.r =   0, .g = 229, .b = 208},
     .lightest = {.r = 234, .g = 255, .b = 107}},

    // 30 - plasma power saver, black through magenta to a hot orange.
    {.darkest  = {.r =  16, .g =   0, .b =  16},
     .dark     = {.r = 110, .g =  11, .b =  82},
     .light    = {.r = 214, .g =  59, .b = 107},
     .lightest = {.r = 255, .g = 184, .b = 107}},

    // 31 - pastel, the one ramp here that never goes near black: mauve to a warm white.
    {.darkest  = {.r = 110, .g =  90, .b = 120},
     .dark     = {.r = 169, .g = 143, .b = 196},
     .light    = {.r = 201, .g = 226, .b = 192},
     .lightest = {.r = 251, .g = 242, .b = 228}},

    // 32 - black leather jacket, near-black up to chrome.
    {.darkest  = {.r =   5, .g =   5, .b =  10},
     .dark     = {.r =  31, .g =  34, .b =  41},
     .light    = {.r =  94, .g = 101, .b = 112},
     .lightest = {.r = 198, .g = 203, .b = 210}},
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
