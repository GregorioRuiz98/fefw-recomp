// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_framework.h"
#include "../src/fefw_game.h"
#include "nxrt/game.h"
#include "nxrt/input.h"
#include "play/pad_map.h"
#include "play/play_game.h"
#include "play/prompt_resolve.h"
#include <array>

NXTEST_GROUP(playstation_prompts)
using namespace nxrt::play;

NXTEST(game_connects_font_glyph_updates_without_changing_input_timing) {
    nxrt_register_game_fefw();
    const auto* play = nxrt::game::info().play;
    REQUIRE(play);
    CHECK(play->install != nullptr);
    CHECK(play->frame != nullptr);
    CHECK(play->remove != nullptr);
    CHECK_EQ(play->frame_update, uint64_t{0}); // Preserve the service-driven input path.
}

NXTEST(playstation_icons_follow_both_face_layouts_and_the_actual_pad_output) {
    for (bool by_label : {true, false}) {
        PadSettings pad;
        pad.label_abxy = by_label;
        const Btn buttons[] = {Btn::A, Btn::B, Btn::X, Btn::Y};
        const nxrt::hid::NpadButtonBit bits[] = {nxrt::hid::kBtnA, nxrt::hid::kBtnB, nxrt::hid::kBtnX, nxrt::hid::kBtnY};
        const char* names[] = {"ps/cross", "ps/circle", "ps/square", "ps/triangle"};
        const int positions[] = {1, 0, 3, 2};
        for (int physical = 0; physical < 4; ++physical) {
            nxrt::platform::GamepadRaw raw{};
            raw.buttons = 1u << physical;
            for (int i = 0; i < 4; ++i) raw.face_label[i] = i;
            nxrt::hid::NpadInput out{};
            map_pad(raw, pad, &out);
            const Btn guest = buttons[by_label ? physical : positions[physical]];
            CHECK_EQ(out.buttons, nxrt::hid::btn(bits[by_label ? physical : positions[physical]]));
            CHECK_EQ(pad_button_name(PromptStyle::PlayStation, pad, by_label, guest), names[physical]);
        }
    }
}

NXTEST(remapped_controls_and_auto_device_style_are_honored) {
    PadSettings pad;
    pad.remap[static_cast<int>(PadSrc::South)] = nxrt::hid::btn(nxrt::hid::kBtnR);
    pad.remap[static_cast<int>(PadSrc::East)] = nxrt::hid::btn(nxrt::hid::kBtnA);
    CHECK_EQ(pad_button_name(PromptStyle::PlayStation, pad, true, Btn::A), "ps/circle");
    CHECK_EQ(pad_button_name(PromptStyle::PlayStation, pad, true, Btn::R), "ps/cross");
    CHECK_EQ(pad_button_name(PromptStyle::PlayStation, pad, true, Btn::L), "ps/l1");
    CHECK_EQ(pad_button_name(PromptStyle::PlayStation, pad, true, Btn::ZR), "ps/r2");
    CHECK_EQ(auto_style(LastDevice::None, 0, static_cast<int>(nxrt::input::PadKind::PlayStation)), PromptStyle::PlayStation);
    CHECK_EQ(auto_style(LastDevice::Pad, static_cast<int>(nxrt::input::PadKind::PlayStation), -1), PromptStyle::PlayStation);
    CHECK_EQ(auto_style(LastDevice::Pad, static_cast<int>(nxrt::input::PadKind::Switch), -1), PromptStyle::Switch);
}
