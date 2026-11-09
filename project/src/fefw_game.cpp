// SPDX-License-Identifier: GPL-3.0-or-later
#include "fefw_game.h"

#include "nxrt/game.h"
#include "nxrt/services.h"
#include "play/play_types.h"

namespace {
const nxrt::play::GameDefaults kControls = {.settings_text = R"ini(
[meta]
defaults_rev = 2
[display]
window_mode = windowed
width = 1280
height = 720
fps_limit = 60
keep_aspect = 1
[mouse]
menu_cursor = 0
[pad]
label_abxy = 1
deadzone = 0.10
right_deadzone = 0.10
trigger_threshold = 0.35
rumble = 1
gyro = 1
[padmap]
south = default
east = default
west = default
north = default
left_shoulder = L
right_shoulder = R
left_trigger = ZL
right_trigger = ZR
start = PLUS
back = MINUS
left_stick = LSTICK
right_stick = RSTICK
dpad_up = UP
dpad_down = DOWN
dpad_left = LEFT
dpad_right = RIGHT
[bindings.all]
)ini", .defaults_rev = 2, .controller_only = true};

void title_defaults(nxrt::services::TitleProfile& profile) {
    profile.program_id = 0x04007EF01F8AE000ull;
    profile.name = "Fire Emblem: Fortune's Weave";
    // Facts read from this build's main.npdm, not copied from Pokopia.
    profile.main_thread_stack = 0x100000;
    profile.main_thread_priority = 44;
    profile.main_thread_core = 0;
    profile.system_resource_size = 0x4000000;
    // control.nacp has not been supplied. Save sizes/languages/display version
    // stay unspecified and may be set in the ini after verification.
}

const nxrt::game::Info& game_info() {
    static const nxrt::game::Info info = [] {
        nxrt::game::Info game;
        game.id = "fefw";
        game.title = "Fire Emblem: Fortune's Weave";
        game.product = "Fire Emblem Recomp (experimental)";
        game.short_title = "Fire Emblem";
        game.program_id = 0x04007EF01F8AE000ull;
        game.version = "build DB93B012 / update-directory 131072";
        game.platform = nxrt::game::Platform::Switch2;
        game.present_interval_hz = 60;
        game.main_base = 0x7100000000ull;
        game.exe_name = "fefw.exe";
        game.game_ini = "fefw.ini";
        game.save_folder = "FireEmblemRecomp";
        game.title_defaults = &title_defaults;
        game.play_defaults = &kControls;
        return game;
    }();
    return info;
}
}  // namespace

void nxrt_register_game_fefw() {
    nxrt::game::set_current(&game_info());
}
