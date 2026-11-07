// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_framework.h"
#include "fefw_game.h"
#include "nxrt/config.h"
#include "nxrt/input.h"
#include "nxrt/input_source.h"
#include "nxrt/platform.h"
#include "nxrt/play.h"
#include "nxrt/runtime.h"
#include "nxrt/svc.h"
#include "play/play_types.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <thread>

NXTEST_GROUP(interactive_input)

namespace {
void pump() {
    SDL_Delay(20);
    ::Ctx ctx{};
    nxrt::svc_dispatch(&ctx, 0xfe);
}
void key(int code, bool down) {
    nxrt::input::Event e;
    e.type = down ? nxrt::input::EventType::KeyDown : nxrt::input::EventType::KeyUp;
    e.u.key.scancode = code;
    nxrt::input::queue_push(e);
}
nxrt::hid::NpadInput sample() {
    nxrt::hid::InputSnapshot s;
    if (auto source = nxrt::hid::input_source()) source->poll(s);
    return s.npad[0];
}
struct Cleanup {
    SDL_JoystickID id = 0;
    SDL_Joystick* joy = nullptr;
    ~Cleanup() {
        if (joy) SDL_CloseJoystick(joy);
        if (id) SDL_DetachVirtualJoystick(id);
        nxrt::play::stop();
        nxrt::platform::shutdown();
        nxrt::shutdown();
    }
};
}

NXTEST(preconnected_controller_recovers_without_added_event) {
    // A controller can already exist when the game starts, with its initial
    // notification consumed by another SDL user. Discovery must enumerate it.
    REQUIRE(SDL_Init(SDL_INIT_GAMEPAD));
    Cleanup cleanup;
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.vendor_id = 0x054c;
    desc.product_id = 0x09cc;
    desc.name = "Preconnected DualShock 4 regression";
    cleanup.id = SDL_AttachVirtualJoystick(&desc);
    REQUIRE(cleanup.id);
    cleanup.joy = SDL_OpenJoystick(cleanup.id);
    REQUIRE(cleanup.joy);
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_GAMEPAD_ADDED, SDL_EVENT_GAMEPAD_ADDED);
    REQUIRE(nxrt::platform::init());
    nxrt::platform::pump_events();
    nxrt::platform::GamepadRaw raw;
    REQUIRE(nxrt::platform::gamepad_poll(cleanup.id, &raw));
    CHECK_EQ(nxrt::input::gamepad_kind_of(cleanup.id), nxrt::input::PadKind::PlayStation);
    const auto count = nxrt::input::gamepad_count();
    nxrt::platform::rescan_gamepads();
    nxrt::platform::pump_events();
    CHECK_EQ(nxrt::input::gamepad_count(), count);
    SDL_SetJoystickVirtualButton(cleanup.joy, SDL_GAMEPAD_BUTTON_SOUTH, true);
    nxrt::platform::pump_events();
    REQUIRE(nxrt::platform::gamepad_poll(cleanup.id, &raw));
    CHECK(raw.buttons & (1u << SDL_GAMEPAD_BUTTON_SOUTH));
    // A lost hot-plug notification must also heal without restarting.
    const auto disconnected = cleanup.id;
    SDL_CloseJoystick(cleanup.joy); cleanup.joy = nullptr;
    SDL_DetachVirtualJoystick(cleanup.id); cleanup.id = 0;
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_GAMEPAD_REMOVED, SDL_EVENT_GAMEPAD_REMOVED);
    CHECK(!nxrt::platform::gamepad_poll(disconnected, &raw));
    nxrt::platform::pump_events();
    cleanup.id = SDL_AttachVirtualJoystick(&desc);
    REQUIRE(cleanup.id);
    cleanup.joy = SDL_OpenJoystick(cleanup.id);
    REQUIRE(cleanup.joy);
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_GAMEPAD_ADDED, SDL_EVENT_GAMEPAD_ADDED);
    SDL_Delay(1100);
    nxrt::platform::pump_events();
    CHECK(nxrt::platform::gamepad_poll(cleanup.id, &raw));
}

