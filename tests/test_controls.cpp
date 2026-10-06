// The player's controls and what they mean to the game — behavioral tests over src/state/controls.h
// and src/systems/controls.h.
//
// Device-free except the store case, which runs through a hermetic SaveStore rooted at a temporary
// directory. The controls are the port's own, so every asserted value comes from the surface's stated
// contract; the default controls are held to the same 26 rows tests/test_input.cpp pins for
// defaultActionMap.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <span>
#include <tuple>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include <kirpich/action.h>

#include <retropp/input.h>
#include <retropp/input_actions.h>
#include <retropp/save_store.h>

#include "state/controls.h"
#include "state/settings.h"
#include "systems/controls.h"
#include "systems/input.h"

namespace {

using kirpich::Action;
using kirpich::ButtonBinding;
using kirpich::Controls;
using kirpich::GbButton;
using kirpich::kDefaultControls;
using kirpich::kGbButtonCount;
using retropp::ControllerType;
using retropp::PadButton;

// A row reduced to what identifies it: the action, the source kind, and the key or pad button.
using Row = std::tuple<int, int, int>;

std::set<Row> rowsOf(const retropp::ActionMap& map) {
    std::set<Row> rows;
    for (const retropp::ActionBinding& r : map.rows()) {
        const int value = r.source.kind == retropp::Source::Kind::Key
                              ? static_cast<int>(r.source.key)
                              : static_cast<int>(r.source.pad);
        rows.insert({r.action, static_cast<int>(r.source.kind), value});
    }
    return rows;
}

Row keyRow(Action a, SDL_Scancode k) {
    return {retropp::actionId(a), static_cast<int>(retropp::Source::Kind::Key), static_cast<int>(k)};
}
Row padRow(Action a, PadButton p) {
    return {retropp::actionId(a), static_cast<int>(retropp::Source::Kind::Pad), static_cast<int>(p)};
}

GbButton buttonAt(std::size_t i) { return static_cast<GbButton>(i); }

// Every key distinct, every pad button distinct, and the cancel key bound nowhere.
void expectDistinct(const Controls& c) {
    for (std::size_t i = 0; i < kGbButtonCount; ++i) {
        EXPECT_NE(c.buttons[i].key, kirpich::kCancelKey) << "button " << i;
        for (std::size_t j = i + 1; j < kGbButtonCount; ++j) {
            EXPECT_NE(c.buttons[i].key, c.buttons[j].key) << "keys " << i << "," << j;
            EXPECT_NE(c.buttons[i].pad, c.buttons[j].pad) << "pads " << i << "," << j;
        }
    }
}

// The physical button a pad source reads on a family: positions resolve to themselves, letters by
// family.
SDL_GamepadButton physical(PadButton p, ControllerType family) {
    return retropp::resolvePadButton(p, family);
}

// (1) The default controls produce exactly the default bindings: the 26 rows of the thirteen actions,
// keyboard and pad each, and defaultActionMap is that same map.
TEST(Controls, DefaultsReproduceTheShippedMap) {
    const std::set<Row> expected = {
        keyRow(Action::MoveLeft, SDL_SCANCODE_LEFT),
        padRow(Action::MoveLeft, PadButton::DpadLeft),
        keyRow(Action::MoveRight, SDL_SCANCODE_RIGHT),
        padRow(Action::MoveRight, PadButton::DpadRight),
        keyRow(Action::SoftDrop, SDL_SCANCODE_DOWN),
        padRow(Action::SoftDrop, PadButton::DpadDown),
        keyRow(Action::RotateClockwise, SDL_SCANCODE_X),
        padRow(Action::RotateClockwise, PadButton::FaceLabelA),
        keyRow(Action::RotateCounterClockwise, SDL_SCANCODE_Z),
        padRow(Action::RotateCounterClockwise, PadButton::FaceLabelB),
        keyRow(Action::Start, SDL_SCANCODE_RETURN),
        padRow(Action::Start, PadButton::Start),
        keyRow(Action::Select, SDL_SCANCODE_BACKSPACE),
        padRow(Action::Select, PadButton::Select),
        keyRow(Action::MenuUp, SDL_SCANCODE_UP),
        padRow(Action::MenuUp, PadButton::DpadUp),
        keyRow(Action::MenuDown, SDL_SCANCODE_DOWN),
        padRow(Action::MenuDown, PadButton::DpadDown),
        keyRow(Action::MenuLeft, SDL_SCANCODE_LEFT),
        padRow(Action::MenuLeft, PadButton::DpadLeft),
        keyRow(Action::MenuRight, SDL_SCANCODE_RIGHT),
        padRow(Action::MenuRight, PadButton::DpadRight),
        keyRow(Action::Confirm, SDL_SCANCODE_X),
        padRow(Action::Confirm, PadButton::FaceLabelA),
        keyRow(Action::Back, SDL_SCANCODE_Z),
        padRow(Action::Back, PadButton::FaceLabelB),
    };

    const retropp::ActionMap derived = kirpich::systems::actionMapFor(kDefaultControls);
    EXPECT_EQ(derived.rows().size(), 26u);
    EXPECT_EQ(rowsOf(derived), expected);

    const retropp::ActionMap shipped = kirpich::systems::defaultActionMap();
    EXPECT_EQ(rowsOf(shipped), expected);
}

// (2) Every action carries exactly its own button's key and pad button, and nothing else — swept with
// a set whose sixteen sources are all different from the defaults and from each other.
TEST(Controls, EachActionFollowsItsButton) {
    const std::array<SDL_Scancode, kGbButtonCount> keys = {
        SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D,
        SDL_SCANCODE_K, SDL_SCANCODE_J, SDL_SCANCODE_SPACE, SDL_SCANCODE_TAB};
    const std::array<PadButton, kGbButtonCount> pads = {
        PadButton::LeftStickUp, PadButton::LeftStickDown, PadButton::LeftStickLeft,
        PadButton::LeftStickRight, PadButton::FaceEast, PadButton::FaceSouth,
        PadButton::ShoulderR, PadButton::ShoulderL};

    Controls controls;
    for (std::size_t i = 0; i < kGbButtonCount; ++i) {
        controls.buttons[i] = ButtonBinding{.key = keys[i], .pad = pads[i]};
    }

    std::set<Row> expected;
    for (std::size_t i = 0; i < kGbButtonCount; ++i) {
        for (const Action a : kirpich::systems::actionsFor(buttonAt(i))) {
            expected.insert(keyRow(a, keys[i]));
            expected.insert(padRow(a, pads[i]));
        }
    }
    EXPECT_EQ(rowsOf(kirpich::systems::actionMapFor(controls)), expected);

    // The table itself: Up walks a menu only, Start and Select are themselves, and each other button
    // carries a gameplay action and the menu action sharing it.
    using kirpich::systems::actionsFor;
    auto is = [](std::span<const Action> got, std::initializer_list<Action> want) {
        return std::equal(got.begin(), got.end(), want.begin(), want.end());
    };
    EXPECT_TRUE(is(actionsFor(GbButton::UP), {Action::MenuUp}));
    EXPECT_TRUE(is(actionsFor(GbButton::DOWN), {Action::SoftDrop, Action::MenuDown}));
    EXPECT_TRUE(is(actionsFor(GbButton::LEFT), {Action::MoveLeft, Action::MenuLeft}));
    EXPECT_TRUE(is(actionsFor(GbButton::RIGHT), {Action::MoveRight, Action::MenuRight}));
    EXPECT_TRUE(is(actionsFor(GbButton::A), {Action::RotateClockwise, Action::Confirm}));
    EXPECT_TRUE(is(actionsFor(GbButton::B), {Action::RotateCounterClockwise, Action::Back}));
    EXPECT_TRUE(is(actionsFor(GbButton::START), {Action::Start}));
    EXPECT_TRUE(is(actionsFor(GbButton::SELECT), {Action::Select}));
}

// (3) Binding a key: a free key is simply taken, a held one swaps, the same key changes nothing, and
// the cancel key is refused with the whole set untouched. Every button taking every other button's
// key keeps the set distinct.
TEST(Controls, AssignKeyTakesSwapsAndRefusesCancel) {
    Controls c = kDefaultControls;
    EXPECT_TRUE(kirpich::systems::assignKey(c, GbButton::A, SDL_SCANCODE_SPACE));
    EXPECT_EQ(c[GbButton::A].key, SDL_SCANCODE_SPACE);
    expectDistinct(c);

    c = kDefaultControls;
    EXPECT_TRUE(kirpich::systems::assignKey(c, GbButton::A, SDL_SCANCODE_Z));
    EXPECT_EQ(c[GbButton::A].key, SDL_SCANCODE_Z);
    EXPECT_EQ(c[GbButton::B].key, SDL_SCANCODE_X);  // B took A's old key
    expectDistinct(c);

    c = kDefaultControls;
    EXPECT_TRUE(kirpich::systems::assignKey(c, GbButton::START, SDL_SCANCODE_RETURN));
    EXPECT_EQ(c, kDefaultControls);

    c = kDefaultControls;
    EXPECT_FALSE(kirpich::systems::assignKey(c, GbButton::A, kirpich::kCancelKey));
    EXPECT_EQ(c, kDefaultControls);

    for (std::size_t i = 0; i < kGbButtonCount; ++i) {
        for (std::size_t j = 0; j < kGbButtonCount; ++j) {
            c                    = kDefaultControls;
            const SDL_Scancode k = c.buttons[j].key;
            ASSERT_TRUE(kirpich::systems::assignKey(c, buttonAt(i), k));
            EXPECT_EQ(c.buttons[i].key, k);
            expectDistinct(c);
        }
    }
}

// (4) Binding a pad button. A press is a position; the defaults' A and B are letters. Binding from a
// pad first turns every letter into the position it has on THAT pad, so the column ends up naming
// physical buttons, distinct on every family.
TEST(Controls, AssignPadMakesTheColumnPositional) {
    const std::array kFamilies = {ControllerType::Xbox, ControllerType::PlayStation,
                                  ControllerType::Nintendo, ControllerType::Standard};

    auto expectPhysicallyDistinct = [&](const Controls& c) {
        for (const ControllerType family : kFamilies) {
            for (std::size_t i = 0; i < kGbButtonCount; ++i) {
                for (std::size_t j = i + 1; j < kGbButtonCount; ++j) {
                    EXPECT_NE(physical(c.buttons[i].pad, family), physical(c.buttons[j].pad, family))
                        << "buttons " << i << "," << j << " family " << static_cast<int>(family);
                }
            }
        }
    };

    // From a Nintendo pad, whose printed A is the east button: rebinding B to east is rebinding B to
    // the button A was on, so A takes B's old button - which on that pad is south.
    {
        Controls c = kDefaultControls;
        ASSERT_TRUE(kirpich::systems::assignPad(c, GbButton::B, PadButton::FaceEast,
                                                ControllerType::Nintendo));
        EXPECT_EQ(c[GbButton::B].pad, PadButton::FaceEast);
        EXPECT_EQ(c[GbButton::A].pad, PadButton::FaceSouth);
        expectDistinct(c);
        expectPhysicallyDistinct(c);
    }

    // The same press from an Xbox pad, where east is the printed B: B was already there, so nothing
    // moves except the letters becoming positions.
    {
        Controls c = kDefaultControls;
        ASSERT_TRUE(kirpich::systems::assignPad(c, GbButton::B, PadButton::FaceEast,
                                                ControllerType::Xbox));
        EXPECT_EQ(c[GbButton::B].pad, PadButton::FaceEast);
        EXPECT_EQ(c[GbButton::A].pad, PadButton::FaceSouth);
        expectDistinct(c);
        expectPhysicallyDistinct(c);
    }

    // A free button is taken; the shoulder belongs to nobody, so only A and the letters change.
    {
        Controls c = kDefaultControls;
        ASSERT_TRUE(kirpich::systems::assignPad(c, GbButton::A, PadButton::ShoulderR,
                                                ControllerType::Xbox));
        EXPECT_EQ(c[GbButton::A].pad, PadButton::ShoulderR);
        EXPECT_EQ(c[GbButton::B].pad, PadButton::FaceEast);
        EXPECT_EQ(c[GbButton::START].pad, PadButton::Start);
        expectPhysicallyDistinct(c);
    }

    // A swap between two positions, and the same button again changing nothing.
    {
        Controls c = kDefaultControls;
        ASSERT_TRUE(kirpich::systems::assignPad(c, GbButton::START, PadButton::Select,
                                                ControllerType::Xbox));
        EXPECT_EQ(c[GbButton::START].pad, PadButton::Select);
        EXPECT_EQ(c[GbButton::SELECT].pad, PadButton::Start);

        const Controls before = c;
        ASSERT_TRUE(kirpich::systems::assignPad(c, GbButton::START, PadButton::Select,
                                                ControllerType::Xbox));
        EXPECT_EQ(c, before);
    }

    // A letter is never a press, and is refused with the set untouched.
    {
        Controls c = kDefaultControls;
        EXPECT_FALSE(kirpich::systems::assignPad(c, GbButton::A, PadButton::FaceLabelX,
                                                 ControllerType::Xbox));
        EXPECT_EQ(c, kDefaultControls);
    }
}

// (5) The actions behind a key: Enter is Start by default, follows Start's key wherever the player
// moves it, and a key no button holds stands for nothing.
TEST(Controls, ActionsOnKeyFollowTheBinding) {
    auto only = [](std::initializer_list<Action> list) {
        retropp::ActionSet set;
        for (const Action a : list) set.set(retropp::actionId(a), true);
        return set;
    };

    EXPECT_EQ(kirpich::systems::actionsOnKey(kDefaultControls, SDL_SCANCODE_RETURN),
              only({Action::Start}));
    EXPECT_EQ(kirpich::systems::actionsOnKey(kDefaultControls, SDL_SCANCODE_X),
              only({Action::RotateClockwise, Action::Confirm}));

    Controls c = kDefaultControls;
    ASSERT_TRUE(kirpich::systems::assignKey(c, GbButton::START, SDL_SCANCODE_SPACE));
    ASSERT_TRUE(kirpich::systems::assignKey(c, GbButton::SELECT, SDL_SCANCODE_RETURN));
    EXPECT_EQ(kirpich::systems::actionsOnKey(c, SDL_SCANCODE_RETURN), only({Action::Select}));
    EXPECT_EQ(kirpich::systems::actionsOnKey(c, SDL_SCANCODE_SPACE), only({Action::Start}));
    EXPECT_EQ(kirpich::systems::actionsOnKey(c, SDL_SCANCODE_Q), retropp::ActionSet{});
}

// (6) The document: a non-default set round-trips, the bytes are laid out record by record with the
// key little-endian, and every refusal leaves the value as it was.
TEST(Controls, CodecRoundTripAndRefusals) {
    Controls c = kDefaultControls;
    ASSERT_TRUE(kirpich::systems::assignKey(c, GbButton::UP, SDL_SCANCODE_W));
    ASSERT_TRUE(kirpich::systems::assignPad(c, GbButton::A, PadButton::RightStickRight,
                                            ControllerType::Xbox));

    const auto image = kirpich::encodeControls(c);
    ASSERT_EQ(image.size(), 24u);

    Controls back = kDefaultControls;
    ASSERT_TRUE(kirpich::decodeControls(image, back));
    EXPECT_EQ(back, c);

    // Layout: Up's record first, the key low byte then high byte, then the pad button.
    EXPECT_EQ(image[0], static_cast<std::uint8_t>(SDL_SCANCODE_W & 0xFF));
    EXPECT_EQ(image[1], static_cast<std::uint8_t>(SDL_SCANCODE_W >> 8));
    EXPECT_EQ(image[2], static_cast<std::uint8_t>(PadButton::DpadUp));
    EXPECT_EQ(image[12], static_cast<std::uint8_t>(SDL_SCANCODE_X));  // A's key
    EXPECT_EQ(image[14], static_cast<std::uint8_t>(PadButton::RightStickRight));

    // A key whose value needs the high byte survives the trip.
    {
        static_assert(SDL_SCANCODE_MODE > 0xFF);
        Controls wide = kDefaultControls;
        ASSERT_TRUE(kirpich::systems::assignKey(wide, GbButton::UP, SDL_SCANCODE_MODE));
        const auto bytes = kirpich::encodeControls(wide);
        EXPECT_EQ(bytes[1], static_cast<std::uint8_t>(SDL_SCANCODE_MODE >> 8));
        Controls got = kDefaultControls;
        ASSERT_TRUE(kirpich::decodeControls(bytes, got));
        EXPECT_EQ(got, wide);
    }

    auto refused = [&](std::span<const std::uint8_t> bytes) {
        Controls target = kDefaultControls;
        EXPECT_FALSE(kirpich::decodeControls(bytes, target));
        EXPECT_EQ(target, kDefaultControls);
    };

    refused(std::span<const std::uint8_t>(image.data(), 23));  // short
    std::array<std::uint8_t, 25> longer{};
    std::copy(image.begin(), image.end(), longer.begin());
    refused(longer);  // long

    auto mutated = [&](std::size_t index, std::uint8_t value) {
        auto bytes   = image;
        bytes[index] = value;
        return bytes;
    };
    refused(mutated(0, 0));                                                  // Up has no key
    refused(mutated(1, 0xFF));                                               // key out of range
    refused(mutated(0, static_cast<std::uint8_t>(kirpich::kCancelKey)));     // the cancel key
    refused(mutated(2, static_cast<std::uint8_t>(PadButton::RightStickRight) + 1));  // no such pad
    refused(mutated(3, static_cast<std::uint8_t>(SDL_SCANCODE_W)));          // Down's key = Up's
    refused(mutated(5, static_cast<std::uint8_t>(PadButton::DpadUp)));       // Down's pad = Up's
}

// (7) The store: absent is an ordinary first run, a written set comes back equal, a damaged document
// leaves the defaults and stays on disk, and the document lives beside the settings, each read at its
// own version.
TEST(Controls, StoreRoundTrip) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_controls_store_roundtrip";
    std::filesystem::remove_all(root);

