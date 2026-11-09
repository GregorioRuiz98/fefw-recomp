// SPDX-License-Identifier: GPL-3.0-or-later
#include "fefw_config.h"
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <stdexcept>
namespace s = fefw::setup;
namespace fs = std::filesystem;
int main() {
    int failed = 0, checks = 0;
    auto check = [&](bool ok, const char* what) { ++checks; if (!ok) { ++failed; std::printf("FAIL: %s\n", what); } };
    const auto dir = fs::current_path() / L"config-test-results" / (L"fefw-config-test-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(dir);
    try {
        const auto p = dir / L"ajustes á 漢字.ini";
        { std::ofstream out(p); out << "; keep comment\n[fs]\nsave_dir = save/user\n[padmap]\nstart = PLUS\n[display]\nwidth = 1280\n[display]\nwidth = 1920\n"; }
        s::Ini ini(p);
        check(ini.get("FS", "save_dir") == "save/user", "case-insensitive section lookup");
        check(ini.get("display", "width") == "1920", "last duplicate wins on read");
        ini.set("display", "width", "2560"); ini.set("system", "language", "Spanish");
        const std::string path = s::path_text(dir / L"Juego á 漢字"); ini.set("fs", "romfs", path); ini.save(p);
        s::Ini read(p);
        check(read.get("display", "width") == "2560", "updates every duplicate key");
        check(read.get("fs", "save_dir") == "save/user", "preserves save directory");
        check(read.get("padmap", "start") == "PLUS", "preserves controller binding");
        check(read.get("system", "language") == "Spanish", "adds a new section");
        check(read.get("fs", "romfs") == path, "UTF-8 paths roundtrip");
        bool rejected = false; try { ini.set("fs", "romfs", "good\n[bad]"); } catch (...) { rejected = true; }
        check(rejected, "rejects newline injection");
        { std::ifstream in(p); std::string line; std::getline(in, line); check(line == "; keep comment", "preserves comments"); }
        HANDLE locked = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        check(locked != INVALID_HANDLE_VALUE, "opens locked fixture");
        read.set("display", "width", "800"); rejected = false;
        try { read.save(p); } catch (...) { rejected = true; }
        if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
        check(rejected && s::Ini(p).get("display", "width") == "2560", "failed atomic replacement preserves settings");
        check(!s::validate_source(dir).ok, "rejects missing ROMFS");
        fs::create_directories(dir / L"romfs/Msg/EUes"); fs::create_directories(dir / L"exefs");
        check(!s::validate_source(dir).ok, "rejects missing NPDM");
        { std::ofstream out(dir / L"exefs/main.npdm"); out << "META"; }
        check(!s::validate_source(dir).ok, "rejects truncated NPDM");
        check(!s::validate_prepared(dir / L"exefs").ok, "rejects missing prepared modules");
        const auto ls = s::available_languages(dir);
        check(ls.size() == 1 && std::string(ls[0].value) == "Spanish", "languages come from the selected resources");
        s::Settings edit; edit.source = dir; edit.language = "Spanish"; edit.width = 1600; edit.height = 900; edit.skip = false;
        s::Ini game(dir / L"missing.ini"), controls(p);
        s::edit_settings(edit, game, controls);
        check(game.get("system", "language") == "Spanish", "requested game language is written");
        check(game.get("title", "supported_languages") == "Spanish", "language mask includes actual resources instead of English-only fallback");
        check(controls.get("tweaks", "language") == "Spanish", "runtime settings agree on language");
        check(controls.get("display", "width") == "1600" && controls.get("display", "height") == "900", "writes output dimensions");
        check(game.get("launcher", "skip_setup") == "0", "show-setup preference is saved");
        check(controls.get("display", "vsync") == "1", "first-run profile enables synchronized presentation");
        check(controls.get("padmap", "start") == "PLUS", "editing setup preserves controller mapping");
        check(game.get("mods", "fps60") == "0", "60 FPS patch defaults to off");
        check(game.get("dlss", "upscaler") == "taau", "temporal reconstruction is the initial profile");
        edit.upscaler = "bilinear";
        s::edit_settings(edit, game, controls);
        check(game.get("dlss", "upscaler") == "bilinear", "bilinear comparison can be selected");
        edit.upscaler = "invalid";
        rejected = false; try { s::edit_settings(edit, game, controls); } catch (...) { rejected = true; }
        check(rejected, "unsupported reconstruction mode rejected");
        edit.upscaler = "taau";
        edit.fps60 = true;
        s::edit_settings(edit, game, controls);
        check(game.get("mods", "fps60") == "1" && controls.get("display", "fps_limit") == "60", "60 FPS changes game patch and host cap");
        edit.fps60 = false;
        s::edit_settings(edit, game, controls);
        check(game.get("mods", "fps60") == "0", "60 FPS can be disabled");
        const auto before = s::load(dir);
        check(!before.completed && s::needs_setup(dir), "first launch requires setup");
        check(!s::game_running(dir), "game mutex absent before start");
        HANDLE mutex = CreateMutexW(nullptr, FALSE, s::game_mutex_name(dir).c_str());
        check(s::game_running(dir), "detects game already running");
        rejected = false; try { s::save(dir, before); } catch (...) { rejected = true; }
        check(rejected && !fs::exists(dir / L"fefw.ini"), "refuses to write during gameplay");
        CloseHandle(mutex);
        auto bad = before; bad.completed = true; rejected = false;
        try { s::save(dir, bad); } catch (...) { rejected = true; }
        check(rejected && !fs::exists(dir / L"fefw.ini"), "invalid setup never becomes complete");
        check(s::wide(s::utf8(L"á 漢字")) == L"á 漢字", "Unicode conversion");
        check(s::quote_arg(L"C:\\Juego con espacios\\") == L"\"C:\\Juego con espacios\\\\\"", "quotes a trailing slash safely");
        check(s::quote_arg(L"a\"b") == L"\"a\\\"b\"", "quotes embedded quotes safely");
        fs::create_directories(dir / L"updates/131072/exefs"); { std::ofstream out(dir / L"updates/131072/exefs/main"); }
        check(s::candidates(dir).size() == 1, "discovers update folders");
    } catch (const std::exception& e) { ++failed; std::printf("EXCEPTION: %s\n", e.what()); }
    std::printf("Config: %d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
