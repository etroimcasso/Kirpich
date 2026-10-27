#pragma once

// The palette rotations the game ships: ten palettes each, level 0 first, which a round is drawn
// through while Settings::paletteRotation names the preset (src/systems/frame_palette.h).
//
// A preset's palettes are its own colors, authored here, not picks from the ramp table: nothing
// requires an entry to match a built-in ramp. Each entry runs dark to light by Rec.601 luminance, as
// every ramp does, and tests/test_rotation_presets.cpp holds every entry to that and holds each
// preset to the direction its name promises.
//
// To add a preset: add its table here, an enumerator to PaletteRotation (src/state/settings.h) before
// CUSTOM, and a line to presetRotation.

#include <array>

#include "render/palettes.h"              // ShadeRamp
#include "state/rotation_progression.h"   // kRotationLevelCount
#include "state/settings.h"               // PaletteRotation

namespace kirpich::render {

// Ten palettes, one per level modulo ten.
using RotationPalettes = std::array<ShadeRamp, kRotationLevelCount>;

// Hotter: warm cream at level 0 to an intense red at level 9. The darkest shade's red rises and the
// dark shade's green falls, level over level.
inline constexpr RotationPalettes kHotterRotation{{
    // level 0
    {.darkest  = {.r =  60, .g =  40, .b =  20},
     .dark     = {.r = 140, .g = 106, .b =  52},
     .light    = {.r = 210, .g = 180, .b = 120},
     .lightest = {.r = 252, .g = 240, .b = 210}},
    // level 1
    {.darkest  = {.r =  72, .g =  38, .b =  16},
     .dark     = {.r = 160, .g = 100, .b =  40},
     .light    = {.r = 222, .g = 176, .b = 104},
     .lightest = {.r = 253, .g = 236, .b = 200}},
    // level 2
    {.darkest  = {.r =  84, .g =  36, .b =  14},
     .dark     = {.r = 178, .g =  96, .b =  34},
     .light    = {.r = 232, .g = 170, .b =  92},
     .lightest = {.r = 254, .g = 232, .b = 192}},
    // level 3
    {.darkest  = {.r =  96, .g =  32, .b =  12},
     .dark     = {.r = 194, .g =  90, .b =  30},
     .light    = {.r = 240, .g = 162, .b =  80},
     .lightest = {.r = 255, .g = 228, .b = 184}},
    // level 4
    {.darkest  = {.r = 108, .g =  28, .b =  12},
     .dark     = {.r = 208, .g =  82, .b =  28},
     .light    = {.r = 246, .g = 150, .b =  70},
     .lightest = {.r = 255, .g = 222, .b = 176}},
    // level 5
    {.darkest  = {.r = 120, .g =  24, .b =  12},
     .dark     = {.r = 220, .g =  72, .b =  26},
     .light    = {.r = 250, .g = 138, .b =  62},
     .lightest = {.r = 255, .g = 216, .b = 168}},
    // level 6
    {.darkest  = {.r = 132, .g =  20, .b =  14},
     .dark     = {.r = 230, .g =  62, .b =  26},
     .light    = {.r = 252, .g = 124, .b =  58},
     .lightest = {.r = 255, .g = 208, .b = 160}},
    // level 7
    {.darkest  = {.r = 144, .g =  16, .b =  16},
     .dark     = {.r = 238, .g =  52, .b =  28},
     .light    = {.r = 254, .g = 110, .b =  56},
     .lightest = {.r = 255, .g = 200, .b = 152}},
    // level 8
    {.darkest  = {.r = 156, .g =  12, .b =  18},
     .dark     = {.r = 244, .g =  42, .b =  32},
     .light    = {.r = 255, .g =  96, .b =  56},
     .lightest = {.r = 255, .g = 190, .b = 146}},
    // level 9
    {.darkest  = {.r = 168, .g =   8, .b =  20},
     .dark     = {.r = 250, .g =  32, .b =  36},
     .light    = {.r = 255, .g =  82, .b =  58},
     .lightest = {.r = 255, .g = 180, .b = 140}},
}};

// Colder: cool slate at level 0 to a deep blue at level 9. The darkest shade's blue rises and the
// dark shade's red falls, level over level.
inline constexpr RotationPalettes kColderRotation{{
    // level 0
    {.darkest  = {.r =  24, .g =  40, .b =  60},
     .dark     = {.r =  70, .g = 110, .b = 140},
     .light    = {.r = 140, .g = 180, .b = 210},
     .lightest = {.r = 220, .g = 236, .b = 250}},
    // level 1
    {.darkest  = {.r =  20, .g =  40, .b =  70},
     .dark     = {.r =  60, .g = 108, .b = 150},
     .light    = {.r = 130, .g = 176, .b = 216},
     .lightest = {.r = 214, .g = 232, .b = 250}},
    // level 2
    {.darkest  = {.r =  16, .g =  40, .b =  80},
     .dark     = {.r =  52, .g = 104, .b = 160},
     .light    = {.r = 120, .g = 172, .b = 222},
     .lightest = {.r = 208, .g = 228, .b = 250}},
    // level 3
    {.darkest  = {.r =  14, .g =  38, .b =  90},
     .dark     = {.r =  46, .g = 100, .b = 170},
     .light    = {.r = 110, .g = 166, .b = 228},
     .lightest = {.r = 202, .g = 224, .b = 250}},
    // level 4
    {.darkest  = {.r =  12, .g =  36, .b = 100},
     .dark     = {.r =  40, .g =  94, .b = 180},
     .light    = {.r = 100, .g = 160, .b = 234},
     .lightest = {.r = 196, .g = 220, .b = 250}},
    // level 5
    {.darkest  = {.r =  10, .g =  32, .b = 110},
     .dark     = {.r =  36, .g =  88, .b = 190},
     .light    = {.r =  90, .g = 152, .b = 240},
     .lightest = {.r = 190, .g = 216, .b = 250}},
    // level 6
    {.darkest  = {.r =   8, .g =  28, .b = 120},
     .dark     = {.r =  32, .g =  80, .b = 200},
     .light    = {.r =  82, .g = 144, .b = 244},
     .lightest = {.r = 184, .g = 212, .b = 250}},
    // level 7
    {.darkest  = {.r =   8, .g =  24, .b = 130},
     .dark     = {.r =  28, .g =  72, .b = 210},
     .light    = {.r =  74, .g = 134, .b = 248},
     .lightest = {.r = 178, .g = 208, .b = 250}},
    // level 8
    {.darkest  = {.r =   6, .g =  20, .b = 140},
     .dark     = {.r =  24, .g =  64, .b = 220},
     .light    = {.r =  66, .g = 124, .b = 250},
     .lightest = {.r = 172, .g = 204, .b = 250}},
    // level 9
    {.darkest  = {.r =   6, .g =  16, .b = 150},
     .dark     = {.r =  20, .g =  56, .b = 230},
     .light    = {.r =  58, .g = 114, .b = 252},
     .lightest = {.r = 166, .g = 200, .b = 250}},
}};

// Rainbow: one hue step of 36 degrees per level from red, each hue at the same four lightness tiers.
inline constexpr RotationPalettes kRainbowRotation{{
    // level 0 - red
    {.darkest  = {.r = 120, .g =  16, .b =  16},
     .dark     = {.r = 210, .g =  40, .b =  40},
     .light    = {.r = 250, .g = 128, .b = 120},
     .lightest = {.r = 255, .g = 210, .b = 205}},
    // level 1 - orange
    {.darkest  = {.r = 100, .g =  44, .b =   8},
     .dark     = {.r = 200, .g = 100, .b =  20},
     .light    = {.r = 250, .g = 170, .b =  90},
     .lightest = {.r = 255, .g = 222, .b = 180}},
    // level 2 - yellow
    {.darkest  = {.r =  70, .g =  64, .b =   8},
     .dark     = {.r = 150, .g = 138, .b =  20},
     .light    = {.r = 220, .g = 204, .b =  80},
     .lightest = {.r = 252, .g = 244, .b = 170}},
    // level 3 - chartreuse
    {.darkest  = {.r =  48, .g =  72, .b =  10},
     .dark     = {.r = 104, .g = 150, .b =  24},
     .light    = {.r = 180, .g = 220, .b =  90},
     .lightest = {.r = 230, .g = 250, .b = 180}},
    // level 4 - green
    {.darkest  = {.r =  16, .g =  72, .b =  30},
     .dark     = {.r =  40, .g = 150, .b =  70},
     .light    = {.r = 120, .g = 220, .b = 150},
     .lightest = {.r = 200, .g = 250, .b = 215}},
    // level 5 - teal
    {.darkest  = {.r =  10, .g =  70, .b =  72},
     .dark     = {.r =  24, .g = 140, .b = 146},
     .light    = {.r = 110, .g = 215, .b = 220},
     .lightest = {.r = 200, .g = 245, .b = 248}},
    // level 6 - azure
    {.darkest  = {.r =  12, .g =  50, .b =  96},
     .dark     = {.r =  30, .g = 110, .b = 190},
     .light    = {.r = 120, .g = 180, .b = 240},
     .lightest = {.r = 205, .g = 228, .b = 252}},
    // level 7 - blue
    {.darkest  = {.r =  24, .g =  28, .b = 110},
     .dark     = {.r =  60, .g =  70, .b = 200},
     .light    = {.r = 140, .g = 150, .b = 245},
     .lightest = {.r = 210, .g = 214, .b = 252}},
    // level 8 - violet
    {.darkest  = {.r =  70, .g =  20, .b = 110},
     .dark     = {.r = 140, .g =  50, .b = 200},
     .light    = {.r = 200, .g = 140, .b = 245},
     .lightest = {.r = 236, .g = 210, .b = 252}},
    // level 9 - magenta
    {.darkest  = {.r = 110, .g =  16, .b =  70},
     .dark     = {.r = 200, .g =  40, .b = 140},
     .light    = {.r = 250, .g = 130, .b = 200},
     .lightest = {.r = 255, .g = 210, .b = 235}},
}};

// The preset a mode names, or nothing for OFF and CUSTOM.
[[nodiscard]] constexpr const RotationPalettes* presetRotation(PaletteRotation mode) noexcept {
    switch (mode) {
        case PaletteRotation::HOTTER:
            return &kHotterRotation;
        case PaletteRotation::COLDER:
            return &kColderRotation;
        case PaletteRotation::RAINBOW:
            return &kRainbowRotation;
        case PaletteRotation::OFF:
        case PaletteRotation::CUSTOM:
            break;
    }
    return nullptr;
}

}  // namespace kirpich::render