    Controls saved = kDefaultControls;
    ASSERT_TRUE(kirpich::systems::assignKey(saved, GbButton::START, SDL_SCANCODE_SPACE));
    ASSERT_TRUE(kirpich::systems::assignPad(saved, GbButton::B, PadButton::FaceEast,
                                            ControllerType::Nintendo));

    {
        auto     store  = retropp::SaveStore::atPath(root);
        Controls loaded = kDefaultControls;
        EXPECT_FALSE(kirpich::loadControls(store, loaded));
        EXPECT_EQ(loaded, kDefaultControls);
    }

    const kirpich::Settings settings{.fullscreen = true, .windowScale = 3, .shadeRamp = 7};
    {
        auto store = retropp::SaveStore::atPath(root);
        ASSERT_TRUE(kirpich::saveSettings(settings, store));
        ASSERT_TRUE(kirpich::saveControls(saved, store));

        kirpich::Settings settingsBack;
        ASSERT_TRUE(kirpich::loadSettings(store, settingsBack));
        EXPECT_EQ(settingsBack, settings);

        Controls loaded = kDefaultControls;
        ASSERT_TRUE(kirpich::loadControls(store, loaded));
        EXPECT_EQ(loaded, saved);

        // And the settings still read after the controls declared their own version.
        kirpich::Settings again;
        ASSERT_TRUE(kirpich::loadSettings(store, again));
        EXPECT_EQ(again, settings);
    }

    {
        auto store     = retropp::SaveStore::atPath(root);
        bool corrupted = false;
        for (const auto& entry : std::filesystem::directory_iterator(store.basePath())) {
            if (!entry.is_regular_file()) continue;
            if (!entry.path().filename().string().starts_with("controls")) continue;
            std::ofstream(entry.path(), std::ios::binary | std::ios::trunc).put('\x01');
            corrupted = true;
        }
        ASSERT_TRUE(corrupted) << "no controls document on disk to corrupt";

        Controls loaded = kDefaultControls;
        EXPECT_FALSE(kirpich::loadControls(store, loaded));
        EXPECT_EQ(loaded, kDefaultControls);

        bool stillPresent = false;
        for (const auto& entry : std::filesystem::directory_iterator(store.basePath())) {
            if (entry.path().filename().string().starts_with("controls")) stillPresent = true;
        }
        EXPECT_TRUE(stillPresent);

        kirpich::Settings settingsBack;
        ASSERT_TRUE(kirpich::loadSettings(store, settingsBack));
        EXPECT_EQ(settingsBack, settings);
    }

    std::filesystem::remove_all(root);
}

}  // namespace