NXTEST(service_driven_controller_only_and_shutdown) {
    nxrt_register_game_fefw();
    std::string err;
    nxrt::play::Settings settings;
    REQUIRE(nxrt::play::parse_settings(nxrt::play::default_settings_text(), &settings, &err));
    CHECK(settings.bindings.empty());
    CHECK(!settings.mouse.menu_cursor);
    CHECK(settings.pad.label_abxy);
    CHECK(settings.pad.rumble);
    nxrt::InitOptions init;
    init.watchdog = false;
    init.memguard = false;
    REQUIRE(nxrt::init(init));
    Cleanup cleanup;
    const auto controls = std::filesystem::temp_directory_path() / "fefw_interactive_test_controls.ini";
    {   // A fresh file: exercise the game's factory defaults, not a previous test's settings.
        std::ofstream out(controls);
        out << nxrt::play::default_settings_text();
        // Old custom bindings must not restore keyboard/mouse gameplay.
        out << "\n[bindings.all]\nReturn = btn_a\nP = btn_plus\nUp = dpad_up\nW = move_forward\n"
               "Mouse1 = btn_a\nMouse3 = btn_b\nWheelUp = btn_r\n[mouse]\nmenu_cursor = 1\n";
    }
    nxrt::config_set("play", "controls", controls.string().c_str());
    nxrt::config_set("play", "service_input_fallback", "1");
    REQUIRE(nxrt::platform::init());
    nxrt::platform::WindowDesc wd;
    wd.hidden = true;
    REQUIRE(nxrt::platform::create_window(wd));
    nxrt::play::StartOptions opt;
    opt.window = true;
    opt.overlay = false;
    REQUIRE(nxrt::play::start(opt));
    nxrt::svc_register(0xfe, [](::Ctx*) {});
    pump();

    for (auto [code, mask] : {
        std::pair{SDL_SCANCODE_RETURN, nxrt::hid::btn(nxrt::hid::kBtnA)},
        std::pair{SDL_SCANCODE_P, nxrt::hid::btn(nxrt::hid::kBtnPlus)},
        std::pair{SDL_SCANCODE_UP, nxrt::hid::btn(nxrt::hid::kBtnUp)}}) {
        key(code, true);
        pump();
        CHECK((sample().buttons & mask) == 0);
        key(code, false);
        pump();
        CHECK((sample().buttons & mask) == 0);
    }
    key(SDL_SCANCODE_W, true);
    pump();
    CHECK_EQ(sample().left.y, 0);
    key(SDL_SCANCODE_W, false);
    pump();
    CHECK_EQ(sample().left.y, 0);

    // A guest worker must not pump SDL or consume events owned by the main thread.
    key(SDL_SCANCODE_RETURN, true);
    const auto before = nxrt::play::stats().frames;
    std::thread other([] { pump(); });
    other.join();
    CHECK_EQ(nxrt::play::stats().frames, before);
    CHECK((sample().buttons & nxrt::hid::btn(nxrt::hid::kBtnA)) == 0);
    pump();
    CHECK((sample().buttons & nxrt::hid::btn(nxrt::hid::kBtnA)) == 0);
    key(SDL_SCANCODE_RETURN, false);
    pump();

    for (uint8_t button : {1, 3}) {
        nxrt::input::Event e;
        e.type = nxrt::input::EventType::MouseButtonDown;
        e.u.button.button = button;
        nxrt::input::queue_push(e);
        pump();
        CHECK_EQ(sample().buttons, 0ull);
        e.type = nxrt::input::EventType::MouseButtonUp;
        nxrt::input::queue_push(e);
        pump();
    }
    nxrt::input::Event wheel;
    wheel.type = nxrt::input::EventType::MouseWheel;
    wheel.u.wheel.dy = 1;
    nxrt::input::queue_push(wheel);
    nxrt::input::Event motion;
    motion.type = nxrt::input::EventType::MouseMotion;
    motion.flags = nxrt::input::kFlagRelative;
    motion.u.motion.dx = 100;
    motion.u.motion.dy = -100;
    nxrt::input::queue_push(motion);
    pump();
    CHECK_EQ(sample().buttons, 0ull);
    CHECK_EQ(sample().right.x, 0);
    CHECK_EQ(sample().right.y, 0);
    CHECK(!nxrt::platform::relative_mouse());

    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.vendor_id = 0x045e;
    desc.product_id = 0x028e;
    desc.name = "Fire Emblem input regression";
    cleanup.id = SDL_AttachVirtualJoystick(&desc);
    REQUIRE(cleanup.id);
    cleanup.joy = SDL_OpenJoystick(cleanup.id);
    REQUIRE(cleanup.joy);
    SDL_SetJoystickVirtualAxis(cleanup.joy, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
    SDL_SetJoystickVirtualAxis(cleanup.joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    SDL_SetJoystickVirtualButton(cleanup.joy, SDL_GAMEPAD_BUTTON_SOUTH, true);
    for (int n = 0; n < 25 && !(sample().buttons & nxrt::hid::btn(nxrt::hid::kBtnA)); ++n) pump();
    CHECK(sample().connected);
    CHECK((sample().buttons & nxrt::hid::btn(nxrt::hid::kBtnA)) != 0);
    // Using the PC inputs while a controller is held must not change its output.
    key(SDL_SCANCODE_W, true);
    nxrt::input::Event click;
    click.type = nxrt::input::EventType::MouseButtonDown;
    click.u.button.button = 3;
    nxrt::input::queue_push(click);
    pump();
    CHECK_EQ(sample().buttons, nxrt::hid::btn(nxrt::hid::kBtnA));
    CHECK_EQ(sample().left.y, 0);
    key(SDL_SCANCODE_W, false);
    click.type = nxrt::input::EventType::MouseButtonUp;
    nxrt::input::queue_push(click);
    SDL_SetJoystickVirtualButton(cleanup.joy, SDL_GAMEPAD_BUTTON_SOUTH, false);
    pump();
    CHECK((sample().buttons & nxrt::hid::btn(nxrt::hid::kBtnA)) == 0);

    // Every physical button goes through SDL and the service-driven HID path.
    struct Button { SDL_GamepadButton physical; nxrt::hid::NpadButtonBit guest; };
    const Button buttons[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, nxrt::hid::kBtnA},
        {SDL_GAMEPAD_BUTTON_EAST, nxrt::hid::kBtnB},
        {SDL_GAMEPAD_BUTTON_WEST, nxrt::hid::kBtnX},
        {SDL_GAMEPAD_BUTTON_NORTH, nxrt::hid::kBtnY},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, nxrt::hid::kBtnL},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, nxrt::hid::kBtnR},
        {SDL_GAMEPAD_BUTTON_START, nxrt::hid::kBtnPlus},
        {SDL_GAMEPAD_BUTTON_BACK, nxrt::hid::kBtnMinus},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, nxrt::hid::kBtnStickL},
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, nxrt::hid::kBtnStickR},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, nxrt::hid::kBtnUp},
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, nxrt::hid::kBtnDown},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, nxrt::hid::kBtnLeft},
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, nxrt::hid::kBtnRight},
    };
    for (const auto& button : buttons) {
        SDL_SetJoystickVirtualButton(cleanup.joy, button.physical, true);
        pump();
        CHECK_EQ(sample().buttons, nxrt::hid::btn(button.guest));
        SDL_SetJoystickVirtualButton(cleanup.joy, button.physical, false);
        pump();
        CHECK_EQ(sample().buttons, 0ull);
    }
    SDL_SetJoystickVirtualAxis(cleanup.joy, SDL_GAMEPAD_AXIS_LEFTX, 32767);
    SDL_SetJoystickVirtualAxis(cleanup.joy, SDL_GAMEPAD_AXIS_RIGHTY, -32768);
    SDL_SetJoystickVirtualAxis(cleanup.joy, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767);
    SDL_SetJoystickVirtualAxis(cleanup.joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    pump();
    CHECK(sample().left.x > 32000);
    CHECK(sample().right.y > 32000);
    CHECK_EQ(sample().buttons, nxrt::hid::btn(nxrt::hid::kBtnZL) | nxrt::hid::btn(nxrt::hid::kBtnZR));
    SDL_SetJoystickVirtualAxis(cleanup.joy, SDL_GAMEPAD_AXIS_LEFTX, 1024);
    SDL_SetJoystickVirtualAxis(cleanup.joy, SDL_GAMEPAD_AXIS_RIGHTY, -1024);
    SDL_SetJoystickVirtualAxis(cleanup.joy, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -26000);
    SDL_SetJoystickVirtualAxis(cleanup.joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    pump();
    CHECK_EQ(sample().left.x, 0);
    CHECK_EQ(sample().right.y, 0);
    CHECK_EQ(sample().buttons, 0ull);

    // Disconnect while held: no button or stick may stay latched. Reconnect
    // other families and verify their actual SDL face labels without restarting.
    SDL_SetJoystickVirtualButton(cleanup.joy, SDL_GAMEPAD_BUTTON_SOUTH, true);
    SDL_SetJoystickVirtualAxis(cleanup.joy, SDL_GAMEPAD_AXIS_LEFTX, 32767);
    pump();
    CHECK(sample().buttons != 0);
    SDL_CloseJoystick(cleanup.joy);
    cleanup.joy = nullptr;
    SDL_DetachVirtualJoystick(cleanup.id);
    cleanup.id = 0;
    pump();
    CHECK_EQ(sample().buttons, 0ull);
    CHECK_EQ(sample().left.x, 0);
    CHECK_EQ(sample().connected, nxrt::input::gamepad_count() != 0);
    const struct Family { uint16_t vendor, product; nxrt::input::PadKind kind; } families[] = {
        {0x054c, 0x05c4, nxrt::input::PadKind::PlayStation},
        {0x054c, 0x09cc, nxrt::input::PadKind::PlayStation},
        {0x054c, 0x0ce6, nxrt::input::PadKind::PlayStation},
        {0x057e, 0x2009, nxrt::input::PadKind::Switch},
    };
    for (const auto& family : families) {
        desc.vendor_id = family.vendor;
        desc.product_id = family.product;
        cleanup.id = SDL_AttachVirtualJoystick(&desc);
        REQUIRE(cleanup.id);
        cleanup.joy = SDL_OpenJoystick(cleanup.id);
        REQUIRE(cleanup.joy);
        SDL_SetJoystickVirtualAxis(cleanup.joy, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
        SDL_SetJoystickVirtualAxis(cleanup.joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
        nxrt::platform::GamepadRaw raw;
        for (int n = 0; n < 25 && !nxrt::platform::gamepad_poll(cleanup.id, &raw); ++n) pump();
        REQUIRE(nxrt::platform::gamepad_poll(cleanup.id, &raw));
        CHECK_EQ(nxrt::input::gamepad_kind_of(cleanup.id), family.kind);
        const nxrt::hid::NpadButtonBit labels[] = {
            nxrt::hid::kBtnA, nxrt::hid::kBtnB, nxrt::hid::kBtnX, nxrt::hid::kBtnY
        };
        for (int i = 0; i < 4; ++i) {
            const SDL_GamepadButton face[] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH};
            SDL_SetJoystickVirtualButton(cleanup.joy, face[i], true);
            pump();
            CHECK(raw.face_label[i] < 4);
            CHECK_EQ(sample().buttons, nxrt::hid::btn(labels[raw.face_label[i] % 4]));
            SDL_SetJoystickVirtualButton(cleanup.joy, face[i], false);
            pump();
        }
        SDL_CloseJoystick(cleanup.joy);
        cleanup.joy = nullptr;
        SDL_DetachVirtualJoystick(cleanup.id);
        cleanup.id = 0;
        pump();
        CHECK_EQ(sample().buttons, 0ull);
    }

    nxrt::play::stop();
    const auto stopped_frames = nxrt::play::stats().frames;
    pump();
    CHECK_EQ(nxrt::play::stats().frames, stopped_frames);
    CHECK(nxrt::hid::input_source() == nullptr);
}
