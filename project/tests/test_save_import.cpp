// SPDX-License-Identifier: GPL-3.0-or-later
#include "fefw_saves.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace s=fefw::setup;
namespace fs=std::filesystem;
int main() {
    int checks=0;
    auto check=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);++checks;};
    const auto root=fs::current_path()/L"save-import-test-results"/std::to_wstring(GetCurrentProcessId());
    auto write=[](const fs::path& p,const char* data){fs::create_directories(p.parent_path());std::ofstream(p,std::ios::binary)<<data;};
    auto read=[](const fs::path& p){std::ifstream in(fs::path(L"\\\\?\\"+fs::absolute(p).wstring()),std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
    auto fixture=[&](const fs::path& build,const fs::path& save,const char* value){
        write(save/L"account/0100000000000000726573757472786e.meta","own synthetic account metadata");
        write(save/L"account/0100000000000000726573757472786e/000/Global","own synthetic global");
        write(save/L"account/0100000000000000726573757472786e/000/Auto",value);
        write(save/L"account/0100000000000000726573757472786e/000/Chapter_Ism_0_2","own synthetic chapter");
        fs::create_directories(build);s::Ini ini(build/L"fefw.ini");ini.set("fs","save_dir",s::path_text(save));ini.set("system","language","Spanish");ini.save(build/L"fefw.ini");
    };
    try {
        const auto source=root/L"otra build ÃƒÂ¡ Ã¦Â¼Â¢Ã¥Â­â€”",saves=source/L"custom-saves",game=root/L"new build";
        fixture(source,saves,"source progress");fixture(game,game/L"save","destination progress");
        const fs::path slot=L"account/0100000000000000726573757472786e/000/Auto";
        const auto original_ini=read(source/L"fefw.ini"),original_current=read(game/L"fefw.ini");
        check(s::inspect_saved_game(source).root==fs::canonical(saves),"build-folder import honors custom absolute save_dir");
        check(s::inspect_saved_game(saves).files==4,"save-folder import includes account metadata and every slot");
        auto result=s::import_saved_game(game,source);
        check(result.files==4&&read(result.destination/slot)=="source progress","copies selected progress exactly");
        check(read(result.destination/L"account/0100000000000000726573757472786e.meta")=="own synthetic account metadata","copies volume metadata beside account directory");
        check(read(result.backup/L"save"/slot)=="destination progress"&&read(result.backup/L"fefw.ini")==original_current,"backs up current save and config before switching");
        check(read(game/L"save"/slot)=="destination progress","original destination save remains untouched");
        check(read(saves/slot)=="source progress"&&read(source/L"fefw.ini")==original_ini,"other build and original save remain untouched");
        check(s::Ini(game/L"fefw.ini").get("fs","save_dir")==s::path_text(result.destination),"activates only the imported copy");
        check(s::Ini(game/L"fefw.ini").get("system","language")=="Spanish","preserves unrelated settings");
        auto settings=s::load(game);s::Ini updated(game/L"fefw.ini"),controls(game/L"controls.ini");
        s::edit_settings(settings,updated,controls);updated.save(game/L"fefw.ini");
        check(s::Ini(game/L"fefw.ini").get("fs","save_dir")==s::path_text(result.destination),"saving normal setup settings preserves the imported save location");
        bool rejected=false;const auto active_ini=read(game/L"fefw.ini");
        try{s::import_saved_game(game,result.destination);}catch(...){rejected=true;}
        check(rejected&&read(game/L"fefw.ini")==active_ini,"already-active selection does not change state");
        const auto incomplete=root/L"incomplete";write(incomplete/L"account/user/000/Auto","partial");
        rejected=false;try{s::import_saved_game(game,incomplete);}catch(...){rejected=true;}
        check(rejected&&read(game/L"fefw.ini")==active_ini,"partial save is rejected without switching state");
        HANDLE running=CreateMutexW(nullptr,FALSE,s::game_mutex_name(game).c_str());
        rejected=false;try{s::import_saved_game(game,source);}catch(...){rejected=true;}CloseHandle(running);
        check(rejected,"running destination game blocks import");
        running=CreateMutexW(nullptr,FALSE,s::game_mutex_name(source).c_str());
        rejected=false;try{s::import_saved_game(game,source);}catch(...){rejected=true;}CloseHandle(running);
        check(rejected,"running source build blocks import");
        HANDLE lock=CreateFileW((game/L"fefw.ini").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        check(lock!=INVALID_HANDLE_VALUE,"locks destination config fixture");
        rejected=false;try{s::import_saved_game(game,source);}catch(...){rejected=true;}CloseHandle(lock);
        check(rejected&&read(game/L"fefw.ini")==active_ini&&read(result.destination/slot)=="source progress","failed atomic config replacement preserves the active save");
        auto again=s::import_saved_game(game,saves);
        check(again.destination!=result.destination&&read(again.backup/L"save"/slot)=="source progress","repeat import uses a new copy and backs up the previous active import");
        const auto relative=root/L"relative build";fixture(relative,relative/L"save","relative");
        s::Ini ini(relative/L"fefw.ini");ini.set("fs","save_dir","save");ini.save(relative/L"fefw.ini");
        check(s::inspect_saved_game(relative).root==fs::canonical(relative/L"save"),"relative save_dir resolves against selected build");
        const auto external=root/L"external";fs::create_directories(external);
        check(CreateHardLinkW((external/L"linked-file").c_str(),(saves/slot).c_str(),nullptr)!=0,"creates own synthetic hard-link fixture");
        // Hard links are ordinary files and are copied as independent bytes.
        check(fs::hard_link_count(saves/slot)==2,"source fixture has the expected link count");
        const auto final=s::import_saved_game(game,saves);
        check(fs::hard_link_count(fs::path(L"\\\\?\\"+fs::absolute(final.destination/slot).wstring()))==1,"import creates independent files rather than sharing source writes");
        std::cout<<checks<<" saved-game import checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
