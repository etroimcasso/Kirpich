#include "render/sprites.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace kirpich::render {

namespace {

// An object's name, in two halves, each doing a different job.
//
// The first half says which object this is, so that two objects in one frame are never confused for
// each other: which descriptor drew it, which sprite that descriptor was drawing, and which part of
// that sprite this is (the renderer recorded all three - systems/oam_source.h). Naming an object
// after the buffer entry it landed in would not do - entries are a shared resource, a wider sprite
// pushes everything after it along, and a screen change refills the buffer entirely.
//
// The second half changes every tick, and it is there to SUPPRESS easing. The engine matches an
// object to its previous frame by name and glides it between the two positions, which is right for
// a machine whose objects move continuously. Nothing here does: every object sits on the 8-pixel
// grid and moves a whole step at a time, once a tick - a piece is never between two rows, a cursor
// never between two cells. A name the renderer has not seen before cannot be matched against
// anything, so every object draws where it is rather than sliding in from where it was.
//
// The tag counts rather than alternates, and that distinction is the whole of it. The renderer
// forgets a name the moment a submission arrives without it, so a stale position survives exactly
// one submission - and one submission can cover several ticks, since the loop runs as many as the
// elapsed time earned before it draws. A tag that merely alternated would come back around on any
// two-tick frame and hand the renderer the name it saw last time, with two ticks of movement in
// between: the object glides instead of landing.
//
// So the tag has to stay clear of itself for as many ticks as one submission can span, which the
// loop bounds at fourteen (it caps catch-up at a quarter second of simulation). The counter is
// sixteen bits - far past that bound, and the width is free. The wrap is harmless: returning to a
// value would take a frame that spanned the counter's whole range, which the cap forbids. A count
// is also what a random tag is not - random repeats by chance, a count cannot repeat inside its
// period, and the port keeps its randomness in the machine rather than in the drawing.
//
// A frame that spans no ticks at all reuses the tag, which is right: nothing moved, so there is
// nothing to ease, and the match finds a position identical to the one it already has.
//
// It is done per object rather than by turning the engine's interpolation off, because that switch
// is also what holds the frame rate steady on a display refreshing faster than the simulation ticks.
// Easing is unwanted here; the pacing it comes with is not.
//
// Composed rather than looked up, because it says something that changes; short enough to stay
// inside the string's own storage, so it does not reach the heap.
std::string tickSuffix(std::uint16_t tick) {
    return "#" + std::to_string(tick);
}

std::string drawnKey(const OamSource& src, std::uint16_t tick) {
    return "s" + std::to_string(src.slot) + "." +
           std::to_string(static_cast<unsigned>(src.sprite)) + "." + std::to_string(src.part) +
           tickSuffix(tick);
}

// An entry the renderer never wrote is one the game filled in itself, always at a fixed place, so
// the entry is what identifies it.
std::string directKey(std::size_t index, std::uint16_t tick) {
    return "oam" + std::to_string(index) + tickSuffix(tick);
}

// Whether an entry puts any pixel on the screen. The buffer's coordinates are offset from the
// screen's, so an untouched entry sits above and left of the first pixel and a hidden one below the
// last - both draw nothing, and neither is submitted. An object that draws nothing has no business
// having a position the next frame can be eased from.
bool onScreen(int x, int y) {
    return x > -kSpriteSizePx && x < kScreenWidthPx && y > -kSpriteSizePx && y < kScreenHeightPx;
}

}  // namespace

std::optional<retropp::Sprite> oamEntrySprite(const EngineState& engine,
                                              const OamSourceTable& sources, std::size_t index,
                                              TileSheet sheet, std::uint16_t tick,
                                              const TileAtlas& atlas, std::uint8_t ramp,
                                              bool includeOffScreen) {
    if (index >= engine.oam.size()) {
        return std::nullopt;
    }
    const OamEntry& entry = engine.oam[index];

    const int x = static_cast<int>(entry.x) - kSpriteScreenOffsetX;
    const int y = static_cast<int>(entry.y) - kSpriteScreenOffsetY;
    if (!includeOffScreen && !onScreen(x, y)) {
        return std::nullopt;
    }

    const OamSource&   src = sources.entries[index];
    const ResolvedTile art = resolveSpriteTile(entry.tile, sheet, entry.palette1, atlas, ramp);

    return retropp::Sprite{
        .key = src.drawn ? drawnKey(src, tick) : directKey(index, tick),
        .x   = x,
        .y   = y,
        // Earlier entries draw over later ones, which is how the hardware broke a tie between
        // two objects at the same place. Ascending z draws back to front, so the order reverses
        // here. (The hardware's other rule - that a further-left object wins outright, whatever
        // its entry - is not reproduced; see the header.)
        .z       = static_cast<std::int32_t>(engine.oam.size() - 1 - index),
        .atlas   = art.atlas,
        .tile    = art.cell,
        .palette = art.palette,
        .flipX   = entry.xflip,
        .flipY   = entry.yflip,
    };
}

void composeSprites(const EngineState& engine, const OamSourceTable& sources, TileSheet sheet,
                    std::uint16_t tick, const TileAtlas& atlas,
                    std::vector<retropp::Sprite>& sprites, std::uint8_t ramp) {
    sprites.clear();
    sprites.reserve(engine.oam.size());

    for (std::size_t i = 0; i < engine.oam.size(); ++i) {
        if (auto sprite = oamEntrySprite(engine, sources, i, sheet, tick, atlas, ramp)) {
            sprites.push_back(std::move(*sprite));
        }
    }
}

retropp::DrawLayer spriteLayer(const std::vector<retropp::Sprite>& sprites,
                               retropp::ViewportResolution viewport) {
    return retropp::DrawLayer{
        .key = kSpriteLayerKey,
        .z   = kSpriteLayerZ,
        // The objects are placed in screen coordinates and the game never scrolls, so the layer is
        // the viewport, parked at the origin - the same shape the background layer takes.
        .size    = viewport.size(),
        .scroll  = retropp::LayerScroll{},
        .content = retropp::SpriteContent{.sprites = std::span<const retropp::Sprite>(sprites)},
    };
}

}  // namespace kirpich::render
