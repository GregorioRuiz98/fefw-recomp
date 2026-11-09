// SPDX-License-Identifier: GPL-3.0-or-later
#include "fefw_config.h"
#include "fefw_preferences.h"
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
        edit.check_updates = true; edit.fps120 = true; edit.window_mode = "borderless"; edit.output = "1920x1080"; edit.render_ratio = 2.0/3.0;
        s::edit_settings(edit, game, controls);
        check(game.get("mods", "fps120") == "1" && controls.get("display", "fps_limit") == "120", "120 FPS sets game hook and host ceiling");
        check(controls.get("display", "window_mode") == "borderless" && game.get("fefw", "output") == "1920x1080", "borderless and game output saved independently");
        check(game.get("launcher", "check_updates") == "1", "optional startup update check saved");
        check(game.get("fefw", "render").empty(), "old explicit render override cleared");
        edit.fps120 = false; s::edit_settings(edit, game, controls);
        check(controls.get("display", "fps_limit") == "60", "disabling 120 restores the original host cap");
        edit.output = "3841x2160"; rejected = false; try { s::edit_settings(edit, game, controls); } catch (...) { rejected = true; }
        check(rejected, "unsafe game output rejected"); edit.output = "console";
        edit.render_ratio = 2; rejected = false; try { s::edit_settings(edit, game, controls); } catch (...) { rejected = true; }
        check(rejected, "oversized internal scale rejected");
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
        s::Settings prefs; prefs.source=dir; prefs.language="Spanish";
        prefs.ui_language="es"; prefs.volume=0.35; prefs.mute_unfocused=true; prefs.deadzone=0.2; prefs.right_deadzone=0.25;
        s::edit_settings(prefs,game,controls);
        check(game.get("audio","volume")==controls.get("audio","volume")&&game.get("audio","volume")=="0.350000","master volume agrees with runtime startup override");
        check(controls.get("audio","mute_unfocused")=="1"&&controls.get("pad","right_deadzone")=="0.250000","writes runtime mute and independent deadzones");
        check(game.get("launcher","ui_language")=="es"&&game.get("system","language")=="Spanish","interface and game language are independent");
        game.save(dir/L"fefw.ini"); controls.save(dir/L"controls.ini");
        auto loaded=s::load(dir);
        check(loaded.volume==0.35&&loaded.deadzone==0.2&&loaded.right_deadzone==0.25&&loaded.ui_language=="es"&&loaded.mute_unfocused,"audio and controller settings load back");
        const auto portable=dir/L"portable.ini"; s::export_preferences(portable,prefs);
        auto imported=s::import_preferences(portable,loaded);
        check(imported.source==loaded.source&&imported.language==loaded.language&&imported.volume==0.35,"portable import preserves local game folder and dialogue language");
        { std::ifstream in(portable); std::string contents((std::istreambuf_iterator<char>(in)),{});
          check(contents.find(s::path_text(dir))==std::string::npos&&contents.find("save_dir")==std::string::npos&&contents.find("Spanish")==std::string::npos,"portable export omits paths, saves and game language"); }
        for(auto value:{"nan","1.1","0.2junk","-1"}) {
            s::Ini badprofile(portable); badprofile.set("preferences","volume",value); badprofile.save(dir/L"bad-profile.ini");
            rejected=false;try{s::import_preferences(dir/L"bad-profile.ini",prefs);}catch(...){rejected=true;} check(rejected,"rejects malformed imported volume");
        }
        s::Ini badprofile(portable);badprofile.set("preferences","schema","2");badprofile.save(dir/L"bad-profile.ini");
        rejected=false;try{s::import_preferences(dir/L"bad-profile.ini",prefs);}catch(...){rejected=true;}check(rejected,"rejects future profile schema");
        for(int i=0;i<3;++i) { auto preset=prefs;s::graphics_preset(preset,i);s::edit_settings(preset,game,controls);
            check(!preset.fps60&&!preset.fps120&&preset.language==prefs.language&&preset.volume==prefs.volume,"preset validates and preserves unrelated settings"); }
        std::string guid="00112233445566778899aabbccddeeff";
        s::controller_profile(dir,guid,prefs,true); auto padprefs=prefs; padprefs.deadzone=0;padprefs.right_deadzone=0;
        s::controller_profile(dir,guid,padprefs,false);check(padprefs.deadzone==0.2&&padprefs.right_deadzone==0.25,"per-model controller profile roundtrip");
        rejected=false;try{s::controller_profile(dir,"../outside",padprefs,true);}catch(...){rejected=true;}check(rejected,"controller profile rejects path traversal");
        s::export_diagnostics(dir/L"diagnostics.ini",dir,prefs,1);
        {std::ifstream in(dir/L"diagnostics.ini");std::string contents((std::istreambuf_iterator<char>(in)),{});
         check(contents.find(s::path_text(dir))==std::string::npos&&contents.find("save_dir")==std::string::npos&&contents.find("controllers = 1")!=std::string::npos,"diagnostics reports hardware count without paths or saves");}
    } catch (const std::exception& e) { ++failed; std::printf("EXCEPTION: %s\n", e.what()); }
    std::printf("Config: %d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
